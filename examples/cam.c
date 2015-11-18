/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * This is a contrived, bare-bones program to provide a simple
 * example of how the Custom Activity Map (CAM) macros are used.
 */
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "streamline_annotate.h"

#define NS_PER_S 1000000000LL

#define VIEW_UID1 0
#define VIEW_UID2 2
#define TRACK_ROOT 555
#define TRACK_CHILD 556
#define JOB_UID 4

static long base_id;

static void do_some_work()
{
	/* delay due to some fictitious work */
	sleep(1);
}

static int get_uid(int id)
{
	return base_id + id;
}

static void create_base_id()
{
	/* create a random base_id so this application can be run multiple times within the same capture and still have unique ids */
	/* this is not a perfect solution but is very simple and should work in a very high percentage of cases */
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		printf("Error opening /dev/urandom: %s\n", strerror(errno));
	} else {
		if (read(fd, &base_id, sizeof(base_id)) != sizeof(base_id)) {
			printf("Error reading from /dev/urandom: %s\n", strerror(errno));
		}
		close(fd);
	}
}

int main(void)
{
	int job_id;
	long long start_time, stop_time;
	char job1[] = "$job$";

	ANNOTATE_SETUP;
	create_base_id();
	job_id = get_uid(JOB_UID);

	/* view 1 */
	CAM_VIEW_NAME(VIEW_UID1, "Custom Activity Map 1");
	CAM_TRACK(VIEW_UID1, TRACK_ROOT, -1, "[track 1]");
	CAM_TRACK(VIEW_UID1, TRACK_CHILD, TRACK_ROOT, "[track 2]");
	start_time = gator_get_time();
	do_some_work();
	stop_time = gator_get_time();
	CAM_JOB(VIEW_UID1, job_id, job1, TRACK_CHILD, start_time,
		stop_time - start_time, ANNOTATE_WHITE);

	/* view 2 */
	CAM_VIEW_NAME(VIEW_UID2, "Custom Activity Map 2");
	CAM_TRACK(VIEW_UID2, TRACK_ROOT, -1, "[track 1]");
	CAM_TRACK(VIEW_UID2, TRACK_CHILD, TRACK_ROOT, "[track 2]");
	CAM_TRACK(VIEW_UID2, 557, -1, "[track X]");
	CAM_JOB(VIEW_UID2, job_id++, job1, TRACK_ROOT, start_time,
		stop_time - start_time, ANNOTATE_YELLOW);
	CAM_JOB(VIEW_UID2, job_id++, job1, 557, start_time,
		stop_time - start_time, ANNOTATE_YELLOW);
	start_time = gator_get_time();
	do_some_work();
	stop_time = gator_get_time();
	/* job with a single dependency */
	CAM_JOB_DEP(VIEW_UID2, job_id++, "dependent job", TRACK_CHILD, start_time,
		    stop_time - start_time, ANNOTATE_COLOR_CYCLE, get_uid(JOB_UID));
	/* job with multiple dependencies */
	{
		uint32_t dependencies[2];
		dependencies[0] = get_uid(JOB_UID + 1);
		dependencies[1] = get_uid(JOB_UID + 2);
		start_time = gator_get_time();
		do_some_work();
		stop_time = gator_get_time();
		CAM_JOB_DEPS(VIEW_UID2, job_id++, "dependent job", 557, start_time,
			     stop_time - start_time, ANNOTATE_COLOR_CYCLE, 2,
			     dependencies);
	}

	return 0;
}
