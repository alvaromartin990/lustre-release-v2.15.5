#!/usr/bin/env python3

import matplotlib.pyplot as plt
import re
import numpy as np
from matplotlib.colors import LogNorm
import sys

def parse_log(log_file_content):
    # Regular expression to extract timestamp and operation duration
    pattern = r'\[(\d+\.\d+)\].*took (\d+) microseconds'
    
    timestamps = []
    durations = []
    operations = []
    
    for line in log_file_content.split('\n'):
        match = re.search(pattern, line)
        if match:
            timestamp = float(match.group(1))
            duration = int(match.group(2))
            
            # Extract operation type
            op_match = re.search(r'Operation (\w+)', line)
            if op_match:
                operation = op_match.group(1)
            else:
                operation = "Unknown"
            
            timestamps.append(timestamp)
            durations.append(duration)
            operations.append(operation)
    
    return timestamps, durations, operations

def create_histogram_plot(log_file_content):
    # Parse the log file
    timestamps, durations, operations = parse_log(log_file_content)
    
    if not timestamps:
        print("No timing data found in the log file.")
        return
    
    # Calculate elapsed times from the first operation
    start_time = min(timestamps)
    elapsed_times = [t - start_time for t in timestamps]
    
    # Create figure for the 2D histogram (heatmap)
    plt.figure(figsize=(12, 8))
    
    # Create 2D histogram showing distribution of operation times at each timestamp
    h = plt.hist2d(
        elapsed_times,
        durations,
        bins=[20, 20],  # 20 bins on each axis
        cmap='viridis',
        norm=LogNorm()  # Use log scale for better visibility of distribution
    )
    
    # Add colorbar
    cbar = plt.colorbar(h[3])
    cbar.set_label('Number of Operations')
    
    # Set labels and title
    plt.xlabel('Time Elapsed Since Start (seconds)')
    plt.ylabel('Operation Duration (microseconds)')
    plt.title('Distribution of MDT Operation Durations Over Time')
    
    # Add grid for better readability
    plt.grid(True, linestyle='--', alpha=0.3)
    
    # Save the plot
    plt.tight_layout()
    plt.savefig('mdt_timing_histogram.png')
    
    print("Plot saved as 'mdt_timing_histogram.png'")

def create_comprehensive_analysis(log_file_content):
    # Parse the log file
    timestamps, durations, operations = parse_log(log_file_content)
    
    if not timestamps:
        print("No timing data found in the log file.")
        return
    
    # Calculate elapsed times
    start_time = min(timestamps)
    elapsed_times = [t - start_time for t in timestamps]
    
    # Get unique operations for color coding
    unique_ops = list(set(operations))
    colors = plt.cm.tab10(np.linspace(0, 1, len(unique_ops)))
    color_map = dict(zip(unique_ops, colors))
    
    # Create a figure with multiple plots
    fig, axs = plt.subplots(2, 1, figsize=(12, 10), gridspec_kw={'height_ratios': [1, 1.5]})
    
    # 1. Scatter plot - operations over time colored by operation type
    for i, op in enumerate(unique_ops):
        indices = [j for j, x in enumerate(operations) if x == op]
        axs[0].scatter(
            [elapsed_times[j] for j in indices],
            [durations[j] for j in indices],
            label=op,
            alpha=0.7
        )
    
    axs[0].set_xlabel('Time Elapsed (seconds)')
    axs[0].set_ylabel('Duration (μs)')
    axs[0].set_title('MDT Operation Durations by Type')
    axs[0].legend(title="Operation Type")
    axs[0].grid(True, linestyle='--', alpha=0.7)
    
    # 2. 2D Histogram (heatmap) - distribution of durations over time
    h = axs[1].hist2d(
        elapsed_times,
        durations,
        bins=[20, 20],
        cmap='viridis',
        norm=LogNorm()
    )
    cbar = fig.colorbar(h[3], ax=axs[1])
    cbar.set_label('Operation Count')
    axs[1].set_xlabel('Time Elapsed (seconds)')
    axs[1].set_ylabel('Duration (μs)')
    axs[1].set_title('2D Histogram of Operation Durations Over Time')
    
    # Adjust layout and save
    plt.tight_layout()
    plt.savefig('mdt_timing_comprehensive.png')
    
    print("Plot saved as 'mdt_timing_comprehensive.png'")

# Check for command line arguments
if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <log_file>")
        sys.exit(1)
    
    log_file = sys.argv[1]
    
    try:
        with open(log_file, 'r') as f:
            log_content = f.read()
        
        # Create both the simple histogram and the comprehensive analysis
        create_histogram_plot(log_content)
        create_comprehensive_analysis(log_content)
        print("Analysis complete. Plots have been saved.")
        
    except FileNotFoundError:
        print(f"Log file not found: {log_file}")
        sys.exit(1)
