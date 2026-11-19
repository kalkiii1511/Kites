# Branch prediction demo
# Select RV5 H/F (or RV5 H/NF) with the 2-bit predictor, then Assemble and Run.
# Three branches live in this loop: an alternating BEQ, an unconditional J, and
# the loop-back BNE, so the BTB fills with multiple entries.
.text
_start:
    li   t0, 6          # loop counter
    li   a0, 0          # iteration counter
    li   a1, 0          # accumulator
loop:
    addi a0, a0, 1
    andi t1, a0, 1      # test the low bit
    beq  t1, x0, even   # alternating branch
    addi a1, a1, 10
    j    after
even:
    addi a1, a1, 3
after:
    addi t0, t0, -1
    bne  t0, x0, loop   # loop-back branch
    addi a2, a1, 0      # a2 = 39 when the loop finishes
