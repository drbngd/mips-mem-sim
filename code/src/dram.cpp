#include "dram.h"
#include <cmath>
// algorithm, iostream, optional removed as unused (manual loop implemented)

DRAM::DRAM() : cmd_bus_avail_cycle(0), data_bus_avail_cycle(0) {
    // Banks initialized by default
}

DRAM::AddressMapping DRAM::decode(uint32_t addr) const {
    /*
     * Spec:
     * Bank = [7:5] (3 bits)
     * Row = [31:16] (16 bits)
     * Implicitly:
     * Offset = [4:0] (32 bytes)
     * Column/Rest = [15:8] (8 bits)
     * Channel/Rank = 0 (Single Rank/Channel)
     */
    
    // 1. Offset [4:0]
    // uint32_t offset = addr & 0x1F;
    
    // 2. Bank [7:5]
    uint32_t bank = (addr >> 5) & 0x7;
    
    // 3. Row [31:16]
    uint32_t row = (addr >> 16) & 0xFFFF;
    
    return {0, 0, bank, row}; // Channel 0, Rank 0
}

uint32_t DRAM::get_flat_bank_id(uint32_t addr) const {
    // Single Rank, Single Channel -> Just Bank Index
    AddressMapping m = decode(addr);
    return m.bank;
}

bool DRAM::enqueue(bool is_write, uint32_t addr, int core_id, DRAM_Req::Source src, uint64_t cycle) {
    uint32_t bank_id = get_flat_bank_id(addr);
    AddressMapping mapping = decode(addr);
    
    DRAM_Req req;
    req.valid = true;
    req.ready = false;
    req.addr = addr;
    req.is_write = is_write;
    req.core_id = core_id;
    req.arrival_cycle = cycle; // Track arrival for Priority Rule 2
    req.completion_cycle = 0;
    
    req.bank_id = bank_id;
    req.row_index = mapping.row;
    req.source = src;
    
    active_requests.push_back(req); 
#ifdef DEBUG
    printf("[DRAM] Enqueued Req %08x (Bank %d Row %d)\n", addr, bank_id, mapping.row);
#endif
    return true;
}


DRAM_Req DRAM::execute(uint64_t current_cycle) {
    /* 
     * 1. Check for Completions 
     */
    for (auto it = active_requests.begin(); it != active_requests.end(); ) {
        if (it->ready && it->completion_cycle <= current_cycle) {
            DRAM_Req completed = *it;
            it = active_requests.erase(it);
            return completed; // Return one completion per cycle max
        } else {
            ++it;
        }
    }
    
    /* 
     * 2. Schedule Requests (FR-FCFS with Spec Constraints)
     * Constraints:
     * - Cmd Bus Free: T to T+3
     * - Bank Free: T to T+99
     * - Data Bus Free: T_data to T_data+49
     */
    
    // Check if Command Bus is free NOW
    if (current_cycle < cmd_bus_avail_cycle) {
        return DRAM_Req(); // Invalid, Command bus busy
    }

    int best_cand_idx = -1;
    bool best_is_row_hit = false;
    
    for (size_t i = 0; i < active_requests.size(); i++) {
        if (active_requests[i].ready) continue; // Already scheduled
        
        DRAM_Req& req = active_requests[i];
        Bank& bank = banks[req.bank_id];
        
        /* 
         * Determine Latencies & Resource Checks 
         */
        bool row_hit = (bank.active && bank.active_row == req.row_index);
        bool row_conflict = (bank.active && bank.active_row != req.row_index);
        
        uint64_t cmd_latency = 0; // Latency until Data Transfer Starts
        uint64_t data_start_offset = 0;
        
        /* Check Bank Availability for Initial Command */
        if (current_cycle < bank.bank_busy_until) {
#ifdef DEBUG
            if (active_requests.size() < 5) printf("[DRAM] Skip %08x: Bank %d Busy until %llu (Curr %llu)\n", req.addr, req.bank_id, bank.bank_busy_until, current_cycle);
#endif
            continue; 
        }

        bool is_open_policy = (DRAM_PAGE_POLICY == 0);

        if (is_open_policy) {
            /* Open Row Policy */
            if (row_hit) {
                // READ/WRITE
                data_start_offset = DRAM_RDWR_CMD_BUS_BUSY_CYCLES + DRAM_RDWR_BANK_BUSY_CYCLES;
            } else if (row_conflict) {
                 // PRE(cmd) + ACT(cmd) + READ(cmd+bank)
                 data_start_offset = DRAM_PRE_CMD_BUS_BUSY_CYCLES + 
                                     DRAM_ACT_CMD_BUS_BUSY_CYCLES + 
                                     DRAM_RDWR_CMD_BUS_BUSY_CYCLES + DRAM_RDWR_BANK_BUSY_CYCLES;
            } else {
                 // ACT(cmd) + READ(cmd+bank)
                 data_start_offset = DRAM_ACT_CMD_BUS_BUSY_CYCLES + 
                                     DRAM_RDWR_CMD_BUS_BUSY_CYCLES + DRAM_RDWR_BANK_BUSY_CYCLES;
            }
        } else {
            /* Closed Row Policy */
            if (bank.active) {
                // Conflict
                 data_start_offset = DRAM_PRE_CMD_BUS_BUSY_CYCLES + 
                                     DRAM_ACT_CMD_BUS_BUSY_CYCLES + 
                                     DRAM_RDWR_CMD_BUS_BUSY_CYCLES + DRAM_RDWR_BANK_BUSY_CYCLES;
            } else {
                // ACT + READ
                data_start_offset = DRAM_ACT_CMD_BUS_BUSY_CYCLES + 
                                    DRAM_RDWR_CMD_BUS_BUSY_CYCLES + DRAM_RDWR_BANK_BUSY_CYCLES;
            }
        }

        // Check Data Bus Availability
        uint64_t data_start_abs = current_cycle + data_start_offset;
        if (data_start_abs < data_bus_avail_cycle) {
#ifdef DEBUG
            if (active_requests.size() < 5) printf("[DRAM] Skip %08x: Data Bus Busy (Start %llu < Avail %llu)\n", req.addr, data_start_abs, data_bus_avail_cycle);
#endif
            continue; // Collision on Data Bus
        }

        /* Priority Logic */
        // 1. Row Hit (Only applies if Open Policy)
        // 2. Oldest (Index i)
        // 3. Source (Memory > Fetch)
        
        if (best_cand_idx == -1) {
            best_cand_idx = i;
            best_is_row_hit = row_hit;
        } else {
            DRAM_Req& best_req = active_requests[best_cand_idx];

            // Priority Check
            if (is_open_policy && row_hit != best_is_row_hit) {
                if (row_hit) {
                     best_cand_idx = i; best_is_row_hit = true;
                }
            } 
            else if (row_hit == best_is_row_hit) {
                // Rule 1 Tie.
                // Rule 2: Arrival Time.
                if (req.arrival_cycle < best_req.arrival_cycle) {
                    best_cand_idx = i;
                    best_is_row_hit = row_hit;
                } else if (req.arrival_cycle == best_req.arrival_cycle) {
                    // Rule 2 Tie.
                    // Rule 3: Source (Memory > Fetch)
                    if (req.source == DRAM_Req::SRC_MEMORY && best_req.source == DRAM_Req::SRC_FETCH) {
                        best_cand_idx = i;
                        best_is_row_hit = row_hit;
                    }
                }
            } 
        }
    }
    
    if (best_cand_idx != -1) {
        // Schedule Best Candidate
        DRAM_Req& req = active_requests[best_cand_idx];
        Bank& bank = banks[req.bank_id];
        bool is_open_policy = (DRAM_PAGE_POLICY == 0);
        
        bool row_hit = (bank.active && bank.active_row == req.row_index);
        bool row_conflict = (bank.active && bank.active_row != req.row_index);
        
        // Update Command Bus
        uint64_t initial_cmd_cycles = 0;
        if (is_open_policy) {
            if (row_hit) initial_cmd_cycles = DRAM_RDWR_CMD_BUS_BUSY_CYCLES;
            else if (row_conflict) initial_cmd_cycles = DRAM_PRE_CMD_BUS_BUSY_CYCLES;
            else initial_cmd_cycles = DRAM_ACT_CMD_BUS_BUSY_CYCLES;
        } else {
            if (bank.active) initial_cmd_cycles = DRAM_PRE_CMD_BUS_BUSY_CYCLES;
            else initial_cmd_cycles = DRAM_ACT_CMD_BUS_BUSY_CYCLES;
        }
        cmd_bus_avail_cycle = current_cycle + initial_cmd_cycles; 

        uint64_t latency = 0;
        
        // Helper to sum latencies
        uint64_t hit_latency = DRAM_RDWR_CMD_BUS_BUSY_CYCLES + DRAM_RDWR_BANK_BUSY_CYCLES;
        uint64_t act_hit_latency = DRAM_ACT_CMD_BUS_BUSY_CYCLES + hit_latency;
        uint64_t conflict_latency = DRAM_PRE_CMD_BUS_BUSY_CYCLES + act_hit_latency;
        
        if (is_open_policy) {
            if (row_hit) {
                bank.bank_busy_until = current_cycle + hit_latency;
                latency = hit_latency + DRAM_RDWR_DATA_BUS_BUSY_CYCLES; 
                data_bus_avail_cycle = current_cycle + latency;
            } else if (row_conflict) {
                 bank.bank_busy_until = current_cycle + conflict_latency; 
                 bank.active_row = req.row_index;
                 latency = conflict_latency + DRAM_RDWR_DATA_BUS_BUSY_CYCLES;
                 data_bus_avail_cycle = current_cycle + latency;
            } else {
                 bank.bank_busy_until = current_cycle + act_hit_latency;
                 bank.active = true;
                 bank.active_row = req.row_index;
                 latency = act_hit_latency + DRAM_RDWR_DATA_BUS_BUSY_CYCLES;
                 data_bus_avail_cycle = current_cycle + latency;
            }
        } else {
            /* Closed Row Policy Execution */
            
            // Case 1: Bank somehow active (Conflict/Bug recovery)
            if (bank.active) {
                 // PRE(cmd) + ACT(cmd) + READ(cmd+busy) + PRE(cmd+busy?)
                 // Does implicit close PRE have a generic 100 busy? The user deleted PRE_BANK_BUSY_CYCLES.
                 // So implicit close is just command overhead? PRE is instant bank release?
                 // Or is it just the bus overhead?
                 // If PRE_BANK_BUSY is gone, then PRE is 4 cycles.
                 uint64_t close_overhead = DRAM_PRE_CMD_BUS_BUSY_CYCLES; 
                 bank.bank_busy_until = current_cycle + conflict_latency + close_overhead;
                 bank.active = false; 
                 latency = conflict_latency + DRAM_RDWR_DATA_BUS_BUSY_CYCLES;
                 data_bus_avail_cycle = current_cycle + latency; 
            } else {
                 // ACT + READ(cmd+busy) + PRE(cmd)
                 uint64_t close_overhead = DRAM_PRE_CMD_BUS_BUSY_CYCLES;
                 bank.bank_busy_until = current_cycle + act_hit_latency + close_overhead;
                 bank.active = false; 
                 bank.active_row = req.row_index; 
                 
                 latency = act_hit_latency + DRAM_RDWR_DATA_BUS_BUSY_CYCLES;
                 data_bus_avail_cycle = current_cycle + latency;
            }
        }
        
        req.ready = true;
        req.completion_cycle = current_cycle + latency;
    }
    
    return DRAM_Req(); // Invalid (nothing completed)
}
