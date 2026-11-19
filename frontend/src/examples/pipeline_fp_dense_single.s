# Dense single-precision F/D chain (no NOP spacing).
#
# Recommended presets:
#   - Pipeline with Hazard Detection (No Forwarding)
#   - Pipeline with Hazard Control (Forwarding)
#
# This exercises FP RAW interlocks / forwarding, FP load-use, and FP store data.
#
# Expected final state:
#   f2 = 3.75   (0x40700000)
#   f3 = 5.625  (0x40B40000)
#   f4 = 2.5    (0x40200000)
#   mem[0x10000008] = 0x40700000
#   mem[0x1000000C] = 0x40B40000
#   mem[0x10000010] = 0x40200000

.data
single_a: .float 1.5
single_b: .float 2.25
out0: .float 0.0
out1: .float 0.0
out2: .float 0.0

.text
_start:
  li a0, 0x10000000
  flw f0, 0(a0)
  flw f1, 4(a0)
  fadd.s f2, f0, f1    # depends on both flw
  fmul.s f3, f2, f0    # depends on fadd.s f2
  fdiv.s f4, f3, f1    # depends on fmul.s f3
  fsw f2, 8(a0)        # store f2
  fsw f3, 12(a0)       # store f3
  fsw f4, 16(a0)       # store f4
  li a7, 10
  ecall
