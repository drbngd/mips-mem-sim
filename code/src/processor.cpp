/*
 * Computer Architecture - Professor Onur Mutlu
 *
 * MIPS pipeline timing simulator
 *
 * Chris Fallin, 2012
 */

#include "processor.h"
#include "config.h"

Processor::Processor() : l2_cache(&dram) {
    /* Initialize NUM_CORES Cores */
    for (int i = 0; i < NUM_CORES; i++) {
        cores.push_back(std::make_unique<Core>(i, this, &l2_cache));
    }
}

extern uint32_t stat_cycles;
extern uint32_t mem_read_32(uint32_t address); // From shell.cpp

void Processor::cycle() {
    /* 1. Drive Memory Hierarchy */
    // L2 access is demand-driven by Cores (in core->cycle), but DRAM is autonomous.
    DRAM_Req completed_req = dram.execute(stat_cycles);
    
    if (completed_req.valid) {
        // Data returned from Memory
        // 1. Update L2 (Clear MSHR and Install)
        l2_cache.complete_mshr(completed_req.addr);
        
        // 2. Update L1 (Wakeup Core)
        if (completed_req.core_id >= 0 && completed_req.core_id < NUM_CORES) {
             cores[completed_req.core_id]->icache.fill(completed_req.addr);
             cores[completed_req.core_id]->dcache.fill(completed_req.addr);
        }
    }

    /* 2. Tick all cores */
    for (int i = 0; i < NUM_CORES; i++) {
        cores[i]->cycle();
    }
}

int Processor::active_cores_count() {
    int count = 0;
    for (int i = 0; i < NUM_CORES; i++) {
        if (cores[i]->is_running) count++;
    }
    return count;
}
