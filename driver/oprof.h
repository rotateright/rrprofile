/**
 * @file oprof.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef OPROF_H
#define OPROF_H

int oprofile_setup(void);
void oprofile_shutdown(void);

int oprofilefs_register(void);
void oprofilefs_unregister(void);

int oprofile_start(void);
void oprofile_stop(void);

struct oprofile_operations;
 
extern unsigned long oprofile_buffer_size;
extern unsigned long oprofile_cpu_buffer_size;
extern unsigned long oprofile_buffer_watershed;
#ifdef RRPROFILE
extern char          oprofile_cpu_type[];
extern unsigned int  oprofile_num_counters;
#endif // RRPROFILE
extern struct oprofile_operations oprofile_ops;
extern unsigned long oprofile_started;
extern unsigned long oprofile_backtrace_depth;
#ifdef RRPROFILE
extern unsigned long oprofile_timer_count;
extern unsigned long oprofile_adapt_value;
#endif // RRPROFILE

struct super_block;
struct dentry;

void oprofile_create_files(struct super_block *sb, struct dentry *root);
int oprofile_timer_init(struct oprofile_operations *ops);
void oprofile_timer_exit(void);

int oprofile_set_ulong(unsigned long *addr, unsigned long val);
int oprofile_set_timeout(unsigned long time);

#ifdef RRPROFILE
int oprofile_set_oprofile_timer_count(unsigned long val);

#ifdef CONFIG_X86_LOCAL_APIC
int oprofile_adapt(void);
#endif
#endif // RRPROFILE
 
#endif /* OPROF_H */
