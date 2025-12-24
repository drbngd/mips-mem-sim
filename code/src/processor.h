/*
 * Computer Architecture - Professor Onur Mutlu
 *
 * MIPS pipeline timing simulator
 *
 * Chris Fallin, 2012
 */

#ifndef _PROCESSOR_H_
#define _PROCESSOR_H_

#include "core.h"
#include "cache.h"
#include "dram.h"
#include <vector>
#include <memory>

class Processor {
public:
    Processor();

    /* Pointers to the 4 Cores */
    std::vector<std::unique_ptr<Core>> cores;
    
    /* Shared Memory Hierarchy */
    L2Cache l2_cache;
    DRAM dram;

    /* Ticks the entire system (all cores) */
    void cycle();

    /* Returns number of cores currently running */
    int active_cores_count();
};

#endif
