/* Copyright (c) 2016 by ARM Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

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
         */
        virtual void nextCounterValue(uint32_t nameBlockIndex, uint32_t counterIndex, uint64_t delta) = 0;

        /**
         * Used to check if the user selected the counter in the config dialog when building the fast lookup list
         *
         * @param nameBlockIndex
         * @param counterIndex
         * @return True if the user selected the counter, false otherwise
         */
        virtual bool isCounterActive(uint32_t nameBlockIndex, uint32_t counterIndex) const = 0;
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

        ~MaliDevice();

        /**
         * Factory method, returns a pointer to a heap allocated object which the caller must take ownership of.
         *
         * @param mpNumber
         * @param gpuId
         * @param devicePath
         * @param clockPath (Which may be null)
         * @return The MaliDevice object, or NULL on failure
         */
        static MaliDevice * create(uint32_t mpNumber, uint32_t gpuId, const char * devicePath, const char * clockPath);

        /**
         * @return The path to the device file
         */
        inline const char * getDevicePath() const
        {
            return mDevicePath;
        }

        /**
         * @return The path to the clock file
         */
        inline const char * getClockPath() const
        {
            return mClockPath;
        }

        /**
         * @return The number of shader cores
         */
        inline uint32_t getNumberOfShaderCores() const
        {
            return mNumShaderCores;
        }

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
        void dumpAllCounters(uint32_t hardwareVersion, uint32_t mmul2BlockCount, const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IMaliDeviceCounterDumpCallback & callback) const;

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
        const char * getSupportedDeviceFamilyName() const;

    private:

        /** Internal product version counter information */
        const MaliProductVersion & mProductVersion;

        /** The path to the /dev/mali device */
        const char * const mDevicePath;

        /** The path to the /sys/class/misc/mali0/device/clock file used to read GPU clock frequency */
        const char * const mClockPath;

        /** The number of shader cores */
        const uint32_t mNumShaderCores;

        /** The GPU ID code */
        const uint32_t mGpuId;

        MaliDevice(const MaliProductVersion & productVersion, const char * devicePath, const char * clockPath, uint32_t numShaderCores, uint32_t gpuId);

        CLASS_DELETE_COPY_MOVE(MaliDevice);

        /**
         * Dump all the counter data on V4 layout
         *
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters_V4(const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IMaliDeviceCounterDumpCallback & callback) const;

        /**
         * Dump all the counter data encoded in the provided sample buffer, passing it to the callback object
         *
         * @param mmul2BlockCount
         * @param counterList
         * @param buffer
         * @param bufferLength
         * @param callback
         */
        void dumpAllCounters_V56(uint32_t mmul2BlockCount, const MaliDeviceCounterList & counterList, const uint32_t * buffer, size_t bufferLength, IMaliDeviceCounterDumpCallback & callback) const;
    };
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_ */
