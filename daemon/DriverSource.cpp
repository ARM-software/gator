/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

// Define to get format macros from inttypes.h
#define __STDC_FORMAT_MACROS

#include "DriverSource.h"

#include <fcntl.h>
#include <inttypes.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <dirent.h>

#include "linux/perf/PerfAttrsBuffer.h"
#include "Child.h"
#include "DynBuf.h"
#include "Fifo.h"
#include "Logging.h"
#include "KMod.h"
#include "OlyUtility.h"
#include "Proc.h"
#include "Sender.h"
#include "SessionData.h"
#include "lib/Utils.h"

DriverSource::DriverSource(Child & child, sem_t & senderSem, sem_t & startProfile, FtraceDriver & ftraceDriver)
        : Source(child),
          mBuffer(NULL),
          mFifo(NULL),
          mSenderSem(senderSem),
          mStartProfile(startProfile),
          mBufferSize(0),
          mBufferFD(0),
          mLength(1),
          mFtraceDriver(ftraceDriver)
{
    mBuffer = new PerfAttrsBuffer(4 * 1024 * 1024, &senderSem);
    KMod::checkVersion();

    int enable = -1;
    if (lib::readIntFromFile("/dev/gator/enable", enable) != 0 || enable != 0) {
        logg.logError("Driver already enabled, possibly a session is already in progress.");
        handleException();
    }

    if (lib::readIntFromFile("/dev/gator/buffer_size", mBufferSize) || mBufferSize <= 0) {
        logg.logError("Unable to read the driver buffer size");
        handleException();
    }
}

DriverSource::~DriverSource()
{
    delete mFifo;

    // Write zero for safety, as a zero should have already been written
    lib::writeCStringToFile("/dev/gator/enable", "0");

    // Calls event_buffer_release in the driver
    if (mBufferFD) {
        close(mBufferFD);
    }
}

bool DriverSource::prepare()
{
    // Create user-space buffers, add 5 to the size to account for the 1-byte type and 4-byte length
    logg.logMessage("Created %d MB collector buffer with a %d-byte ragged end", gSessionData.mTotalBufferSize, mBufferSize);
    mFifo = new Fifo(mBufferSize + 5, gSessionData.mTotalBufferSize * 1024 * 1024, &mSenderSem);

    return true;
}

void DriverSource::bootstrapThread()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-proc"), 0, 0, 0);

    DynBuf printb;
    DynBuf b1;

    // MonotonicStarted may not be not assigned yet
    const uint64_t currTime = 0; //getTime() - gSessionData.mMonotonicStarted;

    if (!readProcSysDependencies(currTime, *mBuffer, &printb, &b1, mFtraceDriver)) {
        logg.logError("readProcSysDependencies failed");
        handleException();
    }

    mBuffer->commit(currTime);
    mBuffer->setDone();
}

void *DriverSource::bootstrapThreadStatic(void *arg)
{
    static_cast<DriverSource *>(arg)->bootstrapThread();
    return NULL;
}

void DriverSource::run()
{
    // Get the initial pointer to the collect buffer
    char *collectBuffer = mFifo->start();
    int bytesCollected = 0;

    logg.logMessage("********** Profiling started **********");

    // Set the maximum backtrace depth
    if (lib::writeReadIntInFile("/dev/gator/backtrace_depth", gSessionData.mBacktraceDepth)) {
        logg.logError("Unable to set the driver backtrace depth");
        handleException();
    }

    // open the buffer which calls userspace_buffer_open() in the driver
    mBufferFD = open("/dev/gator/buffer", O_RDONLY | O_CLOEXEC);
    if (mBufferFD < 0) {
        logg.logError("The gator driver did not set up properly. Please view the linux console or dmesg log for more information on the failure.");
        handleException();
    }

    // set the tick rate of the profiling timer
    if (lib::writeReadIntInFile("/dev/gator/tick", gSessionData.mSampleRate) != 0) {
        logg.logError("Unable to set the driver tick");
        handleException();
    }

    // notify the kernel of the response type
    const ResponseType response_type = gSessionData.mLocalCapture ? ResponseType::NONE : ResponseType::APC_DATA;
    if (lib::writeIntToFile("/dev/gator/response_type", static_cast<int>(response_type))) {
        logg.logError("Unable to write the response type");
        handleException();
    }

    // Set the live rate
    if (lib::writeReadInt64InFile("/dev/gator/live_rate", gSessionData.mLiveRate)) {
        logg.logError("Unable to set the driver live rate");
        handleException();
    }

    logg.logMessage("Start the driver");

    // This command makes the driver start profiling by calling gator_op_start() in the driver
    if (lib::writeCStringToFile("/dev/gator/enable", "1") != 0) {
        logg.logError("The gator driver did not start properly. Please view the linux console or dmesg log for more information on the failure.");
        handleException();
    }

    lseek(mBufferFD, 0, SEEK_SET);

    sem_post(&mStartProfile);

    pthread_t bootstrapThreadID;
    if (pthread_create(&bootstrapThreadID, NULL, bootstrapThreadStatic, this) != 0) {
        logg.logError("Unable to start the gator_bootstrap thread");
        handleException();
    }

    // Collect Data
    do {
        // This command will stall until data is received from the driver
        // Calls event_buffer_read in the driver
        errno = 0;
        bytesCollected = read(mBufferFD, collectBuffer, mBufferSize);

        // If read() returned due to an interrupt signal, re-read to obtain the last bit of collected data
        if (bytesCollected == -1 && errno == EINTR) {
            bytesCollected = read(mBufferFD, collectBuffer, mBufferSize);
        }

        // return the total bytes written
        logg.logMessage("Driver read of %d bytes", bytesCollected);

        // In one shot mode, stop collection once all the buffers are filled
        if (gSessionData.mOneShot && gSessionData.mSessionIsActive) {
            if (bytesCollected == -1 || mFifo->willFill(bytesCollected)) {
                logg.logMessage("One shot (gator.ko)");
                mChild.endSession();
            }
        }
        collectBuffer = mFifo->write(bytesCollected);
    } while (bytesCollected > 0);

    logg.logMessage("Exit collect data loop");

    pthread_join(bootstrapThreadID, NULL);
}

void DriverSource::interrupt()
{
    // This command should cause the read() function in collect() to return and stop the driver from profiling
    if (lib::writeCStringToFile("/dev/gator/enable", "0") != 0) {
        logg.logMessage("Stopping kernel failed");
    }
}

bool DriverSource::isDone()
{
    return mLength <= 0 && (mBuffer == NULL || mBuffer->isDone());
}

void DriverSource::write(ISender *sender)
{
    char *data = mFifo->read(&mLength);
    if (data != NULL) {
        // driver will frame the response with type if needed and length
        sender->writeData(data, mLength, ResponseType::RAW);
        mFifo->release();
        // Assume the summary packet is in the first block received from the driver
        gSessionData.mSentSummary = true;
    }
    if (mBuffer != NULL && !mBuffer->isDone()) {
        mBuffer->write(sender);
        if (mBuffer->isDone()) {
            PerfAttrsBuffer *buf = mBuffer;
            mBuffer = NULL;
            delete buf;
        }
    }
}

