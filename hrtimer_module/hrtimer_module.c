/**
 * Copyright (C) Arm Limited 2010-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/cpu.h>
#include <linux/hrtimer.h>
#include <linux/module.h>

static DEFINE_PER_CPU(struct hrtimer, percpu_hrtimer);
static DEFINE_PER_CPU(int, hrtimer_is_active);
static DEFINE_PER_CPU(ktime_t, hrtimer_expire);
static DEFINE_PER_CPU(int, hrtimer_count);
static DEFINE_PER_CPU(int, hrtimer_fast);
static DEFINE_PER_CPU(int, hrtimer_ok);
static DEFINE_PER_CPU(int, hrtimer_slow);
static DEFINE_PER_CPU(s64, hrtimer_last_hrtimer);
static DEFINE_PER_CPU(s64, percpu_timestamp);
static int hrtimer_running;
static ktime_t interval;

static enum hrtimer_restart hrtimer_notify(struct hrtimer *hrtimer)
{
    struct timespec ts;
    s64 timestamp;
    s64 last_hrtimer;
    int cpu;

    cpu = smp_processor_id();
    hrtimer_forward(hrtimer, per_cpu(hrtimer_expire, cpu), interval);
    per_cpu(hrtimer_expire, cpu) = ktime_add(per_cpu(hrtimer_expire, cpu), interval);

    getnstimeofday(&ts);
    timestamp = timespec_to_ns(&ts);
    last_hrtimer = per_cpu(hrtimer_last_hrtimer, cpu);
    per_cpu(hrtimer_last_hrtimer, cpu) = timestamp;

    if (last_hrtimer == 0) {
        /* Set the initial value */
        per_cpu(percpu_timestamp, cpu) = timestamp;
    } else {
        s64 delta = timestamp - last_hrtimer;

        per_cpu(hrtimer_count, cpu)++;
        if (delta < NSEC_PER_MSEC/2)
            per_cpu(hrtimer_fast, cpu)++;
        else if (delta > 2*NSEC_PER_MSEC)
            per_cpu(hrtimer_slow, cpu)++;
        else
            per_cpu(hrtimer_ok, cpu)++;

        if (per_cpu(hrtimer_count, cpu) % 1000 == 0) {
            const char *result;
            s64 last_timestamp = per_cpu(percpu_timestamp, cpu);
            s64 jitter = timestamp - last_timestamp - NSEC_PER_SEC;

            if (jitter < 0)
                jitter = -jitter;

            if ((per_cpu(hrtimer_ok, cpu) >= 800) &&
                (jitter < NSEC_PER_SEC/10))
                result = "pass";
            else
                result = "fail";

            pr_err("core: %d hrtimer: %s (jitter %lld, too fast %d, ok %d, too slow %d)\n",
                   cpu, result, jitter, per_cpu(hrtimer_fast, cpu),
                   per_cpu(hrtimer_ok, cpu), per_cpu(hrtimer_slow,
                                 cpu));

            per_cpu(hrtimer_fast, cpu) = 0;
            per_cpu(hrtimer_ok, cpu) = 0;
            per_cpu(hrtimer_slow, cpu) = 0;

            per_cpu(percpu_timestamp, cpu) = timestamp;
        }
    }

    return HRTIMER_RESTART;
}

static void __timer_offline(void *unused)
{
    int cpu = smp_processor_id();
    if (per_cpu(hrtimer_is_active, cpu)) {
        struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);
        hrtimer_cancel(hrtimer);
        per_cpu(hrtimer_is_active, cpu) = 0;
    }
}

static void timer_offline(void)
{
    if (hrtimer_running) {
        hrtimer_running = 0;

        on_each_cpu(__timer_offline, NULL, 1);
    }
}

static void __timer_online(void *unused)
{
    int cpu = smp_processor_id();
    if (!per_cpu(hrtimer_is_active, cpu)) {
        struct hrtimer *hrtimer = &per_cpu(percpu_hrtimer, cpu);
        hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
        hrtimer->function = hrtimer_notify;
        per_cpu(hrtimer_expire, cpu) = ktime_add(hrtimer->base->get_time(), interval);
        hrtimer_start(hrtimer, per_cpu(hrtimer_expire, cpu), HRTIMER_MODE_ABS_PINNED);
        per_cpu(hrtimer_is_active, cpu) = 1;
    }
}

static int timer_online(unsigned long setup)
{
    if (!setup) {
        pr_err("hrtimer_module: cannot start due to a hrtimer value of zero");
        return -1;
    } else if (hrtimer_running) {
        pr_notice("hrtimer_module: high res timer already running");
        return 0;
    }

    hrtimer_running = 1;
    interval = ns_to_ktime(1000000000UL / setup);
    on_each_cpu(__timer_online, NULL, 1);

    return 0;
}

static int __init hrtimer_module_init(void)
{
    printk(KERN_ERR "hrtimer module init\n");
    timer_online(1000); // number of interrupts per second per core
    return 0;
}

static void __exit hrtimer_module_exit(void)
{
    printk(KERN_ERR "hrtimer module exit\n");
    timer_offline();
}

module_init(hrtimer_module_init);
module_exit(hrtimer_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arm Ltd");
MODULE_DESCRIPTION("hrtimer module");
