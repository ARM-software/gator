/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/math64.h>

#ifdef MALI_SUPPORT
#include "linux/mali_linux_trace.h"
#endif
#include "gator_trace_gpu.h"

#define ACTIVITY_START  1
#define ACTIVITY_STOP   2

/* Note whether tracepoints have been registered */
static int mali_trace_registered;
static int gpu_trace_registered;

#define GPU_OVERFLOW		-1
#define GPU_START			1
#define GPU_STOP			2

#define GPU_UNIT_VP			1
#define GPU_UNIT_FP			2

#define TRACESIZE				(8*1024)

static DEFINE_PER_CPU(uint64_t *[2], theGpuTraceBuf);
static DEFINE_PER_CPU(int, theGpuTraceSel);
static DEFINE_PER_CPU(int, theGpuTracePos);
static DEFINE_PER_CPU(int, theGpuTraceErr);

int gator_trace_gpu_read(long long **buffer);

static void probe_gpu_write(int type, int unit, int core, struct task_struct* task)
{
	int tracePos;
	unsigned long flags;
	uint64_t *traceBuf, time;
	int pid, tgid;
	int cpu = smp_processor_id();

	if (!per_cpu(theGpuTraceBuf, cpu)[per_cpu(theGpuTraceSel, cpu)])
		return;

	if (task) {
		tgid = (int)task->tgid;
		pid = (int)task->pid;
	} else {
		tgid = pid = 0;
	}

	// disable interrupts to synchronize with gator_trace_gpu_read(); spinlocks not needed since percpu buffers are used
	local_irq_save(flags);

	time = gator_get_time();
	tracePos = per_cpu(theGpuTracePos, cpu);
	traceBuf = per_cpu(theGpuTraceBuf, cpu)[per_cpu(theGpuTraceSel, cpu)];

	if (tracePos < (TRACESIZE - 100)) {
		// capture
		traceBuf[tracePos++] = type;
		traceBuf[tracePos++] = time;
		traceBuf[tracePos++] = unit;
		traceBuf[tracePos++] = core;
		traceBuf[tracePos++] = tgid;
		traceBuf[tracePos++] = pid;
	} else if (!per_cpu(theGpuTraceErr, cpu)) {
		per_cpu(theGpuTraceErr, cpu) = 1;
		traceBuf[tracePos++] = GPU_OVERFLOW;
		traceBuf[tracePos++] = time;
		traceBuf[tracePos++] = 0;
		traceBuf[tracePos++] = 0;
		traceBuf[tracePos++] = 0;
		traceBuf[tracePos++] = 0;
		pr_debug("gator: gpu trace overflow\n");
	}
	per_cpu(theGpuTracePos, cpu) = tracePos;
	local_irq_restore(flags);
}

#ifdef MALI_SUPPORT

enum components {
    COMPONENT_VP0 = 1,
    COMPONENT_FP0 = 5,
    COMPONENT_FP1,
    COMPONENT_FP2,
    COMPONENT_FP3,
    COMPONENT_FP4,
    COMPONENT_FP5,
    COMPONENT_FP6,
    COMPONENT_FP7,
};

GATOR_DEFINE_PROBE(mali_timeline_event, TP_PROTO(unsigned int event_id, unsigned int d0, unsigned int d1, unsigned int d2, unsigned int d3, unsigned int d4))
{
	unsigned int component, state;

	// do as much work as possible before disabling interrupts
	component = (event_id >> 16) & 0xF;
	state = (event_id >> 24) & 0xF;

	if ((component == COMPONENT_VP0) || (component >= COMPONENT_FP0 && component <= COMPONENT_FP7)) {
		if (state == ACTIVITY_START || state == ACTIVITY_STOP) {
			unsigned int type = (state == ACTIVITY_START) ? GPU_START : GPU_STOP;
			unsigned int unit = (component < COMPONENT_FP0) ? GPU_UNIT_VP : GPU_UNIT_FP;
			unsigned int core = (component < COMPONENT_FP0) ? component - COMPONENT_VP0 : component - COMPONENT_FP0;
			struct task_struct* task = (state == ACTIVITY_START) ? (struct task_struct*)d2 : NULL;

			probe_gpu_write(type, unit, core, task);
    	}
    }
}
#endif

GATOR_DEFINE_PROBE(gpu_activity_start, TP_PROTO(int gpu_unit, int gpu_core, struct task_struct *p))
{
	probe_gpu_write(GPU_START, gpu_unit, gpu_core, p);
}

GATOR_DEFINE_PROBE(gpu_activity_stop, TP_PROTO(int gpu_unit, int gpu_core))
{
	probe_gpu_write(GPU_STOP, gpu_unit, gpu_core, NULL);
}

int gator_trace_gpu_start(void)
{
	int cpu;

	/*
	 * Returns 0 for installation failed
	 * Absence of gpu trace points is not an error
	 */

	gpu_trace_registered = mali_trace_registered = 0;

#ifdef MALI_SUPPORT
    if (!GATOR_REGISTER_TRACE(mali_timeline_event)) {
    	mali_trace_registered = 1;
    }
#endif

    if (!mali_trace_registered) {
        if (GATOR_REGISTER_TRACE(gpu_activity_start)) {
        	return 0;
        }
        if (GATOR_REGISTER_TRACE(gpu_activity_stop)) {
        	GATOR_UNREGISTER_TRACE(gpu_activity_start);
        	return 0;
        }
        gpu_trace_registered = 1;
    }

	if (!gpu_trace_registered && !mali_trace_registered) {
		return 0;
	}

	for_each_present_cpu(cpu) {
		per_cpu(theGpuTraceSel, cpu) = 0;
		per_cpu(theGpuTracePos, cpu) = 0;
		per_cpu(theGpuTraceErr, cpu) = 0;
		per_cpu(theGpuTraceBuf, cpu)[0] = kmalloc(TRACESIZE * sizeof(uint64_t), GFP_KERNEL);
		per_cpu(theGpuTraceBuf, cpu)[1] = kmalloc(TRACESIZE * sizeof(uint64_t), GFP_KERNEL);
		if (!per_cpu(theGpuTraceBuf, cpu)[0] || !per_cpu(theGpuTraceBuf, cpu)[1]) {
#ifdef MALI_SUPPORT
			if (mali_trace_registered) {
				GATOR_UNREGISTER_TRACE(mali_timeline_event);
			}
#endif
			if (gpu_trace_registered) {
				GATOR_UNREGISTER_TRACE(gpu_activity_stop);
				GATOR_UNREGISTER_TRACE(gpu_activity_start);
			}

			gpu_trace_registered = mali_trace_registered = 0;

			return -1;
		}
	}

	return 0;
}

int gator_trace_gpu_offline(long long **buffer)
{
	return gator_trace_gpu_read(buffer);
}

void gator_trace_gpu_stop(void)
{
	int cpu;

    if (gpu_trace_registered || mali_trace_registered) {
		for_each_present_cpu(cpu) {
			kfree(per_cpu(theGpuTraceBuf, cpu)[0]);
			kfree(per_cpu(theGpuTraceBuf, cpu)[1]);
			per_cpu(theGpuTraceBuf, cpu)[0] = NULL;
			per_cpu(theGpuTraceBuf, cpu)[1] = NULL;
		}

#ifdef MALI_SUPPORT
		if (mali_trace_registered) {
			GATOR_UNREGISTER_TRACE(mali_timeline_event);
		}
#endif
		if (gpu_trace_registered) {
			GATOR_UNREGISTER_TRACE(gpu_activity_stop);
			GATOR_UNREGISTER_TRACE(gpu_activity_start);
		}

		gpu_trace_registered = mali_trace_registered = 0;
    }
}

int gator_trace_gpu_read(long long **buffer)
{
	int cpu = smp_processor_id();
	unsigned long flags;
	uint64_t *traceBuf;
	int tracePos;

	if (!per_cpu(theGpuTraceBuf, cpu)[per_cpu(theGpuTraceSel, cpu)])
		return 0;

	local_irq_save(flags);

	traceBuf = per_cpu(theGpuTraceBuf, cpu)[per_cpu(theGpuTraceSel, cpu)];
	tracePos = per_cpu(theGpuTracePos, cpu);

	per_cpu(theGpuTraceSel, cpu) = !per_cpu(theGpuTraceSel, cpu);
	per_cpu(theGpuTracePos, cpu) = 0;
	per_cpu(theGpuTraceErr, cpu) = 0;

	local_irq_restore(flags);

	if (buffer)
		*buffer = traceBuf;

	return tracePos;
}
