#pragma once
#include "d3d11_device.hpp"

namespace dxmt {
template <typename TEXTURE_DESC>
void InitializeTextureData(
    MTLD3D11Device *pDevice, WMT::Texture target, const TEXTURE_DESC &Desc, const D3D11_SUBRESOURCE_DATA *subresources
);
} // namespace dxmt