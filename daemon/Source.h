/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef SOURCE_H
#define SOURCE_H

#include <pthread.h>

class Child;
class ISender;

class Source {
public:
    Source(Child & child);
    virtual ~Source() = default;

    virtual bool prepare() = 0;
    void start();
    virtual void run() = 0;
    virtual void interrupt() = 0;
    void join() const;

    virtual bool isDone() = 0;
    virtual void write(ISender & sender) = 0;

protected:
    // active child object
    Child & mChild;

private:
    static void * runStatic(void * arg);

    pthread_t mThreadID;

    // Intentionally undefined
    Source(const Source &) = delete;
    Source & operator=(const Source &) = delete;
    Source(Source &&) = delete;
    Source & operator=(Source &&) = delete;
};

#endif // SOURCE_H
