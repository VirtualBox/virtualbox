#include "d3d10_buffer.hpp"

namespace dxmt {

HRESULT STDMETHODCALLTYPE
MTLD3D10Buffer::Map(D3D10_MAP MapType, UINT MapFlags, void **ppData) {
  D3D11_MAPPED_SUBRESOURCE subresource{};
  HRESULT hr = d3d11_context->Map(d3d11, 0, D3D11_MAP(MapType), MapFlags, &subresource);
  if (hr == S_OK)
    *ppData = subresource.pData;
  return hr;
}

void STDMETHODCALLTYPE
MTLD3D10Buffer::Unmap() {
  d3d11_context->Unmap(d3d11, 0);
}

void STDMETHODCALLTYPE
MTLD3D10Buffer::GetDesc(D3D10_BUFFER_DESC *pDesc) {
  D3D11_BUFFER_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->ByteWidth = d3d11_desc.ByteWidth;
  pDesc->Usage = D3D10_USAGE(d3d11_desc.Usage);
  pDesc->BindFlags = d3d11_desc.BindFlags;
  pDesc->CPUAccessFlags = d3d11_desc.CPUAccessFlags;
  pDesc->MiscFlags = ConvertD3D11ResourceFlags(d3d11_desc.MiscFlags);
}
} // namespace dxmt