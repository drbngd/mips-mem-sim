# Pointer Chase Benchmark (Latency Stress)
# Pattern: idx = memory[idx] (pseudo-random jumps)
# 
# Uses LFSR to generate pseudo-random indices
# Tests: Pipeline Stalls, Miss Penalty, Zero Locality
# Expected: Very low hit rate (~0%), exposes raw latency
#
# Phase 1: Initialize array with LFSR-generated "next" pointers
# Phase 2: Chase the pointers

.text
    # Base address for array
    lui $s0, 0x1000             # base = 0x10000000
    
    # Array size: 4096 entries (16KB), mask = 0xFFF
    lui $s1, 0
    ori $s1, $s1, 4096          # N = 4096
    addiu $s2, $s1, -1          # mask = N-1 = 0xFFF
    
    # ===== Phase 1: Initialize with LFSR pattern =====
    # LFSR: x = (x >> 1) ^ (-(x & 1) & 0xB400)
    # This creates a pseudo-random permutation
    
    lui $t0, 0
    ori $t0, $t0, 0xACE1        # LFSR seed
    move $t1, $s0               # current address
    move $t2, $s1               # counter

init_loop:
    # Generate next LFSR value
    andi $t3, $t0, 1            # t3 = x & 1
    srl $t4, $t0, 1             # t4 = x >> 1
    beq $t3, $zero, no_xor
    lui $t5, 0
    ori $t5, $t5, 0xB400        # polynomial
    xor $t4, $t4, $t5
no_xor:
    move $t0, $t4               # x = new LFSR value
    
    # Store (LFSR & mask) * 4 as next index offset
    and $t3, $t0, $s2           # t3 = LFSR & mask (0 to N-1)
    sll $t3, $t3, 2             # t3 = index * 4 (byte offset)
    sw $t3, 0($t1)              # memory[i] = next offset
    
    addiu $t1, $t1, 4
    addiu $t2, $t2, -1
    bne $t2, $zero, init_loop
    
    # ===== Phase 2: Pointer Chase =====
    # 8192 chases (2x array size for good coverage)
    lui $s3, 0
    ori $s3, $s3, 8192          # chase count
    
    move $t0, $s0               # current = base (start at element 0)

chase_loop:
    # Load next offset from current position
    lw $t1, 0($t0)              # t1 = memory[current] = offset to next
    
    # Calculate next address: base + offset
    addu $t0, $s0, $t1          # current = base + offset
    
    # Loop
    addiu $s3, $s3, -1
    bne $s3, $zero, chase_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

