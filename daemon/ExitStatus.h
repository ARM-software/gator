/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#ifndef EXITSTATUS_H_
#define EXITSTATUS_H_

static constexpr int EXCEPTION_EXIT_CODE = 1;
static constexpr int SECOND_EXCEPTION_EXIT_CODE = 2;
// constexpr int secondSignalExitCode = 3; no longer used
// constexpr int alarmExitCode = 4; no longer used
static constexpr int NO_SINGLETON_EXIT_CODE = 5;
static constexpr int SIGNAL_FAILED_EXIT_CODE = 6;
// child will return this exit code on exit_ok command
static constexpr int OK_TO_EXIT_GATOR_EXIT_CODE = 7;
// command failed
static constexpr int COMMAND_FAILED_EXIT_CODE = 8;

#endif /* EXITSTATUS_H_ */
