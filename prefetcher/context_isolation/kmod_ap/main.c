// SPDX-License-Identifier: GPL-3.0-only
/**
 * Run arbitrary code *As Privileged*
 .*
 * DANGER!DANGER!DANGER!DANGER!DANGER!DANGER!DANGER!
 * This kernel module will blindly jump to a user-provided pointer and execute
 * from there. You can completely break your system with this - enjoy!
 * DANGER!DANGER!DANGER!DANGER!DANGER!DANGER!DANGER!
 */
// clang-format off
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <asm/tlbflush.h>
#include <linux/uaccess.h>
#include "./ap_ioctl.h"

static struct proc_dir_entry *procfs_file;

static unsigned long cr4;

static long handle_ioctl(struct file *filp, unsigned int request,
                         unsigned long argp)
{
    struct ap_payload *ap;
    unsigned long old_cr4, new_cr4;

    /* Work with the live CR4, not the snapshot from init. */
    old_cr4 = __read_cr4();
    new_cr4 = old_cr4;
    if (new_cr4 & (X86_CR4_SMEP | X86_CR4_SMAP))
        new_cr4 &= ~(X86_CR4_SMEP | X86_CR4_SMAP);

    switch (request) {
    case AP_IOCTL_RUN:
        ap = (struct ap_payload *)argp;
        preempt_disable();
        if (new_cr4 != old_cr4) {
            asm volatile("mov %0, %%cr4" :: "r" (new_cr4) : "memory");
            pr_info("try to disable smep and smap\n");
        }

        /* Verify current CR4 state after the write. */
        if (__read_cr4() & (X86_CR4_SMEP | X86_CR4_SMAP))
            pr_info("SMEP/SMAP still enabled (likely blocked by hypervisor)\n");
        else
            pr_info("SMEP/SMAP disabled\n");
        
        pr_info("jump to %px, and args is %px\n", (void *)ap->fptr, (void *)ap->data);
        // ciao, good luck.
        ap->fptr(ap->data);
        /* Restore original CR4. */
        if (new_cr4 != old_cr4)
            asm volatile("mov %0, %%cr4" :: "r" (old_cr4) : "memory");

        preempt_enable();
        break;
    default:
        return -ENOIOCTLCMD;
    }
    return 0;
}

static struct proc_ops pops = {
    .proc_ioctl = handle_ioctl,
    .proc_open = nonseekable_open,
#ifdef no_llseek
    .proc_lseek = no_llseek,
#else
    .proc_lseek = noop_llseek,
#endif
};

static int ap_init(void)
{
    procfs_file = proc_create(PROC_AP, 0, NULL, &pops);
    cr4 = __read_cr4();
    if (cr4 & (X86_CR4_SMEP|X86_CR4_SMAP)) {
        pr_info("You will need to update cr4 (VMEXIT) to disable smep\n");
    }

    pr_info("handle @ %lx\n", (unsigned long) handle_ioctl);
    return 0;
}

static void ap_exit(void)
{
    proc_remove(procfs_file);
}

module_init(ap_init);
module_exit(ap_exit);
MODULE_LICENSE("GPL");
