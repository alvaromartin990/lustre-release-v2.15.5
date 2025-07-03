#!/usr/bin/env python3

import re
import sys
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
from collections import defaultdict

def parse_timing_data(log_file):
    """Parse the MDT timing data from the log file supporting both old and new formats."""
    data = defaultdict(list)
    detailed_data = []  # Store detailed records with MDT/Node info
    
    # Pattern for new format: MDT_TIMING: [MDT:lustre-MDT0000 Node:0] Operation OPEN_FILE_OP (1) took 123 microseconds
    new_pattern = re.compile(r'MDT_TIMING: \[MDT:([^\s]+) Node:(\d+)\] Operation (\w+) \((\d+)\) took (\d+) microseconds')
    
    # Pattern for old format: MDT_TIMING: Operation OPEN_FILE_OP (1) took 123 microseconds
    old_pattern = re.compile(r'MDT_TIMING: Operation (\w+) \((\d+)\) took (\d+) microseconds')
    
    with open(log_file, 'r') as f:
        for line_num, line in enumerate(f, 1):
            # Try new format first
            match = new_pattern.search(line)
            if match:
                mdt_name = match.group(1)
                node_id = int(match.group(2))
                op_name = match.group(3)
                op_code = int(match.group(4))
                time_us = int(match.group(5))
                
                # Store in simple format for backward compatibility
                data[op_name].append(time_us)
                
                # Store detailed information
                detailed_data.append({
                    'line_num': line_num,
                    'mdt_name': mdt_name,
                    'node_id': node_id,
                    'operation': op_name,
                    'op_code': op_code,
                    'time_us': time_us,
                    'format': 'new'
                })
                continue
            
            # Try old format
            match = old_pattern.search(line)
            if match:
                op_name = match.group(1)
                op_code = int(match.group(2))
                time_us = int(match.group(3))
                
                # Store in simple format
                data[op_name].append(time_us)
                
                # Store detailed information (no MDT/Node info available)
                detailed_data.append({
                    'line_num': line_num,
                    'mdt_name': 'unknown',
                    'node_id': -1,
                    'operation': op_name,
                    'op_code': op_code,
                    'time_us': time_us,
                    'format': 'old'
                })
    
    return data, detailed_data

def plot_timing_data(data, detailed_data, output_file='mdt_timing_analysis.png'):
    """Create visualizations of the MDT timing data."""
    # Prepare basic data for plotting
    op_names = []
    op_counts = []
    op_avg = []
    op_min = []
    op_max = []
    
    for op_name, times in data.items():
        if not times:
            continue
        op_names.append(op_name)
        op_counts.append(len(times))
        op_avg.append(np.mean(times))
        op_min.append(min(times))
        op_max.append(max(times))
    
    # Analyze detailed data
    df = pd.DataFrame(detailed_data)
    has_mdt_info = len(df[df['format'] == 'new']) > 0
    
    # Create figure with appropriate number of subplots
    if has_mdt_info:
        fig, axes = plt.subplots(3, 2, figsize=(15, 18))
        fig.suptitle('Enhanced MDT Operation Timing Analysis', fontsize=16)
    else:
        fig, axes = plt.subplots(2, 2, figsize=(15, 12))
        fig.suptitle('MDT Operation Timing Analysis', fontsize=16)
        axes = [axes[0], axes[1]]  # Flatten for consistent indexing
    
    # Plot 1: Average time by operation type (bar chart)
    ax1 = axes[0][0]
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
                     xytext=(0, 3),
                     textcoords="offset points",
                     ha='center', va='bottom')
    
    # Plot 2: Operation count (bar chart)
    ax2 = axes[0][1]
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
                     xytext=(0, 3),
                     textcoords="offset points",
                     ha='center', va='bottom')
    
    # Plot 3: Min/Max/Avg comparison (bar chart)
    ax3 = axes[1][0]
    x_pos = np.arange(len(op_names))
    width = 0.25
    
    bars1 = ax3.bar(x_pos - width, op_min, width, label='Min', alpha=0.8)
    bars2 = ax3.bar(x_pos, op_avg, width, label='Avg', alpha=0.8)
    bars3 = ax3.bar(x_pos + width, op_max, width, label='Max', alpha=0.8)
    
    ax3.set_title('Min/Avg/Max Time Comparison')
    ax3.set_xlabel('Operation Type')
    ax3.set_ylabel('Time (μs)')
    ax3.set_xticks(x_pos)
    ax3.set_xticklabels(op_names)
    ax3.tick_params(axis='x', rotation=45)
    ax3.legend()
    ax3.grid(axis='y', linestyle='--', alpha=0.7)
    
    # Plot 4: Box plot of timing distributions
    ax4 = axes[1][1]
    data_for_boxplot = [data[op] for op in op_names]
    ax4.boxplot(data_for_boxplot, labels=op_names)
    ax4.set_title('Timing Distribution')
    ax4.set_xlabel('Operation Type')
    ax4.set_ylabel('Time (μs)')
    ax4.tick_params(axis='x', rotation=45)
    ax4.grid(axis='y', linestyle='--', alpha=0.7)
    
    # Additional plots if we have MDT/Node information
    if has_mdt_info and len(axes) > 2:
        # Plot 5: Operations by MDT
        ax5 = axes[2][0]
        mdt_counts = df[df['format'] == 'new'].groupby('mdt_name').size()
        if len(mdt_counts) > 0:
            bars = ax5.bar(mdt_counts.index, mdt_counts.values)
            ax5.set_title('Operations by MDT')
            ax5.set_xlabel('MDT Name')
            ax5.set_ylabel('Operation Count')
            ax5.tick_params(axis='x', rotation=45)
            ax5.grid(axis='y', linestyle='--', alpha=0.7)
            
            # Add value labels
            for bar in bars:
                height = bar.get_height()
                ax5.annotate(f'{int(height)}',
                           xy=(bar.get_x() + bar.get_width() / 2, height),
                           xytext=(0, 3),
                           textcoords="offset points",
                           ha='center', va='bottom')
        
        # Plot 6: Average timing by MDT and operation
        ax6 = axes[2][1]
        if len(df[df['format'] == 'new']) > 0:
            pivot_data = df[df['format'] == 'new'].groupby(['mdt_name', 'operation'])['time_us'].mean().unstack(fill_value=0)
            if not pivot_data.empty:
                pivot_data.plot(kind='bar', ax=ax6, rot=45)
                ax6.set_title('Average Timing by MDT and Operation')
                ax6.set_xlabel('MDT Name')
                ax6.set_ylabel('Average Time (μs)')
                ax6.legend(title='Operation', bbox_to_anchor=(1.05, 1), loc='upper left')
                ax6.grid(axis='y', linestyle='--', alpha=0.7)
    
    plt.tight_layout()
    plt.subplots_adjust(top=0.92)
    plt.savefig(output_file, dpi=100, bbox_inches='tight')
    print(f"Plot saved to {output_file}")
    
    # Create comprehensive statistics
    stats_df = pd.DataFrame({
        'Operation': op_names,
        'Count': op_counts,
        'Min (μs)': op_min,
        'Avg (μs)': op_avg,
        'Max (μs)': op_max,
        'Std Dev (μs)': [np.std(data[op]) for op in op_names]
    })
    
    # Save statistics
    stats_file = 'mdt_timing_stats.csv'
    stats_df.to_csv(stats_file, index=False)
    print(f"Statistics saved to {stats_file}")
    
    # Save detailed data if we have MDT info
    if has_mdt_info:
        detailed_file = 'mdt_timing_detailed.csv'
        df.to_csv(detailed_file, index=False)
        print(f"Detailed data saved to {detailed_file}")
    
    # Print comprehensive statistics
    print("\nMDT Operation Timing Statistics:")
    print("=" * 50)
    print(stats_df.to_string(index=False))
    
    if has_mdt_info:
        print(f"\nEnhanced Analysis (with MDT/Node information):")
        print("=" * 50)
        
        # MDT-specific statistics
        mdt_stats = df[df['format'] == 'new'].groupby('mdt_name').agg({
            'time_us': ['count', 'mean', 'min', 'max', 'std'],
            'operation': lambda x: len(x.unique())
        }).round(2)
        mdt_stats.columns = ['Count', 'Avg (μs)', 'Min (μs)', 'Max (μs)', 'Std Dev (μs)', 'Unique Ops']
        print(f"\nStatistics by MDT:")
        print(mdt_stats.to_string())
        
        # Node-specific statistics
        node_stats = df[df['format'] == 'new'].groupby('node_id').agg({
            'time_us': ['count', 'mean', 'min', 'max', 'std']
        }).round(2)
        node_stats.columns = ['Count', 'Avg (μs)', 'Min (μs)', 'Max (μs)', 'Std Dev (μs)']
        print(f"\nStatistics by Node:")
        print(node_stats.to_string())
        
        # Operation distribution by MDT
        print(f"\nOperation Distribution by MDT:")
        op_dist = df[df['format'] == 'new'].groupby(['mdt_name', 'operation']).size().unstack(fill_value=0)
        print(op_dist.to_string())
    
    return stats_df, df if has_mdt_info else None

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <log_file> [output_image]")
        print("Example: python plot_mdt_timing.py /tmp/mdt_timing_test_1747265062.log")
        print()
        print("Supported log formats:")
        print("  Old: MDT_TIMING: Operation OPEN (1) took 123 microseconds")
        print("  New: MDT_TIMING: [MDT:lustre-MDT0000 Node:0] Operation OPEN (1) took 123 microseconds")
        sys.exit(1)
    
    log_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'mdt_timing_analysis.png'
    
    print(f"Parsing timing data from: {log_file}")
    data, detailed_data = parse_timing_data(log_file)
    
    if not data:
        print(f"No timing data found in {log_file}")
        print("Expected format examples:")
        print("  MDT_TIMING: Operation OPEN (1) took 123 microseconds")
        print("  MDT_TIMING: [MDT:lustre-MDT0000 Node:0] Operation OPEN (1) took 123 microseconds")
        sys.exit(1)
    
    print(f"Found {len(detailed_data)} timing entries")
    new_format_count = len([d for d in detailed_data if d['format'] == 'new'])
    old_format_count = len([d for d in detailed_data if d['format'] == 'old'])
    
    print(f"  - New format (with MDT/Node): {new_format_count}")
    print(f"  - Old format: {old_format_count}")
    
    stats_df, detailed_df = plot_timing_data(data, detailed_data, output_file)
    
    print(f"\nAnalysis complete! Check the generated files:")
    print(f"  - Visualization: {output_file}")
    print(f"  - Basic statistics: mdt_timing_stats.csv")
    if detailed_df is not None:
        print(f"  - Detailed data: mdt_timing_detailed.csv")

if __name__ == "__main__":
    main()
