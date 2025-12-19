# Strided Access Benchmark
# Access memory with different strides to test cache behavior
# Stride = 64 bytes (2 cache lines) - causes more misses

.text
    # Setup base address: 0x10000000
    lui $s0, 0x1000
    
    # Loop count: 1024 iterations
    addiu $s1, $zero, 1024
    
    move $t1, $s0           # $t1 = current address
    
stride_loop:
    # Load one word
    lw $t2, 0($t1)
    
    # Stride by 64 bytes (skip one cache line)
    addiu $t1, $t1, 64
    
    addiu $s1, $s1, -1
    bne $s1, $zero, stride_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

