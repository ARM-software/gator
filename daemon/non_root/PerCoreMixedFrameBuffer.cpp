/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "non_root/PerCoreMixedFrameBuffer.h"
#include "Logging.h"
#include "ISender.h"

namespace non_root
{
    PerCoreMixedFrameBuffer::PerCoreMixedFrameBuffer(FrameType frameType_, int bufferSize_, sem_t & readerSem_)
            : buffers(),
              wrappers(),
              readerSem(readerSem_),
              frameType(frameType_),
              bufferSize(bufferSize_)
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

    bool PerCoreMixedFrameBuffer::allDone() const
    {
        for (const auto & entry : buffers) {
            if (entry.second && !entry.second->isDone()) {
                return false;
            }
        }

        return true;
    }

    void PerCoreMixedFrameBuffer::write(ISender * sender)
    {
        for (auto & entry : buffers) {
            if (entry.second && !entry.second->isDone()) {
                entry.second->write(sender);
            }
        }
    }

    MixedFrameBuffer & PerCoreMixedFrameBuffer::operator[](core_type core)
    {
        auto & wrapperPtrRef = wrappers[core];

        if (wrapperPtrRef == nullptr) {
            auto & bufferPtrRef = buffers[core];
            if (bufferPtrRef == nullptr) {
                bufferPtrRef.reset(new Buffer(core, frameType, bufferSize, &readerSem));
            }

            wrapperPtrRef.reset(new MixedFrameBuffer(*bufferPtrRef));
        }

        return *wrapperPtrRef;
    }
}
