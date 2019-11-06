/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#ifndef NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICEAPI_DDKDEFINES_H_
#define NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICEAPI_DDKDEFINES_H_

#include <cstdint>

namespace mali_userspace
{
    /**
     * Supporting DDK versions m_r12-m_r21, b_r0-b_r9
     */
    namespace ddk_pre_r21
    {
        /** Message header */
        union kbase_uk_header
        {
            /* 32-bit number identifying the UK function to be called. */
            uint32_t id;
            /* The int return code returned by the called UK function. */
            uint32_t ret;
            /* Used to ensure 64-bit alignment of this union. Do not remove. */
            uint64_t sizer;
        };

        /** IOCTL parameters to check version */
        struct kbase_uk_version_check_args
        {
            kbase_uk_header header;

            uint16_t major;
            uint16_t minor;
            uint8_t padding[4];
        };

        /** IOCTL parameters to set flags */
        struct kbase_uk_set_flags
        {
            kbase_uk_header header;

            uint32_t create_flags;
            uint32_t padding;
        };

        static constexpr std::size_t BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS = 3;
        static constexpr std::size_t BASE_MAX_COHERENT_GROUPS = 16;
        static constexpr std::size_t GPU_MAX_JOB_SLOTS = 16;

        /** IOCTL parameters to probe GPU properties */
        struct kbase_uk_gpuprops
        {
            kbase_uk_header header;

            struct mali_base_gpu_props
            {
                struct mali_base_gpu_core_props
                {
                    uint32_t product_id;
                    uint16_t version_status;
                    uint16_t minor_revision;
                    uint16_t major_revision;
                    uint16_t padding;
                    uint32_t gpu_speed_mhz;
                    uint32_t gpu_freq_khz_max;
                    uint32_t gpu_freq_khz_min;
                    uint32_t log2_program_counter_size;
                    uint32_t texture_features[BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS];
                    uint64_t gpu_available_memory_size;
                } core_props;

                struct mali_base_gpu_l2_cache_props
                {
                    uint8_t log2_line_size;
                    uint8_t log2_cache_size;
                    uint8_t num_l2_slices;
                    uint8_t padding[5];
                } l2_props;

                uint64_t unused;

                struct mali_base_gpu_tiler_props
                {
                    uint32_t bin_size_bytes;
                    uint32_t max_active_levels;
                } tiler_props;

                struct mali_base_gpu_thread_props
                {
                    uint32_t max_threads;
                    uint32_t max_workgroup_size;
                    uint32_t max_barrier_size;
                    uint16_t max_registers;
                    uint8_t max_task_queue;
                    uint8_t max_thread_group_split;
                    uint8_t impl_tech;
                    uint8_t padding[7];
                } thread_props;

                struct gpu_raw_gpu_props
                {
                    uint64_t shader_present;
                    uint64_t tiler_present;
                    uint64_t l2_present;
                    uint64_t unused_1;

                    uint32_t l2_features;
                    uint32_t suspend_size;
                    uint32_t mem_features;
                    uint32_t mmu_features;

                    uint32_t as_present;

                    uint32_t js_present;
                    uint32_t js_features[GPU_MAX_JOB_SLOTS];
                    uint32_t tiler_features;
                    uint32_t texture_features[3];

                    uint32_t gpu_id;

                    uint32_t thread_max_threads;
                    uint32_t thread_max_workgroup_size;
                    uint32_t thread_max_barrier_size;
                    uint32_t thread_features;

                    uint32_t coherency_mode;
                } raw_props;

                struct mali_base_gpu_coherent_group_info
                {
                    uint32_t num_groups;
                    uint32_t num_core_groups;
                    uint32_t coherency;
                    uint32_t padding;

                    struct mali_base_gpu_coherent_group
                    {
                        uint64_t core_mask;
                        uint16_t num_cores;
                        uint16_t padding[3];
                    } group[BASE_MAX_COHERENT_GROUPS];
                } coherency_info;
            } props;
        };

        /** IOCTL parameters to configure reader */
        struct kbase_uk_hwcnt_reader_setup
        {
            kbase_uk_header header;

            /* IN */
            uint32_t buffer_count;
            uint32_t jm_bm;
            uint32_t shader_bm;
            uint32_t tiler_bm;
            uint32_t mmu_l2_bm;

            /* OUT */
            int32_t fd;
        };

        enum
        {
            /* Related to mali0 ioctl interface */
            LINUX_UK_BASE_MAGIC = 0x80,
            BASE_CONTEXT_CREATE_KERNEL_FLAGS = 0x2,
            KBASE_FUNC_UK_FUNC_ID = 512,
            KBASE_FUNC_HWCNT_READER_SETUP = KBASE_FUNC_UK_FUNC_ID + 36,
            KBASE_FUNC_DUMP = KBASE_FUNC_UK_FUNC_ID + 11,
            KBASE_FUNC_CLEAR = KBASE_FUNC_UK_FUNC_ID + 12,
            KBASE_FUNC_GET_PROPS = KBASE_FUNC_UK_FUNC_ID + 14,
            KBASE_FUNC_SET_FLAGS = KBASE_FUNC_UK_FUNC_ID + 18,
        };
    }

    /**
     * Supporting DDK versions m_r22-m_r28, b_r10+
     */
    namespace ddk_post_r21
    {
        /** IOCTL parameters to check version */
        struct kbase_ioctl_version_check {
            uint16_t major;
            uint16_t minor;
        };

        /** IOCTL parameters to set flags */
        struct kbase_ioctl_set_flags {
            uint32_t create_flags;
        };

        /** IOCTL parameters to configure reader */
        struct kbase_ioctl_hwcnt_reader_setup
        {
            uint32_t buffer_count;
            uint32_t jm_bm;
            uint32_t shader_bm;
            uint32_t tiler_bm;
            uint32_t mmu_l2_bm;
        };

        /** IOCTL parameters to read GPU properties */
        struct kbase_ioctl_get_gpuprops
        {
            union kbase_pointer
            {
                void *   value;
                uint32_t compat_value;
                uint64_t sizer;
            } buffer;
            uint32_t      size;
            uint32_t      flags;
        };

        static constexpr std::size_t BASE_MAX_COHERENT_GROUPS = 16;

        /** GPU properties decoded from data blob */
        struct gpu_propeties
        {
            uint32_t product_id;
            uint32_t minor_revision;
            uint32_t major_revision;
            uint32_t num_core_groups;
            uint32_t num_l2_slices;
            uint32_t core_mask[BASE_MAX_COHERENT_GROUPS];
        };

        /** Identify the size of a gpuprop value */
        enum class KBaseGpuPropValueSize
        {
            U8  = 0,
            U16 = 1,
            U32 = 2,
            U64 = 3
        };

        /** Identify which property a gpuprop value is */
        enum class KBaseGpuPropKey
        {
            PRODUCT_ID = 1,
            MINOR_REVISION = 3,
            MAJOR_REVISION = 4,
            COHERENCY_NUM_CORE_GROUPS = 62,
            COHERENCY_GROUP_0 = 64,
            COHERENCY_GROUP_1 = 65,
            COHERENCY_GROUP_2 = 66,
            COHERENCY_GROUP_3 = 67,
            COHERENCY_GROUP_4 = 68,
            COHERENCY_GROUP_5 = 69,
            COHERENCY_GROUP_6 = 70,
            COHERENCY_GROUP_7 = 71,
            COHERENCY_GROUP_8 = 72,
            COHERENCY_GROUP_9 = 73,
            COHERENCY_GROUP_10 = 74,
            COHERENCY_GROUP_11 = 75,
            COHERENCY_GROUP_12 = 76,
            COHERENCY_GROUP_13 = 77,
            COHERENCY_GROUP_14 = 78,
            COHERENCY_GROUP_15 = 79,
            L2_NUM_L2_SLICES = 15
        };
    }
}

#endif /* NATIVE_GATOR_DAEMON_MALI_USERSPACE_MALIDEVICEAPI_DDKDEFINES_H_ */
