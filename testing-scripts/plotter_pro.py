#!/usr/bin/env python3

import matplotlib.pyplot as plt
import re
import numpy as np
from matplotlib.colors import LogNorm
import sys
from collections import defaultdict, Counter
import seaborn as sns

def parse_log(log_file_content):
    """Parse log file to extract timestamp, duration, operation, and MDT information."""
    # Multiple patterns to handle different log formats
    patterns = [
        # Pattern 1: [timestamp] ... MDT_TIMING: Operation TYPE (code) took X microseconds
        r'\[(\d+\.\d+)\].*MDT_TIMING.*Operation (\w+).*took (\d+) microseconds',
        # Pattern 2: timestamp ... MDT operation timing
        r'(\d+\.\d+).*MDT_TIMING.*Operation (\w+).*took (\d+) microseconds',
        # Pattern 3: General pattern
        r'.*(\d+\.\d+).*Operation (\w+).*took (\d+) microseconds'
    ]
    
    # Patterns to extract MDT identification
    mdt_patterns = [
        r'\[MDT:(\w+)\]',           # [MDT:0001] format
        r'MDT(\d+)',                # MDT0001 format
        r'mdt(\d+)',                # mdt0001 format
        r'lustre-MDT(\w+)',         # lustre-MDT0001 format
        r'MDT_(\w+)',               # MDT_0001 format
    ]
    
    timestamps = []
    durations = []
    operations = []
    mdts = []
    
    for line in log_file_content.split('\n'):
        # Try to extract timing information
        timestamp = None
        duration = None
        operation = None
        
        for pattern in patterns:
            match = re.search(pattern, line)
            if match:
                if len(match.groups()) == 3:
                    timestamp = float(match.group(1))
                    operation = match.group(2)
                    duration = int(match.group(3))
                    break
        
        if timestamp is None or duration is None or operation is None:
            continue
            
        # Try to extract MDT information
        mdt_id = "MDT_Unknown"
        for mdt_pattern in mdt_patterns:
            mdt_match = re.search(mdt_pattern, line)
            if mdt_match:
                mdt_id = f"MDT_{mdt_match.group(1)}"
                break
        
        # If no specific MDT pattern found, try to infer from hostname or other identifiers
        if mdt_id == "MDT_Unknown":
            # Look for node identifiers
            node_match = re.search(r'(\w+)-MDT\w*', line)
            if node_match:
                mdt_id = f"MDT_{node_match.group(1)}"
            else:
                # Try to extract from kernel log format
                kernel_match = re.search(r'kernel:\s*(.*?):', line)
                if kernel_match:
                    mdt_id = "MDT_Primary"
                else:
                    mdt_id = "MDT_0"
        
        timestamps.append(timestamp)
        durations.append(duration)
        operations.append(operation)
        mdts.append(mdt_id)
    
    return timestamps, durations, operations, mdts

def create_comprehensive_analysis(log_file_content):
    """Create comprehensive analysis with operation types and MDT information."""
    # Parse the log file
    timestamps, durations, operations, mdts = parse_log(log_file_content)
    
    if not timestamps:
        print("No timing data found in the log file.")
        return
    
    # Calculate elapsed times
    start_time = min(timestamps)
    elapsed_times = [t - start_time for t in timestamps]
    
    # Get unique operations and MDTs
    unique_ops = sorted(list(set(operations)))
    unique_mdts = sorted(list(set(mdts)))
    
    print(f"Found {len(timestamps)} operations across {len(unique_mdts)} MDTs")
    print(f"Operation types: {unique_ops}")
    print(f"MDTs identified: {unique_mdts}")
    
    # Create color maps
    op_colors = plt.cm.tab10(np.linspace(0, 1, len(unique_ops)))
    op_color_map = dict(zip(unique_ops, op_colors))
    
    mdt_markers = ['o', 's', '^', 'D', 'v', '<', '>', 'p', '*', 'h']
    mdt_marker_map = dict(zip(unique_mdts, mdt_markers[:len(unique_mdts)]))
    
    # Create comprehensive figure
    fig = plt.figure(figsize=(20, 16))
    
    # 1. Main scatter plot: Operations over time, colored by operation, shaped by MDT
    ax1 = plt.subplot(3, 2, 1)
    for op in unique_ops:
        for mdt in unique_mdts:
            # Get indices for this combination
            indices = [i for i, (o, m) in enumerate(zip(operations, mdts)) if o == op and m == mdt]
            if indices:
                ax1.scatter(
                    [elapsed_times[i] for i in indices],
                    [durations[i] for i in indices],
                    c=[op_color_map[op]],
                    marker=mdt_marker_map[mdt],
                    label=f"{op} ({mdt})" if len(unique_mdts) > 1 else op,
                    alpha=0.7,
                    s=50
                )
    
    ax1.set_xlabel('Time Elapsed (seconds)')
    ax1.set_ylabel('Duration (μs)')
    ax1.set_title('MDT Operations: Type (Color) & MDT (Shape)')
    ax1.grid(True, alpha=0.3)
    if len(unique_ops) * len(unique_mdts) <= 20:
        ax1.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
    
    # 2. Operation count by type and MDT
    ax2 = plt.subplot(3, 2, 2)
    op_mdt_counts = defaultdict(lambda: defaultdict(int))
    for op, mdt in zip(operations, mdts):
        op_mdt_counts[op][mdt] += 1
    
    # Prepare data for grouped bar chart
    mdt_width = 0.8 / len(unique_mdts)
    x_pos = np.arange(len(unique_ops))
    
    for i, mdt in enumerate(unique_mdts):
        counts = [op_mdt_counts[op][mdt] for op in unique_ops]
        ax2.bar(x_pos + i * mdt_width, counts, mdt_width, 
                label=mdt, alpha=0.8)
    
    ax2.set_xlabel('Operation Type')
    ax2.set_ylabel('Count')
    ax2.set_title('Operation Count by Type and MDT')
    ax2.set_xticks(x_pos + mdt_width * (len(unique_mdts) - 1) / 2)
    ax2.set_xticklabels(unique_ops, rotation=45)
    if len(unique_mdts) > 1:
        ax2.legend()
    ax2.grid(True, alpha=0.3)
    
    # 3. Average duration by operation and MDT
    ax3 = plt.subplot(3, 2, 3)
    op_mdt_durations = defaultdict(lambda: defaultdict(list))
    for op, mdt, dur in zip(operations, mdts, durations):
        op_mdt_durations[op][mdt].append(dur)
    
    for i, mdt in enumerate(unique_mdts):
        avg_durations = [np.mean(op_mdt_durations[op][mdt]) if op_mdt_durations[op][mdt] 
                        else 0 for op in unique_ops]
        ax3.bar(x_pos + i * mdt_width, avg_durations, mdt_width, 
                label=mdt, alpha=0.8)
    
    ax3.set_xlabel('Operation Type')
    ax3.set_ylabel('Average Duration (μs)')
    ax3.set_title('Average Operation Duration by Type and MDT')
    ax3.set_xticks(x_pos + mdt_width * (len(unique_mdts) - 1) / 2)
    ax3.set_xticklabels(unique_ops, rotation=45)
    if len(unique_mdts) > 1:
        ax3.legend()
    ax3.grid(True, alpha=0.3)
    
    # 4. Timeline view by MDT
    ax4 = plt.subplot(3, 2, 4)
    for i, mdt in enumerate(unique_mdts):
        mdt_indices = [j for j, m in enumerate(mdts) if m == mdt]
        if mdt_indices:
            ax4.plot([elapsed_times[j] for j in mdt_indices],
                    [i] * len(mdt_indices), 'o', 
                    label=mdt, markersize=4)
    
    ax4.set_xlabel('Time Elapsed (seconds)')
    ax4.set_ylabel('MDT')
    ax4.set_title('Operation Timeline by MDT')
    ax4.set_yticks(range(len(unique_mdts)))
    ax4.set_yticklabels(unique_mdts)
    ax4.grid(True, alpha=0.3)
    
    # 5. 2D Histogram of all operations
    ax5 = plt.subplot(3, 2, 5)
    h = ax5.hist2d(elapsed_times, durations, bins=[20, 20], 
                   cmap='viridis', norm=LogNorm())
    plt.colorbar(h[3], ax=ax5, label='Operation Count')
    ax5.set_xlabel('Time Elapsed (seconds)')
    ax5.set_ylabel('Duration (μs)')
    ax5.set_title('Overall Operation Distribution')
    
    # 6. Statistics summary
    ax6 = plt.subplot(3, 2, 6)
    ax6.axis('off')
    
    # Create statistics text
    stats_text = []
    stats_text.append("=== SUMMARY STATISTICS ===\n")
    stats_text.append(f"Total Operations: {len(timestamps)}")
    stats_text.append(f"Time Span: {max(elapsed_times):.2f} seconds")
    stats_text.append(f"MDTs Active: {len(unique_mdts)}")
    stats_text.append(f"Operation Types: {len(unique_ops)}")
    stats_text.append(f"Avg Duration: {np.mean(durations):.1f} μs")
    stats_text.append(f"Duration Range: {min(durations)}-{max(durations)} μs\n")
    
    stats_text.append("=== BY OPERATION TYPE ===")
    for op in unique_ops:
        op_durations = [d for o, d in zip(operations, durations) if o == op]
        if op_durations:
            stats_text.append(f"{op}: {len(op_durations)} ops, "
                            f"avg {np.mean(op_durations):.1f}μs")
    
    if len(unique_mdts) > 1:
        stats_text.append("\n=== BY MDT ===")
        for mdt in unique_mdts:
            mdt_durations = [d for m, d in zip(mdts, durations) if m == mdt]
            if mdt_durations:
                stats_text.append(f"{mdt}: {len(mdt_durations)} ops, "
                                f"avg {np.mean(mdt_durations):.1f}μs")
    
    ax6.text(0.05, 0.95, '\n'.join(stats_text), transform=ax6.transAxes,
             fontsize=10, verticalalignment='top', fontfamily='monospace')
    
    plt.tight_layout()
    plt.savefig('mdt_timing_comprehensive_with_mdt.png', dpi=300, bbox_inches='tight')
    print("Comprehensive plot saved as 'mdt_timing_comprehensive_with_mdt.png'")

def create_mdt_focused_analysis(log_file_content):
    """Create MDT-focused analysis with separate plots per MDT."""
    timestamps, durations, operations, mdts = parse_log(log_file_content)
    
    if not timestamps:
        return
        
    unique_mdts = sorted(list(set(mdts)))
    unique_ops = sorted(list(set(operations)))
    
    if len(unique_mdts) <= 1:
        print("Only one MDT found, skipping MDT-focused analysis")
        return
    
    # Calculate elapsed times
    start_time = min(timestamps)
    elapsed_times = [t - start_time for t in timestamps]
    
    # Create figure with subplots for each MDT
    fig, axes = plt.subplots(len(unique_mdts), 1, figsize=(15, 4 * len(unique_mdts)))
    if len(unique_mdts) == 1:
        axes = [axes]
    
    op_colors = plt.cm.tab10(np.linspace(0, 1, len(unique_ops)))
    op_color_map = dict(zip(unique_ops, op_colors))
    
    for i, mdt in enumerate(unique_mdts):
        ax = axes[i]
        
        # Plot operations for this MDT
        for op in unique_ops:
            indices = [j for j, (o, m) in enumerate(zip(operations, mdts)) 
                      if o == op and m == mdt]
            if indices:
                ax.scatter([elapsed_times[j] for j in indices],
                          [durations[j] for j in indices],
                          c=[op_color_map[op]], label=op, alpha=0.7, s=30)
        
        ax.set_xlabel('Time Elapsed (seconds)')
        ax.set_ylabel('Duration (μs)')
        ax.set_title(f'{mdt} - Operations Over Time')
        ax.legend()
        ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    plt.savefig('mdt_timing_by_mdt.png', dpi=300, bbox_inches='tight')
    print("MDT-focused plot saved as 'mdt_timing_by_mdt.png'")

def print_summary_stats(log_file_content):
    """Print detailed summary statistics."""
    timestamps, durations, operations, mdts = parse_log(log_file_content)
    
    if not timestamps:
        print("No timing data found.")
        return
    
    print("\n" + "="*60)
    print("DETAILED TIMING ANALYSIS SUMMARY")
    print("="*60)
    
    print(f"Total Operations Analyzed: {len(timestamps)}")
    print(f"Time Span: {max(timestamps) - min(timestamps):.2f} seconds")
    print(f"Unique Operation Types: {len(set(operations))}")
    print(f"Unique MDTs: {len(set(mdts))}")
    
    print(f"\nDuration Statistics:")
    print(f"  Average: {np.mean(durations):.1f} μs")
    print(f"  Median: {np.median(durations):.1f} μs")
    print(f"  Min: {min(durations)} μs")
    print(f"  Max: {max(durations)} μs")
    print(f"  Std Dev: {np.std(durations):.1f} μs")
    
    print(f"\nOperation Type Breakdown:")
    op_counts = Counter(operations)
    for op, count in op_counts.most_common():
        op_durations = [d for o, d in zip(operations, durations) if o == op]
        print(f"  {op:12}: {count:4d} ops, avg {np.mean(op_durations):6.1f}μs")
    
    if len(set(mdts)) > 1:
        print(f"\nMDT Breakdown:")
        mdt_counts = Counter(mdts)
        for mdt, count in mdt_counts.most_common():
            mdt_durations = [d for m, d in zip(mdts, durations) if m == mdt]
            print(f"  {mdt:12}: {count:4d} ops, avg {np.mean(mdt_durations):6.1f}μs")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <log_file>")
        print("This script analyzes MDT timing logs and creates visualizations")
        print("showing both operation types and MDT identification.")
        sys.exit(1)
    
    log_file = sys.argv[1]
    
    try:
        with open(log_file, 'r') as f:
            log_content = f.read()
        
        print("Analyzing MDT timing log...")
        
        # Print summary statistics
        print_summary_stats(log_content)
        
        # Create comprehensive analysis
        create_comprehensive_analysis(log_content)
        
        # Create MDT-focused analysis if multiple MDTs found
        create_mdt_focused_analysis(log_content)
        
        print("\nAnalysis complete!")
        print("Generated plots:")
        print("  - mdt_timing_comprehensive_with_mdt.png (main analysis)")
        print("  - mdt_timing_by_mdt.png (per-MDT breakdown, if applicable)")
        
    except FileNotFoundError:
        print(f"Error: Log file not found: {log_file}")
        sys.exit(1)
    except Exception as e:
        print(f"Error processing log file: {e}")
        sys.exit(1)
