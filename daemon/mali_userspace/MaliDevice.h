/* Copyright (C) 2016-2023 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_

#include "Constant.h"
#include "IBlockCounterFrameBuilder.h"
#include "Logging.h"
#include "device/handle.hpp"
#include "device/hwcnt/block_metadata.hpp"
#include "device/hwcnt/sample.hpp"
#include "device/instance.hpp"
#include "device/product_id.hpp"
#include "lib/AutoClosingFd.h"
#include "lib/Span.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

namespace mali_userspace {
    /* forward declarations */
    class MaliDevice;
    enum class MaliCounterBlockName : uint32_t;

    struct MaliProductVersion {
        hwcpipe::device::product_id product_id;
        const char * mName;
        const char * mProductFamilyName;
        const char * const * mCounterNames;
        uint32_t mNumCounterNames;
        bool mLegacyLayout;
    };

    /**
     * Interface implemented by counter value receiver; the object that is passed data by MaliDevice::dumpAllCounters
     */
    struct IMaliDeviceCounterDumpCallback {
        virtual ~IMaliDeviceCounterDumpCallback() = default;

        /**
         * Receive the next counter value
         */
        virtual void nextCounterValue(uint32_t nameBlockIndex,
                                      uint32_t counterIndex,
                                      uint64_t delta,
                                      uint32_t gpuId,
                                      IBlockCounterFrameBuilder & buffer) = 0;

        /**
         * Used to check if the user selected the counter in the config dialog when building the fast lookup list
         *
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
             */
        MaliDeviceCounterList(uint32_t numBlocks, uint32_t numGroups, uint32_t numWords);
        MaliDeviceCounterList(MaliDeviceCounterList &&) noexcept;
        MaliDeviceCounterList(const MaliDeviceCounterList &) = delete;
        MaliDeviceCounterList & operator=(const MaliDeviceCounterList &) = delete;
        ~MaliDeviceCounterList();

        /** @return The number of enabled counters in the list */
        size_t size() const { return countersListValid; }

        /** @return The `index`th item in the list */
        const Address & operator[](size_t index) const
        {
            assert(index < countersListValid);

            return countersList[index];
        }

        lib::Span<const Address> operator[](hwcpipe::device::hwcnt::block_type type) const;

    private:
        using counters_by_block_map_t = std::map<hwcpipe::device::hwcnt::block_type, std::vector<Address>>;

        size_t countersListLength;
        size_t countersListValid;
        Address * countersList;
        counters_by_block_map_t counters_by_block;

        /* It can call enable */
        friend class MaliDevice;

        /**
             * Mark counter at particular address as enabled
             */
        void enable(MaliCounterBlockName nameBlock, uint32_t repeatCount, uint32_t groupIndex, uint32_t wordIndex);
    };

    /**
     * Mali device object, manages properties of the device, and data buffer
     */
    class MaliDevice {
    public:
        struct block_metadata_t {
            std::size_t num_counters_per_block;
            unsigned int num_counters_per_enable_group;
            unsigned int num_enable_groups;
            static constexpr unsigned int block_enable_bits_counter_index = 2;
        };

        /**
         * Factory method, returns a pointer to a heap allocated object which the caller must take ownership of.
         *
         * @param clockPath (Which may be empty meaning no clock)
         * @return The MaliDevice object, or nullptr on failure
         */
        static std::unique_ptr<MaliDevice> create(hwcpipe::device::handle::handle_ptr handle, std::string clockPath);

        MaliDevice(const MaliDevice &) = delete;
        MaliDevice & operator=(const MaliDevice &) = delete;
        MaliDevice(MaliDevice &&) = delete;
        MaliDevice & operator=(MaliDevice &&) = delete;

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
            return static_cast<std::uint32_t>(hwcpipe::device::hwcnt::block_type::num_block_types);
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
         * @return The name of the counter, or nullptr if invalid combination of indexes
         */
        const char * getCounterName(uint32_t nameBlockIndex, uint32_t counterIndex) const;

        /**
         * Create the active counter list
         *
         * @return The list of active counters
         */
        MaliDeviceCounterList createCounterList(IMaliDeviceCounterDumpCallback & callback) const;

        /**
         * Dump all the counter data encoded in the provided sample buffer, passing it to the callback object
         */
        void dumpCounters(const MaliDeviceCounterList & counter_list,
                          const hwcpipe::device::hwcnt::sample & sample,
                          bool has_block_state_feature,
                          IBlockCounterFrameBuilder & buffer_data,
                          IMaliDeviceCounterDumpCallback & callback) const;

        static void insertConstants(std::set<Constant> & dest);

        std::map<CounterKey, int64_t> getConstantValues() const;

        block_metadata_t get_block_metadata() const { return block_metadata; }

        std::size_t get_num_counters_per_block() const { return block_metadata.num_counters_per_block; }

    private:
        struct AccumulatedCounter {
            uint64_t sum;
            uint32_t count;

            AccumulatedCounter() : sum(0), count(0) {}

            AccumulatedCounter & operator+=(uint64_t delta)
            {
                sum += delta;
                count += 1;

                return *this;
            }

            bool isValid() const { return count > 0; }

            uint64_t average() const { return sum / count; }
        };

        using counter_delta_writer_t = std::function<void(const MaliDeviceCounterList &,
                                                          const hwcpipe::device::hwcnt::block_metadata &,
                                                          IBlockCounterFrameBuilder &,
                                                          IMaliDeviceCounterDumpCallback &)>;

        using counter_accumulator_t = std::function<void(const MaliDeviceCounterList &,
                                                         const hwcpipe::device::hwcnt::block_metadata &,
                                                         lib::Span<AccumulatedCounter>)>;

        /** Init a block in the enable list */
        void initCounterList(uint32_t gpuId,
                             IMaliDeviceCounterDumpCallback & callback,
                             MaliDeviceCounterList & list,
                             MaliCounterBlockName block,
                             uint32_t repeatCount) const;

        /** Internal product version counter information */
        const MaliProductVersion & mProductVersion;

        /** The hwcpipe device handles */
        const hwcpipe::device::handle::handle_ptr handle;
        const hwcpipe::device::instance::instance_ptr instance;

        /** The path to the /sys/class/misc/mali0/device/clock file used to read GPU clock frequency */
        const std::string clockPath;

        /** The shader core block mask */
        std::uint64_t shaderCoreAvailabilityMask;

        /** The shader core max block count */
        std::uint32_t shaderCoreMaxCount;

        block_metadata_t block_metadata;

        counter_delta_writer_t delta_writer;
        counter_accumulator_t counter_accumulator;

        MaliDevice(const MaliProductVersion & productVersion,
                   hwcpipe::device::handle::handle_ptr handle,
                   hwcpipe::device::instance::instance_ptr instance,
                   std::string clockPath);

        template<typename CounterType>
        void dump_delta_counters(const MaliDeviceCounterList & counter_list,
                                 const hwcpipe::device::hwcnt::block_metadata & block,
                                 IBlockCounterFrameBuilder & buffer_data,
                                 IMaliDeviceCounterDumpCallback & callback)
        {
            auto active_counters = counter_list[block.type];
            auto counter_values = reinterpret_cast<const CounterType *>(block.values);

            for (const auto & address : active_counters) {
                const std::uint32_t counter_index =
                    address.groupIndex * block_metadata.num_counters_per_enable_group + address.wordIndex;
                auto delta = counter_values[counter_index];

                callback.nextCounterValue(static_cast<std::uint32_t>(block.type),
                                          counter_index,
                                          delta,
                                          static_cast<std::uint32_t>(mProductVersion.product_id),
                                          buffer_data);
            }
        }

        template<typename CounterType>
        void accumulate_counters(const MaliDeviceCounterList & counter_list,
                                 const hwcpipe::device::hwcnt::block_metadata & block,
                                 lib::Span<AccumulatedCounter> counters)
        {
            auto active_counters = counter_list[block.type];
            if (active_counters.empty()) {
                return;
            }

            auto counter_values = reinterpret_cast<const CounterType *>(block.values);
            for (const auto & address : active_counters) {
                const std::uint32_t counter_index =
                    address.groupIndex * block_metadata.num_counters_per_enable_group + address.wordIndex;
                auto delta = counter_values[counter_index];
                counters[counter_index] += delta;
            }
        }
    };

    /**
     * Map a product id to a product name string
     *
     * @return The product name string, or null if not recognized
     */
    const char * findMaliProductNameFromId(uint32_t productId);
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICE_H_ */
