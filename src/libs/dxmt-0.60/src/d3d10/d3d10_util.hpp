#pragma once

#include "d3d11.h"

namespace dxmt {

void GetD3D10Device(ID3D11DeviceChild *d3d11, ID3D10Device **ppDevice);

void GetD3D10Resource(ID3D11View *d3d11, ID3D10Resource **ppResource);

UINT ConvertD3D11ResourceFlags(UINT MiscFlags);

UINT ConvertD3D10ResourceFlags(UINT MiscFlags);

} // namespace dxmt