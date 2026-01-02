#include "cache.h"
#include "config.h"
#include "core.h" // Needed for Core def
#include "processor.h"
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

void Cache::evict(uint32_t set_idx, int way, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data, bool writeback_clean) {
    CacheBlock& block = sets[set_idx].blocks[way];
    
    if (block.state != INVALID) {
        bool needs_writeback = block.dirty || writeback_clean;
        
        if (dirty_evicted) *dirty_evicted = block.dirty; // Report actual dirty status
        
        // Reconstruct address: (Tag << tag_shift) | (Set << index_shift)
        // Note: Offset is 0 for block address
        if (needs_writeback) {
             if (evicted_addr) {
                *evicted_addr = (block.tag << tag_shift) | (set_idx << index_shift);
            }
            if (evicted_data) {
                *evicted_data = block.data; // Copy data
            }
        }
    } else {
        if (dirty_evicted) *dirty_evicted = false;
    }
    
    // Invalidate
    block.state = INVALID; 
    block.dirty = false;
}

CacheBlock* Cache::install(uint32_t addr, const uint8_t* data, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data, bool writeback_clean) {
    uint32_t set_idx = get_index(addr);
    uint32_t tag = get_tag(addr);
    
    // Check if block already exists (e.g. Upgrade from Shared)
    int way = find_block(set_idx, tag);
    
    if (way != -1) {
        // Found existing block, update it in place.
        // No eviction needed.
        if (dirty_evicted) *dirty_evicted = false;
    } else {
        // Not found, need to allocate new way
        way = find_victim(set_idx);
        // Handle Eviction
        evict(set_idx, way, dirty_evicted, evicted_addr, evicted_data, writeback_clean);
    }
    
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
        if (probe_write(addr, nullptr)) {
            // In INCLUSIVE policy: L2 Hit is normal.
            
            // In EXCLUSIVE policy: L2 Hit means block is moving to L1.
            // We must invalidate the L2 copy.
            if (incl_policy == INCL_EXCLUSIVE) {
                uint32_t set_idx = get_index(addr);
                uint32_t tag = get_tag(addr);
                int way = find_block(set_idx, tag);
                if (way != -1) {
                   sets[set_idx].blocks[way].state = INVALID;
                   sets[set_idx].blocks[way].dirty = false;
                }
            }
            return L2_HIT; // Hit
        }
    } else {
        if (probe_read(addr) != nullptr) {
            // EXCLUSIVE Policy: On L2 Hit, invalidate block (move to L1)
            // Note: probe_read updated LRU. Invalidate effectively removes it.
            if (incl_policy == INCL_EXCLUSIVE) {
                uint32_t set_idx = get_index(addr);
                uint32_t tag = get_tag(addr);
                int way = find_block(set_idx, tag);
                if (way != -1) {
                   sets[set_idx].blocks[way].state = INVALID;
                   sets[set_idx].blocks[way].dirty = false;
                }
            }
            return L2_HIT; // Hit
        }
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

void L2Cache::complete_mshr(uint32_t addr, std::vector<std::unique_ptr<class Core>>& cores) {
    uint32_t block_addr = addr & ~(block_size - 1);
    for (int i = 0; i < L2_MSHR_SIZE; i++) {
        if (mshrs[i].valid && mshrs[i].address == block_addr) {
            mshrs[i].valid = false;
            
            // Install in L2
            bool dirty_evicted;
            uint32_t evicted_addr;
            std::vector<uint8_t> evicted_data;
            
            install(addr, nullptr, &dirty_evicted, &evicted_addr, &evicted_data);
            
            // Handle L2 Writeback to DRAM
            if (dirty_evicted && dram_ref) {
                 // Use stat_cycles. Spec: "Immediately written into main memory"
                 // Note: L2 eviction goes to SRC_MEMORY.
                 dram_ref->enqueue(true, evicted_addr, -1, DRAM_Req::SRC_MEMORY, stat_cycles);
            }
            
            // Wake up L1
            // Use stored core_id
            int cid = mshrs[i].core_id;
            if (cid >= 0 && cid < cores.size()) {
                // Determine L1 state based on request type
                // If it was a write, we grant MODIFIED.
                // If read, we grant EXCLUSIVE (assuming we are the only one, or SHARED if others have it - but that logic belongs in Coherence step). 
                // However, L2 Fill usually means we fetched from DRAM, so it's fresh.
                MESI_State st = mshrs[i].is_write ? MODIFIED : EXCLUSIVE;
                
                cores[cid]->icache.fill(addr, st);
                cores[cid]->dcache.fill(addr, st);
            }
        }
    }
}

void L2Cache::handle_l1_writeback(uint32_t addr, const std::vector<uint8_t>& data) {
    // Probe L2 for Write
    if (probe_write(addr, data.data())) {
        // Hit: L2 updated (dirty bit set, LRU updated, data copied)
        return;
    }
    
    // Miss: Write directly to DRAM (Bypass L2 allocation)
    if (dram_ref) {
        dram_ref->enqueue(true, addr, -1, DRAM_Req::SRC_MEMORY, stat_cycles);
    }
}


// Helper to get back pointers
void L2Cache::evict(uint32_t set_idx, int way, bool* dirty_evicted, uint32_t* evicted_addr, std::vector<uint8_t>* evicted_data, bool writeback_clean) {
    // 1. Get address of victim block
    uint32_t old_tag = sets[set_idx].blocks[way].tag;
    uint32_t old_addr = (old_tag << tag_shift) | (set_idx << index_shift);
    bool is_valid = sets[set_idx].blocks[way].state != INVALID;

    // 2. Call base eviction (handles data extraction and invalidation)
    Cache::evict(set_idx, way, dirty_evicted, evicted_addr, evicted_data, writeback_clean);

    // 3. Inclusive Policy: Invalidate in all L1s logic
    // If the policy is Inclusive, an eviction from L2 forces invalidation in all L1s (Back-invalidation).
    // If any L1 has a dirty copy, it must be written back to Memory to preserve data consistency.
    if (incl_policy == INCL_INCLUSIVE && is_valid) {
        for (auto* l1 : l1_refs) {
            // First check if L1 has it and if it's dirty
            bool is_modified = false;
            std::vector<uint8_t> data;
            // Hack: Reuse probe_coherence to get data, then invalidate? 
            // Or just allow invalidate to return data. 
            // Let's use probe_coherence (which we implemented).
            bool present = l1->probe_coherence(old_addr, true, &is_modified, &data); 
            // true arg means "is_write_req" -> will invalidate L1 block. Perfect.
            
            if (present && is_modified) {
                // We back-invalidated a dirty block from L1. 
                // Since L2 is evicting, we must write this data to Memory.
                if (dram_ref) {
                     dram_ref->enqueue(true, old_addr, -1, DRAM_Req::SRC_MEMORY, stat_cycles);
                }
            }
        }
    }
}

/* L1 Cache Methods */

bool L1Cache::invalidate(uint32_t addr) {
    uint32_t set_idx = get_index(addr);
    uint32_t tag = get_tag(addr);
    int way = find_block(set_idx, tag);
    
    if (way != -1) {
        sets[set_idx].blocks[way].state = INVALID;
        sets[set_idx].blocks[way].dirty = false;
        return true;
    }
    return false;
}

// Helper to get block index
// (No change)

L1Cache::L1Cache(int core_id, L2Cache* l2, class Core* core, uint32_t s, uint32_t w) 
    : Cache(s, w, BLOCK_SIZE), id(core_id), l2_ref(l2), parent_core(core)
{
    // Initialize MSHR
    mshr.valid = false;
    mshr.address = 0;
    mshr.ready_cycle = 0;
    mshr.core_id = id;
    
    // Register self with L2
    if (l2_ref) {
        l2_ref->l1_refs.push_back(this);
    }
}


bool L1Cache::probe_coherence(uint32_t addr, bool is_write_req, bool* is_modified, std::vector<uint8_t>* data) {
    uint32_t set_idx = get_index(addr);
    uint32_t tag = get_tag(addr);
    int way = find_block(set_idx, tag);
    
    if (way != -1) {
        CacheBlock& blk = sets[set_idx].blocks[way];
        if (blk.state == INVALID) return false;

        bool was_modified = (blk.state == MODIFIED);
        if (is_modified) *is_modified = was_modified;
        
        // If dirty, provide data
        if (was_modified && data) {
            *data = blk.data;
        }

        // State Transitions based on Snoop
        if (is_write_req) {
            // Another core is writing -> Invalidate our copy
            blk.state = INVALID;
            blk.dirty = false;
        } else {
            // Another core is reading -> Downgrade to Shared
            // If we were Modified or Exclusive, we become Shared.
            if (blk.state == MODIFIED || blk.state == EXCLUSIVE) {
                blk.state = SHARED;
                // If the block was Modified, we technically clean it w.r.t the system here,
                // because the probe logic (caller) handles the writeback to memory immediately.
                // Thus, this cache line is now Shared and Clean.
                blk.dirty = false; 
            }
        }
        return true;
    }
    return false;
}

bool L1Cache::access(uint32_t addr, bool is_write, bool is_data_cache) {
    // 1. Check MSHR (Pending Miss)
    if (mshr.valid) {
        // If we are waiting for this address, check if it's ready
        uint32_t block_addr = addr & ~(block_size - 1);
        if (mshr.address == block_addr) {
            if (stat_cycles >= mshr.ready_cycle) {
                // Determine target state from MSHR context (saved or inferred)
                // For now, assume we fill nicely.
                // Actually fill() is called by L2 response or Snoop response.
                // If ready_cycle is set, it means we hit somewhere and are waiting for latency.
#ifdef DEBUG
                printf("[L1 Core %d] Access %08x: MSHR Ready at %lu (Curr %u). Completing.\n", id, addr, mshr.ready_cycle, stat_cycles);
#endif
                // NOTE: If we satisfied the miss via Snoop or L2 Hit, the data isn't "pushed" to us via callback nicely in this framework without events.
                // So we simulate the "fill" happening here if it wasn't triggered by L2 callback.
                
                // Fill logic:
                // We use the 'target_state' determined during the Miss phase (Snoop or L2 Response).
                // This ensures we enter the correct state (Shared, Exclusive, or Modified).
                MESI_State target = (MESI_State)mshr.target_state;
                
                // Perform the installation of the block into L1
                fill(mshr.address, target);
                return true; // Hit now (Access satisfied)
            } else {
                return false; // Stall
            }
        } else {
            return false; // Stall, MSHR busy
        }
    }

    // 2. Check Hit
    CacheBlock* block = nullptr;
    if (is_write) {
        // Warning: This probe_write updates LRU and Dirty bit.
        // We only want to do that if it's a real hit (M or E).
        // If S, it's an UPGRADE MISS.
        // So we need to peek first.
        uint32_t set_idx = get_index(addr);
        uint32_t tag = get_tag(addr);
        int way = find_block(set_idx, tag);
        
        if (way != -1) {
            block = &sets[set_idx].blocks[way];
            if (block->state == MODIFIED || block->state == EXCLUSIVE) {
                // Hit!
                update_lru(set_idx, way);
                block->state = MODIFIED;
                block->dirty = true;
                return true;
            } else if (block->state == SHARED) {
                // Upgrade Miss! Fall through to Step 1.
            }
        }
    } else {
        // Read
        block = probe_read(addr); // Handles LRU update if hit
        if (block) return true;
    }
    
    // --- MISS HANDLING START ---
    
    // Step 1: Write Exclusion
    // Check if any *pending* write to this block exists in other MSHRs.
    // Also if this is a write, check if *any* pending read exists.
    bool conflict = false;
    for (const auto& core_ptr : parent_core->proc->cores) {
        if (core_ptr->id == id) continue; // Skip self
        
        // Check I-Cache
        if (core_ptr->icache.mshr.valid && core_ptr->icache.mshr.address == (addr & ~31)) {
             if (core_ptr->icache.mshr.is_write || is_write) conflict = true;
        }
        // Check D-Cache
        if (core_ptr->dcache.mshr.valid && core_ptr->dcache.mshr.address == (addr & ~31)) {
             if (core_ptr->dcache.mshr.is_write || is_write) conflict = true;
        }
        if (conflict) break;
    }
    
    if (conflict) return false; // Stall and Retry
    
    // Step 2 & 3: L2 MSHR Checks
    // Check active MSHR in L2 (Step 2)
    int l2_mshr_idx = l2_ref->check_mshr(addr);
    if (l2_mshr_idx != -1) return false; // Stall if L2 is already handling this (simplify: no merge for L1 initiated reqs per spec suggestion?)
    // Spec Step 3: Check availability
    // We can't easily check "availability" without allocating, but we can check loop.
    // L2Cache has fixed size MSHR.
    // If we call access() later it handles it. 
    // Spec says: "If no L2 MSHRs available, stall."
    // Let's peek.
    bool l2_full = true;
    for(int i=0; i<L2_MSHR_SIZE; i++) { if(!l2_ref->mshrs[i].valid) { l2_full = false; break; } }
    if (l2_full) return false; // Stall
    
    // Step 4: Probe Other L1 Caches
    bool found_shared = false;
    bool found_modified = false;
    std::vector<uint8_t> coherence_data; // To capture modified data
    
    for (const auto& core_ptr : parent_core->proc->cores) {
        if (core_ptr->id == id) continue; // Skip self
        
        // Probe I-Cache
        bool m = false; 
        if (core_ptr->icache.probe_coherence(addr, is_write, &m, &coherence_data)) {
            found_shared = true;
            if (m) found_modified = true;
        }
        
        // Probe D-Cache
        m = false;
        if (core_ptr->dcache.probe_coherence(addr, is_write, &m, &coherence_data)) {
            found_shared = true;
            if (m) found_modified = true;
        }
    }

    // Handle Coherence Results
    if (found_shared) {
        // If reading, we found it shared. Our target state is SHARED.
        // If writing, we invalidated copies. Target is MODIFIED.
        
        // Writeback modified data if found
        if (found_modified) {
             // "Immediately written into main memory" (Bypassing L2 update)
             if (l2_ref->dram_ref) {
                 // Using 'stat_cycles' for timestamp
                 l2_ref->dram_ref->enqueue(true, addr & ~31, -1, DRAM_Req::SRC_MEMORY, stat_cycles);
             }
        }
        
        mshr.valid = true;
        mshr.address = addr & ~31;
        mshr.is_write = is_write;
        mshr.ready_cycle = stat_cycles + 5; 
        
        // Determine Target State from Snoop
        if (is_write) {
            mshr.target_state = MODIFIED;
        } else {
            mshr.target_state = SHARED; // We found a copy, so we join as Shared
        }
        
        return false; 
    }
    
    // Step 5: Probe L2
    if (l2_ref->probe_read(addr)) { // L2 Has it (Hit)
         int res = l2_ref->access(addr, is_write, id);
         
         if (res == L2_HIT) {
             mshr.valid = true;
             mshr.address = addr & ~31;
             mshr.is_write = is_write;
             mshr.ready_cycle = stat_cycles + 5 + L2_HIT_LATENCY;
             
             // L2 Hit State Logic:
             // If Write -> MODIFIED
             // If Read -> EXCLUSIVE (Since we passed snooping step without finding it Shared)
             mshr.target_state = is_write ? MODIFIED : EXCLUSIVE;
             
             return false;
         }
    }
    
    // Step 6: Go to Memory
    // Allocates MSHR through L2 access logic
    int res = l2_ref->access(addr, is_write, id);
    if (res == L2_MISS) {
         mshr.valid = true;
         mshr.address = addr & ~31;
         mshr.is_write = is_write;
         mshr.ready_cycle = -1; // Wait for callback
         
         // DRAM Fill State Logic:
         // If Write -> MODIFIED
         // If Read -> EXCLUSIVE (First fetch)
         mshr.target_state = is_write ? MODIFIED : EXCLUSIVE;
         
         return false;
    }

    return false; // Should not reach here typically unless L2 Busy (checked earlier) or weird state
}

void L1Cache::fill(uint32_t addr, MESI_State target_state) {
    if (mshr.valid && mshr.address == (addr & ~(block_size - 1))) {
        bool dirty_evicted;
        uint32_t evicted_addr;
        std::vector<uint8_t> evicted_data;
        bool wb_clean = (l2_ref->incl_policy == INCL_EXCLUSIVE);
        
        CacheBlock* blk = install(addr, nullptr, &dirty_evicted, &evicted_addr, &evicted_data, wb_clean);
        if (blk) {
            blk->state = target_state;
            if (target_state == MODIFIED) blk->dirty = true;
        }

        if (dirty_evicted) {
             l2_ref->handle_l1_writeback(evicted_addr, evicted_data);
        } else if (wb_clean && evicted_data.size() > 0) {
             l2_ref->handle_l1_writeback(evicted_addr, evicted_data);
        }
        
        mshr.valid = false;
    }
}
