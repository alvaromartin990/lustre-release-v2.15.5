// ===== READER PROGRAM (save as cxl_reader.c - RUN ON S7) =====
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include "cxl_shared_test.h"

int main() {
    printf("=== CXL Reader (S7) ===\n");
    
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
    printf("⏳ Waiting for data from S6...\n");
    
    // Poll for data with timeout
    int attempts = 0;
    const int max_attempts = 30;  // 30 seconds timeout
    
    while (attempts < max_attempts) {
        cxl_read_barrier();  // Ensure fresh read from CXL
        
        if (shared->magic == CXL_MAGIC && shared->sequence > 0) {
            printf("\n🎉 SUCCESS! Data received from S6:\n");
            printf("  Magic: 0x%lx (valid)\n", shared->magic);
            printf("  Writer: S%lu\n", shared->writer_id);
            printf("  Sequence: %lu\n", shared->sequence);
            printf("  Message: %s\n", shared->message);
            printf("  Timestamp: %lu\n", shared->timestamp);
            
            // Write response back to S6
            shared->sequence = 2;
            strcpy((char*)shared->message, "ACK from S7 - CXL communication works!");
            cxl_write_barrier(shared, sizeof(cxl_shared_data_t));
            
            printf("\n✓ Response sent back to S6\n");
            break;
        }
        
        printf(".");
        fflush(stdout);
        sleep(1);
        attempts++;
    }
    
    if (attempts >= max_attempts) {
        printf("\n❌ Timeout: No data received from S6\n");
        printf("Current memory state:\n");
        printf("  Magic: 0x%lx (expected: 0x%lx)\n", shared->magic, CXL_MAGIC);
        printf("  Sequence: %lu\n", shared->sequence);
    }
    
    // Cleanup
    if (munmap(shared, CXL_TEST_SIZE) == -1) {
        perror("Failed to unmap CXL memory");
    }
    close(fd);
    return (attempts >= max_attempts) ? 1 : 0;
}