/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#ifndef ANNOTATELISTENER_H
#define ANNOTATELISTENER_H

struct AnnotateClient;
class OlyServerSocket;

class AnnotateListener {
public:
    AnnotateListener();
    ~AnnotateListener();

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

    // Intentionally unimplemented
    AnnotateListener(const AnnotateListener &) = delete;
    AnnotateListener & operator=(const AnnotateListener &) = delete;
    AnnotateListener(AnnotateListener &&) = delete;
    AnnotateListener & operator=(AnnotateListener &&) = delete;
};

#endif // ANNOTATELISTENER_H
