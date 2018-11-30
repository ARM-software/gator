/**
 * Copyright (C) Arm Limited 2011-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

static void (*callback)(void);
static DEFINE_PER_CPU(struct hrtimer, percpu_hrtimer);
static DEFINE_PER_CPU(ktime_t, hrtimer_expire);
static DEFINE_PER_CPU(int, hrtimer_is_active);
static ktime_t profiling_interval;
static void gator_hrtimer_online(void);
static void gator_hrtimer_offline(void);

static enum hrtimer_restart gator_hrtimer_notify(struct hrtimer *hrtimer)
{
    int cpu = get_logical_cpu();

    hrtimer_forward(hrtimer, per_cpu(hrtimer_expire, cpu), profiling_interval);
    per_cpu(hrtimer_expire, cpu) = ktime_add(per_cpu(hrtimer_expire, cpu), profiling_interval);
    (*callback)();
    return HRTIMER_RESTART;
}

static void gator_hrtimer_online(void)
{
    int cpu = get_logical_cpu();
    struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);

    if (per_cpu(hrtimer_is_active, cpu) || (ktime_to_ns(profiling_interval) == 0))
        return;

    per_cpu(hrtimer_is_active, cpu) = 1;
    hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
    hrtimer->function = gator_hrtimer_notify;
#if defined(CONFIG_PREEMPT_RT_BASE) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
    /* irqsafe is removed between preempt_rt 4.9 and 4.11 */
    hrtimer->irqsafe = 1;
#endif
    per_cpu(hrtimer_expire, cpu) = ktime_add(hrtimer->base->get_time(), profiling_interval);
    hrtimer_start(hrtimer, per_cpu(hrtimer_expire, cpu), HRTIMER_MODE_ABS_PINNED);
}

static void gator_hrtimer_offline(void)
{
    int cpu = get_logical_cpu();
    struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);

    if (!per_cpu(hrtimer_is_active, cpu))
        return;

    per_cpu(hrtimer_is_active, cpu) = 0;
    hrtimer_cancel(hrtimer);
}

static int gator_hrtimer_init(int interval, void (*func)(void))
{
    int cpu;

    (callback) = (func);

    for_each_present_cpu(cpu) {
        per_cpu(hrtimer_is_active, cpu) = 0;
    }

    /* calculate profiling interval */
    if (interval > 0)
        profiling_interval = ns_to_ktime(1000000000UL / interval);
    else
        profiling_interval = ns_to_ktime(0);

    return 0;
}

static void gator_hrtimer_shutdown(void)
{
    /* empty */
}
