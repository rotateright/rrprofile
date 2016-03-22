/**
 * @file op_model_ppro.h
 * pentium pro / P6 model-specific MSR operations
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 * @author Graydon Hoare
 */

#ifdef RRPROFILE
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/slab.h>
#include "../oprofile.h"
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <asm/ptrace.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/nmi.h>
 
#include "op_x86_model.h"
#include "op_counter.h"

#ifdef RRPROFILE
static int num_counters = 2;
static int counter_width = 32;
#else
#define NUM_COUNTERS 2
#define NUM_CONTROLS 2
#endif // RRPROFILE

#define CTR_READ(l,h,msrs,c) do {rdmsr(msrs->counters[(c)].addr, (l), (h));} while (0)
#define CTR_WRITE(l,msrs,c) do {wrmsr(msrs->counters[(c)].addr, -(u32)(l), -1);} while (0)
#define CTR_OVERFLOWED(n) (!((n) & (1U<<31)))

#define CTRL_READ(l,h,msrs,c) do {rdmsr((msrs->controls[(c)].addr), (l), (h));} while (0)
#define CTRL_WRITE(l,h,msrs,c) do {wrmsr((msrs->controls[(c)].addr), (l), (h));} while (0)
#define CTRL_SET_ACTIVE(n) (n |= (1<<22))
#define CTRL_SET_INACTIVE(n) (n &= ~(1<<22))
#define CTRL_CLEAR(x) (x &= (1<<21))
#define CTRL_SET_ENABLE(val) (val |= 1<<20)
#define CTRL_SET_USR(val,u) (val |= ((u & 1) << 16))
#define CTRL_SET_KERN(val,k) (val |= ((k & 1) << 17))
#define CTRL_SET_UM(val, m) (val |= (m << 8))
#define CTRL_SET_EVENT(val, e) (val |= e)

#ifdef RRPROFILE
static u64 *reset_value;
static uint64_t start_timestamp[NR_CPUS];
#else
static unsigned long reset_value[NUM_COUNTERS];
#endif // RRPROFILE

#ifdef RRPROFILE
/*
 * Intel "Architectural Performance Monitoring" CPUID
 * detection/enumeration details:
 */
union rr_cpuid10_eax {
	struct {
		unsigned int version_id:8;
		unsigned int num_events:8;
		unsigned int bit_width:8;
		unsigned int mask_length:8;
	} split;
	unsigned int full;
};


static int ppro_init(struct oprofile_operations *ignore)
{
	if (!reset_value) {
		reset_value = kmalloc(sizeof(reset_value[0]) * num_counters, GFP_ATOMIC);
		if (!reset_value)
			return -ENOMEM;
	}

	return 0;
}
#endif // RRPROFILE

static void ppro_fill_in_addresses(struct op_msrs * const msrs)
{
	msrs->counters[0].addr = MSR_P6_PERFCTR0;
	msrs->counters[1].addr = MSR_P6_PERFCTR1;
	
	msrs->controls[0].addr = MSR_P6_EVNTSEL0;
	msrs->controls[1].addr = MSR_P6_EVNTSEL1;
}


static void ppro_setup_ctrs(struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;

	/* clear all counters */
#ifdef RRPROFILE
	for (i = 0 ; i < num_counters; ++i) {
#else
	for (i = 0 ; i < NUM_CONTROLS; ++i) {
#endif // RRPROFILE
		CTRL_READ(low, high, msrs, i);
		CTRL_CLEAR(low);
		CTRL_WRITE(low, high, msrs, i);
	}
	
	/* avoid a false detection of ctr overflows in NMI handler */
#ifdef RRPROFILE
	for (i = 0; i < num_counters; ++i) {
#else
	for (i = 0; i < NUM_COUNTERS; ++i) {
#endif // RRPROFILE
		CTR_WRITE(1, msrs, i);
	}

	/* enable active counters */
#ifdef RRPROFILE
	for (i = 0; i < num_counters; ++i) {
#else
	for (i = 0; i < NUM_COUNTERS; ++i) {
#endif // RRPROFILE
		if (counter_config[i].enabled) {
			reset_value[i] = counter_config[i].count;

			CTR_WRITE(counter_config[i].count, msrs, i);

			CTRL_READ(low, high, msrs, i);
			CTRL_CLEAR(low);
			CTRL_SET_ENABLE(low);
			CTRL_SET_USR(low, counter_config[i].user);
			CTRL_SET_KERN(low, counter_config[i].kernel);
			CTRL_SET_UM(low, counter_config[i].unit_mask);
			CTRL_SET_EVENT(low, counter_config[i].event);
			CTRL_WRITE(low, high, msrs, i);
		}
	}
}

#ifdef RRPROFILE
static void ppro_start(struct op_msrs const * const msrs);
static void ppro_stop(struct op_msrs const * const msrs);
#endif // RRPROFILE

static int ppro_check_ctrs(struct pt_regs * const regs,
			   struct op_msrs const * const msrs)
{
	unsigned int low, high;
	int i;
#ifdef RRPROFILE
	int cpu = smp_processor_id();
	uint64_t end_timestamp = oprofile_get_tb();

	ppro_stop(msrs);
#endif // RRPROFILE
	
#ifdef RRPROFILE
	for (i = 0 ; i < num_counters; ++i) {
#else
	for (i = 0 ; i < NUM_COUNTERS; ++i) {
#endif // RRPROFILE
		CTR_READ(low, high, msrs, i);
		if (CTR_OVERFLOWED(low)) {
#ifdef RRPROFILE
			oprofile_add_sample_start(start_timestamp[cpu]);
			oprofile_add_sample_stop(end_timestamp);
#endif // RRPROFILE
			oprofile_add_sample(regs, i);
			CTR_WRITE(reset_value[i], msrs, i);
		}
	}

	/* Only P6 based Pentium M need to re-unmask the apic vector but it
	 * doesn't hurt other P6 variant */
	apic_write(APIC_LVTPC, apic_read(APIC_LVTPC) & ~APIC_LVT_MASKED);

#ifdef RRPROFILE
	ppro_start(msrs);
	start_timestamp[cpu] = oprofile_get_tb();
#endif // RRPROFILE

	/* We can't work out if we really handled an interrupt. We
	 * might have caught a *second* counter just after overflowing
	 * the interrupt for this counter then arrives
	 * and we don't find a counter that's overflowed, so we
	 * would return 0 and get dazed + confused. Instead we always
	 * assume we found an overflow. This sucks.
	 */
	
	return 1;
}

 
static void ppro_start(struct op_msrs const * const msrs)
{
	unsigned int low,high;
#ifdef RRPROFILE
	int cpu = smp_processor_id();
	start_timestamp[cpu] = oprofile_get_tb();
#endif // RRPROFILE

	CTRL_READ(low, high, msrs, 0);
	CTRL_SET_ACTIVE(low);
	CTRL_WRITE(low, high, msrs, 0);
}


static void ppro_stop(struct op_msrs const * const msrs)
{
	unsigned int low,high;
	CTRL_READ(low, high, msrs, 0);
	CTRL_SET_INACTIVE(low);
	CTRL_WRITE(low, high, msrs, 0);
}

#ifdef RRPROFILE
static void ppro_exit(void)
{
	if (reset_value) {
		kfree(reset_value);
		reset_value = NULL;
	}
}
#endif // RRPROFILE


struct op_x86_model_spec const op_ppro_spec = {
#ifdef RRPROFILE
	.init = &ppro_init,
    .exit = &ppro_exit,
	.num_counters = 2, /* can be overriden */
	.num_controls = 2, /* ditto */
#else
	.num_counters = NUM_COUNTERS,
	.num_controls = NUM_CONTROLS,
#endif // RRPROFILE
	.fill_in_addresses = &ppro_fill_in_addresses,
	.setup_ctrs = &ppro_setup_ctrs,
	.check_ctrs = &ppro_check_ctrs,
	.start = &ppro_start,
	.stop = &ppro_stop
};

#ifdef RRPROFILE
/*
 * Architectural performance monitoring.
 *
 * Newer Intel CPUs (Core1+) have support for architectural
 * events described in CPUID 0xA. See the IA32 SDM Vol3b.18 for details.
 * The advantage of this is that it can be done without knowing about
 * the specific CPU.
 */

static void arch_perfmon_setup_counters(void)
{
	union rr_cpuid10_eax eax;

	eax.full = cpuid_eax(0xa);

	/* Workaround for BIOS bugs in 6/15. Taken from perfmon2 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,38)
        if (eax.split.version_id == 0 && __this_cpu_read(cpu_info.x86) == 6 &&
			__this_cpu_read(cpu_info.x86_model) == 15) {
#else
	if (eax.split.version_id == 0 && current_cpu_data.x86 == 6 &&
			current_cpu_data.x86_model == 15) {
#endif
		eax.split.version_id = 2;
		eax.split.num_events = 2;
		eax.split.bit_width = 40;
	}

	if (counter_width < eax.split.bit_width)
		counter_width = eax.split.bit_width;

	// FIXME: We need to implement support for > 2 pmcs.
	// num_counters = eax.split.num_events;
	num_counters = 2;
	op_arch_perfmon_spec.num_counters = num_counters;
	op_arch_perfmon_spec.num_controls = num_counters;
}

static int arch_perfmon_init(struct oprofile_operations *ignore)
{
	arch_perfmon_setup_counters();
	return ppro_init(ignore);
}

struct op_x86_model_spec op_arch_perfmon_spec = {
	.init                   = &arch_perfmon_init,
	/* num_counters/num_controls filled in at runtime */
	.fill_in_addresses      = &ppro_fill_in_addresses,
	/* user space does the cpuid check for available events */
	.setup_ctrs             = &ppro_setup_ctrs,
	.check_ctrs             = &ppro_check_ctrs,
	.start                  = &ppro_start,
	.stop                   = &ppro_stop,
	.exit					= &ppro_exit
};
#endif // RRPROFILE

