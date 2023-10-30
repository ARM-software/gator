/* Copyright (C) 2023 by Arm Limited. All rights reserved. */

#pragma once

#include "armnn/ICaptureController.h"
#include "armnn/ICounterConsumer.h"
#include "armnn/IStartStopHandler.h"

#include <condition_variable>
#include <mutex>

namespace armnn {

    /**
     * Handles activation of data collection session when using the ArmNN agent worker.
     *
     * In this model, ArmNN connections only happen during the capture inside the gatord-child process,
     * along with the agent worker process.
     */
    class DriverSourceWithAgent : public ICounterConsumer, public ICaptureController {
    public:
        /**
         * @param startStopHandler (Driver.h relies on startStopHandler not being accessed in this constructor).
         */
        DriverSourceWithAgent(ICaptureStartStopHandler & startStopHandler);

        DriverSourceWithAgent(DriverSourceWithAgent const &) = delete;
        DriverSourceWithAgent(DriverSourceWithAgent &&) noexcept = delete;
        DriverSourceWithAgent & operator=(DriverSourceWithAgent const &) = delete;
        DriverSourceWithAgent & operator=(DriverSourceWithAgent &&) noexcept = delete;

        // these three are needed by DriverSourceIpc for its parent->child handling, but are not relevant when using agent workers

        void prepareForFork()
        {
            // ignored
        }

        void afterFork()
        {
            // ignored
        }

        void onChildDeath()
        {
            // ignored
        }

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
        // RAII helper object that sets and unsets the mSessionCounterConsumer field at the start and end of the run function
        class ActiveSessionSetUnset {
        public:
            explicit ActiveSessionSetUnset(DriverSourceWithAgent & driverSource, ICounterConsumer & counterConsumer);

            ActiveSessionSetUnset(ActiveSessionSetUnset const &) = delete;
            ActiveSessionSetUnset(ActiveSessionSetUnset &&) noexcept = delete;
            ActiveSessionSetUnset & operator=(ActiveSessionSetUnset const &) = delete;
            ActiveSessionSetUnset & operator=(ActiveSessionSetUnset &&) noexcept = delete;

            ~ActiveSessionSetUnset() noexcept;

        private:
            DriverSourceWithAgent & mDriverSource;
        };

        friend class ActiveSessionSetUnset;

        std::mutex mSessionMutex {};
        std::condition_variable mSessionNotify {};
        ICaptureStartStopHandler & mArmnnController;
        ICounterConsumer * mSessionCounterConsumer = nullptr;
        bool mInterrupted = false;
        bool mBufferFull = false;
    };

}
