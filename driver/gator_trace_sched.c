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

#define SCHED_TIMER_EVENT		0
#define SCHED_WAIT_TASK			1
#define SCHED_WAKEUP			2
#define SCHED_WAKEUP_NEW		3
#define SCHED_SWITCH			4
#define SCHED_MIGRATE_TASK		5
#define SCHED_PROCESS_FREE		6
#define SCHED_PROCESS_EXIT		7
#define SCHED_PROCESS_WAIT		8
#define SCHED_PROCESS_FORK		9
#define SCHED_OVERFLOW			-1

#define SCHEDSIZE				(16*1024)

static DEFINE_PER_CPU(int *[2], theSchedBuf);
static DEFINE_PER_CPU(int, theSchedSel);
static DEFINE_PER_CPU(int, theSchedPos);
static DEFINE_PER_CPU(int, theSchedErr);

static void probe_sched_write(int type, int param1, int param2, int param3)
{
	unsigned long flags;
	int cpu = smp_processor_id();
	uint64_t time = gator_get_time();
	int *schedBuf;
	int schedPos;

	if (!per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)])
		return;

	// disable interrupts to synchronize with gator_trace_sched_read(); spinlocks not needed since percpu buffers are used
	local_irq_save(flags);

	schedPos = per_cpu(theSchedPos, cpu);
	schedBuf = per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)];

	if (schedPos < (SCHEDSIZE-100)) {
		// capture
		schedBuf[schedPos+0] = type;
		schedBuf[schedPos+1] = (int)time;
		schedBuf[schedPos+2] = (int)(time >> 32);
		schedBuf[schedPos+3] = param1;
		schedBuf[schedPos+4] = param2;
		schedBuf[schedPos+5] = param3;
		per_cpu(theSchedPos, cpu) = schedPos + 6;
	} else if (!per_cpu(theSchedErr, cpu)) {
		per_cpu(theSchedErr, cpu) = 1;
		schedBuf[schedPos+0] = SCHED_OVERFLOW;
		schedBuf[schedPos+1] = 0;
		schedBuf[schedPos+2] = 0;
		schedBuf[schedPos+3] = 0;
		schedBuf[schedPos+4] = 0;
		schedBuf[schedPos+5] = 0;
		per_cpu(theSchedPos, cpu) = schedPos + 6;
		pr_debug("gator: tracepoint overflow\n");
	}
	local_irq_restore(flags);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_wait_task, TP_PROTO(struct rq *rq, struct task_struct *p))
#else
GATOR_DEFINE_PROBE(sched_wait_task, TP_PROTO(struct task_struct *p))
#endif
{
	probe_sched_write(SCHED_WAIT_TASK, 0, p->pid, 0);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_wakeup, TP_PROTO(struct rq *rq, struct task_struct *p, int success))
#else
GATOR_DEFINE_PROBE(sched_wakeup, TP_PROTO(struct task_struct *p, int success))
#endif
{
	if (success)
		probe_sched_write(SCHED_WAKEUP, 0, p->pid, 0);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_wakeup_new, TP_PROTO(struct rq *rq, struct task_struct *p, int success))
#else
GATOR_DEFINE_PROBE(sched_wakeup_new, TP_PROTO(struct task_struct *p, int success))
#endif
{
	if (success)
		probe_sched_write(SCHED_WAKEUP_NEW, 0, p->tgid, p->pid);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct rq *rq, struct task_struct *prev, struct task_struct *next))
#else
GATOR_DEFINE_PROBE(sched_switch, TP_PROTO(struct task_struct *prev, struct task_struct *next))
#endif
{
	probe_sched_write(SCHED_SWITCH, (int)next, next->tgid, next->pid);
}

GATOR_DEFINE_PROBE(sched_migrate_task, TP_PROTO(struct task_struct *p, int dest_cpu))
{
	probe_sched_write(SCHED_MIGRATE_TASK, 0, dest_cpu, p->pid);
}

GATOR_DEFINE_PROBE(sched_process_free, TP_PROTO(struct task_struct *p))
{
	probe_sched_write(SCHED_PROCESS_FREE, 0, p->pid, 0);
}

GATOR_DEFINE_PROBE(sched_process_exit, TP_PROTO(struct task_struct *p))
{
	probe_sched_write(SCHED_PROCESS_EXIT, 0, p->pid, 0);
}

GATOR_DEFINE_PROBE(sched_process_wait, TP_PROTO(struct pid *pid))
{
	probe_sched_write(SCHED_PROCESS_WAIT, 0, pid_nr(pid), 0);
}

GATOR_DEFINE_PROBE(sched_process_fork, TP_PROTO(struct task_struct *parent, struct task_struct *child))
{
	probe_sched_write(SCHED_PROCESS_FORK, (int)child, parent->pid, child->pid);
}

int gator_trace_sched_init(void)
{
	return 0;
}

int gator_trace_sched_start(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		per_cpu(theSchedSel, cpu) = 0;
		per_cpu(theSchedPos, cpu) = 0;
		per_cpu(theSchedErr, cpu) = 0;
		per_cpu(theSchedBuf, cpu)[0] = kmalloc(SCHEDSIZE * sizeof(int), GFP_KERNEL);
		per_cpu(theSchedBuf, cpu)[1] = kmalloc(SCHEDSIZE * sizeof(int), GFP_KERNEL);
		if (!per_cpu(theSchedBuf, cpu))
			return -1;
	}

	// register tracepoints
	if (GATOR_REGISTER_TRACE(sched_wait_task))
		goto fail_sched_wait_task;
	if (GATOR_REGISTER_TRACE(sched_wakeup))
		goto fail_sched_wakeup;
	if (GATOR_REGISTER_TRACE(sched_wakeup_new))
		goto fail_sched_wakeup_new;
	if (GATOR_REGISTER_TRACE(sched_switch))
		goto fail_sched_switch;
	if (GATOR_REGISTER_TRACE(sched_migrate_task))
		goto fail_sched_migrate_task;
	if (GATOR_REGISTER_TRACE(sched_process_free))
		goto fail_sched_process_free;
	if (GATOR_REGISTER_TRACE(sched_process_exit))
		goto fail_sched_process_exit;
	if (GATOR_REGISTER_TRACE(sched_process_wait))
		goto fail_sched_process_wait;
	if (GATOR_REGISTER_TRACE(sched_process_fork))
		goto fail_sched_process_fork;
	pr_debug("gator: registered tracepoints\n");

	return 0;

	// unregister tracepoints on error
fail_sched_process_fork:
	GATOR_UNREGISTER_TRACE(sched_process_wait);
fail_sched_process_wait:
	GATOR_UNREGISTER_TRACE(sched_process_exit);
fail_sched_process_exit:
	GATOR_UNREGISTER_TRACE(sched_process_free);
fail_sched_process_free:
	GATOR_UNREGISTER_TRACE(sched_migrate_task);
fail_sched_migrate_task:
	GATOR_UNREGISTER_TRACE(sched_switch);
fail_sched_switch:
	GATOR_UNREGISTER_TRACE(sched_wakeup_new);
fail_sched_wakeup_new:
	GATOR_UNREGISTER_TRACE(sched_wakeup);
fail_sched_wakeup:
	GATOR_UNREGISTER_TRACE(sched_wait_task);
fail_sched_wait_task:
	pr_err("gator: tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

void gator_trace_sched_stop(void)
{
	int cpu;
	GATOR_UNREGISTER_TRACE(sched_wait_task);
	GATOR_UNREGISTER_TRACE(sched_wakeup);
	GATOR_UNREGISTER_TRACE(sched_wakeup_new);
	GATOR_UNREGISTER_TRACE(sched_switch);
	GATOR_UNREGISTER_TRACE(sched_migrate_task);
	GATOR_UNREGISTER_TRACE(sched_process_free);
	GATOR_UNREGISTER_TRACE(sched_process_exit);
	GATOR_UNREGISTER_TRACE(sched_process_wait);
	GATOR_UNREGISTER_TRACE(sched_process_fork);
	pr_debug("gator: unregistered tracepoints\n");

	for_each_present_cpu(cpu) {
		kfree(per_cpu(theSchedBuf, cpu)[0]);
		kfree(per_cpu(theSchedBuf, cpu)[1]);
		per_cpu(theSchedBuf, cpu)[0] = NULL;
		per_cpu(theSchedBuf, cpu)[1] = NULL;
	}
}

int gator_trace_sched_read(int **buffer)
{
	uint64_t time = gator_get_time();
	int cpu = smp_processor_id();
	unsigned long flags;
	int *schedBuf;
	int schedPos;
	int i;

	if (!per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)])
		return 0;

	local_irq_save(flags);

	schedBuf = per_cpu(theSchedBuf, cpu)[per_cpu(theSchedSel, cpu)];
	schedPos = per_cpu(theSchedPos, cpu);

	per_cpu(theSchedSel, cpu) = !per_cpu(theSchedSel, cpu);
	per_cpu(theSchedPos, cpu) = 0;
	per_cpu(theSchedErr, cpu) = 0;

	local_irq_restore(flags);

	// find mm and replace with cookies
	for (i = 0; i < schedPos; i += 6) {
		uint32_t cookie = schedBuf[i+3];
		if (cookie) {
			struct task_struct *task = (struct task_struct *)cookie;
			schedBuf[i+3] = get_exec_cookie(cpu, task);
		}
	}

	// timer/end event
	schedBuf[schedPos++] = SCHED_TIMER_EVENT;
	schedBuf[schedPos++] = (int)time;
	schedBuf[schedPos++] = (int)(time >> 32);

	if (buffer)
		*buffer = schedBuf;

	return schedPos;
}
