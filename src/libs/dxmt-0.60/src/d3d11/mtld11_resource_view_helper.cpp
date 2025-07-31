#include "dxmt_format.hpp"
#include "mtld11_resource.hpp"

namespace dxmt {

void FixDepthStencilFormat(Texture *pTexture, MTL_DXGI_FORMAT_DESC &Desc) {
  switch (pTexture->pixelFormat()) {
  case WMTPixelFormatDepth16Unorm:
    // DXGI_FORMAT_R16_TYPELESS
    // DXGI_FORMAT_R16_UNORM
    Desc.PixelFormat = WMTPixelFormatDepth16Unorm;
    break;
  case WMTPixelFormatDepth32Float:
    // DXGI_FORMAT_R32_TYPELESS
    // DXGI_FORMAT_R32_FLOAT
    Desc.PixelFormat = WMTPixelFormatDepth32Float;
    break;
  default:
    break;
  }
}

template <>
HRESULT
InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture* pTexture,
    D3D11_SHADER_RESOURCE_VIEW_DESC1 &ViewDesc, TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format, metal_format))) {
    ERR("Failed to create SRV due to unsupported format ", ViewDesc.Format);
    return E_FAIL;
  }

  Descriptor.usage = WMTTextureUsageShaderRead;
  FixDepthStencilFormat(pTexture, metal_format);
  WMTTextureType TextureType = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D_SRV_DIMENSION_TEXTURE1D: {
    if (~ViewDesc.Texture1D.MipLevels == 0)
      ViewDesc.Texture1D.MipLevels = MiplevelCount - ViewDesc.Texture1D.MostDetailedMip;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture1D.MipLevels;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.MipLevels == 0)
      ViewDesc.Texture1DArray.MipLevels = MiplevelCount - ViewDesc.Texture1DArray.MostDetailedMip;
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture1DArray.MipLevels;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2D: {
    if (~ViewDesc.Texture2D.MipLevels == 0)
      ViewDesc.Texture2D.MipLevels = MiplevelCount - ViewDesc.Texture2D.MostDetailedMip;

    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture2D.MipLevels;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.MipLevels == 0)
      ViewDesc.Texture2DArray.MipLevels = MiplevelCount - ViewDesc.Texture2DArray.MostDetailedMip;
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture2DArray.MipLevels;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMS: {
    if (TextureType == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisample;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    if (TextureType == WMTTextureType2D) {
      WARN("A texture2d view is created on multisampled texture");
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY: {
    if (~ViewDesc.Texture2DMSArray.ArraySize == 0)
      ViewDesc.Texture2DMSArray.ArraySize = ArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice;
    // TODO
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE3D: {
    if (~ViewDesc.Texture3D.MipLevels == 0)
      ViewDesc.Texture3D.MipLevels = MiplevelCount - ViewDesc.Texture3D.MostDetailedMip;
    if (TextureType == WMTTextureType3D) {
      Descriptor.type = WMTTextureType3D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture3D.MostDetailedMip;
      Descriptor.miplevelCount =ViewDesc.Texture3D.MipLevels;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURECUBE: {
    if (~ViewDesc.TextureCube.MipLevels == 0)
      ViewDesc.TextureCube.MipLevels = MiplevelCount - ViewDesc.TextureCube.MostDetailedMip;
    if (TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureTypeCube;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.TextureCube.MostDetailedMip;
      Descriptor.miplevelCount = ~ViewDesc.TextureCube.MipLevels ? ViewDesc.TextureCube.MipLevels
                                                               : MiplevelCount - ViewDesc.TextureCube.MostDetailedMip;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 6;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: {
    D3D11_ASSERT((ViewDesc.TextureCubeArray.First2DArrayFace % 6) == 0);
    D3D11_ASSERT((ArraySize % 6) == 0);
    if (~ViewDesc.TextureCubeArray.MipLevels == 0)
      ViewDesc.TextureCubeArray.MipLevels = MiplevelCount - ViewDesc.TextureCubeArray.MostDetailedMip;
    if (~ViewDesc.TextureCubeArray.NumCubes == 0)
      ViewDesc.TextureCubeArray.NumCubes = (ArraySize - ViewDesc.TextureCubeArray.First2DArrayFace) / 6;
    if (TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureTypeCubeArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.TextureCubeArray.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.TextureCubeArray.MipLevels;
      Descriptor.firstArraySlice = ViewDesc.TextureCubeArray.First2DArrayFace;
      Descriptor.arraySize = ViewDesc.TextureCubeArray.NumCubes * 6;
      return S_OK;
    }
    break;
  }
  }
  ERR("Unhandled srv creation: \n Source: ", TextureType, "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

template <>
HRESULT
InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture *pTexture,
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 &ViewDesc, TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format,
                                metal_format))) {
    return E_FAIL;
  }

  Descriptor.usage = WMTTextureUsageShaderRead | WMTTextureUsageShaderWrite;
  FixDepthStencilFormat(pTexture, metal_format);
  WMTTextureType TextureType = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1D: {
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2D: {
    D3D11_ASSERT(ViewDesc.Texture2D.PlaneSlice == 0);
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE3D: {
    if (~ViewDesc.Texture3D.WSize == 0)
      ViewDesc.Texture3D.WSize = ArraySize - ViewDesc.Texture3D.FirstWSlice;
    if (TextureType == WMTTextureType3D) {

      if (ViewDesc.Texture3D.FirstWSlice == 0) {
        if (ViewDesc.Texture3D.WSize != ArraySize) {
          WARN("Created a subview of 3D texture.");
        }
        Descriptor.type = WMTTextureType3D;
        Descriptor.format = metal_format.PixelFormat;
        Descriptor.firstMiplevel = ViewDesc.Texture3D.MipSlice;
        Descriptor.miplevelCount = 1;
        Descriptor.firstArraySlice = 0;
        Descriptor.arraySize = 1;
        return S_OK;
      }
      ERR("tex3d uav creation not properly handled: ", ViewDesc.Texture3D.FirstWSlice, ":", ViewDesc.Texture3D.WSize,
          ":", ArraySize);
    }
    break;
  }
  }
  ERR("Unhandled uav creation: \n Source: ", TextureType,
      "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

inline void
GetRenderTargetSize(Texture *pTexture, uint32_t level, MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc) {
  AttachmentDesc.Width = std::max(1u, pTexture->width() >> level);
  AttachmentDesc.Height = std::max(1u, pTexture->height() >> level);
}

template <>
HRESULT InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture* pTexture,
    D3D11_RENDER_TARGET_VIEW_DESC1 &ViewDesc, MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc, TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format,
                                metal_format))) {
    return E_FAIL;
  }

  Descriptor.usage = WMTTextureUsageRenderTarget;
  AttachmentDesc.DepthPlane = 0;
  AttachmentDesc.RenderTargetArrayLength = 0;
  AttachmentDesc.SampleCount = pTexture->sampleCount();

  auto texture_type = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1D: {
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2D: {
    D3D11_ASSERT(ViewDesc.Texture2D.PlaneSlice == 0);
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMS: {
    if (texture_type == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisample;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY: {
    if (~ViewDesc.Texture2DMSArray.ArraySize == 0)
      ViewDesc.Texture2DMSArray.ArraySize = ArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice;
    if (texture_type == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisampleArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0; // FIXME: we only handle 2dms here!
      Descriptor.arraySize = 1;
      AttachmentDesc.RenderTargetArrayLength = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE3D: {
    if (~ViewDesc.Texture3D.WSize == 0)
      ViewDesc.Texture3D.WSize = ArraySize - ViewDesc.Texture3D.FirstWSlice;
    if (texture_type == WMTTextureType3D) {
      if (ViewDesc.Texture3D.WSize == 1) {
        Descriptor.type = WMTTextureType3D;
        Descriptor.format = metal_format.PixelFormat;
        Descriptor.firstMiplevel = ViewDesc.Texture3D.MipSlice;
        Descriptor.miplevelCount = 1;
        Descriptor.firstArraySlice = 0;
        Descriptor.arraySize = 1;
        AttachmentDesc.DepthPlane = ViewDesc.Texture3D.FirstWSlice;
        GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
        return S_OK;
      }
      if (ViewDesc.Texture3D.FirstWSlice == 0) {
        if (ViewDesc.Texture3D.WSize != ArraySize) {
          WARN("Created a subview of 3D texture.");
        }
        Descriptor.type = WMTTextureType3D;
        Descriptor.format = metal_format.PixelFormat;
        Descriptor.firstMiplevel = ViewDesc.Texture3D.MipSlice;
        Descriptor.miplevelCount = 1;
        Descriptor.firstArraySlice = 0;
        Descriptor.arraySize = 1;
        AttachmentDesc.RenderTargetArrayLength = ArraySize;
        GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
        return S_OK;
      }
      ERR("tex3d rtv creation not properly handled: ", ViewDesc.Texture3D.FirstWSlice, ":", ViewDesc.Texture3D.WSize,
          ":", ArraySize);
    }
    break;
  }
  }
  ERR("Unhandled rtv creation: \n Source: ", texture_type,
      "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

template <>
HRESULT
InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture *pTexture,
    D3D11_DEPTH_STENCIL_VIEW_DESC &ViewDesc, MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc,
    TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format, metal_format))) {
    ERR("Failed to create DSV due to unsupported format ", ViewDesc.Format);
    return E_FAIL;
  }

  ViewDesc.Flags = ViewDesc.Flags & 3;
  Descriptor.usage = WMTTextureUsageRenderTarget;
  AttachmentDesc.DepthPlane = 0;
  AttachmentDesc.RenderTargetArrayLength = 0;
  AttachmentDesc.SampleCount = pTexture->sampleCount();

  auto texture_type = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D11_DSV_DIMENSION_TEXTURE1D: {
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2D: {
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2DMS: {
    if (texture_type == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisample;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    if (texture_type == WMTTextureType2D) {
      WARN("A texture2d view is created on multisampled texture");
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY: {
    break;
  }
  }
  ERR("Unhandled dsv creation: \n Source: ", texture_type, "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

template <>
void DowngradeViewDescription(const D3D11_SHADER_RESOURCE_VIEW_DESC1 &src,
                              D3D11_SHADER_RESOURCE_VIEW_DESC *pDst) {
  pDst->ViewDimension = src.ViewDimension;
  pDst->Format = src.Format;
  switch (src.ViewDimension) {
  case D3D_SRV_DIMENSION_UNKNOWN:
    break;
  case D3D_SRV_DIMENSION_BUFFER:
    pDst->Buffer = src.Buffer;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1D:
    pDst->Texture1D = src.Texture1D;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
    pDst->Texture1DArray = src.Texture1DArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2D: {
    pDst->Texture2D.MipLevels = src.Texture2D.MipLevels;
    pDst->Texture2D.MostDetailedMip = src.Texture2D.MostDetailedMip;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DARRAY: {
    pDst->Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
    pDst->Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
    pDst->Texture2DArray.MipLevels = src.Texture2DArray.MipLevels;
    pDst->Texture2DArray.MostDetailedMip = src.Texture2DArray.MostDetailedMip;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMS:
    pDst->Texture2DMS = src.Texture2DMS;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
    pDst->Texture2DMSArray = src.Texture2DMSArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE3D:
    pDst->Texture3D = src.Texture3D;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBE:
    pDst->TextureCube = src.TextureCube;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
    pDst->TextureCubeArray = src.TextureCubeArray;
    break;
  case D3D_SRV_DIMENSION_BUFFEREX:
    pDst->BufferEx = src.BufferEx;
    break;
  }
}

template <>
void DowngradeViewDescription(const D3D11_RENDER_TARGET_VIEW_DESC1 &src,
                              D3D11_RENDER_TARGET_VIEW_DESC *pDst) {
  pDst->ViewDimension = src.ViewDimension;
  pDst->Format = src.Format;
  switch (src.ViewDimension) {
  case D3D11_RTV_DIMENSION_UNKNOWN:
    break;
  case D3D11_RTV_DIMENSION_BUFFER:
    pDst->Buffer = src.Buffer;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1D:
    pDst->Texture1D = src.Texture1D;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
    pDst->Texture1DArray = src.Texture1DArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2D: {
    pDst->Texture2D.MipSlice = src.Texture2D.MipSlice;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: {
    pDst->Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
    pDst->Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
    pDst->Texture2DArray.MipSlice = src.Texture2DArray.MipSlice;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMS:
    pDst->Texture2DMS = src.Texture2DMS;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
    pDst->Texture2DMSArray = src.Texture2DMSArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE3D:
    pDst->Texture3D = src.Texture3D;
    break;
  }
}

template <>
void DowngradeViewDescription(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 &src,
                              D3D11_UNORDERED_ACCESS_VIEW_DESC *pDst) {
  pDst->ViewDimension = src.ViewDimension;
  pDst->Format = src.Format;
  switch (src.ViewDimension) {
  case D3D11_UAV_DIMENSION_UNKNOWN:
    break;
  case D3D11_UAV_DIMENSION_BUFFER:
    pDst->Buffer = src.Buffer;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1D:
    pDst->Texture1D = src.Texture1D;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
    pDst->Texture1DArray = src.Texture1DArray;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE2D: {
    pDst->Texture2D.MipSlice = src.Texture2D.MipSlice;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: {
    pDst->Texture2DArray.MipSlice = src.Texture2DArray.MipSlice;
    pDst->Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
    pDst->Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE3D:
    pDst->Texture3D = src.Texture3D;
    break;
  }
}

template <>
void UpgradeViewDescription(const D3D11_SHADER_RESOURCE_VIEW_DESC *pSrc,
                            D3D11_SHADER_RESOURCE_VIEW_DESC1 &dst) {
  dst.Format = pSrc->Format;
  dst.ViewDimension = pSrc->ViewDimension;
  switch (pSrc->ViewDimension) {
  case D3D_SRV_DIMENSION_UNKNOWN:
    break;
  case D3D_SRV_DIMENSION_BUFFER:
    dst.Buffer = pSrc->Buffer;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1D:
    dst.Texture1D = pSrc->Texture1D;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
    dst.Texture1DArray = pSrc->Texture1DArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2D: {
    dst.Texture2D.MipLevels = pSrc->Texture2D.MipLevels;
    dst.Texture2D.MostDetailedMip = pSrc->Texture2D.MostDetailedMip;
    dst.Texture2D.PlaneSlice = 0;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DARRAY: {
    dst.Texture2DArray.MipLevels = pSrc->Texture2DArray.MipLevels;
    dst.Texture2DArray.MostDetailedMip = pSrc->Texture2DArray.MostDetailedMip;
    dst.Texture2DArray.ArraySize = pSrc->Texture2DArray.ArraySize;
    dst.Texture2DArray.FirstArraySlice = pSrc->Texture2DArray.FirstArraySlice;
    dst.Texture2DArray.PlaneSlice = 0;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMS:
    dst.Texture2DMS = pSrc->Texture2DMS;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
    dst.Texture2DMSArray = pSrc->Texture2DMSArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE3D:
    dst.Texture3D = pSrc->Texture3D;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBE:
    dst.TextureCube = pSrc->TextureCube;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
    dst.TextureCubeArray = pSrc->TextureCubeArray;
    break;
  case D3D_SRV_DIMENSION_BUFFEREX:
    dst.BufferEx = pSrc->BufferEx;
    break;
  }
};

template <>
void UpgradeViewDescription(const D3D11_UNORDERED_ACCESS_VIEW_DESC *pSrc,
                            D3D11_UNORDERED_ACCESS_VIEW_DESC1 &dst) {
  dst.Format = pSrc->Format;
  dst.ViewDimension = pSrc->ViewDimension;
  switch (pSrc->ViewDimension) {
  case D3D11_UAV_DIMENSION_UNKNOWN:
    break;
  case D3D11_UAV_DIMENSION_BUFFER:
    dst.Buffer = pSrc->Buffer;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1D:
    dst.Texture1D = pSrc->Texture1D;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
    dst.Texture1DArray = pSrc->Texture1DArray;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE2D: {
    dst.Texture2D.MipSlice = pSrc->Texture2D.MipSlice;
    dst.Texture2D.PlaneSlice = 0;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: {
    dst.Texture2DArray.MipSlice = pSrc->Texture2DArray.MipSlice;
    dst.Texture2DArray.ArraySize = pSrc->Texture2DArray.ArraySize;
    dst.Texture2DArray.FirstArraySlice = pSrc->Texture2DArray.FirstArraySlice;
    dst.Texture2DArray.PlaneSlice = 0;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE3D:
    dst.Texture3D = pSrc->Texture3D;
    break;
  }
};

template <>
void UpgradeViewDescription(const D3D11_RENDER_TARGET_VIEW_DESC *pSrc,
                            D3D11_RENDER_TARGET_VIEW_DESC1 &dst) {
  dst.Format = pSrc->Format;
  dst.ViewDimension = pSrc->ViewDimension;
  switch (pSrc->ViewDimension) {
  case D3D11_RTV_DIMENSION_UNKNOWN:
    break;
  case D3D11_RTV_DIMENSION_BUFFER:
    dst.Buffer = pSrc->Buffer;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1D:
    dst.Texture1D = pSrc->Texture1D;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
    dst.Texture1DArray = pSrc->Texture1DArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2D: {
    dst.Texture2D.MipSlice = pSrc->Texture2D.MipSlice;
    dst.Texture2D.PlaneSlice = 0;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: {
    dst.Texture2DArray.MipSlice = pSrc->Texture2DArray.MipSlice;
    dst.Texture2DArray.ArraySize = pSrc->Texture2DArray.ArraySize;
    dst.Texture2DArray.FirstArraySlice = pSrc->Texture2DArray.FirstArraySlice;
    dst.Texture2DArray.PlaneSlice = 0;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMS:
    dst.Texture2DMS = pSrc->Texture2DMS;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
    dst.Texture2DMSArray = pSrc->Texture2DMSArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE3D:
    dst.Texture3D = pSrc->Texture3D;
    break;
  }
};

} // namespace dxmt