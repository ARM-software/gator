/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#ifndef GATORCLIPARSER_H_
#define GATORCLIPARSER_H_

#include "Configuration.h"
#include "GatorCLIFlags.h"
#include "Logging.h"
#include "OlyUtility.h"

#include <cstring>
#include <getopt.h>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

const int ERROR_PARSING = -101;

#define DEFAULT_PORT 8080
#define DISABLE_TCP_USE_UDS_PORT (-1)

/**
 * For containing the results of parsing
 */
class ParserResult {
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

    std::vector<SpeConfiguration> mSpeConfigs;
    const char * mCaptureWorkingDir;
    std::vector<std::string> mCaptureCommand;
    std::set<int> mPids;
    const char * mSessionXMLPath;
    const char * mTargetPath;
    const char * mConfigurationXMLPath;
    const char * mEventsXMLPath;
    const char * mEventsXMLAppend;
    const char * mWaitForCommand;

    int mBacktraceDepth;
    int mSampleRate;
    int mDuration;
    int mAndroidApiLevel;
    int mPerfMmapSizeInPages;
    int mSpeSampleRate;

    bool mFtraceRaw;
    bool mStopGator;
    bool mSystemWide;
    bool mAllowCommands;
    bool mDisableCpuOnlining;

    const char * pmuPath;
    int port;

    int64_t parameterSetFlag;

    std::map<std::string, EventCode> events;

    ExecutionMode mode;
    std::set<Printable> printables;

    ParserResult(const ParserResult &) = delete;
    ParserResult & operator=(const ParserResult &) = delete;
    ParserResult(ParserResult &&) = delete;
    ParserResult & operator=(ParserResult &&) = delete;
};
/**
 * This class is responsible for parsing all the command line arguments
 * passed to Gator.
 */
class GatorCLIParser {
public:
    GatorCLIParser();

    void parseCLIArguments(int argc,
                           char * argv[],
                           const char * version_string,
                           int maxPerformanceCounter,
                           const char * gSrcMd5);
    static bool hasDebugFlag(int argc, const char * const argv[]);
    struct cmdline_t getGatorSetting();
    ParserResult result;

private:
    int perfCounterCount;
    void addCounter(int startpos, int pos, std::string & counters);
    int findAndUpdateCmndLineCmnd(int argc, char ** argv);
    void parseAndUpdateSpe();
};

#endif /* GATORCLIPARSER_H_ */
