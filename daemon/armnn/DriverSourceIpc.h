/* Copyright (C) 2020-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/ICaptureController.h"
#include "armnn/ICounterConsumer.h"
#include "armnn/IStartStopHandler.h"
#include "lib/AutoClosingFd.h"
#include "lib/Span.h"

#include <functional>
#include <mutex>
#include <optional>
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

        Pipe() = default;

        /// return false on failure, in which case this Pipe will be in an indeterminate state
        bool writeAll(Bytes buf);
        /// return false on failure, in which case this Pipe will be in an indeterminate state
        bool readAll(MutBytes buf);
        std::string toString();

    private:
        lib::AutoClosingFd mReadFd;
        lib::AutoClosingFd mWriteFd;
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
        Pipe mChildToParent;
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

        bool readMessage(ICounterConsumer & destination,
                         bool isOneShot,
                         const std::function<unsigned int()> & getBufferBytesAvailable);
        bool interruptReader();
        bool consumeCounterValue(std::uint64_t timestamp,
                                 ApcCounterKeyAndCoreNumber keyAndCore,
                                 std::uint32_t counterValue);
        bool consumePacket(std::uint32_t sessionId, lib::Span<const std::uint8_t> data);

        /**
         * @returns whether one shot mode is enabled,
         *          and if the available size of the buffer is not large enough for the next packet
         */
        bool getOneShotModeEnabledAndEnded() const { return mOneShotModeEnabledAndEnded; }

    private:
        Pipe mToChild;
        bool mOneShotModeEnabledAndEnded;
        bool readCounterStruct(ICounterConsumer & destination);
        bool readPacket(ICounterConsumer & destination,
                        bool isOneShot,
                        const std::function<unsigned int()> & getBufferBytesAvailable);
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
         * the child process.  This procedure prepares the communication
         * channels to be used by the child.
         */
        void prepareForFork();

        /**
         * To be called when the parent process has created
         * the child process.  This procedure starts communication
         * channels to be used by the child.
         */
        void afterFork();

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
        bool consumeCounterValue(std::uint64_t timestamp,
                                 ApcCounterKeyAndCoreNumber keyAndCore,
                                 std::uint32_t counterValue) override;

        /**
         * @return whether the data was successfully consumed
         */
        bool consumePacket(std::uint32_t sessionId, lib::Span<const std::uint8_t> data) override;

        // ICaptureController -------------------------------------------------

        /**
         * Should be run within gator-child when a capture is initiated.
         */
        void run(ICounterConsumer & counterConsumer,
                 bool isOneShot,
                 std::function<void()> endSession,
                 std::function<unsigned int()> getBufferBytesAvailable) override;

        /**
         * To be called by gator-child when a running capture should be
         * stopped
         */
        void interrupt() override;

    private:
        ChildToParentController mControlChannel;
        std::optional<ParentToChildCounterConsumer> mCountersChannel;
        std::thread mControlThread;
        ICaptureStartStopHandler & mArmnnController;
        std::mutex mParentMutex;
    };

}
