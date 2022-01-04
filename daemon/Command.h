/* Copyright (C) 2014-2021 by Arm Limited. All rights reserved. */

#ifndef COMMAND_H
#define COMMAND_H

#include "lib/SharedMemory.h"

#include <atomic>
#include <functional>
#include <thread>

#include <semaphore.h>

class Command {
public:
    static Command run(const std::function<void()> & terminationCallback);

    void start();
    void cancel();
    void join() { thread.join(); };

    int getPid() const { return pid; };

private:
    enum State {
        INITIALIZING,
        RUNNING,
        BEING_KILLED,
        KILLED_OR_EXITED,
    };

    struct SharedData {
        sem_t start {};
        std::atomic<State> state {};

        SharedData() { sem_init(&start, 1, 0); }

        ~SharedData() { sem_destroy(&start); }
    };

    int pid;
    std::thread thread;
    shared_memory::unique_ptr<SharedData> sharedData;

    Command(int pid, std::thread thread, shared_memory::unique_ptr<SharedData> sharedData)
        : pid {pid}, thread {std::move(thread)}, sharedData {std::move(sharedData)}
    {
    }
};

#endif // COMMAND_H
