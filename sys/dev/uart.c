// uart.c -  NS8250-compatible serial port
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef UART_TRACE
#define TRACE
#endif

#ifdef UART_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "misc.h"
#include "uart.h"
#include "devimpl.h"
#include "intr.h"
#include "heap.h"
#include "thread.h"
#include "console.h"

#include "error.h"

#include <stdint.h>

// COMPILE-TIME CONSTANT DEFINITIONS
//

#ifndef UART_RBUFSZ
#define UART_RBUFSZ 64
#endif

#ifndef UART_INTR_PRIO
#define UART_INTR_PRIO 1
#endif

#ifndef UART_DEVNAME
#define UART_DEVNAME "uart"
#endif


// INTERNAL TYPE DEFINITIONS
// 

struct uart_regs {
    union {
        char rbr; // DLAB=0 read
        char thr; // DLAB=0 write
        uint8_t dll; // DLAB=1
    };
    
    union {
        uint8_t ier; // DLAB=0
        uint8_t dlm; // DLAB=1
    };
    
    union {
        uint8_t iir; // read
        uint8_t fcr; // write
    };

    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
};

#define LCR_DLAB (1 << 7)
#define LSR_OE (1 << 1)
#define LSR_DR (1 << 0)
#define LSR_THRE (1 << 5)
#define IER_DRIE (1 << 0)
#define IER_THREIE (1 << 1)

// Simple fixed-size ring buffer

struct ringbuf {
    unsigned int hpos; // head of queue (from where elements are removed)
    unsigned int tpos; // tail of queue (where elements are inserted)
    char data[UART_RBUFSZ];
};

// UART device structure

struct uart_serial {
    struct serial base;
    volatile struct uart_regs * regs;
    int irqno;
    char opened;

    unsigned long rxovrcnt; ///< number of times OE was set
    
    struct condition rxbnotempty; ///< signalled when rxbuf becomes not empty
    struct condition txbnotfull;  ///< signalled when txbuf becomes not full
    struct lock lock;

    struct ringbuf rxbuf;
    struct ringbuf txbuf;
};

// INTERNAL FUNCTION DEFINITIONS
//

static int uart_serial_open(struct serial * ser);
static void uart_serial_close(struct serial * ser);
static int uart_serial_recv(struct serial * ser, void * buf, unsigned int bufsz);
static int uart_serial_send(struct serial * ser, const void * buf, unsigned int bufsz);
static void uart_isr(int srcno, void * aux);

// Ring buffer (struct rbuf) functions

static void rbuf_init(struct ringbuf * rbuf);
static int rbuf_empty(const struct ringbuf * rbuf);
static int rbuf_full(const struct ringbuf * rbuf);
static void rbuf_putc(struct ringbuf * rbuf, char c);
static char rbuf_getc(struct ringbuf * rbuf);

// INTERNAL GLOBAL VARIABLES
//

static const struct serial_intf uart_serial_intf = {
    .blksz = 1,
    .open = &uart_serial_open,
    .close = &uart_serial_close,
    .recv = &uart_serial_recv,
    .send = &uart_serial_send
};

// EXPORTED FUNCTION DEFINITIONS
// 


void attach_uart(void * mmio_base, int irqno) {
    struct uart_serial * uart;

    trace("%s(%p,%d)", __func__, mmio_base, irqno);
    
    // UART0 is used for the console and should not be attached as a normal
    // device. It should already be initialized by console_init(). We still
    // register the device (to reserve the name uart0), but pass a NULL device
    // pointer, so that find_serial("uart", 0) returns NULL.

    if (mmio_base == (void*)UART0_MMIO_BASE) {
        register_device(UART_DEVNAME, DEV_SERIAL, NULL);
        return;
    }
    
    uart = kcalloc(1, sizeof(struct uart_serial));

    uart->regs = mmio_base;
    uart->irqno = irqno;
    uart->opened = 0;

    // Initialize condition variables. The ISR is registered when our interrupt
    // source is enabled in uart_serial_open().

    condition_init(&uart->rxbnotempty, "uart.rxnotempty");
    condition_init(&uart->txbnotfull, "uart.txnotfull");
    lock_init(&uart->lock);


    // Initialize hardware

    uart->regs->ier = 0;
    uart->regs->lcr = LCR_DLAB;
    // fence o,o ?
    uart->regs->dll = 0x01;
    uart->regs->dlm = 0x00;
    // fence o,o ?
    uart->regs->lcr = 0; // DLAB=0

    serial_init(&uart->base, &uart_serial_intf);
    register_device(UART_DEVNAME, DEV_SERIAL, uart);
}


/*
static int uart_serial_open(struct serial * ser)
Inputs: struct serial * ser
Outputs: None
Description: Opens the UART for communication
Side effects: Initializes rx/tx buffers, enables interrupts, mark as opened, and registers the UART
*/
int uart_serial_open(struct serial * ser) {
    struct uart_serial * const uart =  
        (void*)ser - offsetof(struct uart_serial, base);

    trace("%s()", __func__);

    if (uart->opened)
        return -EBUSY;
    
    // Reset receive and transmit buffers
    rbuf_init(&uart->rxbuf);
    rbuf_init(&uart->txbuf);

    // Read receive buffer register to flush any stale data in hardware buffer
    uart->regs->rbr; // forces a read because uart->regs is volatile

    
    // FIXME your code goes here
// Enable interrupts when data ready (DR) status asserted
//registers the UART’s interrupt handle
//     You must also indicate the
// corresponding uart serial is opened using the opened flag
    
    //register device??
    uart->opened = 0x01; //mark as opened
    uart->regs->ier |= IER_DRIE; //enable interrupt flag and the source
    enable_intr_source(uart->irqno, UART_INTR_PRIO, &uart_isr, uart); 

    return 0;
}

/*
static int uart_serial_close(struct serial * ser)
Inputs: struct serial * ser
Outputs: None
Description: Closes the UART for communication
Side effects: Disables interrupts, marks as closed
*/
void uart_serial_close(struct serial * ser) {
    struct uart_serial * const uart =
        (void*)ser - offsetof(struct uart_serial, base);

    trace("%s()", __func__);

    // FIXME your code goes here

    //if the device is not opened, do nothing
    if(uart->opened == 0) {
        return;
    }

    //unregister the device, then disable all interrupts
    //how do we unregister the device
    uart->opened = 0x00; //indicate uart serial is closed
    uart->regs->ier = 0x00; //disable interrupts

    disable_intr_source(uart->irqno);
}


/*
static int uart_serial_recv(struct serial * ser, void * buf, unsigned int bufsz)
Inputs: struct serial * ser, 
        void * buf,
        unsigned int bufsz
Outputs: int, number of bytes written to buffer
Description: Reads from UART
Side effects: continously reads from UART, and writes to the inputted buffer. 
Returns the number of bytes written to buffer. 
*/
int uart_serial_recv(struct serial * ser, void * buf, unsigned int bufsz) {
    // FIXME your code goes here
    //copy the uart intilization with the serial like in the above 2 functions
    //also check if the serial is valid first 
    if (ser == NULL || buf == NULL) {
        return -EINVAL;
    }

    struct uart_serial * const uart =
        (void*)ser - offsetof(struct uart_serial, base);

    trace("%s()", __func__);

    //if it isnt opened then return and do nothing, or if buffer size isnt valid or then do nothign
    if (uart->opened == 0) {
        return -EINVAL;
    }

    if (bufsz == 0) {
        return 0;
    }

    lock_acquire(&uart->lock);

    unsigned int bytesread = 0; //count that we will be returning

    //enable interrupt for data sending in
    uart->regs->ier |= IER_DRIE;

    while (bytesread < bufsz) {
        //if its empty then keep looping till its not
        //enable interrupt for data sending in
        uart->regs->ier |= IER_DRIE;
        
        //disable interrupts
        int pie = disable_interrupts();
        while (rbuf_empty(&uart->rxbuf) == 1) {
            //continue; //replace continue with condition wait
            condition_wait(&uart->rxbnotempty);
        }
        restore_interrupts(pie);
        
        //enable interrupt for data sending in
        uart->regs->ier |= IER_DRIE;

        //not empty, so read the data and write it to the buffer
        ((char*)buf)[bytesread] = rbuf_getc(&uart->rxbuf);
        bytesread++; //increment the number of bytes we read so far
        //enable interrupt for data sending in
        uart->regs->ier |= IER_DRIE;
    }

    lock_release(&uart->lock);

    //then return number of bytes written to buffer
    return bytesread;
}


/*
static int uart_serial_send(struct serial * ser, void * buf, unsigned int bufsz)
Inputs: struct serial * ser, 
        void * buf,
        unsigned int bufsz
Outputs: int, number of bytes written to transmit buffer
Description: Writes to UART
Side effects: continously reads from inputted buffer, and writes to the transmit buffer. 
Returns the number of bytes written to transmit buffer. 
*/
int uart_serial_send(struct serial * ser, const void * buf, unsigned int bufsz) {
    // FIXME your code goes here
    //copy the uart intilization with the serial like in the above 2 functions
    //also check if the serial is valid first 
    if (ser == NULL || buf == NULL) {
        return -EINVAL;
    }

    struct uart_serial * const uart =
        (void*)ser - offsetof(struct uart_serial, base);

    trace("%s()", __func__);

    //if it isnt opened then return and do nothing, or if buffer size isnt valid or buf pointer is null then do nothign
    if (uart->opened == 0) {
        return -EINVAL;
    }

    if (bufsz == 0) {
        return 0;
    }

    lock_acquire(&uart->lock);

    unsigned int byteswritten = 0; //count that we will be returning
    
    //enable interrupt for tranmission
    uart->regs->ier |= IER_THREIE;
    while (byteswritten < bufsz) {
        //enable interrupt for tranmission
        uart->regs->ier |= IER_THREIE;
        //not empty, so read the data and write it to the buffer
        //if transmit ringbuffer is full, then keep looping

        //disable interrupts to prevent race conditions
        int pie = disable_interrupts();
        while (rbuf_full(&uart->txbuf) == 1) {
            //continue; replace with condition wait
            condition_wait(&uart->txbnotfull);
        }
        restore_interrupts(pie);
        //enable interrupt for data sending in
        uart->regs->ier |= IER_THREIE;
        
        //now write
        rbuf_putc(&uart->txbuf, ((char*)buf)[byteswritten]);
        byteswritten++; //increment the number of bytes we read so far
        //enable interrupt for tranmission
        uart->regs->ier |= IER_THREIE;
    }

    lock_release(&uart->lock);

    //then return number of bytes written to buffer
    return byteswritten;
}

/*
static void uart_isr(int srcno, void * aux)
Inputs: int srcno, 
        void * aux
Outputs: None
Description: Services the UART interrupts
Side effects: If data is available and receive buffer isn’t full:
– Store data from RBR to receive buffer.
3. If transmit buffer isn’t empty:
– Write from transmit buffer to THR.
4. Disable interrupts if:
(a) Receive buffer is full.
(b) Transmit buffer is empty.
Condition waits
*/
void uart_isr(int srcno, void * aux) {
    if (aux == NULL) {
        return;
    }


    //kprintf("ISR is fired\n");
    // FIXME your code goes here
    struct uart_serial * const uart = (struct uart_serial *)aux;

    if (srcno != uart->irqno) {
        return;
    }

    //1. Checking LSR for UART state
    //2. if data is avaible and recieve buffer isnt full
    //Store state from RBR to recieve buffer
    //so check LSR data ready bit which is bit zero
    /*For CP3, broadcast when:
(a) Receive buffer isn’t empty.
(b) Transmit buffer isn’t full.*/



    if (uart->regs->lsr & LSR_DR) {
        if (!(rbuf_full(&uart->rxbuf))) {
            rbuf_putc(&uart->rxbuf, uart->regs->rbr);
            //(a) Receive buffer isn’t empty.
            condition_broadcast(&uart->rxbnotempty);
        }

        //4a disable if recieve bufer if full
        if (rbuf_full(&uart->rxbuf)) {
            uart->regs->ier &= ~IER_DRIE;
        }
    }

    //3  transmit
    //THRE is 5th bit
    if (uart->regs->lsr & LSR_THRE) {
        if (!(rbuf_empty(&uart->txbuf))) {
            //write from transmit buffer to THR
            uart->regs->thr = rbuf_getc(&uart->txbuf);
            ///(b) Transmit buffer isn’t full.*/
            condition_broadcast(&uart->txbnotfull);
        }

        //4b disable interupts if tramsit buffer is empty
        if (rbuf_empty(&uart->txbuf)) {
            uart->regs->ier &= ~IER_THREIE;
        }
    }
}

void rbuf_init(struct ringbuf * rbuf) {
    rbuf->hpos = 0;
    rbuf->tpos = 0;
}



int rbuf_empty(const struct ringbuf * rbuf) {
    return (rbuf->hpos == rbuf->tpos);
}


int rbuf_full(const struct ringbuf * rbuf) {
    return (rbuf->tpos - rbuf->hpos == UART_RBUFSZ);
}


void rbuf_putc(struct ringbuf * rbuf, char c) {
    uint_fast16_t tpos;

    tpos = rbuf->tpos;
    rbuf->data[tpos % UART_RBUFSZ] = c;
    asm volatile ("" ::: "memory");
    rbuf->tpos = tpos + 1;
}

char rbuf_getc(struct ringbuf * rbuf) {
    uint_fast16_t hpos;
    char c;

    hpos = rbuf->hpos;
    c = rbuf->data[hpos % UART_RBUFSZ];
    asm volatile ("" ::: "memory");
    rbuf->hpos = hpos + 1;
    return c;
}

// The functions below provide polled uart input and output for the console.

#define UART0 (*(volatile struct uart_regs*)UART0_MMIO_BASE)

void console_device_init(void) {
    UART0.ier = 0x00;

    // Configure UART0. We set the baud rate divisor to 1, the lowest value,
    // for the fastest baud rate. In a physical system, the actual baud rate
    // depends on the attached oscillator frequency. In a virtualized system,
    // it doesn't matter.
    
    UART0.lcr = LCR_DLAB;
    UART0.dll = 0x01;
    UART0.dlm = 0x00;

    // The com0_putc and com0_getc functions assume DLAB=0.

    UART0.lcr = 0;
}

void console_device_putc(char c) {
    // Spin until THR is empty
    while (!(UART0.lsr & LSR_THRE))
        continue;

    UART0.thr = c;
}

char console_device_getc(void) {
    // Spin until RBR contains a byte
    while (!(UART0.lsr & LSR_DR))
        continue;
    
    return UART0.rbr;
}