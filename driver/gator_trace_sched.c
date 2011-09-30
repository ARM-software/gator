/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <trace/events/sched.h>
#include "gator.h"

#define SCHED_OVERFLOW			-1
#define SCHED_SWITCH			1
#define SCHED_PROCESS_FREE		2

#define FIELD_TYPE				0
#define FIELD_TIME				1
#define FIELD_PARAM1			2
#define FIELD_PARAM2			3
#define FIELD_PARAM3			4
#define FIELDS_PER_SCHED		5

#define SCHEDSIZE				(8*1024)
#define TASK_MAP_ENTRIES		1024		/* must be power of 2 */
#define TASK_MAX_COLLISIONS		2

static DEFINE_PER_CPU(uint64_t *[2], theSchedBuf);
static DEFINE_PER_CPU(int, theSchedSel);
static DEFINE_PER_CPU(int, theSchedPos);
static DEFINE_PER_CPU(int, theSchedErr);
static DEFINE_PER_CPU(uint64_t *, taskname_keys);

void emit_pid_name(uint64_t time, struct task_struct* task)
{
	bool found = false;
	unsigned long flags;
	char taskcomm[TASK_COMM_LEN + 3];
	int x, cpu = smp_processor_id();
	uint64_t *keys = &(per_cpu(taskname_keys, cpu)[(task->pid & 0xFF) * TASK_MAX_COLLISIONS]);
	uint64_t value;
	
	value = gator_chksum_crc32(task->comm);
	value = (value << 32) | (uint32_t)task->pid;

	// determine if the thread name was emitted already
	for (x = 0; x < TASK_MAX_COLLISIONS; x++) {
		if (keys[x] == value) {
			found = true;
			break;
		}
	}

	if (!found) {
		// shift values, new value always in front
		uint64_t oldv, newv = value;
		for (x = 0; x < TASK_MAX_COLLISIONS; x++) {
			oldv = keys[x];
			keys[x] = newv;
			newv = oldv;
		}

		// emit pid names, cannot use get_task_comm, as it's not exported on all kernel versions
		if (strlcpy(taskcomm, task->comm, TASK_COMM_LEN) == TASK_COMM_LEN - 1)
			// append ellipses if task->comm has length of TASK_COMM_LEN - 1
			strcat(taskcomm, "...");

		// disable interrupts to synchronize with hrtimer populating timer buf
		local_irq_save(flags);
		gator_buffer_write_packed_int(cpu, TIMER_BUF, MESSAGE_PID_NAME);
		gator_buffer_write_packed_int64(cpu, TIMER_BUF, time);
		gator_buffer_write_packed_int(cpu, TIMER_BUF, task->pid);
		gator_buffer_write_string(cpu, TIMER_BUF, taskcomm);
		local_irq_restore(flags);
	}
}

static void probe_sched_write(int type, int param1, int param2, int param3)
{
	unsigned long flags;
	int cpu = smp_processor_id();
	uint64_t time = gator_get_time();
	uint64_t *schedBuf;
	int schedPos, cookie = param3;

	if (!per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)])
		return;

	if (param3) {
		// do as much work as possible before disabling interrupts
		struct task_struct *task = (struct task_struct *)param3;
		cookie = get_exec_cookie(cpu, TIMER_BUF, task);
		emit_pid_name(time, task);
	}

	// disable interrupts to synchronize with gator_trace_sched_read(); spinlocks not needed since percpu buffers are used
	local_irq_save(flags);

	schedPos = per_cpu(theSchedPos, cpu);
	schedBuf = per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)];

	if (schedPos < (SCHEDSIZE-100)) {
		// capture
		schedBuf[schedPos+FIELD_TYPE] = type;
		schedBuf[schedPos+FIELD_TIME] = time;
		schedBuf[schedPos+FIELD_PARAM1] = param1;
		schedBuf[schedPos+FIELD_PARAM2] = param2;
		schedBuf[schedPos+FIELD_PARAM3] = cookie;
		per_cpu(theSchedPos, cpu) = schedPos + FIELDS_PER_SCHED;
	} else if (!per_cpu(theSchedErr, cpu)) {
		per_cpu(theSchedErr, cpu) = 1;
		schedBuf[schedPos+FIELD_TYPE] = SCHED_OVERFLOW;
		schedBuf[schedPos+FIELD_TIME] = time;
		schedBuf[schedPos+FIELD_PARAM1] = 0;
		schedBuf[schedPos+FIELD_PARAM2] = 0;
		schedBuf[schedPos+FIELD_PARAM3] = 0;
		per_cpu(theSchedPos, cpu) = schedPos + FIELDS_PER_SCHED;
		pr_debug("gator: tracepoint overflow\n");
	}
	local_irq_restore(flags);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct rq *rq, struct task_struct *prev, struct task_struct *next))
#else
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
#endif
{
	probe_sched_write(SCHED_SWITCH, next->pid, next->tgid, (int)next);
}

GATOR_DEFINE_PROBE(sched_process_free, TP_PROTO(struct task_struct *p))
{
	probe_sched_write(SCHED_PROCESS_FREE, p->pid, 0, 0);
}

int gator_trace_sched_init(void)
{
	return 0;
}

int gator_trace_sched_start(void)
{
	int cpu, size;

	for_each_present_cpu(cpu) {
		per_cpu(theSchedSel, cpu) = 0;
		per_cpu(theSchedPos, cpu) = 0;
		per_cpu(theSchedErr, cpu) = 0;
		per_cpu(theSchedBuf, cpu)[0] = kmalloc(SCHEDSIZE * sizeof(uint64_t), GFP_KERNEL);
		per_cpu(theSchedBuf, cpu)[1] = kmalloc(SCHEDSIZE * sizeof(uint64_t), GFP_KERNEL);
		if (!per_cpu(theSchedBuf, cpu))
			return -1;

		size = TASK_MAP_ENTRIES * TASK_MAX_COLLISIONS * sizeof(uint64_t);
		per_cpu(taskname_keys, cpu) = (uint64_t*)kmalloc(size, GFP_KERNEL);
		if (!per_cpu(taskname_keys, cpu))
			return -1;
		memset(per_cpu(taskname_keys, cpu), 0, size);
	}

	// register tracepoints
	if (GATOR_REGISTER_TRACE(sched_switch))
		goto fail_sched_switch;
	if (GATOR_REGISTER_TRACE(sched_process_free))
		goto fail_sched_process_free;
	pr_debug("gator: registered tracepoints\n");

	return 0;

	// unregister tracepoints on error
fail_sched_process_free:
	GATOR_UNREGISTER_TRACE(sched_switch);
fail_sched_switch:
	pr_err("gator: tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

void gator_trace_sched_stop(void)
{
	int cpu;
	GATOR_UNREGISTER_TRACE(sched_switch);
	GATOR_UNREGISTER_TRACE(sched_process_free);
	pr_debug("gator: unregistered tracepoints\n");

	for_each_present_cpu(cpu) {
		kfree(per_cpu(theSchedBuf, cpu)[0]);
		kfree(per_cpu(theSchedBuf, cpu)[1]);
		per_cpu(theSchedBuf, cpu)[0] = NULL;
		per_cpu(theSchedBuf, cpu)[1] = NULL;
		kfree(per_cpu(taskname_keys, cpu));
	}
}

int gator_trace_sched_read(long long **buffer)
{
	int cpu = smp_processor_id();
	unsigned long flags;
	uint64_t *schedBuf;
	int schedPos;

	if (!per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)])
		return 0;

	local_irq_save(flags);

	schedBuf = per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)];
	schedPos = per_cpu(theSchedPos, cpu);

	per_cpu(theSchedSel, cpu) = !per_cpu(theSchedSel, cpu);
	per_cpu(theSchedPos, cpu) = 0;
	per_cpu(theSchedErr, cpu) = 0;

	local_irq_restore(flags);

	if (buffer)
		*buffer = schedBuf;

	return schedPos;
}
