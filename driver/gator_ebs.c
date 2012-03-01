/**
 * Copyright (C) ARM Limited 2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#if defined(__arm__) && (GATOR_PERF_PMU_SUPPORT)
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/pmu.h>

extern int pmnc_counters;
extern int ccnt;
extern unsigned long pmnc_enabled[];
extern unsigned long pmnc_event[];
extern unsigned long pmnc_count[];
extern unsigned long pmnc_key[];

static DEFINE_PER_CPU(struct perf_event *, pevent);
static DEFINE_PER_CPU(struct perf_event_attr *, pevent_attr);
static DEFINE_PER_CPU(int, key);
static DEFINE_PER_CPU(unsigned int, prev_value);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
static void ebs_overflow_handler(struct perf_event *event, int unused, struct perf_sample_data *data, struct pt_regs *regs)
#else
static void ebs_overflow_handler(struct perf_event *event, struct perf_sample_data *data, struct pt_regs *regs)
#endif
{
	unsigned int value, delta, cpu = smp_processor_id(), buftype = EVENT_BUF;

	if (event != per_cpu(pevent, cpu))
		return;

	if (buffer_check_space(cpu, buftype, 5 * MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
		value = local64_read(&event->count);
		delta = value - per_cpu(prev_value, cpu);
		per_cpu(prev_value, cpu) = value;

		// Counters header
		gator_buffer_write_packed_int(cpu, buftype, MESSAGE_COUNTERS);     // type
		gator_buffer_write_packed_int64(cpu, buftype, gator_get_time());   // time

		// Output counter
		gator_buffer_write_packed_int(cpu, buftype, 2);                    // length
		gator_buffer_write_packed_int(cpu, buftype, per_cpu(key, cpu));    // key
		gator_buffer_write_packed_int(cpu, buftype, delta);                // delta

		// End Counters, length of zero
		gator_buffer_write_packed_int(cpu, buftype, 0);
	}

	// Output backtrace
	if (buffer_check_space(cpu, buftype, gator_backtrace_depth * 2 * MAXSIZE_PACK32))
		gator_add_sample(cpu, buftype, regs);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	buffer_check(cpu, buftype);
}

static void gator_event_sampling_online(void)
{
	int cpu = smp_processor_id(), buftype = EVENT_BUF;

	// read the counter and toss the invalid data, return zero instead
	struct perf_event * ev = per_cpu(pevent, cpu);
	if (ev != NULL && ev->state == PERF_EVENT_STATE_ACTIVE) {
		ev->pmu->read(ev);
		per_cpu(prev_value, cpu) = local64_read(&ev->count);

		// Counters header
		gator_buffer_write_packed_int(cpu, buftype, MESSAGE_COUNTERS);     // type
		gator_buffer_write_packed_int64(cpu, buftype, gator_get_time());   // time

		// Output counter
		gator_buffer_write_packed_int(cpu, buftype, 2);                    // length
		gator_buffer_write_packed_int(cpu, buftype, per_cpu(key, cpu));    // key
		gator_buffer_write_packed_int(cpu, buftype, 0);                    // delta - zero for initialization

		// End Counters, length of zero
		gator_buffer_write_packed_int(cpu, buftype, 0);
	}
}

static void gator_event_sampling_online_dispatch(int cpu)
{
	struct perf_event * ev;

	if (!event_based_sampling)
		return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
	ev = per_cpu(pevent, cpu) = perf_event_create_kernel_counter(per_cpu(pevent_attr, cpu), cpu, 0, ebs_overflow_handler);
#else
	ev = per_cpu(pevent, cpu) = perf_event_create_kernel_counter(per_cpu(pevent_attr, cpu), cpu, 0, ebs_overflow_handler, 0);
#endif

	if (IS_ERR(ev)) {
		pr_err("gator: unable to start event-based-sampling");
		return;
	}

	if (ev->state != PERF_EVENT_STATE_ACTIVE) {
		pr_err("gator: unable to start event-based-sampling");
		perf_event_release_kernel(ev);
		return;
	}

	ev->pmu->read(ev);
	per_cpu(prev_value, cpu) = local64_read(&ev->count);
}

static void gator_event_sampling_offline_dispatch(int cpu)
{
	if (per_cpu(pevent, cpu)) {
		perf_event_release_kernel(per_cpu(pevent, cpu));
		per_cpu(pevent, cpu) = NULL;
	}
}

static int gator_event_sampling_start(void)
{
	int cnt, event = 0, count = 0, ebs_key = 0, cpu;

	for_each_present_cpu(cpu) {
		per_cpu(pevent, cpu) = NULL;
		per_cpu(pevent_attr, cpu) = NULL;
	}

	event_based_sampling = false;
	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		if (pmnc_count[cnt] > 0) {
			event_based_sampling = true;
			event = pmnc_event[cnt];
			count = pmnc_count[cnt];
			ebs_key = pmnc_key[cnt];
			break;
		}
	}

	if (!event_based_sampling)
		return 0;

	for_each_present_cpu(cpu) {
		u32 size = sizeof(struct perf_event_attr);
		per_cpu(pevent_attr, cpu) = kmalloc(size, GFP_KERNEL);
		if (!per_cpu(pevent_attr, cpu))
			return -1;

		memset(per_cpu(pevent_attr, cpu), 0, size);
		per_cpu(pevent_attr, cpu)->type = PERF_TYPE_RAW;
		per_cpu(pevent_attr, cpu)->size = size;
		per_cpu(pevent_attr, cpu)->config = event;
		per_cpu(pevent_attr, cpu)->sample_period = count;
		per_cpu(pevent_attr, cpu)->pinned = 1;

		// handle special case for ccnt
		if (cnt == ccnt) {
			per_cpu(pevent_attr, cpu)->type = PERF_TYPE_HARDWARE;
			per_cpu(pevent_attr, cpu)->config = PERF_COUNT_HW_CPU_CYCLES;
		}

		per_cpu(key, cpu) = ebs_key;
	}

	return 0;
}

static void gator_event_sampling_stop(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		if (per_cpu(pevent_attr, cpu)) {
			kfree(per_cpu(pevent_attr, cpu));
			per_cpu(pevent_attr, cpu) = NULL;
		}
	}
}

#else
static void gator_event_sampling_online(void) {}
static void gator_event_sampling_online_dispatch(int cpu) {}
static void gator_event_sampling_offline_dispatch(int cpu) {}
static int gator_event_sampling_start(void) {return 0;}
static void gator_event_sampling_stop(void) {}
#endif
