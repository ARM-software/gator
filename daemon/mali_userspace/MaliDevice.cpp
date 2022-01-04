/* Copyright (C) 2016-2021 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliDevice.h"

#include "GetEventKey.h"
#include "Logging.h"
#include "lib/Assert.h"
#include "mali_userspace/MaliHwCntrNames.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace mali_userspace {

    static const Constant maliBusWidthBits = Constant(getEventKey(),
                                                      "ARM_Mali-CONST_BUS_WIDTH_BITS",
                                                      "Mali Constants",
                                                      "Bus Width Bits",
                                                      ConstantMode::PerCore);
    static const Constant maliCacheSliceCount = Constant(getEventKey(),
                                                         "ARM_Mali-CONST_L2_SLICE_COUNT",
                                                         "Mali Constants",
                                                         "L2 Slice Count",
                                                         ConstantMode::PerCore);
    static const Constant maliShaderCoreCount = Constant(getEventKey(),
                                                         "ARM_Mali-CONST_SHADER_CORE_COUNT",
                                                         "Mali Constants",
                                                         "Shader Core Count",
                                                         ConstantMode::PerCore);

    enum class MaliCounterBlockName : uint32_t {
        JM = 0,
        TILER = 1,
        SHADER = 2,
        MMU = 3,
    };

    struct MaliProductVersion {
        uint32_t mGpuIdMask;
        uint32_t mGpuIdValue;
        const char * mName;
        const char * mProductFamilyName;
        const char * const * mCounterNames;
        uint32_t mNumCounterNames;
        bool mLegacyLayout;
    };

#define COUNT_OF(A) (sizeof(A) / sizeof((A)[0]))
#define MALI_COUNTER_BLOCK(N, B, NB)                                                                                   \
    {                                                                                                                  \
        (N), (B), (NB), (NB)                                                                                           \
    }
#define MALI_PRODUCT_VERSION(M, V, PN, FN, CN, V4)                                                                     \
    {                                                                                                                  \
        (M), (V), (PN), (FN), (CN), COUNT_OF(CN), (V4)                                                                 \
    }

    namespace {
        enum {
            /* product id masks for old and new versions of the id field. NB: the T60x must be tested before anything else as it could exceptionally be
             * treated as a new style of id with product code 0x6006 */
            PRODUCT_ID_MASK_OLD = 0xffff,
            PRODUCT_ID_MASK_NEW = 0xf00f,
            /* Old style product ids */
            PRODUCT_ID_T60X = 0x6956,
            PRODUCT_ID_T62X = 0x0620,
            PRODUCT_ID_T72X = 0x0720,
            PRODUCT_ID_T76X = 0x0750,
            PRODUCT_ID_T82X = 0x0820,
            PRODUCT_ID_T83X = 0x0830,
            PRODUCT_ID_T86X = 0x0860,
            PRODUCT_ID_TFRX = 0x0880,
            /* New style product ids */
            PRODUCT_ID_TMIX = 0x6000,
            PRODUCT_ID_THEX = 0x6001,
            PRODUCT_ID_TSIX = 0x7000,
            PRODUCT_ID_TNOX = 0x7001,
            PRODUCT_ID_TGOX = 0x7002,
            PRODUCT_ID_TDVX = 0x7003,
            PRODUCT_ID_TTRX = 0x9000,
            PRODUCT_ID_TNAXa = 0x9001,
            PRODUCT_ID_TNAXb = 0x9003,
            PRODUCT_ID_TOTX = 0x9004,
            PRODUCT_ID_TBOX = 0x9002,
            PRODUCT_ID_TBOXAE = 0x9005,
            PRODUCT_ID_TODX = 0xa002,
            PRODUCT_ID_TVIX = 0xa007,
            PRODUCT_ID_TGRX = 0xa003,
            PRODUCT_ID_TVAX = 0xa004
        };

        /* supported product versions */
        const MaliProductVersion PRODUCT_VERSIONS[] = {MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_T60X,
                                                                            "T60x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t60x,
                                                                            true),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_T62X,
                                                                            "T62x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t62x,
                                                                            true),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_T72X,
                                                                            "T72x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t72x,
                                                                            true),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_T76X,
                                                                            "T76x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t76x,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_T82X,
                                                                            "T82x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t82x,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_T83X,
                                                                            "T83x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t83x,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_T86X,
                                                                            "T86x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t86x,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_OLD,
                                                                            PRODUCT_ID_TFRX,
                                                                            "T88x",
                                                                            "Midgard",
                                                                            hardware_counters_mali_t88x,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TMIX,
                                                                            "G71",
                                                                            "Bifrost",
                                                                            hardware_counters_mali_tMIx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_THEX,
                                                                            "G72",
                                                                            "Bifrost",
                                                                            hardware_counters_mali_tHEx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TDVX,
                                                                            "G31",
                                                                            "Bifrost",
                                                                            hardware_counters_mali_tDVx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TSIX,
                                                                            "G51",
                                                                            "Bifrost",
                                                                            hardware_counters_mali_tSIx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TGOX,
                                                                            "G52",
                                                                            "Bifrost",
                                                                            hardware_counters_mali_tGOx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TNOX,
                                                                            "G76",
                                                                            "Bifrost",
                                                                            hardware_counters_mali_tNOx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TNAXa,
                                                                            "G57",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tNAx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TNAXb,
                                                                            "G57",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tNAx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TTRX,
                                                                            "G77",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tTRx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TOTX,
                                                                            "G68",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tOTx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TBOX,
                                                                            "G78",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tBOx,
                                                                            false),
                                                       // Detect Mali-G78E as a specific product, but alias
                                                       // to the same underlying counter definitions as
                                                       // Mali-G78, as they are the same in both cases ...
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TBOXAE,
                                                                            "G78AE",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tBOx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TODX,
                                                                            "G710",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tODx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TVIX,
                                                                            "G610",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tVIx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TGRX,
                                                                            "G510",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tGRx,
                                                                            false),
                                                       MALI_PRODUCT_VERSION(PRODUCT_ID_MASK_NEW,
                                                                            PRODUCT_ID_TVAX,
                                                                            "G310",
                                                                            "Valhall",
                                                                            hardware_counters_mali_tVAx,
                                                                            false)};

        enum { NUM_PRODUCT_VERSIONS = COUNT_OF(PRODUCT_VERSIONS) };

        struct AccumulatedCounter {
            uint64_t sum;
            uint32_t count;

            AccumulatedCounter() : sum(0), count(0) {}

            AccumulatedCounter & operator+=(uint32_t delta)
            {
                sum += delta;
                count += 1;

                return *this;
            }

            [[nodiscard]] bool isValid() const { return count > 0; }

            [[nodiscard]] uint32_t average() const { return sum / count; }
        };

        /**
         * Map from the index'th block of a particular type to the actual block number within the list of data blocks.
         * For GPU's with legacy counter data layout
         *
         * @return The actual physical block number
         */
        inline uint32_t mapV4BlockIndexToBlockNumber(MaliCounterBlockName nameBlock, uint32_t index)
        {
            /*
             * BLOCKS ARE LAYED OUT AS:
             *
             *  0. SC 0
             *  1. SC 1
             *  2. SC 2
             *  3. SC 3
             *  4. TILER
             *  5. MMU/L2
             *  7. JOB MANAGER
             */
            switch (nameBlock) {
                case MaliCounterBlockName::JM:
                    runtime_assert(index == 0, "Unexpected block index");
                    return 7;
                case MaliCounterBlockName::TILER:
                    runtime_assert(index == 0, "Unexpected block index");
                    return 4;
                case MaliCounterBlockName::MMU:
                    runtime_assert(index == 0, "Unexpected block index");
                    return 5;
                default:
                    runtime_assert(nameBlock == MaliCounterBlockName::SHADER, "Unexpected name block");
                    runtime_assert(index < 4, "Unexpected block index");
                    return index;
            }
        }

        /**
         * Map from the index'th block of a particular type to the actual block number within the list of data blocks.
         * For GPU's with modern counter data layout
         *
         * @return The actual physical block number
         */
        inline uint32_t mapV56BlockIndexToBlockNumber(MaliCounterBlockName nameBlock,
                                                      uint32_t numL2MmuBlocks,
                                                      uint32_t numShaderBlocks,
                                                      uint32_t index)
        {
            /*
             * BLOCKS ARE LAYED OUT AS:
             *
             * 0.         JOB MANAGER
             * 1.         TILER
             * 2 + 0.     MMU/L2 0
             *   + 1.     MMU/L2 1
             *            ...
             * 2 + n + 0. SC 0
             *       + 1. SC 1
             *            ...
             */
            switch (nameBlock) {
                case MaliCounterBlockName::JM:
                    runtime_assert(index == 0, "Unexpected block index");
                    return 0;
                case MaliCounterBlockName::TILER:
                    runtime_assert(index == 0, "Unexpected block index");
                    return 1;
                case MaliCounterBlockName::MMU:
                    runtime_assert(index < numL2MmuBlocks, "Unexpected block index");
                    return 2 + index;
                default:
                    runtime_assert(nameBlock == MaliCounterBlockName::SHADER, "Unexpected name block");
                    runtime_assert(index < numShaderBlocks, "Unexpected block index");
                    return 2 + numL2MmuBlocks + index;
            }
        }

        constexpr uint32_t mapNameBlockToIndex(MaliCounterBlockName nameBlock) { return uint32_t(nameBlock); }

        const MaliProductVersion * findMaliProductRecordFromId(uint32_t productId)
        {
            for (const auto & index : PRODUCT_VERSIONS) {
                if ((productId & index.mGpuIdMask) == index.mGpuIdValue) {
                    return &index;
                }
            }
            return nullptr;
        }
    }

    MaliDeviceCounterList::MaliDeviceCounterList(uint32_t numBlocks, uint32_t numGroups, uint32_t numWords)
        : countersListLength(numBlocks * numGroups * numWords),
          countersListValid(0),
          countersList(new Address[countersListLength])
    {
    }

    MaliDeviceCounterList::MaliDeviceCounterList(MaliDeviceCounterList && that) noexcept
        : countersListLength(that.countersListLength),
          countersListValid(that.countersListValid),
          countersList(that.countersList)
    {
        that.countersListLength = 0;
        that.countersListValid = 0;
        that.countersList = nullptr;
    }

    MaliDeviceCounterList::~MaliDeviceCounterList() { delete[] countersList; }

    void MaliDeviceCounterList::enable(MaliCounterBlockName nameBlock,
                                       uint32_t repeatCount,
                                       uint32_t groupIndex,
                                       uint32_t wordIndex)
    {
        const size_t index = countersListValid++;

        assert(index < countersListLength);

        countersList[index].nameBlock = nameBlock;
        countersList[index].repeatCount = repeatCount;
        countersList[index].groupIndex = groupIndex;
        countersList[index].wordIndex = wordIndex;
    }

    const char * findMaliProductNameFromId(uint32_t productId)
    {
        const auto * productRecord = findMaliProductRecordFromId(productId);
        return (productRecord != nullptr ? productRecord->mName : nullptr);
    }

    std::unique_ptr<MaliDevice> MaliDevice::create(std::unique_ptr<IMaliDeviceApi> deviceApi, std::string clockPath)
    {
        if (deviceApi) {
            const std::uint32_t gpuId = deviceApi->getGpuId();
            const auto * productRecord = findMaliProductRecordFromId(gpuId);

            if (productRecord != nullptr) {
                return std::unique_ptr<MaliDevice>(
                    new MaliDevice(*productRecord, std::move(deviceApi), std::move(clockPath)));
            }
        }
        return {};
    }

    MaliDevice::MaliDevice(const MaliProductVersion & productVersion,
                           std::unique_ptr<IMaliDeviceApi> deviceApi,
                           std::string clockPath)
        : mProductVersion(productVersion),
          deviceApi(std::move(deviceApi)),
          clockPath(std::move(clockPath)),
          shaderCoreAvailabilityMask(this->deviceApi->getShaderCoreAvailabilityMask()),
          shaderCoreMaxCount(this->deviceApi->getMaxShaderCoreBlockIndex())
    {
    }

    uint32_t MaliDevice::getGpuId() const { return mProductVersion.mGpuIdValue; }

    uint32_t MaliDevice::getShaderBlockCount() const { return std::max(1U, shaderCoreMaxCount); }

    uint32_t MaliDevice::getL2MmuBlockCount() const { return std::max(1U, deviceApi->getNumberOfL2Slices()); }

    const char * MaliDevice::getProductName() const { return mProductVersion.mName; }

    const char * MaliDevice::getSupportedDeviceFamilyName() const { return mProductVersion.mProductFamilyName; }

    lib::AutoClosingFd MaliDevice::createHwCntReaderFd(std::size_t bufferCount,
                                                       std::uint32_t jmBitmask,
                                                       std::uint32_t shaderBitmask,
                                                       std::uint32_t tilerBitmask,
                                                       std::uint32_t mmuL2Bitmask,
                                                       bool & failedDueToBufferCount) const
    {
        return deviceApi->createHwCntReaderFd(bufferCount,
                                              jmBitmask,
                                              shaderBitmask,
                                              tilerBitmask,
                                              mmuL2Bitmask,
                                              failedDueToBufferCount);
    }

    const char * MaliDevice::getCounterName(uint32_t nameBlockIndex, uint32_t counterIndex) const
    {
        if ((nameBlockIndex >= getNameBlockCount()) || (counterIndex >= NUM_COUNTERS_PER_BLOCK)) {
            return nullptr;
        }

        const char * result = mProductVersion.mCounterNames[(nameBlockIndex * NUM_COUNTERS_PER_BLOCK) + counterIndex];

        if ((result == nullptr) || (result[0] == 0)) {
            return nullptr;
        }

        return result;
    }

    void MaliDevice::initCounterList(uint32_t gpuId,
                                     IMaliDeviceCounterDumpCallback & callback,
                                     MaliDeviceCounterList & list,
                                     MaliCounterBlockName nameBlock,
                                     uint32_t repeatCount)
    {
        const uint32_t nameBlockIndex = mapNameBlockToIndex(nameBlock);

        for (uint32_t groupIndex = 0; groupIndex < MaliDevice::NUM_ENABLE_GROUPS; ++groupIndex) {
            for (uint32_t wordIndex = 0; wordIndex < MaliDevice::NUM_COUNTERS_PER_ENABLE_GROUP; ++wordIndex) {
                const uint32_t counterIndex = (groupIndex * MaliDevice::NUM_COUNTERS_PER_ENABLE_GROUP) + wordIndex;
                if (counterIndex != MaliDevice::BLOCK_ENABLE_BITS_COUNTER_INDEX) {
                    if (callback.isCounterActive(nameBlockIndex, counterIndex, gpuId)) {
                        list.enable(nameBlock, repeatCount, groupIndex, wordIndex);
                    }
                }
            }
        }
    }

    MaliDeviceCounterList MaliDevice::createCounterList(IMaliDeviceCounterDumpCallback & callback) const
    {
        const uint32_t numL2MmuBlocks = getL2MmuBlockCount();
        const uint32_t numShaderBlocks = getShaderBlockCount();

        MaliDeviceCounterList result(4, NUM_ENABLE_GROUPS, NUM_COUNTERS_PER_ENABLE_GROUP);

        initCounterList(mProductVersion.mGpuIdValue, callback, result, MaliCounterBlockName::JM, 1);
        initCounterList(mProductVersion.mGpuIdValue, callback, result, MaliCounterBlockName::TILER, 1);
        initCounterList(mProductVersion.mGpuIdValue, callback, result, MaliCounterBlockName::MMU, numL2MmuBlocks);
        initCounterList(mProductVersion.mGpuIdValue, callback, result, MaliCounterBlockName::SHADER, numShaderBlocks);

        return result;
    }

    void MaliDevice::dumpAllCounters(uint32_t hardwareVersion,
                                     const MaliDeviceCounterList & counterList,
                                     const uint32_t * buffer,
                                     size_t bufferLength,
                                     IBlockCounterFrameBuilder & bufferData,
                                     IMaliDeviceCounterDumpCallback & callback) const
    {
        switch (hardwareVersion) {
            case 4: {
                dumpAllCounters_V4(counterList, buffer, bufferLength, bufferData, callback);
                break;
            }
            case 5:
            case 6: {
                dumpAllCounters_V56(counterList, buffer, bufferLength, bufferData, callback);
                break;
            }
            default: {
                static bool shownLog = false;
                if (!shownLog) {
                    shownLog = true;
                    LOG_ERROR("MaliDevice::dumpAllCounters - Cannot process hardware V%u", hardwareVersion);
                }
                break;
            }
        }
    }

    void MaliDevice::dumpAllCounters_V4(const MaliDeviceCounterList & counterList,
                                        const uint32_t * buffer,
                                        size_t bufferLength,
                                        IBlockCounterFrameBuilder & bufferData,
                                        IMaliDeviceCounterDumpCallback & callback) const
    {
        const size_t counterListSize = counterList.size();

        // we must average the shader core counters across all shader cores
        AccumulatedCounter shaderCoreCounters[NUM_COUNTERS_PER_BLOCK];

        for (size_t counterListIndex = 0; counterListIndex < counterListSize; ++counterListIndex) {
            const MaliDeviceCounterList::Address & counterAddress = counterList[counterListIndex];

            const MaliCounterBlockName nameBlock = counterAddress.nameBlock;
            const uint32_t nameBlockIndex = mapNameBlockToIndex(nameBlock);
            const bool isShaderCore = (nameBlock == MaliCounterBlockName::SHADER);

            for (uint32_t blockIndex = 0; blockIndex < counterAddress.repeatCount; ++blockIndex) {
                const uint32_t blockNumber = mapV4BlockIndexToBlockNumber(nameBlock, blockIndex);
                const size_t maskBufferIndex = (blockNumber * NUM_COUNTERS_PER_BLOCK) + BLOCK_ENABLE_BITS_COUNTER_INDEX;

                if (maskBufferIndex >= bufferLength) {
                    continue;
                }

                const uint32_t mask = buffer[maskBufferIndex];

                if (((mask & (1 << counterAddress.groupIndex)) == 0U)
                    || (isShaderCore && !(shaderCoreAvailabilityMask & (1ull << blockIndex)))) {
                    continue;
                }

                const uint32_t counterIndex =
                    (counterAddress.groupIndex * NUM_COUNTERS_PER_ENABLE_GROUP) + counterAddress.wordIndex;
                const size_t bufferIndex = (blockNumber * NUM_COUNTERS_PER_BLOCK) + counterIndex;

                if (bufferIndex >= bufferLength) {
                    continue;
                }

                const uint32_t delta = buffer[bufferIndex];

                if (counterIndex != BLOCK_ENABLE_BITS_COUNTER_INDEX) {
                    if (isShaderCore) {
                        shaderCoreCounters[counterIndex] += delta;
                    }
                    else {
                        callback.nextCounterValue(nameBlockIndex,
                                                  counterIndex,
                                                  delta,
                                                  mProductVersion.mGpuIdValue,
                                                  bufferData);
                    }
                }
            }
        }

        // now send shader core and mmu/l2 averages
        for (uint32_t shaderCounterIndex = 0; shaderCounterIndex < NUM_COUNTERS_PER_BLOCK; ++shaderCounterIndex) {
            if (shaderCoreCounters[shaderCounterIndex].isValid()) {
                callback.nextCounterValue(mapNameBlockToIndex(MaliCounterBlockName::SHADER),
                                          shaderCounterIndex,
                                          shaderCoreCounters[shaderCounterIndex].average(),
                                          mProductVersion.mGpuIdValue,
                                          bufferData);
            }
        }
    }

    void MaliDevice::dumpAllCounters_V56(const MaliDeviceCounterList & counterList,
                                         const uint32_t * buffer,
                                         size_t bufferLength,
                                         IBlockCounterFrameBuilder & bufferData,
                                         IMaliDeviceCounterDumpCallback & callback) const
    {
        const uint32_t numL2MmuBlocks = getL2MmuBlockCount();
        const uint32_t numShaderBlocks = getShaderBlockCount();
        const size_t counterListSize = counterList.size();

        // we must accumulate the mmu counters accross all mmu blocks
        AccumulatedCounter mmuL2Counters[NUM_COUNTERS_PER_BLOCK];
        // we must average the shader core counters across all shader cores
        AccumulatedCounter shaderCoreCounters[NUM_COUNTERS_PER_BLOCK];

        for (size_t counterListIndex = 0; counterListIndex < counterListSize; ++counterListIndex) {
            const MaliDeviceCounterList::Address & counterAddress = counterList[counterListIndex];

            const MaliCounterBlockName nameBlock = counterAddress.nameBlock;
            const uint32_t nameBlockIndex = mapNameBlockToIndex(nameBlock);
            const bool isShaderCore = (nameBlock == MaliCounterBlockName::SHADER);
            const bool isMMUL2 = (nameBlock == MaliCounterBlockName::MMU);

            for (uint32_t blockIndex = 0; blockIndex < counterAddress.repeatCount; ++blockIndex) {
                const uint32_t blockNumber =
                    mapV56BlockIndexToBlockNumber(nameBlock, numL2MmuBlocks, numShaderBlocks, blockIndex);
                const size_t maskBufferIndex = (blockNumber * NUM_COUNTERS_PER_BLOCK) + BLOCK_ENABLE_BITS_COUNTER_INDEX;

                if (maskBufferIndex >= bufferLength) {
                    continue;
                }

                const uint32_t mask = buffer[maskBufferIndex];

                if (((mask & (1 << counterAddress.groupIndex)) == 0U)
                    || (isShaderCore && !(shaderCoreAvailabilityMask & (1ull << blockIndex)))) {
                    continue;
                }

                const uint32_t counterIndex =
                    (counterAddress.groupIndex * NUM_COUNTERS_PER_ENABLE_GROUP) + counterAddress.wordIndex;
                const size_t bufferIndex = (blockNumber * NUM_COUNTERS_PER_BLOCK) + counterIndex;

                if (bufferIndex >= bufferLength) {
                    continue;
                }

                const uint32_t delta = buffer[bufferIndex];

                if (counterIndex != BLOCK_ENABLE_BITS_COUNTER_INDEX) {
                    if (isShaderCore) {
                        shaderCoreCounters[counterIndex] += delta;
                    }
                    else if (isMMUL2) {
                        mmuL2Counters[counterIndex] += delta;
                    }
                    else {
                        callback.nextCounterValue(nameBlockIndex,
                                                  counterIndex,
                                                  delta,
                                                  mProductVersion.mGpuIdValue,
                                                  bufferData);
                    }
                }
            }
        }

        // now send shader core and mmu/l2 averages
        for (uint32_t mmuL2CounterIndex = 0; mmuL2CounterIndex < NUM_COUNTERS_PER_BLOCK; ++mmuL2CounterIndex) {
            if (mmuL2Counters[mmuL2CounterIndex].isValid()) {
                callback.nextCounterValue(mapNameBlockToIndex(MaliCounterBlockName::MMU),
                                          mmuL2CounterIndex,
                                          mmuL2Counters[mmuL2CounterIndex].sum,
                                          mProductVersion.mGpuIdValue,
                                          bufferData);
            }
        }
        for (uint32_t shaderCounterIndex = 0; shaderCounterIndex < NUM_COUNTERS_PER_BLOCK; ++shaderCounterIndex) {
            if (shaderCoreCounters[shaderCounterIndex].isValid()) {
                callback.nextCounterValue(mapNameBlockToIndex(MaliCounterBlockName::SHADER),
                                          shaderCounterIndex,
                                          shaderCoreCounters[shaderCounterIndex].average(),
                                          mProductVersion.mGpuIdValue,
                                          bufferData);
            }
        }
    }

    void MaliDevice::insertConstants(std::set<Constant> & dest)
    {
        dest.insert(maliBusWidthBits);
        dest.insert(maliCacheSliceCount);
        dest.insert(maliShaderCoreCount);
    };

    std::map<CounterKey, int64_t> MaliDevice::getConstantValues() const
    {
        return {{maliBusWidthBits.getKey(), deviceApi->getExternalBusWidth()},
                {maliCacheSliceCount.getKey(), deviceApi->getNumberOfL2Slices()},
                {maliShaderCoreCount.getKey(), deviceApi->getNumberOfUsableShaderCores()}};
    };
}
