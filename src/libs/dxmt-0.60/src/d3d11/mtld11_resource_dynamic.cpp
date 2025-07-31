#include "d3d11_private.h"
#include "com/com_pointer.hpp"
#include "d3d11_device.hpp"
#include "dxmt_dynamic.hpp"
#include "dxmt_format.hpp"
#include "dxmt_staging.hpp"
#include "dxmt_texture.hpp"
#include "mtld11_resource.hpp"

namespace dxmt {

#pragma region DynamicTexture

template <typename tag_texture>
class TDynamicTexture : public TResourceBase<tag_texture> {
private:
  Rc<Texture> texture_;
  Rc<DynamicTexture> dynamic_;
  size_t bytes_per_image_;
  size_t bytes_per_row_;

  using SRVBase = TResourceViewBase<tag_shader_resource_view<TDynamicTexture>>;

  class SRV : public SRVBase {
    TextureViewKey view_key;

  public:
    SRV(const tag_shader_resource_view<>::DESC1 *pDesc, TDynamicTexture *pResource, MTLD3D11Device *pDevice,
        TextureViewKey view_key) :
        SRVBase(pDesc, pResource, pDevice),
        view_key(view_key) {}

    ~SRV() {}

    Rc<Buffer> buffer() final { return {}; };
    Rc<Texture> texture() final { return this->resource->texture_; };
    unsigned viewId() final { return view_key;};
    BufferSlice bufferSlice() final { return {};}
  };

public:
TDynamicTexture(
      const tag_texture::DESC1 *pDesc, const WMTTextureInfo &descriptor,
      const D3D11_SUBRESOURCE_DATA *pInitialData, UINT bytes_per_image, UINT bytes_per_row, MTLD3D11Device *device
  ) :
      TResourceBase<tag_texture>(*pDesc, device),
      bytes_per_image_(bytes_per_image),
      bytes_per_row_(bytes_per_row) {
    texture_ = new Texture(bytes_per_image, bytes_per_row, descriptor, device->GetMTLDevice());
    Flags<TextureAllocationFlag> flags;
    if (!this->m_parent->IsTraced() && pDesc->Usage == D3D11_USAGE_DYNAMIC)
      flags.set(TextureAllocationFlag::CpuWriteCombined);
    // if (pDesc->Usage != D3D11_USAGE_DEFAULT)
    if (pDesc->Usage == D3D11_USAGE_IMMUTABLE)
      flags.set(TextureAllocationFlag::GpuReadonly);
    if (pDesc->Usage != D3D11_USAGE_DYNAMIC)
      flags.set(TextureAllocationFlag::GpuManaged);
    auto allocation = texture_->allocate(flags);
    auto _ = texture_->rename(Rc(allocation));
    D3D11_ASSERT(_.ptr() == nullptr);

    if (pInitialData) {
      if (pInitialData->SysMemPitch != bytes_per_row_) {
        for (unsigned row = 0; row < texture_->height(); row++) {
          memcpy(
              ptr_add(allocation->mappedMemory, row * bytes_per_row_),
              ptr_add(pInitialData->pSysMem, row * pInitialData->SysMemPitch),
              pInitialData->SysMemPitch);
        }
      } else {
        memcpy(allocation->mappedMemory, pInitialData->pSysMem, bytes_per_image);
      }
    }
    dynamic_ = new DynamicTexture(texture_.ptr(), flags);
  }

  Rc<Buffer> buffer() final { return {}; };
  Rc<Texture> texture() final { return this->texture_; };
  BufferSlice bufferSlice() final { return {};}
  Rc<StagingResource> staging(UINT) final { return nullptr; }
  Rc<DynamicBuffer> dynamicBuffer(UINT*, UINT*) final { return {}; };
  Rc<DynamicTexture> dynamicTexture(UINT* pBytesPerRow, UINT* pBytesPerImage) final {
    *pBytesPerRow = bytes_per_row_;
    *pBytesPerImage = bytes_per_image_;
    return dynamic_; 
  };

  HRESULT
  STDMETHODCALLTYPE
  CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc, ID3D11ShaderResourceView1 **ppView) override;
};

template<>
HRESULT STDMETHODCALLTYPE TDynamicTexture<tag_texture_2d>::CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc, ID3D11ShaderResourceView1 **ppView){
  D3D11_SHADER_RESOURCE_VIEW_DESC1 finalDesc;
  if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc, &finalDesc))) {
    return E_INVALIDARG;
  }
  if (finalDesc.ViewDimension != D3D10_SRV_DIMENSION_TEXTURE2D) {
    ERR("Only 2d texture SRV can be created on dynamic texture");
    return E_FAIL;
  }
  if (~finalDesc.Texture2D.MipLevels == 0)
    finalDesc.Texture2D.MipLevels = this->desc.MipLevels - finalDesc.Texture2D.MostDetailedMip;
  if (finalDesc.Texture2D.MostDetailedMip != 0 || finalDesc.Texture2D.MipLevels != 1) {
    ERR("2d texture SRV must be non-mipmapped on dynamic texture");
    return E_FAIL;
  }
  if (!ppView) {
    return S_FALSE;
  }
  MTL_DXGI_FORMAT_DESC format;
  if (FAILED(MTLQueryDXGIFormat(this->m_parent->GetMTLDevice(), finalDesc.Format, format))) {
    return E_FAIL;
  }

  auto view_key = texture_->createView(
      {.format = format.PixelFormat,
       .type = WMTTextureType2D,
       .firstMiplevel = 0,
       .miplevelCount = 1,
       .firstArraySlice = 0,
       .arraySize = 1}
  );

  auto srv = ref(new SRV(&finalDesc, this, this->m_parent, view_key));

  *ppView = srv;
  return S_OK;
};

template<>
HRESULT STDMETHODCALLTYPE TDynamicTexture<tag_texture_1d>::CreateShaderResourceView(const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc, ID3D11ShaderResourceView1 **ppView){
  D3D11_SHADER_RESOURCE_VIEW_DESC1 finalDesc;
  if (FAILED(ExtractEntireResourceViewDescription(&this->desc, pDesc, &finalDesc))) {
    return E_INVALIDARG;
  }
  if (finalDesc.ViewDimension != D3D10_SRV_DIMENSION_TEXTURE2D) {
    ERR("Only 1d texture SRV can be created on dynamic texture");
    return E_FAIL;
  }
  if (~finalDesc.Texture1D.MipLevels == 0)
    finalDesc.Texture1D.MipLevels = this->desc.MipLevels - finalDesc.Texture1D.MostDetailedMip;
  if (finalDesc.Texture1D.MostDetailedMip != 0 || finalDesc.Texture1D.MipLevels != 1) {
    ERR("1d texture SRV must be non-mipmapped on dynamic texture");
    return E_FAIL;
  }
  if (!ppView) {
    return S_FALSE;
  }
  MTL_DXGI_FORMAT_DESC format;
  if (FAILED(MTLQueryDXGIFormat(this->m_parent->GetMTLDevice(), finalDesc.Format, format))) {
    return E_FAIL;
  }

  auto view_key = texture_->createView(
      {.format = format.PixelFormat,
       .type = WMTTextureType2D, // since all 1d texture is implemented as 1-row 2d texture
       .firstMiplevel = 0,
       .miplevelCount = 1,
       .firstArraySlice = 0,
       .arraySize = 1}
  );

  auto srv = ref(new SRV(&finalDesc, this, this->m_parent, view_key));

  *ppView = srv;
  return S_OK;
};

#pragma endregion

HRESULT
CreateDynamicTexture2D(
    MTLD3D11Device *pDevice, const D3D11_TEXTURE2D_DESC1 *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Texture2D1 **ppTexture
) {
  MTL_DXGI_FORMAT_DESC format;
  MTLQueryDXGIFormat(pDevice->GetMTLDevice(), pDesc->Format, format);
  if (format.Flag & MTL_DXGI_FORMAT_BC) {
    return E_FAIL;
  }
  if (format.PixelFormat == WMTPixelFormatInvalid) {
    return E_FAIL;
  }
  WMTTextureInfo info;
  D3D11_TEXTURE2D_DESC1 finalDesc;
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &info))) {
    return E_INVALIDARG;
  }
  uint32_t bytesPerRow, bytesPerImage, bufferLen;
  if (FAILED(GetLinearTextureLayout(pDevice, finalDesc, 0, bytesPerRow, bytesPerImage, bufferLen))) {
    return E_FAIL;
  }

  *ppTexture = reinterpret_cast<ID3D11Texture2D1 *>(
      ref(new TDynamicTexture<tag_texture_2d>(pDesc, info, pInitialData, bufferLen, bytesPerRow, pDevice))
  );
  return S_OK;
}

HRESULT
CreateDynamicTexture1D(
    MTLD3D11Device *pDevice, const D3D11_TEXTURE1D_DESC *pDesc, const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Texture1D **ppTexture
) {
  MTL_DXGI_FORMAT_DESC format;
  MTLQueryDXGIFormat(pDevice->GetMTLDevice(), pDesc->Format, format);
  if (format.Flag & MTL_DXGI_FORMAT_BC) {
    return E_FAIL;
  }
  if (format.PixelFormat == WMTPixelFormatInvalid) {
    return E_FAIL;
  }
  WMTTextureInfo info;
  D3D11_TEXTURE1D_DESC finalDesc;
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &info))) {
    return E_INVALIDARG;
  }
  uint32_t bytesPerRow, bytesPerImage, bufferLen;
  if (FAILED(GetLinearTextureLayout(pDevice, finalDesc, 0, bytesPerRow, bytesPerImage, bufferLen))) {
    return E_FAIL;
  }

  *ppTexture = reinterpret_cast<ID3D11Texture1D *>(
      ref(new TDynamicTexture<tag_texture_1d>(pDesc, info, pInitialData, bufferLen, bytesPerRow, pDevice))
  );
  return S_OK;
}

} // namespace dxmt
