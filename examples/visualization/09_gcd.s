# Kites visualization example: GCD(48, 36) by repeated subtraction.
# Best with: RV5 · H / F or RV5 · H / NF. Short loop with branches and register RAW hazards.
# Expected result at 0x10000000: 12

.text
_start:
    li   t0, 48
    li   t1, 36

loop:
    beq  t1, zero, done
    blt  t0, t1, swap
    sub  t0, t0, t1
    j    loop

swap:
    mv   t2, t0
    mv   t0, t1
    mv   t1, t2
    j    loop

done:
    li   a0, 0x10000000
    sw   t0, 0(a0)
