# Recency-friendly: Nested loops with same data
# Small working set accessed repeatedly in nested pattern
# LRU should excel here

.text
.globl main
main:
    # Small working set: 16 words (64 bytes)
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    addiu $s1, $zero, 20     # Outer loop: 20 iterations
    addiu $s2, $zero, 10     # Inner loop: 10 iterations
    
outer_loop:
    move $t0, $s2
    
inner_loop:
    # Access same 16 addresses repeatedly
    sw $t0, 0($s0)
    sw $t0, 4($s0)
    sw $t0, 8($s0)
    sw $t0, 12($s0)
    sw $t0, 16($s0)
    sw $t0, 20($s0)
    sw $t0, 24($s0)
    sw $t0, 28($s0)
    sw $t0, 32($s0)
    sw $t0, 36($s0)
    sw $t0, 40($s0)
    sw $t0, 44($s0)
    sw $t0, 48($s0)
    sw $t0, 52($s0)
    sw $t0, 56($s0)
    sw $t0, 60($s0)
    
    addiu $t0, $t0, -1
    bne $t0, $zero, inner_loop
    
    addiu $s1, $s1, -1
    bne $s1, $zero, outer_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

