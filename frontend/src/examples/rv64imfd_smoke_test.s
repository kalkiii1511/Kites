.data
single_a: .float 1.5
single_b: .float 2.25
double_a: .double 3.5
double_b: .double 1.25
result: .dword 0

.text
_start:
  li t0, 0x100000001
  li t1, 7
  add t2, t0, t1
  sub t3, t2, t1
  slli t4, t1, 3
  mul t5, t1, t1
  div t6, t5, t1
  rem s0, t5, t1
  mulh s1, t0, t1
  mulw s2, t1, t1
  divw s3, s2, t1
  remuw s4, s2, t1
  la a0, result
  sd t2, 0(a0)
  ld a1, 0(a0)
  la a2, single_a
  flw f0, 0(a2)
  flw f1, 4(a2)
  fadd.s f2, f0, f1
  fmul.s f3, f0, f1
  fdiv.s f4, f2, f0
  fsw f2, 0(a2)
  la a3, double_a
  fld f5, 0(a3)
  fld f6, 8(a3)
  fadd.d f7, f5, f6
  fmul.d f8, f5, f6
  fdiv.d f9, f7, f5
  fsd f7, 0(a3)
  flt.d a4, f5, f6
  fcvt.s.d f10, f7
  fcvt.d.s f11, f10
  fmadd.d f12, f5, f6, f7
  li a7, 10
  ecall
