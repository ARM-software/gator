/* Copyright (C) 2017-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_PERCOREMIXEDFRAMEBUFFER_H
#define INCLUDE_NON_ROOT_PERCOREMIXEDFRAMEBUFFER_H

#include "Buffer.h"
#include "non_root/MixedFrameBuffer.h"

#include <map>
#include <memory>
#include <semaphore.h>

class ISender;

namespace non_root {
    class PerCoreMixedFrameBuffer {
    public:
        using core_type = unsigned long;

        PerCoreMixedFrameBuffer(int bufferSize, sem_t & readerSem);

        bool anyFull() const;
        void setDone();
        bool write(ISender & sender);

        MixedFrameBuffer & operator[](core_type core);

    private:
        std::map<core_type, std::unique_ptr<Buffer>> buffers {};
        std::map<core_type, std::unique_ptr<MixedFrameBuffer>> wrappers {};
        sem_t & readerSem;
        int bufferSize;
    };
}

#endif /* INCLUDE_NON_ROOT_PERCOREMIXEDFRAMEBUFFER_H */
