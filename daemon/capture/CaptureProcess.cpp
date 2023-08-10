/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "capture/CaptureProcess.h"

#include "AnnotateListener.h"
#include "Child.h"
#include "Drivers.h"
#include "ExitStatus.h"
#include "GatorException.h"
#include "Logging.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "Sender.h"
#include "SessionData.h"
#include "StreamlineSetupLoop.h"
#include "agents/spawn_agent.h"
#include "android/AndroidActivityManager.h"
#include "capture/internal/UdpListener.h"
#include "lib/FileDescriptor.h"
#include "lib/Process.h"
#include "xml/CurrentConfigXML.h"
#include "xml/PmuXMLParser.h"

#include <memory>
#include <sstream>
#include <thread>

#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
    constexpr int high_priority = -19;

    enum class State {
        IDLE,
        CAPTURING,
        EXITING, /// CAPTURING but we have received a request to exit

        /**
         * Final state for gatord where the subordinate process has exited and
         * we're in the process of cleaning up before exiting the parent.
         */
        EXIT,
    };
    struct StateAndPid {
        State state;
        /**
         * PID will contain the exit code once the process has finished.
         */
        int pid;
    };

    constexpr std::array<const char, sizeof("\0streamline-data")> NO_TCP_PIPE = {"\0streamline-data"};

    Monitor monitor;
    capture::internal::UdpListener udpListener;
    std::unique_ptr<AnnotateListener> annotateListenerPtr;

    StateAndPid handleSigchld(StateAndPid currentStateAndChildPid, Drivers & drivers)
    {
        int status;
        int pid = waitpid(currentStateAndChildPid.pid, &status, WNOHANG);
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (pid < 1 || !(WIFEXITED(status) || WIFSIGNALED(status))) {
            // wasn't gator-child  or it was but just a stop/continue
            // so just ignore it
            return currentStateAndChildPid;
        }

        for (const auto & driver : drivers.getAll()) {
            driver->postChildExitInParent();
        }

        int exitStatus;
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (WIFEXITED(status)) {
            exitStatus = WEXITSTATUS(status);
            LOG_FINE("Child process %d terminated normally with status %d", pid, exitStatus);
            if (exitStatus == OK_TO_EXIT_GATOR_EXIT_CODE) {
                LOG_FINE("Received EXIT_OK command. exiting gatord");
                return {State::EXIT, 0};
            }
        }
        else {
            assert(WIFSIGNALED(status));
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            int signal = WTERMSIG(status);
            //NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_FINE("Child process %d was terminated by signal %s (%d)", pid, strsignal(signal), signal);
            // child exit codes start from 1 so should be less than 64.
            // add 64 for signal to differentiate from normal exit.
            // can't use 128 to 255 because that would be used by a shell
            // if this process (gator-main) signalled.
            exitStatus = 64 + signal;
        }

        assert(currentStateAndChildPid.state != State::IDLE);
        if (currentStateAndChildPid.state == State::CAPTURING) {
            return {.state = State::IDLE, .pid = exitStatus};
        }

        return {State::EXIT, exitStatus};
    }

    StateAndPid handleSignal(StateAndPid currentStateAndChildPid, Drivers & drivers, int signum)
    {
        if (signum == SIGCHLD) {
            return handleSigchld(currentStateAndChildPid, drivers);
        }

        LOG_FINE("Received signal %d, gator daemon exiting", signum);

        switch (currentStateAndChildPid.state) {
            case State::CAPTURING:
                // notify child to exit
                LOG_ERROR("Waiting for gator-child to finish, send SIGKILL or SIGQUIT (Ctrl+\\) to force exit");
                kill(currentStateAndChildPid.pid, SIGINT);
                currentStateAndChildPid.state = State::EXITING;
                break;
            case State::IDLE:
                return {State::EXIT, 0};
            case State::EXITING:
                LOG_ERROR("Still waiting for gator-child to finish, send SIGKILL or SIGQUIT (Ctrl+\\) to force exit");
                kill(currentStateAndChildPid.pid, SIGINT);
                break;
            case State::EXIT:
                break;
        }

        return currentStateAndChildPid;
    }

    class StreamlineCommandHandler : public IStreamlineCommandHandler {
    public:
        State handleRequest(char *) override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_REQUEST_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleDeliver(char *) override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_DELIVER_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleApcStart() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_APC_START");
            return State::EXIT_APC_START;
        }
        State handleApcStop() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_APC_STOP");
            return State::EXIT_APC_STOP;
        }
        State handleDisconnect() override { return State::EXIT_DISCONNECT; }
        State handlePing() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_PING");
            return State::PROCESS_COMMANDS;
        }
        State handleExit() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_EXIT_OK");
            return State::EXIT_OK;
        }
        State handleRequestCurrentConfig() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_REQUEST_CURRENT_CONFIG");
            return State::PROCESS_COMMANDS_CONFIG;
        }
    };

    /**
     * Handles an incoming connection when there is already a session active.
     *
     * The user may only send the COMMAND_DISCONNECT. All other commands are considered errors.
     *
     * This is used to allow the ADB device scanner to continue to function even during a
     * capture without flooding the console with "Session already active" messages.
     * @param fd The newly accepted connection's file handle
     * @param log_ops Supplies the last generated error log message for reporting back to Streamline
     */
    void handleSecondaryConnection(int fd, const logging::log_access_ops_t * log_ops)
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-2ndconn"), 0, 0, 0);

        OlySocket client {fd};
        Sender sender(&client);

        assert(log_ops != nullptr);

        // Wait to receive a single command
        StreamlineCommandHandler commandHandler;
        const auto result = streamlineSetupCommandIteration(client, commandHandler, [](bool) -> void {});

        if (result == IStreamlineCommandHandler::State::PROCESS_COMMANDS_CONFIG) {
            auto currentConfigXML =
                current_config_xml::generateCurrentConfigXML(getpid(), // since its main get the pid, instead of ppid
                                                             getuid(),
                                                             gSessionData.mSystemWide,
                                                             gSessionData.mWaitingOnCommand,
                                                             gSessionData.mWaitForProcessCommand,
                                                             gSessionData.mCaptureWorkingDir,
                                                             gSessionData.mPids);
            sender.writeData(currentConfigXML.data(), currentConfigXML.size(), ResponseType::CURRENT_CONFIG, true);
        }
        else if (result != IStreamlineCommandHandler::State::EXIT_DISCONNECT) {
            // the expectation is that the user sends COMMAND_DISCONNECT, so anything else is an error
            LOG_ERROR("Session already in progress");
            std::string last_error = log_ops->get_last_log_error();
            sender.writeData(last_error.data(), last_error.size(), ResponseType::ERROR, true);
        }

        // Ensure all data is flushed the host receive the data (not closing socket too quick)
        sleep(1);
        client.shutdownConnection();
        client.closeSocket();
    }

    std::array<std::unique_ptr<agents::i_agent_spawner_t>, 2> create_spawners()
    {
        auto high_privilege_spawner = std::make_unique<agents::simple_agent_spawner_t>();
        std::unique_ptr<agents::i_agent_spawner_t> low_privilege_spawner {};

        // If running as root, never use run-as, just fork directly
        const auto is_root = lib::geteuid() == 0;
        if (!is_root && !gSessionData.mSystemWide && (gSessionData.mAndroidPackage != nullptr)) {
            low_privilege_spawner = std::make_unique<agents::android_pkg_agent_spawner_t>(gSessionData.mAndroidPackage);
        }
        else {
            // If a package has been specified, check that it exists (error logging comes from AndroidActivityManager).
            // This is done as a part of android_pkg_agent_spawner_t's construction so doesn't need to be specified
            // above
            if (!gSessionData.mSystemWide && (gSessionData.mAndroidPackage != nullptr)
                && !AndroidActivityManager::has_package(gSessionData.mAndroidPackage)) {
                handleException();
            }
            low_privilege_spawner = std::make_unique<agents::simple_agent_spawner_t>();
        }

        return {std::move(high_privilege_spawner), std::move(low_privilege_spawner)};
    }

    StateAndPid handleClient(StateAndPid currentStateAndChildPid,
                             Drivers & drivers,
                             OlyServerSocket & sock,
                             OlyServerSocket * otherSock,
                             capture::capture_process_event_listener_t & event_listener,
                             const logging::log_access_ops_t & log_ops)
    {
        if (currentStateAndChildPid.state != State::IDLE) {
            // A temporary socket connection to host, to transfer error message
            std::thread handler {handleSecondaryConnection, sock.acceptConnection(), &log_ops};
            handler.detach();

            return currentStateAndChildPid;
        }

        OlySocket client(sock.acceptConnection());
        for (const auto & driver : drivers.getAll()) {
            driver->preChildFork();
        }

        int pid = fork();
        if (pid < 0) {
            // Error
            auto ss = std::stringstream("Fork process failed with errno: ");
            ss << errno;
            throw GatorException(ss.str());
        }

        if (pid == 0) {
            gator::process::set_parent_death_signal(SIGKILL);

            // Child
            for (const auto & driver : drivers.getAll()) {
                driver->postChildForkInChild();
            }
            sock.closeServerSocket();
            if (otherSock != nullptr) {
                otherSock->closeServerSocket();
            }

            udpListener.close();
            monitor.close();
            annotateListenerPtr.reset();

            // create the agent process spawners
            auto [high_privilege_spawner, low_privilege_spawner] = create_spawners();

            auto child = Child::createLive(*high_privilege_spawner,
                                           *low_privilege_spawner,
                                           drivers,
                                           client,
                                           event_listener,
                                           log_ops);
            child->run();
            child.reset();
            low_privilege_spawner.reset(); // the dtor may perform some necessary cleanup
            high_privilege_spawner.reset();

            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            exit(CHILD_EXIT_AFTER_CAPTURE);
        }
        else {
            // Parent
            for (const auto & driver : drivers.getAll()) {
                driver->postChildForkInParent();
            }
            client.closeSocket();
            return {.state = State::CAPTURING, .pid = pid};
        }
    }

    StateAndPid doLocalCapture(Drivers & drivers,
                               const Child::Config & config,
                               capture::capture_process_event_listener_t & event_listener,
                               const logging::log_access_ops_t & log_ops)
    {
        for (const auto & driver : drivers.getAll()) {
            driver->preChildFork();
        }
        int pid = fork();
        if (pid < 0) {
            // Error
            auto ss = std::stringstream("Fork process failed with errno: ");
            ss << errno;
            throw GatorException(ss.str());
        }
        else if (pid == 0) {
            // Child
            for (const auto & driver : drivers.getAll()) {
                driver->postChildForkInChild();
            }
            monitor.close();
            annotateListenerPtr.reset();

            // create the agent process spawners
            auto [high_privilege_spawner, low_privilege_spawner] = create_spawners();

            auto child = Child::createLocal(*high_privilege_spawner,
                                            *low_privilege_spawner,
                                            drivers,
                                            config,
                                            event_listener,
                                            log_ops);
            LOG_FINE("Starting gator-child");
            child->run();
            LOG_FINE("gator-child finished running");
            child.reset();

            low_privilege_spawner.reset(); // the dtor may perform some necessary cleanup
            high_privilege_spawner.reset();

            LOG_FINE("gator-child exiting");

            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            exit(0);
        }
        else {
            for (const auto & driver : drivers.getAll()) {
                driver->postChildForkInParent();
            }
            return {.state = State::EXITING, // we should exit immediately after this capture finishes
                    .pid = pid};
        }
    }
}

int capture::beginCaptureProcess(const ParserResult & result,
                                 Drivers & drivers,
                                 std::array<int, 2> signalPipe,
                                 logging::log_access_ops_t & log_ops,
                                 capture::capture_process_event_listener_t & event_listener)
{
    // Set to high priority
    if (setpriority(PRIO_PROCESS, lib::gettid(), high_priority) == -1) {
        LOG_WARNING("setpriority() failed");
    }

    // Ignore the SIGPIPE signal so that any send to a broken socket will return an error code instead of asserting a signal
    // Handling the error at the send function call is much easier than trying to do anything intelligent in the sig handler
    signal(SIGPIPE, SIG_IGN);

    // only enable when running in system-wide mode
    bool enable_annotation_listener = result.mSystemWide;

    StateAndPid stateAndChildPid = {.state = State::IDLE, .pid = -1};

    try {
        std::unique_ptr<OlyServerSocket> socketUds;
        std::unique_ptr<OlyServerSocket> socketTcp;

        if (enable_annotation_listener) {
            annotateListenerPtr = std::make_unique<AnnotateListener>();
            annotateListenerPtr->setup();
        }

        int pipefd[2];
        if (lib::pipe_cloexec(pipefd) != 0) {
            throw GatorException("Unable to set up annotate pipe");
        }
        gSessionData.mAnnotateStart = pipefd[1];
        if (!monitor.init()
#ifdef TCP_ANNOTATIONS
            || ((annotateListenerPtr != nullptr) && !monitor.add(annotateListenerPtr->getSockFd()))
#endif
            || ((annotateListenerPtr != nullptr) && !monitor.add(annotateListenerPtr->getUdsFd()))
            || !monitor.add(pipefd[0]) || !monitor.add(signalPipe[0])) {
            throw GatorException("Monitor setup failed");
        }

        // If the command line argument is a session xml file, no need to open a socket
        if (gSessionData.mLocalCapture) {
            Child::Config childConfig {{}, {}};
            for (const auto & event : result.events) {
                CounterConfiguration config;
                config.counterName = event.first;
                config.event = event.second;
                childConfig.events.insert(std::move(config));
            }
            for (const auto & spe : result.mSpeConfigs) {
                childConfig.spes.insert(spe);
            }
            stateAndChildPid = doLocalCapture(drivers, childConfig, event_listener, log_ops);
        }
        else {
            // enable TCP socket
            if (result.port != DISABLE_TCP_USE_UDS_PORT) {
                socketTcp = std::make_unique<OlyServerSocket>(result.port);
                udpListener.setup(result.port);
                if (!monitor.add(socketTcp->getFd()) || !monitor.add(udpListener.getReq())) {
                    throw GatorException("Monitor setup failed: couldn't add host listeners");
                }
            }
            // always enable UDS socket
            {
                socketUds = std::make_unique<OlyServerSocket>(NO_TCP_PIPE.data(), NO_TCP_PIPE.size(), true);
                if (!monitor.add(socketUds->getFd())) {
                    throw GatorException("Monitor setup failed: couldn't add host listeners");
                }
            }
        }

        event_listener.process_initialised();

        // Forever loop, can be exited via a signal or exception
        while (stateAndChildPid.state != State::EXIT) {
            struct epoll_event events[3];
            int ready = monitor.wait(events, ARRAY_LENGTH(events), -1);
            if (ready < 0) {
                throw GatorException("Monitor::wait failed");
            }

            for (int i = 0; i < ready; ++i) {
                if ((socketUds != nullptr) && (events[i].data.fd == socketUds->getFd())) {
                    stateAndChildPid =
                        handleClient(stateAndChildPid, drivers, *socketUds, socketTcp.get(), event_listener, log_ops);
                }
                else if ((socketTcp != nullptr) && (events[i].data.fd == socketTcp->getFd())) {
                    stateAndChildPid =
                        handleClient(stateAndChildPid, drivers, *socketTcp, socketUds.get(), event_listener, log_ops);
                }
                else if (events[i].data.fd == udpListener.getReq()) {
                    udpListener.handle();
                }
#ifdef TCP_ANNOTATIONS
                else if ((annotateListenerPtr != nullptr) && (events[i].data.fd == annotateListenerPtr->getSockFd())) {
                    annotateListenerPtr->handleSock();
                }
#endif
                else if ((annotateListenerPtr != nullptr) && (events[i].data.fd == annotateListenerPtr->getUdsFd())) {
                    annotateListenerPtr->handleUds();
                }
                else if (events[i].data.fd == pipefd[0]) {
                    uint64_t val;
                    if (read(pipefd[0], &val, sizeof(val)) != sizeof(val)) {
                        LOG_WARNING("Reading annotate pipe failed");
                    }
                    if (annotateListenerPtr != nullptr) {
                        annotateListenerPtr->signal();
                    }
                }
                else if (events[i].data.fd == signalPipe[0]) {
                    int signum;
                    const int amountRead = ::read(signalPipe[0], &signum, sizeof(signum));
                    if (amountRead != sizeof(signum)) {
                        auto ss = std::stringstream("read failed(");
                        ss << errno << ") " << strerror(errno);
                        throw GatorException(ss.str());
                    }

                    const auto old_state = stateAndChildPid.state;
                    stateAndChildPid = handleSignal(stateAndChildPid, drivers, signum);

                    // if the gator-child process has just completed a capture we should restart the log file
                    // to prevent it from growing in size infinitely.
                    // NOTE: this needs to happen here, in gator-main, because at this point we know there will be
                    // only one process with a handle to the file (gator-main). The original idea was to close and
                    // move the file in gator-child, at the end of the capture, but this doesn't work because
                    // gator-main still has a handle to the old log file. This means log data ends up in the wrong
                    // file when running in daemon mode.
                    if (old_state == State::CAPTURING && stateAndChildPid.state == State::IDLE
                        && stateAndChildPid.pid == CHILD_EXIT_AFTER_CAPTURE) {
                        log_ops.restart_log_file();
                        // change to the "exit OK" status
                        stateAndChildPid.pid = 0;
                    }
                }
                else {
                    // shouldn't really happen unless we forgot to handle a new item
                    throw GatorException("Unexpected fd in monitor");
                }
            }
        }

        // pid contains the exit code once the child process has ended
        return stateAndChildPid.pid;
    }
    catch (const GatorException & ex) {
        LOG_WARNING("GatorException caught %s", ex.what());

        // hard-kill the child process if its running
        switch (stateAndChildPid.state) {
            case State::CAPTURING:
                LOG_INFO("Sending SIGKILL to child process");
                kill(-stateAndChildPid.pid, SIGKILL);
                break;
            case State::IDLE:
            case State::EXITING:
            case State::EXIT:
                break;
        }

        handleException();
    }

    return EXCEPTION_EXIT_CODE;
}
