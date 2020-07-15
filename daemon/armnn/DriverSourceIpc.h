/* Copyright (C) 2020 by Arm Limited. All rights reserved. */

#pragma once

#include "IBuffer.h"
#include "armnn/ICaptureController.h"
#include "armnn/ICounterConsumer.h"
#include "armnn/IStartStopHandler.h"
#include "lib/AutoClosingFd.h"
#include "lib/Optional.h"
#include "lib/Span.h"

#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace armnn {

    using Bytes = lib::Span<const std::uint8_t>;
    using MutBytes = lib::Span<std::uint8_t>;

    class Pipe {
    public:
        Pipe(lib::AutoClosingFd && readFd, lib::AutoClosingFd && writeFd)
            : mReadFd {std::move(readFd)}, mWriteFd {std::move(writeFd)}
        {
        }

        Pipe()
            : mReadFd {},
              mWriteFd {} {

              };

        /// return false on failure, in which case this Pipe will be in an indeterminate state
        bool writeAll(Bytes buf);
        /// return false on failure, in which case this Pipe will be in an indeterminate state
        bool readAll(MutBytes buf);
        std::string toString();

    private:
        lib::AutoClosingFd mReadFd {};
        lib::AutoClosingFd mWriteFd {};
    };

    /**
     * Responsible for propagating control messages to start and stop a capture
     * from child process to gator-main
     *
     * Intended to be private within DriverSourceIpc, but is publically
     * visible to be unit testable.
     */
    class ChildToParentController {
    public:
        ChildToParentController();
        void startCapture();
        void stopCapture();
        void onChildDeath();
        bool consumeControlMsg(ICaptureStartStopHandler & armnnHandler);

    private:
        Pipe mChildToParent {};
        bool mCalledStart {false};
    };

    /**
     * Responsible for propagating messages containing counter information
     * to the child process.
     *
     * Intended to be private within DriverSourceIpc, but is publically
     * visible to be unit testable.
     */
    class ParentToChildCounterConsumer {
    public:
        ParentToChildCounterConsumer();

        bool readMessage(ICounterConsumer & destination);
        bool interruptReader();
        bool consumerCounterValue(std::uint64_t timestamp,
                                  ApcCounterKeyAndCoreNumber keyAndCore,
                                  std::uint32_t counterValue);

    private:
        Pipe mToChild {};
        bool readCounterStruct(ICounterConsumer & destination);
    };

    /**
     * Handles interprocess communication between the Driver in gator-main
     * (the parent process) and the Source in gator-child (the child process).
     *
     * gator-main uses the ICounterConsumer interface and handles requests via ICaptureStartStopHandler
     *
     * gator-child uses the ICaptureController interface and receives the forwarded counter values through
     * the IBlockCounterMessageConsumer passed to the run method.
     */
    class DriverSourceIpc : public ICounterConsumer, public ICaptureController {
    public:
        /**
         * @param startStopHandler (Driver.h relies on startStopHandler not being accessed in this constructor).
         */
        DriverSourceIpc(ICaptureStartStopHandler & startStopHandler);

        /**
         * To be called when the parent process is about to create
         * the child process.  This procedure sets up communication
         * channels to be used by the child.
         */
        void prepareForFork();

        /**
         * To be called within the parent's signal handler when it detects the
         * child has terminated.
         */
        void onChildDeath();

        // ICounterConsumer --------------------------------------------------

        /**
         * Used to transmit counter data from gator-main to gator-child (and
         * thereby to Streamline)
         */
        virtual bool consumerCounterValue(std::uint64_t timestamp,
                                          ApcCounterKeyAndCoreNumber keyAndCore,
                                          std::uint32_t counterValue) override;

        // ICaptureController -------------------------------------------------

        /**
         * Should be run within gator-child when a capture is initiated.
         */
        virtual void run(ICounterConsumer & counterConsumer) override;

        /**
         * To be called by gator-child when a running capture should be
         * stopped
         */
        virtual void interrupt() override;

    private:
        ChildToParentController mControlChannel {};
        lib::Optional<ParentToChildCounterConsumer> mCountersChannel {};
        std::thread mControlThread {};
        ICaptureStartStopHandler & mArmnnController;
        std::mutex mParentMutex {};
    };

}
