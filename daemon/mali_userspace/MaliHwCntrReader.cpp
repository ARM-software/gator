/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliHwCntrReader.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include "lib/Syscall.h"
#include "Logging.h"

namespace mali_userspace
{

#if defined(ANDROID) || defined(__ANDROID__)
/* We use _IOR_BAD/_IOW_BAD rather than _IOR/_IOW otherwise fails to compile with NDK-BUILD because of _IOC_TYPECHECK is defined, not because the paramter is invalid */
#define MALI_IOR(a,b,c)  _IOR_BAD(a, b, c)
#define MALI_IOW(a,b,c)  _IOW_BAD(a, b, c)
#define MALI_IOWR(a,b,c) _IOWR_BAD(a, b, c)
#else
#define MALI_IOR(a,b,c)  _IOR(a, b, c)
#define MALI_IOW(a,b,c)  _IOW(a, b, c)
#define MALI_IOWR(a,b,c) _IOWR(a, b, c)
#endif
#define MALI_IO(a,b)     _IO(a, b)

    /* --------------------------------------------------------------------- */

    namespace ddk_pre_r21
    {
        /** Message header */
        union kbase_uk_hwcnt_header {
            /* 32-bit number identifying the UK function to be called. */
            uint32_t id;
            /* The int return code returned by the called UK function. */
            uint32_t ret;
            /* Used to ensure 64-bit alignment of this union. Do not remove. */
            uint64_t sizer;
        };

        /** IOCTL parameters to check version */
        struct kbase_uk_hwcnt_reader_version_check_args {
            union kbase_uk_hwcnt_header header;

            uint16_t major;
            uint16_t minor;
            uint8_t  padding[4];
        };

        /** IOCTL parameters to set flags */
        struct kbase_uk_hwcnt_reader_set_flags {
            union kbase_uk_hwcnt_header header;

            uint32_t create_flags;
            uint32_t padding;
        };

        /** IOCTL parameters to configure reader */
        struct kbase_uk_hwcnt_reader_setup
        {
            union kbase_uk_hwcnt_header header;

            /* IN */
            uint32_t buffer_count;
            uint32_t jm_bm;
            uint32_t shader_bm;
            uint32_t tiler_bm;
            uint32_t mmu_l2_bm;

            /* OUT */
            int32_t  fd;
        };

        enum {
            /* Related to mali0 ioctl interface */
            LINUX_UK_BASE_MAGIC                 = 0x80,
            BASE_CONTEXT_CREATE_KERNEL_FLAGS    = 0x2,
            KBASE_FUNC_HWCNT_UK_FUNC_ID         = 512,
            KBASE_FUNC_HWCNT_READER_SETUP       = KBASE_FUNC_HWCNT_UK_FUNC_ID + 36,
            KBASE_FUNC_HWCNT_DUMP               = KBASE_FUNC_HWCNT_UK_FUNC_ID + 11,
            KBASE_FUNC_HWCNT_CLEAR              = KBASE_FUNC_HWCNT_UK_FUNC_ID + 12,
            KBASE_FUNC_SET_FLAGS                = KBASE_FUNC_HWCNT_UK_FUNC_ID + 18,
        };

        template<typename T>
        static int doMaliIoctl(int fd, T & arg)
        {
            union kbase_uk_hwcnt_header * hdr = &arg.header;
            const int cmd = _IOC(_IOC_READ | _IOC_WRITE, LINUX_UK_BASE_MAGIC, hdr->id, sizeof(T));

            if (lib::ioctl(fd, cmd, reinterpret_cast<unsigned long> (&arg))) {
                return -1;
            }
            if (hdr->ret) {
                return -1;
            }

            return 0;
        }

        /**
         * Attempt to talk to mali kernel module, check version of mali driver
         *
         * @param devFd
         * @return True if the module is supported pre-r21 version of kernel module
         */
        static bool version_check(int devFd)
        {
            struct kbase_uk_hwcnt_reader_version_check_args version_check;

            memset(&version_check, 0, sizeof(version_check));
            version_check.major = 0;
            version_check.minor = 0;

            if (doMaliIoctl(devFd, version_check) != 0) {
                logg.logMessage("MaliHwCntrReader: Failed setting ABI version ioctl - may be r21p0 or later...");
                return false;
            }
            else if (version_check.major < 10) {
                logg.logError("MaliHwCntrReader: Unsupported ABI version %u.%u", version_check.major, version_check.minor);
                return false;
            }

            logg.logMessage("MaliHwCntrReader: ABI version: %u.%u", version_check.major, version_check.minor);
            return true;
        }

        /**
         * Perform necessary initialisation to get hw counter reader handle
         *
         * @param devFd
         * @param bufferCount
         * @param jmBitmask
         * @param shaderBitmask
         * @param tilerBitmask
         * @param mmuL2Bitmask
         * @param failedDueToBufferCount
         * @param hwcntReaderFd
         * @return True if successful
         */
        static bool initialize(int devFd, uint32_t bufferCount, uint32_t jmBitmask, uint32_t shaderBitmask, uint32_t tilerBitmask, uint32_t mmuL2Bitmask,
                               /* OUT */ bool & failedDueToBufferCount,
                               /* OUT */ int & hwcntReaderFd)
        {
            // set the flags
            {
                struct kbase_uk_hwcnt_reader_set_flags flags;

                memset(&flags, 0, sizeof(flags));
                flags.header.id = KBASE_FUNC_SET_FLAGS;
                flags.create_flags = BASE_CONTEXT_CREATE_KERNEL_FLAGS;

                if (doMaliIoctl(devFd, flags) != 0) {
                    logg.logError("MaliHwCntrReader: Failed setting flags ioctl");
                    return false;
                }
            }

            // probe the hwcnt file descriptor
            {
                struct kbase_uk_hwcnt_reader_setup setup_args;

                memset(&setup_args, 0, sizeof(setup_args));
                setup_args.header.id = KBASE_FUNC_HWCNT_READER_SETUP;
                setup_args.buffer_count = bufferCount;
                setup_args.jm_bm = jmBitmask;
                setup_args.shader_bm = shaderBitmask;
                setup_args.tiler_bm = tilerBitmask;
                setup_args.mmu_l2_bm = mmuL2Bitmask;
                setup_args.fd = -1;

                if (doMaliIoctl(devFd, setup_args) != 0) {
                    if (setup_args.header.ret != 0) {
                        logg.logMessage("MaliHwCntrReader: Failed sending hwcnt reader ioctl. ret = %lu", static_cast<unsigned long>(setup_args.header.ret));
                    }
                    else {
                        logg.logMessage("MaliHwCntrReader: Failed sending hwcnt reader ioctl");
                    }
                    failedDueToBufferCount = true;
                    return false;
                }

                // we have a handle to the reader fd
                hwcntReaderFd = setup_args.fd;
            }

            return true;
        }
    }

    namespace ddk_post_r21
    {
        /** IOCTL parameters to check version */
        struct kbase_ioctl_version_check {
            uint16_t major;
            uint16_t minor;
        };

        /** IOCTL parameters to set flags */
        struct kbase_ioctl_set_flags {
            uint32_t create_flags;
        };

        /** IOCTL parameters to configure reader */
        struct kbase_ioctl_hwcnt_reader_setup
        {
            uint32_t buffer_count;
            uint32_t jm_bm;
            uint32_t shader_bm;
            uint32_t tiler_bm;
            uint32_t mmu_l2_bm;
        };

        enum {
            /* Related to mali0 ioctl interface */
            KBASE_IOCTL_TYPE                = 0x80,
            BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED = 0x2,
            KBASE_IOCTL_VERSION_CHECK       = MALI_IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check),
            KBASE_IOCTL_SET_FLAGS           = MALI_IOW(KBASE_IOCTL_TYPE, 1, struct kbase_ioctl_set_flags),
            KBASE_IOCTL_HWCNT_READER_SETUP  = MALI_IOW(KBASE_IOCTL_TYPE, 8, struct kbase_ioctl_hwcnt_reader_setup),
        };


        /**
         * Attempt to talk to mali kernel module, check version of mali driver
         *
         * @param devFd
         * @return True if the module is supported post-r21 version of kernel module
         */
        static bool version_check(int devFd)
        {
            struct kbase_ioctl_version_check version_check;

            version_check.major = 0;
            version_check.minor = 0;

            if (lib::ioctl(devFd, KBASE_IOCTL_VERSION_CHECK,  reinterpret_cast<unsigned long> (&version_check))) {
                logg.logError("MaliHwCntrReader: Failed setting ABI version ioctl");
                return false;
            }
            else if (version_check.major < 11) {
                logg.logError("MaliHwCntrReader: Unsupported ABI version %u.%u", version_check.major, version_check.minor);
                return false;
            }

            logg.logMessage("MaliHwCntrReader: ABI version: %u.%u", version_check.major, version_check.minor);
            return true;
        }

        /**
         * Perform necessary initialisation to get hw counter reader handle
         *
         * @param devFd
         * @param bufferCount
         * @param jmBitmask
         * @param shaderBitmask
         * @param tilerBitmask
         * @param mmuL2Bitmask
         * @param failedDueToBufferCount
         * @param hwcntReaderFd
         * @return True if successful
         */
        static bool initialize(int devFd, uint32_t bufferCount, uint32_t jmBitmask, uint32_t shaderBitmask, uint32_t tilerBitmask, uint32_t mmuL2Bitmask,
                              /* OUT */ bool & failedDueToBufferCount,
                              /* OUT */ int & hwcntReaderFd)
        {
            // set the flags
            {
                struct kbase_ioctl_set_flags flags;

                flags.create_flags = BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED;

                if (lib::ioctl(devFd, KBASE_IOCTL_SET_FLAGS,reinterpret_cast<unsigned long>(&flags))) {
                    logg.logError("MaliHwCntrReader: Failed setting flags ioctl");
                    return false;
                }
            }

            // probe the hwcnt file descriptor
            {
                struct kbase_ioctl_hwcnt_reader_setup setup_args;

                setup_args.buffer_count = bufferCount;
                setup_args.jm_bm = jmBitmask;
                setup_args.shader_bm = shaderBitmask;
                setup_args.tiler_bm = tilerBitmask;
                setup_args.mmu_l2_bm = mmuL2Bitmask;

                hwcntReaderFd = lib::ioctl(devFd, KBASE_IOCTL_HWCNT_READER_SETUP, reinterpret_cast<unsigned long>(&setup_args));
                if (hwcntReaderFd < 0) {
                    logg.logMessage("MaliHwCntrReader: Failed sending hwcnt reader ioctl");
                    failedDueToBufferCount = true;
                    return false;
                }
            }

            return true;
        }
    }

    /* --------------------------------------------------------------------- */

    namespace
    {
        enum
        {
            /* reader API version */
            HWCNT_READER_API                    = 1,

            /* The ids of ioctl commands for the reader interface */
            KBASE_HWCNT_READER                  = 0xBE,
            KBASE_HWCNT_READER_GET_HWVER        = MALI_IOR(KBASE_HWCNT_READER, 0x00, uint32_t),
            KBASE_HWCNT_READER_GET_BUFFER_SIZE  = MALI_IOR(KBASE_HWCNT_READER, 0x01, uint32_t),
            KBASE_HWCNT_READER_DUMP             = MALI_IOW(KBASE_HWCNT_READER, 0x10, uint32_t),
            KBASE_HWCNT_READER_CLEAR            = MALI_IOW(KBASE_HWCNT_READER, 0x11, uint32_t),
            KBASE_HWCNT_READER_GET_BUFFER       = MALI_IOR(KBASE_HWCNT_READER, 0x20, struct kbase_hwcnt_reader_metadata),
            KBASE_HWCNT_READER_PUT_BUFFER       = MALI_IOW(KBASE_HWCNT_READER, 0x21, struct kbase_hwcnt_reader_metadata),
            KBASE_HWCNT_READER_SET_INTERVAL     = MALI_IOW(KBASE_HWCNT_READER, 0x30, uint32_t),
            KBASE_HWCNT_READER_ENABLE_EVENT     = MALI_IOW(KBASE_HWCNT_READER, 0x40, uint32_t),
            KBASE_HWCNT_READER_DISABLE_EVENT    = MALI_IOW(KBASE_HWCNT_READER, 0x41, uint32_t),
            KBASE_HWCNT_READER_GET_API_VERSION  = MALI_IOW(KBASE_HWCNT_READER, 0xFF, uint32_t)
        };

        enum
        {
            PIPE_DESCRIPTOR_IN,   /**< The index of a pipe's input descriptor. */
            PIPE_DESCRIPTOR_OUT,  /**< The index of a pipe's output descriptor. */

            PIPE_DESCRIPTOR_COUNT /**< The number of descriptors forming a pipe. */
        };

        enum
        {
            POLL_DESCRIPTOR_SIGNAL,       /**< The index of the signal descriptor in poll fds array. */
            POLL_DESCRIPTOR_HWCNT_READER, /**< The index of the hwcnt reader descriptor in poll fds array. */

            POLL_DESCRIPTOR_COUNT         /**< The number of descriptors poll is waiting for. */
        };

        /** Write a single byte into the pipe to interrupt the reader thread */
        typedef char poll_data_t;
    }

    MaliHwCntrReader::MaliHwCntrReader(const MaliDevice& device_, unsigned mmul2count_, uint32_t bufferCount_, CounterBitmask jmBitmask, CounterBitmask shaderBitmask,
                               CounterBitmask tilerBitmask, CounterBitmask mmuL2Bitmask)

        :   device(device_),
            devFd(-1),
            hwcntReaderFd(-1),
            sampleMemory(nullptr),
            bufferCount(bufferCount_),
            sampleBufferSize(0),
            hardwareVersion(0),
            mmuL2BlockCount(mmul2count_),
            selfPipe(),
            initialized(false),
            failedDueToBufferCount(false)
    {
        // pipes are not configured yet
        selfPipe[0] = -1; selfPipe[1] = -1;

        // open the device
        devFd = lib::open(device.getDevicePath().c_str(), O_RDWR|O_CLOEXEC|O_NONBLOCK);
        if (devFd == -1) {
            logg.logError("MaliHwCntrReader: Failed to open mali device '%s' due to '%s'", device.getDevicePath().c_str(), strerror(errno));
            return;
        }

        // query/set the ABI version and initialize handle to hw counter reader
        if (ddk_pre_r21::version_check(devFd))
        {
            if (!ddk_pre_r21::initialize(devFd, bufferCount, jmBitmask, shaderBitmask, tilerBitmask, mmuL2Bitmask, failedDueToBufferCount, hwcntReaderFd)) {
                return;
            }
        }
        else if (ddk_post_r21::version_check(devFd))
        {
            if (!ddk_post_r21::initialize(devFd, bufferCount, jmBitmask, shaderBitmask, tilerBitmask, mmuL2Bitmask, failedDueToBufferCount, hwcntReaderFd)) {
                return;
            }
        }
        else
        {
            return;
        }

        // verify the API version
        {
            uint32_t api_version = ~HWCNT_READER_API;
            if (lib::ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_API_VERSION, reinterpret_cast<unsigned long> (&api_version)) != 0) {
                logg.logError("MaliHwCntrReader: Could not determine hwcnt reader api version due to ioctl failure (%s)", strerror(errno));
                return;
            }
            else if (api_version != HWCNT_READER_API) {
                logg.logError("MaliHwCntrReader: Invalid API version (%lu)", static_cast<unsigned long>(api_version));
                return;
            }
        }

        // get sample buffer size
        if (lib::ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_BUFFER_SIZE, reinterpret_cast<unsigned long> (&sampleBufferSize)) != 0) {
            logg.logError("MaliHwCntrReader: Could not determine hwcnt reader sample buffer size due to ioctl failure (%s)", strerror(errno));
            return;
        }

        // get hardware version
        if (lib::ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_HWVER, reinterpret_cast<unsigned long> (&hardwareVersion)) != 0) {
            logg.logError("MaliHwCntrReader: Could not determine hwcnt reader hardware version due to ioctl failure (%s)", strerror(errno));
            return;
        }

        if ((hardwareVersion < 4) || (hardwareVersion > 6)) {
            logg.logError("MaliHwCntrReader: Hardware version %u is not supported", hardwareVersion);
            return;
        }

        if ((hardwareVersion > 4) && (mmuL2BlockCount < 1)) {
            logg.logError("MaliHwCntrReader: Hardware version %u detected, but mmuL2BlockCount = %u", hardwareVersion, mmuL2BlockCount);
            return;
        }

        // mmap the data
        sampleMemory = reinterpret_cast<uint8_t *>(lib::mmap(nullptr, bufferCount_ * sampleBufferSize, PROT_READ, MAP_PRIVATE, hwcntReaderFd, 0));
        if ((sampleMemory == nullptr) || (sampleMemory == reinterpret_cast<uint8_t *>(-1ul))) {
            logg.logMessage("MaliHwCntrReader: Could not mmap sample buffer");
            failedDueToBufferCount = true;
            return;
        }

        // create the thread notification pipe
        if (pipe(selfPipe) != 0) {
            logg.logError("MaliHwCntrReader: Could not create pipe (%s)", strerror(errno));
            return;
        }

        // set initialized
        initialized = true;
    }

    MaliHwCntrReader::~MaliHwCntrReader()
    {

        initialized = false;
        if (selfPipe[0] != -1) {
            close(selfPipe[0]);
        }
        if (selfPipe[1] != -1) {
            close(selfPipe[1]);
        }
        if (sampleMemory != nullptr) {
            lib::munmap(sampleMemory, bufferCount * sampleBufferSize);
        }
        if (hwcntReaderFd != -1) {
            lib::close(hwcntReaderFd);
        }
        if (devFd != -1) {
            lib::close(devFd);
        }
    }

    bool MaliHwCntrReader::triggerCounterRead()
    {
        if (!initialized) {
            return false;
        }

        return ioctl(hwcntReaderFd, KBASE_HWCNT_READER_DUMP, 0) == 0;
    }

    bool MaliHwCntrReader::startPeriodicSampling(uint32_t interval)
    {
        if (!initialized) {
            return false;
        }

        if (lib::ioctl(hwcntReaderFd, KBASE_HWCNT_READER_SET_INTERVAL, interval) != 0) {
            return false;
        }

        return true;
    }

    bool MaliHwCntrReader::configureJobBasedSampled(bool preJob, bool postJob)
    {
        if (!initialized) {
            return false;
        }

        if (ioctl(hwcntReaderFd, (preJob ? KBASE_HWCNT_READER_ENABLE_EVENT : KBASE_HWCNT_READER_DISABLE_EVENT), HWCNT_READER_EVENT_PREJOB) != 0) {
            return false;
        }

        if (ioctl(hwcntReaderFd, (postJob ? KBASE_HWCNT_READER_ENABLE_EVENT : KBASE_HWCNT_READER_DISABLE_EVENT), HWCNT_READER_EVENT_POSTJOB) != 0) {
            return false;
        }

        return true;
    }

    SampleBuffer MaliHwCntrReader::waitForBuffer(int timeout)
    {
        SampleBuffer temp;
        if (!initialized) {
            logg.logError("MaliHwCntrReader::waitForBuffer - not initialized");
            temp.status = WAIT_STATUS_ERROR;
            return temp;
        }

        // poll for any updates
        pollfd fds[POLL_DESCRIPTOR_COUNT];
        fds[POLL_DESCRIPTOR_SIGNAL].fd = selfPipe[PIPE_DESCRIPTOR_IN];
        fds[POLL_DESCRIPTOR_SIGNAL].events = POLLIN;
        fds[POLL_DESCRIPTOR_SIGNAL].revents = 0;
        fds[POLL_DESCRIPTOR_HWCNT_READER].fd = hwcntReaderFd;
        fds[POLL_DESCRIPTOR_HWCNT_READER].events = POLLIN;
        fds[POLL_DESCRIPTOR_HWCNT_READER].revents = 0;

        const int ready = lib::poll(fds, POLL_DESCRIPTOR_COUNT, timeout);

        // process result
        if (ready < 0) {
            // error occured
            logg.logError("MaliHwCntrReader::waitForBuffer - poll failed");
            temp.status = WAIT_STATUS_ERROR;
            return temp;
        }
        else if (ready == 0) {
            // clear buffer
            temp.status = WAIT_STATUS_SUCCESS;
            return temp;
        }
        else if (fds[POLL_DESCRIPTOR_SIGNAL].revents != 0) {
            // read the data from the pipe if necessary
            if ((fds[POLL_DESCRIPTOR_SIGNAL].revents & POLLIN) == POLLIN) {
                // read the data
                poll_data_t value;
                int result = read(selfPipe[PIPE_DESCRIPTOR_IN], &value, sizeof(value));
                if (result < 0) {
                    temp.status = WAIT_STATUS_SUCCESS;
                    return temp;
                }
            }

            // terminated
            temp.status = WAIT_STATUS_TERMINATED;
            return temp;
        }
        else if ((fds[POLL_DESCRIPTOR_HWCNT_READER].revents & POLLIN) == POLLIN) {
            // get the buffer
            kbase_hwcnt_reader_metadata metadata;
            if (lib::ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_BUFFER, reinterpret_cast<unsigned long> (&metadata)) != 0) {
                logg.logError("MaliHwCntrReader: Could not get buffer due to ioctl failure (%s)", strerror(errno));
                temp.status = WAIT_STATUS_ERROR;
                return temp;
            }
            temp.timestamp = metadata.timestamp;
            temp.eventId = metadata.event_id;
            temp.bufferId = metadata.buffer_idx;
            temp.size = sampleBufferSize;
            unique_ptr_with_deleter<uint8_t> data_temp(&(sampleMemory[sampleBufferSize * metadata.buffer_idx]), [=](uint8_t* )
                    {
                                                           kbase_hwcnt_reader_metadata metadata_tmp = metadata;
                                                           releaseBuffer(metadata_tmp);
                    });
            temp.data = std::move(data_temp);
            temp.status = WAIT_STATUS_SUCCESS;
            return temp;
        }
        else if ((fds[POLL_DESCRIPTOR_HWCNT_READER].revents & POLLHUP) == POLLHUP) {
            // terminated
            temp.status = WAIT_STATUS_TERMINATED;
            return temp;
        }
        else {
            // error occured
            logg.logError("MaliHwCntrReader::waitForBuffer - unexpected event 0x%x", fds[POLL_DESCRIPTOR_HWCNT_READER].revents);
            temp.status = WAIT_STATUS_ERROR;
            return temp;
        }
    }

    void MaliHwCntrReader::interrupt()
    {
        if (!initialized) {
            logg.logError("MaliHwCntrReader::interrupt - not initialized");
            return;
        }

        poll_data_t exit = 0;
        if (write(selfPipe[PIPE_DESCRIPTOR_OUT], &exit, sizeof(exit)) < 0) {
            logg.logError("MaliHwCntrReader::interrupt failed (%s)", strerror(errno));
        }
    }

    bool MaliHwCntrReader::releaseBuffer(kbase_hwcnt_reader_metadata & metadata)
    {
        return ioctl(hwcntReaderFd, KBASE_HWCNT_READER_PUT_BUFFER, &metadata) == 0;
    }

    unsigned MaliHwCntrReader::probeBlockMaskCount()
    {
        // version 4 hardware only ever has 1 MMU/L2 block
        if (hardwareVersion < 5) {
            return 1;
        }

        // probe it by looking for the block with the matching mask
        // have to retry as sometimes the counters read zero (including the mask)
        // until after *usually* the first read
        for (unsigned retry = 0; retry < 10; ++retry) {
            if (triggerCounterRead())
            {

                SampleBuffer waitStatus = waitForBuffer(10000);

                if ((waitStatus.status == WAIT_STATUS_SUCCESS) && waitStatus.data) {
                    const unsigned result = device.probeBlockMaskCount(reinterpret_cast<const uint32_t *>(waitStatus.data.get()), waitStatus.size / sizeof(uint32_t));
                    if (result != 0) {
                        return result;
                    }
                }
            }
        }

        return 0;
    }

    std::unique_ptr<MaliHwCntrReader> MaliHwCntrReader::create(const MaliDevice& device, unsigned mmul2count,
                                                               CounterBitmask jmBitmask_, CounterBitmask shaderBitmask_,
                                                               CounterBitmask tilerBitmask_,
                                                               CounterBitmask mmuL2Bitmask_)
    {
        // we do not know the best buffer count up front, so we have to test for it by repeatedly attempting to create the
        // reader until we succeed (or until some arbitrary limit)
        for (unsigned bufferCount = 1; bufferCount < 1024; ++bufferCount) {
            std::unique_ptr<MaliHwCntrReader>  result(new MaliHwCntrReader(device, mmul2count, bufferCount, jmBitmask_, shaderBitmask_,
                                                             tilerBitmask_, mmuL2Bitmask_));

            if (result->isInitialized()) {
                logg.logMessage("MaliHwCntrReader: Successfully created reader, with buffer size of %u", bufferCount);
                return result;
            }
            else if (!result->failedDueToBufferCount) {
                // the failure happened for some reason other than probing buffer count
                logg.logMessage("MaliHwCntrReader: Successfully created reader, but failedDueToBufferCount set to true");
                return result;
            }
        }
        return nullptr;
    }

    std::unique_ptr<MaliHwCntrReader>  MaliHwCntrReader::createReader(const MaliDevice& device)
    {
        // probe for the MMU/L2 block - on Freya there can be more than one.
        // we do this by setting its mask, and clearing the other masks for the other blocks
        // then looking for the only blocks that are set with non-zero mask

        std::unique_ptr<MaliHwCntrReader> reader = create(device, 1, 0, 0, 0, ~0u);
        if (!reader) {
            return nullptr;
        }
        //create actual reader..
        const unsigned mmul2count = reader->probeBlockMaskCount();
        return std::move(create(device, mmul2count, ~0u, ~0u, ~0u, ~0u));
    }

}
