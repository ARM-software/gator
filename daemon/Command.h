/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#ifndef COMMAND_H
#define COMMAND_H

#include <functional>
#include <semaphore.h>
#include <thread>

struct Command {
    int pid;
    std::thread thread;
};

Command runCommand(sem_t & waitToStart, std::function<void()> terminationCallback);

#endif // COMMAND_H
