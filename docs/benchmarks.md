# Benchmarks

Numbers below are measured, not claimed. Reproduce with:

```bash
make bench   # Release build inside the Linux verification image
```

Environment: `gcc:bookworm` container (GCC 12), Release `-O2`, Windows Docker Desktop host.

## book_bench — synthetic ladder (5 000 levels/side)

| Benchmark | n | mean | p50 | p95 | p99 |
|---|---|---|---|---|---|
| applySnapshot(5000/side) | 1 | 797.9 us | 797.9 us | 797.9 us | 797.9 us |
| applyDelta (200k ops) | 200 000 | 164.2 ns | 122 ns | 236 ns | 463 ns |
| execute(notional $250k) × 50k runs | 50 000 | 151.8 us | 112.1 us | 253.9 us | 760.6 us |
| serialize() (136 601 bytes) | 200 | 1.38 ms | 1.16 ms | 2.55 ms | 3.98 ms |

`applyDelta` sustains ~6.09M ops/s; `execute` ~6 588 runs/s.

## replay_bench — recorded log (100k deltas)

| Metric | Value |
|---|---|
| events applied | 100 001 |
| replay wall time | 0.417 s |
| sustained throughput | ~239 656 events/s |
| per-event apply p50/p95/p99 | 98 ns / 263 ns / 693 ns |
| execute(replayed book) mean | 151.1 us (p50 117.2 us) |
| recording size | 15.2 MB JSONL |
