#include "cache.h"
#include <cstdint>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Cache Statistics */
uint32_t stat_i_cache_read_misses = 0;
uint32_t stat_d_cache_read_misses = 0;
uint32_t stat_i_cache_write_misses = 0;
uint32_t stat_d_cache_write_misses = 0;
uint32_t stat_i_cache_read_hits = 0;
uint32_t stat_d_cache_read_hits = 0;
uint32_t stat_i_cache_write_hits = 0;
uint32_t stat_d_cache_write_hits = 0;




/* global variable -- cache */
Cache i_cache(I_CACHE_NUM_SETS, I_CACHE_ASSOC, L1_CACHE_MISS_PENALTY);
Cache d_cache(D_CACHE_NUM_SETS, D_CACHE_ASSOC, L1_CACHE_MISS_PENALTY);



int log2_32(int n) 
{
    if ( n == 0 ) return -1;
    return 31 - __builtin_clz(n);
}


void decipher_address(uint32_t address, uint32_t num_sets, uint32_t &tag, uint32_t &set_index, uint32_t &offset) 
{
    offset = address & (CACHE_LINE_SIZE - 1);
    set_index = (address >> log2_32(CACHE_LINE_SIZE)) & (num_sets - 1);
    tag = address >> (log2_32(CACHE_LINE_SIZE) + log2_32(num_sets));
}


uint32_t Cache::find_victim(uint32_t set_index) const 
{
    return find_victim_lru(set_index);
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

void Cache::evict(uint32_t tag, uint32_t set_index, uint32_t way)
{
    if (sets[set_index].lines[way].dirty) {
        uint32_t line_addr = (tag << (log2_32(num_sets) + log2_32(CACHE_LINE_SIZE))) | 
                             (set_index << log2_32(CACHE_LINE_SIZE));
        for (auto i = 0; i < CACHE_LINE_SIZE; i+=4) {
            mem_write_32(line_addr + i, (sets[set_index].lines[way].data[i+3] << 24) |
                                                       (sets[set_index].lines[way].data[i+2] << 16) |
                                                       (sets[set_index].lines[way].data[i+1] <<  8) |
                                                       (sets[set_index].lines[way].data[i+0] <<  0));
        }

    }

    sets[set_index].lines[way].valid = false;
    sets[set_index].lines[way].dirty = false;
    sets[set_index].lines[way].last_touch_tick = 0;
   
}

void Cache::fetch(uint32_t address, uint32_t tag, uint32_t set_index, uint32_t way)
{
    /* we are supposed to fetch 4 words at a time, depening on line size */ 
    auto& line = sets[set_index].lines[way];
    for (auto i = 0; i < CACHE_LINE_SIZE; i+=4) {
        uint32_t word = mem_read_32((address + i) & ~3);
        line.data[i+3] = (word >> 24) & 0xFF;
        line.data[i+2] = (word >> 16) & 0xFF;
        line.data[i+1] = (word >>  8) & 0xFF;
        line.data[i+0] = (word >>  0) & 0xFF;
    }

    line.valid = true;
    line.dirty = false;
    line.last_touch_tick = stat_cycles;
    line.tag = tag;
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
    decipher_address(address, num_sets, tag, set_index, offset);
    auto& set = sets[set_index].lines;

    uint32_t way = lookup(set, tag);
    if (way != UINT32_MAX) {
        return {static_cast<uint32_t>(
               (set[way].data[offset+3] << 24) |
               (set[way].data[offset+2] << 16) |
               (set[way].data[offset+1] <<  8) |
               (set[way].data[offset+0] <<  0)), 
               0};
    }

    uint32_t victim_way = find_victim(set_index);
    
    /* evict the victim way */
    /* no latency added for a dirty evict */
    evict(tag, set_index, victim_way);

    fetch(address, tag, set_index, victim_way);
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
    decipher_address(address, num_sets, tag, set_index, offset);
    auto& set = sets[set_index].lines;

    uint32_t way = lookup(set, tag);
    if (way != UINT32_MAX) {
        /* write */
        set[way].dirty = true;
        set[way].data[offset+3] = (value >> 24) & 0xFF;
        set[way].data[offset+2] = (value >> 16) & 0xFF;
        set[way].data[offset+1] = (value >>  8) & 0xFF;
        set[way].data[offset+0] = (value >>  0) & 0xFF;
        return {0, 0};
    }

    uint32_t victim_way = find_victim(set_index);
    evict(tag, set_index, victim_way);

    fetch(address, tag, set_index, victim_way);
    set[victim_way].dirty = true;
    set[victim_way].data[offset+3] = (value >> 24) & 0xFF;
    set[victim_way].data[offset+2] = (value >> 16) & 0xFF;
    set[victim_way].data[offset+1] = (value >>  8) & 0xFF;
    set[victim_way].data[offset+0] = (value >>  0) & 0xFF;
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