#include "dram.h"
#include "shell.h"
#include "mshr.h"
#include <limits>
#include <algorithm>
#include <stdio.h>

// #define DEBUG_DRAM

extern uint32_t stat_cycles;

DRAM_Controller::DRAM_Controller() {
    // Banks initialized by constructor
}

void DRAM_Controller::decode_address(uint32_t address, uint32_t& row, uint32_t& bank) {
    /* 
     * Spec: 
     * Bank Index: [7:5] -> 3 bits
     * Row Index: [31:16] -> 16 bits
     */
    bank = (address >> 5) & 0x7;
    row = (address >> 16) & 0xFFFF;
}

void DRAM_Controller::enqueue_request(uint32_t address, bool is_write, bool is_inst_fetch) {
    DRAM_Request req;
    req.address = address;
    req.is_write = is_write;
    req.is_instruction_fetch = is_inst_fetch;
    req.entry_cycle = stat_cycles;
    req.scheduled = false;
    req.completion_cycle = 0;
    
    /* Decode immediately */
    decode_address(address, req.row_index, req.bank_index);
    
    static uint32_t next_req_id = 0;
    req.req_id = next_req_id++;
    
    request_queue.push_back(req);
    
#ifdef DEBUG_DRAM
    printf("[DRAM %u] Enqueued Req %u: Addr=0x%08x (Bank %u, Row %u) %s\n", 
           stat_cycles, req.req_id, address, req.bank_index, req.row_index, 
           is_write ? "WR" : "RD");
#endif
}

bool DRAM_Controller::check_bus_availability(std::map<uint64_t, bool>& bus, uint32_t start, uint32_t duration) {
    for (uint32_t i = 0; i < duration; i++) {
        if (bus.count(start + i)) {
            return false; /* Only entries in map are busy cycles */
        }
    }
    return true;
}

void DRAM_Controller::reserve_bus(std::map<uint64_t, bool>& bus, uint32_t start, uint32_t duration) {
    for (uint32_t i = 0; i < duration; i++) {
        bus[start + i] = true;
    }
}

bool DRAM_Controller::is_schedulable(DRAM_Request& req, uint32_t current_cycle) {
    /* 1. Check Bank Availability */
    if (banks[req.bank_index].busy_until > current_cycle) {
        return false;
    }
    
    /* Determine Command Sequence needed */
    bool row_hit = (banks[req.bank_index].active_row == (int32_t)req.row_index);
    bool row_closed = (banks[req.bank_index].active_row == -1);
    
    /* 
     * Timing Windows Required:
     * CMD Bus: 4 cycles
     * DATA Bus: 50 cycles
     */
    
    if (row_hit) {
        /* Sequence: READ/WRITE (CMD) -> ... -> DATA */
        /* CMD needed at T */
        if (!check_bus_availability(cmd_bus_reserved, current_cycle, DRAM_CMD_BUS_OCCUPANCY)) 
            return false;
            
        /* DATA needed at T + DATA_DELAY */
        if (!check_bus_availability(data_bus_reserved, current_cycle + DRAM_DATA_DELAY, DRAM_DATA_BUS_OCCUPANCY))
            return false;
            
    } else if (row_closed) {
        /* Sequence: ACT (CMD) ... -> RD/WR (CMD) ... -> DATA */
        /* ACT needed at T */
        if (!check_bus_availability(cmd_bus_reserved, current_cycle, DRAM_CMD_BUS_OCCUPANCY))
            return false;
            
        /* RD/WR needed at T + BANK_BUSY_DELAY */
        /* Note: Bank is busy after ACT, but we can issue next CMD exactly when busy ends */
        if (!check_bus_availability(cmd_bus_reserved, current_cycle + DRAM_BANK_BUSY_DELAY, DRAM_CMD_BUS_OCCUPANCY))
            return false;
            
        /* DATA needed at (T + BANK_BUSY_DELAY) + DATA_DELAY */
        if (!check_bus_availability(data_bus_reserved, current_cycle + DRAM_BANK_BUSY_DELAY + DRAM_DATA_DELAY, DRAM_DATA_BUS_OCCUPANCY))
            return false;

    } else { /* Row Conflict */
        /* Sequence: PRE (CMD) ... -> ACT (CMD) ... -> RD/WR (CMD) ... -> DATA */
        
        /* PRE needed at T */
        if (!check_bus_availability(cmd_bus_reserved, current_cycle, DRAM_CMD_BUS_OCCUPANCY))
            return false;
            
        /* ACT needed at T + BANK_BUSY_DELAY */
        if (!check_bus_availability(cmd_bus_reserved, current_cycle + DRAM_BANK_BUSY_DELAY, DRAM_CMD_BUS_OCCUPANCY))
            return false;
            
        /* RD/WR needed at T + 2*BANK_BUSY_DELAY */
        if (!check_bus_availability(cmd_bus_reserved, current_cycle + 2*DRAM_BANK_BUSY_DELAY, DRAM_CMD_BUS_OCCUPANCY))
            return false;
            
        /* DATA needed at (T + 2*BANK_BUSY_DELAY) + DATA_DELAY */
        if (!check_bus_availability(data_bus_reserved, current_cycle + 2*DRAM_BANK_BUSY_DELAY + DRAM_DATA_DELAY, DRAM_DATA_BUS_OCCUPANCY))
            return false;
    }
    
    return true;
}

void DRAM_Controller::schedule_request(DRAM_Request& req, uint32_t current_cycle) {
    bool row_hit = (banks[req.bank_index].active_row == (int32_t)req.row_index);
    bool row_closed = (banks[req.bank_index].active_row == -1);
    
    uint32_t data_start_cycle = 0;
    
    if (row_hit) {
        /* CMD: READ/WRITE at T */
        reserve_bus(cmd_bus_reserved, current_cycle, DRAM_CMD_BUS_OCCUPANCY);
        
        /* Bank Busy: T + 100 */
        banks[req.bank_index].busy_until = current_cycle + DRAM_BANK_BUSY_DELAY;
        
        /* DATA */
        data_start_cycle = current_cycle + DRAM_DATA_DELAY;
        
    } else if (row_closed) {
        /* CMD: ACT at T */
        reserve_bus(cmd_bus_reserved, current_cycle, DRAM_CMD_BUS_OCCUPANCY);
        
        /* CMD: RD/WR at T + 100 */
        reserve_bus(cmd_bus_reserved, current_cycle + DRAM_BANK_BUSY_DELAY, DRAM_CMD_BUS_OCCUPANCY);
        
        /* Update Row State */
        banks[req.bank_index].active_row = req.row_index;
        
        /* Bank Busy: T + 100 (ACT) + 100 (RD/WR) = T + 200 */
        banks[req.bank_index].busy_until = current_cycle + 2*DRAM_BANK_BUSY_DELAY;
        
        /* DATA */
        data_start_cycle = current_cycle + DRAM_BANK_BUSY_DELAY + DRAM_DATA_DELAY;

    } else { /* Row Conflict */
        /* CMD: PRE at T */
        reserve_bus(cmd_bus_reserved, current_cycle, DRAM_CMD_BUS_OCCUPANCY);
        
        /* CMD: ACT at T + 100 */
        reserve_bus(cmd_bus_reserved, current_cycle + DRAM_BANK_BUSY_DELAY, DRAM_CMD_BUS_OCCUPANCY);

        /* CMD: RD/WR at T + 200 */
        reserve_bus(cmd_bus_reserved, current_cycle + 2*DRAM_BANK_BUSY_DELAY, DRAM_CMD_BUS_OCCUPANCY);
        
        /* Update Row State */
        banks[req.bank_index].active_row = req.row_index; // Effectively opens new row after PRE
        
        /* Bank Busy: T + 100(PRE) + 100(ACT) + 100(RD/WR) = T + 300 */
        banks[req.bank_index].busy_until = current_cycle + 3*DRAM_BANK_BUSY_DELAY;
        
        /* DATA */
        data_start_cycle = current_cycle + 2*DRAM_BANK_BUSY_DELAY + DRAM_DATA_DELAY;
    }
    
    /* Reserve Data Bus */
    reserve_bus(data_bus_reserved, data_start_cycle, DRAM_DATA_BUS_OCCUPANCY);
    
    /* Set Completion */
    req.scheduled = true;
    /* Request completes when Data Bus usage finishes */
    req.completion_cycle = data_start_cycle + DRAM_DATA_BUS_OCCUPANCY;
    
#ifdef DEBUG_DRAM
    printf("[DRAM %u] Scheduled Req %u: Bank %u Row %u -> Comp Cycle %u\n", 
           stat_cycles, req.req_id, req.bank_index, req.row_index, req.completion_cycle);
#endif
}

void DRAM_Controller::process_cycle(MSHRManager* mshr_manager) {
    /* 1. Complete Finished requests */
    /* Deque allows unsafe removal, but we usually remove from head. 
       Careful: In FR-FCFS, strict FIFO (head) removal isn't guaranteed. 
       We need to iterate and remove completed. */
       
    for (auto it = request_queue.begin(); it != request_queue.end(); ) {
        if (it->scheduled && it->completion_cycle <= stat_cycles) {
            /* Notify MSHR */
            /* MSHR Manager will handle L2 fill latency from here */
            if (mshr_manager) {
                mshr_manager->dram_complete(it->address);
            }
            
#ifdef DEBUG_DRAM
            printf("[DRAM %u] Completed Req %u: Addr=0x%08x\n", stat_cycles, it->req_id, it->address);
#endif
            it = request_queue.erase(it); /* Remove from queue */
        } else {
            ++it;
        }
    }
    
    /* 2. Schedule New Requests (FR-FCFS) */
    /* Only 1 request can initiate commands per cycle (Controller Limit? Specs imply controller scans queue)
       We should issue ONE Request that is "Schedulable" this cycle.
       Why One? Because CMD bus is single resource. If we schedule multiple, they would clash on T=0. 
       The Reservation system handles future clashes, but typically controller picks Best Candidate per cycle. */
       
    DRAM_Request* best_req = nullptr;
    
    for (auto& req : request_queue) {
        if (req.scheduled) continue; /* Already inflight */
        
        if (is_schedulable(req, stat_cycles)) {
            if (!best_req) {
                best_req = &req;
            } else {
                /* Compare Priority:
                   1. Row Buffer Hit 
                   2. Arrived Earlier (req_id)
                   3. Memory Stage > Fetch Stage
                */
                
                bool best_hit = (banks[best_req->bank_index].active_row == (int32_t)best_req->row_index);
                bool cur_hit = (banks[req.bank_index].active_row == (int32_t)req.row_index);
                
                if (cur_hit && !best_hit) {
                    best_req = &req; continue;
                } else if (best_hit && !cur_hit) {
                    continue;
                }
                
                /* Tie on Row Hit status: Check Arrival Time (Req ID lower is older) */
                if (req.entry_cycle < best_req->entry_cycle) {
                     // Should be picked by loop order usually, but let's be safe
                }
                
                // If same cycle (rare), check rule 3
                if (req.entry_cycle == best_req->entry_cycle) {
                    if (req.is_instruction_fetch == false && best_req->is_instruction_fetch == true) {
                         best_req = &req; // Mem (Not Fetch) > Fetch
                    }
                }
            }
        }
    }
    
    if (best_req) {
        schedule_request(*best_req, stat_cycles);
    }
    
    /* Garbage Collect Reservation Maps */
    /* Remove keys < stat_cycles to keep map small */
    /* Optional optimization */
    if (stat_cycles % 1000 == 0) {
        if (stat_cycles % 10000 == 0) printf("Cycle %u\n", stat_cycles);

        auto it = cmd_bus_reserved.begin();
        while(it != cmd_bus_reserved.end() && it->first < stat_cycles) it = cmd_bus_reserved.erase(it);
        
        auto it2 = data_bus_reserved.begin();
        while(it2 != data_bus_reserved.end() && it2->first < stat_cycles) it2 = data_bus_reserved.erase(it2);
    }
}

uint32_t DRAM_Controller::read_data(uint32_t address) {
    return mem_read_32(address);
}

void DRAM_Controller::write_data(uint32_t address, uint32_t value) {
    mem_write_32(address, value);
}
