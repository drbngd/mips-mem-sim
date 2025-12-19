# Mixed: Small hot set + periodic scans
# Access small working set, then scan large array
# LRU discards hot set after scan

.text
.globl main
main:
    # Hot set: 32 words (128 bytes) - fits in cache
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    # Scan array: 8192 words (32KB) - larger than cache
    lui $s1, 0x1000
    ori $s1, $s1, 0x10000
    
    addiu $s2, $zero, 20      # Repeat 20 times
    addiu $s3, $zero, 32      # Hot set size
    lui $s4, 0x8              # Scan size: 2048 words
    ori $s4, $s4, 0x0000
    
mixed_loop:
    # Phase 1: Access hot set repeatedly (recency-friendly)
    addiu $t0, $zero, 10      # Access hot set 10 times
hot_loop:
    move $t1, $s3
    move $t2, $s0
hot_access:
    lw $t3, 0($t2)
    addiu $t2, $t2, 4
    addiu $t1, $t1, -1
    bne $t1, $zero, hot_access
    
    addiu $t0, $t0, -1
    bne $t0, $zero, hot_loop
    
    # Phase 2: Scan large array (streaming, pollutes cache)
    move $t1, $s4
    move $t2, $s1
scan_loop:
    lw $t3, 0($t2)
    addiu $t2, $t2, 4
    addiu $t1, $t1, -1
    bne $t1, $zero, scan_loop
    
    # Reset scan pointer
    lui $s1, 0x1000
    ori $s1, $s1, 0x10000
    
    addiu $s2, $s2, -1
    bne $s2, $zero, mixed_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

