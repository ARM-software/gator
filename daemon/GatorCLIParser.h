/* Copyright (C) 2014-2021 by Arm Limited. All rights reserved. */

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

    std::vector<SpeConfiguration> mSpeConfigs {};
    std::vector<std::string> mCaptureCommand {};
    std::set<int> mPids {};
    std::map<std::string, EventCode> events {};
    std::set<Printable> printables {};

    std::uint64_t parameterSetFlag {0};

    ExecutionMode mode {ExecutionMode::DAEMON};

    const char * mCaptureWorkingDir {nullptr};
    const char * mSessionXMLPath {nullptr};
    const char * mTargetPath {nullptr};
    const char * mConfigurationXMLPath {nullptr};
    const char * mEventsXMLPath {nullptr};
    const char * mEventsXMLAppend {nullptr};
    const char * mWaitForCommand {nullptr};
    const char * pmuPath {nullptr};

    int mBacktraceDepth {0};
    int mSampleRate {0};
    int mDuration {0};
    int mAndroidApiLevel {0};
    int mPerfMmapSizeInPages {-1};
    int mSpeSampleRate {-1};
    int port {DEFAULT_PORT};

    bool mFtraceRaw {false};
    bool mStopGator {false};
    bool mSystemWide {true};
    bool mAllowCommands {false};
    bool mDisableCpuOnlining {false};
    bool mDisableKernelAnnotations {false};
    bool mExcludeKernelEvents {false};

    ParserResult() = default;
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
    ParserResult result {};

    static bool hasDebugFlag(int argc, const char * const argv[]);

    void parseCLIArguments(int argc,
                           char * argv[],
                           const char * version_string,
                           int maxPerformanceCounter,
                           const char * gSrcMd5);
    struct cmdline_t getGatorSetting();

private:
    int perfCounterCount {0};

    void addCounter(int startpos, int pos, std::string & counters);
    int findAndUpdateCmndLineCmnd(int argc, char ** argv);
    void parseAndUpdateSpe();
};

#endif /* GATORCLIPARSER_H_ */
