/**
 * Copyright (C) Arm Limited 2014-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "GatorCLIParser.h"
#include "Config.h"

#include "lib/Istream.h"
#include <sstream>
#include <algorithm>

static const char OPTSTRING_SHORT[] = "ahvVS:d::p:s:c:e:E:P:m:N:u:r:t:f:x:o:w:C:A:i:Q:D:M:Z:X:R:";
static const struct option OPTSTRING_LONG[] = { //
{ "call-stack-unwinding", /**/required_argument, NULL, 'u' }, //
{ "sample-rate", /***********/required_argument, NULL, 'r' }, //
{ "max-duration", /**********/required_argument, NULL, 't' }, //
{ "use-efficient-ftrace", /**/required_argument, NULL, 'f' }, //
{ "system-wide", /***********/required_argument, NULL, 'S' }, //
{ "stop-on-exit", /**********/required_argument, NULL, 'x' }, //
{ "output", /****************/required_argument, NULL, 'o' }, //
{ "app-cwd", /***************/required_argument, NULL, 'w' }, //
{ "app", /*******************/required_argument, NULL, 'A' }, //
{ "counters", /**************/required_argument, NULL, 'C' }, //
{ "help", /******************/no_argument, /****/NULL, 'h' }, //
{ "events-xml", /************/required_argument, NULL, 'e' }, //
{ "append-events-xml", /*****/required_argument, NULL, 'E' }, //
{ "pmus-xml", /**************/required_argument, NULL, 'P' }, //
{ "module-path", /***********/required_argument, NULL, 'm' }, //
{ "version", /***************/required_argument, NULL, 'v' }, //
{ "debug", /*****************/no_argument, /****/NULL, 'd' }, //
{ "allow-command", /*********/no_argument, /****/NULL, 'a' }, //
{ "port", /******************/required_argument, NULL, 'p' }, //
{ "session-xml", /***********/required_argument, NULL, 's' }, //
{ "config-xml", /************/required_argument, NULL, 'c' }, //
{ "pid", /*******************/required_argument, NULL, 'i' }, //
{ "wait-process", /**********/required_argument, NULL, 'Q' }, //
{ "mali-device", /***********/required_argument, NULL, 'D' }, //
{ "mali-type", /*************/required_argument, NULL, 'M' }, //
{ "mmap-pages", /************/required_argument, NULL, 'Z' }, //
{ "spe", /*******************/required_argument, NULL, 'X' }, //
{ "print", /*****************/required_argument, NULL, 'R' }, //
{ NULL, 0, NULL, 0 } };

static const char PRINTABLE_SEPARATOR = ',';

using ExecutionMode = ParserResult::ExecutionMode;

static const char* LOAD_OPS   = "LD";
static const char* STORE_OPS  = "ST";
static const char* BRANCH_OPS = "B";
// SPE
static const char SPES_KEY_VALUE_DELIMITER = ',';
static const char SPE_DATA_DELIMITER = ':';
static const char SPE_KEY_VALUE_DELIMITER = '=';
static const char* SPE_MIN_LATENCY_KEY = "min_latency";
static const char* SPE_EVENTS_KEY = "events";
static const char* SPE_OPS_KEY = "ops";

ParserResult::ParserResult()
        : mSpeConfigs(),
          mCaptureWorkingDir(),
          mCaptureCommand(),
          mPids(),
          mSessionXMLPath(),
          mTargetPath(),
          mConfigurationXMLPath(),
          mEventsXMLPath(),
          mEventsXMLAppend(),
          mWaitForCommand(),
          mMaliDevices(),
          mMaliTypes(),
          mBacktraceDepth(),
          mSampleRate(),
          mDuration(),
          mAndroidApiLevel(),
          mPerfMmapSizeInPages(-1),
          mFtraceRaw(),
          mStopGator(false),
          mSystemWide(true),
          mAllowCommands(false),
          module(nullptr),
          pmuPath(nullptr),
          port(DEFAULT_PORT),
          parameterSetFlag(0),
          events(),
          mode(ExecutionMode::DAEMON),
          printables()
{

}

ParserResult::~ParserResult()
{
}

GatorCLIParser::GatorCLIParser()
        : result(),
          perfCounterCount(0)
{
}

GatorCLIParser::~GatorCLIParser()
{
}

SampleRate getSampleRate(std::string value)
{
    if (value.compare("high") == 0) {
        return high;
    }
    else if (value.compare("normal") == 0) {
        return normal;
    }
    else if (value.compare("low") == 0) {
        return low;
    }
    else if (value.compare("none") == 0) {

        return none;
    }
    return invalid;
}

void GatorCLIParser::addCounter(int startpos, int pos, std::string &counters)
{
    std::string counterType = "";
    std::string subStr = counters.substr(startpos, pos);
    int event = -1;
    size_t eventpos = 0;

    //TODO : support for A53:Cycles:1:2:8:0x1
    if ((eventpos = subStr.find(":")) != std::string::npos) {
        if (!stringToInt(&event, subStr.substr(eventpos + 1, subStr.size()).c_str(), 10)) { //check for decimal
            if (!stringToInt(&event, subStr.substr(eventpos + 1, subStr.size()).c_str(), 16)) { //check for hex
                logg.logError("event must be an integer");
                result.mode = ExecutionMode::EXIT;
                return;
            }
        }
    }
    if (eventpos == std::string::npos) {
        counterType = subStr;
    }
    else {
        counterType = subStr.substr(0, eventpos).c_str();
    }

    std::map<std::string, int>::iterator it = result.events.begin();

    while (it != result.events.end()) {
        if (strcasecmp(it->first.c_str(), counterType.c_str()) == 0) {
            logg.logError("Counter already added. %s ", counterType.c_str());
            result.mode = ExecutionMode::EXIT;
            return;
        }
        ++it;
    }
    result.events.insert(std::make_pair(counterType, event));
}

int GatorCLIParser::findAndUpdateCmndLineCmnd(int argc, char** argv)
{
    result.mCaptureCommand.clear();
    int found = 0;
    std::string shortAppArg("-A");
    std::string longAppArg("--app");
    for (int j = 1; j < argc; j++) {
        std::string arg(argv[j]);
        if (arg.compare(shortAppArg) == 0 || arg.compare(longAppArg) == 0) {
            found = j;
            break;
        }
    }
    if (found > 0) {
        if (argc > found) {
            if (argc > found + 1) {
                for (int i = found + 1; i < argc; i++) {
                    result.mCaptureCommand.push_back(argv[i]);
                }
            }

            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_CAPTURE_COMMAND;
        }
    }
    return found;
}

/**
 *
 * @param value
 * @return 1 for a true-like string, 0 for a false-like string and -1 otherwise
 */
static int parseBoolean(const char * value)
{
    if (strcasecmp(value, "yes") == 0 //
    || strcasecmp(value, "y") == 0 //
    || strcasecmp(value, "true") == 0 //
    || strcmp(value, "1") == 0)
        return 1;
    else if (strcasecmp(value, "no") == 0 //
    || strcasecmp(value, "n") == 0 //
    || strcasecmp(value, "false") == 0 //
    || strcmp(value, "0") == 0)
        return 0;
    else
        return -1;
}


// trim
static void trim(std::string &data)
{
    //trim from  left
    data.erase(data.begin(), std::find_if(data.begin(), data.end(), [](int ch) {
        return !std::isspace(ch);
    }));
    //trim from right
    data.erase(std::find_if(data.rbegin(), data.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), data.end());
}


static void split(const std::string& data, char delimiter, std::vector<std::string> &tokens)
{
   std::string token;
   std::istringstream dataStream(data);
   while (std::getline(dataStream, token, delimiter))
   {
        trim(token);
        tokens.push_back(token);
    }
}

void GatorCLIParser::parseAndUpdateSpe() {
    std::vector<std::string> spe_data;
    split(std::string(optarg), SPE_DATA_DELIMITER, spe_data);
    if (!spe_data.empty()) {
        //add details to structure
        if (!spe_data[0].empty()) { //check if cpu id provided
            SpeConfiguration data;
            data.id = spe_data[0];
            spe_data.erase(spe_data.begin());
            for (const auto& spe_data_it : spe_data) {
                std::vector<std::string> spe;
                split(spe_data_it, SPE_KEY_VALUE_DELIMITER, spe);
                if (spe.size() == 2) { //should be a key value pair to add
                    if (spe[0].compare(SPE_MIN_LATENCY_KEY) == 0) {
                        if (!stringToInt(&(data.min_latency), spe[1].c_str(), 0)) {
                            logg.logError("latency not an integer %s (%s)", data.id.c_str(), spe[1].c_str());
                            result.mode = ExecutionMode::EXIT;
                            return;
                        }
                        else if (data.min_latency < 0 || data.min_latency >= 4096) {
                            logg.logError("Invalid minimum latency for %s (%d)", data.id.c_str(), data.min_latency);
                            result.mode = ExecutionMode::EXIT;
                            return;
                        }
                    }
                    else if (spe[0].compare(SPE_EVENTS_KEY) == 0) {
                        std::vector<std::string> spe_events;
                        split(spe[1], SPES_KEY_VALUE_DELIMITER, spe_events);
                        for (std::string spe_event : spe_events) {
                            int event;
                            if (!stringToInt(&event, spe_event.c_str(), 10)) {
                                logg.logError("Event filter cannot be a non integer , failed for %s ", spe_event.c_str());
                                result.mode = ExecutionMode::EXIT;
                                return;
                            } else if((event < 0 || event > 63)) {
                                logg.logError("Event filter should be a bit position from 0 - 63 , failed for %d ", event);
                                result.mode = ExecutionMode::EXIT;
                                return;
                            }
                            data.event_filter_mask = data.event_filter_mask | 1 << event;
                        }
                    }
                    else if (spe[0].compare(SPE_OPS_KEY) == 0) {
                        std::vector<std::string> spe_ops;
                        split(spe[1], SPES_KEY_VALUE_DELIMITER, spe_ops);
                        if (!spe_ops.empty()) {
                            data.ops.clear();
                            //convert to enum
                            for (const auto& spe_ops_it : spe_ops) {
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
                                    logg.logError("Not a valid Ops %s", spe_ops_it.c_str());
                                    result.mode = ExecutionMode::EXIT;
                                    return;
                                }
                            }
                        }
                    } else { // invalid key
                        logg.logError("--spe arguments not in correct format %s ", spe_data_it.c_str());
                        result.mode = ExecutionMode::EXIT;
                        return;
                    }
                }
                else {
                    logg.logError("--spe arguments not in correct format %s ", spe_data_it.c_str());
                    result.mode = ExecutionMode::EXIT;
                    return;
                }
            }
            result.mSpeConfigs.push_back(data);
            logg.logMessage("Adding spe -> %s", data.id.c_str());
        }
        else {
            logg.logError("No Id provided for --spe");
            result.mode = ExecutionMode::EXIT;
            return;
        }
    }
}

void GatorCLIParser::parseCLIArguments(int argc, char* argv[], const char* version_string, int maxPerformanceCounter,
                                       const char* gSrcMd5)
{
    logg.logError("%s", version_string);
    const int indexApp = findAndUpdateCmndLineCmnd(argc, argv);
    if (indexApp > 0) {
        argc = indexApp;
    }
    bool systemWideSet = false;
    optind = 1;
    opterr = 1;
    int c;
    while ((c = getopt_long(argc, argv, OPTSTRING_SHORT, OPTSTRING_LONG, NULL)) != -1) {
        const int optionInt = optarg == nullptr ? -1 : parseBoolean(optarg);
        SampleRate sampleRate;
        std::string value = "";
        int startpos = -1;
        size_t counterSplitPos = 0;
        switch (c) {
        case 'N':
            if (!stringToInt(&result.mAndroidApiLevel, optarg, 10)) {
                logg.logError("-N must be followed by an int");
                result.mode = ExecutionMode::EXIT;
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
        case 'm':
            result.module = optarg;
            break;
        case 'p': //port
            if (strcasecmp(optarg, "uds") == 0) {
                logg.logWarning("Using unix domain socket instead of TCP connection");
                result.port = DISABLE_TCP_USE_UDS_PORT;
            }
            else {
                if (!stringToInt(&result.port, optarg, 10)) {
                    logg.logError("Port must be an integer");
                    result.mode = ExecutionMode::EXIT;
                    return;
                }
                if ((result.port == 8082) || (result.port == 8083)) {
                    logg.logError("Gator can't use port %i, as it already uses ports 8082 and 8083 for annotations. Please select a different port.", result.port);
                    result.mode = ExecutionMode::EXIT;
                    return;
                }
                if (result.port < 1 || result.port > 65535) {
                    logg.logError("Gator can't use port %i, as it is not valid. Please pick a value between 1 and 65535", result.port);
                    result.mode = ExecutionMode::EXIT;
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
                logg.logError("Invalid value for --call-stack-unwinding (%s), 'yes' or 'no' expected.", optarg);
                result.mode = ExecutionMode::EXIT;
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
                logg.logError("Invalid sample rate (%s).", optarg);
                result.mode = ExecutionMode::EXIT;
                return;
            }
            break;
        case 't': //max-duration
            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_DURATION;

            if (!stringToInt(&result.mDuration, optarg, 10)) {
                logg.logError("Invalid max duration (%s).", optarg);
                result.mode = ExecutionMode::EXIT;
                return;
            }
            break;
        case 'f': //use-efficient-ftrace
            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_FTRACE_RAW;
            if (optionInt < 0) {
                logg.logError("Invalid value for --use-efficient-ftrace (%s), 'yes' or 'no' expected.", optarg);
                result.mode = ExecutionMode::EXIT;
                return;
            }
            result.mFtraceRaw = optionInt == 1;
            break;
        case 'S': //--system-wide
            if (optionInt < 0) {
                logg.logError("Invalid value for --system-wide (%s), 'yes' or 'no' expected.", optarg);
                result.mode = ExecutionMode::EXIT;
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
                logg.logError("Invalid value for --stop-on-exit (%s), 'yes' or 'no' expected.", optarg);
                result.mode = ExecutionMode::EXIT;
                return;
            }
            result.mStopGator = optionInt == 1;
            break;
        case 'C': //counter
            if (perfCounterCount > maxPerformanceCounter) {
                continue;
            }
            value = std::string(optarg);

            while ((counterSplitPos = value.find(",", startpos + 1)) != std::string::npos) {
                addCounter(startpos + 1, counterSplitPos, value);
                startpos = counterSplitPos;
            }
            //adding last counter in list
            addCounter(startpos + 1, value.length(), value);
            break;
        case 'X': // spe
        {
            parseAndUpdateSpe();
            if(result.mode == ExecutionMode::EXIT) {
                return;
            }
        }
            break;
        case 'i': // pid
        {
            std::stringstream stream { optarg };
            std::vector<int> pids = lib::parseCommaSeparatedNumbers<int>(stream);
            if (stream.fail()) {
                logg.logError("Invalid value for --pid (%s), comma separated list expected.", optarg);
                result.mode = ExecutionMode::EXIT;
                return;
            }

            result.mPids.insert(pids.begin(), pids.end());
            break;
        }
        case 'M': // mali-type
        {
            split(std::string(optarg), PRINTABLE_SEPARATOR, result.mMaliTypes);
            break;
        }
        case 'D': // mali-device
        {
            split(std::string(optarg), PRINTABLE_SEPARATOR, result.mMaliDevices);
            break;
        }
        case 'h':
        case '?':
            logg.logError(
                    /* ------------------------------------ last character before new line here ----+ */
                    /*                                                                              | */
                    /*                                                                              v */
                    "\n"
                    "Streamline has 2 modes of operation. Daemon mode (the default), and local\n"
                    "capture mode, which will capture to disk and then exit. To enable local capture\n"
                    "mode specify an output directory with --output.\n\n"
                    "* Arguments available to all modes:\n"
                    "  -h|--help                             This help page\n"
                    "  -c|--config-xml <config_xml>          Specify path and filename of the\n"
                    "                                        configuration XML. In daemon mode the\n"
                    "                                        list of counters will be written to\n"
                    "                                        this file. In local capture mode the\n"
                    "                                        list of counters will be read from this\n"
                    "                                        file.\n"
                    "  -e|--events-xml <events_xml>          Specify path and filename of the events\n"
                    "                                        XML to use\n"
                    "  -E|--append-events-xml <events_xml>   Specify path and filename of events XML\n"
                    "                                        to append\n"
                    "  -P|--pmus-xml <pmu_xml>               Specify path and filename of pmu XML to\n"
                    "                                        append\n"
                    "  -m|--module-path <module>             Specify path and filename of gator.ko\n"
                    "  -v|--version                          Print version information\n"
                    "  -d|--debug                            Enable debug messages\n"
                    "  -A|--app <cmd> <args...>              Specify the command to execute once the\n"
                    "                                        capture starts. Must be the last\n"
                    "                                        argument passed to gatord as all\n"
                    "                                        subsequent arguments are passed to the\n"
                    "                                        launched application.\n"
                    "  -S|--system-wide (yes|no)             Specify whether to capture the whole\n"
                    "                                        system. In daemon mode, 'no' is only\n"
                    "                                        applicable when --allow-command is\n"
                    "                                        specified, but a command must be entered\n"
                    "                                        in the Capture & Analysis Options of\n"
                    "                                        Streamline. (Defaults to 'yes' unless\n"
                    "                                        --app, --pid or --wait-process is\n"
                    "                                        specified).\n"
                    "  -u|--call-stack-unwinding (yes|no)    Enable or disable call stack unwinding\n"
                    "                                        (defaults to 'yes')\n"
                    "  -r|--sample-rate <low/normal>         Specify sample rate for capture\n"
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
                    "  -M|--mali-type <types>                Specify a comma separated list defining\n"
                    "                                        the Arm Mali GPU(s) present on the\n"
                    "                                        system. Used when it is not possible to\n"
                    "                                        auto-detect the GPU(s) (for example\n"
                    "                                        because access to '/sys' is denied.\n"
                    "                                        The value specified must be the product\n"
                    "                                        name (e.g. Mali-T880, or G72), or the \n"
                    "                                        16-bit hex GPU ID value. The number\n"
                    "                                        of types specified may be one if all\n"
                    "                                        devices are the same type, otherwise it\n"
                    "                                        must be a list matching the order of \n"
                    "                                        --mali-device.\n"
                    "  -D|--mali-device <paths>              The path to the Mali GPU device node(s)\n"
                    "                                        in the '/dev' filesystem. When not\n"
                    "                                        supplied and not auto-detected, defaults\n"
                    "                                        to '/dev/mali0,/dev/mali1', and so on\n"
                    "                                        for the amount of GPUs present. Only\n"
                    "                                        valid if --mali-type is also specified.\n"
                    "  -Z|--mmap-pages <n>                   The maximum number of pages to map per\n"
                    "                                        mmap'ed perf buffer is equal to <n+1>.\n"
                    "                                        Must be a power of 2.\n"
                    "* Arguments available in daemon mode only:\n"
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
                    "* Arguments available to local capture mode only:\n"
                    "  -s|--session-xml <session_xml>        Take configuration from specified\n"
                    "                                        session.xml file. Any additional\n"
                    "                                        arguments will override values\n"
                    "                                        specified in this file.\n"
                    "  -o|--output <apc_dir>                 The path and name of the output for\n"
                    "                                        a local capture\n"
                    "  -i|--pid <pids...>                    Comma separated list of process IDs to\n"
                    "                                        profile\n"
                    "  -C|--counters <counters>              A comma separated list of counters to\n"
                    "                                        enable. This option may be specified\n"
                    "                                        multiple times.\n"
#if 0
                    "  -X|--spe <id>[:events=<indexes>][:ops=<types>][:min_latency=<lat>]\n"
                    "                                        Enable Statistical Profiling Extension\n"
                    "                                        (SPE). Where:\n"
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
                    "                                          will only be record if its latency is\n"
                    "                                          equal to or greater than this value.\n"
                    "                                          The valid range is [0,4096).\n"
#endif
            );
            result.mode = ExecutionMode::EXIT;
            return;
        case 'v': // version is already printed/logged at the start of this function
            result.mode = ExecutionMode::EXIT;
            return;
        case 'V':
            logg.logError("%s\nSRC_MD5: %s\nBUILD_ID: %s", version_string, gSrcMd5, STRIFY(GATORD_BUILD_ID));
            result.mode = ExecutionMode::EXIT;
            return;
        case 'Q':
            result.mWaitForCommand = optarg;
            break;
        case 'Z':
            result.mPerfMmapSizeInPages = -1;
            if (!stringToInt(&result.mPerfMmapSizeInPages, optarg, 0)) {
                logg.logError("Invalid value for --mmap-pages (%s): not an integer", optarg);
                result.mode = ExecutionMode::EXIT;
                result.mPerfMmapSizeInPages = -1;
            }
            else if (result.mPerfMmapSizeInPages < 1) {
                logg.logError("Invalid value for --mmap-pages (%s): not more than 0", optarg);
                result.mode = ExecutionMode::EXIT;
                result.mPerfMmapSizeInPages = -1;
            }
            else if (((result.mPerfMmapSizeInPages - 1) & result.mPerfMmapSizeInPages) != 0) {
                logg.logError("Invalid value for --mmap-pages (%s): not a power of 2", optarg);
                result.mode = ExecutionMode::EXIT;
                result.mPerfMmapSizeInPages = -1;
            }
            break;
        case 'R': {
            result.mode = ExecutionMode::PRINT;
            std::vector <std::string> parts;
            split(optarg, PRINTABLE_SEPARATOR, parts);
            for (const auto & part : parts) {
                if (strcasecmp(part.c_str(), "events.xml") == 0)
                    result.printables.insert(ParserResult::Printable::EVENTS_XML);
                else if (strcasecmp(part.c_str(), "counters.xml") == 0)
                    result.printables.insert(ParserResult::Printable::COUNTERS_XML);
                else if (strcasecmp(part.c_str(), "defaults.xml") == 0)
                    result.printables.insert(ParserResult::Printable::DEFAULT_CONFIGURATION_XML);
                else {
                    logg.logError("Invalid value for --print (%s)", optarg);
                    result.mode = ExecutionMode::EXIT;
                    return;
                }
            }
            break;
        }
        }
    }

    // Defaults depending on other flags
    const bool haveProcess = !result.mCaptureCommand.empty() || !result.mPids.empty()
            || result.mWaitForCommand != nullptr;

    if ((result.parameterSetFlag & USE_CMDLINE_ARG_STOP_GATOR) == 0) {
        result.mStopGator = haveProcess;
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
    if (result.mode == ExecutionMode::LOCAL_CAPTURE) {
        if (result.mAllowCommands) {
            logg.logError("--allow-command is not applicable in local capture mode.");
            result.mode = ExecutionMode::EXIT;
            return;
        }
        if (result.port != DEFAULT_PORT) {
            logg.logError("--port is not applicable in local capture mode");
            result.mode = ExecutionMode::EXIT;
            return;
        }

        if (!result.mSystemWide && result.mSessionXMLPath == nullptr && !haveProcess) {
            logg.logError("In local capture mode, without --system-wide=yes, a process to profile must be specified with --session-xml, --app, --wait-process or --pid.");
            result.mode = ExecutionMode::EXIT;
            return;
        }

        if (result.events.empty() && (result.mConfigurationXMLPath == nullptr)) {
            logg.logWarning("No counters (--counters) specified, default counters will be used");
        }
    }
    else if (result.mode == ExecutionMode::DAEMON) {
        if (!result.mSystemWide && !result.mAllowCommands && !haveProcess) {
            logg.logError("In daemon mode, without --system-wide=yes, a process to profile must be specified with --allow-command, --app, --wait-process or --pid.");
            result.mode = ExecutionMode::EXIT;
            return;
        }
        if (result.mSessionXMLPath != NULL) {
            logg.logError("--session-xml is not applicable in daemon mode.");
            result.mode = ExecutionMode::EXIT;
            return;
        }
        if (!result.events.empty()) {
            logg.logError("--counters is not applicable in daemon mode.");
            result.mode = ExecutionMode::EXIT;
            return;
        }
    }

    if (result.mDuration < 0) {
        logg.logError("Capture duration cannot be a negative value : %d ", result.mDuration);
        result.mode = ExecutionMode::EXIT;
        return;
    }

    if (result.mMaliTypes.size() == 0 && result.mMaliDevices.size() > 0) {
        logg.logError("--mali-device must have a corresponding --mali-type.");
        result.mode = ExecutionMode::EXIT;
        return;
    }

    if (result.mMaliTypes.size() > 1 && result.mMaliDevices.size() > 0
            && result.mMaliDevices.size() != result.mMaliTypes.size())
    {
        logg.logError("The number of --mali-device paths should equal the amount of --mali-type, given the amount of --mali-type is greater than one.");
        result.mode = ExecutionMode::EXIT;
        return;
    }

    if (indexApp > 0 && result.mCaptureCommand.empty()) {
        logg.logError("--app requires a command to be specified");
        result.mode = ExecutionMode::EXIT;
        return;
    }

    if ((indexApp > 0) && (result.mWaitForCommand != nullptr)) {
        logg.logError("--app and --wait-process are mutually exclusive");
        result.mode = ExecutionMode::EXIT;
        return;
    }
    if (indexApp > 0 && result.mAllowCommands) {
        logg.logError("Cannot allow command (--allow-command) from Streamline, if --app is specified.");
        result.mode = ExecutionMode::EXIT;
        return;
    }
    // Error checking
    if (optind < argc) {
        logg.logError("Unknown argument: %s. Use --help to list valid arguments.", argv[optind]);
        result.mode = ExecutionMode::EXIT;
        return;
    }
}

static int findIndexOfArg(std::string arg_toCheck, int argc, const char* const argv[])
{
    for (int j = 1; j < argc; j++) {
        std::string arg(argv[j]);
        if (arg.compare(arg_toCheck) == 0) {
            return j;
        }
    }
    return -1;
}

bool GatorCLIParser::hasDebugFlag(int argc, const char* const argv[])
{
    //had to use this instead of opt api, when -A or --app is made an optional argument opt api
    //permute the contents of argv while scanning it so that eventually all the non-options are at the end.
    //when --app or -A is given as an optional argument. and has option as ls -lrt
    //-lrt get treated as another option for gatord
    //TODO: needs investigation
    std::string debugArgShort = "-d";
    std::string debugArgLong = "--debug";
    for (int j = 1; j < argc; j++) {
        std::string arg(argv[j]);
        if (arg.compare(debugArgShort) == 0 || arg.compare(debugArgLong) == 0) {
            int appIndex = findIndexOfArg("--app", argc, argv);
            if (appIndex == -1) {
                appIndex = findIndexOfArg("-A", argc, argv);
            }
            if (appIndex > -1 && j > appIndex) {
                return false;
            }
            return true;
        }
    }
    return false;
}

