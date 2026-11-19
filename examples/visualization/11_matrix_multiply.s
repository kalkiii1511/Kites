# Kites visualization example: 3x3 integer matrix multiplication.
# Best with: RV5 · H / F or RV5 · H / NF. Nested loops, loads/stores, and mul/add chains.
# Expected C at 0x10000048: 30, 24, 18, 84, 69, 54, 138, 114, 90

.data
matrix_a: .word 1, 2, 3
          .word 4, 5, 6
          .word 7, 8, 9
matrix_b: .word 9, 8, 7
          .word 6, 5, 4
          .word 3, 2, 1
matrix_c: .word 0, 0, 0
          .word 0, 0, 0
          .word 0, 0, 0
matrix_size: .word 3

.text
_start:
    la   s0, matrix_a
    la   s1, matrix_b
    la   s2, matrix_c
    la   t0, matrix_size
    lw   s3, 0(t0)          # size = 3
    li   s4, 0              # i = 0

row_loop:
    bge  s4, s3, done
    li   s5, 0              # j = 0

col_loop:
    bge  s5, s3, next_row
    li   t0, 0              # sum = 0
    li   s6, 0              # k = 0

dot_loop:
    bge  s6, s3, store_c

    # A[i][k]
    mul  t1, s4, s3
    add  t1, t1, s6
    slli t1, t1, 2
    add  t1, s0, t1
    lw   t2, 0(t1)

    # B[k][j]
    mul  t1, s6, s3
    add  t1, t1, s5
    slli t1, t1, 2
    add  t1, s1, t1
    lw   t3, 0(t1)

    mul  t4, t2, t3
    add  t0, t0, t4
    addi s6, s6, 1
    j    dot_loop

store_c:
    mul  t1, s4, s3
    add  t1, t1, s5
    slli t1, t1, 2
    add  t1, s2, t1
    sw   t0, 0(t1)
    addi s5, s5, 1
    j    col_loop

next_row:
    addi s4, s4, 1
    j    row_loop

done:
