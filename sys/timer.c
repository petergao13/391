// timer.c - A timer system
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//


#ifdef TIMER_TRACE
#define TRACE
#endif

#ifdef TIMER_DEBUG
#define DEBUG
#endif

#include "timer.h"
#include "thread.h"
#include "riscv.h"
#include "misc.h"
#include "intr.h"
#include "conf.h"
#include "see.h" // for set_stcmp
#include <stddef.h> // For NULL

// EXPORTED GLOBAL VARIABLE DEFINITIONS
// 

char timer_initialized = 0;

// INTERNVAL GLOBAL VARIABLE DEFINITIONS
//

static struct alarm * sleep_list;

// INTERNAL FUNCTION DECLARATIONS
//

// EXPORTED FUNCTION DEFINITIONS
//

void timer_init(void) {
    // set_stcmp(UINT64_MAX);
    unsigned long long now = rdtime();
    set_stcmp(now + (10*(TIMER_FREQ / 1000))); //set to current time + 10ms
    timer_initialized = 1;
}


/*struct alarm {
    struct condition cond; ///< Condition variable
    struct alarm * next; ///< Linked list of pending alarms sorted earliest to latest
    unsigned long long twake; ///< The absolute time when an alarm should trigger
};*/


/*
void alarm_init(struct alarm * al, const char * name) 
Inputs: struct alarm * al, 
        const char * name
Outputs: None
Description: Intializes Alarm Fields
Side Effects: Sets current time, initliazes the condition
*/
void alarm_init(struct alarm * al, const char * name) {
    // FIXME your code goes here
    //if the alarm pointer is null
    if (al == NULL) {
        return;  
    }

    al->next = NULL;
    al->twake = rdtime(); //set to current time

    condition_init(&al->cond, name); //initialize the name
    // kprintf("initialized alarm\n");
}


/*
void alarm_sleep(struct alarm * al, unsigned long long tcnt)
Inputs: struct alarm * al, 
        unsigned long long tcnt
Outputs: None
Description: Puts the current thread to sleep
Side Effects: Ammends the sleep list
*/
void alarm_sleep(struct alarm * al, unsigned long long tcnt) {
    // kprintf("alarm sleep called\n");
    if (al == NULL) {
        return;
    }
    
    unsigned long long now;
    struct alarm * prev;
    int pie;

    now = rdtime();

    //Step 1 Given to us
    // If the tcnt is so large it wraps around, set it to UINT64_MAX
    if (UINT64_MAX - al->twake < tcnt)
        al->twake = UINT64_MAX;
    else
        al->twake += tcnt;
    
    // If the wake-up time has already passed, return
    if (al->twake < now)
        return;
    
    // FIXME your code goes here

    //prevent race conditions
    pie = disable_interrupts(); 
    //Step 2
    prev = NULL; //prev will stay NULL when we insert at head
    struct alarm * curr = sleep_list;

    while (curr != NULL && curr->twake <= al->twake) {
        prev = curr;
        curr = curr->next;
    }

    al->next = curr; //insert

    if (prev == NULL) {
        sleep_list = al; 
        //update mtimecmp, Step 3
        // set_stcmp(al->twake);
        set_stcmp(MIN(al->twake, now + (10*(TIMER_FREQ / 1000))));
    } else {
        prev->next = al; //connect the list
    }

    //step 4 enable timer interrupts
    csrs_sie(RISCV_SIE_STIE);

    //put the current thread to sleep //NEED TO PUT THIS AFTER WE ENABLE TIMER INETERRUPTS
    condition_wait(&al->cond);
    restore_interrupts(pie);

    // kprintf("exited sleep\n");
}

// Resets the alarm so that the next sleep increment is relative to the time
// alarm_reset is called.

void alarm_reset(struct alarm * al) {
    al->twake = rdtime();
}

void alarm_sleep_sec(struct alarm * al, unsigned int sec) {
    alarm_sleep(al, sec * TIMER_FREQ);
}

void alarm_sleep_ms(struct alarm * al, unsigned long ms) {
    alarm_sleep(al, ms * (TIMER_FREQ / 1000));
}

void alarm_sleep_us(struct alarm * al, unsigned long us) {
    alarm_sleep(al, us * (TIMER_FREQ / 1000 / 1000));
}

void sleep_sec(unsigned int sec) {
    sleep_ms(1000UL * sec);
}

void sleep_ms(unsigned long ms) {
    sleep_us(1000UL * ms);
}

void sleep_us(unsigned long us) {
    struct alarm al;

    alarm_init(&al, "sleep");
    alarm_sleep_us(&al, us);
}


/*
void handle_timer_interrupt(void)
Inputs: None
Outputs: None
Description: Handles timer interrupts
Side Effects: Updates the sleep list
*/
void handle_timer_interrupt(void) {
    //this should be called a lot, if it isnt then issue with alarm sleep
    // kprintf("entered timer ISR\n");

    struct alarm * head = sleep_list;
    struct alarm * next;
    uint64_t now;

    now = rdtime();

    trace("[%lu] %s()", now, __func__);
    debug("[%lu] mtcmp = %lu", now, rdtime());

    // FIXME your code goes here
    //remove all alarms that are past their threshold
    while (head != NULL && head->twake <= now) {
        //wake up all threads that are waiting on this alarms condition
        condition_broadcast(&head->cond);
        sleep_list = head->next;
        head = head->next; //remove this node from the list
    }

    //disable timer interrupts if no alarm objects
    if (head == NULL) {
        // csrc_sie(RISCV_SIE_STIE); //if sleeplist is empty clear the STIE bit
        //FOR MP3 CP3 WE ALWAYS WANT TIMER INTERRUPTS
        set_stcmp(now + (10*(TIMER_FREQ / 1000)));
    } else {
        //check if we should set stcmp to += 10ms, or next item in list
        // set_stcmp(head->twake); //set the new threshold with the current alarm
        set_stcmp(MIN(head->twake, now + (10*(TIMER_FREQ / 1000))));
    }

    // kprintf("exited timer isr\n");
}