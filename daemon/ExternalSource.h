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
    ExternalSource(Child & child, sem_t & senderSem, Drivers & drivers);

    virtual bool prepare() override;
    virtual void run() override;
    virtual void interrupt() override;
    virtual bool isDone() override;
    virtual void write(ISender & sender) override;

private:
    void waitFor(int bytes);
    void configureConnection(int fd, const char * handshake, size_t size);
    bool connectMidgard();
    bool connectMve();
    void connectFtrace();
    bool transfer(uint64_t currTime, int fd);

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
