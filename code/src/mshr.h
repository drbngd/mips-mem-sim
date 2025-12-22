#ifndef _MSHR_H_
#define _MSHR_H_

#include "cache.h"
#include <cstdint>
#include <stdint.h>
#include <vector>

extern uint32_t stat_cycles;

/* MSHR State Machine */
enum MSHRState {
    MSHR_IDLE = 0,           /* Not allocated */
    MSHR_WAITING_SEND,       /* Waiting to send request to DRAM (5 cycles) */
    MSHR_WAITING_DRAM,       /* Request sent, waiting for DRAM response */
    MSHR_WAITING_FILL,       /* DRAM responded, waiting to fill L2 (5 cycles) */
    MSHR_READY               /* Ready to fill L1 and complete */
};

struct MSHR {
    bool valid;              /* Is this MSHR entry in use? */
    uint32_t address;        /* Line-aligned L2 address */
    MSHRState state;         /* Current state */
    uint32_t alloc_tick;     /* When allocated (for ordering) */
    uint32_t completion_cycle; /* When this MSHR will complete (absolute cycle) */
    uint32_t dram_request_cycle; /* When DRAM request was sent */
    std::vector<uint8_t> data;  /* Data buffer from memory (cache line size) */
};

class MSHRManager {
private:
    MSHR mshrs[NUM_MSHRS];
    uint32_t line_size;  /* Cache line size (L2-specific) */
    
public:
    MSHRManager(uint32_t line_size);
    ~MSHRManager();
    
    /* Allocate an MSHR for an L2 miss */
    /* Returns MSHR index, or -1 if no free MSHR */
    /* address: line-aligned L2 address */
    int allocate(uint32_t address);
    
    /* Free an MSHR */
    void free(int mshr_index);
    
    /* Find MSHR by address (for checking if already pending) */
    int find_by_address(uint32_t address);
    
    /* Process MSHRs each cycle - update states only */
    /* Process MSHRs each cycle - update states only */
    void process_cycle();
    
    /* Get MSHR */
    MSHR* get_mshr(int index);
    
    /* Check if any MSHR is free */
    bool has_free_mshr();
    
    /* Check if MSHR is ready to fill L2 */
    bool is_ready(int mshr_index);
};

#endif /* _MSHR_H_ */

