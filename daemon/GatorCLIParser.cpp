/* Copyright (C) 2014-2025 by Arm Limited. All rights reserved. */

#include "GatorCLIParser.h"

#include "Config.h"
#include "Configuration.h"
#include "EventCode.h"
#include "GatorCLIFlags.h"
#include "OlyUtility.h"
#include "ParserResult.h"
#include "android/Utils.h"
#include "lib/Format.h"
#include "lib/String.h"
#include "lib/Utils.h"
#include "linux/smmu_identifier.h"
#include "logging/configuration.h"
#include "metrics/definitions.hpp"
#include "metrics/metric_group_set.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/config.hpp>

#include <getopt.h>

namespace {
    constexpr int MIN_LATENCY = 4096;
    constexpr int MAX_EVENT_BIT_POSITION = 63;
    constexpr int GATOR_ANNOTATION_PORT1 = 8082;
    constexpr int GATOR_ANNOTATION_PORT2 = 8083;
    constexpr int GATOR_MAX_VALUE_PORT = 65535;
    constexpr int SPE_MAX_SAMPLE_RATE = 1000000000;

    constexpr const char * OPTSTRING_SHORT =
        ":ac:de:f:g:hi:k:l:m:n:o:p:r:s:t:u:vw:x:A:C:DE:F:I:JLM:N:O:P:Q:R:S:TW:VX:Y:Z:";

    const struct option OPTSTRING_LONG[] = { // PLEASE KEEP THIS LIST IN ALPHANUMERIC ORDER TO ALLOW EASY SELECTION
                                             // OF NEW ITEMS.
                                             // Remaining free letters are: bjqyBGHKU
        {"allow-command", /**********/ no_argument, /***/ nullptr, 'a'}, //
        {"config-xml", /*************/ required_argument, nullptr, 'c'}, //
        {"debug", /******************/ no_argument, /***/ nullptr, 'd'}, //
        {"events-xml", /*************/ required_argument, nullptr, 'e'}, //
        {"use-efficient-ftrace", /***/ required_argument, nullptr, 'f'}, //
        {"gpu-timeline", /***********/ required_argument, nullptr, 'g'}, //
        {"help", /*******************/ no_argument, /***/ nullptr, 'h'}, //
        {"pid", /********************/ required_argument, nullptr, 'i'}, //
        {"exclude-kernel", /*********/ required_argument, nullptr, 'k'}, //
        ANDROID_PACKAGE,                                                 //
        ANDROID_ACTIVITY,                                                //
        PACKAGE_FLAGS,                                                   //
        {"output", /*****************/ required_argument, nullptr, 'o'}, //
        {"port", /*******************/ required_argument, nullptr, 'p'}, //
        {"sample-rate", /************/ required_argument, nullptr, 'r'}, //
        {"session-xml", /************/ required_argument, nullptr, 's'}, //
        {"max-duration", /***********/ required_argument, nullptr, 't'}, //
        {"call-stack-unwinding", /***/ required_argument, nullptr, 'u'}, //
        {"version", /****************/ no_argument, /***/ nullptr, 'v'}, //
        {"app-cwd", /****************/ required_argument, nullptr, 'w'}, //
        {"stop-on-exit", /***********/ required_argument, nullptr, 'x'}, //
        {"smmuv3-model", /***********/ required_argument, nullptr, 'z'}, //
        APP,                                                             //
        {"counters", /***************/ required_argument, nullptr, 'C'}, //
        {"disable-kernel-annotations", no_argument, /***/ nullptr, 'D'}, //
        {"append-events-xml", /******/ required_argument, nullptr, 'E'}, //
        {"spe-sample-rate", /********/ required_argument, nullptr, 'F'}, //
        {"inherit", /****************/ required_argument, nullptr, 'I'}, //
        {"probe-report", /***********/ no_argument, /***/ nullptr, 'J'}, //
        {"capture-log", /************/ no_argument, /***/ nullptr, 'L'}, //
        {"metric-group", /***********/ required_argument, nullptr, 'M'}, //
        {"num-pmu-counters", /*******/ required_argument, nullptr, 'N'}, //
        {"disable-cpu-onlining", /***/ required_argument, nullptr, 'O'}, //
        {"pmus-xml", /***************/ required_argument, nullptr, 'P'}, //
        WAIT_PROCESS,                                                    //
        {"print", /******************/ required_argument, nullptr, 'R'}, //
        {"system-wide", /************/ required_argument, nullptr, 'S'}, //
        {"trace", /******************/ no_argument, /***/ nullptr, 'T'}, //
        {"version", /****************/ no_argument, /***/ nullptr, 'V'}, //
        {"workflow", /***************/ required_argument, nullptr, 'W'}, //
        {"spe", /********************/ required_argument, nullptr, 'X'}, //
        {"off-cpu-time", /***********/ required_argument, nullptr, 'Y'}, //
        {"mmap-pages", /*************/ required_argument, nullptr, 'Z'}, //
        {nullptr, 0, nullptr, 0}};

    const char PRINTABLE_SEPARATOR = ',';

    const char * LOAD_OPS = "LD";
    const char * STORE_OPS = "ST";
    const char * BRANCH_OPS = "B";
    // SPE
    const char SPES_KEY_VALUE_DELIMITER = ',';
    const char SPE_DATA_DELIMITER = ':';
    const char SPE_KEY_VALUE_DELIMITER = '=';
    const char * SPE_MIN_LATENCY_KEY = "min_latency";
    const char * SPE_EVENTS_KEY = "events";
    const char * SPE_OPS_KEY = "ops";
    const char * SPE_INV_KEY = "inv";

    // trim
    void trim(std::string & data)
    {
        //trim from  left
        data.erase(data.begin(), std::find_if(data.begin(), data.end(), [](int ch) { return std::isspace(ch) == 0; }));
        //trim from right
        data.erase(std::find_if(data.rbegin(), data.rend(), [](int ch) { return std::isspace(ch) == 0; }).base(),
                   data.end());
    }

    void split(const std::string & data, char delimiter, std::vector<std::string> & tokens)
    {
        std::string token;
        std::istringstream dataStream(data);
        while (std::getline(dataStream, token, delimiter)) {
            trim(token);
            tokens.push_back(token);
        }
    }

    /**
     * @return 1 for a true-like string, 0 for a false-like string and -1 otherwise
     */
    int parseBoolean(const char * value)
    {
        if (strcasecmp(value, "yes") == 0     //
            || strcasecmp(value, "y") == 0    //
            || strcasecmp(value, "true") == 0 //
            || strcmp(value, "1") == 0) {
            return 1;
        }
        if (strcasecmp(value, "no") == 0       //
            || strcasecmp(value, "n") == 0     //
            || strcasecmp(value, "false") == 0 //
            || strcmp(value, "0") == 0) {
            return 0;
        }
        return -1;
    }

    //NOLINTNEXTLINE(modernize-avoid-c-arrays)
    int findIndexOfArg(const std::string_view arg_toCheck, int argc, const char * const argv[])
    {
        for (int j = 1; j < argc; j++) {
            if (std::string_view(argv[j]) == arg_toCheck) {
                return j;
            }
        }
        return -1;
    }

    // check the presence of any of the arguments in dict before --app or -A
    template<std::size_t SIZE>
    //NOLINTNEXTLINE(modernize-avoid-c-arrays)
    bool checkBeforeApp(const std::array<std::string_view, SIZE> & dict, int argc, const char * const argv[])
    {
        int appIndex = findIndexOfArg("--app", argc, argv);
        if (appIndex == -1) {
            appIndex = findIndexOfArg("-A", argc, argv);
        }

        for (int j = 1; j < argc; ++j) {
            const std::string_view arg(argv[j]);
            if (std::find(dict.begin(), dict.end(), arg) != std::end(dict)) {
                return appIndex <= -1 || j < appIndex;
            }
        }

        return false;
    }

    // Returns true if the string represented a metric group and this function
    // handled it.
    bool handleMetricGroupOption(ParserResult & result, std::string_view arg_value)
    {
        using metrics::metric_group_id_t;
        using metrics::metric_group_set_t;

        if (arg_value == "workflow_topdown_basic") {
            metric_group_set_t const s {{metrics::metric_group_id_t::basic}};
            result.enabled_metric_groups = result.enabled_metric_groups.set_union(s);
            return true;
        }
        if (arg_value == "workflow_all") {
            metric_group_set_t const s {true};
            result.enabled_metric_groups = result.enabled_metric_groups.set_union(s);
            return true;
        }

        using enum_type = std::underlying_type_t<metric_group_id_t>;
        for (auto i = static_cast<enum_type>(metric_group_id_t::begin);
             i != static_cast<enum_type>(metric_group_id_t::end);
             ++i) {
            auto group = static_cast<metric_group_id_t>(i);
            if (arg_value == metrics::metric_group_id_to_string(group)) {
                metric_group_set_t const s({group});
                result.enabled_metric_groups = result.enabled_metric_groups.set_union(s);
                return true;
            }
        }

        return false;
    }

    std::string_view slice(std::string const & src, int startpos, int pos)
    {
        std::size_t const len = pos - startpos;

        return {src.c_str() + startpos, len};
    }

    std::pair<std::string_view, std::string_view> split_one(std::string_view data, char delimiter)
    {
        if (auto offset = data.find(delimiter); offset != std::string_view::npos) {
            return {data.substr(0, offset), data.substr(offset + 1)};
        }
        return {data, ""};
    }

    EventCode parseEvent(std::string_view event, ParserResult & result)
    {
        if (event.empty()) {
            return {};
        }
        long long eventCode;
        std::string eventStr {event};
        if (!stringToLongLong(&eventCode, eventStr.c_str(), OlyBase::Decimal)) {         //check for decimal
            if (!stringToLongLong(&eventCode, eventStr.c_str(), OlyBase::Hexadecimal)) { //check for hex
                result.error_messages.emplace_back("event must be an integer");
                return {};
            }
        }
        return EventCode {static_cast<uint64_t>(eventCode)};
    }

    bool tryInsert(std::map<std::string, EventCode> & events, std::string_view counterName, EventCode eventCode)
    {
        for (auto const & kv_pair : events) {
            auto const & key = kv_pair.first;
            if (boost::iequals(key, counterName)) {
                return false;
            }
        }
        auto const ins_result = events.insert(std::make_pair(counterName, eventCode));
        return ins_result.second;
    }
}

using ExecutionMode = ParserResult::ExecutionMode;

std::pair<SampleRate, SampleRate> getSampleRate(const std::string & value)
{
    if (value == "high") {
        return {high, high};
    }
    if (value == "normal") {
        return {normal, normal_x2};
    }
    if (value == "low") {
        return {low, low};
    }
    if (value == "none") {

        return {none, none};
    }
    return {invalid, invalid};
}

void GatorCLIParser::addCounter(std::string_view counter)
{
    if (handleMetricGroupOption(result, counter)) {
        return;
    }

    auto const [counterNameView, eventStrView] = split_one(counter, ':');

    EventCode const event = parseEvent(eventStrView, result);
    if (!eventStrView.empty() && !event.isValid()) {
        result.parsingFailed();
        return;
    }

    bool const inserted = tryInsert(result.events, counterNameView, event);

    if (!inserted) {
        result.error_messages.emplace_back(lib::Format() << "Counter already added. " << counterNameView);
        result.parsingFailed();
    }
}

int GatorCLIParser::findAndUpdateCmndLineCmnd(int argc, char ** argv)
{
    std::string command;
    result.mCaptureCommand.clear();
    int found = 0;
    std::string shortAppArg("-A");
    std::string longAppArg("--app");
    for (int j = 1; j < argc; j++) {
        std::string arg(argv[j]);
        if (arg == shortAppArg || arg == longAppArg) {
            found = j;
            break;
        }
    }
    if (found > 0) {
        if (argc > found) {
            if (argc > found + 1) {
                for (int i = found + 1; i < argc; i++) {
                    result.mCaptureCommand.emplace_back(argv[i]);
                    command += " " + std::string(argv[i]);
                }
            }
            size_t start = command.find_first_not_of(' ');
            result.addArgValuePair({"A",
                                    start == std::string::npos ? std::optional<std::string>(std::move(command))
                                                               : std::optional<std::string>(command.substr(start))});
            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_CAPTURE_COMMAND;
        }
    }
    return found;
}

//NOLINTNEXTLINE(readability-function-cognitive-complexity)
void GatorCLIParser::parseAndUpdateSpe(const std::string & arguments)
{
    std::vector<std::string> spe_data;
    split(arguments, SPE_DATA_DELIMITER, spe_data);
    if (!spe_data.empty()) {
        //add details to structure
        if (!spe_data[0].empty()) { //check if cpu id provided
            SpeConfiguration data;
            data.id = spe_data[0];
            spe_data.erase(spe_data.begin());
            for (const auto & spe_data_it : spe_data) {
                std::vector<std::string> spe;
                split(spe_data_it, SPE_KEY_VALUE_DELIMITER, spe);
                if (spe[0] == SPE_INV_KEY) {
                    // If the inverse toggle is included, enable the filter mask.
                    data.inverse_event_filter_mask = true;
                    continue;
                }
                if (spe.size() == 2) { //should be a key value pair to add
                    if (spe[0] == SPE_MIN_LATENCY_KEY) {
                        if (!stringToInt(&(data.min_latency), spe[1].c_str())) {
                            result.error_messages.emplace_back(lib::Format() << "latency not an integer " << data.id
                                                                             << " (" << spe[1] << ")");
                            result.parsingFailed();
                            return;
                        }
                        if (data.min_latency < 0 || data.min_latency >= MIN_LATENCY) {
                            result.error_messages.emplace_back(lib::Format()
                                                               << "Invalid minimum latency for " << data.id.c_str()
                                                               << " (" << data.min_latency << ")");
                            result.parsingFailed();
                            return;
                        }
                    }
                    else if (spe[0] == SPE_EVENTS_KEY) {
                        std::vector<std::string> spe_events;
                        split(spe[1], SPES_KEY_VALUE_DELIMITER, spe_events);
                        for (const std::string & spe_event : spe_events) {
                            int event;
                            if (!stringToInt(&event, spe_event.c_str(), OlyBase::Decimal)) {
                                result.error_messages.emplace_back(
                                    lib::Format() << "Event filter cannot be a non integer , failed for " << spe_event);
                                result.parsingFailed();
                                return;
                            }
                            if ((event < 0 || event > MAX_EVENT_BIT_POSITION)) {
                                result.error_messages.emplace_back(
                                    lib::Format()
                                    << "Event filter should be a bit position from 0 - 63 , failed for " << event);
                                result.parsingFailed();
                                return;
                            }
                            //FIXME
                            //NOLINTNEXTLINE(hicpp-signed-bitwise)
                            data.event_filter_mask = data.event_filter_mask | 1 << event;
                        }
                    }
                    else if (spe[0] == SPE_OPS_KEY) {
                        std::vector<std::string> spe_ops;
                        split(spe[1], SPES_KEY_VALUE_DELIMITER, spe_ops);
                        if (!spe_ops.empty()) {
                            data.ops.clear();
                            //convert to enum
                            for (const auto & spe_ops_it : spe_ops) {
                                if (strcasecmp(spe_ops_it.c_str(), LOAD_OPS) == 0) {
                                    data.ops.insert(SpeOps::LOAD);
                                }
                                else if (strcasecmp(spe_ops_it.c_str(), STORE_OPS) == 0) {
                                    data.ops.insert(SpeOps::STORE);
                                }
                                else if (strcasecmp(spe_ops_it.c_str(), BRANCH_OPS) == 0) {
                                    data.ops.insert(SpeOps::BRANCH);
                                }
                                else {
                                    result.error_messages.emplace_back(lib::Format()
                                                                       << "Not a valid Ops " << spe_ops_it);
                                    result.parsingFailed();
                                    return;
                                }
                            }
                        }
                    }
                    else { // invalid key
                        result.error_messages.emplace_back(lib::Format()
                                                           << "--spe arguments not in correct format " << spe_data_it);
                        result.parsingFailed();
                        return;
                    }
                }
                else {
                    result.error_messages.emplace_back(lib::Format()
                                                       << "--spe arguments not in correct format " << spe_data_it);
                    result.parsingFailed();
                    return;
                }
            }
            result.mSpeConfigs.push_back(data);
            result.error_messages.emplace_back(lib::Format() << "Adding spe -> " << data.id);
        }
        else {
            result.error_messages.emplace_back("No Id provided for --spe");
            result.parsingFailed();
            return;
        }
    }
}

bool handleMetricGroups(ParserResult & parserResult, std::string const & arg_value)
{
    int startpos = -1;
    size_t metricSplitPos = 0;

    std::string failedMetric;

    while ((metricSplitPos = arg_value.find(',', startpos + 1)) != std::string::npos) {
        auto current_metric = slice(arg_value, startpos + 1, metricSplitPos);
        auto result = handleMetricGroupOption(parserResult, current_metric);
        if (!result) {
            failedMetric = current_metric;

            parserResult.error_messages.emplace_back(lib::Format()
                                                     << "Invalid value for --metric-group (" << failedMetric << "):");

            return false;
        }

        startpos = metricSplitPos;
    }

    auto last_value = slice(arg_value, startpos + 1, arg_value.length());
    auto result = handleMetricGroupOption(parserResult, last_value);

    if (!result) {
        if (failedMetric.empty()) {
            failedMetric = last_value;
        }
        parserResult.error_messages.emplace_back(lib::Format()
                                                 << "Invalid value for --metric-group (" << failedMetric << "):");
        return false;
    }

    return true;
}

void GatorCLIParser::handleCounterList(const std::string & value)
{
    int startpos = -1;
    size_t counterSplitPos = 0;

    while ((counterSplitPos = value.find(',', startpos + 1)) != std::string::npos) {
        addCounter(slice(value, startpos + 1, counterSplitPos));
        startpos = counterSplitPos;
    }
    addCounter(slice(value, startpos + 1, value.length()));
}

void GatorCLIParser::parseCLIArguments(int argc,
                                       char * argv[], // NOLINT(modernize-avoid-c-arrays)
                                       const char * version_string,
                                       const char * gSrcMd5,
                                       const char * gBuildId)
{
    const int indexApp = findAndUpdateCmndLineCmnd(argc, argv);
    if (indexApp > 0) {
        argc = indexApp;
    }
    bool inheritSet = false;
    bool systemWideSet = false;
    bool userSetIncludeKernelEvents = false;
    optind = 1;
    opterr = 0; // Tell getopt_long not to report errors
    int c;
    while ((c = getopt_long(argc, argv, OPTSTRING_SHORT, OPTSTRING_LONG, nullptr)) != -1) {
        const int optionInt = optarg == nullptr ? -1 : parseBoolean(optarg);
        std::pair<SampleRate, SampleRate> sampleRate;
        std::string value;
        result.addArgValuePair({std::string(1, char(c)),                               //
                                optarg != nullptr ? std::optional<std::string>(optarg) //
                                                  : std::nullopt});
        switch (c) {
            case 'N':
                if (!stringToInt(&result.mOverrideNoPmuSlots, optarg, OlyBase::Decimal)) {
                    result.error_messages.emplace_back("-N must be followed by an non-zero positive number");
                    result.parsingFailed();
                    return;
                }
                if (result.mOverrideNoPmuSlots <= 0) {
                    result.error_messages.emplace_back("-N must be followed by an non-zero positive number");
                    result.parsingFailed();
                    return;
                }
                break;
            case 'c':
                result.mConfigurationXMLPath = optarg;
                break;
            case 'd':
                // Already handled - see references to GatorCLIParser::hasDebugFlag
                break;
            case 'e': //event xml path
                result.mEventsXMLPath = optarg;
                break;
            case 'E': // events xml path for append
                result.mEventsXMLAppend = optarg;
                break;
            case 'g':
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_GPU_TIMELINE;
                if (optionInt > 0) {
                    result.mGPUTimelineEnablement = GPUTimelineEnablement::enable;
                }
                else if (optionInt == 0) {
                    result.mGPUTimelineEnablement = GPUTimelineEnablement::disable;
                }
                else if (strcasecmp(optarg, "auto") == 0) {
                    result.mGPUTimelineEnablement = GPUTimelineEnablement::automatic;
                }
                else {
                    result.error_messages.emplace_back(lib::Format() << "Invalid argument for -g/--gpu-timeline (" << optarg << ")");
                    result.parsingFailed();
                    return;
                }
                break;
            case 'P':
                result.pmuPath = optarg;
                break;
            case 'p': //port
                if (strcasecmp(optarg, "uds") == 0) {
                    result.port = DISABLE_TCP_USE_UDS_PORT;
                }
                else {
                    if (!stringToInt(&result.port, optarg, OlyBase::Decimal)) {
                        result.error_messages.emplace_back("Port must be an integer");
                        result.parsingFailed();
                        return;
                    }
                    if ((result.port == GATOR_ANNOTATION_PORT1) || (result.port == GATOR_ANNOTATION_PORT2)) {
                        result.error_messages.emplace_back(
                            lib::Format()
                            << "Gator can't use port" << result.port
                            << "as it already uses ports 8082 and 8083 for annotations. Please select a different "
                               "port.");
                        result.parsingFailed();
                        return;
                    }
                    if (result.port < 1 || result.port > GATOR_MAX_VALUE_PORT) {

                        result.error_messages.emplace_back(
                            lib::Format() << "Gator can't use port" << result.port
                                          << ", as it is not valid. Please pick a value between 1 and 65535");
                        result.parsingFailed();
                        return;
                    }
                }
                break;
            case 's': //specify path for session xml
                result.mSessionXMLPath = optarg;
                break;
            case 'o': //output
                result.mTargetPath = optarg;
                result.mode = ExecutionMode::LOCAL_CAPTURE;
                break;
            case 'a': //app allowed needed while running in interactive mode not needed for local capture.
                result.mAllowCommands = true;
                break;
            case 'u': //-call-stack-unwinding
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_CALL_STACK_UNWINDING;
                if (optionInt < 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --call-stack-unwinding ("
                                                                     << optarg << "), 'yes' or 'no' expected.");
                    result.parsingFailed();
                    return;
                }
                result.mBacktraceDepth = optionInt == 1 ? 128 : 0;
                break;
            case 'r': //sample-rate
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_SAMPLE_RATE;
                value = std::string(optarg);
                sampleRate = getSampleRate(value);
                if (sampleRate.first != invalid) {
                    result.mSampleRate = sampleRate.first;
                    result.mSampleRateGpu = sampleRate.second;
                }
                else {
                    result.error_messages.emplace_back(lib::Format() << "Invalid sample rate (" << optarg << ").");
                    result.parsingFailed();
                    return;
                }
                break;
            case 't': //max-duration
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_DURATION;

                if (!stringToInt(&result.mDuration, optarg, OlyBase::Decimal)) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid max duration (" << optarg << ").");
                    result.parsingFailed();
                    return;
                }
                break;
            case 'f': //use-efficient-ftrace
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_FTRACE_RAW;
                if (optionInt < 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --use-efficient-ftrace ("
                                                                     << optarg << "), 'yes' or 'no' expected.");
                    result.parsingFailed();
                    return;
                }
                result.mFtraceRaw = optionInt == 1;
                break;
            case 'S': //--system-wide
            {
                if (optionInt < 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --system-wide (" << optarg
                                                                     << "), 'yes' or 'no' expected.");
                    result.parsingFailed();
                    return;
                }
                auto const is_system_wide = (optionInt == 1);
                if (inheritSet) {
                    if ((is_system_wide && !isCaptureOperationModeSystemWide(result.mCaptureOperationMode))
                        || (!is_system_wide && isCaptureOperationModeSystemWide(result.mCaptureOperationMode))) {
                        result.error_messages.emplace_back(
                            "Invalid combination for --system-wide and --inherit arguments");
                        result.parsingFailed();
                        return;
                    }
                    // no change in state
                    break;
                }
                result.mCaptureOperationMode = (is_system_wide ? CaptureOperationMode::system_wide //
                                                               : CaptureOperationMode::application_default);
                systemWideSet = true;
                break;
            }
            case 'w': //app-pwd
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_CAPTURE_WORKING_DIR;

                result.mCaptureWorkingDir = optarg;
                break;
            case 'A': //app
                //already handled for --app
                break;
            case 'x': //stop on exit
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_STOP_GATOR;
                if (optionInt < 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --stop-on-exit (" << optarg
                                                                     << "), 'yes' or 'no' expected.");
                    result.parsingFailed();
                    return;
                }
                result.mStopGator = optionInt == 1;
                break;
            case 'z':
                if (optarg != nullptr) {
                    auto args = std::string_view(optarg);
                    auto split_pos = args.find(',');

                    result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_SMMU_MODEL;

                    if (split_pos != std::string::npos && split_pos < args.size() - 1) {
                        result.smmu_identifiers.set_tcu_identifier(
                            gator::smmuv3::smmuv3_identifier_t(args.substr(0, split_pos)));
                        result.smmu_identifiers.set_tbu_identifier(
                            gator::smmuv3::smmuv3_identifier_t(args.substr(split_pos + 1)));
                    }
                    else {
                        result.smmu_identifiers.set_tcu_identifier(gator::smmuv3::smmuv3_identifier_t(args));
                        result.smmu_identifiers.set_tbu_identifier(gator::smmuv3::smmuv3_identifier_t(args));
                    }
                }
                break;
            case 'W': // workflow
            {
                bool workflowSet = false;
                value = std::string(optarg);
                if (value == "topdown") {
                    handleCounterList("workflow_topdown_basic");
                    workflowSet = true;
                }
                else if (value == "spe") {
                    const std::string defaultSPEParameters = "workflow_spe:";
                    parseAndUpdateSpe(defaultSPEParameters);
                    workflowSet = true;
                }
                if (!workflowSet) {
                    result.error_messages.emplace_back(lib::Format()
                                                       << "Invalid value for --workflow (" << optarg << "):");
                    result.parsingFailed();
                }
                break;
            }
            case 'M': {
                auto metric_result = handleMetricGroups(result, optarg);
                if (!metric_result) {
                    result.parsingFailed();
                }
                break;
            }
            case 'C': //counter
            {
                value = std::string(optarg);
                handleCounterList(value);
                break;
            }
            case 'D': // disable kernel annotations
                result.mDisableKernelAnnotations = true;
                break;
            case 'X': // spe
            {
                value = std::string(optarg);
                parseAndUpdateSpe(value);
                if (result.mode == ExecutionMode::EXIT) {
                    return;
                }
            } break;
            case 'i': // pid
            {
                auto const pids = lib::parseCommaSeparatedNumbers<int>(optarg);
                if (!pids) {
                    result.error_messages.emplace_back(lib::Format()
                                                       << "Invalid value for --pid (" << optarg
                                                       << "), comma separated and numeric list expected.");
                    result.parsingFailed();
                    return;
                }

                result.mPids.insert(pids->begin(), pids->end());
                break;
            }
            case 'h':
                result.mode = ExecutionMode::USAGE;
                return;
            case 'v': // version is already printed/logged at the start of this function
                result.parsingFailed();
                return;
            case 'V':
                result.error_messages.emplace_back(lib::Format() << version_string << "\nSRC_MD5: " << gSrcMd5
                                                                 << "\nBUILD_ID: " << gBuildId);
                result.parsingFailed();
                return;
            case 'O':
                if (optionInt < 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --disable-cpu-onlining ("
                                                                     << optarg << "), 'yes' or 'no' expected.");
                    result.parsingFailed();
                    return;
                }
                result.mDisableCpuOnlining = optionInt == 1;
                break;
            case 'Q':
                result.mWaitForCommand = optarg;
                break;
            case 'Z':
                result.mPerfMmapSizeInPages = -1;
                if (!stringToInt(&result.mPerfMmapSizeInPages, optarg)) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --mmap-pages (" << optarg
                                                                     << "): not an integer");
                    result.parsingFailed();
                    result.mPerfMmapSizeInPages = -1;
                }
                else if (result.mPerfMmapSizeInPages < 1) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --mmap-pages (" << optarg
                                                                     << "): not more than 0");
                    result.parsingFailed();
                    result.mPerfMmapSizeInPages = -1;
                }
                //FIXME
                //NOLINTNEXTLINE(hicpp-signed-bitwise)
                else if (((result.mPerfMmapSizeInPages - 1) & result.mPerfMmapSizeInPages) != 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --mmap-pages (" << optarg
                                                                     << "): not a power of 2");
                    result.parsingFailed();
                    result.mPerfMmapSizeInPages = -1;
                }
                break;
            case 'R': {
                result.mode = ExecutionMode::PRINT;
                std::vector<std::string> parts;
                split(optarg, PRINTABLE_SEPARATOR, parts);
                for (const auto & part : parts) {
                    if (strcasecmp(part.c_str(), "events.xml") == 0) {
                        result.printables.insert(ParserResult::Printable::EVENTS_XML);
                    }
                    else if (strcasecmp(part.c_str(), "counters.xml") == 0) {
                        result.printables.insert(ParserResult::Printable::COUNTERS_XML);
                    }
                    else if (strcasecmp(part.c_str(), "defaults.xml") == 0) {
                        result.printables.insert(ParserResult::Printable::DEFAULT_CONFIGURATION_XML);
                    }
                    else if (strcasecmp(part.c_str(), "counters") == 0) {
                        result.printables.insert(ParserResult::Printable::COUNTERS);
                    }
                    else if (strcasecmp(part.c_str(), "detailed-counters") == 0) {
                        result.printables.insert(ParserResult::Printable::COUNTERS_DETAILED);
                    }
                    else if (strcasecmp(part.c_str(), "workflows") == 0) {
                        result.printables.insert(ParserResult::Printable::WORKFLOW);
                    }
                    else {
                        result.error_messages.emplace_back(lib::Format()
                                                           << "Invalid value for --print (" << optarg << ")");
                        result.parsingFailed();
                        return;
                    }
                }
                break;
            }
            case 'F': {
                result.mSpeSampleRate = -1;
                if (!stringToInt(&result.mSpeSampleRate, optarg)) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --spe-sample-rate ("
                                                                     << optarg << "): not an integer");
                    result.parsingFailed();
                    result.mSpeSampleRate = -1;
                }
                else if ((result.mSpeSampleRate < 1) || (result.mSpeSampleRate > SPE_MAX_SAMPLE_RATE)) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --spe-sample-rate ("
                                                                     << optarg << "): default value will be used");
                    result.mSpeSampleRate = -1;
                }
                break;
            }
            case 'k': {
                if (optionInt < 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --exclude-kernel (" << optarg
                                                                     << "), 'yes' or 'no' expected.");
                    result.parsingFailed();
                    return;
                }
                result.mExcludeKernelEvents = optionInt == 1;
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_EXCLUDE_KERNEL;

                if (!result.mExcludeKernelEvents) {
                    userSetIncludeKernelEvents = true;
                }
                break;
            }
            case 'Y': {
                if (optionInt < 0) {
                    result.error_messages.emplace_back(lib::Format() << "Invalid value for --off-cpu-time (" << optarg
                                                                     << "), 'yes' or 'no' expected.");
                    result.parsingFailed();
                    return;
                }
                result.mEnableOffCpuSampling = optionInt == 1;
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_OFF_CPU_PROFILING;
                break;
            }
            case 'I': {
                CaptureOperationMode newMode;

                if (optionInt > 0) {
                    newMode = CaptureOperationMode::application_inherit;
                }
                else if (optionInt == 0) {
                    newMode = CaptureOperationMode::application_no_inherit;
                }
                else if (strcasecmp(optarg, "poll") == 0) {
                    newMode = CaptureOperationMode::application_poll;
                }
                else if (strcasecmp(optarg, "experimental") == 0) {
                    newMode = CaptureOperationMode::application_experimental_patch;
                }
                else {
                    result.error_messages.emplace_back(lib::Format()
                                                       << "Invalid value for --inherit (" << optarg
                                                       << "), 'yes', 'no', 'poll', or 'experimental' expected.");
                    result.parsingFailed();
                    return;
                }

                if (systemWideSet && isCaptureOperationModeSystemWide(result.mCaptureOperationMode)) {
                    result.error_messages.emplace_back("Invalid combination for --system-wide and --inherit arguments");
                    result.parsingFailed();
                    return;
                }

                inheritSet = true;
                result.mCaptureOperationMode = newMode;
                break;
            }
            case 'l': // android-pkg
            {
                result.mAndroidPackage = optarg;
                break;
            }
            case 'm': // android-activity
            {
                result.mAndroidActivity = optarg;
                break;
            }
            case 'n': // activity-args
            {
                result.mAndroidActivityFlags = optarg;
                break;
            }
            case 'T': {
                logging::set_log_enable_trace(true);
                break;
            }
            case 'L': {
                result.mLogToFile = true;
                break;
            }
            case 'J': {
                result.mHasProbeReportFlag = GatorCLIParser::hasProbeReportFlag(argc, argv);
                break;
            }
            case ':': // Missing argument
            case '?': // Unrecognised
            default: {
                const char * opt_string = argv[optind - 1];
                while (*opt_string == '-') {
                    opt_string++;
                }

                result.error_messages.emplace_back(
                    lib::Format() << (c == ':' ? "Missing argument for options: " : "Unrecognised option: ")
                                  << opt_string << "\nSee --help for more information.");
                result.parsingFailed();
                return;
            }
        }
    }
    if (indexApp > 0) {
        //Some --app args was found
        result.moveAppArgToEndOfVector();
    }

    // Defaults depending on other flags
    const bool haveProcess = !result.mCaptureCommand.empty() || !result.mPids.empty()
                          || result.mWaitForCommand != nullptr || result.mAndroidPackage != nullptr;

    // default to stopping on process exit unless user specified otherwise
    if (haveProcess && ((result.parameterSetFlag & USE_CMDLINE_ARG_STOP_GATOR) == 0)) {
        result.mStopGator = true;
        result.parameterSetFlag |=
            USE_CMDLINE_ARG_STOP_GATOR; // must be set, otherwise session.xml will override during live mode (which leads to counter-intuitive behaviour)
    }

    if (!systemWideSet && !inheritSet) {
#if CONFIG_PREFER_SYSTEM_WIDE_MODE
        // default to system-wide mode unless a process option was specified
        result.mCaptureOperationMode =
            (!haveProcess ? CaptureOperationMode::system_wide : CaptureOperationMode::application_default);
#else
        // user must explicitly request system-wide mode
        result.mCaptureOperationMode = CaptureOperationMode::application_default;
#endif
    }

    auto const is_system_wide = isCaptureOperationModeSystemWide(result.mCaptureOperationMode);

    //If the the capture isn't system wide and the user didn't explicitly include kernel events, we exclude them by default.
    if (!is_system_wide && !userSetIncludeKernelEvents) {
        result.mExcludeKernelEvents = true;
    }

    if (result.mode == ExecutionMode::LOCAL_CAPTURE) {
        if (result.mAllowCommands) {
            result.error_messages.emplace_back("--allow-command is not applicable in local capture mode.");
            result.parsingFailed();
            return;
        }
        if (result.port != DEFAULT_PORT) {
            result.error_messages.emplace_back("--port is not applicable in local capture mode");
            result.parsingFailed();
            return;
        }

        if (!is_system_wide && result.mSessionXMLPath == nullptr && !haveProcess) {
            result.error_messages.emplace_back(
                "In local capture mode, without --system-wide=yes, a process to profile must be specified "
                "with --session-xml, --app, --wait-process, --pid, or --android-pkg.");
            result.parsingFailed();
            return;
        }

        if ((result.events.empty() && result.enabled_metric_groups.empty())
            && (result.mConfigurationXMLPath == nullptr)) {
            result.error_messages.emplace_back("No counters (--counters) specified, default counters will be used");
        }
    }
    else if (result.mode == ExecutionMode::DAEMON) {
        if (!is_system_wide && !result.mAllowCommands && !haveProcess) {
            result.error_messages.emplace_back(
                "In daemon mode, without --system-wide=yes, a process to profile must be specified with "
                "--allow-command, --app, --wait-process, --pid, or --android-pkg.");
            result.parsingFailed();
            return;
        }
        if (result.mSessionXMLPath != nullptr) {
            result.error_messages.emplace_back("--session-xml is not applicable in daemon mode.");
            result.parsingFailed();
            return;
        }
        if (!result.events.empty() || !result.enabled_metric_groups.empty()) {
            result.error_messages.emplace_back("--counters is not applicable in daemon mode.");
            result.parsingFailed();
            return;
        }
        if (!result.mSpeConfigs.empty()) {
            result.error_messages.emplace_back("--spe is not applicable in daemon mode.");
            result.parsingFailed();
            return;
        }
    }

    if ((result.mAndroidActivity != nullptr) && (result.mAndroidPackage == nullptr)) {
        result.error_messages.emplace_back("--android-pkg must be specified when supplying --android-activity.");
        result.parsingFailed();
        return;
    }

    if (result.mAndroidActivityFlags != nullptr
        && (result.mAndroidActivity == nullptr || result.mAndroidPackage == nullptr)) {
        result.error_messages.emplace_back(
            "--activity-args must be used together with --android-package and --android-activity");
        result.parsingFailed();
        return;
    }

    const bool hasAnotherProcessArg = !result.mCaptureCommand.empty() || !result.mPids.empty()
                                   || result.mWaitForCommand != nullptr || result.mAllowCommands;
    if ((result.mAndroidPackage != nullptr) && hasAnotherProcessArg) {
        result.error_messages.emplace_back(
            "--android-pkg is not compatible with --allow-command, --app, --wait-process, or --pid.");
        result.parsingFailed();
        return;
    }

#if !defined(__ANDROID__)
    if (result.mAndroidPackage != nullptr) {
        //__ANDROID__ will not be defined in case of static linking with musl, logging this only as a warning.
        result.error_messages.emplace_back("--android-pkg will only work on Android OS.");
    }
#endif

    if (result.mAndroidPackage != nullptr && !lib::isRootOrShell()) {
        result.error_messages.emplace_back("--android-pkg requires to be run from a shell or root user.");
        result.parsingFailed();
        return;
    }

    if (result.mAndroidPackage != nullptr) {
        const bool packageFound = android_utils::packageExists(result.mAndroidPackage);
        if (!packageFound) {
            const std::string error_msg = "Android package, " + std::string(result.mAndroidPackage) + ", not found.";
            result.error_messages.emplace_back(error_msg);
            result.parsingFailed();
            return;
        }
    }

    if (result.mDuration < 0) {
        result.error_messages.emplace_back(lib::Format()
                                           << "Capture duration cannot be a negative value : " << result.mDuration);
        result.parsingFailed();
        return;
    }

    if (indexApp > 0 && result.mCaptureCommand.empty()) {
        result.error_messages.emplace_back("--app requires a command to be specified");
        result.parsingFailed();
        return;
    }

    if ((indexApp > 0) && (result.mWaitForCommand != nullptr)) {
        result.error_messages.emplace_back("--app and --wait-process are mutually exclusive");
        result.parsingFailed();
        return;
    }
    if (indexApp > 0 && result.mAllowCommands) {
        result.error_messages.emplace_back(
            "Cannot allow command (--allow-command) from Streamline, if --app is specified.");
        result.parsingFailed();
        return;
    }
    // Error checking
    if (optind < argc) {
        result.error_messages.emplace_back(lib::Format() << "Unknown argument:" << argv[optind]
                                                         << ". Use --help to list valid arguments.");
        result.parsingFailed();
        return;
    }
}

using namespace std::literals::string_view_literals;

//NOLINTNEXTLINE(modernize-avoid-c-arrays)
bool GatorCLIParser::hasDebugFlag(int argc, const char * const argv[])
{
    constexpr std::array<std::string_view, 4> args {{"-d"sv, "--debug"sv, "-T"sv, "--trace"sv}};

    return checkBeforeApp(args, argc, argv);
}

//NOLINTNEXTLINE(modernize-avoid-c-arrays)
bool GatorCLIParser::hasCaptureLogFlag(int argc, const char * const argv[])
{
    constexpr std::array<std::string_view, 2> args {{"-L"sv, "--capture-log"sv}};

    return checkBeforeApp(args, argc, argv);
}

bool GatorCLIParser::hasProbeReportFlag(int argc, const char * const argv[])
{
    constexpr std::array<std::string_view, 2> args {{"-J"sv, "--probe-report"sv}};

    return checkBeforeApp(args, argc, argv);
}
/* ------------------------------------ last character before new line here ----+ */
/*                                                                              | */
/*                                                                              v */
const char * const GatorCLIParser::USAGE_MESSAGE = R"(
Streamline has 2 modes of operation. Daemon mode (the default), and local
capture mode, which will capture to disk and then exit. To enable local capture
mode specify an output directory with --output.

* Arguments available to all modes:
  -h|--help                             This help page
  -c|--config-xml <config_xml>          Specify path and filename of the
                                        configuration XML. In daemon mode the
                                        list of counters will be written to
                                        this file. In local capture mode the
                                        list of counters will be read from this
                                        file.
  -e|--events-xml <events_xml>          Specify path and filename of the events
                                        XML to use
  -E|--append-events-xml <events_xml>   Specify path and filename of events XML
                                        to append
  -P|--pmus-xml <pmu_xml>               Specify path and filename of pmu XML to
                                        append
  -v|--version                          Print version information
  -d|--debug                            Enable debug messages
  -A|--app <cmd> <args...>              Specify the command to execute once the
                                        capture starts. Must be the last
                                        argument passed to gatord as all
                                        subsequent arguments are passed to the
                                        launched application.
  -D|--disable-kernel-annotations       Disable collection of kernel annotations
  -k|--exclude-kernel (yes|no)          Specify whether kernel events should be
                                        filtered out of perf results.
  -S|--system-wide (yes|no)             Specify whether to capture the whole
                                        system. In daemon mode, 'no' is only
                                        applicable when --allow-command is
                                        specified, but a command must be entered
                                        in the Capture and Analysis Options of
                                        Streamline.
                                        (Defaults to 'yes' unless --app, --pid
                                        or--wait-process is specified).
  -u|--call-stack-unwinding (yes|no)    Enable or disable call stack unwinding
                                        (defaults to 'yes')
  -r|--sample-rate (none|low|normal|high)
                                        Specify sample rate for capture. The
                                        frequencies for each sample rate are:
                                        high=10kHz, normal=1kHz (2kHz in GPU),
                                        low=100Hz.
                                        Setting the sample rate to none will
                                        sample at the lowest possible rate.
                                        (defaults to 'normal')
  -t|--max-duration <s>                 Specify the maximum duration the capture
                                        may run for in seconds or 0 for
                                        unlimited (defaults to '0')
  -f|--use-efficient-ftrace (yes|no)    Enable efficient ftrace data collection
                                        mode (defaults to 'yes')
  -w|--app-cwd <path>                   Specify the working directory for the
                                        application launched by gatord (defaults
                                        to current directory)
  -x|--stop-on-exit (yes|no)            Stop capture when launched application
                                        exits (defaults to 'no' unless --app,
                                        --pid or --wait-process is specified).
  -Q|--wait-process <command>           Wait for a process matching the
                                        specified command to launch before
                                        starting capture. Attach to the
                                        specified process and profile it.
  -Z|--mmap-pages <n>                   The maximum number of pages to map per
                                        mmap'ed perf buffer is equal to <n+1>.
                                        Must be a power of 2.
  -O|--disable-cpu-onlining (yes|no)    Disables turning CPUs temporarily online
                                        to read their information. This option
                                        is useful for kernels that fail to
                                        handle this correctly (e.g., they
                                        reboot) (defaults to 'no').
  -F|--spe-sample-rate <n>              Specify the SPE periodic sampling rate.
                                        The rate, <n> is the number of
                                        operations between each sample, and must
                                        be a non-zero positive integer. The rate
                                        is subject to certain minimum rate
                                        specified by the hardware its self.
                                        Values below this threshold are ignored
                                        and the hardware minimum is used
                                        instead.
  -L|--capture-log                      Enable to generate a log file for
                                        the capture in the capture's directory,
                                        as well as sending the logs to 'stderr'.
  --smmuv3-model <model_id>|<iidr>      Specify the SMMUv3 model.
                                        The user can specify the model ID
                                        string directly (e.g., mmu-600) or
                                        the hex value representation for the
                                        model's IIDR number  either
                                        fully (e.g., 4832243b) or
                                        partially (e.g., 483_43b).
  -Y|--off-cpu-time (yes|no)            Collect Off-CPU time statistics.
                                        Detailed statistics require 'root' permission.
  -I|--inherit (yes|no|poll|experimental)
                                        When profiling an application, gatord
                                        monitors all threads and child processes.
                                        Specify 'no' to monitor only the initial
                                        thread of the application. Specify 'poll' to
                                        periodically poll for new processes/threads.
                                        Specify "experimental" if you have applied
                                        the kernel patches provided by Arm for
                                        top-down profiling.
                                        NB: Per-function metrics are only supported in
                                        system-wide mode, or when '--inherit' is set to
                                        'no', 'poll' or 'experimental'. The default
                                        is 'yes'.
  -N|--num-pmu-counters <n>             Override the number of programmable PMU
                                        counters that are available.
                                        This option reduces the number of programmable
                                        PMU counters available for profiling.
                                        Use this option when the default is
                                        incorrect, or because some programmable
                                        counters are unavailable because they are
                                        consumed by the OS, or other processes, or by
                                        a hypervisor.
                                        NB: The Arm PMU typically exposes 6
                                        programmable counters, and one fixed function
                                        cycle counter. This argument assumes the fixed
                                        cycle counter is not part of the reduced set
                                        of counters. If your target exposes 2
                                        programmable counters and the fixed cycle
                                        counter, then pass '2' for the value
                                        of '<n>'. However, if your target exposes 2
                                        programmable counters and no fixed cycle
                                        counter, then pass '1' for the value
                                        of '<n>'.
  -g|--gpu-timeline (yes|no|auto)       Controls GPU timeline data collection.
                                        'yes' enables collection and produces
                                        an error if the MaliTimeline_Perfetto
                                        counter is not enabled. 'no' disables
                                        collection. 'auto', the default,
                                        collects data if the counter is enabled
                                        but otherwise disables collection
                                        without error. Note: Timeline data is
                                        provided by a layer driver loaded into
                                        your application.

* Arguments available only on Android targets:

  -l|--android-pkg <pkg>                Profiles the specified android package.
                                        Waits for the package app to launch
                                        before starting a capture unless
                                        --android-activity is specified.
  -m|--android-activity <activity>      Launch the specified activity of a
                                        package and profile its process. You
                                        must also specify --android-pkg.
  -n|--activity-args <arguments>        Launch the package and activity
                                        with the supplied activity manager (am)
                                        arguments.
                                        Must be used with --android-pkg and
                                        --android-activity
                                        Arguments should be supplied as a single string.

* Arguments available in daemon mode only:

  -p|--port <port_number>|uds           Port upon which the server listens;
                                        default is 8080.
                                        If the argument given here is 'uds' then
                                        the TCP socket will be disabled and an
                                        abstract unix domain socket will be
                                        created named 'streamline-data'. This is
                                        useful for Android users where gatord is
                                        prevented from creating an TCP server
                                        socket. Instead the user can use:

                     adb forward tcp:<local_port> localabstract:streamline-data

                                        and connect to localhost:<local_port>
                                        in Streamline.
  -a|--allow-command                    Allow the user to issue a command from
                                        Streamline

* Arguments available to local capture mode only:

  -s|--session-xml <session_xml>        Take configuration from specified
                                        session.xml file. Any additional
                                        arguments will override values
                                        specified in this file.
  -o|--output <apc_dir>                 The path and name of the output for
                                        a local capture.
                                        If used with android options (-m, -l),
                                        apc will be created inside the android
                                        package. Eg if -o /data/local/tmp/test.apc,
                                        apc will be at /data/data/<pkg>/test.apc
                                        and copied to -o path
                                        after capture finished.
  -i|--pid <pids...>                    Comma separated list of process IDs to
                                        profile
  -C|--counters <counters>              A comma separated list of counters or
                                        metrics to enable. This option may be
                                        specified multiple times.  The name
                                        'workflow_topdown_basic' is used to
                                        enable metrics related to topdown
                                        analysis.  The name 'workflow_all'
                                        is used to enable all available
                                        metrics.
  -M|--metric-group <metrics>           A comma separated list of
                                        metric groups to enable. This option may
                                        be specified multiple times.
  -W|--workflow <selected-workflow>     Specify an automated workflow methodology.
                                        Workflows can be viewed by using the
                                        appropriate print command,
                                        `--print workflows`.
                                        Workflows available vary depending on
                                        the device capabilities.
  -X|--spe <id>[:events=<indexes>][:ops=<types>][:min_latency=<lat>][:inv]
                                        Enable Statistical Profiling Extension
                                        (SPE). Where:
                                        * <id> is the name of the SPE properties
                                          specified in the events.xml or
                                          pmus.xml file. It uniquely identifies
                                          the available events and counters for
                                          the SPE hardware.  An <id> of
                                          'workflow_spe' is treated specially to
                                          enable SPE on any capable processor.
                                        * <indexes> are a comma separated list
                                          of event indexes to filter the
                                          sampling by, a sample will only be
                                          recorded if all events are present.
                                        * <types> are a comma separated list
                                          of operation types to filter the
                                          sampling by, a sample will be recorded
                                          if it is any of the types in <types>.
                                          Valid types are LD for load, ST for
                                          store and B for branch.
                                        * <lat> is the minimum latency, a sample
                                          will only be recorded if its latency
                                          is greater than or equal to this
                                          value. The valid range is [0,4096).
                                        * :inv include this flag if you would like to
                                          invert the SPE event filter. This value is
                                          ignored if the device does not support SPE 1.2
                                          By default this is disabled.
)";
/*                                                                              ^ */
/*                                                                              | */
/* ------------------------------------ last character before new line here ----+ */
