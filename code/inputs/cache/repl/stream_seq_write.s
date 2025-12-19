# Streaming: Sequential write through large array
# Write-allocate causes misses, no re-reference

.text
.globl main
main:
    # Large array: 4096 words (16KB)
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    lui $s1, 0x10             # 4096 words
    ori $s1, $s1, 0x0000
    addiu $t1, $zero, 1       # Value to write
    
stream_write:
    sw $t1, 0($s0)
    addiu $s0, $s0, 4
    addiu $s1, $s1, -1
    bne $s1, $zero, stream_write
    
    # Exit
    addiu $v0, $zero, 10
    syscall

