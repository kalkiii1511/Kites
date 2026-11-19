# Dense double-precision F/D chain (no NOP spacing).
#
# Recommended presets:
#   - Pipeline with Hazard Detection (No Forwarding)
#   - Pipeline with Hazard Control (Forwarding)
#
# Expected final state:
#   f2 = 4.75   (0x4013000000000000)
#   f3 = 16.625 (0x4030A00000000000)
#   mem[0x10000010] = 0x4013000000000000
#   mem[0x10000018] = 0x4030A00000000000

.data
double_a: .double 3.5
double_b: .double 1.25
out0: .double 0.0
out1: .double 0.0

.text
_start:
  li a0, 0x10000000
  fld f0, 0(a0)
  fld f1, 8(a0)
  fadd.d f2, f0, f1    # depends on both fld
  fmul.d f3, f2, f0    # depends on fadd.d f2
  fsd f2, 16(a0)       # store f2
  fsd f3, 24(a0)       # store f3
  li a7, 10
  ecall
