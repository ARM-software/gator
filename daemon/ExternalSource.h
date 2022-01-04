/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#pragma once

#include <memory>

#include <semaphore.h>

class Child;
class Source;
class Drivers;

/// Counters from external sources like graphics drivers and annotations
std::unique_ptr<Source> createExternalSource(sem_t & senderSem, Drivers & drivers);
