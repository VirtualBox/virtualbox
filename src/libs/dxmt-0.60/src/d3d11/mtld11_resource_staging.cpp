#include "d3d11_private.h"
#ifndef VBOX
#include "d3d11_enumerable.hpp"
#endif
#include "dxmt_dynamic.hpp"
#include "dxmt_staging.hpp"
#include "mtld11_resource.hpp"
namespace dxmt {

#pragma region StagingBuffer

class StagingBuffer : public TResourceBase<tag_buffer> {
private:
  Rc<StagingResource> internal;

public:
  StagingBuffer(const tag_buffer::DESC1 *pDesc, MTLD3D11Device *device,
                Rc<StagingResource> &&internal)
      : TResourceBase<tag_buffer>(*pDesc, device),
        internal(std::move(internal)) {}

  void OnSetDebugObjectName(LPCSTR Name) override {
  }

  Rc<Buffer>
  buffer() final {
    return {};
  };
  Rc<Texture>
  texture() final {
    return {};
  };
  BufferSlice
  bufferSlice() final {
    return {};
  }
  Rc<StagingResource>
  staging(UINT Subresource) final {
    assert(Subresource == 0);
    return internal;
  }
  Rc<DynamicBuffer>
  dynamicBuffer(UINT*, UINT*) final {
    return {};
  };
  Rc<DynamicTexture>
  dynamicTexture(UINT *, UINT *) final {
    return {};
  };
};

HRESULT
CreateStagingBuffer(MTLD3D11Device *pDevice, const D3D11_BUFFER_DESC *pDesc,
                    const D3D11_SUBRESOURCE_DATA *pInitialData,
                    ID3D11Buffer **ppBuffer) {
  auto metal = pDevice->GetMTLDevice();
  auto byte_width = pDesc->ByteWidth;
  auto buffer = new StagingResource(metal, byte_width, WMTResourceStorageModeShared, byte_width, byte_width);
  if (pInitialData) {
    memcpy(buffer->mappedImmediateMemory(), pInitialData->pSysMem, byte_width);
  }
  *ppBuffer = reinterpret_cast<ID3D11Buffer *>(ref(new StagingBuffer(pDesc, pDevice, buffer)));
  return S_OK;
}

#pragma endregion

#pragma region StagingTexture

template <typename tag_texture>
class StagingTexture : public TResourceBase<tag_texture> {
  std::vector<Rc<StagingResource>> subresources;
  uint32_t subresource_count;

public:
  StagingTexture(const tag_texture::DESC1 *pDesc, MTLD3D11Device *pDevice,
                 std::vector<Rc<StagingResource>> &&subresources)
      : TResourceBase<tag_texture>(*pDesc, pDevice),
        subresources(std::move(subresources)),
        subresource_count(this->subresources.size()) {}

  void OnSetDebugObjectName(LPCSTR Name) override {
    if (!Name) {
      return;
    }
  }

  Rc<Buffer>
  buffer() final {
    return {};
  };
  Rc<Texture>
  texture() final {
    return {};
  };
  BufferSlice
  bufferSlice() final {
    return {};
  }
  Rc<StagingResource>
  staging(UINT Subresource) final {
    return subresources.at(Subresource);
  }
  Rc<DynamicBuffer>
  dynamicBuffer(UINT *, UINT *) final {
    return {};
  };
  Rc<DynamicTexture>
  dynamicTexture(UINT *, UINT *) final {
    return {};
  };
};

#pragma endregion

#ifndef VBOX
template <typename tag>
HRESULT CreateStagingTextureInternal(MTLD3D11Device *pDevice,
                                     const typename tag::DESC1 *pDesc,
                                     const D3D11_SUBRESOURCE_DATA *pInitialData,
                                     typename tag::COM_IMPL **ppTexture) {
  auto metal = pDevice->GetMTLDevice();
  typename tag::DESC1 finalDesc;
  WMTTextureInfo texDesc; // unused
  // clang-format, why do you piss me off
  // is this really expected to be read by human?
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &texDesc))) {
    return E_INVALIDARG;
  }
  std::vector<Rc<StagingResource>> subresources;
  for (auto &sub : EnumerateSubresources(finalDesc)) {
    uint32_t w, h, d;
    GetMipmapSize(&finalDesc, sub.MipLevel, &w, &h, &d);
    uint32_t bpr, bpi, buf_len;
    if (FAILED(GetLinearTextureLayout(pDevice, finalDesc, sub.MipLevel, bpr, bpi, buf_len))) {
      return E_FAIL;
    }
    D3D11_ASSERT(subresources.size() == sub.SubresourceId);
    auto buffer = new StagingResource(metal, buf_len, WMTResourceStorageModeShared, bpr, bpi);
    if (pInitialData) {
      // FIXME: SysMemPitch and SysMemSlicePitch should be respected!
      memcpy(buffer->mappedImmediateMemory(), pInitialData[sub.SubresourceId].pSysMem, buf_len);
    }
    subresources.push_back(buffer);
  }

  *ppTexture = reinterpret_cast<typename tag::COM_IMPL *>(ref(
      new StagingTexture<tag>(&finalDesc, pDevice, std::move(subresources))));
  return S_OK;
}

HRESULT
CreateStagingTexture1D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE1D_DESC *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture1D **ppTexture) {
  return CreateStagingTextureInternal<tag_texture_1d>(pDevice, pDesc,
                                                      pInitialData, ppTexture);
}

HRESULT
CreateStagingTexture2D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE2D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture2D1 **ppTexture) {
  return CreateStagingTextureInternal<tag_texture_2d>(pDevice, pDesc,
                                                      pInitialData, ppTexture);
}

HRESULT
CreateStagingTexture3D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE3D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture3D1 **ppTexture) {
  return CreateStagingTextureInternal<tag_texture_3d>(pDevice, pDesc,
                                                      pInitialData, ppTexture);
}
#else /* VBOX */

/* I know this is ugly af but allows us to use an older SDK/clang toolchain. */
template <typename tag>
HRESULT CreateStagingTextureInternal(MTLD3D11Device *pDevice,
                                     auto metal,
                                     typename tag::DESC1 *pDesc,
                                     const D3D11_SUBRESOURCE_DATA *pInitialData,
                                     std::vector<Rc<StagingResource>> &subresources,
                                     uint32_t MipLevel,
                                     uint32_t SubresourceId) {

  uint32_t w, h, d;
  GetMipmapSize(pDesc, MipLevel, &w, &h, &d);
  uint32_t bpr, bpi, buf_len;
  if (FAILED(GetLinearTextureLayout(pDevice, *pDesc, MipLevel, bpr, bpi, buf_len))) {
    return E_FAIL;
  }
  D3D11_ASSERT(subresources.size() == SubresourceId);
  auto buffer = new StagingResource(metal, buf_len, WMTResourceStorageModeShared, bpr, bpi);
  if (pInitialData) {
    if (bpr > pInitialData[SubresourceId].SysMemPitch)
    {
            uint8_t *pbDst = (uint8_t *)buffer->mappedImmediateMemory();
      const uint8_t *pbSrc = (const uint8_t *)pInitialData[SubresourceId].pSysMem;
      for (uint32_t i = 0; i < h; i++)
      {
        memcpy(pbDst, pbSrc, pInitialData[SubresourceId].SysMemPitch);
        pbDst += bpr;
        pbSrc += pInitialData[SubresourceId].SysMemPitch;
      }
    }
    else
      memcpy(buffer->mappedImmediateMemory(), pInitialData[SubresourceId].pSysMem, buf_len);
  }
  subresources.push_back(buffer);

  return S_OK;
}

HRESULT
CreateStagingTexture1D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE1D_DESC *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture1D **ppTexture) {
  auto metal = pDevice->GetMTLDevice();
  typename tag_texture_1d::DESC1 finalDesc;
  WMTTextureInfo texDesc; // unused
  // clang-format, why do you piss me off
  // is this really expected to be read by human?
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &texDesc))) {
    return E_INVALIDARG;
  }
  std::vector<Rc<StagingResource>> subresources;

  for (uint32_t slice = 0; slice < finalDesc.ArraySize; slice++) {
    for (uint32_t level = 0; level < finalDesc.MipLevels; level++) {
        uint32_t SubresourceId = D3D11CalcSubresource(level, slice, finalDesc.MipLevels);
        HRESULT hrc = CreateStagingTextureInternal<tag_texture_1d>(pDevice, metal, &finalDesc,
                                                                   pInitialData, subresources,
                                                                   level, SubresourceId);
        if (FAILED(hrc))
          return hrc;
    }
  }

  *ppTexture = reinterpret_cast<typename tag_texture_1d::COM_IMPL *>(ref(
      new StagingTexture<tag_texture_1d>(&finalDesc, pDevice, std::move(subresources))));
  return S_OK;
}

HRESULT
CreateStagingTexture2D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE2D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture2D1 **ppTexture) {
  auto metal = pDevice->GetMTLDevice();
  typename tag_texture_2d::DESC1 finalDesc;
  WMTTextureInfo texDesc; // unused
  // clang-format, why do you piss me off
  // is this really expected to be read by human?
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &texDesc))) {
    return E_INVALIDARG;
  }
  std::vector<Rc<StagingResource>> subresources;

  for (uint32_t slice = 0; slice < finalDesc.ArraySize; slice++) {
    for (uint32_t level = 0; level < finalDesc.MipLevels; level++) {
        uint32_t SubresourceId = D3D11CalcSubresource(level, slice, finalDesc.MipLevels);
        HRESULT hrc = CreateStagingTextureInternal<tag_texture_2d>(pDevice, metal, &finalDesc,
                                                                   pInitialData, subresources,
                                                                   level, SubresourceId);
        if (FAILED(hrc))
          return hrc;
    }
  }

  *ppTexture = reinterpret_cast<typename tag_texture_2d::COM_IMPL *>(ref(
      new StagingTexture<tag_texture_2d>(&finalDesc, pDevice, std::move(subresources))));
  return S_OK;
}

HRESULT
CreateStagingTexture3D(MTLD3D11Device *pDevice,
                       const D3D11_TEXTURE3D_DESC1 *pDesc,
                       const D3D11_SUBRESOURCE_DATA *pInitialData,
                       ID3D11Texture3D1 **ppTexture) {
  auto metal = pDevice->GetMTLDevice();
  typename tag_texture_3d::DESC1 finalDesc;
  WMTTextureInfo texDesc; // unused
  // clang-format, why do you piss me off
  // is this really expected to be read by human?
  if (FAILED(CreateMTLTextureDescriptor(pDevice, pDesc, &finalDesc, &texDesc))) {
    return E_INVALIDARG;
  }
  std::vector<Rc<StagingResource>> subresources;

  for (uint32_t level = 0; level < finalDesc.MipLevels; level++) {
      uint32_t SubresourceId = D3D11CalcSubresource(level, 0, finalDesc.MipLevels);
      HRESULT hrc = CreateStagingTextureInternal<tag_texture_3d>(pDevice, metal, &finalDesc,
                                                                 pInitialData, subresources,
                                                                 level, SubresourceId);
      if (FAILED(hrc))
        return hrc;
  }

  *ppTexture = reinterpret_cast<typename tag_texture_3d::COM_IMPL *>(ref(
      new StagingTexture<tag_texture_3d>(&finalDesc, pDevice, std::move(subresources))));
  return S_OK;
}
#endif

} // namespace dxmt