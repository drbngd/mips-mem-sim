# Streaming: Sequential read through large array
# No re-reference, infinite re-reference interval
# No cache hits regardless of policy

.text
.globl main
main:
    # Large array: 4096 words (16KB)
    # Sequential read, no re-reference
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    lui $s1, 0x10             # 4096 words
    ori $s1, $s1, 0x0000
    
stream_read:
    lw $t0, 0($s0)
    addiu $s0, $s0, 4
    addiu $s1, $s1, -1
    bne $s1, $zero, stream_read
    
    # Exit
    addiu $v0, $zero, 10
    syscall

