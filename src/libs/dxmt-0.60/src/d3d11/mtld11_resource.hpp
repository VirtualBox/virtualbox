#pragma once
#include "dxmt_buffer.hpp"
#include "dxmt_dynamic.hpp"
#include "dxmt_staging.hpp"
#include "dxmt_texture.hpp"
#include "d3d11_device_child.hpp"
#include "com/com_pointer.hpp"
#include "com/com_guid.hpp"
#include "d3d11_view.hpp"
#include "dxgi_resource.hpp"
#include "dxmt_resource_binding.hpp"
#include "log/log.hpp"
#include "../d3d10/d3d10_buffer.hpp"
#include "../d3d10/d3d10_texture.hpp"
#include "../d3d10/d3d10_view.hpp"
#include <memory>
#include <type_traits>

DEFINE_COM_INTERFACE("9a6f6549-d4b1-45ea-8794-8503d190d3d1",
                     IMTLMinLODClampable)
    : public IUnknown {
  virtual void SetMinLOD(float MinLOD) = 0;
  virtual float GetMinLOD() = 0;
};

namespace dxmt {

template <typename RESOURCE_DESC>
void UpgradeResourceDescription(const RESOURCE_DESC *pSrc, RESOURCE_DESC &dst) {
  dst = *pSrc;
}
template <typename RESOURCE_DESC>
void DowngradeResourceDescription(const RESOURCE_DESC &src,
                                  RESOURCE_DESC *pDst) {
  *pDst = src;
}

template <typename RESOURCE_DESC_SRC, typename RESOURCE_DESC_DST>
void UpgradeResourceDescription(const RESOURCE_DESC_SRC *pSrc,
                                RESOURCE_DESC_DST &dst);
template <typename RESOURCE_DESC_SRC, typename RESOURCE_DESC_DST>
void DowngradeResourceDescription(const RESOURCE_DESC_SRC &src,
                                  RESOURCE_DESC_DST *pDst);

template <typename VIEW_DESC>
void UpgradeViewDescription(const VIEW_DESC *pSrc, VIEW_DESC &dst) {
  dst = *pSrc;
}
template <typename VIEW_DESC>
void DowngradeViewDescription(const VIEW_DESC &src, VIEW_DESC *pDst) {
  *pDst = src;
}

template <typename VIEW_DESC_SRC, typename VIEW_DESC_DST>
void UpgradeViewDescription(const VIEW_DESC_SRC *pSrc, VIEW_DESC_DST &dst);
template <typename VIEW_DESC_SRC, typename VIEW_DESC_DST>
void DowngradeViewDescription(const VIEW_DESC_SRC &src, VIEW_DESC_DST *pDst);

struct tag_buffer {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_BUFFER;
  using COM = ID3D11Buffer;
  using COM_IMPL = ID3D11Buffer;
  using DESC = D3D11_BUFFER_DESC;
  using DESC1 = D3D11_BUFFER_DESC;
  using D3D10 = ID3D10Buffer;
  using D3D10_IMPL = MTLD3D10Buffer;
  static constexpr std::string_view debug_name = "buffer";
};

struct tag_texture_1d {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_TEXTURE1D;
  using COM = ID3D11Texture1D;
  using COM_IMPL = ID3D11Texture1D;
  using DESC = D3D11_TEXTURE1D_DESC;
  using DESC1 = D3D11_TEXTURE1D_DESC;
  using D3D10 = ID3D10Texture1D;
  using D3D10_IMPL = MTLD3D10Texture1D;
  static constexpr std::string_view debug_name = "tex1d";
};

struct tag_texture_2d {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_TEXTURE2D;
  using COM = ID3D11Texture2D;
  using COM_IMPL = ID3D11Texture2D1;
  using DESC = D3D11_TEXTURE2D_DESC;
  using DESC1 = D3D11_TEXTURE2D_DESC1;
  using D3D10 = ID3D10Texture2D;
  using D3D10_IMPL = MTLD3D10Texture2D;
  static constexpr std::string_view debug_name = "tex2d";
};

struct tag_texture_3d {
  static const D3D11_RESOURCE_DIMENSION dimension =
      D3D11_RESOURCE_DIMENSION_TEXTURE3D;
  using COM = ID3D11Texture3D;
  using COM_IMPL = ID3D11Texture3D1;
  using DESC = D3D11_TEXTURE3D_DESC;
  using DESC1 = D3D11_TEXTURE3D_DESC1;
  using D3D10 = ID3D10Texture3D;
  using D3D10_IMPL = MTLD3D10Texture3D;
  static constexpr std::string_view debug_name = "tex3d";
};

struct D3D11ResourceCommon : ID3D11Resource {
    /* ID3D11Buffer::GetDesc */
    /* ID3D11Texture1D::GetDesc */
    /* ID3D11Texture2D::GetDesc */
    /* ID3D11Texture3D::GetDesc */
  virtual void STDMETHODCALLTYPE GetDesc(void *pDesc) = 0;
    /* ID3D11Texture2D1::GetDesc */
    /* ID3D11Texture3D1::GetDesc */
  virtual void STDMETHODCALLTYPE GetDesc1(void *pDesc) = 0;

  virtual void AddRefPrivate() = 0;
  virtual void ReleasePrivate() = 0;
  virtual HRESULT STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc, ID3D11ShaderResourceView1 **ppView) = 0;
  virtual HRESULT STDMETHODCALLTYPE
  CreateUnorderedAccessView(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc, ID3D11UnorderedAccessView1 **ppView) = 0;
  virtual HRESULT STDMETHODCALLTYPE
  CreateRenderTargetView(const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc, ID3D11RenderTargetView1 **ppView) = 0;
  virtual HRESULT STDMETHODCALLTYPE
  CreateDepthStencilView(const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc, ID3D11DepthStencilView **ppView) = 0;

  virtual Rc<Buffer> buffer() = 0;
  virtual BufferSlice bufferSlice() = 0;
  virtual Rc<Texture> texture() = 0;
  virtual Rc<StagingResource> staging(UINT Subresource) = 0;
  virtual Rc<DynamicBuffer> dynamicBuffer(UINT *pBufferLength, UINT *pBindFlags) = 0;
  virtual Rc<DynamicTexture> dynamicTexture(UINT *pBytesPerRow, UINT *pBytesPerImage) = 0;
};

inline Rc<DynamicBuffer>
GetDynamicBuffer(ID3D11Resource *pResource, UINT *pBufferLength, UINT *pBindFlags) {
  return static_cast<D3D11ResourceCommon *>(pResource)->dynamicBuffer(pBufferLength, pBindFlags);
}
inline Rc<DynamicTexture>
GetDynamicTexture(ID3D11Resource *pResource, UINT *pBytesPerRow, UINT *pBytesPerImage) {
  return static_cast<D3D11ResourceCommon *>(pResource)->dynamicTexture(pBytesPerRow, pBytesPerImage);
}
inline Rc<StagingResource>
GetStagingResource(ID3D11Resource *pResource, UINT Subresource) {

  return static_cast<D3D11ResourceCommon *>(pResource)->staging(Subresource);
};
inline Rc<Texture>
GetTexture(ID3D11Resource *pResource) {
  return static_cast<D3D11ResourceCommon *>(pResource)->texture();
}

template <typename tag, typename... Base>
class TResourceBase : public MTLD3D11DeviceChild<D3D11ResourceCommon, Base...> {
public:
#ifndef VBOX
  TResourceBase(const tag::DESC1 &desc, MTLD3D11Device *device)
#else
  TResourceBase(const typename tag::DESC1 &desc, MTLD3D11Device *device)
#endif
      : MTLD3D11DeviceChild<D3D11ResourceCommon, Base...>(
            device),
        desc(desc),
        dxgi_resource(new MTLDXGIResource<TResourceBase<tag, Base...>>(this)),
#ifndef VBOX
        d3d10(reinterpret_cast<tag::COM *>(this), device->GetImmediateContextPrivate()) {}
#else
        d3d10(reinterpret_cast<typename tag::COM *>(this), device->GetImmediateContextPrivate()) {}
#endif

  template <std::size_t n> HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    return E_NOINTERFACE;
  };

  template <std::size_t n, typename V, typename... Args>
  HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    if (riid == __uuidof(V)) {
      *ppvObject = ref_and_cast<V>(this);
      return S_OK;
    }
    return ResolveBase<n + 1, Args...>(riid, ppvObject);
  };

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    auto hr = ResolveBase<0, Base...>(riid, ppvObject);
    if (SUCCEEDED(hr)) {
      return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11Resource) ||
        riid == __uuidof(typename tag::COM) ||
        riid == __uuidof(typename tag::COM_IMPL)) {
      *ppvObject = ref_and_cast<D3D11ResourceCommon>(this);
      return S_OK;
    }

    if (riid == __uuidof(ID3D10DeviceChild) ||
        riid == __uuidof(ID3D10Resource) ||
        riid == __uuidof(typename tag::D3D10)) {
      *ppvObject = ref_and_cast<typename tag::D3D10>(&d3d10);
      return S_OK;
    }

    if (riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGIResource) || riid == __uuidof(IDXGIResource1)) {
      *ppvObject = ref(dxgi_resource.get());
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(typename tag::COM), riid)) {
      WARN("D3D11Resource(", tag::debug_name ,"): Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  void STDMETHODCALLTYPE GetDesc(void *pDesc) final {
    ::dxmt::DowngradeResourceDescription(desc, (typename tag::DESC *)pDesc);
  }

  void STDMETHODCALLTYPE GetDesc1(void *pDesc) /* override / final */ { *(typename tag::DESC1 *)pDesc = desc; }

  void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION *pResourceDimension) final {
    *pResourceDimension = tag::dimension;
  }

  void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) final {}

  UINT STDMETHODCALLTYPE GetEvictionPriority() final { return DXGI_RESOURCE_PRIORITY_NORMAL; }

  virtual HRESULT GetDeviceInterface(REFIID riid, void **ppDevice) {
    Com<ID3D11Device> device;
    this->GetDevice(&device);
    return device->QueryInterface(riid, ppDevice);
  };

  virtual HRESULT GetDXGIUsage(DXGI_USAGE *pUsage) {
    if (!pUsage) {
      return E_INVALIDARG;
    }
    *pUsage = 0;
    return S_OK;
  }

  virtual HRESULT STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc,
                           ID3D11ShaderResourceView1 **ppView) {
    return E_INVALIDARG;
  };
  virtual HRESULT STDMETHODCALLTYPE
  CreateUnorderedAccessView(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc,
                            ID3D11UnorderedAccessView1 **ppView) {
    return E_INVALIDARG;
  };
  virtual HRESULT STDMETHODCALLTYPE
  CreateRenderTargetView(const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc,
                         ID3D11RenderTargetView1 **ppView) {
    return E_INVALIDARG;
  };
  virtual HRESULT STDMETHODCALLTYPE
  CreateDepthStencilView(const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
                         ID3D11DepthStencilView **ppView) {
    return E_INVALIDARG;
  };

protected:
#ifndef VBOX
  tag::DESC1 desc;
#else
  typename tag::DESC1 desc;
#endif
  std::unique_ptr<IDXGIResource1> dxgi_resource;
#ifndef VBOX
  tag::D3D10_IMPL d3d10;
#else
  typename tag::D3D10_IMPL d3d10;
#endif
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = IMTLD3D11RenderTargetView>
struct tag_render_target_view {
  using COM = ID3D11RenderTargetView;
  using COM1 = ID3D11RenderTargetView1;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_RENDER_TARGET_VIEW_DESC;
  using DESC1 = D3D11_RENDER_TARGET_VIEW_DESC1;
  using D3D10 = ID3D10RenderTargetView;
  using D3D10_1 = ID3D10RenderTargetView;
  using D3D10_IMPL = MTLD3D10RenderTargetView;
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = IMTLD3D11DepthStencilView>
struct tag_depth_stencil_view {
  using COM = ID3D11DepthStencilView;
  using COM1 = ID3D11DepthStencilView;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_DEPTH_STENCIL_VIEW_DESC;
  using DESC1 = D3D11_DEPTH_STENCIL_VIEW_DESC;
  using D3D10 = ID3D10DepthStencilView;
  using D3D10_1 = ID3D10DepthStencilView;
  using D3D10_IMPL = MTLD3D10DepthStencilView;
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = D3D11ShaderResourceView>
struct tag_shader_resource_view {
  using COM = ID3D11ShaderResourceView;
  using COM1 = ID3D11ShaderResourceView1;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_SHADER_RESOURCE_VIEW_DESC;
  using DESC1 = D3D11_SHADER_RESOURCE_VIEW_DESC1;
  using D3D10 = ID3D10ShaderResourceView;
  using D3D10_1 = ID3D10ShaderResourceView1;
  using D3D10_IMPL = MTLD3D10ShaderResourceView;
};

class ImaginaryD3D10UnorderedAccessView {
public:
  ImaginaryD3D10UnorderedAccessView(IUnknown *d3d11) {}
};

template <typename RESOURCE_IMPL_ = ID3D11Resource,
          typename COM_IMPL_ = D3D11UnorderedAccessView>
struct tag_unordered_access_view {
  using COM = ID3D11UnorderedAccessView;
  using COM1 = ID3D11UnorderedAccessView1;
  using COM_IMPL = COM_IMPL_;
  using RESOURCE = ID3D11Resource;
  using RESOURCE_IMPL = RESOURCE_IMPL_;
  using DESC = D3D11_UNORDERED_ACCESS_VIEW_DESC;
  using DESC1 = D3D11_UNORDERED_ACCESS_VIEW_DESC1;
  using D3D10 = IUnknown;
  using D3D10_1 = IUnknown;
  using D3D10_IMPL = ImaginaryD3D10UnorderedAccessView;
};

template <typename tag, typename... Base>
class TResourceViewBase
    : public MTLD3D11DeviceChild<typename tag::COM_IMPL, Base...> {
public:
#ifndef VBOX
  TResourceViewBase(const tag::DESC1 *pDesc, tag::RESOURCE_IMPL *pResource,
#else
  TResourceViewBase(const typename tag::DESC1 *pDesc, typename tag::RESOURCE_IMPL *pResource,
#endif
                    MTLD3D11Device *device)
      : MTLD3D11DeviceChild<typename tag::COM_IMPL, Base...>(device),
        d3d10(static_cast<typename tag::COM_IMPL *>(this)), resource(pResource) {
    if (pDesc) {
      desc = *pDesc;
    }
  }

  template <std::size_t n> HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    return E_NOINTERFACE;
  };

  template <std::size_t n, typename V, typename... Args>
  HRESULT ResolveBase(REFIID riid, void **ppvObject) {
    if (riid == __uuidof(V)) {
      *ppvObject = ref_and_cast<V>(this);
      return S_OK;
    }
    return ResolveBase<n + 1, Args...>(riid, ppvObject);
  };

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    auto hr = ResolveBase<0, Base...>(riid, ppvObject);
    if (SUCCEEDED(hr)) {
      return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11View) || riid == __uuidof(typename tag::COM) ||
        riid == __uuidof(typename tag::COM1)) {
      *ppvObject = ref_and_cast<typename tag::COM_IMPL>(this);
      return S_OK;
    }

    if constexpr (!std::is_same<typename tag::D3D10_IMPL, ImaginaryD3D10UnorderedAccessView>::value) {
      if (riid == __uuidof(ID3D10DeviceChild) || riid == __uuidof(ID3D10View) ||
          riid == __uuidof(typename tag::D3D10) ||
          riid == __uuidof(typename tag::D3D10_1)) {
        *ppvObject = ref_and_cast<typename tag::D3D10_IMPL>(&d3d10);
        return S_OK;
      }
    }

    if (logQueryInterfaceError(__uuidof(typename tag::COM), riid)) {
      WARN("D3D11View: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

#ifndef VBOX
  void STDMETHODCALLTYPE GetDesc(tag::DESC *pDesc) final {
#else
  void STDMETHODCALLTYPE GetDesc(typename tag::DESC *pDesc) final {
#endif
    DowngradeViewDescription(desc, pDesc);
  }

#ifndef VBOX
  void STDMETHODCALLTYPE GetDesc1(tag::DESC1 *pDesc) /* override / final */ { *pDesc = desc; }
#else
  void STDMETHODCALLTYPE GetDesc1(typename tag::DESC1 *pDesc) /* override / final */ { *pDesc = desc; }
#endif

#ifndef VBOX
  void STDMETHODCALLTYPE GetResource(tag::RESOURCE **ppResource) final {
#else
  void STDMETHODCALLTYPE GetResource(typename tag::RESOURCE **ppResource) final {
#endif
    resource->QueryInterface(IID_PPV_ARGS(ppResource));
  }

  virtual ULONG64 GetUnderlyingResourceId() { return (ULONG64)resource.ptr(); };

  virtual dxmt::ResourceSubset GetViewRange() { return ResourceSubset(desc); };

protected:
#ifndef VBOX
  tag::DESC1 desc;
  tag::D3D10_IMPL d3d10;
#else
  typename tag::DESC1 desc;
  typename tag::D3D10_IMPL d3d10;
#endif
  /**
  strong ref to resource
  */
  Com<typename tag::RESOURCE_IMPL> resource;
};

#pragma region Resource Factory

HRESULT
CreateStagingBuffer(MTLD3D11Device *pDevice, const D3D11_BUFFER_DESC *pDesc,
                    const D3D11_SUBRESOURCE_DATA *pInitialData,
                    ID3D11Buffer **ppBuffer);

HRESULT
CreateStagingTexture1D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE1D_DESC *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture1D **ppTexture);

HRESULT
CreateStagingTexture2D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE2D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture2D1 **ppTexture);

HRESULT
CreateStagingTexture3D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE3D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture3D1 **ppTexture);

HRESULT CreateDeviceTexture1D(MTLD3D11Device *pDevice,
                              const D3D11_TEXTURE1D_DESC *pDesc,
                              const D3D11_SUBRESOURCE_DATA *pInitialData,
                              ID3D11Texture1D **ppTexture);

HRESULT CreateDeviceTexture2D(MTLD3D11Device *pDevice,
                              const D3D11_TEXTURE2D_DESC1 *pDesc,
                              const D3D11_SUBRESOURCE_DATA *pInitialData,
                              ID3D11Texture2D1 **ppTexture);

HRESULT CreateDeviceTexture3D(MTLD3D11Device *pDevice,
                              const D3D11_TEXTURE3D_DESC1 *pDesc,
                              const D3D11_SUBRESOURCE_DATA *pInitialData,
                              ID3D11Texture3D1 **ppTexture);

HRESULT
CreateBuffer(MTLD3D11Device *pDevice, const D3D11_BUFFER_DESC *pDesc,
                    const D3D11_SUBRESOURCE_DATA *pInitialData,
                    ID3D11Buffer **ppBuffer);

HRESULT CreateDynamicTexture1D(MTLD3D11Device *pDevice,
                               const D3D11_TEXTURE1D_DESC *pDesc,
                               const D3D11_SUBRESOURCE_DATA *pInitialData,
                               ID3D11Texture1D **ppTexture);

HRESULT CreateDynamicTexture2D(MTLD3D11Device *pDevice,
                               const D3D11_TEXTURE2D_DESC1 *pDesc,
                               const D3D11_SUBRESOURCE_DATA *pInitialData,
                               ID3D11Texture2D1 **ppTexture);
#pragma endregion

#pragma region Helper
template <typename RESOURCE_DESC, typename VIEW_DESC>
HRESULT ExtractEntireResourceViewDescription(const RESOURCE_DESC *pResourceDesc,
                                             VIEW_DESC *pViewDescOut);

template <typename RESOURCE_DESC, typename VIEW_DESC>
HRESULT ExtractEntireResourceViewDescription(const RESOURCE_DESC *pResourceDesc,
                                             const VIEW_DESC *pViewDescIn,
                                             VIEW_DESC *pViewDescOut) {
  if (pViewDescIn) {
    *pViewDescOut = *pViewDescIn;
    if constexpr (!std::is_same_v<RESOURCE_DESC, D3D11_BUFFER_DESC>) {
      if (pViewDescOut->Format == DXGI_FORMAT_UNKNOWN) {
        pViewDescOut->Format = pResourceDesc->Format;
      }
    }
    return S_OK;
  } else {
    return ExtractEntireResourceViewDescription(pResourceDesc, pViewDescOut);
  }
}

template <typename TEXTURE_DESC>
HRESULT CreateMTLTextureDescriptor(MTLD3D11Device *pDevice,
                                   const TEXTURE_DESC *pDesc,
                                   TEXTURE_DESC *pOutDesc,
                                   WMTTextureInfo *pMtlDescOut);

template <typename VIEW_DESC>
HRESULT InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture *pTexture, VIEW_DESC &ViewDesc,
    TextureViewDescriptor &Descriptor
);

template <typename VIEW_DESC>
HRESULT InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture* pTexture,
    VIEW_DESC &ViewDesc, MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc, TextureViewDescriptor &Descriptor
);

template <typename TEXTURE_DESC>
void GetMipmapSize(const TEXTURE_DESC *pDesc, uint32_t level, uint32_t *pWidth,
                   uint32_t *pHeight, uint32_t *pDepth);

template <typename TEXTURE_DESC>
HRESULT GetLinearTextureLayout(MTLD3D11Device *pDevice,
                               const TEXTURE_DESC &Desc, uint32_t Level,
                               uint32_t &BytesPerRow, uint32_t &BytesPerImage,
                               uint32_t &BytesPerSlice, bool Aligned = true);

constexpr void
CalculateBufferViewOffsetAndSize(const D3D11_BUFFER_DESC &buffer_desc,
                                 uint32_t element_stride,
                                 uint32_t first_element, uint32_t num_elements,
                                 uint32_t &offset, uint32_t &size) {
  offset = first_element * element_stride;
  size = std::min(std::max(0u, buffer_desc.ByteWidth - offset),
                  element_stride * num_elements);
};
#pragma endregion

} // namespace dxmt