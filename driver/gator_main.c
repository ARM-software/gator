/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static unsigned long gator_protocol_version = 6;

#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/hardirq.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>

#include "gator.h"
#include "gator_events.h"

#ifndef CONFIG_GENERIC_TRACER
#ifndef CONFIG_TRACING
#error gator requires the kernel to have CONFIG_GENERIC_TRACER or CONFIG_TRACING defined
#endif
#endif

#ifndef CONFIG_PROFILING
#error gator requires the kernel to have CONFIG_PROFILING defined
#endif

#ifndef CONFIG_HIGH_RES_TIMERS
#error gator requires the kernel to have CONFIG_HIGH_RES_TIMERS defined
#endif

#if defined (__arm__)
#ifdef CONFIG_SMP
#ifndef CONFIG_LOCAL_TIMERS
#error gator requires the kernel to have CONFIG_LOCAL_TIMERS defined on SMP systems
#endif
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
#error kernels prior to 2.6.32 are not supported
#endif

/******************************************************************************
 * DEFINES
 ******************************************************************************/
#define TIMER_BUFFER_SIZE_DEFAULT	(256*1024)
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

#define LINUX_PMU_SUPPORT LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35) && defined(CONFIG_CPU_HAS_PMU)

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

unsigned long gator_net_traffic;
bool event_based_sampling;

#define COMMIT_SIZE		128
#define COMMIT_MASK		(COMMIT_SIZE-1)
static DEFINE_SPINLOCK(timer_commit_lock);
static int *gator_commit[NUM_GATOR_BUFS];
static int gator_commit_read[NUM_GATOR_BUFS];
static int gator_commit_write[NUM_GATOR_BUFS];

static DECLARE_WAIT_QUEUE_HEAD(gator_buffer_wait);
static DEFINE_PER_CPU(int, gator_first_time);

#if LINUX_PMU_SUPPORT
static void event_buffer_check(int cpu);
static DEFINE_SPINLOCK(event_commit_lock);
#endif

/******************************************************************************
 * Prototypes
 ******************************************************************************/
static void gator_buffer_write_packed_int(int cpu, int buftype, unsigned int x);
static void gator_buffer_write_packed_int64(int cpu, int buftype, unsigned long long x);
static void gator_buffer_write_string(int cpu, int buftype, char *x);
static int  gator_write_packed_int(char *buffer, unsigned int x);
static int  gator_write_packed_int64(char *buffer, unsigned long long x);
static void gator_add_trace(int cpu, int buftype, unsigned int address);
static void gator_add_sample(int cpu, int buftype, struct pt_regs * const regs);
static uint64_t gator_get_time(void);

/******************************************************************************
 * Application Includes
 ******************************************************************************/
#include "gator_cookies.c"
#include "gator_trace_sched.c"
#include "gator_backtrace.c"
#include "gator_annotate.c"
#include "gator_fs.c"
#include "gator_ebs.c"

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
static int buffer_commit_ready(int buftype)
{
	return gator_commit_read[buftype] != gator_commit_write[buftype];
}

static void buffer_commit_read(int *cpu, int buftype, int *readval, int *writeval)
{
	int read = gator_commit_read[buftype];
	*cpu      = gator_commit[buftype][read+0];
	*readval  = gator_commit[buftype][read+1];
	*writeval = gator_commit[buftype][read+2];
	gator_commit_read[buftype] = (read + 4) & COMMIT_MASK;
}

static void buffer_commit_write(int cpu, int buftype, int readval, int writeval) {
	int write = gator_commit_write[buftype];
	gator_commit[buftype][write+0] = cpu;
	gator_commit[buftype][write+1] = readval;
	gator_commit[buftype][write+2] = writeval;
	gator_commit_write[buftype] = (write + 4) & COMMIT_MASK;
}

/******************************************************************************
 * Buffer management
 ******************************************************************************/
static uint32_t gator_buffer_size[NUM_GATOR_BUFS];
static uint32_t gator_buffer_mask[NUM_GATOR_BUFS];
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_read);
static DEFINE_PER_CPU(int[NUM_GATOR_BUFS], gator_buffer_write);
static DEFINE_PER_CPU(char *[NUM_GATOR_BUFS], gator_buffer);
#include "gator_pack.c"

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

static void gator_buffer_commit(int cpu, int buftype)
{
	buffer_commit_write(cpu, buftype, per_cpu(gator_buffer_read, cpu)[buftype], per_cpu(gator_buffer_write, cpu)[buftype]);
	per_cpu(gator_buffer_read, cpu)[buftype] = per_cpu(gator_buffer_write, cpu)[buftype];
	gator_buffer_header(cpu, buftype);
	wake_up(&gator_buffer_wait);
}

static void timer_buffer_check(int cpu)
{
	int available = per_cpu(gator_buffer_write, cpu)[TIMER_BUF] - per_cpu(gator_buffer_read, cpu)[TIMER_BUF];
	if (available < 0) {
		available += gator_buffer_size[TIMER_BUF];
	}
	if (available >= ((gator_buffer_size[TIMER_BUF] * 3) / 4)) {
		spin_lock(&timer_commit_lock);
		gator_buffer_commit(cpu, TIMER_BUF);
		spin_unlock(&timer_commit_lock);
	}
}

#if LINUX_PMU_SUPPORT
static void event_buffer_check(int cpu)
{
	int available = per_cpu(gator_buffer_write, cpu)[EVENT_BUF] - per_cpu(gator_buffer_read, cpu)[EVENT_BUF];
	if (available < 0) {
		available += gator_buffer_size[EVENT_BUF];
	}
	if (available >= ((gator_buffer_size[EVENT_BUF] * 3) / 4)) {
		spin_lock(&event_commit_lock);
		gator_buffer_commit(cpu, EVENT_BUF);
		spin_unlock(&event_commit_lock);
	}
}
#endif

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
	struct module *mod;
	unsigned int addr, cookie = 0;
	int inKernel = regs ? !user_mode(regs) : 1;
	unsigned long exec_cookie = inKernel ? NO_COOKIE : get_exec_cookie(cpu, buftype, current);

	gator_buffer_write_packed_int(cpu, buftype, MESSAGE_START_BACKTRACE);
	gator_buffer_write_packed_int64(cpu, buftype, gator_get_time());
	gator_buffer_write_packed_int(cpu, buftype, exec_cookie);
	gator_buffer_write_packed_int(cpu, buftype, (unsigned int)current->tgid); 
	gator_buffer_write_packed_int(cpu, buftype, (unsigned int)current->pid);
	gator_buffer_write_packed_int(cpu, buftype, inKernel);

	// get_irq_regs() will return NULL outside of IRQ context (e.g. nested IRQ)
	if (regs) {
		if (inKernel) {
			addr = PC_REG;
			mod = __module_address(addr);
			if (mod) {
				cookie = get_cookie(cpu, buftype, current, NULL, mod, true);
				addr = addr - (unsigned long)mod->module_core;
			}
			gator_buffer_write_packed_int(cpu, buftype, addr & ~1);
			gator_buffer_write_packed_int(cpu, buftype, cookie);
		} else {
			// Cookie+PC
			gator_add_trace(cpu, buftype, PC_REG);

			// Backtrace
			if (gator_backtrace_depth)
				arm_backtrace_eabi(cpu, buftype, regs, gator_backtrace_depth);
		}
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

	// check full backtrace has enough space, otherwise may
	// have breaks between samples in the same callstack
	if (per_cpu(gator_first_time, cpu)) {
		per_cpu(gator_first_time, cpu) = 0;

		list_for_each_entry(gi, &gator_events, list)
			if (gi->read)
				gi->read(NULL);

		return;
	}

	// Output scheduler
	len = gator_trace_sched_read(&buffer64);
	if (len > 0) {
		gator_buffer_write_packed_int(cpu, buftype, MESSAGE_SCHEDULER_TRACE);
		gator_buffer_write_packed_int(cpu, buftype, len);
		for (i = 0; i < len; i++) {
			gator_buffer_write_packed_int64(cpu, buftype, buffer64[i]);
		}
	}

	// Output counters
	gator_buffer_write_packed_int(cpu, buftype, MESSAGE_COUNTERS);
	gator_buffer_write_packed_int64(cpu, buftype, gator_get_time());
	list_for_each_entry(gi, &gator_events, list) {
		if (gi->read) {
			len = gi->read(&buffer);
			if (len > 0) {
				gator_buffer_write_packed_int(cpu, buftype, len);
				for (i = 0; i < len; i++) {
					gator_buffer_write_packed_int(cpu, buftype, buffer[i]);
				}
			}
		} else if (gi->read64) {
			len = gi->read64(&buffer64);
			if (len > 0)
				gator_buffer_write_packed_int(cpu, buftype, len);
				for (i = 0; i < len; i++) {
					gator_buffer_write_packed_int64(cpu, buftype, buffer64[i]);
				}
		}
	}
	gator_buffer_write_packed_int(cpu, buftype, 0);

	// Output backtrace
	if (!event_based_sampling) {
		gator_add_sample(cpu, buftype, regs);
	}

	// Check and commit; generally, commit is set to occur once per second
	timer_buffer_check(cpu);
}

DEFINE_PER_CPU(struct hrtimer, percpu_hrtimer);
DEFINE_PER_CPU(int, hrtimer_is_active);
static int hrtimer_running;
static ktime_t profiling_interval;

static enum hrtimer_restart gator_hrtimer_notify(struct hrtimer *hrtimer)
{
	hrtimer_forward_now(hrtimer, profiling_interval);
	gator_timer_interrupt();
	return HRTIMER_RESTART;
}

static int gator_timer_init(void)
{
	return 0;
}

static void __gator_timer_offline(void *unused)
{
	int cpu = smp_processor_id();
	if (per_cpu(hrtimer_is_active, cpu)) {
		struct gator_interface *gi;
		struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);
		hrtimer_cancel(hrtimer);
		per_cpu(hrtimer_is_active, cpu) = 0;
		gator_buffer_commit(cpu, TIMER_BUF);
		if (event_based_sampling)
			gator_buffer_commit(cpu, EVENT_BUF);

		// offline any events
		list_for_each_entry(gi, &gator_events, list)
			if (gi->offline)
				gi->offline();
	}
}

static void gator_timer_offline(void)
{
	if (hrtimer_running) {
		hrtimer_running = 0;

		on_each_cpu(__gator_timer_offline, NULL, 1);
	}
}

static void __gator_timer_online(void *unused)
{
	int cpu = smp_processor_id();
	if (!per_cpu(hrtimer_is_active, cpu)) {
		struct gator_interface *gi;
		struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);
		hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		hrtimer->function = gator_hrtimer_notify;
		hrtimer_start(hrtimer, profiling_interval, HRTIMER_MODE_REL_PINNED);
		per_cpu(gator_first_time, cpu) = 1;
		per_cpu(hrtimer_is_active, cpu) = 1;

		// online any events
		list_for_each_entry(gi, &gator_events, list)
			if (gi->online)
				gi->online();
	}
}

int gator_timer_online(unsigned long setup)
{
	if (!setup) {
		pr_err("gator: cannot start due to a system tick value of zero\n");
		return -1;
	} else if (hrtimer_running) {
		pr_notice("gator: high res timer already running\n");
		return 0;
	}

	hrtimer_running = 1;

	// calculate profiling interval
	profiling_interval = ns_to_ktime(1000000000UL / setup);

	// timer interrupt
	on_each_cpu(__gator_timer_online, NULL, 1);

	return 0;
}

static uint64_t gator_get_time(void)
{
	struct timespec ts;
	uint64_t timestamp;

	ktime_get_ts(&ts);
	timestamp = timespec_to_ns(&ts);

	return timestamp;
}

/******************************************************************************
 * cpu online notifier
 ******************************************************************************/
static int __cpuinit gator_cpu_notify(struct notifier_block *self,
											unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {
		case CPU_ONLINE:
		case CPU_ONLINE_FROZEN:
			smp_call_function_single(cpu, __gator_timer_online, NULL, 1);
			break;
		case CPU_DOWN_PREPARE:
		case CPU_DOWN_PREPARE_FROZEN:
			smp_call_function_single(cpu, __gator_timer_offline, NULL, 1);
			break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __refdata gator_cpu_notifier = {
	.notifier_call = gator_cpu_notify,
};

static int gator_notifier_start(void)
{
	return register_hotcpu_notifier(&gator_cpu_notifier);
}

static void gator_notifier_stop(void)
{
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

	if (gator_timer_init())
		return -1;
	if (gator_trace_sched_init())
		return -1;
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
				gi = list_entry(ptr, struct gator_interface,
						list);

				if (gi->stop)
					gi->stop();

				ptr = ptr->prev;
			}
			goto events_failure;
		}
	}

	// cookies shall be initialized before trace_sched_start() and gator_timer_online()
	if (cookies_initialize())
		goto cookies_failure;
	if (gator_annotate_start())
		goto annotate_failure;
	if (gator_trace_sched_start())
		goto sched_failure;
	if (gator_event_sampling_start())
		goto event_sampling_failure;
	if (gator_timer_online(gator_timer_count))
		goto timer_failure;
	if (gator_notifier_start())
		goto notifier_failure;

	return 0;

notifier_failure:
	gator_timer_offline();
timer_failure:
	gator_event_sampling_stop();
event_sampling_failure:
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
	gator_event_sampling_stop();

	// stop all interrupt callback reads before tearing down other interfaces
	gator_notifier_stop(); // should be called before gator_timer_offline to avoid re-enabling the hrtimer after it has been offlined
	gator_timer_offline();
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

	gator_net_traffic = 0;

	// Initialize per buffer variables
	for (i = 0; i < NUM_GATOR_BUFS; i++) {
		gator_commit_read[i] = gator_commit_write[i] = 0;
		gator_commit[i] = vmalloc(COMMIT_SIZE * sizeof(int));
		if (!gator_commit[i]) {
			err = -ENOMEM;
			goto setup_error;
		}

		// Initialize percpu per buffer variables
		for_each_present_cpu(cpu) {
			per_cpu(gator_buffer, cpu)[i] = vmalloc(gator_buffer_size[i]);
			if (!per_cpu(gator_buffer, cpu)[i]) {
				err = -ENOMEM;
				goto setup_error;
			}

			per_cpu(gator_buffer_read, cpu)[i] = 0;
			per_cpu(gator_buffer_write, cpu)[i] = 0;
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

	for (i = 0; i < NUM_GATOR_BUFS; i++) {
		vfree(gator_commit[i]);
		gator_commit[i] = NULL;
	}

	for_each_present_cpu(cpu) {
		mutex_lock(&gator_buffer_mutex);
		for (i = 0; i < NUM_GATOR_BUFS; i++) {
			vfree(per_cpu(gator_buffer, cpu)[i]);
			per_cpu(gator_buffer, cpu)[i] = NULL;
			per_cpu(gator_buffer_read, cpu)[i] = 0;
			per_cpu(gator_buffer_write, cpu)[i] = 0;
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
	int commit, length1, length2, read;
	char *buffer1;
	char *buffer2 = NULL;
	int cpu, i;

	/* do not handle partial reads */
	if (count != userspace_buffer_size || *offset)
		return -EINVAL;

	// sleep until the condition is true or a signal is received
	// the condition is checked each time gator_buffer_wait is woken up
	wait_event_interruptible(gator_buffer_wait, buffer_commit_ready(TIMER_BUF) || buffer_commit_ready(EVENT_BUF) || gator_annotate_ready() || !gator_started);

	if (signal_pending(current))
		return -EINTR;

	length2 = 0;
	retval = -EFAULT;

	mutex_lock(&gator_buffer_mutex);

	i = -1;
	if (buffer_commit_ready(TIMER_BUF)) {
		i = TIMER_BUF;
	} else if (buffer_commit_ready(EVENT_BUF)) {
		i = EVENT_BUF;
	}

	if (i != -1) {
		buffer_commit_read(&cpu, i, &read, &commit);

		/* May happen if the buffer is freed during pending reads. */
		if (!per_cpu(gator_buffer, cpu)[i]) {
			retval = -EFAULT;
			goto out;
		}

		/* determine the size of two halves */
		length1 = commit - read;
		buffer1 = &(per_cpu(gator_buffer, cpu)[i][read]);
		buffer2 = &(per_cpu(gator_buffer, cpu)[i][0]);
		if (length1 < 0) {
			length1 = gator_buffer_size[i] - read;
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

	retval = length1 + length2;

	/* kick just in case we've lost an SMP event */
	wake_up(&gator_buffer_wait);

out:
	// only adjust network stats if in streaming mode
	if (gator_streaming)
		gator_net_traffic += retval;
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
