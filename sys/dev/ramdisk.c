/*! @file ramdisk.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‍‌​​‌​⁠⁠‌​⁠⁠‌
    @brief Memory-backed storage implementation
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef RAMDISK_DEBUG
#define DEBUG
#endif

#ifdef RAMDISK_TRACE
#define TRACE
#endif

#include <stddef.h>

#include "console.h"
#include "devimpl.h"
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "uio.h"

#ifndef RAMDISK_NAME
#define RAMDISK_NAME "ramdisk"
#endif

// INTERNAL TYPE DEFINITIONS
//

/**
 * @brief Storage device backed by a block of memory. Allows modification of the backing memory
 * block.
 */
struct ramdisk {
    struct storage storage;  ///< Storage struct of memory storage
    void *buf;               ///< Block of memory
    size_t size;             ///< Size of memory block
    char opened;
};

// INTERNAL FUNCTION DECLARATIONS
//

static int ramdisk_open(struct storage *sto);
static void ramdisk_close(struct storage *sto);
static long ramdisk_fetch(struct storage *sto, unsigned long long pos, void *buf,
                          unsigned long bytecnt);
static int ramdisk_cntl(struct storage *sto, int cmd, void *arg);

// INTERNAL GLOBAL CONSTANTS
//

static const struct storage_intf ramdisk_intf = {
    .blksz = 1,
    .open = &ramdisk_open,
    .close = &ramdisk_close,
    .fetch = &ramdisk_fetch,
    .store = NULL,  // Read-only storage (blob data in .rodata)
    .cntl = &ramdisk_cntl};

// EXPORTED FUNCTION DEFINITIONS
//

/**
 * @brief Creates and registers a memory-backed storage device
 * @return None
 */
void ramdisk_attach() {
    // External symbols from linker script for embedded blob data
    extern char _kimg_blob_start[], _kimg_blob_end[];
        
    // FIXME, just follow vioblk format
    struct ramdisk* ramdisk;
    size_t size;

    //allocate memory
    ramdisk = kcalloc(1, sizeof(struct ramdisk));
    ramdisk->buf = _kimg_blob_start;
    size = _kimg_blob_end-_kimg_blob_start;
    ramdisk->size = size;
    ramdisk->opened = 0;

    //initilize then register
    storage_init(&ramdisk->storage, &ramdisk_intf, ramdisk->size);
    register_device(RAMDISK_NAME, DEV_STORAGE, ramdisk);
}

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * @brief Opens the _ramdisk_ device.
 * @param sto Storage struct pointer for memory storage
 * @return 0 on success
 */
static int ramdisk_open(struct storage *sto) {
    // FIXME
    if (sto == NULL) {
        return -EINVAL;
    }

    struct ramdisk * const ramdisk = 
        (void*)sto - offsetof(struct ramdisk, storage);

    trace("%s()", __func__);

    if (ramdisk->opened) {
        return -EBUSY;
    }

    ramdisk->opened = 1;

    return 0;
}

/**
 * @brief Closes the _ramdisk_ device.
 * @param sto Storage struct pointer for memory storage
 */
static void ramdisk_close(struct storage *sto) {
    // FIXME
    if (sto == NULL) {
        return;
    }

    struct ramdisk * const ramdisk = 
        (void*)sto - offsetof(struct ramdisk, storage);

    trace("%s()", __func__);

    if (!ramdisk->opened) {
        return;
    }

    ramdisk->opened = 0;
}

/**
 * @brief Reads bytecnt number of bytes from the disk and writes them to buf.
 * @details Performs proper bounds checks, then copies data from memory block to passed buffer
 * @param sto Storage struct pointer for memory storage
 * @param pos Position in storage to read from
 * @param buf Buffer to copy data from memory to
 * @param bytecnt Number of bytes to read from memory
 * @return Number of bytes successfully read
 */
static long ramdisk_fetch(struct storage *sto, unsigned long long pos, void *buf,
                          unsigned long bytecnt) {
    // FIXME
    if (sto == NULL || buf == NULL) {
        return -EINVAL;
    }

    trace("%s()", __func__);

    struct ramdisk * const ramdisk = 
        (void*)sto - offsetof(struct ramdisk, storage);
    
    if (pos >= ramdisk->size || bytecnt == 0) {
        return 0; //no bytes to be read
    }

    //calculate how many bytes to be read
    unsigned long bytesToBeRead = bytecnt;
    if (pos + bytesToBeRead > ramdisk->size) {
        bytesToBeRead = ramdisk->size - pos;
    }

    //reads directly from memory, and we can do this because it reads 
    //singular bytes at a time cuz blksz is 1. 
    memcpy(buf, ramdisk->buf + pos, bytesToBeRead);

    return bytesToBeRead;
}

/**
 * @brief _cntl_ functions for memory storage.
 * @details Memory storage supports basic control operations
 * @details Any commands such as FCNTL_GETEND should pass back through the arg variable. Do not
 * directly return the value.
 * @details FCNTL_GETEND should return the capacity of the VirtIO block device in bytes.
 * @param sto Storage struct pointer for memory storage
 * @param cmd command to execute. ramdisk should support FCNTL_GETEND.
 * @param arg Argument for commands
 * @return 0 on success, error on failure or unsupported command
 */
static int ramdisk_cntl(struct storage *sto, int cmd, void *arg) {
    // FIXME
    if (sto == NULL) {
        return -EINVAL;
    }

    struct ramdisk * const ramdisk = 
        (void*)sto - offsetof(struct ramdisk, storage);

    switch (cmd) {
        case FCNTL_GETEND:
            if (!arg) return -EINVAL;

            *((unsigned long long *)arg) = ramdisk->size;

            return 0;    // return 0 
            
        default:
            return -ENOTSUP;      // cannot be anything else
    }
}
