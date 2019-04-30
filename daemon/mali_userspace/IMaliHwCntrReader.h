/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#ifndef MALI_USERSPACE_IMALIHWCNTRREADER_H_
#define MALI_USERSPACE_IMALIHWCNTRREADER_H_

#include <cstdint>
#include "MaliDevice.h"
#include <memory>
#include <functional>

namespace mali_userspace
{
    struct kbase_hwcnt_reader_metadata
    {
        uint64_t timestamp; /**< Time when sample was collected. */
        uint32_t event_id; /**< Id of an event that triggered sample collection. */
        uint32_t buffer_idx; /**< Position in sampling area where sample buffer was stored. */

        kbase_hwcnt_reader_metadata()
                : timestamp(0),
                  event_id(0),
                  buffer_idx(0)
        {
        }

        kbase_hwcnt_reader_metadata(uint64_t timestamp_, uint32_t event_id_, uint32_t buffer_idx_)
                : timestamp(timestamp_),
                  event_id(event_id_),
                  buffer_idx(buffer_idx_)
        {
        }
    };
    class MaliHwCntrReader;
    /** Wait status result */
    typedef enum
    {
        WAIT_STATUS_ERROR, /**< The wait failed due to error */
        WAIT_STATUS_SUCCESS, /**< The wait succeeded and buffer contains data */
        WAIT_STATUS_TERMINATED /**< The wait ended as the connection was terminated */
    } WaitStatus;

    template<typename T>
    using unique_ptr_with_deleter = std::unique_ptr<T,std::function<void(T*)>>;

    struct SampleBuffer {

        WaitStatus status = WaitStatus::WAIT_STATUS_ERROR;
        uint64_t timestamp = 0;
        uint32_t eventId = 0;
        uint32_t bufferId = 0;
        size_t size = 0;
        unique_ptr_with_deleter<uint8_t> data= nullptr;
    };


    class IMaliHwCntrReader
    {

    public:
        virtual ~IMaliHwCntrReader() = default;


        typedef uint32_t CounterBitmask;
        typedef uint32_t HardwareVersion;

        /** Hwcnt dumping events. */
        typedef enum
        {
            HWCNT_READER_EVENT_MANUAL, /**< Manual request for dump. */
            HWCNT_READER_EVENT_PERIODIC, /**< Periodic dump. */
            HWCNT_READER_EVENT_PREJOB, /**< Prejob dump request. */
            HWCNT_READER_EVENT_POSTJOB, /**< Postjob dump request. */

            HWCNT_READER_EVENT_COUNT /**< Number of supported events. */
        } HwcntReaderEvent;

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
         * @param   timeout Number of milliseconds function shall wait for sample buffer.
         *                  Specifying negative value means infinite wait. If zero function
         *                  will return immediately.
         * @return SampleBuffer which is a struct with informations about the buffer as well as the wait status
         *
         * WAIT_STATUS_ERROR       The wait failed due to error
         * WAIT_STATUS_SUCCES      The wait succeeded and buffer contains data (but may not be valid if timeout == 0)
         * WAIT_STATUS_TERMINATED  The wait ended as the connection was terminated
         */
        virtual SampleBuffer waitForBuffer(int timeout)= 0;
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
        virtual bool startPeriodicSampling(uint32_t interval) = 0;

        /**
         * Returns the mali device associated with the reader
         */
        virtual const MaliDevice & getDevice() const = 0;

        /**
         * Get the architecture version of the hardware counters.
         *
         * @return  Architecture version of the hardware counters, or 0 if not available
         */
        virtual HardwareVersion getHardwareVersion() const = 0;

        /**
         * @return The number of mmu/l2 blocks
         */
        virtual unsigned getMmuL2BlockCount() const = 0;

    };
}  // namespace

#endif /* MALI_USERSPACE_IMALIHWCNTRREADER_H_ */
