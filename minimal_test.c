/*
 * Minimal test module for kernel module building
 * This version removes all complex dependencies
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test");
MODULE_DESCRIPTION("Minimal test module");
MODULE_VERSION("1.0");

static int __init minimal_test_init(void)
{
    printk(KERN_INFO "minimal_test: Module loaded successfully\n");
    return 0;
}

static void __exit minimal_test_exit(void)
{
    printk(KERN_INFO "minimal_test: Module unloaded\n");
}

module_init(minimal_test_init);
module_exit(minimal_test_exit);
