/**
 * @file oprof.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#ifdef RRPROFILE
#include "../oprofile.h"
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/moduleparam.h>
#ifdef RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
#else
#include <linux/workqueue.h>
#include <linux/time.h>
#include <asm/mutex.h>
#endif

#include "oprof.h"
#include "event_buffer.h"
#include "cpu_buffer.h"
#include "buffer_sync.h"
#include "oprofile_stats.h"

#ifdef RRPROFILE
int rrprofile_debug = 0;

// not supported (yet)
#undef CONFIG_OPROFILE_EVENT_MULTIPLEX

#endif // RRPROFILE

struct oprofile_operations oprofile_ops;
#ifdef RRPROFILE
struct oprofile_operations arch_ops;
struct oprofile_operations timer_ops;
#endif // RRPROFILE
unsigned long oprofile_started;
unsigned long oprofile_backtrace_depth;
#ifdef RRPROFILE
unsigned long oprofile_timer_count = 1; // jiffy count between samples
#endif // RRPROFILE
static unsigned long is_setup;
#ifdef RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
static DEFINE_SEMAPHORE(start_sem);
#else
static DECLARE_MUTEX(start_sem);
#endif
unsigned long oprofile_adapt_value = 1;
#else
static DEFINE_MUTEX(start_mutex);
#endif // RRPROFILE

#ifndef RRPROFILE
/* timer
   0 - use performance monitoring hardware if available
   1 - use the timer int mechanism regardless
 */
static int timer = 0;
#endif // RRPROFILE

int oprofile_setup(void)
{
	int err;

#ifdef RRPROFILE
	down(&start_sem);
#else
	mutex_lock(&start_mutex);
#endif // RRPROFILE

	if ((err = alloc_cpu_buffers()))
		goto out;

	if ((err = alloc_event_buffer()))
		goto out1;

	if (oprofile_ops.setup && (err = oprofile_ops.setup()))
		goto out2;

	/* Note even though this starts part of the
	 * profiling overhead, it's necessary to prevent
	 * us missing task deaths and eventually oopsing
	 * when trying to process the event buffer.
	 */
	if (oprofile_ops.sync_start) {
		int sync_ret = oprofile_ops.sync_start();
		switch (sync_ret) {
		case 0:
			goto post_sync;
		case 1:
			goto do_generic;
		case -1:
			goto out3;
		default:
			goto out3;
		}
	}
do_generic:
	if ((err = sync_start()))
		goto out3;

post_sync:
	is_setup = 1;
#ifdef RRPROFILE
	atomic_set(&buffer_dump, 0);
	up(&start_sem);
#else
	mutex_unlock(&start_mutex);
#endif // RRPROFILE
	return 0;
 
out3:
	if (oprofile_ops.shutdown)
		oprofile_ops.shutdown();
out2:
	free_event_buffer();
out1:
	free_cpu_buffers();
out:
#ifdef RRPROFILE
	atomic_set(&buffer_dump, 0);
	up(&start_sem);
#else
	mutex_unlock(&start_mutex);
#endif // RRPROFILE
	return err;
}



#ifdef CONFIG_OPROFILE_EVENT_MULTIPLEX

static void switch_worker(struct work_struct *work);
static DECLARE_DELAYED_WORK(switch_work, switch_worker);

static void start_switch_worker(void)
{
	if (oprofile_ops.switch_events)
		schedule_delayed_work(&switch_work, oprofile_time_slice);
}

static void stop_switch_worker(void)
{
	cancel_delayed_work_sync(&switch_work);
}

static void switch_worker(struct work_struct *work)
{
	if (oprofile_ops.switch_events())
		return;

	atomic_inc(&oprofile_stats.multiplex_counter);
	start_switch_worker();
}

/* User inputs in ms, converts to jiffies */
int oprofile_set_timeout(unsigned long val_msec)
{
	int err = 0;
	unsigned long time_slice;

	mutex_lock(&start_mutex);

	if (oprofile_started) {
		err = -EBUSY;
		goto out;
	}

	if (!oprofile_ops.switch_events) {
		err = -EINVAL;
		goto out;
	}

	time_slice = msecs_to_jiffies(val_msec);
	if (time_slice == MAX_JIFFY_OFFSET) {
		err = -EINVAL;
		goto out;
	}

	oprofile_time_slice = time_slice;

out:
	mutex_unlock(&start_mutex);
	return err;

}

#else

static inline void start_switch_worker(void) { }
static inline void stop_switch_worker(void) { }

#endif

/* Actually start profiling (echo 1>/dev/oprofile/enable) */
int oprofile_start(void)
{
	int err = -EINVAL;

 #ifdef RRPROFILE
	down(&start_sem);
 #else
 	mutex_lock(&start_mutex);
 #endif // RRPROFILE

	if (!is_setup)
		goto out;

	err = 0;

	if (oprofile_started)
		goto out;

	oprofile_reset_stats();
 #ifdef RRPROFILE
	oprofile_adapt_value = 1;
	add_event_entry(ESCAPE_CODE);
	add_event_entry(RR_ADAPT_SAMPLING_INTERVAL_CODE);
	add_event_entry(oprofile_adapt_value);
 #endif // RRPROFILE

	if ((err = oprofile_ops.start()))
		goto out;

	start_switch_worker();
	
	oprofile_started = 1;
	atomic_set(&buffer_dump, 0);
out:
 #ifdef RRPROFILE
	up(&start_sem); 
 #else
	mutex_unlock(&start_mutex);
 #endif // RRPROFILE
	return err;
}


/* echo 0>/dev/oprofile/enable */
void oprofile_stop(void)
{
#ifdef RRPROFILE
	int i;
	down(&start_sem);
#else
	mutex_lock(&start_mutex);
#endif // RRPROFILE
	if (!oprofile_started)
		goto out;
	oprofile_ops.stop();
	oprofile_started = 0;

#ifdef RRPROFILE
	/* sync the cpu and the event buffers (dump remaining events in cpu buffers) */
	for_each_online_cpu(i) {
		sync_buffer(i);
	}

	add_event_entry(ESCAPE_CODE);
	add_event_entry(RR_ADAPT_SAMPLING_INTERVAL_CODE);
	add_event_entry(oprofile_adapt_value);
#endif // RRPROFILE

	stop_switch_worker();

	/* wake up the daemon to read what remains */
	wake_up_buffer_waiter();
out:
#ifdef RRPROFILE
	up(&start_sem);
#else
	mutex_unlock(&start_mutex);
#endif // RRPROFILE
}

#ifdef RRPROFILE
#ifdef CONFIG_X86_LOCAL_APIC
int oprofile_adapt(void)
{
	int i;
	int err = -EINVAL;

	down(&start_sem);
	if (!oprofile_started) {
		goto out;
	}
	// stop
	oprofile_ops.stop();
	for_each_online_cpu(i) {
		sync_buffer(i);
	}

	if(!oprofile_ops.adapt) {
		goto out;
	}
	// fix up the counter values if possible
	if(oprofile_ops.adapt()) {
		oprofile_adapt_value *= ADAPT_DECAY_FACTOR;
		add_event_entry(ESCAPE_CODE);
		add_event_entry(RR_ADAPT_SAMPLING_INTERVAL_CODE);
		add_event_entry(oprofile_adapt_value);
	}

	// start
	if ((err = oprofile_ops.start())) {
		oprofile_started = 0;
	}
out:
	up(&start_sem);

	return err;
}
#endif
#endif // RRPROFILE

void oprofile_shutdown(void)
{
#ifdef RRPROFILE
	down(&start_sem);
#else
	mutex_lock(&start_mutex);
#endif // RRPROFILE
	if (oprofile_ops.sync_stop) {
		int sync_ret = oprofile_ops.sync_stop();
		switch (sync_ret) {
		case 0:
			goto post_sync;
		case 1:
			goto do_generic;
		default:
			goto post_sync;
		}
	}
do_generic:
	sync_stop();
post_sync:
	if (oprofile_ops.shutdown)
		oprofile_ops.shutdown();
	is_setup = 0;
	free_event_buffer();
	free_cpu_buffers();
#ifdef RRPROFILE
	up(&start_sem);
#else
	mutex_unlock(&start_mutex);
#endif // RRPROFILE
}


int oprofile_set_ulong(unsigned long *addr, unsigned long val)
{
	int err = -EBUSY;

#ifdef RRPROFILE
	down(&start_sem);
#else
	mutex_lock(&start_mutex);
#endif // RRPROFILE

	if (!oprofile_started) {
		*addr = val;
		err = 0;
	}
	
#ifdef RRPROFILE
	up(&start_sem);
#else
	mutex_unlock(&start_mutex);
#endif // RRPROFILE
	return err;
}


#ifdef RRPROFILE

int oprofile_set_oprofile_timer_count(unsigned long val)
{
	int err = -EBUSY;

	down(&start_sem);

	if (!oprofile_started) {
		if(val == 0 && arch_ops.cpu_type != NULL) {
//			printk(KERN_INFO "rrprofile: switched to event mode.\n");
			oprofile_ops = arch_ops;
			oprofile_timer_count = 0;
		} else {
//			printk(KERN_INFO "rrprofile: switched to timer mode.\n");
			oprofile_ops = timer_ops;
			oprofile_timer_count = val;
		}
		
		err = 0;
	}
	
	up(&start_sem);
	return err;
}

#endif // RRPROFILE


#ifdef RRPROFILE
static int __init oprofile_init(void)
{
	int err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	sema_init(&start_sem, 1);
#endif
	init_event_buffer();

	memset(&timer_ops, 0, sizeof(struct oprofile_operations));
	oprofile_timer_init(&timer_ops);

	memset(&arch_ops, 0, sizeof(struct oprofile_operations));
	err = oprofile_arch_init(&arch_ops);

#if 0
#if defined(__PPC__) || defined(__PPC64__) || defined(__ppc__) || defined(__ppc64__)
	/* Enable backtracing for OS timer - this only works for PowerPC? */
	timer_ops.backtrace = arch_ops.backtrace;
#elif defined(__i386__)
	// unknown
	timer_ops.backtrace = NULL;
#elif defined(__x86_64__)
	// confirmed does not work on openSUSE 11.0 x86_64
	timer_ops.backtrace = NULL;
#endif
#else
	// confirmed to work on Fedora 16 x86_64 (requires hrtimer implementation introduced in 2.6.35?)
	timer_ops.backtrace = arch_ops.backtrace;
#endif
	
	if(err < 0) {
		printk(KERN_INFO "rrprofile: only timer mode available.\n");
		arch_ops.cpu_type = NULL;
		oprofile_num_counters = 0;
		oprofile_ops = timer_ops;
	} else {
		printk(KERN_INFO "rrprofile: both timer and event modes available.\n");
		oprofile_num_counters = arch_ops.num_counters;
		oprofile_ops = arch_ops;
	}

	/* Set default backtrace depth to 512 */
	oprofile_set_ulong(&oprofile_backtrace_depth, 512);

	/* oprofile_ops.cpu_type will always have a value */
	strcpy(oprofile_cpu_type, oprofile_ops.cpu_type);

	return oprofilefs_register();
}
#else
static int __init oprofile_init(void)
{
	int err;

	err = oprofile_arch_init(&oprofile_ops);
	if (err < 0 || timer) {
		printk(KERN_INFO "rrprofile: using timer interrupt.\n");
		err = oprofile_timer_init(&oprofile_ops);
		if (err)
			return err;
	}
	return oprofilefs_register();
}
#endif // RRPROFILE

static void __exit oprofile_exit(void)
{
	oprofile_timer_exit();
	oprofilefs_unregister();
	oprofile_arch_exit();
}


module_init(oprofile_init);
module_exit(oprofile_exit);

#ifndef RRPROFILE
module_param_named(timer, timer, int, 0644);
MODULE_PARM_DESC(timer, "force use of timer interrupt");
#endif // RRPROFILE

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Levon <levon@movementarian.org>");
MODULE_DESCRIPTION("RRProfile system profiler");
