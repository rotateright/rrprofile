/**
 * @file cpu_buffer.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 * @author Barry Kasindorf <barry.kasindorf@amd.com>
 *
 * Each CPU has a local buffer that stores PC value/event
 * pairs. We also log context switches when we notice them.
 * Eventually each CPU's buffer is processed into the global
 * event buffer by sync_buffer().
 *
 * We use a local buffer for two reasons: an NMI or similar
 * interrupt cannot synchronise, and high sampling rates
 * would lead to catastrophic global synchronisation if
 * a global buffer was used.
 */

#include <linux/sched.h>
#ifdef RRPROFILE
#include "../oprofile.h"
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/vmalloc.h>
#include <linux/errno.h>

#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprof.h"

#ifdef RRPROFILE
struct oprofile_cpu_buffer cpu_buffer[NR_CPUS] __cacheline_aligned;
#else
DEFINE_PER_CPU(struct oprofile_cpu_buffer, cpu_buffer);
#endif // RRPROFILE

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
static void wq_sync_buffer(struct work_struct *work);
#else
static void wq_sync_buffer(void *);
#endif

#define DEFAULT_TIMER_EXPIRE (HZ / 10)
static int work_enabled;

void free_cpu_buffers(void)
{
	int i;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) 
	for_each_possible_cpu(i) {
#else
	for_each_online_cpu(i) {
#endif
		vfree(cpu_buffer[i].buffer);
		cpu_buffer[i].buffer = NULL;
	}
}

unsigned long oprofile_get_cpu_buffer_size(void)
{
	return oprofile_cpu_buffer_size;
}

void oprofile_cpu_buffer_inc_smpl_lost(void)
{
	struct oprofile_cpu_buffer *cpu_buf = &cpu_buffer[smp_processor_id()];

	cpu_buf->sample_lost_overflow++;
}

int alloc_cpu_buffers(void)
{
	int i;

	unsigned long buffer_size = oprofile_cpu_buffer_size;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) 
	for_each_possible_cpu(i) {
#else
	for_each_online_cpu(i) {
#endif
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15) 
		b->buffer = vmalloc_node(sizeof(struct op_sample) * buffer_size,
			cpu_to_node(i));
#else
		b->buffer = vmalloc(sizeof(struct op_sample) * buffer_size);
#endif
		if (!b->buffer)
			goto fail;

		b->last_task = NULL;
		b->last_is_kernel = -1;
		b->tracing = 0;
		b->buffer_size = buffer_size;
		b->tail_pos = 0;
		b->head_pos = 0;
		b->sample_received = 0;
		b->sample_lost_overflow = 0;
		b->backtrace_aborted = 0;
		b->sample_invalid_eip = 0;
		b->cpu = i;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
		INIT_DELAYED_WORK(&b->work, wq_sync_buffer);
#else
		INIT_WORK(&b->work, wq_sync_buffer, b);
#endif
	}
	return 0;

fail:
	free_cpu_buffers();
	printk(KERN_ERR "rrprofile: failed to allocate CPU buffers (%ld bytes per CPU)\n", sizeof(struct op_sample) * buffer_size);
	return -ENOMEM;
}

void start_cpu_work(void)
{
	int i;

	work_enabled = 1;

	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];

		/*
		 * Spread the work by 1 jiffy per cpu so they dont all
		 * fire at once.
		 */
		schedule_delayed_work_on(i, &b->work, DEFAULT_TIMER_EXPIRE + i);
	}
}

void end_cpu_work(void)
{
	int i;

	work_enabled = 0;

	for_each_online_cpu(i) {
		struct oprofile_cpu_buffer * b = &cpu_buffer[i];

		cancel_delayed_work(&b->work);
	}

	flush_scheduled_work();
}

/* Resets the cpu buffer to a sane state. */
void cpu_buffer_reset(struct oprofile_cpu_buffer *cpu_buf)
{
	/* reset these to invalid values; the next sample
	 * collected will populate the buffer with proper
	 * values to initialize the buffer
	 */
	cpu_buf->last_is_kernel = -1;
	cpu_buf->last_task = NULL;
}

/* compute number of available slots in cpu_buffer queue */
static unsigned long nr_available_slots(struct oprofile_cpu_buffer const *b)
{
	unsigned long head = b->head_pos;
	unsigned long tail = b->tail_pos;

	if (tail > head)
		return (tail - head) - 1;

	return tail + (b->buffer_size - head) - 1;
}

static void increment_head(struct oprofile_cpu_buffer *b)
{
	unsigned long new_head = b->head_pos + 1;

	/* Ensure anything written to the slot before we
	 * increment is visible */
	wmb();

	if (new_head < b->buffer_size)
		b->head_pos = new_head;
	else
		b->head_pos = 0;
}

static inline void
add_sample(struct oprofile_cpu_buffer *cpu_buf,
           unsigned long pc, unsigned long event, uint64_t timestamp)
{
	struct op_sample * entry = &cpu_buf->buffer[cpu_buf->head_pos];
	entry->eip = pc;
	entry->event = event;
	entry->timestamp = timestamp;
	increment_head(cpu_buf);
}

static inline void
add_code(struct oprofile_cpu_buffer * buffer, unsigned long value)
{
	add_sample(buffer, ESCAPE_CODE, value, 0);
}

#ifdef RRPROFILE
static inline void
add_code_ctx_rr(struct oprofile_cpu_buffer * buffer, unsigned long tgid, unsigned long tid)
{
	add_sample(buffer, ESCAPE_CODE, RR_CPU_CTX_TGID, tgid);
	add_sample(buffer, ESCAPE_CODE, RR_CPU_CTX_TID, tid);
}
#endif // RRPROFILE

/* This must be safe from any context. It's safe writing here
 * because of the head/tail separation of the writer and reader
 * of the CPU buffer.
 *
 * is_kernel is needed because on some architectures you cannot
 * tell if you are in kernel or user space simply by looking at
 * pc. We tag this in the buffer by generating kernel enter/exit
 * events whenever is_kernel changes
 */
static int log_sample(struct oprofile_cpu_buffer *cpu_buf, unsigned long pc,
		      int is_kernel, unsigned long event)
{
	struct task_struct *task;

	cpu_buf->sample_received++;

	if (pc == ESCAPE_CODE) {
		cpu_buf->sample_invalid_eip++;
		return 0;
	}

	if (nr_available_slots(cpu_buf) < 3) {
		cpu_buf->sample_lost_overflow++;
		return 0;
	}

	is_kernel = !!is_kernel;

	task = current;

	/* notice a switch from user->kernel or vice versa */
	if (cpu_buf->last_is_kernel != is_kernel) {
		cpu_buf->last_is_kernel = is_kernel;
		add_code(cpu_buf, is_kernel);
	}

	/* notice a task switch */
	if (cpu_buf->last_task != task) {
		cpu_buf->last_task = task;
#ifdef RRPROFILE
		add_code_ctx_rr(cpu_buf, task->tgid, task->pid);
#else
		add_code(cpu_buf, (unsigned long)task);
#endif // RRPROFILE
	}
 
	add_sample(cpu_buf, pc, event, 0);
	return 1;
}

static int oprofile_begin_trace(struct oprofile_cpu_buffer * cpu_buf)
{
	if (nr_available_slots(cpu_buf) < 4) {
		cpu_buf->sample_lost_overflow++;
		return 0;
	}

	add_code(cpu_buf, CPU_TRACE_BEGIN);
	cpu_buf->tracing = 1;
	return 1;
}

static void oprofile_end_trace(struct oprofile_cpu_buffer *cpu_buf)
{
	cpu_buf->tracing = 0;
}

void oprofile_add_ext_sample(unsigned long pc, struct pt_regs * const regs,
				unsigned long event, int is_kernel)
{
	struct oprofile_cpu_buffer *cpu_buf = &cpu_buffer[smp_processor_id()];

#ifdef RRPROFILE
	if (!oprofile_backtrace_depth || !oprofile_ops.backtrace) {
#else
	if (!oprofile_backtrace_depth) {
#endif // RRPROFILE
		log_sample(cpu_buf, pc, is_kernel, event);
		return;
	}

	if (!oprofile_begin_trace(cpu_buf))
		return;

	/* if log_sample() fail we can't backtrace since we lost the source
	 * of this event */
	if (log_sample(cpu_buf, pc, is_kernel, event))
		oprofile_ops.backtrace(regs, oprofile_backtrace_depth);
	oprofile_end_trace(cpu_buf);
}

void oprofile_add_sample(struct pt_regs * const regs, unsigned long event)
{
	int is_kernel;
	unsigned long pc;

	if(regs) {
		is_kernel = !user_mode(regs);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
		pc = instruction_pointer(regs);
#else
		pc = profile_pc(regs);
#endif
	} else {
		is_kernel = 0;    /* This value will not be used */
		pc = ESCAPE_CODE; /* as this causes an early return. */
	}

	oprofile_add_ext_sample(pc, regs, event, is_kernel);
}

void oprofile_add_pc(unsigned long pc, int is_kernel, unsigned long event)
{
	struct oprofile_cpu_buffer * cpu_buf = &cpu_buffer[smp_processor_id()];
	log_sample(cpu_buf, pc, is_kernel, event);
}

void oprofile_add_trace(unsigned long pc)
{
	struct oprofile_cpu_buffer * cpu_buf = &cpu_buffer[smp_processor_id()];

	if (!cpu_buf->tracing)
		return;

	if (nr_available_slots(cpu_buf) < 1) {
		cpu_buf->tracing = 0;
		cpu_buf->sample_lost_overflow++;
		return;
	}

	/* broken frame can give an eip with the same value as an escape code,
	 * abort the trace if we get it */
	if (pc == ESCAPE_CODE) {
		cpu_buf->tracing = 0;
		cpu_buf->backtrace_aborted++;
		return;
	}

	add_sample(cpu_buf, pc, 0, 0);
}

#ifdef RRPROFILE
void oprofile_add_start(void *dummy)
{
	struct oprofile_cpu_buffer *cpu_buf = &cpu_buffer[smp_processor_id()];
	
	add_sample(cpu_buf, ESCAPE_CODE, RR_CPU_SAMPLING_START_TIMESTAMP, oprofile_get_tb());
}

void oprofile_add_stop(void *dummy)
{
	struct oprofile_cpu_buffer *cpu_buf = &cpu_buffer[smp_processor_id()];
	
	add_sample(cpu_buf, ESCAPE_CODE, RR_CPU_SAMPLING_STOP_TIMESTAMP, oprofile_get_tb());
}

void oprofile_add_sample_start(uint64_t timestamp)
{
	struct oprofile_cpu_buffer *cpu_buf = &cpu_buffer[smp_processor_id()];
	
	add_sample(cpu_buf, ESCAPE_CODE, RR_CPU_SAMPLE_START_TIMESTAMP, timestamp);
}

void oprofile_add_sample_stop(uint64_t timestamp)
{
	struct oprofile_cpu_buffer *cpu_buf = &cpu_buffer[smp_processor_id()];
	
	add_sample(cpu_buf, ESCAPE_CODE, RR_CPU_SAMPLE_STOP_TIMESTAMP, timestamp);
}
#endif // RRPROFILE

/*
 * This serves to avoid cpu buffer overflow, and makes sure
 * the task mortuary progresses
 *
 * By using schedule_delayed_work_on and then schedule_delayed_work
 * we guarantee this will stay on the correct cpu
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
static void wq_sync_buffer(struct work_struct *work)
{
	struct oprofile_cpu_buffer *b =
		container_of(work, struct oprofile_cpu_buffer, work.work);
#else
static void wq_sync_buffer(void *data)
{
	struct oprofile_cpu_buffer *b = data;
#endif
	if (b->cpu != smp_processor_id()) {
		printk(KERN_DEBUG "WQ on CPU%d, prefer CPU%d\n",
		       smp_processor_id(), b->cpu);

		if (!cpu_online(b->cpu)) {
			cancel_delayed_work(&b->work);
			return;
		}
	}
	sync_buffer(b->cpu);

	/* don't re-add the work if we're shutting down */
	if (work_enabled)
		schedule_delayed_work(&b->work, DEFAULT_TIMER_EXPIRE);
}
