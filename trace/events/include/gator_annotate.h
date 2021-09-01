/* Copyright (C) 2020-2021 by Arm Limited. All rights reserved. */
// SPDX-License-Identifier: GPL-2.0-only

/**
 * This header file needs to be included for adding
 * tracepoints which can be read by gatord and
 * will be represented in Streamline in various formats
 * as described below.
 *
 * refer example : gator_annotate.c
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gator

#if !defined(_TRACE_GATOR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GATOR_H

#include <linux/tracepoint.h>

#if !defined(TRACEPOINTS_ENABLED) && !defined(CONFIG_TRACEPOINTS)
#error "No tracepoints"
#endif

/**
 * Colors are encoded as a 32-bit integer, with the following format:
 *
 *   0xTTVVVVVV
 *
 * Where 'TT' identifies the 'type' of color, and 'VVVVVV' encodes some value
 * that is specific to that type.
 *
 * To encode an RGB value, TT must be set to '1b' and 'VVVVVV' encode the
 * color as 'RRGGBB', where 'RR', 'BB', and 'GG' are the hex-characters
 * encoding the red, green and blue values respectively.
 * Some example colors:
 *   0x1bff0000 - full red
 *   0x1b00ff00 - full green
 *   0x1b0000ff - full blue
 *   0x1bffffff - white
 *   0x1b000000 - black
 *
 * 'TT' also has the following special values, in each case 'VVVVVV' should be
 * set to zeros.
 *
 *   0x00000000 - rotate through set of 4 predetermined template colors
 *   0x01000000 - template color 1
 *   0x02000000 - template color 2
 *   0x03000000 - template color 3
 *   0x04000000 - template color 4
 */
#define ANNOTATE_RED 0x1bff0000
#define ANNOTATE_BLUE 0x1b0000ff
#define ANNOTATE_GREEN 0x1b00ff00
#define ANNOTATE_PURPLE 0x1bff00ff
#define ANNOTATE_YELLOW 0x1bffff00
#define ANNOTATE_CYAN 0x1b00ffff
#define ANNOTATE_WHITE 0x1bffffff
#define ANNOTATE_LTGRAY 0x1bbbbbbb
#define ANNOTATE_DKGRAY 0x1b555555
#define ANNOTATE_BLACK 0x1b000000
#define ANNOTATE_DEFAULT_COLOR 0x1bd0d0d0

/**
 * This value can be passed as the 'tid' value to indicate that the event is associated with the kernel as a whole rather than some specific tid.
 * This value can used only for bookmark and counter TRACE_EVENTS. Annotation texts should have a tid >=0.
 * For tid == 0, it will be the idle process.
 */
#define GATOR_KERNEL_WIDE_PID -1

/**
 * The following are convenience for the trace events defined in this file. Alternatively, the trace event functions can be called directly such
 * as trace_gator_text (refer to the below TRACE_EVENT calls for more information).
 *
 * The following macros creats a gator bookmark
 */
#define GATOR_BOOKMARK_COLOR(color, label) trace_gator_bookmark(GATOR_KERNEL_WIDE_PID, (color), (label))
#define GATOR_BOOKMARK(label) trace_gator_bookmark(GATOR_KERNEL_WIDE_PID, ANNOTATE_DEFAULT_COLOR, (label))

/**
 * The START and STOP macros call the same function but offer some distinction between whether this text annotation is intended to be the start
 * of a text annotation or the end. The STOP macro will send an empty string as the label which will be recognised by Streamline as the end to
 * a text annotation with the given channel and tid.
 */
#define GATOR_TEXT_START(tid, channel, label) trace_gator_text((tid), ANNOTATE_DEFAULT_COLOR, (channel), (label))
#define GATOR_TEXT_START_COLOR(tid, color, channel, label) trace_gator_text((tid), (color), (channel), (label))
#define GATOR_TEXT_STOP(tid, channel) trace_gator_text((tid), ANNOTATE_DEFAULT_COLOR, (channel), "")

/**
 * Use the following macros to output counter values
 */
#define GATOR_DELTA_COUNTER_VALUE(title, name, units, value)                                                           \
    trace_gator_counter(GATOR_KERNEL_WIDE_PID, (title), (name), (units), true, (value))
#define GATOR_ABSOLUTE_COUNTER_VALUE(title, name, units, value)                                                        \
    trace_gator_counter(GATOR_KERNEL_WIDE_PID, (title), (name), (units), false, (value))

/**
 * To add a trace point that will create a bookmark in
 * Streamline use the TRACE_EVENT  gator_bookmark
 *
 * Function to be called trace_gator_bookmark(int tid,
 *                   int color,
 *                   const char * label).
 *
 * @param tid the thread id where the event is generated
 * @param color the color to be used for the bookmark.
 *                Choose a color constant or use color encoding
 *                as defined above.
 * @param label name of the bookmark to be added
 */
TRACE_EVENT(gator_bookmark,
            TP_PROTO(int tid, int color, const char * label),
            TP_ARGS(tid, color, label),
            TP_STRUCT__entry(__field(int, tid) __field(int, color) __string(label, label)),
            TP_fast_assign(__entry->tid = tid; __entry->color = color; __assign_str(label, label);),
            TP_printk("tid=%d color=0x%06x  label=%s", __entry->tid, __entry->color, __get_str(label)));
/**
 * To add a trace point that will create an annotation in
 * Streamline use the TRACE_EVENT  gator_text.
 *
 * Function to be called trace_gator_text(int tid,
 *                   int color,
 *                   const char * channel,
 *                   const char * label).
 *
 * @param tid the thread id where the event is generated
 * @param color the color to be used for the annotation.
 *                Choose a color constant or use color encoding
 *                as defined above.
 * @param channel the channel id
 * @param label name of the annotation
 */
TRACE_EVENT(gator_text,
            TP_PROTO(int tid, int color, const char * channel, const char * label),
            TP_ARGS(tid, color, channel, label),
            TP_STRUCT__entry(__field(int, tid) __field(int, color) __string(channel, channel) __string(label, label)),
            TP_fast_assign(__entry->tid = tid; __entry->color = color; __assign_str(channel, channel);
                           __assign_str(label, label);),
            TP_printk("tid=%d color=0x%06x channel=%s label=%s",
                      __entry->tid,
                      __entry->color,
                      __get_str(channel),
                      __get_str(label)));
/**
 * To add a trace point that creates a counter in
 * Streamline use the TRACE_EVENT  gator_counter.
 *
 * Function to be called trace_gator_counter(int tid,
 *                   const char * title,
 *                   const char * name,
 *                   const char * units,
 *                   bool isdelta,
 *                   unsigned long long value).
 *
 * @param tid the thread id where the event is generated
 * @param title the title of the counter
 * @param name the name of the counter
 * @param units unit for the counter
 * @param isDelta if the counter is a delta or absolute
 * @param value the value of the counter
 */
TRACE_EVENT(gator_counter,
            TP_PROTO(int tid,
                     const char * title,
                     const char * name,
                     const char * units,
                     bool isdelta,
                     unsigned long long value),
            TP_ARGS(tid, title, name, units, isdelta, value),
            TP_STRUCT__entry(__field(int, tid) __field(bool, isdelta) __field(unsigned long long, value)
                                 __string(title, title) __string(name, name) __string(units, units)),
            TP_fast_assign(__entry->tid = tid; __entry->isdelta = isdelta; __entry->value = value;
                           __assign_str(title, title);
                           __assign_str(name, name);
                           __assign_str(units, units);),
            TP_printk("tid=%d isdelta=%d value=%llu title=%s name=%s units=%s",
                      __entry->tid,
                      __entry->isdelta,
                      __entry->value,
                      __get_str(title),
                      __get_str(name),
                      __get_str(units)));

#endif /* _TRACE_GATOR_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../include
// clang-format off
#define TRACE_INCLUDE_FILE gator_annotate
// clang-format on
#include <trace/define_trace.h>
