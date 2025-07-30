#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

// Mock OBD macros for demonstration (normally from obd_support.h)
#define OBD_ALLOC_PTR_ARRAY_LARGE(ptr, n) \
    do { \
        (ptr) = kmalloc((n) * sizeof(*(ptr)), GFP_KERNEL); \
    } while(0)

#define OBD_FREE_PTR_ARRAY_LARGE(ptr, n) \
    do { \
        if (ptr) { \
            kfree(ptr); \
            (ptr) = NULL; \
        } \
    } while(0)

struct my_data {
    int value;
    char name[32];
};

static int my_function(void)
{
    struct my_data *data_array;
    int array_size = 50;  // Large array
    int i;
    
    // Allocate large array
    OBD_ALLOC_PTR_ARRAY_LARGE(data_array, array_size);
    if (data_array == NULL) {
        printk(KERN_ERR "Failed to allocate array\n");
        return -ENOMEM;
    }
    
    // Initialize the array
    for (i = 0; i < array_size; i++) {
        data_array[i].value = i;
        snprintf(data_array[i].name, sizeof(data_array[i].name), "item_%d", i);
    }
    
    // Use your array here...
    printk(KERN_INFO "Successfully allocated and initialized array of %d elements\n", array_size);
    
    // Print first few elements as example
    for (i = 0; i < 3 && i < array_size; i++) {
        printk(KERN_INFO "Element %d: value=%d, name=%s\n", i, data_array[i].value, data_array[i].name);
    }
    
    // Free the array
    OBD_FREE_PTR_ARRAY_LARGE(data_array, array_size);
    
    return 0;
}

static int __init test_module_init(void)
{
    printk(KERN_INFO "Test module loaded\n");
    return my_function();
}

static void __exit test_module_exit(void)
{
    printk(KERN_INFO "Test module unloaded\n");
}

module_init(test_module_init);
module_exit(test_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test Author");
MODULE_DESCRIPTION("Test module for OBD_ALLOC_PTR_ARRAY_LARGE");
MODULE_VERSION("1.0");

