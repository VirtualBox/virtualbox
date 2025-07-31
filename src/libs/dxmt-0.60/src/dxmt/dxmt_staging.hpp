#pragma once
#include "Metal.hpp"
#include "thread.hpp"
#include "wsi_platform.hpp"
#include <cstdint>
#include <atomic>
#include <queue>

namespace dxmt {

enum class StagingMapResult : uint64_t {
  Mappable = 0,
  Renamable = 0xffffffffffffffff,
  Mapped = 0xfffffffffffffffe,
};

class StagingResource {
public:
  void incRef();
  void decRef();

  void useCopyDestination(uint64_t seq_id);
  void useCopySource(uint64_t seq_id);

  StagingMapResult tryMap(uint64_t coherent_seq_id, bool read, bool write);
  void unmap();

  uint64_t allocate(uint64_t coherent_seq_id);
  void updateImmediateName(uint64_t current_seq_id, uint64_t allocation);

  void *
  mappedImmediateMemory() {
    return buffer_pool[immediate_name_].info.memory.get();
  }

  void *
  mappedMemory(uint64_t id) {
    return buffer_pool[id].info.memory.get();
  }

  uint64_t encoding_name;

  WMT::Buffer currentBuffer() {
    return buffer_pool[encoding_name].allocation;
  };

  /**
  readonly
   */
  uint32_t bytesPerRow;
  /**
  readonly
   */
  uint32_t bytesPerImage;
  /**
  readonly
   */
  uint64_t length;

  StagingResource(WMT::Device device, uint64_t length, WMTResourceOptions options, uint32_t bytes_per_row, uint32_t bytes_per_image);

private:
  struct StagingBuffer {
    WMTBufferInfo info;
    WMT::Reference<WMT::Buffer> allocation;

    ~StagingBuffer() {
#ifdef __i386__
      if (info.memory.get()) {
        wsi::aligned_free(info.memory.get());
        info.memory.set(nullptr);
      }
#endif
    }

    StagingBuffer() = default;
    StagingBuffer(const StagingBuffer &) = delete;
    StagingBuffer(StagingBuffer &&move) {
      info = move.info;
      move.info.memory.set(nullptr);
      allocation = std::move(move.allocation);
    };
  };
  struct QueueEntry {
    uint64_t id;
    uint64_t will_free_at;
  };

  uint64_t immediate_name_;
  std::vector<StagingBuffer> buffer_pool;
  std::atomic<uint32_t> refcount_ = {0u};
  std::queue<QueueEntry> fifo;
  dxmt::mutex mutex_;
  bool mapped = false;
  // prevent read from staging before
  uint64_t cpu_coherent_after_finished_seq_id = 0;
  // prevent write to staging before
  uint64_t gpu_occupied_until_finished_seq_id = 0;
  WMT::Device device_;
  WMTResourceOptions options_;
};

} // namespace dxmt