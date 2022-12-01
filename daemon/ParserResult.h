/* Copyright (C) 2014-2022 by Arm Limited. All rights reserved. */

#ifndef PARSERRESULT_H_
#define PARSERRESULT_H_

#include "Configuration.h"
#include "ParserResult.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
/**
 * For containing the results of parsing
 */
#define DEFAULT_PORT 8080

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
    const char * mAndroidPackage {nullptr};
    const char * mAndroidActivity {nullptr};

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

    /**
     * @return - a list of argument-value pairs
     */
    const std::vector<std::pair<std::string, std::optional<std::string>>> & getArgValuePairs() const;

    /**
     * Clears the list of argument-value pairs
     */
    void clearArgValuePairs();

    /**
     * Add a new argument value pair to the list of argument-value pairs
     */
    void addArgValuePair(const std::pair<std::string, std::optional<std::string>> & argValuePair);

    /**
     * Move the --app or -A argument to the end of the list of argument value pairs
     */
    void moveAppArgToEndOfVector();

    /**
     * Clears the  list of argument-value pairs and set the ExecutionMode to Exit
     */
    void parsingFailed();

    ParserResult() = default;
    ParserResult(const ParserResult &) = delete;
    ParserResult & operator=(const ParserResult &) = delete;
    ParserResult(ParserResult &&) = delete;
    ParserResult & operator=(ParserResult &&) = delete;

private:
    std::vector<std::pair<std::string, std::optional<std::string>>> argValuePairs {};
};

#endif /* PARSERRESULT_H_ */
