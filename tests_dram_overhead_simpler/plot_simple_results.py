#!/usr/bin/env python3
"""
Simple plotting script for Lustre DRAM overhead test results.
Reads CSV files and creates basic plots using only standard library + matplotlib.
"""

import sys
import csv
import statistics
from pathlib import Path

try:
    import matplotlib.pyplot as plt
except ImportError:
    print("Error: matplotlib not available. Install with: pip install matplotlib")
    sys.exit(1)

def load_csv_data(filename):
    """Load CSV data and return as list of dictionaries."""
    data = []
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                row['latency_us'] = float(row['latency_us'])
                row['iteration'] = int(row['iteration'])
                data.append(row)
        return data
    except FileNotFoundError:
        print(f"Warning: {filename} not found")
        return []
    except Exception as e:
        print(f"Error reading {filename}: {e}")
        return []

def plot_results(data_files):
    """Create simple plots from the test data."""
    if not data_files:
        print("No data files found")
        return
    
    fig, axes = plt.subplots(1, len(data_files), figsize=(6*len(data_files), 5))
    if len(data_files) == 1:
        axes = [axes]
    
    for idx, filename in enumerate(data_files):
        data = load_csv_data(filename)
        if not data:
            continue
            
        ax = axes[idx]
        
        # Group data by operation type
        operations = {}
        for row in data:
            op = row['operation']
            if op not in operations:
                operations[op] = []
            operations[op].append(row['latency_us'])
        
        # Create bar plot of average latencies
        op_names = list(operations.keys())
        avg_latencies = [statistics.mean(operations[op]) for op in op_names]
        
        bars = ax.bar(range(len(op_names)), avg_latencies)
        ax.set_xlabel('Operation Type')
        ax.set_ylabel('Average Latency (μs)')
        ax.set_title(f'Results from {filename}')
        ax.set_xticks(range(len(op_names)))
        ax.set_xticklabels(op_names, rotation=45, ha='right')
        
        # Add value labels on bars
        for bar, val in zip(bars, avg_latencies):
            ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + max(avg_latencies)*0.01,
                   f'{val:.1f}', ha='center', va='bottom')
    
    plt.tight_layout()
    plt.savefig('simple_results_plot.png', dpi=150, bbox_inches='tight')
    print("Plot saved as: simple_results_plot.png")

def print_summary(data_files):
    """Print summary statistics."""
    print("\n=== Test Results Summary ===")
    
    for filename in data_files:
        data = load_csv_data(filename)
        if not data:
            continue
            
        print(f"\n{filename}:")
        
        operations = {}
        for row in data:
            op = row['operation']
            if op not in operations:
                operations[op] = []
            operations[op].append(row['latency_us'])
        
        for op, latencies in operations.items():
            avg = statistics.mean(latencies)
            min_lat = min(latencies)
            max_lat = max(latencies)
            print(f"  {op:15s}: avg={avg:6.1f}μs, min={min_lat:6.1f}μs, max={max_lat:6.1f}μs, count={len(latencies)}")

def main():
    """Main function."""
    # Look for CSV result files
    csv_files = []
    for pattern in ['*_results.csv', '*results.csv']:
        csv_files.extend(Path('.').glob(pattern))
    
    csv_files = [str(f) for f in csv_files]
    
    if not csv_files:
        print("No CSV result files found. Expected files ending with '_results.csv' or 'results.csv'")
        print("Run the test scripts first:")
        print("  ./test_basic_metadata.sh")
        print("  ./test_lookup_performance.sh")
        return
    
    print(f"Found CSV files: {csv_files}")
    
    plot_results(csv_files)
    print_summary(csv_files)

if __name__ == '__main__':
    main()