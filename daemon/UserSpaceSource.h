/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Span.h"

#include <memory>

#include <semaphore.h>

class PolledDriver;
class Source;

/// User space counters
std::unique_ptr<Source> createUserSpaceSource(sem_t & senderSem, lib::Span<PolledDriver * const> drivers);

bool shouldStartUserSpaceSource(lib::Span<const PolledDriver * const>);
