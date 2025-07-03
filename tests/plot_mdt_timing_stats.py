#!/usr/bin/env python3

import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import glob

def read_timing_stats(filenames):
    """Read multiple timing stats CSV files and return a combined DataFrame."""
    all_dfs = []
    for i, filename in enumerate(filenames):
        df = pd.read_csv(filename)
        # Add source identifier
        df['Source'] = os.path.basename(filename).replace('mdt_timing_stats', '').replace('.csv', '')
        if 'Source' == '':  # If no distinct name, give it a number
            df['Source'] = f"Test_{i+1}"
        all_dfs.append(df)
    
    return all_dfs

def create_comparison_plot(dfs, output_file='mdt_operation_comparison.png'):
    """Create a bar chart comparing average operation times across all files."""
    plt.figure(figsize=(14, 10))
    
    # Get all unique operations across all files
    all_ops = set()
    for df in dfs:
        all_ops.update(df['Operation'].unique())
    
    all_ops = sorted(list(all_ops))
    
    # Set up plot
    bar_width = 0.8 / len(dfs)
    index = np.arange(len(all_ops))
    
    # Plot each file's data with different colors
    for i, df in enumerate(dfs):
        # Create a dictionary of operation -> average time
        avg_times = {op: 0 for op in all_ops}
        for _, row in df.iterrows():
            avg_times[row['Operation']] = row['Avg (μs)']
        
        # Plot the data
        plt.bar(index + i*bar_width, [avg_times[op] for op in all_ops], 
                bar_width, label=df['Source'].iloc[0])
    
    plt.xlabel('Operation Type', fontsize=12)
    plt.ylabel('Average Time (μs)', fontsize=12)
    plt.title('MDT Operation Average Time Comparison', fontsize=14)
    plt.xticks(index + bar_width*(len(dfs)-1)/2, all_ops, rotation=45)
    plt.legend()
    plt.tight_layout()
    plt.grid(axis='y', linestyle='--', alpha=0.7)
    
    plt.savefig(output_file)
    print(f"Comparison plot saved to {output_file}")
    
    return output_file

def create_minmax_plot(dfs, output_file='mdt_operation_minmax.png'):
    """Create a plot showing min, avg, max for each operation type."""
    if len(dfs) == 1:
        df = dfs[0]
    else:
        # If multiple files, combine them with a weighted average based on Count
        combined = {}
        for op in set(df['Operation'] for df in dfs for _, df in df.iterrows()):
            op_rows = [row for df in dfs for _, row in df.iterrows() if row['Operation'] == op]
            
            total_count = sum(row['Count'] for row in op_rows)
            min_time = min(row['Min (μs)'] for row in op_rows)
            max_time = max(row['Max (μs)'] for row in op_rows)
            
            # Weighted average
            avg_time = sum(row['Avg (μs)'] * row['Count'] for row in op_rows) / total_count
            
            combined[op] = {
                'Operation': op,
                'Count': total_count,
                'Min (μs)': min_time,
                'Avg (μs)': avg_time,
                'Max (μs)': max_time
            }
            
        df = pd.DataFrame(list(combined.values()))
    
    # Sort operations by average time
    df = df.sort_values('Avg (μs)')
    
    # Create the plot
    plt.figure(figsize=(14, 8))
    
    # Plot min, avg, max as separate series
    ops = df['Operation'].tolist()
    x = np.arange(len(ops))
    
    plt.plot(x, df['Min (μs)'], 'go-', label='Minimum')
    plt.plot(x, df['Avg (μs)'], 'bo-', label='Average')
    plt.plot(x, df['Max (μs)'], 'ro-', label='Maximum')
    
    # Add error bars to show the range
    plt.errorbar(x, df['Avg (μs)'], 
                 yerr=[df['Avg (μs)'] - df['Min (μs)'], df['Max (μs)'] - df['Avg (μs)']],
                 fmt='none', ecolor='gray', capsize=5, alpha=0.5)
    
    plt.xlabel('Operation Type', fontsize=12)
    plt.ylabel('Time (μs)', fontsize=12)
    plt.title('MDT Operation Time Range (Min/Avg/Max)', fontsize=14)
    plt.xticks(x, ops, rotation=45)
    plt.grid(True, linestyle='--', alpha=0.7)
    plt.legend()
    
    # Add counts as text
    for i, (_, row) in enumerate(df.iterrows()):
        plt.text(i, row['Max (μs)'] + 10, f"n={int(row['Count'])}", 
                 ha='center', va='bottom', fontsize=8)
    
    plt.tight_layout()
    plt.savefig(output_file)
    print(f"Min/Avg/Max plot saved to {output_file}")
    
    return output_file

def create_logarithmic_plot(dfs, output_file='mdt_operation_log_scale.png'):
    """Create a logarithmic scale plot to better show differences in timing."""
    plt.figure(figsize=(14, 8))
    
    # Get all unique operations
    all_ops = sorted(set(row['Operation'] for df in dfs for _, row in df.iterrows()))
    
    # Create positions for grouped bars
    bar_width = 0.25
    index = np.arange(len(all_ops))
    
    # Plot bars for min, avg, max
    for i, df in enumerate(dfs):
        # Create dictionaries for min, avg, max times
        min_times = {op: 0 for op in all_ops}
        avg_times = {op: 0 for op in all_ops}
        max_times = {op: 0 for op in all_ops}
        
        for _, row in df.iterrows():
            op = row['Operation']
            min_times[op] = row['Min (μs)']
            avg_times[op] = row['Avg (μs)']
            max_times[op] = row['Max (μs)']
        
        label_prefix = f"{df['Source'].iloc[0]}: " if len(dfs) > 1 else ""
        
        plt.bar(index + i*bar_width*3, [min_times[op] for op in all_ops], 
                bar_width, alpha=0.7, label=f"{label_prefix}Min")
        plt.bar(index + i*bar_width*3 + bar_width, [avg_times[op] for op in all_ops], 
                bar_width, alpha=0.7, label=f"{label_prefix}Avg")
        plt.bar(index + i*bar_width*3 + 2*bar_width, [max_times[op] for op in all_ops], 
                bar_width, alpha=0.7, label=f"{label_prefix}Max")
    
    plt.xlabel('Operation Type', fontsize=12)
    plt.ylabel('Time (μs) - Log Scale', fontsize=12)
    plt.title('MDT Operation Times (Logarithmic Scale)', fontsize=14)
    plt.xticks(index + bar_width*(3*len(dfs)-1)/2, all_ops, rotation=45)
    plt.yscale('log')
    plt.legend()
    plt.grid(True, which="both", linestyle='--', alpha=0.7)
    plt.tight_layout()
    
    plt.savefig(output_file)
    print(f"Logarithmic plot saved to {output_file}")
    
    return output_file

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <stats_csv_file> [stats_csv_file2 ...]")
        print("Example: python plot_mdt_timing_stats.py mdt_timing_stats.csv")
        # If no arguments provided but CSV files exist in current directory,
        # use those files
        csv_files = glob.glob("*mdt_timing_stats*.csv")
        if not csv_files:
            sys.exit(1)
        print(f"Found {len(csv_files)} CSV files in current directory. Using them...")
    else:
        csv_files = sys.argv[1:]
    
    dfs = read_timing_stats(csv_files)
    
    create_comparison_plot(dfs)
    create_minmax_plot(dfs)
    create_logarithmic_plot(dfs)
    
    print("\nAll plots generated successfully!")

if __name__ == "__main__":
    main()
