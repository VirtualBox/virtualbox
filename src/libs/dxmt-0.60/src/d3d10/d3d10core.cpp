
#include "com/com_object.hpp"
#include "com/com_pointer.hpp"
#include "d3d11.h"
#include "log/log.hpp"

namespace dxmt {
#ifndef VBOX
Logger Logger::s_instance("d3d10core.log");
#endif

extern "C" HRESULT WINAPI D3D11CoreCreateDevice(
    IDXGIFactory *pFactory, IDXGIAdapter *pAdapter, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels,
    UINT FeatureLevels, ID3D11Device **ppDevice
);

extern "C" HRESULT STDMETHODCALLTYPE
D3D10CoreCreateDevice(
    IDXGIFactory *pFactory, IDXGIAdapter *pAdapter, UINT Flags, D3D_FEATURE_LEVEL FeatureLevel, ID3D10Device **ppDevice
) {
  InitReturnPtr(ppDevice);

  Com<ID3D11Device> d3d11_device;

  HRESULT hr = pAdapter->CheckInterfaceSupport(__uuidof(ID3D10Device), nullptr);

  if (FAILED(hr))
    return hr;

  hr = D3D11CoreCreateDevice(pFactory, pAdapter, Flags | 0x80000000 /* DXMT_D3D10_DEVICE */, &FeatureLevel, 1, &d3d11_device);

  if (FAILED(hr))
    return hr;

  Com<ID3D10Multithread> multithread;
  d3d11_device->QueryInterface(IID_PPV_ARGS(&multithread));
  multithread->SetMultithreadProtected(!(Flags & D3D10_CREATE_DEVICE_SINGLETHREADED));

  return d3d11_device->QueryInterface(IID_PPV_ARGS(ppDevice));
}

extern "C" HRESULT STDMETHODCALLTYPE
D3D10CoreRegisterLayers() {
  ERR("D3D10CoreRegisterLayers: stub");
  return E_NOTIMPL;
}
} // namespace dxmt
