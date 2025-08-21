#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    const char *dax_device_path = "/dev/dax0.0";
    size_t mapping_size = 128UL * 1024 * 1024 * 1024; // 128GB
    
    // Open DAX device
    int fd = open(dax_device_path, O_RDWR);
    if (fd == -1) {
        perror("Error opening DAX device");
        return 1;
    }
    
    // Memory-map the shareable region
    void *shared_addr = mmap(NULL, mapping_size, 
                           PROT_READ | PROT_WRITE, 
                           MAP_SHARED, fd, 0);
    if (shared_addr == MAP_FAILED) {
        perror("Error memory-mapping DAX device");
        close(fd);
        return 1;
    }
    
    // Use shared memory
    strcpy((char *)shared_addr, "Hello CXL Shared Memory!");
    
    // IMPORTANT: Flush and fence for CXL
    __builtin_ia32_mfence();  // Memory fence
    __builtin_ia32_clflush(shared_addr);  // Cache flush
    
    // Cleanup
    munmap(shared_addr, mapping_size);
    close(fd);
    return 0;
}