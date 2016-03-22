/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifdef RRPROFILE
#include "../oprofile.h"
#include "op_x86_model.h"
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/init.h>
#include <linux/errno.h>
 
/* We support CPUs that have performance counters like the Pentium Pro
 * with the NMI mode driver.
 */
 
extern int op_nmi_init(struct oprofile_operations * ops);
extern int op_nmi_timer_init(struct oprofile_operations * ops);
extern void op_nmi_exit(void);
extern void x86_backtrace(struct pt_regs * const regs, unsigned int depth);
		
int __init oprofile_arch_init(struct oprofile_operations * ops)
{
	int ret;

	ret = -ENODEV;

#ifdef CONFIG_X86_LOCAL_APIC
	ret = op_nmi_init(ops);
#endif
#ifndef RRPROFILE
#ifdef CONFIG_X86_IO_APIC
	if (ret < 0)
		ret = op_nmi_timer_init(ops);
#endif
#endif // !RRPROFILE
	ops->backtrace = x86_backtrace;

#ifdef RRPROFILE
	init_poll_idle();
#endif // RRPROFILE

	return ret;
}


void oprofile_arch_exit(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	op_nmi_exit();
#endif

#ifdef RRPROFILE
	exit_poll_idle();
#endif // RRPROFILE
}

#ifdef RRPROFILE
uint64_t oprofile_get_tb(void) {
	unsigned int low, high;
	uint64_t result;
	
	__asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high));
	result = ((uint64_t)high)<<32 | ((uint64_t)low);

	return result;
}
#endif // RRPROFILE
