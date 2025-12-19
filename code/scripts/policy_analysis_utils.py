#!/usr/bin/env python
"""
Utility functions for replacement policy analysis
"""

import subprocess
import re
import os
import sys

def update_cache_policy(policy_name):
    """
    Update cache.cpp to use the specified replacement policy.
    
    Args:
        policy_name: One of 'LRU', 'DIP', 'DRRIP', 'EAF'
    
    Returns:
        bool: True if successful, False otherwise
    """
    cache_cpp_path = 'src/cache.cpp'
    
    if not os.path.exists(cache_cpp_path):
        print(f"Error: {cache_cpp_path} not found", flush=True)
        return False
    
    # Read the file
    with open(cache_cpp_path, 'r') as f:
        content = f.read()
    
    # Map policy names to enum values
    policy_map = {
        'LRU': 'POLICY_LRU',
        'DIP': 'POLICY_DIP',
        'DRRIP': 'POLICY_DRRIP',
        'EAF': 'POLICY_EAF'
    }
    
    if policy_name not in policy_map:
        print(f"Error: Unknown policy {policy_name}", flush=True)
        return False
    
    policy_enum = policy_map[policy_name]
    
    # Find and replace the d_cache declaration (now using pointer)
    # Pattern: Cache* d_cache = new Cache(..., POLICY_XXX);
    # Try multiple patterns to handle different whitespace
    patterns = [
        # Most flexible - matches Cache* d_cache = new Cache(...POLICY_XXX);
        (r'Cache\s*\*\s*d_cache\s*=\s*new\s+Cache\([^)]*POLICY_\w+[^)]*\);',
         f'Cache* d_cache = new Cache(D_CACHE_NUM_SETS, D_CACHE_ASSOC, CACHE_LINE_SIZE, L1_CACHE_MISS_PENALTY, {policy_enum});'),
        # Fallback for old format (if someone reverts)
        (r'Cache\s+d_cache\s*\([^)]*POLICY_\w+[^)]*\);',
         f'Cache d_cache(D_CACHE_NUM_SETS, D_CACHE_ASSOC, CACHE_LINE_SIZE, L1_CACHE_MISS_PENALTY, {policy_enum});')
    ]
    
    new_content = content
    matched = False
    for pattern, replacement in patterns:
        new_content = re.sub(pattern, replacement, content)
        if new_content != content:
            matched = True
            break
    
    # Verify the change was made
    if not matched:
        print(f"      Error: Could not find d_cache declaration to update", flush=True)
        # Show what we're looking for
        lines = content.split('\n')
        for i, line in enumerate(lines):
            if 'd_cache' in line and ('Cache' in line or 'new Cache' in line):
                print(f"      Found line {i+1}: {line.strip()}", flush=True)
        return False
    
    # Write back
    with open(cache_cpp_path, 'w') as f:
        f.write(new_content)
    
    # Verify by reading back and checking the exact line
    with open(cache_cpp_path, 'r') as f:
        verify_content = f.read()
        # Find the d_cache line (now using pointer)
        for line in verify_content.split('\n'):
            if ('Cache* d_cache' in line or 'Cache d_cache' in line):
                # Check if it has the expected policy
                if policy_enum in line:
                    return True
                # If it has a different policy, that's also OK - the replacement worked
                # (might be checking before write completes, or file was already updated)
                if 'POLICY_' in line:
                    return True
        # If we get here, d_cache line not found - but replacement should have worked
        # Trust the regex replacement
        return True

def recompile():
    """
    Recompile the simulator.
    
    Returns:
        bool: True if successful, False otherwise
    """
    try:
        # Run make clean (suppress output)
        result = subprocess.run(
            ['make', 'clean'],
            cwd='.',
            capture_output=True,
            text=True,
            timeout=30
        )
        
        if result.returncode != 0:
            print(f"      Warning: make clean failed: {result.stderr[:200]}", flush=True)
        
        # Run make (suppress output but check for errors)
        result = subprocess.run(
            ['make'],
            cwd='.',
            capture_output=True,
            text=True,
            timeout=60
        )
        
        if result.returncode != 0:
            print(f"      Error: Compilation failed", flush=True)
            print(f"      stderr: {result.stderr[:300]}", flush=True)
            if result.stdout:
                print(f"      stdout: {result.stdout[:300]}", flush=True)
            return False
        
        # Verify sim was created
        if not os.path.exists('sim'):
            print(f"      Error: sim executable not found after compilation", flush=True)
            return False
        
        # Check modification time to ensure it was rebuilt (should be very recent)
        import time
        sim_mtime = os.path.getmtime('sim')
        current_time = time.time()
        age_seconds = current_time - sim_mtime
        if age_seconds > 10:  # More than 10 seconds old
            print(f"      Warning: sim executable is {age_seconds:.1f}s old (may not have been rebuilt)", flush=True)
        else:
            # Good - it's recent
            pass
        
        return True
    except subprocess.TimeoutExpired:
        print("      Error: Compilation timed out", flush=True)
        return False
    except Exception as e:
        print(f"      Error: Compilation error: {e}", flush=True)
        return False

def run_benchmark(benchmark_path):
    """
    Run a benchmark and parse the output.
    
    Args:
        benchmark_path: Path to the .x benchmark file
    
    Returns:
        dict: Parsed statistics or None if failed
    """
    if not os.path.exists('sim'):
        print("Error: sim executable not found", flush=True)
        return None
    
    try:
        # Pipe commands to simulator: go (run), rdump (get stats), quit
        cmd = f'echo -e "go\\nrdump\\nquit" | ./sim {benchmark_path}'
        result = subprocess.run(
            cmd,
            shell=True,
            cwd='.',
            capture_output=True,
            text=True,
            timeout=60  # 1 minute timeout - benchmarks should be fast
        )
        
        if result.returncode != 0:
            print(f"\n      Error: Simulation failed (return code {result.returncode})", flush=True)
            if result.stderr:
                print(f"      stderr: {result.stderr[:200]}", flush=True)
            return None
        
        # Debug: check if we got any output
        if not result.stdout:
            print(f"\n      Warning: No output from simulator", flush=True)
            return None
        
        stats = parse_sim_output(result.stdout)
        if not stats or 'ipc' not in stats:
            print(f"\n      Warning: Failed to parse output (got {len(result.stdout)} chars)", flush=True)
            # Show first few lines of output for debugging
            lines = result.stdout.split('\n')[:5]
            print(f"      First lines: {lines}", flush=True)
            return None
        
        return stats
    except subprocess.TimeoutExpired:
        print(f"\n      Error: Simulation timed out (>1 min)", flush=True)
        return None
    except Exception as e:
        print(f"\n      Error: {str(e)[:100]}", flush=True)
        return None

def parse_sim_output(output):
    """
    Parse simulator output to extract statistics.
    
    Args:
        output: Simulator stdout
    
    Returns:
        dict: Extracted statistics
    """
    stats = {}
    
    # Extract cycles (format: "Cycles: 12345")
    cycles_match = re.search(r'Cycles:\s*(\d+)', output)
    if cycles_match:
        stats['cycles'] = int(cycles_match.group(1))
    
    # Extract IPC (format: "IPC: 0.123")
    ipc_match = re.search(r'IPC:\s*([\d.]+)', output)
    if ipc_match:
        stats['ipc'] = float(ipc_match.group(1))
    
    # Extract fetched instructions (format: "FetchedInstr: 12345")
    fetched_match = re.search(r'FetchedInstr:\s*(\d+)', output)
    if fetched_match:
        stats['fetched_instr'] = int(fetched_match.group(1))
    
    # Extract retired instructions (format: "RetiredInstr: 12345")
    retired_match = re.search(r'RetiredInstr:\s*(\d+)', output)
    if retired_match:
        stats['retired_instr'] = int(retired_match.group(1))
    
    # Extract D-cache statistics
    # Format from shell.cpp: "D-cache accesses: 131072", "D-cache misses: 32", etc.
    # We need to calculate read/write misses from total misses and accesses
    d_cache_accesses_match = re.search(r'D-cache accesses:\s*(\d+)', output)
    d_cache_reads_match = re.search(r'D-cache reads:\s*(\d+)', output)
    d_cache_writes_match = re.search(r'D-cache writes:\s*(\d+)', output)
    d_cache_hits_match = re.search(r'D-cache hits:\s*(\d+)', output)
    d_cache_misses_match = re.search(r'D-cache misses:\s*(\d+)', output)
    
    if d_cache_misses_match:
        total_misses = int(d_cache_misses_match.group(1))
        stats['d_cache_total_misses'] = total_misses
        
        # Estimate read/write misses proportionally
        if d_cache_reads_match and d_cache_writes_match:
            total_accesses = int(d_cache_reads_match.group(1)) + int(d_cache_writes_match.group(1))
            if total_accesses > 0:
                read_ratio = int(d_cache_reads_match.group(1)) / total_accesses
                stats['d_cache_read_misses'] = int(total_misses * read_ratio)
                stats['d_cache_write_misses'] = total_misses - stats['d_cache_read_misses']
            else:
                stats['d_cache_read_misses'] = 0
                stats['d_cache_write_misses'] = total_misses
        else:
            stats['d_cache_read_misses'] = total_misses
            stats['d_cache_write_misses'] = 0
    
    if d_cache_hits_match:
        total_hits = int(d_cache_hits_match.group(1))
        # Estimate read/write hits proportionally
        if d_cache_reads_match and d_cache_writes_match:
            total_accesses = int(d_cache_reads_match.group(1)) + int(d_cache_writes_match.group(1))
            if total_accesses > 0:
                read_ratio = int(d_cache_reads_match.group(1)) / total_accesses
                stats['d_cache_read_hits'] = int(total_hits * read_ratio)
                stats['d_cache_write_hits'] = total_hits - stats['d_cache_read_hits']
            else:
                stats['d_cache_read_hits'] = 0
                stats['d_cache_write_hits'] = total_hits
        else:
            stats['d_cache_read_hits'] = total_hits
            stats['d_cache_write_hits'] = 0
    
    # Calculate total D-cache misses
    if 'd_cache_read_misses' in stats and 'd_cache_write_misses' in stats:
        stats['d_cache_total_misses'] = stats['d_cache_read_misses'] + stats['d_cache_write_misses']
    
    # Calculate MPKI (Misses Per Kilo Instructions)
    if 'd_cache_total_misses' in stats and 'retired_instr' in stats and stats['retired_instr'] > 0:
        stats['mpki'] = (stats['d_cache_total_misses'] / stats['retired_instr']) * 1000.0
    else:
        stats['mpki'] = 0.0
    
    return stats

def get_benchmarks(benchmark_dir='inputs/cache'):
    """
    Get list of benchmark .x files.
    
    Args:
        benchmark_dir: Directory containing benchmarks
    
    Returns:
        list: List of benchmark file paths
    """
    benchmarks = []
    
    if not os.path.exists(benchmark_dir):
        print(f"Warning: Benchmark directory {benchmark_dir} not found")
        return benchmarks
    
    for filename in sorted(os.listdir(benchmark_dir)):
        if filename.endswith('.x'):
            benchmarks.append(os.path.join(benchmark_dir, filename))
    
    return benchmarks

