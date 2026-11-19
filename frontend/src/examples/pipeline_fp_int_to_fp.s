# Integer -> FP conversions and moves.
#
# Covers:
#   fcvt.s.w f0, a0
#   fcvt.d.w f1, a2
#   fmv.w.x f2, a4
#
# NOP-spaced so the same program also runs on the basic pipeline
# (no forwarding) and the single-cycle datapath.
#
# Recommended presets: Single Cycle, Pipeline, Pipeline with Hazard Control,
#                      Pipeline with Hazard Detection (No Forwarding)
#
# Expected final state:
#   f0 = 5.0   (0x40A00000)
#   f1 = 7.0   (0x401C000000000000)
#   f2 = 1.5   (0x3FC00000, bit pattern passed through fmv.w.x)
#   mem[0x10000000] = 0x40A00000
#   mem[0x10000008] = 0x401C000000000000
#   mem[0x10000010] = 0x3FC00000

.data
out_s: .float 0.0
out_d: .double 0.0
out_m: .float 0.0

.text
_start:
  li a0, 5
  nop
  nop
  nop
  fcvt.s.w f0, a0
  nop
  nop
  nop
  li a1, 0x10000000
  nop
  nop
  nop
  fsw f0, 0(a1)
  nop
  nop
  nop
  li a2, 7
  nop
  nop
  nop
  fcvt.d.w f1, a2
  nop
  nop
  nop
  fsd f1, 8(a1)
  nop
  nop
  nop
  li a4, 0x3FC00000
  nop
  nop
  nop
  fmv.w.x f2, a4
  nop
  nop
  nop
  fsw f2, 16(a1)
  nop
  nop
  nop
  li a7, 10
  ecall
