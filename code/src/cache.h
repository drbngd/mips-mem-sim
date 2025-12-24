#ifndef _CACHE_H_
#define _CACHE_H_

#include "config.h"
#include "dram.h"
#include "mshr.h"

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
    CacheBlock* install(uint32_t addr, const uint8_t* data, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data);
    
    /* Explicit eviction helper */
    void evict(uint32_t set_idx, int way, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data);
};

class L2Cache : public Cache {
public:
    InclusionPolicy incl_policy; 
    // MSHRs
    MSHR mshrs[L2_MSHR_SIZE];
    
    // DRAM Reference for Misses
    struct DRAM* dram_ref; // Forward decl

    L2Cache(struct DRAM* dram); 
    
    // Returns true if hit (or handled), false if blocked/miss-stall
    bool access(uint32_t addr, bool is_write, int core_id);
    
    // MSHR Helpers
    int check_mshr(uint32_t addr);
    int allocate_mshr(uint32_t addr, bool is_write);
    void complete_mshr(uint32_t addr);
};

class L1Cache : public Cache {
public:
    int id;
    L2Cache* l2_ref;
    
    // Blocking Logic for Phase 3
    bool pending_miss;
    uint32_t pending_miss_addr;

    L1Cache(int core_id, L2Cache* l2, uint32_t s, uint32_t w);
    
    // Returns true if hit/available. False if miss/pending.
    // Logic: 
    // - Check Tag. If Hit -> True.
    // - If Miss -> Issue request to L2 (if not already pending). Return False.
    // - If L2 busy -> Return False (Retry).
    bool access(uint32_t addr, bool is_write, bool is_data_cache);
    
    // Called when L2 fills the request
    void fill(uint32_t addr);
};

#endif
