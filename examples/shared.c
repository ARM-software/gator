/**
 * Copyright (C) ARM Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "streamline_annotate.h"

static const int shared_absolute_key = 0xb0;
static const int shared_delta_key = 0xb1;
static int unique_absolute_key;
static int unique_delta_key;

static int get_sys_rand(void)
{
	int i;

	/* create a random base_id so this application can be run multiple times within the same capture and still have unique ids */
	/* this is not a perfect solution but is very simple and should work in a very high percentage of cases */
	int fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		printf("Error opening /dev/urandom: %s\n", strerror(errno));
		abort();
	} else {
		if (read(fd, &i, sizeof(i)) != sizeof(i)) {
			printf("Error reading from /dev/urandom: %s\n", strerror(errno));
			abort();
		}
		close(fd);
	}

	return i;
}

int main(void)
{
	const struct timespec ts = { 0, 10000000 };
	int i;
	char buf[16];

	ANNOTATE_SETUP;

	ANNOTATE_ABSOLUTE_COUNTER(shared_absolute_key, "Shared", "Absolute");
	ANNOTATE_DELTA_COUNTER(shared_delta_key, "Shared", "Delta");

	unique_absolute_key = get_sys_rand();
	unique_delta_key = unique_absolute_key + 1;
	snprintf(buf, sizeof(buf), "Unique %i", getpid());
	ANNOTATE_ABSOLUTE_COUNTER(unique_absolute_key, buf, "Absolute");
	ANNOTATE_DELTA_COUNTER(unique_delta_key, buf, "Delta");

	for (i = 0; i < 500; ++i) {
		nanosleep(&ts, NULL);
		ANNOTATE_COUNTER_VALUE(shared_absolute_key, i);
		nanosleep(&ts, NULL);
		ANNOTATE_COUNTER_VALUE(shared_delta_key, 25);
		nanosleep(&ts, NULL);
		ANNOTATE_COUNTER_VALUE(unique_absolute_key, i);
		nanosleep(&ts, NULL);
		ANNOTATE_COUNTER_VALUE(unique_delta_key, 25);
	}

	ANNOTATE_COUNTER_VALUE(shared_absolute_key, 0);
	ANNOTATE_COUNTER_VALUE(unique_absolute_key, 0);

	return 0;
}
