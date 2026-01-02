/*
 * Computer Architecture - Professor Onur Mutlu
 *
 * MIPS pipeline timing simulator
 *
 * Chris Fallin, 2012
 */


#include "core.h"
#include "processor.h"
#include "shell.h"
#include "config.h"
#include <cstdio>
#include <cstring>

Core::Core(int id, Processor* p, L2Cache* l2) 
    : id(id), proc(p), is_running(false),
      icache(id, l2, this, L1_I_SETS, L1_I_ASSOC), 
      dcache(id, l2, this, L1_D_SETS, L1_D_ASSOC)
{
    pipe = std::make_unique<Pipeline>(this);
    
    /* CPU 0 starts running by default */
    if (id == 0) is_running = true;
}

void Core::cycle() {
    if (!is_running) return;

#ifdef DEBUG
    printf("\n\n----\n\n[Core %d] PIPELINE:\n", id);
    printf("DCODE: "); print_op(pipe->decode_op.get());
    printf("EXEC : "); print_op(pipe->execute_op.get());
    printf("MEM  : "); print_op(pipe->mem_op.get());
    printf("WB   : "); print_op(pipe->wb_op.get());
    printf("\n");
#endif

    /* Execute pipeline stages in reverse order to handle stalls/forwarding correctly */
    pipe->wb();
    pipe->mem();
    pipe->execute();
    pipe->decode();
        
    if (is_running)
        pipe->fetch();

    /* handle branch recoveries */
    if (pipe->branch_recover) {
#ifdef DEBUG
        printf("[Core %d] branch recovery: new dest %08x flush %d stages\n", id, pipe->branch_dest, pipe->branch_flush);
#endif

        pipe->PC = pipe->branch_dest;

        if (pipe->branch_flush >= 2) {
            pipe->decode_op.reset();
        }

        if (pipe->branch_flush >= 3) {
            pipe->execute_op.reset();
        }

        if (pipe->branch_flush >= 4) {
            pipe->mem_op.reset();
        }

        if (pipe->branch_flush >= 5) {
            pipe->wb_op.reset();
        }

        pipe->branch_recover = 0;
        pipe->branch_dest = 0;
        pipe->branch_flush = 0;

        stat_squash++;
    }
}

void Core::handle_syscall(Pipe_Op* op) {
    uint32_t v0 = op->reg_src1_value; 
    uint32_t v1 = op->reg_src2_value; 

    if (v0 == 0xA) {
        /* Syscall 10: Halt current CPU */
        pipe->PC = op->pc + 4; /* fetch will do pc += 4, then we stop with correct PC */
        is_running = false;
    }
    else if (v0 == 0xB) {
        /* Syscall 11: Print output */
        printf("OUT (CPU %d): %08x\n", id, v1);
    }
    else if (v0 >= 1 && v0 <= 3) {
        /* Syscall 1, 2, 3: Spawn thread on CPU $v0 */
        int target_id = (int)v0;
        
        if (target_id >= 0 && target_id < NUM_CORES && target_id != id) {
             Core* target = proc->cores[target_id].get();
             
             if (!target->is_running) {
#ifdef DEBUG
                  printf("Spawning thread on Core %d from Core %d\n", target_id, id);
#endif
                  target->pipe->PC = op->pc + 4;
                  target->pipe->REGS[3] = 1; /* $v1 = 1 for child */
                  target->is_running = true;
                  pipe->REGS[3] = 0; /* $v1 = 0 for parent */
             }
        }
    }
}
