# Cache Stress Test
# Accesses 64KB of data in a streaming pattern
# This will show clear differences with varying cache line sizes
#
# Expected behavior:
# - Smaller cache line = more misses = more cycles
# - Larger cache line = fewer misses (better spatial locality) = fewer cycles

.text
    # Base address for data: 0x10000000
    lui $s0, 0x1000
    
    # Outer loop: 4 passes over the data
    addiu $s4, $zero, 4

pass_loop:
    # Inner loop: access 16384 words = 64KB
    # Each word is 4 bytes, so 16384 * 4 = 65536 bytes
    addiu $s1, $zero, 16384
    move $t0, $s0               # Reset pointer to start
    
access_loop:
    # Load a word (4 bytes)
    lw $t1, 0($t0)
    
    # Do some computation to make it realistic
    addiu $t1, $t1, 1
    
    # Store it back (creates dirty cache lines)
    sw $t1, 0($t0)
    
    # Move to next word (stride = 4 bytes)
    addiu $t0, $t0, 4
    
    # Decrement counter
    addiu $s1, $s1, -1
    bne $s1, $zero, access_loop
    
    # Next pass
    addiu $s4, $s4, -1
    bne $s4, $zero, pass_loop

    # Exit
    addiu $v0, $zero, 10
    syscall

