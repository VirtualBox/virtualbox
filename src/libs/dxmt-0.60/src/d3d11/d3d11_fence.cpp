#include "d3d11_fence.hpp"
#include "d3d11_device_child.hpp"

namespace dxmt {

class MTLD3D11FenceImpl : public MTLD3D11DeviceChild<MTLD3D11Fence> {
public:
  MTLD3D11FenceImpl(MTLD3D11Device *pDevice)
      : MTLD3D11DeviceChild<MTLD3D11Fence>(pDevice) {
    event = pDevice->GetMTLDevice().newSharedEvent();
  };

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11Fence)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(ID3D11Query), riid)) {
      WARN("D3D11Fence: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  };

  HRESULT STDMETHODCALLTYPE
  CreateSharedHandle(const SECURITY_ATTRIBUTES *pAttributes, DWORD Access,
                     const WCHAR *Name, HANDLE *pHandle) final {
    return E_NOTIMPL;
  };

  UINT64 STDMETHODCALLTYPE GetCompletedValue() final {
    return event.signaledValue();
  };

  HRESULT STDMETHODCALLTYPE SetEventOnCompletion(UINT64 Value,
                                                 HANDLE Event) final {
    MTLSharedEvent_setWin32EventAtValue(event.handle, Event, Value);
    return S_OK;
  };
};

HRESULT
CreateFence(MTLD3D11Device *pDevice, UINT64 InitialValue,
            D3D11_FENCE_FLAG Flags, REFIID riid, void **ppFence) {
  // TODO: respect Flags
  auto fence = new MTLD3D11FenceImpl(pDevice);
  fence->event.signalValue(InitialValue);
  return fence->QueryInterface(riid, ppFence);
}

} // namespace dxmt