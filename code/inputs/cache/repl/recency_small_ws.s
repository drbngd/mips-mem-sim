# Recency-friendly: Very small working set
# Tiny working set accessed repeatedly
# LRU should be optimal

.text
.globl main
main:
    # Tiny working set: 4 words (16 bytes)
    # Fits easily in any cache
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    addiu $s1, $zero, 500    # Many iterations
    
small_ws_loop:
    # Access same 4 addresses repeatedly
    lw $t0, 0($s0)
    lw $t1, 4($s0)
    lw $t2, 8($s0)
    lw $t3, 12($s0)
    
    # Re-access immediately
    lw $t0, 0($s0)
    lw $t1, 4($s0)
    lw $t2, 8($s0)
    lw $t3, 12($s0)
    
    addiu $s1, $s1, -1
    bne $s1, $zero, small_ws_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

