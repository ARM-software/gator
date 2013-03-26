/**
 * Copyright (C) ARM Limited 2010-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

/*
 * EABI backtrace stores {fp,lr} on the stack.
 */
struct frame_tail_eabi {
	union {
		struct {
			unsigned long fp;	// points to prev_lr
			unsigned long lr;
		};
		// Used to read 32 bit fp/lr from a 64 bit kernel
		struct {
			u32 fp_32;
			u32 lr_32;
		};
	};
};

static void arm_backtrace_eabi(int cpu, struct pt_regs *const regs, unsigned int depth)
{
#if defined(__arm__) || defined(__aarch64__)
	struct frame_tail_eabi *tail;
	struct frame_tail_eabi *next;
	struct frame_tail_eabi buftail;
#if defined(__arm__)
	const bool is_compat = false;
	unsigned long fp = regs->ARM_fp;
	unsigned long sp = regs->ARM_sp;
	unsigned long lr = regs->ARM_lr;
	const int frame_offset = 4;
#else
	// Is userspace aarch32 (32 bit)
	const bool is_compat = compat_user_mode(regs);
	unsigned long fp = (is_compat ? regs->regs[11] : regs->regs[29]);
	unsigned long sp = (is_compat ? regs->compat_sp : regs->sp);
	unsigned long lr = (is_compat ? regs->compat_lr : regs->regs[30]);
	const int frame_offset = (is_compat ? 4 : 0);
#endif
	int is_user_mode = user_mode(regs);

	if (!is_user_mode) {
		return;
	}

	/* entry preamble may not have executed */
	gator_add_trace(cpu, lr);

	/* check tail is valid */
	if (fp == 0 || fp < sp) {
		return;
	}

	tail = (struct frame_tail_eabi *)(fp - frame_offset);

	while (depth-- && tail && !((unsigned long)tail & 3)) {
		/* Also check accessibility of one struct frame_tail beyond */
		if (!access_ok(VERIFY_READ, tail, sizeof(struct frame_tail_eabi)))
			return;
		if (__copy_from_user_inatomic(&buftail, tail, sizeof(struct frame_tail_eabi)))
			return;

		lr = (is_compat ? buftail.lr_32 : buftail.lr);
		gator_add_trace(cpu, lr);

		/* frame pointers should progress back up the stack, towards higher addresses */
		next = (struct frame_tail_eabi *)(lr - frame_offset);
		if (tail >= next || lr == 0) {
			fp = (is_compat ? buftail.fp_32 : buftail.fp);
			next = (struct frame_tail_eabi *)(fp - frame_offset);
			/* check tail is valid */
			if (tail >= next || fp == 0) {
				return;
			}
		}

		tail = next;
	}
#endif
}

#if defined(__arm__) || defined(__aarch64__)
static int report_trace(struct stackframe *frame, void *d)
{
	unsigned int *depth = d, cookie = NO_COOKIE, cpu = get_physical_cpu();
	unsigned long addr = frame->pc;

	if (*depth) {
#if defined(MODULE)
		struct module *mod = __module_address(addr);
		if (mod) {
			cookie = get_cookie(cpu, current, mod->name, false);
			addr = addr - (unsigned long)mod->module_core;
		}
#endif
		marshal_backtrace(addr & ~1, cookie);
		(*depth)--;
	}

	return *depth == 0;
}
#endif

// Uncomment the following line to enable kernel stack unwinding within gator, note it can also be defined from the Makefile
// #define GATOR_KERNEL_STACK_UNWINDING
static void kernel_backtrace(int cpu, struct pt_regs *const regs)
{
#if defined(__arm__) || defined(__aarch64__)
#ifdef GATOR_KERNEL_STACK_UNWINDING
	int depth = gator_backtrace_depth;
#else
	int depth = 1;
#endif
	struct stackframe frame;
	if (depth == 0)
		depth = 1;
#if defined(__arm__)
	frame.fp = regs->ARM_fp;
	frame.sp = regs->ARM_sp;
	frame.lr = regs->ARM_lr;
	frame.pc = regs->ARM_pc;
#else
	frame.fp = regs->regs[29];
	frame.sp = regs->sp;
	frame.pc = regs->pc;
#endif
	walk_stackframe(&frame, report_trace, &depth);
#else
	marshal_backtrace(PC_REG & ~1, NO_COOKIE);
#endif
}
