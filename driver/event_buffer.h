/**
 * @file event_buffer.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef EVENT_BUFFER_H
#define EVENT_BUFFER_H

#include <linux/version.h>
#include <linux/types.h> 

#ifdef RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
void init_event_buffer(void);
#else
#include <asm/mutex.h>
#endif // RRPROFILE
 
int alloc_event_buffer(void);

void free_event_buffer(void);

/**
 * Add data to the event buffer.
 * The data passed is free-form, but typically consists of
 * file offsets, dcookies, context information, and ESCAPE codes.
 */
void add_event_entry(unsigned long data);

/* wake up the process sleeping on the event file */
void wake_up_buffer_waiter(void);

#define INVALID_COOKIE ~0UL
#define NO_COOKIE 0UL

extern const struct file_operations event_buffer_fops;

/* mutex between sync_cpu_buffers() and the
 * file reading code.
 */
#ifdef RRPROFILE
extern struct semaphore buffer_sem;
extern atomic_t buffer_dump;
extern unsigned long buffer_opened;
#else
extern struct mutex buffer_mutex;
#endif // RRPROFILE

#endif /* EVENT_BUFFER_H */
