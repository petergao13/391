/*! @file memory.c
    @brief Physical and virtual memory manager
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

#include <stdint.h>
#include <sys/_types.h>
#ifdef MEMORY_TRACE
#define TRACE
#endif

#ifdef MEMORY_DEBUG
#define DEBUG
#endif

#include "memory.h"

#include "conf.h"
#include "console.h"
#include "error.h"
#include "heap.h"
#include "misc.h"
#include "process.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"

// COMPILE-TIME CONFIGURATION
//

// Minimum amount of memory in the initial heap block.

#ifndef HEAP_INIT_MIN
#define HEAP_INIT_MIN 256
#endif

// INTERNAL CONSTANT DEFINITIONS
//

#define MEGA_SIZE ((1UL << 9) * PAGE_SIZE)  // megapage size
#define GIGA_SIZE ((1UL << 9) * MEGA_SIZE)  // gigapage size

#define PTE_ORDER 3
#define PTE_CNT (1U << (PAGE_ORDER - PTE_ORDER))

#ifndef PAGING_MODE
#define PAGING_MODE RISCV_SATP_MODE_Sv39
#endif

#ifndef ROOT_LEVEL
#define ROOT_LEVEL 2
#endif

// IMPORTED GLOBAL SYMBOLS
//

// linker-provided (kernel.ld)
extern char _kimg_start[];
extern char _kimg_text_start[];
extern char _kimg_text_end[];
extern char _kimg_rodata_start[];
extern char _kimg_rodata_end[];
extern char _kimg_data_start[];
extern char _kimg_data_end[];
extern char _kimg_end[];

// EXPORTED GLOBAL VARIABLES
//

char memory_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//

// We keep free physical pages in a linked list of _chunks_, where each chunk
// consists of several consecutive pages of memory. Initially, all free pages
// are in a single large chunk. To allocate a block of pages, we break up the
// smallest chunk on the list.

/**
 * @brief Section of consecutive physical pages. We keep free physical pages in a
 * linked list of chunks. Initially, all free pages are in a single large chunk. To
 * allocate a block of pages, we break up the smallest chunk in the list
 */
struct page_chunk {
    struct page_chunk *next;  ///< Next page in list
    unsigned long pagecnt;    ///< Number of pages in chunk
};

/**
 * @brief RISC-V PTE. RTDC (RISC-V docs) for what each of these fields means!
 */
struct pte {
    uint64_t flags : 8;
    uint64_t rsw : 2;
    uint64_t ppn : 44;
    uint64_t reserved : 7;
    uint64_t pbmt : 2;
    uint64_t n : 1;
};

// INTERNAL MACRO DEFINITIONS
//

#define VPN(vma) ((vma) / PAGE_SIZE)
#define VPN2(vma) ((VPN(vma) >> (2 * 9)) % PTE_CNT)
#define VPN1(vma) ((VPN(vma) >> (1 * 9)) % PTE_CNT)
#define VPN0(vma) ((VPN(vma) >> (0 * 9)) % PTE_CNT)

// The following macros test is a PTE is valid, global, or a leaf. The argument
// is a struct pte (*not* a pointer to a struct pte).

#define PTE_VALID(pte) (((pte).flags & PTE_V) != 0)
#define PTE_GLOBAL(pte) (((pte).flags & PTE_G) != 0)
#define PTE_LEAF(pte) (((pte).flags & (PTE_R | PTE_W | PTE_X)) != 0)

#define PT_INDEX(lvl, vpn) \
    (((vpn) & (0x1FF << (lvl * (PAGE_ORDER - PTE_ORDER)))) >> (lvl * (PAGE_ORDER - PTE_ORDER)))
// INTERNAL FUNCTION DECLARATIONS
//

static void ptab_reset(struct pte *ptab  // page table to reset
);

static struct pte *ptab_clone(struct pte *ptab  // page table to clone
);

static void ptab_discard(struct pte *ptab  // page table to discard
);

static void ptab_insert(struct pte *ptab,   // page table to modify
                        unsigned long vpn,  // virtual page number to insert
                        void *pp,           // pointer to physical page to insert
                        int rwxug_flags     // flags for inserted mapping
);

static void *ptab_remove(struct pte *ptab, unsigned long vpn);

static void ptab_adjust(struct pte *ptab, unsigned long vpn, int rwxug_flags);

struct pte *ptab_fetch(struct pte *ptab, unsigned long vpn);

static inline mtag_t active_space_mtag(void);
static inline mtag_t ptab_to_mtag(struct pte *root, unsigned int asid);
static inline struct pte *mtag_to_ptab(mtag_t mtag);
static inline struct pte *active_space_ptab(void);

static inline void *pageptr(uintptr_t n);
static inline uintptr_t pagenum(const void *p);
static inline int wellformed(uintptr_t vma);

static inline struct pte leaf_pte(const void *pp, uint_fast8_t rwxug_flags);
static inline struct pte ptab_pte(const struct pte *pt, uint_fast8_t g_flag);
static inline struct pte null_pte(void);

// INTERNAL GLOBAL VARIABLES
//

static mtag_t main_mtag;

static struct pte main_pt2[PTE_CNT] __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt1_0x80000[PTE_CNT]
    __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct pte main_pt0_0x80000[PTE_CNT]
    __attribute__((section(".bss.pagetable"), aligned(4096)));

static struct page_chunk *free_chunk_list; //impotrant, THIS IS OUR FREE CHUNK LIST

// EXPORTED FUNCTION DECLARATIONS
//

void memory_init(void) {
    const void *const text_start = _kimg_text_start;
    const void *const text_end = _kimg_text_end;
    const void *const rodata_start = _kimg_rodata_start;
    const void *const rodata_end = _kimg_rodata_end;
    const void *const data_start = _kimg_data_start;

    void *heap_start;
    void *heap_end;

    uintptr_t pma;
    const void *pp;

    trace("%s()", __func__);

    assert(RAM_START == _kimg_start);

    debug("           RAM: [%p,%p): %zu MB", RAM_START, RAM_END, RAM_SIZE / 1024 / 1024);
    debug("  Kernel image: [%p,%p)", _kimg_start, _kimg_end);

    // Kernel must fit inside 2MB megapage (one level 1 PTE)

    if (MEGA_SIZE < _kimg_end - _kimg_start) panic(NULL);

    // Initialize main page table with the following direct mapping:
    //
    //         0 to RAM_START:           RW gigapages (MMIO region)
    // RAM_START to _kimg_end:           RX/R/RW pages based on kernel image
    // _kimg_end to RAM_START+MEGA_SIZE: RW pages (heap and free page pool)
    // RAM_START+MEGA_SIZE to RAM_END:   RW megapages (free page pool)
    //
    // RAM_START = 0x80000000
    // MEGA_SIZE = 2 MB
    // GIGA_SIZE = 1 GB

    // Identity mapping of MMIO region as two gigapage mappings
    for (pma = 0; pma < RAM_START_PMA; pma += GIGA_SIZE)
        main_pt2[VPN2(pma)] = leaf_pte((void *)pma, PTE_R | PTE_W | PTE_G);

    // Third gigarange has a second-level subtable
    main_pt2[VPN2(RAM_START_PMA)] = ptab_pte(main_pt1_0x80000, PTE_G);

    // First physical megarange of RAM is mapped as individual pages with
    // permissions based on kernel image region.

    main_pt1_0x80000[VPN1(RAM_START_PMA)] = ptab_pte(main_pt0_0x80000, PTE_G);

    for (pp = text_start; pp < text_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_X | PTE_G);
    }

    for (pp = rodata_start; pp < rodata_end; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_G);
    }

    for (pp = data_start; pp < RAM_START + MEGA_SIZE; pp += PAGE_SIZE) {
        main_pt0_0x80000[VPN0((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Remaining RAM mapped in 2MB megapages

    for (pp = RAM_START + MEGA_SIZE; pp < RAM_END; pp += MEGA_SIZE) {
        main_pt1_0x80000[VPN1((uintptr_t)pp)] = leaf_pte(pp, PTE_R | PTE_W | PTE_G);
    }

    // Enable paging; this part always makes me nervous.

    main_mtag = ptab_to_mtag(main_pt2, 0);
    csrw_satp(main_mtag);

    // Give the memory between the end of the kernel image and the next page
    // boundary to the heap allocator, but make sure it is at least
    // HEAP_INIT_MIN bytes.

    heap_start = _kimg_end; //heap starts at king end
    heap_end = (void *)ROUND_UP((uintptr_t)heap_start, PAGE_SIZE); //so then the end of the head should be the free space  start?

    if (heap_end - heap_start < HEAP_INIT_MIN) {
        heap_end += ROUND_UP(HEAP_INIT_MIN - (heap_end - heap_start), PAGE_SIZE);
    }

    if (RAM_END < heap_end) panic("out of memory");

    // Initialize heap memory manager

    heap_init(heap_start, heap_end);

    debug("Heap allocator: [%p,%p): %zu KB free", heap_start, heap_end,
          (heap_end - heap_start) / 1024);

    // FIXME: Initialize the free chunk list here
    free_chunk_list = (struct page_chunk *)heap_end; //point the head to heap end
    free_chunk_list->next = NULL;
    free_chunk_list->pagecnt = (RAM_END-heap_end)/PAGE_SIZE;

    // Allow supervisor to access user memory. We could be more precise by only
    // enabling supervisor access to user memory when we are explicitly trying
    // to access user memory, and disable it at other times. This would catch
    // bugs that cause inadvertent access to user memory (due to bugs).
    
    csrs_sstatus(RISCV_SSTATUS_SUM); //If sstatus.SUM=1 supervisor can read and write pages where U=1
    //If sstatus.SUM=0 and PTE is U=0, access in S mode page faults

    memory_initialized = 1;
}

/*	Gets the active memory space.*/
mtag_t active_mspace(void) { return active_space_mtag(); }

/*	Switches the active memory space by writing the satp register.*/
mtag_t switch_mspace(mtag_t mtag) {
    mtag_t prev; 

    prev = csrrw_satp(mtag);
    sfence_vma(); 
    return prev;
}

/*	Copies all pages and page tables from the active memory space into newly allocated memory.*/
//Tag corresponding to newly allocated memory
/*
Creates a new memory space that is a copy of the currently
active memory space
Deep copy: user pages and parts of page table copied
Calling discard_active_mspace on either memory space should not
affect the other
Returns a memory space tag that may be used to refer to
the new memory space, but does not switch
Call switch_mspace to switch to created copy

PAGE 71 SLIDES
*/
mtag_t clone_active_mspace(void) {
    // FIXME
    //deep copy the old stuff into the new stuff   
    //loop through all the entries and set equal
    //global mappings should be directly copied (kernel/uio stuff)
    //usermappings and physical pages should be newly allocated
    struct pte * old_pt2 = active_space_ptab();
    struct pte * new_pt2 = alloc_phys_page();

    for (unsigned int i = 0; i < 512; i++) {
        struct pte * old_pte2 = &old_pt2[i];
        struct pte * new_pte2 = &new_pt2[i];
        //if its a USER
        if ((PTE_GLOBAL(*old_pte2) == 0) && (PTE_VALID(*old_pte2) == 1)) {
            struct pte * old_pt1 = (struct pte *)pageptr(old_pte2->ppn);
            struct pte * new_pt1 = alloc_phys_page();
            *new_pte2 = ptab_pte(new_pt1, 0);

            for (unsigned int j = 0; j < 512; j++) {
                struct pte * old_pte1 = &old_pt1[j];
                struct pte * new_pte1 = &new_pt1[j];
                //if its a user
                if ((PTE_GLOBAL(*old_pte1) == 0) && (PTE_VALID(*old_pte1) == 1)) {
                    struct pte * old_pt0 = (struct pte *)pageptr(old_pte1->ppn);
                    struct pte * new_pt0 = alloc_phys_page();
                    *new_pte1 = ptab_pte(new_pt0, 0); 

                    for (unsigned int k = 0; k < 512; k++) {
                        struct pte * old_pte0 = &old_pt0[k];
                        struct pte * new_pte0 = &new_pt0[k];

                        //if its a user
                        if ((PTE_GLOBAL(*old_pte0) == 0) && (PTE_VALID(*old_pte0) == 1)) {
                            //deep copy the pps, and now also the flags
                            void * old_pp = pageptr(old_pte0->ppn);
                            void * new_pp = alloc_phys_page();
                            *new_pte0 = leaf_pte(new_pp, old_pte0->flags);
                            memcpy(new_pp, old_pp, PAGE_SIZE);
                        }
                    }
                }
            }
            
          //IF ITS A GLOBAL, only needa do for level 2
        } else if ((PTE_GLOBAL(*old_pte2) == 1) && (PTE_VALID(*old_pte2))) {
            //just copy directly
            *new_pte2 = *old_pte2;
        }
    }

    //new mtag corresponding
    mtag_t new_mtag = ptab_to_mtag(new_pt2, 0); //set to 0 for now
    return new_mtag;
}

/*Unmaps any pages in the user range [0xC000’0000,0x1’0000’0000)
mapped with the U flag set and frees the underlying physical pages.
Called by process_exec. from the slides page 99 */
//or
/*Unmaps and frees all non-global pages from the active memory space.*/
void reset_active_mspace(void) {
    // FIXME
    //go through current active memory space? go through each page, then call free pages where the U flag is set?
    //active memory space
    struct pte * pt2 = active_space_ptab();
    for (unsigned int i = 0; i < 512; i++) {
        struct pte * pte2 = &pt2[i];
        if ((PTE_GLOBAL(*pte2) == 0) && (PTE_VALID(*pte2) == 1)) {
            struct pte * pt1 = (struct pte *)pageptr(pte2->ppn);
            for (unsigned int j = 0; j < 512; j++) {
                struct pte * pte1 = &pt1[j];
                if ((PTE_GLOBAL(*pte1) == 0) && (PTE_VALID(*pte1) == 1)) {
                    struct pte * pt0 = (struct pte *)pageptr(pte1->ppn);
                    for (unsigned int k = 0; k < 512; k++) {
                        struct pte * pte0 = &pt0[k];
                        if ((PTE_GLOBAL(*pte0) == 0) && (PTE_VALID(*pte0) == 1)) {
                            free_phys_page(pageptr(pte0->ppn));
                            pte0->flags &= ~PTE_V;
                        }
                    }
                    free_phys_page(pageptr(pte1->ppn));
                    pte1->flags &= ~PTE_V;
                }
            }
            free_phys_page(pageptr(pte2->ppn));
            pte2->flags &= ~PTE_V;
        }
    }
    sfence_vma();
}

/*	Switches memory spaces to main, unmaps and frees all non-global pages from the previously active memory space.*/
//reutns Tag corresponding to main memory space
mtag_t discard_active_mspace(void) {
    // FIXME
    reset_active_mspace(); //first umap and free all non-global pages from previoulsy active memory space by calling reset active mspace
    switch_mspace(main_mtag); //parameter is new satp value
    return main_mtag; //return main mtag which is a global variable
}

// The map_page() function maps a single page into the active address space at
// the specified address. The map_range() function maps a range of contiguous
// pages into the active address space. Note that map_page() is a special case
// of map_range(), so it can be implemented by calling map_range(). Or
//map_range() can be implemented by calling map_page() for each page in the
// range. The current implementation does the latter.

// We currently map 4K pages only. At some point it may be disirable to support
// mapping megapages and gigapages.

struct pte * getLeaf(uintptr_t vma, int rwxug_flags){
    struct pte * pt2 = active_space_ptab();
    struct pte * pte2 = &pt2[VPN2(vma)];
    if(PTE_VALID(*pte2) == 0){
        *pte2 = ptab_pte(alloc_phys_page(), rwxug_flags & PTE_G);
    }
    
    struct pte * pt1 = (struct pte *)pageptr(pte2->ppn);
    struct pte * pte1 = &pt1[VPN1(vma)];

    if(PTE_VALID(*pte1) == 0){ 
        *pte1 = ptab_pte(alloc_phys_page(), rwxug_flags & PTE_G);
    }

    struct pte * pt0 = (struct pte *)pageptr(pte1->ppn);
    struct pte * pte0 = &pt0[VPN0(vma)];
    
    sfence_vma();
    return pte0;
}

void *map_page(uintptr_t vma, void *pp, int rwxug_flags) {
    // FIXME
    return map_range(vma, PAGE_SIZE, pp, rwxug_flags);
}

/*Adds a range of contiguous pages with provided virtual memory address, size, and flags to page table.*/
void *map_range(uintptr_t vma, size_t size, void *pp, int rwxug_flags) {
    // FIXME
    size = ROUND_UP(size, PAGE_SIZE);
    for(int i = 0; i < (size/PAGE_SIZE); i++){
        uintptr_t cur_vma = vma+(i*PAGE_SIZE);
        void * cur_pp = (void *)((uintptr_t)pp+(i*PAGE_SIZE));
        struct pte * pte = getLeaf(cur_vma, rwxug_flags);
        *pte = leaf_pte(cur_pp, rwxug_flags);
    }
    sfence_vma();
    return (void*)vma;
}

/*Allocates and maps physical pages at virtual address vma. The region is
at least size bytes (will be rounded up to page boundary). PTE entries
will have flags given by rwxug_flags with D, A, and V flags also set.
Returns (void*)vma. Does not fail, panics when out of memory.
In MP3cp2 and after, called by elf_load to allocate memory for each
program header entry*/
void *alloc_and_map_range(uintptr_t vma, size_t size, int rwxug_flags) {
    // FIXME
    size = ROUND_UP(size, PAGE_SIZE);
    return map_range(vma, size, alloc_phys_pages(size/PAGE_SIZE), rwxug_flags);
}

void set_range_flags(const void *vp, size_t size, int rwxug_flags) {
    // FIXME
    size = ROUND_UP(size, PAGE_SIZE);
    for(int i = 0; i < (size/PAGE_SIZE); i++){
        uintptr_t cur_vma = (uintptr_t)vp+(i*PAGE_SIZE);
        struct pte * pte = getLeaf(cur_vma, rwxug_flags);
        *pte = leaf_pte((void *)(uintptr_t)(pte->ppn << PAGE_ORDER), rwxug_flags);
    }
    sfence_vma();
}

void unmap_and_free_range(void *vp, size_t size) {
    // FIXME
    size = ROUND_UP(size, PAGE_SIZE);
    for(int i = 0; i < (size/PAGE_SIZE); i++){
        uintptr_t cur_vma = (uintptr_t)vp+(i*PAGE_SIZE);
        struct pte * pte = getLeaf(cur_vma, 0);
        free_phys_page((void *)(uintptr_t)(pte->ppn << PAGE_ORDER));
        pte->flags &= ~1; // set valid flag to 0 ????????
    }
    sfence_vma();
}


/*
Checks that pointer is wellformed and pointer + len does not wrap around zero, 
then iterates over pages in range, confirming the pages are mapped and have the passed flags set.
Parameters
vp	Virtual memory address to start validation (must be a multiple of PAGE_SIZE)
len	Size (in bytes) of range
rwxu_flags	Flags to check pages in range for
Returns
0 on success; error on malformed pointer, unmapped page, or mismatching flags
*/
int validate_vptr(const void *vp, size_t len, int rwxu_flags) {
    // FIXME
    if (vp == NULL || !wellformed((uintptr_t)vp) || ((uintptr_t)vp + len) < (uintptr_t)vp) {
        return -EINVAL;
    }

    //In memory.c the vp argument in the function validate_vptr does not have to be page aligned. It should validate arbitrary pointer.
    for (int i=0; i < len; i++) {
        uintptr_t cur_vma = (uintptr_t)vp + i;

        struct pte * pt2 = active_space_ptab();
        struct pte * pte2 = &pt2[VPN2(cur_vma)];
        if(PTE_VALID(*pte2) == 0){
            return -EINVAL;
        }
        
        struct pte * pt1 = (struct pte *)pageptr(pte2->ppn);
        struct pte * pte1 = &pt1[VPN1(cur_vma)];

        if(PTE_VALID(*pte1) == 0){ 
            return -EINVAL;
        }

        struct pte * pt0 = (struct pte *)pageptr(pte1->ppn);
        struct pte * pte0 = &pt0[VPN0(cur_vma)];

        //[11/20 16:32] In memory.c the function validate_vptr checks if the page has a SUPERSET of rwxu_flags instead of having the exact flags.
        if ((pte0->flags & rwxu_flags) != rwxu_flags) {
            return -EINVAL;
        }
    }
    return 0;
}

//TO DO
/*Checks that pointer is wellformed and the given string is valid. Since the length of the string is unknown, 
we iterate through all characters of the string until \0 terminator, 
confirming that the pages are mapped and have the passed flags set.*/
//Returns
//0 on success; error on malformed pointer, unmapped page, or mismatching flags
int validate_vstr(const char *vs , int rug_flags) {
    // FIXME
    if (vs == NULL || !wellformed((uintptr_t)vs)) {
        return -EINVAL;
    }
    uintptr_t cur_vma = (uintptr_t)vs;
    while(1){
        if (cur_vma < UMEM_START_VMA || cur_vma >= UMEM_END_VMA) {
            return -EINVAL;
        }
        struct pte * pt2 = active_space_ptab();
        struct pte * pte2 = &pt2[VPN2(cur_vma)];
        if(PTE_VALID(*pte2) == 0){
            return -EINVAL;
        }
        
        struct pte * pt1 = (struct pte *)pageptr(pte2->ppn);
        struct pte * pte1 = &pt1[VPN1(cur_vma)];

        if(PTE_VALID(*pte1) == 0){ 
            return -EINVAL;
        }

        struct pte * pt0 = (struct pte *)pageptr(pte1->ppn);
        struct pte * pte0 = &pt0[VPN0(cur_vma)];
        
        if ((pte0->flags & rug_flags) != rug_flags) {
            return -EINVAL;
        }
        if(*((char *)cur_vma) == '\0'){
            break;
        }
        cur_vma++;
    }
    return 0;
}

/*	Allocates a single new page using alloc_phys_pages().*/
void *alloc_phys_page(void) {
    // FIXME
    return alloc_phys_pages(1);
}

void free_phys_page(void *pp) {
    // FIXME
    free_phys_pages(pp, 1);
}

/*	Allocates the passed number of physical pages from the free chunk list.*/
void *alloc_phys_pages(unsigned int cnt) {
    // FIXME
    //take from the head cnt amount???
    //then remove from list??
    if (cnt == 0) return NULL;    //idk maybe NULL??
    if (free_chunk_list == NULL) { 
        panic("out of memory dumass");
    }

    unsigned int best_cnt;
    struct page_chunk * best_prev = NULL;
    struct page_chunk * best_curr = NULL;
    // initialize best to first chunk
    if(free_chunk_list != NULL){
        best_cnt = free_chunk_list->pagecnt;
        best_prev = NULL;
        best_curr = free_chunk_list;
    }
    // find closest chunk
    for(struct page_chunk * prev = NULL, * curr = free_chunk_list; curr != NULL; prev = curr, curr = curr->next){
        if(best_cnt < cnt || (curr->pagecnt >= cnt && curr->pagecnt < best_cnt)){ //works now
            best_cnt = curr->pagecnt;
            best_prev = prev;
            best_curr = curr;
        }
    }
    // remove node
    if(best_curr->pagecnt == cnt){
        if(best_prev == NULL){
            free_chunk_list = best_curr->next;
        }
        else{
            best_prev->next = best_curr->next;
        }
    }
    // fragment if chunk is not exactly the size we want
    // update node to smaller size and make a new node with the size we want
    else if(best_curr->pagecnt > cnt){
        // update node left in list
        best_curr->pagecnt -= cnt;
        // update node removed
        best_curr = (struct page_chunk *)((uintptr_t)best_curr + ((best_cnt - cnt) * PAGE_SIZE));
    }
    // panic if no chunks big enough
    else if(best_curr->pagecnt < cnt){
        panic("😋😋😋😋😋🤪🤪🤪🤪out of memory type shi🤪🤪🤪🤪🤪😋😋😋😋😋😋");
    }
    
    memset(best_curr, 0, cnt * PAGE_SIZE);
    return (void *)best_curr;
}

void free_phys_pages(void *pp, unsigned int cnt) {
    // FIXME
    if (pp == NULL || cnt == 0) {
        return;
    }

    struct page_chunk * temp = pp;
    temp->pagecnt = cnt;
    temp->next = free_chunk_list;
    free_chunk_list = temp;
}

/*	Counts the number of pages remaining in the free chunk list.*/
unsigned long free_phys_page_count(void) {
    unsigned long count = 0;
    for(struct page_chunk * curr = free_chunk_list; curr != NULL; curr = curr->next){
        count += curr->pagecnt;
    }
    return count;
}

/*Called by handle_umode_exception() in excp.c to handle U mode load and store page faults. 
It returns 1 to indicate the fault has been handled (the instruction should be restarted) 
and 0 to indicate that the page fault is fatal and the process should be terminated.*/
//stval CSR contains faulting address.
//sepc CSR contains address of faulting instruction
/*If faulting address is in user region and currently unmapped, allocate
and map a RW page at the faulting page number. Resume execution.*/
//also trap frame not used according to doc
int handle_umode_page_fault(struct trap_frame *tfr, uintptr_t vma) {
    // FIXME
    vma = ROUND_DOWN(vma, PAGE_SIZE);
    if (wellformed(vma) != 1) {
        return 0;
    }

    if (vma < UMEM_START_VMA || vma >= UMEM_END_VMA) {
        return 0;
    }
    
    if (alloc_and_map_range(vma, PAGE_SIZE, PTE_R | PTE_W | PTE_U) != NULL) {
        return 1;
    }
    else {
        return 0;
    }
}

/**
 * @brief Reads satp to retrieve tag for active memory space
 * @return Tag for active memory space
 */
mtag_t active_space_mtag(void) { return csrr_satp(); }

/**
 * @brief Constructs tag from page table address and address space identifier
 * @param ptab Pointer to page table to use in tag
 * @param asid Address space identifier to use in tag
 * @return Memory tag formed from paging mode, page table address, and ASID
 */
static inline mtag_t ptab_to_mtag(struct pte *ptab, unsigned int asid) {
    return (((unsigned long)PAGING_MODE << RISCV_SATP_MODE_shift) |
            ((unsigned long)asid << RISCV_SATP_ASID_shift) | pagenum(ptab) << RISCV_SATP_PPN_shift);
}

/**
 * @brief Retrives a page table address from a tag
 * @param mtag Tag to extract page table address from
 * @return Pointer to page table retrieved from tag
 */
static inline struct pte *mtag_to_ptab(mtag_t mtag) { return (struct pte *)((mtag << 20) >> 8); }

/**
 * @brief Returns the address of the page table corresponding to the active memory space
 * @return Pointer to page table extracted from active memory space tag
 */
static inline struct pte *active_space_ptab(void) { return mtag_to_ptab(active_space_mtag()); }

/**
 * @brief Constructs a physical pointer from a physical page number
 * @param n Physical page number to derive physical pointer from
 * @return Pointer to memory corresponding to physical page
 */
static inline void *pageptr(uintptr_t n) { return (void *)(n << PAGE_ORDER); }

/**
 * @brief Constructs a physical page number from a pointer
 * @param p Pointer to derive physical page number from
 * @return Physical page number corresponding to pointer
 */
static inline unsigned long pagenum(const void *p) { return (unsigned long)p >> PAGE_ORDER; }

/**
 * @brief Checks if bits 63:38 of passed virtual memory address are all 1 or all 0
 * @param vma Virtual memory address to check well-formedness of
 * @return 1 if pointer is well-formed, 0 otherwise
 */
static inline int wellformed(uintptr_t vma) {
    // Address bits 63:38 must be all 0 or all 1
    uintptr_t const bits = (intptr_t)vma >> 38;
    return (!bits || !(bits + 1));
}

/**
 * @brief Constructs a page table entry corresponding to a leaf
 * @details For our purposes, a leaf PTE has the A, D, and V flags set
 * @param pp Physical address to set physical page number of PTE from
 * @param rwxug_flags Flags to set on PTE
 * @return PTE initialized with proper flags and PPN
 */
static inline struct pte leaf_pte(const void *pp, uint_fast8_t rwxug_flags) {
    return (struct pte){.flags = rwxug_flags | PTE_A | PTE_D | PTE_V, .ppn = pagenum(pp)};
}

/**
 * @brief Constructs a page table entry corresponding to a page table
 * @param pt Physical address to set physical page number of PTE from
 * @param g_flag Flags to set on PTE (should either be G flag or nothing)
 * @return PTE initialized with proper flags and PPN
 */
static inline struct pte ptab_pte(const struct pte *pt, uint_fast8_t g_flag) {
    return (struct pte){.flags = g_flag | PTE_V, .ppn = pagenum(pt)};
}

/**
 * @brief Returns an empty pte
 * @return An empty pte
 */
static inline struct pte null_pte(void) { return (struct pte){}; }


