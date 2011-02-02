/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

#if defined(__arm__)

#define CORTEX_A8	0xc08
#define CORTEX_A9	0xc09

static const char *pmnc_name;
static int pmnc_count;

extern u32 gator_cpuid(void);

/*
 * Per-CPU PMNC: config reg
 */
#define PMNC_E		(1 << 0)	/* Enable all counters */
#define PMNC_P		(1 << 1)	/* Reset all counters */
#define PMNC_C		(1 << 2)	/* Cycle counter reset */
#define PMNC_D		(1 << 3)	/* CCNT counts every 64th cpu cycle */
#define PMNC_X		(1 << 4)	/* Export to ETM */
#define PMNC_DP		(1 << 5)	/* Disable CCNT if non-invasive debug*/
#define	PMNC_MASK	0x3f		/* Mask for writable bits */

/*
 * CNTENS: counters enable reg
 */
#define CNTENS_P0	(1 << 0)
#define CNTENS_P1	(1 << 1)
#define CNTENS_P2	(1 << 2)
#define CNTENS_P3	(1 << 3)
#define CNTENS_C	(1 << 31)
#define	CNTENS_MASK	0x8000000f	/* Mask for writable bits */

/*
 * CNTENC: counters disable reg
 */
#define CNTENC_P0	(1 << 0)
#define CNTENC_P1	(1 << 1)
#define CNTENC_P2	(1 << 2)
#define CNTENC_P3	(1 << 3)
#define CNTENC_C	(1 << 31)
#define	CNTENC_MASK	0x8000000f	/* Mask for writable bits */

/*
 * INTENS: counters overflow interrupt enable reg
 */
#define INTENS_P0	(1 << 0)
#define INTENS_P1	(1 << 1)
#define INTENS_P2	(1 << 2)
#define INTENS_P3	(1 << 3)
#define INTENS_C	(1 << 31)
#define	INTENS_MASK	0x8000000f	/* Mask for writable bits */

/*
 * EVTSEL: Event selection reg
 */
#define	EVTSEL_MASK	0x7f		/* Mask for writable bits */

/*
 * SELECT: Counter selection reg
 */
#define	SELECT_MASK	0x1f		/* Mask for writable bits */

/*
 * FLAG: counters overflow flag status reg
 */
#define FLAG_P0		(1 << 0)
#define FLAG_P1		(1 << 1)
#define FLAG_P2		(1 << 2)
#define FLAG_P3		(1 << 3)
#define FLAG_C		(1 << 31)
#define	FLAG_MASK	0x8000000f	/* Mask for writable bits */

#define CCNT 		0
#define CNT0		1
#define CNTMAX 		(6+1)

static unsigned long pmnc_enabled[CNTMAX];
static unsigned long pmnc_event[CNTMAX];
static unsigned long pmnc_key[CNTMAX];

static DEFINE_PER_CPU(int[CNTMAX], perfPrev);
static DEFINE_PER_CPU(int[CNTMAX * 2], perfCnt);

static inline void armv7_pmnc_write(u32 val)
{
	val &= PMNC_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r" (val));
}

static inline u32 armv7_pmnc_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
	return val;
}

static inline u32 armv7_ccnt_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
	return val;
}

static inline u32 armv7_cntn_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));
	return val;
}

static inline u32 armv7_pmnc_enable_counter(unsigned int cnt)
{
	u32 val;

	if (cnt >= CNTMAX) {
		pr_err("gator: CPU%u enabling wrong PMNC counter %d\n", smp_processor_id(), cnt);
		return -1;
	}

	if (cnt == CCNT)
		val = CNTENS_C;
	else
		val = (1 << (cnt - CNT0));

	val &= CNTENS_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (val));

	return cnt;
}

static inline u32 armv7_pmnc_disable_counter(unsigned int cnt)
{
	u32 val;

	if (cnt >= CNTMAX) {
		pr_err("gator: CPU%u disabling wrong PMNC counter %d\n", smp_processor_id(), cnt);
		return -1;
	}

	if (cnt == CCNT)
		val = CNTENC_C;
	else
		val = (1 << (cnt - CNT0));

	val &= CNTENC_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (val));

	return cnt;
}

static inline u32 armv7_pmnc_enable_intens(unsigned int cnt)
{
	u32 val;

	if (cnt >= CNTMAX) {
		pr_err("gator: CPU%u enabling wrong PMNC counter interrupt enable %d\n", smp_processor_id(), cnt);
		return -1;
	}

	if (cnt == CCNT)
		val = INTENS_C;
	else
		val = (1 << (cnt - CNT0));

	val &= INTENS_MASK;
	asm volatile("mcr p15, 0, %0, c9, c14, 1" : : "r" (val));

	return cnt;
}

static inline u32 armv7_pmnc_getreset_flags(void)
{
	u32 val;

	/* Read */
	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));

	/* Write to clear flags */
	val &= FLAG_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 3" : : "r" (val));

	return val;
}

static inline int armv7_pmnc_select_counter(unsigned int cnt)
{
	u32 val;

	if ((cnt == CCNT) || (cnt >= CNTMAX)) {
		pr_err("gator: CPU%u selecting wrong PMNC counter %d\n", smp_processor_id(), cnt);
		return -1;
	}

	val = (cnt - CNT0) & SELECT_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (val));

	return cnt;
}

static inline void armv7_pmnc_write_evtsel(unsigned int cnt, u32 val)
{
	if (armv7_pmnc_select_counter(cnt) == cnt) {
		val &= EVTSEL_MASK;
		asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r" (val));
	}
}

static void armv7_pmnc_reset_counter(unsigned int cnt)
{
	u32 val = 0;

	if (cnt == CCNT) {
		armv7_pmnc_disable_counter(cnt);

		asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (val));

		if (pmnc_enabled[cnt] != 0)
		    armv7_pmnc_enable_counter(cnt);

	} else if (cnt >= CNTMAX) {
		pr_err("gator: CPU%u resetting wrong PMNC counter %d\n", smp_processor_id(), cnt);
	} else {
		armv7_pmnc_disable_counter(cnt);

		if (armv7_pmnc_select_counter(cnt) == cnt)
		    asm volatile("mcr p15, 0, %0, c9, c13, 2" : : "r" (val));

		if (pmnc_enabled[cnt] != 0)
		    armv7_pmnc_enable_counter(cnt);
	}
}

static int gator_events_armv7_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;

	for (i = 0; i < pmnc_count; i++) {
		char buf[40];
		if (i == 0) {
			snprintf(buf, sizeof buf, "ARM_%s_ccnt", pmnc_name);
		} else {
			snprintf(buf, sizeof buf, "ARM_%s_cnt%d", pmnc_name, i-1);
		}
		dir = gatorfs_mkdir(sb, root, buf);
		if (!dir) {
			return -1;
		}
		gatorfs_create_ulong(sb, dir, "enabled", &pmnc_enabled[i]);
		if (i > 0) {
			gatorfs_create_ulong(sb, dir, "event", &pmnc_event[i]);
		}
		gatorfs_create_ro_ulong(sb, dir, "key", &pmnc_key[i]);
	}

	return 0;
}

static int gator_events_armv7_init(int *key)
{
	unsigned int cnt;

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
		pmnc_key[cnt] = *key;
		*key = *key + 1;
	}

	return 0;
}

static void gator_events_armv7_online(void)
{
	unsigned int cnt;

	if (armv7_pmnc_read() & PMNC_E) {
		armv7_pmnc_write(armv7_pmnc_read() & ~PMNC_E);
	}

	/* Initialize & Reset PMNC: C bit and P bit */
	armv7_pmnc_write(PMNC_P | PMNC_C);

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		unsigned long event;

		per_cpu(perfPrev, raw_smp_processor_id())[cnt] = 0;

		if (!pmnc_enabled[cnt])
			continue;

		/*
		 * Disable counter
		 */
		armv7_pmnc_disable_counter(cnt);

		event = pmnc_event[cnt] & 255;

		/*
		 * Set event (if destined for PMNx counters)
		 * We don't need to set the event if it's a cycle count
		 */
		if (cnt != CCNT)
			armv7_pmnc_write_evtsel(cnt, event);

		/*
		 * [Do not] Enable interrupt for this counter
		 */
		/* armv7_pmnc_enable_intens(cnt); */

		/*
		 * Reset counter
		 */
		armv7_pmnc_reset_counter(cnt);

		/*
		 * Enable counter
		 */
		armv7_pmnc_enable_counter(cnt);
	}

	// enable
	armv7_pmnc_write(armv7_pmnc_read() | PMNC_E);
}

static void gator_events_armv7_offline(void)
{
	armv7_pmnc_write(armv7_pmnc_read() & ~PMNC_E);
}

static void gator_events_armv7_stop(void)
{
	unsigned int cnt;

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
	}
}

static int gator_events_armv7_read(int **buffer)
{
	int cnt, len = 0;
	int cpu = raw_smp_processor_id();

	if (!pmnc_count)
		return 0;

	armv7_pmnc_getreset_flags();
	for (cnt = 0; cnt < pmnc_count; cnt++) {
		if (pmnc_enabled[cnt]) {
			int value;
			if (cnt == CCNT) {
				value = armv7_ccnt_read();
			} else if (armv7_pmnc_select_counter(cnt) == cnt) {
				value = armv7_cntn_read();
			} else {
				value = 0;
			}
			armv7_pmnc_reset_counter(cnt);
			if (value != per_cpu(perfPrev, cpu)[cnt]) {
				per_cpu(perfPrev, cpu)[cnt] = value;
				per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
				per_cpu(perfCnt, cpu)[len++] = value;
			}
		}
	}

	// update or discard
	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}
#endif

int gator_events_armv7_install(gator_interface *gi) {
#if defined(__arm__)
	switch (gator_cpuid()) {
	case CORTEX_A8:
		pmnc_name = "Cortex-A8";
		pmnc_count = 4;
		break;
	case CORTEX_A9:
		pmnc_name = "Cortex-A9";
		pmnc_count = 6;
		break;
	default:
		return -1;
	}

	pmnc_count++; // CNT[n] + CCNT

	gi->create_files = gator_events_armv7_create_files;
	gi->init = gator_events_armv7_init;
	gi->stop = gator_events_armv7_stop;
	gi->online = gator_events_armv7_online;
	gi->offline = gator_events_armv7_offline;
	gi->read = gator_events_armv7_read;
#endif
	return 0;
}
