/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */
// SPDX-License-Identifier: GPL-2.0-only

/*
 * gator kernel annotation trace points.
 */

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include "../include/gator_annotate.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(gator_bookmark);
EXPORT_TRACEPOINT_SYMBOL_GPL(gator_counter);
EXPORT_TRACEPOINT_SYMBOL_GPL(gator_text);

MODULE_AUTHOR("Arm Streamline");
MODULE_DESCRIPTION("gator");
MODULE_LICENSE("GPL");
