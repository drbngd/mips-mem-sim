#include "mshr.h"
#include "cache.h"
#include "shell.h"
#include <algorithm>
#include <vector>

// #define DEBUG

MSHRManager::MSHRManager(uint32_t line_size) : line_size(line_size) {
    for (int i = 0; i < NUM_MSHRS; i++) {
        mshrs[i].valid = false;
        mshrs[i].state = MSHR_IDLE;
        mshrs[i].address = 0;
        mshrs[i].alloc_tick = 0;
        mshrs[i].completion_cycle = 0;
        mshrs[i].dram_request_cycle = 0;
        mshrs[i].data.resize(line_size, 0);  /* Initialize data buffer */
    }
}

MSHRManager::~MSHRManager() {
    // Nothing to clean up
}

int MSHRManager::allocate(uint32_t address) {
    /* Find first free MSHR */
    /* address should already be line-aligned */
    for (int i = 0; i < NUM_MSHRS; i++) {
        if (!mshrs[i].valid) {
            mshrs[i].valid = true;
            mshrs[i].address = address & ~(line_size - 1);  /* Line-align */
            mshrs[i].state = MSHR_WAITING_SEND;
            mshrs[i].alloc_tick = stat_cycles;
            mshrs[i].completion_cycle = stat_cycles + L2_TO_MEM_LATENCY + DRAM_LATENCY + MEM_TO_L2_LATENCY;
            mshrs[i].dram_request_cycle = 0;
            mshrs[i].data.resize(line_size, 0);  /* Initialize/reset data buffer */
#ifdef DEBUG
            printf("[MSHR] Allocated MSHR %d: addr=0x%08x, alloc_tick=%u, completion_cycle=%u\n",
                   i, mshrs[i].address, mshrs[i].alloc_tick, mshrs[i].completion_cycle);
#endif
            return i;
        }
    }
    return -1;  /* No free MSHR */
}

void MSHRManager::free(int mshr_index) {
    if (mshr_index >= 0 && mshr_index < NUM_MSHRS) {
        mshrs[mshr_index].valid = false;
        mshrs[mshr_index].state = MSHR_IDLE;
    }
}

int MSHRManager::find_by_address(uint32_t address) {
    uint32_t line_addr = address & ~(line_size - 1);
    for (int i = 0; i < NUM_MSHRS; i++) {
        if (mshrs[i].valid && mshrs[i].address == line_addr) {
            return i;
        }
    }
    return -1;
}

MSHR* MSHRManager::get_mshr(int index) {
    if (index >= 0 && index < NUM_MSHRS && mshrs[index].valid) {
        return &mshrs[index];
    }
    return nullptr;
}

bool MSHRManager::has_free_mshr() {
    for (int i = 0; i < NUM_MSHRS; i++) {
        if (!mshrs[i].valid) {
            return true;
        }
    }
    return false;
}

bool MSHRManager::is_ready(int mshr_index) {
    if (mshr_index >= 0 && mshr_index < NUM_MSHRS && mshrs[mshr_index].valid) {
        return mshrs[mshr_index].state == MSHR_READY;
    }
    return false;
}

void MSHRManager::process_cycle() {
    /* Process MSHRs and update states only */
    /* Pipeline will check is_ready() on MSHRs it's tracking */
    
    for (int i = 0; i < NUM_MSHRS; i++) {
        if (!mshrs[i].valid) continue;
        
        MSHR& mshr = mshrs[i];
        
        switch (mshr.state) {
            case MSHR_WAITING_SEND:
                /* Check if 5 cycles have passed since allocation */
                if (stat_cycles >= mshr.alloc_tick + L2_TO_MEM_LATENCY) {
                    /* Send DRAM request */
                    mshr.dram_request_cycle = stat_cycles;
                    mshr.state = MSHR_WAITING_DRAM;
                    mshr.completion_cycle = stat_cycles + DRAM_LATENCY + MEM_TO_L2_LATENCY;
#ifdef DEBUG
                    printf("[MSHR] MSHR %d: WAITING_SEND -> WAITING_DRAM (cycle %u, will complete at %u)\n",
                           i, stat_cycles, mshr.completion_cycle);
#endif
                }
                break;
                
            case MSHR_WAITING_DRAM:
                /* Check if DRAM latency has passed */
                if (stat_cycles >= mshr.dram_request_cycle + DRAM_LATENCY) {
                    /* DRAM responded, now wait 5 cycles to fill L2 */
                    mshr.state = MSHR_WAITING_FILL;
                    mshr.completion_cycle = stat_cycles + MEM_TO_L2_LATENCY;
#ifdef DEBUG
                    printf("[MSHR] MSHR %d: WAITING_DRAM -> WAITING_FILL (cycle %u, will complete at %u)\n",
                           i, stat_cycles, mshr.completion_cycle);
#endif
                }
                break;
                
            case MSHR_WAITING_FILL:
                /* Check if fill latency has passed */
                if (stat_cycles >= mshr.completion_cycle) {
                    /* DRAM has delivered data - read from memory into MSHR buffer */
                    extern uint32_t mem_read_32(uint32_t addr);
                    uint32_t line_base = mshr.address;
                    for (uint32_t i = 0; i < line_size; i += 4) {
                        uint32_t word = mem_read_32(line_base + i);
                        mshr.data[i+3] = (word >> 24) & 0xFF;
                        mshr.data[i+2] = (word >> 16) & 0xFF;
                        mshr.data[i+1] = (word >>  8) & 0xFF;
                        mshr.data[i+0] = (word >>  0) & 0xFF;
                    }
                    /* Ready to fill L2 */
                    mshr.state = MSHR_READY;
#ifdef DEBUG
                    printf("[MSHR] MSHR %d: WAITING_FILL -> READY (cycle %u, addr=0x%08x)\n",
                           i, stat_cycles, mshr.address);
#endif
                }
                break;
                
            case MSHR_READY:
                /* Already ready - pipeline will handle it */
                break;
                
            case MSHR_IDLE:
                /* Should not happen */
                break;
        }
    }
}

