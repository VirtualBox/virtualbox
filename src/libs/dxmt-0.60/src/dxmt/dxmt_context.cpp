#include "dxmt_context.hpp"
#include "Metal.hpp"
#include "dxmt_command_queue.hpp"
#include "dxmt_format.hpp"
#include "dxmt_occlusion_query.hpp"
#include "dxmt_presenter.hpp"
#include "wsi_platform.hpp"
#include <cstdint>
#include <cfloat>

namespace dxmt {

ArgumentEncodingContext::ArgumentEncodingContext(CommandQueue &queue, WMT::Device device, InternalCommandLibrary &lib) :
    emulated_cmd(device, lib, *this),
    clear_rt_cmd(device, lib, *this),
    device_(device),
    queue_(queue) {
  dummy_sampler_info_.support_argument_buffers = true;
  dummy_sampler_info_.border_color = WMTSamplerBorderColorTransparentBlack;
  dummy_sampler_info_.compare_function = WMTCompareFunctionNever;
  dummy_sampler_info_.normalized_coords = true;
  dummy_sampler_info_.r_address_mode = WMTSamplerAddressModeClampToEdge;
  dummy_sampler_info_.s_address_mode = WMTSamplerAddressModeClampToEdge;
  dummy_sampler_info_.t_address_mode = WMTSamplerAddressModeClampToEdge;
  dummy_sampler_info_.min_filter = WMTSamplerMinMagFilterNearest;
  dummy_sampler_info_.mag_filter = WMTSamplerMinMagFilterNearest;
  dummy_sampler_info_.mip_filter = WMTSamplerMipFilterNotMipmapped;
  dummy_sampler_info_.lod_min_clamp = 0.0f;
  dummy_sampler_info_.lod_max_clamp = FLT_MAX;
  dummy_sampler_info_.max_anisotroy = 1;
  dummy_sampler_info_.lod_average = false;
  dummy_sampler_ = device.newSamplerState(dummy_sampler_info_);
  dummy_cbuffer_host_ = wsi::aligned_malloc(65536, DXMT_PAGE_SIZE);
  dummy_cbuffer_info_.length = 65536;
  dummy_cbuffer_info_.memory.set(dummy_cbuffer_host_);
  dummy_cbuffer_info_.options = WMTResourceOptionCPUCacheModeWriteCombined | WMTResourceStorageModeShared |
                                WMTResourceHazardTrackingModeUntracked;
  dummy_cbuffer_ = device.newBuffer(dummy_cbuffer_info_);
  std::memset(dummy_cbuffer_info_.memory.get(), 0, 65536);
  cpu_buffer_ = malloc(kEncodingContextCPUHeapSize);
};

ArgumentEncodingContext::~ArgumentEncodingContext() {
  free(cpu_buffer_);
  wsi::aligned_free(dummy_cbuffer_host_);
};

template void ArgumentEncodingContext::encodeVertexBuffers<PipelineKind::Ordinary>(uint32_t slot_mask, uint64_t argument_buffer_offset);
template void ArgumentEncodingContext::encodeVertexBuffers<PipelineKind::Tessellation>(uint32_t slot_mask, uint64_t argument_buffer_offset);
template void ArgumentEncodingContext::encodeVertexBuffers<PipelineKind::Geometry>(uint32_t slot_mask, uint64_t argument_buffer_offset);

template <PipelineKind kind>
void
ArgumentEncodingContext::encodeVertexBuffers(uint32_t slot_mask, uint64_t offset) {
  struct VERTEX_BUFFER_ENTRY {
    uint64_t buffer_handle;
    uint32_t stride;
    uint32_t length;
  };
  uint32_t max_slot = 32 - __builtin_clz(slot_mask);

  VERTEX_BUFFER_ENTRY *entries = getMappedArgumentBuffer<VERTEX_BUFFER_ENTRY>(offset);

  for (unsigned slot = 0, index = 0; slot < max_slot; slot++) {
    if (!(slot_mask & (1 << slot)))
      continue;
    auto &state = vbuf_[slot];
    auto &buffer = state.buffer;
    if (!buffer.ptr()) {
      entries[index].buffer_handle = 0;
      entries[index].stride = 0;
      entries[index++].length = 0;
      continue;
    }
    auto length = buffer->length();
    auto [buffer_alloc, buffer_offset] = access(buffer, DXMT_ENCODER_RESOURCE_ACESS_READ);
    entries[index].buffer_handle = buffer_alloc->gpuAddress() + buffer_offset + state.offset;
    entries[index].stride = state.stride;
    entries[index++].length = length > state.offset ? length - state.offset : 0;
    // FIXME: did we intended to use the whole buffer?
    makeResident<PipelineStage::Vertex, kind>(buffer.ptr());
  };
  if constexpr (kind == PipelineKind::Tessellation) {
    auto &cmd = encodePreTessRenderCommand<wmtcmd_render_setbufferoffset>();
    cmd.offset = getFinalArgumentBufferOffset(offset);
    cmd.index = 16;
    cmd.type = WMTRenderCommandSetObjectBufferOffset;
  }
  else {
    auto &cmd = encodeRenderCommand<wmtcmd_render_setbufferoffset>();
    cmd.offset = getFinalArgumentBufferOffset(offset);
    cmd.index = 16;
    if constexpr (kind == PipelineKind::Geometry)
      cmd.type = WMTRenderCommandSetObjectBufferOffset;
    else
      cmd.type = WMTRenderCommandSetVertexBufferOffset;
  }
}

template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Vertex, PipelineKind::Ordinary>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Pixel, PipelineKind::Ordinary>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Vertex, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Pixel, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Hull, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Domain, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Compute, PipelineKind::Ordinary>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Vertex, PipelineKind::Geometry>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Geometry, PipelineKind::Geometry>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeConstantBuffers<PipelineStage::Pixel, PipelineKind::Geometry>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *constant_buffers,
    uint64_t argument_buffer_offset
);

template <PipelineStage stage, PipelineKind kind>
void
ArgumentEncodingContext::encodeConstantBuffers(const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT * constant_buffers, uint64_t offset) {
  uint64_t *encoded_buffer = getMappedArgumentBuffer<uint64_t, stage == PipelineStage::Compute>(offset);

  for (unsigned i = 0; i < reflection->NumConstantBuffers; i++) {
    auto &arg = constant_buffers[i];
    auto slot = 14 * unsigned(stage) + arg.SM50BindingSlot;
    switch (arg.Type) {
    case SM50BindingType::ConstantBuffer: {
      auto &cbuf = cbuf_[slot];
      if (!cbuf.buffer.ptr()) {
        encoded_buffer[arg.StructurePtrOffset] = dummy_cbuffer_info_.gpu_address;
        makeResident<stage, kind>(dummy_cbuffer_, GetResidencyMask<kind>(stage, true, false));
        continue;
      }
      auto argbuf = cbuf.buffer;
      // FIXME: did we intended to use the whole buffer?
      auto [argbuf_alloc, argbuf_offset] = access(argbuf, DXMT_ENCODER_RESOURCE_ACESS_READ);
      encoded_buffer[arg.StructurePtrOffset] = argbuf_alloc->gpuAddress() + argbuf_offset + cbuf.offset;
      makeResident<stage, kind>(argbuf.ptr());
      break;
    }
    default:
      DXMT_UNREACHABLE
    }
  }

  /* kConstantBufferTableBinding = 29 */
  if constexpr (stage == PipelineStage::Compute) {
    auto &cmd = encodeComputeCommand<wmtcmd_compute_setbufferoffset>();
    cmd.type = WMTComputeCommandSetBufferOffset;
    cmd.offset = getFinalArgumentBufferOffset<true>(offset);
    cmd.index = 29;
  } else if constexpr (kind == PipelineKind::Tessellation && (stage == PipelineStage::Hull || stage == PipelineStage::Vertex)) {
    auto &cmd = encodePreTessRenderCommand<wmtcmd_render_setbufferoffset>();
    cmd.offset = getFinalArgumentBufferOffset(offset);
    cmd.index = 29;
    if constexpr (stage == PipelineStage::Vertex)
      cmd.type = WMTRenderCommandSetObjectBufferOffset;
    else
      cmd.type = WMTRenderCommandSetMeshBufferOffset;
  } else {
    auto &cmd = encodeRenderCommand<wmtcmd_render_setbufferoffset>();
    cmd.offset = getFinalArgumentBufferOffset(offset);
    cmd.index = 29;
    if constexpr (stage == PipelineStage::Vertex) {
      if constexpr (kind == PipelineKind::Geometry)
        cmd.type = WMTRenderCommandSetObjectBufferOffset;
      else
        cmd.type = WMTRenderCommandSetVertexBufferOffset;
    } else if constexpr (stage == PipelineStage::Pixel) {
      cmd.type = WMTRenderCommandSetFragmentBufferOffset;
    } else if constexpr (stage == PipelineStage::Domain) {
      cmd.type = WMTRenderCommandSetVertexBufferOffset;
    } else if constexpr (stage == PipelineStage::Geometry) {
      cmd.type = WMTRenderCommandSetMeshBufferOffset;
    } else {
      assert(0 && "Not implemented or unreachable");
    }
  }
};

template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Vertex, PipelineKind::Ordinary>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Pixel, PipelineKind::Ordinary>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Vertex, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Pixel, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Hull, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Domain, PipelineKind::Tessellation>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Compute, PipelineKind::Ordinary>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Vertex, PipelineKind::Geometry>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Geometry, PipelineKind::Geometry>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);
template void ArgumentEncodingContext::encodeShaderResources<PipelineStage::Pixel, PipelineKind::Geometry>(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t argument_buffer_offset
);

template <PipelineStage stage, PipelineKind kind>
void
ArgumentEncodingContext::encodeShaderResources(
    const MTL_SHADER_REFLECTION *reflection, const MTL_SM50_SHADER_ARGUMENT *arguments, uint64_t offset
) {
  auto BindingCount = reflection->NumArguments;
  uint64_t *encoded_buffer = getMappedArgumentBuffer<uint64_t, stage == PipelineStage::Compute>(offset);

  auto &UAVBindingSet = stage == PipelineStage::Compute ? cs_uav_ : om_uav_;

  for (unsigned i = 0; i < BindingCount; i++) {
    auto &arg = arguments[i];
    switch (arg.Type) {
    case SM50BindingType::ConstantBuffer: {
      DXMT_UNREACHABLE
    }
    case SM50BindingType::Sampler: {
      auto slot = 16 * unsigned(stage) + arg.SM50BindingSlot;
      if (!sampler_[slot].sampler) {
        encoded_buffer[arg.StructurePtrOffset] = dummy_sampler_info_.gpu_resource_id;
        encoded_buffer[arg.StructurePtrOffset + 1] = (uint64_t)std::bit_cast<uint32_t>(0.0f);
        break;
      }
      encoded_buffer[arg.StructurePtrOffset] = sampler_[slot].sampler_id;
      encoded_buffer[arg.StructurePtrOffset + 1] = (uint64_t)std::bit_cast<uint32_t>(sampler_[slot].bias);
      break;
    }
    case SM50BindingType::SRV: {
      auto slot = 128 * unsigned(stage) + arg.SM50BindingSlot;
      auto &srv = resview_[slot];

      if (arg.Flags & MTL_SM50_SHADER_ARGUMENT_BUFFER) {
        if (srv.buffer.ptr()) {
          auto [srv_alloc, offset] = access(srv.buffer, srv.slice.byteOffset, srv.slice.byteLength, DXMT_ENCODER_RESOURCE_ACESS_READ);
          encoded_buffer[arg.StructurePtrOffset] = srv_alloc->gpuAddress() + offset + srv.slice.byteOffset;
          encoded_buffer[arg.StructurePtrOffset + 1] = srv.slice.byteLength;
          makeResident<stage, kind>(srv.buffer.ptr());
        } else {
          encoded_buffer[arg.StructurePtrOffset] = 0;
          encoded_buffer[arg.StructurePtrOffset + 1] = 0;
        }
      } else if (arg.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE) {
        if (srv.buffer.ptr()) {
          assert(arg.Flags & MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET);
          auto [view, offset] = access(srv.buffer, srv.viewId, DXMT_ENCODER_RESOURCE_ACESS_READ);
          encoded_buffer[arg.StructurePtrOffset] = view.gpu_resource_id;
          encoded_buffer[arg.StructurePtrOffset + 1] =
              ((uint64_t)srv.slice.elementCount << 32) | (uint64_t)(srv.slice.firstElement + offset);
          makeResident<stage, kind>(srv.buffer.ptr(), srv.viewId);
        } else if (srv.texture.ptr()) {
          assert(arg.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE_MINLOD_CLAMP);
          auto viewIdChecked = srv.texture->checkViewUseArray(srv.viewId, arg.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE_ARRAY);
          encoded_buffer[arg.StructurePtrOffset] =
              access(srv.texture, viewIdChecked, DXMT_ENCODER_RESOURCE_ACESS_READ).gpu_resource_id;
          encoded_buffer[arg.StructurePtrOffset + 1] = 0;
          makeResident<stage, kind>(srv.texture.ptr(), viewIdChecked);
        } else {
          encoded_buffer[arg.StructurePtrOffset] = 0;
          encoded_buffer[arg.StructurePtrOffset + 1] = 0;
        }
      }
      if (arg.Flags & MTL_SM50_SHADER_ARGUMENT_UAV_COUNTER) {
        assert(0 && "srv can not have counter associated");
      }
      break;
    }
    case SM50BindingType::UAV: {
      auto &uav = UAVBindingSet[arg.SM50BindingSlot];
      bool read = (arg.Flags >> 10) & 1, write = (arg.Flags >> 10) & 2;
      auto access_flags =  (DXMT_ENCODER_RESOURCE_ACESS)((arg.Flags >> 10) & 3);

      if (arg.Flags & MTL_SM50_SHADER_ARGUMENT_BUFFER) {
        if (uav.buffer.ptr()) {
          auto [uav_alloc, offset] = access(uav.buffer, uav.slice.byteOffset, uav.slice.byteLength, access_flags);
          encoded_buffer[arg.StructurePtrOffset] = uav_alloc->gpuAddress() + offset + uav.slice.byteOffset;
          encoded_buffer[arg.StructurePtrOffset + 1] = uav.slice.byteLength;
          makeResident<stage, kind>(uav.buffer.ptr(), read, write);
        } else {
          encoded_buffer[arg.StructurePtrOffset] = 0;
          encoded_buffer[arg.StructurePtrOffset + 1] = 0;
        }
      } else if (arg.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE) {
        if (uav.buffer.ptr()) {
          assert(arg.Flags & MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET);
          auto [view, offset] = access(uav.buffer, uav.viewId, DXMT_ENCODER_RESOURCE_ACESS_READ);
          encoded_buffer[arg.StructurePtrOffset] = view.gpu_resource_id;
          encoded_buffer[arg.StructurePtrOffset + 1] =
              ((uint64_t)uav.slice.elementCount << 32) | (uint64_t)(uav.slice.firstElement + offset);
          makeResident<stage, kind>(uav.buffer.ptr(), uav.viewId, read, write);
        } else if (uav.texture.ptr()) {
          assert(arg.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE_MINLOD_CLAMP);
          auto viewIdChecked = uav.texture->checkViewUseArray(uav.viewId, arg.Flags & MTL_SM50_SHADER_ARGUMENT_TEXTURE_ARRAY);
          encoded_buffer[arg.StructurePtrOffset] = access(uav.texture, viewIdChecked, access_flags).gpu_resource_id;
          encoded_buffer[arg.StructurePtrOffset + 1] = 0;
          makeResident<stage, kind>(uav.texture.ptr(), viewIdChecked, read, write);
        } else {
          encoded_buffer[arg.StructurePtrOffset] = 0;
          encoded_buffer[arg.StructurePtrOffset + 1] = 0;
        }
      }
      if (arg.Flags & MTL_SM50_SHADER_ARGUMENT_UAV_COUNTER) {
        if (uav.counter) {
          auto [counter_alloc, offset] = access(uav.counter, 0, 4, DXMT_ENCODER_RESOURCE_ACESS_READ | DXMT_ENCODER_RESOURCE_ACESS_WRITE);
          encoded_buffer[arg.StructurePtrOffset + 2] = counter_alloc->gpuAddress() + offset;
          makeResident<stage, kind>(uav.counter.ptr(), true, true);
        } else {
          /*
           * potentially cause gpu pagefault, even providing a dummy buffer doesn't improve since the returned
           * counter value is likely to be used as an index to another read/write operation.
           */
          encoded_buffer[arg.StructurePtrOffset + 2] = 0;
        }
      }
      break;
    }
    }
  }

  if constexpr (stage == PipelineStage::Compute) {
    auto &cmd = encodeComputeCommand<wmtcmd_compute_setbufferoffset>();
    cmd.type = WMTComputeCommandSetBufferOffset;
    cmd.offset = getFinalArgumentBufferOffset<true>(offset);
    cmd.index = 30;
  } else if constexpr (kind == PipelineKind::Tessellation && (stage == PipelineStage::Hull || stage == PipelineStage::Vertex)) {
    auto &cmd = encodePreTessRenderCommand<wmtcmd_render_setbufferoffset>();
    cmd.offset = getFinalArgumentBufferOffset(offset);
    cmd.index = 30;
    if constexpr (stage == PipelineStage::Vertex)
      cmd.type = WMTRenderCommandSetObjectBufferOffset;
    else
      cmd.type = WMTRenderCommandSetMeshBufferOffset;
  } else {
    auto &cmd = encodeRenderCommand<wmtcmd_render_setbufferoffset>();
    cmd.offset = getFinalArgumentBufferOffset(offset);
    cmd.index = 30;
    if constexpr (stage == PipelineStage::Vertex) {
      if constexpr (kind == PipelineKind::Geometry)
        cmd.type = WMTRenderCommandSetObjectBufferOffset;
      else
        cmd.type = WMTRenderCommandSetVertexBufferOffset;
    } else if constexpr (stage == PipelineStage::Pixel) {
      cmd.type = WMTRenderCommandSetFragmentBufferOffset;
    } else if constexpr (stage == PipelineStage::Domain) {
      cmd.type = WMTRenderCommandSetVertexBufferOffset;
    } else if constexpr (stage == PipelineStage::Geometry) {
      cmd.type = WMTRenderCommandSetMeshBufferOffset;
    } else {
      assert(0 && "Not implemented or unreachable");
    }
  }
}

void
ArgumentEncodingContext::retainAllocation(Allocation* allocation) {
  if (allocation->checkRetained(seq_id_))
    return;
  queue_.Retain(seq_id_, allocation);
}

void
ArgumentEncodingContext::clearColor(Rc<Texture> &&texture, unsigned viewId, unsigned arrayLength, WMTClearColor color) {
  assert(!encoder_current);
  auto encoder_info = allocate<ClearEncoderData>();
  encoder_info->type = EncoderType::Clear;
  encoder_info->id = nextEncoderId();
  encoder_info->clear_dsv = 0;
  encoder_info->color = color;
  encoder_info->array_length = arrayLength;
  encoder_info->width = texture->width();
  encoder_info->height = texture->height();
  encoder_current = encoder_info;

  encoder_info->texture = access(texture, viewId, DXMT_ENCODER_RESOURCE_ACESS_WRITE).texture;

  currentFrameStatistics().clear_pass_count++;

  endPass();
}

void
ArgumentEncodingContext::clearDepthStencil(
    Rc<Texture> &&texture, unsigned viewId, unsigned arrayLength, unsigned flag, float depth, uint8_t stencil
) {
  assert(!encoder_current);
  auto encoder_info = allocate<ClearEncoderData>();
  encoder_info->type = EncoderType::Clear;
  encoder_info->id = nextEncoderId();
  encoder_info->clear_dsv = flag & DepthStencilPlanarFlags(texture->pixelFormat());
  encoder_info->depth_stencil = {depth, stencil};
  encoder_info->array_length = arrayLength;
  encoder_info->width = texture->width();
  encoder_info->height = texture->height();
  encoder_current = encoder_info;

  encoder_info->texture = access(texture, viewId, DXMT_ENCODER_RESOURCE_ACESS_WRITE).texture;

  currentFrameStatistics().clear_pass_count++;
  
  endPass();
}

void
ArgumentEncodingContext::resolveTexture(
    Rc<Texture> &&src, TextureViewKey src_view, Rc<Texture> &&dst, TextureViewKey dst_view
) {
  assert(!encoder_current);
  auto encoder_info = allocate<ResolveEncoderData>();
  encoder_info->type = EncoderType::Resolve;
  encoder_current = encoder_info;

  encoder_info->src = access(src, src_view, DXMT_ENCODER_RESOURCE_ACESS_READ).texture;
  encoder_info->dst = access(dst, dst_view, DXMT_ENCODER_RESOURCE_ACESS_WRITE).texture;

  endPass();
};

void
ArgumentEncodingContext::present(Rc<Texture> &texture, Rc<Presenter> &presenter, double after) {
  assert(!encoder_current);
  auto encoder_info = allocate<PresentData>();
  encoder_info->type = EncoderType::Present;
  encoder_info->id = nextEncoderId();
  encoder_info->backbuffer = texture->current()->texture();
  encoder_info->presenter = presenter;
  encoder_info->after = after;

  encoder_info->tex_read.add(texture->current()->depkey);

  encoder_current = encoder_info;
  endPass();
}

void
ArgumentEncodingContext::upscale(Rc<Texture> &texture, Rc<Texture> &upscaled, WMT::Reference<WMT::FXSpatialScaler> &scaler) {
  assert(!encoder_current);
  auto encoder_info = allocate<SpatialUpscaleData>();
  encoder_info->type = EncoderType::SpatialUpscale;
  encoder_info->id = nextEncoderId();
  encoder_info->backbuffer = texture->current()->texture();
  encoder_info->upscaled = upscaled->current()->texture();
  encoder_info->scaler = scaler;

  encoder_info->tex_read.add(texture->current()->depkey);
  encoder_info->tex_write.add(upscaled->current()->depkey);

  encoder_current = encoder_info;
  endPass();
}

void
ArgumentEncodingContext::upscaleTemporal(
    Rc<Texture> &input, Rc<Texture> &output, Rc<Texture> &depth, Rc<Texture> &motion_vector, TextureViewKey mvViewId,
    Rc<Texture> &exposure, WMT::Reference<WMT::FXTemporalScaler> &scaler, const WMTFXTemporalScalerProps &props
) {
  assert(!encoder_current);
  auto encoder_info = allocate<TemporalUpscaleData>();
  encoder_info->type = EncoderType::TemporalUpscale;
  encoder_info->id = nextEncoderId();
  encoder_info->input = input->current()->texture();
  encoder_info->output = output->current()->texture();
  encoder_info->depth = depth->current()->texture();
  encoder_info->motion_vector = motion_vector->view(mvViewId);
  encoder_info->scaler = scaler;
  encoder_info->props = props;

  encoder_info->tex_read.add(input->current()->depkey);
  encoder_info->tex_read.add(depth->current()->depkey);
  encoder_info->tex_read.add(motion_vector->current()->depkey);
  encoder_info->tex_write.add(output->current()->depkey);
  if(exposure) {
    encoder_info->exposure = exposure->current()->texture();
    encoder_info->tex_read.add(exposure->current()->depkey);
  } else {
    encoder_info->exposure = nullptr;
  }

  encoder_current = encoder_info;
  endPass();
}

void
ArgumentEncodingContext::signalEvent(uint64_t value) {
  assert(!encoder_current);
  auto encoder_info = allocate<SignalEventData>();
  encoder_info->type = EncoderType::SignalEvent;
  encoder_info->id = nextEncoderId();
  encoder_info->event = queue_.event;
  encoder_info->value = value;

  encoder_current = encoder_info;
  endPass();
}

void
ArgumentEncodingContext::signalEvent(WMT::Reference<WMT::Event> &&event, uint64_t value) {
  assert(!encoder_current);
  auto encoder_info = allocate<SignalEventData>();
  encoder_info->type = EncoderType::SignalEvent;
  encoder_info->id = nextEncoderId();
  encoder_info->event = std::move(event);
  encoder_info->value = value;

  encoder_current = encoder_info;
  endPass();
}

void
ArgumentEncodingContext::waitEvent(WMT::Reference<WMT::Event> &&event, uint64_t value) {
  assert(!encoder_current);
  auto encoder_info = allocate<WaitForEventData>();
  encoder_info->type = EncoderType::WaitForEvent;
  encoder_info->id = nextEncoderId();
  encoder_info->event = std::move(event);
  encoder_info->value = value;

  encoder_current = encoder_info;
  endPass();
}

RenderEncoderData *
ArgumentEncodingContext::startRenderPass(
    uint8_t dsv_planar_flags, uint8_t dsv_readonly_flags, uint8_t render_target_count, uint64_t encoder_argbuf_size
) {
  assert(!encoder_current);
  auto encoder_info = allocate<RenderEncoderData>();
  encoder_info->type = EncoderType::Render;
  encoder_info->id = nextEncoderId();
  WMT::InitializeRenderPassInfo(encoder_info->info);
  encoder_info->cmd_head.type = WMTRenderCommandNop;
  encoder_info->cmd_head.next.set(0);
  encoder_info->cmd_tail = (wmtcmd_base *)&encoder_info->cmd_head;
  encoder_info->pretess_cmd_head.type = WMTRenderCommandNop;
  encoder_info->pretess_cmd_head.next.set(0);
  encoder_info->pretess_cmd_tail = (wmtcmd_base *)&encoder_info->pretess_cmd_head;
  encoder_info->dsv_planar_flags = dsv_planar_flags;
  encoder_info->dsv_readonly_flags = dsv_readonly_flags;
  encoder_info->render_target_count = render_target_count;
  auto [gpu_buffer_contents, gpu_buffer_, offset] = queue_.AllocateArgumentBuffer(seq_id_, encoder_argbuf_size);
  encoder_info->allocated_argbuf = gpu_buffer_;
  encoder_info->allocated_argbuf_offset = offset;
  encoder_info->allocated_argbuf_mapping = gpu_buffer_contents;
  encoder_current = encoder_info;

  currentFrameStatistics().render_pass_count++;

  vro_state_.beginEncoder();

  return encoder_info;
}

EncoderData *
ArgumentEncodingContext::startComputePass(uint64_t encoder_argbuf_size) {
  assert(!encoder_current);
  auto encoder_info = allocate<ComputeEncoderData>();
  encoder_info->type = EncoderType::Compute;
  encoder_info->id = nextEncoderId();
  encoder_info->cmd_head.type = WMTComputeCommandNop;
  encoder_info->cmd_head.next.set(0);
  encoder_info->cmd_tail = (wmtcmd_base *)&encoder_info->cmd_head;
  auto [gpu_buffer_contents, gpu_buffer_, offset] = queue_.AllocateArgumentBuffer(seq_id_, encoder_argbuf_size);
  encoder_info->allocated_argbuf = gpu_buffer_;
  encoder_info->allocated_argbuf_offset = offset;
  encoder_info->allocated_argbuf_mapping = gpu_buffer_contents;
  encoder_current = encoder_info;

  currentFrameStatistics().compute_pass_count++;

  return encoder_info;
}

EncoderData *
ArgumentEncodingContext::startBlitPass() {
  assert(!encoder_current);
  auto encoder_info = allocate<BlitEncoderData>();
  encoder_info->type = EncoderType::Blit;
  encoder_info->id = nextEncoderId();
  encoder_info->cmd_head.type = WMTBlitCommandNop;
  encoder_info->cmd_head.next.set(0);
  encoder_info->cmd_tail = (wmtcmd_base *)&encoder_info->cmd_head;
  encoder_current = encoder_info;

  currentFrameStatistics().blit_pass_count++;

  return encoder_info;
}

void
ArgumentEncodingContext::endPass() {
  assert(encoder_current);
  encoder_last->next = encoder_current;
  encoder_last = encoder_current;

  if (encoder_current->type == EncoderType::Render)
    vro_state_.endEncoder();

  encoder_current = nullptr;
  encoder_count_++;
}

std::pair<WMT::Buffer, size_t>
ArgumentEncodingContext::allocateTempBuffer(size_t size, size_t alignment) {
  return queue_.AllocateTempBuffer(seq_id_, size, alignment);
};

AllocatedTempBufferSlice
ArgumentEncodingContext::allocateTempBuffer1(size_t size, size_t alignment) {
  return queue_.AllocateTempBuffer1(seq_id_, size, alignment);
};

void
ArgumentEncodingContext::beginVisibilityResultQuery(Rc<VisibilityResultQuery> &&query) {
  query->begin(seq_id_, vro_state_.getNextReadOffset());
  active_visibility_query_count_++;
  pending_queries_.push_back(std::move(query));
}
void
ArgumentEncodingContext::endVisibilityResultQuery(Rc<VisibilityResultQuery> &&query) {
  query->end(seq_id_, vro_state_.getNextReadOffset());
  assert(active_visibility_query_count_);
  active_visibility_query_count_--;
}
void
ArgumentEncodingContext::bumpVisibilityResultOffset() {
  auto render_encoder = currentRenderEncoder();
  render_encoder->use_visibility_result = render_encoder->use_visibility_result || bool(active_visibility_query_count_);

  uint64_t offset;
  if (vro_state_.tryGetNextWriteOffset(active_visibility_query_count_, offset)) {
    auto &cmd = encodeRenderCommand<wmtcmd_render_setvisibilitymode>();
    cmd.type = WMTRenderCommandSetVisibilityMode;
    if (~offset == 0) {
      cmd.mode = WMTVisibilityResultModeDisabled;
      cmd.offset = 0;
    } else {
      cmd.mode = WMTVisibilityResultModeCounting;
      cmd.offset = offset << 3;
    }
  }
}

FrameStatistics&
ArgumentEncodingContext::currentFrameStatistics() {
  return queue_.statistics.at(frame_id_);
}

void
ArgumentEncodingContext::$$setEncodingContext(uint64_t seq_id, uint64_t frame_id) {
  cpu_buffer_offset_ = 0;
  seq_id_ = seq_id;
  frame_id_ = frame_id;
}

constexpr unsigned kEncoderOptimizerThreshold = 64;

std::unique_ptr<VisibilityResultReadback>
ArgumentEncodingContext::flushCommands(WMT::CommandBuffer cmdbuf, uint64_t seqId, uint64_t event_seq_id) {
  assert(!encoder_current);

  unsigned encoder_count = encoder_count_;
  unsigned encoder_index = 0;
  EncoderData **encoders =
      reinterpret_cast<EncoderData **>(allocate_cpu_heap(sizeof(EncoderData *) * encoder_count, alignof(EncoderData *))
      );

  {
    EncoderData *current = encoder_head.next;
    while (current) {
      encoders[encoder_index++] = current;
      current = current->next;
    }
    assert(encoder_index == encoder_count);
  }

  if (encoder_count > 1) {
    unsigned j, i;
    for (j = encoder_count - 2; j != ~0u; j--) {
      if (encoders[j]->type == EncoderType::Null)
        continue;
      for (i = j + 1; i < std::min(encoder_count, j + kEncoderOptimizerThreshold); i++) {
        if (encoders[i]->type == EncoderType::Null)
          continue;
        if (checkEncoderRelation(encoders[j], encoders[i]) == DXMT_ENCODER_LIST_OP_SYNCHRONIZE)
          break;
      }
    }
  }

  std::unique_ptr<VisibilityResultReadback> visibility_readback {};

  if (auto count = vro_state_.reset()) {
    visibility_readback = std::make_unique<VisibilityResultReadback>(
        device_, seqId, count, pending_queries_
    );
  }
  std::erase_if(pending_queries_, [=](auto &query) -> bool { return query->queryEndAt() == seqId; });

  while (encoder_index) {
    auto current = encoders[encoder_count - encoder_index];
    switch (current->type) {
    case EncoderType::Render: {
      auto data = static_cast<RenderEncoderData *>(current);
      if (data->use_visibility_result) {
        assert(visibility_readback);
        data->info.visibility_buffer = visibility_readback->visibility_result_heap;
      }
      auto gpu_buffer_ = data->allocated_argbuf;
      auto encoder = cmdbuf.renderCommandEncoder(data->info);
      encoder.setVertexBuffer(gpu_buffer_, 0, 16);
      encoder.setVertexBuffer(gpu_buffer_, 0, 29);
      encoder.setVertexBuffer(gpu_buffer_, 0, 30);
      encoder.setFragmentBuffer(gpu_buffer_, 0, 29);
      encoder.setFragmentBuffer(gpu_buffer_, 0, 30);
      if (data->use_tessellation) {
        encoder.setObjectBuffer(gpu_buffer_, 0, 16);
        encoder.setObjectBuffer(gpu_buffer_, 0, 21); // draw arguments
        encoder.setObjectBuffer(gpu_buffer_, 0, 29);
        encoder.setObjectBuffer(gpu_buffer_, 0, 30);
        encoder.setMeshBuffer(gpu_buffer_, 0, 29);
        encoder.setMeshBuffer(gpu_buffer_, 0, 30);
        encoder.setVertexBuffer(gpu_buffer_, 0, 23); // draw arguments
        encoder.encodeCommands(&data->pretess_cmd_head);
        encoder.memoryBarrier(WMTBarrierScopeBuffers, WMTRenderStageMesh, WMTRenderStageVertex);
      }
      if (data->use_geometry && !data->use_tessellation) {
        encoder.setObjectBuffer(gpu_buffer_, 0, 16);
        encoder.setObjectBuffer(gpu_buffer_, 0, 21); // draw arguments
        encoder.setObjectBuffer(gpu_buffer_, 0, 29);
        encoder.setObjectBuffer(gpu_buffer_, 0, 30);
        encoder.setMeshBuffer(gpu_buffer_, 0, 29);
        encoder.setMeshBuffer(gpu_buffer_, 0, 30);
      }
      if (data->gs_arg_marshal_tasks.size()) {
        auto task_count = data->gs_arg_marshal_tasks.size();
        struct GS_MARSHAL_TASK {
          uint64_t draw_args;
          uint64_t dispatch_args_out;
          uint32_t vertex_count_per_warp;
          uint32_t end_of_command;
        };
        auto [mapped_task_data, task_data_buffer, task_data_buffer_offset] =
            queue_.AllocateArgumentBuffer(seq_id_, sizeof(GS_MARSHAL_TASK) * task_count);
        auto tasks_data = (GS_MARSHAL_TASK *)mapped_task_data;
        for (unsigned i = 0; i<task_count; i++) {
          auto & task = data->gs_arg_marshal_tasks[i];
          tasks_data[i].draw_args = task.draw_arguments_va;
          tasks_data[i].dispatch_args_out = task.dispatch_arguments_va;
          tasks_data[i].vertex_count_per_warp = task.vertex_count_per_warp;
          tasks_data[i].end_of_command = 0;
          encoder.useResource(task.draw_arguments, WMTResourceUsageRead, WMTRenderStageVertex);
          encoder.useResource(task.dispatch_arguments_buffer, WMTResourceUsageWrite, WMTRenderStageVertex);
        }
        tasks_data[task_count - 1].end_of_command = 1;
        emulated_cmd.MarshalGSDispatchArguments(encoder, task_data_buffer, task_data_buffer_offset);
        encoder.memoryBarrier(
            WMTBarrierScopeBuffers, WMTRenderStageVertex,
            WMTRenderStageVertex | WMTRenderStageMesh | WMTRenderStageObject
        );
      }
      encoder.encodeCommands(&data->cmd_head);
      encoder.endEncoding();
      data->~RenderEncoderData();
      break;
    }
    case EncoderType::Compute: {
      auto data = static_cast<ComputeEncoderData *>(current);
      auto encoder = cmdbuf.computeCommandEncoder(false);
      struct wmtcmd_compute_setbuffer setcmd;
      setcmd.type = WMTComputeCommandSetBuffer;
      setcmd.next.set(nullptr);
      setcmd.buffer = data->allocated_argbuf;
      setcmd.offset = 0;
      setcmd.index = 29;
      encoder.encodeCommands((const wmtcmd_compute_nop *)&setcmd);
      setcmd.index = 30;
      encoder.encodeCommands((const wmtcmd_compute_nop *)&setcmd);
      encoder.encodeCommands(&data->cmd_head);
      encoder.endEncoding();
      data->~ComputeEncoderData();
      break;
    }
    case EncoderType::Blit: {
      auto data = static_cast<BlitEncoderData *>(current);
      auto encoder = cmdbuf.blitCommandEncoder();
      encoder.encodeCommands(&data->cmd_head);
      encoder.endEncoding();
      data->~BlitEncoderData();
      break;
    }
    case EncoderType::Present: {
      auto data = static_cast<PresentData *>(current);
      auto t0 = clock::now();
      auto drawable = data->presenter->encodeCommands(cmdbuf, {}, data->backbuffer);
      auto t1 = clock::now();
      currentFrameStatistics().drawable_blocking_interval += (t1 - t0);
      if (data->after > 0)
        cmdbuf.presentDrawableAfterMinimumDuration(drawable, data->after);
      else
        cmdbuf.presentDrawable(drawable);
      data->~PresentData();
      break;
    }
    case EncoderType::Clear: {
      auto data = static_cast<ClearEncoderData *>(current);
      {
        WMTRenderPassInfo info;
        WMT::InitializeRenderPassInfo(info);
        if (data->clear_dsv) {
          if (data->clear_dsv & 1) {
            info.depth.clear_depth = data->depth_stencil.first;
            info.depth.texture = data->texture;
            info.depth.load_action = WMTLoadActionClear;
            info.depth.store_action = WMTStoreActionStore;
          }
          if (data->clear_dsv & 2) {
            info.stencil.clear_stencil = data->depth_stencil.second;
            info.stencil.texture = data->texture;
            info.stencil.load_action = WMTLoadActionClear;
            info.stencil.store_action = WMTStoreActionStore;
          }
          info.render_target_width = data->width;
          info.render_target_height = data->height;
        } else {
          info.colors[0].clear_color = data->color;
          info.colors[0].texture = data->texture;
          info.colors[0].load_action = WMTLoadActionClear;
          info.colors[0].store_action = WMTStoreActionStore;
        }
        info.render_target_array_length = data->array_length;
        auto encoder = cmdbuf.renderCommandEncoder(info);
        encoder.setLabel(WMT::String::string("ClearPass", WMTUTF8StringEncoding));
        encoder.endEncoding();
      }
      data->~ClearEncoderData();
      break;
    }
    case EncoderType::Resolve: {
      auto data = static_cast<ResolveEncoderData *>(current);
      {
        WMTRenderPassInfo info;
        WMT::InitializeRenderPassInfo(info);
        info.colors[0].texture = data->src;
        info.colors[0].load_action = WMTLoadActionLoad;
        info.colors[0].store_action = WMTStoreActionStoreAndMultisampleResolve;
        info.colors[0].resolve_texture = data->dst;

        auto encoder = cmdbuf.renderCommandEncoder(info);
        encoder.setLabel(WMT::String::string("ResolvePass", WMTUTF8StringEncoding));
        encoder.endEncoding();
      }
      data->~ResolveEncoderData();
      break;
    }
    case EncoderType::SpatialUpscale: {
      auto data = static_cast<SpatialUpscaleData *>(current);
      cmdbuf.encodeSpatialScale(data->scaler, data->backbuffer, data->upscaled, {});
      data->~SpatialUpscaleData();
      break;
    }
    case EncoderType::SignalEvent: {
      auto data = static_cast<SignalEventData *>(current);
      cmdbuf.encodeSignalEvent(data->event, data->value);
      data->~SignalEventData();
      break;
    }
    case EncoderType::WaitForEvent: {
      auto data = static_cast<WaitForEventData *>(current);
      cmdbuf.encodeWaitForEvent(data->event, data->value);
      data->~WaitForEventData();
      break;
    }
    case EncoderType::TemporalUpscale: {
      auto data = static_cast<TemporalUpscaleData *>(current);
      cmdbuf.encodeTemporalScale(data->scaler, data->input, data->output, data->depth, data->motion_vector, data->exposure, {}, data->props);
      data->~TemporalUpscaleData();
      break;
    }
    default:
      break;
    }
    encoder_index--;
  }
  encoder_head.next = nullptr;
  encoder_last = &encoder_head;
  encoder_count_ = 0;

  cmdbuf.encodeSignalEvent(queue_.event, event_seq_id);

  return visibility_readback;
}

DXMT_ENCODER_LIST_OP
ArgumentEncodingContext::checkEncoderRelation(EncoderData *former, EncoderData *latter) {
  if (former->type == EncoderType::Null)
    return DXMT_ENCODER_LIST_OP_SWAP;
  if (latter->type == EncoderType::Null)
    return DXMT_ENCODER_LIST_OP_SWAP;
  if (former->type == EncoderType::SignalEvent)
    return DXMT_ENCODER_LIST_OP_SYNCHRONIZE;
  if (latter->type == EncoderType::SignalEvent)
    return DXMT_ENCODER_LIST_OP_SYNCHRONIZE;
  if (former->type == EncoderType::WaitForEvent)
    return DXMT_ENCODER_LIST_OP_SYNCHRONIZE;
  if (latter->type == EncoderType::WaitForEvent)
    return DXMT_ENCODER_LIST_OP_SYNCHRONIZE;

  while (former->type != latter->type) {
    if (former->type == EncoderType::Clear && latter->type == EncoderType::Render) {
      auto render = reinterpret_cast<RenderEncoderData *>(latter);
      auto clear = reinterpret_cast<ClearEncoderData *>(former);

      if (render->info.render_target_array_length != clear->array_length)
        break;

      if (clear->clear_dsv) {
        if (auto depth_attachment = isClearDepthSignatureMatched(clear, render)) {
          if (depth_attachment->load_action == WMTLoadActionLoad) {
            depth_attachment->clear_depth = clear->depth_stencil.first;
            depth_attachment->load_action = WMTLoadActionClear;
            depth_attachment->store_action = WMTStoreActionStore;
            render->tex_write.merge(clear->tex_write);
          }
          clear->clear_dsv &= ~1;
        }
        if (auto stencil_attachment = isClearStencilSignatureMatched(clear, render)) {
          if (stencil_attachment->load_action == WMTLoadActionLoad) {
            stencil_attachment->clear_stencil = clear->depth_stencil.second;
            stencil_attachment->load_action = WMTLoadActionClear;
            stencil_attachment->store_action = WMTStoreActionStore;
            render->tex_write.merge(clear->tex_write);
          }
          clear->clear_dsv &= ~2;
        }
        if (clear->clear_dsv == 0) {
          currentFrameStatistics().clear_pass_optimized++;
          clear->~ClearEncoderData();
          clear->next = nullptr;
          clear->type = EncoderType::Null;
          return DXMT_ENCODER_LIST_OP_SYNCHRONIZE;
        }
      } else {
        if (auto attachment = isClearColorSignatureMatched(clear, render)) {
          if (attachment->load_action == WMTLoadActionLoad) {
            attachment->load_action = WMTLoadActionClear;
            attachment->clear_color = clear->color;
            if (attachment->store_action != WMTStoreActionDontCare)
              render->tex_write.merge(clear->tex_write);
          }

          currentFrameStatistics().clear_pass_optimized++;
          clear->~ClearEncoderData();
          clear->next = nullptr;
          clear->type = EncoderType::Null;
          return DXMT_ENCODER_LIST_OP_SYNCHRONIZE;
        }
      }
      break;
    }
    if (latter->type == EncoderType::Clear && former->type == EncoderType::Render) {
      auto render = reinterpret_cast<RenderEncoderData *>(former);
      auto clear = reinterpret_cast<ClearEncoderData *>(latter);

      if (clear->clear_dsv) {
        if (render->info.depth.texture == clear->texture.handle) {
          render->info.depth.store_action = WMTStoreActionDontCare;
        }
        if (render->info.stencil.texture == clear->texture.handle) {
          render->info.stencil.store_action = WMTStoreActionDontCare;
        }
      }
    }
    return hasDataDependency(latter, former) ? DXMT_ENCODER_LIST_OP_SYNCHRONIZE : DXMT_ENCODER_LIST_OP_SWAP;
  }

  if (former->type == EncoderType::Render) {
    auto r1 = reinterpret_cast<RenderEncoderData *>(latter);
    auto r0 = reinterpret_cast<RenderEncoderData *>(former);

    if (isEncoderSignatureMatched(r0, r1)) {
      for (unsigned i = 0; i < r0->render_target_count; i++) {
        auto &a0 = r0->info.colors[i];
        auto &a1 = r1->info.colors[i];
        a1.load_action = a0.load_action;
        a1.clear_color = a0.clear_color;
      }

      r1->info.depth.load_action = r0->info.depth.load_action;
      r1->info.depth.clear_depth = r0->info.depth.clear_depth;
      r1->info.depth.store_action = r0->info.depth.store_action;
      r1->info.stencil.load_action = r0->info.stencil.load_action;
      r1->info.stencil.clear_stencil = r0->info.stencil.clear_stencil;
      r1->info.stencil.store_action = r0->info.stencil.store_action;

      if ((void *)r0->cmd_tail != &r0->cmd_head) {
        r0->cmd_tail->next.set(r1->cmd_head.next.get());
        r1->cmd_head.next.set(r0->cmd_head.next.get());
        r0->cmd_head.next.set(nullptr);
        r0->cmd_tail = (wmtcmd_base *)&r0->cmd_head;
      }
      if ((void *)r0->pretess_cmd_tail != &r0->pretess_cmd_head) {
        r0->pretess_cmd_tail->next.set(r1->pretess_cmd_head.next.get());
        r1->pretess_cmd_head.next.set(r0->pretess_cmd_head.next.get());
        r0->pretess_cmd_head.next.set(nullptr);
        r0->pretess_cmd_tail = (wmtcmd_base *)&r0->pretess_cmd_head;
      }
      r1->use_tessellation = r0->use_tessellation || r1->use_tessellation;
      r1->use_geometry = r0->use_geometry || r1->use_geometry;
      std::move(
        r1->gs_arg_marshal_tasks.begin(),
        r1->gs_arg_marshal_tasks.end(),
        std::back_inserter(r0->gs_arg_marshal_tasks)
      );
      r1->gs_arg_marshal_tasks = std::move(r0->gs_arg_marshal_tasks);
      r1->use_visibility_result = r0->use_visibility_result || r1->use_visibility_result;

      r1->buf_read.merge(r0->buf_read);
      r1->buf_write.merge(r0->buf_write);
      r1->tex_read.merge(r0->tex_read);
      r1->tex_write.merge(r0->tex_write);

      currentFrameStatistics().render_pass_optimized++;
      r0->~RenderEncoderData();
      r0->next = nullptr;
      r0->type = EncoderType::Null;

      return DXMT_ENCODER_LIST_OP_SYNCHRONIZE;
    }
  }

  return hasDataDependency(latter, former) ? DXMT_ENCODER_LIST_OP_SYNCHRONIZE : DXMT_ENCODER_LIST_OP_SWAP;
}

bool
ArgumentEncodingContext::hasDataDependency(EncoderData *latter, EncoderData *former) {
  if (latter->type == EncoderType::Clear && former->type == EncoderType::Clear) {
    // FIXME: prove it's safe to return false
    return false;
  }
  // read-after-write
  if (!former->buf_write.isDisjointWith(latter->buf_read))
    return true;
  if (!former->tex_write.isDisjointWith(latter->tex_read))
    return true;
  // write-after-write
  if (!former->buf_write.isDisjointWith(latter->buf_write))
    return true;
  if (!former->tex_write.isDisjointWith(latter->tex_write))
    return true;
  // write-after-read
  if (!former->buf_read.isDisjointWith(latter->buf_write))
    return true;
  if (!former->tex_read.isDisjointWith(latter->tex_write))
    return true;
  return false;
}

bool
ArgumentEncodingContext::isEncoderSignatureMatched(RenderEncoderData *r0, RenderEncoderData *r1) {
  // FIXME: it can be different?
  if (r0->render_target_count != r1->render_target_count)
    return false;
  if (r0->dsv_planar_flags != r1->dsv_planar_flags)
    return false;
  if (r0->dsv_readonly_flags != r1->dsv_readonly_flags)
    return false;
  if (r0->info.render_target_array_length != r1->info.render_target_array_length)
    return false;
  /**
  In case two encoder has different argument buffer
  It can be further optimized by inserting extra setBuffer() calls
  but let's just simplify it, as it's a rather rare case.
  */
  if (r0->allocated_argbuf != r1->allocated_argbuf)
    return false;
  if (r0->dsv_planar_flags & 1) {
    if (r0->info.depth.texture != r1->info.depth.texture)
      return false;
    if (r0->dsv_readonly_flags & 1) {
      if (r1->info.depth.load_action == WMTLoadActionClear)
        return false;
    } else {
      if (r0->info.depth.store_action != WMTStoreActionStore)
        return false;
      if (r1->info.depth.load_action != WMTLoadActionLoad)
        return false;
    }
  }
  if (r0->dsv_planar_flags & 2) {
    if (r0->info.stencil.texture != r1->info.stencil.texture)
      return false;
    if (r0->dsv_readonly_flags & 2) {
      if (r1->info.stencil.load_action == WMTLoadActionClear)
        return false;
    } else {
      if (r0->info.stencil.store_action != WMTStoreActionStore)
        return false;
      if (r1->info.stencil.load_action != WMTLoadActionLoad)
        return false;
    }
  }
  for (unsigned i = 0; i < r0->render_target_count; i++) {
    auto &a0 = r0->info.colors[i];
    auto &a1 = r1->info.colors[i];
    if (a0.texture != a1.texture)
      return false;
    if (a0.depth_plane != a1.depth_plane)
      return false;
    if (!a0.texture)
      continue;
    if (a0.store_action != WMTStoreActionStore)
      return false;
    if (a1.load_action != WMTLoadActionLoad)
      return false;
  }
  return true;
}

WMTColorAttachmentInfo *
ArgumentEncodingContext::isClearColorSignatureMatched(ClearEncoderData *clear, RenderEncoderData *render) {
  for (unsigned i = 0; i < render->render_target_count; i++) {
    auto &attachment = render->info.colors[i];
    if (attachment.texture == clear->texture.handle) {
      return &attachment;
    }
  }
  return nullptr;
}

WMTDepthAttachmentInfo *
ArgumentEncodingContext::isClearDepthSignatureMatched(ClearEncoderData *clear, RenderEncoderData *render) {
  if ((clear->clear_dsv & 1) == 0)
    return nullptr;
  if (render->info.depth.texture != clear->texture.handle)
    return nullptr;
  return &render->info.depth;
}

WMTStencilAttachmentInfo *
ArgumentEncodingContext::isClearStencilSignatureMatched(ClearEncoderData *clear, RenderEncoderData *render) {
  if ((clear->clear_dsv & 2) == 0)
    return nullptr;
  if (render->info.stencil.texture != clear->texture.handle)
    return nullptr;
  return &render->info.stencil;
}

} // namespace dxmt