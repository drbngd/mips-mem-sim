# Matrix Column Access Benchmark
# Access a 256x256 matrix column-wise (bad for cache)
# Row size: 256 words = 1024 bytes
# This creates a stride that conflicts with cache sets

.text
    lui $s0, 0x1000         # Base address (matrix start)
    
    # Outer loop: 64 columns (reduced for faster runtime)
    addiu $s3, $zero, 64
    
col_loop:
    # Inner loop: 256 rows per column
    addiu $s1, $zero, 256
    move $t1, $s0           # Start of current column
    
row_loop:
    lw $t2, 0($t1)          # Load element
    addiu $t1, $t1, 1024    # Next row (256 words * 4 bytes)
    addiu $s1, $s1, -1
    bne $s1, $zero, row_loop
    
    # Move to next column
    addiu $s0, $s0, 4
    addiu $s3, $s3, -1
    bne $s3, $zero, col_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

