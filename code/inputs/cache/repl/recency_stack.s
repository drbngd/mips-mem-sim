# Recency-friendly: Stack-like access pattern
# Repeatedly access a small working set (stack operations)
# LRU should perform well here

.text
.globl main
main:
    # Initialize stack pointer
    lui $sp, 0x1000
    ori $sp, $sp, 0x8000
    
    # Small working set: 8 words (32 bytes) - fits in cache
    # We'll repeatedly push/pop from this stack
    addiu $t0, $zero, 100     # Repeat 100 times
    
stack_loop:
    # Push sequence: access addresses in order
    sw $t0, 0($sp)
    sw $t0, 4($sp)
    sw $t0, 8($sp)
    sw $t0, 12($sp)
    sw $t0, 16($sp)
    sw $t0, 20($sp)
    sw $t0, 24($sp)
    sw $t0, 28($sp)
    
    # Pop sequence: access in reverse (immediate re-reference)
    lw $t1, 28($sp)
    lw $t1, 24($sp)
    lw $t1, 20($sp)
    lw $t1, 16($sp)
    lw $t1, 12($sp)
    lw $t1, 8($sp)
    lw $t1, 4($sp)
    lw $t1, 0($sp)
    
    addiu $t0, $t0, -1
    bne $t0, $zero, stack_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

