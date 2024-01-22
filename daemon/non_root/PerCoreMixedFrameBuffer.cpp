/* Copyright (C) 2017-2023 by Arm Limited. All rights reserved. */

// Define to adjust Buffer.h interface,
#define BUFFER_USE_SESSION_DATA
// must be before includes

#include "non_root/PerCoreMixedFrameBuffer.h"

#include "Buffer.h"
#include "CommitTimeChecker.h"
#include "ISender.h"
#include "SessionData.h"
#include "non_root/MixedFrameBuffer.h"

#include <memory>

#include <semaphore.h>

namespace non_root {
    PerCoreMixedFrameBuffer::PerCoreMixedFrameBuffer(int bufferSize_, sem_t & readerSem_)
        : readerSem(readerSem_), bufferSize(bufferSize_)
    {
    }

    bool PerCoreMixedFrameBuffer::anyFull() const
    {
        for (const auto & entry : buffers) {
            if (entry.second && entry.second->bytesAvailable() <= 0) {
                return true;
            }
        }

        return false;
    }

    void PerCoreMixedFrameBuffer::setDone()
    {
        for (auto & entry : buffers) {
            if (entry.second) {
                entry.second->setDone();
            }
        }
    }

    bool PerCoreMixedFrameBuffer::write(ISender & sender)
    {
        bool done = true;
        for (auto & entry : buffers) {
            if (entry.second) {
                done &= entry.second->write(sender);
            }
        }
        return done;
    }

    MixedFrameBuffer & PerCoreMixedFrameBuffer::operator[](core_type core)
    {
        auto & wrapperPtrRef = wrappers[core];

        if (wrapperPtrRef == nullptr) {
            auto & bufferPtrRef = buffers[core];
            if (bufferPtrRef == nullptr) {
                bufferPtrRef = std::make_unique<Buffer>(bufferSize, readerSem);
            }

            wrapperPtrRef =
                std::make_unique<MixedFrameBuffer>(*bufferPtrRef, CommitTimeChecker {gSessionData.mLiveRate});
        }

        return *wrapperPtrRef;
    }
}
