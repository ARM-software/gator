/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "barman-protocol.h"
#include "barman-atomics.h"
#include "barman-cache.h"
#include "barman-intrinsics.h"
#include "barman-log.h"
#include "barman-memutils.h"
#include "barman-types.h"
#include "barman-custom-counter-definitions.h"
#include "data-store/barman-data-store.h"
#include "pmu/barman-select-pmu.h"

/* Select the appropriate datastore */
/** @{ */
#if BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_LINEAR_RAM_BUFFER
#   include "data-store/barman-linear-ram-buffer.h"
#   define barman_datastore_initialize(header_data)             barman_linear_ram_buffer_initialize(header_data)
#   define barman_datastore_get_block(core, length)             barman_linear_ram_buffer_get_block(core, length)
#   define barman_datastore_commit_block(core, block_pointer)   barman_linear_ram_buffer_commit_block(core, block_pointer)
#   define barman_datastore_close()                             barman_linear_ram_buffer_close()
#   define barman_datastore_notify_header_updated(timestamp, header, length)    barman_cache_clean(header, length)
#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_CIRCULAR_RAM_BUFFER
#   include "data-store/barman-circular-ram-buffer.h"
#   define barman_datastore_initialize(header_data)             barman_circular_ram_buffer_initialize(header_data)
#   define barman_datastore_get_block(core, length)             barman_circular_ram_buffer_get_block(core, length)
#   define barman_datastore_commit_block(core, block_pointer)   barman_circular_ram_buffer_commit_block(core, block_pointer)
#   define barman_datastore_close()                             barman_circular_ram_buffer_close()
#   define barman_datastore_notify_header_updated(timestamp, header, length)    barman_cache_clean(header, length)
#elif BM_DATASTORE_USES_STREAMING_INTERFACE
#   include "data-store/barman-streaming-interface.h"
#   define barman_datastore_initialize(config)                  barman_streaming_interface_initialize(config)
#   define barman_datastore_get_block(core, length)             barman_streaming_interface_get_block(length)
#   define barman_datastore_commit_block(core, block_pointer)   barman_streaming_interface_commit_block(block_pointer)
#   define barman_datastore_close()                             barman_streaming_interface_close()
#   define barman_datastore_notify_header_updated(timestamp, header, length)    barman_streaming_interface_notify_header_updated(timestamp, header, length)
#elif BM_CONFIG_USE_DATASTORE == BM_CONFIG_USE_DATASTORE_USER_SUPPLIED
#   define barman_datastore_initialize(config_or_header_data)   barman_ext_datastore_initialize(config_or_header_data)
#   define barman_datastore_get_block(core, length)             barman_ext_datastore_get_block(core, length)
#   define barman_datastore_commit_block(core, block_pointer)   barman_ext_datastore_commit_block(core, block_pointer)
#   define barman_datastore_close()                             barman_ext_datastore_close()
#   define barman_datastore_notify_header_updated(timestamp, header, length)    barman_ext_datastore_notify_header_updated(timestamp, header, length)
#endif
/** @} */

/* *************************************** */

/**
 * @def     BM_PROTOCOL_MAGIC_BYTES
 * @brief   Protocol header magic bytes.
 * @details Encodes the string "BARMAN32" or "BARMAN64" depending on the bitness of the target.
 *          The value is written in native endianness to identify the endianness of the target.
 *
 * @def     BM_PROTOCOL_MAGIC_BYTES_64
 * @brief   64 bit target magic bytes
 *
 * @def     BM_PROTOCOL_MAGIC_BYTES_32
 * @brief   32 bit target magic bytes
 */
#define BM_PROTOCOL_MAGIC_BYTES_64      0x4241524D414E3634ull
#define BM_PROTOCOL_MAGIC_BYTES_32      0x4241524D414E3332ull
#define BM_PROTOCOL_MAGIC_BYTES         (sizeof(void*) == 8 ? BM_PROTOCOL_MAGIC_BYTES_64 : BM_PROTOCOL_MAGIC_BYTES_32)

#if BM_BIG_ENDIAN
#define BM_PROTOCOL_MAGIC_BYTES_FIRST_WORD    (bm_uint32)(BM_PROTOCOL_MAGIC_BYTES >> 32)
#define BM_PROTOCOL_MAGIC_BYTES_SECOND_WORD   (bm_uint32)(BM_PROTOCOL_MAGIC_BYTES)
#else
#define BM_PROTOCOL_MAGIC_BYTES_FIRST_WORD    (bm_uint32)(BM_PROTOCOL_MAGIC_BYTES)
#define BM_PROTOCOL_MAGIC_BYTES_SECOND_WORD   (bm_uint32)(BM_PROTOCOL_MAGIC_BYTES >> 32)
#endif

/**
 * @brief   Defines the current protocol version number
 * @details Version     Description
 *          1           First release in Streamline 6.0
 *          2           Second release in Streamline 6.1; forwards compatible extension from 1 adding
 *                      WFI records and textual annotations.
 *          3           Release in Streamline 6.3; forwards compatible extension from 2 adding
 *                      PC sample, event counter without task ID and warning records (not currently used by barman, only by streamline).
 */
#define BM_PROTOCOL_VERSION             3

/** @brief  String table length */
#define BM_PROTOCOL_STRING_TABLE_LENGTH 1024

/**
 * @brief   Contains various compile time configurable constants
 */
struct bm_protocol_config_values
{
    /** The value of BM_CONFIG_MAX_CORES; gives the length of `per_core_pmu_settings` */
    bm_uint32 max_cores;
    /** The value of BM_CONFIG_MAX_TASK_INFOS; gives the length of `task_info` */
    bm_uint32 max_task_infos;
    /** The value of BM_CONFIG_MAX_MMAP_LAYOUTS; gives the length of `mmap_layout` */
    bm_uint32 max_mmap_layout;
    /** The value of BM_MAX_PMU_COUNTERS; gives the length of `per_core_pmu_settings.counter_types` */
    bm_uint32 max_pmu_counters;
    /** The maximum length of the string table */
    bm_uint32 max_string_table_length;
    /** The value of BM_NUM_CUSTOM_COUNTERS; gives the number of custom counters, length of `custom_counters` */
    bm_uint32 num_custom_counters;
}BM_PACKED_TYPE;

/**
 * @brief   Describes the per-core pmu settings
 */
struct bm_protocol_header_pmu_settings
{
    /** The timestamp the configuration was written */
    bm_uint64 configuration_timestamp;
    /** The MIDR of the core; MIDR register */
    bm_uint32 midr;
    /** The multiprocessor affinity register value (MPIDR) */
    bm_uintptr mpidr;
    /** The cluster number of the processor */
    bm_uint32 cluster_id;
    /** The number of valid entries in `counter_types` */
    bm_uint32 num_counters;
    /** The record of counter types associated with the core's PMU */
    bm_uint32 counter_types[BM_MAX_PMU_COUNTERS];
}BM_PACKED_TYPE;

#if BM_CONFIG_MAX_TASK_INFOS > 0
/**
 * @brief   A task information record. Describes information about a unique task within the system
 */
struct bm_protocol_header_task_info
{
    /** The timestamp the record was inserted */
    bm_uint64 timestamp;
    /** The task id */
    bm_task_id_t task_id;
    /** The offset of name of the task in the string table */
    bm_uint32 task_name_ptr;
}BM_PACKED_TYPE;
#endif

#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
/**
 * @brief   A MMAP record; describes an executable image's position in memory
 */
struct bm_protocol_header_mmap_layout
{
#if BM_CONFIG_MAX_TASK_INFOS > 0
    /** The timestamp the record was inserted */
    bm_uint64 timestamp;
    /** The task ID to associate with the map */
    bm_task_id_t task_id;
#endif
    /** The base address of the image or image section */
    bm_uintptr base_address;
    /** The length of the image or image section */
    bm_uintptr length;
    /** The image section offset */
    bm_uintptr image_offset;
    /** The offset of name of the image in the string table */
    bm_uint32 image_ptr;
}BM_PACKED_TYPE;
#endif

/**
 * @brief   A custom chart description
 */
struct bm_protocol_header_custom_chart
{
    /** The offset of name of the chart in the string table */
    bm_uint32 name_ptr;
    /** The series composition */
    bm_uint8 series_composition;
    /** The rendering type */
    bm_uint8 rendering_type;
    /** Boolean flags: average_selection, average_cores, percentage, per_cpu */
    bm_uint8 boolean_flags;
}BM_PACKED_TYPE;

/**
 * @brief   A custom chart series description
 */
struct bm_protocol_header_custom_chart_series
{
    /** The index of the chart the series belongs to */
    bm_uint32 chart_index;
    /** The offset of name of the chart in the string table */
    bm_uint32 name_ptr;
    /** The offset of the Series units in the string table */
    bm_uint32 units_ptr;
    /** The description string pointer */
    bm_uint32 description_ptr;
    /** Series colour */
    bm_uint32 colour;
    /** Multiplier value */
    double multiplier;
    /** Data class */
    bm_uint8 clazz;
    /** Display type */
    bm_uint8 display;
    /** Boolean flags: sampled */
    bm_uint8 boolean_flags;
}BM_PACKED_TYPE;

/**
 * @brief   String table
 */
struct bm_protocol_header_string_table
{
    /** The amount of string table that is used */
    bm_atomic_uint32 string_table_length;
    /** The string table. A sequence of null-terminated strings referenced from elsewhere in the header */
    char string_table[BM_PROTOCOL_STRING_TABLE_LENGTH];
};

/**
 * @brief   In memory protocol header page which is stored at the head of the in memory data buffer
 * @note    Must maintain 8-byte alignment internally as it contains atomic uint64s. No BM_PACKED_TYPE
 */
struct bm_protocol_header
{
    /* -- 00 --------- Every thing past here is known offset */

    /** Magic bytes value */
    /* 00 */ bm_uint64 magic_bytes;
    /** Protocol version value */
    /* 08 */ bm_uint32 protocol_version;
    /** The length of this struct; i.e. sizeof(struct bm_protocol_header) */
    /* 12 */ bm_uint32 header_length;
    /** Data store type */
    /* 16 */ bm_uint32 data_store_type;
    /** The offset into the string table that contains the target description string */
    /* 20 */ bm_uint32 target_name_ptr;
    /** Timestamp of last attempt to write a sample (even if write failed) */
    /* 24 */ bm_atomic_uint64 last_timestamp;
    /** Timer based sampling rate; in Hz. Zero indicates no timer based sampling (assumes max 4GHz sample rate) */
    /* 32 */ bm_uint32 timer_sample_rate;
    /** Config constant values */
    /* 36 */ struct bm_protocol_config_values config_constants;
    /** Clock parameters */
    /* 60 */ struct bm_protocol_clock_info clock_info;


    /* -- 92 --------- Every thing past here is calculated offset */

    /** The string table */
    struct bm_protocol_header_string_table string_table;
    /** Per-core PMU configuration settings. Each index maps to a core. */
    struct bm_protocol_header_pmu_settings per_core_pmu_settings[BM_CONFIG_MAX_CORES];
#if BM_CONFIG_MAX_TASK_INFOS > 0
    /** Number of task records that contain data */
    bm_atomic_uint32 num_task_entries;
    /** Task information */
    struct bm_protocol_header_task_info task_info[BM_CONFIG_MAX_TASK_INFOS];
#endif
#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
    /** Number of mmap records that contain data */
    bm_atomic_uint32 num_mmap_layout_entries;
    /** MMAP information */
    struct bm_protocol_header_mmap_layout mmap_layout[BM_CONFIG_MAX_MMAP_LAYOUTS];
#endif
#if BM_NUM_CUSTOM_COUNTERS > 0
    /** Number of custom charts (must be equal to BM_CUSTOM_CHARTS_COUNT) */
    bm_uint32 num_custom_charts;
    /** Custom chart descriptions */
    struct bm_protocol_header_custom_chart custom_charts[BM_CUSTOM_CHARTS_COUNT];
    /** Custom chart series */
    struct bm_protocol_header_custom_chart_series custom_charts_series[BM_NUM_CUSTOM_COUNTERS];
#endif
    /** Data store parameters (for in memory buffers) */
    struct bm_datastore_header_data data_store_parameters;
};


/**
 * @brief   Record types
 */
enum bm_protocol_record_types
{
    BM_PROTOCOL_RECORD_SAMPLE = 1,                         /**< Counter sample */
    BM_PROTOCOL_RECORD_SAMPLE_WITH_PC = 2,                 /**< Counter sample with PC value */
    BM_PROTOCOL_RECORD_TASK_SWITCH = 3,                    /**< Task switch */
    BM_PROTOCOL_RECORD_CUSTOM_COUNTER = 4,                 /**< Custom counter value */
    BM_PROTOCOL_RECORD_ANNOTATION = 5,                     /**< Annotation */
    BM_PROTOCOL_RECORD_HALT_EVENT = 6,                     /**< Halting event (WFI/WFE) */
    BM_PROTOCOL_RECORD_PC_WITHOUT_TASK_ID = 7,             /**< PC sample that doesn't have a task ID regardless of BM_MAX_TASK_INFOS */
    BM_PROTOCOL_RECORD_EVENT_COUNTER_WITHOUT_TASK_ID = 8,  /**< Counter value that doesn't have a task ID regardless of BM_MAX_TASK_INFOS */
    BM_PROTOCOL_RECORD_WARNING = 9                         /**< Warning for streamline to interpret */
};

/**
 * @brief   Record header
 */
struct bm_protocol_record_header
{
    /** Identifies the record type */
    bm_uint32 record_type;
    /** The core number. A value of ~BM_UINT32(0) means no specific core. */
    bm_uint32 core;
    /** The timestamp of the event. A value of ~BM_UINT64(0) means the last timestamp should be used as an approximation. */
    bm_uint64 timestamp;
}BM_PACKED_TYPE;

/**
 * @brief   Sample record (PC / counter values are appended afterwards)
 */
struct bm_protocol_sample
{
    /** Record header, as all records must have */
    struct bm_protocol_record_header header;
#if BM_CONFIG_MAX_TASK_INFOS > 0
    /** Task id field */
    bm_task_id_t task_id;
#endif
#if BM_NUM_CUSTOM_COUNTERS > 0
    /** The number of custom counter values sent */
    bm_uint32 num_custom_counters;
#endif
}BM_PACKED_TYPE;

#if BM_NUM_CUSTOM_COUNTERS > 0
/**
 * @brief   Sample record custom counter value entry
 */
struct bm_protocol_sample_custom_counter_value
{
    /** Custom counter id */
    bm_uint32 id;
    /** Custom counter value */
    bm_uint64 value;
}BM_PACKED_TYPE;

/**
 * @brief   Custom counter record
 */
struct bm_protocol_custom_counter_record
{
    /** Record header, as all records must have */
    struct bm_protocol_record_header header;
#if BM_CONFIG_MAX_TASK_INFOS > 0
    /** Task id field */
    bm_task_id_t task_id;
#endif
    /** The custom counter id */
    bm_uint32 counter;
    /** The custom counter value */
    bm_uint64 value;
}BM_PACKED_TYPE;

/**
 * @brief   Enumerate boolean flags
 */
enum
{
    /** bm_protocol_header_custom_chart.boolean_flags - average_selection */
    BM_CHART_FLAG_AVERAGE_SELECTION = 0x01,/**!< BM_CHART_FLAG_AVERAGE_SELECTION */
    /** bm_protocol_header_custom_chart.boolean_flags - average_cores */
    BM_CHART_FLAG_AVERAGE_CORES = 0x02,    /**!< BM_CHART_FLAG_AVERAGE_CORES */
    /** bm_protocol_header_custom_chart.boolean_flags - percentage */
    BM_CHART_FLAG_PERCENTAGE = 0x04,       /**!< BM_CHART_FLAG_PERCENTAGE */
    /** bm_protocol_header_custom_chart.boolean_flags - per_cpu */
    BM_CHART_FLAG_PER_CPU = 0x08,          /**!< BM_CHART_FLAG_PER_CPU */

    /** bm_protocol_header_custom_chart_series.boolean_flags - sampled */
    BM_CHART_SERIES_FLAG_SAMPLED = 0x01    /**!< BM_CHART_SERIES_FLAG_SAMPLED */
};

#endif

#if BM_CONFIG_MAX_TASK_INFOS > 0
/**
 * @brief   Task switch
 */
struct bm_protocol_task_switch
{
    /** Record header, as all records must have */
    struct bm_protocol_record_header header;
    /** Task id field */
    bm_task_id_t task_id;
    /** The reason for the task switch */
    bm_uint8 reason;
}BM_PACKED_TYPE;
#endif

/**
 * @brief   Halting event record
 */
struct bm_protocol_halting_event_record
{
    /** Record header, as all records must have */
    struct bm_protocol_record_header header;
    /** Non-zero if entered halting state, Zero if exited */
    bm_uint8 entered_halt;
}BM_PACKED_TYPE;

/**
 * @brief   Annotation record
 */
struct bm_protocol_annotation_record
{
    /** Record header, as all records must have */
    struct bm_protocol_record_header header;
#if BM_CONFIG_MAX_TASK_INFOS > 0
    /** Task id field */
    bm_task_id_t task_id;
#endif
    /** Length of the byte data that follows the record */
    bm_uintptr data_length;
    /** Annotation channel */
    bm_uint32 channel;
    /** Annotation group */
    bm_uint32 group;
    /** Annotation color */
    bm_uint32 color;
    /** Annotation type */
    bm_uint8 type;
}BM_PACKED_TYPE;

/* *************************************** */

#if BM_DATASTORE_IS_IN_MEMORY

/** The configured protocol header at the start of the memory buffer */
static BM_ATOMIC_TYPE(struct bm_protocol_header *) bm_protocol_header_;

/** @brief  Returns the pointer to the protocol header object */
static BM_ALWAYS_INLINE struct bm_protocol_header * bm_protocol_header(void)
{
    return barman_atomic_load(&bm_protocol_header_);
}

#else

/** The configured protocol header in static storage */
static struct bm_protocol_header bm_protocol_header_;

/** @brief  Returns the pointer to the protocol header object */
BM_RET_NONNULL
static BM_ALWAYS_INLINE struct bm_protocol_header * bm_protocol_header(void)
{
    return &bm_protocol_header_;
}

#endif

#if BM_DATASTORE_IS_IN_MEMORY || BM_CONFIG_RECORDS_PER_HEADER_SENT <= 0

#define barman_datastore_commit_block_and_header(core, block_pointer)   barman_datastore_commit_block(core, block_pointer)

#else

static bm_atomic_uint32 record_counter = BM_ATOMIC_VAR_INIT(0);

BM_NONNULL((2)) static void barman_datastore_commit_block_and_header(bm_uint32 core, bm_uint8 * block_pointer)
{
    /* Commit the block */
    barman_datastore_commit_block(core, block_pointer);

    /* Check if we need to update the header */
    if (barman_atomic_fetch_add(&record_counter, 1) == BM_CONFIG_RECORDS_PER_HEADER_SENT)
    {
        barman_atomic_store(&record_counter, 0);
        barman_datastore_notify_header_updated(0, bm_protocol_header(), sizeof(struct bm_protocol_header));
    }
}

#endif

/* *************************************** */

/**
 * @brief   Adjust the last_timestamp value in the data_store_parameters field so that it is equal to the timestamp parameter, but only if the current value
 *          is less than timestamp, otherwise exists without modification.
 * @param   timestamp   The new timestamp value
 */
BM_NONNULL((1))
static BM_INLINE void barman_protocol_update_last_sample_timestamp(struct bm_protocol_header * header_ptr, bm_uint64 timestamp)
{
#if BM_ARM_ARCH_PROFILE != 'M'
    /* atomic CAS update the timestamp */
    bm_uint64 current_value = barman_atomic_load(&header_ptr->last_timestamp);

    while ((current_value < timestamp) && !barman_atomic_cmp_ex_weak_pointer(&header_ptr->last_timestamp, &current_value, timestamp))
        ;
#endif
}

/**
 * @brief   Initialize a bm_protocol_record_header
 * @param   header      The object to initialize
 * @param   core        The core number
 * @param   record_type The record type
 * @param   timestamp   The timestamp value
 */
BM_NONNULL((1))
static BM_INLINE void barman_protocol_init_record_header(struct bm_protocol_record_header * header, bm_uint32 core, enum bm_protocol_record_types record_type,
                                                         bm_uint64 timestamp)
{
    barman_memset(header, 0, sizeof(*header));

    header->record_type = record_type;
    header->core = core;
    header->timestamp = timestamp;
}

/**
 * @brief   Insert an item into the string table
 * @param   string_table
 * @param   string
 * @param   max_length
 * @return  The index of the string (or a substring of it) in the table
 */
BM_NONNULL((1))
static bm_uint32 barman_protocol_string_table_insert(struct bm_protocol_header_string_table * string_table, const char * string, bm_uint32 max_length)
{
    bm_uint32 table_length = barman_atomic_load(&string_table->string_table_length);
    bm_uint32 table_offset, restart_offset, string_length = 0, longest_match;
    bm_bool table_full = BM_FALSE;

    /* null pointer becomes empty string */
    if (string == BM_NULL) {
        string = "";
    }

    /* get string length */
    while (string[string_length] != 0) {
        if (string_length == max_length) {
            BM_WARNING("Truncating to %d characters: %s", string_length, string);
            break;
        }
        string_length += 1;
    }

    /* use atomic RMW to update string_table_length length */
    do {
        longest_match = 0;

        /* search the table to find the string */
        for (table_offset = 0; table_offset < table_length; table_offset = restart_offset) {
            bm_uint32 string_offset;
            bm_bool failed = BM_FALSE;

            /* assume retry from next character */
            restart_offset = table_offset + 1;

            /* search for matching string */
            for (string_offset = 0; (string_offset <= string_length) && (!failed); ++string_offset) {
                /* the character to match, null terminator is forced as last character */
                const char string_char = (string_offset < string_length ? string[string_offset] : 0);

                /* if character does not match string table, or offset is out of bounds then fail */
                if (((table_offset + string_offset) >= table_length) || (string_table->string_table[table_offset + string_offset] != string_char)) {
                    failed = BM_TRUE;
                    break;
                }
                else {
                    /* update longest match length, this is used if we have to truncate and restart */
                    longest_match = BM_MAX(longest_match, string_offset + 1);

                    /* find the next character that matches the first character of `string` so that we can resume searching from there */
                    if ((string_offset > 0) && (restart_offset == (table_offset + 1)) && (string[0] == string_char)) {
                        restart_offset = table_offset + string_offset;
                    }
                }
            }

            if (!failed) {
                return table_offset;
            }
        }

        /* validate can fit */
        if ((table_length + string_length + 1) > BM_PROTOCOL_STRING_TABLE_LENGTH) {
            const bm_uint32 avail_length = (BM_PROTOCOL_STRING_TABLE_LENGTH > table_length ? (BM_PROTOCOL_STRING_TABLE_LENGTH - table_length) - 1 : 0);
            const bm_uint32 restart_length = (string_length > longest_match ? longest_match : longest_match - 1);

            /* if string length already zero then something bugged; just force last char to be zero */
            if ((string_length == 0) || (table_length > BM_PROTOCOL_STRING_TABLE_LENGTH)) {
                BM_ERROR("string table corrupted. No null terminator.");
                string_table->string_table[BM_PROTOCOL_STRING_TABLE_LENGTH - 1] = 0;
                barman_atomic_store(&string_table->string_table_length, BM_PROTOCOL_STRING_TABLE_LENGTH);
                return BM_PROTOCOL_STRING_TABLE_LENGTH - 1;
            }

            /* try again with shorter string */
            if (string_length > 0) {
                string_length = BM_MAX(avail_length, restart_length);
                table_full = BM_TRUE;
            }

            if (string_length == 0) {
                string = "";
            }

            continue;
        }

        /* append the string */
        if (barman_atomic_cmp_ex_strong_pointer(&string_table->string_table_length, &table_length, table_length + string_length + 1)) {
            bm_uint32 string_offset;
            /* copy in the string */
            for (string_offset = 0; string_offset < string_length; ++string_offset) {
                string_table->string_table[table_length + string_offset] = string[string_offset];
            }
            string_table->string_table[table_length + string_length] = 0;

            if (table_full) {
                BM_WARNING("String table full, truncating to %d characters: %s", string_length, string);
            }

            return table_length;
        }
    } while (BM_TRUE);
}

#if BM_CONFIG_MAX_TASK_INFOS > 0
/**
 * @brief   Fill a `task_info` record
 * @param   header_ptr
 * @param   index
 * @param   timestamp
 * @param   task_entry
 */
BM_NONNULL((1, 4))
static BM_INLINE void barman_protocol_fill_task_record(struct bm_protocol_header * header_ptr, bm_uint32 index, bm_uint64 timestamp,
                                                       const struct bm_protocol_task_info * task_entry)
{
    header_ptr->task_info[index].timestamp = timestamp;
    header_ptr->task_info[index].task_id = task_entry->task_id;
    header_ptr->task_info[index].task_name_ptr = barman_protocol_string_table_insert(&header_ptr->string_table, task_entry->task_name, 31);
}
#endif

#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
/**
 * @brief   Fill a `mmap_layout` record
 * @param   header_ptr
 * @param   index
 * @param   timestamp
 * @param   mmap_entry
 */
BM_NONNULL((1, 4))
static BM_INLINE void barman_protocol_fill_mmap_record(struct bm_protocol_header * header_ptr, bm_uint32 index, bm_uint64 timestamp,
                                                       const struct bm_protocol_mmap_layout * mmap_entry)
{
#if BM_CONFIG_MAX_TASK_INFOS > 0
    header_ptr->mmap_layout[index].timestamp = timestamp;
    header_ptr->mmap_layout[index].task_id = mmap_entry->task_id;
#endif
    header_ptr->mmap_layout[index].base_address = mmap_entry->base_address;
    header_ptr->mmap_layout[index].length = mmap_entry->length;
    header_ptr->mmap_layout[index].image_offset = mmap_entry->image_offset;
    header_ptr->mmap_layout[index].image_ptr = barman_protocol_string_table_insert(&header_ptr->string_table, mmap_entry->image_name, 0xffffffffu);
}
#endif

/**
 * @brief   Get a block and fill the record header
 * @param   length          The length of the block to get
 * @param   timestamp       The timestamp to put in the header
 * @param   core            The core number to put in the header
 * @param   record_type     The record type to put in the header
 * @return  Pointer to the block, BM_NULL if failure
 */
static bm_uint8 * barman_protocol_get_block_and_fill_header(bm_datastore_block_length length, bm_uint32 core, enum bm_protocol_record_types record_type, bm_uint64 timestamp)
{
    struct bm_protocol_header * const header_ptr = bm_protocol_header();
    bm_uint8 * block;

    /* validate has header configured */
    if ((header_ptr == BM_NULL) || (header_ptr->magic_bytes != BM_PROTOCOL_MAGIC_BYTES)) {
        BM_ERROR("Could not write as not initialized\n");
        return BM_NULL;
    }

    /* validate core */
    if (core >= BM_CONFIG_MAX_CORES) {
        BM_DEBUG("Could not write as core > BM_CONFIG_MAX_CORES\n");
        return BM_NULL;
    }

    /* update the last_timestamp value */
    barman_protocol_update_last_sample_timestamp(header_ptr, timestamp);

    /* Get the block */
    block = barman_datastore_get_block(core, length);
    if (block != BM_NULL) {
        /* fill it */
        barman_protocol_init_record_header((struct bm_protocol_record_header *) block, core, record_type, timestamp);
    }

    return block;
}

#if BM_NUM_CUSTOM_COUNTERS > 0
/**
 * @brief   Fill a `custom_chart` record
 * @param   header_ptr
 * @param   index
 * @param   chart
 */
BM_NONNULL((1, 3))
static BM_INLINE void barman_protocol_fill_custom_chart_record(struct bm_protocol_header * header_ptr, bm_uint32 index, const struct bm_custom_counter_chart * chart)
{
    header_ptr->custom_charts[index].name_ptr = barman_protocol_string_table_insert(&header_ptr->string_table, chart->name, 0xffffffffu);
    header_ptr->custom_charts[index].rendering_type = chart->rendering_type;
    header_ptr->custom_charts[index].series_composition = chart->series_composition;
    header_ptr->custom_charts[index].boolean_flags = (chart->average_selection ? BM_CHART_FLAG_AVERAGE_SELECTION : 0) |
                                                     (chart->average_cores ? BM_CHART_FLAG_AVERAGE_CORES : 0) |
                                                     (chart->percentage ? BM_CHART_FLAG_PERCENTAGE : 0) |
                                                     (chart->per_cpu ? BM_CHART_FLAG_PER_CPU : 0);
}

/**
 * @brief   Fill a `custom_chart_series` record
 * @param   header_ptr
 * @param   index
 * @param   series
 */
BM_NONNULL((1, 3))
static BM_INLINE void barman_protocol_fill_custom_chart_series_record(struct bm_protocol_header * header_ptr, bm_uint32 index, const struct bm_custom_counter_chart_series * series)
{
    header_ptr->custom_charts_series[index].chart_index = series->chart_index;
    header_ptr->custom_charts_series[index].name_ptr = barman_protocol_string_table_insert(&header_ptr->string_table, series->name, 0xffffffffu);
    header_ptr->custom_charts_series[index].units_ptr = barman_protocol_string_table_insert(&header_ptr->string_table, series->units, 0xffffffffu);
    header_ptr->custom_charts_series[index].description_ptr = barman_protocol_string_table_insert(&header_ptr->string_table, series->description, 0xffffffffu);
    header_ptr->custom_charts_series[index].multiplier = series->multiplier;
    header_ptr->custom_charts_series[index].display = series->display;
    header_ptr->custom_charts_series[index].clazz = series->clazz;
    header_ptr->custom_charts_series[index].colour = series->colour;
    header_ptr->custom_charts_series[index].boolean_flags = (series->sampling_function != BM_NULL ? BM_CHART_SERIES_FLAG_SAMPLED : 0);
}
#endif

/* *************************************** */

bm_bool barman_protocol_initialize(bm_datastore_config datastore_config,
                                   const char * target_name,
                                   const struct bm_protocol_clock_info * clock_info,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                   bm_uint32 num_task_entries,
                                   const struct bm_protocol_task_info * task_entries,
#endif
#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
                                   bm_uint32 num_mmap_entries,
                                   const struct bm_protocol_mmap_layout * mmap_entries,
#endif
                                   bm_uint32 timer_sample_rate)
{
#if BM_DATASTORE_IS_IN_MEMORY
    const bm_uint32 HEADER_SIZE_ALIGNED_8 = ((sizeof(struct bm_protocol_header) + 7) & ~7ul); /* the data buffer pointer must be aligned to 8 byte boundary */

    bm_uintptr alignment;
#endif
    struct bm_protocol_header * header_ptr = bm_protocol_header();
#if (BM_CONFIG_MAX_TASK_INFOS > 0) || (BM_CONFIG_MAX_MMAP_LAYOUTS > 0) || (BM_NUM_CUSTOM_COUNTERS > 0)
    bm_uint32 index;
#endif

    if ((clock_info->timestamp_multiplier == 0) || (clock_info->timestamp_divisor == 0)) {
        BM_ERROR("clock_info is invalid. multiplier and divisor cannot be zero\n");
        return BM_FALSE;
    }

#if BM_CONFIG_MAX_TASK_INFOS > 0
    /* validate task_entries */
    if ((task_entries == BM_NULL) && (num_task_entries != 0)) {
        BM_ERROR("task_entries is invalid.\n");
        return BM_FALSE;
    }
#endif

#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
    /* validate mmap_entries */
    if ((mmap_entries == BM_NULL) && (num_mmap_entries != 0)) {
        BM_ERROR("mmap_entries is invalid.\n");
        return BM_FALSE;
    }
#endif

#if BM_DATASTORE_IS_IN_MEMORY

    /* validate not already initialized */
    if (header_ptr != BM_NULL) {
        BM_ERROR("Protocol cannot be initialized twice\n");
        return BM_FALSE;
    }

    /* buffer argument must not be null */
    if (datastore_config.buffer == BM_NULL) {
        BM_ERROR("Protocol cannot be initialized with (buffer == NULL)\n");
        return BM_FALSE;
    }

    /* calculate alignment of buffer to 8 byte boundary */
    alignment = ((((bm_uintptr) datastore_config.buffer) + 7) & ~7ul) - ((bm_uintptr) datastore_config.buffer);

    /* validate buffer has enough space for the header */
    if (datastore_config.buffer_length < (HEADER_SIZE_ALIGNED_8 + alignment)) {
        BM_ERROR("Protocol cannot be initialized as buffer length is less that minimum required (%lu vs %lu)\n", datastore_config.buffer_length,
                 alignment + HEADER_SIZE_ALIGNED_8);
        return BM_FALSE;
    }

    /* align buffer */
    datastore_config.buffer += alignment;
    datastore_config.buffer_length -= alignment;

    /* Swap the pointer */
    if (!barman_atomic_cmp_ex_strong_value(&bm_protocol_header_, header_ptr, (struct bm_protocol_header * ) BM_ASSUME_ALIGNED(datastore_config.buffer, 8))) {
        BM_ERROR("Protocol cannot be initialized twice\n");
        return BM_FALSE;
    }

    header_ptr = (struct bm_protocol_header *) BM_ASSUME_ALIGNED(datastore_config.buffer, 8);
    datastore_config.buffer += HEADER_SIZE_ALIGNED_8;
    datastore_config.buffer_length -= HEADER_SIZE_ALIGNED_8;

#else

    /* validate not already initialized */
    /* M profile isn't 64 bit atomic so we just rely on the first half being atomic */
    if (barman_atomic_load((bm_uint32 *)&header_ptr->magic_bytes) == BM_PROTOCOL_MAGIC_BYTES_FIRST_WORD) {
        BM_ERROR("Protocol cannot be initialized twice\n");
        return BM_FALSE;
    }

#endif

    /* initialize header */
    barman_memset(header_ptr, 0, sizeof(*header_ptr));

    header_ptr->protocol_version = BM_PROTOCOL_VERSION;
    header_ptr->header_length = sizeof(*header_ptr);
    header_ptr->data_store_type = BM_CONFIG_USE_DATASTORE;
    header_ptr->last_timestamp = 0;
#if BM_DATASTORE_IS_IN_MEMORY
    header_ptr->data_store_parameters.base_pointer = datastore_config.buffer;
    header_ptr->data_store_parameters.buffer_length = datastore_config.buffer_length;
#else
    header_ptr->data_store_parameters.base_pointer = BM_NULL;
    header_ptr->data_store_parameters.buffer_length = 0;
#endif
    header_ptr->timer_sample_rate = timer_sample_rate;
    header_ptr->config_constants.max_cores = BM_CONFIG_MAX_CORES;
    header_ptr->config_constants.max_task_infos = (BM_CONFIG_MAX_TASK_INFOS > 0 ? BM_CONFIG_MAX_TASK_INFOS : 0);
    header_ptr->config_constants.max_mmap_layout = (BM_CONFIG_MAX_MMAP_LAYOUTS > 0 ? BM_CONFIG_MAX_MMAP_LAYOUTS : 0);
    header_ptr->config_constants.max_pmu_counters = BM_MAX_PMU_COUNTERS;
    header_ptr->config_constants.max_string_table_length = BM_PROTOCOL_STRING_TABLE_LENGTH;
    header_ptr->config_constants.num_custom_counters = (BM_NUM_CUSTOM_COUNTERS > 0 ? BM_NUM_CUSTOM_COUNTERS : 0);
    header_ptr->clock_info.timestamp_base = clock_info->timestamp_base;
    header_ptr->clock_info.timestamp_divisor = clock_info->timestamp_divisor;
    header_ptr->clock_info.timestamp_multiplier = clock_info->timestamp_multiplier;
    header_ptr->clock_info.unix_base_ns = clock_info->unix_base_ns;
    header_ptr->string_table.string_table_length = 0;
    header_ptr->target_name_ptr = barman_protocol_string_table_insert(&header_ptr->string_table, target_name, 255);

#if BM_CONFIG_MAX_TASK_INFOS > 0
    /* add task entries */
    header_ptr->num_task_entries = BM_MIN(num_task_entries, BM_CONFIG_MAX_TASK_INFOS);
    for (index = 0; index < header_ptr->num_task_entries; ++index) {
        barman_protocol_fill_task_record(header_ptr, index, header_ptr->clock_info.timestamp_base, &task_entries[index]);
    }
#endif

#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0
    /* add mmap entries */
    header_ptr->num_mmap_layout_entries = BM_MIN(num_mmap_entries, BM_CONFIG_MAX_MMAP_LAYOUTS);
    for (index = 0; index < header_ptr->num_mmap_layout_entries; ++index) {
        barman_protocol_fill_mmap_record(header_ptr, index, header_ptr->clock_info.timestamp_base, &mmap_entries[index]);
    }
#endif

#if BM_NUM_CUSTOM_COUNTERS > 0
    /* add custom counter information */
    header_ptr->num_custom_charts = BM_CUSTOM_CHARTS_COUNT;
    for (index = 0; index < BM_CUSTOM_CHARTS_COUNT; ++index) {
        barman_protocol_fill_custom_chart_record(header_ptr, index, BM_CUSTOM_CHARTS[index]);
    }
    for (index = 0; index < BM_NUM_CUSTOM_COUNTERS; ++index) {
        barman_protocol_fill_custom_chart_series_record(header_ptr, index, BM_CUSTOM_CHARTS_SERIES[index]);
    }
#endif

    /* init data store */
#if BM_DATASTORE_IS_IN_MEMORY
    if (!barman_datastore_initialize(&header_ptr->data_store_parameters)) {
        /* uninitialize */
        barman_atomic_store(&bm_protocol_header_, BM_NULL);
#else
    if (!barman_datastore_initialize(datastore_config)) {
        /* uninitialize */
        barman_atomic_store((bm_uint32 *)&header_ptr->magic_bytes, 0);
#endif

        BM_ERROR("Protocol failed to initialize data store\n");
        return BM_FALSE;
    }

    /* set the magic bytes to indicate initialized */
    ((bm_uint32 *)&header_ptr->magic_bytes)[1] = BM_PROTOCOL_MAGIC_BYTES_SECOND_WORD;
    barman_atomic_store((bm_uint32 *)&header_ptr->magic_bytes, BM_PROTOCOL_MAGIC_BYTES_FIRST_WORD);

    /* notify datastore header changed */
    barman_datastore_notify_header_updated(clock_info->timestamp_base, header_ptr, sizeof(*header_ptr));

    return BM_TRUE;
}

#if BM_CONFIG_MAX_TASK_INFOS > 0

bm_bool barman_add_task_record(bm_uint64 timestamp, const struct bm_protocol_task_info * task_entry)
{
    struct bm_protocol_header * const header_ptr = bm_protocol_header();
    bm_uint32 index;

    /* validate has header configured */
    if ((header_ptr == BM_NULL) || (header_ptr->magic_bytes != BM_PROTOCOL_MAGIC_BYTES)) {
        BM_ERROR("Could not add task info as not initialized\n");
        return BM_FALSE;
    }

    /* use atomic CAS loop to update the index */
    index = barman_atomic_load(&header_ptr->num_task_entries);
    do {
        /* too many items? */
        if (index >= BM_CONFIG_MAX_TASK_INFOS) {
            return BM_FALSE;
        }
        /* perform CAS */
        else if (barman_atomic_cmp_ex_weak_pointer(&header_ptr->num_task_entries, &index, index + 1)) {
            /* update record */
            barman_protocol_fill_task_record(header_ptr, index, timestamp, task_entry);
            /* Update last modified timestamp in header */
            barman_protocol_update_last_sample_timestamp(header_ptr, timestamp);
            /* notify datastore header changed */
            barman_datastore_notify_header_updated(timestamp, header_ptr, sizeof(*header_ptr));
            return BM_TRUE;
        }
        /* failed; retry */
    } while (BM_TRUE);
}

#endif

#if BM_CONFIG_MAX_MMAP_LAYOUTS > 0

bm_bool barman_add_mmap_record(bm_uint64 timestamp, const struct bm_protocol_mmap_layout * mmap_entry)
{
    struct bm_protocol_header * const header_ptr = bm_protocol_header();
    bm_uint32 index;

    /* validate has header configured */
    if ((header_ptr == BM_NULL) || (header_ptr->magic_bytes != BM_PROTOCOL_MAGIC_BYTES)) {
        BM_ERROR("Could not add task info as not initialized\n");
        return BM_FALSE;
    }

    /* update the last_timestamp value */
    barman_protocol_update_last_sample_timestamp(header_ptr, timestamp);

    /* use atomic CAS loop to update the index */
    index = barman_atomic_load(&header_ptr->num_mmap_layout_entries);
    do {
        /* too many items? */
        if (index >= BM_CONFIG_MAX_MMAP_LAYOUTS) {
            return BM_FALSE;
        }
        /* perform CAS */
        else if (barman_atomic_cmp_ex_weak_pointer(&header_ptr->num_mmap_layout_entries, &index, index + 1)) {
            /* update record */
            barman_protocol_fill_mmap_record(header_ptr, index, timestamp, mmap_entry);
            /* Update last modified timestamp in header */
            barman_protocol_update_last_sample_timestamp(header_ptr, timestamp);
            /* notify datastore header changed */
            barman_datastore_notify_header_updated(timestamp, header_ptr, sizeof(*header_ptr));
            return BM_TRUE;
        }
        /* failed; retry */
    } while (BM_TRUE);
}

#endif

bm_bool barman_protocol_write_pmu_settings(bm_uint64 timestamp, bm_uint32 midr, bm_uintptr mpidr, bm_uint32 core, bm_uint32 num_counters, const bm_uint32 * counter_types)
{
    struct bm_protocol_header * const header_ptr = bm_protocol_header();
    bm_uint32 counter;

    /* validate has header configured */
    if ((header_ptr == BM_NULL) || (header_ptr->magic_bytes != BM_PROTOCOL_MAGIC_BYTES)) {
        BM_ERROR("Could not write PMU settings as not initialized\n");
        return BM_FALSE;
    }

    /* validate will fit in data */
    if (core >= BM_COUNT_OF(header_ptr->per_core_pmu_settings)) {
        BM_DEBUG("Could not write PMU settings as core > BM_CONFIG_MAX_CORES\n");
        return BM_FALSE;
    }

    /* validate not overwriting */
    if (header_ptr->per_core_pmu_settings[core].num_counters > 0) {
        BM_DEBUG("Could not write PMU settings already set for core %u\n", core);
        return BM_FALSE;
    }

    /* update the last_timestamp value */
    barman_protocol_update_last_sample_timestamp(header_ptr, timestamp);

    /* write settings */
    header_ptr->per_core_pmu_settings[core].configuration_timestamp = timestamp;
    header_ptr->per_core_pmu_settings[core].midr = midr;
    header_ptr->per_core_pmu_settings[core].mpidr = mpidr;
    header_ptr->per_core_pmu_settings[core].cluster_id = barman_ext_map_multiprocessor_affinity_to_cluster_no(mpidr);
    header_ptr->per_core_pmu_settings[core].num_counters = BM_MIN(num_counters, BM_COUNT_OF(header_ptr->per_core_pmu_settings[core].counter_types));
    for (counter = 0; counter < header_ptr->per_core_pmu_settings[core].num_counters; ++counter) {
        header_ptr->per_core_pmu_settings[core].counter_types[counter] = counter_types[counter];
    }

    /* notify datastore header changed */
    barman_datastore_notify_header_updated(timestamp, header_ptr, sizeof(*header_ptr));

    return BM_TRUE;
}

#if BM_CONFIG_MIN_SAMPLE_PERIOD > 0
bm_uint64 barman_protocol_get_minimum_sample_period(void)
{
    struct bm_protocol_header * const header_ptr = bm_protocol_header();

    /* validate has header configured */
    if ((header_ptr == BM_NULL) || (header_ptr->magic_bytes != BM_PROTOCOL_MAGIC_BYTES)) {
        BM_ERROR("Could not calculate minimum sample period as not initialized\n");
        return ~0; /* a really large sample period should prevent from being sampled */
    }

    return (BM_CONFIG_MIN_SAMPLE_PERIOD * header_ptr->clock_info.timestamp_divisor) / header_ptr->clock_info.timestamp_multiplier;
}
#endif


bm_bool barman_protocol_write_sample(bm_uint64 timestamp, bm_uint32 core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                     bm_task_id_t task_id,
#endif
                                     const void * pc,
                                     bm_uint32 num_counters, const bm_uint64 * counter_values,
                                     bm_uint32 num_custom_counters, const bm_uint32 * custom_counter_ids, const bm_uint64 * custom_counter_values)
{
    const bm_datastore_block_length length = sizeof(struct bm_protocol_sample) +
                                                (pc != BM_NULL ? sizeof(void *) : 0) +
                                                (num_counters * sizeof(bm_uint64)) +
#if BM_NUM_CUSTOM_COUNTERS > 0
                                                (num_custom_counters * (sizeof(struct bm_protocol_sample_custom_counter_value))) +
#endif
                                                0;
    struct bm_protocol_sample * pointer;
    bm_uint8 * value_pointer;
    bm_uint32 index;

    /* Get the block and fill the header */
    pointer = (struct bm_protocol_sample *) barman_protocol_get_block_and_fill_header(length, core, (pc != BM_NULL ? BM_PROTOCOL_RECORD_SAMPLE_WITH_PC : BM_PROTOCOL_RECORD_SAMPLE), timestamp);
    if (pointer == BM_NULL) {
        return BM_FALSE;
    }

    /* fill the rest */
#if BM_CONFIG_MAX_TASK_INFOS > 0
    pointer->task_id = task_id;
#endif
#if BM_NUM_CUSTOM_COUNTERS > 0
    pointer->num_custom_counters = num_custom_counters;
#endif

    /* append pc */
    if (pc != BM_NULL) {
        bm_uint8 * pc_pointer = ((bm_uint8*) pointer) + sizeof(struct bm_protocol_sample);
        BM_UNALIGNED_CAST_DEREF_ASSIGN(const void*, pc_pointer, pc);
    }

    /* append values */
    value_pointer = (((bm_uint8*) pointer) + sizeof(struct bm_protocol_sample) + (pc != BM_NULL ? sizeof(void *) : 0));
    for (index = 0; index < num_counters; ++index) {
        BM_UNALIGNED_CAST_DEREF_ASSIGN(bm_uint64, value_pointer, counter_values[index]);
        value_pointer += sizeof(bm_uint64);
    }

#if BM_NUM_CUSTOM_COUNTERS > 0
    /* append custom counters */
    struct bm_protocol_sample_custom_counter_value * custom_value_pointer = (struct bm_protocol_sample_custom_counter_value *) value_pointer;
    for (index = 0; index < num_custom_counters; ++index) {
        custom_value_pointer[index].id = custom_counter_ids[index];
        custom_value_pointer[index].value = custom_counter_values[index];
    }
#endif

    /* commit the data */
    barman_datastore_commit_block_and_header(core, (bm_uint8* ) pointer);

    return BM_TRUE;
}

#if BM_CONFIG_MAX_TASK_INFOS > 0
bm_bool barman_protocol_write_task_switch(bm_uint64 timestamp, bm_uint32 core, bm_task_id_t task_id, bm_uint8 reason)
{
    const bm_datastore_block_length length = sizeof(struct bm_protocol_task_switch);
    struct bm_protocol_task_switch * pointer;

    /* Get the block and fill the header */
    pointer = (struct bm_protocol_task_switch *) barman_protocol_get_block_and_fill_header(length, core, BM_PROTOCOL_RECORD_TASK_SWITCH, timestamp);
    if (pointer == BM_NULL) {
        return BM_FALSE;
    }

    /* fill the rest */
    pointer->task_id = task_id;
    pointer->reason = reason;

    /* commit the data */
    barman_datastore_commit_block(core, (bm_uint8* ) pointer);

    return BM_TRUE;
}

#endif

#if BM_NUM_CUSTOM_COUNTERS > 0

bm_bool barman_protocol_write_per_core_custom_counter(bm_uint64 timestamp, bm_uint32 core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                                      bm_task_id_t task_id,
#endif
                                                      bm_uint32 counter_id, bm_uint64 value)
{
    const bm_datastore_block_length length = sizeof(struct bm_protocol_custom_counter_record);
    struct bm_protocol_custom_counter_record * pointer;

    /* Get the block and fill the header */
    pointer = (struct bm_protocol_custom_counter_record *) barman_protocol_get_block_and_fill_header(length, core, BM_PROTOCOL_RECORD_CUSTOM_COUNTER, timestamp);
    if (pointer == BM_NULL) {
        return BM_FALSE;
    }

    /* fill the rest */
#if BM_CONFIG_MAX_TASK_INFOS > 0
    pointer->task_id = task_id;
#endif
    pointer->counter = counter_id;
    pointer->value = value;

    /* commit the data */
    barman_datastore_commit_block(core, (bm_uint8 *) pointer);

    return BM_TRUE;
}

#endif

bm_bool barman_protocol_write_halt_event(bm_uint64 timestamp, bm_uint32 core, bm_bool entered_halt)
{
    const bm_datastore_block_length length = sizeof(struct bm_protocol_halting_event_record);
    struct bm_protocol_halting_event_record * pointer;

    /* Get the block and fill the header */
    pointer = (struct bm_protocol_halting_event_record *) barman_protocol_get_block_and_fill_header(length, core, BM_PROTOCOL_RECORD_HALT_EVENT, timestamp);
    if (pointer == BM_NULL) {
        return BM_FALSE;
    }

    /* fill the rest */
    pointer->entered_halt = entered_halt;

    /* commit the data */
    barman_datastore_commit_block(core, (bm_uint8 *) pointer);

    return BM_TRUE;
}

bm_bool barman_protocol_write_annotation(bm_uint64 timestamp, bm_uint32 core,
#if BM_CONFIG_MAX_TASK_INFOS > 0
                                         bm_task_id_t task_id,
#endif
                                         bm_uint8 type, bm_uint32 channel, bm_uint32 group, bm_uint32 color, bm_uintptr data_length, const bm_uint8 * data)
{
    bm_datastore_block_length length = sizeof(struct bm_protocol_annotation_record) + data_length;
    struct bm_protocol_annotation_record * pointer;

    /* Get the block and fill the header */
    pointer = (struct bm_protocol_annotation_record *) barman_protocol_get_block_and_fill_header(length, core, BM_PROTOCOL_RECORD_ANNOTATION, timestamp);
    if (pointer == BM_NULL) {
        return BM_FALSE;
    }

    /* fill the rest */
#if BM_CONFIG_MAX_TASK_INFOS > 0
    pointer->task_id = task_id;
#endif
    pointer->data_length = data_length;
    pointer->channel = channel;
    pointer->group = group;
    pointer->color = color;
    pointer->type = type;

    /* add the data on the end */
    barman_memcpy(pointer + 1, data, data_length);

    /* commit the data */
    barman_datastore_commit_block(core, (bm_uint8 *) pointer);

    return BM_TRUE;
}
