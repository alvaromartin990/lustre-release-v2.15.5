#!/usr/bin/env python3
"""
Memory Allocation Benchmark Results Visualization
Plots performance comparison between Lustre and Regular allocation methods
"""

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
from pathlib import Path

# Set style for better-looking plots
plt.style.use('seaborn-v0_8')
sns.set_palette("husl")

def load_data(filename='benchmark_results.csv'):
    """Load benchmark results from CSV file"""
    try:
        df = pd.read_csv(filename)
        print(f"Loaded {len(df)} results from {filename}")
        return df
    except FileNotFoundError:
        print(f"Error: {filename} not found. Please run the C benchmark first.")
        return None

def plot_allocation_comparison(df):
    """Compare allocation times between Lustre and Regular methods"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Allocation Performance Comparison: Lustre vs Regular', fontsize=16, fontweight='bold')
    
    patterns = df['pattern'].unique()
    
    for idx, pattern in enumerate(patterns[:3]):  # Limit to first 3 patterns
        if idx >= 3:
            break
            
        pattern_data = df[df['pattern'] == pattern]
        
        # Plot time comparison
        ax1 = axes[idx // 2, idx % 2] if idx < 2 else axes[1, 0]
        
        for alloc_type in ['lustre', 'regular']:
            data = pattern_data[pattern_data['allocation_type'] == alloc_type]
            ax1.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                    marker='o', label=f'{alloc_type.capitalize()}', linewidth=2, markersize=6)
        
        ax1.set_xlabel('Array Size')
        ax1.set_ylabel('Allocation Time (μs)')
        ax1.set_title(f'Allocation Time - {pattern.capitalize()} Pattern')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        ax1.set_xscale('log')
        ax1.set_yscale('log')
    
    # Summary comparison across all patterns
    ax_summary = axes[1, 1]
    summary_data = df.groupby(['array_size', 'allocation_type'])['alloc_time_ns'].mean().reset_index()
    
    for alloc_type in ['lustre', 'regular']:
        data = summary_data[summary_data['allocation_type'] == alloc_type]
        ax_summary.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                       marker='s', label=f'{alloc_type.capitalize()} (Average)', 
                       linewidth=2, markersize=6)
    
    ax_summary.set_xlabel('Array Size')
    ax_summary.set_ylabel('Average Allocation Time (μs)')
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
        for alloc_type in ['lustre', 'regular']:
            data = avg_data[avg_data['allocation_type'] == alloc_type]
            ax.plot(data['array_size'], data[phase_col] / 1000, 
                   marker='o', label=f'{alloc_type.capitalize()}', linewidth=2, markersize=6)
        
        ax.set_xlabel('Array Size')
        ax.set_ylabel('Time (μs)')
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)
        ax.set_xscale('log')
        ax.set_yscale('log')
    
    # Combined view
    ax_combined = axes[1, 1]
    width = 0.35
    x = np.arange(len(avg_data[avg_data['allocation_type'] == 'lustre']))
    
    lustre_data = avg_data[avg_data['allocation_type'] == 'lustre']
    regular_data = avg_data[avg_data['allocation_type'] == 'regular']
    
    ax_combined.bar(x - width/2, lustre_data['alloc_time_ns'] / 1000, 
                   width, label='Lustre', alpha=0.8)
    ax_combined.bar(x + width/2, regular_data['alloc_time_ns'] / 1000, 
                   width, label='Regular', alpha=0.8)
    
    ax_combined.set_xlabel('Array Size')
    ax_combined.set_ylabel('Allocation Time (μs)')
    ax_combined.set_title('Allocation Time Comparison (Bar Chart)')
    ax_combined.set_xticks(x)
    ax_combined.set_xticklabels(lustre_data['array_size'])
    ax_combined.legend()
    ax_combined.set_yscale('log')
    
    plt.tight_layout()
    plt.savefig('operation_phases.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_pattern_analysis(df):
    """Analyze performance across different allocation patterns"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Allocation Pattern Analysis', fontsize=16, fontweight='bold')
    
    # Pattern comparison for Lustre
    ax1 = axes[0, 0]
    lustre_data = df[df['allocation_type'] == 'lustre']
    for pattern in lustre_data['pattern'].unique():
        data = lustre_data[lustre_data['pattern'] == pattern]
        ax1.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                marker='o', label=f'{pattern.capitalize()}', linewidth=2, markersize=4)
    
    ax1.set_xlabel('Array Size')
    ax1.set_ylabel('Allocation Time (μs)')
    ax1.set_title('Lustre Allocation - Pattern Comparison')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    
    # Pattern comparison for Regular
    ax2 = axes[0, 1]
    regular_data = df[df['allocation_type'] == 'regular']
    for pattern in regular_data['pattern'].unique():
        data = regular_data[regular_data['pattern'] == pattern]
        ax2.plot(data['array_size'], data['alloc_time_ns'] / 1000, 
                marker='s', label=f'{pattern.capitalize()}', linewidth=2, markersize=4)
    
    ax2.set_xlabel('Array Size')
    ax2.set_ylabel('Allocation Time (μs)')
    ax2.set_title('Regular Allocation - Pattern Comparison')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    
    # Performance ratio (Lustre vs Regular)
    ax3 = axes[1, 0]
    for pattern in df['pattern'].unique():
        pattern_data = df[df['pattern'] == pattern]
        lustre = pattern_data[pattern_data['allocation_type'] == 'lustre']
        regular = pattern_data[pattern_data['allocation_type'] == 'regular']
        
        # Merge on array_size to calculate ratio
        merged = pd.merge(lustre, regular, on='array_size', suffixes=('_lustre', '_regular'))
        merged['ratio'] = merged['alloc_time_ns_lustre'] / merged['alloc_time_ns_regular']
        
        ax3.plot(merged['array_size'], merged['ratio'], 
                marker='d', label=f'{pattern.capitalize()}', linewidth=2, markersize=4)
    
    ax3.set_xlabel('Array Size')
    ax3.set_ylabel('Time Ratio (Lustre/Regular)')
    ax3.set_title('Performance Ratio: Lustre vs Regular')
    ax3.axhline(y=1, color='black', linestyle='--', alpha=0.5, label='Equal Performance')
    ax3.legend()
    ax3.grid(True, alpha=0.3)
    ax3.set_xscale('log')
    
    # CPU Cycles comparison
    ax4 = axes[1, 1]
    avg_cycles = df.groupby(['array_size', 'allocation_type'])['alloc_cycles'].mean().reset_index()
    
    for alloc_type in ['lustre', 'regular']:
        data = avg_cycles[avg_cycles['allocation_type'] == alloc_type]
        ax4.plot(data['array_size'], data['alloc_cycles'], 
                marker='o', label=f'{alloc_type.capitalize()}', linewidth=2, markersize=6)
    
    ax4.set_xlabel('Array Size')
    ax4.set_ylabel('CPU Cycles')
    ax4.set_title('CPU Cycles Comparison')
    ax4.legend()
    ax4.grid(True, alpha=0.3)
    ax4.set_xscale('log')
    ax4.set_yscale('log')
    
    plt.tight_layout()
    plt.savefig('pattern_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_efficiency_analysis(df):
    """Analyze efficiency metrics"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Memory Allocation Efficiency Analysis', fontsize=16, fontweight='bold')
    
    # Calculate efficiency metrics
    df['bytes_allocated'] = df['array_size'] * 32  # sizeof(struct osd_idmap_cache) ≈ 32 bytes
    df['alloc_throughput'] = df['bytes_allocated'] / (df['alloc_time_ns'] / 1e9)  # bytes per second
    df['cycles_per_byte'] = df['alloc_cycles'] / df['bytes_allocated']
    
    # Throughput comparison
    ax1 = axes[0, 0]
    avg_throughput = df.groupby(['array_size', 'allocation_type'])['alloc_throughput'].mean().reset_index()
    
    for alloc_type in ['lustre', 'regular']:
        data = avg_throughput[avg_throughput['allocation_type'] == alloc_type]
        ax1.plot(data['array_size'], data['alloc_throughput'] / 1e6, 
                marker='o', label=f'{alloc_type.capitalize()}', linewidth=2, markersize=6)
    
    ax1.set_xlabel('Array Size')
    ax1.set_ylabel('Throughput (MB/s)')
    ax1.set_title('Allocation Throughput')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    ax1.set_xscale('log')
    
    # Cycles per byte
    ax2 = axes[0, 1]
    avg_cycles_per_byte = df.groupby(['array_size', 'allocation_type'])['cycles_per_byte'].mean().reset_index()
    
    for alloc_type in ['lustre', 'regular']:
        data = avg_cycles_per_byte[avg_cycles_per_byte['allocation_type'] == alloc_type]
        ax2.plot(data['array_size'], data['cycles_per_byte'], 
                marker='s', label=f'{alloc_type.capitalize()}', linewidth=2, markersize=6)
    
    ax2.set_xlabel('Array Size')
    ax2.set_ylabel('CPU Cycles per Byte')
    ax2.set_title('CPU Efficiency (Lower is Better)')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    
    # Memory footprint analysis
    ax3 = axes[1, 0]
    ax3.plot(df['array_size'].unique(), 
             [size * 32 / 1024 for size in df['array_size'].unique()], 
             marker='o', linewidth=2, markersize=6, color='purple')
    
    ax3.set_xlabel('Array Size')
    ax3.set_ylabel('Memory Footprint (KB)')
    ax3.set_title('Memory Footprint')
    ax3.grid(True, alpha=0.3)
    ax3.set_xscale('log')
    ax3.set_yscale('log')
    
    # Summary statistics
    ax4 = axes[1, 1]
    summary_stats = df.groupby('allocation_type').agg({
        'alloc_time_ns': ['mean', 'std'],
        'alloc_throughput': ['mean', 'std']
    }).round(2)
    
    ax4.axis('tight')
    ax4.axis('off')
    table_data = []
    for alloc_type in ['lustre', 'regular']:
        mean_time = summary_stats.loc[alloc_type, ('alloc_time_ns', 'mean')] / 1000
        std_time = summary_stats.loc[alloc_type, ('alloc_time_ns', 'std')] / 1000
        mean_throughput = summary_stats.loc[alloc_type, ('alloc_throughput', 'mean')] / 1e6
        std_throughput = summary_stats.loc[alloc_type, ('alloc_throughput', 'std')] / 1e6
        
        table_data.append([
            alloc_type.capitalize(),
            f"{mean_time:.2f} ± {std_time:.2f}",
            f"{mean_throughput:.2f} ± {std_throughput:.2f}"
        ])
    
    table = ax4.table(cellText=table_data,
                     colLabels=['Allocation Type', 'Avg Time (μs)', 'Avg Throughput (MB/s)'],
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
    print("\n" + "="*60)
    print("MEMORY ALLOCATION BENCHMARK REPORT")
    print("="*60)
    
    print(f"\nTotal test runs: {len(df)}")
    print(f"Array sizes tested: {sorted(df['array_size'].unique())}")
    print(f"Allocation patterns: {list(df['pattern'].unique())}")
    print(f"Allocation types: {list(df['allocation_type'].unique())}")
    
    print("\n--- Performance Summary ---")
    for alloc_type in ['lustre', 'regular']:
        data = df[df['allocation_type'] == alloc_type]
        avg_time = data['alloc_time_ns'].mean() / 1000
        min_time = data['alloc_time_ns'].min() / 1000
        max_time = data['alloc_time_ns'].max() / 1000
        
        print(f"\n{alloc_type.upper()} Allocation:")
        print(f"  Average allocation time: {avg_time:.2f} μs")
        print(f"  Range: {min_time:.2f} - {max_time:.2f} μs")
        print(f"  Average throughput: {data['bytes_allocated'].sum() / (data['alloc_time_ns'].sum() / 1e9) / 1e6:.2f} MB/s")
    
    # Find best performing configurations
    best_lustre = df[df['allocation_type'] == 'lustre'].loc[df[df['allocation_type'] == 'lustre']['alloc_time_ns'].idxmin()]
    best_regular = df[df['allocation_type'] == 'regular'].loc[df[df['allocation_type'] == 'regular']['alloc_time_ns'].idxmin()]
    
    print(f"\n--- Best Performance ---")
    print(f"Best Lustre: {best_lustre['alloc_time_ns']/1000:.2f} μs (size={best_lustre['array_size']}, pattern={best_lustre['pattern']})")
    print(f"Best Regular: {best_regular['alloc_time_ns']/1000:.2f} μs (size={best_regular['array_size']}, pattern={best_regular['pattern']})")

def main():
    """Main function to run all analyses"""
    print("Memory Allocation Benchmark Analysis")
    print("====================================")
    
    # Load data
    df = load_data()
    if df is None:
        return
    
    # Generate all plots
    print("\nGenerating allocation comparison plots...")
    plot_allocation_comparison(df)
    
    print("Generating operation phases plots...")
    plot_operation_phases(df)
    
    print("Generating pattern analysis plots...")
    plot_pattern_analysis(df)
    
    print("Generating efficiency analysis plots...")
    plot_efficiency_analysis(df)
    
    # Generate summary report
    generate_report(df)
    
    print(f"\nAnalysis complete! Generated plots:")
    print("  - allocation_comparison.png")
    print("  - operation_phases.png") 
    print("  - pattern_analysis.png")
    print("  - efficiency_analysis.png")

if __name__ == "__main__":
    main()