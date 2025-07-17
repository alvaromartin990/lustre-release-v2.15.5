#!/usr/bin/env python3
"""
Enhanced Lustre Memory Benchmark Analysis Script
Analyzes benchmark output and kernel logs to generate comparative plots
"""

import sys
import re
import json
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np
from datetime import datetime
import argparse
import subprocess
import os
from pathlib import Path

class BenchmarkAnalyzer:
    def __init__(self, log_file=None, kernel_log_file=None):
        self.log_file = log_file
        self.kernel_log_file = kernel_log_file
        self.benchmark_data = []
        self.kernel_data = []
        self.comparison_data = []
        
        # Set up plotting style
        plt.style.use('seaborn-v0_8')
        sns.set_palette("husl")
        
    def parse_benchmark_output(self, log_content):
        """Parse structured benchmark output"""
        
        # Parse benchmark start/end markers
        benchmark_pattern = r'=== BENCHMARK_END: (\w+) SUCCESS:(\d+) ALLOC_NS:(\d+) INIT_NS:(\d+) ACCESS_NS:(\d+) FLUSH_NS:(\d+) FREE_NS:(\d+) ALLOC_CYCLES:(\d+) ACCESS_CYCLES:(\d+) FLUSH_CYCLES:(\d+) FREE_CYCLES:(\d+) MEMORY_MB:([\d.]+) THROUGHPUT_MB_S:([\d.]+) ==='
        
        matches = re.findall(benchmark_pattern, log_content)
        
        for match in matches:
            data = {
                'test_name': match[0],
                'success': int(match[1]),
                'alloc_time_ns': int(match[2]),
                'init_time_ns': int(match[3]),
                'access_time_ns': int(match[4]),
                'flush_time_ns': int(match[5]),
                'free_time_ns': int(match[6]),
                'alloc_cycles': int(match[7]),
                'access_cycles': int(match[8]),
                'flush_cycles': int(match[9]),
                'free_cycles': int(match[10]),
                'memory_mb': float(match[11]),
                'throughput_mb_s': float(match[12])
            }
            
            # Extract allocation type and pattern
            if 'lustre' in data['test_name']:
                data['alloc_type'] = 'lustre'
                data['pattern'] = data['test_name'].split('_')[-1]
            else:
                data['alloc_type'] = 'regular'
                data['pattern'] = data['test_name'].split('_')[-1]
            
            # Calculate total time
            data['total_time_ns'] = (data['alloc_time_ns'] + data['init_time_ns'] + 
                                   data['access_time_ns'] + data['flush_time_ns'] + 
                                   data['free_time_ns'])
            
            self.benchmark_data.append(data)
        
        # Parse comparison data
        comparison_pattern = r'COMPARISON: SIZE:(\d+) PATTERN:(\d+) LUSTRE_ALLOC_NS:(\d+) REGULAR_ALLOC_NS:(\d+) RATIO:([\d.]+)'
        comparison_matches = re.findall(comparison_pattern, log_content)
        
        for match in comparison_matches:
            comp_data = {
                'size': int(match[0]),
                'pattern': int(match[1]),
                'lustre_alloc_ns': int(match[2]),
                'regular_alloc_ns': int(match[3]),
                'ratio': float(match[4])
            }
            self.comparison_data.append(comp_data)
        
        # Parse multithreaded test results
        mt_pattern = r'=== MULTITHREADED_TEST_END: SUCCESS_RATE:([\d.]+)%% TOTAL_TIME_NS:(\d+) AVG_ALLOC_NS:(\d+) AVG_ACCESS_NS:(\d+) AVG_FLUSH_NS:(\d+) AVG_FREE_NS:(\d+) ==='
        mt_matches = re.findall(mt_pattern, log_content)
        
        for match in mt_matches:
            mt_data = {
                'test_type': 'multithreaded',
                'success_rate': float(match[0]),
                'total_time_ns': int(match[1]),
                'avg_alloc_ns': int(match[2]),
                'avg_access_ns': int(match[3]),
                'avg_flush_ns': int(match[4]),
                'avg_free_ns': int(match[5])
            }
            self.benchmark_data.append(mt_data)
        
        # Parse stress test results
        stress_pattern = r'=== STRESS_ITERATION:(\d+) SIZE:(\d+) PATTERN:(\d+) LUSTRE_TOTAL_NS:([\d.]+) REGULAR_TOTAL_NS:([\d.]+) RATIO:([\d.]+) ==='
        stress_matches = re.findall(stress_pattern, log_content)
        
        for match in stress_matches:
            stress_data = {
                'test_type': 'stress',
                'iteration': int(match[0]),
                'size': int(match[1]),
                'pattern': int(match[2]),
                'lustre_total_ns': float(match[3]),
                'regular_total_ns': float(match[4]),
                'ratio': float(match[5])
            }
            self.benchmark_data.append(stress_data)
    
    def parse_kernel_logs(self, kernel_log_content):
        """Parse kernel logs for memory allocation patterns"""
        
        # Common kernel memory allocation patterns
        patterns = [
            r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}.\d+\+\d{2}:\d{2})\s+.*kmalloc.*size=(\d+)',
            r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}.\d+\+\d{2}:\d{2})\s+.*vmalloc.*size=(\d+)',
            r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}.\d+\+\d{2}:\d{2})\s+.*OBD_ALLOC.*size=(\d+)',
            r'(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}.\d+\+\d{2}:\d{2})\s+.*lustre.*alloc.*(\d+)',
        ]
        
        for pattern in patterns:
            matches = re.findall(pattern, kernel_log_content)
            for match in matches:
                kernel_data = {
                    'timestamp': match[0],
                    'size': int(match[1]),
                    'allocation_type': 'kernel'
                }
                self.kernel_data.append(kernel_data)
    
    def generate_comparison_plots(self, output_dir='plots'):
        """Generate comprehensive comparison plots"""
        
        os.makedirs(output_dir, exist_ok=True)
        
        if not self.benchmark_data:
            print("No benchmark data to plot")
            return
        
        # Convert to DataFrame for easier manipulation
        df = pd.DataFrame(self.benchmark_data)
        
        # Plot 1: Allocation Time Comparison by Pattern
        fig, axes = plt.subplots(2, 2, figsize=(15, 12))
        fig.suptitle('Lustre vs Regular Memory Allocation Performance', fontsize=16)
        
        # Filter data for different allocation types
        lustre_data = df[df.get('alloc_type') == 'lustre']
        regular_data = df[df.get('alloc_type') == 'regular']
        
        if not lustre_data.empty and not regular_data.empty:
            # Allocation time comparison
            ax1 = axes[0, 0]
            patterns = ['0', '1', '2']  # Sequential, Random, Strided
            pattern_names = ['Sequential', 'Random', 'Strided']
            
            lustre_alloc_times = []
            regular_alloc_times = []
            
            for pattern in patterns:
                lustre_pattern = lustre_data[lustre_data['pattern'] == pattern]
                regular_pattern = regular_data[regular_data['pattern'] == pattern]
                
                lustre_alloc_times.append(lustre_pattern['alloc_time_ns'].mean() if not lustre_pattern.empty else 0)
                regular_alloc_times.append(regular_pattern['alloc_time_ns'].mean() if not regular_pattern.empty else 0)
            
            x = np.arange(len(pattern_names))
            width = 0.35
            
            ax1.bar(x - width/2, lustre_alloc_times, width, label='Lustre', alpha=0.8)
            ax1.bar(x + width/2, regular_alloc_times, width, label='Regular', alpha=0.8)
            ax1.set_xlabel('Access Pattern')
            ax1.set_ylabel('Allocation Time (ns)')
            ax1.set_title('Allocation Time by Access Pattern')
            ax1.set_xticks(x)
            ax1.set_xticklabels(pattern_names)
            ax1.legend()
            ax1.grid(True, alpha=0.3)
        
        # Plot 2: Memory Throughput Comparison
        ax2 = axes[0, 1]
        if not lustre_data.empty and not regular_data.empty:
            throughput_data = []
            labels = []
            
            for pattern in patterns:
                lustre_pattern = lustre_data[lustre_data['pattern'] == pattern]
                regular_pattern = regular_data[regular_data['pattern'] == pattern]
                
                if not lustre_pattern.empty:
                    throughput_data.append(lustre_pattern['throughput_mb_s'].mean())
                    labels.append(f'Lustre-{pattern_names[int(pattern)]}')
                
                if not regular_pattern.empty:
                    throughput_data.append(regular_pattern['throughput_mb_s'].mean())
                    labels.append(f'Regular-{pattern_names[int(pattern)]}')
            
            colors = plt.cm.Set3(np.linspace(0, 1, len(throughput_data)))
            ax2.bar(range(len(throughput_data)), throughput_data, color=colors)
            ax2.set_xlabel('Test Type')
            ax2.set_ylabel('Throughput (MB/s)')
            ax2.set_title('Memory Throughput Comparison')
            ax2.set_xticks(range(len(labels)))
            ax2.set_xticklabels(labels, rotation=45)
            ax2.grid(True, alpha=0.3)
        
        # Plot 3: Phase-wise Time Breakdown
        ax3 = axes[1, 0]
        if not lustre_data.empty:
            phases = ['alloc_time_ns', 'init_time_ns', 'access_time_ns', 'flush_time_ns', 'free_time_ns']
            phase_names = ['Allocation', 'Initialization', 'Access', 'Flush', 'Free']
            
            lustre_phases = [lustre_data[phase].mean() for phase in phases]
            regular_phases = [regular_data[phase].mean() if not regular_data.empty else 0 for phase in phases]
            
            x = np.arange(len(phase_names))
            width = 0.35
            
            ax3.bar(x - width/2, lustre_phases, width, label='Lustre', alpha=0.8)
            ax3.bar(x + width/2, regular_phases, width, label='Regular', alpha=0.8)
            ax3.set_xlabel('Phase')
            ax3.set_ylabel('Time (ns)')
            ax3.set_title('Phase-wise Time Breakdown')
            ax3.set_xticks(x)
            ax3.set_xticklabels(phase_names, rotation=45)
            ax3.legend()
            ax3.grid(True, alpha=0.3)
        
        # Plot 4: Size vs Performance (if comparison data available)
        ax4 = axes[1, 1]
        if self.comparison_data:
            comp_df = pd.DataFrame(self.comparison_data)
            sizes = comp_df['size'].unique()
            ratios = [comp_df[comp_df['size'] == size]['ratio'].mean() for size in sizes]
            
            ax4.semilogx(sizes, ratios, 'o-', markersize=8, linewidth=2)
            ax4.set_xlabel('Array Size')
            ax4.set_ylabel('Lustre/Regular Ratio')
            ax4.set_title('Performance Ratio vs Array Size')
            ax4.grid(True, alpha=0.3)
            ax4.axhline(y=1.0, color='r', linestyle='--', alpha=0.5, label='Equal Performance')
            ax4.legend()
        
        plt.tight_layout()
        plt.savefig(f'{output_dir}/allocation_comparison.png', dpi=300, bbox_inches='tight')
        plt.close()
        
        # Additional detailed plots
        self.plot_detailed_analysis(output_dir)
    
    def plot_detailed_analysis(self, output_dir):
        """Generate detailed analysis plots"""
        
        # Stress test analysis
        stress_data = [d for d in self.benchmark_data if d.get('test_type') == 'stress']
        if stress_data:
            fig, axes = plt.subplots(2, 2, figsize=(15, 10))
            fig.suptitle('Stress Test Analysis', fontsize=16)
            
            stress_df = pd.DataFrame(stress_data)
            
            # Distribution of performance ratios
            ax1 = axes[0, 0]
            ax1.hist(stress_df['ratio'], bins=20, alpha=0.7, edgecolor='black')
            ax1.set_xlabel('Lustre/Regular Ratio')
            ax1.set_ylabel('Frequency')
            ax1.set_title('Distribution of Performance Ratios')
            ax1.axvline(x=1.0, color='r', linestyle='--', alpha=0.5)
            ax1.grid(True, alpha=0.3)
            
            # Size vs Ratio scatter plot
            ax2 = axes[0, 1]
            ax2.scatter(stress_df['size'], stress_df['ratio'], alpha=0.6, s=50)
            ax2.set_xlabel('Array Size')
            ax2.set_ylabel('Lustre/Regular Ratio')
            ax2.set_title('Performance Ratio vs Array Size')
            ax2.set_xscale('log')
            ax2.grid(True, alpha=0.3)
            
            # Pattern analysis
            ax3 = axes[1, 0]
            pattern_ratios = stress_df.groupby('pattern')['ratio'].mean()
            pattern_names = ['Sequential', 'Random', 'Strided']
            patterns = [0, 1, 2]
            
            ratios = [pattern_ratios.get(p, 0) for p in patterns]
            ax3.bar(pattern_names, ratios, alpha=0.7)
            ax3.set_xlabel('Access Pattern')
            ax3.set_ylabel('Average Ratio')
            ax3.set_title('Average Performance Ratio by Pattern')
            ax3.grid(True, alpha=0.3)
            
            # Timeline analysis
            ax4 = axes[1, 1]
            ax4.plot(stress_df['iteration'], stress_df['ratio'], 'o-', markersize=4)
            ax4.set_xlabel('Iteration')
            ax4.set_ylabel('Lustre/Regular Ratio')
            ax4.set_title('Performance Ratio Over Time')
            ax4.grid(True, alpha=0.3)
            
            plt.tight_layout()
            plt.savefig(f'{output_dir}/stress_analysis.png', dpi=300, bbox_inches='tight')
            plt.close()
        
        # Multithreaded analysis
        mt_data = [d for d in self.benchmark_data if d.get('test_type') == 'multithreaded']
        if mt_data:
            fig, axes = plt.subplots(1, 2, figsize=(12, 5))
            fig.suptitle('Multithreaded Performance Analysis', fontsize=16)
            
            mt_df = pd.DataFrame(mt_data)
            
            # Success rate
            ax1 = axes[0]
            ax1.bar(['Success Rate'], [mt_df['success_rate'].mean()], alpha=0.7)
            ax1.set_ylabel('Success Rate (%)')
            ax1.set_title('Multithreaded Test Success Rate')
            ax1.set_ylim(0, 100)
            ax1.grid(True, alpha=0.3)
            
            # Phase breakdown
            ax2 = axes[1]
            phases = ['avg_alloc_ns', 'avg_access_ns', 'avg_flush_ns', 'avg_free_ns']
            phase_names = ['Allocation', 'Access', 'Flush', 'Free']
            avg_times = [mt_df[phase].mean() for phase in phases]
            
            ax2.bar(phase_names, avg_times, alpha=0.7)
            ax2.set_ylabel('Average Time (ns)')
            ax2.set_title('Average Phase Times (Multithreaded)')
            ax2.grid(True, alpha=0.3)
            
            plt.tight_layout()
            plt.savefig(f'{output_dir}/multithreaded_analysis.png', dpi=300, bbox_inches='tight')
            plt.close()
    
    def generate_csv_reports(self, output_dir='reports'):
        """Generate CSV reports for further analysis"""
        
        os.makedirs(output_dir, exist_ok=True)
        
        # Main benchmark data
        if self.benchmark_data:
            df = pd.DataFrame(self.benchmark_data)
            df.to_csv(f'{output_dir}/benchmark_data.csv', index=False)
        
        # Comparison data
        if self.comparison_data:
            comp_df = pd.DataFrame(self.comparison_data)
            comp_df.to_csv(f'{output_dir}/comparison_data.csv', index=False)
        
        # Kernel data
        if self.kernel_data:
            kernel_df = pd.DataFrame(self.kernel_data)
            kernel_df.to_csv(f'{output_dir}/kernel_data.csv', index=False)
        
        # Summary statistics
        summary_stats = self.generate_summary_stats()
        with open(f'{output_dir}/summary_stats.json', 'w') as f:
            json.dump(summary_stats, f, indent=2)
    
    def generate_summary_stats(self):
        """Generate summary statistics"""
        
        stats = {
            'total_tests': len(self.benchmark_data),
            'successful_tests': len([d for d in self.benchmark_data if d.get('success', 1)]),
            'test_types': list(set(d.get('test_name', d.get('test_type', 'unknown')) for d in self.benchmark_data)),
        }
        
        # Performance comparison stats
        if self.comparison_data:
            comp_df = pd.DataFrame(self.comparison_data)
            stats['comparison_stats'] = {
                'avg_ratio': comp_df['ratio'].mean(),
                'min_ratio': comp_df['ratio'].min(),
                'max_ratio': comp_df['ratio'].max(),
                'std_ratio': comp_df['ratio'].std(),
                'lustre_better_count': len(comp_df[comp_df['ratio'] < 1.0]),
                'regular_better_count': len(comp_df[comp_df['ratio'] > 1.0])
            }
        
        return stats
    
    def analyze_from_files(self):
        """Analyze data from log files"""
        
        # Read benchmark log
        if self.log_file and os.path.exists(self.log_file):
            with open(self.log_file, 'r') as f:
                log_content = f.read()
            self.parse_benchmark_output(log_content)
        
        # Read kernel log
        if self.kernel_log_file and os.path.exists(self.kernel_log_file):
            with open(self.kernel_log_file, 'r') as f:
                kernel_content = f.read()
            self.parse_kernel_logs(kernel_content)
    
    def run_live_analysis(self):
        """Run live analysis by executing the benchmark"""
        
        print("Running enhanced micro-user benchmark...")
        
        # Compile and run the benchmark
        try:
            # Compile
            compile_cmd = ["gcc", "-o", "micro-user-enhanced", "micro-user-enhanced.c", "-lm", "-lpthread"]
            subprocess.run(compile_cmd, check=True)
            
            # Run and capture output
            run_cmd = ["./micro-user-enhanced"]
            result = subprocess.run(run_cmd, capture_output=True, text=True, timeout=300)
            
            # Parse the output
            self.parse_benchmark_output(result.stdout)
            
            # Also try to capture kernel logs
            try:
                kernel_cmd = ["dmesg", "-T"]
                kernel_result = subprocess.run(kernel_cmd, capture_output=True, text=True)
                self.parse_kernel_logs(kernel_result.stdout)
            except subprocess.SubprocessError:
                print("Warning: Could not capture kernel logs")
            
            print("Live analysis completed successfully")
            
        except subprocess.CalledProcessError as e:
            print(f"Error running benchmark: {e}")
            return False
        except subprocess.TimeoutExpired:
            print("Benchmark execution timed out")
            return False
        
        return True

def main():
    parser = argparse.ArgumentParser(description='Analyze Lustre memory allocation benchmarks')
    parser.add_argument('--log-file', '-l', help='Benchmark log file')
    parser.add_argument('--kernel-log', '-k', help='Kernel log file')
    parser.add_argument('--output-dir', '-o', default='analysis_output', help='Output directory')
    parser.add_argument('--live', action='store_true', help='Run live analysis')
    parser.add_argument('--no-plots', action='store_true', help='Skip plot generation')
    
    args = parser.parse_args()
    
    analyzer = BenchmarkAnalyzer(args.log_file, args.kernel_log)
    
    if args.live:
        if not analyzer.run_live_analysis():
            sys.exit(1)
    else:
        analyzer.analyze_from_files()
    
    if not analyzer.benchmark_data:
        print("No benchmark data found. Please check your log files or run with --live")
        sys.exit(1)
    
    # Generate reports
    os.makedirs(args.output_dir, exist_ok=True)
    analyzer.generate_csv_reports(f'{args.output_dir}/reports')
    
    # Generate plots
    if not args.no_plots:
        try:
            analyzer.generate_comparison_plots(f'{args.output_dir}/plots')
            print(f"Analysis complete! Results saved to {args.output_dir}/")
        except ImportError:
            print("Warning: matplotlib not available, skipping plot generation")
            print("Install with: pip install matplotlib seaborn pandas")
    
    # Print summary
    stats = analyzer.generate_summary_stats()
    print("\n=== ANALYSIS SUMMARY ===")
    print(f"Total tests: {stats['total_tests']}")
    print(f"Successful tests: {stats['successful_tests']}")
    print(f"Test types: {', '.join(stats['test_types'])}")
    
    if 'comparison_stats' in stats:
        comp_stats = stats['comparison_stats']
        print(f"Average Lustre/Regular ratio: {comp_stats['avg_ratio']:.3f}")
        print(f"Lustre better in {comp_stats['lustre_better_count']} tests")
        print(f"Regular better in {comp_stats['regular_better_count']} tests")

if __name__ == "__main__":
    main()