/* Copyright (c) 2017 by ARM Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_PERCOREMIXEDFRAMEBUFFER_H
#define INCLUDE_NON_ROOT_PERCOREMIXEDFRAMEBUFFER_H

#include "Buffer.h"
#include "non_root/MixedFrameBuffer.h"

#include <map>
#include <memory>
#include <semaphore.h>

class Sender;

namespace non_root
{
    class PerCoreMixedFrameBuffer
    {
    public:

        typedef unsigned long core_type;

        PerCoreMixedFrameBuffer(int frameType, int bufferSize, sem_t & readerSem);

        bool anyFull() const;
        void setDone();
        bool allDone() const;
        void write(Sender * sender);

        MixedFrameBuffer & operator[] (core_type core);

    private:

        std::map<core_type, std::unique_ptr<Buffer>> buffers;
        std::map<core_type, std::unique_ptr<MixedFrameBuffer>> wrappers;
        sem_t & readerSem;
        int frameType;
        int bufferSize;
    };
}

#endif /* INCLUDE_NON_ROOT_PERCOREMIXEDFRAMEBUFFER_H */
