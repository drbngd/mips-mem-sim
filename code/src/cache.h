#ifndef _CACHE_H_
#define _CACHE_H_

#include "shell.h"
#include <array>
#include <vector>
#include <cstdint>
#include <stdint.h>
#include <random>

extern uint32_t stat_cycles;

/* Cache Statistics */
extern uint32_t stat_i_cache_read_misses;
extern uint32_t stat_d_cache_read_misses;
extern uint32_t stat_i_cache_write_misses;
extern uint32_t stat_d_cache_write_misses;
extern uint32_t stat_i_cache_read_hits;
extern uint32_t stat_d_cache_read_hits;
extern uint32_t stat_i_cache_write_hits;
extern uint32_t stat_d_cache_write_hits;

#define CACHE_LINE_SIZE 32
#define L1_CACHE_MISS_PENALTY 50
#define I_CACHE_NUM_SETS 16
#define I_CACHE_ASSOC 4
#define D_CACHE_NUM_SETS 256
#define D_CACHE_ASSOC 8

/* L2 Cache Configuration */
#define L2_CACHE_NUM_SETS 512
#define L2_CACHE_ASSOC 16
#define L2_CACHE_HIT_LATENCY 15
#define L2_TO_MEM_LATENCY 5
#define MEM_TO_L2_LATENCY 5
#define DRAM_LATENCY 100  /* Fixed DRAM latency for now */
#define NUM_MSHRS 16

/* Cache Level Enum */
enum CacheLevel {
    CACHE_L1 = 0,
    CACHE_L2
};

/* Replacement Policy Enum */
enum ReplacementPolicy {
    POLICY_LRU = 0,    /* Least Recently Used (default) */
    POLICY_DIP,        /* Dynamic Insertion Policy */
    POLICY_DRRIP,      /* Dynamic Re-Reference Interval Prediction */
    POLICY_EAF         /* Evicted Address Filter */
};

/* Policy Configuration Parameters */
#define DIP_EPSILON 32                 /* Use as: if (rand() % DIP_EPSILON == 0) */
#define EAF_FILTER_SIZE_MULTIPLIER 8   /* Bits per cache line */
#define EAF_NUM_HASH_FUNCTIONS 2       /* Number of hash functions for Bloom filter */
#define DRRIP_BRRIP_PROBABILITY 32     /* Use as: 1/32 chance of "Long" (RRPV=2) */
#define PSEL_INITIAL_VALUE 512         /* 10-bit counter mid-point (0-1023) */
#define PSEL_MAX_VALUE 1023            /* Maximum PSEL counter value */
#define SET_DUELING_LEADER_MASK 0x1F    /* Selects leaders for 32 sets */
#define SET_DUELING_LEADER_0_OFFSET 0  /* Leader 0: (set_index & 0x1F) == 0 */
#define SET_DUELING_LEADER_1_OFFSET 1  /* Leader 1: (set_index & 0x1F) == 1 */
#define SET_DUELING_DISTRIBUTED 1      /* 1 = distributed, 0 = consecutive */
#define RRIP_MAX_RRPV 3                /* Maximum RRPV value (2-bit: 0-3) */

struct Cache_Result {
    uint32_t data;
    int latency;        /* -1 means L2 miss, MSHR allocated */
    int mshr_index;    /* If latency == -1, this is the MSHR index */
};



struct Cache_Line {

    std::vector<uint8_t> data; /* 32 bytes of data */
    uint32_t tag; /* tag */
    bool valid; /* valid bit */
    bool dirty; /* dirty bit */
    uint32_t last_touch_tick; /* clock cycle when the line was last touched (for LRU/DIP) */
    uint8_t rrpv; /* Re-Reference Prediction Value (for DRRIP: 0-3) */
    Cache_Line(uint32_t line_size = CACHE_LINE_SIZE) : data(line_size, 0), tag(0), valid(false), dirty(false), last_touch_tick(0), rrpv(0) {}

};


struct Cache_Set {

    std::vector<Cache_Line> lines; /* lines in the set */
    Cache_Set(uint32_t assoc, uint32_t line_size) : lines(assoc, Cache_Line(line_size)) {}

};

/* Bloom Filter for EAF */
class BloomFilter {
private:
    std::vector<bool> bits;
    uint32_t size_bits;
    uint32_t num_hash_functions;
    
    /* Hash function helpers */
    uint32_t hash1(uint32_t addr) const;
    uint32_t hash2(uint32_t addr) const;
    
public:
    BloomFilter(uint32_t size_bits, uint32_t num_hash_functions);
    void insert(uint32_t addr);
    bool test(uint32_t addr) const;
    void clear();
    uint32_t get_size() const { return size_bits; }
};

class Cache {

private:
    std::vector<Cache_Set> sets; /* pointer to sets in the cache */
    uint32_t assoc; /* associativity */
    uint32_t miss_penalty; /* miss penalty */
    CacheLevel level; /* Cache level (L1 or L2) */
    ReplacementPolicy policy; /* Current replacement policy */
    
public:
    uint32_t num_sets; /* number of sets in the cache */
    uint32_t line_size; /* line size */
    
    /* Policy-specific data structures (only allocated when needed) */
    uint32_t* psel_counter; /* Policy Selector counter (for set dueling in DIP/DRRIP) */
    BloomFilter* eaf_filter; /* Evicted Address Filter (Bloom filter) for EAF */
    uint32_t eaf_fifo_counter; /* Counter for EAF clearing */
    uint32_t total_cache_lines; /* Total number of cache lines (for EAF) */
    
    /* Random number generator for BIP/BRRIP */
    mutable std::mt19937 rng;
    mutable std::uniform_int_distribution<uint32_t> dist;
    
    /* Unified victim selection function */
    uint32_t find_victim(uint32_t set_index);
    uint32_t find_victim_rrip(uint32_t set_index);
    
    
    /* Set dueling helper functions */
    bool is_leader_policy_0(uint32_t set_index) const;
    bool is_leader_policy_1(uint32_t set_index) const;
    bool use_policy_1(uint32_t set_index) const;
    void update_psel_on_miss(uint32_t set_index, bool is_leader_0, bool is_leader_1);
    
    /* Unified insertion function */
    void insert_line(uint32_t set_index, uint32_t way, uint32_t address, uint32_t victim_tick);
    
    /* Unified hit update function */
    void update_on_hit(uint32_t set_index, uint32_t way);

public:
    Cache(uint32_t num_sets, uint32_t assoc, uint32_t line_size, uint32_t miss_penalty, ReplacementPolicy policy = POLICY_LRU, CacheLevel level = CACHE_L1);
    ~Cache();
    
    /* Delete copy constructor and assignment operator to prevent Rule of Three violations */
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    
    /* Check if this is an L2 cache */
    bool is_l2() const { return level == CACHE_L2; }
    
    /* L2-specific: Probe cache without modifying state (for checking hit/miss) */
    Cache_Result probe(uint32_t address);
    
    /* Fill a cache line (used by L2 when filling L1) */
    void fill_line(uint32_t address, uint32_t data);
    
    /* Public accessors for L2 cache operations */
    Cache_Set& get_set(uint32_t set_index) { return sets[set_index]; }
    const Cache_Set& get_set(uint32_t set_index) const { return sets[set_index]; }
    
    /* Public methods for L2 cache management */
    uint32_t find_victim_lru(uint32_t set_index) const;
    void evict(uint32_t tag, uint32_t set_index, uint32_t way);
    void fetch(uint32_t address, uint32_t tag, Cache_Line& line);
    uint32_t lookup(uint32_t set_index, uint32_t tag) const;
    
    Cache_Result read(uint32_t address);
    Cache_Result write(uint32_t address, uint32_t value);
    void flush();

};

/* global variable -- cache */
/* Using pointers to allow runtime policy changes without recompilation */
extern Cache* i_cache;
extern Cache* d_cache;

/* helper function */
void decipher_address(uint32_t address, uint32_t line_size, uint32_t num_sets, uint32_t &tag, uint32_t &set_index, uint32_t &offset);

/* helper function */
int log2_32(int n);

#endif