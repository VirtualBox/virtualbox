#pragma once
#include "d3d11_device.hpp"

namespace dxmt {

struct MTLD3D11Fence : public ID3D11Fence {
  WMT::Reference<WMT::SharedEvent> event;
};

HRESULT
CreateFence(MTLD3D11Device *pDevice, UINT64 InitialValue,
            D3D11_FENCE_FLAG Flags, REFIID riid, void **ppFence);

} // namespace dxmt