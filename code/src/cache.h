#ifndef _CACHE_H_
#define _CACHE_H_

#include "config.h"
#include "dram.h"
#include "mshr.h"
#include <memory>

/* Usage:
 * I-Cache: Sets=L1_I_SETS (8KB, 4-way, 32B)
 * D-Cache: Sets=L1_D_SETS (64KB, 8-way, 32B)
 * L2-Cache: Sets=L2_SETS (256KB, 16-way, 32B)
 */

enum MESI_State {
    INVALID = 0,
    SHARED = 1,
    EXCLUSIVE = 2,
    MODIFIED = 3
};

enum ReplacementPolicy {
    REPL_LRU = 0,
    REPL_RANDOM = 1,
    REPL_FIFO = 2,
    REPL_MRU = 3
};

enum InclusionPolicy {
    INCL_INCLUSIVE = 0,
    INCL_EXCLUSIVE = 1,
    INCL_NINE = 2 /* Non-Inclusive Non-Exclusive */
};

enum L2_Access_Status {
    L2_BUSY = 0,
    L2_HIT = 1,
    L2_MISS = 2
};

struct CacheBlock {
    uint32_t tag;
    MESI_State state;
    bool dirty;        /* Mostly for L2, but L1 uses state=MODIFIED */
    uint32_t lru_count; /* For LRU replacement */
    std::vector<uint8_t> data; /* Data storage */

    CacheBlock(uint32_t size = 32) : tag(0), state(INVALID), dirty(false), lru_count(0) {
        data.resize(size, 0);
    }
    
    /* Endian-Aware Accessors (Little Endian) */
    uint32_t read_word(uint32_t offset) const {
        if (offset + 4 > data.size()) return 0; // Guard
        return (uint32_t)data[offset] | 
               ((uint32_t)data[offset+1] << 8) | 
               ((uint32_t)data[offset+2] << 16) | 
               ((uint32_t)data[offset+3] << 24);
    }
    
    void write_word(uint32_t offset, uint32_t val) {
        if (offset + 4 > data.size()) return; // Guard
        data[offset]   = (val) & 0xFF;
        data[offset+1] = (val >> 8) & 0xFF;
        data[offset+2] = (val >> 16) & 0xFF;
        data[offset+3] = (val >> 24) & 0xFF;
    }
};

struct CacheSet {
    std::vector<CacheBlock> blocks;
    
    CacheSet(int ways, int block_size) {
        blocks.reserve(ways);
        for(int i=0; i<ways; i++) {
            blocks.emplace_back(block_size);
        }
    }
};

class Cache {
public:
    uint32_t num_sets; 
    uint32_t ways;
    uint32_t block_size;
    
    /* Bitwise fields */
    uint32_t index_mask;
    uint32_t index_shift; 
    uint32_t tag_shift;   

    ReplacementPolicy repl_policy; 
    
    std::vector<CacheSet> sets; 

    Cache(uint32_t s, uint32_t w, uint32_t b);
    virtual ~Cache() {}

    uint32_t get_index(uint32_t addr) const {
        return (addr >> index_shift) & index_mask;
    }

    uint32_t get_tag(uint32_t addr) const {
        return (addr >> tag_shift);
    }
    
    uint32_t get_block_offset(uint32_t addr) const {
        return addr & (block_size - 1);
    }

    /* Helper: Find block in a set. Returns way index or -1 */
    int find_block(uint32_t set_idx, uint32_t tag) const;
    
    /* Helper: Update LRU on access */
    void update_lru(uint32_t set_idx, int way);
    
    /* Helper: Find victim for eviction (LRU). Virtual for policy override. */
    virtual int find_victim(uint32_t set_idx) const;
    
    /* Core Methods */
    /* Returns Block* if hit, otherwise nullptr */
    CacheBlock* probe_read(uint32_t addr);
    
    /* Returns true if write hit, false if miss */
    bool probe_write(uint32_t addr, const uint8_t* data);
    
    /* Allocates a new block. Returns pointer to block. 
     * Handles eviction if necessary. 
     * out_evicted_addr/data are populated if a dirty block was evicted. */
    CacheBlock* install(uint32_t addr, const uint8_t* data, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data, bool writeback_clean = false);
    
    /* Explicit eviction helper. 
     * If writeback_clean is true, evicted_data is populated even if not dirty (for Victim Cache). 
     */
    virtual void evict(uint32_t set_idx, int way, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data, bool writeback_clean = false);
};

class L2Cache : public Cache {
public:
    InclusionPolicy incl_policy; // Configured via L2_INCL_POLICY
    std::vector<class L1Cache*> l1_refs; // Pointers to L1s for invalidation/snooping
    
    // MSHRs
    MSHR mshrs[L2_MSHR_SIZE];
    
    // DRAM Reference for Misses
    struct DRAM* dram_ref; // Forward decl

    // Delay Queues for Timing Specs
    struct Req_Queue_Item {
        bool is_write;
        uint32_t addr;
        int core_id;
        uint64_t ready_cycle;
    };
    std::vector<Req_Queue_Item> req_queue;

    struct Ret_Queue_Item {
        uint32_t addr;
        uint64_t ready_cycle;
    };
    std::vector<Ret_Queue_Item> ret_queue;

    L2Cache(struct DRAM* dram); 
    
    // Returns L2_RET_xxx status
    int access(uint32_t addr, bool is_write, int core_id);



    // Timing Cycle (processes queues)
    void cycle(uint64_t current_cycle, std::vector<std::unique_ptr<class Core>>& cores);

    // Handler for DRAM completion
    void handle_dram_completion(uint32_t addr);
    
    // MSHR Helpers
    int check_mshr(uint32_t addr);
    int allocate_mshr(uint32_t addr, bool is_write, int core_id);
    void complete_mshr(uint32_t addr, std::vector<std::unique_ptr<class Core>>& cores);
    
    // Writeback Helper
    void handle_l1_writeback(uint32_t addr, const std::vector<uint8_t>& data);

    // Override evict for Inclusive Policy
    void evict(uint32_t set_idx, int way, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data, bool writeback_clean = false) override;
};

class L1Cache : public Cache {
public:
    int id;
    L2Cache* l2_ref;
    
    // Blocking Logic for MESI (One MSHR)
    MSHR mshr;
    
    // Parent core pointer for snooping other L1s
    class Core* parent_core;

    L1Cache(int core_id, L2Cache* l2, class Core* core, uint32_t s, uint32_t w);
    
    // Returns true if hit/available. False if miss/pending.
    bool access(uint32_t addr, bool is_write, bool is_data_cache);
    
    // Called when L2 fills the request
    // target_state: State to install the block in (SHARED/EXCLUSIVE/MODIFIED)
    void fill(uint32_t addr, MESI_State target_state);

    // Invalidate a specific block (used by L2 for Inclusive Policy / Coherence)
    // Returns true if block was present and valid
    bool invalidate(uint32_t addr);
    
    // Coherence Snoop (used by other L1s)
    // Returns true if block was present (Shared/Exclusive/Modified).
    // is_write_req: If true, invalidates local copy (Write Invalidate).
    //               If false, downgrades to Shared (Read Miss).
    // is_modified: Output, set to true if block was in MODIFIED state.
    // data: Output, populated with dirty data if is_modified is true.
    bool probe_coherence(uint32_t addr, bool is_write_req, bool* is_modified, std::vector<uint8_t>* data);
};

#endif
