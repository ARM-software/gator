/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICEAPI_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICEAPI_H_

#include "lib/AutoClosingFd.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mali_userspace {
    /**
     * Interface that abstracts the main ioctl interface to /dev/mali.
     * Allows talking to different driver versions with different APIs.
     */
    class IMaliDeviceApi {
    public:
        /**
         * For a given device driver path, probe the device and return an appropriate implementation
         * of the interface, or return nullptr if the device path is invalid / not supported
         *
         * @param devMaliPath
         * @return The object implementing this interface that is appropriate for the device, or nullptr
         */
        static std::unique_ptr<IMaliDeviceApi> probe(const char * devMaliPath);

        virtual ~IMaliDeviceApi() = default;

        /**
         * Create an HWCNT reader handle (which is a file-descriptor, for use by MaliHwCntrReader)
         *
         * @param bufferCount
         * @param jmBitmask
         * @param shaderBitmask
         * @param tilerBitmask
         * @param mmuL2Bitmask
         * @param failedDueToBufferCount [OUT] indicates the the creation likely failed due to buffer
         *          count being invalid
         * @return The handle, or invalid handle if failed
         */
        virtual lib::AutoClosingFd createHwCntReaderFd(std::size_t bufferCount,
                                                       std::uint32_t jmBitmask,
                                                       std::uint32_t shaderBitmask,
                                                       std::uint32_t tilerBitmask,
                                                       std::uint32_t mmuL2Bitmask,
                                                       bool & failedDueToBufferCount) = 0;

        /** @return The shader core sparse allocation mask */
        virtual std::uint64_t getShaderCoreAvailabilityMask() const = 0;
        /** @return The shader core sparse allocation mask */
        virtual std::uint32_t getMaxShaderCoreBlockIndex() const = 0;
        /** @return The number of shader cores on the device */
        virtual std::uint32_t getNumberOfUsableShaderCores() const = 0;
        /** @return The number of L2 cache slices */
        virtual std::uint32_t getNumberOfL2Slices() const = 0;
        /** @return The GPUID of the device */
        virtual std::uint32_t getGpuId() const = 0;
        /** @return The hardware version of the device */
        virtual std::uint32_t getHwVersion() const = 0;
    };
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICEAPI_H_ */
