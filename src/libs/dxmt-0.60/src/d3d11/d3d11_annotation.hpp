#pragma once
#include "d3d11_1.h"

namespace dxmt {

class D3D11UserDefinedAnnotation : public ID3DUserDefinedAnnotation {
public:
  D3D11UserDefinedAnnotation(IUnknown *container) : container_(container) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) override {
    return container_->QueryInterface(riid, ppvObject);
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return container_->AddRef(); }

  ULONG STDMETHODCALLTYPE Release() override { return container_->Release(); }

  INT STDMETHODCALLTYPE BeginEvent(LPCWSTR Name) override { return -1; }

  INT STDMETHODCALLTYPE EndEvent() override { return -1; }

  void STDMETHODCALLTYPE SetMarker(LPCWSTR Name) override {}

  WINBOOL STDMETHODCALLTYPE GetStatus() override { return FALSE; }

private:
  IUnknown *container_;
};

} // namespace dxmt
