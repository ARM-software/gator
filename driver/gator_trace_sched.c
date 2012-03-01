/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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

#define SCHEDSIZE				(8*1024)
#define TASK_MAP_ENTRIES		1024		/* must be power of 2 */
#define TASK_MAX_COLLISIONS		2

static DEFINE_PER_CPU(uint64_t *[2], theSchedBuf);
static DEFINE_PER_CPU(int, theSchedSel);
static DEFINE_PER_CPU(int, theSchedPos);
static DEFINE_PER_CPU(int, theSchedErr);
static DEFINE_PER_CPU(uint64_t *, taskname_keys);

enum {
	STATE_CONTENTION = 0,
	STATE_WAIT_ON_IO,
	STATE_WAIT_ON_OTHER
};

int gator_trace_sched_read(long long **buffer);

void emit_pid_name(struct task_struct* task)
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

	if (!found && buffer_check_space(cpu, TIMER_BUF, TASK_COMM_LEN + 2 * MAXSIZE_PACK32 + MAXSIZE_PACK64)) {
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
		gator_buffer_write_packed_int64(cpu, TIMER_BUF, gator_get_time());
		gator_buffer_write_packed_int(cpu, TIMER_BUF, task->pid);
		gator_buffer_write_string(cpu, TIMER_BUF, taskcomm);
		local_irq_restore(flags);
	}
}

static void probe_sched_write(int type, struct task_struct* task, struct task_struct* old_task)
{
	int schedPos, cookie = 0, state = 0;
	unsigned long flags;
	uint64_t *schedBuf, time;
	int cpu = smp_processor_id();
	int pid = task->pid;
	int tgid = task->tgid;

	if (!per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)])
		return;

	if (type == SCHED_SWITCH) {
		// do as much work as possible before disabling interrupts
		cookie = get_exec_cookie(cpu, TIMER_BUF, task);
		emit_pid_name(task);
		if (old_task->state == 0)
			state = STATE_CONTENTION;
		else if (old_task->in_iowait)
			state = STATE_WAIT_ON_IO;
		else
			state = STATE_WAIT_ON_OTHER;
	}

	// disable interrupts to synchronize with gator_trace_sched_read(); spinlocks not needed since percpu buffers are used
	local_irq_save(flags);

	time = gator_get_time();
	schedPos = per_cpu(theSchedPos, cpu);
	schedBuf = per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)];

	if (schedPos < (SCHEDSIZE - 100)) {
		// capture
		schedBuf[schedPos++] = type;
		schedBuf[schedPos++] = time;
		schedBuf[schedPos++] = pid;
		schedBuf[schedPos++] = tgid;
		schedBuf[schedPos++] = cookie;
		schedBuf[schedPos++] = state;
	} else if (!per_cpu(theSchedErr, cpu)) {
		per_cpu(theSchedErr, cpu) = 1;
		schedBuf[schedPos++] = SCHED_OVERFLOW;
		schedBuf[schedPos++] = time;
		schedBuf[schedPos++] = 0;
		schedBuf[schedPos++] = 0;
		schedBuf[schedPos++] = 0;
		schedBuf[schedPos++] = 0;
		pr_debug("gator: tracepoint overflow\n");
	}
	per_cpu(theSchedPos, cpu) = schedPos;
	local_irq_restore(flags);
}

// special case used during a suspend of the system
static void trace_sched_insert_idle(void)
{
	unsigned long flags;
	uint64_t *schedBuf;
	int schedPos, cpu = smp_processor_id();

	if (!per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)])
		return;

	local_irq_save(flags);

	schedPos = per_cpu(theSchedPos, cpu);
	schedBuf = per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)];

	if (schedPos < (SCHEDSIZE - (6 * 8))) {
		// capture
		schedBuf[schedPos++] = SCHED_SWITCH;
		schedBuf[schedPos++] = gator_get_time();
		schedBuf[schedPos++] = 0; // idle pid is zero
		schedBuf[schedPos++] = 0; // idle tid is zero
		schedBuf[schedPos++] = 0; // idle cookie is zero
		schedBuf[schedPos++] = STATE_WAIT_ON_OTHER;
	}

	per_cpu(theSchedPos, cpu) = schedPos;
	local_irq_restore(flags);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct rq *rq, struct task_struct *prev, struct task_struct *next))
#else
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
#endif
{
	probe_sched_write(SCHED_SWITCH, next, prev);
}

GATOR_DEFINE_PROBE(sched_process_free, TP_PROTO(struct task_struct *p))
{
	probe_sched_write(SCHED_PROCESS_FREE, p, 0);
}

static int register_scheduler_tracepoints(void) {
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

	return register_scheduler_tracepoints();
}

int gator_trace_sched_offline(long long **buffer)
{
	trace_sched_insert_idle();
	return gator_trace_sched_read(buffer);
}

static void unregister_scheduler_tracepoints(void)
{
	GATOR_UNREGISTER_TRACE(sched_switch);
	GATOR_UNREGISTER_TRACE(sched_process_free);
	pr_debug("gator: unregistered tracepoints\n");
}

void gator_trace_sched_stop(void)
{
	int cpu;
	unregister_scheduler_tracepoints();

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
