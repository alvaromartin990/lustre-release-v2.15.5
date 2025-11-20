# Simple DRAM Overhead Test Suite

Fast, lightweight tests for measuring Lustre metadata overhead from mmap-backed DRAM changes.

## Usage Instructions

### 1. Run Tests
```bash
cd tests_dram_overhead_simpler
./test_basic_metadata.sh      # Tests file/dir creation, stat, xattr operations
./test_lookup_performance.sh  # Tests file/dir lookups and access
```

### 2. Plot Results
```bash
python3 plot_simple_results.py
```

### 3. View Output
- CSV files: `basic_metadata_results.csv`, `lookup_performance_results.csv`
- Plot: `simple_results_plot.png`
- Console output shows summary statistics

## Test Details

**test_basic_metadata.sh**:
- 10 file creations, 10 directory creations
- 10 stat operations, 5 extended attribute operations
- Runs in ~2-3 seconds

**test_lookup_performance.sh**:
- Creates 10 test files/directories
- Tests file/directory existence checks, file access, directory listing
- Runs in ~1-2 seconds

## Dependencies
- bash (POSIX-compliant)
- python3 + matplotlib (for plotting)