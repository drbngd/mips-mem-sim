# Strided Access Benchmark - 64 byte stride (2 cache lines)
# Each access skips a cache line
# Tests larger stride patterns

.text
    lui $s0, 0x1000         # Base address
    
    # 4096 iterations * 64 byte stride = 256KB range  
    addiu $s1, $zero, 4096
    move $t1, $s0
    
stride_loop:
    lw $t2, 0($t1)          # Load
    addiu $t1, $t1, 64      # Stride by 64 bytes (2 cache lines)
    addiu $s1, $s1, -1
    bne $s1, $zero, stride_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

