/**
 * Copyright (C) 2005 Brian Rogan <bcr6@cornell.edu>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
**/

#ifdef RRPROFILE
#include "../oprofile.h"
#include "oprofile_impl.h"
#include <linux/version.h>
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

#ifdef RRPROFILE
#include <asm/irq.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
static int rr_validate_sp(unsigned long sp, struct task_struct *p, unsigned long nbytes)
{
	unsigned long stack_page = (unsigned long)p->thread_info;

	if (sp >= stack_page + sizeof(struct thread_struct)
		&& sp <= stack_page + THREAD_SIZE - nbytes) {
		return 1;
	}
	return 0;
}
#endif
#endif // RRPROFILE

#define STACK_SP(STACK)		*(STACK)

#define STACK_LR64(STACK)	*((unsigned long *)(STACK) + 2)
#define STACK_LR32(STACK)	*((unsigned int *)(STACK) + 1)

#ifdef CONFIG_PPC64
#define STACK_LR(STACK)         STACK_LR64(STACK)
#else
#define STACK_LR(STACK)         STACK_LR32(STACK)
#endif

static unsigned int user_getsp32(unsigned int sp, int is_first)
{
	unsigned int stack_frame[2];

	if (!access_ok(VERIFY_READ, sp, sizeof(stack_frame)))
		return 0;

	/*
	 * The most likely reason for this is that we returned -EFAULT,
	 * which means that we've done all that we can do from
	 * interrupt context.
	 */
	if (__copy_from_user_inatomic(stack_frame, (void *)(long)sp,
					sizeof(stack_frame)))
		return 0;

	if (!is_first)
		oprofile_add_trace(STACK_LR32(stack_frame));

	/*
	 * We do not enforce increasing stack addresses here because
	 * we may transition to a different stack, eg a signal handler.
	 */
	return STACK_SP(stack_frame);
}

#ifdef CONFIG_PPC64
static unsigned long user_getsp64(unsigned long sp, int is_first)
{
	unsigned long stack_frame[3];

	if (!access_ok(VERIFY_READ, sp, sizeof(stack_frame)))
		return 0;

	if (__copy_from_user_inatomic(stack_frame, (void *)sp,
					sizeof(stack_frame)))
		return 0;

	if (!is_first)
		oprofile_add_trace(STACK_LR64(stack_frame));
	
	return STACK_SP(stack_frame);
}
#endif

static unsigned long kernel_getsp(unsigned long sp, int is_first)
{
	unsigned long *stack_frame = (unsigned long *)sp;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
	if (!rr_validate_sp(sp, current, STACK_FRAME_OVERHEAD))
#else
	if (!validate_sp(sp, current, STACK_FRAME_OVERHEAD))
#endif
		return 0;
	
#ifdef RRPROFILE
	if (!is_first) {
		if(is_kernel_addr(STACK_LR(stack_frame))) {
			oprofile_add_trace(STACK_LR(stack_frame));
		}
	}
#else
	if (!is_first)
		oprofile_add_trace(STACK_LR(stack_frame));
#endif // RRPROFILE

	/*
	 * We do not enforce increasing stack addresses here because
	 * we might be transitioning from an interrupt stack to a kernel
	 * stack. validate_sp() is designed to understand this, so just
	 * use it.
	 */
	return STACK_SP(stack_frame);
}

void op_powerpc_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	unsigned long sp = regs->gpr[1];
#ifdef RRPROFILE
	unsigned long lr = regs->link;
#endif // RRPROFILE
	int first_frame = 1;

	/* We ditch the top stackframe so need to loop through an extra time */
	depth += 1;

	if (!user_mode(regs)) {
		while (depth--) {
			sp = kernel_getsp(sp, first_frame);
			if (!sp)
				break;
			first_frame = 0;
		}
	} else {

#ifdef RRPROFILE
#ifdef CONFIG_PPC64
	if (!test_thread_flag(TIF_32BIT)) {
		if (depth) {
			sp = user_getsp64(sp, first_frame);
			depth--;
		}
		if (sp) {
			while (depth--) {
				sp = user_getsp64(sp, first_frame);
				if (!sp)
					break;
				if (first_frame)
					oprofile_add_trace(lr);
				first_frame = 0;
			}
		}

		return;
	}
#endif
	if (depth) {
		sp = user_getsp32(sp, first_frame);
		depth--;
	}
	if (sp) {
		while (depth--) {
			sp = user_getsp32(sp, first_frame);
			if (!sp)
				break;
			if (first_frame)
				oprofile_add_trace(lr);
			first_frame = 0;
		}
	}
#else
#ifdef CONFIG_PPC64
		if (!test_thread_flag(TIF_32BIT)) {
			while (depth--) {
				sp = user_getsp64(sp, first_frame);
				if (!sp)
					break;
				first_frame = 0;
			}

			return;
		}
#endif

		while (depth--) {
			sp = user_getsp32(sp, first_frame);
			if (!sp)
				break;
			first_frame = 0;
		}
#endif // RRPROFILE
	}
}
