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
#include "core.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <memory>
#include <array>

//#define DEBUG

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

Pipeline::Pipeline(Core* c) : core(c), HI(0), LO(0), PC(0x00400000), 
                             branch_recover(0), branch_dest(0), branch_flush(0),
                             multiplier_stall(0)
{
    REGS.fill(0);
}



void Pipeline::recover(int flush, uint32_t dest)
{
    /* if there is already a recovery scheduled, it must have come from a later
     * stage (which executes older instructions), hence that recovery overrides
     * our recovery. Simply return in this case. */
    if (branch_recover) return;

    /* schedule the recovery. This will be done once all pipeline stages simulate the current cycle. */
    branch_recover = 1;
    branch_flush = flush;
    branch_dest = dest;
}

void Pipeline::wb()
{
    /* if there is no instruction in this pipeline stage, we are done */
    if (!wb_op)
        return;

    /* grab the op out of our input slot */
    Pipe_Op *op = wb_op.get();

    /* if this instruction writes a register, do so now */
    if (op->reg_dst != -1 && op->reg_dst != 0) {
        REGS[op->reg_dst] = op->reg_dst_value;
#ifdef DEBUG
        printf("[Core %d] R%d = %08x\n", core->id, op->reg_dst, op->reg_dst_value);
#endif
    }

    /* internal: if this was a syscall, notify the core */
    if (op->opcode == OP_SPECIAL && op->subop == SUBOP_SYSCALL) {
        core->handle_syscall(op);
    }

    /* free the op */
    wb_op.reset();

    stat_inst_retire++;
}

void Pipeline::mem()
{
    /* if there is no instruction in this pipeline stage, we are done */
    if (!mem_op)
        return;

    /* grab the op out of our input slot */
    Pipe_Op *op = mem_op.get();

    /* Access D-Cache if this is a memory operation */
    if (op->is_mem) {
        bool is_write = op->mem_write; // You might need to verify if mem_write is set correctly in decode for all ops, but looking at decode() it seems so.
        // wait, op->mem_write is set in decode for stores. For loads it is 0.
        // Let's verify decode logic quickly in my head (or look at file). 
        // Yes, `op->mem_write = 1` for stores, `0` for loads.
        
        if (!core->dcache.access(op->mem_addr, op->mem_write, true))
            return;
    }

    uint32_t val = 0;
    if (op->is_mem)
        val = mem_read_32(op->mem_addr & ~3);

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

            mem_write_32(op->mem_addr & ~3, val);
            break;

        case OP_SH:
#ifdef DEBUG
            printf("[Core %d] SH: addr %08x val %04x old word %08x\n", core->id, op->mem_addr, op->mem_value & 0xFFFF, val);
#endif
            if (op->mem_addr & 2)
                val = (val & 0x0000FFFF) | (op->mem_value) << 16;
            else
                val = (val & 0xFFFF0000) | (op->mem_value & 0xFFFF);
#ifdef DEBUG
            printf("[Core %d] new word %08x\n", core->id, val);
#endif

            mem_write_32(op->mem_addr & ~3, val);
            break;

        case OP_SW:
            val = op->mem_value;
            mem_write_32(op->mem_addr & ~3, val);
            break;
    }

    /* clear stage input and transfer to next stage */
    wb_op = std::move(mem_op);
}

void Pipeline::execute()
{
    /* if a multiply/divide is in progress, decrement cycles until value is ready */
    if (multiplier_stall > 0)
        multiplier_stall--;

    /* if downstream stall, return (and leave any input we had) */
    if (mem_op)
        return;

    /* if no op to execute, return */
    if (!execute_op)
        return;

    /* grab op and read sources */
    Pipe_Op *op = execute_op.get();

    /* read register values, and check for bypass; stall if necessary */
    int stall = 0;
    if (op->reg_src1 != -1) {
        if (op->reg_src1 == 0)
            op->reg_src1_value = 0;
        else if (mem_op && mem_op->reg_dst == op->reg_src1) {
            if (!mem_op->reg_dst_value_ready)
                stall = 1;
            else
                op->reg_src1_value = mem_op->reg_dst_value;
        }
        else if (wb_op && wb_op->reg_dst == op->reg_src1) {
            op->reg_src1_value = wb_op->reg_dst_value;
        }
        else
            op->reg_src1_value = REGS[op->reg_src1];
    }
    if (op->reg_src2 != -1) {
        if (op->reg_src2 == 0)
            op->reg_src2_value = 0;
        else if (mem_op && mem_op->reg_dst == op->reg_src2) {
            if (!mem_op->reg_dst_value_ready)
                stall = 1;
            else
                op->reg_src2_value = mem_op->reg_dst_value;
        }
        else if (wb_op && wb_op->reg_dst == op->reg_src2) {
            op->reg_src2_value = wb_op->reg_dst_value;
        }
        else
            op->reg_src2_value = REGS[op->reg_src2];
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
                        HI = (uval >> 32) & 0xFFFFFFFF;
                        LO = (uval >>  0) & 0xFFFFFFFF;

                        /* four-cycle multiplier latency */
                        multiplier_stall = 4;
                    }
                    break;
                case SUBOP_MULTU:
                    {
                        uint64_t val = (uint64_t)op->reg_src1_value * (uint64_t)op->reg_src2_value;
                        HI = (val >> 32) & 0xFFFFFFFF;
                        LO = (val >>  0) & 0xFFFFFFFF;

                        /* four-cycle multiplier latency */
                        multiplier_stall = 4;
                    }
                    break;

                case SUBOP_DIV:
                    if (op->reg_src2_value != 0) {

                        int32_t val1 = (int32_t)op->reg_src1_value;
                        int32_t val2 = (int32_t)op->reg_src2_value;
                        int32_t div, mod;

                        div = val1 / val2;
                        mod = val1 % val2;

                        LO = div;
                        HI = mod;
                    } else {
                        // really this would be a div-by-0 exception
                        HI = LO = 0;
                    }

                    /* 32-cycle divider latency */
                    multiplier_stall = 32;
                    break;

                case SUBOP_DIVU:
                    if (op->reg_src2_value != 0) {
                        HI = (uint32_t)op->reg_src1_value % (uint32_t)op->reg_src2_value;
                        LO = (uint32_t)op->reg_src1_value / (uint32_t)op->reg_src2_value;
                    } else {
                        /* really this would be a div-by-0 exception */
                        HI = LO = 0;
                    }

                    /* 32-cycle divider latency */
                    multiplier_stall = 32;
                    break;

                case SUBOP_MFHI:
                    /* stall until value is ready */
                    if (multiplier_stall > 0)
                        return;

                    op->reg_dst_value = HI;
                    break;
                case SUBOP_MTHI:
                    /* stall to respect WAW dependence */
                    if (multiplier_stall > 0)
                        return;

                    HI = op->reg_src1_value;
                    break;

                case SUBOP_MFLO:
                    /* stall until value is ready */
                    if (multiplier_stall > 0)
                        return;

                    op->reg_dst_value = LO;
                    break;
                case SUBOP_MTLO:
                    /* stall to respect WAW dependence */
                    if (multiplier_stall > 0)
                        return;

                    LO = op->reg_src1_value;
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
        recover(3, op->branch_dest);

    /* remove from upstream stage and place in downstream stage */
    mem_op = std::move(execute_op);
}

void Pipeline::decode()
{
    /* if downstream stall, return (and leave any input we had) */
    if (execute_op)
        return;

    /* if no op to decode, return */
    if (!decode_op)
        return;

    /* Check for SYSCALL serialization */
    /* 1. If we are entering Decode, check if any SYSCALL is currently in Execute, Memory, or Writeback */
    if ((execute_op && execute_op->opcode == OP_SPECIAL && execute_op->subop == SUBOP_SYSCALL) ||
        (mem_op && mem_op->opcode == OP_SPECIAL && mem_op->subop == SUBOP_SYSCALL) ||
        (wb_op && wb_op->opcode == OP_SPECIAL && wb_op->subop == SUBOP_SYSCALL)) {
        return; // Stall
    }

    /* 2. If the current instruction is a SYSCALL, check if Execute, Memory, or Writeback are occupied */
    if (decode_op->instruction == 0x0000000C) { 
        // Decoding opcode/func to confirm SYSCALL
        uint32_t opc = (decode_op->instruction >> 26) & 0x3F;
        uint32_t fun = decode_op->instruction & 0x3F;
        if (opc == OP_SPECIAL && fun == SUBOP_SYSCALL) {
             // If pipeline is not empty downstream, STALL.
             if (execute_op || mem_op || wb_op) {
                 return; 
             }
        }
    }

    /* grab op and remove from stage input */
    Pipe_Op *op = decode_op.get();

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
    execute_op = std::move(decode_op);
}

void Pipeline::fetch()
{
    /* if pipeline is stalled (our output slot is not empty), return */
    if (decode_op)
        return;

    /* Check I-Cache */
    if (!core->icache.access(PC, false, false))
        return;

    /* Allocate an op and send it down the pipeline. */
    auto op = std::make_unique<Pipe_Op>();

    op->instruction = mem_read_32(PC);
    op->pc = PC;
    decode_op = std::move(op);

    /* update PC */
    PC += 4;

    stat_inst_fetch++;
}
