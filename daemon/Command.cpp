/**
 * Copyright (C) ARM Limited 2014-2016. All rights reserved.
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
    snprintf(cmd, sizeof(cmd), "chown %s %s || rm %s", name, gatorTemp, gatorTemp);

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

void *commandThread(void *)
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-command"), 0, 0, 0);

    const char * const name = gSessionData.mCaptureUser == NULL ? "nobody" : gSessionData.mCaptureUser;
    uid_t uid;
    gid_t gid;
    if (!getUid(name, &uid, &gid)) {
        logg.logError("Unable to look up the user %s, please double check that the user exists", name);
        handleException();
    }

    sleep(3);

    char buf[1 << 8];
    int pipefd[2];
    if (pipe_cloexec(pipefd) != 0) {
        logg.logError("pipe failed");
        handleException();
    }

    const int pid = fork();
    if (pid < 0) {
        logg.logError("fork failed");
        handleException();
    }
    if (pid == 0) {
        buf[0] = '\0';
        close(pipefd[0]);

        // Gator runs at a high priority, reset the priority to the default
        if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
            snprintf(buf, sizeof(buf), "setpriority failed");
            goto fail_exit;
        }

        if (setgroups(1, &gid) != 0) {
            snprintf(buf, sizeof(buf), "setgroups failed");
            goto fail_exit;
        }
        if (setresgid(gid, gid, gid) != 0) {
            snprintf(buf, sizeof(buf), "setresgid failed");
            goto fail_exit;
        }
        if (setresuid(uid, uid, uid) != 0) {
            snprintf(buf, sizeof(buf), "setresuid failed");
            goto fail_exit;
        }

        {
            const char * const path = gSessionData.mCaptureWorkingDir == NULL ? "/" : gSessionData.mCaptureWorkingDir;
            if (chdir(path) != 0) {
                snprintf(buf, sizeof(buf),
                         "Unable to cd to %s, please verify the directory exists and is accessible to %s", path, name);
                goto fail_exit;
            }
        }

        execlp("sh", "sh", "-c", gSessionData.mCaptureCommand, nullptr);
        snprintf(buf, sizeof(buf), "execv failed");

        fail_exit: if (buf[0] != '\0') {
            const ssize_t bytes = write(pipefd[1], buf, sizeof(buf));
            // Can't do anything if this fails
            (void) bytes;
        }

        exit(-1);
    }

    close(pipefd[1]);
    const ssize_t bytes = read(pipefd[0], buf, sizeof(buf));
    if (bytes > 0) {
        logg.logError("%s", buf);
        handleException();
    }
    close(pipefd[0]);

    return NULL;
}
