/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

#define CORTEX_A5	0xc05
#define CORTEX_A8	0xc08
#define CORTEX_A9	0xc09
#define CORTEX_A15	0xc0f

static const char *pmnc_name;
static int pmnc_count;

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
	u32 zero = 0;
	u32 den = CCNT_REG;
	u32 val;

	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (den));	// disable
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));	// read
	asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (zero));	// zero
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (den));	// enable

	return val;
}

static inline u32 armv7_cntn_read(unsigned int cnt)
{
	u32 zero = 0;
	u32 sel = (cnt - CNT0);
	u32 den = 1 << sel;
	u32 val;

	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (den));	// disable
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (sel));	// select
	asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));	// read
    asm volatile("mcr p15, 0, %0, c9, c13, 2" : : "r" (zero));	// zero
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (den));	// enable

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
		val = CCNT_REG;
	else
		val = (1 << (cnt - CNT0));

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
		val = CCNT_REG;
	else
		val = (1 << (cnt - CNT0));

	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (val));

	return cnt;
}

static inline int armv7_pmnc_select_counter(unsigned int cnt)
{
	u32 val;

	if ((cnt == CCNT) || (cnt >= CNTMAX)) {
		pr_err("gator: CPU%u selecting wrong PMNC counter %d\n", smp_processor_id(), cnt);
		return -1;
	}

	val = (cnt - CNT0);
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (val));

	return cnt;
}

static inline void armv7_pmnc_write_evtsel(unsigned int cnt, u32 val)
{
	if (armv7_pmnc_select_counter(cnt) == cnt) {
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

static void gator_events_armv7_online(void)
{
	unsigned int cnt;
	int cpu = smp_processor_id();

	if (armv7_pmnc_read() & PMNC_E) {
		armv7_pmnc_write(armv7_pmnc_read() & ~PMNC_E);
	}

	// Initialize & Reset PMNC: C bit and P bit
	armv7_pmnc_write(PMNC_P | PMNC_C);

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		unsigned long event;

		per_cpu(perfPrev, cpu)[cnt] = 0;

		if (!pmnc_enabled[cnt])
			continue;

		// Disable counter
		armv7_pmnc_disable_counter(cnt);

		event = pmnc_event[cnt] & 255;

		// Set event (if destined for PMNx counters), we don't need to set the event if it's a cycle count
		if (cnt != CCNT)
			armv7_pmnc_write_evtsel(cnt, event);

		// Reset counter
		armv7_pmnc_reset_counter(cnt);

		// Enable counter, but do not enable interrupt for this counter
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
	int cpu = smp_processor_id();

	if (!pmnc_count)
		return 0;

	for (cnt = 0; cnt < pmnc_count; cnt++) {
		if (pmnc_enabled[cnt]) {
			int value;
			if (cnt == CCNT) {
				value = armv7_ccnt_read();
			} else {
				value = armv7_cntn_read(cnt);
			}
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

static struct gator_interface gator_events_armv7_interface = {
	.create_files = gator_events_armv7_create_files,
	.stop = gator_events_armv7_stop,
	.online = gator_events_armv7_online,
	.offline = gator_events_armv7_offline,
	.read = gator_events_armv7_read,
};

int gator_events_armv7_init(void)
{
	unsigned int cnt;

	switch (gator_cpuid()) {
	case CORTEX_A5:
		pmnc_name = "Cortex-A5";
		pmnc_count = 2;
		break;
	case CORTEX_A8:
		pmnc_name = "Cortex-A8";
		pmnc_count = 4;
		break;
	case CORTEX_A9:
		pmnc_name = "Cortex-A9";
		pmnc_count = 6;
		break;
	case CORTEX_A15:
		pmnc_name = "Cortex-A15";
		pmnc_count = 6;
		break;
	default:
		return -1;
	}

	pmnc_count++; // CNT[n] + CCNT

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
		pmnc_key[cnt] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_armv7_interface);
}
gator_events_init(gator_events_armv7_init);
