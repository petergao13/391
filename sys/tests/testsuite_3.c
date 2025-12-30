#include "testsuite_1.h"
#include "testsuite_2.h"
#include "testsuite_3.h"
#include "cache.h"
#include "conf.h"
#include "console.h"
#include "dev/rtc.h"
#include "dev/uart.h"
#include "dev/virtio.h"
#include "device.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "intr.h"
#include "memory.h"
#include "process.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "elf.h"

#define CMNTNAME "c"
#define DEVMNTNAME "dev"
#define CDEVNAME "vioblk"
#define CDEVINST 0
#define INITEXE "trek"  // FIXME

void run_testsuite_3() {
    //test mapping??
    //========================================================
    // uintptr_t * page = alloc_phys_page();
    // //map page into memory space (after reset_active_mspace) (start of user stack)
    // map_page(0xc0000000, page, PTE_R | PTE_W | PTE_U); //stack goes high to low

    // alloc_and_map_range(0xc0000000,4000*3, PTE_R | PTE_W | PTE_U);

    // reset_active_mspace();
    
    //==========================================================

    // unsigned long cnt = free_phys_page_count();
    // kprintf("num of free physical pages before is: %lu\n", cnt);
    // uintptr_t * pages1 = alloc_phys_pages(2000);
    // cnt = free_phys_page_count();
    // kprintf("num of free physical pages after is: %lu\n", cnt);
    // map_range(0x100000001,2*PAGE_SIZE, pages1, PTE_W | PTE_U);

    //=========================================================
    
    uintptr_t * page = alloc_phys_page();
    //map page into memory space (after reset_active_mspace) (start of user stack)
    map_page(0xc0000000, page, PTE_R | PTE_W | PTE_U); //stack goes high to low

    alloc_and_map_range(0xc0000000,4000*9, PTE_R | PTE_W | PTE_U);

    reset_active_mspace();

    while (1){
        continue;
    }
}