# FP compare/convert results written to integer registers.
#
# Recommended presets:
#   - Pipeline with Hazard Detection (No Forwarding)
#   - Pipeline with Hazard Control (Forwarding)
#
# Expected final state:
#   a4 (x14) = 1   (flt.s 1.5 < 2.25)
#   a5 (x15) = 0   (feq.s 1.5 == 2.25 is false)
#   a6 (x16) = 2   (fcvt.w.s 2.25 -> 2)

.data
single_a: .float 1.5
single_b: .float 2.25

.text
_start:
  li a0, 0x10000000
  flw f0, 0(a0)
  flw f1, 4(a0)
  flt.s a4, f0, f1
  feq.s a5, f0, f1
  fcvt.w.s a6, f1
  li a7, 10
  ecall
