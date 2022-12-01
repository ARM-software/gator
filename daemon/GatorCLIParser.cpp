/* Copyright (C) 2014-2022 by Arm Limited. All rights reserved. */

#include "GatorCLIParser.h"

#include "Config.h"
#include "GatorCLIFlags.h"
#include "Logging.h"
#include "lib/String.h"
#include "lib/Utils.h"
#include "linux/smmu_identifier.h"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace {
    constexpr int DECIMAL_BASE = 10;
    constexpr int HEX_BASE = 16;
    constexpr int MIN_LATENCY = 4096;
    constexpr int MAX_EVENT_BIT_POSITION = 63;
    constexpr int GATOR_ANNOTATION_PORT1 = 8082;
    constexpr int GATOR_ANNOTATION_PORT2 = 8083;
    constexpr int GATOR_MAX_VALUE_PORT = 65535;

    constexpr std::string_view OPTSTRING_SHORT = "ac:d::e:f:hi:k:l:m:o:p:r:s:t:u:vw:x:A:C:DE:F:N:O:P:Q:R:S:TVX:Z:";

    const struct option OPTSTRING_LONG[] = { // PLEASE KEEP THIS LIST IN ALPHANUMERIC ORDER TO ALLOW EASY SELECTION
                                             // OF NEW ITEMS.
        {"allow-command", /**********/ no_argument, /***/ nullptr, 'a'}, //
        {"config-xml", /*************/ required_argument, nullptr, 'c'}, //
        {"debug", /******************/ no_argument, /***/ nullptr, 'd'}, //
        {"events-xml", /*************/ required_argument, nullptr, 'e'}, //
        {"use-efficient-ftrace", /***/ required_argument, nullptr, 'f'}, //
        {"help", /*******************/ no_argument, /***/ nullptr, 'h'}, //
        {"pid", /********************/ required_argument, nullptr, 'i'}, //
        {"exclude-kernel", /*********/ required_argument, nullptr, 'k'}, //
        ANDROID_PACKAGE,                                                 //
        ANDROID_ACTIVITY,                                                //
        {"output", /*****************/ required_argument, nullptr, 'o'}, //
        {"port", /*******************/ required_argument, nullptr, 'p'}, //
        {"sample-rate", /************/ required_argument, nullptr, 'r'}, //
        {"session-xml", /************/ required_argument, nullptr, 's'}, //
        {"max-duration", /***********/ required_argument, nullptr, 't'}, //
        {"call-stack-unwinding", /***/ required_argument, nullptr, 'u'}, //
        {"version", /****************/ required_argument, nullptr, 'v'}, //
        {"app-cwd", /****************/ required_argument, nullptr, 'w'}, //
        {"stop-on-exit", /***********/ required_argument, nullptr, 'x'}, //
        {"smmuv3-model", /***********/ required_argument, nullptr, 'z'}, //
        APP,                                                             //
        {"counters", /***************/ required_argument, nullptr, 'C'}, //
        {"disable-kernel-annotations", no_argument, /***/ nullptr, 'D'}, //
        {"append-events-xml", /******/ required_argument, nullptr, 'E'}, //
        {"spe-sample-rate", /********/ required_argument, nullptr, 'F'}, //
        /********************************************************* 'N' ***/
        {"disable-cpu-onlining", /***/ required_argument, nullptr, 'O'}, //
        {"pmus-xml", /***************/ required_argument, nullptr, 'P'}, //
        WAIT_PROCESS,                                                    //
        {"print", /******************/ required_argument, nullptr, 'R'}, //
        {"system-wide", /************/ required_argument, nullptr, 'S'}, //
        {"trace", /******************/ no_argument, /***/ nullptr, 'T'}, //
        {"version", /****************/ no_argument, /***/ nullptr, 'V'}, //
        {"spe", /********************/ required_argument, nullptr, 'X'}, //
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
}

using ExecutionMode = ParserResult::ExecutionMode;

SampleRate getSampleRate(const std::string & value)
{
    if (value == "high") {
        return high;
    }
    if (value == "normal") {
        return normal;
    }
    if (value == "low") {
        return low;
    }
    if (value == "none") {

        return none;
    }
    return invalid;
}

void GatorCLIParser::addCounter(int startpos, int pos, std::string & counters)
{
    std::string counterType;
    std::string subStr = counters.substr(startpos, pos - startpos);
    EventCode event;
    size_t eventpos = 0;

    //TODO : support for A53:Cycles:1:2:8:0x1
    if ((eventpos = subStr.find(':')) != std::string::npos) {
        auto eventStr = subStr.substr(eventpos + 1, subStr.size());
        long long eventCode;
        if (!stringToLongLong(&eventCode, eventStr.c_str(), DECIMAL_BASE)) { //check for decimal
            if (!stringToLongLong(&eventCode, eventStr.c_str(), HEX_BASE)) { //check for hex
                LOG_ERROR("event must be an integer");
                result.parsingFailed();
                return;
            }
        }
        event = EventCode(eventCode);
    }
    if (eventpos == std::string::npos) {
        counterType = subStr;
    }
    else {
        counterType = subStr.substr(0, eventpos);
    }

    auto it = result.events.begin();

    while (it != result.events.end()) {
        if (strcasecmp(it->first.c_str(), counterType.c_str()) == 0) {
            LOG_ERROR("Counter already added. %s ", counterType.c_str());
            result.parsingFailed();
            return;
        }
        ++it;
    }
    result.events.insert(std::make_pair(counterType, event));
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
                                    start == std::string::npos ? std::optional<std::string>(command)
                                                               : std::optional<std::string>(command.substr(start))});
            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_CAPTURE_COMMAND;
        }
    }
    return found;
}

/**
 * @return 1 for a true-like string, 0 for a false-like string and -1 otherwise
 */
static int parseBoolean(const char * value)
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

// trim
static void trim(std::string & data)
{
    //trim from  left
    data.erase(data.begin(), std::find_if(data.begin(), data.end(), [](int ch) { return std::isspace(ch) == 0; }));
    //trim from right
    data.erase(std::find_if(data.rbegin(), data.rend(), [](int ch) { return std::isspace(ch) == 0; }).base(),
               data.end());
}

static void split(const std::string & data, char delimiter, std::vector<std::string> & tokens)
{
    std::string token;
    std::istringstream dataStream(data);
    while (std::getline(dataStream, token, delimiter)) {
        trim(token);
        tokens.push_back(token);
    }
}

void GatorCLIParser::parseAndUpdateSpe()
{
    std::vector<std::string> spe_data;
    split(std::string(optarg), SPE_DATA_DELIMITER, spe_data);
    if (!spe_data.empty()) {
        //add details to structure
        if (!spe_data[0].empty()) { //check if cpu id provided
            SpeConfiguration data;
            data.id = spe_data[0];
            spe_data.erase(spe_data.begin());
            for (const auto & spe_data_it : spe_data) {
                std::vector<std::string> spe;
                split(spe_data_it, SPE_KEY_VALUE_DELIMITER, spe);
                if (spe.size() == 2) { //should be a key value pair to add
                    if (spe[0] == SPE_MIN_LATENCY_KEY) {
                        if (!stringToInt(&(data.min_latency), spe[1].c_str(), 0)) {
                            LOG_ERROR("latency not an integer %s (%s)", data.id.c_str(), spe[1].c_str());
                            result.parsingFailed();
                            return;
                        }
                        if (data.min_latency < 0 || data.min_latency >= MIN_LATENCY) {
                            LOG_ERROR("Invalid minimum latency for %s (%d)", data.id.c_str(), data.min_latency);
                            result.parsingFailed();
                            return;
                        }
                    }
                    else if (spe[0] == SPE_EVENTS_KEY) {
                        std::vector<std::string> spe_events;
                        split(spe[1], SPES_KEY_VALUE_DELIMITER, spe_events);
                        for (const std::string & spe_event : spe_events) {
                            int event;
                            if (!stringToInt(&event, spe_event.c_str(), DECIMAL_BASE)) {
                                LOG_ERROR("Event filter cannot be a non integer , failed for %s ", spe_event.c_str());
                                result.parsingFailed();
                                return;
                            }
                            if ((event < 0 || event > MAX_EVENT_BIT_POSITION)) {
                                LOG_ERROR("Event filter should be a bit position from 0 - 63 , failed for %d ", event);
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
                                    LOG_ERROR("Not a valid Ops %s", spe_ops_it.c_str());
                                    result.parsingFailed();
                                    return;
                                }
                            }
                        }
                    }
                    else { // invalid key
                        LOG_ERROR("--spe arguments not in correct format %s ", spe_data_it.c_str());
                        result.parsingFailed();
                        return;
                    }
                }
                else {
                    LOG_ERROR("--spe arguments not in correct format %s ", spe_data_it.c_str());
                    result.parsingFailed();
                    return;
                }
            }
            result.mSpeConfigs.push_back(data);
            LOG_DEBUG("Adding spe -> %s", data.id.c_str());
        }
        else {
            LOG_ERROR("No Id provided for --spe");
            result.parsingFailed();
            return;
        }
    }
}

void GatorCLIParser::parseCLIArguments(int argc,
                                       char * argv[],
                                       const char * version_string,
                                       int maxPerformanceCounter,
                                       const char * gSrcMd5,
                                       const char * gBuildId)
{
    LOG_ERROR("%s", version_string);
    const int indexApp = findAndUpdateCmndLineCmnd(argc, argv);
    if (indexApp > 0) {
        argc = indexApp;
    }
    bool systemWideSet = false;
    optind = 1;
    opterr = 1;
    int c;
    while ((c = getopt_long(argc, argv, OPTSTRING_SHORT.data(), OPTSTRING_LONG, nullptr)) != -1) {
        const int optionInt = optarg == nullptr ? -1 : parseBoolean(optarg);
        SampleRate sampleRate;
        std::string value;
        result.addArgValuePair(
            {std::string(1, char(c)), optarg == nullptr ? std::nullopt : std::optional<std::string>(optarg)});
        switch (c) {
            case 'N':
                if (!stringToInt(&result.mAndroidApiLevel, optarg, 10)) {
                    LOG_ERROR("-N must be followed by an int");
                    result.parsingFailed();
                    return;
                }
                break;
            case 'c':
                result.mConfigurationXMLPath = optarg;
                break;
            case 'd':
                // Already handled
                break;
            case 'e': //event xml path
                result.mEventsXMLPath = optarg;
                break;
            case 'E': // events xml path for append
                result.mEventsXMLAppend = optarg;
                break;
            case 'P':
                result.pmuPath = optarg;
                break;
            case 'p': //port
                if (strcasecmp(optarg, "uds") == 0) {
                    result.port = DISABLE_TCP_USE_UDS_PORT;
                }
                else {
                    if (!stringToInt(&result.port, optarg, DECIMAL_BASE)) {
                        LOG_ERROR("Port must be an integer");
                        result.parsingFailed();
                        return;
                    }
                    if ((result.port == GATOR_ANNOTATION_PORT1) || (result.port == GATOR_ANNOTATION_PORT2)) {
                        LOG_ERROR("Gator can't use port %i, as it already uses ports 8082 and 8083 for "
                                  "annotations. Please select a different port.",
                                  result.port);
                        result.parsingFailed();
                        return;
                    }
                    if (result.port < 1 || result.port > GATOR_MAX_VALUE_PORT) {
                        LOG_ERROR(
                            "Gator can't use port %i, as it is not valid. Please pick a value between 1 and 65535",
                            result.port);
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
                    LOG_ERROR("Invalid value for --call-stack-unwinding (%s), 'yes' or 'no' expected.", optarg);
                    result.parsingFailed();
                    return;
                }
                result.mBacktraceDepth = optionInt == 1 ? 128 : 0;
                break;
            case 'r': //sample-rate
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_SAMPLE_RATE;
                value = std::string(optarg);
                sampleRate = getSampleRate(value);
                if (sampleRate != invalid) {
                    result.mSampleRate = sampleRate;
                }
                else {
                    LOG_ERROR("Invalid sample rate (%s).", optarg);
                    result.parsingFailed();
                    return;
                }
                break;
            case 't': //max-duration
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_DURATION;

                if (!stringToInt(&result.mDuration, optarg, 10)) {
                    LOG_ERROR("Invalid max duration (%s).", optarg);
                    result.parsingFailed();
                    return;
                }
                break;
            case 'f': //use-efficient-ftrace
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_FTRACE_RAW;
                if (optionInt < 0) {
                    LOG_ERROR("Invalid value for --use-efficient-ftrace (%s), 'yes' or 'no' expected.", optarg);
                    result.parsingFailed();
                    return;
                }
                result.mFtraceRaw = optionInt == 1;
                break;
            case 'S': //--system-wide
                if (optionInt < 0) {
                    LOG_ERROR("Invalid value for --system-wide (%s), 'yes' or 'no' expected.", optarg);
                    result.parsingFailed();
                    return;
                }
                result.mSystemWide = optionInt == 1;
                systemWideSet = true;
                break;
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
                    LOG_ERROR("Invalid value for --stop-on-exit (%s), 'yes' or 'no' expected.", optarg);
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
            case 'C': //counter
            {
                int startpos = -1;
                size_t counterSplitPos = 0;
                if (perfCounterCount > maxPerformanceCounter) {
                    continue;
                }
                value = std::string(optarg);

                while ((counterSplitPos = value.find(',', startpos + 1)) != std::string::npos) {
                    addCounter(startpos + 1, counterSplitPos, value);
                    startpos = counterSplitPos;
                }
                //adding last counter in list
                addCounter(startpos + 1, value.length(), value);
                break;
            }
            case 'D': // disable kernel annotations
                result.mDisableKernelAnnotations = true;
                break;
            case 'X': // spe
            {
                parseAndUpdateSpe();
                if (result.mode == ExecutionMode::EXIT) {
                    return;
                }
            } break;
            case 'i': // pid
            {
                auto const pids = lib::parseCommaSeparatedNumbers<int>(optarg);
                if (!pids) {
                    LOG_ERROR("Invalid value for --pid (%s), comma separated and numeric list expected.", optarg);
                    result.parsingFailed();
                    return;
                }

                result.mPids.insert(pids->begin(), pids->end());
                break;
            }
            case 'h':
            case '?':
            default:
                LOG_ERROR(
                    /* ------------------------------------ last character before new line here ----+ */
                    /*                                                                              | */
                    /*                                                                              v */
                    "\n"
                    "Streamline has 2 modes of operation. Daemon mode (the default), and local\n"
                    "capture mode, which will capture to disk and then exit. To enable local capture\n"
                    "mode specify an output directory with --output.\n"
                    "\n"
                    "* Arguments available to all modes:\n"
                    "  -h|--help                             This help page\n"
                    "  -c|--config-xml <config_xml>          Specify path and filename of the\n"
                    "                                        configuration XML. In daemon mode the\n"
                    "                                        list of counters will be written to\n"
                    "                                        this file. In local capture mode the\n"
                    "                                        list of counters will be read from this\n"
                    "                                        file.\n"
                    "  -D|--disable-kernel-annotations       Disable collection of kernel annotations\n"
                    "  -e|--events-xml <events_xml>          Specify path and filename of the events\n"
                    "                                        XML to use\n"
                    "  -E|--append-events-xml <events_xml>   Specify path and filename of events XML\n"
                    "                                        to append\n"
                    "  -P|--pmus-xml <pmu_xml>               Specify path and filename of pmu XML to\n"
                    "                                        append\n"
                    "  -v|--version                          Print version information\n"
                    "  -d|--debug                            Enable debug messages\n"
                    "  -A|--app <cmd> <args...>              Specify the command to execute once the\n"
                    "                                        capture starts. Must be the last\n"
                    "                                        argument passed to gatord as all\n"
                    "                                        subsequent arguments are passed to the\n"
                    "                                        launched application.\n"
                    "  -k|--exclude-kernel (yes|no)          Specify whether kernel events should be\n"
                    "                                        filtered out of perf results.\n"
                    "  -S|--system-wide (yes|no)             Specify whether to capture the whole\n"
                    "                                        system. In daemon mode, 'no' is only\n"
                    "                                        applicable when --allow-command is\n"
                    "                                        specified, but a command must be entered\n"
                    "                                        in the Capture and Analysis Options of\n"
                    "                                        Streamline. Requires kernel events to \n"
                    "                                        be enabled in Capture and Analysis\n"
                    "                                        Options of Streamline, or by setting\n"
                    "                                        '--exclude-kernel no'.\n"
                    "                                        (Defaults to 'yes' unless --app, --pid\n"
                    "                                        or--wait-process is specified).\n"
                    "  -u|--call-stack-unwinding (yes|no)    Enable or disable call stack unwinding\n"
                    "                                        (defaults to 'yes')\n"
                    "  -r|--sample-rate (none|low|normal|high)\n"
                    "                                        Specify sample rate for capture. The\n"
                    "                                        frequencies for each sample rate are: \n"
                    "                                        high=10kHz, normal=1kHz, low=100Hz.\n"
                    "                                        Setting the sample rate to none will\n"
                    "                                        sample at the lowest possible rate.\n"
                    "                                        (defaults to 'normal')\n"
                    "  -t|--max-duration <s>                 Specify the maximum duration the capture\n"
                    "                                        may run for in seconds or 0 for\n"
                    "                                        unlimited (defaults to '0')\n"
                    "  -f|--use-efficient-ftrace (yes|no)    Enable efficient ftrace data collection\n"
                    "                                        mode (defaults to 'yes')\n"
                    "  -w|--app-cwd <path>                   Specify the working directory for the\n"
                    "                                        application launched by gatord (defaults\n"
                    "                                        to current directory)\n"
                    "  -x|--stop-on-exit (yes|no)            Stop capture when launched application\n"
                    "                                        exits (defaults to 'no' unless --app,\n"
                    "                                        --pid or --wait-process is specified).\n"
                    "  -Q|--wait-process <command>           Wait for a process matching the\n"
                    "                                        specified command to launch before\n"
                    "                                        starting capture. Attach to the\n"
                    "                                        specified process and profile it.\n"
                    "  -Z|--mmap-pages <n>                   The maximum number of pages to map per\n"
                    "                                        mmap'ed perf buffer is equal to <n+1>.\n"
                    "                                        Must be a power of 2.\n"
                    "  -O|--disable-cpu-onlining (yes|no)    Disables turning CPUs temporarily online\n"
                    "                                        to read their information. This option\n"
                    "                                        is useful for kernels that fail to\n"
                    "                                        handle this correctly (e.g., they\n"
                    "                                        reboot) (defaults to 'no').\n"
                    "  -F|--spe-sample-rate <n>              Specify the SPE periodic sampling rate.\n"
                    "                                        The rate, <n> is the number of \n"
                    "                                        operations between each sample, and must\n"
                    "                                        be a non-zero positive integer. The rate\n"
                    "                                        is subject to certain minimum rate\n"
                    "                                        specified by the hardware its self.\n"
                    "                                        Values below this threshold are ignored\n"
                    "                                        and the hardware minimum is used\n"
                    "                                        instead.\n"
                    "  --smmuv3-model <model_id>|<iidr>      Specify the SMMUv3 model.\n"
                    "                                        The user can specify the model ID\n"
                    "                                        string directly (e.g., mmu-600) or\n"
                    "                                        the hex value representation for the\n"
                    "                                        model's IIDR number  either\n"
                    "                                        fully (e.g., 4832243b) or\n"
                    "                                        partially (e.g., 483_43b).\n"
                    "\n"
                    "* Arguments available only on Android targets:\n"
                    "\n"
                    "  -l|--android-pkg <pkg>                Profiles the specified android package.\n"
                    "                                        Waits for the package app to launch\n"
                    "                                        before starting a capture unless\n"
                    "                                        --android-activity is specified.\n"
                    "  -m|--android-activity <activity>      Launch the specified activity of a\n"
                    "                                        package and profile its process. You\n"
                    "                                        must also specify --android-pkg.\n"
                    "\n"
                    "* Arguments available in daemon mode only:\n"
                    "\n"
                    "  -p|--port <port_number>|uds           Port upon which the server listens;\n"
                    "                                        default is 8080.\n"
                    "                                        If the argument given here is 'uds' then\n"
                    "                                        the TCP socket will be disabled and an \n"
                    "                                        abstract unix domain socket will be\n"
                    "                                        created named 'streamline-data'. This is\n"
                    "                                        useful for Android users where gatord is\n"
                    "                                        prevented from creating an TCP server\n"
                    "                                        socket. Instead the user can use:\n"
                    "\n"
                    "                     adb forward tcp:<local_port> localabstract:streamline-data\n"
                    "\n"
                    "                                        and connect to localhost:<local_port>\n"
                    "                                        in Streamline.\n"
                    "  -a|--allow-command                    Allow the user to issue a command from\n"
                    "                                        Streamline\n"
                    "\n"
                    "* Arguments available to local capture mode only:\n"
                    "\n"
                    "  -s|--session-xml <session_xml>        Take configuration from specified\n"
                    "                                        session.xml file. Any additional\n"
                    "                                        arguments will override values\n"
                    "                                        specified in this file.\n"
                    "  -o|--output <apc_dir>                 The path and name of the output for\n"
                    "                                        a local capture.\n"
                    "                                        If used with android options (-m, -l),\n"
                    "                                        apc will be created inside the android\n"
                    "                                        package. Eg if -o /data/local/tmp/test.apc,\n"
                    "                                        apc will be at /data/data/<pkg>/test.apc\n"
                    "                                        and copied to -o path \n"
                    "                                        after capture finished.\n"
                    "  -i|--pid <pids...>                    Comma separated list of process IDs to\n"
                    "                                        profile\n"
                    "  -C|--counters <counters>              A comma separated list of counters to\n"
                    "                                        enable. This option may be specified\n"
                    "                                        multiple times.\n"
                    "  -X|--spe <id>[:events=<indexes>][:ops=<types>][:min_latency=<lat>]\n"
                    "                                        Enable Statistical Profiling Extension\n"
                    "                                        (SPE). Where:\n"
                    "                                        * <id> is the name of the SPE properties\n"
                    "                                          specified in the events.xml or \n"
                    "                                          pmus.xml file. It uniquely identifies\n"
                    "                                          the available events and counters for\n"
                    "                                          the SPE hardware.\n"
                    "                                        * <indexes> are a comma separated list\n"
                    "                                          of event indexes to filter the\n"
                    "                                          sampling by, a sample will only be\n"
                    "                                          recorded if all events are present.\n"
                    "                                        * <types> are a comma separated list\n"
                    "                                          of operation types to filter the\n"
                    "                                          sampling by, a sample will be recorded\n"
                    "                                          if it is any of the types in <types>.\n"
                    "                                          Valid types are LD for load, ST for\n"
                    "                                          store and B for branch.\n"
                    "                                        * <lat> is the minimum latency, a sample\n"
                    "                                          will only be recorded if its latency \n"
                    "                                          is greater than or equal to this \n"
                    "                                          value. The valid range is [0,4096).\n"
                    /*                                                                              ^ */
                    /*                                                                              | */
                    /* ------------------------------------ last character before new line here ----+ */
                );
                result.parsingFailed();
                return;
            case 'v': // version is already printed/logged at the start of this function
                result.parsingFailed();
                return;
            case 'V':
                LOG_ERROR("%s\nSRC_MD5: %s\nBUILD_ID: %s", version_string, gSrcMd5, gBuildId);
                result.parsingFailed();
                return;
            case 'O':
                if (optionInt < 0) {
                    LOG_ERROR("Invalid value for --disable-cpu-onlining (%s), 'yes' or 'no' expected.", optarg);
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
                if (!stringToInt(&result.mPerfMmapSizeInPages, optarg, 0)) {
                    LOG_ERROR("Invalid value for --mmap-pages (%s): not an integer", optarg);
                    result.parsingFailed();
                    result.mPerfMmapSizeInPages = -1;
                }
                else if (result.mPerfMmapSizeInPages < 1) {
                    LOG_ERROR("Invalid value for --mmap-pages (%s): not more than 0", optarg);
                    result.parsingFailed();
                    result.mPerfMmapSizeInPages = -1;
                }
                //FIXME
                //NOLINTNEXTLINE(hicpp-signed-bitwise)
                else if (((result.mPerfMmapSizeInPages - 1) & result.mPerfMmapSizeInPages) != 0) {
                    LOG_ERROR("Invalid value for --mmap-pages (%s): not a power of 2", optarg);
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
                    else {
                        LOG_ERROR("Invalid value for --print (%s)", optarg);
                        result.parsingFailed();
                        return;
                    }
                }
                break;
            }
            case 'F': {
                result.mSpeSampleRate = -1;
                if (!stringToInt(&result.mSpeSampleRate, optarg, 0)) {
                    LOG_ERROR("Invalid value for --spe-sample-rate (%s): not an integer", optarg);
                    result.parsingFailed();
                    result.mSpeSampleRate = -1;
                }
                else if ((result.mSpeSampleRate < 1) || (result.mSpeSampleRate > 1000000000)) {
                    LOG_WARNING("Invalid value for --spe-sample-rate (%s): default value will be used", optarg);
                    result.mSpeSampleRate = -1;
                }
                break;
            }
            case 'k': {
                if (optionInt < 0) {
                    LOG_ERROR("Invalid value for --exclude-kernel (%s), 'yes' or 'no' expected.", optarg);
                    result.parsingFailed();
                    return;
                }
                result.mExcludeKernelEvents = optionInt == 1;
                result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_EXCLUDE_KERNEL;
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
            case 'T': {
                logging::set_log_enable_trace(true);
                break;
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

    if (!systemWideSet) {
#if CONFIG_PREFER_SYSTEM_WIDE_MODE
        // default to system-wide mode unless a process option was specified
        result.mSystemWide = !haveProcess;
#else
        // user must explicitly request system-wide mode
        result.mSystemWide = false;
#endif
    }

    // Error checking
    if (result.mSystemWide && result.mExcludeKernelEvents) {
        LOG_ERROR("Kernel events are currently required for system-wide mode. "
                  "Either restart gatord with '--exclude-kernel no' or specify a process to profile.");
        result.parsingFailed();
        return;
    }

    if (result.mode == ExecutionMode::LOCAL_CAPTURE) {
        if (result.mAllowCommands) {
            LOG_ERROR("--allow-command is not applicable in local capture mode.");
            result.parsingFailed();
            return;
        }
        if (result.port != DEFAULT_PORT) {
            LOG_ERROR("--port is not applicable in local capture mode");
            result.parsingFailed();
            return;
        }

        if (!result.mSystemWide && result.mSessionXMLPath == nullptr && !haveProcess) {
            LOG_ERROR("In local capture mode, without --system-wide=yes, a process to profile must be specified "
                      "with --session-xml, --app, --wait-process, --pid, or --android-pkg.");
            result.parsingFailed();
            return;
        }

        if (result.events.empty() && (result.mConfigurationXMLPath == nullptr)) {
            LOG_WARNING("No counters (--counters) specified, default counters will be used");
        }
    }
    else if (result.mode == ExecutionMode::DAEMON) {
        if (!result.mSystemWide && !result.mAllowCommands && !haveProcess) {
            LOG_ERROR("In daemon mode, without --system-wide=yes, a process to profile must be specified with "
                      "--allow-command, --app, --wait-process, --pid, or --android-pkg.");
            result.parsingFailed();
            return;
        }
        if (result.mSessionXMLPath != nullptr) {
            LOG_ERROR("--session-xml is not applicable in daemon mode.");
            result.parsingFailed();
            return;
        }
        if (!result.events.empty()) {
            LOG_ERROR("--counters is not applicable in daemon mode.");
            result.parsingFailed();
            return;
        }
    }

    if ((result.mAndroidActivity != nullptr) && (result.mAndroidPackage == nullptr)) {
        LOG_ERROR("--android-pkg must be specified when supplying --android-activity.");
        result.parsingFailed();
        return;
    }

    const bool hasAnotherProcessArg = !result.mCaptureCommand.empty() || !result.mPids.empty()
                                   || result.mWaitForCommand != nullptr || result.mAllowCommands;
    if ((result.mAndroidPackage != nullptr) && hasAnotherProcessArg) {
        LOG_ERROR("--android-pkg is not compatible with --allow-command, --app, --wait-process, or --pid.");
        result.parsingFailed();
        return;
    }

#if !defined(__ANDROID__)
    if (result.mAndroidPackage != nullptr) {
        //__ANDROID__ will not be defined in case of static linking with musl, logging this only as a warning.
        LOG_WARNING("--android-pkg will only work on Android OS.");
    }
#endif

    if (result.mAndroidPackage != nullptr && !lib::isRootOrShell()) {
        LOG_ERROR("--android-pkg requires to be run from a shell or root user.");
        result.parsingFailed();
        return;
    }

    if (result.mDuration < 0) {
        LOG_ERROR("Capture duration cannot be a negative value : %d ", result.mDuration);
        result.parsingFailed();
        return;
    }

    if (indexApp > 0 && result.mCaptureCommand.empty()) {
        LOG_ERROR("--app requires a command to be specified");
        result.parsingFailed();
        return;
    }

    if ((indexApp > 0) && (result.mWaitForCommand != nullptr)) {
        LOG_ERROR("--app and --wait-process are mutually exclusive");
        result.parsingFailed();
        return;
    }
    if (indexApp > 0 && result.mAllowCommands) {
        LOG_ERROR("Cannot allow command (--allow-command) from Streamline, if --app is specified.");
        result.parsingFailed();
        return;
    }
    // Error checking
    if (optind < argc) {
        LOG_ERROR("Unknown argument: %s. Use --help to list valid arguments.", argv[optind]);
        result.parsingFailed();
        return;
    }
}

static int findIndexOfArg(const std::string & arg_toCheck, int argc, const char * const argv[])
{
    for (int j = 1; j < argc; j++) {
        std::string arg(argv[j]);
        if (arg == arg_toCheck) {
            return j;
        }
    }
    return -1;
}

bool GatorCLIParser::hasDebugFlag(int argc, const char * const argv[])
{
    //had to use this instead of opt api, when -A or --app is made an optional argument opt api
    //permute the contents of argv while scanning it so that eventually all the non-options are at the end.
    //when --app or -A is given as an optional argument. and has option as ls -lrt
    //-lrt get treated as another option for gatord
    //TODO: needs investigation
    constexpr std::string_view debugArgShort = "-d";
    constexpr std::string_view debugArgLong = "--debug";
    constexpr std::string_view traceArgShort = "-T";
    constexpr std::string_view traceArgLong = "--trace";
    for (int j = 1; j < argc; j++) {
        std::string_view arg(argv[j]);
        if ((arg == debugArgShort) || (arg == debugArgLong) || (arg == traceArgShort) || (arg == traceArgLong)) {
            int appIndex = findIndexOfArg("--app", argc, argv);
            if (appIndex == -1) {
                appIndex = findIndexOfArg("-A", argc, argv);
            }
            return !(appIndex > -1 && j > appIndex);
        }
    }
    return false;
}
