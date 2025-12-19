# Working Set Benchmark - 1GB
# Repeatedly access a 1GB working set (16000× D-cache size)
# 
# Tests: Extreme capacity stress, maximum thrashing
# Expected: Near 100% miss rate, cache completely useless
#
# NOTE: Requires modifying MEM_DATA_SIZE in shell.cpp to at least 0x40000000 (1GB)
# Change: #define MEM_DATA_SIZE   0x40000000
#
# Access pattern: 65536 accesses with stride 16384 bytes = spans 1GB
# 65536 × 16384 = 1,073,741,824 bytes = 1GB

.text
    # Base address
    lui $s0, 0x1000             # base = 0x10000000
    
    # Outer loop: 4 passes
    addiu $s4, $zero, 4

pass_loop:
    # Access 65536 words with stride 16384 bytes = spans 1GB
    # Use lui to load 65536 (0x10000) - can't use ori/addiu for values ≥ 65536
    lui $s1, 1                  # 0x00010000 = 65536 ✓
    move $t0, $s0
    
    # Stride = 16384 bytes (16KB)
    # 16384 = 0x4000, which is < 32768, so ori is safe
    lui $s5, 0
    ori $s5, $s5, 16384         # stride = 16KB (16384 bytes)

access_loop:
    # Read-modify-write pattern (realistic workload)
    lw $t1, 0($t0)
    addiu $t1, $t1, 1
    sw $t1, 0($t0)
    
    # Next access (stride 16KB)
    addu $t0, $t0, $s5
    addiu $s1, $s1, -1
    bne $s1, $zero, access_loop
    
    # Next pass
    addiu $s4, $s4, -1
    bne $s4, $zero, pass_loop
    
    # Exit
    addiu $v0, $zero, 10
    syscall

