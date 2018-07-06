/**
 * Copyright (C) Arm Limited 2014-2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "GatorCLIParser.h"

#include "lib/Istream.h"
#include <sstream>

static const char OPTSTRING_SHORT[] = "ahvVS:d::p:s:c:e:E:P:m:N:u:r:t:f:x:o:w:C:A:i:Q:D:M:Z:";
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
{ "mmap-pages", /*************/required_argument, NULL, 'Z' }, //
{ NULL, 0, NULL, 0 } };

ParserResult::ParserResult()
        : mCaptureWorkingDir(),
          mCaptureCommand(),
          mPids(),
          mSessionXMLPath(),
          mTargetPath(),
          mConfigurationXMLPath(),
          mEventsXMLPath(),
          mEventsXMLAppend(),
          mWaitForCommand(),
          mMaliDevice(),
          mMaliType(),
          mBacktraceDepth(),
          mSampleRate(),
          mDuration(),
          mAndroidApiLevel(),
          mPerfMmapSizeInPages(-1),
          mFtraceRaw(),
          mStopGator(false),
          mSystemWide(true),
          mAllowCommands(false),
          mIsLocalCapture(false),
          module(nullptr),
          pmuPath(nullptr),
          port(DEFAULT_PORT),
          parameterSetFlag(0),
          events(),
          parse_error(0),
          parse_error_message()
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
                result.parse_error = ERROR_PARSING;
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
            result.parse_error = ERROR_PARSING;
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
                result.parse_error = ERROR_PARSING;
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
            if (!stringToInt(&result.port, optarg, 10)) {
                logg.logError("Port must be an integer");
                result.parse_error = ERROR_PARSING;
                return;
            }
            if ((result.port == 8082) || (result.port == 8083)) {
                logg.logError("Gator can't use port %i, as it already uses ports 8082 and 8083 for annotations. Please select a different port.", result.port);
                result.parse_error = ERROR_PARSING;
                return;
            }
            if (result.port < 1 || result.port > 65535) {
                logg.logError("Gator can't use port %i, as it is not valid. Please pick a value between 1 and 65535", result.port);
                result.parse_error = ERROR_PARSING;
                return;
            }
            break;
        case 's': //specify path for session xml
            result.mSessionXMLPath = optarg;
            break;
        case 'o': //output
            result.mTargetPath = optarg;
            result.mIsLocalCapture = true;
            break;
        case 'a': //app allowed needed while running in interactive mode not needed for local capture.
            result.mAllowCommands = true;
            break;
        case 'u': //-call-stack-unwinding
            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_CALL_STACK_UNWINDING;
            if (optionInt < 0) {
                logg.logError("Invalid value for --call-stack-unwinding (%s), 'yes' or 'no' expected.", optarg);
                result.parse_error = ERROR_PARSING;
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
                result.parse_error = ERROR_PARSING;
                return;
            }
            break;
        case 't': //max-duration
            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_DURATION;

            if (!stringToInt(&result.mDuration, optarg, 10)) {
                logg.logError("Invalid max duration (%s).", optarg);
                result.parse_error = ERROR_PARSING;
                return;
            }
            break;
        case 'f': //use-efficient-ftrace
            result.parameterSetFlag = result.parameterSetFlag | USE_CMDLINE_ARG_FTRACE_RAW;
            if (optionInt < 0) {
                logg.logError("Invalid value for --use-efficient-ftrace (%s), 'yes' or 'no' expected.", optarg);
                result.parse_error = ERROR_PARSING;
                return;
            }
            result.mFtraceRaw = optionInt == 1;
            break;
        case 'S': //--system-wide
            if (optionInt < 0) {
                logg.logError("Invalid value for --system-wide (%s), 'yes' or 'no' expected.", optarg);
                result.parse_error = ERROR_PARSING;
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
                result.parse_error = ERROR_PARSING;
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
        case 'i': // pid
        {
            std::stringstream stream { optarg };
            std::vector<int> pids = lib::parseCommaSeparatedNumbers<int>(stream);
            if (stream.fail()) {
                logg.logError("Invalid value for --pid (%s), comma separated list expected.", optarg);
                result.parse_error = ERROR_PARSING;
                return;
            }

            result.mPids.insert(pids.begin(), pids.end());
            break;
        }
        case 'M': // mali-type
        {
            result.mMaliType = optarg;
            break;
        }
        case 'D': // mali-device
        {
            result.mMaliDevice = optarg;
            break;
        }
        case 'h':
        case '?':
            logg.logError(
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
                    "  -M|--mali-type <type>                 Specify the Arm Mali GPU present on the\n"
                    "                                        system. Used when it is not possible to\n"
                    "                                        auto-detect the GPU (for example because\n"
                    "                                        access to '/sys' is denied.\n"
                    "                                        The value specified must be the product\n"
                    "                                        name (e.g. Mali-T880, or G72), or the\n"
                    "                                        16-bit hex GPU ID value.\n"
                    "  -D|--mali-device <path>               The path to the Mali GPU device node in\n"
                    "                                        the '/dev' filesystem. When not supplied\n"
                    "                                        and not auto-detected, defaults to\n"
                    "                                        '/dev/mali0'. Only valid if --mali-type\n"
                    "                                        is also specified.\n"
                    "  -Z|--mmap-pages <n>                   The maximum number of pages to map per\n"
                    "                                        mmap'ed perf buffer is equal to <n+1>\n"
                    "* Arguments available in daemon mode only:\n"
                    "  -p|--port <port_number>               Port upon which the server listens;\n"
                    "                                        default is 8080\n"
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
            );
            result.parse_error = ERROR_PARSING;
            return;
        case 'v': // version is already printed/logged at the start of this function
            result.parse_error = ERROR_PARSING;
            return;
        case 'V':
            logg.logError("%s\nSRC_MD5: %s", version_string, gSrcMd5);
            result.parse_error = ERROR_PARSING;
            return;
        case 'Q':
            result.mWaitForCommand = optarg;
            break;
        case 'Z':
            result.mPerfMmapSizeInPages = -1;
            if (!stringToInt(&result.mPerfMmapSizeInPages, optarg, 0)) {
                logg.logError("Invalid value for --mmap-pages (%s)", optarg);
                result.parse_error = ERROR_PARSING;
                result.mPerfMmapSizeInPages = -1;
            }
            else if (result.mPerfMmapSizeInPages < 1) {
                logg.logError("Invalid value for --mmap-pages (%s)", optarg);
                result.parse_error = ERROR_PARSING;
                result.mPerfMmapSizeInPages = -1;
            }
            break;
        }
    }

    // Defaults depending on other flags
    const bool haveProcess = !result.mCaptureCommand.empty() || !result.mPids.empty()
            || result.mWaitForCommand != nullptr;

    if ((result.parameterSetFlag & USE_CMDLINE_ARG_STOP_GATOR) == 0) {
        result.mStopGator = haveProcess;
    }

    if (!systemWideSet) {
        result.mSystemWide = !haveProcess;
    }

    // Error checking
    if (result.mIsLocalCapture) {
        if (result.mAllowCommands) {
            logg.logError("--allow-command is not applicable in local capture mode.");
            result.parse_error = ERROR_PARSING;
            return;
        }
        if (result.port != DEFAULT_PORT) {
            logg.logError("--port is not applicable in local capture mode");
            result.parse_error = ERROR_PARSING;
            return;
        }

        if (!result.mSystemWide && result.mSessionXMLPath == nullptr && !haveProcess) {
            logg.logError("In local capture mode, without --system-wide=yes, a process to profile must be specified with --session-xml, --app, --wait-process or --pid.");
            result.parse_error = ERROR_PARSING;
            return;
        }

        if (result.events.empty() && (result.mConfigurationXMLPath == nullptr)) {
            logg.logWarning("No counters (--counters) specified, default counters will be used");
        }
    }
    else { // daemon mode
        if (!result.mSystemWide && !result.mAllowCommands && !haveProcess) {
            logg.logError("In daemon mode, without --system-wide=yes, a process to profile must be specified with --allow-command, --app, --wait-process or --pid.");
            result.parse_error = ERROR_PARSING;
            return;
        }
        if (result.mSessionXMLPath != NULL) {
            logg.logError("--session-xml is not applicable in daemon mode.");
            result.parse_error = ERROR_PARSING;
            return;
        }
        if (!result.events.empty()) {
            logg.logError("--counters is not applicable in daemon mode.");
            result.parse_error = ERROR_PARSING;
            return;
        }
    }

    if (result.mDuration < 0) {
        logg.logError("Capture duration cannot be a negative value : %d ", result.mDuration);
        result.parse_error = ERROR_PARSING;
        return;
    }
    if (indexApp > 0 && result.mCaptureCommand.empty()) {
        logg.logError("--app requires a command to be specified");
        result.parse_error = ERROR_PARSING;
        return;
    }

    if ((indexApp > 0) && (result.mWaitForCommand != nullptr)) {
        logg.logError("--app and --wait-process are mutually exclusive");
        result.parse_error = ERROR_PARSING;
        return;
    }
    if (indexApp > 0 && result.mAllowCommands) {
        logg.logError("Cannot allow command (--allow-command) from Streamline, if --app is specified.");
        result.parse_error = ERROR_PARSING;
        return;
    }
    // Error checking
    if (optind < argc) {
        logg.logError("Unknown argument: %s. Use --help to list valid arguments.", argv[optind]);
        result.parse_error = ERROR_PARSING;
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

