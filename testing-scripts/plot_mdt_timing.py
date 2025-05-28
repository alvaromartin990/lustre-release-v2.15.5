#!/usr/bin/env python3

import re
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
from collections import defaultdict

def parse_timing_data(log_file):
    """Parse the MDT timing data from the log file."""
    data = defaultdict(list)
    timing_pattern = re.compile(r'MDT_TIMING: Operation (\w+) \((\d+)\) took (\d+) microseconds')
    
    with open(log_file, 'r') as f:
        for line in f:
            match = timing_pattern.search(line)
            if match:
                op_name = match.group(1)
                op_code = int(match.group(2))
                time_us = int(match.group(3))
                data[op_name].append(time_us)
    
    return data

def plot_timing_data(data, output_file='mdt_timing_analysis.png'):
    """Create visualizations of the MDT timing data."""
    # Prepare data for plotting
    op_names = []
    op_times = []
    op_counts = []
    op_avg = []
    op_min = []
    op_max = []
    
    for op_name, times in data.items():
        if not times:
            continue
        op_names.append(op_name)
        op_times.extend([(op_name, t) for t in times])
        op_counts.append(len(times))
        op_avg.append(np.mean(times))
        op_min.append(min(times))
        op_max.append(max(times))
    
    # Create figure with multiple subplots
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('MDT Operation Timing Analysis', fontsize=16)
    
    # Plot 1: Average time by operation type (bar chart)
    ax1 = axes[0, 0]
    bars = ax1.bar(op_names, op_avg)
    ax1.set_title('Average Time by Operation Type')
    ax1.set_xlabel('Operation Type')
    ax1.set_ylabel('Time (μs)')
    ax1.set_yscale('log')  # Use log scale for better visibility
    ax1.tick_params(axis='x', rotation=45)
    ax1.grid(axis='y', linestyle='--', alpha=0.7)
    
    # Add value labels on bars
    for bar in bars:
        height = bar.get_height()
        ax1.annotate(f'{int(height)}',
                     xy=(bar.get_x() + bar.get_width() / 2, height),
                     xytext=(0, 3),  # 3 points vertical offset
                     textcoords="offset points",
                     ha='center', va='bottom', rotation=0)
    
    # Plot 2: Operation count (bar chart)
    ax2 = axes[0, 1]
    bars = ax2.bar(op_names, op_counts)
    ax2.set_title('Operation Count')
    ax2.set_xlabel('Operation Type')
    ax2.set_ylabel('Count')
    ax2.tick_params(axis='x', rotation=45)
    ax2.grid(axis='y', linestyle='--', alpha=0.7)
    
    # Add value labels on bars
    for bar in bars:
        height = bar.get_height()
        ax2.annotate(f'{int(height)}',
                     xy=(bar.get_x() + bar.get_width() / 2, height),
                     xytext=(0, 3),  # 3 points vertical offset
                     textcoords="offset points",
                     ha='center', va='bottom')
    
    # Plot 3: Min/Max/Avg comparison (bar chart with error bars)
    ax3 = axes[1, 0]
    x_pos = np.arange(len(op_names))
    width = 0.25
    
    # Plot min values
    bars1 = ax3.bar(x_pos - width, op_min, width, label='Min')
    # Plot avg values
    bars2 = ax3.bar(x_pos, op_avg, width, label='Avg')
    # Plot max values
    bars3 = ax3.bar(x_pos + width, op_max, width, label='Max')
    
    ax3.set_title('Min/Avg/Max Time Comparison')
    ax3.set_xlabel('Operation Type')
    ax3.set_ylabel('Time (μs)')
    ax3.set_xticks(x_pos)
    ax3.set_xticklabels(op_names)
    ax3.tick_params(axis='x', rotation=45)
    ax3.legend()
    ax3.grid(axis='y', linestyle='--', alpha=0.7)
    
    # Plot 4: Box plot of timing distributions
    ax4 = axes[1, 1]
    data_for_boxplot = [data[op] for op in op_names]
    ax4.boxplot(data_for_boxplot, labels=op_names)
    ax4.set_title('Timing Distribution')
    ax4.set_xlabel('Operation Type')
    ax4.set_ylabel('Time (μs)')
    ax4.tick_params(axis='x', rotation=45)
    ax4.grid(axis='y', linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.subplots_adjust(top=0.92)
    plt.savefig(output_file, dpi=100)
    print(f"Plot saved to {output_file}")
    
    # Create a pandas DataFrame for detailed statistics
    stats_df = pd.DataFrame({
        'Operation': op_names,
        'Count': op_counts,
        'Min (μs)': op_min,
        'Avg (μs)': op_avg,
        'Max (μs)': op_max,
        'Std Dev (μs)': [np.std(data[op]) for op in op_names]
    })
    
    # Save the statistics to a CSV file
    stats_file = 'mdt_timing_stats.csv'
    stats_df.to_csv(stats_file, index=False)
    print(f"Statistics saved to {stats_file}")
    
    # Print statistics to console
    print("\nMDT Operation Timing Statistics:")
    print("--------------------------------")
    print(stats_df.to_string(index=False))
    
    return stats_df

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <log_file> [output_image]")
        print("Example: python plot_mdt_timing.py /tmp/mdt_timing_test_1747265062.log")
        sys.exit(1)
    
    log_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'mdt_timing_analysis.png'
    
    data = parse_timing_data(log_file)
    if not data:
        print(f"No timing data found in {log_file}")
        sys.exit(1)
    
    stats = plot_timing_data(data, output_file)

if __name__ == "__main__":
    main()
