/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#define BUFFER_USE_SESSION_DATA

#include "armnn/Source.h"

#include "armnn/TimestampCorrector.h"

#include <utility>

namespace armnn {
    Source::Source(Child & child,
                   ICaptureController & captureController,
                   sem_t & readerSem,
                   std::function<std::int64_t()> getMonotonicStarted)
        : ::Source(child),
          captureController(captureController),
          buffer(0, FrameType::BLOCK_COUNTER, gSessionData.mTotalBufferSize * 1024 * 1024, readerSem),
          mGetMonotonicStarted {std::move(getMonotonicStarted)}
    {
    }

    bool Source::prepare()
    {
        // nothing to do
        return true;
    }

    void Source::run()
    {
        TimestampCorrector timestampCorrector {buffer, mGetMonotonicStarted};
        captureController.run(timestampCorrector);
        buffer.setDone();
    }

    void Source::interrupt() { captureController.interrupt(); }

    bool Source::isDone() { return buffer.isDone(); }

    void Source::write(::ISender & sender) { buffer.write(sender); }
}
