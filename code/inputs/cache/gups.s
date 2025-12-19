# GUPS Benchmark (Random Updates - Latency Stress)
# Pattern: table[rand() & mask] ^= rand()
# 
# Random updates to a large table
# Tests: Random Access Pattern, Miss Penalty
# Expected: Very low hit rate, worst case for caching
#
# Table: 64KB (16384 words), mask = 0x3FFF

.text
    # Table base
    lui $s0, 0x1000             # base = 0x10000000
    
    # Table size mask: 16384-1 = 0x3FFF
    lui $s1, 0
    ori $s1, $s1, 0x3FFF        # mask
    
    # Number of updates: 16384
    lui $s2, 0
    ori $s2, $s2, 16384
    
    # LFSR state (for random number generation)
    lui $s3, 0xDEAD
    ori $s3, $s3, 0xBEEF        # seed
    
    # LFSR polynomial
    lui $s4, 0
    ori $s4, $s4, 0xB400

update_loop:
    # Generate random index using LFSR
    andi $t0, $s3, 1            # LSB
    srl $t1, $s3, 1             # shift right
    beq $t0, $zero, skip_xor1
    xor $t1, $t1, $s4           # XOR with polynomial
skip_xor1:
    move $s3, $t1               # update LFSR
    
    # Calculate address: base + (rand & mask) * 4
    and $t2, $s3, $s1           # index = rand & mask
    sll $t2, $t2, 2             # byte offset
    addu $t3, $s0, $t2          # address
    
    # Generate another random value for XOR
    andi $t0, $s3, 1
    srl $t1, $s3, 1
    beq $t0, $zero, skip_xor2
    xor $t1, $t1, $s4
skip_xor2:
    move $s3, $t1
    
    # table[index] ^= rand
    lw $t4, 0($t3)              # load
    xor $t4, $t4, $s3           # XOR with random
    sw $t4, 0($t3)              # store
    
    # Loop control
    addiu $s2, $s2, -1
    bne $s2, $zero, update_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

