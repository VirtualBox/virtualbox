#include "dxmt_command.hpp"
#include "Metal.hpp"
#include "dxmt_context.hpp"
#include "dxmt_format.hpp"

#ifndef VBOX
extern "C" unsigned char dxmt_command_metallib[];
extern "C" unsigned int dxmt_command_metallib_len;
#else
# include "dxmt_command.h"
# define dxmt_command_metallib g_abdxmt_command_metallib
# define dxmt_command_metallib_len g_cbdxmt_command_metallib
#endif

#define CREATE_PIPELINE(name)                                                                                          \
  auto name##_function = library.newFunction(#name);           \
  name##_pipeline = device.newComputePipelineState(name##_function, error);

namespace dxmt {

InternalCommandLibrary::InternalCommandLibrary(WMT::Device device) {
  WMT::Reference<WMT::Error> error;
  library_ = device.newLibrary(dxmt_command_metallib, dxmt_command_metallib_len, error);

  if (error) {
    ERR("Failed to create internal command library: ", error.description().getUTF8String());
    abort();
  }
};

EmulatedCommandContext::EmulatedCommandContext(WMT::Device device, InternalCommandLibrary &lib, ArgumentEncodingContext &ctx): ctx(ctx) {
  auto pool = WMT::MakeAutoreleasePool();

  auto library = lib.getLibrary();

  WMT::Reference<WMT::Error> error;

  CREATE_PIPELINE(clear_texture_1d_uint);
  CREATE_PIPELINE(clear_texture_1d_array_uint);
  CREATE_PIPELINE(clear_texture_2d_uint);
  CREATE_PIPELINE(clear_texture_2d_array_uint);
  CREATE_PIPELINE(clear_texture_3d_uint);
  CREATE_PIPELINE(clear_texture_buffer_uint);
  CREATE_PIPELINE(clear_buffer_uint);
  CREATE_PIPELINE(clear_texture_1d_float);
  CREATE_PIPELINE(clear_texture_1d_array_float);
  CREATE_PIPELINE(clear_texture_2d_float);
  CREATE_PIPELINE(clear_texture_2d_array_float);
  CREATE_PIPELINE(clear_texture_3d_float);
  CREATE_PIPELINE(clear_texture_buffer_float);


  auto gs_draw_arguments_marshal_vs = library.newFunction("gs_draw_arguments_marshal");
  {
    WMTRenderPipelineInfo gs_marshal_pipeline;
    WMT::InitializeRenderPipelineInfo(gs_marshal_pipeline);
    gs_marshal_pipeline.vertex_function = gs_draw_arguments_marshal_vs;
    gs_marshal_pipeline.rasterization_enabled = false;
    gs_draw_arguments_marshal = device.newRenderPipelineState(gs_marshal_pipeline, error);
  }
}

void
EmulatedCommandContext::setComputePipelineState(WMT::ComputePipelineState state, const WMTSize &threadgroup_size) {
  auto &cmd = ctx.encodeComputeCommand<wmtcmd_compute_setpso>();
  cmd.type = WMTComputeCommandSetPSO;
  cmd.pso = state;
  cmd.threadgroup_size = threadgroup_size;
}

void EmulatedCommandContext::dispatchThreads(const WMTSize &grid_size) {
  auto &cmd = ctx.encodeComputeCommand<wmtcmd_compute_dispatch>();
  cmd.type = WMTComputeCommandDispatchThreads;
  cmd.size = grid_size;
}

void
EmulatedCommandContext::setComputeBuffer(WMT::Buffer buffer, uint64_t offset, uint8_t index) {
  auto &cmd = ctx.encodeComputeCommand<wmtcmd_compute_setbuffer>();
  cmd.type = WMTComputeCommandSetBuffer;
  cmd.buffer = buffer;
  cmd.offset = offset;
  cmd.index = index;
}

void
EmulatedCommandContext::setComputeTexture(WMT::Texture texture, uint8_t index) {
  auto &cmd = ctx.encodeComputeCommand<wmtcmd_compute_settexture>();
  cmd.type = WMTComputeCommandSetTexture;
  cmd.texture = texture;
  cmd.index = index;
}

void
EmulatedCommandContext::setComputeBytes(const void *buf, uint64_t length, uint8_t index) {
  auto &cmd = ctx.encodeComputeCommand<wmtcmd_compute_setbytes>();
  cmd.type = WMTComputeCommandSetBytes;
  void *temp = ctx.allocate_cpu_heap(length, 8);
  memcpy(temp, buf, length);
  cmd.bytes.set(temp);
  cmd.length = length;
  cmd.index = index;
}

ClearRenderTargetContext::ClearRenderTargetContext(
    WMT::Device device, InternalCommandLibrary &lib, ArgumentEncodingContext &ctx
) :
    ctx_(ctx),
    device_(device) {
  auto library = lib.getLibrary();
  vs_clear_ = library.newFunction("vs_clear_rt");
  fs_clear_depth_ = library.newFunction("fs_clear_rt_depth");
  fs_clear_float_ = library.newFunction("fs_clear_rt_float");
  fs_clear_sint_ = library.newFunction("fs_clear_rt_sint");
  fs_clear_uint_ = library.newFunction("fs_clear_rt_uint");

  WMTDepthStencilInfo ds_info;
  ds_info.front_stencil.enabled = false;
  ds_info.back_stencil.enabled = false;
  ds_info.depth_compare_function = WMTCompareFunctionAlways;
  ds_info.depth_write_enabled = true;

  depth_write_state_ = device.newDepthStencilState(ds_info);
}

void
ClearRenderTargetContext::begin(Rc<Texture> texture, TextureViewKey view) {
  assert(!clearing_texture_);

  WMT::Reference<WMT::Error> err;

  auto format = texture->pixelFormat(view);
  auto dsv_flag = DepthStencilPlanarFlags(format);

  if (dsv_flag & ~1u)
    return; // stencil clear is not supported

  // enforce array render target for now
  view = texture->checkViewUseArray(view, true);

  if (!pso_cache_.contains(format)) {
    WMTRenderPipelineInfo pipeline_info;
    WMT::InitializeRenderPipelineInfo(pipeline_info);
    pipeline_info.raster_sample_count = texture->sampleCount();
    pipeline_info.vertex_function = vs_clear_;
    if (dsv_flag) {
      pipeline_info.fragment_function = fs_clear_depth_;
      pipeline_info.depth_pixel_format = format;
    } else if (IsIntegerFormat(format)) {
      pipeline_info.colors[0].pixel_format = format;
      if (MTLGetUnsignedIntegerFormat(format) == format) {
        pipeline_info.fragment_function = fs_clear_uint_;
      } else {
        pipeline_info.fragment_function = fs_clear_sint_;
      }
    } else {
      pipeline_info.colors[0].pixel_format = format;
      pipeline_info.fragment_function = fs_clear_float_;
    }
    pipeline_info.rasterization_enabled = true;
    pipeline_info.input_primitive_topology = WMTPrimitiveTopologyClassTriangle;
    auto pso = device_.newRenderPipelineState(pipeline_info, err);
    if (pso == nullptr) {
      ERR("Failed to create ClearRenderTarget PSO of format ", format, ": ", err.description().getUTF8String());
    }
    pso_cache_.emplace(format, std::move(pso));
  }

  WMT::RenderPipelineState pso = pso_cache_.at(format);

  if (!pso)
    return;

  auto width = texture->width(view);
  auto height = texture->height(view);
  auto array_length = texture->arrayLength(view);
  auto &pass_info = ctx_.startRenderPass(dsv_flag, 0, 1, 0)->info;

  if (dsv_flag) {
    auto &depth = pass_info.depth;
    depth.texture = ctx_.access(texture, view, DXMT_ENCODER_RESOURCE_ACESS_WRITE).texture;
    depth.depth_plane = 0;
    depth.load_action = WMTLoadActionLoad;
    depth.store_action = WMTStoreActionStore;
  } else {
    auto &color = pass_info.colors[0];
    color.texture = ctx_.access(texture, view, DXMT_ENCODER_RESOURCE_ACESS_WRITE).texture;
    color.depth_plane = 0;
    color.load_action = WMTLoadActionLoad;
    color.store_action = WMTStoreActionStore;
  }

  pass_info.render_target_width = width;
  pass_info.render_target_height = height;
  pass_info.render_target_array_length = array_length;
  pass_info.default_raster_sample_count = texture->sampleCount();

  auto &setpso = ctx_.encodeRenderCommand<wmtcmd_render_setpso>();
  setpso.type = WMTRenderCommandSetPSO;
  setpso.pso = pso;

  auto &setvp = ctx_.encodeRenderCommand<wmtcmd_render_setviewport>();
  setvp.type = WMTRenderCommandSetViewport;
  setvp.viewport = {0.0, 0.0, (double)width, (double)height, 0.0, 1.0};

#ifdef VBOX
  if (dsv_flag)
  {
    auto &setdsso = ctx_.encodeRenderCommand<wmtcmd_render_setdsso>();
    setdsso.type = WMTRenderCommandSetDSSO;
    setdsso.dsso = depth_write_state_.handle;
    setdsso.stencil_ref = 0;
  }
#else
  auto &setdsso = ctx_.encodeRenderCommand<wmtcmd_render_setdsso>();
  setdsso.type = WMTRenderCommandSetDSSO;
  setdsso.dsso = dsv_flag ? depth_write_state_.handle : obj_handle_t{};
  setdsso.stencil_ref = 0;
#endif

  clearing_texture_ = std::move(texture);
  clearing_texture_view_ = view;
}

void
ClearRenderTargetContext::clear(
    uint32_t offset_x, uint32_t offset_y, uint32_t width, uint32_t height, const std::array<float, 4> &color
) {
  if (!clearing_texture_)
    return;
  auto &setscr = ctx_.encodeRenderCommand<wmtcmd_render_setscissorrect>();
  setscr.type = WMTRenderCommandSetScissorRect;
  setscr.scissor_rect = {offset_x, offset_y, width, height};

  auto &setcolor = ctx_.encodeRenderCommand<wmtcmd_render_setbytes>();
  setcolor.type = WMTRenderCommandSetFragmentBytes;
  void *temp = ctx_.allocate_cpu_heap(sizeof(color), 16);
  memcpy(temp, color.data(), sizeof(color));
  setcolor.bytes.set(temp);
  setcolor.length = sizeof(color);
  setcolor.index = 0;

  auto &draw = ctx_.encodeRenderCommand<wmtcmd_render_draw>();
  draw.type = WMTRenderCommandDraw;
  draw.primitive_type = WMTPrimitiveTypeTriangle;
  draw.vertex_start = 0;
  draw.vertex_count = 3;
  draw.base_instance = 0;
  draw.instance_count = ctx_.currentRenderEncoder()->info.render_target_array_length;
}

void
ClearRenderTargetContext::end() {
  if (!clearing_texture_)
    return;
  ctx_.endPass();
  clearing_texture_ = nullptr;
  clearing_texture_view_ = 0;
};

} // namespace dxmt
