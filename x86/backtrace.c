/**
 * @file backtrace.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author David Smith
 */

#ifdef RRPROFILE
#include "../oprofile.h"
#include <linux/version.h>
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/uaccess.h>

struct frame_head {
	struct frame_head * ebp;
	unsigned long ret;
} __attribute__((packed));

#ifdef RRPROFILE

#ifdef CONFIG_X86_64
struct frame_head64 {
	unsigned long ebp_ptr;
	unsigned long ret;
} __attribute__((packed));
#endif

struct frame_head32 {
	unsigned int ebp_ptr;
	unsigned int ret;
} __attribute__((packed));

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
/* HACK HACK HACK 
 * Because follow_page() is not exported, we need to initialize the function pointers 
 * at compile time with values from /dev/kallsyms. 
 */
struct page *
(* follow_page_ptr)(struct mm_struct *mm, unsigned long address, int write) = (void *) 0x0; /* follow_page_ptr */

/* HACK HACK HACK
 * Check against an exported symbol as a sanity check against follow_page() moving. 
 */
int 
(* get_user_pages_ptr)(struct task_struct *tsk, struct mm_struct *mm,
                unsigned long start, int len, int write, int force,
                struct page **pages, struct vm_area_struct **vmas) = (void *) 0x0; /* get_user_pages_ptr */

#ifdef CONFIG_X86_4G
/* With a 4G kernel/user split, user pages are not directly
 * accessible from the kernel, so don't try
 */
static int pages_present(unsigned long head)
{
	return 0;
}
#else
/* check that the page(s) containing the frame head are present */
static int pages_present(unsigned long head)
{
	struct mm_struct * mm = current->mm;

	if(!follow_page_ptr || !get_user_pages_ptr)
		return 0;

	if(get_user_pages_ptr != &get_user_pages)
		return 0;

	/* FIXME: only necessary once per page */
	if (!follow_page_ptr(mm, head, 0))
		return 0;

	return ((unsigned long) follow_page_ptr(mm, head + sizeof(unsigned long), 0) != 0);
}
#endif
#endif

#endif // RRPROFILE

static struct frame_head *
dump_kernel_backtrace(struct frame_head * head)
{
	oprofile_add_trace(head->ret);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= head->ebp)
		return NULL;

	return head->ebp;
}

#ifdef RRPROFILE

#ifdef CONFIG_X86_64
unsigned long
dump_user64_backtrace(unsigned long head)
{
	struct frame_head64 bufhead[2];

	/* Also check accessibility of one struct frame_head beyond */
	if (!access_ok(VERIFY_READ, head, sizeof(bufhead)))
		return 0;
	if (__copy_from_user_inatomic(bufhead, (void *)head, sizeof(bufhead)))
		return 0;

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= bufhead[0].ebp_ptr)
		return 0;

	/* Be conservative. Only if the previous rbp value is valid do we add
	 * the return address. 
	 */
	oprofile_add_trace(bufhead[0].ret);

	return bufhead[0].ebp_ptr;
}
#endif

unsigned int
dump_user32_backtrace(unsigned int head)
{
	struct frame_head32 bufhead[2];
	
	/* Also check accessibility of one struct frame_head beyond */
	if (!access_ok(VERIFY_READ, head, sizeof(bufhead)))
		return 0;
	if (__copy_from_user_inatomic(bufhead, (void *)(long)head, sizeof(bufhead)))
		return 0;

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= bufhead[0].ebp_ptr)
		return 0;
	
	/* Be conservative. Only if the previous rbp value is valid do we add
	 * the return address. 
	 */
	oprofile_add_trace(bufhead[0].ret);
	
	return bufhead[0].ebp_ptr;
}

#else

static struct frame_head *
dump_user_backtrace(struct frame_head * head)
{
	struct frame_head bufhead[2];

	/* Also check accessibility of one struct frame_head beyond */
	if (!access_ok(VERIFY_READ, head, sizeof(bufhead)))
		return NULL;
	if (__copy_from_user_inatomic(bufhead, head, sizeof(bufhead)))
		return NULL;

	oprofile_add_trace(bufhead[0].ret);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= bufhead[0].ebp)
		return NULL;

	return bufhead[0].ebp;
}

#endif // RRPROFILE

/*
 * |             | /\ Higher addresses
 * |             |
 * --------------- stack base (address of current_thread_info)
 * | thread info |
 * .             .
 * |    stack    |
 * --------------- saved regs->ebp value if valid (frame_head address)
 * .             .
 * --------------- saved regs->rsp value if x86_64
 * |             |
 * --------------- struct pt_regs * stored on stack if 32-bit
 * |             |
 * .             .
 * |             |
 * --------------- %esp
 * |             |
 * |             | \/ Lower addresses
 *
 * Thus, regs (or regs->rsp for x86_64) <-> stack base restricts the
 * valid(ish) ebp values. Note: (1) for x86_64, NMI and several other
 * exceptions use special stacks, maintained by the interrupt stack table
 * (IST). These stacks are set up in trap_init() in
 * arch/x86_64/kernel/traps.c. Thus, for x86_64, regs now does not point
 * to the kernel stack; instead, it points to some location on the NMI
 * stack. On the other hand, regs->rsp is the stack pointer saved when the
 * NMI occurred. (2) For 32-bit, regs->esp is not valid because the
 * processor does not save %esp on the kernel stack when interrupts occur
 * in the kernel mode.
 */
#ifdef CONFIG_FRAME_POINTER
static int valid_kernel_stack(struct frame_head * head, struct pt_regs * regs)
{
	unsigned long headaddr = (unsigned long)head;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,30)
	unsigned long stack = kernel_stack_pointer(regs);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	unsigned long stack = kernel_trap_sp(regs);
#else
#ifdef CONFIG_X86_64
	unsigned long stack = (unsigned long)regs->rsp;
#else
	unsigned long stack = (unsigned long)regs;
#endif
#endif
	unsigned long stack_base = (stack & ~(THREAD_SIZE - 1)) + THREAD_SIZE;

	return headaddr > stack && headaddr < stack_base;
}
#else
/* without fp, it's just junk */
static int valid_kernel_stack(struct frame_head * head, struct pt_regs * regs)
{
	return 0;
}
#endif


void
x86_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	struct frame_head *head;
#ifdef RRPROFILE
	unsigned int       head32;
#ifdef CONFIG_X86_64
	unsigned long      head64;
#endif
#endif // RRPROFILE

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	head = (struct frame_head *)frame_pointer(regs);
#else
#ifdef CONFIG_X86_64
	head = (struct frame_head *)regs->rbp;
#else
	head = (struct frame_head *)regs->ebp;
#endif
#endif

#ifdef RRPROFILE
	if (!user_mode(regs)) {
#else
	if (!user_mode_vm(regs)) {
#endif // RRPROFILE
		while (depth-- && valid_kernel_stack(head, regs))
			head = dump_kernel_backtrace(head);
		return;
	}
	
#ifdef RRPROFILE
#ifdef CONFIG_X86_64
	if (!test_thread_flag(TIF_IA32)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
		head64 = frame_pointer(regs);
#else
		head64 = regs->rbp;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
		while (depth-- && head64)
			head64 = dump_user64_backtrace(head64);
#else
		if (!spin_trylock(&current->mm->page_table_lock))
			return;
		while (depth-- && head64 && pages_present(head64))
			head64 = dump_user64_backtrace(head64);
		spin_unlock(&current->mm->page_table_lock);
#endif
		return;
	}

#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
	head32 = frame_pointer(regs);
#else
#ifdef CONFIG_X86_64
	head32 = regs->rbp;
#else
	head32 = regs->ebp;
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	while(depth-- && head32)
		head32 = dump_user32_backtrace(head32);
#else
	if (!spin_trylock(&current->mm->page_table_lock))
		return;
	while(depth-- && head32 && pages_present((unsigned long) head32))
		head32 = dump_user32_backtrace(head32);
	spin_unlock(&current->mm->page_table_lock);
#endif

#else

	while (depth-- && head)
		head = dump_user_backtrace(head);

#endif // RRPROFILE
}

