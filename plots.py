#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
from pathlib import Path

def load_and_process_data(filename='benchmark_results.csv'):
    """Load benchmark results and compute averages across runs."""
    try:
        df = pd.read_csv(filename)
        print(f"Loaded {len(df)} benchmark results")
        
        # Group by array_size and pattern, then compute mean and std
        grouped = df.groupby(['array_size', 'pattern']).agg({
            'alloc_cycles': ['mean', 'std'],
            'access_cycles': ['mean', 'std'], 
            'flush_cycles': ['mean', 'std'],
            'free_cycles': ['mean', 'std'],
            'alloc_time_ns': ['mean', 'std'],
            'access_time_ns': ['mean', 'std'],
            'flush_time_ns': ['mean', 'std'],
            'free_time_ns': ['mean', 'std']
        }).reset_index()
        
        # Flatten column names
        grouped.columns = ['_'.join(col).strip() if col[1] else col[0] for col in grouped.columns.values]
        grouped = grouped.rename(columns={'array_size_': 'array_size', 'pattern_': 'pattern'})
        
        return df, grouped
        
    except FileNotFoundError:
        print(f"Error: {filename} not found. Please run the C benchmark first.")
        return None, None

def plot_cycles_by_phase(grouped_df):
    """Plot CPU cycles for each phase vs array size."""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('CPU Cycles by Phase and Access Pattern', fontsize=16)
    
    phases = ['alloc', 'access', 'flush', 'free']
    colors = ['blue', 'red', 'green']
    patterns = grouped_df['pattern'].unique()
    
    for i, phase in enumerate(phases):
        ax = axes[i//2, i%2]
        
        for j, pattern in enumerate(patterns):
            pattern_data = grouped_df[grouped_df['pattern'] == pattern]
            
            mean_col = f'{phase}_cycles_mean'
            std_col = f'{phase}_cycles_std'
            
            ax.errorbar(pattern_data['array_size'], pattern_data[mean_col], 
                       yerr=pattern_data[std_col], label=pattern, 
                       marker='o', color=colors[j], alpha=0.7)
        
        ax.set_xlabel('Array Size')
        ax.set_ylabel('CPU Cycles')
        ax.set_title(f'{phase.capitalize()} Phase')
        ax.set_xscale('log')
        ax.set_yscale('log')
        ax.legend()
        ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('cycles_by_phase.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_time_by_phase(grouped_df):
    """Plot execution time for each phase vs array size."""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Execution Time by Phase and Access Pattern', fontsize=16)
    
    phases = ['alloc', 'access', 'flush', 'free']
    colors = ['blue', 'red', 'green']
    patterns = grouped_df['pattern'].unique()
    
    for i, phase in enumerate(phases):
        ax = axes[i//2, i%2]
        
        for j, pattern in enumerate(patterns):
            pattern_data = grouped_df[grouped_df['pattern'] == pattern]
            
            mean_col = f'{phase}_time_ns_mean'
            std_col = f'{phase}_time_ns_std'
            
            # Convert to microseconds for better readability
            mean_us = pattern_data[mean_col] / 1000
            std_us = pattern_data[std_col] / 1000
            
            ax.errorbar(pattern_data['array_size'], mean_us, 
                       yerr=std_us, label=pattern, 
                       marker='o', color=colors[j], alpha=0.7)
        
        ax.set_xlabel('Array Size')
        ax.set_ylabel('Time (microseconds)')
        ax.set_title(f'{phase.capitalize()} Phase')
        ax.set_xscale('log')
        ax.set_yscale('log')
        ax.legend()
        ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('time_by_phase.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_pattern_comparison(grouped_df):
    """Compare access patterns for the access phase."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))
    
    patterns = grouped_df['pattern'].unique()
    colors = ['blue', 'red', 'green']
    
    # CPU Cycles comparison
    for i, pattern in enumerate(patterns):
        pattern_data = grouped_df[grouped_df['pattern'] == pattern]
        ax1.errorbar(pattern_data['array_size'], pattern_data['access_cycles_mean'],
                    yerr=pattern_data['access_cycles_std'], 
                    label=pattern, marker='o', color=colors[i], alpha=0.7)
    
    ax1.set_xlabel('Array Size')
    ax1.set_ylabel('CPU Cycles')
    ax1.set_title('Access Phase - CPU Cycles by Pattern')
    ax1.set_xscale('log')
    ax1.set_yscale('log')
    ax1.legend()
    ax1.grid(True, alpha=0.3)
    
    # Time comparison
    for i, pattern in enumerate(patterns):
        pattern_data = grouped_df[grouped_df['pattern'] == pattern]
        mean_us = pattern_data['access_time_ns_mean'] / 1000
        std_us = pattern_data['access_time_ns_std'] / 1000
        ax2.errorbar(pattern_data['array_size'], mean_us,
                    yerr=std_us, 
                    label=pattern, marker='o', color=colors[i], alpha=0.7)
    
    ax2.set_xlabel('Array Size')
    ax2.set_ylabel('Time (microseconds)')
    ax2.set_title('Access Phase - Time by Pattern')
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('pattern_comparison.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_heatmap(df):
    """Create a heatmap showing performance across different configurations."""
    # Prepare data for heatmap
    pivot_data = df.groupby(['array_size', 'pattern'])['access_cycles'].mean().unstack()
    
    plt.figure(figsize=(10, 8))
    sns.heatmap(pivot_data, annot=True, fmt='.0f', cmap='viridis', 
                cbar_kws={'label': 'Average CPU Cycles'})
    plt.title('Access Phase Performance Heatmap\n(CPU Cycles by Array Size and Pattern)')
    plt.xlabel('Access Pattern')
    plt.ylabel('Array Size')
    plt.tight_layout()
    plt.savefig('performance_heatmap.png', dpi=300, bbox_inches='tight')
    plt.show()

def plot_scaling_analysis(grouped_df):
    """Analyze how performance scales with array size."""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Performance Scaling Analysis', fontsize=16)
    
    patterns = grouped_df['pattern'].unique()
    colors = ['blue', 'red', 'green']
    
    # For each pattern, plot cycles per element
    for i, pattern in enumerate(patterns):
        pattern_data = grouped_df[grouped_df['pattern'] == pattern]
        
        # Allocation cycles per element
        cycles_per_elem = pattern_data['alloc_cycles_mean'] / pattern_data['array_size']
        axes[0,0].plot(pattern_data['array_size'], cycles_per_elem, 
                      label=pattern, marker='o', color=colors[i])
        
        # Access cycles per element  
        cycles_per_elem = pattern_data['access_cycles_mean'] / pattern_data['array_size']
        axes[0,1].plot(pattern_data['array_size'], cycles_per_elem,
                      label=pattern, marker='o', color=colors[i])
        
        # Flush cycles per element
        cycles_per_elem = pattern_data['flush_cycles_mean'] / pattern_data['array_size']
        axes[1,0].plot(pattern_data['array_size'], cycles_per_elem,
                      label=pattern, marker='o', color=colors[i])
        
        # Total cycles
        total_cycles = (pattern_data['alloc_cycles_mean'] + 
                       pattern_data['access_cycles_mean'] + 
                       pattern_data['flush_cycles_mean'] + 
                       pattern_data['free_cycles_mean'])
        axes[1,1].plot(pattern_data['array_size'], total_cycles,
                      label=pattern, marker='o', color=colors[i])
    
    titles = ['Allocation Cycles per Element', 'Access Cycles per Element', 
              'Flush Cycles per Element', 'Total Cycles']
    
    for i, (ax, title) in enumerate(zip(axes.flat, titles)):
        ax.set_xlabel('Array Size')
        ax.set_ylabel('Cycles' if i == 3 else 'Cycles per Element')
        ax.set_title(title)
        ax.set_xscale('log')
        if i != 3:  # Don't use log scale for total cycles
            ax.set_yscale('log')
        ax.legend()
        ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('scaling_analysis.png', dpi=300, bbox_inches='tight')
    plt.show()

def print_summary_stats(df, grouped_df):
    """Print summary statistics."""
    print("\n" + "="*60)
    print("BENCHMARK SUMMARY STATISTICS")
    print("="*60)
    
    print(f"\nTotal benchmark runs: {len(df)}")
    print(f"Array sizes tested: {sorted(df['array_size'].unique())}")
    print(f"Access patterns tested: {list(df['pattern'].unique())}")
    
    print("\nMean performance across all configurations:")
    for phase in ['alloc', 'access', 'flush', 'free']:
        cycles_col = f'{phase}_cycles_mean'
        time_col = f'{phase}_time_ns_mean'
        
        mean_cycles = grouped_df[cycles_col].mean()
        mean_time_us = grouped_df[time_col].mean() / 1000
        
        print(f"  {phase.capitalize()}: {mean_cycles:.0f} cycles, {mean_time_us:.2f} μs")
    
    print("\nPerformance by pattern (access phase):")
    for pattern in grouped_df['pattern'].unique():
        pattern_data = grouped_df[grouped_df['pattern'] == pattern]
        mean_cycles = pattern_data['access_cycles_mean'].mean()
        mean_time_us = pattern_data['access_time_ns_mean'].mean() / 1000
        print(f"  {pattern}: {mean_cycles:.0f} cycles, {mean_time_us:.2f} μs")

def main():
    """Main function to run all analyses."""
    print("Loading benchmark results...")
    
    # Load data
    df, grouped_df = load_and_process_data()
    if df is None:
        return
    
    # Print summary
    print_summary_stats(df, grouped_df)
    
    # Generate plots
    print("\nGenerating plots...")
    
    plot_cycles_by_phase(grouped_df)
    plot_time_by_phase(grouped_df)
    plot_pattern_comparison(grouped_df)
    plot_heatmap(df)
    plot_scaling_analysis(grouped_df)
    
    print("\nAnalysis complete! Check the generated PNG files for visualizations.")

if __name__ == "__main__":
    main()