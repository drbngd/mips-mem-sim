#ifndef _DRAM_H_
#define _DRAM_H_

#include <cstdint>
#include <vector>
#include <deque>
#include <optional>
#include "config.h"

struct DRAM_Req {
    bool valid;
    bool ready; // Scheduled and waiting for completion
    uint32_t addr;
    bool is_write;
    int core_id;
    uint64_t arrival_cycle;
    uint64_t completion_cycle;
    
    // Decoded info for scheduling
    uint32_t bank_id;
    uint32_t row_index;
    
    // Source for Priority 3
    enum Source { SRC_FETCH, SRC_MEMORY };
    Source source; 
    
    DRAM_Req() : valid(false), ready(false), addr(0), is_write(false), core_id(0), arrival_cycle(0), completion_cycle(0), bank_id(0), row_index(0), source(SRC_FETCH) {}
};

struct Bank {
    bool active;
    uint32_t active_row;
    uint64_t bank_busy_until; // Spec: Bank busy for 100 cycles after command
    
    Bank() : active(false), active_row(0), bank_busy_until(0) {}
};

class DRAM {
public:
    Bank banks[TOTAL_BANKS];
    
    // In-flight requests (serving as both queue and active list for this skeleton)
    std::vector<DRAM_Req> active_requests;
    
    /* Bus Tracking */
    /* We need to track when buses will be free to schedule future commands */
    uint64_t cmd_bus_avail_cycle; // When command bus is free next
    uint64_t data_bus_avail_cycle; // When data bus is free next
    
    /* Decoded Address Components */
    struct AddressMapping {
        uint32_t channel;
        uint32_t rank;
        uint32_t bank;
        uint32_t row;
    };
    
    DRAM();
    
    /* Decode helper */
    AddressMapping decode(uint32_t addr) const;
    
    /* Get flattened bank index */
    uint32_t get_flat_bank_id(uint32_t addr) const;

    // Enqueue a request
    bool enqueue(bool is_write, uint32_t addr, int core_id, DRAM_Req::Source src, uint64_t cycle);

    // Execute the DRAM controller (formerly tick). Returns the completed request (valid=true if done).
    DRAM_Req execute(uint64_t current_cycle);
};
#endif
