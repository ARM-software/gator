/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIHWCNTRREADER_H_

#include <stddef.h>
#include <stdint.h>

#include "ClassBoilerPlate.h"
#include "mali_userspace/MaliDevice.h"

namespace mali_userspace
{
    class MaliHwCntrReader;

    /** Hwcnt reader sample buffer metadata. */
    struct kbase_hwcnt_reader_metadata
    {
        uint64_t timestamp;  /**< Time when sample was collected. */
        uint32_t event_id;   /**< Id of an event that triggered sample collection. */
        uint32_t buffer_idx; /**< Position in sampling area where sample buffer was stored. */

        kbase_hwcnt_reader_metadata()
            :   timestamp(0),
                event_id(0),
                buffer_idx(0)
        {
        }

        kbase_hwcnt_reader_metadata(uint64_t timestamp_, uint32_t event_id_, uint32_t buffer_idx_)
            :   timestamp(timestamp_),
                event_id(event_id_),
                buffer_idx(buffer_idx_)
        {
        }
    };

    /**
     * Represents a single sample object read from the counter interface
     */
    class SampleBuffer
    {
    public:

        /**
         * Constructors
         */
        SampleBuffer();
        SampleBuffer(SampleBuffer &&);

        /** Destructor */
        ~SampleBuffer();

        /** move assignable */
        SampleBuffer& operator= (SampleBuffer &&);

        /** @return True if the buffer is valid */
        inline bool isValid() const { return data != NULL; }
        /** @return The buffer index */
        inline uint32_t getBufferId() const { return metadata.buffer_idx; }
        /** @return The event that generated the sample */
        inline uint32_t getEventId() const { return metadata.event_id; }
        /** @return The timestamp of the event */
        inline uint64_t getTimestamp() const { return metadata.timestamp; }
        /** @return The size of the sample data */
        inline size_t getSize() const { return data_size; }
        /** @return The sample data */
        inline const uint8_t * getData() const { return data; }

    private:

        kbase_hwcnt_reader_metadata metadata;
        MaliHwCntrReader * parent;
        size_t data_size;
        const uint8_t * data;

        /** Only MaliHwCntrReader can construct a SampleBuffer */
        friend class MaliHwCntrReader;

        /**
         * Constructor
         *
         * @param parent_       The MaliHwCntrReader that created this object
         * @param timestamp_    The timestamp of the event
         * @param eventId_      The event that generated the sample
         * @param bufferId_     The buffer index
         * @param size_         The size of the sample data
         * @param data_         The sample data
         */
        SampleBuffer(MaliHwCntrReader & parent_, uint64_t timestamp_, uint32_t eventId_, uint32_t bufferId_, size_t size_, const uint8_t * data_);

        CLASS_DELETE_COPY(SampleBuffer);
    };

    /**
     * Hardware counter reader
     */
    class MaliHwCntrReader
    {
    public:

        typedef uint32_t CounterBitmask;
        typedef uint32_t HardwareVersion;

        /** Hwcnt dumping events. */
        typedef enum
        {
            HWCNT_READER_EVENT_MANUAL,   /**< Manual request for dump. */
            HWCNT_READER_EVENT_PERIODIC, /**< Periodic dump. */
            HWCNT_READER_EVENT_PREJOB,   /**< Prejob dump request. */
            HWCNT_READER_EVENT_POSTJOB,  /**< Postjob dump request. */

            HWCNT_READER_EVENT_COUNT     /**< Number of supported events. */
        } HwcntReaderEvent;

        /** Wait status result */
        typedef enum {
            WAIT_STATUS_ERROR,          /**< The wait failed due to error */
            WAIT_STATUS_SUCCESS,         /**< The wait succeeded and buffer contains data */
            WAIT_STATUS_TERMINATED      /**< The wait ended as the connection was terminated */
        } WaitStatus;

        /**
         * Probe the number of MMU blocks on V5+ device
         *
         * @param device
         * @return The number of MMU blocks
         */
        static unsigned probeMMUCount(const MaliDevice * device);

        /**
         * Create a new instance of the MaliHwCntrReader object associated with the device object
         *
         * @param device
         * @return The new reader, or NULL if not able to initialize
         */
        static MaliHwCntrReader * create(const MaliDevice * device);

        /**
         * Free an old reader object, but retain the device object that it held (which would otherwise be deleted with the reader)
         *
         * @param oldReader The old reader to release
         * @return The device the reader was attached to
         */
        static const MaliDevice * freeReaderRetainDevice(MaliHwCntrReader * oldReader);


        /** Destructor */
        ~MaliHwCntrReader();

        /**
         * @return The device object
         */
        inline const MaliDevice & getDevice() const
        {
            return *device;
        }

        /**
         * @return True if the reader initialized successfully and is able to provide counter information
         */
        inline bool isInitialized() const
        {
            return initialized;
        }

        /**
         * Get the architecture version of the hardware counters.
         *
         * @return  Architecture version of the hardware counters, or 0 if not available
         */
        inline HardwareVersion getHardwareVersion() const
        {
            return hardwareVersion;
        }

        /**
         * @return The number of mmu/l2 blocks
         */
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

        /**
         * Initiate periodic dumping of hardware counters.
         *
         * Function triggers hwcnt reader to periodically collect values of hardware counters.
         * Sample buffer with event identifier set will become available when dumping is
         * completed. Periodic dumping will continue until this function is called again
         * with interval set to zero or until hwcnt reader is terminated.
         *
         * @param   interval    Interval at which hardware counters shall be sampled (in nanoseconds).
         *                      If zero, periodic sampling will be stopped.
         *
         * @retval  true    Success
         * @retval  false   Failure
         *
         * @note In the case value of @p interval is lower then sampling resolution supported
         *       by hwcnt reader, the sampling interval will be set to the value of sampling resolution.
         *       User shall make no assumptions about the actual interval between samples and shall
         *       determine it by checking timestamps of obtained sample buffers.
         */
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

        /**
         * Obtain hardware counters sampling buffer.
         *
         * Function obtains the buffer from the head of the sample buffers queue. The obtained
         * buffer contains information about time when it was collected
         * and event that triggered the sampling. If no buffer is available, the function
         * will block for specified number of milliseconds or until a buffer is acquired.
         * In the case of timeout, the sample buffer will have its data pointer set to NULL.
         * In this case remaining members of sample buffer structure shall be considered invalid.
         *
         * @param   buffer  The result object to write into.
         * @param   timeout Number of milliseconds function shall wait for sample buffer.
         *                  Specifying negative value means infinite wait. If zero function
         *                  will return immediately.
         * @retval  WAIT_STATUS_ERROR       The wait failed due to error
         * @retval  WAIT_STATUS_SUCCES      The wait succeeded and buffer contains data (but may not be valid if timeout == 0)
         * @retval  WAIT_STATUS_TERMINATED  The wait ended as the connection was terminated
         */
        WaitStatus waitForBuffer(SampleBuffer & buffer, int timeout);

        /**
         * Interrupt a call to {@link #waitForBuffer(SampleBuffer &, int)} from another thread
         */
        void interrupt();

    private:

        /** Mali device object */
        const MaliDevice * device;
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

        /** The sample buffer may call putBuffer when it is destroyed */
        friend class SampleBuffer;

        /**
         * Create a new instance of the MaliHwCntrReader object associated with the device object
         *
         * @param device
         * @param mmul2count        The value returned by probeMMUCount
         * @param jmBitmask_        Counters selection bitmask (JM).
         * @param shaderBitmask_    Counters selection bitmask (Shader).
         * @param tilerBitmask_     Counters selection bitmask (Tiler).
         * @param mmuL2Bitmask_     Counters selection bitmask (MMU_L2).
         * @return The new reader, or NULL if not able to initialize
         */
        static MaliHwCntrReader * create(const MaliDevice * device, unsigned mmul2count, CounterBitmask jmBitmask_,
                                         CounterBitmask shaderBitmask_, CounterBitmask tilerBitmask_, CounterBitmask mmuL2Bitmask_);

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
        MaliHwCntrReader(const MaliDevice * device_, unsigned mmul2count, uint32_t bufferCount_, CounterBitmask jmBitmask_,
                               CounterBitmask shaderBitmask_, CounterBitmask tilerBitmask_, CounterBitmask mmuL2Bitmask_);

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
