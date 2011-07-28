/**
 * PL310 (L2 Cache Controller) event counters for gator
 *
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <asm/hardware/cache-l2x0.h>

#include "gator.h"

#define PL310_COUNTERS_NUM 2

static struct {
	unsigned long enabled;
	unsigned long event;
	unsigned long key;
} pl310_counters[PL310_COUNTERS_NUM];

static int pl310_buffer[PL310_COUNTERS_NUM * 2];

static void __iomem *pl310_base;



static void gator_events_pl310_reset_counters(void)
{
	u32 val = readl(pl310_base + L2X0_EVENT_CNT_CTRL);

	val |= ((1 << PL310_COUNTERS_NUM) - 1) << 1;

	writel(val, pl310_base + L2X0_EVENT_CNT_CTRL);
}


static int gator_events_pl310_create_files(struct super_block *sb,
		struct dentry *root)
{
	int i;

	for (i = 0; i < PL310_COUNTERS_NUM; i++) {
		char buf[16];
		struct dentry *dir;

		snprintf(buf, sizeof(buf), "PL310_cnt%d", i);
		dir = gatorfs_mkdir(sb, root, buf);
		if (WARN_ON(!dir))
			return -1;
		gatorfs_create_ulong(sb, dir, "enabled",
				&pl310_counters[i].enabled);
		gatorfs_create_ulong(sb, dir, "event",
				&pl310_counters[i].event);
		gatorfs_create_ro_ulong(sb, dir, "key",
				&pl310_counters[i].key);
	}

	return 0;
}

static int gator_events_pl310_start(void)
{
	static const unsigned long l2x0_event_cntx_cfg[PL310_COUNTERS_NUM] = {
		L2X0_EVENT_CNT0_CFG,
		L2X0_EVENT_CNT1_CFG,
	};
	int i;

	/* Counter event sources */
	for (i = 0; i < PL310_COUNTERS_NUM; i++)
		writel((pl310_counters[i].event & 0xf) << 2,
				pl310_base + l2x0_event_cntx_cfg[i]);

	gator_events_pl310_reset_counters();

	/* Event counter enable */
	writel(1, pl310_base + L2X0_EVENT_CNT_CTRL);

	return 0;
}

static void gator_events_pl310_stop(void)
{
	/* Event counter disable */
	writel(0, pl310_base + L2X0_EVENT_CNT_CTRL);
}

static int gator_events_pl310_read(int **buffer)
{
	static const unsigned long l2x0_event_cntx_val[PL310_COUNTERS_NUM] = {
		L2X0_EVENT_CNT0_VAL,
		L2X0_EVENT_CNT1_VAL,
	};
	int i;
	int len = 0;

	if (smp_processor_id())
		return 0;

	for (i = 0; i < PL310_COUNTERS_NUM; i++) {
		if (pl310_counters[i].enabled) {
			pl310_buffer[len++] = pl310_counters[i].key;
			pl310_buffer[len++] = readl(pl310_base +
					l2x0_event_cntx_val[i]);
		}
	}
	
	/* PL310 counters are saturating, not wrapping in case of overflow */
	gator_events_pl310_reset_counters();

	if (buffer)
		*buffer = pl310_buffer;

	return len;
}

static struct gator_interface gator_events_pl310_interface = {
	.create_files = gator_events_pl310_create_files,
	.start = gator_events_pl310_start,
	.stop = gator_events_pl310_stop,
	.read = gator_events_pl310_read,
};

static void __maybe_unused gator_events_pl310_probe(unsigned long phys)
{
	if (pl310_base)
		return;

	pl310_base = ioremap(phys, SZ_4K);
	if (pl310_base) {
		u32 cache_id = readl(pl310_base + L2X0_CACHE_ID);

		if ((cache_id & 0xff0003c0) != 0x410000c0) {
			iounmap(pl310_base);
			pl310_base = NULL;
		}
	}
}

int gator_events_pl310_init(void)
{
	int i;

#if defined(CONFIG_ARCH_EXYNOS4)
	gator_events_pl310_probe(0xfe600000);
#endif
#if defined(CONFIG_ARCH_OMAP4)
	gator_events_pl310_probe(0x48242000);
#endif
#if defined(CONFIG_ARCH_TEGRA)
	gator_events_pl310_probe(0x50043000);
#endif
#if defined(CONFIG_ARCH_U8500)
	gator_events_pl310_probe(0xa0412000);
#endif
#if defined(CONFIG_ARCH_VEXPRESS) && !defined(CONFIG_ARCH_VEXPRESS_CA15X4)
	// A9x4 core tile (HBI-0191)
	gator_events_pl310_probe(0x1e00a000);
	// New memory map tiles
	gator_events_pl310_probe(0x2c0f0000);
#endif
	if (!pl310_base)
		return -1;

	for (i = 0; i < PL310_COUNTERS_NUM; i++) {
		pl310_counters[i].enabled = 0;
		pl310_counters[i].key = gator_events_get_key();
	}

	return gator_events_install(&gator_events_pl310_interface);
}
gator_events_init(gator_events_pl310_init);
