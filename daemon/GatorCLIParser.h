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
#include "Configuration.h"
#include "GatorCLIFlags.h"

const int ERROR_PARSING = -101;

#define DEFAULT_PORT                8080
#define DISABLE_TCP_USE_UDS_PORT    -1

/**
 * For containing the results of parsing
 */
class ParserResult
{
public:
    enum class ExecutionMode {
        LOCAL_CAPTURE,
        PRINT,
        DAEMON,
        EXIT,
    };

    enum class Printable {
        EVENTS_XML,
        COUNTERS_XML,
        DEFAULT_CONFIGURATION_XML,
    };

    ParserResult();
    ~ParserResult();

    std::vector<SpeConfiguration> mSpeConfigs;
    const char *mCaptureWorkingDir;
    std::vector<std::string> mCaptureCommand;
    std::set<int> mPids;
    const char *mSessionXMLPath;
    const char *mTargetPath;
    const char *mConfigurationXMLPath;
    const char *mEventsXMLPath;
    const char *mEventsXMLAppend;
    const char *mWaitForCommand;

    std::vector<std::string> mMaliDevices;
    std::vector<std::string> mMaliTypes;

    int mBacktraceDepth;
    int mSampleRate;
    int mDuration;
    int mAndroidApiLevel;
    int mPerfMmapSizeInPages;

    bool mFtraceRaw;
    bool mStopGator;
    bool mSystemWide;
    bool mAllowCommands;

    const char *module;
    const char *pmuPath;
    int port;

    int64_t parameterSetFlag;

    std::map<std::string, int> events;

    ExecutionMode mode;
    std::set<Printable> printables;

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
    void parseAndUpdateSpe();
};

#endif /* GATORCLIPARSER_H_ */
