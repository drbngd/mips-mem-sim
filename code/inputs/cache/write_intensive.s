# Write Intensive Benchmark
# Repeatedly write to a 64KB array
# Tests write-back cache behavior

.text
    lui $s0, 0x1000         # Base address
    
    # 3 passes over 16K words (64KB)
    addiu $s3, $zero, 3
    
pass_loop:
    addiu $s1, $zero, 16384 # 64KB = 16384 words
    move $t1, $s0
    move $t2, $s3           # Use pass number as write value
    
write_loop:
    sw $t2, 0($t1)          # Write
    addiu $t1, $t1, 4       # Next word
    addiu $s1, $s1, -1
    bne $s1, $zero, write_loop
    
    addiu $s3, $s3, -1
    bne $s3, $zero, pass_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

