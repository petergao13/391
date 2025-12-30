// rtc.c - Goldfish RTC driver
// 
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef RTC_TRACE
#define TRACE
#endif

#ifdef RTC_DEBUG
#define DEBUG
#endif

#include "rtc.h"
#include "conf.h"
#include "misc.h"
#include "devimpl.h"
#include "console.h"
#include "string.h"
#include "heap.h"

#include "error.h"

#include <stdint.h>

// INTERNAL TYPE DEFINITIONS
// 

struct rtc_regs {
    uint32_t time_low;  // read first, latches time_high
    uint32_t time_high; //
};

struct rtc_device {
    struct serial base; // must be first
    volatile struct rtc_regs * regs;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int rtc_open(struct serial * ser);
static void rtc_close(struct serial * ser);
static int rtc_recv(struct serial * ser, void * buf, unsigned int bufsz);

static uint64_t read_real_time(volatile struct rtc_regs * regs);

// INTERNAL GLOBAL VARIABLES AND CONSTANTS
//

static const struct serial_intf rtc_serial_intf = {
    .blksz = 8,
    .open = &rtc_open,
    .close = &rtc_close,
    .recv = &rtc_recv
};

// EXPORTED FUNCTION DEFINITIONS
// 

/*
void rtc_attach(void * mmio_base)
Inputs: void * mmio_base
Outputs: None
Description: Attached the RTC device
Side effects: Attaches the bassedin base to serial device, then registers the device
*/
void rtc_attach(void * mmio_base) {
    // FIXME your code goes here
    // kprintf("rtc attached\n");
    struct rtc_device * rtc;
    //allocating memeory

    rtc = kcalloc(1, sizeof(struct rtc_device));
    rtc->regs = mmio_base;

    //connect 
    serial_init(&rtc->base, &rtc_serial_intf);

    //register the device, call it rtc
    register_device("rtc", DEV_SERIAL, rtc);
}

int rtc_open(struct serial * ser) {
    // kprintf("rtc open\n");
    trace("%s()", __func__);
    return 0;
}

void rtc_close(struct serial * ser) {
    // kprintf("rtc close\n");
    trace("%s()", __func__);
}

/*
int rtc_recv(struct serial * ser, void * buf, unsigned int bufsz)
Inputs: struct serial * ser
        void * buf
        unsigned int bufsz
Outputs: int, number of bytes written to buf
Description: Reads the current time
Side effects: Writes the current time to the inputted buffer
*/

int rtc_recv(struct serial * ser, void * buf, unsigned int bufsz) {
    // FIXME your code goes here
    // kprintf("rtc recv\n");
    struct rtc_device * const rtc = 
        (void*)ser - offsetof(struct rtc_device, base);
    uint64_t time_now;

    trace("%s(bufsz=%ld)", __func__, bufsz);

    if (bufsz == 0) {
        return 0;
    }

    time_now = read_real_time(rtc->regs);

    memcpy(buf, &time_now, sizeof(uint64_t));
    return sizeof(uint64_t);
}

/*
uint64_t read_real_time(volatile struct rtc_regs * regs)
Inputs: volatile struct rtc_regs * regs
Outputs: uint64_t current time
Description: Gets the time from the registers
Side effects: 
*/
uint64_t read_real_time(volatile struct rtc_regs * regs) {
    // FIXME your code goes here
    // kprintf("rtc read real time\n");
    uint32_t lo, hi;

    lo = regs->time_low;
    hi = regs->time_high;

    return ((uint64_t)hi << 32) | lo; //dunno what the number is
}