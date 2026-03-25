/*! @file ktfs.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‍‌​​‌​⁠⁠‌​⁠⁠‌
    @brief KTFS Implementation.
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include <stdint.h>
#include <sys/_intsup.h>
#include <sys/types.h>
#ifdef KTFS_TRACE
#define TRACE
#endif

#ifdef KTFS_DEBUG
#define DEBUG
#endif

#include "ktfs.h"

#include "cache.h"
#include "console.h"
#include "device.h"
#include "devimpl.h"
#include "error.h"
#include "filesys.h"
#include "fsimpl.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "thread.h"
#include "uio.h"
#include "uioimpl.h"
#include "dev/virtio.h"

// INTERNAL TYPE DEFINITIONS
//

/// @brief File struct for a file in the Keegan Teal Filesystem
struct ktfs_file {
    // Fill to fulfill spec, 7.2 in the doc
    struct uio file_uio;
    struct ktfs_dir_entry dentry;
    unsigned long long file_size;
    unsigned long long pos;
    struct ktfs_file * next;
};

// INTERNAL FUNCTION DECLARATIONS
//

int ktfs_open(struct filesystem* fs, const char* name, struct uio** uioptr);
void ktfs_close(struct uio* uio);
int ktfs_cntl(struct uio* uio, int cmd, void* arg);
long ktfs_fetch(struct uio* uio, void* buf, unsigned long len);
long ktfs_store(struct uio* uio, const void* buf, unsigned long len);
int ktfs_create(struct filesystem* fs, const char* name);
int ktfs_delete(struct filesystem* fs, const char* name);
void ktfs_flush(struct filesystem* fs);

void ktfs_listing_close(struct uio* uio);
long ktfs_listing_read(struct uio* uio, void* buf, unsigned long bufsz);

//helper functions
static struct ktfs_superblock getSuperblock();
static struct ktfs_bitmap * getInodeBitmap(uint32_t inode_bitmap_block_index);
static struct ktfs_bitmap * getDataBitmap(uint32_t data_bitmap_block_index);
static struct ktfs_inode getInode(uint16_t inode_index);
static struct ktfs_data_block * getInodeBlock(uint16_t inode_index);
static struct ktfs_data_block * getDataBlock(uint32_t block_index);
static void releaseInodeBlock(struct ktfs_data_block * blockptr, int dirty);
static void releaseDataBlock(struct ktfs_data_block * blockptr, int dirty);
static void releaseBitmapBlock(struct ktfs_bitmap * blockptr, int dirty);
static int getDentryBlockWithIndex(struct ktfs_data_block ** dentry_block,
                                     const struct ktfs_inode root_inode,
                                     uint32_t dentry_index);
static int getDentryWithName(struct ktfs_dir_entry * dentry, const char * name);
static int getDentryBlockWithName(struct ktfs_data_block ** dentry_block_ptr, uint32_t * dentry_index_ptr, const char * name);
static int changeFileSize(struct ktfs_file * file, unsigned long long new_size);
static int allocateBlockFromInode(struct ktfs_inode * inode, uint32_t block_index);
static int freeBlockFromInode(struct ktfs_inode * inode, uint32_t block_index);
static uint32_t allocateDataBlock(void);
static uint16_t allocateInode(void);
static void freeDataBlock(uint32_t index);
static void freeInode(uint16_t index);

// INTERNAL GLOBAL VARIABLES
//

static const struct filesystem ktfs = {
    .open = &ktfs_open,
    .create = &ktfs_create,
    .delete = &ktfs_delete,
    .flush = &ktfs_flush
};

static const struct uio_intf ktfs_intf = {
    .close = &ktfs_close,
    .read = &ktfs_fetch,
    .write = &ktfs_store,
    .cntl = &ktfs_cntl
};

static const struct uio_intf ktfs_listing_intf = {
    .close = &ktfs_listing_close,
    .read = &ktfs_listing_read,
    .write = NULL,
    .cntl = NULL
};

//GLOBAL VARIABLES

static struct ktfs_file * openedFileList = NULL;
static struct ktfs_file * openedListingList = NULL;
static struct cache * ktfs_cache = NULL;
static struct ktfs_superblock ktfs_sb_cache;
static int ktfs_sb_cached = 0;

//HELPER FUNCTIONS
static struct ktfs_superblock getSuperblock(){
    if (ktfs_sb_cached) {
        return ktfs_sb_cache;
    }
    void * buffer;
    int rc = cache_get_block(ktfs_cache, 0, &buffer);
    if (rc < 0) {
        panic("ktfs: failed to read superblock from cache");
    }
    struct ktfs_superblock superblock;
    memcpy((void *)&superblock, buffer, sizeof(struct ktfs_superblock));
    cache_release_block(ktfs_cache, buffer, 0);
    ktfs_sb_cache = superblock;
    ktfs_sb_cached = 1;
    return superblock;
}

static struct ktfs_bitmap * getInodeBitmap(uint32_t inode_bitmap_block_index){
    uint32_t block_index = 1 + inode_bitmap_block_index;
    struct ktfs_bitmap * bitmap;
    int rc = cache_get_block(ktfs_cache, block_index*512, (void**)&bitmap);
    if (rc < 0) {
        panic("ktfs: failed to read inode bitmap from cache");
    }
    return bitmap;
}

static struct ktfs_bitmap * getDataBitmap(uint32_t data_bitmap_block_index){
    struct ktfs_superblock superblock = getSuperblock();
    uint32_t block_index = 1 + superblock.inode_bitmap_block_count + data_bitmap_block_index;
    struct ktfs_bitmap * bitmap;
    int rc = cache_get_block(ktfs_cache, block_index*512, (void**)&bitmap);
    if (rc < 0) {
        panic("ktfs: failed to read data bitmap from cache");
    }
    return bitmap;
}

// returns the inode struct with the inode number
static struct ktfs_inode getInode(uint16_t inode_index){
    struct ktfs_superblock superblock = getSuperblock();
    unsigned long long inode_block_index = 1 + superblock.inode_bitmap_block_count + superblock.bitmap_block_count + (inode_index/16); //each inode block can have 16 inodes in it, gets which inode block 
    unsigned long long inode_block_offset = inode_index%16; //offset within inode block
    void * buffer;
    int rc = cache_get_block(ktfs_cache, inode_block_index*512, &buffer);
    if (rc < 0) {
        panic("ktfs: failed to read inode block from cache");
    }
    struct ktfs_inode * inode_array = (struct ktfs_inode *)buffer;
    struct ktfs_inode inode = inode_array[inode_block_offset];
    cache_release_block(ktfs_cache, buffer, 0);
    return inode;
}

static struct ktfs_data_block * getInodeBlock(uint16_t inode_index){
    struct ktfs_superblock superblock = getSuperblock();
    unsigned long long inode_block_index = 1 + superblock.inode_bitmap_block_count + superblock.bitmap_block_count + (inode_index/16); //each inode block can have 16 inodes in it, gets which inode block 
    struct ktfs_data_block * inode_block;
    int rc = cache_get_block(ktfs_cache, inode_block_index*512, (void**)&inode_block);
    if (rc < 0) {
        panic("ktfs: failed to read inode data block from cache");
    }
    return inode_block;
}

static struct ktfs_data_block * getDataBlock(uint32_t block_index){
    struct ktfs_superblock superblock = getSuperblock();
    unsigned long long data_block_index = 1 + superblock.inode_bitmap_block_count + superblock.bitmap_block_count + superblock.inode_block_count + block_index;
    struct ktfs_data_block * data_block;
    int rc = cache_get_block(ktfs_cache, data_block_index*512, (void**)&data_block);
    if (rc < 0) {
        panic("ktfs: failed to read data block from cache");
    }
    return data_block;
}

static void releaseInodeBlock(struct ktfs_data_block * blockptr, int dirty){
    cache_release_block(ktfs_cache, blockptr, dirty);
}

static void releaseDataBlock(struct ktfs_data_block * blockptr, int dirty){
    cache_release_block(ktfs_cache, blockptr, dirty);
}

static void releaseBitmapBlock(struct ktfs_bitmap * blockptr, int dirty){
    cache_release_block(ktfs_cache, blockptr, dirty);
}

static int getDentryBlockWithIndex(struct ktfs_data_block ** dentry_block,
                                    const struct ktfs_inode root_inode,
                                    uint32_t dentry_index){
    uint32_t dentry_block_index = dentry_index/32;
    if(dentry_block_index < 4){   // 0-3  direct
        *dentry_block = getDataBlock(root_inode.block[dentry_block_index]);
        return 0;
    }
    else if(dentry_block_index < 132){   // 4-131  indirect
        struct ktfs_data_block * indirect_dentry_block = getDataBlock(root_inode.indirect);
        uint32_t * indirect_dentry_array = (uint32_t *)indirect_dentry_block->data;
        *dentry_block = getDataBlock(indirect_dentry_array[dentry_block_index-4]);
        releaseDataBlock(indirect_dentry_block, 0);
        return 0;
    }
    else if(dentry_block_index < 32900){  // 132-32899  dindirect
        struct ktfs_data_block * dindirect_dentry_block = getDataBlock(root_inode.dindirect[(dentry_block_index-132)/16384]);
        uint32_t * dindirect_dentry_array = (uint32_t *)dindirect_dentry_block->data;
        struct ktfs_data_block * indirect_dentry_block = getDataBlock(dindirect_dentry_array[((dentry_block_index-132)%16384)/128]);
        uint32_t * indirect_dentry_array = (uint32_t *)indirect_dentry_block->data;
        *dentry_block = getDataBlock(indirect_dentry_array[((dentry_block_index-132)%16384)%128]);
        releaseDataBlock(indirect_dentry_block, 0);
        releaseDataBlock(dindirect_dentry_block, 0);
        return 0;
    }
    return -ENOENT;
}

static int getDentryWithName(struct ktfs_dir_entry * dentry, const char * name){
    struct ktfs_superblock superblock = getSuperblock();
    struct ktfs_inode root_inode = getInode(superblock.root_directory_inode);
    uint32_t dentry_count = root_inode.size/sizeof(struct ktfs_dir_entry);
    struct ktfs_data_block * dentry_block;
    for(uint32_t dentry_index = 0; dentry_index < dentry_count; dentry_index++){
        int retVal = getDentryBlockWithIndex(&dentry_block, root_inode, dentry_index);
        struct ktfs_dir_entry * dentry_array = (struct ktfs_dir_entry *)dentry_block->data;
        if(retVal == 0){
            if(strcmp(name, dentry_array[dentry_index%32].name) == 0){
                *dentry = dentry_array[dentry_index%32];
                releaseDataBlock(dentry_block, 0);
                return 0;
            }
            else{
                releaseDataBlock(dentry_block, 0);
                continue;
            }
        }
        else{
            return retVal;
        }
    }
    return -ENOENT;
}

static int getDentryBlockWithName(struct ktfs_data_block ** dentry_block_ptr, uint32_t * dentry_index_ptr, const char * name){
    struct ktfs_superblock superblock = getSuperblock();
    struct ktfs_inode root_inode = getInode(superblock.root_directory_inode);
    uint32_t dentry_count = root_inode.size/sizeof(struct ktfs_dir_entry);
    struct ktfs_data_block * dentry_block;
    for(uint32_t dentry_index = 0; dentry_index < dentry_count; dentry_index++){
        int retVal = getDentryBlockWithIndex(&dentry_block, root_inode, dentry_index);
        struct ktfs_dir_entry * dentry_array = (struct ktfs_dir_entry *)dentry_block->data;
        if(retVal == 0){
            if(strcmp(name, dentry_array[dentry_index%32].name) == 0){
                *dentry_block_ptr = dentry_block;
                *dentry_index_ptr = dentry_index;
                return 0;
            }
            else{
                releaseDataBlock(dentry_block, 0);
                continue;
            }
        }
        else{
            return retVal;
        }
    }
    return -ENOENT;
}

static int changeFileSize(struct ktfs_file * file, unsigned long long new_size){
    new_size = MIN(new_size, 32900*512);
    struct ktfs_data_block * inode_block = getInodeBlock(file->dentry.inode);
    struct ktfs_inode * inode_array = (struct ktfs_inode *)inode_block->data;
    struct ktfs_inode * inode = &inode_array[file->dentry.inode%16];
    uint32_t start_block_index = ROUND_UP(file->file_size, 512)/512;
    uint32_t end_block_index = ROUND_UP(new_size, 512)/512;
    for(uint32_t curr_block_index = start_block_index; curr_block_index < end_block_index; curr_block_index++){
        int retVal = allocateBlockFromInode(inode, curr_block_index);
        if(retVal != 0){
            releaseInodeBlock(inode_block, 1);
            return retVal;
        }
    }
    inode->size = new_size;
    releaseInodeBlock(inode_block, 1);
    file->file_size = new_size;
    return 0;
}

static int allocateBlockFromInode(struct ktfs_inode * inode, uint32_t block_index){
    uint32_t data_block_index;
    if(block_index < 4){   // 0-3  direct
        data_block_index = allocateDataBlock();
        if(data_block_index < 0){
            return data_block_index;
        }
        inode->block[block_index] = data_block_index;
    }
    else if(block_index < 132){   // 4-131  indirect
        if(block_index-4 == 0){
            data_block_index = allocateDataBlock();
            if(data_block_index < 0){
                return data_block_index;
            }
            inode->indirect = data_block_index;
        }
        struct ktfs_data_block * indirect_data_block = getDataBlock(inode->indirect);
        uint32_t * indirect_data_array = (uint32_t *)indirect_data_block->data;
        data_block_index = allocateDataBlock();
        if(data_block_index < 0){
            releaseDataBlock(indirect_data_block, 1);
            return data_block_index;
        }
        indirect_data_array[block_index-4] = data_block_index;
        releaseDataBlock(indirect_data_block, 1);
    }
    else if(block_index < 32900){  // 132-32899  dindirect
        if((block_index-132)%16384 == 0){
            data_block_index = allocateDataBlock();
            if(data_block_index < 0){
                return data_block_index;
            }
            inode->dindirect[(block_index-132)/16384] = data_block_index;
        }
        struct ktfs_data_block * dindirect_dentry_block = getDataBlock(inode->dindirect[(block_index-132)/16384]);
        uint32_t * dindirect_dentry_array = (uint32_t *)dindirect_dentry_block->data;
        if(((block_index-132)%16384)%128 == 0){
            data_block_index = allocateDataBlock();
            if(data_block_index < 0){
                releaseDataBlock(dindirect_dentry_block, 1);
                return data_block_index;
            }
            dindirect_dentry_array[((block_index-132)%16384)/128] = data_block_index;
        }
        struct ktfs_data_block * indirect_dentry_block = getDataBlock(dindirect_dentry_array[((block_index-132)%16384)/128]);
        uint32_t * indirect_dentry_array = (uint32_t *)indirect_dentry_block->data;
        data_block_index = allocateDataBlock();
        if(data_block_index < 0){
            releaseDataBlock(dindirect_dentry_block, 1);
            releaseDataBlock(indirect_dentry_block, 1);
            return data_block_index;
        }
        indirect_dentry_array[((block_index-132)%16384)%128] = data_block_index;
        releaseDataBlock(dindirect_dentry_block, 1);
        releaseDataBlock(indirect_dentry_block, 1);
    }
    else{   // read out of bounds of total file
        return -EINVAL;
    }
    return 0;
}

static int freeBlockFromInode(struct ktfs_inode * inode, uint32_t block_index){
    if(block_index < 4){   // 0-3  direct
        freeDataBlock(inode->block[block_index]);
    }
    else if(block_index < 132){   // 4-131  indirect
        struct ktfs_data_block * indirect_data_block = getDataBlock(inode->indirect);
        uint32_t * indirect_data_array = (uint32_t *)indirect_data_block->data;
        freeDataBlock(indirect_data_array[block_index-4]);
        releaseDataBlock(indirect_data_block, 0);
        if(block_index-4 == 0){
            freeDataBlock(inode->indirect);
        }
    }
    else if(block_index < 32900){  // 132-32899  dindirect
        struct ktfs_data_block * dindirect_dentry_block = getDataBlock(inode->dindirect[(block_index-132)/16384]);
        uint32_t * dindirect_dentry_array = (uint32_t *)dindirect_dentry_block->data;
        struct ktfs_data_block * indirect_dentry_block = getDataBlock(dindirect_dentry_array[((block_index-132)%16384)/128]);
        uint32_t * indirect_dentry_array = (uint32_t *)indirect_dentry_block->data;
        freeDataBlock(indirect_dentry_array[((block_index-132)%16384)%128]);
        releaseDataBlock(indirect_dentry_block, 0);
        releaseDataBlock(dindirect_dentry_block, 0);
        if(((block_index-132)%16384)%128 == 0){
            freeDataBlock(dindirect_dentry_array[((block_index-132)%16384)/128]);
        }
        if((block_index-132)%16384 == 0){
            freeDataBlock(inode->dindirect[(block_index-132)/16384]);
        }
    }
    else{   // read out of bounds of total file
        return -EINVAL;
    }
    return 0;
}

static uint32_t allocateDataBlock(void){
    struct ktfs_superblock superblock = getSuperblock();
    uint32_t data_block = 0;
    // iterate through bitmap block bumbers
    for (int i=0; i<superblock.bitmap_block_count; i++) {
        struct ktfs_bitmap * datablock_bitmap = getDataBitmap(i);
        for (int j=0; j<512; j++) {     // iterate through bytes in bitmap block
            for (int k=0; k<8; k++) {   // iterate through bits in byte
                if ((datablock_bitmap->bytes[j] & (1<<k)) != 0) {
                    data_block++;
                }
                else {
                    datablock_bitmap->bytes[j] |= (1<<k);
                    releaseBitmapBlock(datablock_bitmap, 1);
                    return data_block;
                }
            }
        }
        releaseBitmapBlock(datablock_bitmap, 0);
    }
    return -EINVAL;
}

static uint16_t allocateInode(void){
    struct ktfs_superblock superblock = getSuperblock();
    int inode = 0;
    // iterate through bitmap block bumbers
    for (int i=0; i<superblock.inode_bitmap_block_count; i++) {
        struct ktfs_bitmap * inodeblock_bitmap = getInodeBitmap(i);
        for (int j=0; j<512; j++) {     // iterate through bytes in bitmap block
            for (int k=0; k<8; k++) {   // iterate through bits in byte
                if ((inodeblock_bitmap->bytes[j] & (1<<k)) != 0) {
                    inode++;
                }
                else {
                    inodeblock_bitmap->bytes[j] |= (1<<k);
                    releaseBitmapBlock(inodeblock_bitmap, 1);
                    return inode;
                }
            }
        }
        releaseBitmapBlock(inodeblock_bitmap, 0);
    }
    return -EINVAL;
}

static void freeDataBlock(uint32_t index){
    //gotta update the bitmap in here
    struct ktfs_bitmap * data_bitmap_block = getDataBitmap((index)/(512*8));
    data_bitmap_block->bytes[((index)%(512*8))/8] &= ~(1 << (index%8)); //set to zero
    releaseBitmapBlock(data_bitmap_block, 1);
}

static void freeInode(uint16_t index){
    //gotta update the bitmap in here
    struct ktfs_bitmap * data_bitmap_block = getInodeBitmap((index)/(512*8));
    data_bitmap_block->bytes[((index)%(512*8))/8] &= ~(1 << (index%8)); //set to zero
    releaseBitmapBlock(data_bitmap_block, 1);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief Mounts the file system with associated backing cache
 * @param cache Pointer to cache struct for the file system
 * @return 0 if mount successful, negative error code if error
 */
int mount_ktfs(const char* name, struct cache* cache) {
    // FIXME
    if (name == NULL || cache == NULL) {
        return -EINVAL; 
    }
    ktfs_cache = cache; //connect the cache NOT USED FOR NOW, JUST CALL VIOBLK FUNCTIONS RIGHT NOW
    ktfs_sb_cached = 0; // ensure fresh superblock read if remounted
    return attach_filesystem(name, (struct filesystem*)&ktfs);
}

/**
 * @brief Opens a file or ls (listing) with the given name and returns a pointer to the uio through
 * the double pointer
 * @param name The name of the file to open or "\" for listing (CP3)
 * @param uioptr Will return a pointer to a file or ls (list) uio pointer through this double
 * pointer
 * @return 0 if open successful, negative error code if error
 */
int ktfs_open(struct filesystem* fs, const char* name, struct uio** uioptr) {
    // FIXME
    //basic null checks
    if (fs == NULL || name == NULL || uioptr == NULL) {
        return -EINVAL;
    }
    for(struct ktfs_file * curr = openedFileList; curr != NULL; curr = curr->next){
        if(strcmp(curr->dentry.name, name) == 0){
            return -EINVAL;
        }
    }
    struct ktfs_file * file = kcalloc(1, sizeof(struct ktfs_file));
    if(strcmp(name, "") == 0){
        *uioptr = uio_init1(&file->file_uio, &ktfs_listing_intf);
        file->pos = 0;
        file->next = openedListingList;
        openedListingList = file;
    }
    else{
        *uioptr = uio_init1(&file->file_uio, &ktfs_intf);
        struct ktfs_dir_entry dentry;
        int retVal = getDentryWithName(&dentry, name);
        if(retVal != 0){
            return retVal;
        }
        file->dentry = dentry;
        struct ktfs_inode inode = getInode(file->dentry.inode);
        file->file_size = inode.size;
        file->pos = 0;
        file->next = openedFileList;
        openedFileList = file;
    }
    return 0;
}

/**
 * @brief Closes the file that is represented by the uio struct
 * @param uio The file io to be closed
 * @return None
 */
void ktfs_close(struct uio* uio) {
    // FIXME
    if(uio == NULL){
        return;
    }
    struct ktfs_file * const file = (void*)uio - offsetof(struct ktfs_file, file_uio);
    for(struct ktfs_file * prev = NULL, * curr = openedFileList; curr != NULL; prev = curr, curr = curr->next){
        if(curr == file){
            if(prev == NULL){
                openedFileList = curr->next;
            }
            else{
                prev->next = curr->next;
            }
            break;
        }
    }
    kfree(file);
}

/**
 * @brief Reads data from file attached to uio into provided argument buffer
 * @param uio uio of file to be read
 * @param buf Buffer to be filled
 * @param len number of bytes to read
 * @return number of bytes read if successful, negative error code if error
 */
long ktfs_fetch(struct uio* uio, void* buf, unsigned long len) {
    // FIXME
    if(uio == NULL || buf == NULL){
        return -EINVAL; //invalid arguments
    } 

    //bytes to be stores is 0 just return 0
    if(len == 0){
        return 0;
    }

    struct ktfs_file * const file = (void*)uio - offsetof(struct ktfs_file, file_uio);
    struct ktfs_file * curr = openedFileList;
    while(curr != NULL){
        if(curr == file){
            break;
        }
        curr = curr->next;
    }
    if(curr == NULL){
        return -EINVAL;
    }

    // if tries to read past end of file
    if(file->pos + len > file->file_size){
        len = file->file_size - file->pos;
    }

    struct ktfs_inode inode = getInode(file->dentry.inode);

    unsigned long long read = 0;
    while(read < len){
        uint32_t inode_data_index = (file->pos+read)/512;
        uint32_t data_block_index;
        if(inode_data_index < 4){   // 0-3  direct
            data_block_index = inode.block[inode_data_index];
        }
        else if(inode_data_index < 132){   // 4-131  indirect
            struct ktfs_data_block * indirect_data_block = getDataBlock(inode.indirect);
            uint32_t * indirect_data_array = (uint32_t *)indirect_data_block->data;
            data_block_index = indirect_data_array[inode_data_index-4];
            releaseDataBlock(indirect_data_block, 0);
        }
        else if(inode_data_index < 32900){  // 132-32899  dindirect
            struct ktfs_data_block * dindirect_data_block = getDataBlock(inode.dindirect[(inode_data_index-132)/16384]);
            uint32_t * dindirect_data_array = (uint32_t *)dindirect_data_block->data;
            struct ktfs_data_block * indirect_data_block = getDataBlock(dindirect_data_array[((inode_data_index-132)%16384)/128]);
            uint32_t * indirect_data_array = (uint32_t *)indirect_data_block->data;
            data_block_index = indirect_data_array[((inode_data_index-132)%16384)%128];
            releaseDataBlock(indirect_data_block, 0);
            releaseDataBlock(dindirect_data_block, 0);
        }
        else{   // read out of bounds of total file
            return -EINVAL;
        }

        struct ktfs_data_block * currData = getDataBlock(data_block_index);
        
        // truncate data block
        if(file->pos/512 == (file->pos+len)/512){   // start and end in the same 
            size_t start_offset = file->pos%512;
            size_t end_offset = (file->pos+len)%512;
            memcpy((void *)(((uint8_t *)buf)+read), (void *)(currData->data+start_offset), end_offset-start_offset);
            read += end_offset-start_offset;
        }
        else if(read == 0){ //start block
            size_t start_offset = file->pos%512;
            // panic here
            memcpy((void *)(((uint8_t *)buf)+read), (void *)(currData->data+start_offset), 512-start_offset);
            read += 512-start_offset;
        }
        else if((len-read) < 512){  //end block
            size_t end_offset = len-read;
            memcpy((void *)(((uint8_t *)buf)+read), (void *)(currData->data), end_offset);
            read += end_offset;
        }
        else{   //middle blocks
            memcpy((void *)(((uint8_t *)buf)+read), (void *)(currData->data), 512);
            read += 512;
        }
        releaseDataBlock(currData, 0);
    }
    file->pos += read;
    return read;
}

/**
 * @brief Write data from the provided argument buffer into file attached to uio
 * @param uio The file to be written to
 * @param buf The buffer to be read from
 * @param len number of bytes to write from the buffer to the file
 * @return number of bytes written from the buffer to the file system if sucessful, negative error
 * code if error
 */
long ktfs_store(struct uio* uio, const void* buf, unsigned long len) {
    if(uio == NULL || buf == NULL){
        return -EINVAL; //invalid arguments
    } 
    //bytes to be stores is 0 just return 0
    if(len == 0){
        return 0;
    }

    struct ktfs_file * const file = (void*)uio - offsetof(struct ktfs_file, file_uio);
    struct ktfs_file * curr = openedFileList;
    while(curr != NULL){
        if(curr == file){
            break;
        }
        curr = curr->next;
    }
    if(curr == NULL){
        return -EINVAL;
    }

    /*
      steps: 𝓓𝓲𝓭𝓭𝔂 𝓽𝓲𝓶𝓮 👅👅👅👅👅
      1. get postition. position will never be past the file size
      2. see if the length we want to write exceeds the file size, if it does we need to allocate more inode blocks, more data blocks?
      3. do all the writing n stuff
      4. update bitmaps? (done in the change file size size function already)
      
      notes: file size, pos, length are in bytes 
    */

    //if the number of bytes we want to write at the specified pos exceeds the file size
    if(file->pos+len > file->file_size){
        //call the helper function to extend the file size
        int retVal = changeFileSize(file, file->pos+len);
        if(retVal != 0){
            return retVal;
        }
    }

    struct ktfs_inode inode = getInode(file->dentry.inode);
    
    unsigned long long written = 0;
    while(written < len){
        // get data block we wanna write to
        uint32_t inode_data_index = (file->pos+written)/512;
        uint32_t data_block_index;
        if(inode_data_index < 4){   // 0-3  direct
            data_block_index = inode.block[inode_data_index];
        }
        else if(inode_data_index < 132){   // 4-131  indirect
            struct ktfs_data_block * indirect_data_block = getDataBlock(inode.indirect);
            uint32_t * indirect_data_array = (uint32_t *)indirect_data_block->data;
            data_block_index = indirect_data_array[inode_data_index-4];
            releaseDataBlock(indirect_data_block, 0);
        }
        else if(inode_data_index < 32900){  // 132-32899  dindirect
            struct ktfs_data_block * dindirect_data_block = getDataBlock(inode.dindirect[(inode_data_index-132)/16384]);
            uint32_t * dindirect_data_array = (uint32_t *)dindirect_data_block->data;
            struct ktfs_data_block * indirect_data_block = getDataBlock(dindirect_data_array[((inode_data_index-132)%16384)/128]);
            uint32_t * indirect_data_array = (uint32_t *)indirect_data_block->data;
            data_block_index = indirect_data_array[((inode_data_index-132)%16384)%128];
            releaseDataBlock(indirect_data_block, 0);
            releaseDataBlock(dindirect_data_block, 0);
        }
        else{   // read out of bounds of total file
            return -EINVAL;
        }

        struct ktfs_data_block * currData = getDataBlock(data_block_index);

        // truncate the data bruh
        if(file->pos/512 == (file->pos+len)/512){   // start and end in the same block
            size_t start_offset = file->pos%512;
            size_t end_offset = (file->pos+len)%512;
            memcpy((void *)(currData->data+start_offset), (void *)(((uint8_t *)buf)+written), end_offset-start_offset);
            written += end_offset-start_offset;
        }
        else if(written == 0){ //start block
            size_t start_offset = file->pos%512;
            memcpy((void *)(currData->data+start_offset), (void *)(((uint8_t *)buf)+written), 512-start_offset);
            written += 512-start_offset;
        }
        else if((len-written) < 512){  //end block
            size_t end_offset = len-written;
            memcpy((void *)(currData->data), (void *)(((uint8_t *)buf)+written), end_offset);
            written += end_offset;
        }
        else{   //middle blocks
            memcpy((void *)(currData->data), (void *)(((uint8_t *)buf)+written), 512);
            written += 512;
        }
        releaseDataBlock(currData, 1);
    }
    file->pos += written;
    return written;
}

/**
 * @brief Create a new file in the file system
 * @param fs The file system in which to create the file
 * @param name The name of the file
 * @return 0 if successful, negative error code if error
 */
int ktfs_create(struct filesystem* fs, const char* name) {
    // FIXME
    //basic null checks
    if (fs == NULL || name == NULL) {
        return -EINVAL;
    }

    // check if file already exists
    struct ktfs_dir_entry dentry_temp;
    int retVal = getDentryWithName(&dentry_temp, name);
    if(retVal == 0){
        return -EINVAL;
    }

    uint32_t inode_index = allocateInode();
    if(inode_index < 0){
        return inode_index;
    }

    struct ktfs_data_block * inode_block = getInodeBlock(inode_index);
    struct ktfs_inode * inode_array = (struct ktfs_inode *)inode_block->data;
    struct ktfs_inode * inode = &inode_array[inode_index%16];
    inode->size = 0;
    releaseInodeBlock(inode_block, 1);

    struct ktfs_superblock superblock = getSuperblock();
    struct ktfs_data_block * root_inode_block = getInodeBlock(superblock.root_directory_inode);
    struct ktfs_inode * root_inode_array = (struct ktfs_inode *)root_inode_block->data;
    struct ktfs_inode * root_inode = &root_inode_array[superblock.root_directory_inode%16];

    root_inode->size += sizeof(struct ktfs_dir_entry);
    
    uint32_t dentry_count = root_inode->size/sizeof(struct ktfs_dir_entry);
    uint32_t last_dentry_index = dentry_count-1;
    uint32_t dentry_block_index = last_dentry_index/32;
    uint32_t dentry_block_offset = last_dentry_index%32;

    if(dentry_block_offset == 0){
        retVal = allocateBlockFromInode(root_inode, dentry_block_index);
        if(retVal != 0){
            releaseInodeBlock(root_inode_block, 1);
            return retVal;
        }
    }

    struct ktfs_data_block * dentry_block;
    retVal = getDentryBlockWithIndex(&dentry_block, *root_inode, last_dentry_index);
    if(retVal != 0){
        releaseInodeBlock(root_inode_block, 1);
        return retVal;
    }
    struct ktfs_dir_entry * dentry_array = (struct ktfs_dir_entry *)dentry_block->data;
    struct ktfs_dir_entry * dentry = &dentry_array[dentry_block_offset];
    dentry->inode = inode_index;
    memcpy(dentry->name, name, strlen(name)+1);
    releaseDataBlock(dentry_block, 1);
    releaseInodeBlock(root_inode_block, 1);
    return 0;
}

/**
 * @brief Deletes a certain file from the file system with the given name
 * @param fs The file system to delete the file from
 * @param name The name of the file to be deleted
 * @return 0 if successful, negative error code if error
 */
int ktfs_delete(struct filesystem* fs, const char* name) {
    /*
    Steps:
        find the file within the dentry
        swap the dentry to delete with the last dentry
            after swap, check if there is only 1 item left in the swapped to block, if there is we have to update the data block bitmap
        if theres only one dentry overall, just set it to zero/null, then update the corresponding data block bitmap
        use the corresponded inode number from the dentry
        go through all the data blocks from the inode, and start to unmap in the data block bitmapo
        unmap the current inode, check if we need to unmap in the inode bitmap
    */

    if (fs == NULL || name == NULL) {
        return -EINVAL;
    }

    // check if file exists
    struct ktfs_dir_entry dentry_temp;
    int retVal = getDentryWithName(&dentry_temp, name);
    if(retVal != 0){
        return -EINVAL;
    }

    struct ktfs_superblock superblock = getSuperblock();
    struct ktfs_data_block * root_inode_block = getInodeBlock(superblock.root_directory_inode);
    struct ktfs_inode * root_inode_array = (struct ktfs_inode *)root_inode_block->data;
    struct ktfs_inode * root_inode = &root_inode_array[superblock.root_directory_inode%16];
    uint32_t dentry_count = root_inode->size/sizeof(struct ktfs_dir_entry);
    uint32_t last_dentry_index = dentry_count-1;

    // Call the helper functions that they wrote 
    // need to get the current dentry with the matching name
    // then needa get the last dentry entry, then swap the 2

    // get dentry we want to delete frin bane
    struct ktfs_data_block * dentry1_block;
    uint32_t dentry1_index;
    retVal = getDentryBlockWithName(&dentry1_block, &dentry1_index, name);
    if(retVal != 0){
        return retVal;
    }
    struct ktfs_dir_entry * dentry1_array = (struct ktfs_dir_entry *)dentry1_block->data;
    struct ktfs_dir_entry * dentry1 = &dentry1_array[dentry1_index%32];

    // get last dentry
    struct ktfs_data_block * dentry2_block;
    retVal = getDentryBlockWithIndex(&dentry2_block, *root_inode, last_dentry_index);
    if(retVal != 0){
        releaseDataBlock(dentry1_block, 1);
        return retVal;
    }
    struct ktfs_dir_entry * dentry2_array = (struct ktfs_dir_entry *)dentry2_block->data;
    struct ktfs_dir_entry * dentry2 = &dentry2_array[last_dentry_index%32];

    // save inode we want to delete
    // set last dentry to dentry we want to delete
    uint16_t inode_index = dentry1->inode;
    dentry1->inode = dentry2->inode;
    strncpy(dentry1->name, dentry2->name, strlen(dentry2->name)+1);

    //get rid of name for dentry we want to delete
    dentry2->name[0] = '\0';

    unsigned long long dentry_block_index = last_dentry_index/32;
    unsigned long long dentry_block_offset = last_dentry_index%32;

    if(dentry_block_offset == 0){
        int retVal = freeBlockFromInode(root_inode, dentry_block_index);
        if(retVal != 0){
            releaseInodeBlock(root_inode_block, 1);
            releaseDataBlock(dentry1_block, 1);
            releaseDataBlock(dentry2_block, 1);
            return retVal;
        }
    }

    // decrement the size
    root_inode->size -= sizeof(struct ktfs_dir_entry);
    releaseInodeBlock(root_inode_block, 1);
    releaseDataBlock(dentry1_block, 1);
    releaseDataBlock(dentry2_block, 1);

    // free all data blocks from the inode
    struct ktfs_inode inode = getInode(inode_index);
    uint32_t data_block_count = ROUND_UP(inode.size, 512)/512;
    for(uint32_t data_block_index = 0; data_block_index < data_block_count; data_block_index++){
        int retVal = freeBlockFromInode(&inode, data_block_count-data_block_index-1);
        if(retVal != 0){
            return retVal;
        }
    }
    freeInode(inode_index);
    return 0;
}

/**
 * @brief Given a file io object, a specific command, and possibly some arguments, execute the
 * corresponding functions
 * @details Any commands such as (FCNTL_GETEND, FCNTL_GETPOS, ...) should pass back through the arg
 * variable. Do not directly return the value.
 * @details FCNTL_GETEND should pass back the size of the file in bytes through the arg variable.
 * @details FCNTL_SETEND should set the size of the file to the value passed in through arg.
 * @details FCNTL_GETPOS should pass back the current position of the file pointer in bytes through
 * the arg variable.
 * @details FCNTL_SETPOS should set the current position of the file pointer to the value passed in
 * through arg.
 * @param uio the uio object of the file to perform the control function
 * @param cmd the operation to execute. KTFS should support FCNTL_GETEND, FCNTL_SETEND (CP2),
 * FCNTL_GETPOS, FCNTL_SETPOS.
 * @param arg the argument to pass in, may be different for different control functions
 * @return 0 if successful, negative error code if error
 */
int ktfs_cntl(struct uio* uio, int cmd, void* arg) {
    // FIXME
    if (!uio || !arg) return -EINVAL;
    struct ktfs_file * const file = (void*)uio - offsetof(struct ktfs_file, file_uio);
    unsigned long long * arg_ptr = (unsigned long long *)arg;
    int retVal;
    
    switch (cmd) {
        case FCNTL_GETEND:
            *arg_ptr = file->file_size;
            break;
        case FCNTL_SETEND:
            if(*arg_ptr < file->file_size){
                return -EINVAL;
            }
            retVal = changeFileSize(file, *arg_ptr);
            if(retVal != 0){
                return retVal;
            }
            break;
        case FCNTL_GETPOS:
            *arg_ptr = file->pos;
            break;
        case FCNTL_SETPOS:
            if(*arg_ptr < 0){
                return -EINVAL;
            }
            file->pos = MIN(*arg_ptr, 32900*512);
            break;
        default:
            return -ENOTSUP;
    }
    return 0;
}

/**
 * @brief Flushes the cache to the backing device
 */
void ktfs_flush(struct filesystem* fs) {
    // FIXME
    cache_flush(ktfs_cache);  // ask about fs
    return;
}

/**
 * @brief Closes the listing device represented by the uio pointer
 * @param uio The uio pointer of ls
 * @return None
 */
void ktfs_listing_close(struct uio* uio) {
    // FIXME
    if(uio == NULL){
        return;
    }
    struct ktfs_file * const file = (void*)uio - offsetof(struct ktfs_file, file_uio);
    for(struct ktfs_file * prev = NULL, * curr = openedListingList; curr != NULL; prev = curr, curr = curr->next){
        if(curr == file){
            if(prev == NULL){
                openedListingList = curr->next;
            }
            else{
                prev->next = curr->next;
            }
            break;
        }
    }
    kfree(file);
}

/**
 * @brief Reads all of the files names in the file system using ls and copies them into the
 * providied buffer
 * @param uio The uio pointer of ls
 * @param buf The buffer to copy the file names to
 * @param bufsz The size of the buffer
 * @return The size written to the buffer
 */
long ktfs_listing_read(struct uio* uio, void* buf, unsigned long bufsz) {
    // FIXME
    if(uio == NULL || buf == NULL){
        return -EINVAL;
    }
    struct ktfs_file * const file = (void*)uio - offsetof(struct ktfs_file, file_uio);
    struct ktfs_file * curr = openedListingList;
    while(curr != NULL){
        if(curr == file){
            break;
        }
        curr = curr->next;
    }
    if(curr == NULL){
        return -EINVAL;
    }

    struct ktfs_superblock superblock = getSuperblock();
    struct ktfs_inode root_inode = getInode(superblock.root_directory_inode);
    uint32_t dentry_count = root_inode.size/sizeof(struct ktfs_dir_entry);

    if(file->pos == dentry_count){
        return -EINVAL;
    }

    struct ktfs_data_block * dentry_block;
    getDentryBlockWithIndex(&dentry_block, root_inode, file->pos);
    struct ktfs_dir_entry * dentry_array = (struct ktfs_dir_entry *)dentry_block;
    releaseDataBlock(dentry_block, 0);
    struct ktfs_dir_entry dentry = dentry_array[file->pos%32];
    strncpy(buf, dentry.name, strlen(dentry.name)+1);
    file->pos++;
    return strlen(dentry.name)+1;
}
