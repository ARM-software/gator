	/**
	 * Copyright (C) Arm Limited 2015-2016. All rights reserved.
	 *
	 * This program is free software; you can redistribute it and/or modify
	 * it under the terms of the GNU General Public License version 2 as
	 * published by the Free Software Foundation.
	 */
	/* ELF64 header */
eh:
	.byte	0x7F		/* 0x00 e_ident[EI_MAG0] */
	.ascii "ELF"		/* 0x01 e_ident[EI_MAG1..3] */
	.byte	2		/* 0x04 e_ident[EI_CLASS] (1 = 32 bit, 2 = 64 bit) */
	.byte	1		/* 0x05 e_ident[EI_DATA] (1 = LE, 2 = BE) */
	.byte	1		/* 0x06 e_ident[EI_VERSION] */
	.byte	0		/* 0x07 e_ident[EI_OSABI] (usually 0 for Arm) */
	.byte	0		/* 0x08 e_ident[EI_ABIVERSION] (usually 0) */
	.fill	7, 1, 0		/* 0x09 e_ident[EI_PAD] */
	.hword	2		/* 0x10 e_type (2 = Executable) */
	.hword	0xb7		/* 0x12 e_machine (0x28 = Arm, 0xb7 = AArch64) */
	.word	1		/* 0x14 e_version */
	.xword	_start		/* 0x18 e_entry */
	.xword	ph - eh		/* 0x20 e_phoff */
	.xword	0		/* 0x28 e_shoff */
	.word	0		/* 0x30 e_flags */
	.hword	eh_size		/* 0x34 e_ehsize */
        .hword	ph_size		/* 0x36 e_phentsize */
	.hword	1		/* 0x38 e_phnum */
	.hword	0		/* 0x3a e_shentsize */
	.hword	0		/* 0x3c e_shnum */
	.hword	0		/* 0x3e e_shstrndx */

	.equ	eh_size, . - eh

	/* Program header */
ph:
	.word	1		/* 0x00 p_type (1 = PT_LOAD) */
	.word	5		/* 0x04 p_flags (4 = R || 1 = X) */
        .xword	0		/* 0x08 p_offset */
        .xword	eh		/* 0x10 p_vaddr */
	.xword	eh		/* 0x18 p_paddr */
	.xword	file_size	/* 0x20 p_filesz */
	.xword	file_size	/* 0x28 p_memsz */
	.xword	0x1000		/* 0x30 p_align */

	.equ	ph_size, . - ph

	.global	_start
_start:
	mov	w0, #1		/* stdout */
	ldr	x1, =bits	/* "64" */
	mov	x2, #7		/* strlen("aarch64") */
	mov	w8, #64		/* sys_write */
	svc	#0

	mov	w0, #0		/* Return code */
	mov	w8, #93		/* sys_exit */
	svc	#0

bits:	.ascii "aarch64"

	.equ	file_size, . - eh
