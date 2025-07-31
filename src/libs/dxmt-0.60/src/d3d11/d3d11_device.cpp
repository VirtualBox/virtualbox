#include "com/com_guid.hpp"
#include "d3d11_fence.hpp"
#include "d3d11_input_layout.hpp"
#include "d3d11_interfaces.hpp"
#include "d3d11_multithread.hpp"
#include "d3d11_pipeline.hpp"
#include "d3d11_class_linkage.hpp"
#include "d3d11_inspection.hpp"
#include "d3d11_context.hpp"
#include "d3d11_context_state.hpp"
#include "d3d11_device.hpp"
#include "d3d11_pipeline_cache.hpp"
#include "d3d11_private.h"
#include "d3d11_query.hpp"
#include "d3d11_shader.hpp"
#include "d3d11_swapchain.hpp"
#include "d3d11_state_object.hpp"
#include "dxgi_interfaces.h"
#include "../d3d10/d3d10_device.hpp"
#include "dxmt_command_queue.hpp"
#include "dxmt_device.hpp"
#include "dxmt_format.hpp"
#include "dxmt_tasks.hpp"
#include "ftl.hpp"
#include "mtld11_resource.hpp"
#include "thread.hpp"
#include "dxgi_object.hpp"
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "d3d11_4.h"
#include "util_win32_compat.h"

namespace dxmt {

template<>
struct task_trait<IMTLThreadpoolWork*> {
  IMTLThreadpoolWork* run_task(IMTLThreadpoolWork* task) {
    return task->RunThreadpoolWork();
  }
  bool get_done(IMTLThreadpoolWork* task) {
    return task->GetIsDone();
  }
  void set_done(IMTLThreadpoolWork* task) {
    task->SetIsDone(true);
  }
};

const GUID kRenderdocUUID = {0xa7aa6116,
                             0x9c8d,
                             0x4bba,
                             {0x90, 0x83, 0xb4, 0xd8, 0x16, 0xb7, 0x1b, 0x78}};
const GUID kPixUUID = {0x9f251514,
                       0x9d4d,
                       0x4902,
                       {0x9d, 0x60, 0x18, 0x98, 0x8a, 0xb7, 0xd4, 0xb5}};
const GUID kGpaUUID = {0xccffef16,
                       0x7b69,
                       0x468f,
                       {0xbc, 0xe3, 0xcd, 0x95, 0x33, 0x69, 0xa3, 0x9a}};

class MTLD3D11DeviceImpl final : public MTLD3D11Device, public IMTLD3D11DeviceExt {
friend class MTLD3D11DXGIDevice;
public:
  MTLD3D11DeviceImpl(MTLDXGIObject<IMTLDXGIDevice> *container,
                 IMTLDXGIAdapter *pAdapter, D3D_FEATURE_LEVEL FeatureLevel,
                 UINT FeatureFlags, Device &device)
      : m_container(container), adapter_(pAdapter),
        m_FeatureLevel(FeatureLevel), m_FeatureFlags(FeatureFlags),
        m_features(container->GetMTLDevice()), sampler_states(this),
        rasterizer_states(this), depthstencil_states(this),
        device_(device), d3dmt_(static_cast<ID3D11Device *>(this), mutex) {
    commandlist_pool_ = InitializeCommandListPool(this);
    pipeline_cache_ = InitializePipelineCache(this);
    context_ = InitializeImmediateContext(this, device_.queue());
    d3d10_ = std::make_unique<MTLD3D10Device>(this, context_.get());
    is_traced_ = !!::GetModuleHandle("dxgitrace.dll");
    format_inspector.Inspect(GetMTLDevice());
  }

  ~MTLD3D11DeviceImpl() {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    return m_container->QueryInterface(riid, ppvObject);
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return m_container->AddRef(); }

  ULONG STDMETHODCALLTYPE Release() override { return m_container->Release(); }

  void AddRefPrivate() override { return m_container->AddRefPrivate(); }

  void ReleasePrivate() override { return m_container->ReleasePrivate(); }

  bool IsTraced() override { return is_traced_; }

  HRESULT STDMETHODCALLTYPE
  CreateBuffer(const D3D11_BUFFER_DESC *pDesc,
               const D3D11_SUBRESOURCE_DATA *pInitialData,
               ID3D11Buffer **ppBuffer) override {
    InitReturnPtr(ppBuffer);

    if (pDesc->ByteWidth == 0 && !(pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILE_POOL))
      return E_INVALIDARG; 

    try {
      switch (pDesc->Usage) {
      case D3D11_USAGE_DEFAULT:
      case D3D11_USAGE_IMMUTABLE:
      case D3D11_USAGE_DYNAMIC:
        return dxmt::CreateBuffer(this, pDesc, pInitialData, ppBuffer);
      case D3D11_USAGE_STAGING:
        return CreateStagingBuffer(this, pDesc, pInitialData, ppBuffer);
      default:
        DXMT_UNREACHABLE
      }
    } catch (const MTLD3DError &err) {
      ERR(err.message());
      return E_FAIL;
    }
  }

  HRESULT STDMETHODCALLTYPE
  CreateTexture1D(const D3D11_TEXTURE1D_DESC *pDesc,
                  const D3D11_SUBRESOURCE_DATA *pInitialData,
                  ID3D11Texture1D **ppTexture1D) override {

    InitReturnPtr(ppTexture1D);

    if (!pDesc)
      return E_INVALIDARG;

    if (pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILED)
      return E_INVALIDARG; // not supported yet

    try {
      switch (pDesc->Usage) {
      case D3D11_USAGE_DEFAULT:
      case D3D11_USAGE_IMMUTABLE:
        return CreateDeviceTexture1D(this, pDesc, pInitialData, ppTexture1D);
      case D3D11_USAGE_DYNAMIC:
        return CreateDynamicTexture1D(this, pDesc, pInitialData, ppTexture1D);
      case D3D11_USAGE_STAGING:
        if (pDesc->BindFlags != 0) {
          return E_INVALIDARG;
        }
        return CreateStagingTexture1D(this, pDesc, pInitialData, ppTexture1D);
      }
      return S_OK;
    } catch (const MTLD3DError &err) {
      ERR(err.message());
      return E_FAIL;
    }
  }

  HRESULT STDMETHODCALLTYPE
  CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc,
                  const D3D11_SUBRESOURCE_DATA *pInitialData,
                  ID3D11Texture2D **ppTexture2D) override {
    D3D11_TEXTURE2D_DESC1 desc1;
    UpgradeResourceDescription(pDesc, desc1);
    return CreateTexture2D1(&desc1, pInitialData,
                            (ID3D11Texture2D1 **)ppTexture2D);
  }

  HRESULT STDMETHODCALLTYPE
  CreateTexture3D(const D3D11_TEXTURE3D_DESC *pDesc,
                  const D3D11_SUBRESOURCE_DATA *pInitialData,
                  ID3D11Texture3D **ppTexture3D) override {
    D3D11_TEXTURE3D_DESC1 desc1;
    UpgradeResourceDescription(pDesc, desc1);
    return CreateTexture3D1(&desc1, pInitialData,
                            (ID3D11Texture3D1 **)ppTexture3D);
  }

  HRESULT STDMETHODCALLTYPE CreateShaderResourceView(
      ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
      ID3D11ShaderResourceView **ppSRView) override {
    if (pDesc) {
      D3D11_SHADER_RESOURCE_VIEW_DESC1 desc1;
      UpgradeViewDescription(pDesc, desc1);
      return CreateShaderResourceView1(pResource, &desc1,
                                       (ID3D11ShaderResourceView1 **)ppSRView);
    }
    return CreateShaderResourceView1(pResource, nullptr,
                                     (ID3D11ShaderResourceView1 **)ppSRView);
  }

  HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(
      ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *pDesc,
      ID3D11UnorderedAccessView **ppUAView) override {
    if (pDesc) {
      D3D11_UNORDERED_ACCESS_VIEW_DESC1 desc1;
      UpgradeViewDescription(pDesc, desc1);
      return CreateUnorderedAccessView1(
          pResource, &desc1, (ID3D11UnorderedAccessView1 **)ppUAView);
    }
    return CreateUnorderedAccessView1(pResource, nullptr,
                                      (ID3D11UnorderedAccessView1 **)ppUAView);
  }

  HRESULT STDMETHODCALLTYPE CreateRenderTargetView(
      ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
      ID3D11RenderTargetView **ppRTView) override {
    if (pDesc) {
      D3D11_RENDER_TARGET_VIEW_DESC1 desc1;
      UpgradeViewDescription(pDesc, desc1);
      return CreateRenderTargetView1(pResource, &desc1,
                                     (ID3D11RenderTargetView1 **)ppRTView);
    }
    return CreateRenderTargetView1(pResource, nullptr,
                                   (ID3D11RenderTargetView1 **)ppRTView);
  }

  HRESULT STDMETHODCALLTYPE CreateDepthStencilView(
      ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
      ID3D11DepthStencilView **ppDepthStencilView) override {
    InitReturnPtr(ppDepthStencilView);

    if (!pResource)
      return E_INVALIDARG;

    if (!ppDepthStencilView)
      return S_FALSE;

    return static_cast<D3D11ResourceCommon *>(pResource)->CreateDepthStencilView(pDesc, ppDepthStencilView);
  }

  HRESULT STDMETHODCALLTYPE CreateInputLayout(
      const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements,
      const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength,
      ID3D11InputLayout **ppInputLayout) override {
    InitReturnPtr(ppInputLayout);

    if (!pInputElementDescs)
      return E_INVALIDARG;
    if (!pShaderBytecodeWithInputSignature)
      return E_INVALIDARG;

    // TODO: must get shader reflection info

    if (!ppInputLayout) {
      return S_FALSE;
    }

    return pipeline_cache_->AddInputLayout(
        pShaderBytecodeWithInputSignature, pInputElementDescs, NumElements,
        (IMTLD3D11InputLayout **)ppInputLayout);
  }

  HRESULT STDMETHODCALLTYPE
  CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength,
                     ID3D11ClassLinkage *pClassLinkage,
                     ID3D11VertexShader **ppVertexShader) override {
    InitReturnPtr(ppVertexShader);
    if (pClassLinkage != nullptr)
      WARN("Class linkage not supported");

    return pipeline_cache_->AddVertexShader(pShaderBytecode, BytecodeLength,
                                            ppVertexShader);
  }

  HRESULT STDMETHODCALLTYPE
  CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength,
                       ID3D11ClassLinkage *pClassLinkage,
                       ID3D11GeometryShader **ppGeometryShader) override {
    InitReturnPtr(ppGeometryShader);
    if (pClassLinkage != nullptr)
      WARN("Class linkage not supported");

    if (!ppGeometryShader)
      return S_FALSE;

    return pipeline_cache_->AddGeometryShader(pShaderBytecode, BytecodeLength,
                                              ppGeometryShader);
  }

  HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(
      const void *pShaderBytecode, SIZE_T BytecodeLength,
      const D3D11_SO_DECLARATION_ENTRY *pSODeclaration, UINT NumEntries,
      const UINT *pBufferStrides, UINT NumStrides, UINT RasterizedStream,
      ID3D11ClassLinkage *pClassLinkage,
      ID3D11GeometryShader **ppGeometryShader) override {
    InitReturnPtr(ppGeometryShader);
    if (pClassLinkage != nullptr)
      WARN("Class linkage not supported");

    if (NumEntries > 0 && RasterizedStream == D3D11_SO_NO_RASTERIZED_STREAM &&
        ((char *)pShaderBytecode)[0] == 'D' &&
        ((char *)pShaderBytecode)[1] == 'X' &&
        ((char *)pShaderBytecode)[2] == 'B' &&
        ((char *)pShaderBytecode)[3] == 'C') {
      // FIXME: ensure the input shader is a vertex shader
      WARN("Emulate stream output");

      Com<IMTLD3D11StreamOutputLayout> so_layout;
      HRESULT hr = pipeline_cache_->AddStreamOutputLayout(
          pShaderBytecode, NumEntries, pSODeclaration, NumStrides,
          pBufferStrides, &so_layout);
      if (FAILED(hr))
        return hr;
      return so_layout->QueryInterface(IID_PPV_ARGS(ppGeometryShader));
    }
    ERR("CreateGeometryShaderWithStreamOutput: not supported, expect problem");
    return pipeline_cache_->AddGeometryShader(pShaderBytecode, BytecodeLength,
                                              ppGeometryShader);
  }

  HRESULT STDMETHODCALLTYPE
  CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength,
                    ID3D11ClassLinkage *pClassLinkage,
                    ID3D11PixelShader **ppPixelShader) override {
    InitReturnPtr(ppPixelShader);
    if (pClassLinkage != nullptr)
      WARN("Class linkage not supported");

    return pipeline_cache_->AddPixelShader(pShaderBytecode, BytecodeLength,
                                           ppPixelShader);
  }

  HRESULT STDMETHODCALLTYPE
  CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength,
                   ID3D11ClassLinkage *pClassLinkage,
                   ID3D11HullShader **ppHullShader) override {
    InitReturnPtr(ppHullShader);
    if (pClassLinkage != nullptr)
      WARN("Class linkage not supported");

    if (!ppHullShader)
      return S_FALSE;

    return pipeline_cache_->AddHullShader(pShaderBytecode, BytecodeLength,
                                          ppHullShader);
  }

  HRESULT STDMETHODCALLTYPE
  CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength,
                     ID3D11ClassLinkage *pClassLinkage,
                     ID3D11DomainShader **ppDomainShader) override {
    InitReturnPtr(ppDomainShader);
    if (pClassLinkage != nullptr)
      WARN("Class linkage not supported");

    if (!ppDomainShader)
      return S_FALSE;

    return pipeline_cache_->AddDomainShader(pShaderBytecode, BytecodeLength,
                                            ppDomainShader);
  }

  HRESULT STDMETHODCALLTYPE
  CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength,
                      ID3D11ClassLinkage *pClassLinkage,
                      ID3D11ComputeShader **ppComputeShader) override {
    InitReturnPtr(ppComputeShader);
    if (pClassLinkage != nullptr)
      WARN("Class linkage not supported");

    return pipeline_cache_->AddComputeShader(pShaderBytecode, BytecodeLength,
                                             ppComputeShader);
  }

  HRESULT STDMETHODCALLTYPE
  CreateClassLinkage(ID3D11ClassLinkage **ppLinkage) override {
    *ppLinkage = ref(new MTLD3D11ClassLinkage(this));
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  CreateBlendState(const D3D11_BLEND_DESC *pBlendStateDesc,
                   ID3D11BlendState **ppBlendState) override {
    ID3D11BlendState1 *pBlendState1;
    D3D11_BLEND_DESC1 desc;
    desc.AlphaToCoverageEnable = pBlendStateDesc->AlphaToCoverageEnable;
    desc.IndependentBlendEnable = pBlendStateDesc->IndependentBlendEnable;

    for (uint32_t i = 0; i < 8; i++) {
      desc.RenderTarget[i].BlendEnable =
          pBlendStateDesc->RenderTarget[i].BlendEnable;
      desc.RenderTarget[i].LogicOpEnable = FALSE;
      desc.RenderTarget[i].LogicOp = D3D11_LOGIC_OP_NOOP;
      desc.RenderTarget[i].SrcBlend = pBlendStateDesc->RenderTarget[i].SrcBlend;
      desc.RenderTarget[i].DestBlend =
          pBlendStateDesc->RenderTarget[i].DestBlend;
      desc.RenderTarget[i].BlendOp = pBlendStateDesc->RenderTarget[i].BlendOp;
      desc.RenderTarget[i].SrcBlendAlpha =
          pBlendStateDesc->RenderTarget[i].SrcBlendAlpha;
      desc.RenderTarget[i].DestBlendAlpha =
          pBlendStateDesc->RenderTarget[i].DestBlendAlpha;
      desc.RenderTarget[i].BlendOpAlpha =
          pBlendStateDesc->RenderTarget[i].BlendOpAlpha;
      desc.RenderTarget[i].RenderTargetWriteMask =
          pBlendStateDesc->RenderTarget[i].RenderTargetWriteMask;
    }
    auto hr = CreateBlendState1(&desc, &pBlendState1);
    *ppBlendState = pBlendState1;
    return hr;
  }

  HRESULT STDMETHODCALLTYPE CreateDepthStencilState(
      const D3D11_DEPTH_STENCIL_DESC *pDesc,
      ID3D11DepthStencilState **ppDepthStencilState) override {
    return depthstencil_states.CreateStateObject(
        pDesc, (IMTLD3D11DepthStencilState **)ppDepthStencilState);
  }

  HRESULT STDMETHODCALLTYPE
  CreateRasterizerState(const D3D11_RASTERIZER_DESC *pRasterizerDesc,
                        ID3D11RasterizerState **ppRasterizerState) override {
    ID3D11RasterizerState2 *pRasterizerState;
    D3D11_RASTERIZER_DESC2 desc;
    desc.FillMode = pRasterizerDesc->FillMode;
    desc.CullMode = pRasterizerDesc->CullMode;
    desc.FrontCounterClockwise = pRasterizerDesc->FrontCounterClockwise;
    desc.DepthBias = pRasterizerDesc->DepthBias;
    desc.DepthBiasClamp = pRasterizerDesc->DepthBiasClamp;
    desc.SlopeScaledDepthBias = pRasterizerDesc->SlopeScaledDepthBias;
    desc.DepthClipEnable = pRasterizerDesc->DepthClipEnable;
    desc.ScissorEnable = pRasterizerDesc->ScissorEnable;
    desc.MultisampleEnable = pRasterizerDesc->MultisampleEnable;
    desc.AntialiasedLineEnable = pRasterizerDesc->AntialiasedLineEnable;
    desc.ForcedSampleCount = 0;
    desc.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    auto hr = CreateRasterizerState2(&desc, &pRasterizerState);
    *ppRasterizerState = pRasterizerState;
    return hr;
  }

  HRESULT STDMETHODCALLTYPE
  CreateSamplerState(const D3D11_SAMPLER_DESC *pSamplerDesc,
                     ID3D11SamplerState **ppSamplerState) override {
    return sampler_states.CreateStateObject(
        pSamplerDesc, (IMTLD3D11SamplerState **)ppSamplerState);
  }

  HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC *pQueryDesc,
                                        ID3D11Query **ppQuery) override {
    InitReturnPtr(ppQuery);

    if (!pQueryDesc)
      return E_INVALIDARG;

    switch (pQueryDesc->Query) {
    case D3D11_QUERY_EVENT:
      *ppQuery = ref(new MTLD3D11EventQueryImpl<BOOL>(this, pQueryDesc));
      return S_OK;
    case D3D11_QUERY_OCCLUSION:
    case D3D11_QUERY_OCCLUSION_PREDICATE:
      return CreateOcculusionQuery(this, pQueryDesc, ppQuery);
    case D3D11_QUERY_TIMESTAMP:
      *ppQuery = ref(new MTLD3D11EventQueryImpl<UINT64>(this, pQueryDesc));
      return S_OK;
    case D3D11_QUERY_TIMESTAMP_DISJOINT: {
      *ppQuery = ref(new MTLD3D11EventQueryImpl<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT>(this, pQueryDesc));
      return S_OK;
    }
    case D3D11_QUERY_PIPELINE_STATISTICS: {
      *ppQuery =
          ref(new MTLD3D11DummyQuery<D3D11_QUERY_DATA_PIPELINE_STATISTICS>(
              this, pQueryDesc));
      return S_OK;
    }
#ifdef VBOX
    case D3D11_QUERY_SO_STATISTICS:
    case D3D11_QUERY_SO_STATISTICS_STREAM0:
    case D3D11_QUERY_SO_STATISTICS_STREAM1:
    case D3D11_QUERY_SO_STATISTICS_STREAM2:
    case D3D11_QUERY_SO_STATISTICS_STREAM3: {
      *ppQuery =
          ref(new MTLD3D11DummyQuery<D3D11_QUERY_DATA_SO_STATISTICS>(
              this, pQueryDesc));
      return S_OK;
    }
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3: {
      *ppQuery =
          ref(new MTLD3D11DummyQuery<BOOL>(
              this, pQueryDesc));
      return S_OK;
    }
#endif
    default:
      ERR("CreateQuery: query type not implemented: ", pQueryDesc->Query);
      return E_NOTIMPL;
    }
  }

  HRESULT STDMETHODCALLTYPE
  CreatePredicate(const D3D11_QUERY_DESC *pPredicateDesc,
                  ID3D11Predicate **ppPredicate) override {
    return CreateQuery(pPredicateDesc,
                       reinterpret_cast<ID3D11Query **>(ppPredicate));
  }

  HRESULT STDMETHODCALLTYPE
  CreateCounter(const D3D11_COUNTER_DESC *pCounterDesc,
                ID3D11Counter **ppCounter) override {
    InitReturnPtr(ppCounter);
    WARN("Not supported");
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE CreateDeferredContext(
      UINT ContextFlags,
      ID3D11DeviceContext **ppDeferredContext) override{
    ID3D11DeviceContext3 *ppDeferredContext3;
    HRESULT hr = CreateDeferredContext3(ContextFlags, &ppDeferredContext3);
    *ppDeferredContext = static_cast<ID3D11DeviceContext *>(ppDeferredContext3);
    return hr;
  }

  HRESULT STDMETHODCALLTYPE
      OpenSharedResource(HANDLE hResource, REFIID ReturnedInterface,
                         void **ppResource) override{IMPLEMENT_ME}

  HRESULT STDMETHODCALLTYPE
      CheckFormatSupport(DXGI_FORMAT Format, UINT *pFormatSupport) override {

    if (pFormatSupport) {
      *pFormatSupport = 0;
    }

    if (Format == DXGI_FORMAT_UNKNOWN) {
      *pFormatSupport =
          D3D11_FORMAT_SUPPORT_BUFFER | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE;
      return S_OK;
    }

    MTL_DXGI_FORMAT_DESC metal_format;
    if (FAILED(MTLQueryDXGIFormat(GetMTLDevice(), Format, metal_format))) {
      return E_INVALIDARG;
    }

    UINT outFormatSupport = 0;

    if (metal_format.PixelFormat) {
      // All graphics and compute kernels can read or sample a texture with any pixel format.
      outFormatSupport |= D3D11_FORMAT_SUPPORT_SHADER_LOAD | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE |
                          D3D11_FORMAT_SUPPORT_SHADER_GATHER | D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD |
                          D3D11_FORMAT_SUPPORT_CPU_LOCKABLE;

      /* UNCHECKED */
      outFormatSupport |= D3D11_FORMAT_SUPPORT_TEXTURE1D | D3D11_FORMAT_SUPPORT_TEXTURE2D |
                          D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_TEXTURECUBE | D3D11_FORMAT_SUPPORT_MIP |
                          D3D11_FORMAT_SUPPORT_MIP_AUTOGEN | // ?
                          D3D11_FORMAT_SUPPORT_CAST_WITHIN_BIT_LAYOUT;

      if (!(metal_format.Flag & (MTL_DXGI_FORMAT_BC | MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER))) {
        outFormatSupport |= D3D11_FORMAT_SUPPORT_BUFFER;
      }

      if (metal_format.Flag & MTL_DXGI_FORMAT_BACKBUFFER) {
        outFormatSupport |= D3D11_FORMAT_SUPPORT_DISPLAY;
      }
    }

    if (metal_format.AttributeFormat) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER;
    }

    if (metal_format.PixelFormat == WMTPixelFormatR32Uint ||
        metal_format.PixelFormat == WMTPixelFormatR16Uint) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_IA_INDEX_BUFFER;
    }

    auto Capability =
        format_inspector.textureCapabilities[metal_format.PixelFormat];

    if (any_bit_set(Capability & FormatCapability::Color)) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_RENDER_TARGET;
    }

    if (any_bit_set(Capability & FormatCapability::Blend)) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_BLENDABLE;
    }

    if (any_bit_set(Capability & FormatCapability::DepthStencil)) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_DEPTH_STENCIL |
                          D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON |
                          D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON;
    }

    if (any_bit_set(Capability & FormatCapability::Resolve)) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE;
    }

    if (any_bit_set(Capability & FormatCapability::MSAA)) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET;
    }

    if (any_bit_set(Capability & FormatCapability::Write)) {
      outFormatSupport |= D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW;
    }

    if (Format == DXGI_FORMAT_R32_FLOAT || Format == DXGI_FORMAT_R32_UINT ||
        Format == DXGI_FORMAT_R32_SINT || Format == DXGI_FORMAT_R32G32_FLOAT ||
        Format == DXGI_FORMAT_R32G32_UINT ||
        Format == DXGI_FORMAT_R32G32_SINT ||
        Format == DXGI_FORMAT_R32G32B32_FLOAT ||
        Format == DXGI_FORMAT_R32G32B32_UINT ||
        Format == DXGI_FORMAT_R32G32B32_SINT ||
        Format == DXGI_FORMAT_R32G32B32A32_FLOAT ||
        Format == DXGI_FORMAT_R32G32B32A32_UINT ||
        Format == DXGI_FORMAT_R32G32B32A32_SINT)
      outFormatSupport |= D3D11_FORMAT_SUPPORT_SO_BUFFER;

    if (pFormatSupport) {
      *pFormatSupport = outFormatSupport;
    }

    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE CheckMultisampleQualityLevels(
      DXGI_FORMAT Format, UINT SampleCount, UINT *pNumQualityLevels) override {
    return CheckMultisampleQualityLevels1(Format, SampleCount, 0,
                                          pNumQualityLevels);
  }

  void STDMETHODCALLTYPE CheckCounterInfo(D3D11_COUNTER_INFO *pCounterInfo)
      override { // We basically don't support counters
    pCounterInfo->LastDeviceDependentCounter = D3D11_COUNTER(0);
    pCounterInfo->NumSimultaneousCounters = 0;
    pCounterInfo->NumDetectableParallelUnits = 0;
  }

  HRESULT STDMETHODCALLTYPE CheckCounter(const D3D11_COUNTER_DESC *pDesc,
                                         D3D11_COUNTER_TYPE *pType,
                                         UINT *pActiveCounters, LPSTR szName,
                                         UINT *pNameLength, LPSTR szUnits,
                                         UINT *pUnitsLength,
                                         LPSTR szDescription,
                                         UINT *pDescriptionLength) override {
    WARN("Not supported");
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT STDMETHODCALLTYPE
  CheckFeatureSupport(D3D11_FEATURE Feature, void *pFeatureSupportData,
                      UINT FeatureSupportDataSize) override {
    switch (Feature) {
    // Format support queries are special in that they use in-out
    // structs
    case D3D11_FEATURE_FORMAT_SUPPORT: {
      auto info =
          static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT *>(pFeatureSupportData);

      if (FeatureSupportDataSize != sizeof(*info))
        return E_INVALIDARG;

      return CheckFormatSupport(info->InFormat, &info->OutFormatSupport);
    }
    case D3D11_FEATURE_FORMAT_SUPPORT2: {
      auto info = static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT2 *>(
          pFeatureSupportData);

      if (FeatureSupportDataSize != sizeof(*info))
        return E_INVALIDARG;
      info->OutFormatSupport2 = 0;

      if (info->InFormat == DXGI_FORMAT_UNKNOWN) {
        info->OutFormatSupport2 |=
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX |
            D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
            D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE |
            D3D11_FORMAT_SUPPORT2_SHAREABLE;
        return S_OK;
      }

      MTL_DXGI_FORMAT_DESC metal_format;
      if (FAILED(MTLQueryDXGIFormat(GetMTLDevice(), info->InFormat,
                                    metal_format))) {
        return E_INVALIDARG;
      }
      auto Capability =
          format_inspector.textureCapabilities[metal_format.PixelFormat];

      if (any_bit_set(Capability & FormatCapability::TextureBufferRead)) {
        info->OutFormatSupport2 |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
      }

      if (any_bit_set(Capability & FormatCapability::TextureBufferWrite)) {
        info->OutFormatSupport2 |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE;
      }

      if (any_bit_set(Capability & FormatCapability::TextureBufferReadWrite)) {
        info->OutFormatSupport2 |= D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD |
                                   D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE;
      }

      if (any_bit_set(Capability & FormatCapability::Atomic)) {
        info->OutFormatSupport2 |=
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
            D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX |
            D3D11_FORMAT_SUPPORT2_SHAREABLE;
      }

#ifndef DXMT_NO_PRIVATE_API
      if (any_bit_set(Capability & FormatCapability::Blend)) {
        /* UNCHECKED */
        info->OutFormatSupport2 |= D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
      }
#endif

      if (any_bit_set(Capability & FormatCapability::Sparse)) {
        info->OutFormatSupport2 |= D3D11_FORMAT_SUPPORT2_TILED;
      }

      return S_OK;
    }
    default:
      // For everything else, we can use the device feature struct
      // that we already initialized during device creation.
      return m_features.GetFeatureData(Feature, FeatureSupportDataSize,
                                       pFeatureSupportData);
    }
  }

  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT *pDataSize,
                                           void *pData) override {
    return m_container->GetPrivateData(guid, pDataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize,
                                           const void *pData) override {
    return m_container->SetPrivateData(guid, DataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE
  SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) override {
    return m_container->SetPrivateDataInterface(guid, pData);
  }

  D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel() override {
    return m_FeatureLevel;
  }

  UINT STDMETHODCALLTYPE GetCreationFlags() override { return m_FeatureFlags & 0x7fffffff; }

  HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override {
    // unless we are deal with eGPU, this method should awalys return S_OK?
    return S_OK;
  }

  void STDMETHODCALLTYPE
  GetImmediateContext(ID3D11DeviceContext **ppImmediateContext) override {
    context_->QueryInterface(IID_PPV_ARGS(ppImmediateContext));
  }

  HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT RaiseFlags) override {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  UINT STDMETHODCALLTYPE GetExceptionMode() override {
    ERR("Not implemented");
    return 0;
  }

  void STDMETHODCALLTYPE
  GetImmediateContext1(ID3D11DeviceContext1 **ppImmediateContext) override {
    context_->QueryInterface(IID_PPV_ARGS(ppImmediateContext));
  }

  HRESULT STDMETHODCALLTYPE CreateDeferredContext1(
      UINT ContextFlags,
      ID3D11DeviceContext1 **ppDeferredContext) override{
    ID3D11DeviceContext3 *ppDeferredContext3;
    HRESULT hr = CreateDeferredContext3(ContextFlags, &ppDeferredContext3);
    *ppDeferredContext = static_cast<ID3D11DeviceContext1 *>(ppDeferredContext3);
    return hr;
  }

  HRESULT STDMETHODCALLTYPE
      CreateBlendState1(const D3D11_BLEND_DESC1 *pBlendStateDesc,
                        ID3D11BlendState1 **ppBlendState) override {
    return pipeline_cache_->AddBlendState(pBlendStateDesc,
                                          (IMTLD3D11BlendState **)ppBlendState);
  }

  HRESULT STDMETHODCALLTYPE
  CreateRasterizerState1(const D3D11_RASTERIZER_DESC1 *pRasterizerDesc,
                         ID3D11RasterizerState1 **ppRasterizerState) override {
    ID3D11RasterizerState2 *pRasterizerState;
    D3D11_RASTERIZER_DESC2 desc;
    desc.FillMode = pRasterizerDesc->FillMode;
    desc.CullMode = pRasterizerDesc->CullMode;
    desc.FrontCounterClockwise = pRasterizerDesc->FrontCounterClockwise;
    desc.DepthBias = pRasterizerDesc->DepthBias;
    desc.DepthBiasClamp = pRasterizerDesc->DepthBiasClamp;
    desc.SlopeScaledDepthBias = pRasterizerDesc->SlopeScaledDepthBias;
    desc.DepthClipEnable = pRasterizerDesc->DepthClipEnable;
    desc.ScissorEnable = pRasterizerDesc->ScissorEnable;
    desc.MultisampleEnable = pRasterizerDesc->MultisampleEnable;
    desc.AntialiasedLineEnable = pRasterizerDesc->AntialiasedLineEnable;
    desc.ForcedSampleCount = pRasterizerDesc->ForcedSampleCount;
    desc.ConservativeRaster = D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    auto hr = CreateRasterizerState2(&desc, &pRasterizerState);
    *ppRasterizerState = pRasterizerState;
    return hr;
  }

  HRESULT STDMETHODCALLTYPE CreateDeviceContextState(
      UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
      UINT SDKVersion, REFIID EmulatedInterface,
      D3D_FEATURE_LEVEL *pChosenFeatureLevel,
      ID3DDeviceContextState **ppContextState) override {
    InitReturnPtr(ppContextState);

    // TODO: validation

    if (ppContextState == nullptr) {
      return S_FALSE;
    }
    *ppContextState = ref(new MTLD3D11DeviceContextState(this));
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OpenSharedResource1(HANDLE hResource,
                                                REFIID returnedInterface,
                                                void **ppResource) override{
      IMPLEMENT_ME}

  HRESULT STDMETHODCALLTYPE
      OpenSharedResourceByName(LPCWSTR lpName, DWORD dwDesiredAccess,
                               REFIID returnedInterface,
                               void **ppResource) override {
    IMPLEMENT_ME
  }

  void STDMETHODCALLTYPE
  GetImmediateContext2(ID3D11DeviceContext2 **ppImmediateContext) override {
    context_->QueryInterface(IID_PPV_ARGS(ppImmediateContext));
  }

  HRESULT STDMETHODCALLTYPE
  CreateDeferredContext2(UINT ContextFlags, ID3D11DeviceContext2 **ppDeferredContext) override {
    ID3D11DeviceContext3 *ppDeferredContext3;
    HRESULT hr = CreateDeferredContext3(ContextFlags, &ppDeferredContext3);
    *ppDeferredContext = static_cast<ID3D11DeviceContext2 *>(ppDeferredContext3);
    return hr;
  }

  void STDMETHODCALLTYPE GetResourceTiling(
      ID3D11Resource *resource, UINT *tile_count,
      D3D11_PACKED_MIP_DESC *mip_desc, D3D11_TILE_SHAPE *tile_shape,
      UINT *subresource_tiling_count, UINT first_subresource_tiling,
      D3D11_SUBRESOURCE_TILING *subresource_tiling) override{IMPLEMENT_ME}

  HRESULT STDMETHODCALLTYPE
      CheckMultisampleQualityLevels1(DXGI_FORMAT Format, UINT SampleCount,
                                     UINT Flags,
                                     UINT *pNumQualityLevels) override {
    if (Flags) {
      IMPLEMENT_ME;
    }
    *pNumQualityLevels = 0;
    MTL_DXGI_FORMAT_DESC desc;
    if (FAILED(MTLQueryDXGIFormat(GetMTLDevice(), Format, desc)) ||
        desc.PixelFormat == WMTPixelFormatInvalid) {
      return E_INVALIDARG;
    }

    // MSDN:
    // FEATURE_LEVEL_11_0 devices are required to support 4x MSAA for all render
    // target formats, and 8x MSAA for all render target formats except
    // R32G32B32A32 formats.

    // seems some pixel format doesn't support MSAA
    // https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf

    if (GetMTLDevice().supportsTextureSampleCount(SampleCount)) {
      *pNumQualityLevels = 1; // always 1: in metal there is no concept of
                              // Quality Level (so is it in vulkan iirc)
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  CreateTexture2D1(const D3D11_TEXTURE2D_DESC1 *pDesc,
                   const D3D11_SUBRESOURCE_DATA *pInitialData,
                   ID3D11Texture2D1 **ppTexture2D) override {
    InitReturnPtr(ppTexture2D);

    if (!pDesc)
      return E_INVALIDARG;

    if ((pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILED))
      return E_INVALIDARG; // not supported yet

    try {
      switch (pDesc->Usage) {
      case D3D11_USAGE_DEFAULT:
      case D3D11_USAGE_IMMUTABLE:
        return CreateDeviceTexture2D(this, pDesc, pInitialData, ppTexture2D);
      case D3D11_USAGE_DYNAMIC:
        return CreateDynamicTexture2D(this, pDesc, pInitialData, ppTexture2D);
      case D3D11_USAGE_STAGING:
        if (pDesc->BindFlags != 0) {
          return E_INVALIDARG;
        }
        return CreateStagingTexture2D(this, pDesc, pInitialData, ppTexture2D);
      }
      return S_OK;
    } catch (const MTLD3DError &err) {
      ERR(err.message());
      return E_FAIL;
    }
  }

  HRESULT STDMETHODCALLTYPE
  CreateTexture3D1(const D3D11_TEXTURE3D_DESC1 *pDesc,
                   const D3D11_SUBRESOURCE_DATA *pInitialData,
                   ID3D11Texture3D1 **ppTexture3D) override {
    InitReturnPtr(ppTexture3D);

    if (!pDesc)
      return E_INVALIDARG;

    if ((pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILED))
      return E_INVALIDARG; // not supported yet

    try {
      switch (pDesc->Usage) {
      case D3D11_USAGE_DEFAULT:
      case D3D11_USAGE_IMMUTABLE:
        return CreateDeviceTexture3D(this, pDesc, pInitialData, ppTexture3D);
      case D3D11_USAGE_DYNAMIC:
        ERR("dynamic texture 3d not supported yet");
        return E_NOTIMPL;
      case D3D11_USAGE_STAGING:
        if (pDesc->BindFlags != 0) {
          return E_INVALIDARG;
        }
        return CreateStagingTexture3D(this, pDesc, pInitialData, ppTexture3D);
      }
      return S_OK;
    } catch (const MTLD3DError &err) {
      ERR(err.message());
      return E_FAIL;
    }
  }

  HRESULT STDMETHODCALLTYPE
  CreateRasterizerState2(const D3D11_RASTERIZER_DESC2 *pRasterizerDesc,
                         ID3D11RasterizerState2 **ppRasterizerState) override {
    return rasterizer_states.CreateStateObject(
        pRasterizerDesc, (IMTLD3D11RasterizerState **)ppRasterizerState);
  }

  HRESULT STDMETHODCALLTYPE CreateShaderResourceView1(
      ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc,
      ID3D11ShaderResourceView1 **ppSRView) override {
    InitReturnPtr(ppSRView);

    if (!pResource)
      return E_INVALIDARG;

    if (!ppSRView)
      return S_FALSE;

    return static_cast<D3D11ResourceCommon *>(pResource)->CreateShaderResourceView(pDesc, ppSRView);
  }

  HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView1(
      ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc,
      ID3D11UnorderedAccessView1 **ppUAView) override {
    InitReturnPtr(ppUAView);

    if (!pResource)
      return E_INVALIDARG;

    if (!ppUAView)
      return S_FALSE;

    return static_cast<D3D11ResourceCommon *>(pResource)->CreateUnorderedAccessView(pDesc, ppUAView);
  }

  HRESULT STDMETHODCALLTYPE CreateRenderTargetView1(
      ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc,
      ID3D11RenderTargetView1 **ppRTView) override {
    InitReturnPtr(ppRTView);

    if (!pResource)
      return E_INVALIDARG;

    if (!ppRTView)
      return S_FALSE;

    return static_cast<D3D11ResourceCommon *>(pResource)->CreateRenderTargetView(pDesc, ppRTView);
  }

  HRESULT STDMETHODCALLTYPE CreateQuery1(const D3D11_QUERY_DESC1 *desc,
                                         ID3D11Query1 **query) override {
    IMPLEMENT_ME
  }

  void STDMETHODCALLTYPE
  GetImmediateContext3(ID3D11DeviceContext3 **ppImmediateContext) override{
    context_->QueryInterface(IID_PPV_ARGS(ppImmediateContext));
  }

  HRESULT STDMETHODCALLTYPE CreateDeferredContext3(
    UINT ContextFlags, ID3D11DeviceContext3 **ppDeferredContext) override {
    *ppDeferredContext = std::move(dxmt::CreateDeferredContext(this, ContextFlags));
    return S_OK;
  }

  void STDMETHODCALLTYPE WriteToSubresource(ID3D11Resource *dst_resource,
                                            UINT dst_subresource,
                                            const D3D11_BOX *dst_box,
                                            const void *src_data,
                                            UINT src_row_pitch,
                                            UINT src_depth_pitch) override {
    IMPLEMENT_ME
  }

  void STDMETHODCALLTYPE
  ReadFromSubresource(void *dst_data, UINT dst_row_pitch, UINT dst_depth_pitch,
                      ID3D11Resource *src_resource, UINT src_subresource,
                      const D3D11_BOX *src_box) override{IMPLEMENT_ME}

  WMT::Device STDMETHODCALLTYPE GetMTLDevice() override {
    return m_container->GetMTLDevice();
  }

  void SubmitThreadgroupWork(IMTLThreadpoolWork *pWork) override {
    scheduler_.submit(pWork);
  }

  HRESULT
  CreateGraphicsPipeline(MTL_GRAPHICS_PIPELINE_DESC *pDesc,
                         IMTLCompiledGraphicsPipeline **ppPipeline) override {
    pipeline_cache_->GetGraphicsPipeline(pDesc, ppPipeline);
    return S_OK;
  };

  HRESULT
  CreateComputePipeline(MTL_COMPUTE_PIPELINE_DESC *pDesc,
                        IMTLCompiledComputePipeline **ppPipeline) override {
    std::lock_guard<dxmt::mutex> lock(mutex_cs_);

    auto iter = pipelines_cs_.find(pDesc->ComputeShader);
    if (iter != pipelines_cs_.end()) {
      *ppPipeline = iter->second.ref();
      return S_OK;
    }
    auto temp = dxmt::CreateComputePipeline(this, pDesc->ComputeShader);
    if (!pipelines_cs_.insert({pDesc->ComputeShader, temp}).second) // copy
    {
      D3D11_ASSERT(0 && "duplicated compute pipeline");
    }
    *ppPipeline = std::move(temp); // move
    return S_OK;
  };

  HRESULT
  CreateTessellationPipeline(
      MTL_GRAPHICS_PIPELINE_DESC *pDesc,
      IMTLCompiledTessellationPipeline **ppPipeline) override {
    pipeline_cache_->GetTessellationPipeline(pDesc, ppPipeline);
    return S_OK;
  };

  virtual HRESULT
  CreateGeometryPipeline(MTL_GRAPHICS_PIPELINE_DESC *pDesc,
                         IMTLCompiledGeometryPipeline **ppPipeline) override {
    pipeline_cache_->GetGeometryPipeline(pDesc, ppPipeline);
    return S_OK;
  };

  Device &GetDXMTDevice() override { return device_; };

  void CreateCommandList(ID3D11CommandList** pCommandList) final {
    commandlist_pool_->CreateCommandList(pCommandList);
  };

  virtual void STDMETHODCALLTYPE SetShaderExtensionSlot(UINT Slot) final {
    // TODO
  };

  virtual FormatCapability
  GetMTLPixelFormatCapability(WMTPixelFormat Format) final {
    if (!format_inspector.textureCapabilities.contains(Format))
      return FormatCapability(0);
    return format_inspector.textureCapabilities.at(Format);
  };

  virtual IMTLD3D11DeviceContext *GetImmediateContextPrivate() final {
    return context_.get();
  };

  virtual unsigned int GetDirectXVersion() final {
    return m_FeatureFlags & 0x80000000 ? 10 : 11;
  };

  virtual HRESULT STDMETHODCALLTYPE RegisterDeviceRemovedEvent(HANDLE Event,
                                                               DWORD *pCookie) final {
    // no device to remove
    return S_OK;
  };

  virtual void STDMETHODCALLTYPE UnregisterDeviceRemoved(DWORD Cookie) final {
    // just do nothing
  };

  virtual HRESULT STDMETHODCALLTYPE OpenSharedFence(HANDLE Handle, REFIID riid,
                                                    void **ppFence) final {
    return E_NOTIMPL;
  };

  virtual HRESULT STDMETHODCALLTYPE CreateFence(UINT64 InitialValue,
                                                D3D11_FENCE_FLAG Flags,
                                                REFIID riid, void **ppFence) final {
    return dxmt::CreateFence(this, InitialValue, Flags, riid, ppFence);
  };

private:
  MTLDXGIObject<IMTLDXGIDevice> *m_container;
  IMTLDXGIAdapter *adapter_;
  D3D_FEATURE_LEVEL m_FeatureLevel;
  UINT m_FeatureFlags;
  MTLD3D11Inspection m_features;
  FormatCapabilityInspector format_inspector;

  task_scheduler<IMTLThreadpoolWork*> scheduler_;

  bool is_traced_;

  std::unordered_map<ManagedShader, Com<IMTLCompiledComputePipeline>>
      pipelines_cs_;
  dxmt::mutex mutex_cs_;

  StateObjectCache<D3D11_SAMPLER_DESC, IMTLD3D11SamplerState> sampler_states;
  StateObjectCache<D3D11_RASTERIZER_DESC2, IMTLD3D11RasterizerState>
      rasterizer_states;
  StateObjectCache<D3D11_DEPTH_STENCIL_DESC, IMTLD3D11DepthStencilState>
      depthstencil_states;

  std::unique_ptr<MTLD3D11CommandListPoolBase> commandlist_pool_;
  std::unique_ptr<MTLD3D11PipelineCacheBase> pipeline_cache_;
  Device& device_;
  /** ensure destructor called first */
  std::unique_ptr<MTLD3D11DeviceContextBase> context_;
  std::unique_ptr<MTLD3D10Device> d3d10_;
  D3D11Multithread d3dmt_;
};

/**
 * \brief D3D11 device container
 *
 * Stores all the objects that contribute to the D3D11
 * device implementation, including the DXGI device.
 */
class MTLD3D11DXGIDevice final : public MTLDXGIObject<IMTLDXGIDevice> {
public:
  friend class MTLDXGIMetalLayerFactory;

  MTLD3D11DXGIDevice(std::unique_ptr<Device> &&device, IMTLDXGIAdapter *adapter,
                     D3D_FEATURE_LEVEL feature_level, UINT feature_flags)
      : adapter_(adapter), device(std::move(device)),
        cmd_queue_(this->device->queue()),
        d3d11_device_(this, adapter, feature_level, feature_flags,
                      *this->device.get()) {
  }

  ~MTLD3D11DXGIDevice() override {}

  bool FinalRelase() override {
    // FIXME: doesn't reliably work
    auto t = std::thread([this]() {
      delete this;
      ExitThread(0); 
    });
    t.detach();
    return true;
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) override {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDevice) || riid == __uuidof(IDXGIDevice1) ||
        riid == __uuidof(IDXGIDevice2) || riid == __uuidof(IDXGIDevice3) ||
        riid == __uuidof(IMTLDXGIDevice)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D11Device) || riid == __uuidof(ID3D11Device1) ||
        riid == __uuidof(ID3D11Device2) || riid == __uuidof(ID3D11Device3) ||
        riid == __uuidof(ID3D11Device4) || riid == __uuidof(ID3D11Device5)) {
      *ppvObject = ref_and_cast<ID3D11Device>(&d3d11_device_);
      return S_OK;
    }

    if (riid == __uuidof(ID3D10Device) || riid == __uuidof(ID3D10Device1)) {
      *ppvObject = ref_and_cast<ID3D10Device>(d3d11_device_.d3d10_.get());
      return S_OK;
    }

    if (riid == __uuidof(IMTLD3D11DeviceExt)) {
      *ppvObject = ref_and_cast<IMTLD3D11DeviceExt>(&d3d11_device_);
      return S_OK;
    }

    if (riid == __uuidof(ID3D11Multithread)
        && !(d3d11_device_.GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)) {
      *ppvObject = ref_and_cast<ID3D11Multithread>(&d3d11_device_.d3dmt_);
      return S_OK;
    }

    if (riid == __uuidof(ID3D11Debug))
      return E_NOINTERFACE;

    if (riid == kRenderdocUUID || riid == kPixUUID || riid == kGpaUUID)
      return E_NOINTERFACE;

    if (logQueryInterfaceError(__uuidof(IMTLDXGIDevice), riid)) {
      WARN("D3D11Device: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) override {
    return adapter_->QueryInterface(riid, ppParent);
  }

  HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter **pAdapter) override {
    if (pAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    *pAdapter = adapter_.ref();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
  CreateSurface(const DXGI_SURFACE_DESC *desc, UINT surface_count,
                DXGI_USAGE usage, const DXGI_SHARED_RESOURCE *shared_resource,
                IDXGISurface **surface) override{IMPLEMENT_ME}

  HRESULT STDMETHODCALLTYPE
      QueryResourceResidency(IUnknown *const *resources,
                             DXGI_RESIDENCY *residency,
                             UINT resource_count) override{IMPLEMENT_ME}

  HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT Priority) override {
    if (Priority < -7 || Priority > 7)
      return E_INVALIDARG;

    WARN("SetGPUThreadPriority: Ignoring");
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT *pPriority) override {
    *pPriority = 0;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override {
    cmd_queue_.SetMaxLatency(MaxLatency);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *pMaxLatency) override {
    if (pMaxLatency) {
      *pMaxLatency = cmd_queue_.GetMaxLatency();
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE
      OfferResources(UINT NumResources, IDXGIResource *const *ppResources,
                     DXGI_OFFER_RESOURCE_PRIORITY Priority) override {
    // stub
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE ReclaimResources(UINT NumResources,
                                             IDXGIResource *const *ppResources,
                                             WINBOOL *pDiscarded) override {
    // stub
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE EnqueueSetEvent(HANDLE hEvent) override {
    return E_FAIL;
  }

  void STDMETHODCALLTYPE Trim() override { WARN("DXGIDevice3::Trim: no-op"); };

  WMT::Device STDMETHODCALLTYPE GetMTLDevice() override {
    return adapter_->GetMTLDevice();
  }

  HRESULT STDMETHODCALLTYPE CreateSwapChain(
      IDXGIFactory1 *pFactory, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
      IDXGISwapChain1 **ppSwapChain) override {

    return dxmt::CreateSwapChain(pFactory, &d3d11_device_, hWnd, pDesc, pFullscreenDesc,
                                 ppSwapChain);
  }

private:
  Com<IMTLDXGIAdapter> adapter_;
  std::unique_ptr<Device> device;
  CommandQueue &cmd_queue_;
  MTLD3D11DeviceImpl d3d11_device_;
};

Com<IMTLDXGIDevice> CreateD3D11Device(std::unique_ptr<Device> &&device,
                                      IMTLDXGIAdapter *adapter,
                                      D3D_FEATURE_LEVEL feature_level,
                                      UINT feature_flags) {
  return Com<IMTLDXGIDevice>::transfer(new MTLD3D11DXGIDevice(
      std::move(device), adapter, feature_level, feature_flags));
};
} // namespace dxmt