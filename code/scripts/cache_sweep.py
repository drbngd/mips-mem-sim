#!/usr/bin/env python
"""
Cache Parameter Sweep Script

Sweeps across cache parameters (size, line size, associativity) and runs
benchmarks to collect performance metrics.
"""

import os
import sys
import json
import csv
from datetime import datetime
from pathlib import Path

# Add scripts directory to path to import utils
sys.path.insert(0, os.path.dirname(__file__))
from cache_sweep_utils import (
    update_cache_h, recompile, run_benchmark, 
    get_benchmarks, calculate_cache_size_kb
)

try:
    from tqdm import tqdm
    HAS_TQDM = True
except ImportError:
    HAS_TQDM = False
    print("Note: Install 'tqdm' for progress bars: pip install tqdm")

def load_config(config_path=None):
    """Load sweep configuration from JSON file."""
    if config_path is None:
        config_path = os.path.join(os.path.dirname(__file__), 'sweep_config.json')
    
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    return config

def is_valid_config(num_sets, line_size_bytes, associativity):
    """
    Check if cache configuration is valid.
    - num_sets must be >= 1
    - num_sets should be power of 2 (typical for caches)
    """
    if num_sets < 1:
        return False
    
    # Check if num_sets is power of 2 (optional but typical)
    # if (num_sets & (num_sets - 1)) != 0:
    #     return False
    
    return True

def run_sweep(config):
    """Run the cache parameter sweep."""
    # Get configuration
    cache_configs = config['cache_configs']
    benchmarks = config['benchmarks']
    output_config = config['output']
    options = config.get('options', {})
    
    # Get available benchmarks
    available_benchmarks = get_benchmarks()
    
    # Filter benchmarks to only those that exist
    benchmarks = [b for b in benchmarks if b in available_benchmarks]
    
    if not benchmarks:
        print("Error: No valid benchmarks found!", file=sys.stderr)
        return
    
    print(f"Found {len(benchmarks)} benchmarks: {', '.join(benchmarks)}")
    
    # Generate all configurations
    configs = []
    for num_sets in cache_configs['d_cache_num_sets']:
        for line_size in cache_configs['line_sizes_bytes']:
            for assoc in cache_configs['associativities']:
                if is_valid_config(num_sets, line_size, assoc):
                    # Calculate cache size for reference
                    cache_size_kb = calculate_cache_size_kb(num_sets, line_size, assoc)
                    # Handle i_cache_num_sets - if it's an array, use first value
                    i_cache_num_sets = cache_configs.get('i_cache_num_sets', 64)
                    if isinstance(i_cache_num_sets, list):
                        i_cache_num_sets = i_cache_num_sets[0]  # Use first value if array
                    
                    configs.append({
                        'd_cache_num_sets': num_sets,
                        'line_size_bytes': line_size,
                        'associativity': assoc,
                        'd_cache_size_kb': cache_size_kb,  # Calculated for reference
                        'i_cache_num_sets': i_cache_num_sets,
                        'i_cache_assoc': cache_configs.get('i_cache_assoc', 4)
                    })
                elif options.get('verbose', True):
                    print(f"Skipping invalid config: {num_sets} sets, {line_size}B, {assoc}-way")
    
    print(f"\nTotal configurations to test: {len(configs)}")
    print(f"Total benchmark runs: {len(configs) * len(benchmarks)}")
    
    # Prepare results
    results = []
    code_dir = os.path.dirname(os.path.dirname(__file__))
    
    # Create results directory
    results_dir = os.path.join(code_dir, output_config.get('results_dir', 'results'))
    os.makedirs(results_dir, exist_ok=True)
    
    # Setup progress tracking
    total_runs = len(configs) * len(benchmarks)
    if HAS_TQDM:
        pbar = tqdm(total=total_runs, desc="Running sweep")
    else:
        current_run = 0
    
    # Run sweep
    for config_idx, cache_config in enumerate(configs):
        if options.get('verbose', True):
            print(f"\n[{config_idx+1}/{len(configs)}] Config: "
                  f"{cache_config['d_cache_num_sets']} sets, "
                  f"{cache_config['line_size_bytes']}B line, "
                  f"{cache_config['associativity']}-way "
                  f"({cache_config['d_cache_size_kb']:.2f}KB)")
        
        # Update cache.h
        try:
            num_sets_info = update_cache_h(
                cache_config['d_cache_num_sets'],
                cache_config['line_size_bytes'],
                cache_config['associativity'],
                cache_config['i_cache_num_sets'],
                cache_config['i_cache_assoc']
            )
        except Exception as e:
            print(f"Error updating cache.h: {e}", file=sys.stderr)
            continue
        
        # Recompile
        try:
            if not recompile():
                print(f"Compilation failed for config", file=sys.stderr)
                continue
        except Exception as e:
            print(f"Compilation error: {e}", file=sys.stderr)
            continue
        
        # Run benchmarks
        for benchmark in benchmarks:
            if HAS_TQDM:
                pbar.set_description(f"Running {benchmark}")
            
            result = run_benchmark(benchmark)
            
            if result:
                # Add configuration info to result
                result['benchmark'] = benchmark
                result['d_cache_num_sets'] = cache_config['d_cache_num_sets']
                result['line_size_bytes'] = cache_config['line_size_bytes']
                result['associativity'] = cache_config['associativity']
                result['d_cache_size_kb'] = cache_config['d_cache_size_kb']
                result['i_cache_num_sets'] = num_sets_info['i_cache_num_sets']
                result['i_cache_assoc'] = cache_config['i_cache_assoc']
                
                results.append(result)
            
            if HAS_TQDM:
                pbar.update(1)
            else:
                current_run += 1
                if current_run % 10 == 0:
                    print(f"Progress: {current_run}/{total_runs}")
    
    if HAS_TQDM:
        pbar.close()
    
    # Save results
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    base_filename = output_config.get('filename', 'cache_sweep_results.csv')
    filename = base_filename.replace('.csv', f'_{timestamp}.csv')
    output_path = os.path.join(results_dir, filename)
    
    if results:
        save_results_csv(results, output_path)
        print(f"\n✓ Saved {len(results)} results to {output_path}")
    else:
        print("\n✗ No results collected!", file=sys.stderr)

def save_results_csv(results, output_path):
    """Save results to CSV file."""
    if not results:
        return
    
    # Get all possible keys
    all_keys = set()
    for result in results:
        all_keys.update(result.keys())
    
    # Define column order
    column_order = [
        'benchmark',
        'd_cache_num_sets',
        'line_size_bytes',
        'associativity',
        'd_cache_size_kb',
        'i_cache_num_sets',
        'i_cache_assoc',
        'cycles',
        'ipc',
        'i_cache_accesses',
        'i_cache_reads',
        'i_cache_writes',
        'i_cache_hits',
        'i_cache_misses',
        'i_cache_hit_rate',
        'i_cache_miss_rate',
        'd_cache_accesses',
        'd_cache_reads',
        'd_cache_writes',
        'd_cache_hits',
        'd_cache_misses',
        'd_cache_hit_rate',
        'd_cache_miss_rate'
    ]
    
    # Add any extra keys
    for key in sorted(all_keys):
        if key not in column_order:
            column_order.append(key)
    
    # Write CSV
    with open(output_path, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=column_order, extrasaction='ignore')
        writer.writeheader()
        writer.writerows(results)

def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description='Cache parameter sweep')
    parser.add_argument('--config', type=str, default=None,
                       help='Path to config JSON file (default: sweep_config.json)')
    parser.add_argument('--benchmarks', nargs='+', default=None,
                       help='Override benchmarks list from config')
    args = parser.parse_args()
    
    # Load config
    config = load_config(args.config)
    
    # Override benchmarks if specified
    if args.benchmarks:
        config['benchmarks'] = args.benchmarks
    
    # Run sweep
    run_sweep(config)

if __name__ == '__main__':
    main()

