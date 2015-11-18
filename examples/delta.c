/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <time.h>

#include "streamline_annotate.h"

int main(void)
{
	struct timespec ts;
	long long collatz = 9780657630LL;

	ANNOTATE_SETUP;
	ANNOTATE_DELTA_COUNTER(0, "collatz", "multiply");
	ANNOTATE_DELTA_COUNTER(1, "collatz", "divide");

	clock_gettime(CLOCK_MONOTONIC, &ts);

	while (collatz != 1) {
		if (collatz & 1) {
			ANNOTATE_COUNTER_VALUE(0, 1);
			printf("multiply\n");
			collatz = 3*collatz + 1;
		} else {
			ANNOTATE_COUNTER_VALUE(1, 1);
			printf("divide\n");
			collatz = collatz/2;
		}

		ts.tv_nsec += 10000000;
		if (ts.tv_nsec > 1000000000) {
			ts.tv_nsec -= 1000000000;
			++ts.tv_sec;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
	}

	return 0;
}
