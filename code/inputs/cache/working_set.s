# Working Set Benchmark
# Repeatedly access a working set larger than the cache
# This tests replacement policy effectiveness
# Working set: 128KB (larger than 64KB D-cache)

.text
    # Setup base address
    lui $s0, 0x1000
    
    # Outer loop: repeat traversal 4 times
    addiu $s2, $zero, 4
    
repeat_loop:
    # Inner loop: traverse 32K words = 128KB
    # Since D-cache is 64KB, this will cause thrashing
    # 32768 = 0x8000, but addiu sign-extends, so use lui+ori
    lui $s1, 0
    ori $s1, $s1, 32768         # 32768 = 0x8000
    move $t1, $s0
    
traverse_loop:
    lw $t2, 0($t1)
    addiu $t2, $t2, 1       # Modify value
    sw $t2, 0($t1)          # Write back
    
    addiu $t1, $t1, 4       # Next word
    addiu $s1, $s1, -1
    bne $s1, $zero, traverse_loop
    
    addiu $s2, $s2, -1
    bne $s2, $zero, repeat_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

