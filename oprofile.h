/**
 * @file oprofile.h
 *
 * API for machine-specific interrupts to interface
 * to oprofile.
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef OPROFILE_H
#define OPROFILE_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
 
/* Each escaped entry is prefixed by ESCAPE_CODE
 * then one of the following codes, then the
 * relevant data.
 * These #defines live in this file so that arch-specific
 * buffer sync'ing code can access them.
 */
#define ESCAPE_CODE			~0UL
#define CTX_SWITCH_CODE			1
#define CPU_SWITCH_CODE			2
#define COOKIE_SWITCH_CODE		3
#define KERNEL_ENTER_SWITCH_CODE	4
#define KERNEL_EXIT_SWITCH_CODE		5
#define MODULE_LOADED_CODE		6
#define CTX_TGID_CODE			7
#define TRACE_BEGIN_CODE		8
#define TRACE_END_CODE			9
#define XEN_ENTER_SWITCH_CODE		10
#define SPU_PROFILING_CODE		11
#define SPU_CTX_SWITCH_CODE		12
#define IBS_FETCH_CODE			13
#define IBS_OP_CODE			14
#ifdef RRPROFILE
#define RR_CPU_SAMPLING_BEGIN_TIMESTAMP_CODE	100
#define RR_CPU_SAMPLING_END_TIMESTAMP_CODE		101
#define RR_SAMPLE_BEGIN_TIMESTAMP_CODE			102
#define RR_SAMPLE_END_TIMESTAMP_CODE			103
#define RR_ADAPT_SAMPLING_INTERVAL_CODE			104
#endif // RRPROFILE

struct super_block;
struct dentry;
struct file_operations;
struct pt_regs;

#ifdef RRPROFILE
#define ADAPT_DECAY_FACTOR 10

struct rrprofile_tid_buffer;
#endif // RRPROFILE
 
/* Operations structure to be filled in */
struct oprofile_operations {
	/* create any necessary configuration files in the oprofile fs.
	 * Optional. */
	int (*create_files)(struct super_block * sb, struct dentry * root);
	/* Do any necessary interrupt setup. Optional. */
	int (*setup)(void);
	/* Do any necessary interrupt shutdown. Optional. */
	void (*shutdown)(void);
	/* Start delivering interrupts. */
	int (*start)(void);
	/* Stop delivering interrupts. */
	void (*stop)(void);
	/* Arch-specific buffer sync functions.
	 * Return value = 0:  Success
	 * Return value = -1: Failure
	 * Return value = 1:  Run generic sync function
	 */
	int (*sync_start)(void);
	int (*sync_stop)(void);

	/* Initiate a stack backtrace. Optional. */
	void (*backtrace)(struct pt_regs * const regs, unsigned int depth);
#ifdef RRPROFILE
	/* Adjust the sampling rate based on decay factor. Optional. */
	int (*adapt)(void);
#endif // RRPROFILE
	/* CPU identification string. */
	char * cpu_type;
#ifdef RRPROFILE
	/* Number of Counters. */
	unsigned int num_counters;
#endif // RRPROFILE
};

/**
 * One-time initialisation. *ops must be set to a filled-in
 * operations structure. This is called even in timer interrupt
 * mode so an arch can set a backtrace callback.
 *
 * If an error occurs, the fields should be left untouched.
 */
int oprofile_arch_init(struct oprofile_operations * ops);
 
/**
 * One-time exit/cleanup for the arch.
 */
void oprofile_arch_exit(void);

/**
 * Add a sample. This may be called from any context. Pass
 * smp_processor_id() as cpu.
 */
void oprofile_add_sample(struct pt_regs * const regs, unsigned long event);

#ifdef RRPROFILE
/**
 * Add a sample. This is called from an interrupt context. Pass
 * smp_processor_id() as cpu.
 * Samples from user space will be scheduled to be processed in process context, 
 * as opposed to interrupt context. 
 */
void rrprofile_add_sample_tid(struct pt_regs * const regs, unsigned long event, 
		uint64_t start, uint64_t stop);
#endif // RRPROFILE

/**
 * Add an extended sample.  Use this when the PC is not from the regs, and
 * we cannot determine if we're in kernel mode from the regs.
 *
 * This function does perform a backtrace.
 *
 */
void oprofile_add_ext_sample(unsigned long pc, struct pt_regs * const regs,
				unsigned long event, int is_kernel);

/* Use this instead when the PC value is not from the regs. Doesn't
 * backtrace. */
void oprofile_add_pc(unsigned long pc, int is_kernel, unsigned long event);

/* add a backtrace entry, to be called from the ->backtrace callback */
void oprofile_add_trace(unsigned long eip);


/**
 * Create a file of the given name as a child of the given root, with
 * the specified file operations.
 */
int oprofilefs_create_file(struct super_block * sb, struct dentry * root,
	char const * name, const struct file_operations * fops);

int oprofilefs_create_file_perm(struct super_block * sb, struct dentry * root,
	char const * name, const struct file_operations * fops, int perm);
 
/** Create a file for read/write access to an unsigned long. */
int oprofilefs_create_ulong(struct super_block * sb, struct dentry * root,
	char const * name, ulong * val);
 
/** Create a file for read-only access to an unsigned long. */
int oprofilefs_create_ro_ulong(struct super_block * sb, struct dentry * root,
	char const * name, ulong * val);

#ifdef RRPROFILE
/** Create a file for reading the tid buffer. */
int oprofilefs_create_tid_buffer_file(struct super_block * sb, struct dentry * root,
	char const * name, struct file_operations * fops, struct rrprofile_tid_buffer * tid_buf);
#endif // RRPROFILE

/** Create a file for read-only access to an atomic_t. */
int oprofilefs_create_ro_atomic(struct super_block * sb, struct dentry * root,
	char const * name, atomic_t * val);
 
/** create a directory */
struct dentry * oprofilefs_mkdir(struct super_block * sb, struct dentry * root,
	char const * name);

/**
 * Write the given asciz string to the given user buffer @buf, updating *offset
 * appropriately. Returns bytes written or -EFAULT.
 */
ssize_t oprofilefs_str_to_user(char const * str, char __user * buf, size_t count, loff_t * offset);

/**
 * Convert an unsigned long value into ASCII and copy it to the user buffer @buf,
 * updating *offset appropriately. Returns bytes written or -EFAULT.
 */
ssize_t oprofilefs_ulong_to_user(unsigned long val, char __user * buf, size_t count, loff_t * offset);

/**
 * Read an ASCII string for a number from a userspace buffer and fill *val on success.
 * Returns 0 on success, < 0 on error.
 */
int oprofilefs_ulong_from_user(unsigned long * val, char const __user * buf, size_t count);

/** lock for read/write safety */
extern spinlock_t oprofilefs_lock;

/**
 * Add the contents of a circular buffer to the event buffer.
 */
void oprofile_put_buff(unsigned long *buf, unsigned int start,
			unsigned int stop, unsigned int max);

unsigned long oprofile_get_cpu_buffer_size(void);
void oprofile_cpu_buffer_inc_smpl_lost(void);

#ifdef RRPROFILE

/**
 * Get the timestamp or timebase register. Returns an unsigned long
 **/
uint64_t oprofile_get_tb(void);

/**
 * Called by each cpu to record start timestamp.
 */
void oprofile_add_start(void *dummy);

/**
 * Called by each cpu to record stop timestamp.
 */
void oprofile_add_stop(void *dummy);

/**
 * Called by to record sample start timestamp.
 */
void oprofile_add_sample_start(uint64_t timestamp);

/**
 * Called by to record sample stop timestamp.
 */
void oprofile_add_sample_stop(uint64_t timestamp);

/** boolean for logging debug info */
extern int rrprofile_debug;

#endif // RRPROFILE

#endif /* OPROFILE_H */
