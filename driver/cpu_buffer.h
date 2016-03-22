/**
 * @file cpu_buffer.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef OPROFILE_CPU_BUFFER_H
#define OPROFILE_CPU_BUFFER_H

#include <linux/version.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/cache.h>
#include <linux/sched.h>

struct task_struct;

int alloc_cpu_buffers(void);
void free_cpu_buffers(void);

void start_cpu_work(void);
void end_cpu_work(void);

/* CPU buffer is composed of such entries (which are
 * also used for context switch notes)
 */
struct op_sample {
	unsigned long eip;
	unsigned long event;
#ifdef RRPROFILE
	uint64_t timestamp;
#endif // RRPROFILE
};

struct oprofile_cpu_buffer {
	volatile unsigned long head_pos;
	volatile unsigned long tail_pos;
	unsigned long buffer_size;
	struct task_struct *last_task;
	int last_is_kernel;
	int tracing;
	struct op_sample *buffer;
	unsigned long sample_received;
	unsigned long sample_lost_overflow;
	unsigned long backtrace_aborted;
	unsigned long sample_invalid_eip;
	int cpu;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
	struct delayed_work work;
#else
	struct work_struct work;
#endif
};

#ifdef RRPROFILE
extern struct oprofile_cpu_buffer cpu_buffer[];
#else
DECLARE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);
#endif // RRPROFILE

void cpu_buffer_reset(struct oprofile_cpu_buffer *cpu_buf);

/* transient events for the CPU buffer -> event buffer */
#define CPU_IS_KERNEL 1
#define CPU_TRACE_BEGIN 2
#define IBS_FETCH_BEGIN 3
#define IBS_OP_BEGIN    4
#ifdef RRPROFILE
#define RR_CPU_CTX_TGID						100
#define RR_CPU_CTX_TID						101
#define RR_CPU_SAMPLING_START_TIMESTAMP		102
#define RR_CPU_SAMPLING_STOP_TIMESTAMP		103
#define RR_CPU_SAMPLE_START_TIMESTAMP		104
#define RR_CPU_SAMPLE_STOP_TIMESTAMP		105
#endif // RRPROFILE

#endif /* OPROFILE_CPU_BUFFER_H */
