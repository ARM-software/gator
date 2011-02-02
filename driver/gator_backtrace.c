/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
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
	unsigned long fp; // points to prev_lr
	unsigned long lr;
};

static void arm_backtrace_eabi(int cpu, struct pt_regs * const regs, unsigned int depth)
{
#if defined(__arm__)
	struct frame_tail_eabi *tail;
	struct frame_tail_eabi *next;
	struct frame_tail_eabi *ptrtail;
	struct frame_tail_eabi buftail;
	unsigned long fp = regs->ARM_fp;
	unsigned long lr = regs->ARM_lr;
	int is_user_mode = user_mode(regs);

	if (!is_user_mode) {
		return;
	}

	/* entry preamble may not have executed */
	gator_add_trace(cpu, lr);

	/* check tail is valid */
	if (fp == 0) {
		return;
	}

	tail = (struct frame_tail_eabi *)(fp - 4);

	while (depth-- && tail && !((unsigned long) tail & 3)) {
		/* Also check accessibility of one struct frame_tail beyond */
		if (!access_ok(VERIFY_READ, tail, sizeof(struct frame_tail_eabi)))
			return;
		if (__copy_from_user_inatomic(&buftail, tail, sizeof(struct frame_tail_eabi)))
			return;
		ptrtail = &buftail;

		lr = ptrtail[0].lr;
		gator_add_trace(cpu, lr);

		/* frame pointers should progress back up the stack, towards higher addresses */
		next = (struct frame_tail_eabi *)(lr - 4);
		if (tail >= next || lr == 0) {
			fp = ptrtail[0].fp;
			next = (struct frame_tail_eabi *)(fp - 4);
			/* check tail is valid */
			if (tail >= next || fp == 0) {
				return;
			}
		}

		tail = next;
	}
#endif
}
