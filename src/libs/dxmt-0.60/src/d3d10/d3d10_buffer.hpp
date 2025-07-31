#pragma once

#include "d3d10_util.hpp"

namespace dxmt {

class MTLD3D10Buffer final : public ID3D10Buffer {

public:
MTLD3D10Buffer(ID3D11Buffer *d3d11, ID3D11DeviceContext *d3d11_context) : d3d11(d3d11), d3d11_context(d3d11_context) {}

  ID3D11Buffer *d3d11;
  ID3D11DeviceContext *d3d11_context;

  HRESULT STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) {
    return d3d11->QueryInterface(riid, ppvObject);
  }

  ULONG STDMETHODCALLTYPE
  AddRef() {
    return d3d11->AddRef();
  }

  ULONG STDMETHODCALLTYPE
  Release() {
    return d3d11->Release();
  }

  void STDMETHODCALLTYPE GetDevice(ID3D10Device **ppDevice) {
    GetD3D10Device(d3d11, ppDevice);
  }

  HRESULT STDMETHODCALLTYPE
  GetPrivateData(REFGUID guid, UINT *pDataSize, void *pData) {
    return d3d11->GetPrivateData(guid, pDataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE
  SetPrivateData(REFGUID guid, UINT DataSize, const void *pData) {
    return d3d11->SetPrivateData(guid, DataSize, pData);
  }

  HRESULT STDMETHODCALLTYPE
  SetPrivateDataInterface(REFGUID guid, const IUnknown *pData) {
    return d3d11->SetPrivateDataInterface(guid, pData);
  }

  void STDMETHODCALLTYPE
  GetType(D3D10_RESOURCE_DIMENSION *rType) {
    d3d11->GetType((D3D11_RESOURCE_DIMENSION *)rType);
  }

  void STDMETHODCALLTYPE
  SetEvictionPriority(UINT EvictionPriority) {
    d3d11->SetEvictionPriority(EvictionPriority);
  }

  UINT STDMETHODCALLTYPE
  GetEvictionPriority() {
    return d3d11->GetEvictionPriority();
  }

  HRESULT STDMETHODCALLTYPE Map(D3D10_MAP MapType, UINT MapFlags, void **ppData);

  void STDMETHODCALLTYPE Unmap();

  void STDMETHODCALLTYPE GetDesc(D3D10_BUFFER_DESC *pDesc);
};

}; // namespace dxmt