# Thrashing: Multiple competing streams
# Access multiple arrays that conflict in cache sets
# Causes thrashing when working sets compete

.text
.globl main
main:
    # Three competing arrays, each 256 words (1KB)
    # They map to same cache sets, causing conflicts
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000      # Array A
    lui $s1, 0x1000
    ori $s1, $s1, 0xC000      # Array B (offset by cache size)
    lui $s2, 0x1000
    ori $s2, $s2, 0x10000      # Array C
    
    addiu $s3, $zero, 100     # Iterations
    addiu $s4, $zero, 256      # Array size
    
compete_loop:
    # Access all three arrays in round-robin
    # This causes constant evictions
    move $t0, $s4
    move $t1, $s0
array_a:
    lw $t2, 0($t1)
    addiu $t1, $t1, 4
    addiu $t0, $t0, -1
    bne $t0, $zero, array_a
    
    move $t0, $s4
    move $t1, $s1
array_b:
    lw $t2, 0($t1)
    addiu $t1, $t1, 4
    addiu $t0, $t0, -1
    bne $t0, $zero, array_b
    
    move $t0, $s4
    move $t1, $s2
array_c:
    lw $t2, 0($t1)
    addiu $t1, $t1, 4
    addiu $t0, $t0, -1
    bne $t0, $zero, array_c
    
    addiu $s3, $s3, -1
    bne $s3, $zero, compete_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

