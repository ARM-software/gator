/**
 * Copyright (C) ARM Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * This is a contrived, bare-bones program to provide a simple
 * example of how the Custom Activity Map (CAM) macros are used.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "streamline_annotate.h"

#define NS_PER_S 1000000000LL

#define VIEW_UID1 1
#define VIEW_UID2 2
#define TRACK_ROOT 1
#define TRACK_CHILD 2
#define TRACK_X 3

static long base_id;

static void do_some_work(void)
{
	/* delay due to some fictitious work */
	sleep(1);
}

static void create_base_id(void)
{
	/*
	 * create a random base_id so this application can be run
	 * multiple times within the same capture and still have
	 * unique ids this is not a perfect solution but is very
	 * simple and should work in a very high percentage of cases
	 */
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		printf("Error opening /dev/urandom: %s\n", strerror(errno));
	} else {
		if (read(fd, &base_id, sizeof(base_id)) != sizeof(base_id)) {
			printf("Error reading from /dev/urandom: %s\n",
			       strerror(errno));
		}
		close(fd);
	}
}

int main(void)
{
	ANNOTATE_SETUP;
	create_base_id();

	/* view 1 */
	CAM_VIEW_NAME(VIEW_UID1, "Custom Activity Map 1");
	CAM_TRACK(VIEW_UID1, TRACK_ROOT, -1, "[track 1]");
	CAM_TRACK(VIEW_UID1, TRACK_CHILD, TRACK_ROOT, "[track 2]");

	/* view 2 */
	CAM_VIEW_NAME(VIEW_UID2, "Custom Activity Map 2");
	CAM_TRACK(VIEW_UID2, TRACK_ROOT, -1, "[track 1]");
	CAM_TRACK(VIEW_UID2, TRACK_CHILD, TRACK_ROOT, "[track 2]");
	CAM_TRACK(VIEW_UID2, TRACK_X, -1, "[track X]");

	uint64_t start_time = gator_get_time();
	{
		CAM_JOB_START(VIEW_UID2, base_id, "$job$", TRACK_ROOT,
			      start_time, ANNOTATE_YELLOW);
		CAM_JOB_START(VIEW_UID2, base_id + 1, "$job$", TRACK_X,
			      start_time, ANNOTATE_YELLOW);
	}
	do_some_work();
	{
		uint64_t time = gator_get_time();

		CAM_JOB(VIEW_UID1, base_id, "$job$", TRACK_CHILD, start_time,
			time - start_time, ANNOTATE_WHITE);
		CAM_JOB_STOP(VIEW_UID2, base_id, time);
		CAM_JOB_STOP(VIEW_UID2, base_id + 1, time);
	}

	/* job with a single dependency */
	{
		uint64_t time = gator_get_time();

		CAM_JOB_START(VIEW_UID2, base_id + 2, "dependent job",
			      TRACK_CHILD, time, ANNOTATE_COLOR_CYCLE);
		CAM_JOB_SET_DEP(VIEW_UID2, base_id + 2, time, base_id);
	}
	do_some_work();
	CAM_JOB_STOP(VIEW_UID2, base_id + 2, gator_get_time());

	/* job with multiple dependencies */
	{
		uint64_t time = gator_get_time();
		uint32_t dependencies[2];

		dependencies[0] = base_id + 1;
		dependencies[1] = base_id + 2;
		CAM_JOB_START(VIEW_UID2, base_id + 3, "dependent job", TRACK_X,
			      time, ANNOTATE_COLOR_CYCLE);
		CAM_JOB_SET_DEPS(VIEW_UID2, base_id + 3, time, 2,
				 dependencies);
	}
	do_some_work();
	CAM_JOB_STOP(VIEW_UID2, base_id + 3, gator_get_time());

	return 0;
}
