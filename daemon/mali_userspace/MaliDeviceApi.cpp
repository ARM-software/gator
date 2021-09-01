/* Copyright (C) 2019-2021 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliDeviceApi.h"

#include "Logging.h"
#include "lib/Assert.h"
#include "lib/Format.h"
#include "lib/Syscall.h"
#include "mali_userspace/MaliDevice.h"
#include "mali_userspace/MaliDeviceApi_DdkDefines.h"

#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <utility>

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

namespace mali_userspace {
    namespace {
        uint32_t calcShaderCoreMaskBlockCount(uint64_t core_mask) { return (64 - __builtin_clzll(core_mask)); }

        uint32_t calcNumShaders(uint64_t core_mask) { return __builtin_popcountll(core_mask); }

        /**
         * Send a logSetup message detailing the detected Mali device
         */
        void logDetectedMaliDevice(const char * maliDevicePath,
                                   uint32_t productId,
                                   uint32_t major,
                                   uint32_t minor,
                                   uint32_t frequency,
                                   uint32_t l2Slices,
                                   uint32_t busWidth,
                                   uint64_t shaderCoreMask)
        {
            const char * const productName = findMaliProductNameFromId(productId);

            const auto shaderCores = calcNumShaders(shaderCoreMask);
            const auto maxBit = calcShaderCoreMaskBlockCount(shaderCoreMask);

            runtime_assert(maxBit >= shaderCores, "Unexpected mask vs count");

            lib::Format formatter;

            formatter << "Mali GPU counters\nSuccessfully probed Mali device " << maliDevicePath;

            if (productName != nullptr) {
                formatter << " as Mali-" << productName << " (0x" << std::hex << productId << std::dec << " r" << major
                          << "p" << minor << ")";
                if (frequency > 0) {
                    formatter << " clocked at " << frequency << "MHz";
                }
            }
            else {
                formatter << " but it is not recognized (id: 0x" << std::hex << productId << std::dec << " r" << major
                          << "p" << minor;
            }

            formatter << ", " << l2Slices << " L2 Slices, ";
            formatter << busWidth << "-bit Bus, ";
            formatter << shaderCores << " Shader Cores";

            if (shaderCoreMask != ((1ull << shaderCores) - 1)) {
                formatter << " (sparse layout, mask is 0x" << std::hex << shaderCoreMask << std::dec << ")";
            }

            if (productName != nullptr) {
                formatter << ".";
            }
            else {
                formatter << "). Please try updating your version of gatord.";
            }

            logg.logSetup("%s", std::string(formatter).c_str());
        }

        uint32_t extractBusWidth(uint32_t raw_l2_features)
        {
            uint32_t log2_bus_width = raw_l2_features >> 24;

            // If the log2 is >31 then the exp2 of it will not fit in our 32-bit result
            runtime_assert(log2_bus_width <= 31, "Unexpectedly large bus width value");

            // The value is log2 of the real value, so use a bitshift to invert that
            return (1u << log2_bus_width);
        }
    }

    /**
     * Supporting DDK versions m_r12-m_r21, b_r0-b_r9
     */
    namespace ddk_pre_r21 {
        template<typename T>
        static int doMaliIoctl(int fd, T & arg)
        {
            union kbase_uk_header * hdr = &arg.header;
            const int cmd = _IOC(_IOC_READ | _IOC_WRITE, LINUX_UK_BASE_MAGIC, hdr->id, sizeof(T));

            if (lib::ioctl(fd, cmd, reinterpret_cast<unsigned long>(&arg))) {
                return -1;
            }
            if (hdr->ret) {
                return -1;
            }

            return 0;
        }

        /**
         * IMaliDeviceApi implementation for this version of the Mali driver
         */
        class MaliDeviceApi final : public IMaliDeviceApi {
        public:
            MaliDeviceApi(const char * maliDevicePath, lib::AutoClosingFd devFd, const kbase_uk_gpuprops & props)
                : devFd(std::move(devFd)),
                  shaderCoreAvailabilityMask(calcShaderCoreMask(props)),
                  numberOfL2Slices(props.props.l2_props.num_l2_slices),
                  gpuId(props.props.core_props.product_id),
                  hwVersion((uint32_t(props.props.core_props.major_revision) << 16) |
                            props.props.core_props.minor_revision),
                  busWidth(extractBusWidth(props.props.raw_props.l2_features))
            {
                logDetectedMaliDevice(maliDevicePath,
                                      props.props.core_props.product_id,
                                      props.props.core_props.major_revision,
                                      props.props.core_props.minor_revision,
                                      props.props.core_props.gpu_speed_mhz,
                                      props.props.l2_props.num_l2_slices,
                                      busWidth,
                                      shaderCoreAvailabilityMask);
            }

            lib::AutoClosingFd createHwCntReaderFd(size_t bufferCount,
                                                   uint32_t jmBitmask,
                                                   uint32_t shaderBitmask,
                                                   uint32_t tilerBitmask,
                                                   uint32_t mmuL2Bitmask,
                                                   bool & failedDueToBufferCount) override
            {
                logg.logMessage("MaliDeviceApi: create (%zu, 0x%x, 0x%x, 0x%x, 0x%x)",
                                bufferCount,
                                jmBitmask,
                                shaderBitmask,
                                tilerBitmask,
                                mmuL2Bitmask);

                kbase_uk_hwcnt_reader_setup setup_args {};
                setup_args.header.id = KBASE_FUNC_HWCNT_READER_SETUP;
                setup_args.buffer_count = bufferCount;
                setup_args.jm_bm = jmBitmask;
                setup_args.shader_bm = shaderBitmask;
                setup_args.tiler_bm = tilerBitmask;
                setup_args.mmu_l2_bm = mmuL2Bitmask;
                setup_args.fd = -1;

                if (doMaliIoctl(*devFd, setup_args) != 0) {
                    if (setup_args.header.ret != 0) {
                        logg.logMessage("MaliDeviceApi: Failed sending hwcnt reader ioctl. fd=%i ret = %lu",
                                        *devFd,
                                        static_cast<unsigned long>(setup_args.header.ret));
                    }
                    else {
                        logg.logMessage("MaliDeviceApi: Failed sending hwcnt reader ioctl");
                    }
                    failedDueToBufferCount = true;
                    return {};
                }

                // we have a handle to the reader fd
                return setup_args.fd;
            }

            uint64_t getShaderCoreAvailabilityMask() const override { return shaderCoreAvailabilityMask; }

            uint32_t getMaxShaderCoreBlockIndex() const override
            {
                return calcShaderCoreMaskBlockCount(shaderCoreAvailabilityMask);
            }

            uint32_t getNumberOfUsableShaderCores() const override
            {
                return calcNumShaders(shaderCoreAvailabilityMask);
            }

            uint32_t getNumberOfL2Slices() const override { return numberOfL2Slices; }

            uint32_t getGpuId() const override { return gpuId; }

            uint32_t getHwVersion() const override { return hwVersion; }

            uint32_t getExternalBusWidth() const override { return busWidth; }

        private:
            static uint64_t calcShaderCoreMask(const kbase_uk_gpuprops & props)
            {
                uint64_t core_mask = 0;
                for (uint32_t i = 0; i < props.props.coherency_info.num_core_groups; i++) {
                    core_mask |= props.props.coherency_info.group[i].core_mask;
                }
                return core_mask;
            }

            lib::AutoClosingFd devFd;
            const uint64_t shaderCoreAvailabilityMask;
            const uint32_t numberOfL2Slices;
            const uint32_t gpuId;
            const uint32_t hwVersion;
            const uint32_t busWidth;
        };

        std::unique_ptr<IMaliDeviceApi> probe(const char * maliDevicePath, lib::AutoClosingFd devFd)
        {
            // get & check API version
            {
                kbase_uk_version_check_args version_check {};
                version_check.major = 0;
                version_check.minor = 0;

                if (doMaliIoctl(*devFd, version_check) != 0) {
                    logg.logMessage("MaliDeviceApi: Failed setting ABI version ioctl - may be r21p0 or later...");
                    return {};
                }
                if (version_check.major < 10) {
                    logg.logMessage("MaliDeviceApi: Unsupported ABI version %u.%u",
                                    version_check.major,
                                    version_check.minor);
                    return {};
                }

                logg.logMessage("MaliDeviceApi: ABI version: %u.%u", version_check.major, version_check.minor);
            }

            // set the flags / create context
            {
                kbase_uk_set_flags flags {};
                flags.header.id = KBASE_FUNC_SET_FLAGS;
                flags.create_flags = BASE_CONTEXT_CREATE_KERNEL_FLAGS;

                if (doMaliIoctl(*devFd, flags) != 0) {
                    logg.logMessage("MaliDeviceApi: Failed setting flags ioctl");
                    return {};
                }
            }

            // probe properties
            {
                kbase_uk_gpuprops props {};
                props.header.id = KBASE_FUNC_GET_PROPS;

                if (doMaliIoctl(*devFd, props) != 0) {
                    logg.logMessage("MaliDeviceApi: Failed getting props from ioctl");
                    return {};
                }

                return std::unique_ptr<IMaliDeviceApi> {new MaliDeviceApi(maliDevicePath, std::move(devFd), props)};
            }
        }
    }

    /**
     * Supporting DDK versions m_r22-m_r28, b_r10+
     */
    namespace ddk_post_r21 {
        enum {
            /* Related to mali0 ioctl interface */
            KBASE_IOCTL_TYPE = 0x80,
            BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED = 0x2,
            KBASE_IOCTL_VERSION_CHECK_JM = MALI_IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check),
            KBASE_IOCTL_VERSION_CHECK_CSF = MALI_IOWR(KBASE_IOCTL_TYPE, 52, struct kbase_ioctl_version_check),
            KBASE_IOCTL_SET_FLAGS = MALI_IOW(KBASE_IOCTL_TYPE, 1, struct kbase_ioctl_set_flags),
            KBASE_IOCTL_GET_GPUPROPS = MALI_IOW(KBASE_IOCTL_TYPE, 3, struct kbase_ioctl_get_gpuprops),
            KBASE_IOCTL_HWCNT_READER_SETUP = MALI_IOW(KBASE_IOCTL_TYPE, 8, struct kbase_ioctl_hwcnt_reader_setup),
        };

        /** Read a u8 from the gpu properties blob */
        static inline uint8_t readU8(uint8_t * buffer, int & pos, int size)
        {
            runtime_assert(pos < size, "Buffer overflow reading GPU properties");

            return buffer[pos++];
        }

        /** Read a u16 from the gpu properties blob */
        static inline uint16_t readU16(const uint8_t * buffer, int & pos, int size)
        {
            runtime_assert((pos + 2) <= size, "Buffer overflow reading GPU properties");

            const uint16_t result = buffer[pos] | (uint16_t(buffer[pos + 1]) << 8);
            pos += 2;
            return result;
        }

        /** Read a u32 from the gpu properties blob */
        static inline uint32_t readU32(const uint8_t * buffer, int & pos, int size)
        {
            runtime_assert((pos + 4) <= size, "Buffer overflow reading GPU properties");

            const uint32_t result = buffer[pos] | (uint32_t(buffer[pos + 1]) << 8) | (uint32_t(buffer[pos + 2]) << 16) |
                                    (uint32_t(buffer[pos + 3]) << 24);
            pos += 4;
            return result;
        }

        /** Read a u64 from the gpu properties blob */
        static inline uint64_t readU64(const uint8_t * buffer, int & pos, int size)
        {
            runtime_assert((pos + 8) <= size, "Buffer overflow reading GPU properties");

            const uint64_t result = buffer[pos] | (uint64_t(buffer[pos + 1]) << 8) | (uint64_t(buffer[pos + 2]) << 16) |
                                    (uint64_t(buffer[pos + 3]) << 24) | (uint64_t(buffer[pos + 4]) << 32) |
                                    (uint64_t(buffer[pos + 5]) << 40) | (uint64_t(buffer[pos + 6]) << 48) |
                                    (uint64_t(buffer[pos + 7]) << 56);
            pos += 8;
            return result;
        }

        /** Decode some property value */
        static inline uint64_t readValue(KBaseGpuPropValueSize value_size, uint8_t * buffer, int & pos, int size)
        {
            switch (value_size) {
                case KBaseGpuPropValueSize::U8:
                    return readU8(buffer, pos, size);
                case KBaseGpuPropValueSize::U16:
                    return readU16(buffer, pos, size);
                case KBaseGpuPropValueSize::U32:
                    return readU32(buffer, pos, size);
                case KBaseGpuPropValueSize::U64:
                    return readU64(buffer, pos, size);
                default:
                    std::terminate(); // impossible
            }
        }

        /**
         * Decode the blob data that is returned from the gpu properties ioctl
         * @return The useful decoded fields
         */
        static gpu_properties decodeProperties(uint8_t * buffer, int size)
        {
            gpu_properties result {};

            for (int pos = 0; pos < size;) {
                const uint32_t token = readU32(buffer, pos, size);
                const auto key = KBaseGpuPropKey(token >> 2);
                const auto value_size = KBaseGpuPropValueSize(token & 3);
                const uint64_t value = readValue(value_size, buffer, pos, size);

                switch (key) {
                    case KBaseGpuPropKey::PRODUCT_ID:
                        result.product_id = value;
                        break;
                    case KBaseGpuPropKey::MINOR_REVISION:
                        result.minor_revision = value;
                        break;
                    case KBaseGpuPropKey::MAJOR_REVISION:
                        result.major_revision = value;
                        break;
                    case KBaseGpuPropKey::RAW_L2_FEATURES:
                        runtime_assert(value_size == KBaseGpuPropValueSize::U32, "Unexpected L2 features size");
                        result.bus_width = extractBusWidth(value);
                        break;
                    case KBaseGpuPropKey::COHERENCY_NUM_CORE_GROUPS:
                        runtime_assert(value <= BASE_MAX_COHERENT_GROUPS, "Too many core groups");
                        result.num_core_groups = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_0:
                        result.core_mask[0] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_1:
                        result.core_mask[1] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_2:
                        result.core_mask[2] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_3:
                        result.core_mask[3] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_4:
                        result.core_mask[4] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_5:
                        result.core_mask[5] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_6:
                        result.core_mask[6] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_7:
                        result.core_mask[7] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_8:
                        result.core_mask[8] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_9:
                        result.core_mask[9] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_10:
                        result.core_mask[10] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_11:
                        result.core_mask[11] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_12:
                        result.core_mask[12] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_13:
                        result.core_mask[13] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_14:
                        result.core_mask[14] = value;
                        break;
                    case KBaseGpuPropKey::COHERENCY_GROUP_15:
                        result.core_mask[15] = value;
                        break;
                    case KBaseGpuPropKey::L2_NUM_L2_SLICES:
                        result.num_l2_slices = value;
                        break;
                    default: // ignored
                        break;
                }
            }

            return result;
        }

        /**
         * IMaliDeviceApi implementation for this version of the Mali driver
         */
        class MaliDeviceApi final : public IMaliDeviceApi {
        public:
            MaliDeviceApi(const char * maliDevicePath, lib::AutoClosingFd devFd, const gpu_properties & props)
                : devFd(std::move(devFd)),
                  shaderCoreAvailabilityMask(calcShaderCoreMask(props)),
                  numberOfL2Slices(props.num_l2_slices),
                  gpuId(props.product_id),
                  hwVersion((uint32_t(props.major_revision) << 16) | props.minor_revision),
                  busWidth(props.bus_width)
            {
                logDetectedMaliDevice(maliDevicePath,
                                      props.product_id,
                                      props.major_revision,
                                      props.minor_revision,
                                      0,
                                      props.num_l2_slices,
                                      busWidth,
                                      shaderCoreAvailabilityMask);
            }

            lib::AutoClosingFd createHwCntReaderFd(size_t bufferCount,
                                                   uint32_t jmBitmask,
                                                   uint32_t shaderBitmask,
                                                   uint32_t tilerBitmask,
                                                   uint32_t mmuL2Bitmask,
                                                   bool & failedDueToBufferCount) override
            {
                struct kbase_ioctl_hwcnt_reader_setup setup_args {
                };

                setup_args.buffer_count = bufferCount;
                setup_args.jm_bm = jmBitmask;
                setup_args.shader_bm = shaderBitmask;
                setup_args.tiler_bm = tilerBitmask;
                setup_args.mmu_l2_bm = mmuL2Bitmask;

                const int hwcntReaderFd =
                    lib::ioctl(*devFd, KBASE_IOCTL_HWCNT_READER_SETUP, reinterpret_cast<unsigned long>(&setup_args));
                if (hwcntReaderFd < 0) {
                    logg.logMessage("MaliDeviceApi: Failed sending hwcnt reader ioctl");
                    failedDueToBufferCount = true;
                    return {};
                }
                return hwcntReaderFd;
            }

            uint64_t getShaderCoreAvailabilityMask() const override { return shaderCoreAvailabilityMask; }

            uint32_t getMaxShaderCoreBlockIndex() const override
            {
                return calcShaderCoreMaskBlockCount(shaderCoreAvailabilityMask);
            }

            uint32_t getNumberOfUsableShaderCores() const override
            {
                return calcNumShaders(shaderCoreAvailabilityMask);
            }

            uint32_t getNumberOfL2Slices() const override { return numberOfL2Slices; }

            uint32_t getGpuId() const override { return gpuId; }

            uint32_t getHwVersion() const override { return hwVersion; }

            uint32_t getExternalBusWidth() const override { return busWidth; }

        private:
            static uint64_t calcShaderCoreMask(const gpu_properties & props)
            {
                uint64_t core_mask = 0;
                for (uint32_t i = 0; i < props.num_core_groups; i++) {
                    core_mask |= props.core_mask[i];
                }
                return core_mask;
            }

            lib::AutoClosingFd devFd;
            const uint64_t shaderCoreAvailabilityMask;
            const uint32_t numberOfL2Slices;
            const uint32_t gpuId;
            const uint32_t hwVersion;
            const uint32_t busWidth;
        };

        std::unique_ptr<IMaliDeviceApi> probe(const char * maliDevicePath, lib::AutoClosingFd devFd)
        {
            // get & check API version
            {
                kbase_ioctl_version_check version_check {};
                version_check.major = 0;
                version_check.minor = 0;

                if (lib::ioctl(*devFd, KBASE_IOCTL_VERSION_CHECK_JM, reinterpret_cast<unsigned long>(&version_check)) !=
                    0) {
                    logg.logMessage("MaliDeviceApi: Failed setting ABI version ioctl for JM based ddk. Trying with CSF "
                                    "ioctl version");
                    if (lib::ioctl(*devFd,
                                   KBASE_IOCTL_VERSION_CHECK_CSF,
                                   reinterpret_cast<unsigned long>(&version_check)) != 0) {
                        logg.logMessage("MaliDeviceApi: Failed setting ABI version ioctl for CSF based ddk");
                        return {};
                    }
                }

                if ((version_check.major != 1) && (version_check.major != 11)) {
                    logg.logMessage("MaliDeviceApi: Unsupported ABI version %u.%u",
                                    version_check.major,
                                    version_check.minor);
                    return {};
                }

                logg.logMessage("MaliDeviceApi: ABI version: %u.%u", version_check.major, version_check.minor);
            }

            // set the flags
            {
                kbase_ioctl_set_flags flags {};
                flags.create_flags = BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED;

                if (lib::ioctl(*devFd, KBASE_IOCTL_SET_FLAGS, reinterpret_cast<unsigned long>(&flags)) != 0) {
                    logg.logMessage("MaliDeviceApi: Failed setting flags ioctl");
                    return {};
                }
            }

            // read the GPU properties
            {
                kbase_ioctl_get_gpuprops get_props {};

                // probe first for size
                int size = lib::ioctl(*devFd, KBASE_IOCTL_GET_GPUPROPS, reinterpret_cast<unsigned long>(&get_props));
                if (size < 0) {
                    logg.logMessage("MaliDeviceApi: Failed getting properties ioctl (1)");
                    return {};
                }

                // now probe again for data
                std::unique_ptr<uint8_t[]> buffer {new uint8_t[size]};
                get_props.size = size;
                get_props.buffer.value = buffer.get();

                size = lib::ioctl(*devFd, KBASE_IOCTL_GET_GPUPROPS, reinterpret_cast<unsigned long>(&get_props));
                if (size < 0) {
                    logg.logMessage("MaliDeviceApi: Failed getting properties ioctl (2)");
                    return {};
                }

                // decode the properties data
                {
                    const gpu_properties properties = decodeProperties(buffer.get(), size);
                    return std::unique_ptr<IMaliDeviceApi> {
                        new MaliDeviceApi(maliDevicePath, std::move(devFd), properties)};
                }
            }

            return {};
        }
    }

    std::unique_ptr<IMaliDeviceApi> IMaliDeviceApi::probe(const char * devMaliPath)
    {
        {
            lib::AutoClosingFd devFd {lib::open(devMaliPath, O_RDWR | O_CLOEXEC | O_NONBLOCK)};
            if (!devFd) {
                if (errno != ENOENT) {
                    logg.logMessage("MaliDeviceApi: Failed to open mali device '%s' due to '%s'",
                                    devMaliPath,
                                    strerror(errno));
                }
                return {};
            }

            // try first version
            std::unique_ptr<IMaliDeviceApi> result = ddk_pre_r21::probe(devMaliPath, std::move(devFd));
            if (result) {
                return result;
            }
        }

        {
            lib::AutoClosingFd devFd {lib::open(devMaliPath, O_RDWR | O_CLOEXEC | O_NONBLOCK)};
            if (!devFd) {
                if (errno != ENOENT) {
                    logg.logMessage("MaliDeviceApi: Failed to open mali device '%s' due to '%s'",
                                    devMaliPath,
                                    strerror(errno));
                }
                return {};
            }

            // try first version
            std::unique_ptr<IMaliDeviceApi> result = ddk_post_r21::probe(devMaliPath, std::move(devFd));
            if (result) {
                return result;
            }
        }

        return {};
    }
}
