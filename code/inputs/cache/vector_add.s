# Vector Add Benchmark (Simple Bandwidth)
# Pattern: c[i] = a[i] + b[i]
# 
# Simplest streaming pattern - read 2, write 1
# Tests: Basic spatial locality, block filling
# Expected: High hit rate (~87-95%)
#
# Arrays: 16KB each (4096 words)

.text
    # Array base addresses
    lui $s0, 0x1000             # A base = 0x10000000
    lui $s1, 0x1000
    ori $s1, $s1, 0x4000        # B base = 0x10004000
    lui $s2, 0x1000
    ori $s2, $s2, 0x8000        # C base = 0x10008000
    
    # Loop count: 4096 elements
    lui $s3, 0
    ori $s3, $s3, 4096
    
    # Initialize pointers
    move $t0, $s0               # a_ptr
    move $t1, $s1               # b_ptr
    move $t2, $s2               # c_ptr

add_loop:
    # c[i] = a[i] + b[i]
    lw $t3, 0($t0)              # load a[i]
    lw $t4, 0($t1)              # load b[i]
    addu $t5, $t3, $t4          # a[i] + b[i]
    sw $t5, 0($t2)              # store c[i]
    
    # Advance pointers
    addiu $t0, $t0, 4
    addiu $t1, $t1, 4
    addiu $t2, $t2, 4
    
    # Loop control
    addiu $s3, $s3, -1
    bne $s3, $zero, add_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

