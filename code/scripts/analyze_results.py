#!/usr/bin/env python
"""
Cache Sweep Results Analysis Script

Analyzes cache parameter sweep results and generates:
- Summary statistics
- Performance comparisons
- Visualizations
- Optimal configuration recommendations
"""

import os
import sys
import csv
import json
import argparse
from pathlib import Path
from collections import defaultdict

try:
    import pandas as pd
    import numpy as np
    HAS_PANDAS = True
except ImportError:
    HAS_PANDAS = False
    pd = None
    np = None

try:
    import matplotlib.pyplot as plt
    import seaborn as sns
    HAS_PLOTTING = True
except ImportError:
    HAS_PLOTTING = False

if not HAS_PANDAS:
    print("Warning: pandas/numpy not installed. Install with: pip install pandas numpy")
    print("Some analysis features will be limited.")
if not HAS_PLOTTING:
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
            
            # Convert to simple dict-based structure
            if not data:
                return None
            
            # Create a simple DataFrame-like structure
            class SimpleDF:
                def __init__(self, data):
                    self.data = data
                    self.columns = list(data[0].keys()) if data else []
                
                def __len__(self):
                    return len(self.data)
                
                def __getitem__(self, key):
                    return [row.get(key) for row in self.data]
                
                def get(self, key, default=None):
                    return [row.get(key, default) for row in self.data]
            
            return SimpleDF(data)
        except Exception as e:
            print(f"Error loading CSV: {e}", file=sys.stderr)
            return None

def analyze_by_parameter(df, param_name, metric='ipc'):
    """Analyze how a parameter affects a metric."""
    if df is None or param_name not in df.columns or metric not in df.columns:
        return None
    
    if HAS_PANDAS:
        results = df.groupby(param_name)[metric].agg(['mean', 'std', 'min', 'max', 'count'])
        results = results.sort_index()
        return results
    else:
        # Fallback: manual calculation
        from collections import defaultdict
        groups = defaultdict(list)
        for row in df.data:
            param_val = row.get(param_name)
            metric_val = row.get(metric)
            if param_val is not None and metric_val is not None:
                try:
                    groups[param_val].append(float(metric_val))
                except (ValueError, TypeError):
                    pass
        
        results = {}
        for param_val, values in groups.items():
            if values:
                results[param_val] = {
                    'mean': sum(values) / len(values),
                    'std': (sum((x - sum(values)/len(values))**2 for x in values) / len(values))**0.5 if len(values) > 1 else 0,
                    'min': min(values),
                    'max': max(values),
                    'count': len(values)
                }
        return results

def find_best_configs(df, metric='ipc', top_n=5):
    """Find top N configurations by metric."""
    if df is None or metric not in df.columns:
        return None
    
    # Group by configuration parameters
    config_cols = ['d_cache_num_sets', 'line_size_bytes', 'associativity']
    if not all(col in df.columns for col in config_cols):
        return None
    
    if HAS_PANDAS:
        grouped = df.groupby(config_cols)[metric].mean().reset_index()
        grouped = grouped.sort_values(metric, ascending=False)
        return grouped.head(top_n)
    else:
        # Fallback: manual calculation
        from collections import defaultdict
        groups = defaultdict(list)
        for row in df.data:
            config_key = tuple(row.get(col) for col in config_cols)
            metric_val = row.get(metric)
            if all(k is not None for k in config_key) and metric_val is not None:
                try:
                    groups[config_key].append(float(metric_val))
                except (ValueError, TypeError):
                    pass
        
        # Calculate means and sort
        results = []
        for config_key, values in groups.items():
            if values:
                mean_val = sum(values) / len(values)
                results.append({
                    'd_cache_num_sets': config_key[0],
                    'line_size_bytes': config_key[1],
                    'associativity': config_key[2],
                    metric: mean_val
                })
        
        # Sort by metric (descending) and return top N
        results.sort(key=lambda x: x.get(metric, 0), reverse=True)
        return results[:top_n]

def generate_summary_stats(df):
    """Generate summary statistics."""
    if df is None:
        return None
    
    if HAS_PANDAS:
        numeric_cols = df.select_dtypes(include=[np.number]).columns
        summary = df[numeric_cols].describe()
        return summary
    else:
        # Fallback: basic stats for numeric columns
        numeric_cols = []
        for col in df.columns:
            try:
                float(df.data[0].get(col, 0))
                numeric_cols.append(col)
            except (ValueError, TypeError):
                pass
        
        summary = {}
        for col in numeric_cols:
            values = []
            for row in df.data:
                val = row.get(col)
                if val is not None:
                    try:
                        values.append(float(val))
                    except (ValueError, TypeError):
                        pass
            
            if values:
                summary[col] = {
                    'mean': sum(values) / len(values),
                    'std': (sum((x - sum(values)/len(values))**2 for x in values) / len(values))**0.5 if len(values) > 1 else 0,
                    'min': min(values),
                    'max': max(values),
                    'count': len(values)
                }
        return summary

def create_visualizations(df, output_dir):
    """Create visualization plots."""
    if not HAS_PLOTTING or df is None or not HAS_PANDAS:
        return
    
    os.makedirs(output_dir, exist_ok=True)
    sns.set_style("whitegrid")
    
    # 1. IPC vs Cache Size (by line size)
    if all(col in df.columns for col in ['d_cache_size_kb', 'ipc', 'line_size_bytes']):
        plt.figure(figsize=(12, 6))
        for line_size in sorted(df['line_size_bytes'].unique()):
            subset = df[df['line_size_bytes'] == line_size]
            grouped = subset.groupby('d_cache_size_kb')['ipc'].mean()
            # Use log2 scale for cache size (X-axis)
            plt.plot(grouped.index, grouped.values, marker='o', label=f'{line_size}B line')
        plt.xlabel('Cache Size (KB, log2 scale)')
        plt.ylabel('IPC')
        plt.title('IPC vs Cache Size (by Line Size)')
        plt.xscale('log', base=2)
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'ipc_vs_cache_size.png'), dpi=150)
        plt.close()
    
    # 2. IPC vs Associativity
    if all(col in df.columns for col in ['associativity', 'ipc']):
        plt.figure(figsize=(10, 6))
        grouped = df.groupby('associativity')['ipc'].agg(['mean', 'std'])
        plt.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'], 
                    marker='o', capsize=5, capthick=2)
        plt.xlabel('Associativity (log2 scale)')
        plt.ylabel('IPC (mean ± std)')
        plt.title('IPC vs Associativity')
        plt.xscale('log', base=2)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'ipc_vs_associativity.png'), dpi=150)
        plt.close()
    
    # 3. IPC vs Line Size
    if all(col in df.columns for col in ['line_size_bytes', 'ipc']):
        plt.figure(figsize=(10, 6))
        grouped = df.groupby('line_size_bytes')['ipc'].agg(['mean', 'std'])
        plt.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                    marker='o', capsize=5, capthick=2)
        plt.xlabel('Line Size (bytes, log2 scale)')
        plt.ylabel('IPC (mean ± std)')
        plt.title('IPC vs Line Size')
        plt.xscale('log', base=2)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'ipc_vs_line_size.png'), dpi=150)
        plt.close()
    
    # 4. Miss Rate vs Line Size
    if all(col in df.columns for col in ['line_size_bytes', 'd_cache_miss_rate']):
        plt.figure(figsize=(10, 6))
        grouped = df.groupby('line_size_bytes')['d_cache_miss_rate'].agg(['mean', 'std'])
        plt.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                    marker='o', capsize=5, capthick=2)
        plt.xlabel('Line Size (bytes, log2 scale)')
        plt.ylabel('D-Cache Miss Rate (mean ± std)')
        plt.title('D-Cache Miss Rate vs Line Size')
        plt.xscale('log', base=2)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'miss_rate_vs_line_size.png'), dpi=150)
        plt.close()
    
    # 5. Miss Rate vs Associativity
    if all(col in df.columns for col in ['associativity', 'd_cache_miss_rate']):
        plt.figure(figsize=(10, 6))
        grouped = df.groupby('associativity')['d_cache_miss_rate'].agg(['mean', 'std'])
        plt.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                    marker='o', capsize=5, capthick=2)
        plt.xlabel('Associativity (log2 scale)')
        plt.ylabel('D-Cache Miss Rate (mean ± std)')
        plt.title('D-Cache Miss Rate vs Associativity')
        plt.xscale('log', base=2)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'miss_rate_vs_associativity.png'), dpi=150)
        plt.close()
    
    # 6. Miss Rate vs Cache Size
    if all(col in df.columns for col in ['d_cache_size_kb', 'd_cache_miss_rate']):
        plt.figure(figsize=(10, 6))
        grouped = df.groupby('d_cache_size_kb')['d_cache_miss_rate'].agg(['mean', 'std'])
        plt.errorbar(grouped.index, grouped['mean'], yerr=grouped['std'],
                    marker='o', capsize=5, capthick=2)
        plt.xlabel('Cache Size (KB, log2 scale)')
        plt.ylabel('D-Cache Miss Rate (mean ± std)')
        plt.title('D-Cache Miss Rate vs Cache Size')
        plt.xscale('log', base=2)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'miss_rate_vs_cache_size.png'), dpi=150)
        plt.close()
    
    # 7. Heatmap: IPC by Cache Size and Associativity
    if all(col in df.columns for col in ['d_cache_size_kb', 'associativity', 'ipc']):
        plt.figure(figsize=(12, 8))
        pivot = df.pivot_table(values='ipc', index='d_cache_size_kb', 
                              columns='associativity', aggfunc='mean')
        sns.heatmap(pivot, annot=True, fmt='.3f', cmap='YlOrRd', cbar_kws={'label': 'IPC'})
        plt.xlabel('Associativity')
        plt.ylabel('Cache Size (KB)')
        plt.title('IPC Heatmap: Cache Size vs Associativity')
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'ipc_heatmap.png'), dpi=150)
        plt.close()
    
    # 8. Miss Rate by Benchmark
    if all(col in df.columns for col in ['benchmark', 'd_cache_miss_rate']):
        plt.figure(figsize=(14, 6))
        grouped = df.groupby('benchmark')['d_cache_miss_rate'].agg(['mean', 'std']).sort_values('mean')
        plt.barh(range(len(grouped)), grouped['mean'], xerr=grouped['std'])
        plt.yticks(range(len(grouped)), grouped.index)
        plt.xlabel('D-Cache Miss Rate (mean ± std)')
        plt.title('Miss Rate by Benchmark')
        plt.grid(True, alpha=0.3, axis='x')
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, 'miss_rate_by_benchmark.png'), dpi=150)
        plt.close()

def get_column_values(df, col):
    """Get values from a column, handling both pandas and simple DF."""
    if HAS_PANDAS:
        return df[col].tolist()
    else:
        return [row.get(col) for row in df.data]

def get_mean(df, col):
    """Get mean of a column."""
    values = get_column_values(df, col)
    numeric_values = [float(v) for v in values if v is not None and v != '']
    if numeric_values:
        return sum(numeric_values) / len(numeric_values)
    return None

def get_std(df, col):
    """Get std of a column."""
    mean = get_mean(df, col)
    if mean is None:
        return None
    values = get_column_values(df, col)
    numeric_values = [float(v) for v in values if v is not None and v != '']
    if len(numeric_values) <= 1:
        return 0.0
    variance = sum((x - mean)**2 for x in numeric_values) / len(numeric_values)
    return variance ** 0.5

def get_min(df, col):
    """Get min of a column."""
    values = get_column_values(df, col)
    numeric_values = [float(v) for v in values if v is not None and v != '']
    return min(numeric_values) if numeric_values else None

def get_max(df, col):
    """Get max of a column."""
    values = get_column_values(df, col)
    numeric_values = [float(v) for v in values if v is not None and v != '']
    return max(numeric_values) if numeric_values else None

def get_unique_values(df, col):
    """Get unique values from a column."""
    if HAS_PANDAS:
        return sorted(df[col].unique())
    else:
        values = set()
        for row in df.data:
            val = row.get(col)
            if val is not None:
                values.add(val)
        return sorted(values)

def filter_df(df, col, value):
    """Filter dataframe by column value."""
    if HAS_PANDAS:
        return df[df[col] == value]
    else:
        filtered_data = [row for row in df.data if row.get(col) == value]
        class SimpleDF:
            def __init__(self, data):
                self.data = data
                self.columns = list(data[0].keys()) if data else []
        return SimpleDF(filtered_data)

def generate_report(df, output_path):
    """Generate text report."""
    if df is None:
        return
    
    with open(output_path, 'w') as f:
        f.write("=" * 80 + "\n")
        f.write("Cache Parameter Sweep Analysis Report\n")
        f.write("=" * 80 + "\n\n")
        
        # Summary statistics
        f.write("SUMMARY STATISTICS\n")
        f.write("-" * 80 + "\n")
        numeric_cols = ['cycles', 'ipc', 'd_cache_miss_rate', 'i_cache_miss_rate']
        for col in numeric_cols:
            if col in df.columns:
                mean_val = get_mean(df, col)
                std_val = get_std(df, col)
                min_val = get_min(df, col)
                max_val = get_max(df, col)
                if mean_val is not None:
                    f.write(f"{col}:\n")
                    f.write(f"  Mean: {mean_val:.4f}\n")
                    f.write(f"  Std:  {std_val:.4f}\n")
                    f.write(f"  Min:  {min_val:.4f}\n")
                    f.write(f"  Max:  {max_val:.4f}\n\n")
        
        # Best configurations
        f.write("\nTOP 5 CONFIGURATIONS BY IPC\n")
        f.write("-" * 80 + "\n")
        best = find_best_configs(df, 'ipc', 5)
        if best is not None:
            if HAS_PANDAS:
                for idx, row in best.iterrows():
                    f.write(f"{idx+1}. Sets: {row['d_cache_num_sets']}, "
                           f"Line: {row['line_size_bytes']}B, "
                           f"Assoc: {row['associativity']}-way, "
                           f"IPC: {row['ipc']:.4f}\n")
            else:
                for idx, config in enumerate(best):
                    f.write(f"{idx+1}. Sets: {config['d_cache_num_sets']}, "
                           f"Line: {config['line_size_bytes']}B, "
                           f"Assoc: {config['associativity']}-way, "
                           f"IPC: {config['ipc']:.4f}\n")
        
        # Parameter impact analysis
        f.write("\n\nPARAMETER IMPACT ANALYSIS\n")
        f.write("-" * 80 + "\n")
        
        # Cache size impact
        if 'd_cache_size_kb' in df.columns:
            f.write("\nCache Size Impact on IPC:\n")
            size_impact = analyze_by_parameter(df, 'd_cache_size_kb', 'ipc')
            if size_impact is not None:
                if HAS_PANDAS:
                    for size, stats in size_impact.iterrows():
                        f.write(f"  {size:.2f}KB: IPC = {stats['mean']:.4f} ± {stats['std']:.4f}\n")
                else:
                    for size in sorted(size_impact.keys()):
                        stats = size_impact[size]
                        f.write(f"  {size}KB: IPC = {stats['mean']:.4f} ± {stats['std']:.4f}\n")
        
        # Line size impact
        if 'line_size_bytes' in df.columns:
            f.write("\nLine Size Impact on D-Cache Miss Rate:\n")
            line_impact = analyze_by_parameter(df, 'line_size_bytes', 'd_cache_miss_rate')
            if line_impact is not None:
                if HAS_PANDAS:
                    for line_size, stats in line_impact.iterrows():
                        f.write(f"  {line_size}B: Miss Rate = {stats['mean']:.4f} ± {stats['std']:.4f}\n")
                else:
                    for line_size in sorted(line_impact.keys()):
                        stats = line_impact[line_size]
                        f.write(f"  {line_size}B: Miss Rate = {stats['mean']:.4f} ± {stats['std']:.4f}\n")
        
        # Associativity impact
        if 'associativity' in df.columns:
            f.write("\nAssociativity Impact on IPC:\n")
            assoc_impact = analyze_by_parameter(df, 'associativity', 'ipc')
            if assoc_impact is not None:
                if HAS_PANDAS:
                    for assoc, stats in assoc_impact.iterrows():
                        f.write(f"  {assoc}-way: IPC = {stats['mean']:.4f} ± {stats['std']:.4f}\n")
                else:
                    for assoc in sorted(assoc_impact.keys()):
                        stats = assoc_impact[assoc]
                        f.write(f"  {assoc}-way: IPC = {stats['mean']:.4f} ± {stats['std']:.4f}\n")
        
        # Benchmark analysis
        f.write("\n\nBENCHMARK ANALYSIS\n")
        f.write("-" * 80 + "\n")
        if 'benchmark' in df.columns:
            for benchmark in get_unique_values(df, 'benchmark'):
                subset = filter_df(df, 'benchmark', benchmark)
                f.write(f"\n{benchmark}:\n")
                ipc_mean = get_mean(subset, 'ipc')
                miss_rate_mean = get_mean(subset, 'd_cache_miss_rate')
                cycles_mean = get_mean(subset, 'cycles')
                if ipc_mean is not None:
                    f.write(f"  Avg IPC: {ipc_mean:.4f}\n")
                if miss_rate_mean is not None:
                    f.write(f"  Avg D-Cache Miss Rate: {miss_rate_mean:.4f}\n")
                if cycles_mean is not None:
                    f.write(f"  Avg Cycles: {cycles_mean:.0f}\n")

def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description='Analyze cache sweep results')
    parser.add_argument('csv_file', type=str, help='Path to CSV results file')
    parser.add_argument('--output-dir', type=str, default='results/analysis',
                       help='Output directory for analysis results')
    parser.add_argument('--no-plots', action='store_true',
                       help='Skip generating plots')
    args = parser.parse_args()
    
    # Load data
    print(f"Loading results from {args.csv_file}...")
    df = load_results(args.csv_file)
    
    if df is None:
        print("Error: Could not load results file", file=sys.stderr)
        sys.exit(1)
    
    print(f"Loaded {len(df)} data points")
    print(f"Columns: {', '.join(df.columns)}")
    
    # Create output directory
    os.makedirs(args.output_dir, exist_ok=True)
    
    # Generate report
    report_path = os.path.join(args.output_dir, 'analysis_report.txt')
    print(f"\nGenerating report: {report_path}")
    generate_report(df, report_path)
    
    # Generate visualizations
    if not args.no_plots and HAS_PLOTTING and HAS_PANDAS:
        plots_dir = os.path.join(args.output_dir, 'plots')
        print(f"\nGenerating visualizations: {plots_dir}")
        create_visualizations(df, plots_dir)
        print("✓ Visualizations saved")
    elif args.no_plots:
        print("\nSkipping visualizations (--no-plots specified)")
    else:
        print("\nSkipping visualizations (plotting libraries not available)")
    
    # Print summary to console
    print("\n" + "=" * 80)
    print("QUICK SUMMARY")
    print("=" * 80)
    print(f"Total configurations tested: {len(df)}")
    
    ipc_mean = get_mean(df, 'ipc')
    miss_rate_mean = get_mean(df, 'd_cache_miss_rate')
    if ipc_mean is not None:
        print(f"Average IPC: {ipc_mean:.4f}")
    if miss_rate_mean is not None:
        print(f"Average D-Cache Miss Rate: {miss_rate_mean:.4f}")
    
    best = find_best_configs(df, 'ipc', 1)
    if best is not None and len(best) > 0:
        if HAS_PANDAS:
            row = best.iloc[0]
            print(f"\nBest Configuration (by IPC):")
            print(f"  Sets: {row['d_cache_num_sets']}, "
                  f"Line: {row['line_size_bytes']}B, "
                  f"Assoc: {row['associativity']}-way")
            print(f"  IPC: {row['ipc']:.4f}")
        else:
            config = best[0]
            print(f"\nBest Configuration (by IPC):")
            print(f"  Sets: {config['d_cache_num_sets']}, "
                  f"Line: {config['line_size_bytes']}B, "
                  f"Assoc: {config['associativity']}-way")
            print(f"  IPC: {config['ipc']:.4f}")
    
    print(f"\n✓ Analysis complete! Results saved to {args.output_dir}")

if __name__ == '__main__':
    main()

