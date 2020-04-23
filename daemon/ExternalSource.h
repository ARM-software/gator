/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef EXTERNALSOURCE_H
#define EXTERNALSOURCE_H

#include "Buffer.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "Source.h"

#include <semaphore.h>

class Drivers;

// Counters from external sources like graphics drivers and annotations
class ExternalSource : public Source {
public:
    ExternalSource(Child & child, sem_t * senderSem, Drivers & drivers);
    ~ExternalSource();

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(ISender * sender) override;

private:
    void waitFor(const int bytes);
    void configureConnection(const int fd, const char * const handshake, size_t size);
    bool connectMidgard();
    bool connectMve();
    void connectFtrace();
    bool transfer(const uint64_t currTime, const int fd);

    sem_t mBufferSem;
    Buffer mBuffer;
    Monitor mMonitor;
    OlyServerSocket mMveStartupUds;
    OlyServerSocket mMidgardStartupUds;
    OlyServerSocket mUtgardStartupUds;
#ifdef TCP_ANNOTATIONS
    OlyServerSocket mAnnotate;
#endif
    OlyServerSocket mAnnotateUds;
    int mInterruptFd;
    int mMidgardUds;
    int mMveUds;
    Drivers & mDrivers;

    // Intentionally unimplemented
    ExternalSource(const ExternalSource &) = delete;
    ExternalSource & operator=(const ExternalSource &) = delete;
    ExternalSource(ExternalSource &&) = delete;
    ExternalSource & operator=(ExternalSource &&) = delete;
};

#endif // EXTERNALSOURCE_H
