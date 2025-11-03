/* Copyright (C) 2010-2025 by Arm Limited. All rights reserved. */

#ifndef EXITSTATUS_H_
#define EXITSTATUS_H_

static constexpr int EXCEPTION_EXIT_CODE = 1;
static constexpr int SECOND_EXCEPTION_EXIT_CODE = 2;
static constexpr int NO_SINGLETON_EXIT_CODE = 5;
static constexpr int SIGNAL_FAILED_EXIT_CODE = 6;
/// child will return this exit code on exit_ok command
static constexpr int OK_TO_EXIT_GATOR_EXIT_CODE = 7;
/// command failed
static constexpr int COMMAND_FAILED_EXIT_CODE = 8;
/// returned by gator-child to indicate that the capture has completed
static constexpr int CHILD_EXIT_AFTER_CAPTURE = 9;

/// Returned when failing to write a notification to the signal handling pipe during the signal handler
static constexpr int SIGNAL_NOTIFICATION_FAILED_CODE = 10;

#endif /* EXITSTATUS_H_ */
