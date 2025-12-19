# Mixed: Working set + occasional large array access
# Small working set accessed frequently, then large scan
# LRU loses working set after scan

.text
.globl main
main:
    # Working set: 128 words (512 bytes)
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    # Large array: 4096 words (16KB)
    lui $s1, 0x1000
    ori $s1, $s1, 0x20000
    
    addiu $s2, $zero, 15      # Repeat 15 times
    addiu $s3, $zero, 128    # Working set size
    lui $s4, 0x10            # Scan size: 4096 words
    ori $s4, $s4, 0x0000
    
work_loop:
    # Phase 1: Access working set many times
    addiu $t0, $zero, 20     # Access 20 times
work_access_loop:
    move $t1, $s3
    move $t2, $s0
work_inner:
    lw $t3, 0($t2)
    addiu $t2, $t2, 4
    addiu $t1, $t1, -1
    bne $t1, $zero, work_inner
    
    addiu $t0, $t0, -1
    bne $t0, $zero, work_access_loop
    
    # Phase 2: Scan large array
    move $t1, $s4
    move $t2, $s1
scan_loop:
    lw $t3, 0($t2)
    addiu $t2, $t2, 4
    addiu $t1, $t1, -1
    bne $t1, $zero, scan_loop
    
    # Reset scan pointer
    lui $s1, 0x1000
    ori $s1, $s1, 0x20000
    
    addiu $s2, $s2, -1
    bne $s2, $zero, work_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

