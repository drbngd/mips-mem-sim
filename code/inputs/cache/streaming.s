# Streaming Access Benchmark
# Sequential read through a large array
# This tests spatial locality and cache line utilization
# Array size: 16KB (4096 words) - larger than D-cache (64KB) but uses many sets

.text
    # Setup base address: 0x10000000 (data segment)
    lui $s0, 0x1000         # $s0 = base address
    
    # Loop count: 2048 iterations (access 8192 words = 32KB)
    # This will cause many cache misses to test streaming behavior
    addiu $s1, $zero, 2048  # $s1 = outer loop count
    
outer_loop:
    addiu $t0, $zero, 4     # $t0 = inner loop count (4 words per cache line)
    move $t1, $s0           # $t1 = current address
    
inner_loop:
    # Load 4 consecutive words (one cache line worth)
    lw $t2, 0($t1)
    lw $t3, 4($t1)
    lw $t4, 8($t1)
    lw $t5, 12($t1)
    
    # Move to next cache line (32 bytes = 8 words)
    addiu $t1, $t1, 32
    
    addiu $t0, $t0, -1
    bne $t0, $zero, inner_loop
    
    # Increment outer loop
    addiu $s1, $s1, -1
    bne $s1, $zero, outer_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

