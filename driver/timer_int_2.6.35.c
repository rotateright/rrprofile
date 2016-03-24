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
#include <linux/cpu.h>
#include <linux/hrtimer.h>
#include <asm/irq_regs.h>
#include <asm/ptrace.h>

#include "oprof.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
#define __cpuinit
#endif

static DEFINE_PER_CPU(struct hrtimer, oprofile_hrtimer);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
static int ctr_running;
#endif // >= 2.6.37

#ifdef RRPROFILE
#include <linux/kdebug.h>

static int timer_pop[NR_CPUS];
static uint64_t start_timestamp[NR_CPUS];
#endif // RRROFILE

static enum hrtimer_restart oprofile_hrtimer_notify(struct hrtimer *hrtimer)
{
#ifdef RRPROFILE
	int cpu = smp_processor_id();
	uint64_t end_timestamp = oprofile_get_tb();

	timer_pop[cpu]++;

	if(timer_pop[cpu] >= oprofile_timer_count) {
		oprofile_add_sample_start(start_timestamp[cpu]);
		oprofile_add_sample_stop(end_timestamp);
		oprofile_add_sample(get_irq_regs(), 0);

		timer_pop[cpu] = 0;
		start_timestamp[cpu] = oprofile_get_tb();
	}
	// TODO: allow for arbitrary specification of time interval (not just increments of TICK_NSEC)
#else
	oprofile_add_sample(get_irq_regs(), 0);
#endif // RRPROFILE
	hrtimer_forward_now(hrtimer, ns_to_ktime(TICK_NSEC));
	return HRTIMER_RESTART;
}

static void __oprofile_hrtimer_start(void *unused)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
	struct hrtimer *hrtimer = this_cpu_ptr(&oprofile_hrtimer);
#else
	struct hrtimer *hrtimer = &__get_cpu_var(oprofile_hrtimer);
#endif
#ifdef RRPROFILE
	int cpu;
#endif // RRPROFILE

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	if (!ctr_running)
		return;
#endif // >= 2.6.37

	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = oprofile_hrtimer_notify;

	hrtimer_start(hrtimer, ns_to_ktime(TICK_NSEC),
		      HRTIMER_MODE_REL_PINNED);

#ifdef RRPROFILE
	cpu = smp_processor_id();
	timer_pop[cpu] = 0;
	start_timestamp[cpu] = oprofile_get_tb();
#endif // RRPROFILE
}

static int oprofile_hrtimer_start(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	get_online_cpus();
	ctr_running = 1;
#endif // >= 2.6.37
	on_each_cpu(__oprofile_hrtimer_start, NULL, 1);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	put_online_cpus();
#endif // >= 2.6.37
	return 0;
}

static void __oprofile_hrtimer_stop(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(oprofile_hrtimer, cpu);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	if (!ctr_running)
		return;
#endif // >= 2.6.37

	hrtimer_cancel(hrtimer);
}

static void oprofile_hrtimer_stop(void)
{
	int cpu;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	get_online_cpus();
#endif // >= 2.6.37
	for_each_online_cpu(cpu)
		__oprofile_hrtimer_stop(cpu);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	ctr_running = 0;
	put_online_cpus();
#endif // >= 2.6.37
}

#ifdef RRPROFILE
#define MAX_COUNTER_VALUE		(INT_MAX / ADAPT_DECAY_FACTOR)

static int timer_adapt(void)
{
	if(oprofile_timer_count >= MAX_COUNTER_VALUE) {
		return 0;
	}

	oprofile_timer_count *= ADAPT_DECAY_FACTOR;

	return 1;
}
#endif // RRPROFILE

static int __cpuinit oprofile_cpu_notify(struct notifier_block *self,
					 unsigned long action, void *hcpu)
{
	long cpu = (long) hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		smp_call_function_single(cpu, __oprofile_hrtimer_start,
					 NULL, 1);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		__oprofile_hrtimer_stop(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata oprofile_cpu_notifier = {
	.notifier_call = oprofile_cpu_notify,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)

static int oprofile_hrtimer_setup(void)
{
	return register_hotcpu_notifier(&oprofile_cpu_notifier);
}

static void oprofile_hrtimer_shutdown(void)
{
	unregister_hotcpu_notifier(&oprofile_cpu_notifier);
}

int oprofile_timer_init(struct oprofile_operations *ops)
{
	ops->create_files	= NULL;
	ops->setup		= oprofile_hrtimer_setup;
	ops->shutdown		= oprofile_hrtimer_shutdown;
	ops->start		= oprofile_hrtimer_start;
	ops->stop		= oprofile_hrtimer_stop;
#ifdef RRPROFILE
	ops->adapt = timer_adapt;
	ops->backtrace = NULL;
#endif // RRPROFILE
	ops->cpu_type		= "timer";
	printk(KERN_INFO "rrprofile: using timer interrupt.\n");
	return 0;
}

void oprofile_timer_exit(void)
{
}

#else // < 3.3.0

int __init oprofile_timer_init(struct oprofile_operations *ops)
{
	int rc;

	rc = register_hotcpu_notifier(&oprofile_cpu_notifier);
	if (rc)
		return rc;
	ops->create_files = NULL;
	ops->setup = NULL;
	ops->shutdown = NULL;
	ops->start = oprofile_hrtimer_start;
	ops->stop = oprofile_hrtimer_stop;
#ifdef RRPROFILE
	ops->adapt = timer_adapt;
	ops->backtrace = NULL;
#endif // RRPROFILE
	ops->cpu_type = "timer";
	printk(KERN_INFO "rrprofile: using timer interrupt.\n");
	return 0;
}

void __exit oprofile_timer_exit(void)
{
	unregister_hotcpu_notifier(&oprofile_cpu_notifier);
}

#endif // >= 3.3.0
