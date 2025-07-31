#pragma once
#include "d3d11_device_child.hpp"

#include "com/com_pointer.hpp"
#include "d3d11_input_layout.hpp"
#include "d3d11_shader.hpp"
#include "d3d11_state_object.hpp"
#include "d3d11_view.hpp"
#include "dxmt_binding_set.hpp"
#include "log/log.hpp"
#include "mtld11_resource.hpp"
#include "util_string.hpp"

namespace dxmt {

struct UAV_B {
  IUnknown *RawPointer = 0;
  Com<D3D11UnorderedAccessView> View;
};

typedef BindingSet<UAV_B, 64> UAVBindingSet;

template <> struct redunant_binding_trait<UAV_B> {
  static bool is_redunant(const UAV_B &left, const UAV_B &right) {
    return left.RawPointer == right.RawPointer;
  }
};

struct D3D11ComputeStageState {
  UAVBindingSet UAVs;
};

struct CONSTANT_BUFFER_B {
  IUnknown *RawPointer = 0;
  Com<D3D11ResourceCommon> Buffer;
  UINT FirstConstant;
  UINT NumConstants;
};

typedef BindingSet<CONSTANT_BUFFER_B, 14> ConstantBufferBindingSet;

template <> struct redunant_binding_trait<CONSTANT_BUFFER_B> {
  static bool is_redunant(const CONSTANT_BUFFER_B &left,
                          const CONSTANT_BUFFER_B &right) {
    return left.RawPointer == right.RawPointer;
  }
};

struct SAMPLER_B {
  IUnknown *RawPointer = 0;
  IMTLD3D11SamplerState* Sampler;
};

typedef BindingSet<SAMPLER_B, 16> SamplerBindingSet;

template <> struct redunant_binding_trait<SAMPLER_B> {
  static bool is_redunant(const SAMPLER_B &left, const SAMPLER_B &right) {
    return left.RawPointer == right.RawPointer;
  }
};

struct SRV_B {
  IUnknown *RawPointer = 0;
  Com<D3D11ShaderResourceView> SRV;
};

typedef BindingSet<SRV_B, 128> SRVBindingSet;

template <> struct redunant_binding_trait<SRV_B> {
  static bool is_redunant(const SRV_B &left, const SRV_B &right) {
    return left.RawPointer == right.RawPointer;
  }
};

struct D3D11ShaderStageState {
  SRVBindingSet SRVs;
  SamplerBindingSet Samplers;
  ConstantBufferBindingSet ConstantBuffers;
  Com<IMTLD3D11Shader> Shader;
};

struct VERTEX_BUFFER_B {
  IUnknown *RawPointer = 0;
  Com<D3D11ResourceCommon> Buffer;
  UINT Stride;
  UINT Offset;
};

template <> struct redunant_binding_trait<VERTEX_BUFFER_B> {
  static bool is_redunant(const VERTEX_BUFFER_B &left,
                          const VERTEX_BUFFER_B &right) {
    return left.RawPointer == right.RawPointer;
  }
};

struct D3D11InputAssemblerStageState {
  Com<IMTLD3D11InputLayout> InputLayout;
  BindingSet<VERTEX_BUFFER_B, 16> VertexBuffers;
  Com<D3D11ResourceCommon> IndexBuffer;
  /**
  either DXGI_FORMAT_R16_UINT or DXGI_FORMAT_R32_UINT
  */
  DXGI_FORMAT IndexBufferFormat;
  UINT IndexBufferOffset;
  D3D11_PRIMITIVE_TOPOLOGY Topology;
};

struct D3D11OutputMergerStageState {
  Com<IMTLD3D11RenderTargetView> RTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
  Com<IMTLD3D11DepthStencilView> DSV;
  UINT NumRTVs;

  UAVBindingSet UAVs;

  IMTLD3D11DepthStencilState* DepthStencilState;
  UINT StencilRef;

  IMTLD3D11BlendState* BlendState;
  FLOAT BlendFactor[4];

  UINT SampleMask = 0xffffffff;

  // state derived from valid RTV/DSV
  UINT SampleCount = 1;
  UINT ArrayLength = 0;
};

struct STREAM_OUTPUT_BUFFER_B {
  IUnknown *RawPointer = 0;
  Com<D3D11ResourceCommon> Buffer;
  UINT Offset;
};

template <> struct redunant_binding_trait<STREAM_OUTPUT_BUFFER_B> {
  static bool is_redunant(const STREAM_OUTPUT_BUFFER_B &left,
                          const STREAM_OUTPUT_BUFFER_B &right) {
    return left.RawPointer == right.RawPointer;
  }
};

struct D3D11StreamOutputStageState {
  BindingSet<STREAM_OUTPUT_BUFFER_B, 4> Targets;
};

struct D3D11RasterizerStageState {
  D3D11_RECT
  scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {
      {}};
  D3D11_VIEWPORT
  viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {{}};
  UINT NumScissorRects;
  UINT NumViewports;
  IMTLD3D11RasterizerState* RasterizerState;
};

template <typename T> class D3D11StagesState: public std::array<T, 6> {
public:
  T &
  operator[](PipelineStage stage) {
    return this->data()[(uint32_t)stage];
  }

  const T &
  operator[](PipelineStage stage) const {
    return this->data()[(uint32_t)stage];
  }
};

struct D3D11ContextState {
  D3D11StagesState<D3D11ShaderStageState> ShaderStages = {{}};
  D3D11ComputeStageState ComputeStageUAV = {};
  D3D11StreamOutputStageState StreamOutput = {};
  D3D11InputAssemblerStageState InputAssembler = {};
  D3D11OutputMergerStageState OutputMerger = {};
  D3D11RasterizerStageState Rasterizer = {};

  Com<ID3D11Predicate> predicate = nullptr;
  BOOL predicate_value = FALSE;
};

// TODO: implemente it properly
class MTLD3D11DeviceContextState
    : public MTLD3D11DeviceChild<ID3DDeviceContextState> {

public:
  MTLD3D11DeviceContextState(MTLD3D11Device *pDevice)
      : MTLD3D11DeviceChild<ID3DDeviceContextState>(pDevice) {}

  ~MTLD3D11DeviceContextState() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3DDeviceContextState)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3DDeviceContextState), riid)) {
      WARN("D3DDeviceContextState: Unknown interface query ",
           str::format(riid));
    }

    return E_NOINTERFACE;
  }
};

} // namespace dxmt