// viorng.c - VirtIO rng device
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "virtio.h"
#include "intr.h"
#include "heap.h"
#include "error.h"
#include "string.h"
#include "thread.h"
#include "devimpl.h"
#include "misc.h"
#include "conf.h"
#include "intr.h"
#include "console.h"

// INTERNAL CONSTANT DEFINITIONS
//

#ifndef VIORNG_BUFSZ
#define VIORNG_BUFSZ 256
#endif

#ifndef VIORNG_NAME
#define VIORNG_NAME "viorng"
#endif

#ifndef VIORNG_IRQ_PRIO
#define VIORNG_IRQ_PRIO 1
#endif

// INTERNAL TYPE DEFINITIONS
//


//Viorng Device
/*Define the necessary items required to implement the VirtIO entropy device, including avail, used virtqueues,
and descriptors. Refer to the UART serial structure for other possibly important fields.*/
struct viorng_serial {
    // FIXME your code goes here
    struct serial base;

    volatile struct virtio_mmio_regs * regs;

    int irqno; 
    char opened; 

    struct virtq_desc descq;
    union { 
        struct virtq_avail availq;
        char avilbuf[VIRTQ_AVAIL_SIZE(1)]; //adds 1
    };


    union {
        struct virtq_used usedq; 
        char usedbuf[VIRTQ_USED_SIZE(1)];
    };

    // int qflag; 

    char buffer[VIORNG_BUFSZ];

    struct condition ready;
    struct lock lock;
};

// INTERNAL FUNCTION DECLARATIONS
//


static int viorng_serial_open(struct serial * ser);
static void viorng_serial_close(struct serial * ser);
static int viorng_serial_recv(struct serial * ser, void * buf, unsigned int bufsz);
static void viorng_isr(int irqno, void * aux);

// INTERNAL GLOBAL VARIABLES
//

static const struct serial_intf viorng_serial_intf = {
    .blksz = 1,
    .open = &viorng_serial_open,
    .close = &viorng_serial_close,
    .recv = &viorng_serial_recv
};

// EXPORTED FUNCTION DEFINITIONS
//

// Attaches a VirtIO rng device. Declared and called directly from virtio.c.

/*This function initializes the VirtIO Entropy device with the necessary IO operation functions and sets the required feature bits (if any). 
Also fills out the descriptors in the virtqueue struct. It attaches the virtq avail
and virtq used structs using the virtio attach virtq function. Next, the serial struct is initialized with
the correct interface. Finally, the device is registered.*/
void viorng_attach(volatile struct virtio_mmio_regs * regs, int irqno) {
    virtio_featset_t enabled_features, wanted_features, needed_features;
    struct viorng_serial * vrng;
    int result;
    
    assert (regs->device_id == VIRTIO_ID_RNG);

    // Signal device that we found a driver

    regs->status |= VIRTIO_STAT_DRIVER;
    // fence o,io
    __sync_synchronize();

    virtio_featset_init(needed_features);
    virtio_featset_init(wanted_features);
    result = virtio_negotiate_features(regs, enabled_features, wanted_features, needed_features);

    if (result != 0) {
        kprintf("%p: virtio feature negotiation failed\n", regs);
        return;
    }

    // Allocate and initialize device struct
    // FIXME your code goes here 
    vrng = kcalloc(1, sizeof(struct viorng_serial));
    vrng->regs = regs; //attach the registers and inqno
    vrng->irqno = irqno;
    vrng->opened = 0; 
    //intialize the queue indexes, maybe flags too?
    vrng->availq.idx = 0;
    vrng->usedq.idx = 0; 

    //intialize condition variable
    condition_init(&vrng->ready, "vrg.ready");
    lock_init(&vrng->lock);

    //set the device ID?
    // vrng->regs->device_id = 4; 
    
    //attatch queues
    //1 because this device only has 1 descriptor
    virtio_attach_virtq(regs, 0, 1, (uint64_t)(&vrng->descq), (uint64_t)(&vrng->usedq), (uint64_t)(&vrng->availq));
    

    regs->status |= VIRTIO_STAT_DRIVER_OK; //set the driver to OK
    // fence o,oi
    __sync_synchronize();

    // FIXME your code goes here
    //initialize interface then register
    serial_init(&vrng->base, &viorng_serial_intf);
    register_device(VIORNG_NAME, DEV_SERIAL, vrng);
}

/*
static int viorng_serial_open(struct serial * ser);
Inputs: struct serial * ser
Outputs: int, 0 on success
Description: Opens the Viorng Device for use
Side Effects: Makes the used/avail queue availble for use, marks as opened
and also enables interrupts on this device
*/
int viorng_serial_open(struct serial * ser) {
    // FIXME your code goes here
    struct viorng_serial * const vrng =  
        (void*)ser - offsetof(struct viorng_serial, base);

    trace("%s()", __func__);

    if (vrng->opened)
        return -EBUSY;


    __sync_synchronize();

    //makes the avail/used queues available
    virtio_enable_virtq(vrng->regs, 0);
    enable_intr_source(vrng->irqno, VIORNG_IRQ_PRIO, &viorng_isr, vrng); //enables interrupt source for device
    vrng->opened = 0x01; //mark as opened

    __sync_synchronize();
    return 0;
}

/*
static int viorng_serial_open(struct serial * ser);
Inputs: struct serial * ser
Outputs: int, 0 on success
Description: Opens the Viorng Device for use
Side Effects: Makes the used/avail queue availble for use, marks as opened
and also enables interrupts on this device
*/
void viorng_serial_close(struct serial * ser) {
    // FIXME your code goes here
    struct viorng_serial * const vrng =  
        (void*)ser - offsetof(struct viorng_serial, base);

    trace("%s()", __func__);

    if (!(vrng->opened))
        return;

    __sync_synchronize();
    
    
    disable_intr_source(vrng->irqno); //disables interrupt source for device
    virtio_reset_virtq(vrng->regs, 0); //Resets the queues
    vrng->opened = 0x00; //mark as closed
    

    // kprintf("viorng closed\n");

    __sync_synchronize();
}

/*
static int viorng_serial_recv(struct serial * ser, void * buf, unsigned int bufsz);
Inputs: struct serial * ser, 
        void * buf, 
        unsigned int bufsz
Outputs: int, number of bytes writted to inputted buffer
Description: Reads bytes from viorng device, and writes to inputted buffer
Side Effects: Sets up the descriptors
*/
int viorng_serial_recv(struct serial * ser, void * buf, unsigned int bufsz) {
    // FIXME your code goes here
    if (ser == NULL || buf == NULL) {
        return -EINVAL;
    }

    struct viorng_serial * const vrng =  
        (void*)ser - offsetof(struct viorng_serial, base);

    trace("%s()", __func__);

    //if it isnt opened then return and do nothing, or if buffer size isnt valid or then do nothign
    if (vrng->opened == 0) {
        return -EINVAL;
    }

    if (bufsz == 0) {
        return 0;
    }

    //gotta cap the bufsize so it doesnt go over 256
    if (bufsz > VIORNG_BUFSZ) {
        bufsz = VIORNG_BUFSZ;
    }

    __sync_synchronize();

    lock_acquire(&vrng->lock);
    
    //set the flag so it writes to buffer
    //setup descriptors

    vrng->descq.addr = (uint64_t)(vrng->buffer);
    vrng->descq.len = bufsz;
    vrng->descq.next = 0;
    vrng->descq.flags |= VIRTQ_DESC_F_WRITE;

    /*set the appropriate registers to request entropy from the device, waiting until the randomness has been
    placed into a buffer*/
    
    vrng->availq.ring[0] = 0;
    vrng->availq.idx++;
    virtio_notify_avail(vrng->regs, 0);

    __sync_synchronize();

    int pie = disable_interrupts();
    while (vrng->availq.idx != vrng->usedq.idx) {
        // kprintf("in vior rcv wait loop\n");
        //continue;
        condition_wait(&vrng->ready);
    }
    restore_interrupts(pie);

    // kprintf("recieved something\n");
    __sync_synchronize();
    //memcpy
    memcpy(buf, vrng->buffer, bufsz); //nothings getting written to the buffer

    __sync_synchronize();

    lock_release(&vrng->lock);

    return bufsz;
}

/*
static int viorng_serial_recv(struct serial * ser, void * buf, unsigned int bufsz);
Inputs: struct serial * ser, 
        void * buf, 
        unsigned int bufsz
Outputs: int, number of bytes writted to inputted buffer
Description: Reads bytes from viorng device, and writes to inputted buffer
Side Effects: Sets up the descriptors
*/
void viorng_isr(int irqno, void * aux) {
    // FIXME your code goes here
    if (aux == NULL) {
        return;
    }

    struct viorng_serial * const vrng = (struct viorng_serial *)aux;

    if (irqno != vrng->irqno) {
        return;
    }

    __sync_synchronize();

    if (vrng->regs->interrupt_status == 1)
    {
        vrng->regs->interrupt_ack = vrng->regs->interrupt_status;
        //broadcast
        condition_broadcast(&vrng->ready);
    }
    
    __sync_synchronize();
}