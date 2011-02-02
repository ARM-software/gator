/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <trace/events/block.h>

#define BLOCK_RQ_WR		0
#define BLOCK_RQ_RD		1

#define BLOCK_TOTAL		(BLOCK_RQ_RD+1)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
#define EVENTWRITE REQ_RW
#else
#define EVENTWRITE REQ_WRITE
#endif

static ulong block_rq_wr_enabled;
static ulong block_rq_rd_enabled;
static ulong block_rq_wr_key;
static ulong block_rq_rd_key;
static DEFINE_PER_CPU(int[BLOCK_TOTAL], blockCnt);
static DEFINE_PER_CPU(int[BLOCK_TOTAL * 2], blockGet);
static DEFINE_PER_CPU(bool, new_data_avail);

GATOR_DEFINE_PROBE(block_rq_complete, TP_PROTO(struct request_queue *q, struct request *rq))
{
	unsigned long flags;
	int write, size;
	int cpu = raw_smp_processor_id();

	if (!rq)
		return;

	write = rq->cmd_flags & EVENTWRITE;
	size = rq->resid_len;

	if (!size)
		return;

	// disable interrupts to synchronize with gator_events_block_read()
	// spinlocks not needed since percpu buffers are used
	local_irq_save(flags);
	if (write)
		per_cpu(blockCnt, cpu)[BLOCK_RQ_WR] += size;
	else
		per_cpu(blockCnt, cpu)[BLOCK_RQ_RD] += size;
	local_irq_restore(flags);

	per_cpu(new_data_avail, cpu) = true;
}

static int gator_events_block_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	/* block_complete_wr */
	dir = gatorfs_mkdir(sb, root, "Linux_block_rq_wr");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &block_rq_wr_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &block_rq_wr_key);

	/* block_complete_rd */
	dir = gatorfs_mkdir(sb, root, "Linux_block_rq_rd");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &block_rq_rd_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &block_rq_rd_key);

	return 0;
}

static int gator_events_block_init(int *key)
{
	block_rq_wr_enabled = 0;
	block_rq_rd_enabled = 0;

	block_rq_wr_key = *key;
	*key = *key + 1;
	block_rq_rd_key = *key;
	*key = *key + 1;

	return 0;
}

static int gator_events_block_start(void)
{
	int cpu;

	for_each_present_cpu(cpu) 
		per_cpu(new_data_avail, cpu) = true;

	// register tracepoints
	if (block_rq_wr_enabled || block_rq_rd_enabled)
		if (GATOR_REGISTER_TRACE(block_rq_complete))
			goto fail_block_rq_exit;
	pr_debug("gator: registered block event tracepoints\n");

	return 0;

	// unregister tracepoints on error
fail_block_rq_exit:
	pr_err("gator: block event tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

static void gator_events_block_stop(void)
{
	if (block_rq_wr_enabled || block_rq_rd_enabled)
		GATOR_UNREGISTER_TRACE(block_rq_complete);
	pr_debug("gator: unregistered block event tracepoints\n");

	block_rq_wr_enabled = 0;
	block_rq_rd_enabled = 0;
}

static int gator_events_block_read(int **buffer)
{
	unsigned long flags;
	int len, value, cpu, data = 0;
	cpu = raw_smp_processor_id();

	if (per_cpu(new_data_avail, cpu) == false)
		return 0;

	per_cpu(new_data_avail, cpu) = false;

	len = 0;
	if (block_rq_wr_enabled) {
		local_irq_save(flags);
		value = per_cpu(blockCnt, cpu)[BLOCK_RQ_WR];
		per_cpu(blockCnt, cpu)[BLOCK_RQ_WR] = 0;
		local_irq_restore(flags);
		per_cpu(blockGet, cpu)[len++] = block_rq_wr_key;
		per_cpu(blockGet, cpu)[len++] = value;
		data += value;
	}
	if (block_rq_rd_enabled) {
		local_irq_save(flags);
		value = per_cpu(blockCnt, cpu)[BLOCK_RQ_RD];
		per_cpu(blockCnt, cpu)[BLOCK_RQ_RD] = 0;
		local_irq_restore(flags);
		per_cpu(blockGet, cpu)[len++] = block_rq_rd_key;
		per_cpu(blockGet, cpu)[len++] = value;
		data += value;
	}

	if (data != 0)
		per_cpu(new_data_avail, cpu) = true;

	if (buffer)
		*buffer = per_cpu(blockGet, cpu);

	return len;
}

int gator_events_block_install(gator_interface *gi) {
	gi->create_files = gator_events_block_create_files;
	gi->init = gator_events_block_init;
	gi->start = gator_events_block_start;
	gi->stop = gator_events_block_stop;
	gi->read = gator_events_block_read;
	return 0;
}
