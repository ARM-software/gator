/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <linux/workqueue.h>
#include <trace/events/kmem.h>

#define MEMINFO_MEMFREE		0
#define MEMINFO_MEMUSED		1
#define MEMINFO_BUFFERRAM	2
#define MEMINFO_TOTAL		3

static ulong meminfo_global_enabled;
static ulong meminfo_enabled[MEMINFO_TOTAL];
static ulong meminfo_key[MEMINFO_TOTAL];
static int meminfo_buffer[MEMINFO_TOTAL * 2];
static int meminfo_length = 0;
static unsigned int mem_event = 0;
static bool new_data_avail;

static void wq_sched_handler(struct work_struct *wsptr);

DECLARE_WORK(work, wq_sched_handler);

GATOR_DEFINE_PROBE(mm_page_free_direct, TP_PROTO(struct page *page, unsigned int order)) {
	mem_event++;
}

GATOR_DEFINE_PROBE(mm_pagevec_free, TP_PROTO(struct page *page, int cold)) {
	mem_event++;
}

GATOR_DEFINE_PROBE(mm_page_alloc, TP_PROTO(struct page *page, unsigned int order, gfp_t gfp_flags, int migratetype)) {
	mem_event++;
}

static int gator_events_meminfo_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;

	for (i = 0; i < MEMINFO_TOTAL; i++) {
		switch (i) {
		case MEMINFO_MEMFREE:
			dir = gatorfs_mkdir(sb, root, "Linux_meminfo_memfree");
			break;
		case MEMINFO_MEMUSED:
			dir = gatorfs_mkdir(sb, root, "Linux_meminfo_memused");
			break;
		case MEMINFO_BUFFERRAM:
			dir = gatorfs_mkdir(sb, root, "Linux_meminfo_bufferram");
			break;
		default:
			return -1;
		}
		if (!dir) {
			return -1;
		}
		gatorfs_create_ulong(sb, dir, "enabled", &meminfo_enabled[i]);
		gatorfs_create_ro_ulong(sb, dir, "key", &meminfo_key[i]);
	}

	return 0;
}

static int gator_events_meminfo_start(void)
{
	int i;

	new_data_avail = true;
	for (i = 0; i < MEMINFO_TOTAL; i++) {
		if (meminfo_enabled[i]) {
			meminfo_global_enabled = 1;
		}
	}

	if (meminfo_global_enabled == 0)
		return 0;

	if (GATOR_REGISTER_TRACE(mm_page_free_direct))
		goto mm_page_free_direct_exit;
	if (GATOR_REGISTER_TRACE(mm_pagevec_free))
		goto mm_pagevec_free_exit;
	if (GATOR_REGISTER_TRACE(mm_page_alloc))
		goto mm_page_alloc_exit;

	return 0;

mm_page_alloc_exit:
	GATOR_UNREGISTER_TRACE(mm_pagevec_free);
mm_pagevec_free_exit:
	GATOR_UNREGISTER_TRACE(mm_page_free_direct);
mm_page_free_direct_exit:
	return -1;
}

static void gator_events_meminfo_stop(void)
{
	int i;

	if (meminfo_global_enabled) {
		GATOR_UNREGISTER_TRACE(mm_page_free_direct);
		GATOR_UNREGISTER_TRACE(mm_pagevec_free);
		GATOR_UNREGISTER_TRACE(mm_page_alloc);
	}

	meminfo_global_enabled = 0;
	for (i = 0; i < MEMINFO_TOTAL; i++) {
		meminfo_enabled[i] = 0;
	}
}

// Must be run in a work queue as the kernel function si_meminfo() can sleep
static void wq_sched_handler(struct work_struct *wsptr)
{
	struct sysinfo info;
	int i, len, value;

	meminfo_length = len = 0;

	si_meminfo(&info);
	for (i = 0; i < MEMINFO_TOTAL; i++) {
		if (meminfo_enabled[i]) {
			switch (i) {
			case MEMINFO_MEMFREE:
				value = info.freeram * PAGE_SIZE;
				break;
			case MEMINFO_MEMUSED:
				value = (info.totalram - info.freeram) * PAGE_SIZE;
				break;
			case MEMINFO_BUFFERRAM:
				value = info.bufferram * PAGE_SIZE;
				break;
			default:
				value = 0;
				break;
			}
			meminfo_buffer[len++] = meminfo_key[i];
			meminfo_buffer[len++] = value;
		}
	}

	meminfo_length = len;
	new_data_avail = true;
}

static int gator_events_meminfo_read(int **buffer)
{
	static unsigned int last_mem_event = 0;

	if (smp_processor_id() || !meminfo_global_enabled)
		return 0;

	if (last_mem_event != mem_event) {
		last_mem_event = mem_event;
		schedule_work(&work);
	}

	if (!new_data_avail)
		return 0;

	new_data_avail = false;

	if (buffer)
		*buffer = meminfo_buffer;

	return meminfo_length;
}

static struct gator_interface gator_events_meminfo_interface = {
	.create_files = gator_events_meminfo_create_files,
	.start = gator_events_meminfo_start,
	.stop = gator_events_meminfo_stop,
	.read = gator_events_meminfo_read,
};

int gator_events_meminfo_init(void)
{
	int i;

	meminfo_global_enabled = 0;
	for (i = 0; i < MEMINFO_TOTAL; i++) {
		meminfo_enabled[i] = 0;
		meminfo_key[i] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_meminfo_interface);
}
gator_events_init(gator_events_meminfo_init);
