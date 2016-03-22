/**
 * @file timer_int.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#ifdef RRPROFILE
#include <linux/version.h>
#include "../oprofile.h"
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/profile.h>
#include <linux/init.h>
#include <asm/ptrace.h>

#include "oprof.h"

#ifdef RRPROFILE

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
#include <linux/kdebug.h>
#else
#include <asm/kdebug.h>
#endif

static int timer_pop[NR_CPUS];
static uint64_t start_timestamp[NR_CPUS];

#if LINUX_VERSION_CODE >=  KERNEL_VERSION(2,6,15)
static int timer_notify(struct pt_regs *regs)
{
#else
static int timer_notify(struct notifier_block * self, unsigned long val, void * data)
{
	struct pt_regs * regs = (struct pt_regs *)data;
#endif
	int cpu = smp_processor_id();
	uint64_t end_timestamp = oprofile_get_tb();

	timer_pop[cpu]++;

	if(timer_pop[cpu] >= oprofile_timer_count) {
		oprofile_add_sample_start(start_timestamp[cpu]);
		oprofile_add_sample_stop(end_timestamp);
		oprofile_add_sample(regs, 0);

		timer_pop[cpu] = 0;
		start_timestamp[cpu] = oprofile_get_tb();
	}
	return 0;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
static struct notifier_block timer_notifier = {
	.notifier_call  = timer_notify,
};
#endif

static void timer_cpu_init(void *arg)
{
	int cpu = smp_processor_id();
	timer_pop[cpu] = 0;
	start_timestamp[cpu] = oprofile_get_tb();
}

static int timer_start(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(timer_cpu_init, NULL, 1);
	on_each_cpu(oprofile_add_start, NULL, 1);
#else
	on_each_cpu(timer_cpu_init, NULL, 0, 1);
	on_each_cpu(oprofile_add_start, NULL, 0, 1);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	return register_timer_hook(timer_notify);
#else
	return register_profile_notifier(&timer_notifier);
#endif
}


static void timer_stop(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	unregister_timer_hook(timer_notify);
#else
	unregister_profile_notifier(&timer_notifier);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(oprofile_add_stop, NULL, 1);
#else
	on_each_cpu(oprofile_add_stop, NULL, 0, 1);
#endif
}

#define MAX_COUNTER_VALUE		(INT_MAX / ADAPT_DECAY_FACTOR)

static int timer_adapt(void)
{
	if(oprofile_timer_count >= MAX_COUNTER_VALUE) {
		return 0;
	}

	oprofile_timer_count *= ADAPT_DECAY_FACTOR;

	return 1;
}

#else // RRPROFILE

static int timer_notify(struct pt_regs *regs)
{
	oprofile_add_sample(regs, 0);
	return 0;
}

static int timer_start(void)
{
	return register_timer_hook(timer_notify);
}


static void timer_stop(void)
{
	unregister_timer_hook(timer_notify);
}

#endif // RRPROFILE

int oprofile_timer_init(struct oprofile_operations *ops)
{
	ops->create_files = NULL;
	ops->setup = NULL;
	ops->shutdown = NULL;
	ops->start = timer_start;
	ops->stop = timer_stop;
#ifdef RRPROFILE
	ops->adapt = timer_adapt;
	ops->backtrace = NULL;
#endif // RRPROFILE
	ops->cpu_type = "timer";
	printk(KERN_INFO "rrprofile: using timer interrupt.\n");

	return 0;
}

void oprofile_timer_exit(void)
{
}
