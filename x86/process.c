/**
 * @file process.c
 *
 * @remark Copyright 1995 Linux Kernel Authors
 * @remark Read the file COPYING
 * @remark This file is based on arch/x86_64/kernel/process.c
 */

#include <linux/version.h>
#include <linux/smp.h>
#include <linux/thread_info.h>
#include <linux/pm.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
#include <asm/system.h>
#endif

unsigned int poll_idle_enabled = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
static void do_nothing(void *unused)
{
}

static void cpu_idle_wait(void)
{
	smp_mb();
	/* kick all the CPUs so that they exit out of pm_idle */
	smp_call_function(do_nothing, NULL, 0, 1);
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
static void cpu_idle_wait(void)
{
	kick_all_cpus_sync();
}
#endif

static void poll_idle(void)
{
	local_irq_enable();
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
	set_thread_flag(TIF_POLLING_NRFLAG);
#endif
	asm volatile(
		"2:"
		"testl %0,%1;"
		"rep; nop;"
		"je 2b;"
		: :
		"i" (_TIF_NEED_RESCHED),
		"m" (current_thread_info()->flags));
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
	clear_thread_flag(TIF_POLLING_NRFLAG);
#endif

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
static void (*pm_idle_save) (void) __read_mostly;
#else
static void (*pm_idle_save) (void);
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,39)

// XXX: pm_idle no longer exported after 2.6.39
// can still use idle=poll kernel boot option, but no longer possible to modify after boot

#include <linux/kallsyms.h>
#include <linux/cpuidle.h>

typedef void (*pm_idle_fn_t) (void);
pm_idle_fn_t *pm_idle_p = NULL;

typedef void (*void_fn_t) (void);
void_fn_t _cpuidle_uninstall_idle_handler = NULL;
void_fn_t _cpuidle_install_idle_handler = NULL;

void init_poll_idle(void)
{
	pm_idle_p = (pm_idle_fn_t *) kallsyms_lookup_name("pm_idle"); // kallsyms_lookup_name() exported since 2.6.33
	if(!pm_idle_p) {
		printk(KERN_WARNING "rrprofile: 'pm_idle' symbol lookup failed");
	} else {
		pm_idle_save = *pm_idle_p;

//		printk(KERN_DEBUG "rrprofile: 'pm_idle' at %p\n", pm_idle_p);
	}

	_cpuidle_uninstall_idle_handler = (void_fn_t)kallsyms_lookup_name("cpuidle_uninstall_idle_handler");
	if(!_cpuidle_uninstall_idle_handler) {
		printk(KERN_WARNING "rrprofile: 'cpuidle_uninstall_idle_handler' symbol lookup failed");
	} else {
//		printk(KERN_DEBUG "rrprofile: 'cpuidle_uninstall_idle_handler' at %p\n", _cpuidle_uninstall_idle_handler);
	}

	_cpuidle_install_idle_handler = (void_fn_t)kallsyms_lookup_name("cpuidle_install_idle_handler");
	if(!_cpuidle_install_idle_handler) {
		printk(KERN_WARNING "rrprofile: 'cpuidle_install_idle_handler' symbol lookup failed");
	} else {
//		printk(KERN_DEBUG "rrprofile: 'cpuidle_install_idle_handler' at %p\n", _cpuidle_install_idle_handler);
	}
}

void enable_poll_idle(void)
{
	if(!pm_idle_p || !pm_idle_save || !_cpuidle_install_idle_handler || !_cpuidle_uninstall_idle_handler) {
		return;
	}

	if(*pm_idle_p != poll_idle) {
		*pm_idle_p = poll_idle;
		_cpuidle_uninstall_idle_handler(); // force kernel to fall back to old pm_idle() implementation

		printk(KERN_INFO "rrprofile: switched to poll_idle (disallow cpu nap)\n");
	}
}

void disable_poll_idle(void)
{
	if(!pm_idle_p || !pm_idle_save || !_cpuidle_install_idle_handler || !_cpuidle_uninstall_idle_handler) {
		return;
	}

	if(*pm_idle_p != pm_idle_save) {
		*pm_idle_p = pm_idle_save;
		_cpuidle_install_idle_handler(); // reinstate cpuidle

		printk(KERN_INFO "rrprofile: switched to original idle (allow cpu nap)\n");
	}
}

void exit_poll_idle (void)
{
	disable_poll_idle();
	cpu_idle_wait(); // XXX can not be called with interrupts disabled (bug #2110)
}

#else

void init_poll_idle(void)
{
	pm_idle_save = pm_idle;
}

void enable_poll_idle(void)
{
	if(pm_idle != poll_idle) {
		pm_idle = poll_idle;

		printk(KERN_INFO "rrprofile: switched to poll_idle (disallow cpu nap)\n");
	}
}

void disable_poll_idle(void)
{
	if(pm_idle != pm_idle_save) {
		pm_idle = pm_idle_save;

		printk(KERN_INFO "rrprofile: switched to original idle (allow cpu nap)\n");
	}
}

void exit_poll_idle (void)
{
	disable_poll_idle();
	cpu_idle_wait(); // XXX can not be called with interrupts disabled (bug #2110)
}

#endif // LINUX_VERSION_CODE > 2.6.39
