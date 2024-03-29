/* Copyright (C) 2014-2021 by Arm Limited. All rights reserved. */

#include "AnnotateListener.h"

#include "OlySocket.h"

#include <unistd.h>

static const char STREAMLINE_ANNOTATE_PARENT[] = "\0streamline-annotate-parent";

struct AnnotateClient {
    AnnotateClient * next;
    int fd;
};

AnnotateListener::AnnotateListener() noexcept
    : mClients(nullptr),
#ifdef TCP_ANNOTATIONS
      mSock(nullptr),
#endif
      mUds(nullptr)
{
}

AnnotateListener::~AnnotateListener()
{
    close();
    delete mUds;
#ifdef TCP_ANNOTATIONS
    delete mSock;
#endif
}

void AnnotateListener::setup()
{
#ifdef TCP_ANNOTATIONS
    mSock = new OlyServerSocket(8082);
#endif
    mUds = new OlyServerSocket(STREAMLINE_ANNOTATE_PARENT, sizeof(STREAMLINE_ANNOTATE_PARENT), true);
}

#ifdef TCP_ANNOTATIONS
int AnnotateListener::getSockFd()
{
    return mSock->getFd();
}
#endif

#ifdef TCP_ANNOTATIONS
void AnnotateListener::handleSock()
{
    AnnotateClient * const client = new AnnotateClient();
    client->fd = mSock->acceptConnection();
    client->next = mClients;
    mClients = client;
}
#endif

int AnnotateListener::getUdsFd()
{
    return mUds->getFd();
}

void AnnotateListener::handleUds()
{
    auto * const client = new AnnotateClient();
    client->fd = mUds->acceptConnection();
    client->next = mClients;
    mClients = client;
}

void AnnotateListener::close()
{
    if (mUds != nullptr) {
        mUds->closeServerSocket();
    }
#ifdef TCP_ANNOTATIONS
    if (mSock != nullptr) {
        mSock->closeServerSocket();
    }
#endif
    while (mClients != nullptr) {
        ::close(mClients->fd);
        AnnotateClient * next = mClients->next;
        delete mClients;
        mClients = next;
    }
}

void AnnotateListener::signal()
{
    const char ch = 0;
    AnnotateClient ** ptr = &mClients;
    AnnotateClient * client = mClients;
    while (client != nullptr) {
        if (write(client->fd, &ch, sizeof(ch)) != 1) {
            ::close(client->fd);
            AnnotateClient * next = client->next;
            delete client;
            *ptr = next;
            client = next;
            continue;
        }
        ptr = &client->next;
        client = client->next;
    }
}
