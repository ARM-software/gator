/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Command.h"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Logging.h"
#include "SessionData.h"

#include "lib/FileDescriptor.h"

static int getUid(const char * const name, const char * const tmpDir, uid_t * const uid)
{
    // Lookups may fail when using a different libc or a statically compiled executable
    char gatorTemp[32];
    snprintf(gatorTemp, sizeof(gatorTemp), "%s/gator_temp", tmpDir);

    const int fd = open(gatorTemp, 600, O_CREAT | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    close(fd);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "chown %s %s || rm -f %s", name, gatorTemp, gatorTemp);

    const int pid = fork();
    if (pid < 0) {
        logg.logError("fork failed");
        handleException();
    }
    if (pid == 0) {
        execlp("sh", "sh", "-c", cmd, nullptr);
        exit(-1);
    }
    while ((waitpid(pid, NULL, 0) < 0) && (errno == EINTR))
        ;

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
    if (user != NULL) {
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

static void checkCommandStatus(int status)
{
    if (WIFEXITED(status)) {
        const int exitCode = WEXITSTATUS(status);

        // add some special case handling for when we are launching via bash shell
        if ((gSessionData.mCaptureCommand.size() == 3) && (gSessionData.mCaptureCommand[0] == "sh")
                && (gSessionData.mCaptureCommand[1] == "-c")) {
            if (exitCode == 126) {
                logg.logError("Failed to run command %s: Permission denied or is a directory", gSessionData.mCaptureCommand[2].c_str());
                handleException();
            }
            if (exitCode == 127) {
                logg.logError("Failed to run command %s: Command not found", gSessionData.mCaptureCommand[2].c_str());
                handleException();
            }
        }

        if (exitCode != 0)
        {
            logg.logError("command exited with code %d", exitCode);
        }
        else
        {
            logg.logMessage("command exited with code 0");
        }
    }
    else if (WIFSIGNALED(status)) {
        const int signal = WTERMSIG(status);
        if (signal != SIGTERM && signal != SIGINT) // should we consider any others normal?
            logg.logError("command terminated abnormally: %s", strsignal(signal));
    }
}

Command runCommand(sem_t & waitToStart, std::function<void()> terminationCallback)
{
    uid_t uid = geteuid();
    gid_t gid = getegid();
    const char * const name = gSessionData.mCaptureUser;

    // if name is null then just use the current user
    if (name != NULL) {
        // for non root.
        // Verify root permissions
        const bool isRoot = (geteuid() == 0);
        if (!isRoot) {
            logg.logError("Unable to set user to %s for command because gatord is not running as root", name);
            handleException();
        }

        if (!getUid(name, &uid, &gid)) {
            logg.logError("Unable to look up the user %s, please double check that the user exists", name);
            handleException();
        }
    }

    constexpr size_t bufSize = 1 << 8;
    int pipefd[2];
    if (lib::pipe_cloexec(pipefd) != 0) {
        logg.logError("pipe failed");
        handleException();
    }

    const int pid = fork();
    if (pid < 0) {
        logg.logError("fork failed");
        handleException();
    }

    if (pid == 0) {
        // child

        // Reset signal handlers while waiting for exec
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGABRT, SIG_DFL);
        signal(SIGALRM, SIG_DFL);

        //Need to change the GPID so that all children of this process will have this processes PID as their GPID.
        setpgid(pid, pid);

        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-command"), 0, 0, 0);

        char buf[bufSize];
        buf[0] = '\0';
        close(pipefd[0]);

        std::vector<char*> cmd_str { };
        for (const auto & string : gSessionData.mCaptureCommand) {
            cmd_str.push_back(const_cast<char *>(string.c_str()));
        }
        cmd_str.push_back(nullptr);
        char * const * const commands = cmd_str.data();

        // Gator runs at a high priority, reset the priority to the default
        if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
            snprintf(buf, sizeof(buf), "setpriority failed");
            goto fail_exit;
        }

        if (name != NULL) {
            if (setgroups(1, &gid) != 0) {
                snprintf(buf, sizeof(buf), "setgroups failed for user: %s, please check if the user is part of group", name );
                goto fail_exit;
            }
            if (setresgid(gid, gid, gid) != 0) {
                snprintf(buf, sizeof(buf), "setresgid failed for user: %s, please check if the user is part of GID %d", name, gid);
                goto fail_exit;
            }
            if (setresuid(uid, uid, uid) != 0) {
                snprintf(buf, sizeof(buf), "setresuid failed for user: %s, please check if the user is part of UID %d", name, uid);
                goto fail_exit;
            }
        }

        {
            const char * const path = gSessionData.mCaptureWorkingDir == NULL ? "/" : gSessionData.mCaptureWorkingDir;
            if (chdir(path) != 0) {
                snprintf(buf, sizeof(buf),
                         "Unable to cd to %s, please verify the directory exists and is accessible to %s", path,
                         name != nullptr ? name : "the current user");
                goto fail_exit;
            }
        }
        sem_wait(&waitToStart);
        sem_post(&waitToStart); // some other thread might be waiting on this too.

        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(commands[0]), 0, 0, 0);
        execvp(commands[0], commands);
        snprintf(buf, sizeof(buf), "Failed to run command %s\nexecvp failed: %s", commands[0], strerror(errno));

        fail_exit: if (buf[0] != '\0') {
            const ssize_t bytes = write(pipefd[1], buf, sizeof(buf));
            // Can't do anything if this fails
            (void) bytes;
        }

        exit(-1);
    }
    else {
        // parent

        close(pipefd[1]);

        return {pid, std::thread {[pipefd, pid, terminationCallback]() {
                    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-command-reader"), 0, 0, 0);
                    char buf[bufSize];
                    ssize_t bytesRead = 0;
                    while (true)
                    {
                        const ssize_t bytes = read(pipefd[0], buf + bytesRead, sizeof(buf) - bytesRead);
                        if (bytes > 0)
                        {
                            bytesRead += bytes;
                        }
                        else if (bytes == 0)
                        {
                            break;
                        }
                        else if (errno != EAGAIN) {
                            buf[bytesRead] = '\0';
                            logg.logError("Failed to read pipe from child: %s", strerror(errno));
                            break;
                        }
                    }

                    close(pipefd[0]);

                    if (bytesRead > 0) {
                        logg.logError("%s", buf);
                        handleException();
                    }
                    else {
                        // wait for the process to exit and read its status.
                        while (true) {
                            int status;
                            if (waitpid(pid, &status, 0) != -1)
                            {
                                checkCommandStatus(status);
                            }
                            else if (errno == EINTR)
                            {
                                continue;
                            }
                            else {
                                logg.logMessage("Could not waitpid on child command. (%s)", strerror(errno));
                            }
                            break;
                        }
                        terminationCallback();
                    }
                }}};
    }
}
