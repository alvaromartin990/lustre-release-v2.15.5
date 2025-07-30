#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ultra minimal test");
MODULE_VERSION("1.0");

static int __init ultra_minimal_init(void)
{
    printk(KERN_INFO "ultra_minimal: Hello from kernel module\n");
    return 0;
}

static void __exit ultra_minimal_exit(void)
{
    printk(KERN_INFO "ultra_minimal: Goodbye from kernel module\n");
}

module_init(ultra_minimal_init);
module_exit(ultra_minimal_exit);
