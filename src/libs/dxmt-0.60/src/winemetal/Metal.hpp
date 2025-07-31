#pragma once

#include "./winemetal.h"
#include <cstddef>
#include <string>
#include <cstring>
#include <vector>

namespace WMT {

class String;

class Object {
public:
  obj_handle_t handle;

  Object() {
    handle = NULL_OBJECT_HANDLE;
  }

  Object(obj_handle_t h) {
    handle = h;
  }

  void
  retain() {
    NSObject_retain(handle);
  }

  void
  release() {
    NSObject_release(handle);
  }

  String description();

  bool
  operator==(std::nullptr_t) const {
    return handle == 0;
  }
  bool
  operator!=(std::nullptr_t) const {
    return handle != 0;
  }
  bool
  operator==(Object obj) const {
    return handle == obj.handle;
  }
  bool
  operator!=(Object obj) const {
    return handle != obj.handle;
  }

  operator bool() const {
    return handle != 0;
  }

  operator obj_handle_t() const {
    return handle;
  }
};

template <typename Class> class Reference : public Class {
public:
  Reference() {
    this->handle = NULL_OBJECT_HANDLE;
  }
  Reference(const Reference &copy) {
    this->handle = copy.handle;
    this->retain();
  }
  Reference(Reference &&move) {
    this->handle = move.handle;
    move.handle = NULL_OBJECT_HANDLE;
  }
  Reference(Class non_retained) {
    this->handle = non_retained.handle;
    this->retain();
  }
  Reference(obj_handle_t retained) {
    this->handle = retained;
  }

  Reference &
  operator=(const Reference &copy) {
    if (this->handle != NULL_OBJECT_HANDLE)
      this->release();
    this->handle = copy.handle;
    this->retain();
    return *this;
  }
  Reference &
  operator=(Class non_retained) {
    if (this->handle != NULL_OBJECT_HANDLE)
      this->release();
    this->handle = non_retained.handle;
    this->retain();
    return *this;
  }
  Reference &
  operator=(Reference &&move) {
    if (this->handle != NULL_OBJECT_HANDLE)
      this->release();
    this->handle = move.handle;
    move.handle = NULL_OBJECT_HANDLE;
    return *this;
  }
  Reference &
  operator=(std::nullptr_t) {
    if (this->handle != NULL_OBJECT_HANDLE)
      this->release();
    this->handle = NULL_OBJECT_HANDLE;
    return *this;
  }

  ~Reference() {
    if (this->handle == NULL_OBJECT_HANDLE)
      return;
    this->release();
    this->handle = NULL_OBJECT_HANDLE;
  }
};

template <typename Class> class Array : public Object {
public:
  uint64_t
  count() const {
    return NSArray_count(handle);
  }

  Class
  object(uint64_t index) const {
    return Class{NSArray_object(handle, index)};
  }
};

class String : public Object {
public:
  static String
  string(const char *data, WMTStringEncoding encoding) {
    return String{NSString_string(data, encoding)};
  }

  bool
  getCString(char *buffer, uint64_t maxLength, WMTStringEncoding encoding) {
    return NSString_getCString(handle, buffer, maxLength, encoding);
  }

  uint64_t
  lengthOfBytesUsingEncoding(WMTStringEncoding encoding) {
    return NSString_lengthOfBytesUsingEncoding(handle, encoding);
  }

  std::string
  getUTF8String() {
    auto len = lengthOfBytesUsingEncoding(WMTUTF8StringEncoding);
    char str[len + 1];
    getCString(str, len + 1, WMTUTF8StringEncoding);
    return str;
  }
};

inline String
Object::description() {
  return String{NSObject_description(handle)};
}

class Error : public Object {
public:
};

class Event : public Object {
public:
};

class SharedEvent : public Event {
public:
  uint64_t
  signaledValue() {
    return MTLSharedEvent_signaledValue(handle);
  }

  void
  signalValue(uint64_t value) {
    MTLSharedEvent_signalValue(handle, value);
  }
};

class Fence : public Object {
public:
};

class Resource : public Object {
public:
};

class Texture : public Resource {
public:
  Reference<Texture>
  newTextureView(
      WMTPixelFormat format, WMTTextureType texture_type, uint16_t level_start, uint16_t level_count,
      uint16_t slice_start, uint16_t slice_count, struct WMTTextureSwizzleChannels swizzle,
      uint64_t &out_gpu_resource_id
  ) {
    return Reference<Texture>(MTLTexture_newTextureView(
        handle, format, texture_type, level_start, level_count, slice_start, slice_count, swizzle, &out_gpu_resource_id
    ));
  }

  WMTPixelFormat
  pixelFormat() {
    return MTLTexture_pixelFormat(handle);
  }

  uint64_t
  width() {
    return MTLTexture_width(handle);
  }

  uint64_t
  height() {
    return MTLTexture_height(handle);
  }

  uint64_t
  depth() {
    return MTLTexture_depth(handle);
  }

  uint64_t
  arrayLength() {
    return MTLTexture_arrayLength(handle);
  }

  uint64_t
  mipmapLevelCount() {
    return MTLTexture_mipmapLevelCount(handle);
  }

  void
  replaceRegion(
      WMTOrigin origin, WMTSize size, uint64_t level, uint64_t slice, const void *pixelBytes, uint64_t bytesPerRow,
      uint64_t bytesPerImage
  ) {
    WMTMemoryPointer data;
    data.set((void *)pixelBytes);
    return MTLTexture_replaceRegion(handle, origin, size, level, slice, data, bytesPerRow, bytesPerImage);
  }
};

class Buffer : public Resource {
public:
  Reference<Texture>
  newTexture(WMTTextureInfo &info, uint64_t offset, uint64_t bytes_per_row) {
    return Reference<Texture>(MTLBuffer_newTexture(handle, &info, offset, bytes_per_row));
  }

  void
  didModifyRange(uint64_t start, uint64_t length) {
    MTLBuffer_didModifyRange(handle, start, length);
  }
};

class SamplerState : public Object {
public:
};

class DepthStencilState : public Object {
public:
};

class CommandEncoder : public Object {
public:
  void
  endEncoding() {
    MTLCommandEncoder_endEncoding(handle);
  }

  void
  setLabel(String label) {
    MTLCommandEncoder_setLabel(handle, label);
  }
};

class ComputePipelineState : public Object {
public:
};

class RenderPipelineState : public Object {
public:
};

class RenderCommandEncoder : public CommandEncoder {
public:
  void
  encodeCommands(const wmtcmd_render_nop *cmd_head) {
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)cmd_head);
  }

  void
  setVertexBuffer(Buffer buffer, uint64_t offset, uint8_t index) {
    struct wmtcmd_render_setbuffer cmd;
    cmd.type = WMTRenderCommandSetVertexBuffer;
    cmd.next.set(nullptr);
    cmd.buffer = buffer;
    cmd.offset = offset;
    cmd.index = index;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  setFragmentBuffer(Buffer buffer, uint64_t offset, uint8_t index) {
    struct wmtcmd_render_setbuffer cmd;
    cmd.type = WMTRenderCommandSetFragmentBuffer;
    cmd.next.set(nullptr);
    cmd.buffer = buffer;
    cmd.offset = offset;
    cmd.index = index;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  setMeshBuffer(Buffer buffer, uint64_t offset, uint8_t index) {
    struct wmtcmd_render_setbuffer cmd;
    cmd.type = WMTRenderCommandSetMeshBuffer;
    cmd.next.set(nullptr);
    cmd.buffer = buffer;
    cmd.offset = offset;
    cmd.index = index;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  setObjectBuffer(Buffer buffer, uint64_t offset, uint8_t index) {
    struct wmtcmd_render_setbuffer cmd;
    cmd.type = WMTRenderCommandSetObjectBuffer;
    cmd.next.set(nullptr);
    cmd.buffer = buffer;
    cmd.offset = offset;
    cmd.index = index;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  memoryBarrier(WMTBarrierScope scope, WMTRenderStages stages_after, WMTRenderStages stages_before) {
    struct wmtcmd_render_memory_barrier cmd;
    cmd.type = WMTRenderCommandMemoryBarrier;
    cmd.next.set(nullptr);
    cmd.scope = scope;
    cmd.stages_before = stages_before;
    cmd.stages_after = stages_after;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  useResource(Resource resource, WMTResourceUsage usage, WMTRenderStages stages) {
    struct wmtcmd_render_useresource cmd;
    cmd.type = WMTRenderCommandUseResource;
    cmd.next.set(nullptr);
    cmd.resource = resource;
    cmd.usage = usage;
    cmd.stages = stages;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  };

  void
  setRenderPipelineState(RenderPipelineState pso) {
    struct wmtcmd_render_setpso cmd;
    cmd.type = WMTRenderCommandSetPSO;
    cmd.next.set(nullptr);
    cmd.pso = pso;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  drawPrimitives(WMTPrimitiveType primitive_type, uint32_t vertex_start, uint32_t vertex_count) {
    struct wmtcmd_render_draw cmd;
    cmd.type = WMTRenderCommandDraw;
    cmd.next.set(nullptr);
    cmd.primitive_type = primitive_type;
    cmd.vertex_start = vertex_start;
    cmd.vertex_count = vertex_count;
    cmd.base_instance = 0;
    cmd.instance_count = 1;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  setFragmentTexture(Texture texture, uint8_t index) {
    struct wmtcmd_render_settexture cmd;
    cmd.type = WMTRenderCommandSetFragmentTexture;
    cmd.next.set(nullptr);
    cmd.texture = texture;
    cmd.index = index;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  };

  void
  setFragmentBytes(const void *buf, uint64_t length, uint8_t index) {
    struct wmtcmd_render_setbytes cmd;
    cmd.type = WMTRenderCommandSetFragmentBytes;
    cmd.next.set(nullptr);
    cmd.bytes.set((void *)buf);
    cmd.length = length;
    cmd.index = index;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  setViewport(WMTViewport viewport) {
    struct wmtcmd_render_setviewports cmd;
    cmd.type = WMTRenderCommandSetViewports;
    cmd.next.set(nullptr);
    cmd.viewports.set(&viewport);
    cmd.viewport_count = 1;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  };

  void
  waitForFence(Fence fence, WMTRenderStages before) {
    struct wmtcmd_render_fence_op cmd;
    cmd.type = WMTRenderCommandWaitForFence;
    cmd.next.set(nullptr);
    cmd.fence = fence.handle;
    cmd.stages = before;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  updateFence(Fence fence, WMTRenderStages after) {
    struct wmtcmd_render_fence_op cmd;
    cmd.type = WMTRenderCommandUpdateFence;
    cmd.next.set(nullptr);
    cmd.fence = fence.handle;
    cmd.stages = after;
    MTLRenderCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }
};

class BlitCommandEncoder : public CommandEncoder {
public:
  void
  encodeCommands(const wmtcmd_blit_nop *cmd_head) {
    MTLBlitCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)cmd_head);
  }

  void
  waitForFence(Fence fence) {
    struct wmtcmd_blit_fence_op cmd;
    cmd.type = WMTBlitCommandWaitForFence;
    cmd.next.set(nullptr);
    cmd.fence = fence.handle;
    MTLBlitCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  updateFence(Fence fence) {
    struct wmtcmd_blit_fence_op cmd;
    cmd.type = WMTBlitCommandUpdateFence;
    cmd.next.set(nullptr);
    cmd.fence = fence.handle;
    MTLBlitCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }
};

class ComputeCommandEncoder : public CommandEncoder {
public:
  void
  encodeCommands(const wmtcmd_compute_nop *cmd_head) {
    MTLComputeCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)cmd_head);
  }

  void
  waitForFence(Fence fence) {
    struct wmtcmd_compute_fence_op cmd;
    cmd.type = WMTComputeCommandWaitForFence;
    cmd.next.set(nullptr);
    cmd.fence = fence.handle;
    MTLComputeCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }

  void
  updateFence(Fence fence) {
    struct wmtcmd_compute_fence_op cmd;
    cmd.type = WMTComputeCommandUpdateFence;
    cmd.next.set(nullptr);
    cmd.fence = fence.handle;
    MTLComputeCommandEncoder_encodeCommands(handle, (const wmtcmd_base *)&cmd);
  }
};

class MetalDrawable : public Object {
public:
  Texture
  texture() {
    return Texture{MetalDrawable_texture(handle)};
  };
};

class MetalLayer : public Object {
public:
  MetalDrawable
  nextDrawable() {
    return MetalDrawable{MetalLayer_nextDrawable(handle)};
  };

  void
  setProps(const WMTLayerProps &props) {
    MetalLayer_setProps(handle, &props);
  };

  void
  getProps(WMTLayerProps &props) {
    MetalLayer_getProps(handle, &props);
  };

  bool
  setColorSpace(WMTColorSpace colorspace) {
    return MetalLayer_setColorSpace(handle, colorspace);
  };
};

class FXTemporalScaler : public Object {
public:
};

class FXSpatialScaler : public Object {
public:
};

class LogContainer : public Object {
public:
  std::vector<Object>
  elements() {
    std::vector<Object> ret;
    uint64_t read = 0;
    uint64_t start;
    do {
      start = ret.size();
      ret.resize(start + 4);
      read = MTLLogContainer_enumerate(handle, start, 4, (obj_handle_t *)ret.data() + start);
    } while (read == 4);
    ret.resize(start + read);
    return ret;
  };
};

class CommandBuffer : public Object {
public:
  void
  commit() {
    return MTLCommandBuffer_commit(handle);
  }

  void
  waitUntilCompleted() {
    return MTLCommandBuffer_waitUntilCompleted(handle);
  }

  WMTCommandBufferStatus
  status() {
    return MTLCommandBuffer_status(handle);
  }

  Error
  error() {
    return Error{MTLCommandBuffer_error(handle)};
  }

  LogContainer
  logs() {
    return LogContainer{MTLCommandBuffer_logs(handle)};
  }

  void
  encodeSignalEvent(Event event, uint64_t value) {
    return MTLCommandBuffer_encodeSignalEvent(handle, event.handle, value);
  }

  void
  encodeWaitForEvent(Event event, uint64_t value) {
    return MTLCommandBuffer_encodeWaitForEvent(handle, event.handle, value);
  }

  RenderCommandEncoder
  renderCommandEncoder(WMTRenderPassInfo &info) {
    return RenderCommandEncoder{MTLCommandBuffer_renderCommandEncoder(handle, &info)};
  }

  BlitCommandEncoder
  blitCommandEncoder() {
    return BlitCommandEncoder{MTLCommandBuffer_blitCommandEncoder(handle)};
  }

  ComputeCommandEncoder
  computeCommandEncoder(bool concurrent) {
    return ComputeCommandEncoder{MTLCommandBuffer_computeCommandEncoder(handle, concurrent)};
  }

  void
  presentDrawable(MetalDrawable drawable) {
    MTLCommandBuffer_presentDrawable(handle, drawable);
  }

  void
  presentDrawableAfterMinimumDuration(MetalDrawable drawable, double after) {
    MTLCommandBuffer_presentDrawableAfterMinimumDuration(handle, drawable, after);
  }

  void
  encodeSpatialScale(FXSpatialScaler scaler, Texture color, Texture output, Fence fence) {
    MTLCommandBuffer_encodeSpatialScale(handle, scaler, color, output, fence);
  }

  void
  encodeTemporalScale(
      FXTemporalScaler scaler, Texture color, Texture output, Texture depth, Texture motion, Texture exposure,
      Fence fence, const WMTFXTemporalScalerProps &props
  ) {
    MTLCommandBuffer_encodeTemporalScale(handle, scaler, color, output, depth, motion, exposure, fence, &props);
  }
};

class CommandQueue : public Object {
public:
  CommandBuffer
  commandBuffer() {
    return CommandBuffer{MTLCommandQueue_commandBuffer(handle)};
  }
};

class Function : public Object {};

class Library : public Object {
public:
  Reference<Function>
  newFunction(const char *name) {
    return Reference<Function>(MTLLibrary_newFunction(handle, name));
  }

  Reference<Function>
  newFunctionWithConstants(
      const char *name, const WMTFunctionConstant *constants, uint32_t num_constants, Error &error
  ) {
    return Reference<Function>(
        MTLLibrary_newFunctionWithConstants(handle, name, constants, num_constants, &error.handle)
    );
  }
};

class Device : public Object {
public:
  uint64_t
  recommendedMaxWorkingSetSize() const {
    return MTLDevice_recommendedMaxWorkingSetSize(handle);
  }

  uint64_t
  currentAllocatedSize() const {
    return MTLDevice_currentAllocatedSize(handle);
  }

  String
  name() const {
    return String{MTLDevice_name(handle)};
  }

  Reference<CommandQueue>
  newCommandQueue(uint64_t maxCommandBufferCount) {
    return Reference<CommandQueue>(MTLDevice_newCommandQueue(handle, maxCommandBufferCount));
  }

  Reference<SharedEvent>
  newSharedEvent() {
    return Reference<SharedEvent>(MTLDevice_newSharedEvent(handle));
  }

  Reference<Buffer>
  newBuffer(WMTBufferInfo &info) {
    return Reference<Buffer>(MTLDevice_newBuffer(handle, &info));
  }

  Reference<SamplerState>
  newSamplerState(WMTSamplerInfo &info) {
    return Reference<SamplerState>(MTLDevice_newSamplerState(handle, &info));
  }

  Reference<DepthStencilState>
  newDepthStencilState(const WMTDepthStencilInfo &info) {
    return Reference<DepthStencilState>(MTLDevice_newDepthStencilState(handle, &info));
  }

  Reference<Texture>
  newTexture(WMTTextureInfo &info) {
    return Reference<Texture>(MTLDevice_newTexture(handle, &info));
  }

  Reference<Library>
  newLibrary(const void *bytecode, uint64_t bytecode_length, Error &error) {
    struct WMTMemoryPointer mem;
    mem.set((void *)bytecode);
    return Reference<Library>(MTLDevice_newLibrary(handle, mem, bytecode_length, &error.handle));
  }

  Reference<Library>
  newLibraryFromNativeBuffer(uint64_t bytecode, uint64_t bytecode_length, Error &error) {
    struct WMTMemoryPointer mem;
    memcpy(&mem, &bytecode, 8);
    return Reference<Library>(MTLDevice_newLibrary(handle, mem, bytecode_length, &error.handle));
  }

  Reference<ComputePipelineState>
  newComputePipelineState(const Function &compute_function, Error &error) {
    return Reference<ComputePipelineState>(
        MTLDevice_newComputePipelineState(handle, compute_function.handle, &error.handle)
    );
  }

  Reference<RenderPipelineState>
  newRenderPipelineState(const WMTRenderPipelineInfo &info, Error &error) {
    return Reference<RenderPipelineState>(MTLDevice_newRenderPipelineState(handle, &info, &error.handle));
  }

  Reference<RenderPipelineState>
  newRenderPipelineState(const WMTMeshRenderPipelineInfo &info, Error &error) {
    return Reference<RenderPipelineState>(MTLDevice_newMeshRenderPipelineState(handle, &info, &error.handle));
  }

  Reference<Fence>
  newFence() {
    return Reference<Fence>(MTLDevice_newFence(handle));
  }

  Reference<Event>
  newEvent() {
    return Reference<Event>(MTLDevice_newEvent(handle));
  }

  uint64_t
  minimumLinearTextureAlignmentForPixelFormat(WMTPixelFormat format) {
    return MTLDevice_minimumLinearTextureAlignmentForPixelFormat(handle, format);
  }

  bool
  supportsFamily(WMTGPUFamily gpu_family) {
    return MTLDevice_supportsFamily(handle, gpu_family);
  }

  bool
  supportsBCTextureCompression() {
    return MTLDevice_supportsBCTextureCompression(handle);
  }

  bool
  supportsTextureSampleCount(uint8_t sample_count) {
    return MTLDevice_supportsTextureSampleCount(handle, sample_count);
  }

  bool
  hasUnifiedMemory() {
    return MTLDevice_hasUnifiedMemory(handle);
  }

  Reference<FXTemporalScaler>
  newTemporalScaler(const WMTFXTemporalScalerInfo &info) {
    return Reference<FXTemporalScaler>(MTLDevice_newTemporalScaler(handle, &info));
  }

  Reference<FXSpatialScaler>
  newSpatialScaler(const WMTFXSpatialScalerInfo &info) {
    return Reference<FXSpatialScaler>(MTLDevice_newSpatialScaler(handle, &info));
  }

  bool
  supportsFXSpatialScaler() {
    return MTLDevice_supportsFXSpatialScaler(handle);
  }

  bool
  supportsFXTemporalScaler() {
    return MTLDevice_supportsFXTemporalScaler(handle);
  }

  void
  setShouldMaximizeConcurrentCompilation(bool value) {
    MTLDevice_setShouldMaximizeConcurrentCompilation(handle, value);
  }
};

inline Reference<Array<Device>>
CopyAllDevices() {
  return Reference<Array<Device>>(WMTCopyAllDevices());
}

class CaptureManager : public Object {
public:
  static CaptureManager
  sharedCaptureManager() {
    return CaptureManager{MTLCaptureManager_sharedCaptureManager()};
  };

  bool
  startCapture(WMTCaptureInfo &info) {
    return MTLCaptureManager_startCapture(handle, &info);
  };

  void
  stopCapture() {
    MTLCaptureManager_stopCapture(handle);
  };
};

class DeveloperHUDProperties : public Object {
public:
  static DeveloperHUDProperties
  instance() {
    return DeveloperHUDProperties{DeveloperHUDProperties_instance()};
  };

  bool
  addLabel(String label, String after) {
    return DeveloperHUDProperties_addLabel(handle, label, after);
  }

  void
  updateLabel(String label, String value) {
    DeveloperHUDProperties_updateLabel(handle, label, value);
  }

  void
  remove(String label) {
    DeveloperHUDProperties_remove(handle, label);
  }
};

inline Reference<Object>
MakeAutoreleasePool() {
  return Reference<Object>(NSAutoreleasePool_alloc_init());
}

inline Reference<String>
MakeString(const char *data, WMTStringEncoding encoding) {
  return Reference<String>(NSString_alloc_init(data, encoding));
}

inline Object
CreateMetalViewFromHWND(intptr_t hwnd, Device device, MetalLayer &layer) {
  return {::CreateMetalViewFromHWND(hwnd, device.handle, &layer.handle)};
}

inline void
ReleaseMetalView(Object view) {
  return ::ReleaseMetalView(view.handle);
}

inline void
InitializeRenderPassInfo(WMTRenderPassInfo &info) {
  std::memset(&info, 0, sizeof(WMTRenderPassInfo));
  for (unsigned i = 0; i < 8; i++) {
    info.colors[i].store_action = WMTStoreActionStore;
    info.colors[i].clear_color.a = 1.0f;
  }
  info.stencil.load_action = WMTLoadActionClear;
  info.depth.load_action = WMTLoadActionClear;
  info.depth.clear_depth = 1.0f;
};

inline void
InitializeRenderPipelineInfo(WMTRenderPipelineInfo &info) {
  for (unsigned i = 0; i < 8; i++) {
    info.colors[i].pixel_format = WMTPixelFormatInvalid;
    info.colors[i].write_mask = WMTColorWriteMaskAll;
    info.colors[i].blending_enabled = false;
    info.colors[i].alpha_blend_operation = WMTBlendOperationAdd;
    info.colors[i].rgb_blend_operation = WMTBlendOperationAdd;
    info.colors[i].dst_alpha_blend_factor = WMTBlendFactorZero;
    info.colors[i].dst_rgb_blend_factor = WMTBlendFactorZero;
    info.colors[i].src_alpha_blend_factor = WMTBlendFactorOne;
    info.colors[i].src_rgb_blend_factor = WMTBlendFactorOne;
  }
  info.alpha_to_coverage_enabled = false;
  info.rasterization_enabled = true;
  info.raster_sample_count = 1;
  info.depth_pixel_format = WMTPixelFormatInvalid;
  info.stencil_pixel_format = WMTPixelFormatInvalid;
  info.logic_operation_enabled = false;
  info.logic_operation = WMTLogicOperationClear;

  info.input_primitive_topology = WMTPrimitiveTopologyClassUnspecified;
  info.max_tessellation_factor = 16.0f;
  info.tessellation_factor_step = WMTTessellationFactorStepFunctionConstant;
  info.tessellation_partition_mode = WMTTessellationPartitionModePow2;
  info.tessellation_output_winding_order = WMTWindingClockwise;

  info.vertex_function = NULL_OBJECT_HANDLE;
  info.fragment_function = NULL_OBJECT_HANDLE;
  info.immutable_vertex_buffers = 0;
  info.immutable_fragment_buffers = 0;
}

inline void
InitializeMeshRenderPipelineInfo(WMTMeshRenderPipelineInfo &info) {
  for (unsigned i = 0; i < 8; i++) {
    info.colors[i].pixel_format = WMTPixelFormatInvalid;
    info.colors[i].write_mask = WMTColorWriteMaskAll;
    info.colors[i].blending_enabled = false;
    info.colors[i].alpha_blend_operation = WMTBlendOperationAdd;
    info.colors[i].rgb_blend_operation = WMTBlendOperationAdd;
    info.colors[i].dst_alpha_blend_factor = WMTBlendFactorZero;
    info.colors[i].dst_rgb_blend_factor = WMTBlendFactorZero;
    info.colors[i].src_alpha_blend_factor = WMTBlendFactorOne;
    info.colors[i].src_rgb_blend_factor = WMTBlendFactorOne;
  }
  info.alpha_to_coverage_enabled = false;
  info.rasterization_enabled = true;
  info.raster_sample_count = 1;
  info.depth_pixel_format = WMTPixelFormatInvalid;
  info.stencil_pixel_format = WMTPixelFormatInvalid;
  info.logic_operation_enabled = false;
  info.logic_operation = WMTLogicOperationClear;

  info.object_function = NULL_OBJECT_HANDLE;
  info.mesh_function = NULL_OBJECT_HANDLE;
  info.fragment_function = NULL_OBJECT_HANDLE;
  info.immutable_object_buffers = 0;
  info.immutable_mesh_buffers = 0;
  info.immutable_fragment_buffers = 0;
  info.payload_memory_length = 0;
}

} // namespace WMT