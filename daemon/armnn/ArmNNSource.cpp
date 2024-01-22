/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */

// Define to adjust Buffer.h interface,
#define BUFFER_USE_SESSION_DATA
// must be before includes

#include "armnn/ArmNNSource.h"

#include "Buffer.h"
#include "SessionData.h"
#include "Source.h"
#include "armnn/FrameBuilderFactory.h"
#include "armnn/ICaptureController.h"
#include "armnn/TimestampCorrector.h"

#include <cstdint>
#include <functional>
#include <memory>

#include <semaphore.h>

namespace armnn {
    class Source : public ::Source {
    public:
        Source(ICaptureController & captureController, sem_t & readerSem)
            : captureController(captureController), buffer(gSessionData.mTotalBufferSize * 1024 * 1024, readerSem)
        {
        }

        void run(std::uint64_t monotonicStarted, std::function<void()> endSession) override
        {
            auto builder = FrameBuilderFactory {buffer, gSessionData.mLiveRate};
            TimestampCorrector timestampCorrector {builder, monotonicStarted};
            std::function<unsigned int(void)> f_bufferBytesAvailable = [&] {
                return (unsigned int) buffer.bytesAvailable();
            };
            captureController.run(timestampCorrector, gSessionData.mOneShot, endSession, f_bufferBytesAvailable);
            buffer.setDone();
        }

        void interrupt() override { captureController.interrupt(); }

        bool write(::ISender & sender) override { return buffer.write(sender); }

    private:
        ICaptureController & captureController;
        Buffer buffer;
    };

    std::shared_ptr<::Source> createSource(ICaptureController & captureController, sem_t & readerSem)
    {
        return std::make_shared<Source>(captureController, readerSem);
    }
}
