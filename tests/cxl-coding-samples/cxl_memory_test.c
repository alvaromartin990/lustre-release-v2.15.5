#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

int main() {
    // Use the device path from your environment
    const char *dax_device_path = "/dev/dax0.0";
    
    // Start with smaller size for testing (2MB - minimum alignment)
    size_t mapping_size = 2 * 1024 * 1024; // 2MB
    
    printf("Testing CXL Memory Mapping on S6\n");
    printf("Device: %s\n", dax_device_path);
    printf("Mapping size: %zu bytes (%zu MB)\n", mapping_size, mapping_size/(1024*1024));
    
    // 1. Open the DAX device
    printf("\n1. Opening DAX device...\n");
    int fd = open(dax_device_path, O_RDWR);
    if (fd == -1) {
        printf("Error opening DAX device: %s\n", strerror(errno));
        printf("Checking if device exists...\n");
        if (access(dax_device_path, F_OK) == -1) {
            printf("Device %s does not exist\n", dax_device_path);
        } else {
            printf("Device exists but cannot be opened (permission issue?)\n");
        }
        return 1;
    }
    printf("✓ DAX device opened successfully (fd=%d)\n", fd);
    
    // 2. Memory-map the DAX device
    printf("\n2. Memory-mapping the device...\n");
    void *dax_addr = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (dax_addr == MAP_FAILED) {
        printf("Error memory-mapping DAX device: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("✓ Memory mapped successfully at address: %p\n", dax_addr);
    
    // 3. Test write operation
    printf("\n3. Testing write operation...\n");
    const char* test_message = "Hello CXL S6 Instance!";
    strcpy((char *)dax_addr, test_message);
    
    // CRITICAL: Flush and fence for CXL shared memory
    printf("   Applying memory fence and cache flush...\n");
    __builtin_ia32_mfence();  // Memory fence
    __builtin_ia32_clflush(dax_addr);  // Cache flush
    printf("✓ Write operation completed with proper flushing\n");
    
    // 4. Test read operation
    printf("\n4. Testing read operation...\n");
    char read_buffer[256];
    strcpy(read_buffer, (char *)dax_addr);
    printf("✓ Read from CXL memory: '%s'\n", read_buffer);
    
    // 5. Verify data integrity
    printf("\n5. Verifying data integrity...\n");
    if (strcmp(test_message, read_buffer) == 0) {
        printf("✓ Data integrity verified - read matches write\n");
    } else {
        printf("✗ Data integrity failed - read does not match write\n");
    }
    
    // 6. Performance test (simple)
    printf("\n6. Simple performance test...\n");
    clock_t start = clock();
    for (int i = 0; i < 1000; i++) {
        ((volatile int*)dax_addr)[i % (mapping_size/sizeof(int))] = i;
    }
    __builtin_ia32_mfence();
    clock_t end = clock();
    double cpu_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("✓ 1000 write operations completed in %f seconds\n", cpu_time);
    
    // 7. Cleanup
    printf("\n7. Cleaning up...\n");
    if (munmap(dax_addr, mapping_size) == -1) {
        printf("Warning: Error unmapping memory: %s\n", strerror(errno));
    } else {
        printf("✓ Memory unmapped successfully\n");
    }
    
    if (close(fd) == -1) {
        printf("Warning: Error closing DAX device: %s\n", strerror(errno));
    } else {
        printf("✓ DAX device closed successfully\n");
    }
    
    printf("\n🎉 CXL Memory Mapping Test Completed Successfully!\n");
    return 0;
}