# Strided Access Benchmark - 32 byte stride (1 cache line)
# Each access misses in a different cache line
# Tests spatial locality (none in this case)

.text
    lui $s0, 0x1000         # Base address
    
    # 8192 iterations * 32 byte stride = 256KB range
    addiu $s1, $zero, 8192
    move $t1, $s0
    
stride_loop:
    lw $t2, 0($t1)          # Load
    addiu $t1, $t1, 32      # Stride by 32 bytes (1 cache line)
    addiu $s1, $s1, -1
    bne $s1, $zero, stride_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

