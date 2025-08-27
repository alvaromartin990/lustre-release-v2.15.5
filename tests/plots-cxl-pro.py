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

def filter_data_for_comparison(df, filter_types=None):
    """Filter dataframe to only include specified allocation types"""
    if filter_types is None:
        return df
    return df[df['allocation_type'].isin(filter_types)]

def get_user_choice():
    """Get user's choice for what to plot"""
    print("\n" + "="*60)
    print("CXL MEMORY BENCHMARK PLOTTING TOOL")
    print("="*60)
    print("\nWhat would you like to plot?")
    print("1. All allocation types")
    print("2. Lustre vs CXL only")
    print("3. Regular vs CXL only")
    print("4. Regular vs Lustre only")
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

def plot_allocation_comparison(df, filter_types=None):
    """Compare allocation times between all available allocation methods"""
    filtered_df = filter_data_for_comparison(df, filter_types)
    available_types = get_available_allocation_types(filtered_df)
    
    if not available_types:
        print("No data available for the selected allocation types")
        return
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    comparison_label = ' vs '.join([ALLOCATION_LABELS[t] for t in available_types])
    fig.suptitle(f'Allocation Performance Comparison: {comparison_label}', 
                 fontsize=16, fontweight='bold')
    
    patterns = filtered_df['pattern'].unique()
    
    for idx, pattern in enumerate(patterns[:3]):  # Limit to first 3 patterns
        if idx >= 3:
            break
            
        pattern_data = filtered_df[filtered_df['pattern'] == pattern]
        
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
    summary_data = filtered_df.groupby(['array_size', 'allocation_type'])['alloc_time_ns'].mean().reset_index()
    
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
    filename = 'allocation_comparison_' + '_'.join(available_types) + '.png'
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.show()

def plot_operation_phases(df, filter_types=None):
    """Plot all three phases: allocation, flush, free"""
    filtered_df = filter_data_for_comparison(df, filter_types)
    available_types = get_available_allocation_types(filtered_df)
    
    if not available_types:
        print("No data available for the selected allocation types")
        return
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    comparison_label = ' vs '.join([ALLOCATION_LABELS[t] for t in available_types])
    fig.suptitle(f'Memory Operation Phases Performance: {comparison_label}', 
                 fontsize=16, fontweight='bold')
    
    # Average across all patterns for cleaner visualization
    avg_data = filtered_df.groupby(['array_size', 'allocation_type']).agg({
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
    filename = 'operation_phases_' + '_'.join(available_types) + '.png'
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.show()

def plot_pattern_analysis(df, filter_types=None):
    """Analyze performance across different allocation patterns"""
    filtered_df = filter_data_for_comparison(df, filter_types)
    available_types = get_available_allocation_types(filtered_df)
    
    if not available_types:
        print("No data available for the selected allocation types")
        return
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    comparison_label = ' vs '.join([ALLOCATION_LABELS[t] for t in available_types])
    fig.suptitle(f'Allocation Pattern Analysis: {comparison_label}', 
                 fontsize=16, fontweight='bold')
    
    # Pattern comparison for each allocation type
    plot_positions = [(0, 0), (0, 1), (1, 0)]
    
    for i, alloc_type in enumerate(available_types[:3]):  # Max 3 types to fit subplots
        if i >= len(plot_positions):
            break
            
        ax = axes[plot_positions[i][0], plot_positions[i][1]]
        type_data = filtered_df[filtered_df['allocation_type'] == alloc_type]
        
        for j, pattern in enumerate(type_data['pattern'].unique()):
            data = type_data[type_data['pattern'] == pattern]
            markers = ['o', 's', '^', 'D', 'v']
            ax.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                   marker=markers[j % len(markers)], 
                   label=f'{pattern.capitalize()}', linewidth=2, markersize=4)
        
        ax.set_xlabel('Array Size')
        ax.set_ylabel('Allocation Time (µs)')
        ax.set_title(f'{ALLOCATION_LABELS[alloc_type]} Allocation - Pattern Comparison')
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale('log')
        ax.set_yscale('log')
    
    # Performance comparison across all types
    ax_comparison = axes[1, 1]
    
    # Calculate average performance for each type
    avg_performance = filtered_df.groupby(['array_size', 'allocation_type'])['alloc_time_ns'].mean().reset_index()
    
    for i, alloc_type in enumerate(available_types):
        data = avg_performance[avg_performance['allocation_type'] == alloc_type]
        ax_comparison.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                          marker='o', label=ALLOCATION_LABELS[alloc_type], 
                          linewidth=3, markersize=6, color=colors[i % len(colors)])
    
    ax_comparison.set_xlabel('Array Size')
    ax_comparison.set_ylabel('Average Allocation Time (µs)')
    ax_comparison.set_title('Overall Performance Comparison')
    ax_comparison.legend()
    ax_comparison.grid(True, alpha=0.3)
    ax_comparison.set_xscale('log')
    ax_comparison.set_yscale('log')
    
    plt.tight_layout()
    filename = 'pattern_analysis_' + '_'.join(available_types) + '.png'
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.show()

def plot_cxl_specific_analysis(df, filter_types=None):
    """CXL-specific analysis plots"""
    filtered_df = filter_data_for_comparison(df, filter_types)
    
    if 'cxl' not in filtered_df['allocation_type'].unique():
        print("No CXL data available for CXL-specific analysis")
        return
    
    available_types = get_available_allocation_types(filtered_df)
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    comparison_label = ' vs '.join([ALLOCATION_LABELS[t] for t in available_types])
    fig.suptitle(f'CXL Memory Analysis: {comparison_label}', fontsize=16, fontweight='bold')
    
    cxl_data = filtered_df[filtered_df['allocation_type'] == 'cxl']
    
    # CXL vs other allocation time
    ax1 = axes[0, 0]
    for i, alloc_type in enumerate(available_types):
        type_data = filtered_df[filtered_df['allocation_type'] == alloc_type]
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
    
    # CXL flush time analysis (important for CXL)
    ax2 = axes[0, 1]
    for i, alloc_type in enumerate(available_types):
        type_data = filtered_df[filtered_df['allocation_type'] == alloc_type]
        avg_flush = type_data.groupby('array_size')['flush_time_ns'].mean()
        ax2.plot(avg_flush.index, avg_flush.values / 1000, 
                marker='o', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax2.set_xlabel('Array Size')
    ax2.set_ylabel('Flush Time (µs)')
    ax2.set_title('Memory Flush Performance Comparison')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    
    # CXL overhead analysis (if other types available)
    ax3 = axes[1, 0]
    if len(available_types) > 1:
        # Compare CXL to the first non-CXL type
        other_types = [t for t in available_types if t != 'cxl']
        if other_types:
            other_type = other_types[0]
            cxl_avg = cxl_data.groupby('array_size')['alloc_time_ns'].mean()
            other_avg = filtered_df[filtered_df['allocation_type'] == other_type].groupby('array_size')['alloc_time_ns'].mean()
            
            common_sizes = cxl_avg.index.intersection(other_avg.index)
            if len(common_sizes) > 0:
                overhead_ratio = cxl_avg[common_sizes] / other_avg[common_sizes]
                
                ax3.plot(common_sizes, overhead_ratio, 
                        marker='d', linewidth=2, markersize=6, color='red')
                ax3.axhline(y=1, color='black', linestyle='--', alpha=0.5, label='Equal Performance')
                ax3.set_xlabel('Array Size')
                ax3.set_ylabel(f'Time Ratio (CXL/{ALLOCATION_LABELS[other_type]})')
                ax3.set_title('CXL Performance Overhead')
                ax3.legend()
                ax3.grid(True, alpha=0.3)
                ax3.set_xscale('log')
    
    # Memory operation breakdown
    ax4 = axes[1, 1]
    cxl_avg = cxl_data.groupby('array_size').agg({
        'alloc_time_ns': 'mean',
        'flush_time_ns': 'mean',
        'free_time_ns': 'mean'
    })
    
    ax4.plot(cxl_avg.index, cxl_avg['alloc_time_ns'] / 1000, 
             marker='o', label='Allocation', linewidth=2)
    ax4.plot(cxl_avg.index, cxl_avg['flush_time_ns'] / 1000, 
             marker='s', label='Flush', linewidth=2)
    ax4.plot(cxl_avg.index, cxl_avg['free_time_ns'] / 1000, 
             marker='^', label='Free', linewidth=2)
    
    ax4.set_xlabel('Array Size')
    ax4.set_ylabel('Time (µs)')
    ax4.set_title('CXL Memory Operation Breakdown')
    ax4.legend()
    ax4.grid(True, alpha=0.3)
    ax4.set_xscale('log')
    ax4.set_yscale('log')
    
    plt.tight_layout()
    filename = 'cxl_analysis_' + '_'.join(available_types) + '.png'
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.show()

def plot_efficiency_analysis(df, filter_types=None):
    """Analyze efficiency metrics"""
    filtered_df = filter_data_for_comparison(df, filter_types)
    available_types = get_available_allocation_types(filtered_df)
    
    if not available_types:
        print("No data available for the selected allocation types")
        return
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    comparison_label = ' vs '.join([ALLOCATION_LABELS[t] for t in available_types])
    fig.suptitle(f'Memory Allocation Efficiency Analysis: {comparison_label}', 
                 fontsize=16, fontweight='bold')
    
    # Calculate efficiency metrics
    filtered_df['bytes_allocated'] = filtered_df['array_size'] * 32  # sizeof(struct osd_idmap_cache) ≈ 32 bytes
    filtered_df['alloc_throughput'] = filtered_df['bytes_allocated'] / (filtered_df['alloc_time_ns'] / 1e9)  # bytes per second
    filtered_df['cycles_per_byte'] = filtered_df['alloc_cycles'] / filtered_df['bytes_allocated']
    
    # Throughput comparison
    ax1 = axes[0, 0]
    avg_throughput = filtered_df.groupby(['array_size', 'allocation_type'])['alloc_throughput'].mean().reset_index()
    
    for i, alloc_type in enumerate(available_types):
        data = avg_throughput[avg_throughput['allocation_type'] == alloc_type]
        ax1.plot(data['array_size'], data['alloc_throughput'] / 1e6, 
                marker='o', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax1.set_xlabel('Array Size')
    ax1.set_ylabel('Throughput (MB/s)')
    ax1.set_title('Allocation Throughput')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xscale('log')
    
    # Cycles per byte
    ax2 = axes[0, 1]
    avg_cycles_per_byte = filtered_df.groupby(['array_size', 'allocation_type'])['cycles_per_byte'].mean().reset_index()
    
    for i, alloc_type in enumerate(available_types):
        data = avg_cycles_per_byte[avg_cycles_per_byte['allocation_type'] == alloc_type]
        ax2.plot(data['array_size'], data['cycles_per_byte'], 
                marker='s', label=ALLOCATION_LABELS[alloc_type], 
                linewidth=2, markersize=6, color=colors[i % len(colors)])
    
    ax2.set_xlabel('Array Size')
    ax2.set_ylabel('CPU Cycles per Byte')
    ax2.set_title('CPU Efficiency (Lower is Better)')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    
    # Memory footprint analysis
    ax3 = axes[1, 0]
    unique_sizes = sorted(filtered_df['array_size'].unique())
    ax3.plot(unique_sizes, 
             [size * 32 / 1024 for size in unique_sizes], 
             marker='o', linewidth=2, markersize=6, color='purple')
    
    ax3.set_xlabel('Array Size')
    ax3.set_ylabel('Memory Footprint (KB)')
    ax3.set_title('Memory Footprint')
    ax3.grid(True, alpha=0.3)
    ax3.set_xscale('log')
    ax3.set_yscale('log')
    
    # Summary statistics table
    ax4 = axes[1, 1]
    summary_stats = filtered_df.groupby('allocation_type').agg({
        'alloc_time_ns': ['mean', 'std'],
        'alloc_throughput': ['mean', 'std']
    }).round(2)
    
    ax4.axis('tight')
    ax4.axis('off')
    table_data = []
    
    for alloc_type in available_types:
        mean_time = summary_stats.loc[alloc_type, ('alloc_time_ns', 'mean')] / 1000
        std_time = summary_stats.loc[alloc_type, ('alloc_time_ns', 'std')] / 1000
        mean_throughput = summary_stats.loc[alloc_type, ('alloc_throughput', 'mean')] / 1e6
        std_throughput = summary_stats.loc[alloc_type, ('alloc_throughput', 'std')] / 1e6
        
        table_data.append([
            ALLOCATION_LABELS[alloc_type],
            f"{mean_time:.2f} ± {std_time:.2f}",
            f"{mean_throughput:.2f} ± {std_throughput:.2f}"
        ])
    
    table = ax4.table(cellText=table_data,
                     colLabels=['Allocation Type', 'Avg Time (µs)', 'Avg Throughput (MB/s)'],
                     cellLoc='center',
                     loc='center')
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1.2, 1.5)
    ax4.set_title('Summary Statistics')
    
    plt.tight_layout()
    filename = 'efficiency_analysis_' + '_'.join(available_types) + '.png'
    plt.savefig(filename, dpi=300, bbox_inches='tight')
    plt.show()

def generate_report(df, filter_types=None):
    """Generate a summary report"""
    filtered_df = filter_data_for_comparison(df, filter_types)
    available_types = get_available_allocation_types(filtered_df)
    
    if not available_types:
        print("No data available for the selected allocation types")
        return
    
    print("\n" + "="*60)
    print("ENHANCED MEMORY ALLOCATION BENCHMARK REPORT")
    if filter_types:
        comparison_label = ' vs '.join([ALLOCATION_LABELS[t] for t in available_types])
        print(f"FILTERED FOR: {comparison_label}")
    print("="*60)
    
    print(f"\nTotal test runs: {len(filtered_df)}")
    print(f"Array sizes tested: {sorted(filtered_df['array_size'].unique())}")
    print(f"Allocation patterns: {list(filtered_df['pattern'].unique())}")
    print(f"Allocation types: {[ALLOCATION_LABELS[t] for t in available_types]}")
    
    print("\n--- Performance Summary ---")
    for alloc_type in available_types:
        data = filtered_df[filtered_df['allocation_type'] == alloc_type]
        avg_time = data['alloc_time_ns'].mean() / 1000
        min_time = data['alloc_time_ns'].min() / 1000
        max_time = data['alloc_time_ns'].max() / 1000
        
        # Calculate throughput
        total_bytes = data['array_size'].sum() * 32
        total_time_s = data['alloc_time_ns'].sum() / 1e9
        avg_throughput = total_bytes / total_time_s / 1e6 if total_time_s > 0 else 0
        
        print(f"\n{ALLOCATION_LABELS[alloc_type].upper()} Allocation:")
        print(f"  Average allocation time: {avg_time:.2f} µs")
        print(f"  Range: {min_time:.2f} - {max_time:.2f} µs")
        print(f"  Average throughput: {avg_throughput:.2f} MB/s")
        
        if alloc_type == 'cxl':
            avg_flush = data['flush_time_ns'].mean() / 1000
            print(f"  Average flush time: {avg_flush:.2f} µs (important for CXL)")
    
    # Find best performing configurations
    best_configs = {}
    for alloc_type in available_types:
        type_data = filtered_df[filtered_df['allocation_type'] == alloc_type]
        best_idx = type_data['alloc_time_ns'].idxmin()
        best_configs[alloc_type] = type_data.loc[best_idx]
    
    print(f"\n--- Best Performance ---")
    for alloc_type in available_types:
        best = best_configs[alloc_type]
        print(f"Best {ALLOCATION_LABELS[alloc_type]}: {best['alloc_time_ns']/1000:.2f} µs "
              f"(size={best['array_size']}, pattern={best['pattern']})")
    
    # Performance comparison if multiple types available
    if len(available_types) >= 2:
        print(f"\n--- Performance Comparisons ---")
        for i, type1 in enumerate(available_types):
            for type2 in available_types[i+1:]:
                avg1 = filtered_df[filtered_df['allocation_type'] == type1]['alloc_time_ns'].mean()
                avg2 = filtered_df[filtered_df['allocation_type'] == type2]['alloc_time_ns'].mean()
                ratio = avg1 / avg2
                
                if ratio > 1:
                    print(f"{ALLOCATION_LABELS[type1]} is {ratio:.2f}x slower than {ALLOCATION_LABELS[type2]} on average")
                else:
                    print(f"{ALLOCATION_LABELS[type1]} is {1/ratio:.2f}x faster than {ALLOCATION_LABELS[type2]} on average")

def run_all_plots(df, filter_types=None):
    """Run all plotting functions with the specified filter"""
    filter_label = ' vs '.join([ALLOCATION_LABELS[t] for t in filter_types]) if filter_types else "All types"
    
    print(f"\nGenerating all plots for: {filter_label}")
    
    print("1. Generating allocation comparison plots...")
    plot_allocation_comparison(df, filter_types)
    
    print("2. Generating operation phases plots...")
    plot_operation_phases(df, filter_types)
    
    print("3. Generating pattern analysis plots...")
    plot_pattern_analysis(df, filter_types)
    
    if not filter_types or 'cxl' in filter_types:
        print("4. Generating CXL-specific analysis plots...")
        plot_cxl_specific_analysis(df, filter_types)
    
    print("5. Generating efficiency analysis plots...")
    plot_efficiency_analysis(df, filter_types)
    
    print("6. Generating summary report...")
    generate_report(df, filter_types)

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
            print("\nGenerating plots for all allocation types...")
            run_all_plots(df, None)
            
        elif choice == 2:
            print("\nGenerating plots for Lustre vs CXL only...")
            if 'lustre' in available_types and 'cxl' in available_types:
                run_all_plots(df, ['lustre', 'cxl'])
            else:
                print("Error: Need both Lustre and CXL data for this comparison")
                missing = []
                if 'lustre' not in available_types:
                    missing.append('Lustre')
                if 'cxl' not in available_types:
                    missing.append('CXL')
                print(f"Missing: {', '.join(missing)}")
            
        elif choice == 3:
            print("\nGenerating plots for Regular vs CXL only...")
            if 'regular' in available_types and 'cxl' in available_types:
                run_all_plots(df, ['regular', 'cxl'])
            else:
                print("Error: Need both Regular and CXL data for this comparison")
                
        elif choice == 4:
            print("\nGenerating plots for Regular vs Lustre only...")
            if 'regular' in available_types and 'lustre' in available_types:
                run_all_plots(df, ['regular', 'lustre'])
            else:
                print("Error: Need both Regular and Lustre data for this comparison")
            
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