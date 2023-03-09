/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#include "pmu/barman-arm-pmu.h"
#include "pmu/barman-pmu.h"
#include "barman-atomics.h"
#include "barman-external-dependencies.h"
#include "barman-intrinsics.h"
#include "multicore/barman-multicore.h"

/* ************************************* */

/* The architecture limit on the number of counters */
/** @{ */
#define ARM_PMU_MAX_COUNTERS      32
#define ARM_PMU_COUNTER_MASK      (ARM_PMU_MAX_COUNTERS - 1)
/** @} */

/* PMCR */
/** @{ */
#define ARM_PMU_PMCR_E            (1 << 0)
#define ARM_PMU_PMCR_P            (1 << 1)
#define ARM_PMU_PMCR_C            (1 << 2)
#define ARM_PMU_PMCR_LC           (1 << 6)
#define ARM_PMU_PMCR_N_SHIFT      11
#define ARM_PMU_PMCR_N_MASK       0x1f
#ifdef BM_READ_PMCCNTR_64
#define ARM_PMU_PMCR_RESET_VALUE  ARM_PMU_PMCR_LC
#else
#define ARM_PMU_PMCR_RESET_VALUE  0
#endif
/** @} */

/* PMXEVTYPER */
/** @{ */
#define ARM_PMU_EVTYPE_MASK       0x0000ffff
#define ARM_PMU_EVTYPE_NSH_BIT    (1u << 27)
/** @} */

/* PMUSERENR */
/** @{ */
#define ARM_PMU_USERENR_EN        (1 << 0)
#define ARM_PMU_USERENR_SW        (1 << 1)
#define ARM_PMU_USERENR_CR        (1 << 2)
#define ARM_PMU_USERENR_ER        (1 << 3)
/** @} */

/** @{ */
#define CYCLE_COUNTER_NO    0
#define CYCLE_COUNTER_HW_NO 31
/** @} */
/* ************************************* */

/**
 * Stores the per-core PMU configuration data
 */
struct barman_arm_pmu_configuration_data
{
    /** Stores the 64-bit accumulated value for each 32-bit counter */
    bm_atomic_uint64 event_counter_values[ARM_PMU_MAX_COUNTERS];
    /** The number of events that were enabled */
    bm_uint32 n_events_enabled;
    /** Indicates PMU is properly configured and can be used */
    bm_bool is_initialized;
};

/** Per core configuration data */
static struct barman_arm_pmu_configuration_data barman_arm_pmu_configuration_data[BM_CONFIG_MAX_CORES];

/* ************************************* */

static BM_INLINE bm_uint32 barman_arm_pmu_get_number_of_counters(void)
{
    bm_uintptr pmcr;
    BM_READ_PMCR(pmcr);
    return ((pmcr >> ARM_PMU_PMCR_N_SHIFT) & ARM_PMU_PMCR_N_MASK);
}

static BM_INLINE bm_uint32 barman_arm_pmu_get_and_validate_counter_hw_no(bm_uint32 counter_no)
{
    const bm_uint32 n_counters = barman_arm_pmu_get_number_of_counters();

    if (counter_no == CYCLE_COUNTER_NO) {
        return CYCLE_COUNTER_HW_NO;
    }

    counter_no--;
    if (counter_no < n_counters) {
        return counter_no;
    }

    return -1u;
}

static BM_INLINE void barman_arm_pmu_configure_cycle_counter(bm_bool enable_interrupts)
{
#ifdef BM_WRITE_PMCCFILTR
    BM_WRITE_PMCCFILTR((bm_uintptr) ARM_PMU_EVTYPE_NSH_BIT);
#else
    /*
     * If we don't have the reentrant PMCCFILTR write available,
     * then we need to disable interrupts while we write it.
     */
    const bm_uintptr interrupt_status = barman_ext_disable_interrupts_local();

    /* Write the filter bits non-reentrantly */
    BM_WRITE_PMCCFILTR_NR((bm_uintptr) ARM_PMU_EVTYPE_NSH_BIT);

    barman_ext_enable_interrupts_local(interrupt_status);
#endif

    if (enable_interrupts) {
        BM_WRITE_PMINTENSET(BM_BIT(CYCLE_COUNTER_HW_NO));
    }

    BM_WRITE_PMCNTENSET(BM_BIT(CYCLE_COUNTER_HW_NO));
}

static BM_INLINE void barman_arm_pmu_configure_counter(bm_bool enable_interrupts, bm_uint32 counter_hw_no, bm_uint32 event_id)
{
#ifdef BM_WRITE_PMEVTYPER
    BM_WRITE_PMEVTYPER(counter_hw_no, (bm_uintptr) (event_id & ARM_PMU_EVTYPE_MASK) | ARM_PMU_EVTYPE_NSH_BIT);
#else
    /*
     * If we don't have the reentrant event type write available,
     * then we need to disable interrupts while we write it.
     */
    const bm_uintptr interrupt_status = barman_ext_disable_interrupts_local();

    /* Write the event type non-reentrantly */
    BM_WRITE_PMEVTYPER_NR(counter_hw_no, (bm_uintptr) (event_id & ARM_PMU_EVTYPE_MASK) | ARM_PMU_EVTYPE_NSH_BIT);

    barman_ext_enable_interrupts_local(interrupt_status);
#endif

    if (enable_interrupts) {
        BM_WRITE_PMINTENSET(BM_BIT(counter_hw_no));
    }

    BM_WRITE_PMCNTENSET(BM_BIT(counter_hw_no));
}

static BM_INLINE void barman_arm_pmu_reset(void)
{
    BM_WRITE_PMINTENCLR(0xfffffffful);
    BM_WRITE_PMCNTENCLR(0xfffffffful);
    barman_isb();
    BM_WRITE_PMOVSR(0xfffffffful);
    BM_WRITE_PMCR((bm_uintptr) (ARM_PMU_PMCR_RESET_VALUE | ARM_PMU_PMCR_P | ARM_PMU_PMCR_C));
    barman_isb();
}

static BM_INLINE bm_uint64 barman_arm_pmu_get_current_counter_value(bm_uint32 current_core_no, bm_uint32 counter_no)
{
    return barman_atomic_load(&(barman_arm_pmu_configuration_data[current_core_no].event_counter_values[counter_no]));
}

static BM_INLINE bm_bool barman_arm_pmu_set_current_counter_value(bm_uint32 current_core_no, bm_uint32 counter_no, bm_uint64 * old_value,
                                                                     bm_uint64 new_value)
{
    /* NB: old_value is modified by barman_atomic_cmp_ex_strong_pointer if it fails */
    return barman_atomic_cmp_ex_strong_pointer(&(barman_arm_pmu_configuration_data[current_core_no].event_counter_values[counter_no]), old_value, new_value);
}

/* ************************************* */

bm_int32 barman_arm_pmu_init(bm_bool enable_el0_access, bm_bool enable_interrupts, bm_bool enable_cycle_counter,
                              bm_uint32 n_event_types, const bm_uint32 * event_types)
{
    const bm_uint32 current_core_no = barman_get_core_no();
    const bm_uint32 n_counters = barman_arm_pmu_get_number_of_counters();
    const bm_uint32 n_events_to_configure = BM_MIN(n_event_types, n_counters);
    bm_uint32 counter_no;

    /* Make sure we do nothing if the core no is invalid */
    if (current_core_no >= BM_CONFIG_MAX_CORES) {
        return 0;
    }

    /* Set un-initialized */
    barman_arm_pmu_configuration_data[current_core_no].is_initialized = BM_FALSE;

    /* Reset the PMU */
    barman_arm_pmu_reset();

    /* Enable the cycle counter */
    if (enable_cycle_counter) {
        barman_arm_pmu_configure_cycle_counter(enable_interrupts);
    }

    /* Enable the other counters */
    for (counter_no = 0; counter_no < ARM_PMU_MAX_COUNTERS; ++counter_no) {
        /* Reset 64-bit counter */
        barman_arm_pmu_configuration_data[current_core_no].event_counter_values[counter_no] = 0;

        /* Enable event */
        if (counter_no < n_events_to_configure) {
            barman_arm_pmu_configure_counter(enable_interrupts, counter_no, event_types[counter_no]);
        }
    }

    /* Configure EL0 access */
    BM_WRITE_PMUSERENR((bm_uintptr) (enable_el0_access ? ARM_PMU_USERENR_EN | ARM_PMU_USERENR_SW | ARM_PMU_USERENR_CR | ARM_PMU_USERENR_ER : 0));

    barman_isb();

    /* Save enabled event count */
    barman_arm_pmu_configuration_data[current_core_no].n_events_enabled = n_events_to_configure;
    /* Set initialized */
    barman_arm_pmu_configuration_data[current_core_no].is_initialized = BM_TRUE;

    /* Make sure data is written to configuration */
    barman_dsb();

    return n_events_to_configure + (enable_cycle_counter ? 1 : 0);
}

void barman_arm_pmu_start(void)
{
    /* Make sure we do nothing if the core no is invalid */
    const bm_uint32 current_core_no = barman_get_core_no();
    if (current_core_no >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* Make sure the PMU has been initialized */
    if (!barman_arm_pmu_configuration_data[current_core_no].is_initialized) {
        return;
    }

    /* Start counters */
    BM_WRITE_PMCR((bm_uintptr) (ARM_PMU_PMCR_RESET_VALUE | ARM_PMU_PMCR_E));
}

void barman_arm_pmu_stop(void)
{
    /* Make sure we do nothing if the core no is invalid */
    const bm_uint32 current_core_no = barman_get_core_no();
    if (current_core_no >= BM_CONFIG_MAX_CORES) {
        return;
    }

    /* Make sure the PMU has been initialized */
    if (!barman_arm_pmu_configuration_data[current_core_no].is_initialized) {
        return;
    }

    /* Stop counters */
    BM_WRITE_PMCR((bm_uintptr) (ARM_PMU_PMCR_RESET_VALUE & ~ARM_PMU_PMCR_E));
}

bm_uint64 barman_arm_pmu_read_counter(bm_uint32 counter_no)
{
    const bm_uint32 current_core_no = barman_get_core_no();
    const bm_uint32 counter_hw_no = barman_arm_pmu_get_and_validate_counter_hw_no(counter_no);
    bm_uint64 value64;

    /* Make sure we do nothing if the core no is invalid */
    if (current_core_no >= BM_CONFIG_MAX_CORES) {
        return BM_ARM_PMU_INVALID_COUNTER_VALUE;
    }

    /* Make sure the PMU has been initialized */
    if (!barman_arm_pmu_configuration_data[current_core_no].is_initialized) {
        return BM_ARM_PMU_INVALID_COUNTER_VALUE;
    }

#ifdef BM_READ_PMCCNTR_64
    /* read the cycle counter as 64 bits */
    if (counter_hw_no == CYCLE_COUNTER_HW_NO) {
        BM_READ_PMCCNTR_64(value64);
    }
    else
#endif
    if (counter_hw_no <= CYCLE_COUNTER_HW_NO) {
        bm_uint64 current_counter_value;
        bm_uintptr read_counter_value_32;
        bm_uintptr overflowed_reg_before, overflowed_reg_after;
        bm_uintptr read_counter;
        bm_bool updated_value = BM_FALSE;

        /* Do an atomic RMW on the stored 64-bit counter value to avoid locking */
        current_counter_value = barman_arm_pmu_get_current_counter_value(current_core_no, counter_no);

        do {
            /*
             * We need to read the counter value and the overflow flag as an atomic pair, but we cannot guarantee that the code
             * wont be preempted between the read of the counter and the overflow because we allow the counters to be read at PL0.
             * Instead we assume that the time between overflows should be so long that even if it was preempted such that the
             * overflow occurred between the read of the counter and the overflow flag, if we tried again a second time the overflow
             * could not happen twice. (we can't detect that anyway because of the nature of the overflow flag)
             *
             * We read the overflow before and after the counter is read. If the flag is different then we try again a second time
             * under the assumption that it is impossible to happen a second time. This ensures the overflow flag is valid for
             * the counter value (at the expense of a possible chance of small delay in read)
             */
            for (read_counter = 0; (read_counter < 2); ++read_counter) {
                /* read the overflow bit before */
                BM_READ_PMOVSR(overflowed_reg_before);

#ifndef BM_READ_PMCCNTR_64
                /* read the cycle counter as 32 bits */
                if (counter_hw_no == CYCLE_COUNTER_HW_NO) {
                    BM_READ_PMCCNTR(read_counter_value_32);
                }
                else
#endif
                /* an event counter instead */
                {
#ifdef BM_READ_PMEVCNTR
                BM_READ_PMEVCNTR(counter_hw_no, read_counter_value_32);
#else
                    /*
                     * If we don't have the reentrant counter read available,
                     * then we need to disable interrupts while we read it.
                     */
                    const bm_uintptr interrupt_status = barman_ext_disable_interrupts_local();

                    /* Read the counter non-reentrantly */
                    BM_READ_PMEVCNTR_NR(counter_hw_no, read_counter_value_32);

                    barman_ext_enable_interrupts_local(interrupt_status);
#endif
                }

                /* read the overflow bit after */
                BM_READ_PMOVSR(overflowed_reg_after);

                /* compare overflow before and after */
                if ((overflowed_reg_before & BM_BIT(counter_hw_no)) == (overflowed_reg_after & BM_BIT(counter_hw_no))) {
                    break;
                }
            }

            /* Check for an overflow in the lower 32-bits; event counters are 32 bits only but externally we expect counters to
             * be 64bits */
            if ((overflowed_reg_after & BM_BIT(counter_hw_no)) != 0) {
                /* Clear the overflow */
                BM_WRITE_PMOVSR(BM_BIT(counter_hw_no));
                barman_isb();

                /* Increment the top 32-bits */
                value64 = ((current_counter_value & 0xffffffff00000000ull) + 0x100000000ul) | read_counter_value_32;
            }
            else {
                /* Just insert the new bottom 32 bits */
                value64 = (current_counter_value & 0xffffffff00000000ull) | read_counter_value_32;
            }

            /* if value64 < current_counter_value then it means this thread was preempted between the read of
             * current_counter_value and the read of the counter and overflow bit by another thread that read and reset
             * an overflow. If the other thread updates current_counter_value then we fail and retry anyway, otherwise
             * if it was preempted by this thread again, we will write a wrong value. */
            if (value64 < current_counter_value) {
                value64 += 0x100000000;
            }

            /* Do the atomic update.
             * If value64 fails to write and the current_counter_value is less that value64, then retry writing
             * until we succeed, or until current_counter_value becomes >= value64 which would mean another thread wrote
             * a larger value in which case we retry */
            do {
                updated_value = barman_arm_pmu_set_current_counter_value(current_core_no, counter_no, &current_counter_value, value64);
            } while ((current_counter_value < value64) && (!updated_value));
        } while (!updated_value);
    }
    /* Not a valid counter */
    else {
        value64 = BM_ARM_PMU_INVALID_COUNTER_VALUE;
    }

    return value64;
}
