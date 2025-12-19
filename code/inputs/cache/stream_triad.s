# STREAM Triad Benchmark (Bandwidth Stress)
# Pattern: a[i] = b[i] + scalar * c[i]
# 
# Sequential access through 3 arrays
# Tests: Block filling, Write-Allocate, Spatial Locality
# Expected: High hit rate (~87-95%) after initial misses
#
# Arrays: 16KB each (4096 words), total 48KB data

.text
    # Array base addresses (0x10000000, 0x10004000, 0x10008000)
    lui $s0, 0x1000             # A base = 0x10000000
    lui $s1, 0x1000
    ori $s1, $s1, 0x4000        # B base = 0x10004000
    lui $s2, 0x1000
    ori $s2, $s2, 0x8000        # C base = 0x10008000
    
    # Scalar multiplier
    addiu $s3, $zero, 3         # scalar = 3
    
    # Loop count: 4096 elements
    lui $s4, 0
    ori $s4, $s4, 4096
    
    # Initialize pointers
    move $t0, $s0               # a_ptr
    move $t1, $s1               # b_ptr
    move $t2, $s2               # c_ptr

triad_loop:
    # Load b[i] and c[i]
    lw $t3, 0($t1)              # t3 = b[i]
    lw $t4, 0($t2)              # t4 = c[i]
    
    # Compute scalar * c[i]
    mult $s3, $t4
    mflo $t5                    # t5 = scalar * c[i]
    
    # a[i] = b[i] + scalar * c[i]
    addu $t6, $t3, $t5
    sw $t6, 0($t0)              # store a[i]
    
    # Advance pointers
    addiu $t0, $t0, 4
    addiu $t1, $t1, 4
    addiu $t2, $t2, 4
    
    # Loop control
    addiu $s4, $s4, -1
    bne $s4, $zero, triad_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

