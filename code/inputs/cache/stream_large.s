# Large Streaming Access Benchmark
# Sequential read through a 256KB array (larger than D-cache)
# This tests cache miss handling for streaming workloads

.text
    lui $s0, 0x1000         # $s0 = base address (0x10000000)
    
    # Loop count: 65536 iterations (access 256KB)
    # D-cache is 64KB, so this will cause many misses
    lui $s1, 1              # $s1 = 0x10000 = 65536
    
    move $t1, $s0           # $t1 = current address
    
stream_loop:
    lw $t2, 0($t1)          # Load word
    addiu $t1, $t1, 4       # Next word
    addiu $s1, $s1, -1
    bne $s1, $zero, stream_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

