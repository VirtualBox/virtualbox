#pragma once

#include <atomic>
#include <cstdint>

namespace dxmt {

class Allocation {
public:
  virtual ~Allocation(){};

  void incRef();
  void decRef();

  bool
  checkRetained(uint64_t seq_id) {
    // FIXME: is a compare-and-swap necessary?
    if (seq_id == last_retained_seq_id)
      return true;
    last_retained_seq_id = seq_id;
    return false;
  }

private:
  std::atomic<uint32_t> refcount_ = {0u};
  uint64_t last_retained_seq_id = 0;
};

class AllocationRefTracking {
public:
  AllocationRefTracking();

  bool track(Allocation *allocation);

  void addStorage(void *ptr, size_t length);

  void clear();

private:
  template <size_t Size = 1> struct RefAddChunk {
    RefAddChunk *next_chunk;
    size_t capacity;
    size_t size;
    Allocation *allocations[Size];
  };
  RefAddChunk<29> chunk_placed;
  RefAddChunk<> *chunk_last = nullptr;
};

} // namespace dxmt
