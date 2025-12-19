# Streaming: Large stride access
# Accesses are far apart, no temporal locality

.text
.globl main
main:
    # Large stride: 1024 words (4KB)
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    addiu $s1, $zero, 1024    # 1024 total accesses
    lui $s2, 0x4              # Stride: 1024 words
    ori $s2, $s2, 0x0000
    
stride_loop:
    lw $t0, 0($s0)
    addu $s0, $s0, $s2
    addiu $s1, $s1, -1
    bne $s1, $zero, stride_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

