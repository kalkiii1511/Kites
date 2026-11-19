# Pipeline-only F/D smoke test. The plain pipeline has no bypass or stall unit,
# so independent NOPs separate producers from their consumers.
.data
single_a: .float 1.5
single_b: .float 2.25

.text
_start:
  li a0, 0x10000000
  nop
  nop
  nop
  flw f0, 0(a0)
  nop
  nop
  nop
  flw f1, 4(a0)
  nop
  nop
  nop
  fadd.s f2, f0, f1
  nop
  nop
  nop
  fsw f2, 0(a0)
  nop
  nop
  nop
  li a7, 10
  ecall
