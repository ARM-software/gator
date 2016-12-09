/* Copyright (c) 2016 by ARM Limited. All rights reserved. */

#include "mali_userspace/MaliDevice.h"
#include "mali_userspace/MaliHwCntrNames.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

        /* memory layout for the v5 architecture */
        static const MaliCounterBlock COUNTER_LAYOUT_V5[] = { MALI_COUNTER_BLOCK( "JM",     0, MALI_NAME_BLOCK_JM ),
                                                              MALI_COUNTER_BLOCK( "TILER",  1, MALI_NAME_BLOCK_TILER ),
                                                              MALI_COUNTER_BLOCK( "MMU",    2, MALI_NAME_BLOCK_MMU ),
                                                              MALI_COUNTER_BLOCK( "SC 0",   3, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 1",   4, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 2",   5, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 3",   6, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 4",   7, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 5",   8, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 6",   9, MALI_NAME_BLOCK_SHADER ),
                                                              MALI_COUNTER_BLOCK( "SC 7",  10, MALI_NAME_BLOCK_SHADER ) };

        /* memory layout for the v6 architecture
         * - currently not used, but available for Bifrost devices that have up to 32 shader cores
         * - `#if 0` to prevent unused variable warnings */
#if 0
        static const MaliCounterBlock COUNTER_LAYOUT_V6[] = { MALI_COUNTER_BLOCK( "JM",      0, MALI_NAME_BLOCK_JM ),
                                                              MALI_COUNTER_BLOCK( "TILER",   1, MALI_NAME_BLOCK_TILER ),
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
#endif

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
            PRODUCT_ID_TSIX = 0x7000
        };

        /* supported product versions */
        static const MaliProductVersion PRODUCT_VERSIONS[] = { MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T60X, "T60x", "Midgard", hardware_counters_mali_t60x, COUNTER_LAYOUT_V4 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T62X, "T62x", "Midgard", hardware_counters_mali_t62x, COUNTER_LAYOUT_V4 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T72X, "T72x", "Midgard", hardware_counters_mali_t72x, COUNTER_LAYOUT_V4 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T76X, "T76x", "Midgard", hardware_counters_mali_t76x, COUNTER_LAYOUT_V5 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T82X, "T82x", "Midgard", hardware_counters_mali_t82x, COUNTER_LAYOUT_V5 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T83X, "T83x", "Midgard", hardware_counters_mali_t83x, COUNTER_LAYOUT_V5 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_T86X, "T86x", "Midgard", hardware_counters_mali_t86x, COUNTER_LAYOUT_V5 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_OLD, PRODUCT_ID_TFRX, "T88x", "Midgard", hardware_counters_mali_t88x, COUNTER_LAYOUT_V5 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_TMIX, "G71",  "Bifrost", hardware_counters_mali_tMIx, COUNTER_LAYOUT_V5 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_THEX, "THEx", "Bifrost", hardware_counters_mali_tHEx, COUNTER_LAYOUT_V5 ),
                                                               MALI_PRODUCT_VERSION( PRODUCT_ID_MASK_NEW, PRODUCT_ID_TSIX, "TSIx", "Bifrost", hardware_counters_mali_tSIx, COUNTER_LAYOUT_V5 ) };

        enum {
            NUM_PRODUCT_VERSIONS = COUNT_OF(PRODUCT_VERSIONS)
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

    MaliDeviceCounterList::~MaliDeviceCounterList()
    {
        delete[] countersList;
    }

    void MaliDeviceCounterList::enable(uint32_t blockIndex, uint32_t groupIndex, uint32_t wordIndex)
    {
        const size_t index = countersListValid++;

        assert(index < countersListLength);

        countersList[index].blockIndex = blockIndex;
        countersList[index].groupIndex = groupIndex;
        countersList[index].wordIndex = wordIndex;
    }

    MaliDevice * MaliDevice::create(uint32_t mpNumber, uint32_t gpuId, const char * devicePath)
    {
        for (int index = 0; index < NUM_PRODUCT_VERSIONS; ++index) {
            if ((gpuId & PRODUCT_VERSIONS[index].mGpuIdMask) == PRODUCT_VERSIONS[index].mGpuIdValue) {
                return new MaliDevice(PRODUCT_VERSIONS[index], devicePath, mpNumber, gpuId);
            }
        }
        return NULL;
    }

    MaliDevice::MaliDevice(const MaliProductVersion & productVersion, const char * devicePath, uint32_t numShaderCores, uint32_t gpuId)
        :   mProductVersion (productVersion),
            mDevicePath (strdup(devicePath)),
            mNumShaderCores (numShaderCores),
            mGpuId (gpuId)
    {
    }

    MaliDevice::~MaliDevice()
    {
        free(const_cast<char *>(mDevicePath));
    }

    uint32_t MaliDevice::getBlockCount() const
    {
        return mProductVersion.mNumCounterBlocks;
    }

    const char * MaliDevice::getCounterName(uint32_t nameBlockIndex, uint32_t counterIndex) const
    {
        if ((nameBlockIndex >= getNameBlockCount()) || (counterIndex >= NUM_COUNTERS_PER_BLOCK)) {
            return NULL;
        }

        const char * result = mProductVersion.mCounterNames[(nameBlockIndex * NUM_COUNTERS_PER_BLOCK) + counterIndex];

        if ((result == NULL) || (result[0] == 0)) {
            return NULL;
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
                        if (callback.isCounterActive(nameBlockNumber, counterIndex)) {
                            result.enable(blockIndex, groupIndex, wordIndex);
                        }
                    }
                }
            }
        }

        return result;
    }

    void MaliDevice::dumpAllCounters(const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IMaliDeviceCounterDumpCallback & callback) const
    {
        struct ShaderCoreCounter
        {
            uint64_t sum;
            uint32_t count;

            ShaderCoreCounter()
                    : sum(0),
                      count(0)
            {
            }

            ShaderCoreCounter& operator += (uint32_t delta)
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

        const size_t counterListSize = counterList.size();

        // we must average the shader core counters across all shader cores
        ShaderCoreCounter shaderCoreCounters[NUM_COUNTERS_PER_BLOCK];

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
                        callback.nextCounterValue(nameBlockNumber, counterIndex, delta);
                    }
                }
            }
        }

        // now send shader core averages
        for (uint32_t shaderCounterIndex = 0; shaderCounterIndex < NUM_COUNTERS_PER_BLOCK; ++shaderCounterIndex) {
            if (shaderCoreCounters[shaderCounterIndex].isValid()) {
                callback.nextCounterValue(MALI_NAME_BLOCK_SHADER, shaderCounterIndex, shaderCoreCounters[shaderCounterIndex].average());
            }
        }
    }

    const char * MaliDevice::getSupportedDeviceFamilyName() const
    {
        return mProductVersion.mProductFamilyName;
    }
}
