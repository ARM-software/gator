/**
 * Copyright (C) ARM Limited 2011-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <linux/slab.h>
#include <linux/cpufreq.h>
#include <trace/events/power.h>

// cpu_frequency and cpu_idle trace points were introduced in Linux kernel v2.6.38
// the now deprecated power_frequency trace point was available prior to 2.6.38, but only for x86
// CPU Idle is currently disabled in the .xml
#if GATOR_CPU_FREQ_SUPPORT
enum {
	POWER_CPU_FREQ,
	POWER_CPU_IDLE,
	POWER_TOTAL
};

static ulong power_cpu_enabled[POWER_TOTAL];
static ulong power_cpu_key[POWER_TOTAL];
static DEFINE_PER_CPU(ulong[POWER_TOTAL], power);
static DEFINE_PER_CPU(ulong[POWER_TOTAL], prev);
static DEFINE_PER_CPU(int *, powerGet);

GATOR_DEFINE_PROBE(cpu_frequency, TP_PROTO(unsigned int frequency, unsigned int cpu))
{
	per_cpu(power, cpu)[POWER_CPU_FREQ] = frequency * 1000;
}

GATOR_DEFINE_PROBE(cpu_idle, TP_PROTO(unsigned int state, unsigned int cpu))
{
	per_cpu(power, cpu)[POWER_CPU_IDLE] = state;
}

static int gator_events_power_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	// cpu_frequency
	dir = gatorfs_mkdir(sb, root, "Linux_power_cpu_freq");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &power_cpu_enabled[POWER_CPU_FREQ]);
	gatorfs_create_ro_ulong(sb, dir, "key", &power_cpu_key[POWER_CPU_FREQ]);

	// cpu_idle
	dir = gatorfs_mkdir(sb, root, "Linux_power_cpu_idle");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &power_cpu_enabled[POWER_CPU_IDLE]);
	gatorfs_create_ro_ulong(sb, dir, "key", &power_cpu_key[POWER_CPU_IDLE]);

	return 0;
}

static int gator_events_power_populate(int cpu, int** buffer)
{
	int i, len = 0;

	for (i = 0; i < POWER_TOTAL; i++) {
		if (power_cpu_enabled[i]) {
			if (per_cpu(power, cpu)[i] != per_cpu(prev, cpu)[i]) {
				per_cpu(prev, cpu)[i] = per_cpu(power, cpu)[i];
				per_cpu(powerGet, cpu)[len++] = power_cpu_key[i];
				per_cpu(powerGet, cpu)[len++] = per_cpu(power, cpu)[i];
			}
		}
	}

	if (buffer)
		*buffer = per_cpu(powerGet, cpu);

	return len;
}

static int gator_events_power_online(int** buffer)
{
	int i, cpu = smp_processor_id();
	for (i = 0; i < POWER_TOTAL; i++)
		per_cpu(prev, cpu)[i] = -1;
	per_cpu(power, cpu)[POWER_CPU_FREQ] = cpufreq_quick_get(cpu) * 1000;
	return gator_events_power_populate(cpu, buffer);
}

static int gator_events_power_offline(int** buffer)
{
	int cpu = smp_processor_id();
	// Set frequency to zero on an offline
	per_cpu(power, cpu)[POWER_CPU_FREQ] = 0;
	return gator_events_power_populate(cpu, buffer);
}

static int gator_events_power_start(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		per_cpu(powerGet, cpu) = kmalloc(POWER_TOTAL * 2, GFP_KERNEL);
		if (!per_cpu(powerGet, cpu))
			return -1;
	}

	// register tracepoints
	if (power_cpu_enabled[POWER_CPU_FREQ])
		if (GATOR_REGISTER_TRACE(cpu_frequency))
			goto fail_cpu_frequency_exit;
	if (power_cpu_enabled[POWER_CPU_IDLE])
		if (GATOR_REGISTER_TRACE(cpu_idle))
			goto fail_cpu_idle_exit;
	pr_debug("gator: registered power event tracepoints\n");

	return 0;

	// unregister tracepoints on error
fail_cpu_idle_exit:
	GATOR_UNREGISTER_TRACE(cpu_frequency);
fail_cpu_frequency_exit:
	pr_err("gator: power event tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

static void gator_events_power_stop(void)
{
	int i, cpu;
	if (power_cpu_enabled[POWER_CPU_FREQ])
		GATOR_UNREGISTER_TRACE(cpu_frequency);
	if (power_cpu_enabled[POWER_CPU_IDLE])
		GATOR_UNREGISTER_TRACE(cpu_idle);
	pr_debug("gator: unregistered power event tracepoints\n");

	for (i = 0; i < POWER_TOTAL; i++) {
		power_cpu_enabled[i] = 0;
	}

	for_each_present_cpu(cpu) {
		kfree(per_cpu(powerGet, cpu));
	}
}

static int gator_events_power_read(int **buffer)
{
	return gator_events_power_populate(smp_processor_id(), buffer);
}

static struct gator_interface gator_events_power_interface = {
	.create_files = gator_events_power_create_files,
	.online = gator_events_power_online,
	.offline = gator_events_power_offline,
	.start = gator_events_power_start,
	.stop = gator_events_power_stop,
	.read = gator_events_power_read,
};
#endif

int gator_events_power_init(void)
{
#if (GATOR_CPU_FREQ_SUPPORT)
	int i;
	for (i = 0; i < POWER_TOTAL; i++) {
		power_cpu_enabled[i] = 0;
		power_cpu_key[i] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_power_interface);
#else
	return -1;
#endif
}
gator_events_init(gator_events_power_init);
