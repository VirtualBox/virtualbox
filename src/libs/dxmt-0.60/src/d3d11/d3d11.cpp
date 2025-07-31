#include "dxmt_names.hpp"
#include "com/com_object.hpp"
#include "com/com_pointer.hpp"
#include "d3d11_device.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include "dxmt_capture.hpp"
#include <exception>

namespace dxmt {
#ifndef VBOX
Logger Logger::s_instance("d3d11.log");
#endif

extern "C" HRESULT WINAPI
D3D11CoreCreateDevice(IDXGIFactory *pFactory, IDXGIAdapter *pAdapter,
                      UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels,
                      UINT FeatureLevels, ID3D11Device **ppDevice) {
  InitReturnPtr(ppDevice);

  Com<IMTLDXGIAdapter> dxgi_adapter;

  // Try to find the corresponding Metal device for the DXGI adapter
  if (FAILED(pAdapter->QueryInterface(IID_PPV_ARGS(&dxgi_adapter)))) {
    ERR("Not a DXMT adapter");
    return E_INVALIDARG;
  }

  // Feature levels to probe if the
  // application does not specify any.
  std::array<D3D_FEATURE_LEVEL, 6> defaultFeatureLevels = {
      D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,  D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1,
  };

  if (!pFeatureLevels || !FeatureLevels) {
    pFeatureLevels = defaultFeatureLevels.data();
    FeatureLevels = defaultFeatureLevels.size();
  }

  // so far stick to 11.1
  // Find the highest feature level supported by the device.
  // This works because the feature level array is ordered.
  D3D_FEATURE_LEVEL maxFeatureLevel = D3D_FEATURE_LEVEL_11_1;
  D3D_FEATURE_LEVEL minFeatureLevel = D3D_FEATURE_LEVEL();
  D3D_FEATURE_LEVEL devFeatureLevel = D3D_FEATURE_LEVEL();

  Logger::info(
      str::format("Maximum supported feature level: ", maxFeatureLevel));

  for (uint32_t flId = 0; flId < FeatureLevels; flId++) {
    minFeatureLevel = pFeatureLevels[flId];

    if (minFeatureLevel <= maxFeatureLevel) {
      devFeatureLevel = minFeatureLevel;
      break;
    }
  }

  if (!devFeatureLevel) {
    Logger::err(str::format("Minimum required feature level ", minFeatureLevel,
                            " not supported"));
    return E_INVALIDARG;
  }

  try {
    Logger::info(str::format("Using feature level ", devFeatureLevel));

    auto device = CreateD3D11Device(
        CreateDXMTDevice({.device = dxgi_adapter->GetMTLDevice()}),
        dxgi_adapter.ptr(), devFeatureLevel, Flags);

    return device->QueryInterface(IID_PPV_ARGS(ppDevice));
  } catch (const MTLD3DError &e) {
    Logger::err("D3D11CoreCreateDevice: Failed to create D3D11 device");
    return E_FAIL;
  }
}

extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    ID3D11DeviceContext **ppImmediateContext) {
  InitReturnPtr(ppDevice);
  InitReturnPtr(ppSwapChain);
  InitReturnPtr(ppImmediateContext);

  if (pFeatureLevel)
    *pFeatureLevel = D3D_FEATURE_LEVEL(0);

  Com<IDXGIFactory> dxgiFactory = nullptr;
  Com<IDXGIAdapter> dxgiAdapter = pAdapter;
  Com<ID3D11Device> device = nullptr;

  HRESULT hr;

  if (ppSwapChain && !pSwapChainDesc)
    return E_INVALIDARG;

  if (!pAdapter) {
    // Ignore DriverType
    if (DriverType != D3D_DRIVER_TYPE_HARDWARE)
      WARN("D3D11CreateDevice: Unsupported driver type ", DriverType);
    // We'll use the first adapter returned by a DXGI factory
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));

    if (FAILED(hr)) {
      Logger::err("D3D11CreateDevice: Failed to create a DXGI factory");
      return hr;
    }

    hr = dxgiFactory->EnumAdapters(0, &dxgiAdapter);

    if (FAILED(hr)) {
      Logger::err("D3D11CreateDevice: No default adapter available");
      return hr;
    }
  } else {
    // We should be able to query the DXGI factory from the adapter
    if (FAILED(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)))) {
      Logger::err(
          "D3D11CreateDevice: Failed to query DXGI factory from DXGI adapter");
      return E_INVALIDARG;
    }

    // In theory we could ignore these, but the Microsoft docs explicitly
    // state that we need to return E_INVALIDARG in case the arguments are
    // invalid. Both the driver type and software parameter can only be
    // set if the adapter itself is unspecified.
    // See:
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ff476082(v=vs.85).aspx
    if (DriverType != D3D_DRIVER_TYPE_UNKNOWN || Software)
      return E_INVALIDARG;
  }
  // Create the actual device
  hr = D3D11CoreCreateDevice(dxgiFactory.ptr(), dxgiAdapter.ptr(), Flags,
                             pFeatureLevels, FeatureLevels, &device);

  if (FAILED(hr))
    return hr;

  // Create the swap chain, if requested
  if (ppSwapChain) {
    DXGI_SWAP_CHAIN_DESC desc = *pSwapChainDesc;
    hr = dxgiFactory->CreateSwapChain(device.ptr(), &desc, ppSwapChain);

    if (FAILED(hr)) {
      Logger::err("D3D11CreateDevice: Failed to create swap chain");
      return hr;
    }
  }
  // Write back whatever info the application requested
  if (pFeatureLevel)
    *pFeatureLevel = device->GetFeatureLevel();

  if (ppDevice)
    *ppDevice = device.ref();

  if (ppImmediateContext)
    device->GetImmediateContext(ppImmediateContext);

  // If we were unable to write back the device and the
  // swap chain, the application has no way of working
  // with the device so we should report S_FALSE here.
  if (!ppDevice && !ppImmediateContext && !ppSwapChain)
    return S_FALSE;

  return S_OK;
}

#ifdef VBOX
__attribute__((visibility("default")))
#endif
extern "C" HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter *pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
    UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, ID3D11Device **ppDevice, D3D_FEATURE_LEVEL *pFeatureLevel,
    ID3D11DeviceContext **ppImmediateContext) {
  return D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags,
                                       pFeatureLevels, FeatureLevels,
                                       SDKVersion, nullptr, nullptr, ppDevice,
                                       pFeatureLevel, ppImmediateContext);
}

extern "C" HRESULT __stdcall D3D11On12CreateDevice(
    IUnknown *pDevice, UINT Flags, const D3D_FEATURE_LEVEL *pFeatureLevels,
    UINT FeatureLevels, IUnknown *const *ppCommandQueues, UINT NumQueues,
    UINT NodeMask, ID3D11Device **ppDevice,
    ID3D11DeviceContext **ppImmediateContext,
    D3D_FEATURE_LEVEL *pChosenFeatureLevel) {
  InitReturnPtr(ppDevice);
  InitReturnPtr(ppImmediateContext);
  return E_NOTIMPL;
}

} // namespace dxmt

#ifndef DXMT_NATIVE

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {  
  if (reason == DLL_PROCESS_DETACH) {
    dxmt::uninitialize_io_hook();
  }
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  dxmt::initialize_io_hook();
  DisableThreadLibraryCalls(instance);
  return TRUE;
}

#endif

extern "C" void _massert(const char *_Message, const char *_File,
                         unsigned _Line) {
  dxmt::Logger::err(dxmt::str::format("Assertion failed: ", _Message,
                                      "\nfile: ", _File, ":", _Line));
  std::terminate();
}

extern "C" void __cxa_pure_virtual() {
  dxmt::Logger::err(dxmt::str::format("Pure virtual function called"));
  __builtin_trap();
}
