#!/usr/bin/env python3
"""
Enhanced Memory Allocation Benchmark Results Visualization
Interactive plotting tool with user selection for specific comparisons
"""

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
from pathlib import Path

# Set style for better-looking plots
plt.style.use('seaborn-v0_8')
# Use a palette that works well with 3 categories
colors = ['#1f77b4', '#ff7f0e', '#2ca02c']  # Blue, Orange, Green
sns.set_palette(colors)

# Define allocation types for consistent ordering
ALLOCATION_TYPES = ['regular', 'lustre', 'cxl']
ALLOCATION_LABELS = {'regular': 'Regular', 'lustre': 'Lustre', 'cxl': 'CXL'}

def load_data(filename='benchmark_results.csv'):
    """Load benchmark results from CSV file"""
    try:
        df = pd.read_csv(filename)
        print(f"Loaded {len(df)} results from {filename}")
        
        # Check which allocation types are available
        available_types = df['allocation_type'].unique()
        print(f"Available allocation types: {list(available_types)}")
        
        return df
    except FileNotFoundError:
        print(f"Error: {filename} not found. Please run the C benchmark first.")
        return None

def get_available_allocation_types(df):
    """Get list of available allocation types in the data"""
    available = [t for t in ALLOCATION_TYPES if t in df['allocation_type'].unique()]
    return available

def filter_data_for_comparison(df, comparison_types):
    """Filter dataframe to only include specified allocation types"""
    return df[df['allocation_type'].isin(comparison_types)]

def plot_lustre_vs_cxl_comparison(df):
    """Specific comparison between Lustre and CXL allocation methods"""
    # Filter data to only include lustre and cxl
    filtered_df = filter_data_for_comparison(df, ['lustre', 'cxl'])
    
    if filtered_df.empty:
        print("No Lustre or CXL data available for comparison")
        return
    
    available_types = get_available_allocation_types(filtered_df)
    if len(available_types) < 2:
        print(f"Need both Lustre and CXL data. Available: {available_types}")
        return
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Lustre vs CXL Memory Allocation Comparison', fontsize=16, fontweight='bold')
    
    # Allocation time comparison
    ax1 = axes[0, 0]
    avg_data = filtered_df.groupby(['array_size', 'allocation_type']).agg({
        'alloc_time_ns': 'mean'
    }).reset_index()
    
    for i, alloc_type in enumerate(available_types):
        data = avg_data[avg_data['allocation_type'] == alloc_type]
        ax1.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                marker='o', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax1.set_xlabel('Array Size')
    ax1.set_ylabel('Allocation Time (µs)')
    ax1.set_title('Allocation Time Comparison')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    
    # Flush time comparison 
    ax2 = axes[0, 1]
    flush_data = filtered_df.groupby(['array_size', 'allocation_type']).agg({
        'flush_time_ns': 'mean'
    }).reset_index()
    
    for i, alloc_type in enumerate(available_types):
        data = flush_data[flush_data['allocation_type'] == alloc_type]
        ax2.plot(data['array_size'], data['flush_time_ns'] / 1000, 
                marker='s', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax2.set_xlabel('Array Size')
    ax2.set_ylabel('Flush Time (µs)')
    ax2.set_title('Memory Flush Time Comparison')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    
    # Performance ratio analysis
    ax3 = axes[1, 0]
    if 'lustre' in available_types and 'cxl' in available_types:
        lustre_data = filtered_df[filtered_df['allocation_type'] == 'lustre'].groupby('array_size')['alloc_time_ns'].mean()
        cxl_data = filtered_df[filtered_df['allocation_type'] == 'cxl'].groupby('array_size')['alloc_time_ns'].mean()
        
        common_sizes = lustre_data.index.intersection(cxl_data.index)
        ratio_data = cxl_data[common_sizes] / lustre_data[common_sizes]
        
        ax3.plot(common_sizes, ratio_data, 
                marker='d', linewidth=2, markersize=6, color='red', label='CXL/Lustre Ratio')
        ax3.axhline(y=1, color='black', linestyle='--', alpha=0.5, label='Equal Performance')
        ax3.set_xlabel('Array Size')
        ax3.set_ylabel('Time Ratio (CXL/Lustre)')
        ax3.set_title('CXL vs Lustre Performance Ratio')
        ax3.legend()
        ax3.grid(True, alpha=0.3)
        ax3.set_xscale('log')
    
    # Throughput comparison
    ax4 = axes[1, 1]
    filtered_df['bytes_allocated'] = filtered_df['array_size'] * 32
    filtered_df['alloc_throughput'] = filtered_df['bytes_allocated'] / (filtered_df['alloc_time_ns'] / 1e9)
    
    throughput_data = filtered_df.groupby(['array_size', 'allocation_type']).agg({
        'alloc_throughput': 'mean'
    }).reset_index()
    
    for i, alloc_type in enumerate(available_types):
        data = throughput_data[throughput_data['allocation_type'] == alloc_type]
        ax4.plot(data['array_size'], data['alloc_throughput'] / 1e6, 
                marker='^', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax4.set_xlabel('Array Size')
    ax4.set_ylabel('Throughput (MB/s)')
    ax4.set_title('Allocation Throughput Comparison')
    ax4.legend()
    ax4.grid(True, alpha=0.3)
    ax4.set_xscale('log')
    
    plt.tight_layout()
    plt.savefig('lustre_vs_cxl_comparison.png', dpi=300, bbox_inches='tight')
    plt.show()
    
    # Print summary statistics
    print("\n" + "="*50)
    print("LUSTRE vs CXL COMPARISON SUMMARY")
    print("="*50)
    
    for alloc_type in available_types:
        data = filtered_df[filtered_df['allocation_type'] == alloc_type]
        avg_time = data['alloc_time_ns'].mean() / 1000
        avg_throughput = data['alloc_throughput'].mean() / 1e6
        
        print(f"\n{ALLOCATION_LABELS[alloc_type].upper()}:")
        print(f"  Average allocation time: {avg_time:.2f} µs")
        print(f"  Average throughput: {avg_throughput:.2f} MB/s")

def plot_allocation_comparison(df):
    """Compare allocation times between all available allocation methods"""
    available_types = get_available_allocation_types(df)
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Allocation Performance Comparison: ' + ' vs '.join([ALLOCATION_LABELS[t] for t in available_types]), 
                 fontsize=16, fontweight='bold')
    
    patterns = df['pattern'].unique()
    
    for idx, pattern in enumerate(patterns[:3]):  # Limit to first 3 patterns
        if idx >= 3:
            break
            
        pattern_data = df[df['pattern'] == pattern]
        
        # Plot time comparison
        ax1 = axes[idx // 2, idx % 2] if idx < 2 else axes[1, 0]
        
        for i, alloc_type in enumerate(available_types):
            data = pattern_data[pattern_data['allocation_type'] == alloc_type]
            ax1.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                    marker='o', label=ALLOCATION_LABELS[alloc_type], 
                    linewidth=2, markersize=6, color=colors[i % len(colors)])
        
        ax1.set_xlabel('Array Size')
        ax1.set_ylabel('Allocation Time (µs)')
        ax1.set_title(f'Allocation Time - {pattern.capitalize()} Pattern')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        ax1.set_xscale('log')
        ax1.set_yscale('log')
    
    # Summary comparison across all patterns
    ax_summary = axes[1, 1]
    summary_data = df.groupby(['array_size', 'allocation_type'])['alloc_time_ns'].mean().reset_index()
    
    for i, alloc_type in enumerate(available_types):
        data = summary_data[summary_data['allocation_type'] == alloc_type]
        ax_summary.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                       marker='s', label=f'{ALLOCATION_LABELS[alloc_type]} (Average)', 
                       linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax_summary.set_xlabel('Array Size')
    ax_summary.set_ylabel('Average Allocation Time (µs)')
    ax_summary.set_title('Average Allocation Time Across All Patterns')
    ax_summary.legend()
    ax_summary.grid(True, alpha=0.3)
    ax_summary.set_xscale('log')
    ax_summary.set_yscale('log')
    
    plt.tight_layout()
    plt.savefig('allocation_comparison.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_operation_phases(df):
    """Plot all three phases: allocation, flush, free"""
    available_types = get_available_allocation_types(df)
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Memory Operation Phases Performance', fontsize=16, fontweight='bold')
    
    # Average across all patterns for cleaner visualization
    avg_data = df.groupby(['array_size', 'allocation_type']).agg({
        'alloc_time_ns': 'mean',
        'flush_time_ns': 'mean', 
        'free_time_ns': 'mean'
    }).reset_index()
    
    phases = [
        ('alloc_time_ns', 'Allocation Time', axes[0, 0]),
        ('flush_time_ns', 'Memory Flush Time', axes[0, 1]),
        ('free_time_ns', 'Deallocation Time', axes[1, 0])
    ]
    
    for phase_col, title, ax in phases:
        for i, alloc_type in enumerate(available_types):
            data = avg_data[avg_data['allocation_type'] == alloc_type]
            ax.plot(data['array_size'], data[phase_col] / 1000, 
                   marker='o', label=ALLOCATION_LABELS[alloc_type], 
                   linewidth=2, markersize=6, color=colors[i % len(colors)])
        
        ax.set_xlabel('Array Size')
        ax.set_ylabel('Time (µs)')
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale('log')
        ax.set_yscale('log')
    
    # Combined view - stacked bar chart
    ax_combined = axes[1, 1]
    n_types = len(available_types)
    width = 0.8 / n_types
    x = np.arange(len(avg_data[avg_data['allocation_type'] == available_types[0]]))
    
    for i, alloc_type in enumerate(available_types):
        data = avg_data[avg_data['allocation_type'] == alloc_type]
        offset = (i - n_types/2 + 0.5) * width
        ax_combined.bar(x + offset, data['alloc_time_ns'] / 1000, 
                       width, label=ALLOCATION_LABELS[alloc_type], 
                       alpha=0.8, color=colors[i % len(colors)])
    
    ax_combined.set_xlabel('Array Size')
    ax_combined.set_ylabel('Allocation Time (µs)')
    ax_combined.set_title('Allocation Time Comparison (Bar Chart)')
    ax_combined.set_xticks(x)
    # Get array sizes from first allocation type
    first_type_data = avg_data[avg_data['allocation_type'] == available_types[0]]
    ax_combined.set_xticklabels(first_type_data['array_size'])
    ax_combined.legend()
    ax_combined.set_yscale('log')
    
    plt.tight_layout()
    plt.savefig('operation_phases.png', dpi=300, bbox_inches='tight')
    plt.show()

def get_user_choice():
    """Get user's choice for what to plot"""
    print("\n" + "="*60)
    print("CXL MEMORY BENCHMARK PLOTTING TOOL")
    print("="*60)
    print("\nWhat would you like to plot?")
    print("1. All allocation types comparison")
    print("2. Lustre vs CXL only")
    print("3. Operation phases (allocation, flush, free)")
    print("4. CXL-specific analysis") 
    print("5. Exit")
    
    while True:
        try:
            choice = input("\nEnter your choice (1-5): ").strip()
            if choice in ['1', '2', '3', '4', '5']:
                return int(choice)
            else:
                print("Invalid choice. Please enter 1, 2, 3, 4, or 5.")
        except (ValueError, KeyboardInterrupt):
            print("Invalid input. Please enter a number between 1 and 5.")

def plot_cxl_specific_analysis(df):
    """CXL-specific analysis plots"""
    if 'cxl' not in df['allocation_type'].unique():
        print("No CXL data available for CXL-specific analysis")
        return
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('CXL Memory Analysis', fontsize=16, fontweight='bold')
    
    cxl_data = df[df['allocation_type'] == 'cxl']
    available_types = get_available_allocation_types(df)
    
    # CXL vs other types allocation time
    ax1 = axes[0, 0]
    for i, alloc_type in enumerate(available_types):
        type_data = df[df['allocation_type'] == alloc_type]
        avg_data = type_data.groupby('array_size')['alloc_time_ns'].mean()
        ax1.plot(avg_data.index, avg_data.values / 1000, 
                marker='o', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax1.set_xlabel('Array Size')
    ax1.set_ylabel('Allocation Time (µs)')
    ax1.set_title('Allocation Time Comparison')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    
    # CXL flush time analysis
    ax2 = axes[0, 1]
    for i, alloc_type in enumerate(available_types):
        type_data = df[df['allocation_type'] == alloc_type]
        avg_flush = type_data.groupby('array_size')['flush_time_ns'].mean()
        ax2.plot(avg_flush.index, avg_flush.values / 1000, 
                marker='s', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax2.set_xlabel('Array Size')
    ax2.set_ylabel('Flush Time (µs)')
    ax2.set_title('Memory Flush Performance')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    
    # CXL memory operation breakdown
    ax3 = axes[1, 0]
    cxl_avg = cxl_data.groupby('array_size').agg({
        'alloc_time_ns': 'mean',
        'flush_time_ns': 'mean',
        'free_time_ns': 'mean'
    })
    
    ax3.plot(cxl_avg.index, cxl_avg['alloc_time_ns'] / 1000, 
             marker='o', label='Allocation', linewidth=2)
    ax3.plot(cxl_avg.index, cxl_avg['flush_time_ns'] / 1000, 
             marker='s', label='Flush', linewidth=2)
    ax3.plot(cxl_avg.index, cxl_avg['free_time_ns'] / 1000, 
             marker='^', label='Free', linewidth=2)
    
    ax3.set_xlabel('Array Size')
    ax3.set_ylabel('Time (µs)')
    ax3.set_title('CXL Memory Operation Breakdown')
    ax3.legend()
    ax3.grid(True, alpha=0.3)
    ax3.set_xscale('log')
    ax3.set_yscale('log')
    
    # Performance comparison table
    ax4 = axes[1, 1]
    ax4.axis('tight')
    ax4.axis('off')
    
    summary_stats = df.groupby('allocation_type').agg({
        'alloc_time_ns': 'mean',
        'flush_time_ns': 'mean'
    }).round(2)
    
    table_data = []
    for alloc_type in available_types:
        mean_alloc = summary_stats.loc[alloc_type, 'alloc_time_ns'] / 1000
        mean_flush = summary_stats.loc[alloc_type, 'flush_time_ns'] / 1000
        table_data.append([
            ALLOCATION_LABELS[alloc_type],
            f"{mean_alloc:.2f}",
            f"{mean_flush:.2f}"
        ])
    
    table = ax4.table(cellText=table_data,
                     colLabels=['Type', 'Avg Alloc (µs)', 'Avg Flush (µs)'],
                     cellLoc='center',
                     loc='center')
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1.2, 1.5)
    ax4.set_title('Performance Summary')
    
    plt.tight_layout()
    plt.savefig('cxl_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()

def main():
    """Main function with interactive menu"""
    # Load data
    df = load_data()
    if df is None:
        return
    
    available_types = get_available_allocation_types(df)
    print(f"\nAvailable allocation types in data: {[ALLOCATION_LABELS[t] for t in available_types]}")
    
    while True:
        choice = get_user_choice()
        
        if choice == 1:
            print("\nGenerating all allocation types comparison...")
            plot_allocation_comparison(df)
            
        elif choice == 2:
            print("\nGenerating Lustre vs CXL comparison...")
            plot_lustre_vs_cxl_comparison(df)
            
        elif choice == 3:
            print("\nGenerating operation phases plots...")
            plot_operation_phases(df)
            
        elif choice == 4:
            print("\nGenerating CXL-specific analysis...")
            plot_cxl_specific_analysis(df)
            
        elif choice == 5:
            print("\nExiting...")
            break
        
        # Ask if user wants to continue
        continue_choice = input("\nWould you like to plot something else? (y/n): ").strip().lower()
        if continue_choice not in ['y', 'yes']:
            break
    
    print("Thanks for using the CXL Memory Benchmark Plotting Tool!")

if __name__ == "__main__":
    main()