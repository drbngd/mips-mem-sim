/*
 * Computer Architecture - Professor Onur Mutlu
 *
 * MIPS pipeline timing simulator
 *
 * Chris Fallin, 2012
 */

#ifndef _CORE_H_
#define _CORE_H_

#include "pipe.h"
#include "cache.h"
#include <memory>
#include <vector>

class Processor;

class Core {
public:
    Core(int id, Processor* p, L2Cache* l2);
    
    int id;
    bool is_running;
    Processor* proc;
    std::unique_ptr<Pipeline> pipe;
    
    /* Private Caches */
    L1Cache icache;
    L1Cache dcache;

    /* Ticks the core logic (pipeline) */
    void cycle();

    /* Handles system calls forwarded from the pipeline WB stage */
    void handle_syscall(Pipe_Op* op);
};

#endif
