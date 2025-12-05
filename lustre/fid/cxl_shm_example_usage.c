#include <fcntl.h>   // For open() flags
#include <stdio.h>   // For perror() and printf()
#include <string.h>  // For strcpy()
#include <sys/mman.h> // For mmap() and munmap()
#include <unistd.h>  // For close()

int main() {
    // Define the DAX device path and mapping size.
    // DAX devices often have alignment requirements, commonly 2MiB.
    const char *dax_device_path = "/dev/dax1.0"; // Adjust if your device is different
    size_t mapping_size = 2 * 1024 * 1024; // 2 MiB, common DAX alignment

    // 1. Open the DAX device
    int fd = open(dax_device_path, O_RDWR); // Open for read and write
    if (fd == -1) {
        perror("Error opening DAX device");
        return 1; // Indicate error
    }

    // 2. Memory-map the DAX device
    void *dax_addr = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (dax_addr == MAP_FAILED) {
        perror("Error memory-mapping DAX device");
        close(fd); // Close the file descriptor if mmap fails
        return 1; // Indicate error
    }

    // 3. Perform read/write operations on the mapped memory
    printf("Writing 'Hello DAX!' to the mapped memory...\n");
    strcpy((char *)dax_addr, "Hello DAX!"); // Write a string to the mapped memory
    // Note: When coping buffers to CXL Shared Memory Pool, you must flush and memory fence to ensure all stores are written and not stored on cacheline only 
    
    printf("Reading from the mapped memory: %s\n", (char *)dax_addr); // Read and print

    // 4. Unmap the memory and close the device
    if (munmap(dax_addr, mapping_size) == -1) {
        perror("Error unmapping memory");
    }
    if (close(fd) == -1) {
        perror("Error closing DAX device");
    }

    printf("Operations completed.\n");
    return 0; // Indicate success
}