/* Copyright (C) 2016-2025 by Arm Limited. All rights reserved. */

#include "mali_userspace/MaliDevice.h"

#include "Constant.h"
#include "ConstantMode.h"
#include "GetEventKey.h"
#include "IBlockCounterFrameBuilder.h"
#include "Logging.h"
#include "device/handle.hpp"
#include "device/hwcnt/block_extents.hpp"
#include "device/hwcnt/block_metadata.hpp"
#include "device/hwcnt/features.hpp"
#include "device/hwcnt/sample.hpp"
#include "device/instance.hpp"
#include "device/product_id.hpp"
#include "lib/Span.h"
#include "libGPUInfo/source/libgpuinfo.hpp"
#include "mali_userspace/MaliHwCntrNamesGenerated.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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
#define MALI_PRODUCT_VERSION(V, PI, PN, FN, CN, V4)                                                                    \
    MaliProductVersion                                                                                                 \
    {                                                                                                                  \
        (V), (PI), (PN), ((FN).data()), (CN), COUNT_OF(CN), (V4)                                                       \
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
            PRODUCT_ID_TTIX2 = 0xc001,
            PRODUCT_ID_TKRX = 0xd000,
            PRODUCT_ID_TKRX2 = 0xd001
        };

        constexpr std::string_view MIDGARD {"Midgard"};
        constexpr std::string_view BIFROST {"Bifrost"};
        constexpr std::string_view VALHALL {"Valhall"};
        constexpr std::string_view GPUGEN5 {"Arm GPU Gen5"};

        /* supported product versions */
        // NOLINTNEXTLINE(cert-err58-cpp)
        const auto PRODUCT_VERSIONS = std::array {MALI_PRODUCT_VERSION(PRODUCT_ID_T72X,
                                                                       dev::product_id::t720,
                                                                       "T72x",
                                                                       MIDGARD,
                                                                       hardware_counters_mali_t720,
                                                                       true),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_T76X,
                                                                       dev::product_id::t760,
                                                                       "T76x",
                                                                       MIDGARD,
                                                                       hardware_counters_mali_t760,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_T82X,
                                                                       dev::product_id::t820,
                                                                       "T82x",
                                                                       MIDGARD,
                                                                       hardware_counters_mali_t820,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_T83X,
                                                                       dev::product_id::t830,
                                                                       "T83x",
                                                                       MIDGARD,
                                                                       hardware_counters_mali_t830,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_T86X,
                                                                       dev::product_id::t860,
                                                                       "T86x",
                                                                       MIDGARD,
                                                                       hardware_counters_mali_t860,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TFRX,
                                                                       dev::product_id::t880,
                                                                       "T88x",
                                                                       MIDGARD,
                                                                       hardware_counters_mali_t880,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TMIX,
                                                                       dev::product_id::g71,
                                                                       "G71",
                                                                       BIFROST,
                                                                       hardware_counters_mali_g71,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_THEX,
                                                                       dev::product_id::g72,
                                                                       "G72",
                                                                       BIFROST,
                                                                       hardware_counters_mali_g72,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TDVX,
                                                                       dev::product_id::g31,
                                                                       "G31",
                                                                       BIFROST,
                                                                       hardware_counters_mali_g31,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TSIX,
                                                                       dev::product_id::g51,
                                                                       "G51",
                                                                       BIFROST,
                                                                       hardware_counters_mali_g51,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TGOX,
                                                                       dev::product_id::g52,
                                                                       "G52",
                                                                       BIFROST,
                                                                       hardware_counters_mali_g52,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TNOX,
                                                                       dev::product_id::g76,
                                                                       "G76",
                                                                       BIFROST,
                                                                       hardware_counters_mali_g76,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TNAXa,
                                                                       dev::product_id::g57,
                                                                       "G57",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g57,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TNAXb,
                                                                       dev::product_id::g57_2,
                                                                       "G57",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g57,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TTRX,
                                                                       dev::product_id::g77,
                                                                       "G77",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g77,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TOTX,
                                                                       dev::product_id::g68,
                                                                       "G68",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g68,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TBOX,
                                                                       dev::product_id::g78,
                                                                       "G78",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g78,
                                                                       false),
                                                  // Detect Mali-G78E as a specific product, but alias
                                                  // to the same underlying counter definitions as
                                                  // Mali-G78, as they are the same in both cases ...
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TBOXAE,
                                                                       dev::product_id::g78ae,
                                                                       "G78AE",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g78,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TODX,
                                                                       dev::product_id::g710,
                                                                       "G710",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g710,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TVIX,
                                                                       dev::product_id::g610,
                                                                       "G610",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g610,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TGRX,
                                                                       dev::product_id::g510,
                                                                       "G510",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g510,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TVAX,
                                                                       dev::product_id::g310,
                                                                       "G310",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g310,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TTUX,
                                                                       dev::product_id::g715,
                                                                       "G715",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g715,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TTUX2,
                                                                       dev::product_id::g615,
                                                                       "G615",
                                                                       VALHALL,
                                                                       hardware_counters_mali_g615,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TTIX,
                                                                       dev::product_id::g720,
                                                                       "G720",
                                                                       GPUGEN5,
                                                                       hardware_counters_mali_g720,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TTIX2,
                                                                       dev::product_id::g620,
                                                                       "G620",
                                                                       GPUGEN5,
                                                                       hardware_counters_mali_g620,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TKRX,
                                                                       dev::product_id::g725,
                                                                       "G725",
                                                                       GPUGEN5,
                                                                       hardware_counters_mali_g725,
                                                                       false),
                                                  MALI_PRODUCT_VERSION(PRODUCT_ID_TKRX2,
                                                                       dev::product_id::g625,
                                                                       "G625",
                                                                       GPUGEN5,
                                                                       hardware_counters_mali_g625,
                                                                       false)};

        enum { NUM_PRODUCT_VERSIONS = COUNT_OF(PRODUCT_VERSIONS) };

        constexpr uint32_t mapNameBlockToIndex(MaliCounterBlockName nameBlock)
        {
            return uint32_t(nameBlock);
        }

        const MaliProductVersion * findMaliProductRecordFromId(uint64_t gpuId)
        {
            auto [err, target_product_id] = dev::product_id_from_raw_gpu_id(gpuId);

            if (err) {
                LOG_ERROR("Failed to detect product_id\n");
                return nullptr;
            }

            LOG_FINE("Product id detected : %d", static_cast<uint32_t>(target_product_id));

            for (const auto & index : PRODUCT_VERSIONS) {
                if (index.product_id == target_product_id) {
                    return &index;
                }
            }

            LOG_ERROR("Failed to find supported product_id\n");
            return nullptr;
        }
    }

    bool maliGpuSampleRateIsUpgradeable(uint32_t gpuId)
    {
        // check GPU family (discard all from Bifrost and Midgard)
        for (const auto & maliProduct : PRODUCT_VERSIONS) {
            if (maliProduct.product_id == dev::product_id(gpuId)
                && (strcmp(maliProduct.mProductFamilyName, BIFROST.data()) == 0
                    || strcmp(maliProduct.mProductFamilyName, MIDGARD.data()) == 0)) {
                return false;
            }
        }
        return true;
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

    std::unique_ptr<MaliDevice> MaliDevice::create(dev::handle::handle_ptr handle, std::string clockPath)
    {
        auto instance = dev::instance::create(*handle);
        if (!instance) {
            LOG_ERROR("Mali device instance creation failed.");
            return {};
        }
        const auto * productRecord = findMaliProductRecordFromId(instance->get_constants().gpu_id);
        if (productRecord != nullptr) {
            return std::unique_ptr<MaliDevice>(
                new MaliDevice(*productRecord, std::move(handle), std::move(instance), std::move(clockPath)));
        }
        LOG_ERROR("Could not map gpu_id to product: %" PRIx64, instance->get_constants().gpu_id);
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
        namespace hwcnt = hwcpipe::device::hwcnt;

        const auto constants = this->instance->get_constants();
        shaderCoreMaxCount = static_cast<std::uint32_t>(constants.num_shader_cores);

        const auto extents = this->instance->get_hwcnt_block_extents();
        const auto counter_type = extents.values_type();

        block_metadata.num_counters_per_block = extents.counters_per_block();
        block_metadata.num_counters_per_enable_group = 4;
        block_metadata.num_enable_groups = extents.counters_per_block() / 4;
        block_metadata.num_block_types = [&extents]() {
            unsigned int count = 0;
            constexpr auto first = static_cast<std::uint8_t>(hwcnt::block_type::first);
            constexpr auto last = static_cast<std::uint8_t>(hwcnt::block_type::last);

            for (auto t = first; t <= last; t++) {
                count += extents.num_blocks_of_type(static_cast<hwcnt::block_type>(t)) > 0 ? 1 : 0;
            }
            return count;
        }();

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
        if (product_record == nullptr) {
            LOG_ERROR("No known Mali device detected for GPU ID 0x%" PRIx64, constants.gpu_id);
        }
        else {
            std::ostringstream log_output;

            log_output << "Mali GPU Counters\nSuccessfully probed device";

            // Try to grab the gpu name from libGPUInfo, if we can't, fall back to product record.
            // We use this to distinguish between Mali and Immortalis variants.
            // Get the instance at id 0, as in multi-gpu case gpus are homogenous.
            auto gpu_info_instance = libarmgpuinfo::instance::create();
            libarmgpuinfo::gpuinfo gpu_info {};

            if (gpu_info_instance != nullptr) {
                gpu_info = gpu_info_instance->get_info();
            }

            std::string product_name;

            if (gpu_info.gpu_name != nullptr) {
                product_name = gpu_info.gpu_name;
            }
            else if (product_record->mName != nullptr) {
                product_name = "Mali-" + std::string(product_record->mName);
            }

            if (!product_name.empty()) {
                log_output << " as " << product_name << " (0x" << std::hex << constants.gpu_id << std::dec << ")";
            }
            else {
                log_output << "but it is not recognised (id: 0x" << std::hex << constants.gpu_id;
            }

            log_output << ", " << constants.num_l2_slices << " L2 Slices, ";
            log_output << constants.axi_bus_width << "-bit Bus, ";
            log_output << constants.num_shader_cores << " Shader Cores";

            if (constants.shader_core_mask != ((1ULL << constants.num_shader_cores) - 1)) {
                log_output << " (sparse layout, mask is 0x" << std::hex << constants.shader_core_mask << std::dec
                           << ")";
            }

            if (product_record->mName != nullptr) {
                log_output << ".";
            }
            else {
                log_output << "). Please try updating your verson of gatord.";
            }

            LOG_SETUP(log_output.str());
        }
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

        const std::size_t counter_offset = nameBlockIndex * block_metadata.num_counters_per_block + counterIndex;

        if (counter_offset >= mProductVersion.mNumCounterNames) {
            return nullptr;
        }

        const char * result = mProductVersion.mCounterNames[counter_offset];

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
                                  const hwcnt::features & features,
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
                    const bool on = features.has_power_states ? it.state.on != 0 : true;
                    const bool available = features.has_vm_states ? it.state.available != 0 : true;
                    if (on && available) {
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
