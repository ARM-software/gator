	/**
	 * Copyright (C) Arm Limited 2015-2016. All rights reserved.
	 *
	 * This program is free software; you can redistribute it and/or modify
	 * it under the terms of the GNU General Public License version 2 as
	 * published by the Free Software Foundation.
	 */
	/* ELF32 header */
eh:
	.byte	0x7F		/* 0x00 e_ident[EI_MAG0] */
	.ascii "ELF"		/* 0x01 e_ident[EI_MAG1..3] */
	.byte	1		/* 0x04 e_ident[EI_CLASS] (1 = 32 bit, 2 = 64 bit) */
	.byte	1		/* 0x05 e_ident[EI_DATA] (1 = LE, 2 = BE) */
	.byte	1		/* 0x06 e_ident[EI_VERSION] */
	.byte	0		/* 0x07 e_ident[EI_OSABI] (usually 0 for Arm) */
	.byte	0		/* 0x08 e_ident[EI_ABIVERSION] (usually 0) */
	.fill	7, 1, 0		/* 0x09 e_ident[EI_PAD] */
	.hword	2		/* 0x10 e_type (2 = Executable) */
	.hword	0x28		/* 0x12 e_machine (0x28 = Arm, 0xb7 = AArch64) */
	.word	1		/* 0x14 e_version */
	.word	_start		/* 0x18 e_entry */
	.word	ph - eh		/* 0x1c e_phoff */
	.word	0		/* 0x20 e_shoff */
	.word	0x5000002	/* 0x24 e_flags (ABI version 5, has entry point) */
	.hword	eh_size		/* 0x28 e_ehsize */
        .hword	ph_size		/* 0x2a e_phentsize */
	.hword	1		/* 0x2c e_phnum */
	.hword	0		/* 0x2e e_shentsize */
	.hword	0		/* 0x30 e_shnum */
	.hword	0		/* 0x32 e_shstrndx */

	.equ	eh_size, . - eh

	/* Program header */
ph:
	.word	1		/* 0x00 p_type (1 = PT_LOAD) */
        .word	0		/* 0x04 p_offset */
        .word	eh		/* 0x08 p_vaddr */
	.word	eh		/* 0x0c p_paddr */
	.word	file_size	/* 0x10 p_filesz */
	.word	file_size	/* 0x14 p_memsz */
	.word	5		/* 0x18 p_flags (4 = R || 1 = X) */
	.word	0x1000		/* 0x1c p_align */

	.equ	ph_size, . - ph

	.global	_start
_start:
	movt	r0, #0		/* ARMv7 only instruction */
	vmov.f64 d0, d0		/* Check for VFP support */
	mov	r0, #1		/* stdout */
	ldr	r1, =bits	/* "32" */
	mov	r2, #5		/* strlen("armv7") */
	mov	r7, #4		/* sys_write */
	svc	#0

	mov	r0, #0		/* Return code */
	mov	r7, #1		/* sys_exit */
	svc	#0

bits:	.ascii "armv7"

	.equ	file_size, . - eh
