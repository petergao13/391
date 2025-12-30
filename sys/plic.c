// plic.c - RISC-V PLIC
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#ifdef PLIC_TRACE
#define TRACE
#endif

#ifdef PLIC_DEBUG
#define DEBUG
#endif

#include "conf.h"
#include "plic.h"
#include "misc.h"

#include <stdint.h>

// INTERNAL MACRO DEFINITIONS
//

// CTX(i,0) is hartid /i/ M-mode context
// CTX(i,1) is hartid /i/ S-mode context

#define CTX(i,s) (2*(i)+(s))

// INTERNAL TYPE DEFINITIONS
// 


struct plic_regs {
	union {
		uint32_t priority[PLIC_SRC_CNT]; /**< Interrupt Priorities registers */
		char _reserved_priority[0x1000];
	};

	union {
		uint32_t pending[PLIC_SRC_CNT/32]; /**< Interrupt Pending Bits registers */
		char _reserved_pending[0x1000];
	};

	union {
		uint32_t enable[PLIC_CTX_CNT][32]; /**< Interrupt Enables registers */
		char _reserved_enable[0x200000-0x2000];
	};

	struct {
		union {
			struct {
				uint32_t threshold;	/**< Priority Thresholds registers */
				uint32_t claim;	/**< Interrupt Claim/Completion registers */
			};
			
			char _reserved_ctxctl[0x1000];
		};
	} ctx[PLIC_CTX_CNT];
};

#define PLIC (*(volatile struct plic_regs*)PLIC_MMIO_BASE)

// INTERNAL FUNCTION DECLARATIONS
//

static void plic_set_source_priority (
	uint_fast32_t srcno, uint_fast32_t level);

static int plic_source_pending(uint_fast32_t srcno);

static void plic_enable_source_for_context (
	uint_fast32_t ctxno, uint_fast32_t srcno);

static void plic_disable_source_for_context (
	uint_fast32_t ctxno, uint_fast32_t srcno);

static void plic_set_context_threshold (
	uint_fast32_t ctxno, uint_fast32_t level);

static uint_fast32_t plic_claim_context_interrupt (
	uint_fast32_t ctxno);

static void plic_complete_context_interrupt (
	uint_fast32_t ctxno, uint_fast32_t srcno);


static void plic_enable_all_sources_for_context(uint_fast32_t ctxno);

static void plic_disable_all_sources_for_context(uint_fast32_t ctxno);

// We currently only support single-hart operation, sending interrupts to S mode
// on hart 0 (context 0). The low-level PLIC functions already understand
// contexts, so we only need to modify the high-level functions (plit_init,
// plic_claim_request, plic_finish_request)to add support for multiple harts.

// EXPORTED FUNCTION DEFINITIONS
// 

void plic_init(void) {
	int i;

	// Disable all sources by setting priority to 0

	for (i = 0; i < PLIC_SRC_CNT; i++)
		plic_set_source_priority(i, 0);
	
	// Route all sources to S mode on hart 0 only

	for (int i = 0; i < PLIC_CTX_CNT; i++)
		plic_disable_all_sources_for_context(i);
	
	plic_enable_all_sources_for_context(CTX(0,1));
}

extern void plic_enable_source(int srcno, int prio) {
	trace("%s(srcno=%d,prio=%d)", __func__, srcno, prio);
	assert (0 < srcno && srcno <= PLIC_SRC_CNT);
	assert (prio > 0);

	plic_set_source_priority(srcno, prio);
}

extern void plic_disable_source(int irqno) {
	if (0 < irqno)
		plic_set_source_priority(irqno, 0);
	else
		debug("plic_disable_irq called with irqno = %d", irqno);
}

extern int plic_claim_interrupt(void) {
	trace("%s()", __func__);
	return plic_claim_context_interrupt(CTX(0,1));
}

extern void plic_finish_interrupt(int irqno) {
	trace("%s(irqno=%d)", __func__, irqno);
	plic_complete_context_interrupt(CTX(0,1), irqno);
}

// INTERNAL FUNCTION DEFINITIONS
//

/*
static inline void plic_set_source_priority(uint_fast32_t srcno, uint_fast32_t level)
Inputs: uint_fast32_t srcno
		uint_fast32_t level
Outputs: None
Description: Set the priority level of an interrupt source
Side effects: Changes the priority array
*/
static inline void plic_set_source_priority(uint_fast32_t srcno, uint_fast32_t level) {
	// FIXME your code goes here
	//check if source is zero? src number 0 does not exist, so if it doesn't exist just do nothing
	if (srcno != 0) {
		PLIC.priority[srcno] = level;
	}
}

/*
static inline int plic_source_pending(uint_fast32_t srcno)
Inputs: uint_fast32_t srcno
Outputs: int, 1 if pending, 0 if not
Description: Check if an interrupt source is pending, returns 1 if pending, 0 if not
Side effects: None
*/
static inline int plic_source_pending(uint_fast32_t srcno) {
	// FIXME your code goes here
	if (srcno == 0) {
		return 0;
	} 

	//need to check the specific bit within the 32 bits
	//just check the value in the pending array at the inputted source. if its 1 return 1, else return 0
	else {
		uint_fast32_t bit = srcno%32; //the specific bit
		uint_fast32_t ret = PLIC.pending[srcno/32] & (1u << bit); //check the specific bit in the reg by masking. 
		if (ret > 0) { //if there is a single bit that is high, that means theres a pending bit
			return 1;
		} else {
			return 0;
		}
	}
}

/*
static inline void plic_enable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcno)
Inputs: uint_fast32_t ctxno, 
		uint_fast32_t srcno
Outputs: None
Description: Enables a interrupt source for context
Side effects: Calculates and modifys the corresponding bit in the enable array
*/
static inline void plic_enable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcno) {
	// FIXME your code goes here
	if (srcno == 0) {
		return;
	}
	//ok calculate the index. divided into 32 bit registers
	//this format uint32_t enable[PLIC_CTX_CNT][32]
	//needa set the sepcific bit to 1 and leave the other 32 bits untouched, use mask?
	else {
		uint_fast32_t bit = (1u << (srcno%32));
		PLIC.enable[ctxno][srcno/32] |= bit; 
	}
}

/*
static inline void plic_disable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcid)
Inputs: uint_fast32_t ctxno, 
		uint_fast32_t srcid
Outputs: None
Description: Disables a interrupt source for context
Side effects: Calculates and clears the corresponding bit in the enable array
*/
static inline void plic_disable_source_for_context(uint_fast32_t ctxno, uint_fast32_t srcid) {
	// FIXME your code goes here
	if (srcid == 0) {
		return;
	}
	//ok calculate the index. divided into 32 bit registers
	//this format uint32_t enable[PLIC_CTX_CNT][32]
	//needa set the sepcific bit to 0 and leave the other 32 bits untouched, use mask?
	else {
		uint_fast32_t bit = ~(1u << (srcid%32)); //invert to set the bit we want to becmoe zero and rest 1, then can just AND
		PLIC.enable[ctxno][srcid/32] &= bit; 
	}
}

/*
static inline void plic_set_context_threshold(uint_fast32_t ctxno, uint_fast32_t level)
Inputs: uint_fast32_t ctxno, 
		uint_fast32_t level
Outputs: None
Description: Sets interrupt prioirty level for a context
Side effects: Moddifys the threshold level
*/
static inline void plic_set_context_threshold(uint_fast32_t ctxno, uint_fast32_t level) {
	// FIXME your code goes here
	PLIC.ctx[ctxno].threshold = level;
}

/*
static static inline uint_fast32_t plic_claim_context_interrupt(uint_fast32_t ctxno) 
Inputs: uint_fast32_t ctxno, 
Outputs: int, interrupt id
Description: Claim an interrupt for a given context
Side effects: None
*/
static inline uint_fast32_t plic_claim_context_interrupt(uint_fast32_t ctxno) {
	// FIXME your code goes here
	//read threshold, if threshold is zero then return 0
	/*On receiving a claim message, the PLIC core will atomically determine the ID of the highest-priority pending interrupt for the target 
	and then clear down the corresponding source’s IP bit. The PLIC core will then return the ID to the
	target. The PLIC core will return an ID of zero, if there were no pending interrupts for the target
	when the claim was serviced. */ 
	//so auto handles if there are no interrupts
	return PLIC.ctx[ctxno].claim; 
}

/*
static inline void plic_complete_context_interrupt(uint_fast32_t ctxno, uint_fast32_t srcno)
Inputs: uint_fast32_t ctxno, 
		uint_fast32_t srcno
Outputs: None
Description: Complete the handling of an interrupt for a given context
Side effects: Writes back to the claim register the source number
*/
static inline void plic_complete_context_interrupt(uint_fast32_t ctxno, uint_fast32_t srcno) {
	// FIXME your code goes here
	if (srcno == 0) {
		return;
	} else {
		PLIC.ctx[ctxno].claim = srcno; 
		//how do i  notify the plic that interrupt has been serviced? is it auto
	}
}

/*
static void plic_enable_all_sources_for_context(uint_fast32_t ctxno)
Inputs: uint_fast32_t ctxno, 
Outputs: None
Description: Enable all interrupt sources for a given context
Side effects: Modifys the enable array
*/
static void plic_enable_all_sources_for_context(uint_fast32_t ctxno) {
	// FIXME your code goes here
	for (int i = 0; i < PLIC_SRC_CNT/32; ++i) {
		PLIC.enable[ctxno][i] = 0xFFFFFFFF;
	}
}

/*
static void plic_disable_all_sources_for_context(uint_fast32_t ctxno)
Inputs: uint_fast32_t ctxno, 
Outputs: None
Description: Disables all interrupt sources for a given context
Side effects: Modifys the enable array
*/
static void plic_disable_all_sources_for_context(uint_fast32_t ctxno) {
	// FIXME your code goes here
	for (int i = 0; i < PLIC_SRC_CNT/32; ++i) {
		PLIC.enable[ctxno][i] = 0x00000000;
	}
}