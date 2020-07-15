/* Copyright (C) 2016-2020 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliHwCntrReader.h"

#include "Logging.h"
#include "lib/Syscall.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

namespace mali_userspace {

#if defined(ANDROID) || defined(__ANDROID__)
/* We use _IOR_BAD/_IOW_BAD rather than _IOR/_IOW otherwise fails to compile with NDK-BUILD because of _IOC_TYPECHECK is defined, not because the paramter is invalid */
#define MALI_IOR(a, b, c) _IOR_BAD(a, b, c)
#define MALI_IOW(a, b, c) _IOW_BAD(a, b, c)
#define MALI_IOWR(a, b, c) _IOWR_BAD(a, b, c)
#else
#define MALI_IOR(a, b, c) _IOR(a, b, c)
#define MALI_IOW(a, b, c) _IOW(a, b, c)
#define MALI_IOWR(a, b, c) _IOWR(a, b, c)
#endif
#define MALI_IO(a, b) _IO(a, b)

    /* --------------------------------------------------------------------- */

    namespace {
        enum {
            /* reader API version */
            HWCNT_READER_API = 1,

            /* The ids of ioctl commands for the reader interface */
            KBASE_HWCNT_READER = 0xBE,
            KBASE_HWCNT_READER_GET_HWVER = MALI_IOR(KBASE_HWCNT_READER, 0x00, uint32_t),
            KBASE_HWCNT_READER_GET_BUFFER_SIZE = MALI_IOR(KBASE_HWCNT_READER, 0x01, uint32_t),
            KBASE_HWCNT_READER_DUMP = MALI_IOW(KBASE_HWCNT_READER, 0x10, uint32_t),
            KBASE_HWCNT_READER_CLEAR = MALI_IOW(KBASE_HWCNT_READER, 0x11, uint32_t),
            KBASE_HWCNT_READER_GET_BUFFER = MALI_IOR(KBASE_HWCNT_READER, 0x20, struct kbase_hwcnt_reader_metadata),
            KBASE_HWCNT_READER_PUT_BUFFER = MALI_IOW(KBASE_HWCNT_READER, 0x21, struct kbase_hwcnt_reader_metadata),
            KBASE_HWCNT_READER_SET_INTERVAL = MALI_IOW(KBASE_HWCNT_READER, 0x30, uint32_t),
            KBASE_HWCNT_READER_ENABLE_EVENT = MALI_IOW(KBASE_HWCNT_READER, 0x40, uint32_t),
            KBASE_HWCNT_READER_DISABLE_EVENT = MALI_IOW(KBASE_HWCNT_READER, 0x41, uint32_t),
            KBASE_HWCNT_READER_GET_API_VERSION = MALI_IOW(KBASE_HWCNT_READER, 0xFF, uint32_t)
        };

        enum {
            PIPE_DESCRIPTOR_IN,  /**< The index of a pipe's input descriptor. */
            PIPE_DESCRIPTOR_OUT, /**< The index of a pipe's output descriptor. */

            PIPE_DESCRIPTOR_COUNT /**< The number of descriptors forming a pipe. */
        };

        enum {
            POLL_DESCRIPTOR_SIGNAL,       /**< The index of the signal descriptor in poll fds array. */
            POLL_DESCRIPTOR_HWCNT_READER, /**< The index of the hwcnt reader descriptor in poll fds array. */

            POLL_DESCRIPTOR_COUNT /**< The number of descriptors poll is waiting for. */
        };

        /** Write a single byte into the pipe to interrupt the reader thread */
        using poll_data_t = char;
    }

    MaliHwCntrReader::MaliHwCntrReader(const MaliDevice & device,
                                       lib::AutoClosingFd hwcntReaderFd,
                                       lib::AutoClosingFd selfPipe0,
                                       lib::AutoClosingFd selfPipe1,
                                       MmappedBuffer sampleMemory,
                                       uint32_t bufferCount,
                                       uint32_t sampleBufferSize,
                                       uint32_t hardwareVersion)

        : device(device),
          hwcntReaderFd(std::move(hwcntReaderFd)),
          selfPipe(),
          sampleMemory(std::move(sampleMemory)),
          bufferCount(bufferCount),
          sampleBufferSize(sampleBufferSize),
          hardwareVersion(hardwareVersion)
    {
        selfPipe[0] = std::move(selfPipe0);
        selfPipe[1] = std::move(selfPipe1);
    }

    const MaliDevice & MaliHwCntrReader::getDevice() const { return device; }

    IMaliHwCntrReader::HardwareVersion MaliHwCntrReader::getHardwareVersion() const { return hardwareVersion; }

    bool MaliHwCntrReader::triggerCounterRead() { return lib::ioctl(*hwcntReaderFd, KBASE_HWCNT_READER_DUMP, 0) == 0; }

    bool MaliHwCntrReader::startPeriodicSampling(uint32_t interval)
    {
        return lib::ioctl(*hwcntReaderFd, KBASE_HWCNT_READER_SET_INTERVAL, interval) == 0;
    }

    bool MaliHwCntrReader::configureJobBasedSampled(bool preJob, bool postJob)
    {
        if (ioctl(*hwcntReaderFd,
                  (preJob ? KBASE_HWCNT_READER_ENABLE_EVENT : KBASE_HWCNT_READER_DISABLE_EVENT),
                  HWCNT_READER_EVENT_PREJOB) != 0) {
            return false;
        }

        if (ioctl(*hwcntReaderFd,
                  (postJob ? KBASE_HWCNT_READER_ENABLE_EVENT : KBASE_HWCNT_READER_DISABLE_EVENT),
                  HWCNT_READER_EVENT_POSTJOB) != 0) {
            return false;
        }

        return true;
    }

    SampleBuffer MaliHwCntrReader::waitForBuffer(int timeout)
    {
        SampleBuffer temp;

        // poll for any updates
        pollfd fds[POLL_DESCRIPTOR_COUNT];
        fds[POLL_DESCRIPTOR_SIGNAL].fd = *selfPipe[PIPE_DESCRIPTOR_IN];
        fds[POLL_DESCRIPTOR_SIGNAL].events = POLLIN;
        fds[POLL_DESCRIPTOR_SIGNAL].revents = 0;
        fds[POLL_DESCRIPTOR_HWCNT_READER].fd = *hwcntReaderFd;
        fds[POLL_DESCRIPTOR_HWCNT_READER].events = POLLIN;
        fds[POLL_DESCRIPTOR_HWCNT_READER].revents = 0;

        const int ready = lib::poll(fds, POLL_DESCRIPTOR_COUNT, timeout);

        // process result
        if (ready < 0) {
            // error occurred
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
                int result = lib::read(*selfPipe[PIPE_DESCRIPTOR_IN], &value, sizeof(value));
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
            if (lib::ioctl(*hwcntReaderFd, KBASE_HWCNT_READER_GET_BUFFER, reinterpret_cast<unsigned long>(&metadata)) !=
                0) {
                logg.logError("MaliHwCntrReader: Could not get buffer due to ioctl failure (%s)", strerror(errno));
                temp.status = WAIT_STATUS_ERROR;
                return temp;
            }
            temp.timestamp = metadata.timestamp;
            temp.eventId = metadata.event_id;
            temp.bufferId = metadata.buffer_idx;
            temp.size = sampleBufferSize;
            unique_ptr_with_deleter<uint8_t> data_temp(&(sampleMemory[sampleBufferSize * metadata.buffer_idx]),
                                                       [=](uint8_t * /*unused*/) {
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
            // error occurred
            logg.logError("MaliHwCntrReader::waitForBuffer - unexpected event 0x%x",
                          fds[POLL_DESCRIPTOR_HWCNT_READER].revents);
            temp.status = WAIT_STATUS_ERROR;
            return temp;
        }
    }

    void MaliHwCntrReader::interrupt()
    {
        poll_data_t exit = 0;
        if (lib::write(*selfPipe[PIPE_DESCRIPTOR_OUT], &exit, sizeof(exit)) < 0) {
            logg.logError("MaliHwCntrReader::interrupt failed (%s)", strerror(errno));
        }
    }

    bool MaliHwCntrReader::releaseBuffer(kbase_hwcnt_reader_metadata & metadata)
    {
        return lib::ioctl(*hwcntReaderFd, KBASE_HWCNT_READER_PUT_BUFFER, reinterpret_cast<unsigned long>(&metadata)) ==
               0;
    }

    std::unique_ptr<MaliHwCntrReader> MaliHwCntrReader::create(const MaliDevice & device,
                                                               CounterBitmask jmBitmask,
                                                               CounterBitmask shaderBitmask,
                                                               CounterBitmask tilerBitmask,
                                                               CounterBitmask mmuL2Bitmask)
    {
        const uint32_t mmuL2BlockCount = device.getL2MmuBlockCount();
        const uint32_t shaderBlockCount = device.getShaderBlockCount();

        if (shaderBlockCount < 1) {
            logg.logError("MaliHwCntrReader: shaderBlockCount = %u", shaderBlockCount);
            return {};
        }

        // we do not know the best buffer count up front, so we have to test for it by repeatedly attempting to create the
        // reader until we succeed (or until some arbitrary limit)
        for (unsigned bufferCount = 1; bufferCount < 1024; ++bufferCount) {
            lib::AutoClosingFd hwcntReaderFd;
            uint32_t sampleBufferSize = 0;
            uint32_t hardwareVersion = 0;

            // create the reader
            {
                bool failedDueToBlockCount = false;
                hwcntReaderFd = device.createHwCntReaderFd(bufferCount,
                                                           jmBitmask,
                                                           shaderBitmask,
                                                           tilerBitmask,
                                                           mmuL2Bitmask,
                                                           failedDueToBlockCount);
                if (!hwcntReaderFd) {
                    if (failedDueToBlockCount) {
                        continue;
                    }
                    else {
                        return {};
                    }
                }
            }

            // verify the API version
            {
                uint32_t api_version = ~HWCNT_READER_API;
                if (lib::ioctl(*hwcntReaderFd,
                               KBASE_HWCNT_READER_GET_API_VERSION,
                               reinterpret_cast<unsigned long>(&api_version)) != 0) {
                    logg.logError(
                        "MaliHwCntrReader: Could not determine hwcnt reader api version due to ioctl failure (%s)",
                        strerror(errno));
                    return {};
                }
                else if (api_version != HWCNT_READER_API) {
                    logg.logError("MaliHwCntrReader: Invalid API version (%lu)",
                                  static_cast<unsigned long>(api_version));
                    return {};
                }
            }

            // get sample buffer size
            if (lib::ioctl(*hwcntReaderFd,
                           KBASE_HWCNT_READER_GET_BUFFER_SIZE,
                           reinterpret_cast<unsigned long>(&sampleBufferSize)) != 0) {
                logg.logError(
                    "MaliHwCntrReader: Could not determine hwcnt reader sample buffer size due to ioctl failure (%s)",
                    strerror(errno));
                return {};
            }

            // get hardware version
            if (lib::ioctl(*hwcntReaderFd,
                           KBASE_HWCNT_READER_GET_HWVER,
                           reinterpret_cast<unsigned long>(&hardwareVersion)) != 0) {
                logg.logError(
                    "MaliHwCntrReader: Could not determine hwcnt reader hardware version due to ioctl failure (%s)",
                    strerror(errno));
                return {};
            }

            if ((hardwareVersion < 4) || (hardwareVersion > 6)) {
                logg.logError("MaliHwCntrReader: Hardware version %u is not supported", hardwareVersion);
                return {};
            }

            if ((hardwareVersion > 4) && (mmuL2BlockCount < 1)) {
                logg.logError("MaliHwCntrReader: Hardware version %u detected, but mmuL2BlockCount = %u",
                              hardwareVersion,
                              mmuL2BlockCount);
                return {};
            }

            // mmap the data
            auto * const sampleMemoryPtr = reinterpret_cast<uint8_t *>(
                lib::mmap(nullptr, bufferCount * sampleBufferSize, PROT_READ, MAP_PRIVATE, *hwcntReaderFd, 0));
            if ((sampleMemoryPtr == nullptr) || (sampleMemoryPtr == reinterpret_cast<uint8_t *>(-1UL))) {
                logg.logMessage("MaliHwCntrReader: Could not mmap sample buffer");
                continue;
            }
            MmappedBuffer sampleMemory {sampleMemoryPtr, [bufferCount, sampleBufferSize](uint8_t * ptr) -> void {
                                            if (ptr != nullptr) {
                                                lib::munmap(ptr, bufferCount * sampleBufferSize);
                                            }
                                        }};

            // create the thread notification pipe
            lib::AutoClosingFd selfPipe[2];
            {
                int selfPipeFd[2] = {-1, -1};
                if (pipe2(selfPipeFd, O_CLOEXEC) != 0) {
                    logg.logError("MaliHwCntrReader: Could not create pipe (%s)", strerror(errno));
                    return {};
                }
                selfPipe[0] = selfPipeFd[0];
                selfPipe[1] = selfPipeFd[1];
            }

            logg.logMessage("MaliHwCntrReader: Successfully created reader, with buffer size of %u", bufferCount);
            return std::unique_ptr<MaliHwCntrReader> {new MaliHwCntrReader(device,
                                                                           std::move(hwcntReaderFd),
                                                                           std::move(selfPipe[0]),
                                                                           std::move(selfPipe[1]),
                                                                           std::move(sampleMemory),
                                                                           bufferCount,
                                                                           sampleBufferSize,
                                                                           hardwareVersion)};
        }
        return nullptr;
    }

    std::unique_ptr<MaliHwCntrReader> MaliHwCntrReader::createReader(const MaliDevice & device)
    {
        return create(device, ~0U, ~0U, ~0U, ~0U);
    }
}
