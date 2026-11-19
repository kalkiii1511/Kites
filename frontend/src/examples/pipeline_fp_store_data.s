# FP store-data dependencies (no NOP spacing).
#
# Recommended presets:
#   - Pipeline with Hazard Detection (No Forwarding)
#   - Pipeline with Hazard Control (Forwarding)
#
# Tests an FP store immediately after an FP load and immediately after fadd.s.
#
# Expected final state:
#   mem[0x10000008] = 0x3FC00000  (f0 = 1.5)
#   mem[0x1000000C] = 0x40700000  (f2 = 3.75)

.data
single_a: .float 1.5
single_b: .float 2.25
out0: .float 0.0
out1: .float 0.0

.text
_start:
  li a0, 0x10000000
  flw f0, 0(a0)
  fsw f0, 8(a0)        # fsw immediately after the load of f0
  flw f1, 4(a0)
  fadd.s f2, f0, f1
  fsw f2, 12(a0)       # fsw immediately after fadd.s f2
  li a7, 10
  ecall
