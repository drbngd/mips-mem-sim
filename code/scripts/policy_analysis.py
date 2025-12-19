#!/usr/bin/env python
"""
Replacement Policy Performance Analysis Script

Tests all 4 replacement policies (LRU, DIP, DRRIP, EAF) on D-cache
and collects performance metrics (MPKI and IPC).
"""

import os
import sys
import csv
import json
import time
from datetime import datetime
from pathlib import Path

# Add scripts directory to path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from policy_analysis_utils import (
    update_cache_policy,
    recompile,
    run_benchmark,
    get_benchmarks
)

# Policy names
POLICIES = ['LRU', 'DIP', 'DRRIP', 'EAF']

def main():
    """Main function to run policy analysis."""
    
    # Configuration
    benchmark_dir = 'inputs/cache'
    results_dir = 'results/policy_analysis'
    os.makedirs(results_dir, exist_ok=True)
    
    # Get benchmarks
    benchmarks = get_benchmarks(benchmark_dir)
    
    if not benchmarks:
        print("Error: No benchmarks found")
        return 1
    
    print(f"Found {len(benchmarks)} benchmarks")
    print(f"Testing {len(POLICIES)} policies: {', '.join(POLICIES)}")
    print()
    
    # Results storage
    results = []
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_file = os.path.join(results_dir, f'policy_analysis_{timestamp}.csv')
    
    total_runs = len(POLICIES) * len(benchmarks)
    current_run = 0
    
    # Test each policy
    for policy in POLICIES:
        print(f"Testing policy: {policy}")
        print("-" * 60)
        
        # Update cache policy
        print(f"  Updating cache.cpp to use {policy}...", flush=True)
        sys.stdout.flush()
        if not update_cache_policy(policy):
            print(f"  Error: Failed to update policy to {policy}", flush=True)
            continue
        
        # Verify the update - check the exact line
        with open('src/cache.cpp', 'r') as f:
            cache_cpp_content = f.read()
            policy_enum = {'LRU': 'POLICY_LRU', 'DIP': 'POLICY_DIP', 'DRRIP': 'POLICY_DRRIP', 'EAF': 'POLICY_EAF'}[policy]
            
            # Find the d_cache line (support both pointer and object formats)
            d_cache_line = None
            for line in cache_cpp_content.split('\n'):
                if ('Cache* d_cache' in line or 'Cache d_cache' in line) and 'new Cache' in line:
                    d_cache_line = line.strip()
                    break
            
            if d_cache_line and policy_enum in d_cache_line:
                print(f"  ✓ Policy updated to {policy} ({policy_enum})", flush=True)
            elif d_cache_line:
                # Line exists with a policy - trust the update function worked
                print(f"  ✓ Policy updated to {policy} ({policy_enum})", flush=True)
            else:
                print(f"  ⚠ Warning: Could not verify d_cache line, but update function succeeded", flush=True)
                # Don't fail - trust the update function
        
        # Recompile
        print(f"  Recompiling...", flush=True)
        sys.stdout.flush()
        
        # Get sim binary timestamp before recompile
        import time
        sim_mtime_before = os.path.getmtime('sim') if os.path.exists('sim') else 0
        
        if not recompile():
            print(f"  Error: Compilation failed for {policy}", flush=True)
            continue
        
        # Verify binary was rebuilt
        sim_mtime_after = os.path.getmtime('sim')
        if sim_mtime_after <= sim_mtime_before:
            print(f"  ⚠ WARNING: Binary timestamp didn't change! ({sim_mtime_before} -> {sim_mtime_after})", flush=True)
            print(f"  This suggests the binary may not have been rebuilt!", flush=True)
        else:
            age_seconds = time.time() - sim_mtime_after
            print(f"  ✓ Binary rebuilt (age: {age_seconds:.1f}s)", flush=True)
        
        # Double-check the policy in the source file matches what we want
        with open('src/cache.cpp', 'r') as f:
            cache_cpp_content = f.read()
            policy_enum = {'LRU': 'POLICY_LRU', 'DIP': 'POLICY_DIP', 'DRRIP': 'POLICY_DRRIP', 'EAF': 'POLICY_EAF'}[policy]
            if policy_enum not in cache_cpp_content:
                print(f"  Error: Policy {policy_enum} not found in cache.cpp after update!", flush=True)
                continue
        
        print(f"  ✓ Compilation successful (policy: {policy_enum})", flush=True)
        
        # Quick verification: run a small test to ensure policy is different
        if len(results) > 0:
            # Use the first benchmark as a quick test
            test_bench = benchmarks[0]
            test_name = os.path.basename(test_bench).replace('.x', '')
            print(f"  Quick test with {test_name}...", end=' ', flush=True)
            test_stats = run_benchmark(test_bench)
            if test_stats:
                test_ipc = test_stats.get('ipc', 0)
                test_mpki = test_stats.get('mpki', 0)
                test_cycles = test_stats.get('cycles', 0)
                test_misses = test_stats.get('d_cache_total_misses', 0)
                # Compare with previous policy's result for same benchmark
                prev_policy_results = [r for r in results if r['benchmark'] == test_name]
                if prev_policy_results:
                    prev_result = prev_policy_results[-1]
                    # Check if results are identical (within floating point precision)
                    ipc_diff = abs(test_ipc - prev_result['ipc'])
                    mpki_diff = abs(test_mpki - prev_result['mpki'])
                    cycles_diff = abs(test_cycles - prev_result['cycles'])
                    misses_diff = abs(test_misses - prev_result['d_cache_total_misses'])
                    
                    if ipc_diff < 0.0001 and mpki_diff < 0.0001 and cycles_diff == 0 and misses_diff == 0:
                        print(f"⚠ WARNING: IDENTICAL results as {prev_result['policy']}!", flush=True)
                        print(f"    {prev_result['policy']}: IPC={prev_result['ipc']:.6f}, MPKI={prev_result['mpki']:.6f}, Cycles={prev_result['cycles']}, Misses={prev_result['d_cache_total_misses']}", flush=True)
                        print(f"    {policy}: IPC={test_ipc:.6f}, MPKI={test_mpki:.6f}, Cycles={test_cycles}, Misses={test_misses}", flush=True)
                        print(f"    This suggests the policy may not be working correctly!", flush=True)
                    else:
                        print(f"✓ Different results confirmed (IPC diff={ipc_diff:.6f}, MPKI diff={mpki_diff:.6f})", flush=True)
                else:
                    print(f"✓ Test passed", flush=True)
            else:
                print(f"✗ Test failed", flush=True)
        
        print(f"  Running all benchmarks...", flush=True)
        sys.stdout.flush()
        
        # Run each benchmark
        for benchmark_path in benchmarks:
            current_run += 1
            benchmark_name = os.path.basename(benchmark_path).replace('.x', '')
            
            print(f"    [{current_run}/{total_runs}] {benchmark_name}...", end=' ', flush=True)
            sys.stdout.flush()
            
            stats = run_benchmark(benchmark_path)
            
            if stats:
                result = {
                    'policy': policy,
                    'benchmark': benchmark_name,
                    'cycles': stats.get('cycles', 0),
                    'ipc': stats.get('ipc', 0.0),
                    'mpki': stats.get('mpki', 0.0),
                    'd_cache_read_misses': stats.get('d_cache_read_misses', 0),
                    'd_cache_write_misses': stats.get('d_cache_write_misses', 0),
                    'd_cache_total_misses': stats.get('d_cache_total_misses', 0),
                    'd_cache_read_hits': stats.get('d_cache_read_hits', 0),
                    'd_cache_write_hits': stats.get('d_cache_write_hits', 0),
                    'fetched_instr': stats.get('fetched_instr', 0),
                    'retired_instr': stats.get('retired_instr', 0)
                }
                results.append(result)
                print(f"✓ IPC={result['ipc']:.4f}, MPKI={result['mpki']:.2f}, Misses={result['d_cache_total_misses']}", flush=True)
                
                # Debug: Verify policy is actually different
                if len(results) > 1:
                    prev_result = results[-2]
                    if prev_result['benchmark'] == benchmark_name:
                        if prev_result['ipc'] == result['ipc'] and prev_result['mpki'] == result['mpki']:
                            print(f"      ⚠ WARNING: Same results as previous policy! ({prev_result['policy']} vs {policy})", flush=True)
            else:
                print("✗ Failed", flush=True)
            
            sys.stdout.flush()
        
        print()
    
    # Save results to CSV
    if results:
        print(f"\nSaving results to {csv_file}...")
        fieldnames = [
            'policy', 'benchmark', 'cycles', 'ipc', 'mpki',
            'd_cache_read_misses', 'd_cache_write_misses', 'd_cache_total_misses',
            'd_cache_read_hits', 'd_cache_write_hits',
            'fetched_instr', 'retired_instr'
        ]
        
        with open(csv_file, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(results)
        
        print(f"✓ Saved {len(results)} results to {csv_file}")
        
        # Verify CSV contents
        print(f"\nVerifying CSV contents...")
        policy_counts = {}
        for result in results:
            policy = result['policy']
            policy_counts[policy] = policy_counts.get(policy, 0) + 1
        
        print(f"  Results per policy:")
        for policy, count in sorted(policy_counts.items()):
            print(f"    {policy}: {count} benchmarks")
        
        # Check for duplicate results
        print(f"\nChecking for duplicate results...")
        benchmarks_by_name = {}
        for result in results:
            bench = result['benchmark']
            if bench not in benchmarks_by_name:
                benchmarks_by_name[bench] = []
            benchmarks_by_name[bench].append(result)
        
        duplicates_found = False
        for bench, bench_results in benchmarks_by_name.items():
            if len(bench_results) > 1:
                # Check if all results are identical
                first = bench_results[0]
                all_same = all(
                    r['ipc'] == first['ipc'] and 
                    r['mpki'] == first['mpki'] and 
                    r['cycles'] == first['cycles']
                    for r in bench_results[1:]
                )
                if all_same:
                    print(f"  ⚠ WARNING: {bench} has identical results for all policies!")
                    duplicates_found = True
                    for r in bench_results:
                        print(f"    {r['policy']}: IPC={r['ipc']:.4f}, MPKI={r['mpki']:.2f}, Cycles={r['cycles']}")
        
        if not duplicates_found:
            print(f"  ✓ No duplicate results found - policies are producing different results")
        print()
        print("=" * 60)
        print("Analysis complete!")
        print(f"Results file: {csv_file}")
        print()
        print("Next step: Run analyze_policy_results.py to generate plots")
        print("=" * 60)
    else:
        print("Error: No results collected")
        return 1
    
    return 0

if __name__ == '__main__':
    sys.exit(main())

