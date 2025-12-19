# Replacement Policy Benchmarks

This directory contains benchmarks designed to stress-test cache replacement policies. The benchmarks are organized into four categories based on access patterns.

## Benchmark Categories

### 1. Recency-Friendly Access Patterns
These benchmarks have near-immediate re-reference intervals and benefit from LRU replacement. Other policies may degrade performance.

- **`recency_stack.s`**: Stack-like push/pop pattern with small working set (8 words, 32 bytes)
- **`recency_nested.s`**: Nested loops accessing same 16-word working set repeatedly
- **`recency_small_ws.s`**: Very small working set (4 words, 16 bytes) accessed repeatedly

**Expected behavior**: LRU should perform best. Policies that insert at LRU position (like BIP) may perform worse.

### 2. Thrashing Access Patterns
These benchmarks use cyclic access patterns where the cycle length (k) is larger than the cache size, causing cache thrashing. LRU gets zero cache hits in these cases.

- **`thrash_cyclic.s`**: Cyclic access pattern of 2048 words (8KB), repeated 500 times
- **`thrash_competing.s`**: Three competing arrays (1024 words each) accessed in round-robin, causing constant evictions
- **`thrash_conflict.s`**: Large-stride access pattern that maps to same cache sets, maximizing conflicts
- **`thrash_large_cycle.s`**: Very large cyclic pattern (16384 words, 64KB), severe thrashing

**Expected behavior**: LRU should perform worst (zero hits). Policies like BIP, BRRIP, and EAF should preserve some working set and perform better.

### 3. Streaming Access Patterns
These benchmarks have no temporal locality (infinite re-reference interval). No replacement policy can achieve cache hits.

- **`stream_seq_read.s`**: Sequential read through large array (65536 words, 256KB)
- **`stream_seq_write.s`**: Sequential write through large array (65536 words, 256KB)
- **`stream_large_stride.s`**: Large-stride access (4096-word stride) with no re-reference

**Expected behavior**: All policies should perform similarly since there are no cache hits regardless of replacement policy.

### 4. Mixed Access Patterns
These benchmarks combine recency-friendly patterns with streaming scans. LRU discards the frequently-referenced working set after scans.

- **`mixed_hot_scan.s`**: Small hot set (32 words) accessed repeatedly, then large scan (8192 words) that pollutes cache
- **`mixed_list_scan.s`**: Linked list traversal (64 nodes) followed by large array scan (4096 words)
- **`mixed_working_scan.s`**: Working set (128 words) accessed many times, then large scan (16384 words)
- **`mixed_small_large.s`**: Alternates between small working set (16 words) and large scan (8192 words)

**Expected behavior**: LRU should perform poorly after scans discard the hot set. Policies that preserve working sets (BIP, BRRIP, EAF) should perform better.

## Cache Configuration

These benchmarks are designed for a D-cache with:
- **Size**: 512KB (1024 sets × 8-way × 64 bytes)
- **Line size**: 64 bytes
- **Associativity**: 8-way

**Note**: Benchmarks have been optimized for quick execution while still effectively stressing replacement policies. Iteration counts and array sizes have been reduced to make them run faster.

Adjust benchmark sizes if your cache configuration differs significantly.

## Usage

Assemble all benchmarks:
```bash
python3 assembler.py inputs/cache/repl/*.s
```

Run a benchmark:
```bash
./sim inputs/cache/repl/<benchmark>.x
```

## Expected Policy Performance

| Pattern Type | LRU | DIP | DRRIP | EAF |
|------------|-----|-----|-------|-----|
| Recency-friendly | Best | Good | Good | Good |
| Thrashing | Worst | Better | Better | Better |
| Streaming | Same | Same | Same | Same |
| Mixed | Poor | Better | Better | Best* |

*EAF should excel in mixed patterns by detecting high-reuse addresses and preserving them after scans.

