/* Copyright (C) 2016-2023 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliDevice.h"

#include "GetEventKey.h"
#include "Logging.h"
#include "device/hwcnt/sample.hpp"
#include "device/product_id.hpp"
#include "lib/Assert.h"
#include "mali_userspace/MaliHwCntrNamesGenerated.h"

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace mali_userspace {

    namespace dev = hwcpipe::device;
    namespace hwcnt = dev::hwcnt;

    // clang-tidy is complaining about the static members possibly throwing during construction, we can't recover from
    // that as it will only happy during a badalloc, so just silence the warning
    // NOLINTNEXTLINE(cert-err58-cpp)
    const Constant maliBusWidthBits = Constant(getEventKey(),
                                               "ARM_Mali-CONST_BUS_WIDTH_BITS",
                                               "Mali Constants",
                                               "Bus Width Bits",
                                               ConstantMode::PerCore);
    // NOLINTNEXTLINE(cert-err58-cpp)
    const Constant maliCacheSliceCount = Constant(getEventKey(),
                                                  "ARM_Mali-CONST_L2_SLICE_COUNT",
                                                  "Mali Constants",
                                                  "L2 Slice Count",
                                                  ConstantMode::PerCore);
    // NOLINTNEXTLINE(cert-err58-cpp)
    const Constant maliShaderCoreCount = Constant(getEventKey(),
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

#define COUNT_OF(A) (sizeof(A) / sizeof((A)[0]))
#define MALI_PRODUCT_VERSION(V, PN, FN, CN, V4)                                                                        \
    MaliProductVersion                                                                                                 \
    {                                                                                                                  \
        dev::product_id(V), (PN), (FN), (CN), COUNT_OF(CN), (V4)                                                       \
    }

    namespace {
        enum {
            /* product id masks for old and new versions of the id field. NB: the T60x must be tested before anything else as it could exceptionally be
             * treated as a new style of id with product code 0x6006 */
            PRODUCT_ID_MASK_OLD = 0xffff,
            PRODUCT_ID_MASK_NEW = 0xf00f,
            /* Old style product ids */
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
            PRODUCT_ID_TVAX = 0xa004,
            PRODUCT_ID_TTUX = 0xb002,
            PRODUCT_ID_TTUX2 = 0xb003,
            PRODUCT_ID_TTIX = 0xc000,
            PRODUCT_ID_TTIX2 = 0xc001
        };

        /* supported product versions */
        // NOLINTNEXTLINE(cert-err58-cpp)
        const auto PRODUCT_VERSIONS = std::array {
            MALI_PRODUCT_VERSION(PRODUCT_ID_T72X, "T72x", "Midgard", hardware_counters_mali_t720, true),
            MALI_PRODUCT_VERSION(PRODUCT_ID_T76X, "T76x", "Midgard", hardware_counters_mali_t760, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_T82X, "T82x", "Midgard", hardware_counters_mali_t820, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_T83X, "T83x", "Midgard", hardware_counters_mali_t830, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_T86X, "T86x", "Midgard", hardware_counters_mali_t860, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TFRX, "T88x", "Midgard", hardware_counters_mali_t880, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TMIX, "G71", "Bifrost", hardware_counters_mali_g71, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_THEX, "G72", "Bifrost", hardware_counters_mali_g72, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TDVX, "G31", "Bifrost", hardware_counters_mali_g31, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TSIX, "G51", "Bifrost", hardware_counters_mali_g51, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TGOX, "G52", "Bifrost", hardware_counters_mali_g52, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TNOX, "G76", "Bifrost", hardware_counters_mali_g76, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TNAXa, "G57", "Valhall", hardware_counters_mali_g57, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TNAXb, "G57", "Valhall", hardware_counters_mali_g57, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TTRX, "G77", "Valhall", hardware_counters_mali_g77, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TOTX, "G68", "Valhall", hardware_counters_mali_g68, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TBOX, "G78", "Valhall", hardware_counters_mali_g78, false),
            // Detect Mali-G78E as a specific product, but alias
            // to the same underlying counter definitions as
            // Mali-G78, as they are the same in both cases ...
            MALI_PRODUCT_VERSION(PRODUCT_ID_TBOXAE, "G78AE", "Valhall", hardware_counters_mali_g78, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TODX, "G710", "Valhall", hardware_counters_mali_g710, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TVIX, "G610", "Valhall", hardware_counters_mali_g610, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TGRX, "G510", "Valhall", hardware_counters_mali_g510, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TVAX, "G310", "Valhall", hardware_counters_mali_g310, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TTUX, "G715", "Valhall", hardware_counters_mali_g715, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TTUX2, "G615", "Valhall", hardware_counters_mali_g615, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TTIX, "G720", "Arm GPU Gen5", hardware_counters_mali_g720, false),
            MALI_PRODUCT_VERSION(PRODUCT_ID_TTIX2, "G620", "Arm GPU Gen5", hardware_counters_mali_g620, false)};

        enum { NUM_PRODUCT_VERSIONS = COUNT_OF(PRODUCT_VERSIONS) };

        constexpr uint32_t mapNameBlockToIndex(MaliCounterBlockName nameBlock)
        {
            return uint32_t(nameBlock);
        }

        const MaliProductVersion * findMaliProductRecordFromId(uint32_t id)
        {
            auto product = dev::product_id(id);

            for (const auto & index : PRODUCT_VERSIONS) {
                if (static_cast<std::uint32_t>(index.product_id) == static_cast<std::uint32_t>(product)) {
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
        counters_by_block[hwcnt::block_type::fe] = {};
        counters_by_block[hwcnt::block_type::tiler] = {};
        counters_by_block[hwcnt::block_type::memory] = {};
        counters_by_block[hwcnt::block_type::core] = {};
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

    MaliDeviceCounterList::~MaliDeviceCounterList()
    {
        delete[] countersList;
    }

    void MaliDeviceCounterList::enable(MaliCounterBlockName nameBlock,
                                       // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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

        hwcnt::block_type type;
        switch (nameBlock) {
            case MaliCounterBlockName::JM:
                type = hwcnt::block_type::fe;
                break;
            case MaliCounterBlockName::TILER:
                type = hwcnt::block_type::tiler;
                break;
            case MaliCounterBlockName::SHADER:
                type = hwcnt::block_type::core;
                break;
            case MaliCounterBlockName::MMU:
                type = hwcnt::block_type::memory;
                break;
            default:
                LOG_ERROR("Unsupported counter block type: %u", static_cast<std::uint32_t>(nameBlock));
                return;
        }
        counters_by_block[type].push_back(countersList[index]);
    }

    lib::Span<const MaliDeviceCounterList::Address> MaliDeviceCounterList::operator[](hwcnt::block_type type) const
    {
        auto it = counters_by_block.find(type);
        if (it != counters_by_block.end()) {
            return it->second;
        }
        LOG_ERROR("Unsupported counter block type: %" PRIu8 ". Returning an empty counter address list.",
                  static_cast<std::uint8_t>(type));
        return {};
    }

    const char * findMaliProductNameFromId(uint32_t productId)
    {
        const auto * productRecord = findMaliProductRecordFromId(productId);
        return (productRecord != nullptr ? productRecord->mName : nullptr);
    }

    std::unique_ptr<MaliDevice> MaliDevice::create(dev::handle::handle_ptr handle, std::string clockPath)
    {
        auto instance = dev::instance::create(*handle);
        if (!instance) {
            LOG_ERROR("Mali device instance creation failed.");
            return {};
        }

        auto product_id = dev::product_id(instance->get_constants().gpu_id);

        const auto * productRecord = findMaliProductRecordFromId(product_id);
        if (productRecord != nullptr) {
            return std::unique_ptr<MaliDevice>(
                new MaliDevice(*productRecord, std::move(handle), std::move(instance), std::move(clockPath)));
        }
        return {};
    }

    MaliDevice::MaliDevice(const MaliProductVersion & productVersion,
                           dev::handle::handle_ptr handle,
                           dev::instance::instance_ptr instance,
                           std::string clockPath)
        : mProductVersion(productVersion),
          handle(std::move(handle)),
          instance(std::move(instance)),
          clockPath(std::move(clockPath))
    {
        using namespace std::placeholders;

        const auto constants = this->instance->get_constants();
        shaderCoreMaxCount = static_cast<std::uint32_t>(constants.num_shader_cores);

        const auto extents = this->instance->get_hwcnt_block_extents();
        const auto counter_type = extents.values_type();

        block_metadata.num_counters_per_block = extents.counters_per_block();
        block_metadata.num_counters_per_enable_group = 4;
        block_metadata.num_enable_groups = extents.counters_per_block() / 4;

        if (counter_type == hwcnt::sample_values_type::uint32) {
            counter_accumulator = [this](auto const & counter_list, auto const & metadata, auto counters) {
                accumulate_counters<std::uint32_t>(counter_list, metadata, counters);
            };
            delta_writer =
                [this](auto const & counter_list, auto const & metadata, auto & frame_builder, auto & callback) {
                    dump_delta_counters<std::uint32_t>(counter_list, metadata, frame_builder, callback);
                };
        }
        else if (counter_type == hwcnt::sample_values_type::uint64) {
            counter_accumulator = [this](auto const & counter_list, auto const & metadata, auto counters) {
                accumulate_counters<std::uint64_t>(counter_list, metadata, counters);
            };
            delta_writer =
                [this](auto const & counter_list, auto const & metadata, auto & frame_builder, auto & callback) {
                    dump_delta_counters<std::uint64_t>(counter_list, metadata, frame_builder, callback);
                };
        }
        else {
            LOG_ERROR("Unsupported counter values type: %" PRIu8, static_cast<std::uint8_t>(counter_type));
            handleException();
        }

        //Log Mali GPU information
        const auto * product_record = findMaliProductRecordFromId(constants.gpu_id);

        std::ostringstream log_output;

        log_output << "Mali GPU Counters\nSuccessfully probed Mali Device";

        if (product_record->mName != nullptr) {
            log_output << " as Mali-" << product_record->mName << " (0x" << std::hex << constants.gpu_id << std::dec
                       << ")";
        }
        else {
            log_output << "but it is not recognised (id: 0x" << std::hex << constants.gpu_id;
        }

        log_output << ", " << constants.num_l2_slices << " L2 Slices, ";
        log_output << constants.axi_bus_width << "-bit Bus, ";
        log_output << constants.num_shader_cores << " Shader Cores";

        if (constants.shader_core_mask != ((1ULL << constants.num_shader_cores) - 1)) {
            log_output << " (sparse layout, mask is 0x" << std::hex << constants.shader_core_mask << std::dec << ")";
        }

        if (product_record->mName != nullptr) {
            log_output << ".";
        }
        else {
            log_output << "). Please try updating your verson of gatord.";
        }

        LOG_SETUP(log_output.str());
    }

    uint32_t MaliDevice::getGpuId() const
    {
        return static_cast<std::uint32_t>(mProductVersion.product_id);
    }

    uint32_t MaliDevice::getShaderBlockCount() const
    {
        return std::max(1U, static_cast<std::uint32_t>(instance->get_constants().num_shader_cores));
    }

    uint32_t MaliDevice::getL2MmuBlockCount() const
    {
        return std::max(1U, static_cast<std::uint32_t>(instance->get_constants().num_l2_slices));
    }

    const char * MaliDevice::getProductName() const
    {
        return mProductVersion.mName;
    }

    const char * MaliDevice::getSupportedDeviceFamilyName() const
    {
        return mProductVersion.mProductFamilyName;
    }

    const char * MaliDevice::getCounterName(uint32_t nameBlockIndex, uint32_t counterIndex) const
    {
        if ((nameBlockIndex >= getNameBlockCount()) || (counterIndex >= block_metadata.num_counters_per_block)) {
            return nullptr;
        }

        const char * result =
            mProductVersion.mCounterNames[(nameBlockIndex * block_metadata.num_counters_per_block) + counterIndex];

        if ((result == nullptr) || (result[0] == 0)) {
            return nullptr;
        }

        return result;
    }

    void MaliDevice::initCounterList(uint32_t gpuId,
                                     IMaliDeviceCounterDumpCallback & callback,
                                     MaliDeviceCounterList & list,
                                     MaliCounterBlockName nameBlock,
                                     uint32_t repeatCount) const
    {
        const uint32_t nameBlockIndex = mapNameBlockToIndex(nameBlock);

        for (uint32_t groupIndex = 0; groupIndex < block_metadata.num_enable_groups; ++groupIndex) {
            for (uint32_t wordIndex = 0; wordIndex < block_metadata.num_counters_per_enable_group; ++wordIndex) {
                const uint32_t counterIndex = (groupIndex * block_metadata.num_counters_per_enable_group) + wordIndex;
                if (counterIndex != block_metadata_t::block_enable_bits_counter_index) {
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

        MaliDeviceCounterList result(4, block_metadata.num_enable_groups, block_metadata.num_counters_per_enable_group);

        auto gpu_id = static_cast<std::uint32_t>(mProductVersion.product_id);
        initCounterList(gpu_id, callback, result, MaliCounterBlockName::JM, 1);
        initCounterList(gpu_id, callback, result, MaliCounterBlockName::TILER, 1);
        initCounterList(gpu_id, callback, result, MaliCounterBlockName::MMU, numL2MmuBlocks);
        initCounterList(gpu_id, callback, result, MaliCounterBlockName::SHADER, numShaderBlocks);

        return result;
    }

    void MaliDevice::dumpCounters(const MaliDeviceCounterList & counter_list,
                                  const hwcnt::sample & sample,
                                  bool has_block_state_feature,
                                  IBlockCounterFrameBuilder & buffer_data,
                                  IMaliDeviceCounterDumpCallback & callback) const
    {
        std::vector<AccumulatedCounter> shader_core_counters(block_metadata.num_counters_per_block);
        std::vector<AccumulatedCounter> l2_counters(block_metadata.num_counters_per_block);

        bool has_l2_counters = false;
        bool already_logged = false;

        for (auto it : sample.blocks()) {
            switch (it.type) {
                case hwcnt::block_type::fe:
                case hwcnt::block_type::tiler:
                    delta_writer(counter_list, it, buffer_data, callback);
                    break;

                case hwcnt::block_type::core: {
                    // skip over any absent shader core blocks based on the availabilty mask
                    const bool available =
                        has_block_state_feature ? (it.state.on != 0 && it.state.available != 0) : true;
                    if (available) {
                        counter_accumulator(counter_list, it, shader_core_counters);
                    }
                } break;

                case hwcnt::block_type::memory:
                    has_l2_counters = true;
                    counter_accumulator(counter_list, it, l2_counters);
                    break;
                default:
                    if (!already_logged) {
                        LOG_ERROR("Received unknown counter block type: %u", static_cast<std::uint32_t>(it.type));
                        already_logged = true;
                    }
            }
        }

        // now send shader core sums
        for (std::size_t i = 0; i < block_metadata.num_counters_per_block; ++i) {
            auto & counter = shader_core_counters[i];
            if (counter.isValid()) {
                callback.nextCounterValue(mapNameBlockToIndex(MaliCounterBlockName::SHADER),
                                          i,
                                          counter.sum,
                                          static_cast<std::uint32_t>(mProductVersion.product_id),
                                          buffer_data);
            }
        }

        // and l2 counters if the device supports them
        if (has_l2_counters) {
            for (std::size_t i = 0; i < block_metadata.num_counters_per_block; ++i) {
                auto & counter = l2_counters[i];
                if (counter.isValid()) {
                    callback.nextCounterValue(mapNameBlockToIndex(MaliCounterBlockName::MMU),
                                              i,
                                              counter.sum,
                                              static_cast<std::uint32_t>(mProductVersion.product_id),
                                              buffer_data);
                }
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
        const auto constants = instance->get_constants();
        return {{maliBusWidthBits.getKey(), static_cast<std::uint32_t>(constants.axi_bus_width)},
                {maliCacheSliceCount.getKey(), static_cast<std::uint32_t>(constants.num_l2_slices)},
                {maliShaderCoreCount.getKey(), static_cast<std::uint32_t>(constants.num_shader_cores)}};
    };
}
