/*
 * Example events provider
 *
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Similar entires must be present in events.xml file:
 *
 * <counter_set name="mmaped_cntX">
 *   <counter name="mmaped_cnt0"/>
 *   <counter name="mmaped_cnt1"/>
 * </counter_set>
 * <category name="mmaped" counter_set="mmaped_cntX" per_cpu="no">
 *   <event event="0x0" title="Simulated" name="Sine" description="Sort-of-sine"/>
 *   <event event="0x1" title="Simulated" name="Triangle" description="Triangular wave"/>
 *   <event event="0x2" title="Simulated" name="PWM" description="PWM Signal"/>
 * </category>
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/ratelimit.h>

#include "gator.h"

#define MMAPED_COUNTERS_NUM 3

static struct {
	unsigned long enabled;
	unsigned long event;
	unsigned long key;
} mmaped_counters[MMAPED_COUNTERS_NUM];

static int mmaped_buffer[MMAPED_COUNTERS_NUM * 2];

#ifdef TODO
static void __iomem *mmaped_base;
#endif

/* Adds mmaped_cntX directories and enabled, event, and key files to /dev/gator/events */
static int gator_events_mmaped_create_files(struct super_block *sb,
		struct dentry *root)
{
	int i;

	for (i = 0; i < MMAPED_COUNTERS_NUM; i++) {
		char buf[16];
		struct dentry *dir;

		snprintf(buf, sizeof(buf), "mmaped_cnt%d", i);
		dir = gatorfs_mkdir(sb, root, buf);
		if (WARN_ON(!dir))
			return -1;
		gatorfs_create_ulong(sb, dir, "enabled",
				&mmaped_counters[i].enabled);
		gatorfs_create_ulong(sb, dir, "event",
				&mmaped_counters[i].event);
		gatorfs_create_ro_ulong(sb, dir, "key",
				&mmaped_counters[i].key);
	}

	return 0;
}

static int gator_events_mmaped_start(void)
{
#ifdef TODO
	for (i = 0; i < MMAPED_COUNTERS_NUM; i++)
		writel(mmaped_counters[i].event,
				mmaped_base + COUNTERS_CONFIG_OFFSET[i]);

	writel(ENABLED, COUNTERS_CONTROL_OFFSET);
#endif

	return 0;
}

static void gator_events_mmaped_stop(void)
{
#ifdef TODO
	writel(DISABLED, COUNTERS_CONTROL_OFFSET);
#endif
}

#ifndef TODO
/* This function "simulates" counters, generating values of fancy
 * functions like sine or triangle... */
static int mmaped_simulate(int counter)
{
	int result = 0;

	switch (counter) {
	case 0: /* sort-of-sine */
		{
			static int t;
			int x;

			if (t % 1024 < 512)
				x = 512 - (t % 512);
			else
				x = t % 512;

			result = 32 * x / 512;
			result = result * result;

			if (t < 1024)
				result = 1922 - result;

			t = (t + 1) % 2048;
		}
		break;
	case 1: /* triangle */
		{
			static int v, d = 1;

			v += d;
			if (v % 2000 == 0)
				d = -d;

			result = v;
		}
		break;
	case 2: /* PWM signal */
		{
			static int t, dc;

			t = (t + 1) % 2000;
			if (t % 100)
				dc = (dc + 200) % 2000;
			
			result = t < dc ? 0 : 2000;
		}
		break;
	}

	return result;
}
#endif

static int gator_events_mmaped_read(int **buffer)
{
	int i;
	int len = 0;

	/* System wide counters - read from one core only */
	if (smp_processor_id())
		return 0;

	for (i = 0; i < MMAPED_COUNTERS_NUM; i++) {
		if (mmaped_counters[i].enabled) {
			mmaped_buffer[len++] = mmaped_counters[i].key;
#ifdef TODO
			mmaped_buffer[len++] = readl(mmaped_base +
					COUNTERS_VALUE_OFFSET[i]);
#else
			mmaped_buffer[len++] = mmaped_simulate(
					mmaped_counters[i].event);
#endif
		}
	}
	
	if (buffer)
		*buffer = mmaped_buffer;

	return len;
}

static struct gator_interface gator_events_mmaped_interface = {
	.create_files = gator_events_mmaped_create_files,
	.start = gator_events_mmaped_start,
	.stop = gator_events_mmaped_stop,
	.read = gator_events_mmaped_read,
};

/* Must not be static! */
int __init gator_events_mmaped_init(void)
{
	int i;

#ifdef TODO
	mmaped_base = ioremap(COUNTERS_PHYS_ADDR, SZ_4K);
	if (!mmaped_base)
		return -ENOMEM;	
#endif

	for (i = 0; i < MMAPED_COUNTERS_NUM; i++) {
		mmaped_counters[i].enabled = 0;
		mmaped_counters[i].key = gator_events_get_key();
	}

	return gator_events_install(&gator_events_mmaped_interface);
}
gator_events_init(gator_events_mmaped_init);
