#pragma once
#include "Metal.hpp"
#include "dxmt_deptrack.hpp"
#include "dxmt_residency.hpp"
#include "dxmt_allocation.hpp"
#include "rc/util_rc_ptr.hpp"
#include "thread.hpp"
#include "util_flags.hpp"

namespace dxmt {

enum class TextureAllocationFlag : uint32_t {
  GpuReadonly = 0,
  NoTracking = 0,
  GpuPrivate = 1,
  CpuInvisible = 1,
  CpuWriteCombined = 2,
  OwnedByCommandList = 3,
  GpuManaged = 4,
};

typedef unsigned TextureViewKey;

struct TextureViewDescriptor {
  WMTPixelFormat format: 32;
  WMTTextureType type;
  WMTTextureUsage usage;
  unsigned firstMiplevel = 0;
  unsigned miplevelCount = 1;
  unsigned firstArraySlice = 0;
  unsigned arraySize = 1;
};

struct TextureView {
  WMT::Reference<WMT::Texture> texture;
  uint64_t gpu_resource_id;
  DXMT_RESOURCE_RESIDENCY_STATE residency{};

  TextureView(WMT::Reference<WMT::Texture> &&texture, uint64_t gpu_resource_id) :
      texture(std::move(texture)),
      gpu_resource_id(gpu_resource_id) {}
  TextureView(const WMT::Reference<WMT::Texture> &texture, uint64_t gpu_resource_id) :
      texture(texture),
      gpu_resource_id(gpu_resource_id) {}
};

class TextureAllocation : public Allocation {
  friend class Texture;

public:

  WMT::Texture texture() {
    return obj_;
  }

  Flags<TextureAllocationFlag>
  flags() const {
    return flags_;
  }

  void *mappedMemory;
  uint64_t gpuResourceID;
  DXMT_RESOURCE_RESIDENCY_STATE residencyState;
  EncoderDepKey depkey;

private:
  TextureAllocation(
      WMT::Reference<WMT::Buffer> &&buffer, void *mapped_buffer, const WMTTextureInfo &info, unsigned bytes_per_row,
      Flags<TextureAllocationFlag> flags
  );
  TextureAllocation(
      WMT::Reference<WMT::Texture> &&texture, const WMTTextureInfo &textureDescriptor,
      Flags<TextureAllocationFlag> flags
  );
  ~TextureAllocation();

  TextureAllocation(const TextureAllocation &) = delete;
  TextureAllocation(TextureAllocation &&) = delete;

  WMT::Reference<WMT::Texture> obj_;
  WMT::Reference<WMT::Buffer> buffer_;
  uint32_t version_ = 0;
  Flags<TextureAllocationFlag> flags_;
  std::vector<std::unique_ptr<TextureView>> cached_view_;
};

class Texture {
public:
  void incRef();
  void decRef();

  TextureViewKey createView(TextureViewDescriptor const &descriptor);

  constexpr TextureAllocation *
  current() {
    return current_.ptr();
  }

  WMTTextureType
  textureType() const {
    return info_.type;
  }

  WMTPixelFormat
  pixelFormat() const {
    return info_.pixel_format;
  }

  WMTTextureType
  textureType(TextureViewKey view) const {
    return viewDescriptors_.data()[view].type;
  }

  WMTPixelFormat
  pixelFormat(TextureViewKey view) const {
    return viewDescriptors_[view].format;
  }

  WMTTextureUsage
  usage() const {
    return info_.usage;
  }

  unsigned
  sampleCount() const {
    return info_.sample_count;
  }

  unsigned
  width() const {
    return info_.width;
  }

  unsigned
  height() const {
    return info_.height;
  }

  unsigned
  width(TextureViewKey view) const {
    return std::max(info_.width >> viewDescriptors_[view].firstMiplevel, 1u);
  }

  unsigned
  height(TextureViewKey view) const {
    return std::max(info_.height >> viewDescriptors_[view].firstMiplevel, 1u);
  }

  unsigned
  arrayLength() const {
    return info_.array_length;
  }

  unsigned
  arrayLength(TextureViewKey view) const {
    return viewDescriptors_[view].arraySize;
  }

  Rc<TextureAllocation> allocate(Flags<TextureAllocationFlag> flags);

  WMT::Texture view(TextureViewKey key);
  WMT::Texture view(TextureViewKey key, TextureAllocation *allocation);

  TextureView const &view_(TextureViewKey key);
  TextureView const &view_(TextureViewKey key, TextureAllocation *allocation);

  TextureViewKey checkViewUseArray(TextureViewKey key, bool isArray);
  TextureViewKey checkViewUseFormat(TextureViewKey key, WMTPixelFormat format);

  DXMT_RESOURCE_RESIDENCY_STATE &residency(TextureViewKey key);
  DXMT_RESOURCE_RESIDENCY_STATE &residency(TextureViewKey key, TextureAllocation *allocation);

  Rc<TextureAllocation> rename(Rc<TextureAllocation> &&newAllocation);

  Texture(const WMTTextureInfo &info, WMT::Device device);

  Texture(unsigned bytes_per_image, unsigned bytes_per_row, const WMTTextureInfo &info, WMT::Device device);

private:
  void prepareAllocationViews(TextureAllocation* allocation);

  WMTTextureInfo info_;
  unsigned bytes_per_image_ = 0;
  unsigned bytes_per_row_ = 0;

  Rc<TextureAllocation> current_;
  uint32_t version_ = 0;
  std::atomic<uint32_t> refcount_ = {0u};

  std::vector<TextureViewDescriptor> viewDescriptors_;
  dxmt::mutex mutex_;
  WMT::Device device_;
};

class RenamableTexturePool {
public:
  void incRef();
  void decRef();
  Rc<TextureAllocation> getNext(uint64_t frame_);

  RenamableTexturePool(Texture *texture, size_t capacity,Flags<TextureAllocationFlag> allocation_flags);

private:
  Rc<Texture> texture_;
  std::vector<Rc<TextureAllocation>> allocations_;
  uint64_t last_frame_ = 0;
  Flags<TextureAllocationFlag> allocation_flags_;
  unsigned capacity_;
  unsigned current_index_ = 0;
  std::atomic<uint32_t> refcount_ = {0u};
};

} // namespace dxmt