/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef COMMAND_H
#define COMMAND_H

#include <semaphore.h>
#include <thread>
#include <functional>

struct Command
{
    int pid;
    std::thread thread;
};

Command runCommand(sem_t & waitToStart, std::function<void()> terminationCallback);

#endif // COMMAND_H
