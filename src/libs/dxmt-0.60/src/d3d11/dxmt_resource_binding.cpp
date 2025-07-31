#include "dxmt_resource_binding.hpp"
#include <cassert>
#include "d3d11_private.h"

#define ASSUME(x) D3D11_ASSERT(x)

namespace dxmt {

ResourceSubset::ResourceSubset(size_t numMips, size_t mostDetailedMipLevel,
                               size_t numArraySlices, size_t firstArraySlice,
                               size_t numPlanes, size_t firstPlane) noexcept
    : begin_array_(firstArraySlice),
      end_array_(firstArraySlice + numArraySlices),
      begin_mip_(mostDetailedMipLevel),
      end_mip_(numMips + mostDetailedMipLevel), begin_plane_(firstPlane),
      end_plane_(firstPlane + numPlanes) {}

bool ResourceSubset::CheckOverlap(const ResourceSubset& other) const noexcept {
  return false;
}

ResourceSubset::ResourceSubset(
    const D3D11_SHADER_RESOURCE_VIEW_DESC &Desc) noexcept
    : begin_array_(0), end_array_(1), begin_mip_(0), end_mip_(1),
      begin_plane_(0), end_plane_(1) {
  switch (Desc.ViewDimension) {
  default:
    break;

  case (D3D11_SRV_DIMENSION_BUFFER):
  case (D3D11_SRV_DIMENSION_BUFFEREX):
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE1D):
    begin_mip_ = UINT8(Desc.Texture1D.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture1D.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE1DARRAY):
    begin_array_ = UINT16(Desc.Texture1DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture1DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture1DArray.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture1DArray.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2D):
    begin_mip_ = UINT8(Desc.Texture2D.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture2D.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2DARRAY):
    begin_array_ = UINT16(Desc.Texture2DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture2DArray.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture2DArray.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2DMS):
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY):
    begin_array_ = UINT16(Desc.Texture2DMSArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DMSArray.ArraySize);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE3D):
    end_array_ = UINT16(-1); // all slices
    begin_mip_ = UINT8(Desc.Texture3D.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture3D.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURECUBE):
    begin_mip_ = UINT8(Desc.TextureCube.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.TextureCube.MipLevels);
    begin_array_ = 0;
    end_array_ = 6;
    break;

  case (D3D11_SRV_DIMENSION_TEXTURECUBEARRAY):
    begin_array_ = UINT16(Desc.TextureCubeArray.First2DArrayFace);
    end_array_ = UINT16(begin_array_ + Desc.TextureCubeArray.NumCubes * 6);
    begin_mip_ = UINT8(Desc.TextureCubeArray.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.TextureCubeArray.MipLevels);
    break;
  }
}

ResourceSubset::ResourceSubset(
    const D3D11_UNORDERED_ACCESS_VIEW_DESC &Desc) noexcept
    : begin_array_(0), end_array_(1), begin_mip_(0), end_mip_(1),
      begin_plane_(0), end_plane_(1) {
  switch (Desc.ViewDimension) {
  default:
    ASSUME(0 && "Corrupt Resource Type on Unordered Access View");
    break;

  case (D3D11_UAV_DIMENSION_BUFFER):
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE1D):
    begin_mip_ = UINT8(Desc.Texture1D.MipSlice);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE1DARRAY):
    begin_array_ = UINT16(Desc.Texture1DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture1DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture1DArray.MipSlice);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE2D):
    begin_mip_ = UINT8(Desc.Texture2D.MipSlice);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE2DARRAY):
    begin_array_ = UINT16(Desc.Texture2DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture2DArray.MipSlice);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE3D):
    begin_array_ = UINT16(Desc.Texture3D.FirstWSlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture3D.WSize);
    begin_mip_ = UINT8(Desc.Texture3D.MipSlice);
    break;
  }

  end_mip_ = begin_mip_ + 1;
}

ResourceSubset::ResourceSubset(
    const D3D11_RENDER_TARGET_VIEW_DESC &Desc) noexcept
    : begin_array_(0), end_array_(1), begin_mip_(0), end_mip_(1),
      begin_plane_(0), end_plane_(1) {
  switch (Desc.ViewDimension) {
  default:
    ASSUME(0 && "Corrupt Resource Type on Render Target View");
    break;

  case (D3D11_RTV_DIMENSION_BUFFER):
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE1D):
    begin_mip_ = UINT8(Desc.Texture1D.MipSlice);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE1DARRAY):
    begin_array_ = UINT16(Desc.Texture1DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture1DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture1DArray.MipSlice);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2D):
    begin_mip_ = UINT8(Desc.Texture2D.MipSlice);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2DMS):
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2DARRAY):
    begin_array_ = UINT16(Desc.Texture2DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture2DArray.MipSlice);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY):
    begin_array_ = UINT16(Desc.Texture2DMSArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DMSArray.ArraySize);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE3D):
    begin_array_ = UINT16(Desc.Texture3D.FirstWSlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture3D.WSize);
    begin_mip_ = UINT8(Desc.Texture3D.MipSlice);
    break;
  }

  end_mip_ = begin_mip_ + 1;
}

ResourceSubset::ResourceSubset(
    const D3D11_SHADER_RESOURCE_VIEW_DESC1 &Desc) noexcept
    : begin_array_(0), end_array_(1), begin_mip_(0), end_mip_(1),
      begin_plane_(0), end_plane_(1) {
  switch (Desc.ViewDimension) {
  default:
    break;

  case (D3D11_SRV_DIMENSION_BUFFER):
  case (D3D11_SRV_DIMENSION_BUFFEREX):
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE1D):
    begin_mip_ = UINT8(Desc.Texture1D.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture1D.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE1DARRAY):
    begin_array_ = UINT16(Desc.Texture1DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture1DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture1DArray.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture1DArray.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2D):
    begin_mip_ = UINT8(Desc.Texture2D.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture2D.MipLevels);
    begin_plane_ = UINT8(Desc.Texture2D.PlaneSlice);
    end_plane_ = UINT8(Desc.Texture2D.PlaneSlice + 1);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2DARRAY):
    begin_array_ = UINT16(Desc.Texture2DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture2DArray.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture2DArray.MipLevels);
    begin_plane_ = UINT8(Desc.Texture2DArray.PlaneSlice);
    end_plane_ = UINT8(Desc.Texture2DArray.PlaneSlice + 1);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2DMS):
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY):
    begin_array_ = UINT16(Desc.Texture2DMSArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DMSArray.ArraySize);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURE3D):
    end_array_ = UINT16(-1); // all slices
    begin_mip_ = UINT8(Desc.Texture3D.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.Texture3D.MipLevels);
    break;

  case (D3D11_SRV_DIMENSION_TEXTURECUBE):
    begin_mip_ = UINT8(Desc.TextureCube.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.TextureCube.MipLevels);
    begin_array_ = 0;
    end_array_ = 6;
    break;

  case (D3D11_SRV_DIMENSION_TEXTURECUBEARRAY):
    begin_array_ = UINT16(Desc.TextureCubeArray.First2DArrayFace);
    end_array_ = UINT16(begin_array_ + Desc.TextureCubeArray.NumCubes * 6);
    begin_mip_ = UINT8(Desc.TextureCubeArray.MostDetailedMip);
    end_mip_ = UINT8(begin_mip_ + Desc.TextureCubeArray.MipLevels);
    break;
  }
}

ResourceSubset::ResourceSubset(
    const D3D11_UNORDERED_ACCESS_VIEW_DESC1 &Desc) noexcept
    : begin_array_(0), end_array_(1), begin_mip_(0), end_mip_(1),
      begin_plane_(0), end_plane_(1) {
  switch (Desc.ViewDimension) {
  default:
    ASSUME(0 && "Corrupt Resource Type on Unordered Access View");
    break;

  case (D3D11_UAV_DIMENSION_BUFFER):
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE1D):
    begin_mip_ = UINT8(Desc.Texture1D.MipSlice);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE1DARRAY):
    begin_array_ = UINT16(Desc.Texture1DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture1DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture1DArray.MipSlice);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE2D):
    begin_mip_ = UINT8(Desc.Texture2D.MipSlice);
    begin_plane_ = UINT8(Desc.Texture2D.PlaneSlice);
    end_plane_ = UINT8(Desc.Texture2D.PlaneSlice + 1);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE2DARRAY):
    begin_array_ = UINT16(Desc.Texture2DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture2DArray.MipSlice);
    begin_plane_ = UINT8(Desc.Texture2DArray.PlaneSlice);
    end_plane_ = UINT8(Desc.Texture2DArray.PlaneSlice + 1);
    break;

  case (D3D11_UAV_DIMENSION_TEXTURE3D):
    begin_array_ = UINT16(Desc.Texture3D.FirstWSlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture3D.WSize);
    begin_mip_ = UINT8(Desc.Texture3D.MipSlice);
    break;
  }

  end_mip_ = begin_mip_ + 1;
}

ResourceSubset::ResourceSubset(
    const D3D11_RENDER_TARGET_VIEW_DESC1 &Desc) noexcept
    : begin_array_(0), end_array_(1), begin_mip_(0), end_mip_(1),
      begin_plane_(0), end_plane_(1) {
  switch (Desc.ViewDimension) {
  default:
    ASSUME(0 && "Corrupt Resource Type on Render Target View");
    break;

  case (D3D11_RTV_DIMENSION_BUFFER):
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE1D):
    begin_mip_ = UINT8(Desc.Texture1D.MipSlice);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE1DARRAY):
    begin_array_ = UINT16(Desc.Texture1DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture1DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture1DArray.MipSlice);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2D):
    begin_mip_ = UINT8(Desc.Texture2D.MipSlice);
    begin_plane_ = UINT8(Desc.Texture2D.PlaneSlice);
    end_plane_ = UINT8(Desc.Texture2D.PlaneSlice + 1);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2DMS):
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2DARRAY):
    begin_array_ = UINT16(Desc.Texture2DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture2DArray.MipSlice);
    begin_plane_ = UINT8(Desc.Texture2DArray.PlaneSlice);
    end_plane_ = UINT8(Desc.Texture2DArray.PlaneSlice + 1);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY):
    begin_array_ = UINT16(Desc.Texture2DMSArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DMSArray.ArraySize);
    break;

  case (D3D11_RTV_DIMENSION_TEXTURE3D):
    begin_array_ = UINT16(Desc.Texture3D.FirstWSlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture3D.WSize);
    begin_mip_ = UINT8(Desc.Texture3D.MipSlice);
    break;
  }

  end_mip_ = begin_mip_ + 1;
}

ResourceSubset::ResourceSubset(
    const D3D11_DEPTH_STENCIL_VIEW_DESC &Desc) noexcept
    : begin_array_(0), end_array_(1), begin_mip_(0), end_mip_(1),
      begin_plane_(0), end_plane_(1) {
  switch (Desc.ViewDimension) {
  default:
    ASSUME(0 && "Corrupt Resource Type on Depth Stencil View");
    break;

  case (D3D11_DSV_DIMENSION_TEXTURE1D):
    begin_mip_ = UINT8(Desc.Texture1D.MipSlice);
    break;

  case (D3D11_DSV_DIMENSION_TEXTURE1DARRAY):
    begin_array_ = UINT16(Desc.Texture1DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture1DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture1DArray.MipSlice);
    break;

  case (D3D11_DSV_DIMENSION_TEXTURE2D):
    begin_mip_ = UINT8(Desc.Texture2D.MipSlice);
    break;

  case (D3D11_DSV_DIMENSION_TEXTURE2DMS):
    break;

  case (D3D11_DSV_DIMENSION_TEXTURE2DARRAY):
    begin_array_ = UINT16(Desc.Texture2DArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DArray.ArraySize);
    begin_mip_ = UINT8(Desc.Texture2DArray.MipSlice);
    break;

  case (D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY):
    begin_array_ = UINT16(Desc.Texture2DMSArray.FirstArraySlice);
    end_array_ = UINT16(begin_array_ + Desc.Texture2DMSArray.ArraySize);
    break;
  }

  end_mip_ = begin_mip_ + 1;
}

} // namespace dxmt