// bench.cpp - measures end-to-end handoff latency through the ring at a paced rate.
//
// The producer emulates a sensor: it pushes one "frame" every 1/rate seconds
// (busy-wait pacing, like a hardware timer) and stamps each frame with the time.
// The consumer spins on pop(), and records (now - stamp) for each frame.
// We report p50 / p99 / p99.9 / max, because the WORST case matters in real time,
// not the average.
//
// Usage: ./bench [rate_hz=100000] [seconds=5] [--pin]
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include <pthread.h>
#include <sched.h>

#include "spsc_ring.hpp"

using Clock = std::chrono::steady_clock;

static inline std::uint64_t now_ns() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          Clock::now().time_since_epoch()).count());
}

struct Frame {                 // 32 bytes: fixed size, trivially copyable
  std::uint64_t seq;
  std::uint64_t t_push_ns;
  std::int16_t samples[8];     // 8 channels x 16 bits
};

static SpscRing<Frame, 1024> ring;   // allocated once, before the threads start

static void pin_to_cpu(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
}

int main(int argc, char** argv) {
  double rate_hz = 100000.0;
  double seconds = 5.0;
  bool pin = false;
  int positional = 0;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--pin")) pin = true;
    else if (positional == 0) { rate_hz = std::atof(argv[i]); ++positional; }
    else if (positional == 1) { seconds = std::atof(argv[i]); ++positional; }
  }

  const std::uint64_t period_ns = static_cast<std::uint64_t>(1e9 / rate_hz);
  const std::uint64_t total = static_cast<std::uint64_t>(rate_hz * seconds);

  // Preallocate everything the consumer needs BEFORE the run (no allocation in the hot loop).
  std::vector<std::uint32_t> latencies_ns;
  latencies_ns.reserve(total + 16);

  std::atomic<bool> producer_done{false};
  std::atomic<std::uint64_t> drops{0};
  std::atomic<std::uint64_t> late_ticks{0};   // times the producer missed its schedule

  std::thread producer([&] {
    if (pin) pin_to_cpu(0);
    std::uint64_t next = now_ns() + period_ns;
    for (std::uint64_t seq = 0; seq < total; ++seq) {
      while (now_ns() < next) { /* busy-wait: emulates a hardware timer tick */ }
      const std::uint64_t t = now_ns();
      if (t > next + period_ns) late_ticks.fetch_add(1, std::memory_order_relaxed);
      Frame f{};
      f.seq = seq;
      f.t_push_ns = t;
      if (!ring.push(f)) drops.fetch_add(1, std::memory_order_relaxed);  // drop policy: drop newest, count it
      next += period_ns;
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    if (pin) pin_to_cpu(1);
    Frame f;
    for (;;) {
      if (ring.pop(f)) {
        const std::uint64_t t = now_ns();
        latencies_ns.push_back(static_cast<std::uint32_t>(t - f.t_push_ns));  // capacity reserved: no realloc
      } else if (producer_done.load(std::memory_order_acquire) && ring.size_approx() == 0) {
        break;
      }
    }
  });

  producer.join();
  consumer.join();

  if (latencies_ns.empty()) { std::puts("no samples"); return 1; }
  std::sort(latencies_ns.begin(), latencies_ns.end());   // offline analysis, not the hot path
  auto pct = [&](double p) {
    std::size_t idx = static_cast<std::size_t>(p / 100.0 * (latencies_ns.size() - 1));
    return latencies_ns[idx];
  };

  std::printf("rate: %.0f Hz   frames: %zu   dropped: %llu   producer late ticks: %llu   pinned: %s\n",
              rate_hz, latencies_ns.size(),
              static_cast<unsigned long long>(drops.load()),
              static_cast<unsigned long long>(late_ticks.load()),
              pin ? "yes" : "no");
  std::printf("handoff latency (ns):  p50=%u  p99=%u  p99.9=%u  max=%u\n",
              pct(50), pct(99), pct(99.9), latencies_ns.back());
  return 0;
}
