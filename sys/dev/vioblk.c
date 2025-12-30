/*! @file vioblk.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‍‌​​‌​⁠⁠‌​⁠⁠‌
    @brief VirtIO block device
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include "devimpl.h"
#ifdef VIOBLK_TRACE
#define TRACE
#endif

#ifdef VIOBLK_DEBUG
#define DEBUG
#endif

#include <limits.h>

#include "conf.h"
#include "console.h"
#include "device.h"
#include "error.h"
#include "heap.h"
#include "intr.h"
#include "misc.h"
#include "string.h"
#include "thread.h"
#include "uio.h"  // FCNTL
#include "virtio.h"

// COMPILE-TIME PARAMETERS
//

#ifndef VIOBLK_INTR_PRIO
#define VIOBLK_INTR_PRIO 1
#endif

#ifndef VIOBLK_NAME
#define VIOBLK_NAME "vioblk"
#endif

// INTERNAL CONSTANT DEFINITIONS
//

// VirtIO block device feature bits (number, *not* mask)

#define VIRTIO_BLK_F_SIZE_MAX 1
#define VIRTIO_BLK_F_SEG_MAX 2
#define VIRTIO_BLK_F_GEOMETRY 4
#define VIRTIO_BLK_F_RO 5
#define VIRTIO_BLK_F_BLK_SIZE 6
#define VIRTIO_BLK_F_FLUSH 9
#define VIRTIO_BLK_F_TOPOLOGY 10
#define VIRTIO_BLK_F_CONFIG_WCE 11
#define VIRTIO_BLK_F_MQ 12
#define VIRTIO_BLK_F_DISCARD 13
#define VIRTIO_BLK_F_WRITE_ZEROES 14

//added constants for the type descriptor
#define VIRTIO_BLK_T_IN           0 
#define VIRTIO_BLK_T_OUT          1 
#define VIRTIO_BLK_T_FLUSH        4 
#define VIRTIO_BLK_T_GET_ID       8 
#define VIRTIO_BLK_T_GET_LIFETIME 10 
#define VIRTIO_BLK_T_DISCARD      11 
#define VIRTIO_BLK_T_WRITE_ZEROES 13 
#define VIRTIO_BLK_T_SECURE_ERASE   14

//added constants for the status descriptor
#define VIRTIO_BLK_S_OK        0 
#define VIRTIO_BLK_S_IOERR     1 
#define VIRTIO_BLK_S_UNSUPP    2

//ADDED THE SERIAL STRUCT
struct vioblk_storage {
    struct storage base;
    volatile struct virtio_mmio_regs * regs;
    int irqno; 
    char opened;
    unsigned int blksz;

    //indirect descriptor table
    struct virtq_desc indirect;

    struct virtq_desc direct[5];

    union { 
        struct virtq_avail availq;
        char avilbuf[VIRTQ_AVAIL_SIZE(1)]; //change this size to???
    };
    union {
        struct virtq_used usedq; 
        char usedbuf[VIRTQ_USED_SIZE(1)]; //change this size to???
    };

    struct condition fetch_ready;
    struct condition store_ready;
    struct lock lock;
};

// INTERNAL FUNCTION DECLARATIONS
//

/**
 * @brief Sets the virtq avail and virtq used queues such that they are available for use. (Hint,
 * read virtio.h) Enables the interupt line for the virtio device and sets necessary flags in vioblk
 * device.
 * @param sto Storage IO struct for the storage device
 * @return Return 0 on success or negative error code if error. If the given sto is already opened,
 * then return -EBUSY.
 */
static int vioblk_storage_open(struct storage* sto);

/**
 * @brief Resets the virtq avail and virtq used queues and sets necessary flags in vioblk device. If
 * the given sto is not opened, this function does nothing.
 * @param sto Storage IO struct for the storage device
 * @return None
 */
static void vioblk_storage_close(struct storage* sto);

/**
 * @brief Reads bytecnt number of bytes from the disk and writes them to buf. Achieves this by
 * repeatedly setting the appropriate registers to request a block from the disk, waiting until the
 * data has been populated in block buffer cache, and then writes that data out to buf. Thread
 * sleeps while waiting for the disk to service the request.
 * @param sto Storage IO struct for the storage device
 * @param pos The starting position for the read within the VirtIO device
 * @param buf A pointer to the buffer to fill with the read data
 * @param bytecnt The number of bytes to read from the VirtIO device into the buffer
 * @return The number of bytes read from the device, or negative error code if error
 */
static long vioblk_storage_fetch(struct storage* sto, unsigned long long pos, void* buf,
                                 unsigned long bytecnt);

/**
 * @brief Writes bytecnt number of bytes from the parameter buf to the disk. The size of the virtio
 * device should not change. You should only overwrite existing data. Write should also not create
 * any new files. Achieves this by filling up the block buffer cache and then setting the
 * appropriate registers to request the disk write the contents of the cache to the specified block
 * location. Thread sleeps while waiting for the disk to service the request.
 * @param sto Storage IO struct for the storage device
 * @param pos The starting position for the write within the VirtIO device
 * @param buf A pointer to the buffer with the data to write
 * @param bytecnt The number of bytes to write to the VirtIO device from the buffer
 * @return The number of bytes written to the device, or negative error code if error
 */
static long vioblk_storage_store(struct storage* sto, unsigned long long pos, const void* buf,
                                 unsigned long bytecnt);

/**
 * @brief Given a file io object, a specific command, and possibly some arguments, execute the
 * corresponding functions on the VirtIO block device.
 * @details Any commands such as FCNTL_GETEND should pass back through the arg variable. Do not
 * directly return the value.
 * @details FCNTL_GETEND should return the capacity of the VirtIO block device in bytes.
 * @param sto Storage IO struct for the storage device
 * @param op Operation to execute. vioblk should support FCNTL_GETEND.
 * @param arg Argument specific to the operation being performed
 * @return Status code on the operation performed
 */
static int vioblk_storage_cntl(struct storage* sto, int op, void* arg);

/**
 * @brief The interrupt handler for the VirtIO device. When an interrupt occurs, the system will
 * call this function.
 * @param irqno The interrupt request number for the VirtIO device
 * @param aux A generic pointer for auxiliary data.
 * @return None
 */
static void vioblk_isr(int irqno, void* aux);

// EXPORTED FUNCTION DEFINITIONS
//
//connecting the storage intf functions
static const struct storage_intf vioblk_storage_intf = {
    .blksz = 512, //blk size??
    .open = &vioblk_storage_open, //fix all of these??
    .close = &vioblk_storage_close,
    .fetch = &vioblk_storage_fetch,
    .store = &vioblk_storage_store,
    .cntl = &vioblk_storage_cntl
};

// Attaches a VirtIO block device. Declared and called directly from virtio.c.
/**
 * @brief Initializes virtio block device with the necessary IO operation functions and sets the
 * required feature bits.
 * @param regs Memory mapped register of Virtio
 * @param irqno Interrupt request number of the device
 * @return None
 */
void vioblk_attach(volatile struct virtio_mmio_regs* regs, int irqno) {
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct vioblk_storage* vbd;
    unsigned int blksz;
    int result;

    trace("%s(regs=%p,irqno=%d)", __func__, regs, irqno);

    assert(regs->device_id == VIRTIO_ID_BLOCK);

    // Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    __sync_synchronize();  // fence o,io

    // Negotiate features. We need:
    //  - VIRTIO_F_RING_RESET and
    //  - VIRTIO_F_INDIRECT_DESC
    // We want:
    //  - VIRTIO_BLK_F_BLK_SIZE and
    //  - VIRTIO_BLK_F_TOPOLOGY.

    virtio_featset_init(needed_features);
    virtio_featset_add(needed_features, VIRTIO_F_RING_RESET);
    virtio_featset_add(needed_features, VIRTIO_F_INDIRECT_DESC);
    virtio_featset_init(wanted_features);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_BLK_SIZE);
    virtio_featset_add(wanted_features, VIRTIO_BLK_F_TOPOLOGY);
    result = virtio_negotiate_features(regs, enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // If the device provides a block size, use it. Otherwise, use 512.

    if (virtio_featset_test(enabled_features, VIRTIO_BLK_F_BLK_SIZE))
        blksz = regs->config.blk.blk_size;
    else
        blksz = 512;

    // blksz must be a power of two
    assert(((blksz - 1) & blksz) == 0);

    // FIXME
    //setup struct members
    vbd = kcalloc(1, sizeof(struct vioblk_storage));
    vbd->regs = regs;
    vbd->irqno = irqno;
    vbd->opened = 0;
    vbd->blksz = blksz;
    vbd->availq.idx = 0;
    vbd->usedq.idx = 0;
    condition_init(&vbd->fetch_ready, "vbd.fetch_ready");
    condition_init(&vbd->store_ready, "vbd.store_ready");
    lock_init(&vbd->lock);

    //set up indirect descriptor
    vbd->indirect.addr = (uint64_t)vbd->direct;
    vbd->indirect.len = sizeof(struct virtq_desc) * 5;
    vbd->indirect.flags = VIRTQ_DESC_F_INDIRECT;
    vbd->indirect.next = 0;

    //setup descriptor table 
    //SET THESE ALL UP IN THE READ/WRITE FUNCTIONS
    vbd->direct[0].addr = (uint64_t)kcalloc(1, 4); //le32 type
    vbd->direct[0].len = 4;
    vbd->direct[0].flags = VIRTQ_DESC_F_NEXT;
    vbd->direct[0].next = 1;

    vbd->direct[1].addr = (uint64_t)kcalloc(1, 4); //le32 reserved
    vbd->direct[1].len = 4;
    vbd->direct[1].flags = VIRTQ_DESC_F_NEXT;
    vbd->direct[1].next = 2;

    vbd->direct[2].addr = (uint64_t)kcalloc(1, 8); //le64 sector
    vbd->direct[2].len = 8;
    vbd->direct[2].flags = VIRTQ_DESC_F_NEXT;
    vbd->direct[2].next = 3;

    vbd->direct[3].addr = (uint64_t)kcalloc(1, 512); //u8 data[] - 512 byte chunks
    vbd->direct[3].len = 512;
    vbd->direct[3].flags = VIRTQ_DESC_F_NEXT;
    vbd->direct[3].next = 4;

    vbd->direct[4].addr = (uint64_t)kcalloc(1, 1); //u8 status
    vbd->direct[4].len = 1;
    vbd->direct[4].flags = 0;
    vbd->direct[4].next = 0; //null or 0???

    //attach queues                                              //ask about this
    virtio_attach_virtq(vbd->regs, 0, 1, (uint64_t)(&vbd->indirect), (uint64_t)(&vbd->usedq), (uint64_t)(&vbd->availq));
    
    regs->status |= VIRTIO_STAT_DRIVER_OK; //set the driver to OK

    __sync_synchronize();

    //initialize device and register device
    storage_init(&vbd->base, &vioblk_storage_intf, vbd->regs->config.blk.capacity * 512);
    register_device(VIOBLK_NAME, DEV_STORAGE, vbd);
}

static int vioblk_storage_open(struct storage* sto) {
    // FIXME
    if (sto == NULL) {
        return -EINVAL;
    }

    struct vioblk_storage * const vbd = 
        (void*)sto - offsetof(struct vioblk_storage, base);

    trace("%s()", __func__);

    if (vbd->opened) {
        return -EBUSY;
    }

    __sync_synchronize();

    virtio_enable_virtq(vbd->regs, 0); 
    enable_intr_source(vbd->irqno, VIOBLK_INTR_PRIO, &vioblk_isr, vbd);
    vbd->opened = 1;

    __sync_synchronize();

    return 0;
}

/*
Resets the virtq avail and virtq used queues and sets necessary flags in vioblk device. 
If the given sto is not opened, this function does nothing.

Parameters
sto	Storage IO struct for the storage device
Returns
None
*/
static void vioblk_storage_close(struct storage* sto) {
    // FIXME
    if (sto == NULL) {
        return;
    }

    struct vioblk_storage * const vbd = 
        (void*)sto - offsetof(struct vioblk_storage, base);

    trace("%s()", __func__);

    if (!(vbd->opened))
        return;

    __sync_synchronize();

    disable_intr_source(vbd->irqno);
    virtio_reset_virtq(vbd->regs, 0);
    vbd->opened = 0;

    __sync_synchronize();
}

/*
Reads bytecnt number of bytes from the disk and writes them to buf. Achieves this by repeatedly setting the 
appropriate registers to request a block from the disk, waiting until the data has been populated in block buffer cache, 
and then writes that data out to buf. Thread sleeps while waiting for the disk to service the request.

Parameters
sto	Storage IO struct for the storage device
pos	The starting position for the read within the VirtIO device
buf	A pointer to the buffer to fill with the read data
bytecnt	The number of bytes to read from the VirtIO device into the buffer
Returns
The number of bytes read from the device, or negative error code if error
*/
static long vioblk_storage_fetch(struct storage* sto, unsigned long long pos, void* buf,
                                 unsigned long bytecnt) {
    // FIXME
    struct vioblk_storage * const vbd = 
        (void*)sto - offsetof(struct vioblk_storage, base);
    
    if (vbd->opened == 0 || buf == NULL || pos%512 != 0 || sto == NULL || pos < 0 || pos > sto->capacity) {
        return -EINVAL;
    }

    unsigned long long bytesToBeRead = (bytecnt/vbd->blksz) * vbd->blksz;       // round down to nearest bl
    if(pos+bytesToBeRead > sto->capacity){                  // truncate if limit total disc space surpassed
        bytesToBeRead = sto->capacity-pos;
        //round again to make sure
        bytesToBeRead = (bytesToBeRead/vbd->blksz) * vbd->blksz;
    }

    if(bytesToBeRead == 0){
        return 0;
    }

    unsigned long bytesRead = 0;

    uint32_t *type_buf = (uint32_t *)vbd->direct[0].addr;
    //uint32_t *reserved_buf = (uint32_t *)vbd->direct[1].addr;
    uint64_t *sector_buf = (uint64_t *)vbd->direct[2].addr;
    uint8_t *data_buf = (uint8_t *)vbd->direct[3].addr;
    uint8_t *status_buf = (uint8_t *)vbd->direct[4].addr;

    lock_acquire(&vbd->lock);

    while(bytesRead < bytesToBeRead){
        unsigned long long sec = (pos+bytesRead)/512;
        *sector_buf = sec;
        *type_buf = VIRTIO_BLK_T_IN;
        vbd->direct[3].flags |= VIRTQ_DESC_F_WRITE;
        vbd->direct[4].flags |= VIRTQ_DESC_F_WRITE;
        __sync_synchronize();

        vbd->availq.ring[0] = 0;
        vbd->availq.idx++;
        virtio_notify_avail(vbd->regs, 0);
        __sync_synchronize();
        
        int pie = disable_interrupts();
        while(vbd->usedq.idx != vbd->availq.idx){
            condition_wait(&vbd->fetch_ready);
        }
        restore_interrupts(pie);

        memcpy(buf+bytesRead, data_buf, 512);
        if(*status_buf != VIRTIO_BLK_S_OK){
            lock_release(&vbd->lock);
            return -EIO;
        }
        bytesRead += 512;
    }
    lock_release(&vbd->lock);
    return bytesRead;
}


/*
Writes bytecnt number of bytes from the parameter buf to the disk. The size of the virtio device should not change. 
You should only overwrite existing data. Write should also not create any new files. 
Achieves this by filling up the block buffer cache and then setting the appropriate registers to request 
the disk write the contents of the cache to the specified block location. Thread sleeps while waiting for the disk to service the request.

Parameters
sto	Storage IO struct for the storage device
pos	The starting position for the write within the VirtIO device
buf	A pointer to the buffer with the data to write
bytecnt	The number of bytes to write to the VirtIO device from the buffer
Returns
The number of bytes written to the device, or negative error code if error
*/
static long vioblk_storage_store(struct storage* sto, unsigned long long pos, const void* buf,
                                 unsigned long bytecnt) {
    // FIXME
    struct vioblk_storage * const vbd = 
        (void*)sto - offsetof(struct vioblk_storage, base);

    trace("%s()", __func__);

    //if it isnt opened then return and do nothing, or if buffer size isnt valid or then do nothing
    if (vbd->opened == 0 || buf == NULL || pos%512 != 0 || sto == NULL || pos < 0 || pos > sto->capacity) {
        return -EINVAL;
    }

    //round down to make it a multiple of blksz
    unsigned long bytesToBeWritten = (bytecnt/vbd->blksz) * vbd->blksz;

    //If we exceed capacity, then we just truncate the number of bytes to be written.
    if (pos + bytesToBeWritten > sto->capacity) {
        bytesToBeWritten = sto->capacity - pos;
        //round again to make sure
        bytesToBeWritten = (bytesToBeWritten/vbd->blksz) * vbd->blksz;
    }

    //if we have nothing to write just return then
    if (bytesToBeWritten == 0) {
        return 0;
    }
    
    unsigned long byteswritten = 0;
    uint32_t *type_buf = (uint32_t *)vbd->direct[0].addr;
    //uint32_t *reserved_buf = (uint32_t *)vbd->direct[1].addr;
    uint64_t *sector_buf = (uint64_t *)vbd->direct[2].addr;
    uint8_t *data_buf = (uint8_t *)vbd->direct[3].addr;
    uint8_t *status_buf = (uint8_t *)vbd->direct[4].addr;
    
    lock_acquire(&vbd->lock);
    
    while (byteswritten < bytesToBeWritten) {
        unsigned long long sec = (pos+byteswritten)/512; //which sector we are in
        *sector_buf = sec;

        //copy from inputed buffer TO the devices data buffer
        memcpy(data_buf, buf + byteswritten, 512);

        //setting up the write
        *type_buf = VIRTIO_BLK_T_OUT; //specify write
        vbd->direct[3].flags &= ~VIRTQ_DESC_F_WRITE;
        vbd->direct[4].flags = VIRTQ_DESC_F_WRITE; //tell device that it can write to status

        __sync_synchronize();
        vbd->availq.ring[0] = 0;
        vbd->availq.idx++;
        virtio_notify_avail(vbd->regs, 0);
        __sync_synchronize();
        int pie = disable_interrupts();
        while (vbd->availq.idx != vbd->usedq.idx) {
            condition_wait(&vbd->store_ready);
        }
        restore_interrupts(pie);

        __sync_synchronize();

        if (*status_buf != VIRTIO_BLK_S_OK) { //STATUS SHOWS NO OK
            lock_release(&vbd->lock);
            return -EIO;
        }   

        __sync_synchronize();

        byteswritten += 512;
    }
    lock_release(&vbd->lock);
    return byteswritten;
}


static int vioblk_storage_cntl(struct storage* sto, int op, void* arg) {
    // FIXME
    if (!sto) return -EINVAL;
    struct vioblk_storage * const vbd = 
        (void*)sto - offsetof(struct vioblk_storage, base);
    if(arg == NULL){
        return -EINVAL;
    }
    switch (op) {
        case FCNTL_GETEND:
            __sync_synchronize();
            *((unsigned long long *)arg) = (unsigned long long)(vbd->regs->config.blk.capacity * 512);
            __sync_synchronize();
            return 0;
        default:
            return -ENOTSUP;
    }
}


static void vioblk_isr(int irqno, void* aux) {
    // FIXME
    if (aux == NULL) {
        return;
    }

    struct vioblk_storage * const vbd = (struct vioblk_storage*)aux;

    if (irqno != vbd->irqno) {
        return;
    }
    
    __sync_synchronize();
    vbd->regs->interrupt_ack = vbd->regs->interrupt_status;
    __sync_synchronize();
    condition_broadcast(&vbd->fetch_ready);
    condition_broadcast(&vbd->store_ready);
}
