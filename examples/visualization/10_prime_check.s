# Kites visualization example: check whether 29 is prime.
# Best with: RV5 · H / F or RV5 · H / NF. Uses mul/rem, branches, and a tight loop.
# Expected result at 0x10000000: 1 (prime)

.text
_start:
    li   s0, 29             # number to test
    li   s1, 1              # assume prime

    li   t0, 1
    ble  s0, t0, not_prime
    li   t0, 2
    beq  s0, t0, done
    andi t0, s0, 1
    beq  t0, zero, not_prime

    li   s2, 3              # i = 3
check_loop:
    mul  t0, s2, s2         # i*i
    bgt  t0, s0, done
    rem  t0, s0, s2         # number % i
    beq  t0, zero, not_prime
    addi s2, s2, 2
    j    check_loop

not_prime:
    li   s1, 0

done:
    li   a0, 0x10000000
    sw   s1, 0(a0)
