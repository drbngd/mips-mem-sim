/*
 * Computer Architecture - Professor Onur Mutlu
 *
 * MIPS pipeline timing simulator
 *
 * Chris Fallin, 2012
 */

 #include "pipe.h"
 #include "shell.h"
 #include "mips.h"
 #include "cache.h"
 #include "mshr.h"
 #include <cstdio>
 #include <cstring>
 #include <cstdlib>
 #include <cassert>
 #include <memory>
 #include <array>
 
//  #define DEBUG
 
 /* debug */
 void print_op(Pipe_Op *op)
 {
     if (op)
         printf("OP (PC=%08x inst=%08x) src1=R%d (%08x) src2=R%d (%08x) dst=R%d valid %d (%08x) br=%d taken=%d dest=%08x mem=%d addr=%08x\n",
                 op->pc, op->instruction, op->reg_src1, op->reg_src1_value, op->reg_src2, op->reg_src2_value, op->reg_dst, op->reg_dst_value_ready,
                 op->reg_dst_value, op->is_branch, op->branch_taken, op->branch_dest, op->is_mem, op->mem_addr);
     else
         printf("(null)\n");
 }
 
 /* global pipeline state */
 Pipe_State pipe;
 
void pipe_init()
{
    pipe = Pipe_State();
    /* Initialize MSHR manager with L2 cache line size */
    extern Cache* l2_cache;
    pipe.mshr_manager = new MSHRManager(l2_cache->line_size);
}
 
void pipe_cycle()
{
#ifdef DEBUG
    printf("\n\n---- Cycle %u ----\n", stat_cycles);
    printf("PC=0x%08x, fetch_stall=%d, mem_stall=%d, fetch_mshr=%d, mem_mshr=%d\n",
           pipe.PC, pipe.fetch_stall, pipe.mem_stall, pipe.fetch_mshr_index, pipe.mem_mshr_index);
    printf("PIPELINE:\n");
    printf("DCODE: "); print_op(pipe.decode_op.get());
    printf("EXEC : "); print_op(pipe.execute_op.get());
    printf("MEM  : "); print_op(pipe.mem_op.get());
    printf("WB   : "); print_op(pipe.wb_op.get());
    printf("\n");
#endif

    /* Process MSHRs first - update states and handle completions */
    /* This allows stages to react to MSHR completions in the same cycle */
    if (pipe.mshr_manager) {
        pipe.mshr_manager->process_cycle();
        process_completed_mshrs();
    }

    /* Process pipe stages after MSHRs */
    pipe_stage_wb();
    pipe_stage_mem();
    pipe_stage_execute();
    pipe_stage_decode();
    pipe_stage_fetch();
 
    /* handle branch recoveries */
    if (pipe.branch_recover) {
        uint32_t old_pc = pipe.PC;
        pipe.PC = pipe.branch_dest;

        /* Clear fetch stall since we're fetching from new PC */
        pipe.fetch_stall = 0;
        if (pipe.fetch_mshr_index >= 0) {
            pipe.mshr_manager->free(pipe.fetch_mshr_index);
        }
        pipe.fetch_mshr_index = -1;  /* Clear MSHR tracking */
#ifdef DEBUG
        printf("[BRANCH] Recovery: PC 0x%08x -> 0x%08x, flush %d stages, fetch_stall=%d, fetch_mshr_index=%d\n",
               old_pc, pipe.PC, pipe.branch_flush, pipe.fetch_stall, pipe.fetch_mshr_index);
#endif

        if (pipe.branch_flush >= 2) {
            pipe.decode_op.reset();
        }

        if (pipe.branch_flush >= 3) {
            pipe.execute_op.reset();
        }

        if (pipe.branch_flush >= 4) {
            pipe.mem_op.reset();
            pipe.mem_stall = 0;  /* Clear mem stall if mem stage is flushed */
            pipe.mem_cache_op_done = false;
            
            /* Free MSHR if it exists */
            if (pipe.mem_mshr_index >= 0) {
                pipe.mshr_manager->free(pipe.mem_mshr_index);
            }
            pipe.mem_mshr_index = -1;  /* Clear MSHR tracking */
        }

        if (pipe.branch_flush >= 5) {
            pipe.wb_op.reset();
        }

        pipe.branch_recover = 0;
        pipe.branch_dest = 0;
        pipe.branch_flush = 0;

        stat_squash++;
    }
}

void process_completed_mshrs()
{
    if (!pipe.mshr_manager) return;
    
    extern Cache* l2_cache;
    
    /* Process MSHRs: mem first, then fetch (MEM stage before FETCH stage) */
    struct MSHRInfo {
        int* mshr_index_ptr;
        uint32_t* l1_addr_ptr;
        Cache* l1_cache;
        uint32_t* pending_data_ptr;
        int* stall_ptr;
        bool* cache_op_done_ptr;
        bool is_fetch;
        const char* debug_name;
    };
    
    MSHRInfo mshrs_to_process[] = {
        {&pipe.mem_mshr_index, &pipe.mem_l1_address, d_cache, &pipe.pending_mem_data, 
         &pipe.mem_stall, &pipe.mem_cache_op_done, false, "Mem"},
        {&pipe.fetch_mshr_index, &pipe.fetch_l1_address, i_cache, &pipe.pending_fetch_inst,
         &pipe.fetch_stall, nullptr, true, "Fetch"}
    };
    
    for (auto& info : mshrs_to_process) {
        if (*info.mshr_index_ptr >= 0 && pipe.mshr_manager->is_ready(*info.mshr_index_ptr)) {
            MSHR* mshr = pipe.mshr_manager->get_mshr(*info.mshr_index_ptr);
            if (mshr) {
#ifdef DEBUG
                if (info.is_fetch) {
                    printf("[MSHR] %s MSHR %d completed: addr=0x%08x, l1_addr=0x%08x, PC=0x%08x\n",
                           info.debug_name, *info.mshr_index_ptr, mshr->address, *info.l1_addr_ptr, pipe.PC);
                } else {
                    printf("[MSHR] %s MSHR %d completed: addr=0x%08x, l1_addr=0x%08x\n",
                           info.debug_name, *info.mshr_index_ptr, mshr->address, *info.l1_addr_ptr);
                }
#endif
                /* Fill L2 cache with data from MSHR buffer */
                uint32_t line_addr = mshr->address;
                uint32_t tag, set_index, offset;
                decipher_address(line_addr, l2_cache->line_size, l2_cache->num_sets, tag, set_index, offset);
                
                /* Find victim in L2 */
                uint32_t victim_way = l2_cache->find_victim_lru(set_index);
                
                /* Evict victim if needed */
                auto& l2_set = l2_cache->get_set(set_index);
                if (l2_set.lines[victim_way].valid) {
                    uint32_t victim_tag = l2_set.lines[victim_way].tag;
                    l2_cache->evict(victim_tag, set_index, victim_way);
                }
                
                /* Copy data from MSHR buffer into L2 */
                for (uint32_t i = 0; i < l2_cache->line_size; i++) {
                    l2_set.lines[victim_way].data[i] = mshr->data[i];
                }
                
                /* Insert at MRU position in L2 */
                l2_set.lines[victim_way].tag = tag;
                l2_set.lines[victim_way].valid = true;
                l2_set.lines[victim_way].dirty = false;
                l2_set.lines[victim_way].last_touch_tick = stat_cycles;
                
                /* Fill L1 from L2 - we just filled L2, so read directly from victim_way */
                uint32_t l1_addr = *info.l1_addr_ptr;
                uint32_t l2_offset = l1_addr & (l2_cache->line_size - 1);
                uint32_t data = (l2_set.lines[victim_way].data[l2_offset+3] << 24) |
                               (l2_set.lines[victim_way].data[l2_offset+2] << 16) |
                               (l2_set.lines[victim_way].data[l2_offset+1] <<  8) |
                               (l2_set.lines[victim_way].data[l2_offset+0] <<  0);
                info.l1_cache->fill_line(l1_addr, data);
                
                /* Update pipeline state */
                *info.pending_data_ptr = data;
                *info.stall_ptr = 1;  /* Set to 1 so stage decrements it to 0 and handles it */
                if (info.cache_op_done_ptr) {
                    *info.cache_op_done_ptr = true;  /* Ensure flag is set so mem stage uses pending_mem_data */
                }
                
#ifdef DEBUG
                if (info.is_fetch) {
                    printf("[MSHR] Fetch MSHR completed: inst=0x%08x, pending_fetch_inst=0x%08x, fetch_stall=%d, PC=0x%08x\n",
                           data, *info.pending_data_ptr, *info.stall_ptr, pipe.PC);
                }
#endif
                pipe.mshr_manager->free(*info.mshr_index_ptr);
                *info.mshr_index_ptr = -1;
            }
        }
    }
}

void pipe_recover(int flush, uint32_t dest)
 {
     /* if there is already a recovery scheduled, it must have come from a later
      * stage (which executes older instructions), hence that recovery overrides
      * our recovery. Simply return in this case. */
     if (pipe.branch_recover) return;
 
     /* schedule the recovery. This will be done once all pipeline stages simulate the current cycle. */
     pipe.branch_recover = 1;
     pipe.branch_flush = flush;
     pipe.branch_dest = dest;
 }
 
 void pipe_stage_wb()
 {
     /* if there is no instruction in this pipeline stage, we are done */
     if (!pipe.wb_op)
         return;
 
     /* grab the op out of our input slot */
     Pipe_Op *op = pipe.wb_op.get();
 
     /* if this instruction writes a register, do so now */
     if (op->reg_dst != -1 && op->reg_dst != 0) {
         pipe.REGS[op->reg_dst] = op->reg_dst_value;
 #ifdef DEBUG
         printf("R%d = %08x\n", op->reg_dst, op->reg_dst_value);
 #endif
     }
 
    /* if this was a syscall, perform action */
    if (op->opcode == OP_SPECIAL && op->subop == SUBOP_SYSCALL) {
        if (op->reg_src1_value == 0xA) {
            pipe.PC = op->pc; /* fetch will do pc += 4, then we stop with correct PC */
            pipe.fetch_stall = 0;  /* Clear any pending fetch stall so PC gets incremented */
            if (pipe.fetch_mshr_index >= 0) {
                /* Free the MSHR */
                pipe.mshr_manager->free(pipe.fetch_mshr_index);
                pipe.fetch_mshr_index = -1;
            }
#ifdef DEBUG
            printf("[WB] Syscall exit: setting PC=0x%08x, fetch_stall=%d\n", pipe.PC, pipe.fetch_stall);
#endif
            RUN_BIT = false;
        }
    }
 
     /* free the op */
     pipe.wb_op.reset();
 
     stat_inst_retire++;
 }
 
void pipe_stage_mem()
{
    /* If waiting for MSHR, check if it completed (handled in pipe_cycle) */
    if (pipe.mem_mshr_index >= 0) {
        return;  /* Still waiting for MSHR */
    }
    
    /* if we're stalling on a D-cache miss, decrement and check */
    if (pipe.mem_stall > 0) {
        pipe.mem_stall--;
        if (pipe.mem_stall > 0) return;  /* still waiting for memory */
        /* Stall just ended - cache op was already done, data in pending_mem_data */
        /* mem_cache_op_done stays true until instruction completes */
    }

    /* if there is no instruction in this pipeline stage, we are done */
    if (!pipe.mem_op)
        return;

    /* grab the op out of our input slot */
    Pipe_Op *op = pipe.mem_op.get();

    uint32_t val = 0;
    if (op->is_mem) {
        if (pipe.mem_cache_op_done) {
            /* Cache op was done before stall, use pending data */
            if (op->opcode != OP_SW) {
                val = pipe.pending_mem_data;
            } else {
                /* For SW, cache was filled (by L2 hit or MSHR), now perform the write */
                d_cache->write(op->mem_addr & ~3, op->mem_value);
            }
        } else {
            /* First time accessing cache for this instruction */
            /* For loads and sub-word stores (SB, SH), we need to read first */
            /* For SW, we can write directly */
            if (op->opcode == OP_SW) {
                /* SW: write full word to cache */
                Cache_Result result = d_cache->write(op->mem_addr & ~3, op->mem_value);
                
                /* Handle L2 miss (latency = -1 means MSHR needed) */
                if (result.latency == -1) {
                    /* L2 miss - allocate MSHR (use L2 line size for alignment) */
                    extern Cache* l2_cache;
                    uint32_t line_addr = (op->mem_addr & ~3) & ~(l2_cache->line_size - 1);
                    int mshr_idx = pipe.mshr_manager->allocate(line_addr);
                    if (mshr_idx >= 0) {
                        pipe.mem_l1_address = op->mem_addr & ~3;  /* Store L1 address */
                        pipe.mem_mshr_index = mshr_idx;
                        pipe.mem_stall = 9999;  /* Large value - MSHR will set to 0 when done */
                        pipe.mem_cache_op_done = true;  /* Wait for MSHR, then retry write */
                    } else {
                        /* No free MSHR - stall pipeline */
                        pipe.mem_stall = 1;  /* Try again next cycle */
                    }
                    return;
                }
                
                if (result.latency > 0) {
                    /* L2 hit - fill D-cache from L2 */
                    d_cache->fill_line(op->mem_addr & ~3, 0);  /* Write will happen after fill */
                    pipe.mem_cache_op_done = true;  /* don't write again after stall */
                    pipe.mem_stall = result.latency - 1;
                    return;
                }
            } else {
                /* All other memory ops need to read first */
                Cache_Result result = d_cache->read(op->mem_addr & ~3);
                
                /* Handle L2 miss (latency = -1 means MSHR needed) */
                if (result.latency == -1) {
                    /* L2 miss - allocate MSHR (use L2 line size for alignment) */
                    extern Cache* l2_cache;
                    uint32_t line_addr = (op->mem_addr & ~3) & ~(l2_cache->line_size - 1);
                    int mshr_idx = pipe.mshr_manager->allocate(line_addr);
                    if (mshr_idx >= 0) {
                        pipe.mem_mshr_index = mshr_idx;
                        pipe.mem_l1_address = op->mem_addr & ~3;  /* Store L1 address */
                        pipe.mem_stall = 9999;  /* Large value - MSHR will set to 0 when done */
                        pipe.pending_mem_data = 0;  /* Will be filled by MSHR completion */
                        pipe.mem_cache_op_done = true;
                    } else {
                        /* No free MSHR - stall pipeline */
                        pipe.mem_stall = 1;  /* Try again next cycle */
                    }
                    return;
                }
                
                if (result.latency > 0) {
                    /* L2 hit - fill D-cache from L2 */
                    d_cache->fill_line(op->mem_addr & ~3, result.data);
                    pipe.pending_mem_data = result.data;  /* save for after stall */
                    pipe.mem_cache_op_done = true;
                    pipe.mem_stall = result.latency - 1;
                    return;
                }
                val = result.data;
            }
        }
    }

    switch (op->opcode) {
        case OP_LW:
        case OP_LH:
        case OP_LHU:
        case OP_LB:
        case OP_LBU:
            {
                /* extract needed value */
                op->reg_dst_value_ready = 1;
                if (op->opcode == OP_LW) {
                    op->reg_dst_value = val;
                }
                else if (op->opcode == OP_LH || op->opcode == OP_LHU) {
                    if (op->mem_addr & 2)
                        val = (val >> 16) & 0xFFFF;
                    else
                        val = val & 0xFFFF;

                    if (op->opcode == OP_LH)
                        val |= (val & 0x8000) ? 0xFFFF8000 : 0;

                    op->reg_dst_value = val;
                }
                else if (op->opcode == OP_LB || op->opcode == OP_LBU) {
                    switch (op->mem_addr & 3) {
                        case 0:
                            val = val & 0xFF;
                            break;
                        case 1:
                            val = (val >> 8) & 0xFF;
                            break;
                        case 2:
                            val = (val >> 16) & 0xFF;
                            break;
                        case 3:
                            val = (val >> 24) & 0xFF;
                            break;
                    }

                    if (op->opcode == OP_LB)
                        val |= (val & 0x80) ? 0xFFFFFF80 : 0;

                    op->reg_dst_value = val;
                }
            }
            break;

        case OP_SB:
            switch (op->mem_addr & 3) {
                case 0: val = (val & 0xFFFFFF00) | ((op->mem_value & 0xFF) << 0); break;
                case 1: val = (val & 0xFFFF00FF) | ((op->mem_value & 0xFF) << 8); break;
                case 2: val = (val & 0xFF00FFFF) | ((op->mem_value & 0xFF) << 16); break;
                case 3: val = (val & 0x00FFFFFF) | ((op->mem_value & 0xFF) << 24); break;
            }

            /* In single-core: will hit since we just read the line.
               In multi-core: could miss due to coherency invalidation.
               For now, ignore write latency (assumes single-core). */
            d_cache->write(op->mem_addr & ~3, val);
            break;

        case OP_SH:
#ifdef DEBUG
            printf("SH: addr %08x val %04x old word %08x\n", op->mem_addr, op->mem_value & 0xFFFF, val);
#endif
            if (op->mem_addr & 2)
                val = (val & 0x0000FFFF) | (op->mem_value) << 16;
            else
                val = (val & 0xFFFF0000) | (op->mem_value & 0xFFFF);
#ifdef DEBUG
            printf("new word %08x\n", val);
#endif

            /* In single-core: will hit since we just read the line.
               In multi-core: could miss due to coherency invalidation.
               For now, ignore write latency (assumes single-core). */
            d_cache->write(op->mem_addr & ~3, val);
            break;

        case OP_SW:
            /* Already handled above in the cache write */
            break;
    }

    /* Reset cache op flag as instruction is leaving this stage */
    pipe.mem_cache_op_done = false;

    /* clear stage input and transfer to next stage */
    pipe.wb_op = std::move(pipe.mem_op);
}
 
 void pipe_stage_execute()
 {
     /* if a multiply/divide is in progress, decrement cycles until value is ready */
     if (pipe.multiplier_stall > 0)
         pipe.multiplier_stall--;
 
     /* if downstream stall, return (and leave any input we had) */
     if (pipe.mem_op)
         return;
 
     /* if no op to execute, return */
     if (!pipe.execute_op)
         return;
 
     /* grab op and read sources */
     Pipe_Op *op = pipe.execute_op.get();
 
     /* read register values, and check for bypass; stall if necessary */
     int stall = 0;
     if (op->reg_src1 != -1) {
         if (op->reg_src1 == 0)
             op->reg_src1_value = 0;
         else if (pipe.mem_op && pipe.mem_op->reg_dst == op->reg_src1) {
             if (!pipe.mem_op->reg_dst_value_ready)
                 stall = 1;
             else
                 op->reg_src1_value = pipe.mem_op->reg_dst_value;
         }
         else if (pipe.wb_op && pipe.wb_op->reg_dst == op->reg_src1) {
             op->reg_src1_value = pipe.wb_op->reg_dst_value;
         }
         else
             op->reg_src1_value = pipe.REGS[op->reg_src1];
     }
     if (op->reg_src2 != -1) {
         if (op->reg_src2 == 0)
             op->reg_src2_value = 0;
         else if (pipe.mem_op && pipe.mem_op->reg_dst == op->reg_src2) {
             if (!pipe.mem_op->reg_dst_value_ready)
                 stall = 1;
             else
                 op->reg_src2_value = pipe.mem_op->reg_dst_value;
         }
         else if (pipe.wb_op && pipe.wb_op->reg_dst == op->reg_src2) {
             op->reg_src2_value = pipe.wb_op->reg_dst_value;
         }
         else
             op->reg_src2_value = pipe.REGS[op->reg_src2];
     }
 
     /* if bypassing requires a stall (e.g. use immediately after load),
      * return without clearing stage input */
     if (stall) 
         return;
 
     /* execute the op */
     switch (op->opcode) {
         case OP_SPECIAL:
             op->reg_dst_value_ready = 1;
             switch (op->subop) {
                 case SUBOP_SLL:
                     op->reg_dst_value = op->reg_src2_value << op->shamt;
                     break;
                 case SUBOP_SLLV:
                     op->reg_dst_value = op->reg_src2_value << op->reg_src1_value;
                     break;
                 case SUBOP_SRL:
                     op->reg_dst_value = op->reg_src2_value >> op->shamt;
                     break;
                 case SUBOP_SRLV:
                     op->reg_dst_value = op->reg_src2_value >> op->reg_src1_value;
                     break;
                 case SUBOP_SRA:
                     op->reg_dst_value = (int32_t)op->reg_src2_value >> op->shamt;
                     break;
                 case SUBOP_SRAV:
                     op->reg_dst_value = (int32_t)op->reg_src2_value >> op->reg_src1_value;
                     break;
                 case SUBOP_JR:
                 case SUBOP_JALR:
                     op->reg_dst_value = op->pc + 4;
                     op->branch_dest = op->reg_src1_value;
                     op->branch_taken = 1;
                     break;
 
                 case SUBOP_MULT:
                     {
                         /* we set a result value right away; however, we will
                          * model a stall if the program tries to read the value
                          * before it's ready (or overwrite HI/LO). Also, if
                          * another multiply comes down the pipe later, it will
                          * update the values and re-set the stall cycle count
                          * for a new operation.
                          */
                         int64_t val = (int64_t)((int32_t)op->reg_src1_value) * (int64_t)((int32_t)op->reg_src2_value);
                         uint64_t uval = (uint64_t)val;
                         pipe.HI = (uval >> 32) & 0xFFFFFFFF;
                         pipe.LO = (uval >>  0) & 0xFFFFFFFF;
 
                         /* four-cycle multiplier latency */
                         pipe.multiplier_stall = 4;
                     }
                     break;
                 case SUBOP_MULTU:
                     {
                         uint64_t val = (uint64_t)op->reg_src1_value * (uint64_t)op->reg_src2_value;
                         pipe.HI = (val >> 32) & 0xFFFFFFFF;
                         pipe.LO = (val >>  0) & 0xFFFFFFFF;
 
                         /* four-cycle multiplier latency */
                         pipe.multiplier_stall = 4;
                     }
                     break;
 
                 case SUBOP_DIV:
                     if (op->reg_src2_value != 0) {
 
                         int32_t val1 = (int32_t)op->reg_src1_value;
                         int32_t val2 = (int32_t)op->reg_src2_value;
                         int32_t div, mod;
 
                         div = val1 / val2;
                         mod = val1 % val2;
 
                         pipe.LO = div;
                         pipe.HI = mod;
                     } else {
                         // really this would be a div-by-0 exception
                         pipe.HI = pipe.LO = 0;
                     }
 
                     /* 32-cycle divider latency */
                     pipe.multiplier_stall = 32;
                     break;
 
                 case SUBOP_DIVU:
                     if (op->reg_src2_value != 0) {
                         pipe.HI = (uint32_t)op->reg_src1_value % (uint32_t)op->reg_src2_value;
                         pipe.LO = (uint32_t)op->reg_src1_value / (uint32_t)op->reg_src2_value;
                     } else {
                         /* really this would be a div-by-0 exception */
                         pipe.HI = pipe.LO = 0;
                     }
 
                     /* 32-cycle divider latency */
                     pipe.multiplier_stall = 32;
                     break;
 
                 case SUBOP_MFHI:
                     /* stall until value is ready */
                     if (pipe.multiplier_stall > 0)
                         return;
 
                     op->reg_dst_value = pipe.HI;
                     break;
                 case SUBOP_MTHI:
                     /* stall to respect WAW dependence */
                     if (pipe.multiplier_stall > 0)
                         return;
 
                     pipe.HI = op->reg_src1_value;
                     break;
 
                 case SUBOP_MFLO:
                     /* stall until value is ready */
                     if (pipe.multiplier_stall > 0)
                         return;
 
                     op->reg_dst_value = pipe.LO;
                     break;
                 case SUBOP_MTLO:
                     /* stall to respect WAW dependence */
                     if (pipe.multiplier_stall > 0)
                         return;
 
                     pipe.LO = op->reg_src1_value;
                     break;
 
                 case SUBOP_ADD:
                 case SUBOP_ADDU:
                     op->reg_dst_value = op->reg_src1_value + op->reg_src2_value;
                     break;
                 case SUBOP_SUB:
                 case SUBOP_SUBU:
                     op->reg_dst_value = op->reg_src1_value - op->reg_src2_value;
                     break;
                 case SUBOP_AND:
                     op->reg_dst_value = op->reg_src1_value & op->reg_src2_value;
                     break;
                 case SUBOP_OR:
                     op->reg_dst_value = op->reg_src1_value | op->reg_src2_value;
                     break;
                 case SUBOP_NOR:
                     op->reg_dst_value = ~(op->reg_src1_value | op->reg_src2_value);
                     break;
                 case SUBOP_XOR:
                     op->reg_dst_value = op->reg_src1_value ^ op->reg_src2_value;
                     break;
                 case SUBOP_SLT:
                     op->reg_dst_value = ((int32_t)op->reg_src1_value <
                             (int32_t)op->reg_src2_value) ? 1 : 0;
                     break;
                 case SUBOP_SLTU:
                     op->reg_dst_value = (op->reg_src1_value < op->reg_src2_value) ? 1 : 0;
                     break;
             }
             break;
 
         case OP_BRSPEC:
             switch (op->subop) {
                 case BROP_BLTZ:
                 case BROP_BLTZAL:
                     if ((int32_t)op->reg_src1_value < 0) op->branch_taken = 1;
                     break;
 
                 case BROP_BGEZ:
                 case BROP_BGEZAL:
                     if ((int32_t)op->reg_src1_value >= 0) op->branch_taken = 1;
                     break;
             }
             break;
 
         case OP_BEQ:
             if (op->reg_src1_value == op->reg_src2_value) op->branch_taken = 1;
             break;
 
         case OP_BNE:
             if (op->reg_src1_value != op->reg_src2_value) op->branch_taken = 1;
             break;
 
         case OP_BLEZ:
             if ((int32_t)op->reg_src1_value <= 0) op->branch_taken = 1;
             break;
 
         case OP_BGTZ:
             if ((int32_t)op->reg_src1_value > 0) op->branch_taken = 1;
             break;
 
         case OP_ADDI:
         case OP_ADDIU:
             op->reg_dst_value_ready = 1;
             op->reg_dst_value = op->reg_src1_value + op->se_imm16;
             break;
         case OP_SLTI:
             op->reg_dst_value_ready = 1;
             op->reg_dst_value = (int32_t)op->reg_src1_value < (int32_t)op->se_imm16 ? 1 : 0;
             break;
         case OP_SLTIU:
             op->reg_dst_value_ready = 1;
             op->reg_dst_value = (uint32_t)op->reg_src1_value < (uint32_t)op->se_imm16 ? 1 : 0;
             break;
         case OP_ANDI:
             op->reg_dst_value_ready = 1;
             op->reg_dst_value = op->reg_src1_value & op->imm16;
             break;
         case OP_ORI:
             op->reg_dst_value_ready = 1;
             op->reg_dst_value = op->reg_src1_value | op->imm16;
             break;
         case OP_XORI:
             op->reg_dst_value_ready = 1;
             op->reg_dst_value = op->reg_src1_value ^ op->imm16;
             break;
         case OP_LUI:
             op->reg_dst_value_ready = 1;
             op->reg_dst_value = op->imm16 << 16;
             break;
 
         case OP_LW:
         case OP_LH:
         case OP_LHU:
         case OP_LB:
         case OP_LBU:
             op->mem_addr = op->reg_src1_value + op->se_imm16;
             break;
 
         case OP_SW:
         case OP_SH:
         case OP_SB:
             op->mem_addr = op->reg_src1_value + op->se_imm16;
             op->mem_value = op->reg_src2_value;
             break;
     }
 
     /* handle branch recoveries at this point */
     if (op->branch_taken)
         pipe_recover(3, op->branch_dest);
 
     /* remove from upstream stage and place in downstream stage */
     pipe.mem_op = std::move(pipe.execute_op);
 }
 
 void pipe_stage_decode()
 {
     /* if downstream stall, return (and leave any input we had) */
     if (pipe.execute_op)
         return;
 
     /* if no op to decode, return */
     if (!pipe.decode_op)
         return;
 
     /* grab op and remove from stage input */
     Pipe_Op *op = pipe.decode_op.get();
 
     /* set up info fields (source/dest regs, immediate, jump dest) as necessary */
     uint32_t opcode = (op->instruction >> 26) & 0x3F;
     uint32_t rs = (op->instruction >> 21) & 0x1F;
     uint32_t rt = (op->instruction >> 16) & 0x1F;
     uint32_t rd = (op->instruction >> 11) & 0x1F;
     uint32_t shamt = (op->instruction >> 6) & 0x1F;
     uint32_t funct1 = (op->instruction >> 0) & 0x1F;
     uint32_t funct2 = (op->instruction >> 0) & 0x3F;
     uint32_t imm16 = (op->instruction >> 0) & 0xFFFF;
     uint32_t se_imm16 = imm16 | ((imm16 & 0x8000) ? 0xFFFF8000 : 0);
     uint32_t targ = (op->instruction & ((1UL << 26) - 1)) << 2;
 
     op->opcode = opcode;
     op->imm16 = imm16;
     op->se_imm16 = se_imm16;
     op->shamt = shamt;
 
     switch (opcode) {
         case OP_SPECIAL:
             /* all "SPECIAL" insts are R-types that use the ALU and both source
              * regs. Set up source regs and immediate value. */
             op->reg_src1 = rs;
             op->reg_src2 = rt;
             op->reg_dst = rd;
             op->subop = funct2;
             if (funct2 == SUBOP_SYSCALL) {
                 op->reg_src1 = 2; // v0
                 op->reg_src2 = 3; // v1
             }
             if (funct2 == SUBOP_JR || funct2 == SUBOP_JALR) {
                 op->is_branch = 1;
                 op->branch_cond = 0;
             }
 
             break;
 
         case OP_BRSPEC:
             /* branches that have -and-link variants come here */
             op->is_branch = 1;
             op->reg_src1 = rs;
             op->reg_src2 = rt;
             op->is_branch = 1;
             op->branch_cond = 1; /* conditional branch */
             op->branch_dest = op->pc + 4 + (se_imm16 << 2);
             op->subop = rt;
             if (rt == BROP_BLTZAL || rt == BROP_BGEZAL) {
                 /* link reg */
                 op->reg_dst = 31;
                 op->reg_dst_value = op->pc + 4;
                 op->reg_dst_value_ready = 1;
             }
             break;
 
         case OP_JAL:
             op->reg_dst = 31;
             op->reg_dst_value = op->pc + 4;
             op->reg_dst_value_ready = 1;
             op->branch_taken = 1;
             /* fallthrough */
         case OP_J:
             op->is_branch = 1;
             op->branch_cond = 0;
             op->branch_taken = 1;
             op->branch_dest = (op->pc & 0xF0000000) | targ;
             break;
 
         case OP_BEQ:
         case OP_BNE:
         case OP_BLEZ:
         case OP_BGTZ:
             /* ordinary conditional branches (resolved after execute) */
             op->is_branch = 1;
             op->branch_cond = 1;
             op->branch_dest = op->pc + 4 + (se_imm16 << 2);
             op->reg_src1 = rs;
             op->reg_src2 = rt;
             break;
 
         case OP_ADDI:
         case OP_ADDIU:
         case OP_SLTI:
         case OP_SLTIU:
             /* I-type ALU ops with sign-extended immediates */
             op->reg_src1 = rs;
             op->reg_dst = rt;
             break;
 
         case OP_ANDI:
         case OP_ORI:
         case OP_XORI:
         case OP_LUI:
             /* I-type ALU ops with non-sign-extended immediates */
             op->reg_src1 = rs;
             op->reg_dst = rt;
             break;
 
         case OP_LW:
         case OP_LH:
         case OP_LHU:
         case OP_LB:
         case OP_LBU:
         case OP_SW:
         case OP_SH:
         case OP_SB:
             /* memory ops */
             op->is_mem = 1;
             op->reg_src1 = rs;
             if (opcode == OP_LW || opcode == OP_LH || opcode == OP_LHU || opcode == OP_LB || opcode == OP_LBU) {
                 /* load */
                 op->mem_write = 0;
                 op->reg_dst = rt;
             }
             else {
                 /* store */
                 op->mem_write = 1;
                 op->reg_src2 = rt;
             }
             break;
     }
 
     /* we will handle reg-read together with bypass in the execute stage */
 
     /* place op in downstream slot */
     pipe.execute_op = std::move(pipe.decode_op);
 }
 
void pipe_stage_fetch()
{
    /* If waiting for MSHR, check if it completed (handled in pipe_cycle) */
    if (pipe.fetch_mshr_index >= 0) {
        return;  /* Still waiting for MSHR */
    }
    
    /* if we're stalling on an I-cache miss, decrement and check */
    if (pipe.fetch_stall > 0) {
        pipe.fetch_stall--;
#ifdef DEBUG
        printf("[FETCH] Stall countdown: fetch_stall=%d, pending_fetch_inst=0x%08x, PC=0x%08x\n",
               pipe.fetch_stall, pipe.pending_fetch_inst, pipe.PC);
#endif
        if (pipe.fetch_stall > 0) return;  /* still waiting for memory */
        
        /* Stall just ended - instruction is already in pending_fetch_inst */
        if (pipe.decode_op) return;  /* downstream stall */
        
        auto op = std::make_unique<Pipe_Op>();
        uint32_t inst = pipe.pending_fetch_inst;
        op->instruction = inst;
        op->pc = pipe.PC;
        uint32_t old_pc = pipe.PC;
        pipe.PC += 4;
        pipe.pending_fetch_inst = 0;  /* Clear pending */
        pipe.decode_op = std::move(op);
        stat_inst_fetch++;
#ifdef DEBUG
        printf("[FETCH] Stall ended: fetched inst=0x%08x, PC: 0x%08x -> 0x%08x\n",
               inst, old_pc, pipe.PC);
#endif
        return;
    }
    

    /* if pipeline is stalled (our output slot is not empty), return */
    if (pipe.decode_op)
        return;

    /* Access instruction cache */
    Cache_Result result = i_cache->read(pipe.PC);
#ifdef DEBUG
    printf("[FETCH] Cache read PC=0x%08x: latency=%d, data=0x%08x\n", pipe.PC, result.latency, result.data);
#endif

    /* Handle L2 miss (latency = -1 means MSHR needed) */
    /* RUN_BIT ensures we don't allocate MSHRs on final cycle */
    if (result.latency == -1 && RUN_BIT) {
        /* L2 miss - allocate MSHR (use L2 line size for alignment) */
        extern Cache* l2_cache;
        uint32_t line_addr = pipe.PC & ~(l2_cache->line_size - 1);
        int mshr_idx = pipe.mshr_manager->allocate(line_addr);
        if (mshr_idx >= 0) {
            pipe.fetch_mshr_index = mshr_idx;
            pipe.fetch_l1_address = pipe.PC;  /* Store L1 address */
            pipe.fetch_stall = 9999;  /* Large value - MSHR will set to 0 when done */
            pipe.pending_fetch_inst = 0;  /* Will be filled by MSHR completion */
#ifdef DEBUG
            printf("[FETCH] L2 miss: allocated MSHR %d, line_addr=0x%08x, PC=0x%08x, fetch_stall=%d\n",
                   mshr_idx, line_addr, pipe.PC, pipe.fetch_stall);
#endif
        } else {
            /* No free MSHR - stall pipeline */
            pipe.fetch_stall = 1;  /* Try again next cycle */
#ifdef DEBUG
            printf("[FETCH] L2 miss: no free MSHR, stalling, PC=0x%08x\n", pipe.PC);
#endif
        }
        return;
    }

    /* If cache miss (L2 hit), store the instruction and start stalling */
    if (result.latency > 0 && RUN_BIT) { /* RUN_BIT ensures we don't stall on final cycle */
        /* L2 hit - fill I-cache from L2 */
        i_cache->fill_line(pipe.PC, result.data);
        pipe.pending_fetch_inst = result.data;  /* save for after stall */
        pipe.fetch_stall = result.latency - 1;  /* -1 because this cycle counts */
#ifdef DEBUG
        printf("[FETCH] L2 hit: inst=0x%08x, fetch_stall=%d, PC=0x%08x\n",
               result.data, pipe.fetch_stall, pipe.PC);
#endif
        return;
    }

    /* Cache hit - proceed immediately */
    auto op = std::make_unique<Pipe_Op>();
    op->instruction = result.data;
    op->pc = pipe.PC;
    pipe.decode_op = std::move(op);

    /* update PC */
    uint32_t old_pc = pipe.PC;
    pipe.PC += 4;
    stat_inst_fetch++;
#ifdef DEBUG
    printf("[FETCH] Cache hit: inst=0x%08x, PC: 0x%08x -> 0x%08x\n",
           result.data, old_pc, pipe.PC);
#endif
}