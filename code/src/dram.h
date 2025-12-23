#ifndef _DRAM_H_
#define _DRAM_H_

#include <stdint.h>
#include <vector>
#include <deque>
#include <map>

/* DRAM Timing Constants */
#define DRAM_CMD_BUS_OCCUPANCY 4
#define DRAM_BANK_BUSY_DELAY   100
#define DRAM_DATA_DELAY        100
#define DRAM_DATA_BUS_OCCUPANCY 50

/* DRAM Configuration */
#define DRAM_NUM_BANKS 8
#define DRAM_ROW_SIZE  8192

/* Forward Declaration */
class MSHRManager;

struct DRAM_Request {
    uint32_t req_id;
    uint32_t address;
    uint32_t entry_cycle;
    bool is_write;
    bool is_instruction_fetch; /* For priority Rule 3 (Mem > Fetch?) */
    bool scheduled;            /* Has this request been scheduled/issued? */
    uint32_t completion_cycle; /* Expected completion cycle (if scheduled) */
    
    /* Decoded Address Components */
    uint32_t row_index;
    uint32_t bank_index;
};

struct Bank {
    int32_t active_row;      /* Currently open row (-1 if closed) */
    uint32_t busy_until;     /* Cycle checking: Bank is busy until this cycle */
    
    Bank() : active_row(-1), busy_until(0) {}
};

class DRAM_Controller {
private:
    /* Components */
    Bank banks[DRAM_NUM_BANKS];
    std::deque<DRAM_Request> request_queue;
    
    /* Bus Reservation Tables (Scoreboarding) */
    /* Map: Cycle -> Is Busy? */
    /* We only need to track future busy cycles */
    std::map<uint64_t, bool> cmd_bus_reserved;
    std::map<uint64_t, bool> data_bus_reserved;

    /* Helper for Address Mapping */
    void decode_address(uint32_t address, uint32_t& row, uint32_t& bank);

    /* Scheduling Helpers */
    bool is_schedulable(DRAM_Request& req, uint32_t current_cycle);
    void schedule_request(DRAM_Request& req, uint32_t current_cycle);
    
    /* Resource Reservation Helper */
    bool check_bus_availability(std::map<uint64_t, bool>& bus, uint32_t start, uint32_t duration);
    void reserve_bus(std::map<uint64_t, bool>& bus, uint32_t start, uint32_t duration);

public:
    DRAM_Controller();
    
    /* Main Tick Function */
    void process_cycle(MSHRManager* mshr_manager);
    
    /* Interface for L2 Cache / MSHR */
    void enqueue_request(uint32_t address, bool is_write, bool is_inst_fetch);
    
    /* Data Interface (Gatekeeper for Coherency/Shared Memory) */
    /* Internally calls shell.cpp mem functions */
    uint32_t read_data(uint32_t address);
    void write_data(uint32_t address, uint32_t value);
};

#endif
