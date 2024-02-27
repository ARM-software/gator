/* Copyright (C) 2014-2024 by Arm Limited. All rights reserved. */

#ifndef PARSERRESULT_H_
#define PARSERRESULT_H_

#include "Configuration.h"
#include "linux/smmu_identifier.h"

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
        COUNTERS,
        COUNTERS_DETAILED,
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
    const char * mAndroidActivityFlags {nullptr};

    gator::smmuv3::default_identifiers_t smmu_identifiers;

    int mBacktraceDepth {0};
    int mSampleRate {0};
    int mDuration {0};
    int mPerfMmapSizeInPages {-1};
    int mSpeSampleRate {-1};
    int mOverrideNoPmuSlots {-1};
    int port {DEFAULT_PORT};

    CaptureOperationMode mCaptureOperationMode = CaptureOperationMode::system_wide;

    bool mFtraceRaw {false};
    bool mStopGator {false};
    bool mAllowCommands {false};
    bool mDisableCpuOnlining {false};
    bool mDisableKernelAnnotations {false};
    bool mExcludeKernelEvents {false};
    bool mEnableOffCpuSampling {false};
    bool mLogToFile {false};

    /**
     * @return - a list of argument-value pairs
     */
    const std::vector<std::pair<std::string, std::optional<std::string>>> & getArgValuePairs() const;

    /**
     * Add a new argument value pair to the list of argument-value pairs
     */
    void addArgValuePair(std::pair<std::string, std::optional<std::string>> argValuePair);

    /**
     * Move the --app or -A argument to the end of the list of argument value pairs
     */
    void moveAppArgToEndOfVector();

    /**
     * Clears the  list of argument-value pairs and set the ExecutionMode to Exit
     */
    void parsingFailed();

    /**
     * @brief Returns whether the argument parsing has succeeded or not.
     *
     * @return true When the parsing is regarded as successful.
     * @return false When the parsing has failed.  ExecutionMode is EXIT.
     */
    [[nodiscard]] bool ok() const;

    ParserResult() = default;
    ParserResult(const ParserResult &) = delete;
    ParserResult & operator=(const ParserResult &) = delete;
    ParserResult(ParserResult &&) = delete;
    ParserResult & operator=(ParserResult &&) = delete;

private:
    std::vector<std::pair<std::string, std::optional<std::string>>> argValuePairs {};
};

#endif /* PARSERRESULT_H_ */
