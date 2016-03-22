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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
#include <linux/kdebug.h>
#else
#include <asm/kdebug.h>
#endif // >= 2.6.22

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
static int profile_timer_exceptions_notify(unsigned int val, struct pt_regs *regs)
{
	oprofile_add_sample(regs, 0);
	return NMI_HANDLED;
}
#else
static int profile_timer_exceptions_notify(struct notifier_block *self,
						unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;

	switch(val) {
	case DIE_NMI:
		oprofile_add_sample(args->regs, 0);
		ret = NOTIFY_STOP;
		break;
	default:
		break;
        }
	return ret;
}

static struct notifier_block profile_timer_exceptions_nb = {
	.notifier_call = profile_timer_exceptions_notify,
	.next = NULL,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
	.priority = NMI_LOW_PRIOR,
#else
	.priority = 0
#endif // >= 2.6.38
};
#endif

static int timer_start(void)
{
#ifdef RRPROFILE
	if(poll_idle_enabled) {
		enable_poll_idle();
	}
#endif // RRPROFILE

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	if (register_nmi_handler(NMI_LOCAL, profile_timer_exceptions_notify,
					0, "rrprofile-timer"))
		return 1;
#else
	if (register_die_notifier(&profile_timer_exceptions_nb))
		return 1;
#endif
	return 0;
}


static void timer_stop(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	unregister_nmi_handler(NMI_LOCAL, "rrprofile-timer");
#else
	unregister_die_notifier(&profile_timer_exceptions_nb);
#endif
	synchronize_sched();  /* Allow already-started NMIs to complete. */

#ifdef RRPROFILE
	disable_poll_idle();
#endif // RRPROFILE
}

int __init op_nmi_timer_init(struct oprofile_operations * ops)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)
	if ((nmi_watchdog != NMI_IO_APIC) || (atomic_read(&nmi_active) <= 0))
		return -ENODEV;
#endif

	ops->start = timer_start;
	ops->stop = timer_stop;
	ops->cpu_type = "timer";
	printk(KERN_INFO "rrprofile: using NMI timer interrupt.\n");
	return 0;
}
