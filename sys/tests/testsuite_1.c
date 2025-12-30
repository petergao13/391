#include "testsuite_1.h"
#include "testsuite_2.h"
#include "conf.h"
#include "console.h"
#include "device.h"
#include "thread.h"
#include "filesys.h"
#include "error.h"
#include "cache.h"
#include "dev/ramdisk.h"
#include "elf.h"
#include "heap.h"

// Definitions
#define CMNTNAME "c"
#define DEVMNTNAME "dev"
#define CDEVNAME "vioblk"
#define CDEVINST 0

// Add args, structs, includes, defines
void run_testsuite_1() {
    int retval = -EINVAL;
    char * test_output;

    retval = testGoon();

    // //Passes
    // retval = test_find_storage();
    // test_output = (retval == 0) ? "Storage1 passed!" : "Storage1 failed!"; 
    // kprintf("%s\n", test_output);

    // //Passes
    // retval = test_simple_storage_write();
    // test_output = (retval == 0) ? "Storage3 passed!" : "Storage3 failed!"; 
    // kprintf("%s\n", test_output);

    // // //Passes
    // retval = test_simple_storage_read();
    // test_output = (retval == 0) ? "Storage2 passed!" : "Storage2 failed!"; 
    // kprintf("%s\n", test_output);

    // retval = test_simple_ramdisk_uio_read();
    // test_output = (retval == 0) ? "Storage4 passed!" : "Storage4 failed!"; 
    // kprintf("%s\n", test_output);

    // retval = test_uio_control_ramdisk_read();
    // test_output = (retval == 0) ? "Storage5 passed!" : "Storage5 failed!"; 
    // kprintf("%s\n", test_output);

    // retval = test_elf_load_with_ramdisk_uio();
    // test_output = (retval == 0) ? "Storage6 passed!" : "Storage6 failed!"; 
    // kprintf("%s\n", test_output);

    // retval = test_cache_get_and_release_block();
    // test_output = (retval == 0) ? "test1 passed!" : "test1 failed!"; 
    // kprintf("%s\n", test_output);

    // retval = test_cache_get();
    // test_output = (retval == 0) ? "test1 passed!" : "test1 failed!"; 
    // kprintf("%s\n", test_output);

    // retval = testMultipleWrite();
    // test_output = (retval == 0) ? "Storage3 passed!" : "Storage3 failed!"; 
    // kprintf("%s\n", test_output);

    //retval = testCacheEvict(); //hello jj
    // test_output = (retval == 0) ? "Storage3 passed!" : "Storage3 failed!"; 
    //kprintf("%s\n", test_output);
}

// Make whatever tests you want.
int testGoon() {
    kprintf("starting goon test\n");
    int child1 = spawn_thread("goon1\n", (void*)goonEntry1);
    int child2 = spawn_thread("goon2\n", (void*)goonEntry2);
    int child3 = spawn_thread("goon3\n", (void*)goonEntry3);
    int child4 = spawn_thread("goon4\n", (void*)goonEntry4);
    int child5 = spawn_thread("goon5\n", (void*)goonEntry5);
    int child6 = spawn_thread("goon6\n", (void*)goonEntry6);
    int child7 = spawn_thread("goon7\n", (void*)goonEntry7);
    int child8 = spawn_thread("goon8\n", (void*)goonEntry8);
    int child9 = spawn_thread("goon9\n", (void*)goonEntry9);
    int child10 = spawn_thread("goon10\n", (void*)goonEntry10);

    struct storage* hd;
    int retval;
    hd = find_storage("vioblk", 0);
    if (hd == NULL) {
        kprintf("goon device not found\n");
        return -1;
    } else {
        kprintf("goon device found\n");
    }

    retval = storage_open(hd);
    if (retval != 0) {
        kprintf("failed to open goon\n");
        return -1;
    } else {
        kprintf("opened goon\n");
    }

    
    char * wdata = kcalloc(1, 1024);
    for(int i = 0; i < 1024; i++){
        *(wdata+i) = i;
    }
    retval = storage_store(hd, 0, wdata, 512);
    retval = storage_store(hd, 512, wdata+512, 512);
    if (retval != 512) {
            kprintf("failed to write goon to goon 1\n");
            return -1;
        }
    
    // for (int i = 0; i < 64; i++) {
    //     *wdata = i;
    //     *(wdata + 1) = 0xFF;
    //     retval = storage_store(hd, i*512, wdata, 512);
    //     if (retval != 512) {
    //         kprintf("failed to write goon to goon 1\n");
    //         return -1;
    //     }
    // }

    kfree(wdata);

    thread_join(0);
    thread_join(0);
    thread_join(0);
    thread_join(0);
    thread_join(0);
    thread_join(0);
    thread_join(0);
    thread_join(0);
    thread_join(0);
    thread_join(0);
}

int goonEntry1(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 1;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry2(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 2;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry3(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 3;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}


int goonEntry4(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 4;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry5(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 5;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry6(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 6;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry7(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 7;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry8(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 8;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry9(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    *(data) = 9;
    for(int i = 0; i < 64; i++){
        storage_store(hd, i*512, data, 512);
    }
    
    kfree(data);
}

int goonEntry10(){
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    
    char * data = kcalloc(1, 512);
    for(int i = 0; i < 64; i++){
        storage_fetch(hd, i*512, data, 512);
        kprintf("goon[%d]: %x\n", i, (uint8_t)*data);
    }
    
    kfree(data);
}






int test_find_storage() {
    struct storage* hd;
    hd = find_storage("vioblk", 0);
    if (hd == NULL) {
        kprintf("Storage device not found\n");
        return -1;
    } else {
        kprintf("Storage device found\n");
    }
    return 0;
}

int test_simple_storage_read() {
    struct storage* hd;
    int retval;
    hd = find_storage("vioblk", 0);
    if (hd == NULL) {
        kprintf("Storage device not found\n");
        return -1;
    } else {
        kprintf("Storage device found\n");
    }

    retval = storage_open(hd);
    if (retval != 0) {
        kprintf("failed to open storage\n");
        return -1;
    } else {
        kprintf("opened storage\n");
    }

    char buf[512];

    retval = storage_fetch(hd, 0, buf, 512);
    if (retval != 512) {
        kprintf("failed to fetch from storage\n");
        return -1;
    } else {
        kprintf("fetched from storage %d bytes\n", retval);
    }
    for (int i = 0; i < 512; ++i) {
        kprintf("buf[%d] = %x\n", i, buf[i]);
    }
    storage_close(hd);
    return 0;
}

int test_simple_storage_write() {
    struct storage* hd;
    int retval;
    hd = find_storage("vioblk", 0);
    if (hd == NULL) {
        kprintf("Storage device not found\n");
        return -1;
    } else {
        kprintf("Storage device found\n");
    }

    retval = storage_open(hd);
    if (retval != 0) {
        kprintf("failed to open storage\n");
        return -1;
    } else {
        kprintf("opened storage\n");
    }

    char wdata[512];
    for (int i = 0; i < 512; ++i) {
        wdata[i] = i;
    }

    retval = storage_store(hd, 0, wdata, 512);
    if (retval != 512 ) {
        kprintf("failed to write to storage\n");
        return -1;
    }

    char rdata[512];

    retval = storage_fetch(hd, 0, rdata, 512);
    if (retval != 512) {
        kprintf("failed to fetch from storage\n");
    } else {
        kprintf("fetched from storage %d bytes\n", retval);
    }
    for (int i = 0; i < 512; ++i) {
        kprintf("rdata[%d] = %d\n", i, rdata[i]);
    }
    storage_close(hd);
    return 0;
}

int test_simple_ramdisk_uio_read() {
    struct uio* ruio;
    ramdisk_attach();
    open_file(DEVMNTNAME, "ramdisk0", &ruio);
    char buf[50];
    int retval = uio_read(ruio, buf, 50);
    if (retval != 50) {
        return -1;
    }
    for (int i = 0; i < 50; ++i) {
        kprintf("buf[%d] = %x\n", i, buf[i]);
    }
    return 0;
}

int test_uio_control_ramdisk_read() {
    struct uio* ruio;
    int retval;
    ramdisk_attach();
    open_file(DEVMNTNAME, "ramdisk0", &ruio);
    char buf[50];
    unsigned long long pos = 5;
    retval = uio_cntl(ruio, FCNTL_SETPOS, &pos);
    if (retval != 0) {
        kprintf("Failed to set pos of ramdisk\n");
        return -1;
    }
    unsigned long long disksz;
    retval = uio_cntl(ruio, FCNTL_GETEND, &disksz);
    if (retval != 0) {
        kprintf("Failed to get end of disk\n");
        return -1;
    }
    kprintf("disksz = %u\n", disksz);
    retval = uio_read(ruio, buf, 10);
    for (int i = 0; i < 10; ++i) {
        kprintf("buf[%d] = %x\n", i, buf[i]);
    }

    retval = uio_cntl(ruio, FCNTL_GETPOS, &pos);
    if (retval != 0) {
        kprintf("Failed to get position of ramdisk\n");
        return -1;
    }
    kprintf("Position of ramdisk uio is %u\n", pos);
    return 0;
}

int test_elf_load_with_ramdisk_uio() {
    struct uio* ruio;
    struct uio* termio;
    int retval;
    ramdisk_attach();
    open_file(DEVMNTNAME, "ramdisk0", &ruio);
    open_file(DEVMNTNAME, "uart1", &termio);
    void (*entry_ptr)(struct uio*);
    retval = elf_load(ruio, &entry_ptr);
    if (retval < 0) {
        kprintf("elf load failed with retval %d\n", retval);
        return -1;
    }
    retval = spawn_thread("hellothr", (void *)entry_ptr, termio);
    if (retval < 0) {
        kprintf("spawn thread failed with retval %d\n", retval);
        return -1;
    }
    thread_join(retval);
    return 0;
}

int test_cache_get_and_release_block() {
    struct storage* disk;
    struct cache* cptr;
    ramdisk_attach();
    disk = find_storage("ramdisk", 0);
    storage_open(disk);
    create_cache(disk, &cptr);
    char* buf;
    cache_get_block(cptr, 0, (void**)&buf);
    for (int i = 0; i < 512; ++i) {
        kprintf("buf[%d] = %x\n", i, buf[i]);
    }
    cache_release_block(cptr, buf, 0);
}

int test_cache_get() {
    //test_simple_storage_read();
    struct storage* hd;
    struct cache* cptr;
    ramdisk_attach();
    hd = find_storage("vioblk", 0);
    storage_open(hd);
    create_cache(hd, &cptr);
    char* buf;
    cache_get_block(cptr, 0, &buf);
    for (int i = 0; i < 512; ++i) {
        kprintf("buf[%d] = %x\n", i, buf[i]);
    }
}


int testMultipleWrite() {
    struct storage* hd;
    int retval;
    hd = find_storage("vioblk", 0);
    if (hd == NULL) {
        kprintf("Storage device not found\n");
        return -1;
    } else {
        kprintf("Storage device found\n");
    }

    retval = storage_open(hd);
    if (retval != 0) {
        kprintf("failed to open storage\n");
        return -1;
    } else {
        kprintf("opened storage\n");
    }

    
    char * wdata = kcalloc(1, 512);
    for (int i = 0; i < 64; i++) {
        *wdata = i;
        *(wdata + 1) = 0xFF;
        retval = storage_store(hd, i*512, wdata, 512);
        if (retval != 512) {
            kprintf("failed to write to storage 1\n");
            return -1;
        }
    }

    *wdata = 3;
    retval = storage_store(hd, 0, wdata, 512);
    if (retval != 512) {
        kprintf("failed to write to storage 2\n");
        return -1;
    }

    *wdata = 0xFE;
    retval = storage_store(hd, 1*512, wdata, 512);
    if (retval != 512) {
        kprintf("failed to write to storage 3\n");
        return -1;
    }


    *wdata = 0xCE;
    retval = storage_store(hd, 2*512, wdata, 512);
    if (retval != 512) {
        kprintf("failed to write to storage 4\n");
        return -1;
    }
    kprintf("Passed\n");

    kfree(wdata);

    storage_close(hd);
    return 0;
}


int testCacheEvict() {
    // testMultipleWrite();
    // struct storage* hd;
    // struct cache* cptr;
    // ramdisk_attach();
    // hd = find_storage("vioblk", 0);
    // storage_open(hd);
    // create_cache(hd, &cptr);

    // char* buf;
    // for (int i = 0; i < 64; i++) {
    //     // kprintf("count: %d\n", cptr->count);
    //     cache_get_block(cptr, i*512, (void**)&buf);
    // }

    // //test evict
    // kprintf("count: %d\n", cptr->count);
    // kprintf("head: %p\n", cptr->head);
    // kprintf("tail: %p\n", cptr->tail);
    // for(int i = 0; i < 512; i++) {
    //     kprintf("node: %x\n", cptr->head->data[i]);
    // }

    // cache_get_block(cptr, (unsigned long long)64*512, (void**)&buf);
    // kprintf("count: %d\n", cptr->count);
    // kprintf("head: %p\n", cptr->head);
    // kprintf("tail: %p\n", cptr->tail);
    // for(int i = 0; i < 512; i++) {
    //     kprintf("node: %x\n", cptr->head->data[i]);
    // }
    


    // char* buf;
    // cache_get_block(cptr, 0, &buf);
    // for (int i = 0; i < 512; ++i) {
    //     kprintf("buf[%d] = %x\n", i, buf[i]);
    // }
}