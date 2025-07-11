#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

struct lu_fid {
    uint64_t f_seq;
    uint32_t f_oid;
    uint32_t f_ver;
};

struct osd_inode_id {
    uint32_t oii_ino;
    uint32_t oii_gen;
};

struct osd_idmap_cache {
    struct lu_fid oic_fid;
    struct osd_inode_id oic_lid;
    int oic_remote;
};

void generate_random_fid(struct lu_fid *fid) {
    fid->f_seq = rand() % 0xFFFFFFFFFFULL;
    fid->f_oid = rand() % 0x100000;
    fid->f_ver = rand() % 100;
}

void generate_random_inode_id(struct osd_inode_id *id) {
    id->oii_ino = rand() % 0x1000000;
    id->oii_gen = rand() % 0x10000;
}

void init_random_idmap_cache_entry(struct osd_idmap_cache *idc, int index) {
    generate_random_fid(&idc->oic_fid);
    generate_random_inode_id(&idc->oic_lid);
    idc->oic_remote = rand() % 2;

    printf("Entry %d initialized - FID: [%llu:%u:%u], Inode: %u/%u, Remote: %d\n",
           index,
           (unsigned long long)idc->oic_fid.f_seq,
           idc->oic_fid.f_oid,
           idc->oic_fid.f_ver,
           idc->oic_lid.oii_ino,
           idc->oic_lid.oii_gen,
           idc->oic_remote);
}

void test_obd_alloc_idmap_cache(int array_size) {
    printf("\nTesting allocation of %d osd_idmap_cache entries\n", array_size);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    struct osd_idmap_cache *idc_array = malloc(array_size * sizeof(struct osd_idmap_cache));
    if (!idc_array) {
        fprintf(stderr, "Allocation failed!\n");
        return;
    }

    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i], i);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    long alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("Successfully allocated %d entries in %ld ns\n", array_size, alloc_time_ns);

    printf("Sample verification:\n");
    for (int i = 0; i < (array_size < 3 ? array_size : 3); i++) {
        struct osd_idmap_cache *entry = &idc_array[i];
        printf("  Entry %d: FID=[%llu:%u:%u], Inode=%u/%u, Remote=%d\n",
               i,
               (unsigned long long)entry->oic_fid.f_seq,
               entry->oic_fid.f_oid,
               entry->oic_fid.f_ver,
               entry->oic_lid.oii_ino,
               entry->oic_lid.oii_gen,
               entry->oic_remote);
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    free(idc_array);
    clock_gettime(CLOCK_MONOTONIC, &end);
    long free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("Successfully freed %d entries in %ld ns\n", array_size, free_time_ns);
}

int main() {
    srand(time(NULL));
    test_obd_alloc_idmap_cache(10);
    return 0;
}
