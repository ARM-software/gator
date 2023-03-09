/* Copyright (C) 2016-2023 by Arm Limited. */
/* SPDX-License-Identifier: BSD-3-Clause */

/** @file */

#ifndef INCLUDE_BARMAN_INTRINSICS
#define INCLUDE_BARMAN_INTRINSICS

#include "barman-types.h"

/**
 * @defgroup    bm_intrinsics   Aliases for intrinsic assembler operations
 * @{
 *
 * @def         barman_isb
 * @brief       Instruction Synchronization Barrier
 * @details     Inserts an "ISB SY" instruction
 *
 * @def         barman_dsb
 * @brief       Data Synchronization Barrier
 * @details     Inserts an "DSB SY" instruction
 *
 * @def         barman_dmb
 * @brief       Data Memory Barrier
 * @details     Inserts an "DMB SY" instruction
 */

#if !BM_ARM_TARGET_ARCH_IS_UNKNOWN()

#define barman_isb()    asm volatile("isb sy")
#define barman_dsb()    asm volatile("dsb sy")
#define barman_dmb()    asm volatile("dmb sy")

#else /* for unit tests */

#define barman_isb()
#define barman_dsb()
#define barman_dmb()

#endif

/**
 * @def         BM_READ_SYS_REG(op1, CRn, CRm, op2, out)
 * @brief       Read from a system register
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRn
 *              Is the CRn parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRn" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       op2
 *              Is the op2/opc2 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op2"/"opc2" field.
 * @param       out Type "bm_uinptr"
 *              L-value to write the result into.
 *
 * @def         BM_WRITE_SYS_REG(op1, CRn, CRm, op2, in)
 * @brief       Writes to a system register
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRn
 *              Is the CRn parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRn" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       op2
 *              Is the op2/opc2 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op2"/"opc2" field.
 * @param       in Type "bm_uinptr"
 *              Value to write into the register.
 *
 * @def         BM_READ_SPECIAL_REG(name, out)
 * @brief       Read from a special purpose register
 * @param       name
 *              Name of the special purpose register to read.
 * @param       out Type "bm_uinptr"
 *              L-value to write the result into.
 *
 * @def         BM_WRITE_SPECIAL_REG(name, in)
 * @brief       Write to a special purpose register
 * @param       name
 *              Name of the special purpose register to write into.
 * @param       in Type "bm_uinptr"
 *              Value to write into the register.
 *
 * @def         BM_SYS_REG_OP0_ENCODING
 * @brief       Usual op0/coproc encoding for system registers
 */

#if BM_ARM_64BIT_STATE
# define    BM_SYS_REG_OP0_ENCODING                   3
#elif BM_ARM_32BIT_STATE

/**
 * @def         BM_READ_SYS_REG_64(op1, CRm, out)
 * @brief       Read from a system register
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       out Type "bm_uint64"
 *              L-value to write the result into.
 *
 * @def         BM_WRITE_SYS_REG_64(op1, CRm, in)
 * @brief       Writes to a system register
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       in Type "bm_uint64"
 *              Value to write into the register.
 */
# define    BM_SYS_REG_OP0_ENCODING                   15
# define    BM_READ_SYS_REG_64(op1, CRm, out)                                                   \
                do {                                                                            \
                    bm_uint32 _bm_mrrc_lower;                                                   \
                    bm_uint32 _bm_mrrc_upper;                                                   \
                    BM_MRRC(BM_SYS_REG_OP0_ENCODING, op1, CRm, _bm_mrrc_lower, _bm_mrrc_upper); \
                    out = (((bm_uint64) _bm_mrrc_upper) << 32) | _bm_mrrc_lower;                \
                } while (0)
# define    BM_WRITE_SYS_REG_64(op1, CRm, in)         BM_MCRR(BM_SYS_REG_OP0_ENCODING, op1, CRm, (bm_uint32) in, (bm_uint32) (in >> 32))
#else
# define    BM_SYS_REG_OP0_ENCODING                   ERROR_NOT_ARM
#endif

#define     BM_READ_SYS_REG(op1, CRn, CRm, op2, out)  BM_MRC(BM_SYS_REG_OP0_ENCODING, op1, CRn, CRm, op2, out)
#define     BM_WRITE_SYS_REG(op1, CRn, CRm, op2, in)  BM_MCR(BM_SYS_REG_OP0_ENCODING, op1, CRn, CRm, op2, in)
#define     BM_READ_SPECIAL_REG(name, out)            BM_MRS(name, out)
#define     BM_WRITE_SPECIAL_REG(name, in)            BM_MSR(name, in)

#if BM_ARM_64BIT_STATE || BM_COMPILER_IS_ARMCC
/* AArch64 and (AArch32 with ARMASM psuedo instruction) allow accessing system registers by name */

/**
 * @def         BM_READ_SYS_REG_NAMED(name, out)
 * @brief       Read from a system register by name
 * @param       name
 *              Name of the system register to read.
 * @param       out Type "bm_uinptr"
 *              L-value to write the result into.
 *
 * @def         BM_WRITE_SYS_REG_NAMED(name, in)
 * @brief       Write to a system register by name
 * @param       name
 *              Name of the system register to write into.
 * @param       in Type "bm_uinptr"
 *              Value to write into the register.
 */
#define     BM_READ_SYS_REG_NAMED(name, out)          BM_MRS(name, out)
#define     BM_WRITE_SYS_REG_NAMED(name, in)          BM_MSR(name, in)
#endif


/**
 * @def         BM_MRC(op0, op1, CRn, CRm, op2, Rt)
 * @brief       MRC instruction (or manually encoded MRS on AArch64)
 * @param       op0
 *              Is the op1/coproc parameter within the System register encoding space,
 *              in the range 0 to 7 or 8 to 15, respectively, encoded in the "op1"/"coproc" field.
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRn
 *              Is the CRn parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRn" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       op2
 *              Is the op2/opc2 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op2"/"opc2" field.
 * @param       Rt Type "bm_uinptr"
 *              L-value to write the result into.
 *
 * @def         BM_MCR(op0, op1, CRn, CRm, op2, Rt)
 * @brief       MCR instruction (or manually encoded MSR on AArch64)
 * @param       op0
 *              Is the op1/coproc parameter within the System register encoding space,
 *              in the range 0 to 7 or 8 to 15, respectively, encoded in the "op1"/"coproc" field.
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRn
 *              Is the CRn parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRn" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       op2
 *              Is the op2/opc2 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op2"/"opc2" field.
 * @param       Rt Type "bm_uinptr"
 *              Value to write into the register.
 *
 * @def         BM_MRS(name, Rt)
 * @brief       MRS instruction
 * @param       name
 *              Name of the special purpose register to read.
 * @param       Rt Type "bm_uinptr"
 *              L-value to write the result into.
 *
 * @def         BM_MSR(name, Rt)
 * @brief       MSR instruction
 * @param       name
 *              Name of the special purpose register to write into.
 * @param       in Type "bm_uinptr"
 *              Value to write into the register.
 */

#if BM_ARM_64BIT_STATE
# define    BM_MRC(op0, op1, CRn, CRm, op2, Rt)    asm volatile ("mrs %0, S" BM_STRINGIFY_VAL(op0) "_" BM_STRINGIFY_VAL(op1) "_C" BM_STRINGIFY_VAL(CRn) "_C" BM_STRINGIFY_VAL(CRm) "_" BM_STRINGIFY_VAL(op2) : "=r"(Rt))
# define    BM_MCR(op0, op1, CRn, CRm, op2, Rt)    asm volatile ("msr S" BM_STRINGIFY_VAL(op0) "_" BM_STRINGIFY_VAL(op1) "_C" BM_STRINGIFY_VAL(CRn) "_C" BM_STRINGIFY_VAL(CRm) "_" BM_STRINGIFY_VAL(op2) ", %0" :: "r"(Rt))
# define    BM_MRS(name, Rt)                       asm volatile ("mrs %0, " BM_STRINGIFY_VAL(name) : "=r"(Rt))
# define    BM_MSR(name, Rt)                       asm volatile ("msr " BM_STRINGIFY_VAL(name) ", %0" :: "r"(Rt))
#elif BM_ARM_32BIT_STATE

/**
 * @def         BM_MRRC(op0, op1, CRm, Rt)
 * @brief       MRRC instruction
 * @param       op0
 *              Is the System register encoding space,
 *              in the range 8 to 15, encoded in the "op1"/"coproc" field.
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       Rt Type "bm_uint32"
 *              L-value to write the first result into.
 * @param       Rt2 Type "bm_uint32"
 *              L-value to write the second result into.
 *
 * @def         BM_MCRR(op0, op1, CRm, Rt)
 * @brief       MCRR instruction
 * @param       op0
 *              Is the System register encoding space,
 *              in the range 8 to 15, encoded in the "op1"/"coproc" field.
 * @param       op1
 *              Is the op1/opc1 parameter within the System register encoding space,
 *              in the range 0 to 7, encoded in the "op1"/"opc1" field.
 * @param       CRm
 *              Is the CRm parameter within the System register encoding space,
 *              in the range 0 to 15, encoded in the "CRm" field.
 * @param       Rt Type "bm_uint32"
 *              First value to write into the register.
 * @param       Rt2 Type "bm_uint32"
 *              Second value to write into the register.
 */
# if BM_COMPILER_IS_ARMCC
#   define  BM_MRC(op0, op1, CRn, CRm, op2, Rt)    __asm ("mrc p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", " BM_STRINGIFY_VAL(Rt) ", c" BM_STRINGIFY_VAL(CRn) ", c" BM_STRINGIFY_VAL(CRm) ", " BM_STRINGIFY_VAL(op2))
#   define  BM_MCR(op0, op1, CRn, CRm, op2, Rt)    __asm ("mcr p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", " BM_STRINGIFY_VAL(Rt) ", c" BM_STRINGIFY_VAL(CRn) ", c" BM_STRINGIFY_VAL(CRm) ", " BM_STRINGIFY_VAL(op2))
#   define  BM_MRRC(op0, op1, CRm, Rt, Rt2)        __asm ("mrrc p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", " BM_STRINGIFY_VAL(Rt) ", " BM_STRINGIFY_VAL(Rt2) ", c" BM_STRINGIFY_VAL(CRm))
#   define  BM_MCRR(op0, op1, CRm, Rt, Rt2)        __asm ("mcrr p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", " BM_STRINGIFY_VAL(Rt) ", " BM_STRINGIFY_VAL(Rt2) ", c" BM_STRINGIFY_VAL(CRm))
#   define  BM_MRS(name, Rt)                       __asm ("mrs " BM_STRINGIFY_VAL(Rt) ", " BM_STRINGIFY_VAL(name))
#   define  BM_MSR(name, Rt)                       __asm ("msr " BM_STRINGIFY_VAL(name) ", " BM_STRINGIFY_VAL(Rt))
# else
#   define  BM_MRC(op0, op1, CRn, CRm, op2, Rt)    asm volatile ("mrc p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", %0, c" BM_STRINGIFY_VAL(CRn) ", c" BM_STRINGIFY_VAL(CRm) ", " BM_STRINGIFY_VAL(op2) : "=r"(Rt))
#   define  BM_MCR(op0, op1, CRn, CRm, op2, Rt)    asm volatile ("mcr p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", %0, c" BM_STRINGIFY_VAL(CRn) ", c" BM_STRINGIFY_VAL(CRm) ", " BM_STRINGIFY_VAL(op2) :: "r"(Rt))
#   define  BM_MRRC(op0, op1, CRm, Rt, Rt2)        asm volatile ("mrrc p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", %0, %1, c" BM_STRINGIFY_VAL(CRm) : "=r"(Rt), "=r"(Rt2))
#   define  BM_MCRR(op0, op1, CRm, Rt, Rt2)        asm volatile ("mcrr p" BM_STRINGIFY_VAL(op0) ", " BM_STRINGIFY_VAL(op1) ", %0, %1, c" BM_STRINGIFY_VAL(CRm) :: "r"(Rt), "r"(Rt2))
#   define  BM_MRS(name, Rt)                       asm volatile ("mrs %0, " BM_STRINGIFY_VAL(name) : "=r"(Rt))
#   define  BM_MSR(name, Rt)                       asm volatile ("msr " BM_STRINGIFY_VAL(name) ", %0" :: "r"(Rt))
# endif
#else
# define    BM_MRC(op0, op1, CRn, CRm, op2, Rt)    Rt = 0
# define    BM_MCR(op0, op1, CRn, CRm, op2, Rt)    (void) (Rt)
# define    BM_MRS(name, Rt)                       Rt = 0
# define    BM_MSR(name, Rt)                       (void) (Rt)
#endif

/** @} */

#endif /* INCLUDE_BARMAN_INTRINSICS */
