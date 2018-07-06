/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GATORCLIPARSER_H_
#define GATORCLIPARSER_H_

#include <unistd.h>
#include <string>
#include <sys/stat.h>
#include <getopt.h>
#include <map>
#include <vector>
#include <set>
#include <string.h>

#include "OlyUtility.h"
#include "Logging.h"
#include "ClassBoilerPlate.h"
#include "GatorCLIFlags.h"

const int ERROR_PARSING = -101;

enum SampleRate
{
    high = 10007,
    normal = 1009,
    low = 101,
    none = 0,
    invalid = -1
};

#define DEFAULT_PORT 8080

/**
 * For containing the results of parsing
 */
class ParserResult
{
public:

    ParserResult();
    ~ParserResult();

    const char *mCaptureWorkingDir;
    std::vector<std::string> mCaptureCommand;
    std::set<int> mPids;
    const char *mSessionXMLPath;
    const char *mTargetPath;
    const char *mConfigurationXMLPath;
    const char *mEventsXMLPath;
    const char *mEventsXMLAppend;
    const char *mWaitForCommand;
    const char *mMaliDevice;
    const char *mMaliType;

    int mBacktraceDepth;
    int mSampleRate;
    int mDuration;
    int mAndroidApiLevel;
    int mPerfMmapSizeInPages;

    bool mFtraceRaw;
    bool mStopGator;
    bool mSystemWide;
    bool mAllowCommands;
    bool mIsLocalCapture;

    const char *module;
    const char *pmuPath;
    int port;

    int64_t parameterSetFlag;

    std::map<std::string, int> events;

    int parse_error = 0;

    std::string parse_error_message;
    CLASS_DELETE_COPY_MOVE(ParserResult);
};
/**
 * This class is responsible for parsing all the command line arguments
 * passed to Gator.
 */
class GatorCLIParser
{
public:
    GatorCLIParser();
    ~GatorCLIParser();

    void parseCLIArguments(int argc, char* argv[], const char* version_string, int maxPerfCounter, const char* gSrcMd5);
    bool hasDebugFlag(int argc, const char* const argv[]);
    struct cmdline_t getGatorSetting();
    ParserResult result;

private:
    int perfCounterCount;
    void addCounter(int startpos, int pos, std::string &counters);
    int findAndUpdateCmndLineCmnd(int argc, char** argv);
};

#endif /* GATORCLIPARSER_H_ */
