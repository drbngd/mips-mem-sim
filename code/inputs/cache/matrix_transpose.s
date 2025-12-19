# Matrix Transpose Benchmark (Conflict Stress)
# Pattern: out[x*N + y] = in[y*N + x]
# 
# Read row-major (stride=4), Write column-major (stride=N*4)
# Tests: Associativity, Conflict Misses
# Expected: Low hit rate on writes due to large stride
#
# Matrix: 64x64 = 4096 words = 16KB per matrix

.text
    # Matrix dimensions
    addiu $s7, $zero, 64        # N = 64
    
    # Input matrix at 0x10000000
    lui $s0, 0x1000             # in_base
    
    # Output matrix at 0x10010000 (offset by 64KB to avoid overlap)
    lui $s1, 0x1001             # out_base
    
    # Outer loop: y = 0 to N-1
    addiu $s2, $zero, 0         # y = 0

outer_loop:
    # Inner loop: x = 0 to N-1
    addiu $s3, $zero, 0         # x = 0

inner_loop:
    # Calculate in[y*N + x]
    # in_addr = in_base + (y * N + x) * 4
    mult $s2, $s7               # y * N
    mflo $t0
    addu $t0, $t0, $s3          # y * N + x
    sll $t0, $t0, 2             # * 4 (bytes)
    addu $t0, $s0, $t0          # in_addr
    
    # Load in[y*N + x]
    lw $t4, 0($t0)
    
    # Calculate out[x*N + y]
    # out_addr = out_base + (x * N + y) * 4
    mult $s3, $s7               # x * N
    mflo $t1
    addu $t1, $t1, $s2          # x * N + y
    sll $t1, $t1, 2             # * 4 (bytes)
    addu $t1, $s1, $t1          # out_addr
    
    # Store out[x*N + y] = in[y*N + x]
    sw $t4, 0($t1)
    
    # x++
    addiu $s3, $s3, 1
    bne $s3, $s7, inner_loop
    
    # y++
    addiu $s2, $s2, 1
    bne $s2, $s7, outer_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

