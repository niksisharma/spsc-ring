// spsc_ring.hpp
// A single-producer / single-consumer lock-free ring buffer.
//
// Design rules (the ones worth being able to explain):
//   1. Storage is allocated once (it is a member array), never in push/pop.
//   2. N is a power of two so "index % N" becomes "index & (N - 1)".
//   3. head_ has exactly one writer (the producer); tail_ has exactly one
//      writer (the consumer). That is why no lock is needed.
//   4. release/acquire ordering makes the slot contents visible to the other
//      thread before it sees the updated counter.
//   5. head_ and tail_ live on separate 64-byte cache lines to avoid
//      false sharing.
//   6. When full, push() returns false. The caller decides the drop policy.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

template <typename T, std::size_t N>
class SpscRing {
  static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

 public:
  // Called by the PRODUCER thread only.
  bool push(const T& value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);  // my own counter
    const std::size_t tail = tail_.load(std::memory_order_acquire);  // consumer's counter
    if (head - tail == N) {
      return false;  // full
    }
    buf_[head & (N - 1)] = value;                                // 1) write the slot
    head_.store(head + 1, std::memory_order_release);            // 2) then publish it
    return true;
  }

  // Called by the CONSUMER thread only.
  bool pop(T& out) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);  // my own counter
    const std::size_t head = head_.load(std::memory_order_acquire);  // producer's counter
    if (head == tail) {
      return false;  // empty
    }
    out = buf_[tail & (N - 1)];                                  // 1) read the slot
    tail_.store(tail + 1, std::memory_order_release);            // 2) then free it
    return true;
  }

  // Approximate (the other thread may be mid-update); fine for monitoring.
  std::size_t size_approx() const noexcept {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }

  static constexpr std::size_t capacity() noexcept { return N; }

 private:
  alignas(64) std::atomic<std::size_t> head_{0};  // written by producer only
  alignas(64) std::atomic<std::size_t> tail_{0};  // written by consumer only
  alignas(64) std::array<T, N> buf_{};
};
