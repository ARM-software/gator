/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#include "GatorMain.h"

#include "Configuration.h"
#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "GatorCLIFlags.h"
#include "GatorCLIParser.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "ParserResult.h"
#include "ProductVersion.h"
#include "SessionData.h"
#include "android/AndroidActivityManager.h"
#include "capture/CaptureProcess.h"
#include "capture/Environment.h"
#include "lib/Error.h"
#include "lib/Format.h"
#include "lib/Process.h"
#include "lib/String.h"
#include "lib/Syscall.h"
#include "linux/Tracepoints.h"
#include "logging/configuration.h"
#include "logging/file_log_sink.h"
#include "logging/global_log.h"
#include "logging/std_log_sink.h"
#include "logging/suppliers.h"
#include "xml/EventsXML.h"
#include "xml/EventsXMLHelpers.h"
#include "xml/PmuXMLParser.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <ios>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Drivers.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
    std::array<int, 2> signalPipe;

    // Signal Handler
    void handler(int signum)
    {
        if (::write(signalPipe[1], &signum, sizeof(signum)) != sizeof(signum)) {
            handleException();
        }
    }

    // Gator ready messages
    constexpr std::string_view gator_shell_ready = "Gator ready";
    constexpr unsigned int VERSION_STRING_CHAR_SIZE = 256;

    [[nodiscard]] std::string_view get_cntn_prefix(std::string const & id)
    {
        auto const last_uscore = id.rfind('_');

        if ((last_uscore == std::string::npos) || (last_uscore >= id.length())) {
            return {};
        }

        auto const remaining = id.length() - (last_uscore + 1);

        if (remaining < 4) {
            return {};
        }

        if ((id[last_uscore + 1] != 'c') || (id[last_uscore + 2] != 'n') || (id[last_uscore + 3] != 't')) {
            return {};
        }

        for (std::size_t n = last_uscore + 4; n < id.length(); ++n) {
            if ((id[n] < '0') || (id[n] > '9')) {
                return {};
            }
        }

        return std::string_view(id).substr(0, last_uscore);
    }

    [[nodiscard]] std::string_view get_id_prefix(std::string_view id)
    {
        auto const first_uscore = id.find('_');

        if ((first_uscore == std::string::npos) || (first_uscore >= id.length())) {
            return id;
        }

        return id.substr(0, first_uscore);
    }

    struct category_order_t {
        bool operator()(events_xml::EventCategory const * lhs, events_xml::EventCategory const * rhs) const
        {
            if (lhs == rhs) {
                return false;
            }
            if (lhs == nullptr) {
                return true;
            }
            if (rhs == nullptr) {
                return false;
            }

            // sort CPU counters before other groups
            if (lhs->cluster != nullptr) {
                if (rhs->cluster != nullptr) {
                    if (lhs->cluster->getCoreName() < rhs->cluster->getCoreName()) {
                        return true;
                    }
                    if (lhs->cluster->getCoreName() > rhs->cluster->getCoreName()) {
                        return false;
                    }
                }
                else {
                    return true;
                }
            }
            else if (rhs->cluster != nullptr) {
                return false;
            }

            // sort metrics next
            if (lhs->contains_metrics && !rhs->contains_metrics) {
                return true;
            }
            if (!lhs->contains_metrics && rhs->contains_metrics) {
                return false;
            }

            // sort uncores after other groups
            if (lhs->uncore != nullptr) {
                if (rhs->uncore != nullptr) {
                    if (lhs->uncore->getCoreName() < rhs->uncore->getCoreName()) {
                        return true;
                    }
                    if (lhs->uncore->getCoreName() > rhs->uncore->getCoreName()) {
                        return false;
                    }
                }
                else {
                    return false;
                }
            }
            else if (rhs->uncore != nullptr) {
                return true;
            }

            // sort by name
            return lhs->name < rhs->name;
        }
    };

    struct event_order_t {
        using value_type = std::pair<std::string_view, events_xml::EventDescriptor const *>;
        bool operator()(value_type const & lhs, value_type const & rhs) const
        {
            auto const & [l_id, l_ev] = lhs;
            auto const & [r_id, r_ev] = rhs;

            if (l_ev == r_ev) {
                return l_id < r_id;
            }
            if (l_ev == nullptr) {
                return true;
            }
            if (r_ev == nullptr) {
                return false;
            }

            if (l_ev->title < r_ev->title) {
                return true;
            }
            if (l_ev->title > r_ev->title) {
                return false;
            }

            auto const l_pref = get_id_prefix(l_ev->id);
            auto const r_pref = get_id_prefix(r_ev->id);

            if (l_pref < r_pref) {
                return true;
            }
            if (l_pref > r_pref) {
                return false;
            }

            if (l_ev->name < r_ev->name) {
                return true;
            }
            if (l_ev->name > r_ev->name) {
                return false;
            }

            return l_id < r_id;
        }
    };

    struct raw_ids_t {
        std::set<std::string> counter_ids;
        std::map<std::string, std::set<std::string>> pmu_counter_ids;
        std::set<std::string> spe_ids;
        std::size_t longest_id = 0;
    };

    [[nodiscard]] raw_ids_t collect_counterids_from_drivers(Drivers const & drivers)
    {
        raw_ids_t result {};

        for (const Driver * driver : drivers.getAllConst()) {
            (void) driver->writeCounters([&result](Driver::counter_type_t type, std::string const & name) {
                switch (type) {
                    case Driver::counter_type_t::counter: {
                        auto const pmu_prefix = get_cntn_prefix(name);
                        if (!pmu_prefix.empty()) {
                            result.pmu_counter_ids[std::string(pmu_prefix)].insert(name);
                        }
                        else {
                            result.counter_ids.insert(name);

                            result.longest_id = std::max(result.longest_id, name.size());
                        }
                        break;
                    }
                    case Driver::counter_type_t::spe: {
                        result.spe_ids.insert(name);
                        break;
                    }
                    default: {
                        throw std::runtime_error("Unexpected counter_type_t");
                    }
                }
            });
        }

        return result;
    }

    struct mapped_ids_t {
        std::map<events_xml::EventCategory const *,
                 std::set<std::pair<std::string_view, events_xml::EventDescriptor const *>, event_order_t>,
                 category_order_t>
            category_events;
    };

    [[nodiscard]] mapped_ids_t map_counter_ids_to_descriptions(raw_ids_t const & raw_ids,
                                                               events_xml::EventsContents const & all_events_categories)
    {
        mapped_ids_t result {};

        // map PMU counters to categories
        for (auto const & [cset, ids] : raw_ids.pmu_counter_ids) {
            auto const * category = find_category_for_cset(all_events_categories, cset);
            if (category == nullptr) {
                continue;
            }

            auto & id_set = result.category_events[category];

            for (auto const & id : ids) {
                id_set.insert(std::make_pair(std::string_view(id), nullptr));
            }
        }

        // map freestanding counters to categories
        for (auto const & id : raw_ids.counter_ids) {
            auto const it = all_events_categories.named_events.find(id);

            if (it == all_events_categories.named_events.end()) {
                continue;
            }

            auto const * descriptor = &(it->second.get());
            auto const * category = &(descriptor->category.get());

            result.category_events[category].insert(std::make_pair(std::string_view(id), descriptor));
        }

        return result;
    }

    void print_counters(raw_ids_t const & raw_ids, mapped_ids_t const & mapped_categories, bool descriptions)
    {
        if (!mapped_categories.category_events.empty()) {
            std::cout << "The following counters are available (for use with -C):\n\n";

            for (auto const & [category_ptr, ids] : mapped_categories.category_events) {
                if (category_ptr->cluster != nullptr) {
                    std::string_view const cn = category_ptr->cluster->getCoreName();
                    if ((cn == category_ptr->name) || (cn == "Other")) {
                        std::cout << "  * CPU Performance counters for " << category_ptr->name << ":\n\n";
                    }
                    else {
                        std::cout << "  * CPU Performance counters for " << category_ptr->cluster->getCoreName() << " ("
                                  << category_ptr->name << "):\n\n";
                    }
                }
                else if (category_ptr->uncore != nullptr) {
                    std::string_view const cn = category_ptr->uncore->getCoreName();
                    if ((cn == category_ptr->name) || (cn == "Other")) {
                        std::cout << "  * Uncore Performance counters for " << category_ptr->name << ":\n\n";
                    }
                    else {
                        std::cout << "  * Uncore Performance counters for " << category_ptr->uncore->getCoreName()
                                  << " (" << category_ptr->name << "):\n\n";
                    }
                }
                else {
                    std::cout << "  * Category " << category_ptr->name << ":\n\n";
                }

                bool log_event_codes = false;
                bool log_named_events = false;

                //
                // Print all the named counters (ones with unique IDs) first, alongside their descriptions
                //
                {
                    std::string_view last_prefix {};
                    std::string_view last_title {};

                    for (auto const & [id, descriptor_ptr] : ids) {
                        if (descriptor_ptr != nullptr) {
                            log_named_events = true;

                            // insert a new line between each new unique title/prefix to aid readability
                            auto const new_prefix = get_id_prefix(id);
                            if ((!last_title.empty() && (last_title != descriptor_ptr->title))
                                || (!last_prefix.empty() && (last_prefix != new_prefix))) {
                                std::cout << "\n";
                            }
                            last_title = descriptor_ptr->title;
                            last_prefix = new_prefix;

                            // output the event id and its details
                            std::cout << "      * " << std::setfill(' ') << std::left << std::setw(raw_ids.longest_id)
                                      << id << std::setw(0) << " - " << descriptor_ptr->title << ": "
                                      << descriptor_ptr->name;

                            if (descriptions && !descriptor_ptr->description.empty()) {
                                std::cout << " - " << descriptor_ptr->description;
                            }

                            if (descriptor_ptr->uses_option_set) {
                                std::cout << " (Additional event modifiers may be specified.)";
                            }

                            std::cout << "\n";
                        }
                    }
                }

                // insert a newline between the named events and the programable events
                if (log_named_events) {
                    std::cout << "\n";
                }

                //
                // output the programmable events (where they require an event code to be specified)
                //
                for (auto const & [id, descriptor_ptr] : ids) {
                    if (descriptor_ptr == nullptr) {
                        log_event_codes = true;

                        std::cout << "      * " << id << ":<0x##>\n";
                    }
                }

                //
                // finally output the event codes and their details
                //
                if (log_event_codes) {
                    // insert a new line between the programmable events and the codes
                    std::cout << "\n";

                    // print each event
                    for (auto const & event_ptr : category_ptr->events) {
                        if (!event_ptr->eventCode.isValid() || !event_ptr->id.empty()) {
                            continue;
                        }

                        std::cout << "          * 0x" << std::hex << std::setfill('0') << std::setw(4) << std::right
                                  << event_ptr->eventCode.asU64() << std::setw(0) << std::dec << ": "
                                  << event_ptr->title << ": " << event_ptr->name;

                        if (descriptions && !event_ptr->description.empty()) {
                            std::cout << " - " << event_ptr->description;
                        }

                        if (event_ptr->uses_option_set) {
                            std::cout << " (Additional event modifiers may be specified.)";
                        }

                        std::cout << "\n";
                    }

                    // and ensure there is a new line at the end of the category
                    std::cout << "\n";
                }
            }
        }
    }

    void print_spes(raw_ids_t const & raw_ids)
    {
        if (!raw_ids.spe_ids.empty()) {
            std::cout << "The following SPE PMUs are available (for use with -X):\n\n";

            for (auto const & id : raw_ids.spe_ids) {
                std::cout << "    " << id << "\n";
            }

            std::cout << "\n";
        }
    }

    void dumpCountersForUser(Drivers const & drivers, bool descriptions)
    {
        // collect all the counter IDs
        auto const raw_ids = collect_counterids_from_drivers(drivers);

        // get all the possible defined events
        auto const all_events_categories =
            events_xml::getEventDescriptors(drivers.getAllConst(),
                                            drivers.getPrimarySourceProvider().getCpuInfo().getClusters(),
                                            drivers.getPrimarySourceProvider().getDetectedUncorePmus());

        // map to categories
        auto const mapped_categories = map_counter_ids_to_descriptions(raw_ids, all_events_categories);

        // output the SPEs
        print_spes(raw_ids);

        // output the counters
        print_counters(raw_ids, mapped_categories, descriptions);

        std::cout << std::flush;
    }
}

void setDefaults()
{
    //default system wide.
    gSessionData.mCaptureOperationMode = CaptureOperationMode::application_default;
    // buffer_mode is normal
    gSessionData.mOneShot = false;
    gSessionData.mTotalBufferSize = 4;
    gSessionData.mPerfMmapSizeInPages = -1;
    //callStack unwinding default is yes
    gSessionData.mBacktraceDepth = 128; // NOLINT(readability-magic-numbers)
    //sample rate is normal
    gSessionData.mSampleRate = normal;
    gSessionData.mSampleRateGpu = normal_x2;
    //duration default to 0
    gSessionData.mDuration = 0;
    //use_efficient_ftrace default is yes
    gSessionData.mFtraceRaw = true;
    gSessionData.mOverrideNoPmuSlots = -1;
#if defined(WIN32)
    //TODO
    gSessionData.mCaptureUser = nullptr;
    gSessionData.mCaptureWorkingDir = nullptr;
#else
    // default to current user
    gSessionData.mCaptureUser = nullptr;

    // use current working directory
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        gSessionData.mCaptureWorkingDir = strdup(cwd);
    }
    else {
        gSessionData.mCaptureWorkingDir = nullptr;
    }
#endif
}

void updateSessionData(const ParserResult & result)
{
    gSessionData.mLocalCapture = result.mode == ParserResult::ExecutionMode::LOCAL_CAPTURE;
    gSessionData.mConfigurationXMLPath = result.mConfigurationXMLPath;
    gSessionData.mEventsXMLAppend = result.mEventsXMLAppend;
    gSessionData.mEventsXMLPath = result.mEventsXMLPath;
    gSessionData.mSessionXMLPath = result.mSessionXMLPath;
    gSessionData.mCaptureOperationMode = result.mCaptureOperationMode;
    gSessionData.mExcludeKernelEvents = result.mExcludeKernelEvents;
    gSessionData.mWaitForProcessCommand = result.mWaitForCommand;
    gSessionData.mPids = result.mPids;
    gSessionData.mLogToFile = result.mLogToFile;

    if (result.mTargetPath != nullptr) {
        if (gSessionData.mTargetPath != nullptr) {
            free(const_cast<char *>(gSessionData.mTargetPath));
        }
        gSessionData.mTargetPath = strdup(result.mTargetPath);
    }

    gSessionData.mAllowCommands = result.mAllowCommands;
    gSessionData.parameterSetFlag = result.parameterSetFlag;
    gSessionData.mStopOnExit = result.mStopGator;
    gSessionData.mPerfMmapSizeInPages = result.mPerfMmapSizeInPages;
    gSessionData.mSpeSampleRate = result.mSpeSampleRate;
    gSessionData.mAndroidPackage = result.mAndroidPackage;
    gSessionData.mAndroidActivity = result.mAndroidActivity;
    gSessionData.mAndroidActivityFlags = (result.mAndroidActivityFlags == nullptr) ? "" : result.mAndroidActivityFlags;
    gSessionData.smmu_identifiers = result.smmu_identifiers;
    gSessionData.mOverrideNoPmuSlots = result.mOverrideNoPmuSlots;

    // when profiling an android package, use the package name as the '--wait-process' value
    if ((gSessionData.mAndroidPackage != nullptr) && (gSessionData.mWaitForProcessCommand == nullptr)) {
        gSessionData.mWaitForProcessCommand = gSessionData.mAndroidPackage;
    }

    //These values are set from command line and are alos part of session.xml
    //and hence cannot be modified during parse session
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_SAMPLE_RATE) != 0) {
        gSessionData.mSampleRate = result.mSampleRate;
        gSessionData.mSampleRateGpu = result.mSampleRateGpu;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_CALL_STACK_UNWINDING) != 0) {
        gSessionData.mBacktraceDepth = result.mBacktraceDepth;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_WORKING_DIR) != 0) {
        gSessionData.mCaptureWorkingDir = strdup(result.mCaptureWorkingDir);
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_COMMAND) != 0) {
        gSessionData.mCaptureCommand = result.mCaptureCommand;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_DURATION) != 0) {
        gSessionData.mDuration = result.mDuration;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_FTRACE_RAW) != 0) {
        gSessionData.mFtraceRaw = result.mFtraceRaw;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_OFF_CPU_PROFILING) != 0) {
        gSessionData.mEnableOffCpuSampling = result.mEnableOffCpuSampling;
    }
}

void dumpCounterDetails(const ParserResult & result,
                        const logging::log_access_ops_t & log_ops,
                        const std::string & header)
{
    Drivers drivers {result.mCaptureOperationMode,
                     readPmuXml(result.pmuPath),
                     result.mDisableCpuOnlining,
                     result.mDisableKernelAnnotations,
                     TraceFsConstants::detect()};

    for (auto printable : result.printables) {
        switch (printable) {
            case ParserResult::Printable::EVENTS_XML: {
                std::cout << events_xml::getDynamicXML(drivers.getAllConst(),
                                                       drivers.getPrimarySourceProvider().getCpuInfo().getClusters(),
                                                       drivers.getPrimarySourceProvider().getDetectedUncorePmus())
                                 .get();
                break;
            }
            case ParserResult::Printable::COUNTERS_XML: {
                std::cout << counters_xml::getXML(drivers.getPrimarySourceProvider().supportsMultiEbs(),
                                                  drivers.getAllConst(),
                                                  drivers.getPrimarySourceProvider().getCpuInfo(),
                                                  log_ops)
                                 .get();
                break;
            }
            case ParserResult::Printable::DEFAULT_CONFIGURATION_XML: {
                std::cout << configuration_xml::getDefaultConfigurationXml(
                                 drivers.getPrimarySourceProvider().getCpuInfo().getClusters())
                                 .get();
                break;
            }
            case ParserResult::Printable::COUNTERS: {
                std::cout << header;
                dumpCountersForUser(drivers, false);
                break;
            }
            case ParserResult::Printable::COUNTERS_DETAILED: {
                std::cout << header;
                dumpCountersForUser(drivers, true);
                break;
            }
            default: {
                break;
            }
        }
    }
}

int start_capture_process(const ParserResult & result, logging::log_access_ops_t & log_ops)
{
    // Call before setting up the SIGCHLD handler, as system() spawns child processes
    Drivers drivers {result.mCaptureOperationMode,
                     readPmuXml(result.pmuPath),
                     result.mDisableCpuOnlining,
                     result.mDisableKernelAnnotations,
                     TraceFsConstants::detect()};

    const bool system_wide = isCaptureOperationModeSystemWide(gSessionData.mCaptureOperationMode);
    if (gSessionData.mLocalCapture && system_wide && !drivers.getFtraceDriver().isSupported()) {
        LOG_ERROR("System-wide capture requested, but tracefs is not available.%s",
                  geteuid() == 0 ? "" : " You may need to run as root.");
        handleException();
    }

    // Handle child exit codes
    if (signal(SIGCHLD, handler) == SIG_ERR) {
        LOG_ERROR("Error setting SIGCHLD signal handler");
    }

    class local_event_handler_t : public capture::capture_process_event_listener_t {
    public:
        local_event_handler_t()
        {
            if (gSessionData.mAndroidPackage != nullptr && gSessionData.mAndroidActivity != nullptr) {
                activity_manager = create_android_activity_manager(gSessionData.mAndroidPackage,
                                                                   gSessionData.mAndroidActivity,
                                                                   gSessionData.mAndroidActivityFlags);
            }
        }

        ~local_event_handler_t() override
        {
            if (activity_manager) {
                static_cast<void>(activity_manager->stop());
            }
        }

        void process_initialised() override
        {
            // When streamline is listening, this line has to be printed so it can detect when
            // gator is ready to listen and accept socket connections via adb forwarding.  Without this
            // print out there is a chance that Streamline establishes a connection to the adb forwarder,
            // but the forwarder cannot establish a connection to a gator, because gator is not up and listening
            // for sockets yet.  If the adb forwarder cannot establish a connection to gator, what streamline
            // experiences is a successful socket connection, but when it attempts to read from the socket
            // it reads an empty line when attempting to read the gator protocol header, and terminates the
            // connection.

            if (!gSessionData.mLocalCapture) {
                std::cout << gator_shell_ready.data() << std::endl; // NOLINT(performance-avoid-endl)
            }
        }

        [[nodiscard]] bool waiting_for_target() override
        {
            if (!activity_manager) {
                return true;
            }

            LOG_DEBUG("Starting the target application now...");
            return activity_manager->start();
        }

    private:
        std::unique_ptr<IAndroidActivityManager> activity_manager;

    } event_handler {};

    // we're starting gator in legacy mode - run the loop as normal
    return capture::beginCaptureProcess(result, drivers, signalPipe, log_ops, event_handler);
}

// Gator data flow: collector -> collector fifo -> sender
int gator_main(int argc, char ** argv)
{
    // Set up global thread-safe logging
    auto global_logging = std::make_shared<logging::global_logger_t>();
    global_logging->add_sink<logging::std_log_sink_t>();
    logging::set_logger(global_logging);

    // and enable debug mode
    global_logging->set_debug_enabled(GatorCLIParser::hasDebugFlag(argc, argv));

    // enable fine level logging mode
    global_logging->set_fine_enabled(GatorCLIParser::hasCaptureLogFlag(argc, argv));

    // write the log to a file, if requested
    if (GatorCLIParser::hasCaptureLogFlag(argc, argv)) {
        try {
            global_logging->add_sink<logging::file_log_sink_t>();
        }
        catch (std::ios_base::failure & e) {
            LOG_ERROR("Log setup error: %s", e.what());
            handleException();
        }
    }

    gSessionData.initialize();
    //setting default values of gSessionData
    setDefaults();

    const int pipeResult = lib::pipe2(signalPipe, O_CLOEXEC);
    if (pipeResult == -1) {
        LOG_ERROR("pipe failed (%d) %s", errno, lib::strerror());
        handleException();
    }

    (void) signal(SIGINT, handler);
    (void) signal(SIGTERM, handler);
    (void) signal(SIGABRT, handler);
    (void) signal(SIGHUP, handler);
    (void) signal(SIGUSR1, handler);
    gator::process::set_parent_death_signal(SIGKILL);

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-main"), 0, 0, 0);

    lib::printf_str_t<VERSION_STRING_CHAR_SIZE> versionString;

    const int baseProductVersion =
        (PRODUCT_VERSION >= 0 ? PRODUCT_VERSION : -(PRODUCT_VERSION % PRODUCT_VERSION_DEV_MULTIPLIER));
    const int protocolDevTag = (PRODUCT_VERSION >= 0 ? 0 : -(PRODUCT_VERSION / PRODUCT_VERSION_DEV_MULTIPLIER));
    const int majorVersion = baseProductVersion / 100;
    const int minorVersion = (baseProductVersion / 10) % 10;
    const int revisionVersion = baseProductVersion % 10;
    const char * formatString =
        (PRODUCT_VERSION >= 0 ? (revisionVersion == 0 ? "Streamline gatord version %d (Streamline v%d.%d)"
                                                      : "Streamline gatord version %d (Streamline v%d.%d.%d)")
                              : "Streamline gatord development version %d (Streamline v%d.%d.%d), "
                                "tag %d");

    versionString.printf(formatString, PRODUCT_VERSION, majorVersion, minorVersion, revisionVersion, protocolDevTag);

    std::string_view const branchName {PRODUCT_VERSION_BRANCH_NAME};
    auto const useBranchName = (!branchName.empty()) && (branchName != "main");

    // Parse the command line parameters
    GatorCLIParser parser;
    parser.parseCLIArguments(argc, argv, versionString, gSrcMd5, gBuildId);
    const ParserResult & result = parser.result;

    lib::Format headerFmt;
    headerFmt << "Streamline Data Recorder v" << majorVersion << '.' << minorVersion << '.' << revisionVersion
              << " (Build " << gBuildId;
    if (useBranchName) {
        headerFmt << " [" << branchName << "]";
    }
    headerFmt << ")\n"
              << "Copyright (c) 2010-" << gCopyrightYear << " Arm Limited. All rights reserved.\n\n";
    const std::string header {headerFmt};

    if (result.mode != ParserResult::ExecutionMode::PRINT) {
        std::cout << header;
    }

    if (!result.error_messages.empty()) {
        for (const auto & message : parser.result.error_messages) {
            LOG_WARNING("%s", message.c_str());
        }
    }

    if (result.mode == ParserResult::ExecutionMode::USAGE) {
        std::cout << GatorCLIParser::USAGE_MESSAGE;
        return 0;
    }

    if (result.mode == ParserResult::ExecutionMode::EXIT) {
        handleException();
    }

    updateSessionData(result);

    // configure any environment settings we'll need to start sampling
    // e.g. perf security settings.
    auto environment = capture::prepareCaptureEnvironment();
    environment->postInit(gSessionData);

    if (result.mode == ParserResult::ExecutionMode::PRINT) {
        dumpCounterDetails(result, *global_logging, header);
    }
    else {
        return start_capture_process(result, *global_logging);
    }

    return 0;
}
