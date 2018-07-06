/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GATORCLIFLAGS_H_
#define GATORCLIFLAGS_H_



enum{
    USE_CMDLINE_ARG_SAMPLE_RATE            = 0x01,
    USE_CMDLINE_ARG_CAPTURE_WORKING_DIR    = 0x02,
    USE_CMDLINE_ARG_CAPTURE_COMMAND        = 0x04,
    USE_CMDLINE_ARG_STOP_GATOR             = 0x08,
    USE_CMDLINE_ARG_CALL_STACK_UNWINDING   = 0x10,
    USE_CMDLINE_ARG_DURATION               = 0x20,
    USE_CMDLINE_ARG_FTRACE_RAW             = 0x40
};

#endif /* GATORCLIFLAGS_H_ */
