
#include "Metal.hpp"
#include "d3d11_device.hpp"
#include "d3d11_state_object.hpp"
#include "log/log.hpp"
#include "../d3d10/d3d10_state_object.hpp"

namespace dxmt {

constexpr WMTCompareFunction kCompareFunctionMap[] = {
  WMTCompareFunctionNever, // padding 0
  WMTCompareFunctionNever, // 1 - 1
  WMTCompareFunctionLess,
  WMTCompareFunctionEqual,
  WMTCompareFunctionLessEqual,
  WMTCompareFunctionGreater,
  WMTCompareFunctionNotEqual,
  WMTCompareFunctionGreaterEqual,
  WMTCompareFunctionAlways // 8 - 1
};

constexpr WMTSamplerAddressMode kAddressModeMap[] = {
    // MTL: Texture coordinates wrap to the other side of the texture,
    // effectively keeping only the fractional part of the texture coordinate.

    // MSDN: Tile the texture at every (u,v) integer junction. For example, for
    // u values between 0 and 3, the texture is repeated three times.
    WMTSamplerAddressModeRepeat, // 1 - 1

    // MTL:Between -1.0 and 1.0, the texture coordinates are mirrored across the
    // axis; outside -1.0 and 1.0, the image is repeated.

    // MSDN: Flip the texture at every (u,v) integer junction. For u values
    // between 0 and 1, for example, the texture is addressed normally; between
    // 1 and 2, the texture is flipped (mirrored); between 2 and 3, the texture
    // is normal again; and so on.
    WMTSamplerAddressModeMirrorRepeat,
    // MTL: Texture coordinates are clamped between 0.0 and 1.0, inclusive.
    // MSDN: Texture coordinates outside the range [0.0, 1.0] are set to the
    // texture color at 0.0 or 1.0, respectively.
    WMTSamplerAddressModeClampToEdge,

    // MTL: Out-of-range texture coordinates return the value specified by the
    // borderColor property.

    // MSDN: Texture coordinates outside the range [0.0, 1.0] are set to the
    // border color specified in D3D11_SAMPLER_DESC or HLSL code.
    WMTSamplerAddressModeClampToBorderColor,
    // MTL: Between -1.0 and 1.0, the texture coordinates are mirrored across
    // the axis; outside -1.0 and 1.0, texture coordinates are clamped.

    // MSDN:
    //  Similar to D3D11_TEXTURE_ADDRESS_MIRROR and D3D11_TEXTURE_ADDRESS_CLAMP.
    //  Takes the absolute value of the texture coordinate (thus, mirroring
    //  around 0), and then clamps to the maximum value.
    WMTSamplerAddressModeMirrorClampToEdge // 5 - 1
};

constexpr WMTStencilOperation kStencilOperationMap[] = {
    WMTStencilOperationZero, // invalid
    WMTStencilOperationKeep,
    WMTStencilOperationZero,
    WMTStencilOperationReplace,
    // D3D11_STENCIL_OP_INCR_SAT: Increment the stencil value by 1, and clamp
    // the result.
    WMTStencilOperationIncrementClamp,
    WMTStencilOperationDecrementClamp,
    WMTStencilOperationInvert,
    // D3D11_STENCIL_OP_INCR:Increment the stencil value by 1, and wrap the
    // result if necessary.

    WMTStencilOperationIncrementWrap,
    WMTStencilOperationDecrementWrap,

};

constexpr WMTBlendOperation kBlendOpMap[] = {
    WMTBlendOperationAdd, // padding 0
    WMTBlendOperationAdd,
    WMTBlendOperationSubtract,
    WMTBlendOperationReverseSubtract,
    WMTBlendOperationMin,
    WMTBlendOperationMax,
};

constexpr WMTLogicOperation kLogicOpMap[] = {
    WMTLogicOperationClear,      WMTLogicOperationSet,
    WMTLogicOperationCopy,       WMTLogicOperationCopyInverted,
    WMTLogicOperationNoOp,       WMTLogicOperationInvert,
    WMTLogicOperationAnd,        WMTLogicOperationNand,
    WMTLogicOperationOr,         WMTLogicOperationNor,
    WMTLogicOperationXor,        WMTLogicOperationEquiv,
    WMTLogicOperationAndReverse, WMTLogicOperationAndInverted,
    WMTLogicOperationOrReverse,  WMTLogicOperationOrInverted,
};

constexpr WMTBlendFactor kBlendFactorMap[] = {
    WMTBlendFactorZero, // padding 0
    WMTBlendFactorZero,
    WMTBlendFactorOne,
    WMTBlendFactorSourceColor,
    WMTBlendFactorOneMinusSourceColor,
    WMTBlendFactorSourceAlpha,
    WMTBlendFactorOneMinusSourceAlpha,
    WMTBlendFactorDestinationAlpha,
    WMTBlendFactorOneMinusDestinationAlpha,
    WMTBlendFactorDestinationColor,
    WMTBlendFactorOneMinusDestinationColor,
    WMTBlendFactorSourceAlphaSaturated,
    WMTBlendFactorZero,       // invalid,12
    WMTBlendFactorZero,       // invalid,13
    WMTBlendFactorBlendColor, // BLEND_FACTOR
    WMTBlendFactorOneMinusBlendColor,
    WMTBlendFactorSource1Color,
    WMTBlendFactorOneMinusSource1Color,
    WMTBlendFactorSource1Alpha,
    WMTBlendFactorOneMinusSource1Alpha,
};

constexpr WMTBlendFactor kBlendAlphaFactorMap[] = {
    WMTBlendFactorZero, // padding 0
    WMTBlendFactorZero,
    WMTBlendFactorOne,
    WMTBlendFactorSourceColor,
    WMTBlendFactorOneMinusSourceColor,
    WMTBlendFactorSourceAlpha,
    WMTBlendFactorOneMinusSourceAlpha,
    WMTBlendFactorDestinationAlpha,
    WMTBlendFactorOneMinusDestinationAlpha,
    WMTBlendFactorDestinationColor,
    WMTBlendFactorOneMinusDestinationColor,
    WMTBlendFactorSourceAlphaSaturated,
    WMTBlendFactorZero,       // invalid,12
    WMTBlendFactorZero,       // invalid,13
    WMTBlendFactorBlendAlpha, // BLEND_FACTOR
    WMTBlendFactorOneMinusBlendAlpha,
    WMTBlendFactorSource1Color,
    WMTBlendFactorOneMinusSource1Color,
    WMTBlendFactorSource1Alpha,
    WMTBlendFactorOneMinusSource1Alpha,
};

constexpr WMTColorWriteMask kColorWriteMaskMap[] = {
    // 0000
    WMTColorWriteMaskNone,
    // 0001
    WMTColorWriteMaskRed,
    // 0010
    WMTColorWriteMaskGreen,
    // 0011,
    WMTColorWriteMaskRed | WMTColorWriteMaskGreen,
    // 0100
    WMTColorWriteMaskBlue,
    // 0101
    WMTColorWriteMaskBlue | WMTColorWriteMaskRed,
    // 0110
    WMTColorWriteMaskBlue | WMTColorWriteMaskGreen,
    // 0111
    WMTColorWriteMaskBlue | WMTColorWriteMaskRed | WMTColorWriteMaskGreen,

    // 1000
    WMTColorWriteMaskAlpha,
    // 1001
    WMTColorWriteMaskAlpha | WMTColorWriteMaskRed,
    // 1010
    WMTColorWriteMaskAlpha | WMTColorWriteMaskGreen,
    // 1011,
    WMTColorWriteMaskAlpha | WMTColorWriteMaskRed |
        WMTColorWriteMaskGreen,
    // 1100
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue,
    // 0101
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue | WMTColorWriteMaskRed,
    // 1110
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue |
        WMTColorWriteMaskGreen,
    // 1111
    WMTColorWriteMaskAlpha | WMTColorWriteMaskBlue |
        WMTColorWriteMaskRed | WMTColorWriteMaskGreen,
};

class MTLD3D11SamplerState : public ManagedDeviceChild<IMTLD3D11SamplerState> {

public:
  friend class MTLD3D11DeviceContext;
  MTLD3D11SamplerState(MTLD3D11Device *device, const WMTSamplerInfo &info,
                       WMT::Reference<WMT::SamplerState> &&samplerState,
                       const D3D11_SAMPLER_DESC &desc, float lod_bias)
      : ManagedDeviceChild<IMTLD3D11SamplerState>(device), desc_(desc),
        metal_sampler_state_(std::move(samplerState)), d3d10_(this), info(info),
        lod_bias(lod_bias) {}
  ~MTLD3D11SamplerState() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11SamplerState) ||
        riid == __uuidof(IMTLD3D11SamplerState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D10DeviceChild) ||
        riid == __uuidof(ID3D10SamplerState)) {
      *ppvObject = ref(&d3d10_);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IMTLD3D11SamplerState), riid)) {
      WARN("D3D11SamplerState: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void STDMETHODCALLTYPE GetDesc(D3D11_SAMPLER_DESC *pDesc) final {
    *pDesc = desc_;
  }

  virtual WMT::SamplerState GetSamplerState() {
    return metal_sampler_state_;
  }

  virtual uint64_t GetArgumentHandle() { return info.gpu_resource_id; }

  virtual float GetLODBias() { return lod_bias; }

private:
  const D3D11_SAMPLER_DESC desc_;
  WMT::Reference<WMT::SamplerState> metal_sampler_state_;
  MTLD3D10SamplerState d3d10_;
  WMTSamplerInfo info;
  float lod_bias;
};

// BlendState

class MTLD3D11BlendState : public ManagedDeviceChild<IMTLD3D11BlendState> {

public:
  friend class MTLD3D11DeviceContext;
  MTLD3D11BlendState(MTLD3D11Device *device, const D3D11_BLEND_DESC1 &desc)
      : ManagedDeviceChild<IMTLD3D11BlendState>(device), desc_(desc), d3d10_(this) {
    dual_source_blending_ =
        (desc_.RenderTarget[0].SrcBlend >= D3D11_BLEND_SRC1_COLOR ||
         desc_.RenderTarget[0].SrcBlendAlpha >= D3D11_BLEND_SRC1_COLOR ||
         desc_.RenderTarget[0].DestBlendAlpha >= D3D11_BLEND_SRC1_COLOR ||
         desc_.RenderTarget[0].DestBlend >= D3D11_BLEND_SRC1_COLOR);
  }
  ~MTLD3D11BlendState() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11BlendState) ||
        riid == __uuidof(ID3D11BlendState1) ||
        riid == __uuidof(IMTLD3D11BlendState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D10DeviceChild) ||
        riid == __uuidof(ID3D10BlendState) ||
        riid == __uuidof(ID3D10BlendState1)) {
      *ppvObject = ref(&d3d10_);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IMTLD3D11BlendState), riid)) {
      WARN("D3D11BlendState: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void STDMETHODCALLTYPE GetDesc(D3D11_BLEND_DESC *pDesc) final {
    pDesc->IndependentBlendEnable = desc_.IndependentBlendEnable;
    pDesc->AlphaToCoverageEnable = desc_.AlphaToCoverageEnable;
    for (size_t i = 0; i < 8; i++) {
      auto &renderTarget = desc_.RenderTarget[i];
      pDesc->RenderTarget[i].RenderTargetWriteMask =
          renderTarget.RenderTargetWriteMask;
      pDesc->RenderTarget[i].BlendEnable = renderTarget.BlendEnable;
      pDesc->RenderTarget[i].BlendOp = renderTarget.BlendOp;
      pDesc->RenderTarget[i].BlendOpAlpha = renderTarget.BlendOpAlpha;
      pDesc->RenderTarget[i].SrcBlend = renderTarget.SrcBlend;
      pDesc->RenderTarget[i].DestBlend = renderTarget.SrcBlendAlpha;
      pDesc->RenderTarget[i].SrcBlendAlpha = renderTarget.DestBlend;
      pDesc->RenderTarget[i].DestBlendAlpha = renderTarget.DestBlendAlpha;
    }
  }

  void STDMETHODCALLTYPE GetDesc1(D3D11_BLEND_DESC1 *pDesc) final {
    *pDesc = desc_;
  }

  bool IsDualSourceBlending() { return dual_source_blending_; }

  void SetupMetalPipelineDescriptor(WMTRenderPipelineBlendInfo *info, uint32_t num_rt) {
    for (unsigned rt = 0; rt < num_rt; rt++) {
      auto i = desc_.IndependentBlendEnable ? rt : 0;
      auto &renderTarget = desc_.RenderTarget[i];
      auto &attachment_desc = info->colors[rt];
      attachment_desc.write_mask = kColorWriteMaskMap[renderTarget.RenderTargetWriteMask];
      if (renderTarget.BlendEnable && attachment_desc.pixel_format != WMTPixelFormatInvalid) {
        if (!any_bit_set(m_parent->GetMTLPixelFormatCapability(attachment_desc.pixel_format) & FormatCapability::Blend)) {
          WARN("Blending is enabled on RTV of non-blendable format ", attachment_desc.pixel_format);
          continue;
        }
        attachment_desc.alpha_blend_operation = kBlendOpMap[renderTarget.BlendOpAlpha];
        attachment_desc.rgb_blend_operation = kBlendOpMap[renderTarget.BlendOp];
        attachment_desc.blending_enabled = renderTarget.BlendEnable;
        attachment_desc.src_alpha_blend_factor = kBlendAlphaFactorMap[renderTarget.SrcBlendAlpha];
        attachment_desc.src_rgb_blend_factor = kBlendFactorMap[renderTarget.SrcBlend];
        attachment_desc.dst_alpha_blend_factor = kBlendAlphaFactorMap[renderTarget.DestBlendAlpha];
        attachment_desc.dst_rgb_blend_factor = kBlendFactorMap[renderTarget.DestBlend];
      }
    }
    /*
    D3D11.3 Spec:
    Configuration of logic op is constrained in the following way:
    (a) for logic ops to be used, IndependentBlendEnable must be set to false, so the logic op that has meaning comes
        from the first RT blend desc and applies to all RTs.
    (b) when logic blending all RenderTargets bound must have a UINT format (undefined rendering otherwise).
    */
    if (!desc_.IndependentBlendEnable && desc_.RenderTarget[0].LogicOpEnable) {
#ifdef DXMT_NO_PRIVATE_API
      ERR("OutputMerger LogicOp is not supported");
      continue;
#else
      info->logic_operation_enabled = true;
      info->logic_operation = kLogicOpMap[desc_.RenderTarget[0].LogicOp];
#endif
    }
    info->alpha_to_coverage_enabled = desc_.AlphaToCoverageEnable;
  }

private:
  const D3D11_BLEND_DESC1 desc_;
  bool dual_source_blending_;
  MTLD3D10BlendState d3d10_;
};

// RasterizerState

class MTLD3D11RasterizerState
    : public ManagedDeviceChild<IMTLD3D11RasterizerState> {

public:
  friend class MTLD3D11DeviceContext;
  MTLD3D11RasterizerState(MTLD3D11Device *device,
                          const D3D11_RASTERIZER_DESC2 *desc)
      : ManagedDeviceChild<IMTLD3D11RasterizerState>(device), desc_(*desc), d3d10_(this) {}
  ~MTLD3D11RasterizerState() {};

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11RasterizerState) ||
        riid == __uuidof(ID3D11RasterizerState1) ||
        riid == __uuidof(ID3D11RasterizerState2) ||
        riid == __uuidof(IMTLD3D11RasterizerState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D10DeviceChild) ||
        riid == __uuidof(ID3D10RasterizerState)) {
      *ppvObject = ref(&d3d10_);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IMTLD3D11RasterizerState), riid)) {
      WARN("D3D11RasterizerState: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void STDMETHODCALLTYPE GetDesc(D3D11_RASTERIZER_DESC *pDesc) final {
    pDesc->FillMode = desc_.FillMode;
    pDesc->CullMode = desc_.CullMode;
    pDesc->FrontCounterClockwise = desc_.FrontCounterClockwise;
    pDesc->DepthBias = desc_.DepthBias;
    pDesc->DepthBiasClamp = desc_.DepthBiasClamp;
    pDesc->SlopeScaledDepthBias = desc_.SlopeScaledDepthBias;
    pDesc->DepthClipEnable = desc_.DepthClipEnable;
    pDesc->ScissorEnable = desc_.ScissorEnable;
    pDesc->MultisampleEnable = desc_.MultisampleEnable;
    pDesc->AntialiasedLineEnable = desc_.AntialiasedLineEnable;
  }

  void STDMETHODCALLTYPE GetDesc1(D3D11_RASTERIZER_DESC1 *pDesc) final {
    pDesc->FillMode = desc_.FillMode;
    pDesc->CullMode = desc_.CullMode;
    pDesc->FrontCounterClockwise = desc_.FrontCounterClockwise;
    pDesc->DepthBias = desc_.DepthBias;
    pDesc->DepthBiasClamp = desc_.DepthBiasClamp;
    pDesc->SlopeScaledDepthBias = desc_.SlopeScaledDepthBias;
    pDesc->DepthClipEnable = desc_.DepthClipEnable;
    pDesc->ScissorEnable = desc_.ScissorEnable;
    pDesc->MultisampleEnable = desc_.MultisampleEnable;
    pDesc->AntialiasedLineEnable = desc_.AntialiasedLineEnable;
    pDesc->ForcedSampleCount = desc_.ForcedSampleCount;
  }

  void STDMETHODCALLTYPE GetDesc2(D3D11_RASTERIZER_DESC2 *pDesc) final {
    *pDesc = desc_;
  }

  void SetupRasterizerState(wmtcmd_render_setrasterizerstate& cmd) {

    cmd.fill_mode = desc_.FillMode == D3D11_FILL_SOLID ? WMTTriangleFillModeFill : WMTTriangleFillModeLines;

    switch (desc_.CullMode) {
    case D3D11_CULL_BACK:
      cmd.cull_mode = WMTCullModeBack;
      break;
    case D3D11_CULL_FRONT:
      cmd.cull_mode = WMTCullModeFront;
      break;
    case D3D11_CULL_NONE:
      cmd.cull_mode = WMTCullModeNone;
      break;
    }

    // https://github.com/gpuweb/gpuweb/issues/2100#issuecomment-924536243
    cmd.depth_clip_mode = desc_.DepthClipEnable ? WMTDepthClipModeClip : WMTDepthClipModeClamp;

    cmd.depth_bias = desc_.DepthBias;
    cmd.scole_scale = desc_.SlopeScaledDepthBias;
    cmd.depth_bias_clamp = desc_.DepthBiasClamp;

    cmd.winding = desc_.FrontCounterClockwise ? WMTWindingCounterClockwise: WMTWindingClockwise;

    if (desc_.AntialiasedLineEnable) {
      // nop , we don't support this
    }

    if (desc_.MultisampleEnable) {
      // used in combination with AntialiasedLineEnable, not supported yet.
    }

    // desc_.ScissorEnable : handled outside

    // desc_.ForcedSampleCount : handled outside
  }

  bool IsScissorEnabled() { return desc_.ScissorEnable; }

  uint32_t UAVOnlySampleCount() { return std::max(1u, desc_.ForcedSampleCount); }

private:
  const D3D11_RASTERIZER_DESC2 desc_;
  MTLD3D10RasterizerState d3d10_;
};

// DepthStencilState

class MTLD3D11DepthStencilState
    : public ManagedDeviceChild<IMTLD3D11DepthStencilState> {

public:
  friend class MTLD3D11DeviceContext;
  MTLD3D11DepthStencilState(MTLD3D11Device *device,
                            WMT::DepthStencilState state,
                            WMT::DepthStencilState stencil_disabled,
                            WMT::DepthStencilState depthsteancil_disabled,
                            const D3D11_DEPTH_STENCIL_DESC &desc)
      : ManagedDeviceChild<IMTLD3D11DepthStencilState>(device), desc_(desc), d3d10_(this),
        state_default_(state), state_stencil_disabled_(stencil_disabled),
        state_depthstencil_disabled_(depthsteancil_disabled) {}
  ~MTLD3D11DepthStencilState() {};

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11DepthStencilState) ||
        riid == __uuidof(IMTLD3D11DepthStencilState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D10DeviceChild) ||
        riid == __uuidof(ID3D10DepthStencilState)) {
      *ppvObject = ref(&d3d10_);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IMTLD3D11DepthStencilState), riid)) {
      WARN("D3D11DepthStencilState: Unknown interface query ",
           str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void STDMETHODCALLTYPE GetDesc(D3D11_DEPTH_STENCIL_DESC *pDesc) final {
    *pDesc = desc_;
  }

  virtual WMT::DepthStencilState GetDepthStencilState(uint32_t planar_flags) {
    switch (planar_flags) {
    case 0:
      return state_depthstencil_disabled_;
    case 1: /* depth without stencil */
      return state_stencil_disabled_;
    case 2: /* stencil without depth */
      ERR("stencil only dsv is not properly handled");
      return state_default_;
    default:
      return state_default_;
    }
  }

  virtual bool IsEnabled() {
    return desc_.DepthEnable || desc_.StencilEnable;
  }

private:
  const D3D11_DEPTH_STENCIL_DESC desc_;
  MTLD3D10DepthStencilState d3d10_;
  WMT::Reference<WMT::DepthStencilState> state_default_;
  WMT::Reference<WMT::DepthStencilState> state_stencil_disabled_;
  WMT::Reference<WMT::DepthStencilState> state_depthstencil_disabled_;
};

template <>
HRESULT StateObjectCache<D3D11_DEPTH_STENCIL_DESC, IMTLD3D11DepthStencilState>::
    CreateStateObject(const D3D11_DEPTH_STENCIL_DESC *pDesc,
                      IMTLD3D11DepthStencilState **ppDepthStencilState) {
  std::lock_guard<dxmt::mutex> lock(mutex_cache);
  InitReturnPtr(ppDepthStencilState);

  // TODO: validation
  if (!pDesc)
    return E_INVALIDARG;

  if (!ppDepthStencilState)
    return S_FALSE;

  if (cache.contains(*pDesc)) {
    cache.at(*pDesc)->QueryInterface(IID_PPV_ARGS(ppDepthStencilState));
    return S_OK;
  }

  WMTDepthStencilInfo info;
  info.depth_compare_function = WMTCompareFunctionAlways;
  info.depth_write_enabled = false;
  info.front_stencil.enabled = false;
  info.back_stencil.enabled = false;
  if (pDesc->DepthEnable) {
    info.depth_compare_function = kCompareFunctionMap[pDesc->DepthFunc];
    info.depth_write_enabled = pDesc->DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL;
  } 

  if (pDesc->StencilEnable) {
    info.front_stencil.enabled = true;
    info.back_stencil.enabled = true;
    {
      info.front_stencil.depth_stencil_pass_op =
          (kStencilOperationMap[pDesc->FrontFace.StencilPassOp]);
      info.front_stencil.stencil_fail_op =
          (kStencilOperationMap[pDesc->FrontFace.StencilFailOp]);
      info.front_stencil.depth_fail_op =
          (kStencilOperationMap[pDesc->FrontFace.StencilDepthFailOp]);
      info.front_stencil.stencil_compare_function =
          kCompareFunctionMap[pDesc->FrontFace.StencilFunc];
      info.front_stencil.write_mask = pDesc->StencilWriteMask;
      info.front_stencil.read_mask = pDesc->StencilReadMask;
    }
    {
      info.back_stencil.depth_stencil_pass_op =
          (kStencilOperationMap[pDesc->BackFace.StencilPassOp]);
      info.back_stencil.stencil_fail_op =
          (kStencilOperationMap[pDesc->BackFace.StencilFailOp]);
      info.back_stencil.depth_fail_op =
          (kStencilOperationMap[pDesc->BackFace.StencilDepthFailOp]);
      info.back_stencil.stencil_compare_function =
          kCompareFunctionMap[pDesc->BackFace.StencilFunc];
      info.back_stencil.write_mask = pDesc->StencilWriteMask;
      info.back_stencil.read_mask = pDesc->StencilReadMask;
    }
  }

  auto state_default = device->GetMTLDevice().newDepthStencilState(info);

  if (pDesc->StencilEnable) {
    info.front_stencil.enabled = false;
    info.back_stencil.enabled = false;
    auto stencil_effective_disabled = device->GetMTLDevice().newDepthStencilState(info);
    if (pDesc->DepthEnable) {
      info.depth_compare_function = WMTCompareFunctionAlways;
      info.depth_write_enabled = false;
      auto depthstencil_effective_disabled = device->GetMTLDevice().newDepthStencilState(info);
      cache.emplace(*pDesc,
                    std::make_unique<MTLD3D11DepthStencilState>(
                        device, state_default, stencil_effective_disabled,
                        depthstencil_effective_disabled, *pDesc));
    } else {
      // stencil_effective_disabled has no depth either...
      cache.emplace(*pDesc,
                    std::make_unique<MTLD3D11DepthStencilState>(
                        device, state_default, stencil_effective_disabled,
                        stencil_effective_disabled, *pDesc));
    }
  } else {
    if (pDesc->DepthEnable) {
      info.depth_compare_function = WMTCompareFunctionAlways;
      info.depth_write_enabled = false;
      auto depth_effective_disabled = device->GetMTLDevice().newDepthStencilState(info);
      cache.emplace(*pDesc, std::make_unique<MTLD3D11DepthStencilState>(
                                device, state_default, state_default,
                                depth_effective_disabled, *pDesc));
    } else {
      cache.emplace(*pDesc, std::make_unique<MTLD3D11DepthStencilState>(
                                device, state_default, state_default,
                                state_default, *pDesc));
    }
  }
  cache.at(*pDesc)->QueryInterface(IID_PPV_ARGS(ppDepthStencilState));
  return S_OK;
}

template <>
HRESULT StateObjectCache<D3D11_RASTERIZER_DESC2, IMTLD3D11RasterizerState>::
    CreateStateObject(const D3D11_RASTERIZER_DESC2 *pRasterizerDesc,
                      IMTLD3D11RasterizerState **ppRasterizerState) {
  std::lock_guard<dxmt::mutex> lock(mutex_cache);
  InitReturnPtr(ppRasterizerState);

  // TODO: validate
  if (!pRasterizerDesc)
    return E_INVALIDARG;

  if (!ppRasterizerState)
    return S_FALSE;

  if (cache.contains(*pRasterizerDesc)) {
    cache.at(*pRasterizerDesc)->QueryInterface(IID_PPV_ARGS(ppRasterizerState));
    return S_OK;
  }

  cache.emplace(*pRasterizerDesc, std::make_unique<MTLD3D11RasterizerState>(
                                      device, pRasterizerDesc));
  cache.at(*pRasterizerDesc)->QueryInterface(IID_PPV_ARGS(ppRasterizerState));

  return S_OK;
}

template <>
HRESULT
StateObjectCache<D3D11_SAMPLER_DESC, IMTLD3D11SamplerState>::CreateStateObject(
    const D3D11_SAMPLER_DESC *pSamplerDesc,
    IMTLD3D11SamplerState **ppSamplerState) {
  std::lock_guard<dxmt::mutex> lock(mutex_cache);

  InitReturnPtr(ppSamplerState);

  if (pSamplerDesc == nullptr)
    return E_INVALIDARG;

  D3D11_SAMPLER_DESC desc = *pSamplerDesc;

  // FIXME: validation

  if (!ppSamplerState)
    return S_FALSE;

  if (cache.contains(*pSamplerDesc)) {
    cache.at(*pSamplerDesc)->QueryInterface(IID_PPV_ARGS(ppSamplerState));
    return S_OK;
  }

  WMTSamplerInfo info;

  info.lod_average = false;
  info.mip_filter = WMTSamplerMipFilterNotMipmapped;
  // filter
  if (D3D11_DECODE_MIN_FILTER(desc.Filter)) { // LINEAR = 1
    info.min_filter = WMTSamplerMinMagFilterLinear;
  } else {
    info.min_filter = WMTSamplerMinMagFilterNearest;
  }
  if (D3D11_DECODE_MAG_FILTER(desc.Filter)) { // LINEAR = 1
    info.mag_filter = WMTSamplerMinMagFilterLinear;
  } else {
    info.mag_filter = WMTSamplerMinMagFilterNearest;
  }
  if (D3D11_DECODE_MIP_FILTER(desc.Filter)) { // LINEAR = 1
    info.mip_filter = WMTSamplerMipFilterLinear;
  } else {
    info.mip_filter = WMTSamplerMipFilterNearest;
  }

  info.lod_min_clamp = desc.MinLOD; // -FLT_MAX vs 0?
                    // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetsamplers
  info.lod_max_clamp = desc.MaxLOD;

  // Anisotropy
  if (D3D11_DECODE_IS_ANISOTROPIC_FILTER(desc.Filter)) {
    info.max_anisotroy = desc.MaxAnisotropy;
  } else {
    info.max_anisotroy = 1;
  }

  // address modes
  // U-S  V-T W-R
  if (desc.AddressU > 0 && desc.AddressU < 6) {
    info.s_address_mode = kAddressModeMap[desc.AddressU - 1];
  }
  if (desc.AddressV > 0 && desc.AddressV < 6) {
    info.t_address_mode = kAddressModeMap[desc.AddressV - 1];
  }
  if (desc.AddressW > 0 && desc.AddressW < 6) {
    info.r_address_mode = kAddressModeMap[desc.AddressW - 1];
  }

  info.compare_function = WMTCompareFunctionNever;
  if (D3D11_DECODE_IS_COMPARISON_FILTER(desc.Filter)) {
    if (desc.ComparisonFunc < 1 || desc.ComparisonFunc > 8) {
      WARN("CreateSamplerState: invalid ComparisonFunc");
    } else {
      info.compare_function = kCompareFunctionMap[desc.ComparisonFunc];
    }
  }

  // border color
  info.border_color = WMTSamplerBorderColorOpaqueWhite;
  if ((desc.AddressU == D3D11_TEXTURE_ADDRESS_BORDER ||
       desc.AddressV == D3D11_TEXTURE_ADDRESS_BORDER ||
       desc.AddressW == D3D11_TEXTURE_ADDRESS_BORDER)) {
    if (desc.BorderColor[0] == 0.0f && desc.BorderColor[1] == 0.0f &&
        desc.BorderColor[2] == 0.0f && desc.BorderColor[3] == 0.0f) {
      info.border_color = WMTSamplerBorderColorTransparentBlack;
    } else if (desc.BorderColor[0] == 0.0f && desc.BorderColor[1] == 0.0f &&
               desc.BorderColor[2] == 0.0f && desc.BorderColor[3] == 1.0f) {
      info.border_color = WMTSamplerBorderColorOpaqueBlack;
    } else if (desc.BorderColor[0] == 1.0f && desc.BorderColor[1] == 1.0f &&
               desc.BorderColor[2] == 1.0f && desc.BorderColor[3] == 1.0f) {
      info.border_color = WMTSamplerBorderColorOpaqueWhite;
    } else {
      WARN("CreateSamplerState: Unsupported border color (",
           desc.BorderColor[0], ", ", desc.BorderColor[1], ", ",
           desc.BorderColor[2], ", ", desc.BorderColor[3], ")");
    }
  }

  info.support_argument_buffers = true;
  info.normalized_coords = true;

  auto mtl_sampler = device->GetMTLDevice().newSamplerState(info);

  cache.emplace(*pSamplerDesc, std::make_unique<MTLD3D11SamplerState>(
                                   device, info, std::move(mtl_sampler), desc,
                                   desc.MipLODBias));
  cache.at(*pSamplerDesc)->QueryInterface(IID_PPV_ARGS(ppSamplerState));

  return S_OK;
};

template <>
HRESULT
StateObjectCache<D3D11_BLEND_DESC1, IMTLD3D11BlendState>::CreateStateObject(
    const D3D11_BLEND_DESC1 *pBlendStateDesc,
    IMTLD3D11BlendState **ppBlendState) {
  std::lock_guard<dxmt::mutex> lock(mutex_cache);
  InitReturnPtr(ppBlendState);

  if (!pBlendStateDesc)
    return E_INVALIDARG;

  unsigned num_blend_target = pBlendStateDesc->IndependentBlendEnable ? 8 : 1;
  for (unsigned i = 0; i < num_blend_target; i++) {
    auto &blend_target = pBlendStateDesc->RenderTarget[i];
    if (blend_target.BlendEnable && blend_target.LogicOpEnable) {
      return E_INVALIDARG;
    }
  }

  if (!ppBlendState)
    return S_FALSE;

  D3D11_BLEND_DESC1 desc_normalized = *pBlendStateDesc;
  desc_normalized.AlphaToCoverageEnable =
      bool(desc_normalized.AlphaToCoverageEnable);
  desc_normalized.IndependentBlendEnable =
      bool(desc_normalized.IndependentBlendEnable);

  if (cache.contains(desc_normalized)) {
    cache.at(desc_normalized)->QueryInterface(IID_PPV_ARGS(ppBlendState));
    return S_OK;
  }

  cache.emplace(desc_normalized,
                std::make_unique<MTLD3D11BlendState>(device, desc_normalized));
  cache.at(desc_normalized)->QueryInterface(IID_PPV_ARGS(ppBlendState));

  return S_OK;
}

} // namespace dxmt
