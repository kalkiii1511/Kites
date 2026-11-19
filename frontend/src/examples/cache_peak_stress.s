# Cache capacity and write-policy stress test.
# Scans 256 bytes twice, then writes and rereads 64 words.
# Try different set counts, line sizes, and associativities in the Cache tab.

.data
working_set: .zero 256

.text
_start:
    la   s0, working_set
    li   s1, 2              # Two read passes over a 256-byte working set

read_pass:
    mv   t0, s0
    li   t1, 64             # 64 words = 256 bytes
read_loop:
    lw   t2, 0(t0)
    addi t0, t0, 4
    addi t1, t1, -1
    bne  t1, zero, read_loop
    addi s1, s1, -1
    bne  s1, zero, read_pass

    # Write every word, then read the same working set back.
    mv   t0, s0
    li   t1, 64
    li   t2, 1
write_loop:
    sw   t2, 0(t0)
    addi t2, t2, 1
    addi t0, t0, 4
    addi t1, t1, -1
    bne  t1, zero, write_loop

    mv   t0, s0
    li   t1, 64
readback_loop:
    lw   t3, 0(t0)
    addi t0, t0, 4
    addi t1, t1, -1
    bne  t1, zero, readback_loop
