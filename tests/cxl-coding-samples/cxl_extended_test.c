#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

int main() {
    const char *dax_device_path = "/dev/dax0.0";
    // Test with larger size (adjust based on your 128GB capacity)
    size_t mapping_size = 64 * 1024 * 1024; // 64MB
    
    printf("Extended CXL Memory Test on S6\n");
    printf("Mapping size: %zu MB\n", mapping_size/(1024*1024));
    
    int fd = open(dax_device_path, O_RDWR);
    if (fd == -1) {
        printf("Error: %s\n", strerror(errno));
        return 1;
    }
    
    void *dax_addr = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (dax_addr == MAP_FAILED) {
        printf("Error mapping: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    
    // Test pattern writing
    printf("Writing test patterns...\n");
    int *int_ptr = (int*)dax_addr;
    size_t int_count = mapping_size / sizeof(int);
    
    for (size_t i = 0; i < int_count && i < 1000000; i++) {
        int_ptr[i] = (int)(i ^ 0xDEADBEEF);
    }
    
    // Flush after writing
    __builtin_ia32_mfence();
    for (size_t i = 0; i < mapping_size; i += 64) {
        __builtin_ia32_clflush((char*)dax_addr + i);
    }
    
    // Verify patterns
    printf("Verifying test patterns...\n");
    int errors = 0;
    for (size_t i = 0; i < int_count && i < 1000000; i++) {
        int expected = (int)(i ^ 0xDEADBEEF);
        if (int_ptr[i] != expected) {
            errors++;
            if (errors < 10) {
                printf("Error at offset %zu: expected 0x%x, got 0x%x\n", 
                       i, expected, int_ptr[i]);
            }
        }
    }
    
    if (errors == 0) {
        printf("✓ All patterns verified successfully!\n");
    } else {
        printf("✗ Found %d errors in pattern verification\n", errors);
    }
    
    munmap(dax_addr, mapping_size);
    close(fd);
    return errors > 0 ? 1 : 0;
}