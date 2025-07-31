#pragma once

#include "Metal.hpp"
#include "dxmt_buffer.hpp"
#include "dxmt_command.hpp"
#include "dxmt_deptrack.hpp"
#include "dxmt_occlusion_query.hpp"
#include "dxmt_residency.hpp"
#include "dxmt_statistics.hpp"
#include "dxmt_texture.hpp"
#include "log/log.hpp"
#include "rc/util_rc_ptr.hpp"
#include "airconv_public.h"
#include <cassert>

#define DXMT_IMPLEMENT_ME __builtin_unreachable();
#define DXMT_UNREACHABLE __builtin_unreachable();

namespace dxmt {

constexpr size_t kCommandChunkCPUHeapSize = 0x400000;
constexpr size_t kCommandChunkGPUHeapSize = 0x400000;
constexpr size_t kEncodingContextCPUHeapSize = 0x1000000;

inline std::size_t
align_forward_adjustment(const void *const ptr, const std::size_t &alignment) noexcept {
  const auto iptr = reinterpret_cast<std::uintptr_t>(ptr);
  const auto aligned = (iptr - 1u + alignment) & -alignment;
  return aligned - iptr;
}

inline void *
ptr_add(const void *const p, const std::uintptr_t &amount) noexcept {
  return reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(p) + amount);
}

enum class PipelineStage { Vertex = 0, Pixel = 1, Geometry = 2, Hull = 3, Domain = 4, Compute = 5 };

enum class PipelineKind { Ordinary, Tessellation, Geometry };

constexpr unsigned kStages = 6;
constexpr unsigned kSRVBindings = 128;
constexpr unsigned kUAVBindings = 64;
constexpr unsigned kVertexBufferSlots = 32;

struct VertexBufferBinding {
  Rc<Buffer> buffer;
  unsigned offset;
  unsigned stride;
};

struct ConstantBufferBinding {
  Rc<Buffer> buffer;
  unsigned offset;
};

struct SamplerBinding {
  WMT::Reference<WMT::SamplerState> sampler;
  uint32_t sampler_id;
  float bias;
};

struct ResourceViewBinding {
  unsigned viewId;
  Rc<Buffer> buffer;
  Rc<Texture> texture;
  BufferSlice slice;
};

struct UnorderedAccessViewBinding {
  unsigned viewId;
  Rc<Buffer> buffer;
  Rc<Texture> texture;
  Rc<Buffer> counter;
  BufferSlice slice;
};

enum class EncoderType {
  Null,
  Render,
  Compute,
  Blit,
  Clear,
  Resolve,
  Present,
  SpatialUpscale,
  SignalEvent,
  TemporalUpscale,
  WaitForEvent,
};

struct EncoderData {
  EncoderType type;
  EncoderData *next = nullptr;
  uint64_t id;
  EncoderDepSet buf_read;
  EncoderDepSet buf_write;
  EncoderDepSet tex_read;
  EncoderDepSet tex_write;
};

struct GSDispatchArgumentsMarshal {
  WMT::Reference<WMT::Buffer> draw_arguments;
  uint64_t draw_arguments_va;
  WMT::Buffer dispatch_arguments_buffer;
  uint64_t dispatch_arguments_va;
  uint32_t vertex_count_per_warp;
};

struct RenderEncoderData : EncoderData {
  WMTRenderPassInfo info;
  std::vector<GSDispatchArgumentsMarshal> gs_arg_marshal_tasks;
  wmtcmd_render_nop cmd_head;
  wmtcmd_base *cmd_tail;
  wmtcmd_render_nop pretess_cmd_head;
  wmtcmd_base *pretess_cmd_tail;
  WMT::Buffer allocated_argbuf;
  uint64_t allocated_argbuf_offset;
  void *allocated_argbuf_mapping;
  uint8_t dsv_planar_flags;
  uint8_t dsv_readonly_flags;
  uint8_t render_target_count;
  bool use_visibility_result = 0;
  bool use_tessellation = 0;
  bool use_geometry = 0;
};

struct ComputeEncoderData : EncoderData {
  wmtcmd_compute_nop cmd_head;
  wmtcmd_base *cmd_tail;
  WMT::Buffer allocated_argbuf;
  uint64_t allocated_argbuf_offset;
  void *allocated_argbuf_mapping;
};

struct BlitEncoderData : EncoderData {
  wmtcmd_blit_nop cmd_head;
  wmtcmd_base *cmd_tail;
};

struct ClearEncoderData : EncoderData {
  union {
    WMTClearColor color;
    std::pair<float, uint8_t> depth_stencil;
  };
  WMT::Reference<WMT::Texture> texture;
  unsigned clear_dsv;
  unsigned array_length;
  unsigned width;
  unsigned height;

  ClearEncoderData() {}
};

struct ResolveEncoderData : EncoderData {
  WMT::Reference<WMT::Texture> src;
  WMT::Reference<WMT::Texture> dst;
};

class Presenter;

struct PresentData : EncoderData {
  WMT::Reference<WMT::Texture> backbuffer;
  Rc<Presenter> presenter;
  double after;
};

struct SpatialUpscaleData : EncoderData {
  WMT::Reference<WMT::Texture> backbuffer;
  WMT::Reference<WMT::Texture> upscaled;
  WMT::Reference<WMT::FXSpatialScaler> scaler;
};

struct SignalEventData : EncoderData {
  WMT::Reference<WMT::Event> event;
  uint64_t value;
};

struct WaitForEventData : EncoderData {
  WMT::Reference<WMT::Event> event;
  uint64_t value;
};

struct TemporalUpscaleData : EncoderData {
  WMT::Reference<WMT::Texture> input;
  WMT::Reference<WMT::Texture> output;
  WMT::Reference<WMT::Texture> depth;
  WMT::Reference<WMT::Texture> motion_vector;
  WMT::Reference<WMT::Texture> exposure;
  WMT::FXTemporalScaler scaler;
  WMTFXTemporalScalerProps props;
};

template <PipelineKind kind>
constexpr DXMT_RESOURCE_RESIDENCY
GetResidencyMask(PipelineStage type, bool read, bool write) {
  switch (type) {
  case PipelineStage::Vertex:
    if constexpr (kind == PipelineKind::Tessellation)
      return (read ? DXMT_RESOURCE_RESIDENCY_OBJECT_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
             (write ? DXMT_RESOURCE_RESIDENCY_OBJECT_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
    else if constexpr (kind == PipelineKind::Geometry)
      return (read ? DXMT_RESOURCE_RESIDENCY_OBJECT_GS_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
             (write ? DXMT_RESOURCE_RESIDENCY_OBJECT_GS_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
    else
      return (read ? DXMT_RESOURCE_RESIDENCY_VERTEX_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
             (write ? DXMT_RESOURCE_RESIDENCY_VERTEX_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
  case PipelineStage::Pixel:
    return (read ? DXMT_RESOURCE_RESIDENCY_FRAGMENT_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
           (write ? DXMT_RESOURCE_RESIDENCY_FRAGMENT_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
  case PipelineStage::Hull:
    return (read ? DXMT_RESOURCE_RESIDENCY_MESH_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
           (write ? DXMT_RESOURCE_RESIDENCY_MESH_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
  case PipelineStage::Geometry:
    return (read ? DXMT_RESOURCE_RESIDENCY_MESH_GS_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
           (write ? DXMT_RESOURCE_RESIDENCY_MESH_GS_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
  case PipelineStage::Domain:
    return (read ? DXMT_RESOURCE_RESIDENCY_VERTEX_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
           (write ? DXMT_RESOURCE_RESIDENCY_VERTEX_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
  case PipelineStage::Compute:
    return (read ? DXMT_RESOURCE_RESIDENCY_READ : DXMT_RESOURCE_RESIDENCY_NULL) |
           (write ? DXMT_RESOURCE_RESIDENCY_WRITE : DXMT_RESOURCE_RESIDENCY_NULL);
  }
  DXMT_UNREACHABLE;
}

enum DXMT_ENCODER_LIST_OP {
  DXMT_ENCODER_LIST_OP_SWAP = 0,
  DXMT_ENCODER_LIST_OP_SYNCHRONIZE = 1,
};

class CommandQueue;

enum DXMT_ENCODER_RESOURCE_ACESS {
  DXMT_ENCODER_RESOURCE_ACESS_READ = 1 <<0,
  DXMT_ENCODER_RESOURCE_ACESS_WRITE = 1 << 1,
};

struct AllocatedTempBufferSlice {
  WMT::Buffer gpu_buffer;
  uint64_t offset;
  uint64_t gpu_address;
};

class ArgumentEncodingContext {
  void
  trackBuffer(BufferAllocation *allocation, DXMT_ENCODER_RESOURCE_ACESS flags) {
    retainAllocation(allocation);
    if (allocation->flags().test(BufferAllocationFlag::GpuReadonly))
      return;
    if (flags & DXMT_ENCODER_RESOURCE_ACESS_READ)
      encoder_current->buf_read.add(allocation->depkey);
    if (flags & DXMT_ENCODER_RESOURCE_ACESS_WRITE)
      encoder_current->buf_write.add(allocation->depkey);
  }

  void
  trackTexture(TextureAllocation *allocation, DXMT_ENCODER_RESOURCE_ACESS flags) {
    retainAllocation(allocation);
    if (allocation->flags().test(TextureAllocationFlag::GpuReadonly))
      return;
    if (flags & DXMT_ENCODER_RESOURCE_ACESS_READ)
      encoder_current->tex_read.add(allocation->depkey);
    if (flags & DXMT_ENCODER_RESOURCE_ACESS_WRITE)
      encoder_current->tex_write.add(allocation->depkey);
  }

public:
  std::pair<BufferAllocation *, uint64_t>
  access(Rc<Buffer> const &buffer, unsigned offset, unsigned length, DXMT_ENCODER_RESOURCE_ACESS flags) {
    auto allocation = buffer->current();
    trackBuffer(allocation, flags);
    return {allocation, allocation->currentSuballocationOffset()};
  }

  std::pair<BufferView const &, uint32_t>
  access(Rc<Buffer> const &buffer, unsigned viewId, DXMT_ENCODER_RESOURCE_ACESS flags) {
    auto allocation = buffer->current();
    trackBuffer(allocation, flags);
    auto &view = buffer->view_(viewId, allocation);
    return {view, allocation->currentSuballocationOffset(view.suballocation_texel)};
  }

  std::pair<BufferAllocation *, uint64_t>
  access(Rc<Buffer> const &buffer, DXMT_ENCODER_RESOURCE_ACESS flags) {
    auto allocation = buffer->current();
    trackBuffer(allocation, flags);
    return {allocation, allocation->currentSuballocationOffset()};
  }

  WMT::Texture
  access(Rc<Texture> const &texture, unsigned level, unsigned slice, DXMT_ENCODER_RESOURCE_ACESS flags) {
    auto allocation = texture->current();
    trackTexture(allocation, flags);
    return allocation->texture();
  }

  TextureView const &
  access(Rc<Texture> const &texture, unsigned viewId, DXMT_ENCODER_RESOURCE_ACESS flags) {
    auto allocation = texture->current();
    trackTexture(allocation, flags);
    return texture->view_(viewId, allocation);
  }

  WMT::Texture
  access(Rc<Texture> const &texture, DXMT_ENCODER_RESOURCE_ACESS flags) {
    auto allocation = texture->current();
    trackTexture(allocation, flags);
    return allocation->texture();
  }

  template <PipelineStage stage>
  void
  bindConstantBuffer(unsigned slot, unsigned offset, Rc<Buffer> &&buffer) {
    unsigned idx = slot + 14 * unsigned(stage);
    auto &entry = cbuf_[idx];
    entry.offset = offset;
    entry.buffer = std::move(buffer);
  }

  template <PipelineStage stage>
  void
  bindConstantBufferOffset(unsigned slot, unsigned offset) {
    unsigned idx = slot + 14 * unsigned(stage);
    auto &entry = cbuf_[idx];
    entry.offset = offset;
  }

  template <PipelineStage stage>
  void
  bindSampler(unsigned slot, WMT::SamplerState sampler, uint64_t sampler_id, float bias) {
    unsigned idx = slot + 16 * unsigned(stage);
    auto &entry = sampler_[idx];
    entry.sampler = sampler;
    entry.sampler_id = sampler_id;
    entry.bias = bias;
  }

  template <PipelineStage stage>
  void
  bindBuffer(unsigned slot, Rc<Buffer> &&buffer, unsigned viewId, BufferSlice slice) {
    unsigned idx = slot + kSRVBindings * unsigned(stage);
    auto &entry = resview_[idx];
    entry.texture = {};
    entry.buffer = std::move(buffer);
    entry.viewId = viewId;
    entry.slice = slice;
  }

  template <PipelineStage stage>
  void
  bindTexture(unsigned slot, Rc<Texture> &&texture, unsigned viewId) {
    unsigned idx = slot + kSRVBindings * unsigned(stage);
    auto &entry = resview_[idx];
    entry.buffer = {};
    entry.texture = std::move(texture);
    entry.viewId = viewId;
  }

  template <PipelineStage stage>
  void bindOutputBuffer(unsigned slot, Rc<Buffer> &&buffer, unsigned viewId, Rc<Buffer> &&counter, BufferSlice slice);

  template <PipelineStage stage> void bindOutputTexture(unsigned slot, Rc<Texture> &&texture, unsigned viewId);

  void bindStreamOutputBuffer(unsigned slot, unsigned offset, Rc<Buffer> &&buffer);
  void bindStreamOutputBufferOffset(unsigned slot, unsigned offset);

  void
  bindVertexBuffer(unsigned slot, unsigned offset, unsigned stride, Rc<Buffer> &&buffer) {
    auto &entry = vbuf_[slot];
    entry.buffer = std::move(buffer);
    entry.offset = offset;
    entry.stride = stride;
  }

  void
  bindVertexBufferOffset(unsigned slot, unsigned offset, unsigned stride) {
    auto &entry = vbuf_[slot];
    entry.offset = offset;
    entry.stride = stride;
  }

  void
  bindIndexBuffer(Rc<Buffer> &&buffer) {
    ibuf_ = std::move(buffer);
  }

  std::pair<WMT::Buffer, uint64_t>
  currentIndexBuffer() {
    // because of indirect draw, we can't predicate the accessed buffer range
    auto [ibuf_alloc, offset] = access(ibuf_, 0, ibuf_->length(), DXMT_ENCODER_RESOURCE_ACESS_READ);
    return {ibuf_alloc->buffer(), offset};
  };

  void clearState() {
    vbuf_ = {{}};
    ibuf_ = {};
    cbuf_ = {{}};
    sampler_ = {{}};
    resview_ = {{}};
    om_uav_ = {{}};
    cs_uav_ = {{}};
  }

  template <PipelineKind kind> void encodeVertexBuffers(uint32_t ia_slot_mask, uint64_t argument_buffer_offset);
  template <PipelineStage stage, PipelineKind kind>
  void encodeConstantBuffers(
      const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
      uint64_t argument_buffer_offset
  );
  template <PipelineStage stage, PipelineKind kind>
  void encodeShaderResources(
      const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments,
      uint64_t argument_buffer_offset
  );

  void retainAllocation(Allocation* allocation);

  template <PipelineStage stage, PipelineKind kind>
  void
  makeResident(WMT::Resource resource, DXMT_RESOURCE_RESIDENCY requested) {
    if constexpr (stage == PipelineStage::Compute) {
      auto &cmd = encodeComputeCommand<wmtcmd_compute_useresource>();
      cmd.type = WMTComputeCommandUseResource;
      cmd.resource = resource;
      cmd.usage = GetUsageFromResidencyMask(requested);
    } else if constexpr (kind == PipelineKind::Tessellation) {
      auto &cmd = encodePreTessRenderCommand<wmtcmd_render_useresource>();
      cmd.type = WMTRenderCommandUseResource;
      cmd.resource = resource;
      cmd.usage = GetUsageFromResidencyMask(requested);
      cmd.stages = GetStagesFromResidencyMask(requested);
    } else {
      auto &cmd = encodeRenderCommand<wmtcmd_render_useresource>();
      cmd.type = WMTRenderCommandUseResource;
      cmd.resource = resource;
      cmd.usage = GetUsageFromResidencyMask(requested);
      cmd.stages = GetStagesFromResidencyMask(requested);
    }
  }

  template <PipelineStage stage, PipelineKind kind>
  void
  makeResident(Buffer *buffer, bool read = true, bool write = false) {
    auto allocation = buffer->current();
    uint64_t encoder_id = currentEncoder()->id;
    DXMT_RESOURCE_RESIDENCY requested = GetResidencyMask<kind>(stage, read, write);
    if (CheckResourceResidency(allocation->residencyState, encoder_id, requested)) {
      makeResident<stage, kind>(allocation->buffer(), requested);
    };
  }
  template <PipelineStage stage, PipelineKind kind>
  void
  makeResident(Buffer *buffer, unsigned viewId, bool read = true, bool write = false) {
    auto allocation = buffer->current();
    uint64_t encoder_id = currentEncoder()->id;
    DXMT_RESOURCE_RESIDENCY requested = GetResidencyMask<kind>(stage, read, write);
    if (CheckResourceResidency(buffer->residency(viewId, allocation), encoder_id, requested)) {
      makeResident<stage, kind>(buffer->view(viewId, allocation), requested);
    };
  }
  template <PipelineStage stage, PipelineKind kind>
  void
  makeResident(Texture *texture, unsigned viewId, bool read = true, bool write = false) {
    auto allocation = texture->current();
    uint64_t encoder_id = currentEncoder()->id;
    DXMT_RESOURCE_RESIDENCY requested = GetResidencyMask<kind>(stage, read, write);
    if (CheckResourceResidency(texture->residency(viewId, allocation), encoder_id, requested)) {
      makeResident<stage, kind>(texture->view(viewId, allocation), requested);
    };
  }

  template <typename cmd_struct>
  cmd_struct &
  encodeRenderCommand() {
    assert(encoder_current->type == EncoderType::Render);
    auto encoder = static_cast<RenderEncoderData *>(encoder_current);
    auto storage = (cmd_struct *)allocate_cpu_heap(sizeof(cmd_struct), 16);
    encoder->cmd_tail->next.set(storage);
    encoder->cmd_tail = (wmtcmd_base *)storage;
    storage->next.set(nullptr);
    return *storage;
  }

  void
  encodeGSDispatchArgumentsMarshal(
      WMT::Buffer draw_args, uint64_t draw_args_resource_id, uint32_t draw_args_offset, uint32_t vertex_count_per_warp,
      WMT::Buffer dispatch_args, uint64_t dispatch_args_resource_id, uint32_t write_offset
  ) {
    assert(encoder_current->type == EncoderType::Render);
    auto data = static_cast<RenderEncoderData *>(encoder_current);
    data->gs_arg_marshal_tasks.push_back(
        {draw_args, draw_args_resource_id + draw_args_offset, dispatch_args, dispatch_args_resource_id + write_offset,
         vertex_count_per_warp}
    );
  }

  template <typename cmd_struct>
  cmd_struct &
  encodePreTessRenderCommand() {
    assert(encoder_current->type == EncoderType::Render);
    auto encoder = static_cast<RenderEncoderData *>(encoder_current);
    auto storage = (cmd_struct *)allocate_cpu_heap(sizeof(cmd_struct), 16);
    encoder->pretess_cmd_tail->next.set(storage);
    encoder->pretess_cmd_tail = (wmtcmd_base *)storage;
    storage->next.set(nullptr);
    return *storage;
  }

  template <typename cmd_struct>
  cmd_struct &
  encodeComputeCommand() {
    assert(encoder_current->type == EncoderType::Compute);
    auto encoder = static_cast<ComputeEncoderData *>(encoder_current);
    auto storage = (cmd_struct *)allocate_cpu_heap(sizeof(cmd_struct), 16);
    encoder->cmd_tail->next.set(storage);
    encoder->cmd_tail = (wmtcmd_base *)storage;
    storage->next.set(nullptr);
    return *storage;
  }

  template <typename cmd_struct>
  cmd_struct &
  encodeBlitCommand() {
    assert(encoder_current->type == EncoderType::Blit);
    auto encoder = static_cast<BlitEncoderData *>(encoder_current);
    auto storage = (cmd_struct *)allocate_cpu_heap(sizeof(cmd_struct), 16);
    encoder->cmd_tail->next.set(storage);
    encoder->cmd_tail = (wmtcmd_base *)storage;
    storage->next.set(nullptr);
    return *storage;
  }

  template <typename T>
  T *
  allocate() {
    return (new (allocate_cpu_heap(sizeof(T), alignof(T))) T());
  };

  void present(Rc<Texture> &texture, Rc<Presenter> &presenter, double after);

  void upscale(Rc<Texture> &texture, Rc<Texture> &upscaled, WMT::Reference<WMT::FXSpatialScaler> &scaler);

  void upscaleTemporal(
      Rc<Texture> &input, Rc<Texture> &output, Rc<Texture> &depth, Rc<Texture> &motion_vector, TextureViewKey mvViewId,
      Rc<Texture> &exposure, WMT::Reference<WMT::FXTemporalScaler> &scaler, const WMTFXTemporalScalerProps &props
  );

  void signalEvent(uint64_t value);
  void signalEvent(WMT::Reference<WMT::Event> &&event, uint64_t value);
  void waitEvent(WMT::Reference<WMT::Event> &&event, uint64_t value);

  uint64_t
  nextEncoderId() {
    static std::atomic_uint64_t global_id = 0;
    return global_id.fetch_add(1);
  };

  void clearColor(Rc<Texture> &&texture, unsigned viewId, unsigned arrayLength, WMTClearColor color);
  void clearDepthStencil(
      Rc<Texture> &&texture, unsigned viewId, unsigned arrayLength, unsigned flag, float depth, uint8_t stencil
  );
  void resolveTexture(Rc<Texture> &&src, TextureViewKey src_view, Rc<Texture> &&dst, TextureViewKey dst_view);

  RenderEncoderData *startRenderPass(
      uint8_t dsv_planar_flags, uint8_t dsv_readonly_flags, uint8_t render_target_count, uint64_t argument_buffer_size
  );
  EncoderData *startComputePass(uint64_t argument_buffer_size);
  EncoderData *startBlitPass();

  void endPass();

  constexpr EncoderData *
  currentEncoder() {
    assert(encoder_current);
    return encoder_current;
  }

  constexpr RenderEncoderData *
  currentRenderEncoder() {
    assert(encoder_current->type == EncoderType::Render);
    return static_cast<RenderEncoderData *>(encoder_current);
  }

  void *
  allocate_cpu_heap(size_t size, size_t alignment) {
    std::size_t adjustment = align_forward_adjustment((void *)cpu_buffer_offset_, alignment);
    auto aligned = cpu_buffer_offset_ + adjustment;
    cpu_buffer_offset_ = aligned + size;
    if (cpu_buffer_offset_ >= kEncodingContextCPUHeapSize) {
      ERR(cpu_buffer_offset_, " - cpu argument heap overflow, expect error.");
    }
    return ptr_add(cpu_buffer_, aligned);
  }

  template <typename T, bool ComputeCommandEncoder = false>
  T *
  getMappedArgumentBuffer(size_t offset) {
    if constexpr (ComputeCommandEncoder)
      return reinterpret_cast<T *>(
          (char *)reinterpret_cast<ComputeEncoderData *>(encoder_current)->allocated_argbuf_mapping + offset
      );
    return reinterpret_cast<T *>(
        (char *)reinterpret_cast<RenderEncoderData *>(encoder_current)->allocated_argbuf_mapping + offset
    );
  }

  template <bool ComputeCommandEncoder = false>
  uint64_t
  getFinalArgumentBufferOffset(size_t offset) {
    if constexpr (ComputeCommandEncoder)
      return reinterpret_cast<ComputeEncoderData *>(encoder_current)->allocated_argbuf_offset + offset;
    return reinterpret_cast<RenderEncoderData *>(encoder_current)->allocated_argbuf_offset + offset;
  }

  std::pair<WMT::Buffer , size_t> allocateTempBuffer(size_t size, size_t alignment);
  
  AllocatedTempBufferSlice allocateTempBuffer1(size_t size, size_t alignment);

  std::unique_ptr<VisibilityResultReadback> flushCommands(WMT::CommandBuffer cmdbuf, uint64_t seqId, uint64_t event_seq_id);

  uint64_t currentSeqId() {return seq_id_;}

  uint64_t currentFrameId() {return frame_id_;}

  CommandQueue& queue() { return queue_;}

  void
  $$setEncodingContext(uint64_t seq_id, uint64_t frame_id);

  void bumpVisibilityResultOffset();
  void beginVisibilityResultQuery(Rc<VisibilityResultQuery> &&query);
  void endVisibilityResultQuery(Rc<VisibilityResultQuery> &&query);
  void
  pushDeferredVisibilityQuerys(Rc<VisibilityResultQuery> *list) {
    deferred_visibility_query_stack_.push_back(list);
  }
  void
  popDeferredVisibilityQuerys() {
    assert(!deferred_visibility_query_stack_.empty());
    deferred_visibility_query_stack_.pop_back();
  }
  Rc<VisibilityResultQuery>
  currentDeferredVisibilityQuery(uint32_t query_id) {
    assert(!deferred_visibility_query_stack_.empty());
    return deferred_visibility_query_stack_.back()[query_id];
  }

  FrameStatistics&
  currentFrameStatistics();

  void
  setCompatibilityFlag(FeatureCompatibility flag) {
    currentFrameStatistics().compatibility_flags.set(flag);
  }

  ArgumentEncodingContext(CommandQueue &queue, WMT::Device device, InternalCommandLibrary &lib);
  ~ArgumentEncodingContext();

  uint32_t tess_num_output_control_point_element;
  uint32_t tess_num_output_patch_constant_scalar;
  uint32_t tess_threads_per_patch;

  EmulatedCommandContext emulated_cmd;
  ClearRenderTargetContext clear_rt_cmd;

private:
  DXMT_ENCODER_LIST_OP checkEncoderRelation(EncoderData* former, EncoderData* latter);
  bool hasDataDependency(EncoderData* from, EncoderData* to);
  bool isEncoderSignatureMatched(RenderEncoderData* former, RenderEncoderData* latter);
  WMTColorAttachmentInfo *isClearColorSignatureMatched(ClearEncoderData* former, RenderEncoderData* latter);
  WMTDepthAttachmentInfo *isClearDepthSignatureMatched(ClearEncoderData* former, RenderEncoderData* latter);
  WMTStencilAttachmentInfo *isClearStencilSignatureMatched(ClearEncoderData* former, RenderEncoderData* latter);

  std::array<VertexBufferBinding, kVertexBufferSlots> vbuf_;
  Rc<Buffer> ibuf_;

  std::array<ConstantBufferBinding, 14 * kStages> cbuf_;
  std::array<SamplerBinding, 16 * kStages> sampler_;
  std::array<ResourceViewBinding, kSRVBindings * kStages> resview_;

  std::array<UnorderedAccessViewBinding, kUAVBindings> om_uav_;
  std::array<UnorderedAccessViewBinding, kUAVBindings> cs_uav_;

  WMT::Reference<WMT::SamplerState> dummy_sampler_;
  WMTSamplerInfo dummy_sampler_info_;
  WMT::Reference<WMT::Buffer> dummy_cbuffer_;
  void *dummy_cbuffer_host_;
  WMTBufferInfo dummy_cbuffer_info_;

  EncoderData encoder_head = {EncoderType::Null, nullptr};
  EncoderData *encoder_last = &encoder_head;
  EncoderData *encoder_current = nullptr;
  unsigned encoder_count_ = 0;

  void *cpu_buffer_;
  uint64_t cpu_buffer_offset_;
  uint64_t seq_id_;
  uint64_t frame_id_;

  VisibilityResultOffsetBumpState vro_state_;
  std::vector<Rc<VisibilityResultQuery>> pending_queries_;
  unsigned active_visibility_query_count_ = 0;
  Flags<FeatureCompatibility> compatibility_flag_;

  std::vector<Rc<VisibilityResultQuery> *> deferred_visibility_query_stack_;

  WMT::Device device_;
  CommandQueue& queue_;
};

template <>
inline void
ArgumentEncodingContext::bindOutputBuffer<PipelineStage::Compute>(
    unsigned slot, Rc<Buffer> &&buffer, unsigned viewId, Rc<Buffer> &&counter, BufferSlice slice
) {
  auto &entry = cs_uav_[slot];
  entry.texture = {};
  entry.buffer = std::move(buffer);
  entry.viewId = viewId;
  entry.counter = std::move(counter);
  entry.slice = slice;
}
template <>
inline void
ArgumentEncodingContext::bindOutputBuffer<PipelineStage::Pixel>(
    unsigned slot, Rc<Buffer> &&buffer, unsigned viewId, Rc<Buffer> &&counter, BufferSlice slice
) {
  auto &entry = om_uav_[slot];
  entry.texture = {};
  entry.buffer = std::move(buffer);
  entry.viewId = viewId;
  entry.counter = std::move(counter);
  entry.slice = slice;
}

template <>
inline void
ArgumentEncodingContext::bindOutputTexture<PipelineStage::Compute>(
    unsigned slot, Rc<Texture> &&texture, unsigned viewId
) {
  auto &entry = cs_uav_[slot];
  entry.buffer = {};
  entry.texture = std::move(texture);
  entry.viewId = viewId;
}
template <>
inline void
ArgumentEncodingContext::bindOutputTexture<PipelineStage::Pixel>(
    unsigned slot, Rc<Texture> &&texture, unsigned viewId
) {
  auto &entry = om_uav_[slot];
  entry.buffer = {};
  entry.texture = std::move(texture);
  entry.viewId = viewId;
}

} // namespace dxmt