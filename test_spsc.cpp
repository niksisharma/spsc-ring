// test_spsc.cpp - correctness tests for SpscRing.
// Build with ThreadSanitizer (see Makefile: `make test-tsan`) to catch data races.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "spsc_ring.hpp"

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

static void test_empty_and_full() {
  SpscRing<int, 8> r;
  int out = 0;
  CHECK(!r.pop(out));                       // empty at start
  for (int i = 0; i < 8; ++i) CHECK(r.push(i));
  CHECK(!r.push(99));                       // full after N pushes
  CHECK(r.size_approx() == 8);
  for (int i = 0; i < 8; ++i) {
    CHECK(r.pop(out));
    CHECK(out == i);                        // FIFO order
  }
  CHECK(!r.pop(out));                       // empty again
}

static void test_wraparound() {
  SpscRing<int, 4> r;
  int out = 0;
  // Push/pop far more items than the capacity so indices wrap many times.
  for (int i = 0; i < 1000; ++i) {
    CHECK(r.push(i));
    CHECK(r.pop(out));
    CHECK(out == i);
  }
}

static void test_two_threads() {
  static SpscRing<std::uint64_t, 1024> ring;   // static: no 8 KB on the stack
  constexpr std::uint64_t kCount = 5'000'000;

  std::thread producer([] {
    for (std::uint64_t i = 0; i < kCount; ++i) {
      while (!ring.push(i)) std::this_thread::yield();  // block on full (test only)
    }
  });

  std::uint64_t expected = 0;
  std::thread consumer([&expected] {
    std::uint64_t v = 0;
    while (expected < kCount) {
      if (ring.pop(v)) {
        CHECK(v == expected);                // no loss, no reordering, no corruption
        ++expected;
      } else {
        std::this_thread::yield();
      }
    }
  });

  producer.join();
  consumer.join();
  CHECK(expected == kCount);
}

int main() {
  test_empty_and_full();
  test_wraparound();
  test_two_threads();
  std::puts("all tests passed");
  return 0;
}
