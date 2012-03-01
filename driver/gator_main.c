/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static unsigned long gator_protocol_version = 8;

#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <asm/stacktrace.h>
#include <asm/uaccess.h>

#include "gator.h"
#include "gator_events.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
#error kernels prior to 2.6.32 are not supported
#endif

#if !defined(CONFIG_GENERIC_TRACER) && !defined(CONFIG_TRACING)
#error gator requires the kernel to have CONFIG_GENERIC_TRACER or CONFIG_TRACING defined
#endif

#ifndef CONFIG_PROFILING
#error gator requires the kernel to have CONFIG_PROFILING defined
#endif

#ifndef CONFIG_HIGH_RES_TIMERS
#error gator requires the kernel to have CONFIG_HIGH_RES_TIMERS defined
#endif

#if defined(__arm__) && defined(CONFIG_SMP) && !defined(CONFIG_LOCAL_TIMERS)
#error gator requires the kernel to have CONFIG_LOCAL_TIMERS defined on SMP systems
#endif

#if (GATOR_PERF_SUPPORT) && (!(GATOR_PERF_PMU_SUPPORT))
#ifndef CONFIG_PERF_EVENTS
#warning gator requires the kernel to have CONFIG_PERF_EVENTS defined to support pmu hardware counters
#elif !defined CONFIG_HW_PERF_EVENTS
#warning gator requires the kernel to have CONFIG_HW_PERF_EVENTS defined to support pmu hardware counters
#endif
#endif

#if (!(GATOR_CPU_FREQ_SUPPORT))
#warning gator requires kernel version 2.6.38 or greater and CONFIG_CPU_FREQ defined in order to enable the CPU Freq timeline chart
#endif

/******************************************************************************
 * DEFINES
 ******************************************************************************/
#define TIMER_BUFFER_SIZE_DEFAULT	(512*1024)
#define EVENT_BUFFER_SIZE_DEFAULT	(128*1024)

#define NO_COOKIE				0UL
#define INVALID_COOKIE			~0UL

#define FRAME_HRTIMER		1
#define FRAME_EVENT			2
#define FRAME_ANNOTATE		3

#define MESSAGE_COOKIE				1
#define MESSAGE_COUNTERS			3
#define MESSAGE_START_BACKTRACE		5
#define MESSAGE_END_BACKTRACE		7
#define MESSAGE_SCHEDULER_TRACE		9
#define MESSAGE_PID_NAME			11
#define MESSAGE_GPU_TRACE           13
#define MESSAGE_OVERFLOW			127

#define MAXSIZE_PACK32		5
#define MAXSIZE_PACK64		9

#if defined(__arm__)
#define PC_REG regs->ARM_pc
#else
#define PC_REG regs->ip
#endif

enum {TIMER_BUF, EVENT_BUF, NUM_GATOR_BUFS};

/******************************************************************************
 * Globals
 ******************************************************************************/
static unsigned long gator_cpu_cores;
static unsigned long userspace_buffer_size;
static unsigned long gator_backtrace_depth;

static unsigned long gator_started;
static unsigned long gator_buffer_opened;
static unsigned long gator_timer_count;
static unsigned long gator_streaming;
static DEFINE_MUTEX(start_mutex);
static DEFINE_MUTEX(gator_buffer_mutex);

bool event_based_sampling;

static DECLARE_WAIT_QUEUE_HEAD(gator_buffer_wait);

static void buffer_check(int cpu, int buftype);

/******************************************************************************
 * Prototypes
 ******************************************************************************/
static bool buffer_check_space(int cpu, int buftype, int bytes);
static void gator_buffer_write_packed_int(int cpu, int buftype, unsigned int x);
static void gator_buffer_write_packed_int64(int cpu, int buftype, unsigned long long x);
static void gator_buffer_write_string(int cpu, int buftype, char *x);
static int  gator_write_packed_int(char *buffer, unsigned int x);
static int  gator_write_packed_int64(char *buffer, unsigned long long x);
static void gator_add_trace(int cpu, int buftype, unsigned int address);
static void gator_add_sample(int cpu, int buftype, struct pt_regs * const regs);
static uint64_t gator_get_time(void);

static uint32_t gator_buffer_size[NUM_GATOR_BUFS];
static uint32_t gator_buffer_mask[NUM_GATOR_BUFS];
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_read);
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_write);
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_commit);
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], buffer_space_available);
static DEFINE_PER_CPU(char *[NUM_GATOR_BUFS], gator_buffer);
static DEFINE_PER_CPU(uint64_t, emit_overflow);

/******************************************************************************
 * Application Includes
 ******************************************************************************/
#include "gator_hrtimer_perf.c"
#include "gator_hrtimer_gator.c"
#include "gator_cookies.c"
#include "gator_trace_sched.c"
#include "gator_trace_gpu.c"
#include "gator_backtrace.c"
#include "gator_annotate.c"
#include "gator_fs.c"
#include "gator_ebs.c"
#include "gator_pack.c"

/******************************************************************************
 * Misc
 ******************************************************************************/
#if defined(__arm__)
u32 gator_cpuid(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r" (val));
	return (val >> 4) & 0xfff;
}
#endif

/******************************************************************************
 * Commit interface
 ******************************************************************************/
static bool buffer_commit_ready(int* cpu, int* buftype)
{
	int cpu_x, x;
	for_each_present_cpu(cpu_x) {
		for (x = 0; x < NUM_GATOR_BUFS; x++)
			if (per_cpu(gator_buffer_commit, cpu_x)[x] != per_cpu(gator_buffer_read, cpu_x)[x]) {
				*cpu = cpu_x;
				*buftype = x;
				return true;
			}
	}
	return false;
}

/******************************************************************************
 * Buffer management
 ******************************************************************************/
static bool buffer_check_space(int cpu, int buftype, int bytes)
{
	int remaining, filled;
	
	filled = per_cpu(gator_buffer_write, cpu)[buftype] - per_cpu(gator_buffer_read, cpu)[buftype];
	if (filled < 0) {
		filled += gator_buffer_size[buftype];
	}

	remaining = gator_buffer_size[buftype] - filled;

	if (per_cpu(buffer_space_available, cpu)[buftype]) {
		// Give some extra room; also allows space to insert the overflow error packet
		remaining -= 200;
	} else {
		// Hysteresis, prevents multiple overflow messages
		remaining -= 2000;
	}

	if (remaining < bytes) {
		if (per_cpu(buffer_space_available, cpu)[buftype] == true) {
			// overflow packet to be emitted at a later time, as we may be in the middle of writing a message, e.g. counters
			per_cpu(emit_overflow, cpu) = gator_get_time();
			pr_err("overflow: remaining = %d\n", gator_buffer_size[buftype] - filled);
		}
		per_cpu(buffer_space_available, cpu)[buftype] = false;
	} else {
		per_cpu(buffer_space_available, cpu)[buftype] = true;
	}

	return per_cpu(buffer_space_available, cpu)[buftype];
}

static void gator_buffer_write_bytes(int cpu, int buftype, char *x, int len)
{
	int i;
	u32 write = per_cpu(gator_buffer_write, cpu)[buftype];
	u32 mask = gator_buffer_mask[buftype];
	char* buffer = per_cpu(gator_buffer, cpu)[buftype];

	for (i = 0; i < len; i++) {
		buffer[write] = x[i];
		write = (write + 1) & mask;
	}

	per_cpu(gator_buffer_write, cpu)[buftype] = write;
}

static void gator_buffer_write_string(int cpu, int buftype, char *x)
{
	int len = strlen(x);
	gator_buffer_write_packed_int(cpu, buftype, len);
	gator_buffer_write_bytes(cpu, buftype, x, len);
}

static void gator_buffer_header(int cpu, int buftype)
{
	int frame;

	if (buftype == TIMER_BUF)
		frame = FRAME_HRTIMER;
	else if (buftype == EVENT_BUF)
		frame = FRAME_EVENT;
	else
		frame = -1;

	gator_buffer_write_packed_int(cpu, buftype, frame);
	gator_buffer_write_packed_int(cpu, buftype, cpu);
}

static void gator_commit_buffer(int cpu, int buftype)
{
	per_cpu(gator_buffer_commit, cpu)[buftype] = per_cpu(gator_buffer_write, cpu)[buftype];
	gator_buffer_header(cpu, buftype);
	wake_up(&gator_buffer_wait);
}

static void buffer_check(int cpu, int buftype)
{
	int filled = per_cpu(gator_buffer_write, cpu)[buftype] - per_cpu(gator_buffer_commit, cpu)[buftype];
	if (filled < 0) {
		filled += gator_buffer_size[buftype];
	}
	if (filled >= ((gator_buffer_size[buftype] * 3) / 4)) {
		gator_commit_buffer(cpu, buftype);
	}
}

static void gator_add_trace(int cpu, int buftype, unsigned int address)
{
	off_t offset = 0;
	unsigned long cookie = get_address_cookie(cpu, buftype, current, address & ~1, &offset);

	if (cookie == NO_COOKIE || cookie == INVALID_COOKIE) {
		offset = address;
	}

	gator_buffer_write_packed_int(cpu, buftype, offset & ~1);
	gator_buffer_write_packed_int(cpu, buftype, cookie);
}

static void gator_add_sample(int cpu, int buftype, struct pt_regs * const regs)
{
	int inKernel = regs ? !user_mode(regs) : 1;
	unsigned long exec_cookie = inKernel ? NO_COOKIE : get_exec_cookie(cpu, buftype, current);

	if (!regs)
		return;

	gator_buffer_write_packed_int(cpu, buftype, MESSAGE_START_BACKTRACE);
	gator_buffer_write_packed_int64(cpu, buftype, gator_get_time());
	gator_buffer_write_packed_int(cpu, buftype, exec_cookie);
	gator_buffer_write_packed_int(cpu, buftype, (unsigned int)current->tgid); 
	gator_buffer_write_packed_int(cpu, buftype, (unsigned int)current->pid);
	gator_buffer_write_packed_int(cpu, buftype, inKernel);

	if (inKernel) {
		kernel_backtrace(cpu, buftype, regs);
	} else {
		// Cookie+PC
		gator_add_trace(cpu, buftype, PC_REG);

		// Backtrace
		if (gator_backtrace_depth)
			arm_backtrace_eabi(cpu, buftype, regs, gator_backtrace_depth);
	}

	gator_buffer_write_packed_int(cpu, buftype, MESSAGE_END_BACKTRACE);
}

/******************************************************************************
 * hrtimer interrupt processing
 ******************************************************************************/
static LIST_HEAD(gator_events);

static void gator_timer_interrupt(void)
{
	struct pt_regs * const regs = get_irq_regs();
	int cpu = smp_processor_id();
	int *buffer, len, i, buftype = TIMER_BUF;
	long long *buffer64;
	struct gator_interface *gi;

	// Output scheduler trace
	len = gator_trace_sched_read(&buffer64);
	if (len > 0 && buffer_check_space(cpu, buftype, len * MAXSIZE_PACK64 + 2 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, buftype, MESSAGE_SCHEDULER_TRACE);
		gator_buffer_write_packed_int(cpu, buftype, len);
		for (i = 0; i < len; i++) {
			gator_buffer_write_packed_int64(cpu, buftype, buffer64[i]);
		}
	}

	// Output GPU trace
	len = gator_trace_gpu_read(&buffer64);
	if (len > 0 && buffer_check_space(cpu, buftype, len * MAXSIZE_PACK64 + 2 * MAXSIZE_PACK32)) {
		gator_buffer_write_packed_int(cpu, buftype, MESSAGE_GPU_TRACE);
		gator_buffer_write_packed_int(cpu, buftype, len);
		for (i = 0; i < len; i++) {
			gator_buffer_write_packed_int64(cpu, buftype, buffer64[i]);
		}
	}

	// Output counters
	if (buffer_check_space(cpu, buftype, MAXSIZE_PACK32 * 2 + MAXSIZE_PACK64)) {
		gator_buffer_write_packed_int(cpu, buftype, MESSAGE_COUNTERS);
		gator_buffer_write_packed_int64(cpu, buftype, gator_get_time());
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->read) {
				len = gi->read(&buffer);
				if (len > 0 && buffer_check_space(cpu, buftype, len * MAXSIZE_PACK32 + MAXSIZE_PACK32)) {
					gator_buffer_write_packed_int(cpu, buftype, len);
					for (i = 0; i < len; i++) {
						gator_buffer_write_packed_int(cpu, buftype, buffer[i]);
					}
				}
			} else if (gi->read64) {
				len = gi->read64(&buffer64);
				if (len > 0 && buffer_check_space(cpu, buftype, len * MAXSIZE_PACK64 + MAXSIZE_PACK32)) {
					gator_buffer_write_packed_int(cpu, buftype, len);
					for (i = 0; i < len; i++) {
						gator_buffer_write_packed_int64(cpu, buftype, buffer64[i]);
					}
				}
			}
		}
		gator_buffer_write_packed_int(cpu, buftype, 0);
	}

	// Output backtrace
	if (!event_based_sampling && buffer_check_space(cpu, buftype, gator_backtrace_depth * 2 * MAXSIZE_PACK32))
		gator_add_sample(cpu, buftype, regs);

	// Overflow message
	if (per_cpu(emit_overflow, cpu)) {
		gator_buffer_write_packed_int(cpu, buftype, MESSAGE_OVERFLOW);
		gator_buffer_write_packed_int64(cpu, buftype, per_cpu(emit_overflow, cpu));
		per_cpu(emit_overflow, cpu) = 0;
	}

	// Check and commit; generally, commit is set to occur once per second
	buffer_check(cpu, buftype);
}

DEFINE_PER_CPU(int, hrtimer_is_active);
static int hrtimer_running;

// This function runs in interrupt context and on the appropriate core
static void gator_timer_offline(void* unused)
{
	int i, len, cpu = smp_processor_id();
	int* buffer;
	long long* buffer64;

	if (per_cpu(hrtimer_is_active, cpu)) {
		struct gator_interface *gi;
		gator_hrtimer_offline(cpu);
		per_cpu(hrtimer_is_active, cpu) = 0;

		// Output scheduler trace
		len = gator_trace_sched_offline(&buffer64);
		if (len > 0 && buffer_check_space(cpu, TIMER_BUF, len * MAXSIZE_PACK64 + 2 * MAXSIZE_PACK32)) {
			gator_buffer_write_packed_int(cpu, TIMER_BUF, MESSAGE_SCHEDULER_TRACE);
			gator_buffer_write_packed_int(cpu, TIMER_BUF, len);
			for (i = 0; i < len; i++) {
				gator_buffer_write_packed_int64(cpu, TIMER_BUF, buffer64[i]);
			}
		}

		// Output GPU trace
		len = gator_trace_gpu_offline(&buffer64);
		if (len > 0 && buffer_check_space(cpu, TIMER_BUF, len * MAXSIZE_PACK64 + 2 * MAXSIZE_PACK32)) {
			gator_buffer_write_packed_int(cpu, TIMER_BUF, MESSAGE_GPU_TRACE);
			gator_buffer_write_packed_int(cpu, TIMER_BUF, len);
			for (i = 0; i < len; i++) {
				gator_buffer_write_packed_int64(cpu, TIMER_BUF, buffer64[i]);
			}
		}

		// offline any events and output counters
		gator_buffer_write_packed_int(cpu, TIMER_BUF, MESSAGE_COUNTERS);
		gator_buffer_write_packed_int64(cpu, TIMER_BUF, gator_get_time());
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->offline) {
				len = gi->offline(&buffer);
				if (len > 0 && buffer_check_space(cpu, TIMER_BUF, len * MAXSIZE_PACK32 + MAXSIZE_PACK32)) {
					gator_buffer_write_packed_int(cpu, TIMER_BUF, len);
					for (i = 0; i < len; i++)
						gator_buffer_write_packed_int(cpu, TIMER_BUF, buffer[i]);
				}
			}
		}
		gator_buffer_write_packed_int(cpu, TIMER_BUF, 0);

		gator_commit_buffer(cpu, TIMER_BUF);
	}

	if (event_based_sampling) {
		gator_commit_buffer(cpu, EVENT_BUF);
	}
}

// This function runs in interrupt context and may be running on a core other than core 'cpu'
static void gator_timer_offline_dispatch(int cpu)
{
	struct gator_interface *gi;

	list_for_each_entry(gi, &gator_events, list)
		if (gi->offline_dispatch)
			gi->offline_dispatch(cpu);

	gator_event_sampling_offline_dispatch(cpu);
}

static void gator_timer_stop(void)
{
	int cpu;

	if (hrtimer_running) {
		on_each_cpu(gator_timer_offline, NULL, 1);
		for_each_online_cpu(cpu) {
			gator_timer_offline_dispatch(cpu);
		}

		hrtimer_running = 0;
		gator_hrtimer_shutdown();
	}
}

// This function runs in interrupt context and on the appropriate core
static void gator_timer_online(void* unused)
{
	int i, len, cpu = smp_processor_id();
	int* buffer;

	if (!per_cpu(hrtimer_is_active, cpu)) {
		struct gator_interface *gi;

		// online any events and output counters
		gator_buffer_write_packed_int(cpu, TIMER_BUF, MESSAGE_COUNTERS);
		gator_buffer_write_packed_int64(cpu, TIMER_BUF, gator_get_time());
		list_for_each_entry(gi, &gator_events, list) {
			if (gi->online) {
				len = gi->online(&buffer);
				if (len > 0 && buffer_check_space(cpu, TIMER_BUF, len * MAXSIZE_PACK32 + MAXSIZE_PACK32)) {
					gator_buffer_write_packed_int(cpu, TIMER_BUF, len);
					for (i = 0; i < len; i++)
						gator_buffer_write_packed_int(cpu, TIMER_BUF, buffer[i]);
				}
			}
		}
		gator_buffer_write_packed_int(cpu, TIMER_BUF, 0);

		gator_event_sampling_online();

		gator_hrtimer_online(cpu);
		per_cpu(hrtimer_is_active, cpu) = 1;
	}
}

// This function runs in interrupt context and may be running on a core other than core 'cpu'
static void gator_timer_online_dispatch(int cpu)
{
	struct gator_interface *gi;

	list_for_each_entry(gi, &gator_events, list)
		if (gi->online_dispatch)
			gi->online_dispatch(cpu);

	gator_event_sampling_online_dispatch(cpu);
}

int gator_timer_start(unsigned long setup)
{
	int cpu;

	if (!setup) {
		pr_err("gator: cannot start due to a system tick value of zero\n");
		return -1;
	} else if (hrtimer_running) {
		pr_notice("gator: high res timer already running\n");
		return 0;
	}

	hrtimer_running = 1;

	if (gator_hrtimer_init(setup, gator_timer_interrupt) == -1)
		return -1;

	for_each_online_cpu(cpu) {
		gator_timer_online_dispatch(cpu);
	}
	on_each_cpu(gator_timer_online, NULL, 1);

	return 0;
}

static uint64_t gator_get_time(void)
{
	struct timespec ts;
	uint64_t timestamp;

	getnstimeofday(&ts);
	timestamp = timespec_to_ns(&ts);

	return timestamp;
}

/******************************************************************************
 * cpu hotplug and pm notifiers
 ******************************************************************************/
static int __cpuinit gator_cpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
		case CPU_DOWN_PREPARE:
		case CPU_DOWN_PREPARE_FROZEN:
			smp_call_function_single(cpu, gator_timer_offline, NULL, 1);
			gator_timer_offline_dispatch(cpu);
			break;
		case CPU_ONLINE:
		case CPU_ONLINE_FROZEN:
			gator_timer_online_dispatch(cpu);
			smp_call_function_single(cpu, gator_timer_online, NULL, 1);
			break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __refdata gator_cpu_notifier = {
	.notifier_call = gator_cpu_notify,
};

// n.b. calling "on_each_cpu" only runs on those that are online
// Registered linux events are not disabled, so their counters will continue to collect
static int gator_pm_notify(struct notifier_block *nb, unsigned long event, void *dummy)
{
	int cpu;

	switch (event) {
		case PM_HIBERNATION_PREPARE:
		case PM_SUSPEND_PREPARE:
			unregister_hotcpu_notifier(&gator_cpu_notifier);
			unregister_scheduler_tracepoints();
			on_each_cpu(gator_timer_offline, NULL, 1);
			for_each_online_cpu(cpu) {
				gator_timer_offline_dispatch(cpu);
			}
			break;
		case PM_POST_HIBERNATION:
		case PM_POST_SUSPEND:
			for_each_online_cpu(cpu) {
				gator_timer_online_dispatch(cpu);
			}
			on_each_cpu(gator_timer_online, NULL, 1);
			register_scheduler_tracepoints();
			register_hotcpu_notifier(&gator_cpu_notifier);
			break;
	}

	return NOTIFY_OK;
}

static struct notifier_block gator_pm_notifier = {
	.notifier_call = gator_pm_notify,
};

static int gator_notifier_start(void)
{
	int retval;
	retval = register_hotcpu_notifier(&gator_cpu_notifier);
	if (retval == 0)
		retval = register_pm_notifier(&gator_pm_notifier);
	return retval;
}

static void gator_notifier_stop(void)
{
	unregister_pm_notifier(&gator_pm_notifier);
	unregister_hotcpu_notifier(&gator_cpu_notifier);
}

/******************************************************************************
 * Main
 ******************************************************************************/
int gator_events_install(struct gator_interface *interface)
{
	list_add_tail(&interface->list, &gator_events);

	return 0;
}

int gator_events_get_key(void)
{
	static int key;

	return key++;
}

static int gator_init(void)
{
	int i;

	if (gator_annotate_init())
		return -1;

	// events sources (gator_events.h, generated by gator_events.sh)
	for (i = 0; i < ARRAY_SIZE(gator_events_list); i++)
		if (gator_events_list[i])
			gator_events_list[i]();

	return 0;
}

static int gator_start(void)
{
	struct gator_interface *gi;

	// start all events
	list_for_each_entry(gi, &gator_events, list) {
		if (gi->start && gi->start() != 0) {
			struct list_head *ptr = gi->list.prev;

			while (ptr != &gator_events) {
				gi = list_entry(ptr, struct gator_interface, list);

				if (gi->stop)
					gi->stop();

				ptr = ptr->prev;
			}
			goto events_failure;
		}
	}

	// cookies shall be initialized before trace_sched_start() and gator_timer_start()
	if (cookies_initialize())
		goto cookies_failure;
	if (gator_annotate_start())
		goto annotate_failure;
	if (gator_trace_sched_start())
		goto sched_failure;
	if (gator_trace_gpu_start())
		goto gpu_failure;
	if (gator_event_sampling_start())
		goto event_sampling_failure;
	if (gator_timer_start(gator_timer_count))
		goto timer_failure;
	if (gator_notifier_start())
		goto notifier_failure;

	return 0;

notifier_failure:
	gator_timer_stop();
timer_failure:
	gator_event_sampling_stop();
event_sampling_failure:
	gator_trace_gpu_stop();
gpu_failure:
	gator_trace_sched_stop();
sched_failure:
	gator_annotate_stop();
annotate_failure:
	cookies_release();
cookies_failure:
	// stop all events
	list_for_each_entry(gi, &gator_events, list)
		if (gi->stop)
			gi->stop();
events_failure:

	return -1;
}

static void gator_stop(void)
{
	struct gator_interface *gi;

	// stop all events
	list_for_each_entry(gi, &gator_events, list)
		if (gi->stop)
			gi->stop();

	gator_annotate_stop();
	gator_trace_sched_stop();
	gator_trace_gpu_stop();
	gator_event_sampling_stop();

	// stop all interrupt callback reads before tearing down other interfaces
	gator_notifier_stop(); // should be called before gator_timer_stop to avoid re-enabling the hrtimer after it has been offlined
	gator_timer_stop();
}

static void gator_exit(void)
{
	gator_annotate_exit();
}

/******************************************************************************
 * Filesystem
 ******************************************************************************/
/* fopen("buffer") */
static int gator_op_setup(void)
{
	int err = 0;
	int cpu, i;

	mutex_lock(&start_mutex);

	gator_buffer_size[TIMER_BUF] = userspace_buffer_size;
	gator_buffer_mask[TIMER_BUF] = userspace_buffer_size - 1;

	// must be a power of 2
	if (gator_buffer_size[TIMER_BUF] & (gator_buffer_size[TIMER_BUF] - 1)) {
		err = -ENOEXEC;
		goto setup_error;
	}

	gator_buffer_size[EVENT_BUF] = EVENT_BUFFER_SIZE_DEFAULT;
	gator_buffer_mask[EVENT_BUF] = gator_buffer_size[EVENT_BUF] - 1;

	// Initialize percpu per buffer variables
	for (i = 0; i < NUM_GATOR_BUFS; i++) {
		for_each_present_cpu(cpu) {
			per_cpu(gator_buffer, cpu)[i] = vmalloc(gator_buffer_size[i]);
			if (!per_cpu(gator_buffer, cpu)[i]) {
				err = -ENOMEM;
				goto setup_error;
			}

			per_cpu(gator_buffer_read, cpu)[i] = 0;
			per_cpu(gator_buffer_write, cpu)[i] = 0;
			per_cpu(gator_buffer_commit, cpu)[i] = 0;
			per_cpu(buffer_space_available, cpu)[i] = true;
			per_cpu(emit_overflow, cpu) = 0;
			gator_buffer_header(cpu, i);
		}
	}

setup_error:
	mutex_unlock(&start_mutex);
	return err;
}

/* Actually start profiling (echo 1>/dev/gator/enable) */
static int gator_op_start(void)
{
	int err = 0;

	mutex_lock(&start_mutex);

	if (gator_started || gator_start())
		err = -EINVAL;
	else
		gator_started = 1;

	mutex_unlock(&start_mutex);

	return err;
}

/* echo 0>/dev/gator/enable */
static void gator_op_stop(void)
{
	mutex_lock(&start_mutex);

	if (gator_started) {
		gator_stop();

		mutex_lock(&gator_buffer_mutex);

		gator_started = 0;
		cookies_release();
		wake_up(&gator_buffer_wait);

		mutex_unlock(&gator_buffer_mutex);
	}

	mutex_unlock(&start_mutex);
}

static void gator_shutdown(void)
{
	int cpu, i;

	mutex_lock(&start_mutex);

	gator_annotate_shutdown();

	for_each_present_cpu(cpu) {
		mutex_lock(&gator_buffer_mutex);
		for (i = 0; i < NUM_GATOR_BUFS; i++) {
			vfree(per_cpu(gator_buffer, cpu)[i]);
			per_cpu(gator_buffer, cpu)[i] = NULL;
			per_cpu(gator_buffer_read, cpu)[i] = 0;
			per_cpu(gator_buffer_write, cpu)[i] = 0;
			per_cpu(gator_buffer_commit, cpu)[i] = 0;
			per_cpu(buffer_space_available, cpu)[i] = true;
			per_cpu(emit_overflow, cpu) = 0;
		}
		mutex_unlock(&gator_buffer_mutex);
	}

	mutex_unlock(&start_mutex);
}

static int gator_set_backtrace(unsigned long val)
{
	int err = 0;

	mutex_lock(&start_mutex);

	if (gator_started)
		err = -EBUSY;
	else
		gator_backtrace_depth = val;

	mutex_unlock(&start_mutex);

	return err;
}

static ssize_t enable_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return gatorfs_ulong_to_user(gator_started, buf, count, offset);
}

static ssize_t enable_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = gatorfs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	if (val)
		retval = gator_op_start();
	else
		gator_op_stop();

	if (retval)
		return retval;
	return count;
}

static const struct file_operations enable_fops = {
	.read		= enable_read,
	.write		= enable_write,
};

static int userspace_buffer_open(struct inode *inode, struct file *file)
{
	int err = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (test_and_set_bit_lock(0, &gator_buffer_opened))
		return -EBUSY;

	if ((err = gator_op_setup()))
		goto fail;

	/* NB: the actual start happens from userspace
	 * echo 1 >/dev/gator/enable
	 */

	return 0;

fail:
	__clear_bit_unlock(0, &gator_buffer_opened);
	return err;
}

static int userspace_buffer_release(struct inode *inode, struct file *file)
{
	gator_op_stop();
	gator_shutdown();
	__clear_bit_unlock(0, &gator_buffer_opened);
	return 0;
}

static ssize_t userspace_buffer_read(struct file *file, char __user *buf,
				 size_t count, loff_t *offset)
{
	int retval = -EINVAL;
	int commit = 0, length1, length2, read;
	char *buffer1;
	char *buffer2 = NULL;
	int cpu, buftype;

	/* do not handle partial reads */
	if (count != userspace_buffer_size || *offset)
		return -EINVAL;

	// sleep until the condition is true or a signal is received
	// the condition is checked each time gator_buffer_wait is woken up
	buftype = cpu = -1;
	wait_event_interruptible(gator_buffer_wait, buffer_commit_ready(&cpu, &buftype) || gator_annotate_ready() || !gator_started);

	if (signal_pending(current))
		return -EINTR;

	length2 = 0;
	retval = -EFAULT;

	mutex_lock(&gator_buffer_mutex);

	if (buftype != -1 && cpu != -1) {
		read = per_cpu(gator_buffer_read, cpu)[buftype];
		commit = per_cpu(gator_buffer_commit, cpu)[buftype];

		/* May happen if the buffer is freed during pending reads. */
		if (!per_cpu(gator_buffer, cpu)[buftype]) {
			retval = -EFAULT;
			goto out;
		}

		/* determine the size of two halves */
		length1 = commit - read;
		buffer1 = &(per_cpu(gator_buffer, cpu)[buftype][read]);
		buffer2 = &(per_cpu(gator_buffer, cpu)[buftype][0]);
		if (length1 < 0) {
			length1 = gator_buffer_size[buftype] - read;
			length2 = commit;
		}
	} else if (gator_annotate_ready()) {
		length1 = gator_annotate_read(&buffer1);
		if (!length1)
			goto out;
	} else {
		retval = 0;
		goto out;
	}

	/* start, middle or end */
	if (length1 > 0) {
		if (copy_to_user(&buf[0], buffer1, length1)) {
			goto out;
		}
	}

	/* possible wrap around */
	if (length2 > 0) {
		if (copy_to_user(&buf[length1], buffer2, length2)) {
			goto out;
		}
	}

	if (buftype != -1 && cpu != -1)
		per_cpu(gator_buffer_read, cpu)[buftype] = commit;

	retval = length1 + length2;

	/* kick just in case we've lost an SMP event */
	wake_up(&gator_buffer_wait);

out:
	mutex_unlock(&gator_buffer_mutex);
	return retval;
}

const struct file_operations gator_event_buffer_fops = {
	.open		= userspace_buffer_open,
	.release	= userspace_buffer_release,
	.read		= userspace_buffer_read,
};

static ssize_t depth_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return gatorfs_ulong_to_user(gator_backtrace_depth, buf, count,
					offset);
}

static ssize_t depth_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = gatorfs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	retval = gator_set_backtrace(val);

	if (retval)
		return retval;
	return count;
}

static const struct file_operations depth_fops = {
	.read		= depth_read,
	.write		= depth_write
};

void gator_op_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	struct gator_interface *gi;
	int cpu;

	/* reinitialize default values */
	gator_cpu_cores = 0;
	for_each_present_cpu(cpu) {
		gator_cpu_cores++;
	}
	userspace_buffer_size =	TIMER_BUFFER_SIZE_DEFAULT;
	gator_streaming = 1;

	gatorfs_create_file(sb, root, "enable", &enable_fops);
	gatorfs_create_file(sb, root, "buffer", &gator_event_buffer_fops);
	gatorfs_create_file(sb, root, "backtrace_depth", &depth_fops);
	gatorfs_create_ulong(sb, root, "cpu_cores", &gator_cpu_cores);
	gatorfs_create_ulong(sb, root, "buffer_size", &userspace_buffer_size);
	gatorfs_create_ulong(sb, root, "tick", &gator_timer_count);
	gatorfs_create_ulong(sb, root, "streaming", &gator_streaming);
	gatorfs_create_ro_ulong(sb, root, "version", &gator_protocol_version);

	// Annotate interface
	gator_annotate_create_files(sb, root);

	// Linux Events
	dir = gatorfs_mkdir(sb, root, "events");
	list_for_each_entry(gi, &gator_events, list)
		if (gi->create_files)
			gi->create_files(sb, dir);
}

/******************************************************************************
 * Module
 ******************************************************************************/
static int __init gator_module_init(void)
{
	if (gatorfs_register()) {
		return -1;
	}

	if (gator_init()) {
		gatorfs_unregister();
		return -1;
	}

	return 0;
}

static void __exit gator_module_exit(void)
{
	tracepoint_synchronize_unregister();
	gatorfs_unregister();
	gator_exit();
}

module_init(gator_module_init);
module_exit(gator_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd");
MODULE_DESCRIPTION("Gator system profiler");
