/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "AnnotateListener.h"

#include <unistd.h>

#include "OlySocket.h"

static const char STREAMLINE_ANNOTATE_PARENT[] = "\0streamline-annotate-parent";

struct AnnotateClient
{
    AnnotateClient *next;
    int fd;
};

AnnotateListener::AnnotateListener()
        : mClients(NULL),
#ifdef TCP_ANNOTATIONS
          mSock(NULL),
#endif
          mUds(NULL)
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
    AnnotateClient * const client = new AnnotateClient();
    client->fd = mUds->acceptConnection();
    client->next = mClients;
    mClients = client;
}

void AnnotateListener::close()
{
    if (mUds != NULL) {
        mUds->closeServerSocket();
    }
#ifdef TCP_ANNOTATIONS
    if (mSock != NULL) {
        mSock->closeServerSocket();
    }
#endif
    while (mClients != NULL) {
        ::close(mClients->fd);
        AnnotateClient *next = mClients->next;
        delete mClients;
        mClients = next;
    }
}

void AnnotateListener::signal()
{
    const char ch = 0;
    AnnotateClient **ptr = &mClients;
    AnnotateClient *client = mClients;
    while (client != NULL) {
        if (write(client->fd, &ch, sizeof(ch)) != 1) {
            ::close(client->fd);
            AnnotateClient *next = client->next;
            delete client;
            *ptr = next;
            client = next;
            continue;
        }
        ptr = &client->next;
        client = client->next;
    }
}
