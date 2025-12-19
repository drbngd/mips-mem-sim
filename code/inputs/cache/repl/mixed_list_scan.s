# Mixed: Linked list traversal with scans
# Access linked list nodes, then scan large array
# Simulates list operations with periodic searches

.text
.globl main
main:
    # Linked list: 64 nodes (256 bytes) - fits in cache
    lui $s0, 0x1000
    ori $s0, $s0, 0x8000
    
    # Scan array: 4096 words (16KB)
    lui $s1, 0x1000
    ori $s1, $s1, 0x10000
    
    addiu $s2, $zero, 30      # Repeat 30 times
    addiu $s3, $zero, 64      # List size
    lui $s4, 0x8              # Scan size: 2048 words
    ori $s4, $s4, 0x0000
    
list_loop:
    # Phase 1: Traverse linked list (recency-friendly)
    addiu $t0, $zero, 5       # Traverse list 5 times
traverse_loop:
    move $t1, $s3
    move $t2, $s0
list_access:
    lw $t3, 0($t2)           # Read node data
    lw $t4, 4($t2)           # Read next pointer (simulated)
    addiu $t2, $t2, 8        # Next node
    addiu $t1, $t1, -1
    bne $t1, $zero, list_access
    
    addiu $t0, $t0, -1
    bne $t0, $zero, traverse_loop
    
    # Phase 2: Scan array (pollutes cache)
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
    bne $s2, $zero, list_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall
