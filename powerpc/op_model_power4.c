/*
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef RRRPROFILE
#include "../oprofile.h"
#include <linux/version.h>
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/init.h>
#include <linux/smp.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17)
#include <asm/firmware.h>
#endif
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/rtas.h>
#ifdef RRPROFILE
#include "oprofile_impl.h"
#else
#include <asm/oprofile_impl.h>
#endif // RRPROFILE
#include <asm/reg.h>

#define dbg(args...)

static unsigned long reset_value[OP_MAX_COUNTER];

#ifdef RRPROFILE
static uint64_t start_timestamp[NR_CPUS];
#endif // RRPROFILE

static int oprofile_running;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
static int mmcra_has_sihv = 0;
#endif

/* mmcr values are set in power4_reg_setup, used in power4_cpu_setup */
static u32 mmcr0_val;
static u64 mmcr1_val;
static u64 mmcra_val;

#ifdef RRPROFILE
// Event Select
#define MMCR0_PMC1SEL 0x0000000000001F00ULL /* PMC 1 Event */
#define MMCR0_PMC2SEL 0x000000000000003EULL /* PMC 2 Event */
#define MMCR1_PMC3SEL 0x00000000F8000000ULL /* PMC 3 Event */
#define MMCR1_PMC4SEL 0x0000000007C00000ULL /* PMC 4 Event */
#define MMCR1_PMC5SEL 0x00000000003E0000ULL /* PMC 5 Event */
#define MMCR1_PMC6SEL 0x000000000001F000ULL /* PMC 6 Event */
#define MMCR1_PMC7SEL 0x0000000000000F80ULL /* PMC 7 Event */
#define MMCR1_PMC8SEL 0x000000000000007CULL /* PMC 8 Event */

// Unit Select
#define MMCR1_TTM0SEL 0xC000000000000000ULL
#define MMCR1_TTM1SEL 0x1800000000000000ULL
#define MMCR1_TTM3SEL 0x0060000000000000ULL

// Byte Lane Select
#define MMCR1_DBG0SEL 0x000C000000000000ULL
#define MMCR1_DBG1SEL 0x0003000000000000ULL
#define MMCR1_DBG2SEL 0x0000C00000000000ULL
#define MMCR1_DBG3SEL 0x0000300000000000ULL

// Event Adder Lane Select
#define MMCR1_PMC1ADDRSEL 0x0000008000000000ULL
#define MMCR1_PMC2ADDRSEL 0x0000004000000000ULL
#define MMCR1_PMC3ADDRSEL 0x0000000200000000ULL
#define MMCR1_PMC4ADDRSEL 0x0000000100000000ULL
#define MMCR1_PMC5ADDRSEL 0x0000001000000000ULL
#define MMCR1_PMC6ADDRSEL 0x0000002000000000ULL
#define MMCR1_PMC7ADDRSEL 0x0000000400000000ULL
#define MMCR1_PMC8ADDRSEL 0x0000000800000000ULL

// Speculative Count Event Select
#define MMCR1_SPCSEL 0x0000000000000003ULL

// Event Select - Shift
#define MMCR0_PMC1_SHIFT 8
#define MMCR0_PMC2_SHIFT 1
#define MMCR1_PMC3_SHIFT 27
#define MMCR1_PMC4_SHIFT 22
#define MMCR1_PMC5_SHIFT 17
#define MMCR1_PMC6_SHIFT 12
#define MMCR1_PMC7_SHIFT 7
#define MMCR1_PMC8_SHIFT 2

// Unit Select - Shift
#define MMCR1_TTM0SEL_SHIFT 62
#define MMCR1_TTM1SEL_SHIFT 59
#define MMCR1_TTM3SEL_SHIFT 53

// Byte Lane Select - Shift
#define MMCR1_DBG0SEL_SHIFT 50
#define MMCR1_DBG1SEL_SHIFT 48
#define MMCR1_DBG2SEL_SHIFT 46
#define MMCR1_DBG3SEL_SHIFT 44

// Event Adder Lane Select - Shift
#define MMCR1_PMC1ADDRSEL_SHIFT 39
#define MMCR1_PMC2ADDRSEL_SHIFT 38
#define MMCR1_PMC3ADDRSEL_SHIFT 33
#define MMCR1_PMC4ADDRSEL_SHIFT 32
#define MMCR1_PMC5ADDRSEL_SHIFT 36
#define MMCR1_PMC6ADDRSEL_SHIFT 37
#define MMCR1_PMC7ADDRSEL_SHIFT 34
#define MMCR1_PMC8ADDRSEL_SHIFT 35

// Speculative Count Event Select - Shift
#define MMCR1_SPCSEL_SHIFT 0

#define mmcr0_event1(event) \
	((event << MMCR0_PMC1_SHIFT) & MMCR0_PMC1SEL)
#define mmcr0_event2(event) \
	((event << MMCR0_PMC2_SHIFT) & MMCR0_PMC2SEL)

#define mmcr1_event3(event) \
	((event << MMCR1_PMC3_SHIFT) & MMCR1_PMC3SEL)
#define mmcr1_event4(event) \
	((event << MMCR1_PMC4_SHIFT) & MMCR1_PMC4SEL)
#define mmcr1_event5(event) \
	((event << MMCR1_PMC5_SHIFT) & MMCR1_PMC5SEL)
#define mmcr1_event6(event) \
	((event << MMCR1_PMC6_SHIFT) & MMCR1_PMC6SEL)
#define mmcr1_event7(event) \
	((event << MMCR1_PMC7_SHIFT) & MMCR1_PMC7SEL)
#define mmcr1_event8(event) \
	((event << MMCR1_PMC8_SHIFT) & MMCR1_PMC8SEL)

#define mmcr1_ttm0(value) \
	((value << MMCR1_TTM0SEL_SHIFT) & MMCR1_TTM0SEL)
#define mmcr1_ttm1(value) \
	((value << MMCR1_TTM1SEL_SHIFT) & MMCR1_TTM1SEL)
#define mmcr1_ttm3(value) \
	((value << MMCR1_TTM3SEL_SHIFT) & MMCR1_TTM3SEL)

#define mmcr1_dbg0(value) \
	((value << MMCR1_DBG0SEL_SHIFT) & MMCR1_DBG0SEL)
#define mmcr1_dbg1(value) \
	((value << MMCR1_DBG1SEL_SHIFT) & MMCR1_DBG1SEL)
#define mmcr1_dbg2(value) \
	((value << MMCR1_DBG2SEL_SHIFT) & MMCR1_DBG2SEL)
#define mmcr1_dbg3(value) \
	((value << MMCR1_DBG3SEL_SHIFT) & MMCR1_DBG3SEL)

#define mmcr1_event1_addr(value) \
	((value << MMCR1_PMC1ADDRSEL_SHIFT) & MMCR1_PMC1ADDRSEL)
#define mmcr1_event2_addr(value) \
	((value << MMCR1_PMC2ADDRSEL_SHIFT) & MMCR1_PMC2ADDRSEL)
#define mmcr1_event3_addr(value) \
	((value << MMCR1_PMC3ADDRSEL_SHIFT) & MMCR1_PMC3ADDRSEL)
#define mmcr1_event4_addr(value) \
	((value << MMCR1_PMC4ADDRSEL_SHIFT) & MMCR1_PMC4ADDRSEL)
#define mmcr1_event5_addr(value) \
	((value << MMCR1_PMC5ADDRSEL_SHIFT) & MMCR1_PMC5ADDRSEL)
#define mmcr1_event6_addr(value) \
	((value << MMCR1_PMC6ADDRSEL_SHIFT) & MMCR1_PMC6ADDRSEL)
#define mmcr1_event7_addr(value) \
	((value << MMCR1_PMC7ADDRSEL_SHIFT) & MMCR1_PMC7ADDRSEL)
#define mmcr1_event8_addr(value) \
	((value << MMCR1_PMC8ADDRSEL_SHIFT) & MMCR1_PMC8ADDRSEL)

#define mmcr1_spc(value) \
	((value << MMCR1_SPCSEL_SHIFT) & MMCR1_SPCSEL)

// No need to init. Will be done in power4_cpu_setup
#define MMCR0_INIT 0x0UL
#define MMCR1_INIT 0x0UL
#define MMCRA_INIT 0x0UL

#endif // RRPROFILE

static void power4_reg_setup(struct op_counter_config *ctr,
			     struct op_system_config *sys,
			     int num_ctrs)
{
	int i;
#ifdef RRPROFILE
	// *** ROTATERIGHT INTERNAL ***
	// Do not use mmcr0, mmcr1 and mmcra to set the performance counter
	// settings directly. Use event and unit_mask.
	//	/*
	//	 * The performance counter event settings are given in the mmcr0,
	//	 * mmcr1 and mmcra values passed from the user in the
	//	 * op_system_config structure (sys variable).
	//	 */

	unsigned long ctrEvent[8]  = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned long ctrAddrSel[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned long ttm0Value = 0;
	unsigned long ttm1Value = 0;
	unsigned long ttm3Value = 0;
	unsigned long dbg0Value = 0;
	unsigned long dbg1Value = 0;
	unsigned long dbg2Value = 0;
	unsigned long dbg3Value = 0;
	unsigned long spcValue  = 0;

#if 0
	/*
	 * SIHV / SIPR bits are only implemented on POWER4+ (GQ) and above.
	 * However we disable it on all POWER4 until we verify it works
	 * (I was seeing some strange behaviour last time I tried).
	 *
	 * It has been verified to work on POWER5 so we enable it there.
	 */
	if (cpu_has_feature(CPU_FTR_MMCRA_SIHV))
		mmcra_has_sihv = 1;
#endif

	// Initialize the event adder lane select
	for(i = 0; i < cur_cpu_spec->num_pmcs; i++) {
		ctrAddrSel[i] = (ctr[i].event >> 5) & 0x1UL;
	}

	// Initialize the events based on enable bit.
	for(i = 0; i < cur_cpu_spec->num_pmcs; i++) {
		ctrEvent[i] = ctr[i].enabled ? (ctr[i].event & 0x1FUL) : 0x08UL;
	}

	// Enumerate the counters and set the muxes.
	for(i = 0; i < cur_cpu_spec->num_pmcs; i++) {
		unsigned int unitSelect     = (ctr[i].unit_mask >> 12) & 0x0000000FUL;
		unsigned int unitValue      = (ctr[i].unit_mask >>  8) & 0x0000000FUL;
		unsigned int byteLaneSelect = (ctr[i].unit_mask >>  4) & 0x0000000FUL;
		unsigned int byteLaneValue  = (ctr[i].unit_mask >>  0) & 0x0000000FUL;

		switch(unitSelect) {
			// Selects ttm0
			case 1: {
				ttm0Value = unitValue;
				break;
			}
			// Selects ttm1
			case 2: {
				ttm1Value = unitValue;
				break;
			}
			// Selects ttm3
			case 3: {
				ttm3Value = unitValue;
				break;
			}
			// Selects spc
			case 4: {
				spcValue  = unitValue;
				break;
			}
			default: {
				// Do nothing.
				break;
			}
		}

		switch (byteLaneSelect) {
			// Selects dbg0
			case 1: {
				dbg0Value = byteLaneValue;
				break;
			}
			case 2: {
				dbg1Value = byteLaneValue;
				break;
			}
			case 3: {
				dbg2Value = byteLaneValue;
				break;
			}
			case 4: {
				dbg3Value = byteLaneValue;
				break;
			}
			default: {
				// Do nothing.
				break;
			}
		}
	}

	// Initialize the mmcr0, mmcr1 and mmcra registers.
	mmcr0_val = MMCR0_INIT |
			mmcr0_event1(ctrEvent[0]) | mmcr0_event2(ctrEvent[1]);
	mmcr1_val = MMCR1_INIT |
			mmcr1_event3(ctrEvent[2]) | mmcr1_event4(ctrEvent[3]) |
			mmcr1_event5(ctrEvent[4]) | mmcr1_event6(ctrEvent[5]) |
			mmcr1_event7(ctrEvent[6]) | mmcr1_event8(ctrEvent[7]) |
			mmcr1_event1_addr(ctrAddrSel[0]) | mmcr1_event2_addr(ctrAddrSel[1]) |
			mmcr1_event3_addr(ctrAddrSel[2]) | mmcr1_event4_addr(ctrAddrSel[3]) |
			mmcr1_event5_addr(ctrAddrSel[4]) | mmcr1_event6_addr(ctrAddrSel[5]) |
			mmcr1_event7_addr(ctrAddrSel[6]) | mmcr1_event8_addr(ctrAddrSel[7]) |
			mmcr1_ttm0(ttm0Value) | mmcr1_ttm1(ttm1Value) |
			mmcr1_ttm3(ttm3Value) | mmcr1_dbg0(dbg0Value) |
			mmcr1_dbg1(dbg1Value) | mmcr1_dbg2(dbg2Value) |
			mmcr1_dbg3(dbg3Value) | mmcr1_spc(spcValue);
	mmcra_val = MMCRA_INIT;

#else
	 * The performance counter event settings are given in the mmcr0,
	 * mmcr1 and mmcra values passed from the user in the
	 * op_system_config structure (sys variable).
	 */
	mmcr0_val = sys->mmcr0;
	mmcr1_val = sys->mmcr1;
	mmcra_val = sys->mmcra;
#endif // RRPROFILE

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i)
		reset_value[i] = 0x80000000UL - ctr[i].count;

	/* setup user and kernel profiling */
	if (sys->enable_kernel)
		mmcr0_val &= ~MMCR0_KERNEL_DISABLE;
	else
		mmcr0_val |= MMCR0_KERNEL_DISABLE;

	if (sys->enable_user)
		mmcr0_val &= ~MMCR0_PROBLEM_DISABLE;
	else
		mmcr0_val |= MMCR0_PROBLEM_DISABLE;
}

extern void ppc64_enable_pmcs(void);

/*
 * Older CPUs require the MMCRA sample bit to be always set, but newer
 * CPUs only want it set for some groups. Eventually we will remove all
 * knowledge of this bit in the kernel, oprofile userspace should be
 * setting it when required.
 *
 * In order to keep current installations working we force the bit for
 * those older CPUs. Once everyone has updated their oprofile userspace we
 * can remove this hack.
 */
static inline int mmcra_must_set_sample(void)
{
	if (__is_processor(PV_POWER4) || __is_processor(PV_POWER4p) ||
	    __is_processor(PV_970) || __is_processor(PV_970FX) ||
	    __is_processor(PV_970MP))
		return 1;

	return 0;
}

static void power4_cpu_setup(void *unused)
{
	unsigned int mmcr0 = mmcr0_val;
	unsigned long mmcra = mmcra_val;

	ppc64_enable_pmcs();

	/* set the freeze bit */
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	mmcr0 |= MMCR0_FCM1|MMCR0_PMXE|MMCR0_FCECE;
	mmcr0 |= MMCR0_PMC1CE|MMCR0_PMCjCE;
	mtspr(SPRN_MMCR0, mmcr0);

	mtspr(SPRN_MMCR1, mmcr1_val);

	if (mmcra_must_set_sample())
		mmcra |= MMCRA_SAMPLE_ENABLE;
	mtspr(SPRN_MMCRA, mmcra);

	dbg("setup on cpu %d, mmcr0 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR0));
	dbg("setup on cpu %d, mmcr1 %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCR1));
	dbg("setup on cpu %d, mmcra %lx\n", smp_processor_id(),
	    mfspr(SPRN_MMCRA));
}

static void power4_start(struct op_counter_config *ctr)
{
	int i;
	unsigned int mmcr0;
#ifdef RRPROFILE
	int cpu = smp_processor_id();
	start_timestamp[cpu] = oprofile_get_tb();
#endif // RRPROFILE

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
#ifdef RRPROFILE
		if (ctr[i].enabled && ctr[i].count != 0) {
#else
		if (ctr[i].enabled) {
#endif // RRPROFILE
			ctr_write(i, reset_value[i]);
		} else {
			ctr_write(i, 0);
		}
	}

	mmcr0 = mfspr(SPRN_MMCR0);

	/*
	 * We must clear the PMAO bit on some (GQ) chips. Just do it
	 * all the time
	 */
	mmcr0 &= ~MMCR0_PMAO;

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this excetion, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	oprofile_running = 1;

	dbg("start on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);
}

static void power4_stop(void)
{
	unsigned int mmcr0;

	/* freeze counters */
	mmcr0 = mfspr(SPRN_MMCR0);
	mmcr0 |= MMCR0_FC;
	mtspr(SPRN_MMCR0, mmcr0);

	oprofile_running = 0;

	dbg("stop on cpu %d, mmcr0 %x\n", smp_processor_id(), mmcr0);

	mb();
}

/* Fake functions used by canonicalize_pc */
static void __attribute_used__ hypervisor_bucket(void)
{
}

static void __attribute_used__ rtas_bucket(void)
{
}

static void __attribute_used__ kernel_unknown_bucket(void)
{
}

/*
 * On GQ and newer the MMCRA stores the HV and PR bits at the time
 * the SIAR was sampled. We use that to work out if the SIAR was sampled in
 * the hypervisor, our exception vectors or RTAS.
 */
static unsigned long get_pc(struct pt_regs *regs)
{
	unsigned long pc = mfspr(SPRN_SIAR);
	unsigned long mmcra;
#ifdef RRPROFILE
	unsigned long mmcra_sihv;
	unsigned long mmcra_sipr;
#endif // RRPROFILE

	/* Cant do much about it */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
	if (!cur_cpu_spec->oprofile_mmcra_sihv)
#else
	if (!mmcra_has_sihv)
#endif
		return pc;

	mmcra = mfspr(SPRN_MMCRA);

#ifdef RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
	mmcra_sihv = cur_cpu_spec->oprofile_mmcra_sihv;
#else
	mmcra_sihv = MMCRA_SIHV;
#endif
#endif // RRPROFILE

	/* Were we in the hypervisor? */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)
	if (platform_is_lpar() && (mmcra & mmcra_sihv))
#else
	if (firmware_has_feature(FW_FEATURE_LPAR) && (mmcra & mmcra_sihv))
#endif
		/* function descriptor madness */
		return *((unsigned long *)hypervisor_bucket);

#ifdef RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
	mmcra_sipr = cur_cpu_spec->oprofile_mmcra_sipr;
#else
	mmcra_sipr = MMCRA_SIPR;
#endif
#endif // RRPROFILE

	/* We were in userspace, nothing to do */
#ifdef RRPROFILE
	if (mmcra & cur_cpu_spec->oprofile_mmcra_sipr)
#else
	if (mmcra & mmcra_sipr)
#endif // RRPROFILE
		return pc;

#ifdef CONFIG_PPC_RTAS
	/* Were we in RTAS? */
	if (pc >= rtas.base && pc < (rtas.base + rtas.size))
		/* function descriptor madness */
		return *((unsigned long *)rtas_bucket);
#endif

	/* Were we in our exception vectors or SLB real mode miss handler? */
	if (pc < 0x1000000UL)
		return (unsigned long)__va(pc);

	/* Not sure where we were */
	if (!is_kernel_addr(pc))
		/* function descriptor madness */
		return *((unsigned long *)kernel_unknown_bucket);

	return pc;
}

static int get_kernel(unsigned long pc, unsigned long mmcra)
{
	int is_kernel;
#ifdef RRPROFILE
	unsigned long mmcra_sipr;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
	mmcra_sipr = cur_cpu_spec->oprofile_mmcra_sipr;
#else
	mmcra_sipr = MMCRA_SIPR;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
	if (!cur_cpu_spec->oprofile_mmcra_sihv) {
#else
	if (!mmcra_has_sihv) {
#endif
		is_kernel = is_kernel_addr(pc);
	} else {
		unsigned long mmcra = mfspr(SPRN_MMCRA);
		is_kernel = ((mmcra & mmcra_sipr) == 0);
	}
#else
	if (!cur_cpu_spec->oprofile_mmcra_sihv) {
		is_kernel = is_kernel_addr(pc);
	} else {
		is_kernel = ((mmcra & cur_cpu_spec->oprofile_mmcra_sipr) == 0);
	}
#endif // RRPROFILE

	return is_kernel;
}

static void power4_handle_interrupt(struct pt_regs *regs,
				    struct op_counter_config *ctr)
{
	unsigned long pc;
	int is_kernel;
	int val;
	int i;
	unsigned int mmcr0;
#ifdef RRPROFILE
	int cpu = smp_processor_id();
	uint64_t end_timestamp = oprofile_get_tb();
#endif
	unsigned long mmcra;

	mmcra = mfspr(SPRN_MMCRA);

	pc = get_pc(regs);
	is_kernel = get_kernel(pc, mmcra);

	/* set the PMM bit (see comment below) */
	mtmsrd(mfmsr() | MSR_PMM);

#ifdef RRPROFILE
	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
		val = ctr_read(i);
		if (oprofile_running && ctr[i].enabled) {
			if(ctr[i].count == 0) {
				// TODO: Add support for counter mode.
				/* counter is in counter mode */
				// Convert val to unsigned long
				// add to cpu buffer
				// reset counter to 0.
				ctr_write(i, 0);
			} else if (val < 0) {
				/* counter is in trigger mode */
				oprofile_add_sample_start(start_timestamp[cpu]);
				oprofile_add_sample_stop(end_timestamp);
				oprofile_add_ext_sample(pc, regs, i, is_kernel);
				ctr_write(i, reset_value[i]);
			}
		} else if (val < 0) {
			ctr_write(i, 0);
		}
	}
#else
	for (i = 0; i < cur_cpu_spec->num_pmcs; ++i) {
		val = ctr_read(i);
		if (val < 0) {
			if (oprofile_running && ctr[i].enabled) {
				oprofile_add_ext_sample(pc, regs, i, is_kernel);
				ctr_write(i, reset_value[i]);
			} else {
				ctr_write(i, 0);
			}
		}
	}
#endif

	mmcr0 = mfspr(SPRN_MMCR0);

	/* reset the perfmon trigger */
	mmcr0 |= MMCR0_PMXE;

	/*
	 * We must clear the PMAO bit on some (GQ) chips. Just do it
	 * all the time
	 */
	mmcr0 &= ~MMCR0_PMAO;

#ifndef RRPROFILE
	/* Clear the appropriate bits in the MMCRA */
	mmcra &= ~cur_cpu_spec->oprofile_mmcra_clear;
	mtspr(SPRN_MMCRA, mmcra);
#endif // !RRPROFILE

	/*
	 * now clear the freeze bit, counting will not start until we
	 * rfid from this exception, because only at that point will
	 * the PMM bit be cleared
	 */
	mmcr0 &= ~MMCR0_FC;
#ifdef RRPROFILE
	start_timestamp[cpu] = oprofile_get_tb();
#endif // RRPROFILE
	mtspr(SPRN_MMCR0, mmcr0);
}

struct op_powerpc_model op_model_power4 = {
	.reg_setup		= power4_reg_setup,
	.cpu_setup		= power4_cpu_setup,
	.start			= power4_start,
	.stop			= power4_stop,
	.handle_interrupt	= power4_handle_interrupt,
};
