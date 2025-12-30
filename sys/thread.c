// thread.c - Threads
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

/*! @file thread.c
    @brief Thread manager and operations
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#include "console.h"
#include "memory.h" //ADDDED FOR CP3
#ifdef THREAD_TRACE
#define TRACE
#endif

#ifdef THREAD_DEBUG
#define DEBUG
#endif

#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#include "misc.h"
#include "heap.h"
#include "string.h"
#include "riscv.h"
#include "intr.h"
#include "error.h"
#include "process.h"

#include "see.h"
#include <stddef.h>


#include <stdarg.h>

// COMPILE-TIME PARAMETERS
//

// NTHR is the maximum number of threads

#ifndef NTHR
#define NTHR 16
#endif

// EXPORTED GLOBAL VARIABLES
//

char thrmgr_initialized = 0;

// INTERNAL TYPE DEFINITIONS
//


enum thread_state {
    THREAD_UNINITIALIZED = 0,
    THREAD_WAITING,
    THREAD_SELF,
    THREAD_READY,
    THREAD_EXITED
};

struct thread_context {
    union {
        uint64_t s[12];
        struct {
            uint64_t a[8];      // s0 .. s7
            void (*pc)(void);   // s8
            uint64_t _pad;      // s9
            void * fp;          // s10
            void * ra;          // s11
        } startup;
    };

    void * ra;
    void * sp;
};

struct thread_stack_anchor {
    struct thread * ktp;
    void * kgp;
};

struct thread {
    struct thread_context ctx;  // must be first member (thrasm.s)
    int id; // index into thrtab[]
    enum thread_state state;
    const char * name;
    struct thread_stack_anchor * stack_anchor;
    void * stack_lowest;
    struct process * proc;
    struct thread * parent;
    struct thread * list_next;
    struct condition * wait_cond;
    struct condition child_exit;
    struct lock * lock_list;
};

// INTERNAL MACRO DEFINITIONS
// 

// Pointer to running thread, which is kept in the tp (x4) register.

#define TP ((struct thread*)__builtin_thread_pointer())

// Macro for changing thread state. If compiled for debugging (DEBUG is
// defined), prints function that changed thread state.

#define set_thread_state(t,s) do { \
    debug("Thread <%s:%d> state changed from %s to %s by <%s:%d> in %s", \
        (t)->name, (t)->id, \
        thread_state_name((t)->state), \
        thread_state_name(s), \
        TP->name, TP->id, \
        __func__); \
    (t)->state = (s); \
} while (0)

// INTERNAL FUNCTION DECLARATIONS
//

// Initializes the main and idle threads. called from threads_init().

static void init_main_thread(void);
static void init_idle_thread(void);

// Sets the RISC-V thread pointer to point to a thread.

static void set_running_thread(struct thread * thr);

// Returns a string representing the state name. Used by debug and trace
// statements, so marked unused to avoid compiler warnings.

static const char * thread_state_name(enum thread_state state)
    __attribute__ ((unused));

// void thread_reclaim(int tid)
//
// Reclaims a thread's slot in thrtab and makes its parent the parent of its
// children. Frees the struct thread of the thread.

static void thread_reclaim(int tid);

// struct thread * create_thread(const char * name)
//
// Creates and initializes a new thread structure. The new thread is not added
// to any list and does not have a valid context (_thread_switch cannot be
// called to switch to the new thread).

static struct thread * create_thread(const char * name);

// void running_thread_suspend(void)
// Suspends the currently running thread and resumes the next thread on the
// ready-to-run list using _thread_swtch (in threasm.s). Must be called with
// interrupts enabled. Returns when the current thread is next scheduled for
// execution. If the current thread is TP, it is marked READY and placed
// on the ready-to-run list. Note that running_thread_suspend will only return if the
// current thread becomes READY.

static void running_thread_suspend(void);

void lock_release_completely(struct lock * lock);

// void release_all_thread_locks(struct thread * thr)
// Releases all locks held by a thread. Called when a thread exits.

static void release_all_thread_locks(struct thread * thr);

// The following functions manipulate a thread list (struct thread_list). Note
// that threads form a linked list via the list_next member of each thread
// structure. Thread lists are used for the ready-to-run list (ready_list) and
// for the list of waiting threads of each condition variable. These functions
// are not interrupt-safe! The caller must disable interrupts before calling any
// thread list function that may modify a list that is used in an ISR.
//MUST DISABLE INTERRUPTS IF WE CALL THESE
static void tlclear(struct thread_list * list);
static int tlempty(const struct thread_list * list);
static void tlinsert(struct thread_list * list, struct thread * thr);
static struct thread * tlremove(struct thread_list * list);
static void tlappend(struct thread_list * l0, struct thread_list * l1);

static void idle_thread_func(void);

// IMPORTED FUNCTION DECLARATIONS
// defined in thrasm.s
//

extern struct thread * _thread_swtch(struct thread * thr);

extern void _thread_startup(void);

// INTERNAL GLOBAL VARIABLES
//

#define MAIN_TID 0
#define IDLE_TID (NTHR-1)

static struct thread main_thread;
static struct thread idle_thread;

extern char _main_stack_lowest[]; // from start.s
extern char _main_stack_anchor[]; // from start.s

static struct thread main_thread = {
    .id = MAIN_TID,
    .name = "main",
    .state = THREAD_SELF,
    .stack_anchor = (void*)_main_stack_anchor,
    .stack_lowest = _main_stack_lowest,
    .child_exit.name = "main.child_exit"
};

extern char _idle_stack_lowest[]; // from thrasm.s
extern char _idle_stack_anchor[]; // from thrasm.s

static struct thread idle_thread = {
    .id = IDLE_TID,
    .name = "idle",
    .state = THREAD_READY,
    .parent = &main_thread,
    .stack_anchor = (void*)_idle_stack_anchor,
    .stack_lowest = _idle_stack_lowest,
    .ctx.sp = _idle_stack_anchor,
    .ctx.ra = &_thread_startup,
    // FIXME your code goes here
    .ctx.startup.pc = &idle_thread_func,
    .ctx.startup.ra = &running_thread_exit
};

static struct thread * thrtab[NTHR] = {
    [MAIN_TID] = &main_thread,
    [IDLE_TID] = &idle_thread
};

static struct thread_list ready_list = {
    .head = &idle_thread,
    .tail = &idle_thread
};

// EXPORTED FUNCTION DEFINITIONS
//


int running_thread(void) {
    return TP->id;
}

void thrmgr_init(void) {
    trace("%s()", __func__);
    init_main_thread();
    init_idle_thread();
    set_running_thread(&main_thread);
    thrmgr_initialized = 1;
}


/*
int spawn thread(const char * name, void (*entry)(void), ...)
Inputs: const char * name,
        void (*entry)(void)
Outputs: Int, child id
Description: Creates and Schedules a new Thread
Side Effects: Initializes context structures of the thread, adds to thread to ready list
*/
int spawn_thread (
    const char * name,
    void (*entry)(void),
    ...)
{
    struct thread * child;
    va_list ap;
    int pie;
    int i;

    child = create_thread(name); //call create thread to create the new thread

    if (child == NULL)
        return -EMTHR;

    set_thread_state(child, THREAD_READY); //set the thread state to ready

    pie = disable_interrupts();
    tlinsert(&ready_list, child);
    restore_interrupts(pie);

   // FIXME your code goes here
    //initialize context structure of the new thread
    child->ctx.startup.fp = child->ctx.sp;
    child->ctx.sp = child->stack_anchor; 
    child->ctx.startup.pc = entry;
    child->ctx.startup.ra = &running_thread_exit;
    child->ctx.ra = &_thread_startup;
    
   // filling in entry function arguments is given below, the rest is up to you
    va_start(ap, entry);
    for (i = 0; i < 8; i++)
        child->ctx.startup.a[i] = va_arg(ap, uint64_t);
    va_end(ap);

    // kprintf("exited spawn\n");
    return child->id;
}


/*
void running_thread_exit(void)
Inputs: None
Outputs: None
Description: Exits the current running thread
Side effects: Sets thread status to exited, and call suspend
*/
void running_thread_exit(void) {
    // FIXME your code goes here
    //first check currently runnign thread is the main thread
    if (running_thread() == MAIN_TID) {
        halt_success();
    }

    //Otherwise, it should set the current thread’s state to THREAD EXITED.
    set_thread_state(TP, THREAD_EXITED);

    //Next, it should signal the parent thread in case it is waiting for the current thread to exit.
    if (TP->parent != NULL) {
        condition_broadcast(&TP->parent->child_exit);
    }
    //Lastly, it should call running thread suspend()
    release_all_thread_locks(TP);

    running_thread_suspend(); //should not return cuz why?

    //if we return call hald failure
    halt_failure();
}


void running_thread_yield(void) {
    trace("%s() in <%s:%d>", __func__, TP->name, TP->id);
    running_thread_suspend();
}


/*
int thread_join(int tid)
Inputs: int tid
Outputs: int, tid
Description: Wait for a child to exit
Side effects: Releases resources used by child
*/
int thread_join(int tid) {
    //kprintf("entered thread join\n");
    if (tid != 0) {
    //kprintf("tid is not 0\n");
    //  if ((thrtab[tid] != NULL) && ((tid < 0 || tid >= NTHR) || (thrtab[tid]->parent != TP))) {
    //     return -EINVAL;
    //  }

        if (tid < 0 || tid >= NTHR || thrtab[tid] == NULL || thrtab[tid]->parent != TP) {
        return -EINVAL;
        }

        //now wait for thread to exit?
        while (thrtab[tid]->state != THREAD_EXITED) {
        condition_wait(&TP->child_exit);
        }

        //thread reclaim
        thread_reclaim(tid);
        return tid;
    }

    //if TID == 0, then we wait for ANY child to be done, then return that child
    if (tid == 0) {
        //first check if it has children
        int childflag = 0;
        for (int i = 0; i < NTHR-1; i++) {
            if (thrtab[i] != NULL && thrtab[i]->parent == TP) {
                //then TP has a child, so we can move on
                //kprintf("child found\n");
                childflag = 1;
                break;
            }
        }

        if (childflag == 0) {
            //kprintf("no children exist\n");
            return -EINVAL; //no children exist
        }

        for (;;) {
            for (int i = 0; i < NTHR-1; i++) {
                if (thrtab[i] != NULL && thrtab[i]->parent == TP && thrtab[i]->state == THREAD_EXITED) {
                    thread_reclaim(i);
                    return i; //returns tid of the child that exited
                }
            }

            //else nothing is exited, so confition wait until condition is broadcasted
            //kprintf("before condition waiting\n");
            // condition_broadcast(&TP->child_exit);
            condition_wait(&TP->child_exit);
        }
    }

    //shouldn't ever get here
    return 0;
}

struct process * thread_process(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->proc;
}

struct process * running_thread_process(void) {
    return TP->proc;
}

void thread_set_process(int tid, struct process * proc) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    thrtab[tid]->proc = proc;
}

void thread_detach(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    thrtab[tid]->parent = NULL;
}

const char * thread_name(int tid) {
    assert (0 <= tid && tid < NTHR);
    assert (thrtab[tid] != NULL);
    return thrtab[tid]->name;
}

const char * running_thread_name(void) {
    return TP->name;
}

void * running_thread_stack_base(void){
    return TP->stack_anchor;
}

void condition_init(struct condition * cond, const char * name) {
    tlclear(&cond->wait_list);
    cond->name = name;
}

void condition_wait(struct condition * cond) {
    int pie;

    trace("%s(cond=<%s>) in <%s:%d>", __func__,
        cond->name, TP->name, TP->id);

    assert(TP->state == THREAD_SELF);

    // Insert current thread into condition wait list
    
    set_thread_state(TP, THREAD_WAITING);
    TP->wait_cond = cond;
    TP->list_next = NULL;

    pie = disable_interrupts();
    tlinsert(&cond->wait_list, TP);
    restore_interrupts(pie);

    running_thread_suspend();
}


/*
void condition_broadcast(struct condition * cond)
Inputs: struct condition * cond
Outputs: None
Description: Wake up threads waiting on the condition variable
Side effects: Modifys the read list
*/
void condition_broadcast(struct condition * cond) {
    // FIXME your code goes here
    int pie = disable_interrupts();
    while (tlempty(&cond->wait_list) != 1) {
        //remove then change the state
        struct thread * removed = tlremove(&cond->wait_list);
        set_thread_state(removed, THREAD_READY);

        //add to list
        removed->wait_cond = NULL;
        tlinsert(&ready_list, removed);
    }

    restore_interrupts(pie);

    // kprintf("exited condition broadcast\n");
}

void lock_init(struct lock * lock) {
    memset(lock, 0, sizeof(struct lock));
    condition_init(&lock->release, "lock_release");
}

void lock_acquire(struct lock * lock) {
    if (lock->owner != TP) {
        while (lock->owner != NULL)
            condition_wait(&lock->release);
        
        lock->owner = TP;
        lock->cnt = 1;
        lock->next = TP->lock_list;
        TP->lock_list = lock;
    } else
        lock->cnt += 1;
}

void lock_release(struct lock * lock) {
    assert (lock->owner == TP);
    assert (lock->cnt != 0);

    lock->cnt -= 1;

    if (lock->cnt == 0)
        lock_release_completely(lock);
}

// INTERNAL FUNCTION DEFINITIONS
//

void init_main_thread(void) {
    // Initialize stack anchor with pointer to self
    main_thread.stack_anchor->ktp = &main_thread;
}

void init_idle_thread(void) {
    // Initialize stack anchor with pointer to self
    idle_thread.stack_anchor->ktp = &idle_thread;
}

static void set_running_thread(struct thread * thr) {
    asm inline ("mv tp, %0" :: "r"(thr) : "tp");
}

const char * thread_state_name(enum thread_state state) {
    static const char * const names[] = {
        [THREAD_UNINITIALIZED] = "UNINITIALIZED",
        [THREAD_WAITING] = "WAITING",
        [THREAD_SELF] = "SELF",
        [THREAD_READY] = "READY",
        [THREAD_EXITED] = "EXITED"
    };

    if (0 <= (int)state && (int)state < sizeof(names)/sizeof(names[0]))
        return names[state];
    else
        return "UNDEFINED";
};

void thread_reclaim(int tid) {
    struct thread * const thr = thrtab[tid];
    int ctid;

    assert (0 < tid && tid < NTHR && thr != NULL);
    assert (thr->state == THREAD_EXITED);

    // Make our parent thread the parent of our child threads. We need to scan
    // all threads to find our children. We could keep a list of all of a
    // thread's children to make this operation more efficient.

    for (ctid = 1; ctid < NTHR; ctid++) {
        if (thrtab[ctid] != NULL && thrtab[ctid]->parent == thr)
            thrtab[ctid]->parent = thr->parent;
    }

    thrtab[tid] = NULL;
    kfree(thr);
}

struct thread * create_thread(const char * name) {
    struct thread_stack_anchor * anchor;
    void * stack_lowest;
    size_t stack_size;
    struct thread * thr;
    int tid;

    trace("%s(name=\"%s\") in <%s:%d>", __func__, name, TP->name, TP->id);

    // Find a free thread slot.

    tid = 0;
    while (++tid < NTHR)
        if (thrtab[tid] == NULL)
            break;
    
    if (tid == NTHR)
        return NULL;
    
    // Allocate a struct thread and a stack

    thr = kcalloc(1, sizeof(struct thread));
    
    stack_size = PAGE_SIZE; // change to PAGE_SIZE in mp3
    stack_lowest = alloc_phys_page();
    anchor = stack_lowest + stack_size;
    anchor -= 1; // anchor is at base of stack
    thr->stack_lowest = stack_lowest;
    thr->stack_anchor = anchor;
    anchor->ktp = thr;
    anchor->kgp = NULL;

    thrtab[tid] = thr;

    thr->id = tid;
    thr->name = name;
    thr->parent = TP;
    thr->proc = TP->proc;
    return thr;
}


/*
void running thread suspend(void)
Inputs: None
Outputs: None
Description: Suspend current thread and switch to a ready one
Side Effects: Modifys readylist, and also frees stack
*/
void running_thread_suspend(void) {
    //this should be called a lot
    // kprintf("suspend called\n");


    // FIXME your code goes here
    int pie;
    int currentThread = running_thread(); //get the current thread running

    //check if its running
    if (thrtab[currentThread]->state == THREAD_SELF) {
        //update state
        set_thread_state(thrtab[currentThread], THREAD_READY); //set it to ready

        //insert at tail of list
        pie = disable_interrupts();
        tlinsert(&ready_list, thrtab[currentThread]);
        restore_interrupts(pie);
    }

    //TAke the next thread from the head of the list, and resume it
    pie = disable_interrupts();
    struct thread * next_thread = tlremove(&ready_list);
    restore_interrupts(pie);

    //set next thread to running
    set_thread_state(next_thread, THREAD_SELF);

    //swtich memory space as well?
    if (next_thread->proc != NULL) {
        switch_mspace(next_thread->proc->mtag);
    }

    //switch to the thread
    struct thread * prev = _thread_swtch(next_thread);

    //free its stack
    if (prev->state == THREAD_EXITED) {
        // kfree(prev->stack_lowest); //kfree
        //You will also have to change the corresponding kfree in running thread suspend to a free phys page
        free_phys_page(prev->stack_lowest);
    }
}

void tlclear(struct thread_list * list) {
    list->head = NULL;
    list->tail = NULL;
}

int tlempty(const struct thread_list * list) {
    return (list->head == NULL);
}

void tlinsert(struct thread_list * list, struct thread * thr) {
    thr->list_next = NULL;

    if (thr == NULL)
        return;

    if (list->tail != NULL) {
        assert (list->head != NULL);
        list->tail->list_next = thr;
    } else {
        assert(list->head == NULL);
        list->head = thr;
    }

    list->tail = thr;
}

struct thread * tlremove(struct thread_list * list) {
    struct thread * thr;

    thr = list->head;
    
    if (thr == NULL)
        return NULL;

    list->head = thr->list_next;
    
    if (list->head != NULL)
        thr->list_next = NULL;
    else
        list->tail = NULL;

    thr->list_next = NULL;
    return thr;
}

void tlappend(struct thread_list * l0, struct thread_list * l1) {
    if (l0->head != NULL) {
        assert(l0->tail != NULL);
        
        if (l1->head != NULL) {
            assert(l1->tail != NULL);
            l0->tail->list_next = l1->head;
            l0->tail = l1->tail;
        }
    } else {
        assert(l0->tail == NULL);
        l0->head = l1->head;
        l0->tail = l1->tail;
    }

    l1->head = NULL;
    l1->tail = NULL;
}

void lock_release_completely(struct lock * lock) {
    struct lock ** hptr;

    condition_broadcast(&lock->release);
    hptr = &TP->lock_list;
    while (*hptr != lock && *hptr != NULL)
        hptr = &(*hptr)->next;
    assert (*hptr != NULL);
    *hptr = (*hptr)->next;
    lock->owner = NULL;
    lock->next = NULL;
}

void release_all_thread_locks(struct thread * thr) {
    struct lock * head;
    struct lock * next;

    head = thr->lock_list;

    while (head != NULL) {
        next = head->next;
        head->next = NULL;
        head->owner = NULL;
        head->cnt = 0;
        condition_broadcast(&head->release);
        head = next;
    }

    thr->lock_list = NULL;
}

void idle_thread_func(void) {
    // The idle thread sleeps using wfi if the ready list is empty. Note that we
    // need to disable interrupts before checking if the thread list is empty to
    // avoid a race condition where an ISR marks a thread ready to run between
    // the call to tlempty() and the wfi instruction.

    for (;;) {
        // If there are runnable threads, yield to them.

        while (!tlempty(&ready_list))
            running_thread_yield();
        
        // No runnable threads. Sleep using the wfi instruction. Note that we
        // need to disable interrupts and check the runnable thread list one
        // more time (make sure it is empty) to avoid a race condition where an
        // ISR marks a thread ready before we call the wfi instruction.

        disable_interrupts();
        if (tlempty(&ready_list))
            asm ("wfi");
        enable_interrupts();
    }
}