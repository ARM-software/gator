/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "Source.h"

#include "Child.h"
#include "Logging.h"

Source::Source(Child & child) : mChild(child), mThreadID() {}

Source::~Source() {}

void Source::start()
{
    if (pthread_create(&mThreadID, NULL, runStatic, this)) {
        logg.logError("Failed to create source thread");
        handleException();
    }
}

void Source::join()
{
    pthread_join(mThreadID, NULL);
}

void * Source::runStatic(void * arg)
{
    static_cast<Source *>(arg)->run();
    return NULL;
}
