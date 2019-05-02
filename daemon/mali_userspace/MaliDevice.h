/* Copyright (c) 2016 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <memory>
#include "IBuffer.h"
#include "ClassBoilerPlate.h"

namespace mali_userspace
{
    /* forward declarations */
    struct MaliProductVersion;
    class MaliDevice;

    /**
     * Interface implemented by counter value receiver; the object that is passed data by MaliDevice::dumpAllCounters
     */
    struct IMaliDeviceCounterDumpCallback
    {
        virtual ~IMaliDeviceCounterDumpCallback();

        /**
         * Receive the next counter value
         *
         * @param nameBlockIndex
         * @param counterIndex
         * @param delta
         * @param gpuId
         */
        virtual void nextCounterValue(uint32_t nameBlockIndex, uint32_t counterIndex, uint64_t delta, uint32_t gpuId, IBuffer& buffer) = 0;

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
    class MaliDeviceCounterList
    {
        public:

            /** An address of an enabled counter */
            struct Address
            {
                uint32_t blockIndex;
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
            size_t size() const
            {
                return countersListValid;
            }

            /** @return The `index`th item in the list */
            const Address & operator[] (size_t index) const
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
             * @param blockIndex
             * @param groupIndex
             * @param wordIndex
             */
            void enable(uint32_t blockIndex, uint32_t groupIndex, uint32_t wordIndex);

            CLASS_DELETE_COPY(MaliDeviceCounterList);
    };

    /**
     * Mali device object, manages properties of the device, and data buffer
     */
    class MaliDevice
    {
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
         * Using the provided product name string, attempt to map it to some recognized product GPU ID.
         * The name provided must match the pattern /(Mali[ -])?([GT]\d+)/
         * Matching is case insensitive.
         *
         * @param productName The name to look up
         * @return The matched product GPU ID, or 0 if not found
         */
        static uint32_t findProductByName(const char * productName);

        /**
         * Factory method, returns a pointer to a heap allocated object which the caller must take ownership of.
         *
         * @param gpuId
         * @param devicePath
         * @param clockPath (Which may be null)
         * @return The MaliDevice object, or NULL on failure
         */
        static std::unique_ptr<MaliDevice>  create(uint32_t gpuId, std::string devicePath, std::string clockPath);

        /**
         * @return The path to the device file
         */
        inline std::string getDevicePath() const
        {
            return mDevicePath;
        }
        /**
         * @return The path to the clock file
         */
        inline std::string  getClockPath() const
        {
            return mClockPath;
        }
        /**
         * @return the gpuid of the device
         */
        uint32_t getGPUId() const;

        /**
         * @return The number of counter blocks
         */
        uint32_t getBlockCount() const;

        /**
         * @return The number of counter name blocks
         */
        inline uint32_t getNameBlockCount() const
        {
            return 4; // currently this is always 4
        }

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
         * @param mmul2BlockCount
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters(uint32_t hardwareVersion, uint32_t mmul2BlockCount, const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IBuffer& bufferData , IMaliDeviceCounterDumpCallback & callback) const;

        /**
         * Count all the blocks with the mask field set to non-zero value
         *
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        unsigned probeBlockMaskCount(const uint32_t * buffer, size_t bufferLength) const;

        /**
         * @return The family name of the device
         */
        const char* getProductName() const;

        /**
         * @return The family name of the device
         */
        const char* getSupportedDeviceFamilyName() const;

    private:

        /** Internal product version counter information */
        const MaliProductVersion & mProductVersion;

        /** The path to the /dev/mali device */
        const std::string mDevicePath;

        /** The path to the /sys/class/misc/mali0/device/clock file used to read GPU clock frequency */
        const std::string mClockPath;

        MaliDevice(const MaliProductVersion & productVersion, std::string devicePath, std::string clockPath);

        CLASS_DELETE_COPY_MOVE(MaliDevice);

        /**
         * Dump all the counter data on V4 layout
         *
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters_V4(const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IBuffer& bufferData, IMaliDeviceCounterDumpCallback & callback) const;

        /**
         * Dump all the counter data encoded in the provided sample buffer, passing it to the callback object
         *
         * @param mmul2BlockCount
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters_V56(uint32_t mmul2BlockCount, const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IBuffer& bufferData, IMaliDeviceCounterDumpCallback & callback) const;
    };
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_ */
