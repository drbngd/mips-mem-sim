# Phase 2 Implementation: Memory Hierarchy

This document details the implementation of the Memory Hierarchy for the MIPS Simulator, transforming it from an ideal 1-cycle memory model to a realistic timing simulation.

## Overview

The Phase 2 update introduces a three-level memory hierarchy:
1.  **L1 Caches**: Separate Instruction (I-Cache) and Data (D-Cache).
2.  **L2 Cache**: Unified, inclusive L2 cache with MSHRs (Miss Status Holding Registers) for non-blocking access.
3.  **DRAM Controller**: A realistic main memory controller modeling bank, row, and bus latencies.

## Components

### 1. L1 Caches (`L1Cache` in `src/cache.cpp`)
-   **Configuration**: Direct-mapped or set-associative (configurable).
-   **Blocking**: The L1 cache stalls the pipeline on a miss until data is retrieved from L2.
-   **Interface**: 
    -   `access(addr, is_write)`: Checks for hits. Returns `false` on miss, triggering a pipeline stall.
    -   `fill(addr)`: Called by L2 when data arrives, waking up the pipeline.

### 2. L2 Cache (`L2Cache` in `src/cache.cpp`)
-   **Configuration**: Larger, unified cache.
-   **Non-Blocking**: Uses **MSHRs** (Miss Status Holding Registers) to track multiple outstanding requests to DRAM.
-   **Inclusion**: Enforces inclusion (mostly); invalidations back to L1 are planned for future coherence updates.
-   **Interface**:
    -   Accepts requests from L1.
    -   If miss, allocates MSHR and enqueues request to DRAM.

### 3. DRAM Controller (`DRAM` in `src/dram.cpp`)
-   **Model**: Single-channel, Multi-bank DDR-style memory.
-   **Scheduling**: FR-FCFS (First-Ready First-Come-First-Serve) logic.
-   **Latency Modeling**:
    -   Tracks `Bank Busy`, `Command Bus`, and `Data Bus` timings.
    -   Models Row Hits (fast), Row Closed (medium), and Row Conflicts (slow).
-   **Cycle-Accurate**: `dram.execute(cycle)` is called every simulator tick to progress state and return completed requests.

## Pipeline Integration (`src/pipe.cpp`)

To make the pipeline sensitive to latency, we "hooked" the memory hierarchy into the `fetch` and `mem` stages:

-   **Fetch Stage**:
    ```cpp
    if (!core->icache.access(PC, ...)) return; // Stall on I-Cache Miss
    ```
-   **Memory Stage**:
    ```cpp
    if (!core->dcache.access(addr, ...)) return; // Stall on D-Cache Miss
    ```

## PC Mismatch Fix
A critical bug was resolved where the CPU would halt non-deterministically depending on whether the final fetch hit or missed the cache.
-   **Fix**: `Core::handle_syscall` (halt) now explicitly sets `PC = InstructionPC + 4`.
-   **Guard**: `Core::cycle` strictly prevents `fetch()` from running if the CPU is halted.

## Verification
-   **Functionality**: Validated using `random*.x` and `jalr.x` tests. Register values match the Baseline Simulator.
-   **Timing**: Cycle counts for memory-heavy tests increased by ~17x (e.g., from 2.5k to 42k cycles), correctly reflecting the cost of DRAM accesses.
