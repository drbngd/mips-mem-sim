# Thrashing: Conflict-based thrashing
# Access pattern that causes set conflicts
# Stride access that maps to same sets

.text
.globl main
main:
    # Access with stride that causes conflicts
    # Stride = cache_size / associativity to maximize conflicts
    # For 512KB cache, 8-way: stride = 64KB = 16384 words
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    addiu $s1, $zero, 500     # Many iterations
    lui $s2, 4                # Stride: 16384 words (0x10000 bytes)
    ori $s2, $s2, 0x0000
    
conflict_loop:
    # Access addresses with large stride
    # All map to same cache sets
    lw $t0, 0($s0)
    addu $s0, $s0, $s2
    lw $t0, 0($s0)
    addu $s0, $s0, $s2
    lw $t0, 0($s0)
    addu $s0, $s0, $s2
    lw $t0, 0($s0)
    addu $s0, $s0, $s2
    
    # Reset to base
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    addiu $s1, $s1, -1
    bne $s1, $zero, conflict_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

