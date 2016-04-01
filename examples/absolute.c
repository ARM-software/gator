/**
 * Copyright (C) ARM Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdio.h>
#include <time.h>

#include "streamline_annotate.h"

/* This function "simulates" counters, generating values of fancy
 * functions like sine or triangle... */
static int mmapped_simulate(int counter, int delta_in_us)
{
	int result = 0;

	switch (counter) {
	case 0:		/* sort-of-sine */
		{
			static int t = 0;
			int x;

			t += delta_in_us;
			if (t > 2048000)
				t = 0;

			if (t % 1024000 < 512000)
				x = 512000 - (t % 512000);
			else
				x = t % 512000;

			result = 32 * x / 512000;
			result = result * result;

			if (t < 1024000)
				result = 1922 - result;
		}
		break;
	case 1:		/* triangle */
		{
			static int v, d = 1;

			v = v + d * delta_in_us;
			if (v < 0) {
				v = 0;
				d = 1;
			} else if (v > 1000000) {
				v = 1000000;
				d = -1;
			}

			result = v;
		}
		break;
	case 2:		/* PWM signal */
		{
			static int dc, x, t = 0;

			t += delta_in_us;
			if (t > 1000000)
				t = 0;
			if (x / 1000000 != (x + delta_in_us) / 1000000)
				dc = (dc + 100000) % 1000000;
			x += delta_in_us;

			result = t < dc ? 0 : 10;
		}
		break;
	}

	return result;
}

int main(void)
{
	struct timespec ts;
	int i;

	ANNOTATE_SETUP;
	ANNOTATE_ABSOLUTE_COUNTER(0xa0, "Simulated4", "Sine");
	ANNOTATE_ABSOLUTE_COUNTER(0xa1, "Simulated5", "Triangle");
	ANNOTATE_ABSOLUTE_COUNTER(0xa2, "Simulated6", "PWM");

	clock_gettime(CLOCK_MONOTONIC, &ts);

	for (i = 0; i < 2000; ++i) {
		ANNOTATE_COUNTER_VALUE(0xa0, mmapped_simulate(0, 10000));
		ANNOTATE_COUNTER_VALUE(0xa1, mmapped_simulate(1, 10000));
		ANNOTATE_COUNTER_VALUE(0xa2, mmapped_simulate(2, 10000));

		ts.tv_nsec += 10000000;
		if (ts.tv_nsec > 1000000000) {
			ts.tv_nsec -= 1000000000;
			++ts.tv_sec;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
	}

	/* absolute counters will display the last value used, thus
	 *   set all values to zero before exiting */
	ANNOTATE_COUNTER_VALUE(0xa0, 0);
	ANNOTATE_COUNTER_VALUE(0xa1, 0);
	ANNOTATE_COUNTER_VALUE(0xa2, 0);

	return 0;
}
