#include "testsuite_2.h"
#include "conf.h"
#include "console.h"
#include "device.h"
#include "fsimpl.h"
#include "thread.h"
#include "filesys.h"
#include "error.h"
#include "cache.h"
#include "dev/ramdisk.h"
#include "elf.h"
#include <string.h>
#include "ktfs.h"

// Definitions
#define CMNTNAME "c"
#define DEVMNTNAME "dev"
#define CDEVNAME "vioblk"
#define CDEVINST 0

static void printblk(const uint8_t * blk, uint32_t size);

void parentEntry(void);
void child1Entry(void);
void child2Entry(void);



// testsuite
void run_testsuite_2() {
    kprintf("starting thread test\n");
    int parent_id = spawn_thread("parent thread\n", (void*)parentEntry);
    int a = thread_join(parent_id);
    kprintf("parent: %d\n", a);
}

void parentEntry(void){
    spawn_thread("child1", (void*)child1Entry);
    spawn_thread("child2", (void*)child2Entry);
    int a = thread_join(0);
    int b = thread_join(0);
    kprintf("child1: %d\n", a);
    kprintf("child2: %d\n", b);
}
void child1Entry(void){
    kprintf("exited chil dentry \n");
    return;
}

void child2Entry(void){
    for (int i = 0; i < 10; i++) {
        kprintf("iteration: %d\n", i);
    }
}































// // testcases
// int test_mount() {
//     struct storage* hd;
//     struct cache* cptr;
//     struct filesystem *fs;
//     struct  uio* uio;
//     int retval;
    
//     hd = find_storage("vioblk", 0);
//     if (!hd) {
//         kprintf("failed to find storage device\n");
//         return -1;
//     }

//     retval = storage_open(hd);
//     if (retval != 0) {
//         kprintf("failed to open storage device");
//         return -1;
//     }

//     retval = create_cache(hd, &cptr);
//     if (retval != 0 || cptr == NULL) {
//         kprintf("failed to create cache\n");
//     }

//     retval = mount_ktfs("file system", cptr);
//     if (retval != 0) {
//         kprintf("mount failed\n");
//     }
//     kprintf("mount succeeded\n");

//     storage_close(hd);

//     return 0;
// }

// int test_open() {
//     struct storage* hd;
//     struct cache* cptr;
//     struct filesystem *fs;
//     struct  uio* uio;
//     int retval;
    
//     hd = find_storage("vioblk", 0);
//     if (!hd) {
//         kprintf("failed to find storage device\n");
//         return -1;
//     }

//     retval = storage_open(hd);
//     if (retval != 0) {
//         kprintf("failed to open storage device");
//         return -1;
//     }

//     retval = create_cache(hd, &cptr);
//     if (retval != 0 || cptr == NULL) {
//         kprintf("failed to create cache\n");
//     }

//     retval = mount_ktfs("file system", cptr);
//     if (retval != 0) {
//         kprintf("mount failed\n");
//     }
//     kprintf("mount succeeded\n");

//     //testing opening trek???
    


//     retval = ktfs_open(struct filesystem* fs, const char* name, struct uio** uioptr);
//     if (retval != 0) {
//         kprintf("open failed\n");
//     }
//     kprintf("open succeeded\n");
    
//     storage_close(hd);

//     return 0;
// }