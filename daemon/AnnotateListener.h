/* Copyright (C) 2014-2021 by Arm Limited. All rights reserved. */

#ifndef ANNOTATELISTENER_H
#define ANNOTATELISTENER_H

struct AnnotateClient;
class OlyServerSocket;

class AnnotateListener {
public:
    AnnotateListener();
    ~AnnotateListener();

    // Intentionally unimplemented
    AnnotateListener(const AnnotateListener &) = delete;
    AnnotateListener & operator=(const AnnotateListener &) = delete;
    AnnotateListener(AnnotateListener &&) = delete;
    AnnotateListener & operator=(AnnotateListener &&) = delete;

    void setup();
#ifdef TCP_ANNOTATIONS
    int getSockFd();
#endif
    int getUdsFd();

#ifdef TCP_ANNOTATIONS
    void handleSock();
#endif
    void handleUds();
    void close();
    void signal();

private:
    AnnotateClient * mClients;
#ifdef TCP_ANNOTATIONS
    OlyServerSocket * mSock;
#endif
    OlyServerSocket * mUds;
};

#endif // ANNOTATELISTENER_H
