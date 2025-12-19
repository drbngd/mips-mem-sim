#!/usr/bin/env python
"""
Utility functions for cache parameter sweeping.
"""

import re
import subprocess
import os
import sys

def calculate_cache_size_kb(num_sets, line_size_bytes, associativity):
    """
    Calculate cache size in KB from number of sets.
    Formula: cache_size_kb = (num_sets * line_size_bytes * associativity) / 1024
    """
    cache_size_bytes = num_sets * line_size_bytes * associativity
    cache_size_kb = cache_size_bytes / 1024
    return cache_size_kb

def update_cache_h(d_cache_num_sets, line_size_bytes, d_cache_assoc, 
                   i_cache_num_sets=None, i_cache_assoc=None):
    """
    Update cache.h with new parameters.
    
    Args:
        d_cache_num_sets: Data cache number of sets
        line_size_bytes: Cache line size in bytes
        d_cache_assoc: Data cache associativity
        i_cache_num_sets: Instruction cache number of sets (optional, defaults to 64)
        i_cache_assoc: Instruction cache associativity (optional, defaults to 4)
    """
    cache_h_path = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'src', 'cache.h')
    
    if i_cache_num_sets is None:
        i_cache_num_sets = 64  # Default I-cache sets
    elif isinstance(i_cache_num_sets, list):
        i_cache_num_sets = i_cache_num_sets[0]  # Use first value if array
    
    if i_cache_assoc is None:
        i_cache_assoc = 4  # Default I-cache associativity
    elif isinstance(i_cache_assoc, list):
        i_cache_assoc = i_cache_assoc[0]  # Use first value if array
    
    # Read current cache.h
    with open(cache_h_path, 'r') as f:
        content = f.read()
    
    # Replace D-cache parameters (handle both number and array syntax)
    content = re.sub(r'#define D_CACHE_NUM_SETS\s+[^\n]+', 
                     f'#define D_CACHE_NUM_SETS {d_cache_num_sets}', content)
    content = re.sub(r'#define D_CACHE_ASSOC\s+[^\n]+',
                     f'#define D_CACHE_ASSOC {d_cache_assoc}', content)
    
    # Replace I-cache parameters (handle both number and array syntax)
    content = re.sub(r'#define I_CACHE_NUM_SETS\s+[^\n]+',
                     f'#define I_CACHE_NUM_SETS {i_cache_num_sets}', content)
    content = re.sub(r'#define I_CACHE_ASSOC\s+[^\n]+',
                     f'#define I_CACHE_ASSOC {i_cache_assoc}', content)
    
    # Replace line size (handle both number and array syntax)
    content = re.sub(r'#define CACHE_LINE_SIZE\s+[^\n]+',
                     f'#define CACHE_LINE_SIZE {line_size_bytes}', content)
    
    # Write back
    with open(cache_h_path, 'w') as f:
        f.write(content)
    
    return {
        'd_cache_num_sets': d_cache_num_sets,
        'i_cache_num_sets': i_cache_num_sets
    }

def recompile():
    """Recompile the simulator."""
    code_dir = os.path.dirname(os.path.dirname(__file__))
    os.chdir(code_dir)
    
    # Clean
    result = subprocess.run(['make', 'clean'], 
                          capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Warning: make clean failed: {result.stderr}", file=sys.stderr)
    
    # Compile
    result = subprocess.run(['make'], 
                          capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"Compilation failed: {result.stderr}")
    
    return result.returncode == 0

def run_benchmark(benchmark_name, sim_path='./sim'):
    """
    Run a benchmark and extract metrics.
    
    Args:
        benchmark_name: Name of benchmark (without .x extension)
        sim_path: Path to simulator executable
    
    Returns:
        dict with metrics or None if failed
    """
    code_dir = os.path.dirname(os.path.dirname(__file__))
    benchmark_path = os.path.join(code_dir, 'inputs', 'cache', f'{benchmark_name}.x')
    
    if not os.path.exists(benchmark_path):
        print(f"Warning: Benchmark {benchmark_path} not found", file=sys.stderr)
        return None
    
    # Run simulator
    cmd = f'echo -e "go\\nrdump\\nquit" | {sim_path} {benchmark_path}'
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd=code_dir)
    
    if result.returncode != 0:
        print(f"Warning: Benchmark {benchmark_name} failed: {result.stderr}", file=sys.stderr)
        return None
    
    # Parse output
    return parse_sim_output(result.stdout)

def parse_sim_output(output):
    """
    Parse simulator output to extract metrics.
    
    Currently rdump() outputs:
    - Cycles
    - FetchedInstr (stat_inst_fetch)
    - RetiredInstr (stat_inst_retire)
    - IPC
    
    Note: Cache statistics are tracked in cache.cpp but not printed by rdump().
    To get cache stats, you may need to add them to rdump() in shell.cpp.
    
    Returns:
        dict with: cycles, ipc, fetched_instr, retired_instr
    """
    metrics = {
        'cycles': None,
        'ipc': None,
        'fetched_instr': None,
        'retired_instr': None,
        'i_cache_accesses': None,
        'i_cache_reads': None,
        'i_cache_writes': None,
        'i_cache_hits': None,
        'i_cache_misses': None,
        'i_cache_hit_rate': None,
        'i_cache_miss_rate': None,
        'd_cache_accesses': None,
        'd_cache_reads': None,
        'd_cache_writes': None,
        'd_cache_hits': None,
        'd_cache_misses': None,
        'd_cache_hit_rate': None,
        'd_cache_miss_rate': None
    }
    
    # Parse cycles
    match = re.search(r'Cycles:\s+(\d+)', output)
    if match:
        metrics['cycles'] = int(match.group(1))
    
    # Parse IPC
    match = re.search(r'IPC:\s+([\d.]+)', output)
    if match:
        metrics['ipc'] = float(match.group(1))
    
    # Parse fetched instructions
    match = re.search(r'FetchedInstr:\s+(\d+)', output)
    if match:
        metrics['fetched_instr'] = int(match.group(1))
    
    # Parse retired instructions
    match = re.search(r'RetiredInstr:\s+(\d+)', output)
    if match:
        metrics['retired_instr'] = int(match.group(1))
    
    # Parse cache statistics
    match = re.search(r'I-cache\s+accesses:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['i_cache_accesses'] = int(match.group(1))
    
    match = re.search(r'I-cache\s+reads:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['i_cache_reads'] = int(match.group(1))
    
    match = re.search(r'I-cache\s+writes:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['i_cache_writes'] = int(match.group(1))
    
    match = re.search(r'I-cache\s+hits:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['i_cache_hits'] = int(match.group(1))
    
    match = re.search(r'I-cache\s+misses:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['i_cache_misses'] = int(match.group(1))
    
    match = re.search(r'I-cache\s+hit\s+rate:\s+([\d.]+)', output, re.IGNORECASE)
    if match:
        metrics['i_cache_hit_rate'] = float(match.group(1))
    
    match = re.search(r'I-cache\s+miss\s+rate:\s+([\d.]+)', output, re.IGNORECASE)
    if match:
        metrics['i_cache_miss_rate'] = float(match.group(1))
    
    match = re.search(r'D-cache\s+accesses:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['d_cache_accesses'] = int(match.group(1))
    
    match = re.search(r'D-cache\s+reads:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['d_cache_reads'] = int(match.group(1))
    
    match = re.search(r'D-cache\s+writes:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['d_cache_writes'] = int(match.group(1))
    
    match = re.search(r'D-cache\s+hits:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['d_cache_hits'] = int(match.group(1))
    
    match = re.search(r'D-cache\s+misses:\s+(\d+)', output, re.IGNORECASE)
    if match:
        metrics['d_cache_misses'] = int(match.group(1))
    
    match = re.search(r'D-cache\s+hit\s+rate:\s+([\d.]+)', output, re.IGNORECASE)
    if match:
        metrics['d_cache_hit_rate'] = float(match.group(1))
    
    match = re.search(r'D-cache\s+miss\s+rate:\s+([\d.]+)', output, re.IGNORECASE)
    if match:
        metrics['d_cache_miss_rate'] = float(match.group(1))
    
    return metrics

def get_benchmarks():
    """Get list of available benchmarks."""
    code_dir = os.path.dirname(os.path.dirname(__file__))
    cache_dir = os.path.join(code_dir, 'inputs', 'cache')
    
    benchmarks = []
    if os.path.exists(cache_dir):
        for filename in os.listdir(cache_dir):
            if filename.endswith('.x'):
                benchmarks.append(filename[:-2])  # Remove .x extension
    
    return sorted(benchmarks)

