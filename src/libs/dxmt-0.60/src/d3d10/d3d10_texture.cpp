#include "d3d10_texture.hpp"

namespace dxmt {

HRESULT STDMETHODCALLTYPE
MTLD3D10Texture1D::Map(UINT Subresource, D3D10_MAP MapType, UINT MapFlags, void **ppData) {
  D3D11_MAPPED_SUBRESOURCE subresource{};
  HRESULT hr = d3d11_context->Map(d3d11, Subresource, D3D11_MAP(MapType), MapFlags, &subresource);
  if (hr == S_OK)
    *ppData = subresource.pData;
  return hr;
}

void STDMETHODCALLTYPE
MTLD3D10Texture1D::Unmap(UINT Subresource) {
  d3d11_context->Unmap(d3d11, Subresource);
}

void STDMETHODCALLTYPE
MTLD3D10Texture1D::GetDesc(D3D10_TEXTURE1D_DESC *pDesc) {
  D3D11_TEXTURE1D_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->Width = d3d11_desc.Width;
  pDesc->MipLevels = d3d11_desc.MipLevels;
  pDesc->ArraySize = d3d11_desc.ArraySize;
  pDesc->Format = d3d11_desc.Format;
  pDesc->Usage = D3D10_USAGE(d3d11_desc.Usage);
  pDesc->BindFlags = d3d11_desc.BindFlags;
  pDesc->CPUAccessFlags = d3d11_desc.CPUAccessFlags;
  pDesc->MiscFlags = ConvertD3D11ResourceFlags(d3d11_desc.MiscFlags);
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Texture2D::Map(UINT Subresource, D3D10_MAP MapType, UINT MapFlags, D3D10_MAPPED_TEXTURE2D *pMappedTex2D) {
  D3D11_MAPPED_SUBRESOURCE subresource{};
  HRESULT hr = d3d11_context->Map(d3d11, Subresource, D3D11_MAP(MapType), MapFlags, &subresource);
  if (hr == S_OK) {
    pMappedTex2D->pData = subresource.pData;
    pMappedTex2D->RowPitch = subresource.RowPitch;
  }
  return hr;
}

void STDMETHODCALLTYPE
MTLD3D10Texture2D::Unmap(UINT Subresource) {
  d3d11_context->Unmap(d3d11, Subresource);
}

void STDMETHODCALLTYPE
MTLD3D10Texture2D::GetDesc(D3D10_TEXTURE2D_DESC *pDesc) {
  D3D11_TEXTURE2D_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->Width = d3d11_desc.Width;
  pDesc->Height = d3d11_desc.Height;
  pDesc->MipLevels = d3d11_desc.MipLevels;
  pDesc->ArraySize = d3d11_desc.ArraySize;
  pDesc->Format = d3d11_desc.Format;
  pDesc->SampleDesc = d3d11_desc.SampleDesc;
  pDesc->Usage = D3D10_USAGE(d3d11_desc.Usage);
  pDesc->BindFlags = d3d11_desc.BindFlags;
  pDesc->CPUAccessFlags = d3d11_desc.CPUAccessFlags;
  pDesc->MiscFlags = ConvertD3D11ResourceFlags(d3d11_desc.MiscFlags);
}

HRESULT STDMETHODCALLTYPE
MTLD3D10Texture3D::Map(UINT Subresource, D3D10_MAP MapType, UINT MapFlags, D3D10_MAPPED_TEXTURE3D *pMappedTex3D) {
  D3D11_MAPPED_SUBRESOURCE subresource{};
  HRESULT hr = d3d11_context->Map(d3d11, Subresource, D3D11_MAP(MapType), MapFlags, &subresource);
  if (hr == S_OK) {
    pMappedTex3D->pData = subresource.pData;
    pMappedTex3D->RowPitch = subresource.RowPitch;
    pMappedTex3D->DepthPitch = subresource.DepthPitch;
  }
  return hr;
}

void STDMETHODCALLTYPE
MTLD3D10Texture3D::Unmap(UINT Subresource) {
  d3d11_context->Unmap(d3d11, Subresource);
}

void STDMETHODCALLTYPE
MTLD3D10Texture3D::GetDesc(D3D10_TEXTURE3D_DESC *pDesc) {
  D3D11_TEXTURE3D_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->Width = d3d11_desc.Width;
  pDesc->Height = d3d11_desc.Height;
  pDesc->Depth = d3d11_desc.Depth;
  pDesc->MipLevels = d3d11_desc.MipLevels;
  pDesc->Format = d3d11_desc.Format;
  pDesc->Usage = D3D10_USAGE(d3d11_desc.Usage);
  pDesc->BindFlags = d3d11_desc.BindFlags;
  pDesc->CPUAccessFlags = d3d11_desc.CPUAccessFlags;
  pDesc->MiscFlags = ConvertD3D11ResourceFlags(d3d11_desc.MiscFlags);
}

} // namespace dxmt