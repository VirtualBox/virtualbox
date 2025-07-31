#pragma once

#include "d3d10_util.hpp"

namespace dxmt {

class MTLD3D10InputLayout final : public ID3D10InputLayout {
public:
  MTLD3D10InputLayout(ID3D11InputLayout *d3d11) : d3d11(d3d11) {}

  ID3D11InputLayout *d3d11;

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

  void STDMETHODCALLTYPE
  GetDevice(ID3D10Device **ppDevice) {
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
};

}; // namespace dxmt