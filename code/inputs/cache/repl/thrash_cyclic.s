# Thrashing: Cyclic access pattern larger than cache
# Access k addresses in cycle where k > cache size
# LRU gets zero hits, better policies should preserve some

.text
.globl main
main:
    # Cycle length: 512 words (2048 bytes = 2KB)
    # This is larger than typical cache, causing thrashing
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    addiu $s1, $zero, 50     # Repeat cycle 50 times
    addiu $s2, $zero, 512    # Cycle length: 512 words
    
cycle_loop:
    move $t0, $s2
    move $t1, $s0
    
    # Access addresses in cycle
    # Each iteration accesses all 2048 words sequentially
access_cycle:
    lw $t2, 0($t1)
    addiu $t1, $t1, 4
    addiu $t0, $t0, -1
    bne $t0, $zero, access_cycle
    
    addiu $s1, $s1, -1
    bne $s1, $zero, cycle_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall
