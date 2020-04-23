/* Copyright (C) 2016-2020 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_

#include "lib/AutoClosingFd.h"
#include "mali_userspace/IMaliHwCntrReader.h"
#include "mali_userspace/MaliDevice.h"

#include <memory>
#include <stddef.h>
#include <stdint.h>

namespace mali_userspace {
    class MaliHwCntrReader;
    /**
     * Hardware counter reader
     */
    class MaliHwCntrReader : public IMaliHwCntrReader {
    public:
        virtual ~MaliHwCntrReader() = default;
        virtual const MaliDevice & getDevice() const override;
        virtual HardwareVersion getHardwareVersion() const override;
        virtual SampleBuffer waitForBuffer(int timeout) override;
        virtual bool startPeriodicSampling(uint32_t interval) override;

        /**
         * Get the size of hardware counters sample buffer.
         *
         * @return  The size of the hardware counters sample buffer
         */
        inline size_t getSampleSize() const { return sampleBufferSize; }

        /**
         * Trigger manual dump of hardware counters.
         *
         * Function triggers the hwcnt reader to collect values of the hardware counters.
         * Sample buffer with event identifier set will become available when dumping is
         * completed.
         *
         * @retval true     Success
         * @retval false    Failure
         */
        bool triggerCounterRead();

        /**
         * Initiate dumping of hardware counters, pre and post job.
         *
         * Functions triggers the hwcnt reader to dump counters before and after a job. Dumping will continue
         * until this function is called again with the appropriate event disabled.
         *
         * @param   preJob      When true, enables dumping before a job, when false disables dumping before a job
         * @param   postJob     When true, enables dumping after a job, when false disables dumping after a job
         *
         * @retval  true    Success
         * @retval  false   Failure
         */
        bool configureJobBasedSampled(bool preJob, bool postJob);

        /**
         * Interrupt a call to {@link #waitForBuffer(SampleBuffer &, int)} from another thread
         */
        void interrupt();
        /**
         * Create a new instance of the MaliHwCntrReader object associated with the device object
         *
         * @param device
         * @return The new reader, or NULL if not able to initialize
         */
        static std::unique_ptr<MaliHwCntrReader> createReader(const MaliDevice & device);

    private:
        using MmappedBuffer = std::unique_ptr<uint8_t[], std::function<void(uint8_t *)>>;

        /** Mali device object */
        const MaliDevice & device;
        /** File descriptor used to access vinstr client in kernel. */
        lib::AutoClosingFd hwcntReaderFd;
        /** Pipe to allow one thread to signal to poll to wake. Used to stop read. */
        lib::AutoClosingFd selfPipe[2];
        /** Sample capture memory */
        MmappedBuffer sampleMemory;
        /** Buffer count */
        uint32_t bufferCount;
        /** Size of a single sample buffer */
        uint32_t sampleBufferSize;
        /** Hardware version */
        uint32_t hardwareVersion;

        /**
         * Constructor
         */
        MaliHwCntrReader(const MaliDevice & device,
                         lib::AutoClosingFd hwcntReaderFd,
                         lib::AutoClosingFd selfPipe0,
                         lib::AutoClosingFd selfPipe1,
                         MmappedBuffer sampleMemory,
                         uint32_t bufferCount,
                         uint32_t sampleBufferSize,
                         uint32_t hardwareVersion);

        /**
         * Create a Mali HW Cntr reader by probing multiple times the mmu block
         *
         * @param device
         * @param jmBitmask
         * @param shaderBitmask
         * @param tilerBitmask
         * @param mmuL2Bitmask
         */
        static std::unique_ptr<MaliHwCntrReader> create(const MaliDevice & device,
                                                        CounterBitmask jmBitmask,
                                                        CounterBitmask shaderBitmask,
                                                        CounterBitmask tilerBitmask,
                                                        CounterBitmask mmuL2Bitmask);
        /**
         * Release hardware counters sampling buffer.
         *
         * Function releases buffer obtained with @ref waitForBuffer.
         *
         * @param metadata  The buffer metadata of the buffer to release
         *
         * @retval  true    Success
         * @retval  false   Failure
         */
        bool releaseBuffer(kbase_hwcnt_reader_metadata & metadata);

        /**
         * Probe the number of blocks on V5+ device with non-zero mask
         *
         * @return The number of matching blocks
         */
        unsigned probeBlockMaskCount();

        MaliHwCntrReader(const MaliHwCntrReader &) = delete;
        MaliHwCntrReader & operator=(const MaliHwCntrReader &) = delete;
        MaliHwCntrReader(MaliHwCntrReader &&) = delete;
        MaliHwCntrReader & operator=(MaliHwCntrReader &&) = delete;
    };
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_ */
