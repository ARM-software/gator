/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/**
 * This file is #included in gator_main.c
 *  Update this file and Makefile to add custom counters.
 */

extern int gator_events_armv6_install(gator_interface *gi);
extern int gator_events_armv7_install(gator_interface *gi);
extern int gator_events_irq_install(gator_interface *gi);
extern int gator_events_sched_install(gator_interface *gi);
extern int gator_events_block_install(gator_interface *gi);
extern int gator_events_meminfo_install(gator_interface *gi);
extern int gator_events_net_install(gator_interface *gi);

static int gator_events_install(void)
{
	if (gator_event_install(gator_events_armv6_install))
		return -1;
	if (gator_event_install(gator_events_armv7_install))
		return -1;
	if (gator_event_install(gator_events_irq_install))
		return -1;
	if (gator_event_install(gator_events_sched_install))
		return -1;
	if (gator_event_install(gator_events_block_install))
		return -1;
	if (gator_event_install(gator_events_meminfo_install))
		return -1;
	if (gator_event_install(gator_events_net_install))
		return -1;
	return 0;
}
