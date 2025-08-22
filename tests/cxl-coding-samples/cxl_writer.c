// ===== WRITER PROGRAM (save as cxl_writer.c - RUN ON S6) =====
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "cxl_shared_test.h"

int main() {
    printf("=== CXL Writer (S6) ===\n");
    
    // Open CXL device
    int fd = open(CXL_DEVICE_PATH, O_RDWR);
    if (fd == -1) {
        perror("Failed to open CXL device");
        return 1;
    }
    
    // Map shared memory
    cxl_shared_data_t *shared = mmap(NULL, CXL_TEST_SIZE, 
                                     PROT_READ | PROT_WRITE, 
                                     MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("Failed to map CXL memory");
        close(fd);
        return 1;
    }
    
    printf("✓ CXL memory mapped at %p\n", shared);
    
    // Write test data
    shared->magic = CXL_MAGIC;
    shared->writer_id = 6;  // S6 identifier
    shared->timestamp = time(NULL);
    shared->sequence = 1;
    strcpy((char*)shared->message, "Hello from S6 via CXL shared memory!");
    
    // CRITICAL: Ensure data is written to CXL memory
    // The cxl_write_barrier function enforces a memory write barrier to guarantee
    // that all changes to the shared structure are visible to other processes
    // accessing the CXL shared memory, preventing reordering or caching issues.
    cxl_write_barrier(shared, sizeof(cxl_shared_data_t));
    
    printf("✓ Data written to CXL shared memory:\n");
    printf("  Magic: 0x%lx\n", shared->magic);
    printf("  Writer: S%lu\n", shared->writer_id);
    printf("  Sequence: %lu\n", shared->sequence);
    printf("  Message: %s\n", shared->message);
    printf("  Timestamp: %lu\n", shared->timestamp);
    
    printf("\n🚀 Ready for S7 to read! Press Enter to exit...");
    getchar();
    
    // Cleanup
    munmap(shared, CXL_TEST_SIZE);
    close(fd);
    return 0;
}