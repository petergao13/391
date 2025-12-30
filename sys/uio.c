/*! @file uio.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‍‌​​‌​⁠⁠‌​⁠⁠‌
    @brief Uniform I/O interface
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifdef UIO_DEBUG
#define DEBUG
#endif

#ifdef UIO_TRACE
#define TRACE
#endif

#include "uio.h"

#include <stddef.h>  // for NULL and offsetof

#include "error.h"
#include "heap.h"
#include "memory.h"
#include "misc.h"
#include "string.h"
#include "thread.h"
#include "uioimpl.h"

static void nulluio_close(struct uio* uio);

static long nulluio_read(struct uio* uio, void* buf, unsigned long bufsz);

static long nulluio_write(struct uio* uio, const void* buf, unsigned long buflen);

// INTERNAL GLOBAL VARIABLES AND CONSTANTS
//

struct ringbuf {
    unsigned long long hpos; // head of queue (from where elements are removed)
    unsigned long long tpos; // tail of queue (where elements are inserted)
    char * data;
};

struct pipe {
    struct uio read_uio;
    struct uio write_uio;
    
    struct ringbuf rbuf;
    
    struct condition read_condition;
    struct condition write_condition;

    struct lock lock;
};

// helper functions
static void pipe_close(struct uio* uio);
static long pipe_read(struct uio* uio, void* buf, unsigned long bufsz);
static long  pipe_write(struct uio* uio, const void* buf, unsigned long bufsz);
// static void pipe_cntl(struct uio* uio, int op, void* arg);

int rbuf_empty(const struct ringbuf * rbuf);
int rbuf_full(const struct ringbuf * rbuf);
void rbuf_putc(struct ringbuf * rbuf, char c);
char rbuf_getc(struct ringbuf * rbuf);

// interfaces
static const struct uio_intf pipe_read_intf = {
    .close = &pipe_close,
    .read = &pipe_read,
    .write = NULL,
    .cntl = NULL
};

static const struct uio_intf pipe_write_intf = {
    .close = &pipe_close,
    .read = NULL,
    .write = &pipe_write,
    .cntl = NULL
};

void uio_close(struct uio* uio) {
    debug("uio_close: refcnt=%d, has_close=%d", uio->refcnt, (uio->intf->close != NULL));

    // Decrement reference count if it's greater than 0
    if (uio->refcnt > 0) {
        uio->refcnt--;
        debug("uio_close: decremented refcnt to %d", uio->refcnt);
    }

    // Only call the actual close method when refcnt reaches 0
    if (uio->refcnt == 0 && uio->intf->close != NULL) {
        debug("uio_close: calling close method");
        uio->intf->close(uio);
    } else if (uio->refcnt > 0) {
        debug("uio_close: NOT calling close (refcnt=%d still has references)", uio->refcnt);
    }
}

long uio_read(struct uio* uio, void* buf, unsigned long bufsz) {
    if (uio->intf->read != NULL) {
        if (0 <= (long)bufsz)
            return uio->intf->read(uio, buf, bufsz);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

long uio_write(struct uio* uio, const void* buf, unsigned long buflen) {
    if (uio->intf->write != NULL) {
        if (0 <= (long)buflen)
            return uio->intf->write(uio, buf, buflen);
        else
            return -EINVAL;
    } else
        return -ENOTSUP;
}

int uio_cntl(struct uio* uio, int op, void* arg) {
    if (uio->intf->cntl != NULL)
        return uio->intf->cntl(uio, op, arg);
    else
        return -ENOTSUP;
}

unsigned long uio_refcnt(const struct uio* uio) {
    assert(uio != NULL);
    return uio->refcnt;
}

int uio_addref(struct uio* uio) { return ++uio->refcnt; }

struct uio* create_null_uio(void) {
    static const struct uio_intf nulluio_intf = {
        .close = &nulluio_close, .read = &nulluio_read, .write = &nulluio_write};

    static struct uio nulluio = {.intf = &nulluio_intf, .refcnt = 0};

    return &nulluio;
}

static void nulluio_close(struct uio* uio) {
    // ...
}

static long nulluio_read(struct uio* uio, void* buf, unsigned long bufsz) {
    // ...
    return -ENOTSUP;
}

static long nulluio_write(struct uio* uio, const void* buf, unsigned long buflen) {
    // ...
    return -ENOTSUP;
}

/*
Creates a unidirectional pipe.

Allocates memory for the pipe struct and initializes all necessary parts for the pipe.
You will have to modify the passed in pointers to point to the relevant pipe io interfaces.
For example, you will have to create a pipe write interface that will be referenced by the wioptr.
For this we also have intentionally chosen to give you freedom with how your pipes implementation works internally.
The following are additional details regarding implementation.
You should use a simple buffer of PAGE_SIZE that you can allocate using your alloc_phys_page function.
We recommend head and tail pointers to keep track of read and write positions.
This buffer is the channel in which we write based on the tail and read based on the head.
You will also have to use condition variables to have the reader signal the writer and vice versa.
This signaling has to be done on updates to the buffer so that the other can properly respond.
This signaling is necessary because the reader and writer must sleep while they are waiting for the others actions.
You should note that the reader should only be forced to wait if a writer exists, and a writer should only be forced to wait if a reader exists.
When closing the reader or writer you should also free the buffer if it is possible (consider why it would not always be possible).
This implementation facilitates one-way communication, allowing messages to be sent from one program to another, kind of like a mailbox.

Parameters
wptr	Double pointer to return write uio struct to caller
rptr	Double pointer to return read uio struct to caller
Returns
None
*/
void create_pipe(struct uio ** wptr, struct uio ** rptr) {
    struct pipe * p = kcalloc(1, sizeof(struct pipe));
    
    p->rbuf.hpos = 0; //head
    p->rbuf.tpos = 0; //tail
    p->rbuf.data = alloc_phys_page();

    //intialize the conditions
    condition_init(&p->read_condition, "pipe read");
    condition_init(&p->write_condition, "pipe write");

    //initialize the uios
    uio_init1(&p->read_uio, &pipe_read_intf);
    uio_init1(&p->write_uio, &pipe_write_intf);

    //initilizte the lock
    lock_init(&p->lock);

    //return new uio pointers
    *wptr = &p->write_uio;
    *rptr = &p->read_uio;
}

static void pipe_close(struct uio* uio){
    struct pipe * p;
    if(uio->intf == &pipe_read_intf){
        p = (void*)uio - offsetof(struct pipe, read_uio);
    }
    else if(uio->intf == &pipe_write_intf){
        p = (void*)uio - offsetof(struct pipe, write_uio);
    }
    else{
        return;
    }

    lock_acquire(&p->lock);
    condition_broadcast(&p->write_condition);
    condition_broadcast(&p->read_condition);
    lock_release(&p->lock);

    if(p->read_uio.refcnt == 0 && p->write_uio.refcnt == 0){
        free_phys_page(p->rbuf.data);
        kfree(p);
    }
}

static long pipe_read(struct uio* uio, void* buf, unsigned long bufsz){
    if (uio == NULL || buf == NULL) {
        return -EINVAL;
    }

    struct pipe * const p = (void*)uio - offsetof(struct pipe, read_uio);

    if (bufsz == 0) {
        return 0;
    }

    lock_acquire(&p->lock);
    
    while(rbuf_empty(&p->rbuf)){
        if(p->write_uio.refcnt == 0){
            lock_release(&p->lock);
            return 0;
        }
        lock_release(&p->lock);
        condition_wait(&p->read_condition);
        lock_acquire(&p->lock);
    }
    
    unsigned long long read = 0;
    while(!rbuf_empty(&p->rbuf) && read < bufsz){
        ((char *)buf)[read] = rbuf_getc(&p->rbuf);
        read++;
    }

    condition_broadcast(&p->write_condition);
    lock_release(&p->lock);
    return read;
}

static long pipe_write(struct uio* uio, const void* buf, unsigned long bufsz){
    if (uio == NULL || buf == NULL) {
        return -EINVAL;
    }

    struct pipe * const p = (void*)uio - offsetof(struct pipe, write_uio);

    if (bufsz == 0) {
        return 0;
    }

    lock_acquire(&p->lock);
    
    while(rbuf_full(&p->rbuf)){
        if(p->read_uio.refcnt == 0){
            lock_release(&p->lock);
            return -EPIPE;
        }
        lock_release(&p->lock);
        condition_wait(&p->write_condition);
        lock_acquire(&p->lock);
    }

    if(p->read_uio.refcnt == 0){
        lock_release(&p->lock);
        return -EPIPE;
    }   
    
    unsigned long long written = 0;
    while(!rbuf_full(&p->rbuf) && written < bufsz){
        rbuf_putc(&p->rbuf, ((char*)buf)[written]);
        written++;
    }

    condition_broadcast(&p->read_condition);
    lock_release(&p->lock);
    return written;
}

int rbuf_empty(const struct ringbuf * rbuf) {
    return (rbuf->hpos == rbuf->tpos);
}


int rbuf_full(const struct ringbuf * rbuf) {
    return (rbuf->tpos - rbuf->hpos == PAGE_SIZE);
}

void rbuf_putc(struct ringbuf * rbuf, char c) {
    uint_fast16_t tpos = rbuf->tpos;
    rbuf->data[tpos % PAGE_SIZE] = c;
    rbuf->tpos = tpos + 1;
}

char rbuf_getc(struct ringbuf * rbuf) {
    uint_fast16_t hpos = rbuf->hpos;
    char c = rbuf->data[hpos % PAGE_SIZE];
    rbuf->hpos = hpos + 1;
    return c;
}