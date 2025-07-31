#include "d3d11_texture.hpp"
#include "mtld11_resource.hpp"

namespace dxmt {

template <>
void InitializeTextureData(MTLD3D11Device *pDevice, WMT::Texture target,
                           const D3D11_TEXTURE3D_DESC1 &Desc,
                           const D3D11_SUBRESOURCE_DATA *subresources) {
  if (subresources == nullptr)
    return;
  uint32_t width = Desc.Width;
  uint32_t height = Desc.Height;
  uint32_t depth = Desc.Depth;
  for (uint32_t level = 0; level < Desc.MipLevels; level++) {
    target.replaceRegion({0, 0, 0}, {width, height, depth}, level, 0,
                         subresources[level].pSysMem,
                         subresources[level].SysMemPitch,
                         subresources[level].SysMemSlicePitch);
    width = std::max(1u, width >> 1);
    height = std::max(1u, height >> 1);
    depth = std::max(1u, depth >> 1);
  }
};

template <>
void InitializeTextureData(MTLD3D11Device *pDevice, WMT::Texture target,
                           const D3D11_TEXTURE2D_DESC1 &Desc,
                           const D3D11_SUBRESOURCE_DATA *subresources) {
  if (subresources == nullptr)
    return;
  uint32_t width = Desc.Width;
  uint32_t height = Desc.Height;
  for (uint32_t level = 0; level < Desc.MipLevels; level++) {
    uint32_t bytes_per_row = 0, _, __;
    GetLinearTextureLayout(pDevice, Desc, level, bytes_per_row, _, __, false);
    for (uint32_t slice = 0; slice < Desc.ArraySize; slice++) {
      auto idx = D3D11CalcSubresource(level, slice, Desc.MipLevels);
      if (subresources[idx].SysMemPitch < bytes_per_row) {
        WARN("RowPitch provided ", subresources[idx].SysMemPitch,
             ", expect at least ", bytes_per_row);
      }
      target.replaceRegion(
          {0, 0, 0}, {width, height, 1}, level, slice,
          subresources[idx].pSysMem,
          std::max(subresources[idx].SysMemPitch, bytes_per_row), 0);
    }
    width = std::max(1u, width >> 1);
    height = std::max(1u, height >> 1);
  }
};

template <>
void InitializeTextureData(MTLD3D11Device *pDevice, WMT::Texture target,
                           const D3D11_TEXTURE1D_DESC &Desc,
                           const D3D11_SUBRESOURCE_DATA *subresources) {
  if (subresources == nullptr)
    return;
  uint32_t width = Desc.Width;
  for (uint32_t level = 0; level < Desc.MipLevels; level++) {
    for (uint32_t slice = 0; slice < Desc.ArraySize; slice++) {
      auto idx = D3D11CalcSubresource(level, slice, Desc.MipLevels);
      uint32_t bytes_per_row = 0, _, __;
      GetLinearTextureLayout(pDevice, Desc, level, bytes_per_row, _, __, false);
      target.replaceRegion(
          {0, 0, 0}, {width, 1, 1}, level, slice, subresources[idx].pSysMem,
          std::max(subresources[idx].SysMemPitch, bytes_per_row), 0);
    }
    width = std::max(1u, width >> 1);
  }
};

} // namespace dxmt