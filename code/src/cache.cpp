#include "cache.h"
#include "config.h"
#include "core.h" // Needed for Core def
#include <cstring>
#include <cmath>

extern uint32_t stat_cycles; // From shell.cpp


/* Base Cache Methods */

Cache::Cache(uint32_t s, uint32_t w, uint32_t b) 
    : num_sets(s), ways(w), block_size(b), repl_policy((ReplacementPolicy)CACHE_REPL_POLICY) 
{
    sets.reserve(num_sets);
    for (uint32_t i = 0; i < num_sets; i++) {
        sets.emplace_back(ways, block_size);
    }

    /* Calculate bitwise fields assuming power of 2 */
    // index_shift = log2(block_size)
    index_shift = (uint32_t)std::log2(block_size);
    
    // index_mask = num_sets - 1
    index_mask = num_sets - 1;
    
    // tag_shift = index_shift + log2(num_sets)
    tag_shift = index_shift + (uint32_t)std::log2(num_sets);
}

int Cache::find_block(uint32_t set_idx, uint32_t tag) const {
    const auto& set = sets[set_idx];
    for (int i = 0; i < ways; i++) {
        if (set.blocks[i].tag == tag && set.blocks[i].state != INVALID) {
            return i;
        }
    }
    return -1;
}

void Cache::update_lru(uint32_t set_idx, int way) {
    auto& set = sets[set_idx];
    uint32_t current_lru = set.blocks[way].lru_count;
    
    for (int i = 0; i < ways; i++) {
        if (i != way && set.blocks[i].state != INVALID) {
            if (set.blocks[i].lru_count < current_lru) {
                set.blocks[i].lru_count++; 
            }
        }
    }
    set.blocks[way].lru_count = 0;
}

int Cache::find_victim(uint32_t set_idx) const {
    const auto& set = sets[set_idx];
    
    // First, look for INVALID block
    for (int i = 0; i < ways; i++) {
        if (set.blocks[i].state == INVALID) return i;
    }

    // Else find LRU (highest count)
    int victim = -1;
    uint32_t max_lru = 0;
    
    for (int i = 0; i < ways; i++) {
        if (set.blocks[i].lru_count >= max_lru) {
            max_lru = set.blocks[i].lru_count;
            victim = i;
        }
    }
    return victim;
}

CacheBlock* Cache::probe_read(uint32_t addr) {
    uint32_t set_idx = get_index(addr);
    uint32_t tag = get_tag(addr);
    
    int way = find_block(set_idx, tag);
    if (way != -1) {
        update_lru(set_idx, way);
        return &sets[set_idx].blocks[way];
    }
    return nullptr;
}

bool Cache::probe_write(uint32_t addr, const uint8_t* data) {
    uint32_t set_idx = get_index(addr);
    uint32_t tag = get_tag(addr);
    
    int way = find_block(set_idx, tag);
    if (way != -1) {
        update_lru(set_idx, way);
        CacheBlock& block = sets[set_idx].blocks[way];
        block.dirty = true;
        // In simple simulation, we might not always have full data payload. 
        // If data is provided, copy it.
        if (data) {
             std::memcpy(block.data.data(), data, block_size);
        }
        return true;
    }
    return false;
}

void Cache::evict(uint32_t set_idx, int way, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data) {
    CacheBlock& block = sets[set_idx].blocks[way];
    
    if (block.state != INVALID && block.dirty) {
        if (dirty_evicted) *dirty_evicted = true;
        
        // Reconstruct address: (Tag << tag_shift) | (Set << index_shift)
        // Note: Offset is 0 for block address
        if (evicted_addr) {
            *evicted_addr = (block.tag << tag_shift) | (set_idx << index_shift);
        }
        if (evicted_data) {
            *evicted_data = block.data; // Copy data
        }
    } else {
        if (dirty_evicted) *dirty_evicted = false;
    }
    
    // Invalidate
    block.state = INVALID; 
    block.dirty = false;
}

CacheBlock* Cache::install(uint32_t addr, const uint8_t* data, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data) {
    uint32_t set_idx = get_index(addr);
    uint32_t tag = get_tag(addr);
    
    int way = find_victim(set_idx);
    
    // Handle Eviction
    evict(set_idx, way, dirty_evicted, evicted_addr, evicted_data);
    
    // Install new block
    CacheBlock& block = sets[set_idx].blocks[way];
    block.tag = tag;
    block.state = EXCLUSIVE; // Default for new block (or SHARED depending on coherence - fix later)
    block.dirty = false;
    block.lru_count = 0; // MRU
    
    if (data) {
        std::memcpy(block.data.data(), data, block_size);
    }
    
    // Update LRU for others
    update_lru(set_idx, way); 
    
    return &block;
}

/* L2 Cache Methods */

L2Cache::L2Cache(struct DRAM* dram) : Cache(L2_SETS, L2_ASSOC, BLOCK_SIZE), incl_policy((InclusionPolicy)L2_INCL_POLICY), dram_ref(dram) {
    // Parent constructor handles initialization
}

int L2Cache::check_mshr(uint32_t addr) {
    uint32_t block_addr = addr & ~(block_size - 1);
    for (int i = 0; i < L2_MSHR_SIZE; i++) {
        if (mshrs[i].valid && mshrs[i].address == block_addr) {
            return i;
        }
    }
    return -1;
}

int L2Cache::allocate_mshr(uint32_t addr, bool is_write, int core_id) {
    uint32_t block_addr = addr & ~(block_size - 1);
    for (int i = 0; i < L2_MSHR_SIZE; i++) {
        if (!mshrs[i].valid) {
            mshrs[i].valid = true;
            mshrs[i].address = block_addr;
            mshrs[i].is_write = is_write;
            mshrs[i].core_id = core_id; // Store requester
            mshrs[i].done = false;
            mshrs[i].ready_cycle = 0;
            return i;
        }
    }
    return -1; // Full
}

int L2Cache::access(uint32_t addr, bool is_write, int core_id) {
    // 1. Spec: "A free MSHR is a prerequisite for an access to the L2 cache"
    // Check if we can allocate OR if it's already pending (merge).
    // If not merged and valid MSHRs full, we must stall.
    
    int pending_idx = check_mshr(addr);
    
    // If NOT pending and MSHRs full, we can't even probe.
    if (pending_idx == -1) {
        // Check for free slot
        bool free_slot = false;
        for (int i=0; i<L2_MSHR_SIZE; i++) {
            if (!mshrs[i].valid) { free_slot = true; break; }
        }
        if (!free_slot) return L2_BUSY;
    }

    // 2. Check Cache Hit
    if (is_write) {
        if (probe_write(addr, nullptr)) return L2_HIT; // Hit
    } else {
        if (probe_read(addr) != nullptr) return L2_HIT; // Hit
    }

    // 3. Miss: Check MSHRs (Merge)
    if (pending_idx != -1) {
        return L2_MISS; // Request already pending (merged)
    }

    // 4. New Miss: Allocate MSHR
    // We already checked for a free slot above.
    int mshr_idx = allocate_mshr(addr, is_write, core_id); 
    if (mshr_idx != -1) {
        // Enqueue to Request Queue (5 cycle delay)
        Req_Queue_Item item;
        item.is_write = is_write;
        item.addr = addr;
        item.core_id = core_id;
        item.ready_cycle = stat_cycles + L2_TO_DRAM_DELAY;
        req_queue.push_back(item);
        
        return L2_MISS; 
    }

    // Should not reach here if logic holds
    return L2_BUSY;
}

void L2Cache::handle_dram_completion(uint32_t addr) {
    // DRAM returned data. Enqueue to Return Queue (5 cycle delay).
    Ret_Queue_Item item;
    item.addr = addr;
    item.ready_cycle = stat_cycles + DRAM_TO_L2_DELAY;
    ret_queue.push_back(item);
}

void L2Cache::cycle(uint64_t current_cycle, std::vector<std::unique_ptr<Core>>& cores) {
    // 1. Process Request Queue (L2 -> DRAM)
    for (auto it = req_queue.begin(); it != req_queue.end(); ) {
        if (current_cycle >= it->ready_cycle) {
            // Send to DRAM
            if (dram_ref) {
                dram_ref->enqueue(it->is_write, it->addr, it->core_id, DRAM_Req::SRC_MEMORY, current_cycle);
            }
            it = req_queue.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Process Return Queue (DRAM -> L2)
    for (auto it = ret_queue.begin(); it != ret_queue.end(); ) {
        if (current_cycle >= it->ready_cycle) {
            // Complete MSHR and Install
            complete_mshr(it->addr, cores);
            it = ret_queue.erase(it);
        } else {
            ++it;
        }
    }
}

void L2Cache::complete_mshr(uint32_t addr, std::vector<std::unique_ptr<Core>>& cores) {
    uint32_t block_addr = addr & ~(block_size - 1);
    for (int i = 0; i < L2_MSHR_SIZE; i++) {
        if (mshrs[i].valid && mshrs[i].address == block_addr) {
            mshrs[i].valid = false;
            
            // Install in L2
            bool dirty_evicted;
            uint32_t evicted_addr;
            install(addr, nullptr, &dirty_evicted, &evicted_addr, nullptr);
            
            // Wake up L1
            // Use stored core_id
            int cid = mshrs[i].core_id;
            if (cid >= 0 && cid < cores.size()) {
                cores[cid]->icache.fill(addr);
                cores[cid]->dcache.fill(addr);
            }
        }
    }
}


/* L1 Cache Methods */

L1Cache::L1Cache(int core_id, L2Cache* l2, uint32_t s, uint32_t w) 
    : Cache(s, w, BLOCK_SIZE), id(core_id), l2_ref(l2), pending_miss(false), pending_miss_addr(0), pending_miss_ready_cycle(0)
{
    // Parent constructor handles initialization
}

bool L1Cache::access(uint32_t addr, bool is_write, bool is_data_cache) {
#ifdef DEBUG
    printf("[L1 Core %d] Access Addr=%08x write=%d pending=%d\n", id, addr, is_write, pending_miss);
#endif
    // 1. Check for Pending Miss
    // 1. Check for Pending Miss
    if (pending_miss) {
        // Check if latency is satisfied
        if (stat_cycles >= pending_miss_ready_cycle) {
             fill(pending_miss_addr); // Clears pending_miss
             // Fall through to probe (which will now HIT)
        } else {
#ifdef DEBUG
            printf("[L1 Core %d] STALL: Pending miss for %08x (Req: %08x) ReadyAt: %u Curr: %u\n", id, pending_miss_addr, addr, pending_miss_ready_cycle, stat_cycles);
#endif
            return false;
        }
    }

    // 2. Check Hit
    if (is_write) {
        if (probe_write(addr, nullptr)) return true;
    } else {
        if (probe_read(addr) != nullptr) return true;
    }
    
    // 3. Handle Miss
    // Issue to L2 (Pass core_id)
    int l2_status = l2_ref->access(addr, is_write, id);
    
    if (l2_status == L2_HIT) {
        // L2 Hit! 
        pending_miss = true;
        pending_miss_addr = addr;
        pending_miss_ready_cycle = stat_cycles + L2_HIT_LATENCY;
        
        return false; // Stall this cycle and wait for latency
    }
    else if (l2_status == L2_MISS) {
#ifdef DEBUG
        printf("[L1 Core %d] MISS: L2 Accepted %08x. Entering Pending.\n", id, addr);
#endif
        pending_miss = true;
        pending_miss_addr = addr;
        return false; // Stall pipeline
    } else {
#ifdef DEBUG
        printf("[L1 Core %d] MISS: L2 REJECTED %08x. Retry.\n", id, addr);
#endif
        // L2 busy (MSHRs full). Retry next cycle.
        return false;
    }
}

void L1Cache::fill(uint32_t addr) {
#ifdef DEBUG
    printf("[L1 Core %d] FILL: %08x (Pending: %08x)\n", id, addr, pending_miss_addr);
#endif
    if (pending_miss && (addr & ~31) == (pending_miss_addr & ~31)) {
        // Install data (Assuming L2 sent it, though we don't pass data ptr here yet)
        bool dirty_evicted;
        uint32_t evicted_addr;
        std::vector<uint8_t> evicted_data;
        
        install(pending_miss_addr, nullptr, &dirty_evicted, &evicted_addr, &evicted_data);
        
        // Phase 3: Ignore writeback of evicted data for now (Clean/Dirty not fully utilized)
        
        pending_miss = false;
#ifdef DEBUG
        printf("[L1 Core %d] UNBLOCK: %08x\n", id, addr);
#endif
    }
}
