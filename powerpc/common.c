/*
 * PPC 64 oprofile support:
 * Copyright (C) 2004 Anton Blanchard <anton@au.ibm.com>, IBM
 * PPC 32 oprofile support: (based on PPC 64 support)
 * Copyright (C) Freescale Semiconductor, Inc 2004
 *	Author: Andy Fleming
 *
 * Based on alpha version.
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
#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pmc.h>
#include <asm/cputable.h>
#ifdef RRPROFILE
#include <linux/fs.h>
#include <asm/prom.h>
#include "oprofile_impl.h"
#else
#include <asm/oprofile_impl.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,17)
#include <asm/firmware.h>
#endif
#endif // RRPROFILE

static struct op_powerpc_model *model;

static struct op_counter_config ctr[OP_MAX_COUNTER];
static struct op_system_config sys;

#ifdef RRPROFILE
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16)
static char *cpu_type;
#endif

unsigned int cpu_khz = 0;
#endif // RRPROFILE

static void op_handle_interrupt(struct pt_regs *regs)
{
	model->handle_interrupt(regs, ctr);
}

static int op_powerpc_setup(void)
{
	int err;

	/* Grab the hardware */
	err = reserve_pmc_hardware(op_handle_interrupt);
	if (err)
		return err;

	/* Pre-compute the values to stuff in the hardware registers.  */
	model->reg_setup(ctr, &sys, model->num_counters);

	/* Configure the registers on all cpus.  */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(model->cpu_setup, NULL, 1);
#else
	on_each_cpu(model->cpu_setup, NULL, 0, 1);
#endif

	return 0;
}

static void op_powerpc_shutdown(void)
{
	release_pmc_hardware();
}

static void op_powerpc_cpu_start(void *dummy)
{
#ifdef RRPROFILE
	// record the timestamps before start
	oprofile_add_start(NULL);
#endif // RRPROFILE

	model->start(ctr);
}

static int op_powerpc_start(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(op_powerpc_cpu_start, NULL, 1);
#else
	on_each_cpu(op_powerpc_cpu_start, NULL, 0, 1);
#endif
	return 0;
}

static inline void op_powerpc_cpu_stop(void *dummy)
{
	model->stop();

#ifdef RRPROFILE
	// record the timestamp after stop
	oprofile_add_stop(NULL);
#endif // RRPROFILE
}

static void op_powerpc_stop(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(op_powerpc_cpu_stop, NULL, 1);
#else
	on_each_cpu(op_powerpc_cpu_stop, NULL, 0, 1);
#endif
}

static int op_powerpc_create_files(struct super_block *sb, struct dentry *root)
{
	int i;

#ifdef CONFIG_PPC64
	/*
	 * There is one mmcr0, mmcr1 and mmcra for setting the events for
	 * all of the counters.
	 */
	oprofilefs_create_ulong(sb, root, "mmcr0", &sys.mmcr0);
	oprofilefs_create_ulong(sb, root, "mmcr1", &sys.mmcr1);
	oprofilefs_create_ulong(sb, root, "mmcra", &sys.mmcra);
#endif

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry *dir;
#ifdef RRPROFILE
		char buf[6];

		snprintf(buf, sizeof buf, "pmc%d", i);
#else
		char buf[4];

		snprintf(buf, sizeof buf, "%d", i);
#endif // RRPROFILE
		dir = oprofilefs_mkdir(sb, root, buf);

		oprofilefs_create_ulong(sb, dir, "enabled", &ctr[i].enabled);
		oprofilefs_create_ulong(sb, dir, "event", &ctr[i].event);
		oprofilefs_create_ulong(sb, dir, "count", &ctr[i].count);

		/*
		 * Classic PowerPC doesn't support per-counter
		 * control like this, but the options are
		 * expected, so they remain.  For Freescale
		 * Book-E style performance monitors, we do
		 * support them.
		 */
		oprofilefs_create_ulong(sb, dir, "kernel", &ctr[i].kernel);
		oprofilefs_create_ulong(sb, dir, "user", &ctr[i].user);

		oprofilefs_create_ulong(sb, dir, "unit_mask", &ctr[i].unit_mask);
	}

#ifdef RRPROFILE
	/* Always accept the default to trace both kernel and user level. */
	/* Let user space application do the filtering. */
#else
	oprofilefs_create_ulong(sb, root, "enable_kernel", &sys.enable_kernel);
	oprofilefs_create_ulong(sb, root, "enable_user", &sys.enable_user);
#endif // RRPROFILE

	/* Default to tracing both kernel and user */
	sys.enable_kernel = 1;
	sys.enable_user = 1;

	return 0;
}

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	struct device_node *cpu;
	unsigned int *fp;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
	if (!cur_cpu_spec->oprofile_cpu_type)
		return -ENODEV;

#ifndef RRPROFILE
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return -ENODEV;
#endif // !RRPROFILE

	switch (cur_cpu_spec->oprofile_type) {
#ifdef CONFIG_PPC64
		case PPC_OPROFILE_RS64:
			model = &op_model_rs64;
			break;
		case PPC_OPROFILE_POWER4:
			model = &op_model_power4;
			break;
#else
		case PPC_OPROFILE_G4:
			model = &op_model_7450;
			break;
#endif
#ifdef CONFIG_FSL_BOOKE
		case PPC_OPROFILE_BOOKE:
			model = &op_model_fsl_booke;
			break;
#endif
		default:
			return -ENODEV;
	}

	ops->cpu_type = cur_cpu_spec->oprofile_cpu_type;
#else
	/*
	 * Only support PPC970 for now.
	 */
	if (strcmp(cur_cpu_spec->cpu_name, "PPC970") != 0) {
		return -ENODEV;
	}
	model = &op_model_power4;
	cpu_type = kmalloc(32, GFP_KERNEL);
	if (NULL == cpu_type)
		return -ENOMEM;

	sprintf(cpu_type, "ppc64/970");
	ops->cpu_type = cpu_type;
#endif
	model->num_counters = cur_cpu_spec->num_pmcs;

	ops->num_counters = cur_cpu_spec->num_pmcs;
	ops->create_files = op_powerpc_create_files;
	ops->setup = op_powerpc_setup;
	ops->shutdown = op_powerpc_shutdown;
	ops->start = op_powerpc_start;
	ops->stop = op_powerpc_stop;
	ops->backtrace = op_powerpc_backtrace;

	printk(KERN_INFO "rrprofile: using %s performance monitoring.\n",
	       ops->cpu_type);

#ifdef RRPROFILE
	/* calculate cpu_khz */
	cpu = of_find_node_by_type(NULL, "cpu");
	printk(KERN_INFO "cpu: %lx\n", (unsigned long) cpu);
	if (cpu) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
		fp = (unsigned int *)of_get_property(cpu, "clock-frequency", NULL);
#else
		fp = (unsigned int *)get_property(cpu, "clock-frequency", NULL);
#endif
		printk(KERN_INFO "fp: %lx\n", (unsigned long) fp);
		if (fp) {
			cpu_khz = (*fp)/1000;
			printk(KERN_INFO "cpu_khz: %u\n", cpu_khz);
		}
	}
#endif // RRPROFILE

	return 0;
}

void oprofile_arch_exit(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16)
	kfree(cpu_type);
	cpu_type = NULL;
#endif
}

#ifdef RRPROFILE
uint64_t oprofile_get_tb()
{
	unsigned int hi, lo, hic;
	uint64_t tb;

	do {
		asm volatile("  mftbu %0" : "=r" (hi));
		asm volatile("  mftb %0" : "=r" (lo));
		asm volatile("  mftbu %0" : "=r" (hic));
	} while (hic != hi);

	tb = ((uint64_t)hi)<<32 | ((uint64_t)lo);

	return tb;
}
#endif // RRPROFILE

