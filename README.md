# MIPS MC: Multi-Core Memory Hierarchy Simulator

A cycle-accurate, multi-core MIPS32 simulator featuring a detailed memory hierarchy with **MESI cache coherence** and configurable **inclusion policies**.

## Overview

This project simulates a 5-stage pipelined MIPS processor connected to a realistic memory hierarchy. It is designed to model the timing and behavior of caching protocols in a multi-core environment. The simulator supports:

- **Instruction Set**: MIPS32 (User-level subset)
- **Cores**: Configurable number of cores (Single or Multi-core)
- **Coherence**: Full **MESI** (Modified, Exclusive, Shared, Invalid) protocol with bus snooping.
- **Inclusion**: Configurable L2 inclusion policies (**Inclusive**, **Exclusive**, Non-Inclusive).

## Key Features

### 1. Processor Pipeline
*   **5-Stage Pipeline**: Fetch, Decode, Execute, Memory, Writeback.
*   **Hazard Handling**: Full forwarding and stall logic.
*   **SYSCALL Serialization**: Strict serialization for system calls to ensure correctness in multi-threaded execution.
*   **Branch Prediction**: Static Not-Taken predictor with recovery flushing.

### 2. Memory Hierarchy
*   **L1 Caches**: Private Instruction and Data caches per core.
    *   Non-blocking access using **MSHRs** (Miss Status Handling Registers).
    *   **Snooping**: Peer-to-peer invalidation and downgrades.
*   **L2 Cache**: Unified, shared L2 cache.
    *   Handles **Back-Invalidation** for Inclusive policies.
    *   Acts as **Victim Cache** for Exclusive policy.
*   **DRAM**: Bandwidth-limited main memory with bank conflicts and access latency.

### 3. Coherence & Policies
*   **Protocol**: MESI (Invalidate-on-Write).
*   **L2 Policies**:
    *   `INCL_INCLUSIVE`: L2 implies L1. L2 eviction forces L1 invalidation.
    *   `INCL_EXCLUSIVE`: Blocks exist in *either* L1 or L2, never both in valid state. L2 acts as a victim cache.
    *   `INCL_NINE`: Non-Inclusive Non-Exclusive.

## Getting Started

### Prerequisites
*   C++17 compatible compiler (g++, clang++)
*   Python 3 (for running test scripts)
*   Make

### Building
Compile the simulator using the provided Makefile:
```bash
make clean run
# This produces the 'sim' binary
```

### Running
Run the simulator on a specific input trace or executable:
```bash
./sim <input_file.hex>
```

Or use the python runner for verification:
```bash
python run.py inputs/tests/thread_tests/test1.hex
```

## Configuration
System parameters can be adjusted in `src/config.h`:

```c
#define NUM_CORES 4              // Number of active cores
#define L2_INCL_POLICY INCL_INCLUSIVE // INCL_INCLUSIVE, INCL_EXCLUSIVE, or INCL_NINE
#define L1_LATENCY 1
#define L2_LATENCY 5
#define DRAM_LATENCY 100
```
*Recompile (`make clean run`) after changing configuration.*

## Project Structure

*   `src/cache.cpp/h`: Implementation of L1/L2 caches, MESI state transitions, probe logic, and inclusion handling.
*   `src/pipe.cpp/h`: 5-stage MIPS pipeline logic, hazard detection, and syscall serialization.
*   `src/core.cpp/h`: Core container connecting pipeline and private caches.
*   `src/processor.cpp`: Top-level orchestration of cores and shared memory.
*   `src/dram.cpp/h`: Main memory timing model.
*   `src/mshr.h`: Miss Status Handling Register definition.

## Attribution
This project is based on the **Computer Architecture** lab assignments by [Professor Onur Mutlu](https://safari.ethz.ch/) at ETH Zurich. Use these materials for educational purposes.
*   Original Lab Specs: [Computer Architecture - Labs](https://safari.ethz.ch/architecture/fall2025/doku.php?id=labs)