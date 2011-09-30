/**
 * Copyright (C) ARM Limited 2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GATOR_EVENTS_ARMV7_H_
#define GATOR_EVENTS_ARMV7_H_

// Per-CPU PMNC: config reg
#define PMNC_E		(1 << 0)	/* Enable all counters */
#define PMNC_P		(1 << 1)	/* Reset all counters */
#define PMNC_C		(1 << 2)	/* Cycle counter reset */
#define	PMNC_MASK	0x3f		/* Mask for writable bits */

// ccnt reg
#define CCNT_REG	(1 << 31)

#define CCNT 		0
#define CNT0		1
#define CNTMAX 		(6+1)

// Function prototypes
extern void armv7_pmnc_write(u32 val);
extern u32 armv7_pmnc_read(void);
extern u32 armv7_ccnt_read(u32 reset_value);
extern u32 armv7_cntn_read(unsigned int cnt, u32 reset_value);
extern u32 armv7_pmnc_reset_interrupt(void);

// Externed variables
extern unsigned long pmnc_enabled[CNTMAX];
extern unsigned long pmnc_event[CNTMAX];
extern unsigned long pmnc_count[CNTMAX];
extern unsigned long pmnc_key[CNTMAX];

#endif // GATOR_EVENTS_ARMV7_H_
