#include "DXBCParser/d3d12tokenizedprogramformat.hpp"
#include "Metal.hpp"
#include "airconv_public.h"
#include "d3d11_device.hpp"
#include "d3d11_pipeline.hpp"
#include "d3d11_shader.hpp"
#include "log/log.hpp"

namespace dxmt {

class MTLCompiledTessellationPipeline
    : public ComObject<IMTLCompiledTessellationPipeline> {
public:
  MTLCompiledTessellationPipeline(MTLD3D11Device *pDevice,
                                  const MTL_GRAPHICS_PIPELINE_DESC *pDesc)
      : ComObject<IMTLCompiledTessellationPipeline>(),
        num_rtvs(pDesc->NumColorAttachments),
        depth_stencil_format(pDesc->DepthStencilFormat),
        topology_class(pDesc->TopologyClass), device_(pDevice),
        pBlendState(pDesc->BlendState),
        RasterizationEnabled(pDesc->RasterizationEnabled),
        SampleCount(pDesc->SampleCount) {
    uint32_t unorm_output_reg_mask = 0;
    for (unsigned i = 0; i < num_rtvs; i++) {
      rtv_formats[i] = pDesc->ColorAttachmentFormats[i];
      unorm_output_reg_mask |= (uint32_t(IsUnorm8RenderTargetFormat(pDesc->ColorAttachmentFormats[i])) << i);
    }
    VertexShader =
        pDesc->VertexShader->get_shader(ShaderVariantTessellationVertex{
            (uint64_t)pDesc->InputLayout, (uint64_t)pDesc->HullShader->handle(),
            pDesc->IndexBufferFormat});
    HullShader = pDesc->HullShader->get_shader(
        ShaderVariantTessellationHull{(uint64_t)pDesc->VertexShader->handle()});
    DomainShader =
        pDesc->DomainShader->get_shader(ShaderVariantTessellationDomain{
            (uint64_t)pDesc->HullShader->handle(), pDesc->GSPassthrough, !pDesc->RasterizationEnabled});
    if (pDesc->PixelShader) {
      PixelShader = pDesc->PixelShader->get_shader(ShaderVariantPixel{
          pDesc->SampleMask, pDesc->BlendState->IsDualSourceBlending(),
          depth_stencil_format == WMTPixelFormatInvalid,
          unorm_output_reg_mask});
    }
    hull_reflection = pDesc->HullShader->reflection();
  }

  void SubmitWork() { device_->SubmitThreadgroupWork(this); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IMTLThreadpoolWork) ||
        riid == __uuidof(IMTLCompiledGraphicsPipeline)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  bool IsReady() final { return ready_.load(std::memory_order_relaxed); }

  void GetPipeline(MTL_COMPILED_TESSELLATION_PIPELINE *pPipeline) final {
    ready_.wait(false, std::memory_order_acquire);
    *pPipeline = {state_mesh_,
                  state_rasterization_,
                  hull_reflection.NumOutputElement,
                  hull_reflection.NumPatchConstantOutputScalar,
                  hull_reflection.ThreadsPerPatch};
  }

  IMTLThreadpoolWork *RunThreadpoolWork() {

    WMT::Reference<WMT::Error> err;
    MTL_COMPILED_SHADER vs, hs, ds, ps;

    if (!VertexShader->GetShader(&vs)) {
      return VertexShader.ptr();
    }
    if (!HullShader->GetShader(&hs)) {
      return HullShader.ptr();
    }
    if (!DomainShader->GetShader(&ds)) {
      return DomainShader.ptr();
    }
    if (PixelShader && !PixelShader->GetShader(&ps)) {
      return PixelShader.ptr();
    }

    WMTMeshRenderPipelineInfo info_mesh;
    WMT::InitializeMeshRenderPipelineInfo(info_mesh);

    info_mesh.object_function = vs.Function;
    info_mesh.mesh_function = hs.Function;
    info_mesh.rasterization_enabled = false;
    info_mesh.payload_memory_length = 16256;

    info_mesh.immutable_object_buffers = (1 << 16) | (1 << 21) | (1 << 29) | (1 << 30);
    info_mesh.immutable_mesh_buffers = (1 << 29) | (1 << 30);

    state_mesh_ = device_->GetMTLDevice().newRenderPipelineState(info_mesh, err);

    if (state_mesh_ == nullptr) {
      ERR("Failed to create tessellation mesh PSO: ", err.description().getUTF8String());
      return this;
    }

    WMTRenderPipelineInfo info;
    WMT::InitializeRenderPipelineInfo(info);

    info.vertex_function = ds.Function;
    if (PixelShader) {
      info.fragment_function = ps.Function;
    }
   info.rasterization_enabled = RasterizationEnabled;

    uint32_t max_tess_factor = hull_reflection.Tessellator.MaxFactor;
    max_tess_factor = std::max(1u, std::min(64u, max_tess_factor));
    switch ((microsoft::D3D11_SB_TESSELLATOR_PARTITIONING)
                hull_reflection.Tessellator.Partition) {
    case microsoft::D3D11_SB_TESSELLATOR_PARTITIONING_INTEGER:
      info.tessellation_partition_mode = WMTTessellationPartitionModeInteger;
      break;
    case microsoft::D3D11_SB_TESSELLATOR_PARTITIONING_POW2:
      info.tessellation_partition_mode = WMTTessellationPartitionModePow2;
      max_tess_factor = max_tess_factor & ((1u << (31 - __builtin_clz(max_tess_factor)))); // force pow2
      break;
    case microsoft::D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD:
      info.tessellation_partition_mode = WMTTessellationPartitionModeFractionalOdd;
      max_tess_factor = max_tess_factor & (~1u); // force even number
      break;
    case microsoft::D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN:
      info.tessellation_partition_mode = WMTTessellationPartitionModeFractionalEven;
      max_tess_factor = max_tess_factor & (~1u); // force even number
      break;
    case microsoft::D3D11_SB_TESSELLATOR_PARTITIONING_UNDEFINED:
      break;
    }
    info.max_tessellation_factor = max_tess_factor;
    switch (hull_reflection.Tessellator.OutputPrimitive) {
    default:
      D3D11_ASSERT(0 && "unexpected tessellator output primitive");
      break;
    // TODO: figure out why it's inverted
    case MTL_TESSELLATOR_OUTPUT_TRIANGLE_CW:
      info.tessellation_output_winding_order = WMTWindingCounterClockwise;
      break;
    case MTL_TESSELLATOR_TRIANGLE_CCW:
    info.tessellation_output_winding_order = WMTWindingClockwise;
      break;
    }
    info.tessellation_factor_step = WMTTessellationFactorStepFunctionPerPatch;

    for (unsigned i = 0; i < num_rtvs; i++) {
      if (rtv_formats[i] == WMTPixelFormatInvalid)
        continue;
      info.colors[i].pixel_format = rtv_formats[i];
    }

    if (depth_stencil_format != WMTPixelFormatInvalid) {
      info.depth_pixel_format = depth_stencil_format;
    }
    // FIXME: don't hardcoding!
    if (depth_stencil_format == WMTPixelFormatDepth32Float_Stencil8 ||
        depth_stencil_format == WMTPixelFormatDepth24Unorm_Stencil8 ||
        depth_stencil_format == WMTPixelFormatStencil8) {
      info.stencil_pixel_format = depth_stencil_format;
    }

    if (pBlendState) {
      pBlendState->SetupMetalPipelineDescriptor((WMTRenderPipelineBlendInfo *)&info, num_rtvs);
    }

    info.input_primitive_topology = topology_class;
    info.raster_sample_count = SampleCount;
    info.immutable_vertex_buffers = (1 << 20) |  (1 << 21) |  (1 << 22) | (1 << 23) |  (1 << 29) |  (1 << 30);
    info.immutable_fragment_buffers = (1 << 29) | (1 << 30);

    state_rasterization_ = device_->GetMTLDevice().newRenderPipelineState(info, err);
    if (state_rasterization_ == nullptr) {
      ERR("Failed to create tessellation raster PSO: ", err.description().getUTF8String());
    }

    return this;
  }

  bool GetIsDone() { return ready_; }

  void SetIsDone(bool state) {
    ready_.store(state);
    ready_.notify_all();
  }

private:
  UINT num_rtvs;
  WMTPixelFormat rtv_formats[8];
  WMTPixelFormat depth_stencil_format;
  WMTPrimitiveTopologyClass topology_class;
  MTLD3D11Device *device_;
  std::atomic_bool ready_;
  IMTLD3D11BlendState *pBlendState;
  WMT::Reference<WMT::RenderPipelineState> state_mesh_;
  WMT::Reference<WMT::RenderPipelineState> state_rasterization_;
  bool RasterizationEnabled;
  UINT SampleCount;

  MTL_SHADER_REFLECTION hull_reflection;

  Com<CompiledShader> VertexShader;
  Com<CompiledShader> PixelShader;
  Com<CompiledShader> HullShader;
  Com<CompiledShader> DomainShader;
};

Com<IMTLCompiledTessellationPipeline>
CreateTessellationPipeline(MTLD3D11Device *pDevice,
                           MTL_GRAPHICS_PIPELINE_DESC *pDesc) {
  Com<IMTLCompiledTessellationPipeline> pipeline =
      new MTLCompiledTessellationPipeline(pDevice, pDesc);
  pipeline->SubmitWork();
  return pipeline;
}

} // namespace dxmt