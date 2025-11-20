#!/usr/bin/env python3

import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
import argparse
import os
import sys
from pathlib import Path

# Set style for publication-quality plots
plt.style.use('seaborn-v0_8')
sns.set_palette("husl")

def load_combined_results(results_dir):
    """Load the combined results CSV file."""
    combined_file = Path(results_dir) / "combined_results.csv"
    if not combined_file.exists():
        print(f"Error: {combined_file} not found")
        sys.exit(1)
    
    try:
        df = pd.read_csv(combined_file)
        # Convert latency from nanoseconds to microseconds for better readability
        df['latency_us'] = df['latency_ns'] / 1000
        return df
    except Exception as e:
        print(f"Error loading results: {e}")
        sys.exit(1)

def plot_latency_distributions(df, output_dir):
    """Plot latency distributions for each test type."""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Latency Distributions by Test Type', fontsize=16, fontweight='bold')
    
    test_types = df['test_name'].unique()
    
    for idx, test_name in enumerate(test_types[:4]):  # Plot first 4 test types
        row, col = idx // 2, idx % 2
        ax = axes[row, col]
        
        test_data = df[df['test_name'] == test_name]['latency_us']
        
        # Create histogram with log scale for better visibility
        ax.hist(test_data, bins=50, alpha=0.7, edgecolor='black', linewidth=0.5)
        ax.set_xlabel('Latency (μs)')
        ax.set_ylabel('Frequency')
        ax.set_title(f'{test_name.replace("_", " ").title()}')
        ax.set_yscale('log')
        
        # Add statistics text
        median_lat = test_data.median()
        p95_lat = test_data.quantile(0.95)
        ax.axvline(median_lat, color='red', linestyle='--', alpha=0.8, label=f'Median: {median_lat:.1f}μs')
        ax.axvline(p95_lat, color='orange', linestyle='--', alpha=0.8, label=f'95th: {p95_lat:.1f}μs')
        ax.legend()
    
    plt.tight_layout()
    plt.savefig(Path(output_dir) / "latency_distributions.png", dpi=300, bbox_inches='tight')
    plt.close()

def plot_operation_comparison(df, output_dir):
    """Plot comparison of operations within each test."""
    fig, axes = plt.subplots(2, 3, figsize=(18, 12))
    fig.suptitle('Operation Latency Comparison by Test Type', fontsize=16, fontweight='bold')
    
    test_types = df['test_name'].unique()
    
    for idx, test_name in enumerate(test_types[:6]):  # Plot up to 6 test types
        row, col = idx // 3, idx % 3
        ax = axes[row, col]
        
        test_data = df[df['test_name'] == test_name]
        
        # Create box plot for different operations in this test
        operations = test_data['operation'].unique()
        if len(operations) > 1:
            sns.boxplot(data=test_data, x='operation', y='latency_us', ax=ax)
            ax.set_xticklabels(ax.get_xticklabels(), rotation=45, ha='right')
        else:
            # If only one operation type, show distribution
            ax.hist(test_data['latency_us'], bins=30, alpha=0.7)
            ax.set_xlabel('Latency (μs)')
            ax.set_ylabel('Frequency')
        
        ax.set_title(f'{test_name.replace("_", " ").title()}')
        ax.set_yscale('log')
    
    # Hide unused subplots
    for idx in range(len(test_types), 6):
        row, col = idx // 3, idx % 3
        axes[row, col].set_visible(False)
    
    plt.tight_layout()
    plt.savefig(Path(output_dir) / "operation_comparison.png", dpi=300, bbox_inches='tight')
    plt.close()

def plot_throughput_over_time(df, output_dir):
    """Plot throughput over time for each test."""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Throughput Over Time', fontsize=16, fontweight='bold')
    
    test_types = df['test_name'].unique()[:4]
    
    for idx, test_name in enumerate(test_types):
        row, col = idx // 2, idx % 2
        ax = axes[row, col]
        
        test_data = df[df['test_name'] == test_name].copy()
        
        # Calculate rolling throughput (operations per second)
        # Group by time windows of 100 operations
        test_data['window'] = test_data['iteration'] // 100
        throughput = test_data.groupby('window').agg({
            'latency_us': ['count', 'sum']
        }).reset_index()
        
        throughput.columns = ['window', 'ops_count', 'total_latency_us']
        throughput['throughput_ops_sec'] = throughput['ops_count'] / (throughput['total_latency_us'] / 1e6)
        
        ax.plot(throughput['window'], throughput['throughput_ops_sec'], marker='o', linewidth=2, markersize=4)
        ax.set_xlabel('Time Window (×100 operations)')
        ax.set_ylabel('Throughput (ops/sec)')
        ax.set_title(f'{test_name.replace("_", " ").title()}')
        ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(Path(output_dir) / "throughput_over_time.png", dpi=300, bbox_inches='tight')
    plt.close()

def plot_latency_percentiles(df, output_dir):
    """Plot latency percentiles comparison across tests."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))
    fig.suptitle('Latency Percentiles Comparison', fontsize=16, fontweight='bold')
    
    # Calculate percentiles for each test
    percentiles = [50, 75, 90, 95, 99]
    test_stats = []
    
    for test_name in df['test_name'].unique():
        test_data = df[df['test_name'] == test_name]['latency_us']
        stats = {
            'test_name': test_name.replace('_', ' ').title(),
            'count': len(test_data),
            'mean': test_data.mean(),
            'std': test_data.std()
        }
        
        for p in percentiles:
            stats[f'p{p}'] = test_data.quantile(p/100)
        
        test_stats.append(stats)
    
    stats_df = pd.DataFrame(test_stats)
    
    # Plot 1: Percentiles comparison
    x_pos = np.arange(len(stats_df))
    width = 0.15
    
    for i, p in enumerate(percentiles):
        ax1.bar(x_pos + i*width, stats_df[f'p{p}'], width, label=f'{p}th percentile')
    
    ax1.set_xlabel('Test Type')
    ax1.set_ylabel('Latency (μs)')
    ax1.set_title('Latency Percentiles by Test')
    ax1.set_xticks(x_pos + width * 2)
    ax1.set_xticklabels(stats_df['test_name'], rotation=45, ha='right')
    ax1.legend()
    ax1.set_yscale('log')
    ax1.grid(True, alpha=0.3)
    
    # Plot 2: Mean vs Std deviation
    ax2.scatter(stats_df['mean'], stats_df['std'], s=100, alpha=0.7)
    
    for i, row in stats_df.iterrows():
        ax2.annotate(row['test_name'], (row['mean'], row['std']), 
                    xytext=(5, 5), textcoords='offset points', fontsize=8)
    
    ax2.set_xlabel('Mean Latency (μs)')
    ax2.set_ylabel('Standard Deviation (μs)')
    ax2.set_title('Latency Mean vs Variability')
    ax2.set_xscale('log')
    ax2.set_yscale('log')
    ax2.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig(Path(output_dir) / "latency_percentiles.png", dpi=300, bbox_inches='tight')
    plt.close()

def plot_error_analysis(df, output_dir):
    """Plot error rates and analysis."""
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))
    fig.suptitle('Error Analysis', fontsize=16, fontweight='bold')
    
    # Error rates by test type
    error_stats = df.groupby('test_name')['errors'].agg(['sum', 'count']).reset_index()
    error_stats['error_rate'] = error_stats['sum'] / error_stats['count'] * 100
    
    # Plot 1: Error rates
    bars = ax1.bar(range(len(error_stats)), error_stats['error_rate'])
    ax1.set_xlabel('Test Type')
    ax1.set_ylabel('Error Rate (%)')
    ax1.set_title('Error Rates by Test Type')
    ax1.set_xticks(range(len(error_stats)))
    ax1.set_xticklabels([name.replace('_', ' ').title() for name in error_stats['test_name']], 
                       rotation=45, ha='right')
    
    # Color bars based on error rate
    for bar, rate in zip(bars, error_stats['error_rate']):
        if rate > 5:
            bar.set_color('red')
        elif rate > 1:
            bar.set_color('orange')
        else:
            bar.set_color('green')
    
    ax1.grid(True, alpha=0.3)
    
    # Plot 2: Latency vs Errors scatter
    scatter_data = df[df['errors'] > 0]  # Only show data points with errors
    
    if not scatter_data.empty:
        ax2.scatter(scatter_data['latency_us'], scatter_data['errors'], alpha=0.6)
        ax2.set_xlabel('Latency (μs)')
        ax2.set_ylabel('Errors')
        ax2.set_title('Latency vs Error Correlation')
        ax2.set_xscale('log')
        ax2.grid(True, alpha=0.3)
    else:
        ax2.text(0.5, 0.5, 'No errors detected', transform=ax2.transAxes, 
                ha='center', va='center', fontsize=14)
        ax2.set_title('No Errors Detected')
    
    plt.tight_layout()
    plt.savefig(Path(output_dir) / "error_analysis.png", dpi=300, bbox_inches='tight')
    plt.close()

def generate_summary_table(df, output_dir):
    """Generate a summary table with key statistics."""
    summary_data = []
    
    for test_name in df['test_name'].unique():
        test_data = df[df['test_name'] == test_name]['latency_us']
        errors = df[df['test_name'] == test_name]['errors'].sum()
        total_ops = len(test_data)
        
        summary_data.append({
            'Test': test_name.replace('_', ' ').title(),
            'Operations': total_ops,
            'Errors': errors,
            'Error Rate (%)': f"{errors/total_ops*100:.2f}",
            'Min (μs)': f"{test_data.min():.2f}",
            'Median (μs)': f"{test_data.median():.2f}",
            'Mean (μs)': f"{test_data.mean():.2f}",
            'Max (μs)': f"{test_data.max():.2f}",
            'P95 (μs)': f"{test_data.quantile(0.95):.2f}",
            'P99 (μs)': f"{test_data.quantile(0.99):.2f}",
            'Std Dev (μs)': f"{test_data.std():.2f}"
        })
    
    summary_df = pd.DataFrame(summary_data)
    
    # Save as CSV
    summary_df.to_csv(Path(output_dir) / "summary_statistics.csv", index=False)
    
    # Create a nice table plot
    fig, ax = plt.subplots(figsize=(16, 8))
    ax.axis('tight')
    ax.axis('off')
    
    table = ax.table(cellText=summary_df.values, colLabels=summary_df.columns,
                    cellLoc='center', loc='center', fontsize=10)
    
    table.auto_set_font_size(False)
    table.set_fontsize(9)
    table.scale(1.2, 1.5)
    
    # Style the table
    for i in range(len(summary_df.columns)):
        table[(0, i)].set_facecolor('#4CAF50')
        table[(0, i)].set_text_props(weight='bold', color='white')
    
    plt.title('Lustre DRAM Overhead Test Summary', fontsize=16, fontweight='bold', pad=20)
    plt.savefig(Path(output_dir) / "summary_table.png", dpi=300, bbox_inches='tight')
    plt.close()
    
    return summary_df

def main():
    parser = argparse.ArgumentParser(description='Plot Lustre DRAM overhead test results')
    parser.add_argument('results_dir', help='Directory containing test results')
    parser.add_argument('--output', '-o', default=None, 
                       help='Output directory for plots (default: results_dir/plots)')
    parser.add_argument('--format', choices=['png', 'pdf', 'svg'], default='png',
                       help='Output format for plots')
    
    args = parser.parse_args()
    
    # Set up directories
    results_dir = Path(args.results_dir)
    if not results_dir.exists():
        print(f"Error: Results directory {results_dir} does not exist")
        sys.exit(1)
    
    output_dir = Path(args.output) if args.output else results_dir / "plots"
    output_dir.mkdir(exist_ok=True)
    
    print(f"Loading results from: {results_dir}")
    print(f"Saving plots to: {output_dir}")
    
    # Load data
    df = load_combined_results(results_dir)
    print(f"Loaded {len(df)} data points from {df['test_name'].nunique()} test types")
    
    if len(df) == 0:
        print("No test data found. Check that:")
        print("1. Test scripts completed successfully")
        print("2. CSV files were generated")
        print("3. Combined results file exists")
        sys.exit(1)
    
    # Generate plots
    print("Generating latency distribution plots...")
    plot_latency_distributions(df, output_dir)
    
    print("Generating operation comparison plots...")
    plot_operation_comparison(df, output_dir)
    
    print("Generating throughput over time plots...")
    plot_throughput_over_time(df, output_dir)
    
    print("Generating latency percentiles plots...")
    plot_latency_percentiles(df, output_dir)
    
    print("Generating error analysis plots...")
    plot_error_analysis(df, output_dir)
    
    print("Generating summary table...")
    summary_df = generate_summary_table(df, output_dir)
    
    # Print summary to console
    print("\n" + "="*80)
    print("TEST SUMMARY")
    print("="*80)
    print(summary_df.to_string(index=False))
    print("="*80)
    
    print(f"\nAll plots saved to: {output_dir}")
    print("Generated files:")
    print("  - latency_distributions.png")
    print("  - operation_comparison.png") 
    print("  - throughput_over_time.png")
    print("  - latency_percentiles.png")
    print("  - error_analysis.png")
    print("  - summary_table.png")
    print("  - summary_statistics.csv")

if __name__ == "__main__":
    main()