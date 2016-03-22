/**
 * @file nmi_timer_int.c
 *
 * @remark Copyright 2003 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Zwane Mwaikambo <zwane@linuxpower.ca>
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/errno.h>
#ifdef RRPROFILE
#include "../oprofile.h"
#include "op_x86_model.h"
#include <linux/version.h>
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/rcupdate.h>

#include <asm/nmi.h>
#include <asm/apic.h>
#include <asm/ptrace.h>

#include <asm/kdebug.h>

static int nmi_timer_callback(struct pt_regs * regs, int cpu)
{
	oprofile_add_sample(regs, 0);
	return 1;
}

static int timer_start(void)
{
#ifdef RRPROFILE
	if(poll_idle_enabled) {
		enable_poll_idle();
	}
#endif // RRPROFILE

	disable_timer_nmi_watchdog();
	set_nmi_callback(nmi_timer_callback);
	return 0;
}


static void timer_stop(void)
{
	enable_timer_nmi_watchdog();
	unset_nmi_callback();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	synchronize_sched();  /* Allow already-started NMIs to complete. */
#else
	synchronize_kernel();
#endif

#ifdef RRPROFILE
	disable_poll_idle();
#endif // RRPROFILE
}


int __init op_nmi_timer_init(struct oprofile_operations * ops)
{
	extern int nmi_active;

	if (nmi_active <= 0)
		return -ENODEV;

	ops->start = timer_start;
	ops->stop = timer_stop;
	ops->cpu_type = "timer";
	printk(KERN_INFO "rrprofile: using NMI timer interrupt.\n");
	return 0;
}
