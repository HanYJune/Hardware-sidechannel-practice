// check_smep.c
// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/module.h>
#include <asm/processor.h>   // __read_cr4()

MODULE_LICENSE("GPL");
MODULE_AUTHOR("you");
MODULE_DESCRIPTION("Print CR4 and SMEP bit state");

static int __init check_smep_init(void)
{
    unsigned long cr4 = __read_cr4();
    pr_info("check_smep: CR4 = 0x%lx, SMEP(bit20) = %s\n",
            cr4, (cr4 & (1UL << 20)) ? "ON" : "OFF");
    return 0;
}

static void __exit check_smep_exit(void)
{
    pr_info("check_smep: unloaded\n");
}

module_init(check_smep_init);
module_exit(check_smep_exit);