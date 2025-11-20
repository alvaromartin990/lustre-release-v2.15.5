#!/usr/bin/env python3
"""
Simple plotting script for Lustre mmap timing results.
Reads CSV data and creates basic plots showing mmap allocation overhead.
"""

import sys
import csv
import statistics
from pathlib import Path

try:
    import matplotlib.pyplot as plt
    import numpy as np
except ImportError:
    print("Error: matplotlib/numpy not available.")
    print("Install with: pip install matplotlib numpy")
    sys.exit(1)

def load_timing_data(filename="mmap_timing_results.csv"):
    """Load CSV timing data."""
    data = []
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row['duration_ns'].strip() and row['duration_ns'] != 'duration_ns':
                    row['duration_ns'] = float(row['duration_ns'])
                    row['size_bytes'] = int(row['size_bytes']) if row['size_bytes'] else 0
                    data.append(row)
        return data
    except FileNotFoundError:
        print(f"Error: {filename} not found")
        return []
    except Exception as e:
        print(f"Error reading {filename}: {e}")
        return []

def plot_timing_results(data):
    """Create plots from timing data."""
    if not data:
        print("No data to plot")
        return
    
    # Group data by component and operation
    components = {}
    for row in data:
        comp = row['component']
        op = row['operation']
        key = f"{comp}_{op}"
        
        if key not in components:
            components[key] = []
        components[key].append(row['duration_ns'])
    
    if not components:
        print("No valid timing data found")
        return
    
    # Create figure with subplots
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    # Plot 1: Average duration by operation type
    op_names = list(components.keys())
    avg_durations = [statistics.mean(components[op]) for op in op_names]
    
    bars = ax1.bar(range(len(op_names)), avg_durations)
    ax1.set_xlabel('Operation Type')
    ax1.set_ylabel('Average Duration (ns)')
    ax1.set_title('MMAP Allocation Timing by Component/Operation')
    ax1.set_xticks(range(len(op_names)))
    ax1.set_xticklabels(op_names, rotation=45, ha='right')
    
    # Add value labels on bars
    for bar, val in zip(bars, avg_durations):
        ax1.text(bar.get_x() + bar.get_width()/2, bar.get_height() + max(avg_durations)*0.01,
               f'{val:.0f}', ha='center', va='bottom', fontsize=9)
    
    # Plot 2: Distribution of timing values
    all_durations = []
    labels = []
    for op, durations in components.items():
        all_durations.extend(durations)
        labels.extend([op] * len(durations))
    
    if len(set(labels)) > 1:
        # Box plot if multiple operation types
        box_data = [components[op] for op in op_names]
        ax2.boxplot(box_data, labels=op_names)
        ax2.set_xticklabels(op_names, rotation=45, ha='right')
    else:
        # Histogram if single operation type
        ax2.hist(all_durations, bins=min(20, len(all_durations)), alpha=0.7)
    
    ax2.set_ylabel('Duration (ns)')
    ax2.set_title('Timing Distribution')
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('mmap_timing_plot.png', dpi=150, bbox_inches='tight')
    print("Plot saved as: mmap_timing_plot.png")

def print_summary(data):
    """Print summary statistics."""
    if not data:
        print("No data to summarize")
        return
    
    print(f"\n=== Timing Analysis Summary ===")
    print(f"Total mmap operations: {len(data)}")
    
    # Group by component
    by_component = {}
    for row in data:
        comp = row['component']
        if comp not in by_component:
            by_component[comp] = []
        by_component[comp].append(row['duration_ns'])
    
    # Overall statistics
    all_durations = [row['duration_ns'] for row in data]
    print(f"\nOverall timing (ns):")
    print(f"  Min: {min(all_durations):.0f}")
    print(f"  Max: {max(all_durations):.0f}")
    print(f"  Mean: {statistics.mean(all_durations):.0f}")
    print(f"  Median: {statistics.median(all_durations):.0f}")
    
    # By component
    for comp, durations in by_component.items():
        print(f"\n{comp.upper()} component:")
        print(f"  Operations: {len(durations)}")
        print(f"  Mean: {statistics.mean(durations):.0f} ns")
        print(f"  Min: {min(durations):.0f} ns") 
        print(f"  Max: {max(durations):.0f} ns")
    
    # Size analysis
    sizes = [row['size_bytes'] for row in data if row['size_bytes'] > 0]
    if sizes:
        print(f"\nAllocation sizes:")
        print(f"  Min: {min(sizes)} bytes")
        print(f"  Max: {max(sizes)} bytes")
        print(f"  Average: {statistics.mean(sizes):.0f} bytes")

def main():
    """Main function."""
    csv_file = "mmap_timing_results.csv"
    
    if not Path(csv_file).exists():
        print(f"Error: {csv_file} not found")
        print("Run ./test_mmap_timing.sh first to generate timing data")
        return
    
    print(f"Loading timing data from: {csv_file}")
    data = load_timing_data(csv_file)
    
    if not data:
        print("No timing data found in CSV file")
        return
    
    print(f"Loaded {len(data)} timing measurements")
    
    plot_timing_results(data)
    print_summary(data)

if __name__ == '__main__':
    main()