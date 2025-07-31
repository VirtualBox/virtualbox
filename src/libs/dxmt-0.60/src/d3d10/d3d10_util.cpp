#include "d3d10_util.hpp"
#include "com/com_pointer.hpp"

namespace dxmt {

void
GetD3D10Device(ID3D11DeviceChild *d3d11, ID3D10Device **ppDevice) {
  Com<ID3D11Device> d3d11_device;
  d3d11->GetDevice(&d3d11_device);
  d3d11_device->QueryInterface(IID_PPV_ARGS(ppDevice));
}

void
GetD3D10Resource(ID3D11View *d3d11, ID3D10Resource **ppResource) {
  Com<ID3D11Resource> d3d11_resource;
  d3d11->GetResource(&d3d11_resource);
  d3d11_resource->QueryInterface(IID_PPV_ARGS(ppResource));
}

UINT
ConvertD3D11ResourceFlags(UINT MiscFlags) {
  UINT ret = 0;
  if (MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
    ret |= D3D10_RESOURCE_MISC_GENERATE_MIPS;
  if (MiscFlags & D3D11_RESOURCE_MISC_SHARED)
    ret |= D3D10_RESOURCE_MISC_SHARED;
  if (MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
    ret |= D3D10_RESOURCE_MISC_TEXTURECUBE;
  if (MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)
    ret |= D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  if (MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE)
    ret |= D3D10_RESOURCE_MISC_GDI_COMPATIBLE;
  return ret;
}

UINT
ConvertD3D10ResourceFlags(UINT MiscFlags) {
  UINT ret = 0;
  if (MiscFlags & D3D10_RESOURCE_MISC_GENERATE_MIPS)
    ret |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
  if (MiscFlags & D3D10_RESOURCE_MISC_SHARED)
    ret |= D3D11_RESOURCE_MISC_SHARED;
  if (MiscFlags & D3D10_RESOURCE_MISC_TEXTURECUBE)
    ret |= D3D11_RESOURCE_MISC_TEXTURECUBE;
  if (MiscFlags & D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX)
    ret |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
  if (MiscFlags & D3D10_RESOURCE_MISC_GDI_COMPATIBLE)
    ret |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
  return ret;
}

} // namespace dxmt