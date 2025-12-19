#!/usr/bin/env python
"""
Analyze Replacement Policy Performance Results

Generates analysis and visualizations comparing LRU, DIP, DRRIP, and EAF policies.
"""

import os
import sys
import csv
import argparse
from pathlib import Path

# Try to import plotting libraries
try:
    import pandas as pd
    import numpy as np
    HAS_PANDAS = True
except ImportError:
    HAS_PANDAS = False
    print("Warning: pandas/numpy not installed. Install with: pip install pandas numpy")
    print("Some analysis features will be limited.")

try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend
    import matplotlib.pyplot as plt
    import seaborn as sns
    HAS_PLOTTING = True
except ImportError:
    HAS_PLOTTING = False
    print("Warning: matplotlib/seaborn not installed. Install with: pip install matplotlib seaborn")
    print("Visualizations will be skipped.")

def load_results(csv_path):
    """Load results from CSV file."""
    if HAS_PANDAS:
        try:
            df = pd.read_csv(csv_path)
            return df
        except Exception as e:
            print(f"Error loading CSV: {e}", file=sys.stderr)
            return None
    else:
        # Fallback: use built-in csv module
        try:
            data = []
            with open(csv_path, 'r') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    data.append(row)
            
            if not data:
                return None
            
            # Create simple dict-based structure
            class SimpleDF:
                def __init__(self, data):
                    self.data = data
                    self.columns = list(data[0].keys()) if data else []
            
            return SimpleDF(data)
        except Exception as e:
            print(f"Error loading CSV: {e}", file=sys.stderr)
            return None

def generate_summary_stats(df):
    """Generate summary statistics by policy."""
    if not HAS_PANDAS or df is None:
        return None
    
    summary = df.groupby('policy').agg({
        'ipc': ['mean', 'std', 'min', 'max'],
        'mpki': ['mean', 'std', 'min', 'max']
    }).round(4)
    
    return summary

def create_visualizations(df, output_dir):
    """Create visualization plots."""
    if not HAS_PLOTTING or df is None or not HAS_PANDAS:
        return
    
    os.makedirs(output_dir, exist_ok=True)
    sns.set_style("whitegrid")
    
    # 1. IPC Comparison by Policy
    if 'ipc' in df.columns and 'policy' in df.columns:
        plt.figure(figsize=(12, 6))
        
        # Box plot
        plt.subplot(1, 2, 1)
        df.boxplot(column='ipc', by='policy', ax=plt.gca())
        plt.title('IPC Distribution by Policy')
        plt.suptitle('')  # Remove default title
        plt.xlabel('Policy')
        plt.ylabel('IPC')
        plt.xticks(rotation=45)
        
        # Bar plot with error bars
        plt.subplot(1, 2, 2)
        ipc_stats = df.groupby('policy')['ipc'].agg(['mean', 'std'])
        ipc_stats.plot(kind='bar', y='mean', yerr='std', capsize=5, ax=plt.gca(), legend=False)
        plt.title('Average IPC by Policy')
        plt.xlabel('Policy')
        plt.ylabel('IPC (mean ± std)')
        plt.xticks(rotation=45)
        plt.tight_layout()
        
        plt.savefig(os.path.join(output_dir, 'ipc_by_policy.png'), dpi=150, bbox_inches='tight')
        plt.close()
    
    # 2. MPKI Comparison by Policy
    if 'mpki' in df.columns and 'policy' in df.columns:
        plt.figure(figsize=(12, 6))
        
        # Box plot
        plt.subplot(1, 2, 1)
        df.boxplot(column='mpki', by='policy', ax=plt.gca())
        plt.title('MPKI Distribution by Policy')
        plt.suptitle('')
        plt.xlabel('Policy')
        plt.ylabel('MPKI (Misses per Kilo Instructions)')
        plt.xticks(rotation=45)
        
        # Bar plot with error bars
        plt.subplot(1, 2, 2)
        mpki_stats = df.groupby('policy')['mpki'].agg(['mean', 'std'])
        mpki_stats.plot(kind='bar', y='mean', yerr='std', capsize=5, ax=plt.gca(), legend=False)
        plt.title('Average MPKI by Policy')
        plt.xlabel('Policy')
        plt.ylabel('MPKI (mean ± std)')
        plt.xticks(rotation=45)
        plt.tight_layout()
        
        plt.savefig(os.path.join(output_dir, 'mpki_by_policy.png'), dpi=150, bbox_inches='tight')
        plt.close()
    
    # 3. IPC vs MPKI Scatter Plot
    if 'ipc' in df.columns and 'mpki' in df.columns and 'policy' in df.columns:
        plt.figure(figsize=(10, 6))
        for policy in df['policy'].unique():
            policy_data = df[df['policy'] == policy]
            plt.scatter(policy_data['mpki'], policy_data['ipc'], label=policy, alpha=0.6, s=50)
        
        plt.xlabel('MPKI (Misses per Kilo Instructions)')
        plt.ylabel('IPC')
        plt.title('IPC vs MPKI by Policy')
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'ipc_vs_mpki.png'), dpi=150, bbox_inches='tight')
        plt.close()
    
    # 4. Policy Comparison by Benchmark
    if 'benchmark' in df.columns and 'ipc' in df.columns and 'policy' in df.columns:
        plt.figure(figsize=(14, 8))
        
        # Pivot for heatmap
        ipc_pivot = df.pivot_table(values='ipc', index='benchmark', columns='policy', aggfunc='mean')
        
        sns.heatmap(ipc_pivot, annot=True, fmt='.3f', cmap='YlOrRd', cbar_kws={'label': 'IPC'})
        plt.title('IPC by Benchmark and Policy')
        plt.xlabel('Policy')
        plt.ylabel('Benchmark')
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'ipc_heatmap_by_benchmark.png'), dpi=150, bbox_inches='tight')
        plt.close()
    
    # 5. MPKI Heatmap by Benchmark
    if 'benchmark' in df.columns and 'mpki' in df.columns and 'policy' in df.columns:
        plt.figure(figsize=(14, 8))
        
        mpki_pivot = df.pivot_table(values='mpki', index='benchmark', columns='policy', aggfunc='mean')
        
        sns.heatmap(mpki_pivot, annot=True, fmt='.2f', cmap='YlOrRd_r', cbar_kws={'label': 'MPKI'})
        plt.title('MPKI by Benchmark and Policy')
        plt.xlabel('Policy')
        plt.ylabel('Benchmark')
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'mpki_heatmap_by_benchmark.png'), dpi=150, bbox_inches='tight')
        plt.close()
    
    # 6. Relative Performance (normalized to LRU)
    if 'ipc' in df.columns and 'policy' in df.columns and 'benchmark' in df.columns:
        plt.figure(figsize=(12, 6))
        
        # Calculate relative IPC (normalized to LRU = 1.0)
        if 'LRU' in df['policy'].values:
            lru_ipc = df[df['policy'] == 'LRU'].set_index('benchmark')['ipc']
            relative_data = []
            
            for policy in df['policy'].unique():
                policy_data = df[df['policy'] == policy].set_index('benchmark')
                for benchmark in policy_data.index:
                    if benchmark in lru_ipc.index and lru_ipc[benchmark] > 0:
                        relative_ipc = policy_data.loc[benchmark, 'ipc'] / lru_ipc[benchmark]
                        relative_data.append({
                            'policy': policy,
                            'benchmark': benchmark,
                            'relative_ipc': relative_ipc
                        })
            
            if relative_data:
                rel_df = pd.DataFrame(relative_data)
                rel_df.boxplot(column='relative_ipc', by='policy', ax=plt.gca())
                plt.axhline(y=1.0, color='r', linestyle='--', label='LRU Baseline')
                plt.title('Relative IPC (normalized to LRU)')
                plt.suptitle('')
                plt.xlabel('Policy')
                plt.ylabel('Relative IPC (vs LRU)')
                plt.xticks(rotation=45)
                plt.legend()
                plt.tight_layout()
                plt.savefig(os.path.join(output_dir, 'relative_ipc_vs_lru.png'), dpi=150, bbox_inches='tight')
                plt.close()
    
    print(f"✓ Generated visualizations in {output_dir}")

def generate_report(df, output_file):
    """Generate text report."""
    if df is None:
        return
    
    with open(output_file, 'w') as f:
        f.write("=" * 80 + "\n")
        f.write("REPLACEMENT POLICY PERFORMANCE ANALYSIS\n")
        f.write("=" * 80 + "\n\n")
        
        if HAS_PANDAS:
            # Summary statistics
            f.write("SUMMARY STATISTICS\n")
            f.write("-" * 80 + "\n")
            summary = generate_summary_stats(df)
            if summary is not None:
                f.write(str(summary))
                f.write("\n\n")
            
            # Best policy by metric
            f.write("BEST POLICY BY METRIC\n")
            f.write("-" * 80 + "\n")
            avg_ipc = df.groupby('policy')['ipc'].mean()
            avg_mpki = df.groupby('policy')['mpki'].mean()
            
            best_ipc_policy = avg_ipc.idxmax()
            best_mpki_policy = avg_mpki.idxmin()  # Lower is better for MPKI
            
            f.write(f"Best IPC: {best_ipc_policy} (IPC = {avg_ipc[best_ipc_policy]:.4f})\n")
            f.write(f"Best MPKI: {best_mpki_policy} (MPKI = {avg_mpki[best_mpki_policy]:.2f})\n")
            f.write("\n")
            
            # Policy comparison
            f.write("POLICY COMPARISON\n")
            f.write("-" * 80 + "\n")
            for policy in df['policy'].unique():
                policy_data = df[df['policy'] == policy]
                f.write(f"{policy}:\n")
                f.write(f"  Average IPC: {policy_data['ipc'].mean():.4f} ± {policy_data['ipc'].std():.4f}\n")
                f.write(f"  Average MPKI: {policy_data['mpki'].mean():.2f} ± {policy_data['mpki'].std():.2f}\n")
                f.write(f"  Number of benchmarks: {len(policy_data)}\n")
                f.write("\n")
        else:
            f.write("Summary statistics require pandas library.\n")
        
        f.write("=" * 80 + "\n")

def main():
    parser = argparse.ArgumentParser(description='Analyze replacement policy performance results')
    parser.add_argument('csv_file', help='Path to CSV results file')
    parser.add_argument('--output-dir', default='results/policy_analysis/analysis',
                       help='Output directory for analysis results')
    parser.add_argument('--no-plots', action='store_true',
                       help='Skip generating plots')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.csv_file):
        print(f"Error: CSV file not found: {args.csv_file}")
        return 1
    
    print(f"Loading results from {args.csv_file}...")
    df = load_results(args.csv_file)
    
    if df is None:
        print("Error: Failed to load results")
        return 1
    
    if HAS_PANDAS:
        print(f"Loaded {len(df)} data points")
        print(f"Columns: {', '.join(df.columns)}")
        
        # Verify data integrity
        print(f"\nData verification:")
        policies = df['policy'].unique()
        print(f"  Policies found: {sorted(policies)}")
        
        # Check for duplicate results
        print(f"\nChecking for duplicate results...")
        for benchmark in df['benchmark'].unique():
            bench_data = df[df['benchmark'] == benchmark]
            if len(bench_data) > 1:
                # Check if all policies have identical results
                ipc_values = bench_data['ipc'].unique()
                mpki_values = bench_data['mpki'].unique()
                cycles_values = bench_data['cycles'].unique()
                
                if len(ipc_values) == 1 and len(mpki_values) == 1 and len(cycles_values) == 1:
                    print(f"  ⚠ WARNING: {benchmark} has IDENTICAL results for all policies!")
                    print(f"    IPC={ipc_values[0]:.6f}, MPKI={mpki_values[0]:.6f}, Cycles={cycles_values[0]}")
                    for _, row in bench_data.iterrows():
                        print(f"      {row['policy']}: IPC={row['ipc']:.6f}, MPKI={row['mpki']:.6f}")
                else:
                    print(f"  ✓ {benchmark}: Different results across policies")
                    for _, row in bench_data.iterrows():
                        print(f"      {row['policy']}: IPC={row['ipc']:.6f}, MPKI={row['mpki']:.6f}, Cycles={row['cycles']}")
    else:
        print(f"Loaded {len(df.data)} data points")
    
    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Generate report
    report_file = os.path.join(args.output_dir, 'analysis_report.txt')
    print(f"\nGenerating report: {report_file}")
    generate_report(df, report_file)
    
    # Generate visualizations
    if not args.no_plots:
        plots_dir = os.path.join(args.output_dir, 'plots')
        print(f"\nGenerating visualizations: {plots_dir}")
        create_visualizations(df, plots_dir)
    else:
        print("\nSkipping visualizations (--no-plots specified)")
    
    print("\n" + "=" * 80)
    print("Analysis complete!")
    print(f"Results saved to {args.output_dir}")
    print("=" * 80)
    
    return 0

if __name__ == '__main__':
    sys.exit(main())

