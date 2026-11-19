# Deliberate integer RAW, load-use, and control hazards for the Pipeline with Hazard Control preset.
.text
_start:
  li t0, 5
  add t1, t0, t0
  sd t1, 0(gp)
  ld t2, 0(gp)
  add t3, t2, t1
  beq t3, t3, branch_taken
  addi s1, zero, 99
branch_taken:
  addi s1, zero, 42
  li a7, 10
  ecall
