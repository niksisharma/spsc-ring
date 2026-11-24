# spsc-ring

A small single-producer / single-consumer lock-free ring buffer in C++17, with correctness tests and a latency benchmark.

**Status: learning project.** I built this to understand real-time data handoff (zero allocation in the hot path, memory ordering, false sharing, drop policy, worst-case latency measurement). It is not production code.

## What it demonstrates
- Fixed-size, preallocated storage; no `new`/`malloc` in `push`/`pop`.
- Power-of-two capacity so wrapping is `index & (N-1)`.
- One writer per counter (`head_` = producer, `tail_` = consumer), so no lock.
- `release`/`acquire` ordering so slot contents are visible before the counter update.
- `head_` and `tail_` on separate cache lines (avoids false sharing).
- Full buffer returns `false`; the caller applies a drop policy (the benchmark drops the newest frame and counts it).

## Build and run
```
make test         # unit + two-thread tests
make test-tsan    # same tests under ThreadSanitizer (data race detector)
make test-asan    # AddressSanitizer + UBSan
make run-bench    # 100 kHz paced producer, 5 s, threads pinned to cores 0 and 1
```

## Experiment worth trying
Change every `memory_order_release` / `memory_order_acquire` in `spsc_ring.hpp` to `memory_order_relaxed` and run `make test-tsan`. ThreadSanitizer reports a data race. The normal test can still pass on x86, which is why ordering bugs are easy to miss.

## Benchmark 
Measured on a shared 2-vCPU cloud sandbox, 100 kHz, 5 s, 500,000 frames, 32-byte frames, 1024 slots, 0 dropped:

| Setting  | p50 | p99 | p99.9 | max |
|----------|-----|-----|-------|-----|
| pinned   | 345 ns | 2.35 us | 169 us | 1.64 ms |
| unpinned | 359 ns | 40.7 us | 1.98 ms | 3.95 ms |

The median is similar, but the tail is very different. Pinning threads to cores reduces the worst case, which is why real-time systems care about p99.9 and max, not the average. A shared cloud VM is a noisy environment, so treat these as illustrative.

## Limitations / next steps
- Only one producer and one consumer are supported.
- The frame is copied into and out of the ring. A zero-copy variant would hand out slot references (claim/commit) so a DMA engine could write directly into a slot.
- Real isolation needs `isolcpus`, real-time scheduling and a quiet machine.
- Not tested on weakly ordered CPUs (ARM) beyond what the sanitizers show.
