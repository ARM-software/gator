/* Copyright (C) 2014-2023 by Arm Limited. All rights reserved. */

#ifndef GATORCLIPARSER_H_
#define GATORCLIPARSER_H_

#include "Configuration.h"
#include "GatorCLIFlags.h"
#include "OlyUtility.h"
#include "ParserResult.h"

#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>

const int ERROR_PARSING = -101;
static const struct option ANDROID_ACTIVITY = {"android-activity", /*******/ required_argument, nullptr, 'm'};
static const struct option ANDROID_PACKAGE = {"android-pkg", /************/ required_argument, nullptr, 'l'};
static const struct option PACKAGE_FLAGS = {"activity-args", /************/ required_argument, nullptr, 'n'};
static const struct option WAIT_PROCESS = {"wait-process", /***********/ required_argument, nullptr, 'Q'};

#define DISABLE_TCP_USE_UDS_PORT (-1)

static const struct option APP = {"app", /********************/ required_argument, nullptr, 'A'};
/**
 * This class is responsible for parsing all the command line arguments
 * passed to Gator.
 */
class GatorCLIParser {
public:
    ParserResult result {};

    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    static bool hasDebugFlag(int argc, const char * const argv[]);

    //NOLINTNEXTLINE(modernize-avoid-c-arrays)
    static bool hasCaptureLogFlag(int argc, const char * const argv[]);

    void parseCLIArguments(int argc,
                           //NOLINTNEXTLINE(modernize-avoid-c-arrays)
                           char * argv[],
                           const char * version_string,
                           const char * gSrcMd5,
                           const char * gBuildId);
    struct cmdline_t getGatorSetting();

private:
    int perfCounterCount {0};

    void addCounter(int startpos, int pos, std::string & counters);
    int findAndUpdateCmndLineCmnd(int argc, char ** argv);
    void parseAndUpdateSpe();
};

#endif /* GATORCLIPARSER_H_ */
