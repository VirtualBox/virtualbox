#include "d3d11_device.hpp"
#include "d3d11_private.h"
#include "dxmt_format.hpp"
#include "mtld11_resource.hpp"
#include "d3d11_1.h"
#include "util_math.hpp"

namespace dxmt {

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_BUFFER_DESC,
                                             D3D11_SHADER_RESOURCE_VIEW_DESC1>(
    const D3D11_BUFFER_DESC *pResourceDesc,
    D3D11_SHADER_RESOURCE_VIEW_DESC1 *pViewDescOut) {
  if (pResourceDesc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
    pViewDescOut->Format = DXGI_FORMAT_UNKNOWN;
    pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    pViewDescOut->Buffer.FirstElement = 0;
    pViewDescOut->Buffer.NumElements =
        pResourceDesc->ByteWidth / pResourceDesc->StructureByteStride;
  } else {
    return E_FAIL;
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_BUFFER_DESC,
                                             D3D11_UNORDERED_ACCESS_VIEW_DESC1>(
    const D3D11_BUFFER_DESC *pResourceDesc,
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pViewDescOut) {
  if (pResourceDesc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) {
    pViewDescOut->Format = DXGI_FORMAT_UNKNOWN;
    pViewDescOut->ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    pViewDescOut->Buffer.FirstElement = 0;
    pViewDescOut->Buffer.NumElements =
        pResourceDesc->ByteWidth / pResourceDesc->StructureByteStride;
    pViewDescOut->Buffer.Flags = 0;
  } else {
    return E_FAIL;
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE1D_DESC,
                                             D3D11_SHADER_RESOURCE_VIEW_DESC1>(
    const D3D11_TEXTURE1D_DESC *pResourceDesc,
    D3D11_SHADER_RESOURCE_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;

  if (pResourceDesc->ArraySize == 1) {
    pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
    pViewDescOut->Texture1D.MostDetailedMip = 0;
    pViewDescOut->Texture1D.MipLevels = pResourceDesc->MipLevels;
  } else {
    pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
    pViewDescOut->Texture1DArray.MostDetailedMip = 0;
    pViewDescOut->Texture1DArray.MipLevels = pResourceDesc->MipLevels;
    pViewDescOut->Texture1DArray.FirstArraySlice = 0;
    pViewDescOut->Texture1DArray.ArraySize = pResourceDesc->ArraySize;
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE1D_DESC,
                                             D3D11_UNORDERED_ACCESS_VIEW_DESC1>(
    const D3D11_TEXTURE1D_DESC *pResourceDesc,
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;

  if (pResourceDesc->ArraySize == 1) {
    pViewDescOut->ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D;
    pViewDescOut->Texture1D.MipSlice = 0;
  } else {
    pViewDescOut->ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1DARRAY;
    pViewDescOut->Texture1DArray.MipSlice = 0;
    pViewDescOut->Texture1DArray.FirstArraySlice = 0;
    pViewDescOut->Texture1DArray.ArraySize = pResourceDesc->ArraySize;
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE1D_DESC,
                                             D3D11_RENDER_TARGET_VIEW_DESC1>(
    const D3D11_TEXTURE1D_DESC *pResourceDesc,
    D3D11_RENDER_TARGET_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;
  if (pResourceDesc->ArraySize == 1) {
    pViewDescOut->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1D;
    pViewDescOut->Texture1D.MipSlice = 0;
  } else {
    pViewDescOut->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE1DARRAY;
    pViewDescOut->Texture1DArray.MipSlice = 0;
    pViewDescOut->Texture1DArray.FirstArraySlice = 0;
    pViewDescOut->Texture1DArray.ArraySize = pResourceDesc->ArraySize;
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE1D_DESC,
                                             D3D11_DEPTH_STENCIL_VIEW_DESC>(
    const D3D11_TEXTURE1D_DESC *pResourceDesc,
    D3D11_DEPTH_STENCIL_VIEW_DESC *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;

  if (pResourceDesc->ArraySize == 1) {
    pViewDescOut->ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1D;
    pViewDescOut->Texture1D.MipSlice = 0;
  } else {
    pViewDescOut->ViewDimension = D3D11_DSV_DIMENSION_TEXTURE1DARRAY;
    pViewDescOut->Texture1DArray.MipSlice = 0;
    pViewDescOut->Texture1DArray.FirstArraySlice = 0;
    pViewDescOut->Texture1DArray.ArraySize = pResourceDesc->ArraySize;
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE2D_DESC1,
                                             D3D11_SHADER_RESOURCE_VIEW_DESC1>(
    const D3D11_TEXTURE2D_DESC1 *pResourceDesc,
    D3D11_SHADER_RESOURCE_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;

  if (pResourceDesc->SampleDesc.Count == 1) {
    if (pResourceDesc->ArraySize == 1) {
      pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      pViewDescOut->Texture2D.MostDetailedMip = 0;
      pViewDescOut->Texture2D.MipLevels = pResourceDesc->MipLevels;
      pViewDescOut->Texture2D.PlaneSlice = 0;
    } else if (pResourceDesc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) {
      auto texture_cube_array_length = pResourceDesc->ArraySize / 6;
      if (texture_cube_array_length > 1) {
        pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        pViewDescOut->TextureCubeArray.MostDetailedMip = 0;
        pViewDescOut->TextureCubeArray.MipLevels = pResourceDesc->MipLevels;
        pViewDescOut->TextureCubeArray.First2DArrayFace = 0;
        pViewDescOut->TextureCubeArray.NumCubes = texture_cube_array_length;
      } else {
        pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        pViewDescOut->TextureCube.MostDetailedMip = 0;
        pViewDescOut->TextureCube.MipLevels = pResourceDesc->MipLevels;
      }
    } else {
      pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
      pViewDescOut->Texture2DArray.MostDetailedMip = 0;
      pViewDescOut->Texture2DArray.MipLevels = pResourceDesc->MipLevels;
      pViewDescOut->Texture2DArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DArray.ArraySize = pResourceDesc->ArraySize;
      pViewDescOut->Texture2DArray.PlaneSlice = 0;
    }
  } else {
    if (pResourceDesc->ArraySize == 1) {
      pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    } else {
      pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
      pViewDescOut->Texture2DMSArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DMSArray.ArraySize = pResourceDesc->ArraySize;
    }
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE2D_DESC1,
                                             D3D11_UNORDERED_ACCESS_VIEW_DESC1>(
    const D3D11_TEXTURE2D_DESC1 *pResourceDesc,
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;

  if (pResourceDesc->SampleDesc.Count == 1) {
    if (pResourceDesc->ArraySize == 1) {
      pViewDescOut->ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      pViewDescOut->Texture2D.MipSlice = 0;
      pViewDescOut->Texture2D.PlaneSlice = 0;
    } else {
      pViewDescOut->ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
      pViewDescOut->Texture2DArray.MipSlice = 0;
      pViewDescOut->Texture2DArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DArray.ArraySize = pResourceDesc->ArraySize;
      pViewDescOut->Texture2DArray.PlaneSlice = 0;
    }
  } else {
    ERR("Invalid to create DSV from muiltisampled texture");
    return E_FAIL;
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE2D_DESC1,
                                             D3D11_DEPTH_STENCIL_VIEW_DESC>(
    const D3D11_TEXTURE2D_DESC1 *pResourceDesc,
    D3D11_DEPTH_STENCIL_VIEW_DESC *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;
  if (pResourceDesc->SampleDesc.Count == 1) {
    if (pResourceDesc->ArraySize == 1) {
      pViewDescOut->ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
      pViewDescOut->Texture2D.MipSlice = 0;
    } else {
      pViewDescOut->ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
      pViewDescOut->Texture2DArray.MipSlice = 0;
      pViewDescOut->Texture2DArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DArray.ArraySize = pResourceDesc->ArraySize;
    }
  } else {
    if (pResourceDesc->ArraySize == 1) {
      pViewDescOut->ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    } else {
      pViewDescOut->ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY;
      pViewDescOut->Texture2DMSArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DMSArray.ArraySize = pResourceDesc->ArraySize;
    }
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE2D_DESC1,
                                             D3D11_RENDER_TARGET_VIEW_DESC1>(
    const D3D11_TEXTURE2D_DESC1 *pResourceDesc,
    D3D11_RENDER_TARGET_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;
  if (pResourceDesc->SampleDesc.Count == 1) {
    if (pResourceDesc->ArraySize == 1) {
      pViewDescOut->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      pViewDescOut->Texture2D.MipSlice = 0;
      pViewDescOut->Texture2D.PlaneSlice = 0;
    } else {
      pViewDescOut->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
      pViewDescOut->Texture2DArray.MipSlice = 0;
      pViewDescOut->Texture2DArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DArray.ArraySize = pResourceDesc->ArraySize;
      pViewDescOut->Texture2DArray.PlaneSlice = 0;
    }
  } else {
    if (pResourceDesc->ArraySize == 1) {
      pViewDescOut->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
    } else {
      pViewDescOut->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
      pViewDescOut->Texture2DMSArray.FirstArraySlice = 0;
      pViewDescOut->Texture2DMSArray.ArraySize = pResourceDesc->ArraySize;
    }
  }
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE3D_DESC1,
                                             D3D11_SHADER_RESOURCE_VIEW_DESC1>(
    const D3D11_TEXTURE3D_DESC1 *pResourceDesc,
    D3D11_SHADER_RESOURCE_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;

  pViewDescOut->ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
  pViewDescOut->Texture3D.MostDetailedMip = 0;
  pViewDescOut->Texture3D.MipLevels = pResourceDesc->MipLevels;
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE3D_DESC1,
                                             D3D11_UNORDERED_ACCESS_VIEW_DESC1>(
    const D3D11_TEXTURE3D_DESC1 *pResourceDesc,
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;
  pViewDescOut->ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
  pViewDescOut->Texture3D.MipSlice = 0;
  pViewDescOut->Texture3D.FirstWSlice = 0;
  pViewDescOut->Texture3D.WSize = pResourceDesc->Depth;
  return S_OK;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE3D_DESC1,
                                             D3D11_DEPTH_STENCIL_VIEW_DESC>(
    const D3D11_TEXTURE3D_DESC1 *pResourceDesc,
    D3D11_DEPTH_STENCIL_VIEW_DESC *pViewDescOut) {
  ERR("Invalid to create DSV from Texture3D");
  return E_FAIL;
}

template <>
HRESULT ExtractEntireResourceViewDescription<D3D11_TEXTURE3D_DESC1,
                                             D3D11_RENDER_TARGET_VIEW_DESC1>(
    const D3D11_TEXTURE3D_DESC1 *pResourceDesc,
    D3D11_RENDER_TARGET_VIEW_DESC1 *pViewDescOut) {
  pViewDescOut->Format = pResourceDesc->Format;
  pViewDescOut->ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
  pViewDescOut->Texture3D.MipSlice = 0;
  pViewDescOut->Texture3D.FirstWSlice = 0;
  pViewDescOut->Texture3D.WSize = pResourceDesc->Depth;
  return S_OK;
}

HRESULT
CreateMTLTextureDescriptorInternal(
    MTLD3D11Device *pDevice, D3D11_RESOURCE_DIMENSION Dimension, UINT Width,
    UINT Height, UINT Depth, UINT ArraySize, UINT SampleCount, UINT BindFlags,
    UINT CPUAccessFlags, UINT MiscFlags, D3D11_USAGE Usage, UINT MipLevels,
    DXGI_FORMAT Format, WMTTextureInfo *pDescOut) {
  if (Width == 0 || Height == 0 || Depth == 0 || ArraySize == 0 || SampleCount == 0)
    return E_INVALIDARG;

  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), Format, metal_format))) {
    ERR("CreateMTLTextureDescriptorInternal: creating a texture of invalid "
        "format: ",
        Format);
    return E_FAIL;
  }

  if (BindFlags & D3D11_BIND_DEPTH_STENCIL) {
    switch (Format) {
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
      metal_format.PixelFormat = WMTPixelFormatDepth32Float;
      break;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
      metal_format.PixelFormat = WMTPixelFormatDepth16Unorm;
      break;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_R24G8_TYPELESS:
      break;
    default:
      ERR("Unhandled depth stencil format ", Format);
      break;
    }
  }
  pDescOut->pixel_format = metal_format.PixelFormat;

  WMTTextureUsage metal_usage = (WMTTextureUsage)0; // actually corresponding to BindFlags

  if (BindFlags & (D3D11_BIND_CONSTANT_BUFFER | D3D11_BIND_VERTEX_BUFFER |
                   D3D11_BIND_INDEX_BUFFER | D3D11_BIND_STREAM_OUTPUT)) {
    ERR("CreateMTLTextureDescriptorInternal: invalid bind flags");
    return E_FAIL;
  } else {
    if (BindFlags & D3D11_BIND_SHADER_RESOURCE)
      metal_usage |= WMTTextureUsageShaderRead;
    if (BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL))
      metal_usage |= WMTTextureUsageRenderTarget;
    if (BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
      metal_usage |= WMTTextureUsageShaderRead | WMTTextureUsageShaderWrite;
      // NOTE: R32/RG32_TYPELESS format should be mapped to uint pixel format
      if (metal_format.PixelFormat == WMTPixelFormatR32Uint ||
          metal_format.PixelFormat == WMTPixelFormatR32Sint ||
          (metal_format.PixelFormat == WMTPixelFormatRG32Uint &&
           pDevice->GetMTLDevice().supportsFamily(WMTGPUFamilyApple8))) {
        metal_usage |= WMTTextureUsageShaderAtomic;
      }
    }
    // decoder not supported: D3D11_BIND_DECODER, D3D11_BIND_VIDEO_ENCODER
  }

  if (metal_format.Flag & MTL_DXGI_FORMAT_TYPELESS) {
    // well this is necessary for passing validation layer
    metal_usage |= WMTTextureUsagePixelFormatView;
  }

  pDescOut->usage = metal_usage;

  WMTResourceOptions options = (WMTResourceOptions)0;
  switch (Usage) {
  case D3D11_USAGE_DEFAULT:
    options |= WMTResourceStorageModeManaged;
    break;
  case D3D11_USAGE_IMMUTABLE:
    options |= WMTResourceStorageModeManaged | // FIXME: switch to
                                                 // ResourceStorageModePrivate
               WMTResourceHazardTrackingModeUntracked;
    break;
  case D3D11_USAGE_DYNAMIC:
    options |=
        WMTResourceStorageModeShared | WMTResourceCPUCacheModeWriteCombined;
    break;
  case D3D11_USAGE_STAGING:
    options |= WMTResourceStorageModeShared;
    break;
  }

  pDescOut->options = options;

  if (MipLevels == 0) {
    pDescOut->mipmap_level_count = 32 - __builtin_clz(Width | Height);
  } else {
    pDescOut->mipmap_level_count = MipLevels;
  }

  pDescOut->sample_count = 1;
  pDescOut->array_length = 1;

  switch (Dimension) {
  default: {
    ERR("CreateMTLTextureDescriptorInternal: invalid texture dimension");
    return E_FAIL;
  }
  case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
    if (ArraySize > 1) {
      pDescOut->type = WMTTextureType2DArray;
      pDescOut->array_length = ArraySize;
    } else {
      pDescOut->type = WMTTextureType2D;
    }
    break;
  case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
    if (MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) {
      D3D11_ASSERT(ArraySize / 6);
      D3D11_ASSERT((ArraySize % 6) == 0);
      if ((ArraySize / 6) > 1) {
        pDescOut->type = WMTTextureTypeCubeArray;
        pDescOut->array_length = ArraySize / 6;
      } else {
        pDescOut->type = WMTTextureTypeCube;
      }
    } else {
      if (SampleCount > 1) {
        if (!pDevice->GetMTLDevice().supportsTextureSampleCount(SampleCount)) {
          ERR("CreateMTLTextureDescriptorInternal: sample count ", SampleCount,
              " is not supported.");
          return E_INVALIDARG;
        }
        if (ArraySize > 1) {
          pDescOut->type = WMTTextureType2DMultisampleArray;
          pDescOut->array_length = ArraySize;
        } else {
          pDescOut->type = WMTTextureType2DMultisample;
        }
        pDescOut->sample_count = SampleCount;
      } else {
        if (ArraySize > 1) {
          pDescOut->type = WMTTextureType2DArray;
          pDescOut->array_length = ArraySize;
        } else {
          pDescOut->type = WMTTextureType2D;
        }
      }
    }
    break;
  case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
    pDescOut->type = WMTTextureType3D;
    break;
  }
  pDescOut->width = Width;
  pDescOut->height = Height;
  pDescOut->depth = Depth;

  return S_OK;
};

template <>
HRESULT CreateMTLTextureDescriptor(MTLD3D11Device *pDevice,
                                   const D3D11_TEXTURE1D_DESC *pDesc,
                                   D3D11_TEXTURE1D_DESC *pOutDesc,
                                   WMTTextureInfo *pMtlDescOut) {
  auto hr = CreateMTLTextureDescriptorInternal(
      pDevice, D3D11_RESOURCE_DIMENSION_TEXTURE1D, pDesc->Width, 1, 1,
      pDesc->ArraySize, 1, pDesc->BindFlags, pDesc->CPUAccessFlags,
      pDesc->MiscFlags, pDesc->Usage, pDesc->MipLevels, pDesc->Format,
      pMtlDescOut);
  if (SUCCEEDED(hr)) {
    *pOutDesc = *pDesc;
    pOutDesc->MipLevels = (*pMtlDescOut).mipmap_level_count;
  }
  return hr;
}

template <>
HRESULT CreateMTLTextureDescriptor(MTLD3D11Device *pDevice,
                                   const D3D11_TEXTURE2D_DESC1 *pDesc,
                                   D3D11_TEXTURE2D_DESC1 *pOutDesc,
                                   WMTTextureInfo *pMtlDescOut) {
  auto hr = CreateMTLTextureDescriptorInternal(
      pDevice, D3D11_RESOURCE_DIMENSION_TEXTURE2D, pDesc->Width, pDesc->Height,
      1, pDesc->ArraySize, pDesc->SampleDesc.Count, pDesc->BindFlags,
      pDesc->CPUAccessFlags, pDesc->MiscFlags, pDesc->Usage, pDesc->MipLevels,
      pDesc->Format, pMtlDescOut);
  if (SUCCEEDED(hr)) {
    *pOutDesc = *pDesc;
    pOutDesc->MipLevels = (*pMtlDescOut).mipmap_level_count;
  }
  return hr;
}

template <>
HRESULT CreateMTLTextureDescriptor(MTLD3D11Device *pDevice,
                                   const D3D11_TEXTURE3D_DESC1 *pDesc,
                                   D3D11_TEXTURE3D_DESC1 *pOutDesc,
                                   WMTTextureInfo *pMtlDescOut) {
  auto hr = CreateMTLTextureDescriptorInternal(
      pDevice, D3D11_RESOURCE_DIMENSION_TEXTURE3D, pDesc->Width, pDesc->Height,
      pDesc->Depth, 1, 1, pDesc->BindFlags, pDesc->CPUAccessFlags,
      pDesc->MiscFlags, pDesc->Usage, pDesc->MipLevels, pDesc->Format,
      pMtlDescOut);
  if (SUCCEEDED(hr)) {
    *pOutDesc = *pDesc;
    pOutDesc->MipLevels = (*pMtlDescOut).mipmap_level_count;
  }
  return hr;
}

template <>
void GetMipmapSize(const D3D11_TEXTURE1D_DESC *pDesc, uint32_t level,
                   uint32_t *pWidth, uint32_t *pHeight, uint32_t *pDepth) {
  *pHeight = 1;
  *pDepth = 1;
  *pWidth = std::max(1u, pDesc->Width >> level);
}

template <>
void GetMipmapSize(const D3D11_TEXTURE2D_DESC *pDesc, uint32_t level,
                   uint32_t *pWidth, uint32_t *pHeight, uint32_t *pDepth) {
  *pDepth = 1;
  *pWidth = std::max(1u, pDesc->Width >> level);
  *pHeight = std::max(1u, pDesc->Height >> level);
}

template <>
void GetMipmapSize(const D3D11_TEXTURE2D_DESC1 *pDesc, uint32_t level,
                   uint32_t *pWidth, uint32_t *pHeight, uint32_t *pDepth) {
  *pDepth = 1;
  *pWidth = std::max(1u, pDesc->Width >> level);
  *pHeight = std::max(1u, pDesc->Height >> level);
}

template <>
void GetMipmapSize(const D3D11_TEXTURE3D_DESC *pDesc, uint32_t level,
                   uint32_t *pWidth, uint32_t *pHeight, uint32_t *pDepth) {
  *pWidth = std::max(1u, pDesc->Width >> level);
  *pHeight = std::max(1u, pDesc->Height >> level);
  *pDepth = std::max(1u, pDesc->Depth >> level);
}

template <>
void GetMipmapSize(const D3D11_TEXTURE3D_DESC1 *pDesc, uint32_t level,
                   uint32_t *pWidth, uint32_t *pHeight, uint32_t *pDepth) {
  *pWidth = std::max(1u, pDesc->Width >> level);
  *pHeight = std::max(1u, pDesc->Height >> level);
  *pDepth = std::max(1u, pDesc->Depth >> level);
}

template <>
HRESULT
GetLinearTextureLayout(
    MTLD3D11Device *pDevice, const D3D11_TEXTURE1D_DESC &Desc, uint32_t level, uint32_t &BytesPerRow,
    uint32_t &BytesPerImage, uint32_t &BytesPerSlice, bool Aligned
) {
  auto metal = pDevice->GetMTLDevice();
  MTL_DXGI_FORMAT_DESC metal_format;

  if (FAILED(MTLQueryDXGIFormat(metal, Desc.Format, metal_format))) {
    ERR("GetLinearTextureLayout: invalid format: ", Desc.Format);
    return E_FAIL;
  }
  if (metal_format.PixelFormat == WMTPixelFormatInvalid) {
    ERR("GetLinearTextureLayout: invalid format: ", Desc.Format);
    return E_FAIL;
  }
  if (metal_format.BytesPerTexel == 0) {
    ERR("GetLinearTextureLayout: not an ordinary or packed format: ", Desc.Format);
    return E_FAIL;
  }
  uint32_t w, h, d;
  GetMipmapSize(&Desc, level, &w, &h, &d);
#ifdef VBOX
  if (metal_format.Flag & MTL_DXGI_FORMAT_DEPTH_PLANER)
  {
    D3D11_ASSERT(w != 0 && h != 0);
    auto w_phisical = align(w, 4);
    auto h_phisical = align(h, 4);
    auto aligned_bytes_per_row = metal_format.BytesPerTexel * w_phisical >> 2;
    BytesPerRow = aligned_bytes_per_row; // 1 row of block is 4 row of pixel
    BytesPerImage = aligned_bytes_per_row * h_phisical >> 2;
    BytesPerSlice = aligned_bytes_per_row * h_phisical >> 2;
    return S_OK;
  }
#endif
  auto bytes_per_row_unaligned = metal_format.BytesPerTexel * w;
  auto alignment = Aligned ? metal.minimumLinearTextureAlignmentForPixelFormat(metal_format.PixelFormat): 1;
  auto aligned_bytes_per_row = align(bytes_per_row_unaligned, alignment);
  BytesPerRow = aligned_bytes_per_row;
  BytesPerImage = aligned_bytes_per_row;
  BytesPerSlice = aligned_bytes_per_row;
  return S_OK;
};

template <>
HRESULT
GetLinearTextureLayout(
    MTLD3D11Device *pDevice, const D3D11_TEXTURE2D_DESC1 &Desc, uint32_t level, uint32_t &BytesPerRow,
    uint32_t &BytesPerImage, uint32_t &BytesPerSlice, bool Aligned
) {
  auto metal = pDevice->GetMTLDevice();
  MTL_DXGI_FORMAT_DESC metal_format;

  if (FAILED(MTLQueryDXGIFormat(metal, Desc.Format, metal_format))) {
    ERR("GetLinearTextureLayout: invalid format: ", Desc.Format);
    return E_FAIL;
  }
  if (metal_format.PixelFormat == WMTPixelFormatInvalid) {
    ERR("GetLinearTextureLayout: invalid format: ", Desc.Format);
    return E_FAIL;
  }
  if (metal_format.BytesPerTexel == 0) {
    ERR("GetLinearTextureLayout: not an ordinary or packed format: ", Desc.Format);
    return E_FAIL;
  }
  uint32_t w, h, d;
  GetMipmapSize(&Desc, level, &w, &h, &d);
  if (metal_format.Flag & MTL_DXGI_FORMAT_BC) {
    D3D11_ASSERT(w != 0 && h != 0);
    auto w_phisical = align(w, 4);
    auto h_phisical = align(h, 4);
    auto aligned_bytes_per_row = metal_format.BytesPerTexel * w_phisical >> 2;
    BytesPerRow = aligned_bytes_per_row; // 1 row of block is 4 row of pixel
    BytesPerImage = aligned_bytes_per_row * h_phisical >> 2;
    BytesPerSlice = aligned_bytes_per_row * h_phisical >> 2;
    return S_OK;
  }
#ifdef VBOX
  if (metal_format.Flag & MTL_DXGI_FORMAT_DEPTH_PLANER)
  {
    auto aligned_bytes_per_row = metal_format.BytesPerTexel * w;
    BytesPerRow = aligned_bytes_per_row; // 1 row of block is 4 row of pixel
    BytesPerImage = aligned_bytes_per_row * h;
    BytesPerSlice = aligned_bytes_per_row * h;
    return S_OK;
  }
#endif
  auto bytes_per_row_unaligned = metal_format.BytesPerTexel * w;
  auto alignment = Aligned ? metal.minimumLinearTextureAlignmentForPixelFormat(metal_format.PixelFormat): 1;
  auto aligned_bytes_per_row = align(bytes_per_row_unaligned, alignment);
  BytesPerRow = aligned_bytes_per_row;
  BytesPerImage = aligned_bytes_per_row * h;
  BytesPerSlice = aligned_bytes_per_row * h;
  return S_OK;
};

template <>
HRESULT
GetLinearTextureLayout(
    MTLD3D11Device *pDevice, const D3D11_TEXTURE3D_DESC1 &Desc, uint32_t level, uint32_t &BytesPerRow,
    uint32_t &BytesPerImage, uint32_t &BytesPerSlice, bool Aligned
) {
  auto metal = pDevice->GetMTLDevice();
  MTL_DXGI_FORMAT_DESC metal_format;

  if (FAILED(MTLQueryDXGIFormat(metal, Desc.Format, metal_format))) {
    ERR("GetLinearTextureLayout: invalid format: ", Desc.Format);
    return E_FAIL;
  }
  if (metal_format.PixelFormat == WMTPixelFormatInvalid) {
    ERR("GetLinearTextureLayout: invalid format: ", Desc.Format);
    return E_FAIL;
  }
  if (metal_format.BytesPerTexel == 0) {
    ERR("GetLinearTextureLayout: not an ordinary or packed format: ", Desc.Format);
    return E_FAIL;
  }
  uint32_t w, h, d;
  GetMipmapSize(&Desc, level, &w, &h, &d);
  if (metal_format.Flag & MTL_DXGI_FORMAT_BC) {
    D3D11_ASSERT(w != 0 && h != 0);
    auto w_phisical = align(w, 4);
    auto h_phisical = align(h, 4);
    auto aligned_bytes_per_row = metal_format.BytesPerTexel * w_phisical >> 2;
    BytesPerRow = aligned_bytes_per_row; // 1 row of block is 4 row of pixel
    BytesPerImage = aligned_bytes_per_row * h_phisical >> 2;
    BytesPerSlice = aligned_bytes_per_row * h_phisical * d >> 2;
    return S_OK;
  }
  auto bytes_per_row_unaligned = metal_format.BytesPerTexel * w;
  auto alignment = Aligned ? metal.minimumLinearTextureAlignmentForPixelFormat(metal_format.PixelFormat) : 1;
  auto aligned_bytes_per_row = align(bytes_per_row_unaligned, alignment);
  BytesPerRow = aligned_bytes_per_row;
  BytesPerImage = aligned_bytes_per_row * h;
  BytesPerSlice = aligned_bytes_per_row * h * d;
  return S_OK;
};

template <>
void DowngradeResourceDescription<D3D11_TEXTURE2D_DESC1, D3D11_TEXTURE2D_DESC>(
    D3D11_TEXTURE2D_DESC1 const &source, D3D11_TEXTURE2D_DESC *dst) {
  dst->Width = source.Width;
  dst->Height = source.Height;
  dst->MipLevels = source.MipLevels;
  dst->ArraySize = source.ArraySize;
  dst->Format = source.Format;
  dst->SampleDesc = source.SampleDesc;
  dst->Usage = source.Usage;
  dst->BindFlags = source.BindFlags;
  dst->CPUAccessFlags = source.CPUAccessFlags;
  dst->MiscFlags = source.MiscFlags;
}

template <>
void UpgradeResourceDescription<D3D11_TEXTURE2D_DESC, D3D11_TEXTURE2D_DESC1>(
    D3D11_TEXTURE2D_DESC const *source, D3D11_TEXTURE2D_DESC1 &dst) {
  dst.Width = source->Width;
  dst.Height = source->Height;
  dst.MipLevels = source->MipLevels;
  dst.ArraySize = source->ArraySize;
  dst.Format = source->Format;
  dst.SampleDesc = source->SampleDesc;
  dst.Usage = source->Usage;
  dst.BindFlags = source->BindFlags;
  dst.CPUAccessFlags = source->CPUAccessFlags;
  dst.MiscFlags = source->MiscFlags;
  dst.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
}

template <>
void DowngradeResourceDescription<D3D11_TEXTURE3D_DESC1, D3D11_TEXTURE3D_DESC>(
    D3D11_TEXTURE3D_DESC1 const &source, D3D11_TEXTURE3D_DESC *dst) {
  dst->Width = source.Width;
  dst->Height = source.Height;
  dst->Depth = source.Depth;
  dst->MipLevels = source.MipLevels;
  dst->Format = source.Format;
  dst->Usage = source.Usage;
  dst->BindFlags = source.BindFlags;
  dst->CPUAccessFlags = source.CPUAccessFlags;
  dst->MiscFlags = source.MiscFlags;
}

template <>
void UpgradeResourceDescription<D3D11_TEXTURE3D_DESC, D3D11_TEXTURE3D_DESC1>(
    D3D11_TEXTURE3D_DESC const *source, D3D11_TEXTURE3D_DESC1 &dst) {
  dst.Width = source->Width;
  dst.Height = source->Height;
  dst.Depth = source->Depth;
  dst.MipLevels = source->MipLevels;
  dst.Format = source->Format;
  dst.Usage = source->Usage;
  dst.BindFlags = source->BindFlags;
  dst.CPUAccessFlags = source->CPUAccessFlags;
  dst.MiscFlags = source->MiscFlags;
  dst.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
}

}; // namespace dxmt