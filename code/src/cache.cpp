#include "cache.h"
#include <cstdint>
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <random>
#include <ctime>

/* Cache Statistics */
uint32_t stat_i_cache_read_misses = 0;
uint32_t stat_d_cache_read_misses = 0;
uint32_t stat_i_cache_write_misses = 0;
uint32_t stat_d_cache_write_misses = 0;
uint32_t stat_i_cache_read_hits = 0;
uint32_t stat_d_cache_read_hits = 0;
uint32_t stat_i_cache_write_hits = 0;
uint32_t stat_d_cache_write_hits = 0;


/* ============================================================================
 * Cache Constructor and Destructor
 * ============================================================================ */

Cache::Cache(uint32_t num_sets, uint32_t assoc, uint32_t line_size, uint32_t miss_penalty, ReplacementPolicy policy) 
    : sets(num_sets, Cache_Set(assoc, line_size)), 
      num_sets(num_sets), 
      assoc(assoc), 
      miss_penalty(miss_penalty), 
      line_size(line_size),
      policy(policy),
      psel_counter(nullptr),
      eaf_filter(nullptr),
      eaf_fifo_counter(0),
      total_cache_lines(0),
      rng(std::time(nullptr)),
      dist(0, 31)
{
    /* Allocate policy-specific data structures only when needed */
    if (policy == POLICY_LRU || policy == POLICY_DRRIP) {
        psel_counter = new uint32_t(PSEL_INITIAL_VALUE);
    }
    
    if (policy == POLICY_EAF) {
        total_cache_lines = num_sets * assoc;
        uint32_t eaf_size_bits = total_cache_lines * EAF_FILTER_SIZE_MULTIPLIER;
        eaf_filter = new BloomFilter(eaf_size_bits, EAF_NUM_HASH_FUNCTIONS);
        eaf_fifo_counter = 0;
    }
}

Cache::~Cache()
{
    if (psel_counter) {
        delete psel_counter;
    }
    if (eaf_filter) {
        delete eaf_filter;
    }
}

/* global variable -- cache */
/* Using pointers to allow runtime policy changes without recompilation */
Cache* i_cache = new Cache(I_CACHE_NUM_SETS, I_CACHE_ASSOC, CACHE_LINE_SIZE, L1_CACHE_MISS_PENALTY, POLICY_LRU);
Cache* d_cache = new Cache(D_CACHE_NUM_SETS, D_CACHE_ASSOC, CACHE_LINE_SIZE, L1_CACHE_MISS_PENALTY, POLICY_EAF);



int log2_32(int n) 
{
    if ( n == 0 ) return -1;
    return 31 - __builtin_clz(n);
}

/* ============================================================================
 * Bloom Filter Implementation for EAF
 * ============================================================================ */

BloomFilter::BloomFilter(uint32_t size_bits, uint32_t num_hash_functions) 
    : bits(size_bits, false), size_bits(size_bits), num_hash_functions(num_hash_functions)
{
}

uint32_t BloomFilter::hash1(uint32_t addr) const
{
    /* Simple hash: use lower bits after shifting */
    return (addr >> 6) % size_bits;
}

uint32_t BloomFilter::hash2(uint32_t addr) const
{
    /* XOR folding hash */
    return ((addr >> 6) ^ (addr >> 14)) % size_bits;
}

void BloomFilter::insert(uint32_t addr)
{
    uint32_t h1 = hash1(addr);
    uint32_t h2 = hash2(addr);
    
    bits[h1] = true;
    bits[h2] = true;
}

bool BloomFilter::test(uint32_t addr) const
{
    uint32_t h1 = hash1(addr);
    uint32_t h2 = hash2(addr);
    
    /* All hash positions must be set for a positive test */
    return bits[h1] && bits[h2];
}

void BloomFilter::clear()
{
    std::fill(bits.begin(), bits.end(), false);
}


void decipher_address(uint32_t address, uint32_t line_size, uint32_t num_sets, uint32_t &tag, uint32_t &set_index, uint32_t &offset) 
{
    offset = address & (line_size - 1);
    set_index = (address >> log2_32(line_size)) & (num_sets - 1);
    tag = address >> (log2_32(line_size) + log2_32(num_sets));
}


/* ============================================================================
 * Set Dueling Helper Functions
 * ============================================================================ */

bool Cache::is_leader_policy_0(uint32_t set_index) const
{
    #if SET_DUELING_DISTRIBUTED
    /* Distributed: Leader 0 if (set_index & 0x1F) == 0 */
    return (set_index & SET_DUELING_LEADER_MASK) == SET_DUELING_LEADER_0_OFFSET;
    #else
    /* Consecutive: Leader 0 if set_index < 32 */
    return set_index < 32;
    #endif
}

bool Cache::is_leader_policy_1(uint32_t set_index) const
{
    #if SET_DUELING_DISTRIBUTED
    /* Distributed: Leader 1 if (set_index & 0x1F) == 1 */
    return (set_index & SET_DUELING_LEADER_MASK) == SET_DUELING_LEADER_1_OFFSET;
    #else
    /* Consecutive: Leader 1 if 32 <= set_index < 64 */
    return (set_index >= 32) && (set_index < 64);
    #endif
}

bool Cache::use_policy_1(uint32_t set_index) const
{
    /* Follower sets use policy based on PSEL counter */
    if (!psel_counter) return false;
    return (*psel_counter >= PSEL_INITIAL_VALUE);
}

void Cache::update_psel_on_miss(uint32_t set_index, bool is_leader_0, bool is_leader_1)
{
    if (!psel_counter) return;
    
    if (is_leader_0) {
        /* Miss in Leader 0: Policy 0 is bad, shift to 1 */
        if (*psel_counter < PSEL_MAX_VALUE) {
            (*psel_counter)++;
        }
    } else if (is_leader_1) {
        /* Miss in Leader 1: Policy 1 is bad, shift to 0 */
        if (*psel_counter > 0) {
            (*psel_counter)--;
        }
    }
    /* Follower sets don't update PSEL */
}

/* ============================================================================
 * Victim Selection Functions
 * ============================================================================ */

uint32_t Cache::find_victim(uint32_t set_index)
{
    switch (policy) {
        case POLICY_DRRIP:
            return find_victim_rrip(set_index);
        case POLICY_LRU:
        case POLICY_DIP:
        case POLICY_EAF:
        default:
            return find_victim_lru(set_index);
    }
}

uint32_t Cache::find_victim_lru(uint32_t set_index) const 
{
    uint32_t lru_way, min_tick = UINT32_MAX;
    const auto& set = sets[set_index].lines;

    for (uint32_t way = 0; way < assoc; way++) {
        if (!set[way].valid) return way;

        if (set[way].last_touch_tick < min_tick) {
            min_tick = set[way].last_touch_tick;
            lru_way = way;
        }
    }
    return lru_way;
}

uint32_t Cache::find_victim_rrip(uint32_t set_index)
{
    auto& set = sets[set_index].lines;
    
    /* First, check for invalid lines */
    for (uint32_t way = 0; way < assoc; way++) {
        if (!set[way].valid) return way;
    }
    
    /* Search for victim with RRPV == 3, aging if needed */
    while (true) {
        /* Look for line with RRPV == 3 */
        for (uint32_t way = 0; way < assoc; way++) {
            if (set[way].rrpv == RRIP_MAX_RRPV) {
                return way;
            }
        }
        
        /* No line with RRPV == 3, age all lines */
        for (uint32_t way = 0; way < assoc; way++) {
            if (set[way].rrpv < RRIP_MAX_RRPV) {
                set[way].rrpv++;
            }
        }
    }
}

/* ============================================================================
 * Unified Insertion Function
 * ============================================================================ */

void Cache::insert_line(uint32_t set_index, uint32_t way, uint32_t address, uint32_t victim_tick)
{
    switch (policy) {
        case POLICY_LRU:
            sets[set_index].lines[way].last_touch_tick = stat_cycles + miss_penalty;
            break;
            
        case POLICY_DIP: {
            bool is_leader_0 = is_leader_policy_0(set_index);
            bool is_leader_1 = is_leader_policy_1(set_index);
            bool use_bip = false;
            
            if (is_leader_0) {
                /* Leader 0: Always use LRU */
                use_bip = false;
            } else if (is_leader_1) {
                /* Leader 1: Always use BIP */
                use_bip = true;
            } else {
                /* Follower: Use PSEL to decide */
                use_bip = use_policy_1(set_index);
            }
            
            if (use_bip) {
                /* BIP: 1/32 chance of MRU, else LRU */
                uint32_t rand_val = dist(rng);
                if (rand_val == 0) {
                    /* MRU insertion */
                    sets[set_index].lines[way].last_touch_tick = stat_cycles + miss_penalty;
                } else {
                    /* LRU insertion: use the victim's tick value */
                    sets[set_index].lines[way].last_touch_tick = victim_tick;
                }
            } else {
                /* LRU: Standard MRU insertion */
                sets[set_index].lines[way].last_touch_tick = stat_cycles + miss_penalty;
            }
            break;
        }
        
        case POLICY_DRRIP: {
            bool is_leader_0 = is_leader_policy_0(set_index);
            bool is_leader_1 = is_leader_policy_1(set_index);
            bool use_brrip = false;
            
            if (is_leader_0) {
                /* Leader 0: Always use SRRIP */
                use_brrip = false;
            } else if (is_leader_1) {
                /* Leader 1: Always use BRRIP */
                use_brrip = true;
            } else {
                /* Follower: Use PSEL to decide */
                use_brrip = use_policy_1(set_index);
            }
            
            if (use_brrip) {
                /* BRRIP: 1/32 chance of RRPV=2, else RRPV=3 */
                uint32_t rand_val = dist(rng);
                if (rand_val == 0) {
                    sets[set_index].lines[way].rrpv = 2; /* Long re-reference */
                } else {
                    sets[set_index].lines[way].rrpv = 3; /* Distant re-reference */
                }
            } else {
                /* SRRIP: Always insert at RRPV=2 */
                sets[set_index].lines[way].rrpv = 2;
            }
            break;
        }
        
        case POLICY_EAF: {
            if (!eaf_filter) break;
            
            /* Check EAF filter */
            uint32_t line_base = address & ~(line_size - 1);
            bool eaf_hit = eaf_filter->test(line_base);
            
            if (eaf_hit) {
                /* High reuse: Insert at MRU */
                sets[set_index].lines[way].last_touch_tick = stat_cycles + miss_penalty;
            } else {
                /* Low reuse: Use BIP insertion */
                uint32_t rand_val = dist(rng);
                if (rand_val == 0) {
                    /* MRU insertion (1/32 chance) */
                    sets[set_index].lines[way].last_touch_tick = stat_cycles + miss_penalty;
                } else {
                    /* LRU insertion: use the victim's tick value */
                    sets[set_index].lines[way].last_touch_tick = victim_tick;
                }
            }
            break;
        }
        
        default:
            sets[set_index].lines[way].last_touch_tick = stat_cycles + miss_penalty;
            break;
    }
}

/* ============================================================================
 * Unified Hit Update Function
 * ============================================================================ */

void Cache::update_on_hit(uint32_t set_index, uint32_t way)
{
    switch (policy) {
        case POLICY_DRRIP:
            /* Set RRPV to 0 (near re-reference), don't touch neighbors */
            sets[set_index].lines[way].rrpv = 0;
            break;
            
        case POLICY_LRU:
        case POLICY_EAF:
        default:
            sets[set_index].lines[way].last_touch_tick = stat_cycles;
            break;
    }
}

/* ============================================================================
 * Eviction Function
 * ============================================================================ */

void Cache::evict(uint32_t tag, uint32_t set_index, uint32_t way)
{
    auto& line = sets[set_index].lines[way];
    
    if (line.dirty) {
        uint32_t line_addr = (tag << (log2_32(num_sets) + log2_32(line_size))) | 
                             (set_index << log2_32(line_size));
        for (auto i = 0; i < line_size; i+=4) {
            mem_write_32(line_addr + i, (line.data[i+3] << 24) |
                                        (line.data[i+2] << 16) |
                                        (line.data[i+1] <<  8) |
                                        (line.data[i+0] <<  0));
        }
    }

    if (policy == POLICY_EAF && eaf_filter && line.valid) {
        /* Add evicted address to EAF filter */
        uint32_t line_addr = (tag << (log2_32(num_sets) + log2_32(line_size))) | 
                             (set_index << log2_32(line_size));
        eaf_filter->insert(line_addr);
        eaf_fifo_counter++;
        
        /* Clear filter when counter reaches total cache lines */
        if (eaf_fifo_counter >= total_cache_lines) {
            eaf_filter->clear();
            eaf_fifo_counter = 0;
        }
    }

    line.valid = false;
    line.dirty = false;
    line.last_touch_tick = 0;
    line.rrpv = 0;
}

void Cache::fetch(uint32_t address, uint32_t tag, Cache_Line& line)
{
    /* we are supposed to fetch the entire cache line starting from line-aligned address */ 
    /* Compute line-aligned base address */
    uint32_t line_base = address & ~(line_size - 1);
    for (auto i = 0; i < line_size; i+=4) {
        uint32_t word = mem_read_32(line_base + i);
        line.data[i+3] = (word >> 24) & 0xFF;
        line.data[i+2] = (word >> 16) & 0xFF;
        line.data[i+1] = (word >>  8) & 0xFF;
        line.data[i+0] = (word >>  0) & 0xFF;
    }

    line.valid = true;
    line.dirty = false;
    line.tag = tag;
    /* Note: last_touch_tick and rrpv are set by the policy-specific insertion functions */
    /* Don't set them here - let the insertion policy decide */
}

uint32_t Cache::lookup(std::vector<Cache_Line>& set, uint32_t tag)
{
    for (int way = 0; way < assoc; way++) {
        if (set[way].valid && set[way].tag == tag) {
            return way;
        }
    }
    return UINT32_MAX;
}


Cache_Result Cache::read(uint32_t address)
{
    uint32_t tag, set_index, offset;
    decipher_address(address, line_size, num_sets, tag, set_index, offset);
    auto& set = sets[set_index].lines;

    uint32_t way = lookup(set, tag);
    if (way != UINT32_MAX) {
        /* Cache hit */
        update_on_hit(set_index, way);
        
        /* Track statistics - determine if I-cache or D-cache */
        if (this == i_cache) {
            stat_i_cache_read_hits++;
        } else {
            stat_d_cache_read_hits++;
        }
        return {static_cast<uint32_t>(
               (set[way].data[offset+3] << 24) |
               (set[way].data[offset+2] << 16) |
               (set[way].data[offset+1] <<  8) |
               (set[way].data[offset+0] <<  0)), 
               0};
    }

    /* Cache miss */
    uint32_t victim_way = find_victim(set_index);
    uint32_t victim_tick = set[victim_way].last_touch_tick;
    
    /* evict the victim way */
    evict(tag, set_index, victim_way);

    fetch(address, tag, set[victim_way]);
    
    /* Apply policy-specific insertion logic */
    insert_line(set_index, victim_way, address, victim_tick);
    
    /* Update PSEL on miss for DIP and DRRIP */
    if ((policy == POLICY_LRU || policy == POLICY_DRRIP) && psel_counter) {
        bool is_leader_0 = is_leader_policy_0(set_index);
        bool is_leader_1 = is_leader_policy_1(set_index);
        update_psel_on_miss(set_index, is_leader_0, is_leader_1);
    }
    
    /* Track statistics - determine if I-cache or D-cache */
    if (this == i_cache) {
        stat_i_cache_read_misses++;
    } else {
        stat_d_cache_read_misses++;
    }
    return {static_cast<uint32_t>(
           (set[victim_way].data[offset+3] << 24) |
           (set[victim_way].data[offset+2] << 16) |
           (set[victim_way].data[offset+1] <<  8) |
           (set[victim_way].data[offset+0] <<  0)), 
           miss_penalty};
}

Cache_Result Cache::write(uint32_t address, uint32_t value) 
{
    uint32_t tag, set_index, offset;
    decipher_address(address, line_size, num_sets, tag, set_index, offset);
    auto& set = sets[set_index].lines;

    uint32_t way = lookup(set, tag);
    if (way != UINT32_MAX) {
        /* Cache hit */
        update_on_hit(set_index, way);
        
        /* write */
        set[way].dirty = true;
        set[way].data[offset+3] = (value >> 24) & 0xFF;
        set[way].data[offset+2] = (value >> 16) & 0xFF;
        set[way].data[offset+1] = (value >>  8) & 0xFF;
        set[way].data[offset+0] = (value >>  0) & 0xFF;
        /* Track statistics - determine if I-cache or D-cache */
        if (this == i_cache) {
            stat_i_cache_write_hits++;
        } else {
            stat_d_cache_write_hits++;
        }
        return {0, 0};
    }

    /* Cache miss */
    uint32_t victim_way = find_victim(set_index);
    uint32_t victim_tick = set[victim_way].last_touch_tick;
    
    evict(tag, set_index, victim_way);

    fetch(address, tag, set[victim_way]);
    
    /* Apply policy-specific insertion logic */
    insert_line(set_index, victim_way, address, victim_tick);
    
    /* Update PSEL on miss for DIP and DRRIP */
    if ((policy == POLICY_LRU || policy == POLICY_DRRIP) && psel_counter) {
        bool is_leader_0 = is_leader_policy_0(set_index);
        bool is_leader_1 = is_leader_policy_1(set_index);
        update_psel_on_miss(set_index, is_leader_0, is_leader_1);
    }
    
    set[victim_way].dirty = true;
    set[victim_way].data[offset+3] = (value >> 24) & 0xFF;
    set[victim_way].data[offset+2] = (value >> 16) & 0xFF;
    set[victim_way].data[offset+1] = (value >>  8) & 0xFF;
    set[victim_way].data[offset+0] = (value >>  0) & 0xFF;
    /* Track statistics - determine if I-cache or D-cache */
    if (this == i_cache) {
        stat_i_cache_write_misses++;
    } else {
        stat_d_cache_write_misses++;
    }
    return {0, miss_penalty};
}

void Cache::flush()
{
    /* evict all dirty lines using the evict() function */
    for (uint32_t set_index = 0; set_index < num_sets; set_index++) {
        for (uint32_t way = 0; way < assoc; way++) {
            if (sets[set_index].lines[way].dirty) {
                evict(sets[set_index].lines[way].tag, set_index, way);
            }
        }
    }
}