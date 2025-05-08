/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#include "UEvent.h"

#include "Logging.h"
#include "OlySocket.h"
#include "lib/Error.h"

#include <cerrno>
#include <cstring>

#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const char EMPTY[] = "";
static const char ACTION[] = "ACTION=";
static const char DEVPATH[] = "DEVPATH=";
static const char SUBSYSTEM[] = "SUBSYSTEM=";

UEvent::UEvent() : mFd(-1)
{
}

UEvent::~UEvent()
{
    if (mFd >= 0) {
        close(mFd);
    }
}

bool UEvent::init()
{
    mFd = socket_cloexec(PF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
    if (mFd < 0) {
        LOG_WARNING("Socket failed for uevents (%d - %s)", errno, lib::strerror());
        return false;
    }

    struct sockaddr_nl sockaddr;
    memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.nl_family = AF_NETLINK;
    sockaddr.nl_groups = 1; // bitmask: (1 << 0) == kernel events, (1 << 1) == udev events
    sockaddr.nl_pid = 0;
    if (bind(mFd, reinterpret_cast<struct sockaddr *>(&sockaddr), sizeof(sockaddr)) != 0) {
        LOG_WARNING("Bind failed for uevents (%d - %s)", errno, lib::strerror());
        return false;
    }

    return true;
}

bool UEvent::read(UEventResult * const result)
{
    ssize_t bytes = recv(mFd, result->mBuf, sizeof(result->mBuf), 0);
    if (bytes <= 0) {
        LOG_WARNING("recv failed");
        return false;
    }

    result->mAction = EMPTY;
    result->mDevPath = EMPTY;
    result->mSubsystem = EMPTY;

    for (int pos = 0; pos < bytes; pos += strlen(result->mBuf + pos) + 1) {
        char * const str = result->mBuf + pos;
        LOG_DEBUG("uevent + %i: %s", pos, str);
        if (strncmp(str, ACTION, sizeof(ACTION) - 1) == 0) {
            result->mAction = str + sizeof(ACTION) - 1;
        }
        else if (strncmp(str, DEVPATH, sizeof(DEVPATH) - 1) == 0) {
            result->mDevPath = str + sizeof(DEVPATH) - 1;
        }
        else if (strncmp(str, SUBSYSTEM, sizeof(SUBSYSTEM) - 1) == 0) {
            result->mSubsystem = str + sizeof(SUBSYSTEM) - 1;
        }
    }

    return true;
}
