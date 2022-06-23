/* Copyright (C) 2014-2022 by Arm Limited. All rights reserved. */

#include "Command.h"

#include "ExitStatus.h"
#include "Logging.h"
#include "SessionData.h"
#include "lib/FileDescriptor.h"
#include "lib/String.h"

#include <cstdio>

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool getUid(const char * const name, const char * const tmpDir, uid_t * const uid)
{
    // Lookups may fail when using a different libc or a statically compiled executable
    lib::printf_str_t<32> gatorTemp {"%s/gator_temp", tmpDir};

    const int fd = open(gatorTemp, O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return false;
    }
    close(fd);

    lib::printf_str_t<128> cmd {"chown %s %s || rm -f %s", name, gatorTemp.c_str(), gatorTemp.c_str()};

    const int pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork failed");
        handleException();
    }

    if (pid == 0) {
        execlp("sh", "sh", "-c", cmd, nullptr);
        exit(COMMAND_FAILED_EXIT_CODE);
    }

    while ((waitpid(pid, nullptr, 0) < 0) && (errno == EINTR)) {
    }

    struct stat st;
    int result = -1;
    if (stat(gatorTemp, &st) != 0) {
        return false;
    }
    result = st.st_uid;
    unlink(gatorTemp);
    *uid = result;
    return true;
}

static bool getUid(const char * const name, uid_t * const uid, gid_t * const gid)
{
    // Look up the username
    struct passwd * const user = getpwnam(name);
    if (user != nullptr) {
        *uid = user->pw_uid;
        *gid = user->pw_gid;
        return true;
    }

    // Unable to get the user without getpwanm, so create a unique uid by adding a fixed number to the pid
    *gid = 0x484560f8 + getpid();

    // Are we on Linux
    if (access("/tmp", W_OK) == 0) {
        return getUid(name, "/tmp", uid);
    }

    // Are we on android
    if (access("/data", W_OK) == 0) {
        return getUid(name, "/data", uid);
    }

    return false;
}

static void checkCommandStatus(int pid)
{
    int status;
    while (waitpid(pid, &status, WNOHANG) == -1) {
        if (errno != EINTR) {
            LOG_ERROR("Could not waitpid(%d) on child command. (%s)", pid, strerror(errno));
            return;
        }
    }

    if (WIFEXITED(status)) {
        const int exitCode = WEXITSTATUS(status);

        // add some special case handling for when we are launching via bash shell
        if ((gSessionData.mCaptureCommand.size() == 3) && (gSessionData.mCaptureCommand[0] == "sh")
            && (gSessionData.mCaptureCommand[1] == "-c")) {
            if (exitCode == 126) {
                LOG_ERROR("Failed to run command %s: Permission denied or is a directory",
                          gSessionData.mCaptureCommand[2].c_str());
                handleException();
            }
            if (exitCode == 127) {
                LOG_ERROR("Failed to run command %s: Command not found", gSessionData.mCaptureCommand[2].c_str());
                handleException();
            }
        }

        if (exitCode != 0) {
            LOG_ERROR("command exited with code %d", exitCode);
        }
        else {
            LOG_DEBUG("command exited with code 0");
        }
    }
    else if (WIFSIGNALED(status)) {
        const int signal = WTERMSIG(status);
        if (signal != SIGTERM && signal != SIGINT) { // should we consider any others normal?
            LOG_ERROR("command terminated abnormally: %s", strsignal(signal));
        }
    }
}

void Command::start()
{
    sem_post(&(sharedData->start));
}

void Command::cancel()
{
    State expected = INITIALIZING;
    if (sharedData->state.compare_exchange_strong(expected, KILLED_OR_EXITED)) {
        // command will kill itself when it's finished initializing
        return;
    }

    expected = RUNNING;
    if (!sharedData->state.compare_exchange_strong(expected, BEING_KILLED)) {
        // already cancelled by someone else or already exited
        return;
    }

    start(); // just in case it was still waiting to start

    // once it is RUNNING, the command will have created it's own process group
    // so we can signal the whole process group
    if (::kill(-pid, SIGTERM) == -1) {
        LOG_ERROR("kill(%d) failed (%d) %s", -pid, errno, strerror(errno));
    }

    if (sharedData->state.exchange(KILLED_OR_EXITED) != BEING_KILLED) {
        // then must of exited in the meantime
        // the waiter thread recognized it was BEING_KILLED, so left us to reap it.
        checkCommandStatus(pid);
    }
}

Command Command::run(const std::function<void()> & terminationCallback)
{
    constexpr size_t buffer_size = 1 << 8;

    uid_t uid = geteuid();
    gid_t gid = getegid();
    const char * const name = gSessionData.mCaptureUser;

    // if name is null then just use the current user
    if (name != nullptr) {
        // for non root.
        // Verify root permissions
        const bool isRoot = (geteuid() == 0);
        if (!isRoot) {
            LOG_ERROR("Unable to set user to %s for command because gatord is not running as root", name);
            handleException();
        }

        if (!getUid(name, &uid, &gid)) {
            LOG_ERROR("Unable to look up the user %s, please double check that the user exists", name);
            handleException();
        }
    }

    auto sharedData = shared_memory::make_unique<SharedData>();
    // get references now before we move the pointer
    auto & state = sharedData->state;
    auto & start = sharedData->start;

    int pipefd[2];
    if (lib::pipe_cloexec(pipefd) != 0) {
        LOG_ERROR("pipe failed");
        handleException();
    }

    const int pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork failed");
        handleException();
    }

    if (pid == 0) {
        // child
        lib::printf_str_t<buffer_size> buf {};

        // Reset signal handlers while waiting for exec
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        signal(SIGALRM, SIG_DFL);

        //Need to change the GPID so that all children of this process will have this processes PID as their GPID.
        setpgid(pid, pid);

        State expected = INITIALIZING;
        if (!state.compare_exchange_strong(expected, RUNNING)) {
            // we've been cancelled before even starting
            exit(0);
        }

        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-command"), 0, 0, 0);

        close(pipefd[0]);

        std::vector<char *> cmd_str {};
        for (const auto & string : gSessionData.mCaptureCommand) {
            cmd_str.push_back(const_cast<char *>(string.c_str()));
        }
        cmd_str.push_back(nullptr);
        char * const * const commands = cmd_str.data();

        // Gator runs at a high priority, reset the priority to the default
        if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
            buf.printf("setpriority failed");
            goto fail_exit;
        }

        if (name != nullptr) {
            if (setgroups(1, &gid) != 0) {
                buf.printf("setgroups failed for user: %s, please check if the user is part of group", name);
                goto fail_exit;
            }
            if (setresgid(gid, gid, gid) != 0) {
                buf.printf("setresgid failed for user: %s, please check if the user is part of GID %d", name, gid);
                goto fail_exit;
            }
            if (setresuid(uid, uid, uid) != 0) {
                buf.printf("setresuid failed for user: %s, please check if the user is part of UID %d", name, uid);
                goto fail_exit;
            }
        }

        {
            const char * const path =
                gSessionData.mCaptureWorkingDir == nullptr ? "/" : gSessionData.mCaptureWorkingDir;
            if (chdir(path) != 0) {
                buf.printf("Unable to cd to %s, please verify the directory exists and is accessible to %s",
                           path,
                           name != nullptr ? name : "the current user");
                goto fail_exit;
            }
        }
        sem_wait(&start);

        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(commands[0]), 0, 0, 0);
        execvp(commands[0], commands);
        buf.printf("Failed to run command %s\nexecvp failed: %s", commands[0], strerror(errno));

    fail_exit:
        if (buf.size() > 0) {
            const ssize_t bytes = write(pipefd[1], buf.c_str(), buf.size());
            // Can't do anything if this fails
            (void) bytes;
        }

        exit(COMMAND_FAILED_EXIT_CODE);
    }
    else {
        // parent

        close(pipefd[1]);

        return {pid,
                std::thread {[pipefd, pid, terminationCallback, &state]() {
                    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-command-reader"), 0, 0, 0);
                    std::array<char, buffer_size> buf {};
                    ssize_t bytesRead = 0;
                    while (true) {
                        const ssize_t bytes = read(pipefd[0], buf.data() + bytesRead, buf.size() - bytesRead);
                        if (bytes > 0) {
                            bytesRead += bytes;
                        }
                        else if (bytes == 0) {
                            break;
                        }
                        else if (errno != EAGAIN) {
                            buf[bytesRead] = '\0';
                            LOG_ERROR("Failed to read pipe from child: %s", strerror(errno));
                            break;
                        }
                    }

                    close(pipefd[0]);

                    if (bytesRead > 0) {
                        LOG_ERROR("%s", buf.data());
                        handleException();
                    }
                    else {
                        siginfo_t info;
                        while (waitid(P_PID, pid, &info, WEXITED | WNOWAIT) == -1) {
                            if (errno != EINTR) {
                                LOG_ERROR("waitid(%d) failed (%d) %s", pid, errno, strerror(errno));
                                break;
                            }
                        }

                        if (state.exchange(KILLED_OR_EXITED) != BEING_KILLED) {
                            checkCommandStatus(pid);
                        }
                        // else cancel is being called. We don't want to reap
                        // the pid while it's trying to kill it in case the pid gets reused.

                        terminationCallback();
                    }
                }},
                std::move(sharedData)};
    }
}
