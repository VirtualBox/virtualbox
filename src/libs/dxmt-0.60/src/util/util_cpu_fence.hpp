#pragma once
#include <atomic>
#include <cstdint>

namespace dxmt {

class CpuFence {
public:
  void wait(uint64_t value) {
    auto current = value_.load(std::memory_order_acquire);
    while (current < value) {
      value_.wait(current);
      current = value_.load(std::memory_order_acquire);
    }
  }

  void signal(uint64_t value) {
    auto current = value_.load(std::memory_order_relaxed);
    do {
      if (value <= current)
        return;
    } while (!value_.compare_exchange_weak(current, value, std::memory_order_release, std::memory_order_relaxed));
    value_.notify_all();
  }

  uint64_t signaledValue() {
    return value_.load(std::memory_order_acquire);
  }

  CpuFence(): value_(0) {}
  CpuFence(uint64_t initial_value): value_(initial_value) {}

private:
  std::atomic<uint64_t> value_;
};
} // namespace dxmt