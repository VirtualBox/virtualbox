#pragma once

#include "d3d11_3.h"

namespace dxmt {

class ResourceSubset {
public:
  ResourceSubset(size_t numMips, size_t mostDetailedMipLevel,
                 size_t numArraySlices, size_t firstArraySlice,
                 size_t numPlanes, size_t firstPlane) noexcept;

  ResourceSubset(const D3D11_SHADER_RESOURCE_VIEW_DESC &Desc) noexcept;
  ResourceSubset(const D3D11_UNORDERED_ACCESS_VIEW_DESC &Desc) noexcept;
  ResourceSubset(const D3D11_RENDER_TARGET_VIEW_DESC &Desc) noexcept;

  ResourceSubset(const D3D11_SHADER_RESOURCE_VIEW_DESC1 &Desc) noexcept;
  ResourceSubset(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 &Desc) noexcept;
  ResourceSubset(const D3D11_RENDER_TARGET_VIEW_DESC1 &Desc) noexcept;
  ResourceSubset(const D3D11_DEPTH_STENCIL_VIEW_DESC &Desc) noexcept;

  bool CheckOverlap(const ResourceSubset &other) const noexcept;

private:
  size_t begin_array_;
  size_t end_array_;
  size_t begin_mip_;
  size_t end_mip_;
  size_t begin_plane_;
  size_t end_plane_;
};
} // namespace dxmt