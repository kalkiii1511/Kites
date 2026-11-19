# Kites visualization example: first 10 Fibonacci numbers.
# Best with: RV5 · H / F or RV5 · H / NF. Uses a loop, array stores, and branches.
# Expected fib array at 0x10000004: 0,1,1,2,3,5,8,13,21,34
# Expected result (F(9)) at 0x1000002c: 34

.data
n:    .word 10
fib:  .word 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
result: .word 0

.text
_start:
    la   t0, n
    lw   s0, 0(t0)          # s0 = n
    li   s1, 0              # F(0)
    li   s2, 1              # F(1)
    la   s3, fib            # fib array base
    sw   s1, 0(s3)
    li   t1, 1
    bge  s0, t1, store_f1
    j    done

store_f1:
    sw   s2, 4(s3)
    li   t2, 2              # i = 2
loop:
    bge  t2, s0, done
    add  t3, s1, s2         # F(i) = F(i-1) + F(i-2)
    slli t4, t2, 2
    add  t4, s3, t4
    sw   t3, 0(t4)
    mv   s1, s2
    mv   s2, t3
    addi t2, t2, 1
    j    loop

done:
    la   a0, result
    lw   a1, 36(s3)         # F(9)
    sw   a1, 0(a0)
