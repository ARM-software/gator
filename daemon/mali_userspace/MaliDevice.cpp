/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliDevice.h"
#include "mali_userspace/MaliHwCntrNames.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "Logging.h"

namespace mali_userspace
{
    struct MaliCounterBlock
    {
        const char * mName;
        uint32_t mBlockNumber;
        uint32_t mNameBlockNumber;
        uint32_t mEnablementBlockNumber;
    };

    struct MaliProductVersion
    {
        uint32_t mGpuIdMask;
        uint32_t mGpuIdValue;
        const char * mName;
        const char * mProductFamilyName;
        const char * const * mCounterNames;
        const MaliCounterBlock * mCounterBlocks;
        uint32_t mNumCounterNames;
        uint32_t mNumCounterBlocks;
    };

#define COUNT_OF(A)                             (sizeof(A) / sizeof(A[0]))
#define MALI_COUNTER_BLOCK(N, B, NB)            { (N), (B), (NB), (NB) }
#define MALI_PRODUCT_VERSION(M, V, PN, FN, CN, L)   { (M), (V), (PN), (FN), (CN), (L), COUNT_OF(CN), COUNT_OF(L) }

    namespace
    {
        /* Counter blocks */
        enum MaliCounterBlockName {
            MALI_NAME_BLOCK_JM      = 0,
            MALI_NAME_BLOCK_TILER   = 1,
            MALI_NAME_BLOCK_SHADER  = 2,
            MALI_NAME_BLOCK_MMU     = 3,
        };

        /* memory layout for the v4 architecture */
        static const MaliCounterBlock COUNTER_LAYOUT_V4[] = { MALI_COUNTER_BLOCK( "SC 0",  0, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 1",  1, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 2",  2, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 3",  3, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "TILER", 4, MALI_NAME_BLOCK_TILER ),
                                                              MALI_COUNTER_BLOCK( "MMU",   5, MALI_NAME_BLOCK_MMU ),
                                                              MALI_COUNTER_BLOCK( "JM",    7, MALI_NAME_BLOCK_JM ) };

        /* memory layout for the v6 architecture, also used by v5 architecture up to SC 7 */
        static const MaliCounterBlock COUNTER_LAYOUT_V6[] = { MALI_COUNTER_BLOCK( "JM",      0, MALI_NAME_BLOCK_JM ),
                                                              MALI_COUNTER_BLOCK( "TILER",   1, MALI_NAME_BLOCK_TILER ),
                                                              // NB: there may infact be more than one of these. we must
                                                              // manually detect that and then account for it when processing the block
                                                              // contents
                                                              MALI_COUNTER_BLOCK( "MMU",     2, MALI_NAME_BLOCK_MMU ),
                                                              MALI_COUNTER_BLOCK( "SC 0",    3, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 1",    4, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 2",    5, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 3",    6, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 4",    7, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 5",    8, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 6",    9, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 7",   10, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 8",   11, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 9",   12, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 10",  13, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 11",  14, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 12",  15, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 13",  16, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 14",  17, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 15",  18, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 16",  19, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 17",  20, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 18",  21, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 19",  22, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 20",  23, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 21",  24, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 22",  25, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 23",  26, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 24",  27, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 25",  28, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 26",  29, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 27",  30, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 28",  31, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 29",  32, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 30",  33, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 31",  34, MALI_NAME_BLOCK_SHADER ) };

        enum {
            /* product id masks for old and new versions of the id field. NB: the T60x must be tested before anything else as it could exceptionally be
             * treated as a new style of id with produce code 0x6006 */
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
            PRODUCT_ID_TDVX = 0x7003

        };

        /* supported product versions */
        static const MaliProductVersion PRODUCT_VERSIONS[] = { MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T60X, "T60x", "Midgard", hardware_counters_mali_t60x, COUNTER_LAYOUT_V4 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T62X, "T62x", "Midgard", hardware_counters_mali_t62x, COUNTER_LAYOUT_V4 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T72X, "T72x", "Midgard", hardware_counters_mali_t72x, COUNTER_LAYOUT_V4 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T76X, "T76x", "Midgard", hardware_counters_mali_t76x, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T82X, "T82x", "Midgard", hardware_counters_mali_t82x, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T83X, "T83x", "Midgard", hardware_counters_mali_t83x, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T86X, "T86x", "Midgard", hardware_counters_mali_t86x, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_TFRX, "T88x", "Midgard", hardware_counters_mali_t88x, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_TMIX, "G71",  "Bifrost", hardware_counters_mali_tMIx, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_THEX, "G72",  "Bifrost", hardware_counters_mali_tHEx, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_TSIX, "G51",  "Bifrost", hardware_counters_mali_tSIx, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_TGOX, "G52",  "Bifrost", hardware_counters_mali_tGOx, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_TDVX, "G31",  "Bifrost", hardware_counters_mali_tDVx, COUNTER_LAYOUT_V6 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_TNOX, "G76",  "Bifrost", hardware_counters_mali_tNOx, COUNTER_LAYOUT_V6 )};

        enum {
            NUM_PRODUCT_VERSIONS = COUNT_OF(PRODUCT_VERSIONS)
        };

        struct AccumulatedCounter
        {
            uint64_t sum;
            uint32_t count;

            AccumulatedCounter()
                    : sum(0),
                      count(0)
            {
            }

            AccumulatedCounter& operator += (uint32_t delta)
            {
                sum += delta;
                count += 1;

                return *this;
            }

            bool isValid() const
            {
                return count > 0;
            }

            uint32_t average() const
            {
                return sum / count;
            }
        };
    }

    IMaliDeviceCounterDumpCallback::~IMaliDeviceCounterDumpCallback()
    {
    }

    MaliDeviceCounterList::MaliDeviceCounterList(uint32_t numBlocks, uint32_t numGroups, uint32_t numWords)
        :   countersListLength(numBlocks * numGroups * numWords),
            countersListValid(0),
            countersList(new Address[countersListLength])
    {
    }

    MaliDeviceCounterList::MaliDeviceCounterList(MaliDeviceCounterList && that)
    :   countersListLength(that.countersListLength),
        countersListValid(that.countersListValid),
        countersList(that.countersList)
    {
        that.countersListLength = 0;
        that.countersListValid = 0;
        that.countersList = nullptr;
    }

    MaliDeviceCounterList::~MaliDeviceCounterList()
    {
        if (countersList != nullptr) {
            delete[] countersList;
        }
    }

    void MaliDeviceCounterList::enable(uint32_t blockIndex, uint32_t groupIndex, uint32_t wordIndex)
    {
        const size_t index = countersListValid++;

        assert(index < countersListLength);

        countersList[index].blockIndex = blockIndex;
        countersList[index].groupIndex = groupIndex;
        countersList[index].wordIndex = wordIndex;
    }

    uint32_t MaliDevice::findProductByName(const char * productName)
    {
        if (productName == nullptr) {
            return 0;
        }

        // skip prefix
        if ((strcasestr(productName, "Mali-") == productName) || (strcasestr(productName, "Mali ") == productName)) {
            productName += 5;
        }

        // dont bother if empty string
        const auto plen = strlen(productName);
        if (plen < 1) {
            return 0;
        }

        // do search
        for (int index = 0; index < NUM_PRODUCT_VERSIONS; ++index) {
            // must be same length to be equal
            const auto len = strlen(PRODUCT_VERSIONS[index].mName);
            if (len != plen) {
                continue;
            }

            // Txxx names end with 'x' so treat that as a wildcard against any digit
            if (PRODUCT_VERSIONS[index].mName[len - 1] == 'x') {
                // validate match against digit or x
                if ((productName[len - 1] != 'x') && (productName[len - 1] != 'X') && ((productName[len - 1] < '0') || (productName[len - 1] > '9'))) {
                    continue;
                }

                // compare prefix
                if (strncasecmp(PRODUCT_VERSIONS[index].mName, productName, len - 1) == 0) {
                    return PRODUCT_VERSIONS[index].mGpuIdValue;
                }
            }

            // other names must match in full
            if (strcasecmp(PRODUCT_VERSIONS[index].mName, productName) == 0) {
                return PRODUCT_VERSIONS[index].mGpuIdValue;
            }
        }

        return 0;
    }

    std::unique_ptr<MaliDevice>  MaliDevice::create(uint32_t gpuId, std::string devicePath, std::string clockPath)
    {
        for (int index = 0; index < NUM_PRODUCT_VERSIONS; ++index) {
            if ((gpuId & PRODUCT_VERSIONS[index].mGpuIdMask) == PRODUCT_VERSIONS[index].mGpuIdValue) {
                return std::unique_ptr<MaliDevice> (new MaliDevice(PRODUCT_VERSIONS[index], devicePath, clockPath));
            }
        }
        return nullptr;
    }

    MaliDevice::MaliDevice(const MaliProductVersion & productVersion,  std::string devicePath, std::string clockPath)
        :   mProductVersion (productVersion),
            mDevicePath (devicePath),
            mClockPath (clockPath)
    {
    }

    uint32_t MaliDevice::getGPUId() const
    {
        return mProductVersion.mGpuIdValue;
    }

    uint32_t MaliDevice::getBlockCount() const
    {
        return mProductVersion.mNumCounterBlocks;
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

    MaliDeviceCounterList MaliDevice::createCounterList(IMaliDeviceCounterDumpCallback & callback) const
    {
        MaliDeviceCounterList result (mProductVersion.mNumCounterBlocks, NUM_ENABLE_GROUPS, NUM_COUNTERS_PER_ENABLE_GROUP);

        for (uint32_t blockIndex = 0; blockIndex < mProductVersion.mNumCounterBlocks; ++blockIndex) {
            const uint32_t nameBlockNumber = mProductVersion.mCounterBlocks[blockIndex].mNameBlockNumber;
            for (uint32_t groupIndex = 0; groupIndex < NUM_ENABLE_GROUPS; ++groupIndex) {
                for (uint32_t wordIndex = 0; wordIndex < NUM_COUNTERS_PER_ENABLE_GROUP; ++wordIndex) {
                    const uint32_t counterIndex = (groupIndex * NUM_COUNTERS_PER_ENABLE_GROUP) + wordIndex;

                    if (counterIndex != BLOCK_ENABLE_BITS_COUNTER_INDEX) {
                        if (callback.isCounterActive(nameBlockNumber, counterIndex, mProductVersion.mGpuIdValue)) {
                            result.enable(blockIndex, groupIndex, wordIndex);
                        }
                    }
                }
            }
        }

        return result;
    }

    void MaliDevice::dumpAllCounters(uint32_t hardwareVersion, uint32_t mmul2BlockCount, const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IBuffer& bufferData, IMaliDeviceCounterDumpCallback & callback) const
    {
        switch (hardwareVersion) {
            case 4: {
                dumpAllCounters_V4(counterList, buffer, bufferLength, bufferData, callback);
                break;
            }
            case 5:
            case 6: {
                dumpAllCounters_V56(mmul2BlockCount, counterList, buffer, bufferLength, bufferData, callback);
                break;
            }
            default: {
                static bool shownLog = false;
                if (!shownLog) {
                    shownLog = true;
                    logg.logError("MaliDevice::dumpAllCounters - Cannot process hardware V%u", hardwareVersion);
                }
                break;
            }
        }
    }

    void MaliDevice::dumpAllCounters_V4(const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IBuffer& bufferData, IMaliDeviceCounterDumpCallback & callback) const
    {
        const size_t counterListSize = counterList.size();

        // we must average the shader core counters across all shader cores
        AccumulatedCounter shaderCoreCounters[NUM_COUNTERS_PER_BLOCK];

        for (size_t counterListIndex = 0; counterListIndex < counterListSize; ++counterListIndex) {
            const MaliDeviceCounterList::Address & counterAddress = counterList[counterListIndex];

            const uint32_t blockNumber = mProductVersion.mCounterBlocks[counterAddress.blockIndex].mBlockNumber;
            const uint32_t nameBlockNumber = mProductVersion.mCounterBlocks[counterAddress.blockIndex].mNameBlockNumber;
            const bool isShaderCore = (nameBlockNumber == MALI_NAME_BLOCK_SHADER);
            const size_t maskBufferIndex = (blockNumber * NUM_COUNTERS_PER_BLOCK) + BLOCK_ENABLE_BITS_COUNTER_INDEX;

            if (maskBufferIndex >= bufferLength) {
                continue;
            }

            const uint32_t mask = buffer[maskBufferIndex];

            if (mask & (1 << counterAddress.groupIndex)) {
                const uint32_t counterIndex = (counterAddress.groupIndex * NUM_COUNTERS_PER_ENABLE_GROUP) + counterAddress.wordIndex;
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
                        callback.nextCounterValue(nameBlockNumber, counterIndex, delta, mProductVersion.mGpuIdValue, bufferData);
                    }
                }
            }
        }

        // now send shader core and mmu/l2 averages
        for (uint32_t shaderCounterIndex = 0; shaderCounterIndex < NUM_COUNTERS_PER_BLOCK; ++shaderCounterIndex) {
            if (shaderCoreCounters[shaderCounterIndex].isValid()) {
                callback.nextCounterValue(MALI_NAME_BLOCK_SHADER, shaderCounterIndex, shaderCoreCounters[shaderCounterIndex].average(), mProductVersion.mGpuIdValue, bufferData);
            }
        }
    }

    void MaliDevice::dumpAllCounters_V56(uint32_t mmul2BlockCount, const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IBuffer& bufferData, IMaliDeviceCounterDumpCallback & callback) const
    {
        const size_t counterListSize = counterList.size();

        // we must accumulate the mmu counters accross all mmu blocks
        AccumulatedCounter mmuL2Counters[NUM_COUNTERS_PER_BLOCK];
        // we must average the shader core counters across all shader cores
        AccumulatedCounter shaderCoreCounters[NUM_COUNTERS_PER_BLOCK];

        for (size_t counterListIndex = 0; counterListIndex < counterListSize; ++counterListIndex) {
            const MaliDeviceCounterList::Address & counterAddress = counterList[counterListIndex];

            const uint32_t nameBlockNumber = mProductVersion.mCounterBlocks[counterAddress.blockIndex].mNameBlockNumber;
            const bool isMMUL2 = (nameBlockNumber == MALI_NAME_BLOCK_MMU);
            const bool isShaderCore = (nameBlockNumber == MALI_NAME_BLOCK_SHADER);

            // on V5/V6 there can be more than one MMU block, they are layout out contiguously starting from offset 3
            // if this is an MMU block, then repeat the call for each offset
            const uint32_t repeatCount = (isMMUL2 ? mmul2BlockCount : 1);

            for (uint32_t repeatNo = 0; repeatNo < repeatCount; ++repeatNo) {
                // the base is 'repeatNo' for MMU blocks,
                // the base is 'mmul2BlockCount - 1' for shader core blocks
                // and is zero for everything else
                const uint32_t blockNumberBase = (isMMUL2 ? repeatNo : (isShaderCore ? (mmul2BlockCount - 1): 0));
                const uint32_t blockNumber = mProductVersion.mCounterBlocks[counterAddress.blockIndex].mBlockNumber + blockNumberBase;
                const size_t maskBufferIndex = (blockNumber * NUM_COUNTERS_PER_BLOCK) + BLOCK_ENABLE_BITS_COUNTER_INDEX;

                if (maskBufferIndex >= bufferLength) {
                    continue;
                }

                const uint32_t mask = buffer[maskBufferIndex];

                if (mask & (1 << counterAddress.groupIndex)) {
                    const uint32_t counterIndex = (counterAddress.groupIndex * NUM_COUNTERS_PER_ENABLE_GROUP) + counterAddress.wordIndex;
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
                            callback.nextCounterValue(nameBlockNumber, counterIndex, delta, mProductVersion.mGpuIdValue, bufferData);
                        }
                    }
                }
            }
        }

        // now send shader core and mmu/l2 averages
        for (uint32_t mmuL2CounterIndex = 0; mmuL2CounterIndex < NUM_COUNTERS_PER_BLOCK; ++mmuL2CounterIndex) {
            if (mmuL2Counters[mmuL2CounterIndex].isValid()) {
                callback.nextCounterValue(MALI_NAME_BLOCK_MMU, mmuL2CounterIndex, mmuL2Counters[mmuL2CounterIndex].sum, mProductVersion.mGpuIdValue, bufferData);
            }
        }
        for (uint32_t shaderCounterIndex = 0; shaderCounterIndex < NUM_COUNTERS_PER_BLOCK; ++shaderCounterIndex) {
            if (shaderCoreCounters[shaderCounterIndex].isValid()) {
                callback.nextCounterValue(MALI_NAME_BLOCK_SHADER, shaderCounterIndex, shaderCoreCounters[shaderCounterIndex].average(), mProductVersion.mGpuIdValue, bufferData);
            }
        }
    }

    unsigned MaliDevice::probeBlockMaskCount(const uint32_t * buffer, size_t bufferLength) const
    {
        unsigned result = 0;
        size_t lastEnabledBlockOffset = 0;

        for (size_t offset = BLOCK_ENABLE_BITS_COUNTER_INDEX; offset < bufferLength; offset += NUM_COUNTERS_PER_BLOCK) {
            if (buffer[offset] != 0) {
                result += 1;

                // blocks are required to be contiguous
                if ((lastEnabledBlockOffset == 0) || ((lastEnabledBlockOffset + NUM_COUNTERS_PER_BLOCK) == offset)) {
                    lastEnabledBlockOffset = offset;
                }
                else {
                    logg.logError("MaliDevice::probeBlockMaskCount - non contiguous blocks found at offset 0x%lx and 0x%lx.", static_cast<unsigned long>(lastEnabledBlockOffset), static_cast<unsigned long>(offset));
                    return 0;
                }
            }
        }

        return result;
    }

    const char* MaliDevice::getProductName() const
    {
       return mProductVersion.mName;
    }

    const char* MaliDevice::getSupportedDeviceFamilyName() const
    {
        return mProductVersion.mProductFamilyName;
    }
}
