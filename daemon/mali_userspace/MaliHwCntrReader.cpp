/* Copyright (c) 2016 by ARM Limited. All rights reserved. */

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

#include "Logging.h"

namespace mali_userspace
{
    namespace
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

        static const uint32_t HWCNT_READER_API = 1;

#if defined(ANDROID) || defined(__ANDROID__)
/* We use _IOR_BAD/_IOW_BAD rather than _IOR/_IOW otherwise fails to compile with NDK-BUILD because of _IOC_TYPECHECK is defined, not because the paramter is invalid */
#define MALI_IOR(a,b,c)  _IOR_BAD(a, b, c)
#define MALI_IOW(a,b,c)  _IOW_BAD(a, b, c)
#else
#define MALI_IOR(a,b,c)  _IOR(a, b, c)
#define MALI_IOW(a,b,c)  _IOW(a, b, c)
#endif

        enum {
            /* Related to mali0 ioctl interface */
            LINUX_UK_BASE_MAGIC                 = 0x80,
            BASE_CONTEXT_CREATE_KERNEL_FLAGS    = 0x2,
            KBASE_FUNC_HWCNT_UK_FUNC_ID         = 512,
            KBASE_FUNC_HWCNT_READER_SETUP       = KBASE_FUNC_HWCNT_UK_FUNC_ID + 36,
            KBASE_FUNC_HWCNT_DUMP               = KBASE_FUNC_HWCNT_UK_FUNC_ID + 11,
            KBASE_FUNC_HWCNT_CLEAR              = KBASE_FUNC_HWCNT_UK_FUNC_ID + 12,
            KBASE_FUNC_SET_FLAGS                = KBASE_FUNC_HWCNT_UK_FUNC_ID + 18,

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

    /* --------------------------------------------------------------------- */


    SampleBuffer::SampleBuffer()
        :   kbase_hwcnt_reader_metadata(0, 0, 0),
            parent(NULL),
            data_size(0),
            data(NULL)
    {
    }

    SampleBuffer::SampleBuffer(MaliHwCntrReader & parent_, uint64_t timestamp_, uint32_t eventId_, uint32_t bufferId_, size_t size_, const uint8_t * data_)
        :   kbase_hwcnt_reader_metadata(timestamp_, eventId_, bufferId_),
            parent(&parent_),
            data_size(size_),
            data(data_)
    {
    }

    SampleBuffer::~SampleBuffer()
    {
        if (parent != NULL) {
            parent->releaseBuffer(*this);
        }
    }

    template<typename T>
    static int doMaliIoctl(int fd, T & arg)
    {
        union kbase_uk_hwcnt_header * hdr = &arg.header;

        const int cmd = _IOC(_IOC_READ | _IOC_WRITE, LINUX_UK_BASE_MAGIC, hdr->id, sizeof(T));

        if (ioctl(fd, cmd, &arg))
            return -1;
        if (hdr->ret)
            return -1;

        return 0;
    }

    /* --------------------------------------------------------------------- */

    const MaliDevice * MaliHwCntrReader::freeReaderRetainDevice(MaliHwCntrReader * oldReader)
    {
        const MaliDevice * device = oldReader->device;

        oldReader->device = NULL;
        delete oldReader;

        return device;
    }

    MaliHwCntrReader::MaliHwCntrReader(const MaliDevice * device_, uint32_t bufferCount_, CounterBitmask jmBitmask, CounterBitmask shaderBitmask,
                               CounterBitmask tilerBitmask, CounterBitmask mmuL2Bitmask)

        :   device(device_),
            devFd(-1),
            hwcntReaderFd(-1),
            sampleMemory(NULL),
            bufferCount(bufferCount_),
            sampleBufferSize(0),
            hardwareVersion(0),
            selfPipe(),
            initialized(false)
    {
        // pipes are not configured yet
        selfPipe[0] = -1; selfPipe[1] = -1;

        // check device object
        if (device == NULL) {
            logg.logError("MaliHwCntrReader: Device object invalid");
            return;
        }

        // open the device
        devFd = open(device->getDevicePath(), O_RDWR|O_CLOEXEC|O_NONBLOCK);
        if (devFd == -1) {
            logg.logError("MaliHwCntrReader: Failed to open mali device '%s' due to '%s'", device->getDevicePath(), strerror(errno));
            return;
        }

        // query/set the ABI version to the current version
        {
            struct kbase_uk_hwcnt_reader_version_check_args version_check;

            memset(&version_check, 0, sizeof(version_check));
            version_check.major = 0;
            version_check.minor = 0;

            if (doMaliIoctl(devFd, version_check) != 0) {
                logg.logError("MaliHwCntrReader: Failed setting ABI version ioctl");
                return;
            }
            else if (version_check.major < 10) {
                logg.logError("MaliHwCntrReader: Unsupported ABI version %u.%u", version_check.major, version_check.minor);
                return;
            }

            logg.logMessage("MaliHwCntrReader: ABI version: %u.%u", version_check.major, version_check.minor);
        }

        // set the flags
        {
            struct kbase_uk_hwcnt_reader_set_flags flags;

            memset(&flags, 0, sizeof(flags));
            flags.header.id = KBASE_FUNC_SET_FLAGS;
            flags.create_flags = BASE_CONTEXT_CREATE_KERNEL_FLAGS;

            if (doMaliIoctl(devFd, flags) != 0) {
                logg.logError("MaliHwCntrReader: Failed setting flags ioctl");
                return;
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
                    logg.logError("MaliHwCntrReader: Failed sending hwcnt reader ioctl. ret = %lu", (unsigned long)(setup_args.header.ret));
                }
                else {
                    logg.logError("MaliHwCntrReader: Failed sending hwcnt reader ioctl");
                }
                return;
            }

            // we have a handle to the reader fd
            hwcntReaderFd = setup_args.fd;
        }

        // verify the API version
        {
            uint32_t api_version = ~HWCNT_READER_API;

            if (ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_API_VERSION, &api_version) != 0) {
                logg.logError("MaliHwCntrReader: Could not determine hwcnt reader api version due to ioctl failure (%s)", strerror(errno));
                return;
            }
            else if (api_version != HWCNT_READER_API) {
                logg.logError("MaliHwCntrReader: Invalid API version (%lu)", (unsigned long)(api_version));
                return;
            }
        }

        // get sample buffer size
        if (ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_BUFFER_SIZE, &sampleBufferSize) != 0) {
            logg.logError("MaliHwCntrReader: Could not determine hwcnt reader sample buffer size due to ioctl failure (%s)", strerror(errno));
            return;
        }

        // get hardware version
        if (ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_HWVER, &hardwareVersion) != 0) {
            logg.logError("MaliHwCntrReader: Could not determine hwcnt reader hardware version due to ioctl failure (%s)", strerror(errno));
            return;
        }

        // mmap the data
        sampleMemory = reinterpret_cast<uint8_t *>(mmap(NULL, bufferCount_ * sampleBufferSize, PROT_READ, MAP_PRIVATE, hwcntReaderFd, 0));
        if ((sampleMemory == NULL) || (sampleMemory == reinterpret_cast<uint8_t *>(-1ul))) {
            logg.logError("MaliHwCntrReader: Could not mmap sample buffer");
            return;
        }

        // create the thread notification pipe
        if (pipe(selfPipe) != 0) {
            logg.logError("MaliHwCntrReader: Could create pipe (%s)", strerror(errno));
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

        if (sampleMemory != NULL) {
            munmap(sampleMemory, bufferCount * sampleBufferSize);
        }

        if (hwcntReaderFd != -1) {
            close(hwcntReaderFd);
        }

        if (devFd != -1) {
            close(devFd);
        }

        if (device != NULL) {
            delete device;
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

        if (ioctl(hwcntReaderFd, KBASE_HWCNT_READER_SET_INTERVAL, interval) != 0) {
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

    MaliHwCntrReader::WaitStatus MaliHwCntrReader::waitForBuffer(SampleBuffer & buffer, int timeout)
    {
        if (!initialized) {
            logg.logError("MaliHwCntrReader::waitForBuffer - not initialized");
            return WAIT_STATUS_ERROR;
        }

        // poll for any updates
        pollfd fds[POLL_DESCRIPTOR_COUNT];
        fds[POLL_DESCRIPTOR_SIGNAL].fd = selfPipe[PIPE_DESCRIPTOR_IN];
        fds[POLL_DESCRIPTOR_SIGNAL].events = POLLIN;
        fds[POLL_DESCRIPTOR_SIGNAL].revents = 0;
        fds[POLL_DESCRIPTOR_HWCNT_READER].fd = hwcntReaderFd;
        fds[POLL_DESCRIPTOR_HWCNT_READER].events = POLLIN;
        fds[POLL_DESCRIPTOR_HWCNT_READER].revents = 0;

        const int ready = poll(fds, POLL_DESCRIPTOR_COUNT, timeout);

        // process result
        if (ready < 0) {
            // error occured
            logg.logError("MaliHwCntrReader::waitForBuffer - poll failed");
            return WAIT_STATUS_ERROR;
        }
        else if (ready == 0) {
            // clear buffer
            buffer = SampleBuffer();
            return WAIT_STATUS_SUCCESS;
        }
        else if (fds[POLL_DESCRIPTOR_SIGNAL].revents != 0) {
            buffer = SampleBuffer();

            // read the data from the pipe if necessary
            if ((fds[POLL_DESCRIPTOR_SIGNAL].revents & POLLIN) == POLLIN) {
                // read the data
                poll_data_t value;
                int result = read(selfPipe[PIPE_DESCRIPTOR_IN], &value, sizeof(value));
                if (result < 0) {
                    return WAIT_STATUS_ERROR;
                }
            }

            // terminated
            return WAIT_STATUS_TERMINATED;
        }
        else if ((fds[POLL_DESCRIPTOR_HWCNT_READER].revents & POLLIN) == POLLIN) {
            // get the buffer
            kbase_hwcnt_reader_metadata metadata;
            if (ioctl(hwcntReaderFd, KBASE_HWCNT_READER_GET_BUFFER, &metadata) != 0) {
                logg.logError("MaliHwCntrReader: Could not get buffer due to ioctl failure (%s)", strerror(errno));
                return WAIT_STATUS_ERROR;
            }

            buffer = SampleBuffer(*this, metadata.timestamp, metadata.event_id, metadata.buffer_idx, sampleBufferSize, &(sampleMemory[sampleBufferSize * metadata.buffer_idx]));
            return WAIT_STATUS_SUCCESS;
        }
        else if ((fds[POLL_DESCRIPTOR_HWCNT_READER].revents & POLLHUP) == POLLHUP) {
            // terminated
            buffer = SampleBuffer();
            return WAIT_STATUS_TERMINATED;
        }
        else {
            // error occured
            logg.logError("MaliHwCntrReader::waitForBuffer - unexpected event 0x%x", fds[POLL_DESCRIPTOR_HWCNT_READER].revents);
            return WAIT_STATUS_ERROR;
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
}
