# Stencil Benchmark (Spatial Locality Stress)
# Pattern: out[i] = (in[i-1] + in[i] + in[i+1]) / 3
# 
# 1D blur/smoothing - accesses 3 consecutive elements per output
# Tests: Spatial Locality, Data Reuse within cache line
# Expected: Very high hit rate due to adjacent accesses
#
# Arrays: 16KB each

.text
    # Input array at 0x10000000
    lui $s0, 0x1000             # in_base
    
    # Output array at 0x10004000
    lui $s1, 0x1000
    ori $s1, $s1, 0x4000        # out_base
    
    # Loop count: 4094 elements (skip first and last)
    lui $s2, 0
    ori $s2, $s2, 4094
    
    # Start at in[1], out[1]
    addiu $t0, $s0, 4           # in_ptr = in_base + 4
    addiu $t1, $s1, 4           # out_ptr = out_base + 4
    
    # Divisor for average
    addiu $s3, $zero, 3

stencil_loop:
    # Load in[i-1], in[i], in[i+1]
    lw $t2, -4($t0)             # in[i-1]
    lw $t3, 0($t0)              # in[i]
    lw $t4, 4($t0)              # in[i+1]
    
    # Sum
    addu $t5, $t2, $t3
    addu $t5, $t5, $t4          # sum = in[i-1] + in[i] + in[i+1]
    
    # Divide by 3 (integer division)
    div $t5, $s3
    mflo $t6                    # avg = sum / 3
    
    # Store result
    sw $t6, 0($t1)              # out[i] = avg
    
    # Advance pointers
    addiu $t0, $t0, 4
    addiu $t1, $t1, 4
    
    # Loop control
    addiu $s2, $s2, -1
    bne $s2, $zero, stencil_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

