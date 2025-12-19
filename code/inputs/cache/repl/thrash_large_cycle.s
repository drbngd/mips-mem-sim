# Thrashing: Very large cyclic pattern
# Cycle size much larger than cache, severe thrashing

.text
.globl main
main:
    # Large cycle: 2048 words (8KB)
    # Larger than typical cache
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    addiu $s1, $zero, 30     # Repeat cycle 30 times
    addiu $s2, $zero, 2048   # Cycle length: 2048 words
    
large_cycle_loop:
    move $t0, $s2
    move $t1, $s0
    
    # Access all addresses in cycle
cycle_access:
    lw $t2, 0($t1)
    addiu $t1, $t1, 4
    addiu $t0, $t0, -1
    bne $t0, $zero, cycle_access
    
    addiu $s1, $s1, -1
    bne $s1, $zero, large_cycle_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

