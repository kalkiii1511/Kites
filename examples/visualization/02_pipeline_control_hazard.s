# Kites visualization example: integer RAW, load-use, and taken-branch control hazard.
# Best with: RV5 · H / F or RV5 · H / NF.
# Expected: t1=10, t2=10, s1=42; a control-hazard toast appears when the branch flushes.
# The original used gp as a memory pointer; this Kites version uses an explicit base.

# Deliberate integer RAW, load-use, and control hazards for the Pipeline with Hazard Control preset.
.text
_start:
  li t0, 5
  add t1, t0, t0
  li t0, 0x10000000
  sd t1, 0(t0)
  ld t2, 0(t0)
  add t3, t2, t1
  beq t3, t3, branch_taken
  addi s1, zero, 99
branch_taken:
  addi s1, zero, 42
  li a7, 10
  ecall
