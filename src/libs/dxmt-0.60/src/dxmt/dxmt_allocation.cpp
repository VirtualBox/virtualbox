#include "dxmt_allocation.hpp"
#include "util_likely.hpp"
#include <cassert>
#include <iterator>
#include <new>

namespace dxmt {

void
Allocation::incRef() {
  refcount_.fetch_add(1u, std::memory_order_acquire);
};

void
Allocation::decRef() {
  if (refcount_.fetch_sub(1u, std::memory_order_release) == 1u)
    delete this;
};

AllocationRefTracking::AllocationRefTracking() {
  chunk_placed.next_chunk = nullptr;
  chunk_placed.size = 0;
  chunk_placed.capacity = std::size(chunk_placed.allocations);
  chunk_last = reinterpret_cast<RefAddChunk<> *>(&chunk_placed);
}

bool
AllocationRefTracking::track(Allocation *allocation) {
  if (unlikely(chunk_last->capacity <= chunk_last->size))
    return false;
  dxmt::Allocation **list = std::launder(chunk_last->allocations);
  allocation->incRef();
  list[chunk_last->size++] = allocation;
  return true;
};

void
AllocationRefTracking::addStorage(void *ptr, size_t length) {
  RefAddChunk<> *new_chunk = std::launder(reinterpret_cast<RefAddChunk<> *>(ptr));
  new_chunk->next_chunk = nullptr;
  new_chunk->size = 0;
  assert(length > sizeof(RefAddChunk<>) && "No enough space for allocation reference tracking");
  new_chunk->capacity = (length - offsetof(RefAddChunk<>, allocations)) / sizeof(Allocation *);
  chunk_last->next_chunk = new_chunk;
  chunk_last = new_chunk;
}

void
AllocationRefTracking::clear() {
  RefAddChunk<> *chunk = reinterpret_cast<RefAddChunk<> *>(&chunk_placed);
  while (chunk) {
    dxmt::Allocation **list = std::launder(chunk->allocations);
    for (unsigned i = 0; i < chunk->size; i++)
      list[i]->decRef();
    chunk->size = 0;
    chunk = chunk->next_chunk;
  }
  // reset state
  chunk_last = reinterpret_cast<RefAddChunk<> *>(&chunk_placed);
  chunk_placed.next_chunk = nullptr;
};

} // namespace dxmt