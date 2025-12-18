#ifndef _CACHE_H_
#define _CACHE_H_

#include "shell.h"
#include <array>
#include <vector>
#include <cstdint>
#include <stdint.h>

extern uint32_t stat_cycles;

#define CACHE_LINE_SIZE 32 /* 32 bytes */
#define L1_CACHE_MISS_PENALTY 50
#define I_CACHE_NUM_SETS 64
#define I_CACHE_ASSOC 4
#define D_CACHE_NUM_SETS 256
#define D_CACHE_ASSOC 8

struct Cache_Result {
    uint32_t data;
    uint32_t latency;
};



struct Cache_Line {

    std::array<uint8_t, CACHE_LINE_SIZE> data; /* 32 bytes of data */
    uint32_t tag; /* tag */
    bool valid; /* valid bit */
    bool dirty; /* dirty bit */
    uint32_t last_touch_tick; /* clock cycle when the line was last touched */
    Cache_Line() : data{}, tag(0), valid(false), dirty(false), last_touch_tick(0) {}

};


struct Cache_Set {

    std::vector<Cache_Line> lines; /* lines in the set */
    Cache_Set(uint32_t assoc) : lines(assoc, Cache_Line()) {}

};


class Cache {

private:
    std::vector<Cache_Set> sets; /* pointer to sets in the cache */
    uint32_t num_sets; /* number of sets in the cache */
    uint32_t assoc; /* associativity */
    uint32_t miss_penalty; /* miss penalty */
    uint32_t find_victim(uint32_t set_index) const;
    uint32_t find_victim_lru(uint32_t set_index) const;
    void evict(uint32_t tag, uint32_t set_index, uint32_t way);
    void fetch(uint32_t address, uint32_t tag, uint32_t set_index, uint32_t way);
    uint32_t lookup(std::vector<Cache_Line>& set, uint32_t tag);

public:
    Cache(uint32_t num_sets, uint32_t assoc, uint32_t miss_penalty) : sets(num_sets, Cache_Set(assoc)), num_sets(num_sets), assoc(assoc), miss_penalty(miss_penalty) {}
    Cache_Result read(uint32_t address);
    Cache_Result write(uint32_t address, uint32_t value);
    void flush();

};

/* global variable -- cache */
extern Cache i_cache;
extern Cache d_cache;

/* helper function */
void decipher_address(uint32_t address, uint32_t &tag, uint32_t &set_index, uint32_t &offset);

/* helper function */
int log2_32(int n);



#endif