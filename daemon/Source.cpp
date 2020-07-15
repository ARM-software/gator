/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "Source.h"

#include "Child.h"
#include "Logging.h"

Source::Source(Child & child) : mChild(child), mThreadID()
{
}

void Source::start()
{
    if (pthread_create(&mThreadID, nullptr, runStatic, this) != 0) {
        logg.logError("Failed to create source thread");
        handleException();
    }
}

void Source::join() const
{
    pthread_join(mThreadID, nullptr);
}

void * Source::runStatic(void * arg)
{
    static_cast<Source *>(arg)->run();
    return nullptr;
}
