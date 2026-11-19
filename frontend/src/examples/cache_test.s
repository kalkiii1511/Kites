# Cache locality test: repeat loads from the same cache lines.
# With the default 16-byte line size, each group should reuse its first line.
.data
values: .word 10, 20, 30, 40, 50, 60, 70, 80

.text
_start:
    la   t0, values
    lw   t1, 0(t0)       # First line access: miss when the cache is cold
    lw   t2, 0(t0)       # Same address: hit
    lw   t3, 4(t0)       # Same cache line: hit
    lw   t4, 12(t0)      # Same cache line: hit
    lw   t5, 16(t0)      # Next line: miss when it is not cached
    lw   t6, 20(t0)      # Same second line: hit
    lw   a0, 16(t0)      # Same address again: hit
