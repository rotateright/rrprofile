/**
 * @file nmi_int.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#ifdef RRPROFILE
#include "../oprofile.h"
#include <linux/version.h>
#include <linux/fs.h>
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#include <linux/sysdev.h>
#endif
#include <linux/slab.h>
#include <linux/moduleparam.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
#include <linux/syscore_ops.h>
#endif

#include <asm/nmi.h>
#include <asm/msr.h>
#include <asm/apic.h>

#ifdef RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
#include <linux/kdebug.h>
#else
#include <asm/kdebug.h>
#endif

int rr_cpu_has_arch_perfmon = 0;

#endif // RRPROFILE

#include "op_counter.h"
#include "op_x86_model.h"

static struct op_x86_model_spec const * model;
static struct op_msrs cpu_msrs[NR_CPUS];
static unsigned long saved_lvtpc[NR_CPUS];
 
static int ctr_running;

static int nmi_start(void);
static void nmi_stop(void);

/* 0 == registered but off, 1 == registered and on */
static int nmi_enabled = 0;

#ifdef CONFIG_PM

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
static int nmi_suspend(void)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
static int nmi_suspend(struct sys_device *dev, pm_message_t state)
#else
static int nmi_suspend(struct sys_device *dev, u32 state)
#endif
{
	if (nmi_enabled == 1)
		nmi_stop();
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
static void nmi_resume(void)
{
	if (nmi_enabled == 1)
		nmi_start();  
}
#else
static int nmi_resume(struct sys_device *dev)
{
	if (nmi_enabled == 1)
		nmi_start();
	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)

static struct syscore_ops oprofile_syscore_ops = {
	.resume		= nmi_resume,
	.suspend	= nmi_suspend,
};

#else // >= 2.6.39

static struct sysdev_class oprofile_sysclass = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	.name		= "oprofile",
#else
	set_kset_name("oprofile"),
#endif
	.resume		= nmi_resume,
	.suspend	= nmi_suspend,
};


static struct sys_device device_oprofile = {
	.id	= 0,
	.cls	= &oprofile_sysclass,
};

#endif // >= 2.6.39

static int __init init_driverfs(void)
{
	int error;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
	register_syscore_ops(&oprofile_syscore_ops);
	error = 0;
#else
	if (!(error = sysdev_class_register(&oprofile_sysclass)))
		error = sysdev_register(&device_oprofile);
#endif // >= 2.6.39
	return error;
}

static void exit_driverfs(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
	unregister_syscore_ops(&oprofile_syscore_ops);
#else
	sysdev_unregister(&device_oprofile);
	sysdev_class_unregister(&oprofile_sysclass);
#endif // >= 2.6.39
}

#else
#define init_driverfs() do { } while (0)
#define exit_driverfs() do { } while (0)
#endif /* CONFIG_PM */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
static int profile_exceptions_notify(unsigned int val, struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	if (ctr_running)
		model->check_ctrs(regs, &cpu_msrs[cpu]);
	else if (!nmi_enabled)
		return NMI_DONE;
	else
		model->stop(&cpu_msrs[cpu]);
	return NMI_HANDLED;
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
static int profile_exceptions_notify(struct notifier_block *self,
					unsigned long val, void *data)
{
	struct die_args *args = (struct die_args *)data;
	int ret = NOTIFY_DONE;
	int cpu = smp_processor_id();

	switch(val) {
	case DIE_NMI:
		if (model->check_ctrs(args->regs, &cpu_msrs[cpu]))
			ret = NOTIFY_STOP;
		break;
	default:
		break;
	}
	return ret;
}
#else
static int nmi_callback(struct pt_regs * regs, int cpu)
{
	return model->check_ctrs(regs, &cpu_msrs[cpu]);
}
#endif
 
 
static void nmi_cpu_save_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr * counters = msrs->counters;
	struct op_msr * controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < nr_ctrs; ++i) {
		if (counters[i].addr) {
			rdmsr(counters[i].addr,
				counters[i].saved.low,
				counters[i].saved.high);
		}
	}
 
	for (i = 0; i < nr_ctrls; ++i) {
		if (controls[i].addr) {
			rdmsr(controls[i].addr,
				controls[i].saved.low,
				controls[i].saved.high);
		}
	}
}


static void nmi_save_registers(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	model->fill_in_addresses(msrs);
	nmi_cpu_save_registers(msrs);
}


static void free_msrs(void)
{
	int i;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	for_each_possible_cpu(i) {
#else
	for_each_online_cpu(i) {
#endif
		kfree(cpu_msrs[i].counters);
		cpu_msrs[i].counters = NULL;
		kfree(cpu_msrs[i].controls);
		cpu_msrs[i].controls = NULL;
	}
}


static int allocate_msrs(void)
{
	int success = 1;
	size_t controls_size = sizeof(struct op_msr) * model->num_controls;
	size_t counters_size = sizeof(struct op_msr) * model->num_counters;

	int i;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	for_each_possible_cpu(i) {
#else
	for_each_online_cpu(i) {
#endif
		cpu_msrs[i].counters = kmalloc(counters_size, GFP_KERNEL);
		if (!cpu_msrs[i].counters) {
			success = 0;
			break;
		}
		cpu_msrs[i].controls = kmalloc(controls_size, GFP_KERNEL);
		if (!cpu_msrs[i].controls) {
			success = 0;
			break;
		}
	}

	if (!success)
		free_msrs();

	return success;
}

static void nmi_cpu_setup(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	spin_lock(&oprofilefs_lock);
	model->setup_ctrs(msrs);
	spin_unlock(&oprofilefs_lock);
	saved_lvtpc[cpu] = apic_read(APIC_LVTPC);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19) && LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)
static struct notifier_block profile_exceptions_nb = {
	.notifier_call = profile_exceptions_notify,
	.next = NULL,
	.priority = 0
};
#endif

static int nmi_setup(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
	int err=0;
#endif

	if (!allocate_msrs())
		return -ENOMEM;

	nmi_enabled = 0;
	ctr_running = 0;
	/* make variables visible to the nmi handler: */
	smp_mb();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	if ((err = register_nmi_handler(NMI_LOCAL, profile_exceptions_notify, 0, "rrprofile"))){
		free_msrs();
		return err;
	}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
	if ((err = register_die_notifier(&profile_exceptions_nb))){
		free_msrs();
		return err;
	}
#else
	/* We walk a thin line between law and rape here.
	 * We need to be careful to install our NMI handler
	 * without actually triggering any NMIs as this will
	 * break the core code horrifically.
	 */
	if (reserve_lapic_nmi() < 0) {
		free_msrs();
		return -EBUSY;
	}
#endif
	/* We need to serialize save and setup for HT because the subset
	 * of msrs are distinct for save and setup operations
	 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(nmi_save_registers, NULL, 1);
	on_each_cpu(nmi_cpu_setup, NULL, 1);
#else
	on_each_cpu(nmi_save_registers, NULL, 0, 1);
	on_each_cpu(nmi_cpu_setup, NULL, 0, 1);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
	set_nmi_callback(nmi_callback);
#endif
	nmi_enabled = 1;
	return 0;
}


static void nmi_restore_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr * counters = msrs->counters;
	struct op_msr * controls = msrs->controls;
	unsigned int i;

	for (i = 0; i < nr_ctrls; ++i) {
		if (controls[i].addr){
			wrmsr(controls[i].addr,
				controls[i].saved.low,
				controls[i].saved.high);
		}
	}
 
	for (i = 0; i < nr_ctrs; ++i) {
		if (counters[i].addr){
			wrmsr(counters[i].addr,
				counters[i].saved.low,
				counters[i].saved.high);
		}
	}
}
 

static void nmi_cpu_shutdown(void * dummy)
{
	unsigned int v;
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
 
	/* restoring APIC_LVTPC can trigger an apic error because the delivery
	 * mode and vector nr combination can be illegal. That's by design: on
	 * power on apic lvt contain a zero vector nr which are legal only for
	 * NMI delivery mode. So inhibit apic err before restoring lvtpc
	 */
	v = apic_read(APIC_LVTERR);
	apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	apic_write(APIC_LVTPC, saved_lvtpc[cpu]);
	apic_write(APIC_LVTERR, v);

#ifdef RRPROFILE
	if(model->shutdown) {
		model->shutdown(msrs);
	}
#endif // RRPROFILE

	nmi_restore_registers(msrs);
}

 
static void nmi_shutdown(void)
{
#ifdef RRPROFILE
	disable_poll_idle();
#endif // RRPROFILE
	nmi_enabled = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(nmi_cpu_shutdown, NULL, 1);
#else
	on_each_cpu(nmi_cpu_shutdown, NULL, 0, 1);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	unregister_nmi_handler(NMI_LOCAL, "rrprofile");
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,19)
	unregister_die_notifier(&profile_exceptions_nb);
#else
	unset_nmi_callback();
	release_lapic_nmi();
#endif
	free_msrs();
}

 
static void nmi_cpu_start(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
#ifdef RRPROFILE
	oprofile_add_start(NULL);
#endif // RRPROFILE
	model->start(msrs);
}
 

static int nmi_start(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	get_online_cpus();
#endif

	ctr_running = 1;
	/* make ctr_running visible to the nmi handler: */
	smp_mb();

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(nmi_cpu_start, NULL, 1);
#else
	on_each_cpu(nmi_cpu_start, NULL, 0, 1);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	put_online_cpus();
#endif

#ifdef RRPROFILE
	if(poll_idle_enabled) {
		enable_poll_idle();
	}
#endif // RRPROFILE

	return 0;
}
 
 
static void nmi_cpu_stop(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
	model->stop(msrs);
#ifdef RRPROFILE
	oprofile_add_stop(NULL);

	disable_poll_idle();
#endif // RRPROFILE
}
 
 
static void nmi_stop(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	get_online_cpus();
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(nmi_cpu_stop, NULL, 1);
#else
	on_each_cpu(nmi_cpu_stop, NULL, 0, 1);
#endif

	ctr_running = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	put_online_cpus();
#endif
}


struct op_counter_config counter_config[OP_MAX_COUNTER];

#ifdef RRPROFILE
static ssize_t idle_poll_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	return oprofilefs_ulong_to_user(poll_idle_enabled, buf, count, offset);
}


static ssize_t idle_poll_write(struct file * file, char const __user * buf, size_t count, loff_t * offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	poll_idle_enabled = val;

	return count;
}


static struct file_operations idle_poll_fops = {
	.read		= idle_poll_read,
	.write		= idle_poll_write
};
#endif // RRPROFILE

static int nmi_create_files(struct super_block * sb, struct dentry * root)
{
	unsigned int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry * dir;
#ifdef RRPROFILE
		char buf[7];
 
		snprintf(buf,  sizeof(buf), "pmc%d", i);
#else
		char buf[4];
 
		snprintf(buf,  sizeof(buf), "%d", i);
#endif // RRPROFILE
		dir = oprofilefs_mkdir(sb, root, buf);
		oprofilefs_create_ulong(sb, dir, "enabled", &counter_config[i].enabled); 
		oprofilefs_create_ulong(sb, dir, "event", &counter_config[i].event); 
		oprofilefs_create_ulong(sb, dir, "count", &counter_config[i].count); 
		oprofilefs_create_ulong(sb, dir, "unit_mask", &counter_config[i].unit_mask); 
		oprofilefs_create_ulong(sb, dir, "kernel", &counter_config[i].kernel); 
		oprofilefs_create_ulong(sb, dir, "user", &counter_config[i].user); 
	}
	
#ifdef RRPROFILE
	/* Control polling of idle threads. */
	oprofilefs_create_file(sb, root, "idle_poll", &idle_poll_fops);
#endif // RRPROFILE

	return 0;
}

#ifdef RRPROFILE
static void nmi_cpu_adapt(void *dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	spin_lock(&oprofilefs_lock);
	model->setup_ctrs(msrs);
	spin_unlock(&oprofilefs_lock);
}


static int nmi_adapt(void)
{
	int maxCounterValue = 0x7FFFFFFF / ADAPT_DECAY_FACTOR;
	int i;

	// Check if all enabled counters can be decayed.
	for (i = 0; i < model->num_counters; ++i) {
		if(counter_config[i].enabled && counter_config[i].count >= maxCounterValue) {
			return 0;
		}
	}

	// Apply the decay factor to counter_config
	for (i = 0; i < model->num_counters; ++i) {
		if(counter_config[i].enabled) {
			counter_config[i].count *= ADAPT_DECAY_FACTOR;
		}
	}

	// Apply the counter_config to the physical registers
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	on_each_cpu(nmi_cpu_adapt, NULL, 1);
#else
	on_each_cpu(nmi_cpu_adapt, NULL, 0, 1);
#endif

	return 1;
}
#endif // RRPROFILE


static int p4force;
module_param(p4force, int, 0);
 
static int __init p4_init(char ** cpu_type)
{
	__u8 cpu_model = boot_cpu_data.x86_model;

	if (!p4force && (cpu_model > 6 || cpu_model == 5))
		return 0;

#ifndef CONFIG_SMP
	*cpu_type = "i386/p4";
	model = &op_p4_spec;
	return 1;
#else
	switch (smp_num_siblings) {
		case 1:
			*cpu_type = "i386/p4";
			model = &op_p4_spec;
			return 1;

		case 2:
			*cpu_type = "i386/p4-ht";
			model = &op_p4_ht2_spec;
			return 1;
	}
#endif

	printk(KERN_INFO "rrprofile: P4 HyperThreading detected with > 2 threads\n");
	printk(KERN_INFO "rrprofile: Reverting to timer mode.\n");
	return 0;
}

#ifdef RRPROFILE
static int force_arch_perfmon;
static int force_cpu_type(const char *str, struct kernel_param *kp)
{
	if (!strcmp(str, "arch_perfmon")) {
		force_arch_perfmon = 1;
		printk(KERN_INFO "rrprofile: forcing architectural perfmon\n");
	}

	return 0;
}
module_param_call(cpu_type, force_cpu_type, NULL, NULL, 0);
#endif // RRPROFILE

#ifdef RRPROFILE
static int __init ppro_init(char ** cpu_type)
{
	unsigned eax = cpuid_eax(1);
	__u8 cpu_model = (eax >> 4) & 0xF;
	__u8 ext_cpu_model = (eax >> 16) & 0xF;
	struct op_x86_model_spec const *spec = &op_ppro_spec; /* default */

	if (force_arch_perfmon && rr_cpu_has_arch_perfmon)
		return 0;

	cpu_model += ext_cpu_model << 4;
	switch(cpu_model) {
	case 0: // 0x0 - Intel P6 Pentium Pro (A-step)
	case 1: // 0x1 - Intel P6 Pentium Pro
	case 2: // 0x2 - ??
		*cpu_type = "i386/ppro";
		break;
	case 3: // 0x3 - Intel PII/PII Overdrive - Klamath
	case 4: // 0x4 - Intel PII - Deschutes?
	case 5: // 0x5 - Intel PII - Deschutes, Tonga/Xeon/Celeron model 5 - Covington
		*cpu_type = "i386/pii";
		break;
	case 6: // 0x6 - Intel PII - Dixon/Celeron model 6 - Mendocino
	case 7: // 0x7 - Intel PIII - Katmai/Xeon model 7 - Tanner
	case 8: // 0x8 - Intel PIII - Coppermine, Geyserville/Xeon/Celeron model 8 - Coppermine
		*cpu_type = "i386/piii";
		break;
	case 9: // 0x9 - Intel Pentium M model 9 - Banias
		*cpu_type = "i386/p6_mobile";
		break;
	case 10: // 0xa - Intel PIII Xeon model A - Cascades
	case 11: // 0xb - Intel PIII model B/PIII-S/Celeron - Tualatin
	case 12: // 0xc - ??
	case 13: // 0xd - Intel Pentium M model D - Dothan
	case 21: // 0x15 - Intel CE3100 - Canmore (Pentium M SoC)
		*cpu_type = "i386/p6";
		break;
	/* Intel Core */
	case 14: // 0xe - Intel Core - Yonah
		*cpu_type = "i386/core";
		break;
	/* Intel Core 2 */
	case 15: //  0xf - Intel Core 2 - Merom / Conroe
	case 22: // 0x16 - Intel Core 2 - Merom-L / Conroe-L - SC (65 nm) with 1 MB on-die L2
	case 23: // 0x17 - Intel Core 2 - Penryn / Wolfdale
	case 29: // 0x1d - Intel Xeon Processor MP 7400 series - Dunnington
		*cpu_type = "i386/core_2";
		break;
	/* Intel Atom */
	case 28: // 0x1c - Intel Atom - Silverthrone / Diamondville
	case 38: // 0x26 - Intel Atom - Lincroft (SoC)
	case 39: // 0x27 - Intel Atom - Saltwell
	case 54: // 0x36 - Intel Atom - Cedarview
		*cpu_type = "i386/atom";
		break;
	/* Intel Nehalem */
	case 26: // 0x1a - Intel Nehalem - Bloomfield / Gainestown
	case 30: // 0x1e - Intel Nehalem - Lynnfield / Clarksfield / Jasper Forest
	case 31: // 0x1f - Intel Nehalem - Havendale
	case 46: // 0x2e - Intel Nehalem-EX Xeon - Beckton
		spec = &op_arch_perfmon_spec;
		*cpu_type = "i386/nehalem";
		break;
	/* Intel Westmere */
	case 37: // 0x25 - Intel Westmere (32nm Nehalem) - Clarkdale / Arrandale
	case 44: // 0x2c - Intel Westmere-EP (32nm Nehalem) - Gulftown
	case 47: // 0x2f - Intel Westmere-EX Xeon - Eagleton
		spec = &op_arch_perfmon_spec;
		*cpu_type = "i386/westmere";
		break;
	case 42: // 0x2a - Intel Sandy Bridge
	case 45: // 0x2d - Intel Sandy Bridge Xeon
	case 58: // 0x3a - Intel Ivy Bridge
	case 60: // 0x3c - Intel Haswell Desktop
	case 62: // 0x3e - Intel Ivy Bridge-EP Xeon - Ivytown 
	case 63: // 0x3f - Intel Haswell-E Xeon - Grantley 
	case 69: // 0x45 - Intel Haswell ULT (MacBook Air)
	case 70: // 0x46 - Intel Haswell Mobile
	case 71: // 0x47 - Intel Haswell less mobile?
		spec = &op_arch_perfmon_spec;
		*cpu_type = "i386/westmere"; // XXX no Sandy Bridge or Ivy Bridge or Haswell event lists (yet?)
		break;
	default:
		return 0;
	}

	model = spec;
	return 1;
}
#else
static int __init ppro_init(char ** cpu_type)
{
	__u8 cpu_model = boot_cpu_data.x86_model;

	if (cpu_model == 14)
		*cpu_type = "i386/core";
	else if (cpu_model == 15)
		*cpu_type = "i386/core_2";
	else if (cpu_model > 0xd)
		return 0;
	else if (cpu_model == 9) {
		*cpu_type = "i386/p6_mobile";
	} else if (cpu_model > 5) {
		*cpu_type = "i386/piii";
	} else if (cpu_model > 2) {
		*cpu_type = "i386/pii";
	} else {
		*cpu_type = "i386/ppro";
	}

	model = &op_ppro_spec;
	return 1;
}
#endif // RRPROFILE

/* in order to get driverfs right */
static int using_nmi;

int __init op_nmi_init(struct oprofile_operations *ops)
{
	__u8 vendor = boot_cpu_data.x86_vendor;
	__u8 family = boot_cpu_data.x86;
	char *cpu_type;
	
#ifdef RRPROFILE
	int ret = 0;

	cpu_type = NULL; // fix compiler warning

	if(boot_cpu_data.cpuid_level > 9) {
		unsigned eax = cpuid_eax(10);
		/* Check for version and the number of counters */
		if ((eax & 0xff) && (((eax>>8) & 0xff) > 1))
			rr_cpu_has_arch_perfmon = 1;
	}
#endif // RRPROFILE

	if (!cpu_has_apic)
		return -ENODEV;
 
	switch (vendor) {
		case X86_VENDOR_AMD:
			/* Needs to be at least an Athlon (or hammer in 32bit mode) */

			switch (family) {
			default:
				return -ENODEV;
			case 6:
				model = &op_athlon_spec;
				cpu_type = "i386/athlon";
				break;
			case 0xf:
				model = &op_athlon_spec;
 #ifdef RRPROFILE
				// show user space a consistent name
				cpu_type = "x86_64/athlon64";
#else
				cpu_type = "x86-64/hammer";
#endif // RRPROFILE
				break;
#ifdef RRPROFILE
			case 0x10:
				model = &op_athlon_spec;
				cpu_type = "x86-64/family10";
				break;
			case 0x11:
				model = &op_athlon_spec;
				cpu_type = "x86-64/family11h";
				break;
			case 0x12:
				model = &op_athlon_spec;
				cpu_type = "x86-64/family12h";
				break;
			case 0x14:
				model = &op_athlon_spec;
				cpu_type = "x86-64/family14h";
				break;
			case 0x15:
				model = &op_athlon_spec;
				cpu_type = "x86-64/family15h";
				break;
#endif // RPROFILE
			}
			break;
 
		case X86_VENDOR_INTEL:
			switch (family) {
				/* Pentium IV */
				case 0xf:
#ifdef RRPROFILE
					p4_init(&cpu_type); // fall back to arch perfmon on failure
#else
					if (!p4_init(&cpu_type))
						return -ENODEV;
#endif // RRPROFILE
					break;

				/* A P6-class processor */
				case 6:
#ifdef RRPROFILE
					ppro_init(&cpu_type); // fall back to arch perfmon on failure
#else
					if (!ppro_init(&cpu_type))
						return -ENODEV;
#endif
					break;

				default:
#ifdef RRPROFILE
					break; // fall back to arch perfmon on failure
#else
					return -ENODEV;
#endif // RRPROFILE
			}

#ifdef RRPROFILE
			if (cpu_type)
				break;

			if (!rr_cpu_has_arch_perfmon)
				return -ENODEV;

			/* use arch perfmon as fallback */
			cpu_type = "i386/arch_perfmon";
			model = &op_arch_perfmon_spec;
#endif // RRPROFILE
			break;

		default:
			return -ENODEV;
	}

#ifdef RRPROFILE
	/* default values, can be overwritten by model */
#else
	init_driverfs();
	using_nmi = 1;
#endif // RRPROFILE
	ops->create_files 	= nmi_create_files;
	ops->setup 			= nmi_setup;
	ops->shutdown 		= nmi_shutdown;
	ops->start 			= nmi_start;
	ops->stop 			= nmi_stop;
#ifdef RRPROFILE
	ops->adapt			= nmi_adapt;
#endif // RRPROFILE
	ops->cpu_type 		= cpu_type;

#ifdef RRPROFILE
	if (model->init)
		ret = model->init(ops);
	if (ret)
		return ret;

	/* num_counters is finalized after init() is called */
	ops->num_counters 	= model->num_counters;

	init_driverfs();
	using_nmi = 1;
#endif // RRPROFILE

	printk(KERN_INFO "rrprofile: using NMI interrupt.\n");

	return 0;
}


void op_nmi_exit(void)
{
#ifdef RRPROFILE
	if (using_nmi) {
		disable_poll_idle();
		exit_driverfs();
	}

	if (model && model->exit)
		model->exit();
#else
	if (using_nmi)
		exit_driverfs();
#endif // RRPROFILE
}
