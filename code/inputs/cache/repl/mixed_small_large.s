# Mixed: Small cache-friendly loop + large array scan
# Alternates between small working set and large scan

.text
.globl main
main:
    # Small working set: 16 words (64 bytes)
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    # Large scan array: 8192 words (32KB)
    lui $s1, 0x1000
    ori $s1, $s1, 0x10000
    
    addiu $s2, $zero, 50     # Repeat 50 times
    addiu $s3, $zero, 16     # Working set size
    lui $s4, 0x8             # Scan size: 2048 words
    ori $s4, $s4, 0x0000
    
alternate_loop:
    # Access small working set
    move $t0, $s3
    move $t1, $s0
small_access:
    lw $t2, 0($t1)
    addiu $t1, $t1, 4
    addiu $t0, $t0, -1
    bne $t0, $zero, small_access
    
    # Scan large array
    move $t0, $s4
    move $t1, $s1
large_scan:
    lw $t2, 0($t1)
    addiu $t1, $t1, 4
    addiu $t0, $t0, -1
    bne $t0, $zero, large_scan
    
    # Reset scan pointer
    lui $s1, 0x1000
    ori $s1, $s1, 0x10000
    
    addiu $s2, $s2, -1
    bne $s2, $zero, alternate_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall
