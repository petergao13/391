/*! @file cache.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‍‌​​‌​⁠⁠‌​⁠⁠‌
    @brief Block cache for a storage device.
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef CACHE_TRACE
#define TRACE
#endif

#ifdef CACHE_DEBUG
#define DEBUG
#endif

#include "cache.h"

#include "conf.h"
#include "console.h"
#include "device.h"
#include "devimpl.h"
#include "error.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "string.h"
#include "thread.h"

#include "dev/virtio.h" 

// INTERNAL TYPE DEFINITIONS
//
struct cache_node {
    char data[512];
    struct cache_node * next;
    struct cache_node * prev;
    unsigned long long blk_pos;
    uint8_t dirty;

    // struct lock lock;
};

struct cache {
    struct storage * disc;          // the disc the cache belongs to 
    struct cache_node * head; //doubly linked list
    struct cache_node * tail;
    int count; //number of items in our list. SHOULD NOT EXCEED 64
    struct lock lock;
};

/**
 * @brief Creates/initializes a cache with the passed backing storage device (disk) and makes it
 * available through cptr.
 * @param disk Pointer to the backing storage device.
 * @param cptr Pointer to the cache to create.
 * @return 0 on success, negative error code if error
 */
int create_cache(struct storage* disk, struct cache** cptr) {
    // FIXME
    // remember to write some initial tests
    if (disk == NULL || cptr == NULL) {
        return -EINVAL;
    }

    //allocate space for our cache
    struct cache * cache = kcalloc(1, sizeof(struct cache));
    if (!cache) {
        return -ENOMEM;     // kcalloc failed
    } 
    cache->disc = disk;
    cache->head = NULL;
    cache->tail = NULL;
    cache->count = 0;
    lock_init(&cache->lock);

    *cptr = cache;
    
    return 0;
}

/**
 * @brief Reads a CACHE_BLKSZ sized block from the backing interface into the cache.
 * @param cache Pointer to the cache.
 * @param pos Position in the backing storage device. Must be aligned to a multiple of the block
 * size of the backing interface.
 * @param pptr Pointer to the block pointer read from the cache. Assume that CACHE_BLKSZ will always
 * be equal to the block size of the storage disk. Any replacement policy is permitted, as long as
 * your design meets the above specifications.
 * @return 0 on success, negative error code if error
 */
int cache_get_block(struct cache* cache, unsigned long long pos, void** pptr) {
    // FIXME
    if (pos % 512 != 0 || cache == NULL || pptr == NULL) {
        return -EINVAL;
    }
    
    lock_acquire(&cache->lock);
    
    //find block pos
    unsigned long long blknum = pos/512;
    
    long result;

    //cache hit
    for(struct cache_node * curr = cache->head; curr != NULL; curr = curr->next){
        if(curr->blk_pos == blknum){
            *pptr = curr->data;
            lock_release(&cache->lock);
            return 0;
        }
    }

    //evict tail if cache is full
    //write back if tail is dirty
    if(cache->count == 64){
        if(cache->tail->dirty == 1){
            result = cache->disc->intf->store(cache->disc, cache->tail->blk_pos*512, cache->tail->data, 512);
            if(result < 512){
                lock_release(&cache->lock);
                return -EIO;
            }
        }
        cache->tail = cache->tail->prev;
        kfree(cache->tail->next);
        cache->tail->next = NULL;
        cache->count--;
    }

    //read new node from disk
    struct cache_node * node = kcalloc(1, sizeof(struct cache_node));
    if(node == NULL){
        kfree(node);
        lock_release(&cache->lock);
        return -ENOMEM;
    }
    node->next = cache->head;
    node->prev = NULL;
    node->blk_pos = blknum;
    node->dirty = 0;
    result = cache->disc->intf->fetch(cache->disc, node->blk_pos*512, node->data, 512);
    if(result < 512){
        kfree(node);
        lock_release(&cache->lock);
        return -EIO;
    }
    cache->head = node;
    if(cache->tail == NULL){
        cache->tail = node;
    }
    else{
        node->next->prev = node;
    }
    cache->count++;

    *pptr = node->data;
    lock_release(&cache->lock);
    return 0;
}

/**
 * @brief Releases a block previously obtained from cache_get_block().
 * @param cache Pointer to the cache.
 * @param pblk Pointer to a block that was made available in cache_get_block() (which means that
 * pblk == *pptr for some pptr).
 * @param dirty Indicates whether the block has been modified (1) or not (0). If dirty == 1, the
 * block has been written to. If dirty == 0, the block has not been written to.
 * @return 0 on success, negative error code if error
 */
void cache_release_block(struct cache* cache, void* pblk, int dirty) {
    // FIXME
    if (cache == NULL || pblk == NULL) {
        return;
    }

    lock_acquire(&cache->lock);

    //iterate through every node and compare
    for(struct cache_node * curr = cache->head; curr != NULL; curr = curr->next){
        if(curr->data == pblk){
            curr->dirty |= dirty;
        }
    }
    lock_release(&cache->lock);
}

/**
 * @brief Flushes the cache to the backing device
 * @param cache Pointer to the cache to flush
 * @return 0 on success, error code if error
 */
int cache_flush(struct cache* cache) {
    // FIXME
    if (cache == NULL) {
        return -EINVAL;
    }

    lock_acquire(&cache->lock);

    //iterate through every node and compare
    for(struct cache_node * curr = cache->head; curr != NULL; curr = curr->next){
        if(curr->dirty == 1){
            long result = cache->disc->intf->store(cache->disc, curr->blk_pos*512, curr->data, 512);
            if(result < 512){
                lock_release(&cache->lock);
                return -EIO;
            }
            curr->dirty = 0; //just added this
        }
    }
    lock_release(&cache->lock);
    return 0;
}
