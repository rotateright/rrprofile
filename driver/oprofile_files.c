/**
 * @file oprofile_files.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include "../oprofile.h"

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cpumask.h>
#include <linux/cpufreq.h>
#include <asm/param.h>

#include "event_buffer.h"
#include "oprofile_stats.h"
#include "oprof.h"

#define BUFFER_SIZE_DEFAULT		131072
#define CPU_BUFFER_SIZE_DEFAULT		8192
#define BUFFER_WATERSHED_DEFAULT	32768	/* FIXME: tune */
#define TIME_SLICE_DEFAULT		1
unsigned long oprofile_buffer_size;
unsigned long oprofile_cpu_buffer_size;
unsigned long oprofile_buffer_watershed;
unsigned long oprofile_time_slice;

#ifdef RRPROFILE
/* oprofile_cpu_buffer_size is defined in units of (struct op_sample). */
/* oprofile_buffer_size and oprofile_buffer_watershed are defined in units of (unsigned long). */

char          oprofile_cpu_type[80] = "null";
unsigned int  oprofile_num_counters = 0;

#if !defined(CONFIG_X86_64) && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
extern unsigned long cpu_khz;
#else
extern unsigned int cpu_khz;
#endif

extern int rrprofile_debug;

// not supported (yet)
#undef CONFIG_OPROFILE_EVENT_MULTIPLEX

#endif // RRPROFILE

#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX

static ssize_t timeout_read(struct file *file, char __user *buf,
		size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(jiffies_to_msecs(oprofile_time_slice),
					buf, count, offset);
}


static ssize_t timeout_write(struct file *file, char const __user *buf,
		size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	retval = oprofile_set_timeout(val);

	if (retval)
		return retval;
	return count;
}


static const struct file_operations timeout_fops = {
	.read		= timeout_read,
	.write		= timeout_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};

#endif


static ssize_t depth_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(oprofile_backtrace_depth, buf, count,
					offset);
}


static ssize_t depth_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;
	
	if (!oprofile_ops.backtrace)
		return -EINVAL;
		
#ifdef RRPROFILE
	if(test_bit(0, &buffer_opened)) {
		return -EINVAL;
	}
#endif // RRPROFILE

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	retval = oprofile_set_ulong(&oprofile_backtrace_depth, val);
	if (retval)
		return retval;

	return count;
}


static const struct file_operations depth_fops = {
	.read		= depth_read,
	.write		= depth_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};


static ssize_t pointer_size_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(sizeof(void *), buf, count, offset);
}


static const struct file_operations pointer_size_fops = {
	.read		= pointer_size_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};


static ssize_t cpu_type_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
#ifdef RRPROFILE
	return oprofilefs_str_to_user(oprofile_cpu_type, buf, count, offset);
#else
	return oprofilefs_str_to_user(oprofile_ops.cpu_type, buf, count, offset);
#endif // RRPROFILE
}


static const struct file_operations cpu_type_fops = {
	.read		= cpu_type_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};


static ssize_t enable_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(oprofile_started, buf, count, offset);
}


static ssize_t enable_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval)
		return retval;

	if (val)
		retval = oprofile_start();
	else
		oprofile_stop();

	if (retval)
		return retval;
	return count;
}


static const struct file_operations enable_fops = {
	.read		= enable_read,
	.write		= enable_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};


static ssize_t dump_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	wake_up_buffer_waiter();
	return count;
}


static const struct file_operations dump_fops = {
	.write		= dump_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= noop_llseek,
#endif // >= 2.6.37
};

#ifdef RRPROFILE
static ssize_t debug_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	return oprofilefs_ulong_to_user(rrprofile_debug, buf, count, offset);
}


static ssize_t debug_write(struct file * file, char const __user * buf, size_t count, loff_t * offset)
{
	int retval;
	unsigned long val;

	if (*offset) {
		return -EINVAL;
	}

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval) {
		return retval;
	}

	if (rrprofile_debug && !val) {
		printk(KERN_DEBUG "rrprofile: debug output disabled");
	}

	rrprofile_debug = val != 0 ? 1 : 0;

	if (rrprofile_debug) {
		printk(KERN_DEBUG "rrprofile: debug output enabled");
	}

	return count;
}

static const struct file_operations debug_fops = {
	.read           = debug_read,
	.write          = debug_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};

static ssize_t timer_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	return oprofilefs_ulong_to_user(oprofile_timer_count, buf, count, offset);
}


static ssize_t timer_write(struct file * file, char const __user * buf, size_t count, loff_t * offset)
{
	unsigned long val;
	int retval;

	if (*offset)
		return -EINVAL;

	if(test_bit(0, &buffer_opened)) {
		return -EINVAL;
	}

	retval = oprofilefs_ulong_from_user(&val, buf, count);
	if (retval) {
		return retval;
	}

	retval = oprofile_set_oprofile_timer_count(val);
	if(retval) {
		return retval;
	}

	return count;
}

static const struct file_operations oprofile_timer_count_fops = {
    .read		= timer_read,
    .write 		= timer_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};

static ssize_t timer_freq_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
#if defined(CONFIG_HZ_100) || defined(CONFIG_HZ_250) || defined(CONFIG_HZ_1000)
	return oprofilefs_ulong_to_user(CONFIG_HZ, buf, count, offset);
#else
	return oprofilefs_ulong_to_user(HZ, buf, count, offset);
#endif
}

static const struct file_operations timer_freq_fops = {
	.read		= timer_freq_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};

static ssize_t user_freq_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
#if defined(USER_HZ)
	return oprofilefs_ulong_to_user(USER_HZ, buf, count, offset);
#else
#error Unsupported User Frequency, USER_HZ
#endif
}

static const struct file_operations user_freq_fops = {
	.read		= user_freq_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};

static ssize_t cpu_khz_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	unsigned int freq;

#if defined(CONFIG_CPU_FREQ) && LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	struct cpufreq_policy *policy;

	// Only the processor frequency for CPU 0.
	// **** Has to change if we support asymmetric multiprocessing systems.  	
	policy = cpufreq_cpu_get(0);
	if (policy == NULL) {
		freq = cpu_khz;
	} else {
		freq = policy->cpuinfo.max_freq;
	}
#else
	freq = cpu_khz;
#endif

	return oprofilefs_ulong_to_user(freq, buf, count, offset);
}

static const struct file_operations cpu_khz_fops = {
	.read		= cpu_khz_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};

static ssize_t num_counters_read(struct file * file, char __user * buf, size_t count, loff_t * offset)
{
	return oprofilefs_ulong_to_user(oprofile_num_counters, buf, count, offset);
}

static const struct file_operations num_counters_fops = {
	.read		= num_counters_read,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};

#ifdef CONFIG_X86_LOCAL_APIC

static ssize_t adapt_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	return oprofilefs_ulong_to_user(oprofile_adapt_value, buf, count, offset);
}

static ssize_t adapt_write(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	int retval = oprofile_adapt();
	if(retval) return retval;
	return count;
}

static const struct file_operations adapt_fops = {
	.read		= adapt_read,
	.write		= adapt_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};
#endif

#endif // RRPROFILE

void oprofile_create_files(struct super_block * sb, struct dentry * root)
{
	/* reinitialize default values */
	oprofile_buffer_size =		BUFFER_SIZE_DEFAULT;
	oprofile_cpu_buffer_size =	CPU_BUFFER_SIZE_DEFAULT;
	oprofile_buffer_watershed =	BUFFER_WATERSHED_DEFAULT;
	oprofile_time_slice =		msecs_to_jiffies(TIME_SLICE_DEFAULT);

#ifdef RRPROFILE
	oprofilefs_create_file_perm(sb, root, "enable", &enable_fops, 0666);
#else
	oprofilefs_create_file(sb, root, "enable", &enable_fops);
#endif // RRPROFILE
	oprofilefs_create_file_perm(sb, root, "dump", &dump_fops, 0666);
#ifdef RRPROFILE
	oprofilefs_create_file_perm(sb, root, "buffer", &event_buffer_fops, 0666);
#else
	oprofilefs_create_file(sb, root, "buffer", &event_buffer_fops);
#endif // RRPROFILE
	oprofilefs_create_ulong(sb, root, "buffer_size", &oprofile_buffer_size);
	oprofilefs_create_ulong(sb, root, "buffer_watershed", &oprofile_buffer_watershed);
	oprofilefs_create_ulong(sb, root, "cpu_buffer_size", &oprofile_cpu_buffer_size);
	oprofilefs_create_file(sb, root, "cpu_type", &cpu_type_fops);
	oprofilefs_create_file(sb, root, "backtrace_depth", &depth_fops);
	oprofilefs_create_file(sb, root, "pointer_size", &pointer_size_fops);
#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX
	oprofilefs_create_file(sb, root, "time_slice", &timeout_fops);
#endif
#ifdef RRPROFILE
	oprofilefs_create_file_perm(sb, root, "timer_count", &oprofile_timer_count_fops, 0666);
	oprofilefs_create_file(sb, root, "timer_freq", &timer_freq_fops);
	oprofilefs_create_file(sb, root, "user_freq", &user_freq_fops);
	oprofilefs_create_file(sb, root, "cpu_khz", &cpu_khz_fops);
	oprofilefs_create_file(sb, root, "num_counters", &num_counters_fops);
#ifdef CONFIG_X86_LOCAL_APIC
	oprofilefs_create_file_perm(sb, root, "adapt", &adapt_fops, 0666);
#endif
	oprofilefs_create_file_perm(sb, root, "debug", &debug_fops, 0666);
#endif // RRPROFILE

	oprofile_create_stats_files(sb, root);
	if (oprofile_ops.create_files)
		oprofile_ops.create_files(sb, root);
}
