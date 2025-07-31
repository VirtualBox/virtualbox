#include "d3d11_inspection.hpp"

#include "log/log.hpp"

namespace dxmt {

MTLD3D11Inspection::MTLD3D11Inspection(WMT::Device pDevice)
    : m_device(pDevice) {

  // FIXME: Apple Silicon definitely TBDR
  m_architectureInfo.TileBasedDeferredRenderer = pDevice.hasUnifiedMemory();

  m_threading.DriverConcurrentCreates = TRUE; // I guess
  m_threading.DriverCommandLists = TRUE;      //  should be?

  /*
  MSL Specification 2.1: Metal does _not_ support double
  */
  m_doubles.DoublePrecisionFloatShaderOps = FALSE;

  m_d3d9Options.FullNonPow2TextureSupport = TRUE; // no doubt
  m_d3d10Options.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x =
      TRUE; // no doubt

  // dx11 features
  m_d3d11Options.ClearView = TRUE; // ..
  m_d3d11Options.ConstantBufferOffsetting = TRUE;
  m_d3d11Options.ConstantBufferPartialUpdate = TRUE;
  m_d3d11Options.MapNoOverwriteOnDynamicConstantBuffer = TRUE;
  /*
  https://developer.apple.com/documentation/metal/mtlblitcommandencoder/1400754-copyfromtexture?language=objc
  Copying data to overlapping regions within the same texture may result in
  unexpected behavior.

  https://learn.microsoft.com/en-us/windows/win32/direct3d11/using-direct3d-optional-features-to-supplement-direct3d-feature-levels
  TODO: ClearView, CopyWithOverlap, ConstantBufferPartialUpdate,
  ConstantBufferOffsetting, and MapNoOverwriteOnDynamicConstantBuffer must be
  all TRUE/FALSE. Maybe we should support it and when there is overlap we use a
  temp buffer
  */
  m_d3d11Options.CopyWithOverlap = TRUE;
  /*
  FXIME: I think it is supported
   */
  m_d3d11Options.MultisampleRTVWithForcedSampleCountOne = TRUE;
  m_d3d11Options.MapNoOverwriteOnDynamicBufferSRV = TRUE;
  /*
  https://github.com/gpuweb/gpuweb/issues/503#issuecomment-908973358
  */
  m_d3d11Options.UAVOnlyRenderingForcedSampleCount = TRUE;
  m_d3d11Options.DiscardAPIsSeenByDriver = TRUE; // wtf
  // In metal we don't support double
  m_d3d11Options.ExtendedDoublesShaderInstructions = FALSE;
  m_d3d11Options.ExtendedResourceSharing = TRUE; // wtf
  // FIXME: check it
  m_d3d11Options.SAD4ShaderInstructions = FALSE;
  m_d3d11Options.FlagsForUpdateAndCopySeenByDriver = TRUE; // wtf
#ifdef DXMT_NO_PRIVATE_API
  m_d3d11Options.OutputMergerLogicOp = FALSE;
#else
  m_d3d11Options.OutputMergerLogicOp = TRUE;
#endif

  m_d3d11Options3.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer = TRUE;

  m_d3d11Options5.SharedResourceTier = D3D11_SHARED_RESOURCE_TIER_0; // TODO

  // use dxvk values
  m_gpuVirtualAddress.MaxGPUVirtualAddressBitsPerProcess = 40;
  m_gpuVirtualAddress.MaxGPUVirtualAddressBitsPerResource = 32;

  m_shaderCache.SupportFlags =
      D3D11_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE |
      D3D11_SHADER_CACHE_SUPPORT_AUTOMATIC_INPROC_CACHE; // why application
                                                         // would ask for
                                                         // this...

  /*
  Metal: half data type
  */
  m_shaderMinPrecision.PixelShaderMinPrecision =
      D3D11_SHADER_MIN_PRECISION_16_BIT;
  m_shaderMinPrecision.AllOtherShaderStagesMinPrecision =
      D3D11_SHADER_MIN_PRECISION_16_BIT;

  m_d3d9Shadow.SupportsDepthAsTextureWithLessEqualComparisonFilter = TRUE;

  m_d3d11Options1.ClearViewAlsoSupportsDepthOnlyFormats = TRUE;
  m_d3d11Options1.MapOnDefaultBuffers = TRUE;
  m_d3d11Options1.MinMaxFiltering = FALSE;
  m_d3d11Options1.TiledResourcesTier = D3D11_TILED_RESOURCES_NOT_SUPPORTED;

  m_d3d11Options2.TiledResourcesTier = D3D11_TILED_RESOURCES_NOT_SUPPORTED;
  m_d3d11Options2.ConservativeRasterizationTier =
      D3D11_CONSERVATIVE_RASTERIZATION_NOT_SUPPORTED;
  m_d3d11Options2.PSSpecifiedStencilRefSupported = TRUE;
  m_d3d11Options2.ROVsSupported = TRUE;
  m_d3d11Options2.MapOnDefaultTextures = TRUE;
  m_d3d11Options2.StandardSwizzle = TRUE;
  m_d3d11Options2.TypedUAVLoadAdditionalFormats = TRUE;
  /**
  It's an intention to not report UMA
   */
  m_d3d11Options2.UnifiedMemoryArchitecture = FALSE;
}

HRESULT MTLD3D11Inspection::GetFeatureData(D3D11_FEATURE Feature,
                                           UINT FeatureDataSize,
                                           void *pFeatureData) const {
  // MSDN order
  // https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_feature
  // so far support dx11.1
  switch (Feature) {
  case D3D11_FEATURE_THREADING:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_threading);
  case D3D11_FEATURE_DOUBLES:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_doubles);
  case D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d10Options);
  case D3D11_FEATURE_D3D11_OPTIONS:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d11Options);
  case D3D11_FEATURE_ARCHITECTURE_INFO:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData,
                               &m_architectureInfo);
  case D3D11_FEATURE_D3D9_OPTIONS:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d9Options);
  case D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData,
                               &m_shaderMinPrecision);
  case D3D11_FEATURE_D3D9_SHADOW_SUPPORT:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d9Shadow);
  case D3D11_FEATURE_D3D11_OPTIONS1:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d11Options1);
    //   case D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT:
    //     return GetTypedFeatureData(FeatureDataSize, pFeatureData,
    //                                &m_d3d9SimpleInstancing);
    //   case D3D11_FEATURE_MARKER_SUPPORT:
    //     return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_marker);
    //   case D3D11_FEATURE_D3D9_OPTIONS1:
    //     return GetTypedFeatureData(FeatureDataSize, pFeatureData,
    //     &m_d3d9Options1);
  case D3D11_FEATURE_D3D11_OPTIONS2:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d11Options2);
  case D3D11_FEATURE_D3D11_OPTIONS3:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d11Options3);
    //   case D3D11_FEATURE_D3D11_OPTIONS4:
    //     return GetTypedFeatureData(FeatureDataSize, pFeatureData,
    //     &m_d3d11Options4);
  case D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData,
                               &m_gpuVirtualAddress);
  case D3D11_FEATURE_D3D11_OPTIONS5:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_d3d11Options5);
  case D3D11_FEATURE_SHADER_CACHE:
    return GetTypedFeatureData(FeatureDataSize, pFeatureData, &m_shaderCache);
  default:
    ERR("Not supported feature: ", Feature);
    return E_INVALIDARG;
  }
}

} // namespace dxmt