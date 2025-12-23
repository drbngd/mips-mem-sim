#include "mshr.h"
#include "cache.h"
#include "dram.h"
#include <stdio.h>
#include <vector>

// #define DEBUG

MSHRManager::MSHRManager(uint32_t line_size) : line_size(line_size), dram_ptr(nullptr) {
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

void MSHRManager::set_dram_controller(DRAM_Controller* dram) {
    this->dram_ptr = dram;
}

int MSHRManager::allocate(uint32_t address, bool is_write, bool is_inst_fetch) {
    /* Find first free MSHR */
    /* address should already be line-aligned */
    for (int i = 0; i < NUM_MSHRS; i++) {
        if (!mshrs[i].valid) {
            mshrs[i].valid = true;
            mshrs[i].address = address & ~(line_size - 1);  /* Line-align */
            mshrs[i].state = MSHR_WAITING_SEND;
            mshrs[i].alloc_tick = stat_cycles;
            mshrs[i].completion_cycle = stat_cycles + L2_TO_MEM_LATENCY;
            mshrs[i].is_write = is_write;
            mshrs[i].is_inst_fetch = is_inst_fetch;
            
            mshrs[i].dram_request_cycle = 0;
            mshrs[i].data.resize(line_size, 0);  /* Initialize/reset data buffer */
#ifdef DEBUG
            printf("[MSHR %d] Allocated: addr=0x%08x, alloc=%u, is_write=%d\n",
                   i, mshrs[i].address, mshrs[i].alloc_tick, is_write);
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

void MSHRManager::dram_complete(uint32_t address) {
    /* DRAM has finished the request for this address */
    /* Find the matching MSHR */
    
    /* NOTE: We might have multiple MSHRs for the same address if we supported MSHR Coalescing 
       properly, but the allocate() function blindly allocates new ones (bug/limitation).
       OR the pipeline might issue multiple requests. 
       We should find ALL MSHRs waiting for DRAM for this address. */
       
    for (int i = 0; i < NUM_MSHRS; i++) {
        if (mshrs[i].valid && mshrs[i].address == address && mshrs[i].state == MSHR_WAITING_DRAM) {
            /* Transition to Filling L2 */
            mshrs[i].state = MSHR_WAITING_FILL;
            mshrs[i].completion_cycle = stat_cycles + MEM_TO_L2_LATENCY;
            
#ifdef DEBUG
            printf("[MSHR %d] DRAM Callback for 0x%08x -> Filling L2 (Done at %u)\n", 
                    i, address, mshrs[i].completion_cycle);
#endif
        }
    }
}

void MSHRManager::process_cycle() {
    for (int i = 0; i < NUM_MSHRS; i++) {
        if (!mshrs[i].valid) continue;
        
        MSHR& mshr = mshrs[i];
        
        switch (mshr.state) {
            case MSHR_WAITING_SEND:
                /* Check if L2->Mem delay is done */
                if (stat_cycles >= mshr.completion_cycle) {
                    /* Send to DRAM Controller */
                    if (dram_ptr) {
                        dram_ptr->enqueue_request(mshr.address, mshr.is_write, mshr.is_inst_fetch);
                        mshr.state = MSHR_WAITING_DRAM;
#ifdef DEBUG
                        printf("[MSHR %d] Sent 0x%08x to DRAM\n", i, mshr.address);
#endif
                    } else {
                        /* Start fallback fixed latency if no DRAM (legacy support/unit test) */
                         mshr.state = MSHR_WAITING_DRAM; // Or Error
                    }
                }
                break;
                
            case MSHR_WAITING_DRAM:
                /* Waiting for callback via dram_complete() */
                /* Do nothing here */
                break;
                
            case MSHR_WAITING_FILL:
                /* Check if fill latency has passed */
                if (stat_cycles >= mshr.completion_cycle) {
                    
                    /* Fetch Data from "Memory" (Gatekeeper) */
                    /* Since DRAM Controller is the gatekeeper now, we should ask IT for data
                       or just use the global mem_read since DRAM Controller allows that architectural bypass logic. */
                    extern uint32_t mem_read_32(uint32_t addr);
                    uint32_t line_base = mshr.address;
                    for (uint32_t k = 0; k < line_size; k += 4) {
                        /* Read directly from Shell memory as planned (Timing Model only) */
                        uint32_t word = mem_read_32(line_base + k);
                        mshr.data[k+3] = (word >> 24) & 0xFF;
                        mshr.data[k+2] = (word >> 16) & 0xFF;
                        mshr.data[k+1] = (word >>  8) & 0xFF;
                        mshr.data[k+0] = (word >>  0) & 0xFF;
                    }
                    
                    mshr.state = MSHR_READY;
                }
                break;
                
            case MSHR_READY:
                break;
                
            case MSHR_IDLE:
                break;
        }
    }
}
