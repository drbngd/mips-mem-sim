/*
 * Computer Architecture - Professor Onur Mutlu
 *
 * MIPS pipeline timing simulator
 *
 * Config Constants
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

/* System Configuration */
#define NUM_CORES 4       /* Number of Cores */
#define DRAM_REQ_QUEUE_SIZE 32
#define DRAM_CHANNEL_WIDTH 8 /* 64-bit channel */

/* DRAM Configuration */
#define DRAM_SIZE 0x4000 /* For simulated main memory array size */

/* Shared Type Definitions for Configuration */
/* Enums defined in cache.h */

/* DRAM Organization */
#define DRAM_CHANNELS 1
#define DRAM_RANKS 1
#define DRAM_BANKS 8        /* Banks per Rank */
#define DRAM_ROWS 32768     /* Rows per Bank */
#define DRAM_ROW_SIZE 2048  /* Bytes per Row */
#define BLOCK_SIZE 32       /* Cache Line Size */

/* Derived for internal array sizing if needed */
#define TOTAL_BANKS (DRAM_CHANNELS * DRAM_RANKS * DRAM_BANKS)

/* DRAM Timing (Cycles) */
#define DRAM_PRE_CMD_BUS_BUSY_CYCLES 4   /* Time on Command Bus (PRE) */
#define DRAM_ACT_CMD_BUS_BUSY_CYCLES 4 /* Time on Command Bus (ACT) */
#define DRAM_RDWR_CMD_BUS_BUSY_CYCLES 4 /* Time on Command Bus (READ, WRITE) */
#define DRAM_RDWR_DATA_BUS_BUSY_CYCLES 50 /* Time on Data Bus */
#define DRAM_RDWR_BANK_BUSY_CYCLES 100 /* Bank Operation Latencies (Bank Busy Time) */


/* Cache Configuration */
/* L1 Instruction Cache (8KB, 4-way, 32B) */
#define L1_I_SETS 64
#define L1_I_ASSOC 4
#define L1_I_BLOCK_SIZE BLOCK_SIZE // Changed to use BLOCK_SIZE

/* L1 Data Cache (64KB, 8-way, 32B) */
#define L1_D_SETS 256
#define L1_D_ASSOC 8
#define L1_D_BLOCK_SIZE 32

/* L2 Cache (256KB, 16-way, 32B) */
#define L2_SIZE 262144
#define L2_ASSOC 16
#define L2_SETS (L2_SIZE / (L2_ASSOC * BLOCK_SIZE))
#define L2_INCL_POLICY INCL_INCLUSIVE /* Inclusive */
#define L2_MSHR_SIZE 16 /* Number of MSHRs */

/* Policies */
/* Replace with 0 (LRU), 1 (Random), etc. */
#define CACHE_REPL_POLICY REPL_LRU // LRU

/* L2 Latencies */
#define L2_HIT_LATENCY 15
#define L2_TO_DRAM_DELAY 5
#define DRAM_TO_L2_DELAY 5

/* DRAM Page Policy */
#define DRAM_PAGE_POLICY 0  // 0 = Open Row, 1 = Closed Row

#endif
