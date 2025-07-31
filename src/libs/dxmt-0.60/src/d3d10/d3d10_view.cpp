#include "d3d10_view.hpp"

namespace dxmt {

void STDMETHODCALLTYPE
MTLD3D10ShaderResourceView::GetDesc(D3D10_SHADER_RESOURCE_VIEW_DESC *pDesc) {
  D3D11_SHADER_RESOURCE_VIEW_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->ViewDimension = D3D10_SRV_DIMENSION(d3d11_desc.ViewDimension);
  pDesc->Format = d3d11_desc.Format;

  switch (d3d11_desc.ViewDimension) {
  case D3D11_SRV_DIMENSION_BUFFER:
    pDesc->Buffer.FirstElement = d3d11_desc.Buffer.FirstElement;
    pDesc->Buffer.NumElements = d3d11_desc.Buffer.NumElements;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE1D:
    pDesc->Texture1D.MipLevels = d3d11_desc.Texture1D.MipLevels;
    pDesc->Texture1D.MostDetailedMip = d3d11_desc.Texture1D.MostDetailedMip;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
    pDesc->Texture1DArray.MipLevels = d3d11_desc.Texture1DArray.MipLevels;
    pDesc->Texture1DArray.MostDetailedMip = d3d11_desc.Texture1DArray.MostDetailedMip;
    pDesc->Texture1DArray.FirstArraySlice = d3d11_desc.Texture1DArray.FirstArraySlice;
    pDesc->Texture1DArray.ArraySize = d3d11_desc.Texture1DArray.ArraySize;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2D:
    pDesc->Texture2D.MipLevels = d3d11_desc.Texture2D.MipLevels;
    pDesc->Texture2D.MostDetailedMip = d3d11_desc.Texture2D.MostDetailedMip;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
    pDesc->Texture2DArray.MipLevels = d3d11_desc.Texture2DArray.MipLevels;
    pDesc->Texture2DArray.MostDetailedMip = d3d11_desc.Texture2DArray.MostDetailedMip;
    pDesc->Texture2DArray.FirstArraySlice = d3d11_desc.Texture2DArray.FirstArraySlice;
    pDesc->Texture2DArray.ArraySize = d3d11_desc.Texture2DArray.ArraySize;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2DMS:
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
    pDesc->Texture2DMSArray.FirstArraySlice = d3d11_desc.Texture2DMSArray.FirstArraySlice;
    pDesc->Texture2DMSArray.ArraySize = d3d11_desc.Texture2DMSArray.ArraySize;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE3D:
    pDesc->Texture3D.MipLevels = d3d11_desc.Texture3D.MipLevels;
    pDesc->Texture3D.MostDetailedMip = d3d11_desc.Texture3D.MostDetailedMip;
    break;
  case D3D11_SRV_DIMENSION_TEXTURECUBE:
    pDesc->TextureCube.MipLevels = d3d11_desc.TextureCube.MipLevels;
    pDesc->TextureCube.MostDetailedMip = d3d11_desc.TextureCube.MostDetailedMip;
    break;
  default:
    pDesc->ViewDimension = D3D10_SRV_DIMENSION_UNKNOWN;
    break;
  }
}

void STDMETHODCALLTYPE
MTLD3D10ShaderResourceView::GetDesc1(D3D10_SHADER_RESOURCE_VIEW_DESC1 *pDesc) {
  D3D11_SHADER_RESOURCE_VIEW_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->ViewDimension = D3D10_SRV_DIMENSION(d3d11_desc.ViewDimension);
  pDesc->Format = d3d11_desc.Format;

  switch (d3d11_desc.ViewDimension) {
  case D3D11_SRV_DIMENSION_BUFFER:
    pDesc->Buffer.FirstElement = d3d11_desc.Buffer.FirstElement;
    pDesc->Buffer.NumElements = d3d11_desc.Buffer.NumElements;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE1D:
    pDesc->Texture1D.MipLevels = d3d11_desc.Texture1D.MipLevels;
    pDesc->Texture1D.MostDetailedMip = d3d11_desc.Texture1D.MostDetailedMip;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
    pDesc->Texture1DArray.MipLevels = d3d11_desc.Texture1DArray.MipLevels;
    pDesc->Texture1DArray.MostDetailedMip = d3d11_desc.Texture1DArray.MostDetailedMip;
    pDesc->Texture1DArray.FirstArraySlice = d3d11_desc.Texture1DArray.FirstArraySlice;
    pDesc->Texture1DArray.ArraySize = d3d11_desc.Texture1DArray.ArraySize;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2D:
    pDesc->Texture2D.MipLevels = d3d11_desc.Texture2D.MipLevels;
    pDesc->Texture2D.MostDetailedMip = d3d11_desc.Texture2D.MostDetailedMip;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
    pDesc->Texture2DArray.MipLevels = d3d11_desc.Texture2DArray.MipLevels;
    pDesc->Texture2DArray.MostDetailedMip = d3d11_desc.Texture2DArray.MostDetailedMip;
    pDesc->Texture2DArray.FirstArraySlice = d3d11_desc.Texture2DArray.FirstArraySlice;
    pDesc->Texture2DArray.ArraySize = d3d11_desc.Texture2DArray.ArraySize;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2DMS:
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
    pDesc->Texture2DMSArray.FirstArraySlice = d3d11_desc.Texture2DMSArray.FirstArraySlice;
    pDesc->Texture2DMSArray.ArraySize = d3d11_desc.Texture2DMSArray.ArraySize;
    break;
  case D3D11_SRV_DIMENSION_TEXTURE3D:
    pDesc->Texture3D.MipLevels = d3d11_desc.Texture3D.MipLevels;
    pDesc->Texture3D.MostDetailedMip = d3d11_desc.Texture3D.MostDetailedMip;
    break;
  case D3D11_SRV_DIMENSION_TEXTURECUBE:
    pDesc->TextureCube.MipLevels = d3d11_desc.TextureCube.MipLevels;
    pDesc->TextureCube.MostDetailedMip = d3d11_desc.TextureCube.MostDetailedMip;
    break;
  case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
    pDesc->TextureCubeArray.MipLevels = d3d11_desc.TextureCubeArray.MipLevels;
    pDesc->TextureCubeArray.MostDetailedMip = d3d11_desc.TextureCubeArray.MostDetailedMip;
    pDesc->TextureCubeArray.First2DArrayFace = d3d11_desc.TextureCubeArray.First2DArrayFace;
    pDesc->TextureCubeArray.NumCubes = d3d11_desc.TextureCubeArray.NumCubes;
    break;
  default:
    pDesc->ViewDimension = D3D10_SRV_DIMENSION_UNKNOWN;
    break;
  }
}

void STDMETHODCALLTYPE
MTLD3D10RenderTargetView::GetDesc(D3D10_RENDER_TARGET_VIEW_DESC *pDesc) {
  D3D11_RENDER_TARGET_VIEW_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->ViewDimension = D3D10_RTV_DIMENSION(d3d11_desc.ViewDimension);
  pDesc->Format = d3d11_desc.Format;

  switch (d3d11_desc.ViewDimension) {
  case D3D11_RTV_DIMENSION_BUFFER:
    pDesc->Buffer.FirstElement = d3d11_desc.Buffer.FirstElement;
    pDesc->Buffer.NumElements = d3d11_desc.Buffer.NumElements;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1D:
    pDesc->Texture1D.MipSlice = d3d11_desc.Texture1D.MipSlice;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
    pDesc->Texture1DArray.MipSlice = d3d11_desc.Texture1DArray.MipSlice;
    pDesc->Texture1DArray.FirstArraySlice = d3d11_desc.Texture1DArray.FirstArraySlice;
    pDesc->Texture1DArray.ArraySize = d3d11_desc.Texture1DArray.ArraySize;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2D:
    pDesc->Texture2D.MipSlice = d3d11_desc.Texture2D.MipSlice;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
    pDesc->Texture2DArray.MipSlice = d3d11_desc.Texture2DArray.MipSlice;
    pDesc->Texture2DArray.FirstArraySlice = d3d11_desc.Texture2DArray.FirstArraySlice;
    pDesc->Texture2DArray.ArraySize = d3d11_desc.Texture2DArray.ArraySize;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DMS:
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
    pDesc->Texture2DMSArray.FirstArraySlice = d3d11_desc.Texture2DMSArray.FirstArraySlice;
    pDesc->Texture2DMSArray.ArraySize = d3d11_desc.Texture2DMSArray.ArraySize;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE3D:
    pDesc->Texture3D.MipSlice = d3d11_desc.Texture3D.MipSlice;
    pDesc->Texture3D.FirstWSlice = d3d11_desc.Texture3D.FirstWSlice;
    pDesc->Texture3D.WSize = d3d11_desc.Texture3D.WSize;
    break;
  default:
    pDesc->ViewDimension = D3D10_RTV_DIMENSION_UNKNOWN;
    break;
  }
}

void STDMETHODCALLTYPE
MTLD3D10DepthStencilView::GetDesc(D3D10_DEPTH_STENCIL_VIEW_DESC *pDesc) {
  D3D11_DEPTH_STENCIL_VIEW_DESC d3d11_desc;
  d3d11->GetDesc(&d3d11_desc);

  pDesc->ViewDimension = D3D10_DSV_DIMENSION(d3d11_desc.ViewDimension);
  pDesc->Format = d3d11_desc.Format;

  switch (d3d11_desc.ViewDimension) {
  case D3D11_DSV_DIMENSION_TEXTURE1D:
    pDesc->Texture1D.MipSlice = d3d11_desc.Texture1D.MipSlice;
    break;
  case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
    pDesc->Texture1DArray.MipSlice = d3d11_desc.Texture1DArray.MipSlice;
    pDesc->Texture1DArray.FirstArraySlice = d3d11_desc.Texture1DArray.FirstArraySlice;
    pDesc->Texture1DArray.ArraySize = d3d11_desc.Texture1DArray.ArraySize;
    break;
  case D3D11_DSV_DIMENSION_TEXTURE2D:
    pDesc->Texture2D.MipSlice = d3d11_desc.Texture2D.MipSlice;
    break;
  case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
    pDesc->Texture2DArray.MipSlice = d3d11_desc.Texture2DArray.MipSlice;
    pDesc->Texture2DArray.FirstArraySlice = d3d11_desc.Texture2DArray.FirstArraySlice;
    pDesc->Texture2DArray.ArraySize = d3d11_desc.Texture2DArray.ArraySize;
    break;
  case D3D11_DSV_DIMENSION_TEXTURE2DMS:
    break;
  case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
    pDesc->Texture2DMSArray.FirstArraySlice = d3d11_desc.Texture2DMSArray.FirstArraySlice;
    pDesc->Texture2DMSArray.ArraySize = d3d11_desc.Texture2DMSArray.ArraySize;
    break;
  default:
    pDesc->ViewDimension = D3D10_DSV_DIMENSION_UNKNOWN;
    break;
  }
}

} // namespace dxmt