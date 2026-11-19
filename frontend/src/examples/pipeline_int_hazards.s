# Dense integer RAW / load-use / store-data test (no NOP spacing).
#
# Recommended presets:
#   - Pipeline with Hazard Detection (No Forwarding)
#   - Pipeline with Hazard Control (Forwarding)
#
# Expected final state:
#   x6 = 6, x7 = 7, x8 = 12
#   x10 = 12 (loaded back from memory)
#   x11 = 24
#   mem[0x10000000] = 12

.text
_start:
  li x5, 5
  addi x6, x5, 1       # ALU RAW on x5
  addi x7, x6, 1       # ALU RAW on x6
  add x8, x7, x5       # ALU RAW on x7
  li x9, 0x10000000
  sw x8, 0(x9)         # store-data RAW on x8
  lw x10, 0(x9)        # load after store
  add x11, x10, x8     # load-use on x10
  li a7, 10
  ecall
