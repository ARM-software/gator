/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_

#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "ClassBoilerPlate.h"
#include "mali_userspace/MaliDevice.h"
#include "mali_userspace/IMaliHwCntrReader.h"

namespace mali_userspace
{
    class MaliHwCntrReader;
    /**
     * Hardware counter reader
     */
    class MaliHwCntrReader : public IMaliHwCntrReader
    {
    public:


        /**
         * Probe the number of MMU blocks on V5+ device
         *
         * @param device
         * @return The number of MMU blocks
         */
        static unsigned probeMMUCount(const MaliDevice * device);

        /** Destructor */
        ~MaliHwCntrReader();

        /**
         * @return The device object
         */
        inline const MaliDevice & getDevice() const
        {
            return device;
        }

        /**
         * @return True if the reader initialized successfully and is able to provide counter information
         */
        inline bool isInitialized() const
        {
            return initialized;
        }

        inline HardwareVersion getHardwareVersion() const
        {
            return hardwareVersion;
        }

        inline unsigned getMmuL2BlockCount() const
        {
            return mmuL2BlockCount;
        }

        /**
         * Get the size of hardware counters sample buffer.
         *
         * @return  The size of the hardware counters sample buffer
         */
        inline size_t getSampleSize() const
        {
            return sampleBufferSize;
        }

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

        bool startPeriodicSampling(uint32_t interval);

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

        SampleBuffer waitForBuffer(int timeout);

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
        static std::unique_ptr<MaliHwCntrReader> createReader(const MaliDevice& device);

    private:

        /** Mali device object */
        const MaliDevice& device;
        /** Device file descriptor */
        int devFd;
        /** File descriptor used to access vinstr client in kernel. */
        int hwcntReaderFd;
        /** Sample capture memory */
        uint8_t * sampleMemory;
        /** Buffer count */
        uint32_t bufferCount;
        /** Size of a single sample buffer */
        uint32_t sampleBufferSize;
        /** Hardware version */
        uint32_t hardwareVersion;
        /** Number of mmu/l2 regions on v5+ */
        unsigned mmuL2BlockCount;
        /** Pipe to allow one thread to signal to poll to wake. Used to stop read. */
        int selfPipe[2];
        /** Set true if the reader was fully initialized */
        bool initialized;
        /** Set true if failure relates to probing buffer count */
        bool failedDueToBufferCount;


        /**
         * Constructor
         *
         * @param device_           The device to open
         * @param mmul2count        The value returned by probeMMUCount
         * @param bufferCount_      Number of buffers that this reader shall use. Must be a power of two.
         * @param jmBitmask_        Counters selection bitmask (JM).
         * @param shaderBitmask_    Counters selection bitmask (Shader).
         * @param tilerBitmask_     Counters selection bitmask (Tiler).
         * @param mmuL2Bitmask_     Counters selection bitmask (MMU_L2).
         */
        MaliHwCntrReader(const MaliDevice & device_, unsigned mmul2count, uint32_t bufferCount_, CounterBitmask jmBitmask_,
                               CounterBitmask shaderBitmask_, CounterBitmask tilerBitmask_, CounterBitmask mmuL2Bitmask_);

        /**
         * Create a Mali HW Cntr reader by probing multiple times the mmu block
         *
         * @param device
         * @param mmul2count
         * @param jmBitmask_
         * @param shaderBitmask_
         * @param tilerBitmask_
         * @param mmuL2Bitmask_
         */
        static std::unique_ptr<MaliHwCntrReader> create(const MaliDevice& device, unsigned mmul2count,
                                                        CounterBitmask jmBitmask_, CounterBitmask shaderBitmask_,
                                                        CounterBitmask tilerBitmask_, CounterBitmask mmuL2Bitmask_);
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

        CLASS_DELETE_COPY_MOVE(MaliHwCntrReader);
    };
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_ */
