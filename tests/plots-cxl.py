#!/usr/bin/env python3
"""
Enhanced Memory Allocation Benchmark Results Visualization
Plots performance comparison between Lustre, Regular, and CXL allocation methods
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
        
        if 'cxl' in available_types:
            print("✓ CXL data detected - full comparison available")
        else:
            print("⚠ No CXL data found - will show Regular vs Lustre only")
            
        return df
    except FileNotFoundError:
        print(f"Error: {filename} not found. Please run the C benchmark first.")
        return None

def get_available_allocation_types(df):
    """Get list of available allocation types in the data"""
    available = [t for t in ALLOCATION_TYPES if t in df['allocation_type'].unique()]
    return available

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

def plot_pattern_analysis(df):
    """Analyze performance across different allocation patterns"""
    available_types = get_available_allocation_types(df)
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Allocation Pattern Analysis', fontsize=16, fontweight='bold')
    
    # Pattern comparison for each allocation type
    plot_positions = [(0, 0), (0, 1), (1, 0)]
    
    for i, alloc_type in enumerate(available_types[:3]):  # Max 3 types to fit subplots
        if i >= len(plot_positions):
            break
            
        ax = axes[plot_positions[i][0], plot_positions[i][1]]
        type_data = df[df['allocation_type'] == alloc_type]
        
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
    avg_performance = df.groupby(['array_size', 'allocation_type'])['alloc_time_ns'].mean().reset_index()
    
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
    plt.savefig('pattern_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_cxl_specific_analysis(df):
    """CXL-specific analysis plots"""
    if 'cxl' not in df['allocation_type'].unique():
        print("No CXL data available for CXL-specific analysis")
        return
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('CXL Memory Analysis', fontsize=16, fontweight='bold')
    
    cxl_data = df[df['allocation_type'] == 'cxl']
    regular_data = df[df['allocation_type'] == 'regular']
    
    # CXL vs Regular allocation time
    ax1 = axes[0, 0]
    avg_cxl = cxl_data.groupby('array_size')['alloc_time_ns'].mean()
    avg_regular = regular_data.groupby('array_size')['alloc_time_ns'].mean()
    
    ax1.plot(avg_cxl.index, avg_cxl.values / 1000, 
             marker='o', label='CXL', linewidth=2, markersize=6, color=colors[2])
    ax1.plot(avg_regular.index, avg_regular.values / 1000, 
             marker='s', label='Regular', linewidth=2, markersize=6, color=colors[0])
    
    ax1.set_xlabel('Array Size')
    ax1.set_ylabel('Allocation Time (µs)')
    ax1.set_title('CXL vs Regular: Allocation Time')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    
    # CXL flush time analysis (important for CXL)
    ax2 = axes[0, 1]
    available_types = get_available_allocation_types(df)
    
    for i, alloc_type in enumerate(available_types):
        data = df[df['allocation_type'] == alloc_type]
        avg_flush = data.groupby('array_size')['flush_time_ns'].mean()
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
    
    # CXL overhead analysis
    ax3 = axes[1, 0]
    merged_data = pd.merge(cxl_data.groupby('array_size')['alloc_time_ns'].mean(),
                          regular_data.groupby('array_size')['alloc_time_ns'].mean(),
                          left_index=True, right_index=True, suffixes=('_cxl', '_regular'))
    merged_data['overhead_ratio'] = merged_data['alloc_time_ns_cxl'] / merged_data['alloc_time_ns_regular']
    
    ax3.plot(merged_data.index, merged_data['overhead_ratio'], 
             marker='d', linewidth=2, markersize=6, color='red')
    ax3.axhline(y=1, color='black', linestyle='--', alpha=0.5, label='Equal Performance')
    ax3.set_xlabel('Array Size')
    ax3.set_ylabel('Time Ratio (CXL/Regular)')
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
    plt.savefig('cxl_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_efficiency_analysis(df):
    """Analyze efficiency metrics"""
    available_types = get_available_allocation_types(df)
    
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Memory Allocation Efficiency Analysis', fontsize=16, fontweight='bold')
    
    # Calculate efficiency metrics
    df['bytes_allocated'] = df['array_size'] * 32  # sizeof(struct osd_idmap_cache) ≈ 32 bytes
    df['alloc_throughput'] = df['bytes_allocated'] / (df['alloc_time_ns'] / 1e9)  # bytes per second
    df['cycles_per_byte'] = df['alloc_cycles'] / df['bytes_allocated']
    
    # Throughput comparison
    ax1 = axes[0, 0]
    avg_throughput = df.groupby(['array_size', 'allocation_type'])['alloc_throughput'].mean().reset_index()
    
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
    avg_cycles_per_byte = df.groupby(['array_size', 'allocation_type'])['cycles_per_byte'].mean().reset_index()
    
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
    unique_sizes = sorted(df['array_size'].unique())
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
    summary_stats = df.groupby('allocation_type').agg({
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
    plt.savefig('efficiency_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()

def generate_report(df):
    """Generate a summary report"""
    available_types = get_available_allocation_types(df)
    
    print("\n" + "="*60)
    print("ENHANCED MEMORY ALLOCATION BENCHMARK REPORT")
    print("="*60)
    
    print(f"\nTotal test runs: {len(df)}")
    print(f"Array sizes tested: {sorted(df['array_size'].unique())}")
    print(f"Allocation patterns: {list(df['pattern'].unique())}")
    print(f"Allocation types: {[ALLOCATION_LABELS[t] for t in available_types]}")
    
    print("\n--- Performance Summary ---")
    for alloc_type in available_types:
        data = df[df['allocation_type'] == alloc_type]
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
        type_data = df[df['allocation_type'] == alloc_type]
        best_idx = type_data['alloc_time_ns'].idxmin()
        best_configs[alloc_type] = type_data.loc[best_idx]
    
    print(f"\n--- Best Performance ---")
    for alloc_type in available_types:
        best = best_configs[alloc_type]
        print(f"Best {ALLOCATION_LABELS[alloc_type]}: {best['alloc_time_ns']/1000:.2f} µs "
              f"(size={best['array_size']}, pattern={best['pattern']})")
    
    # Performance comparison if CXL is available
    if 'cxl' in available_types and 'regular' in available_types:
        print(f"\n--- CXL vs Regular Comparison ---")
        cxl_avg = df[df['allocation_type'] == 'cxl']['alloc_time_ns'].mean()
        regular_avg = df[df['allocation_type'] == 'regular']['alloc_time_ns'].mean()
        ratio = cxl_avg / regular_avg
        
        if ratio > 1:
            print(f"CXL is {ratio:.2f}x slower than Regular on average")
        else:
            print(f"CXL is {1/ratio:.2f}x faster than Regular on average")
        
        # Flush time comparison
        cxl_flush = df[df['allocation_type'] == 'cxl']['flush_time_ns'].mean()
        regular_flush = df[df['allocation_type'] == 'regular']['flush_time_ns'].mean()
        flush_ratio = cxl_flush / regular_flush if regular_flush > 0 else float('inf')
        print(f"CXL flush operations are {flush_ratio:.2f}x the time of Regular")

def main():
    """Main function to run all analyses"""
    print("Enhanced Memory Allocation Benchmark Analysis (with CXL)")
    print("=" * 60)
    
    # Load data
    df = load_data()
    if df is None:
        return
    
    available_types = get_available_allocation_types(df)
    has_cxl = 'cxl' in available_types
    
    # Generate all plots
    print("\nGenerating allocation comparison plots...")
    plot_allocation_comparison(df)
    
    print("Generating operation phases plots...")
    plot_operation_phases(df)
    
    print("Generating pattern analysis plots...")
    plot_pattern_analysis(df)
    
    if has_cxl:
        print("Generating CXL-specific analysis plots...")
        plot_cxl_specific_analysis(df)
    
    print("Generating efficiency analysis plots...")
    plot_efficiency_analysis(df)
    
    # Generate summary report
    generate_report(df)
    
    print(f"\nAnalysis complete! Generated plots:")
    print("  - allocation_comparison.png")
    print("  - operation_phases.png") 
    print("  - pattern_analysis.png")
    if has_cxl:
        print("  - cxl_analysis.png")
    print("  - efficiency_analysis.png")

if __name__ == "__main__":
    main()