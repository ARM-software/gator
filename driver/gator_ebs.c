/**
 * Copyright (C) ARM Limited 2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/******************************************************************************
 * event based sampling handling
 ******************************************************************************/

#if defined (__arm__)
#include "gator_events_armv7.h"
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#if LINUX_PMU_SUPPORT
#include <asm/pmu.h>

static struct platform_device *pmu_device;

static irqreturn_t armv7_pmnc_interrupt(int irq, void *arg)
{
	unsigned int cnt, cpu = smp_processor_id(), buftype = EVENT_BUF;
	struct pt_regs * const regs = get_irq_regs();
	u32 flags;

	// Stop irq generation
	armv7_pmnc_write(armv7_pmnc_read() & ~PMNC_E);

	// Get and reset overflow status flags
	flags = armv7_pmnc_reset_interrupt();

	// Counters header
	gator_buffer_write_packed_int(cpu, buftype, MESSAGE_COUNTERS);      // type
	gator_buffer_write_packed_int64(cpu, buftype, gator_get_time());    // time
	
	// Cycle counter
	if (flags & (1 << 31)) {
		int value = armv7_ccnt_read(pmnc_count[CCNT]);                  // overrun
		gator_buffer_write_packed_int(cpu, buftype, 2);                 // length
		gator_buffer_write_packed_int(cpu, buftype, pmnc_key[CCNT]);    // key
		gator_buffer_write_packed_int(cpu, buftype, value);             // value
	}

	// PMNC counters
	for (cnt = CNT0; cnt < CNTMAX; cnt++) {
		 if (flags & (1 << (cnt - CNT0))) {
			int value = armv7_cntn_read(cnt, pmnc_count[cnt]);          // overrun
			gator_buffer_write_packed_int(cpu, buftype, 2);             // length
			gator_buffer_write_packed_int(cpu, buftype, pmnc_key[cnt]); // key
			gator_buffer_write_packed_int(cpu, buftype, value);         // value
		 }
	}

	// End Counters, length of zero
	gator_buffer_write_packed_int(cpu, buftype, 0);

	// Output backtrace
	gator_add_sample(cpu, buftype, regs);

	// Check and commit; commit is set to occur once buffer is 3/4 full
	event_buffer_check(cpu);

	// Allow irq generation
	armv7_pmnc_write(armv7_pmnc_read() | PMNC_E);

	return IRQ_HANDLED;
}
#endif

static int gator_event_sampling_start(void)
{
	int cnt;

	event_based_sampling = false;
	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		if (pmnc_count[cnt] > 0) {
			event_based_sampling = true;
			break;
		}
	}

#if LINUX_PMU_SUPPORT
	pmu_device = reserve_pmu(ARM_PMU_DEVICE_CPU);
	if (IS_ERR(pmu_device) && (unsigned int)pmu_device != -ENODEV) {
		pr_err("gator: unable to reserve the pmu\n");
		return -1;
	}

	if (event_based_sampling) {
		int irq, i;

		if (IS_ERR(pmu_device)) {
			pr_err("gator: event based sampling is not supported as the kernel function reserve_pmu() failed");
			return -1;
		}

		init_pmu(ARM_PMU_DEVICE_CPU);
		if (pmu_device->num_resources == 0) {
			pr_err("gator: no irqs for PMUs defined\n");
			release_pmu(pmu_device);
			pmu_device = NULL;
			return -1;
		}

		for (i = 0; i < pmu_device->num_resources; ++i) {
			irq = platform_get_irq(pmu_device, i);
			if (irq < 0)
				continue;

			if (request_irq(irq, armv7_pmnc_interrupt, IRQF_DISABLED | IRQF_NOBALANCING, "armpmu", NULL)) {
				pr_err("gator: unable to request IRQ%d for ARM perf counters\n", irq);
				
				// clean up and exit
				for (i = i - 1; i >= 0; --i) {
					irq = platform_get_irq(pmu_device, i);
					if (irq >= 0)
						free_irq(irq, NULL);
				}
				release_pmu(pmu_device);
				pmu_device = NULL;
				return -1;
			}
		}
	}
#else
	if (event_based_sampling) {
		pr_err("gator: event based sampling only supported in kernel versions 2.6.35 and higher and CONFIG_CPU_HAS_PMU=y\n");
		return -1;
	}
#endif

	return 0;
}

static void gator_event_sampling_stop(void)
{
#if LINUX_PMU_SUPPORT
	if (event_based_sampling) {
		int i, irq;
		for (i = pmu_device->num_resources - 1; i >= 0; --i) {
			irq = platform_get_irq(pmu_device, i);
			if (irq >= 0)
				free_irq(irq, NULL);
		}
	}
	if (!IS_ERR(pmu_device))
		release_pmu(pmu_device);
	pmu_device = NULL;
#endif
}

#else
static int gator_event_sampling_start(void) {return 0;}
static void gator_event_sampling_stop(void) {}
#endif
