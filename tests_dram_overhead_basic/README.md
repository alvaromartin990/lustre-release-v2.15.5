# Basic DRAM Overhead Tests

Measures timing overhead of mmap-backed DRAM allocations in Lustre metadata paths.

## Usage Instructions

### 1. Run the test
```bash
cd tests_dram_overhead_basic
./test_mmap_timing.sh
```

### 2. Generate plots
```bash
python3 plot_mmap_timing.py
```

## Output Files
- `mmap_timing_results.csv` - Raw timing data extracted from kernel logs
- `mmap_timing_plot.png` - Visualization of timing results

## What It Measures
The test instruments mmap allocation/deallocation timing in:
- **fid_request.c**: FID client sequence allocation
- **fid_store.c**: FID sequence storage objects  
- **fld_cache.c**: FLD cache structures and entries

Timing is captured via `printk(KERN_ALERT ...)` statements using `ktime_get_ns()`.

## Requirements
- Lustre filesystem (or falls back to `/tmp`)
- Root/sudo access for reading kernel logs (`dmesg`)
- Python3 + matplotlib for plotting

## Notes
- Test runs in seconds with minimal file operations
- Requires modified Lustre kernel modules with timing instrumentation
- May show zero results if instrumented code paths aren't triggered