/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

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
#include "capture/internal/UdpListener.h"
#include "lib/FileDescriptor.h"
#include "lib/Process.h"
#include "xml/CurrentConfigXML.h"
#include "xml/PmuXMLParser.h"

#include <sstream>
#include <thread>

#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

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
    gator::capture::internal::UdpListener udpListener;
    AnnotateListener annotateListener;

    StateAndPid handleSigchld(StateAndPid currentStateAndChildPid, Drivers & drivers)
    {
        int status;
        int pid = waitpid(currentStateAndChildPid.pid, &status, WNOHANG);
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        if (pid == -1 || !(WIFEXITED(status) || WIFSIGNALED(status))) {
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
            LOG_DEBUG("Child process %d terminated normally with status %d", pid, exitStatus);
            if (exitStatus == OK_TO_EXIT_GATOR_EXIT_CODE) {
                LOG_DEBUG("Received EXIT_OK command. exiting gatord");
                return {State::EXIT, 0};
            }
        }
        else {
            assert(WIFSIGNALED(status));
            // NOLINTNEXTLINE(hicpp-signed-bitwise)
            int signal = WTERMSIG(status);
            LOG_DEBUG("Child process %d was terminated by signal %s (%d)", pid, strsignal(signal), signal);
            // child exit codes start from 1 so should be less than 64.
            // add 64 for signal to differentiate from normal exit.
            // can't use 128 to 255 because that would be used by a shell
            // if this process (gator-main) signalled.
            exitStatus = 64 + signal;
        }

        assert(currentStateAndChildPid.state != State::IDLE);
        if (currentStateAndChildPid.state == State::CAPTURING) {
            return {.state = State::IDLE, .pid = -1};
        }

        return {State::EXIT, exitStatus};
    }

    StateAndPid handleSignal(StateAndPid currentStateAndChildPid, Drivers & drivers, int signum)
    {

        if (signum == SIGCHLD) {
            return handleSigchld(currentStateAndChildPid, drivers);
        }

        LOG_DEBUG("Received signal %d, gator daemon exiting", signum);

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
     * @param last_log_error_supplier Supplies the last generated error log message for reporting back to Streamline
     */
    void handleSecondaryConnection(int fd, logging::last_log_error_supplier_t last_log_error_supplier)
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-2ndconn"), 0, 0, 0);

        OlySocket client {fd};
        Sender sender(&client);

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
            std::string last_error = last_log_error_supplier();
            sender.writeData(last_error.data(), last_error.size(), ResponseType::ERROR, true);
        }

        // Ensure all data is flushed the host receive the data (not closing socket too quick)
        sleep(1);
        client.shutdownConnection();
        client.closeSocket();
    }

    StateAndPid handleClient(StateAndPid currentStateAndChildPid,
                             Drivers & drivers,
                             OlyServerSocket & sock,
                             OlyServerSocket * otherSock,
                             logging::last_log_error_supplier_t last_log_error_supplier,
                             logging::log_setup_supplier_t log_setup_supplier)
    {
        if (currentStateAndChildPid.state != State::IDLE) {
            // A temporary socket connection to host, to transfer error message
            std::thread handler {handleSecondaryConnection, sock.acceptConnection(), last_log_error_supplier};
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
        else if (pid == 0) {
            gator::process::set_parent_death_signal(SIGINT);

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
            annotateListener.close();

            auto child = Child::createLive(drivers, client, last_log_error_supplier, std::move(log_setup_supplier));
            child->run();
            child.reset();
            exit(0);
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
                               logging::last_log_error_supplier_t last_log_error_supplier,
                               logging::log_setup_supplier_t log_setup_supplier)
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
            annotateListener.close();

            auto child =
                Child::createLocal(drivers, config, std::move(last_log_error_supplier), std::move(log_setup_supplier));
            child->run();
            LOG_DEBUG("gator-child finished running");
            child.reset();

            LOG_DEBUG("gator-child exiting");
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

int gator::capture::beginCaptureProcess(const ParserResult & result,
                                        Drivers & drivers,
                                        std::array<int, 2> signalPipe,
                                        logging::last_log_error_supplier_t last_log_error_supplier,
                                        logging::log_setup_supplier_t log_setup_supplier,
                                        const gator::capture::GatorReadyCallback & gatorReady)
{
    // Set to high priority
    if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), -19) == -1) {
        LOG_DEBUG("setpriority() failed");
    }

    // Ignore the SIGPIPE signal so that any send to a broken socket will return an error code instead of asserting a signal
    // Handling the error at the send function call is much easier than trying to do anything intelligent in the sig handler
    signal(SIGPIPE, SIG_IGN);

    try {
        std::unique_ptr<OlyServerSocket> socketUds;
        std::unique_ptr<OlyServerSocket> socketTcp;

        annotateListener.setup();
        int pipefd[2];
        if (lib::pipe_cloexec(pipefd) != 0) {
            throw GatorException("Unable to set up annotate pipe");
        }
        gSessionData.mAnnotateStart = pipefd[1];
        if (!monitor.init()
#ifdef TCP_ANNOTATIONS
            || !monitor.add(annotateListener.getSockFd())
#endif
            || !monitor.add(annotateListener.getUdsFd()) || !monitor.add(pipefd[0]) || !monitor.add(signalPipe[0])) {
            throw GatorException("Monitor setup failed");
        }

        StateAndPid stateAndChildPid = {.state = State::IDLE, .pid = -1};

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
            stateAndChildPid = doLocalCapture(drivers, childConfig, last_log_error_supplier, log_setup_supplier);
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

        gatorReady();

        // Forever loop, can be exited via a signal or exception
        while (stateAndChildPid.state != State::EXIT) {
            struct epoll_event events[3];
            int ready = monitor.wait(events, ARRAY_LENGTH(events), -1);
            if (ready < 0) {
                throw GatorException("Monitor::wait failed");
            }

            for (int i = 0; i < ready; ++i) {
                if ((socketUds != nullptr) && (events[i].data.fd == socketUds->getFd())) {
                    stateAndChildPid = handleClient(stateAndChildPid,
                                                    drivers,
                                                    *socketUds,
                                                    socketTcp.get(),
                                                    last_log_error_supplier,
                                                    log_setup_supplier);
                }
                else if ((socketTcp != nullptr) && (events[i].data.fd == socketTcp->getFd())) {
                    stateAndChildPid = handleClient(stateAndChildPid,
                                                    drivers,
                                                    *socketTcp,
                                                    socketUds.get(),
                                                    last_log_error_supplier,
                                                    log_setup_supplier);
                }
                else if (events[i].data.fd == udpListener.getReq()) {
                    udpListener.handle();
                }
#ifdef TCP_ANNOTATIONS
                else if (events[i].data.fd == annotateListener.getSockFd()) {
                    annotateListener.handleSock();
                }
#endif
                else if (events[i].data.fd == annotateListener.getUdsFd()) {
                    annotateListener.handleUds();
                }
                else if (events[i].data.fd == pipefd[0]) {
                    uint64_t val;
                    if (read(pipefd[0], &val, sizeof(val)) != sizeof(val)) {
                        LOG_DEBUG("Reading annotate pipe failed");
                    }
                    annotateListener.signal();
                }
                else if (events[i].data.fd == signalPipe[0]) {
                    int signum;
                    const int amountRead = ::read(signalPipe[0], &signum, sizeof(signum));
                    if (amountRead != sizeof(signum)) {
                        auto ss = std::stringstream("read failed(");
                        ss << errno << ") " << strerror(errno);
                        throw GatorException(ss.str());
                    }
                    stateAndChildPid = handleSignal(stateAndChildPid, drivers, signum);
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
        LOG_DEBUG("%s", ex.what());
        handleException();
    }

    return EXCEPTION_EXIT_CODE;
}
