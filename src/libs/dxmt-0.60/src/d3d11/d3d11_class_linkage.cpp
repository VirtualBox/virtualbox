
#include "d3d11_class_linkage.hpp"
#include "com/com_guid.hpp"
#include "com/com_pointer.hpp"
#include "d3d11_device.hpp"
#include "log/log.hpp"
#include "util_string.hpp"

namespace dxmt {

MTLD3D11ClassLinkage::MTLD3D11ClassLinkage(MTLD3D11Device *pDevice)
    : MTLD3D11DeviceChild<ID3D11ClassLinkage>(pDevice) {}

MTLD3D11ClassLinkage::~MTLD3D11ClassLinkage() {}

HRESULT STDMETHODCALLTYPE
MTLD3D11ClassLinkage::QueryInterface(REFIID riid, void **ppvObject) {
  if (ppvObject == nullptr)
    return E_POINTER;

  *ppvObject = nullptr;

  if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
      riid == __uuidof(ID3D11ClassLinkage)) {
    *ppvObject = ref(this);
    return S_OK;
  }

  if (logQueryInterfaceError(__uuidof(ID3D11ClassLinkage), riid)) {
    Logger::warn("D3D11ClassLinkage::QueryInterface: Unknown interface query");
    Logger::warn(str::format(riid));
  }

  return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE MTLD3D11ClassLinkage::CreateClassInstance(
    LPCSTR pClassTypeName, UINT ConstantBufferOffset, UINT ConstantVectorOffset,
    UINT TextureOffset, UINT SamplerOffset, ID3D11ClassInstance **ppInstance) {
  InitReturnPtr(ppInstance);

  ERR("Not implemented yet");
  return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MTLD3D11ClassLinkage::GetClassInstance(
    LPCSTR pClassInstanceName, UINT InstanceIndex,
    ID3D11ClassInstance **ppInstance) {
  ERR("Not implemented yet");
  return E_NOTIMPL;
}

} // namespace dxmt
