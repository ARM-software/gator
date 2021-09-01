/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */
// SPDX-License-Identifier: GPL-2.0-only

/*
 * A Sample module demonstrating how to integrate and use the gator kernel annotations.
 */

// these headers are required by the module, but not specifically gator kernel annotations
#include <linux/kthread.h>
#include <linux/module.h>

/*
 * To integrate the gator annotations, define CREATE_TRACE_POINTS
 * (this should be added only in a single file for the given header)and include the gator_annotate.h header file as is required
 * by the Linux Tracepoints API
 */
#define CREATE_TRACE_POINTS
#include "../include/gator_annotate.h"

#define GATOR_ANN_SAWTOOTH_FREQ 10
#define GATOR_ANN_SQUARE_FREQ 5

bool switch_on = false;
static struct task_struct * simple_tsk;

// A simple function that toggles a switch and outputs a textual annotation tracepoint to gatord
static void toggle_switch(void)
{
    GATOR_BOOKMARK("Switch Toggled");

    if (!switch_on) {
        switch_on = true;
        // Write a textual annotation - this appears in the heatmap view, and displays horizontal blocks along the time axis.
        // The START macro will be used to start a text annotation that will continue until a STOP (or another text annotation)
        // is recieved on the same channel with the same tid
        GATOR_TEXT_START_COLOR(simple_tsk->pid, ANNOTATE_GREEN, "Channel 1", "Switch is On");
        // The GATOR_TEXT_STOP macro will stop the text annotation sent on "Channel 2" with that tid (see below).
        GATOR_TEXT_STOP(simple_tsk->pid, "Channel 2");
    }
    else {
        switch_on = false;
        GATOR_TEXT_STOP(simple_tsk->pid, "Channel 1");
        GATOR_TEXT_START_COLOR(simple_tsk->pid, ANNOTATE_RED, "Channel 2", "Switch is Off");
    }
}

// A sample function that periodically triggers various gator annotation tracepoints
static void simple_thread_func(int cnt)
{
    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(HZ);

    /*
     * Calling tracepoints
     */

    // Write a bookmark annotation - this appears in the streamline timeline view as a node along the horizontal time axis.
    GATOR_BOOKMARK_COLOR(ANNOTATE_BLUE, "Bookmark Example");

    // Write a counter annotation - these appear as charts/series in the timeline view
    GATOR_ABSOLUTE_COUNTER_VALUE("Kernel Annotations Chart A", "Sawtooth Wave", "units", cnt % GATOR_ANN_SAWTOOTH_FREQ);
    GATOR_DELTA_COUNTER_VALUE("Kernel Annotations Chart B", "Example Delta Series", "units", 1844674407370ull + cnt);

    // Now demonstrate textual annotations (see toggle_switch())
    if ((cnt % GATOR_ANN_SQUARE_FREQ) == 0)
        toggle_switch();
    if (switch_on)
        GATOR_ABSOLUTE_COUNTER_VALUE("Kernel Annotations Chart C", "Square Wave", "units", 1);
    else
        GATOR_ABSOLUTE_COUNTER_VALUE("Kernel Annotations Chart C", "Square Wave", "units", 0);
}

static int simple_thread(void * arg)
{
    int cnt = 0;
    while (!kthread_should_stop())
        simple_thread_func(cnt++);
    return 0;
}

static int __init gator_annotation_standalone_init(void)
{
    simple_tsk = kthread_run(simple_thread, NULL, "event-sample");
    if (IS_ERR(simple_tsk))
        return -1;
    return 0;
}

static void __exit gator_annotation_standalone_exit(void)
{
    kthread_stop(simple_tsk);
    tracepoint_synchronize_unregister();
}

module_init(gator_annotation_standalone_init);
module_exit(gator_annotation_standalone_exit);

MODULE_AUTHOR("Arm Streamline");
MODULE_DESCRIPTION("gator_standalone");
MODULE_LICENSE("GPL");
