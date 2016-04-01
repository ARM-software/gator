/**
 * Copyright (C) ARM Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <time.h>

#include "streamline_annotate.h"

int main(void)
{
	const struct timespec ts = { 0, 100000000 };

	ANNOTATE_SETUP;

	ANNOTATE("A simple textual annotation");
	nanosleep(&ts, NULL);
	ANNOTATE_CHANNEL(1, "An annotation on another channel");
	nanosleep(&ts, NULL);
	ANNOTATE_CHANNEL_COLOR(1, ANNOTATE_BLUE, "A blue annotation");
	nanosleep(&ts, NULL);
	ANNOTATE_END();
	nanosleep(&ts, NULL);
	ANNOTATE_CHANNEL_END(1);

	ANNOTATE_NAME_GROUP(2, "Groups can be named after use");
	ANNOTATE_NAME_CHANNEL(1, 2, "As can channels");

	ANNOTATE_MARKER();
	nanosleep(&ts, NULL);
	ANNOTATE_MARKER_STR("Markers can have comments");
	nanosleep(&ts, NULL);
	ANNOTATE_MARKER_COLOR(ANNOTATE_PURPLE);
	nanosleep(&ts, NULL);
	ANNOTATE_MARKER_COLOR_STR(ANNOTATE_GREEN, "A green marker");

	return 0;
}
