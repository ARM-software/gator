/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#pragma once

#include "Source.h"
#include "agents/ext_source/ext_source_connection.h"
#include "lib/AutoClosingFd.h"

#include <memory>

#include <semaphore.h>

class Drivers;

class ExternalSource : public Source {
public:
    /** Create a pipe and return the write end. The read end will consume bytes from the external source agent and add them into an APC frame */
    virtual lib::AutoClosingFd add_agent_pipe(std::unique_ptr<agents::ext_source_connection_t> connection) = 0;
};

/// Counters from external sources like graphics drivers and annotations
std::shared_ptr<ExternalSource> createExternalSource(sem_t & senderSem, Drivers & drivers);
