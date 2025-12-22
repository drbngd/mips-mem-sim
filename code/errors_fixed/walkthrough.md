# Walkthrough - MSHR Leak Fix

I have fixed the deadlock issue where the simulator would hang after approximately 1.1 million cycles.

## Changes

### [pipe.cpp](file:///Users/drbngd/Developer/github/mips-mem-sim/code/src/pipe.cpp)

I added logic to free MSHRs when the pipeline flushes the Fetch and Memory stages during a branch recovery.

```cpp
// In pipe_cycle() - Fetch Stage Flush
pipe.fetch_stall = 0;
if (pipe.fetch_mshr_index >= 0) {
    pipe.mshr_manager->free(pipe.fetch_mshr_index); // [NEW] Free the MSHR
}
pipe.fetch_mshr_index = -1;
```

```cpp
// In pipe_cycle() - Memory Stage Flush
pipe.mem_op.reset();
// ...
if (pipe.mem_mshr_index >= 0) {
    pipe.mshr_manager->free(pipe.mem_mshr_index); // [NEW] Free the MSHR
}
pipe.mem_mshr_index = -1;
```

# Walkthrough - PC Off-By-4 Fix

I also fixed an issue where the Final PC was off by 4 (one instruction behind) compared to the baseline.

## Root Cause
When the simulation terminates (syscall 10), the pipeline flushes. However, two issues prevented the final PC update:
1. **Pending MSHR Block**: If the fetch stage had an active MSHR (prefetch or speculative fetch), it wasn't cleared by the syscall exit logic, causing the fetch stage to return early without incrementing the PC.
2. **Termination Stall**: If the final instruction fetch experienced a cache miss/stall on the very last cycle, the simulation would terminate (`RUN_BIT=false`) before the fetch completed, leaving the PC un-updated.

## Changes

### [pipe.cpp](file:///Users/drbngd/Developer/github/mips-mem-sim/code/src/pipe.cpp)

1. **Clear Pending Fetch MSHR on Exit**: In `pipe_stage_wb`, when handling syscall exit:
```cpp
// In pipe_stage_wb() - Syscall Exit
pipe.fetch_stall = 0;
if (pipe.fetch_mshr_index >= 0) {
    pipe.mshr_manager->free(pipe.fetch_mshr_index); // [NEW] Free pending fetch MSHR
    pipe.fetch_mshr_index = -1;
}
RUN_BIT = false;
```

2. **Force Fetch Completion on Termination**: In `pipe_stage_fetch`, bypass stall logic if simulation is terminating:
```cpp
// In pipe_stage_fetch()
// Bypass stall checks if !RUN_BIT to ensure the final PC update happens
if (result.latency == -1 && RUN_BIT) { ... }
if (result.latency > 0 && RUN_BIT) { ... }
```

# Walkthrough - Logic Error Fixes

I fixed data corruption issues affecting `pointer_chase.x` and `working_set.x`.

## Root Causes

1. **Incorrect `SW` Retry Logic**: When a store instruction (`SW`) missed in the cache and allocated an MSHR, the pipeline stalled. Upon resumption, the logic incorrectly skipped the actual cache write, assuming it was done. This caused stores to be lost, leading to zeros being read back in `pointer_chase.x`.
2. **Missing L1-to-L2 Writeback**: When a dirty line was evicted from L1, it was written directly to memory, bypassing the L2 cache. This left stale data in L2. If the line was later re-fetched from L2 (as in `working_set.x`), the processor received old data, causing calculation errors (R10 mismatch).

## Changes

### [pipe.cpp](file:///Users/drbngd/Developer/github/mips-mem-sim/code/src/pipe.cpp)

Modified `pipe_stage_mem` to explicitly perform the cache write for `OP_SW` when resuming from a stall (MSHR or L2 Hit).

```cpp
// In pipe_stage_mem()
if (pipe.mem_cache_op_done) {
    if (op->opcode != OP_SW) {
        val = pipe.pending_mem_data; // Loads use pending data
    } else {
        // [NEW] For SW, cache was filled, now perform the actual write
        d_cache->write(op->mem_addr & ~3, op->mem_value);
    }
}
```

### [cache.cpp](file:///Users/drbngd/Developer/github/mips-mem-sim/code/src/cache.cpp)

Modified `Cache::evict` to write back dirty L1 lines to the L2 cache instead of main memory.

```cpp
// In Cache::evict()
if (!is_l2()) {
    /* L1 eviction: write back to L2 */
    extern Cache* l2_cache;
    for (...) {
       l2_cache->write(line_addr + i, val);
    }
} else {
    /* L2 eviction: write back to memory */
    // ...
}
```

## Verification Results

### Automated Tests Coverage

All provided test cases now pass with **REGISTER CONTENTS OK**.

- **inputs/branch/test1.x**: Passed.
- **inputs/cache/vector_add.x**: Passed.
- **inputs/cache/pointer_chase.x**: Passed. Registers match baseline exactly.
- **inputs/cache/working_set.x**: Passed. R10 now correctly reads `0x00000008` (matching baseline).
