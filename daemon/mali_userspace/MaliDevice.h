/* Copyright (C) 2016-2020 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_

#include "IBuffer.h"
#include "lib/AutoClosingFd.h"
#include "mali_userspace/MaliDeviceApi.h"

#include <assert.h>
#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace mali_userspace {
    /* forward declarations */
    struct MaliProductVersion;
    class MaliDevice;
    enum class MaliCounterBlockName : uint32_t;

    /**
     * Interface implemented by counter value receiver; the object that is passed data by MaliDevice::dumpAllCounters
     */
    struct IMaliDeviceCounterDumpCallback {
        virtual ~IMaliDeviceCounterDumpCallback();

        /**
         * Receive the next counter value
         *
         * @param nameBlockIndex
         * @param counterIndex
         * @param delta
         * @param gpuId
         */
        virtual void nextCounterValue(uint32_t nameBlockIndex,
                                      uint32_t counterIndex,
                                      uint64_t delta,
                                      uint32_t gpuId,
                                      IBuffer & buffer) = 0;

        /**
         * Used to check if the user selected the counter in the config dialog when building the fast lookup list
         *
         * @param nameBlockIndex
         * @param counterIndex
         * @param gpuId
         * @return True if the user selected the counter, false otherwise
         */
        virtual bool isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex, uint32_t gpuId) const = 0;
    };

    /**
     * Contains the list of counters to read. Used to improve performance during read instead of having to iterate
     * over all possible counter entries
     */
    class MaliDeviceCounterList {
    public:
        /** An address of an enabled counter */
        struct Address {
            MaliCounterBlockName nameBlock;
            uint32_t repeatCount;
            uint32_t groupIndex;
            uint32_t wordIndex;
        };

        /**
             * Constructor
             *
             * @param numBlocks
             * @param numGroups
             * @param numWords
             */
        MaliDeviceCounterList(uint32_t numBlocks, uint32_t numGroups, uint32_t numWords);
        MaliDeviceCounterList(MaliDeviceCounterList &&);
        ~MaliDeviceCounterList();

        /** @return The number of enabled counters in the list */
        size_t size() const { return countersListValid; }

        /** @return The `index`th item in the list */
        const Address & operator[](size_t index) const
        {
            assert(index < countersListValid);

            return countersList[index];
        }

    private:
        size_t countersListLength;
        size_t countersListValid;
        Address * countersList;

        /* It can call enable */
        friend class MaliDevice;

        /**
             * Mark counter at particular address as enabled
             *
             * @param nameBlockIndex
             * @param repeatCount
             * @param groupIndex
             * @param wordIndex
             */
        void enable(MaliCounterBlockName nameBlock, uint32_t repeatCount, uint32_t groupIndex, uint32_t wordIndex);

        MaliDeviceCounterList(const MaliDeviceCounterList &) = delete;
        MaliDeviceCounterList & operator=(const MaliDeviceCounterList &) = delete;
    };

    /**
     * Mali device object, manages properties of the device, and data buffer
     */
    class MaliDevice {
    public:
        enum {
            /** The number of counters with a block */
            NUM_COUNTERS_PER_BLOCK = 64,
            /** The number of counters that grouped under the same flag within an enable block */
            NUM_COUNTERS_PER_ENABLE_GROUP = 4,
            /** The number of counter enable groups with a block */
            NUM_ENABLE_GROUPS = NUM_COUNTERS_PER_BLOCK / NUM_COUNTERS_PER_ENABLE_GROUP,
            /** The counter index of the enable bits with a block */
            BLOCK_ENABLE_BITS_COUNTER_INDEX = 2
        };

        /**
         * Factory method, returns a pointer to a heap allocated object which the caller must take ownership of.
         *
         * @param deviceApi
         * @param clockPath (Which may be empty meaning no clock)
         * @return The MaliDevice object, or NULL on failure
         */
        static std::unique_ptr<MaliDevice> create(std::unique_ptr<IMaliDeviceApi> deviceApi, std::string clockPath);

        /**
         * @return The path to the clock file
         */
        inline std::string getClockPath() const { return clockPath; }

        /**
         * @return the gpuid of the device
         */
        uint32_t getGpuId() const;

        /**
         * @return The number of shader blocks
         */
        uint32_t getShaderBlockCount() const;

        /**
         * @return The number of l2/mmu blocks
         */
        uint32_t getL2MmuBlockCount() const;

        /**
         * @return The number of counter name blocks
         */
        inline uint32_t getNameBlockCount() const
        {
            return 4; // currently this is always 4
        }

        /**
         * @return The family name of the device
         */
        const char * getProductName() const;

        /**
         * @return The family name of the device
         */
        const char * getSupportedDeviceFamilyName() const;

        /**
         * Get the name of the counter for the given block and index
         *
         * @param nameBlockIndex
         * @param counterIndex
         * @return The name of the counter, or NULL if invalid combination of indexes
         */
        const char * getCounterName(uint32_t nameBlockIndex, uint32_t counterIndex) const;

        /**
         * Create the active counter list
         *
         * @param callback
         * @return The list of active counters
         */
        MaliDeviceCounterList createCounterList(IMaliDeviceCounterDumpCallback & callback) const;

        /**
         * Dump all the counter data encoded in the provided sample buffer, passing it to the callback object
         *
         * @param hardwareVersion
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters(uint32_t hardwareVersion,
                             const MaliDeviceCounterList & counterList,
                             const uint32_t * buffer,
                             size_t bufferLength,
                             IBuffer & bufferData,
                             IMaliDeviceCounterDumpCallback & callback) const;

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
        lib::AutoClosingFd createHwCntReaderFd(std::size_t bufferCount,
                                               std::uint32_t jmBitmask,
                                               std::uint32_t shaderBitmask,
                                               std::uint32_t tilerBitmask,
                                               std::uint32_t mmuL2Bitmask,
                                               bool & failedDueToBufferCount) const;

    private:
        /** Init a block in the enable list */
        static void initCounterList(uint32_t gpuId,
                                    IMaliDeviceCounterDumpCallback & callback,
                                    MaliDeviceCounterList & list,
                                    MaliCounterBlockName block,
                                    uint32_t repeatCount);

        /** Internal product version counter information */
        const MaliProductVersion & mProductVersion;

        /** The path to the /dev/mali device */
        const std::unique_ptr<IMaliDeviceApi> deviceApi;

        /** The path to the /sys/class/misc/mali0/device/clock file used to read GPU clock frequency */
        const std::string clockPath;

        MaliDevice(const MaliProductVersion & productVersion,
                   std::unique_ptr<IMaliDeviceApi> deviceApi,
                   std::string clockPath);

        MaliDevice(const MaliDevice &) = delete;
        MaliDevice & operator=(const MaliDevice &) = delete;
        MaliDevice(MaliDevice &&) = delete;
        MaliDevice & operator=(MaliDevice &&) = delete;

        /**
         * Dump all the counter data on V4 layout
         *
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters_V4(const MaliDeviceCounterList & counterList,
                                const uint32_t * buffer,
                                size_t bufferLength,
                                IBuffer & bufferData,
                                IMaliDeviceCounterDumpCallback & callback) const;

        /**
         * Dump all the counter data encoded in the provided sample buffer, passing it to the callback object
         *
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters_V56(const MaliDeviceCounterList & counterList,
                                 const uint32_t * buffer,
                                 size_t bufferLength,
                                 IBuffer & bufferData,
                                 IMaliDeviceCounterDumpCallback & callback) const;
    };

    /**
     * Map a product id to a product name string
     *
     * @param productId
     * @return The product name string, or null if not recognized
     */
    const char * findMaliProductNameFromId(uint32_t productId);
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_ */
