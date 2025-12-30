/*! @file process.c
    @brief user process
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA

*/

/*!
 * @brief Enables trace messages for process.c
 */
#include "console.h"
#ifdef PROCESS_TRACE
#define TRACE
#endif

/*!
 * @brief Enables debug messages for process.c
 */
#ifdef PROCESS_DEBUG
#define DEBUG
#endif

#include "process.h"

#include "conf.h"
#include "elf.h"
#include "error.h"
#include "filesys.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "riscv.h"
#include "string.h"
#include "thread.h"
#include "trap.h"
#include "uio.h"

// COMPILE-TIME PARAMETERS
//

#define TP ((struct thread*)__builtin_thread_pointer())

struct thread_stack_anchor {
    struct thread * ktp;
    void * kgp;
};

/*!
 * @brief Maximum number of processes
 */
#ifndef NPROC
#define NPROC 16
#endif

// INTERNAL FUNCTION DECLARATIONS
//

static int build_stack(void* stack, int argc, char** argv);

static void fork_func(struct condition* forked, struct trap_frame* tfr);

// INTERNAL GLOBAL VARIABLES
//

/*!
 * @brief The main user process struct
 */
static struct process main_proc;

static struct process* proctab[NPROC] = {&main_proc};

// EXPORTED GLOBAL VARIABLES
//

char procmgr_initialized = 0;

// EXPORTED FUNCTION DEFINITIONS
//

void procmgr_init(void) {
    assert(memory_initialized && heap_initialized);
    assert(!procmgr_initialized);

    main_proc.tid = running_thread();
    main_proc.mtag = active_mspace();
    thread_set_process(main_proc.tid, &main_proc);
    procmgr_initialized = 1;
}

//slides 17-22 on Proccesses Lecture
/*Executes a program referred to by the UIO interface passed in as an argument. We only require a
maximum of 16 concurrent processes.
Executing a loaded program with process exec has 4 main requirements:

(a) First any virtual memory mappings belonging to other user processes should be unmapped.
(b) irrelevant for CP2
(c)  the executable should be loaded from the UIO interface provided as an argument into the
mapped pages. (Note: After an executable is loaded into memory space, the file should be
closed.)
(d) Finally, the thread associated with the process needs to be started in user-mode. (Hint: An assembly function in trap.s would be useful here)
    9.3 Context Switching between User-Mode and Supervisor-Mode
*/
int process_exec(struct uio* exefile, int argc, char** argv) {
    // FIXME
    // We need to copy the arguments from user memory into the kernel
    // before we call reset_active_mspace and then provide them to the new process

    void * page = alloc_phys_page();
    if(page == NULL){
        process_exit();
    }

    int stack_size = build_stack(page, argc, argv);
    if(stack_size < 0){
        free_phys_page(page);
        process_exit();
    }

    reset_active_mspace(); //Part a, any virtual memory mappings belonging to other user processes should be unmapped.
    //map page into memory space (after reset_active_mspace) (start of user stack)

    if (map_page(UMEM_END_VMA-PAGE_SIZE, page, PTE_R | PTE_W | PTE_U) == NULL)  {//stack goes high to low
        free_phys_page(page);
        process_exit();
    }

    void (*entry)(void);
    
    int retVal = elf_load(exefile, &entry); //c the executable should be loaded from the UIO interface provided as an argument into the mapped pages.

    uio_close(exefile); //after loaded, should be closed
    if(retVal != 0){
        free_phys_page(page);
        process_exit();
    }

    //START IN USER MODE SOMEHOW??
    /*Switching to user-mode from supervisor-mode: Since our kernel initially boots into supervisormode, 
    it can be a bit tricky to figure out how to use sret to transition into user-mode. This confusion
    may come up because at first glance sret requires a prior user program to have trapped into supervisormode 
    (so that the SPP and SPIE bits will be pre-populated with values). However, if this is the first
    time a user program is being executed there can’t be any prior user-mode trap frame and SPP and SPIE
    cannot be filled in. It is up to the process exec function to fill in the proper values for SPP and SPIE
    so that sret can properly jump to a user-space function.*/

    // initialize trap_frame struct
    struct trap_frame tfr;
    tfr.a0 = argc;
    tfr.a1 = (long)(UMEM_END_VMA-stack_size); //pointer to our arguments
    tfr.sp = (void*)(UMEM_END_VMA-stack_size);
    tfr.sepc = entry;
    tfr.sstatus = RISCV_SSTATUS_SPIE | RISCV_SSTATUS_SUM; //SPP is set to 0
    
    // set ktp to TP to store TP to restore later
    struct thread_stack_anchor * anchor = running_thread_stack_base();
    anchor->ktp = TP;
    
    // sscratch - pointer to thread stack anchor (kernel stack not user stack) - sizeof(trap frame)
    // already decrement the sizeof(trap frame) in trap.s
    trap_frame_jump(&tfr, anchor); //Uses trap_frame_jump to start execution in user space

    //if we ever get here which we shouldn't EVER, call process exit
    process_exit();
}


/*
Forks a child process.

Creates a new process struct for the child, copies the parent's I/O objects and spawns a new thread for the child. 
The child thread uses the parent's trap frame to return to U mode, signaling the parent that it is done with the trap frame.

Ret: 0 on success, error code on failure
*/
int process_fork(const struct trap_frame* tfr) {
    // FIXME
    struct process * child = kcalloc(1, sizeof(struct process));
    if (child == NULL) {
        return -ENOMEM;
    }

    //add to proccess table
    int found = 0;
    for (int i=0; i<16; i++) {
        if (proctab[i] == NULL) {
            proctab[i] = child;
            found = 1;
            break;
        }
    }
    //no space in proccess table
    if (found == 0) {
        kfree(child);
        return -EMPROC;
    }

    struct process * curr = current_process();

    //set new mtag
    mtag_t new_mtag = clone_active_mspace();
    child->mtag = new_mtag;

    //copy uio objects
    for (int i = 0; i < PROCESS_UIOMAX; i++) {
        if (curr->uiotab[i] != NULL) { //check if theres a uio object
            //copy over to child
            child->uiotab[i] = curr->uiotab[i];
            //increment refcount
            uio_addref(curr->uiotab[i]);
        }
    }
    
    //create a condition 
    struct condition * fork_condition = kcalloc(1, sizeof(struct condition));
    if (fork_condition == NULL) {
        return -ENOMEM;
    }

    condition_init(fork_condition, NULL);

    //create a copy of the parents trap frame for the child
    struct trap_frame child_tfr = *tfr;
    child_tfr.a0 = 0;

    //child's new ID???
    int child_id = spawn_thread("child", (void *)fork_func, fork_condition, child_tfr);
    if (child_id < 0) {
        kfree(child);
        kfree(fork_condition);
        return child_id;
    }

    child->tid = child_id;
    thread_set_process(child_id, child);

    // call wait till child is done???
    condition_wait(fork_condition);
    //free after we done wid it
    kfree(fork_condition);

    return child_id;
}

/** \brief
 *
 *
 *  Discard memory space, close your associated uio, free the memory you're supposed to free.
 *
 *
 */
/*Discard memory space, close your associated uio, free the memory you're supposed to free.
Exits the current process. Frees the process struct, discards the active memory space, closes 
all I/O objects, and exits the thread.*/
/*struct process {
    int tid;                             // thread id of our thread
    mtag_t mtag;                         // memory space
    struct uio* uiotab[PROCESS_UIOMAX];  // IO objects associated with current process
};*/
void process_exit(void) {
    // FIXME
    struct process* curr = current_process(); //get the current process
    
    //Exits the current process by remove from process list
    for (int i = 0; i < NPROC; i++) {
        if (curr == proctab[i]) {
            proctab[i] = NULL;
        }
    }

    //close all uio objects?
    for (int i = 0; i < PROCESS_UIOMAX; i++) {
        if (curr->uiotab[i] != NULL) { //check if theres a uio object
            uio_close(curr->uiotab[i]);
            curr->uiotab[i] = NULL;
        }
    }

    //flush cache
    fsmgr_flushall();

    //free??? for CP3

    //discard memory space?
    discard_active_mspace();
    
    //exits thread
    running_thread_exit();
}

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * \brief Builds the initial user stack for a new process.
 *
 * Builds the stack for a new process, including the argument vector (\p argv)
 * and the strings it points to. Note that \p argv must contain \p argc + 1
 * elements (the last one is a NULL pointer).
 *
 * Remember to round the final stack size up to a multiple of 16 bytes
 * (RISC-V ABI requirement).
 *
 * \param[in,out] stack  Pointer to the stack page (destination buffer).
 * \param[in]     argc   Number of arguments in \p argv.
 * \param[in]     argv   Array of argument pointers; length is \p argc+1 and
 *                       \p argv[argc] must be NULL.
 *
 * \return Size of the stack page on success; negative error code on failure.
 */
int build_stack(void* stack, int argc, char** argv) {
    size_t stksz, argsz;
    uintptr_t* newargv;
    char* p;
    int i;

    // We need to be able to fit argv[] on the initial stack page, so _argc_
    // cannot be too large. Note that argv[] contains argc+1 elements (last one
    // is a NULL pointer).

    if (PAGE_SIZE / sizeof(char*) - 1 < argc) return -ENOMEM;

    stksz = (argc + 1) * sizeof(char*);

    // Add the sizes of the null-terminated strings that argv[] points to.

    for (i = 0; i < argc; i++) {
        argsz = strlen(argv[i]) + 1;
        if (PAGE_SIZE - stksz < argsz) return -ENOMEM;
        stksz += argsz;
    }

    // Round up stksz to a multiple of 16 (RISC-V ABI requirement).

    stksz = ROUND_UP(stksz, 16);
    assert(stksz <= PAGE_SIZE);

    // Set _newargv_ to point to the location of the argument vector on the new
    // stack and set _p_ to point to the stack space after it to which we will
    // copy the strings. Note that the string pointers we write to the new
    // argument vector must point to where the user process will see the stack.
    // The user stack will be at the highest page in user memory, the address of
    // which is `(UMEM_END_VMA - PAGE_SIZE)`. The offset of the _p_ within the
    // stack is given by `p - newargv'.

    newargv = stack + PAGE_SIZE - stksz;
    p = (char*)(newargv + argc + 1);

    for (i = 0; i < argc; i++) {
        newargv[i] = (UMEM_END_VMA - PAGE_SIZE) + ((void*)p - (void*)stack);
        argsz = strlen(argv[i]) + 1;
        memcpy(p, argv[i], argsz);
        p += argsz;
    }

    newargv[argc] = 0;
    return stksz;
}

/**
 * \brief Function to be executed by the child process after fork.
 * This is a very beautiful function.
 * Tell the parent process that it is done with the trap frame, then jumps to user space (hint:
 * which function should we use?)
 *
 * \param[in] done  Pointer to a condition variable to signal parent
 * \param[in] tfr   Pointer to a trap frame
 *
 * \return NONE (very important, this is a hint)
 */
void fork_func(struct condition* done, struct trap_frame* tfr) {
    // FIXME
    condition_broadcast(done);
    
    struct thread_stack_anchor * anchor = running_thread_stack_base();
    anchor->ktp = TP;

    trap_frame_jump(tfr, anchor);
    //should never return
}