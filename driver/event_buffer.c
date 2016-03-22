/**
 * @file event_buffer.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 *
 * This is the global event buffer that the user-space
 * daemon reads from. The event buffer is an untyped array
 * of unsigned longs. Entries are prefixed by the
 * escape value ESCAPE_CODE followed by an identifying code.
 */

#include <linux/vmalloc.h>
#ifdef RRPROFILE
#include "../oprofile.h"
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/dcookies.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#include "oprof.h"
#include "event_buffer.h"
#include "oprofile_stats.h"

#ifdef RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
DEFINE_SEMAPHORE(buffer_sem);
#else
DECLARE_MUTEX(buffer_sem);
#endif
atomic_t buffer_dump = ATOMIC_INIT(0);
unsigned long buffer_opened;
#else
DEFINE_MUTEX(buffer_mutex);
static unsigned long buffer_opened;
#endif // RRPROFILE
static DECLARE_WAIT_QUEUE_HEAD(buffer_wait);
static unsigned long *event_buffer;
static unsigned long buffer_size;
static unsigned long buffer_watershed;
static size_t buffer_pos;
/* atomic_t because wait_event checks it outside of buffer_mutex / buffer_sem */
static atomic_t buffer_ready = ATOMIC_INIT(0);

/* Add an entry to the event buffer. When we
 * get near to the end we wake up the process
 * sleeping on the read() of the file.
 */
void add_event_entry(unsigned long value)
{
	if (buffer_pos == buffer_size) {
		atomic_inc(&oprofile_stats.event_lost_overflow);
		return;
	}

	event_buffer[buffer_pos] = value;
	if (++buffer_pos == buffer_size - buffer_watershed) {
		atomic_set(&buffer_ready, 1);
		wake_up(&buffer_wait);
	}
}


/* Wake up the waiting process if any. This happens
 * on "echo 0 >/dev/oprofile/enable" so the daemon
 * processes the data remaining in the event buffer.
 */
void wake_up_buffer_waiter(void)
{
#ifdef RRPROFILE
	down(&buffer_sem);
	atomic_set(&buffer_ready, 1);
	atomic_set(&buffer_dump, 1);
	wake_up(&buffer_wait);
	up(&buffer_sem);
#else
	mutex_lock(&buffer_mutex);
	atomic_set(&buffer_ready, 1);
	wake_up(&buffer_wait);
	mutex_unlock(&buffer_mutex);
#endif // RRPROFILE
}

#ifdef RRPROFILE
void init_event_buffer(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	sema_init(&buffer_sem, 1);
#endif
}
#endif // RRPROFILE
 
int alloc_event_buffer(void)
{
	int err = -ENOMEM;
#ifndef RRPROFILE
	unsigned long flags;
#endif // !RRPROFILE

#ifdef RRPROFILE
	spin_lock(&oprofilefs_lock);
#else
	spin_lock_irqsave(&oprofilefs_lock, flags);
#endif // RRPROFILE
	buffer_size = oprofile_buffer_size;
	buffer_watershed = oprofile_buffer_watershed;
#ifdef RRPROFILE
	spin_unlock(&oprofilefs_lock);
#else
	spin_unlock_irqrestore(&oprofilefs_lock, flags);
#endif // RRPROFILE
 
	if (buffer_watershed >= buffer_size)
		return -EINVAL;

	event_buffer = vmalloc(sizeof(unsigned long) * buffer_size);
	if (!event_buffer) {
		printk(KERN_ERR "rrprofile: failed to allocate event buffer (%ld bytes)\n", sizeof(unsigned long) * buffer_size);
		goto out;
	}
	
	err = 0;
out:
	return err;
}


void free_event_buffer(void)
{
	vfree(event_buffer);

	event_buffer = NULL;
}


static int event_buffer_open(struct inode *inode, struct file *file)
{
	int err = -EPERM;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

#ifdef RRPROFILE
	if (test_and_set_bit(0, &buffer_opened))
		return -EBUSY;
#else
	if (test_and_set_bit_lock(0, &buffer_opened))
		return -EBUSY;
#endif // RRRPROFILE

#ifndef RRPROFILE
	/* Register as a user of dcookies
	 * to ensure they persist for the lifetime of
	 * the open event file
	 */
	err = -EINVAL;
	file->private_data = dcookie_register();
	if (!file->private_data)
		goto out;
#endif // !RRPROFILE
		 
	if ((err = oprofile_setup()))
		goto fail;

	/* NB: the actual start happens from userspace
	 * echo 1 >/dev/oprofile/enable
	 */

	return 0;

#ifdef RRPROFILE
fail:
	clear_bit(0, &buffer_opened);
#else
fail:
	dcookie_unregister(file->private_data);
out:
	__clear_bit_unlock(0, &buffer_opened);
#endif // RRPROFILE
	return err;
}


static int event_buffer_release(struct inode * inode, struct file * file)
{
	oprofile_stop();
	oprofile_shutdown();
#ifndef RRPROFILE
	dcookie_unregister(file->private_data);
#endif // !RRPROFILE
	buffer_pos = 0;
	atomic_set(&buffer_ready, 0);
#ifdef RRPROFILE
	clear_bit(0, &buffer_opened);
#else
	__clear_bit_unlock(0, &buffer_opened);
#endif // RRPROFILE
	return 0;
}


static ssize_t event_buffer_read(struct file *file, char __user *buf,
				 size_t count, loff_t *offset)
{
	int retval = -EINVAL;
	size_t const max = buffer_size * sizeof(unsigned long);

	/* handling partial reads is more trouble than it's worth */
	if (count != max || *offset)
		return -EINVAL;

#ifdef RRPROFILE
	if (!atomic_read(&buffer_dump)) {	
		wait_event_interruptible(buffer_wait, atomic_read(&buffer_ready));
	}
#else
	wait_event_interruptible(buffer_wait, atomic_read(&buffer_ready));
#endif // RRPROFILE

	if (signal_pending(current))
		return -EINTR;

	/* can't currently happen */
	if (!atomic_read(&buffer_ready))
		return -EAGAIN;

#ifdef RRPROFILE
	down(&buffer_sem);
#else
	mutex_lock(&buffer_mutex);
#endif // RRPROFILE

	atomic_set(&buffer_ready, 0);

	retval = -EFAULT;

	count = buffer_pos * sizeof(unsigned long);

	if (copy_to_user(buf, event_buffer, count))
		goto out;

	retval = count;
	buffer_pos = 0;

out:
#ifdef RRPROFILE
	up(&buffer_sem);
#else
	mutex_unlock(&buffer_mutex);
#endif // RRPROFILE
	return retval;
}

#ifdef RRPROFILE
static ssize_t event_buffer_write(struct file * file, char const __user * buf, size_t count, loff_t * offset)
{
	wake_up_buffer_waiter();
	return count;
}
#endif // RRPROFILE
 
const struct file_operations event_buffer_fops = {
	.open		= event_buffer_open,
	.release	= event_buffer_release,
	.read		= event_buffer_read,
#ifdef RRPROFILE
	.write		= event_buffer_write,
#endif // RRPROFILE
};
