#pragma once

#include "d3d10_util.hpp"

namespace dxmt {

class MTLD3D10ShaderResourceView final : public ID3D10ShaderResourceView1 {

public:
  MTLD3D10ShaderResourceView(ID3D11ShaderResourceView *d3d11) : d3d11(d3d11) {}

  ID3D11ShaderResourceView *d3d11;

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

  void STDMETHODCALLTYPE
  GetResource(ID3D10Resource **ppResource) {
    GetD3D10Resource(d3d11, ppResource);
  }

  void STDMETHODCALLTYPE GetDesc(D3D10_SHADER_RESOURCE_VIEW_DESC *pDesc);

  void STDMETHODCALLTYPE GetDesc1(D3D10_SHADER_RESOURCE_VIEW_DESC1 *pDesc);
};

class MTLD3D10RenderTargetView final : public ID3D10RenderTargetView {

public:
  MTLD3D10RenderTargetView(ID3D11RenderTargetView *d3d11) : d3d11(d3d11) {}
  ID3D11RenderTargetView *d3d11;

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

  void STDMETHODCALLTYPE
  GetResource(ID3D10Resource **ppResource) {
    GetD3D10Resource(d3d11, ppResource);
  }

  void STDMETHODCALLTYPE GetDesc(D3D10_RENDER_TARGET_VIEW_DESC *pDesc);
};

class MTLD3D10DepthStencilView final : public ID3D10DepthStencilView {

public:
  MTLD3D10DepthStencilView(ID3D11DepthStencilView *d3d11) : d3d11(d3d11) {}

  ID3D11DepthStencilView *d3d11;

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

  void STDMETHODCALLTYPE
  GetResource(ID3D10Resource **ppResource) {
    GetD3D10Resource(d3d11, ppResource);
  }

  void STDMETHODCALLTYPE GetDesc(D3D10_DEPTH_STENCIL_VIEW_DESC *pDesc);
};

}; // namespace dxmt