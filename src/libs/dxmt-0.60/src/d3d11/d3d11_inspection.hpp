#pragma once

#include "d3d11_1.h"
#include "Metal.hpp"

namespace dxmt {

class MTLD3D11Inspection {
public:
  MTLD3D11Inspection(WMT::Device pDevice);

  HRESULT GetFeatureData(D3D11_FEATURE Feature, UINT FeatureDataSize,
                         void *pFeatureData) const;

private:
  WMT::Device m_device;

  D3D11_FEATURE_DATA_THREADING m_threading = {};
  D3D11_FEATURE_DATA_DOUBLES m_doubles = {};
  D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS m_d3d10Options = {};
  D3D11_FEATURE_DATA_D3D11_OPTIONS m_d3d11Options = {};
  D3D11_FEATURE_DATA_ARCHITECTURE_INFO m_architectureInfo = {};
  D3D11_FEATURE_DATA_D3D9_OPTIONS m_d3d9Options = {};
  D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT m_shaderMinPrecision = {};
  D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT m_d3d9Shadow = {};
  /* 11.2 */
  D3D11_FEATURE_DATA_D3D11_OPTIONS1 m_d3d11Options1 = {};
  // D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT m_d3d9SimpleInstancing =
  // {};
  // D3D11_FEATURE_DATA_MARKER_SUPPORT m_marker = {};
  // D3D11_FEATURE_DATA_D3D9_OPTIONS1 m_d3d9Options1 = {};
  /* 11.3 */
  D3D11_FEATURE_DATA_D3D11_OPTIONS2 m_d3d11Options2 = {};
  D3D11_FEATURE_DATA_D3D11_OPTIONS3 m_d3d11Options3 = {};
  D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT m_gpuVirtualAddress = {};
  D3D11_FEATURE_DATA_D3D11_OPTIONS5 m_d3d11Options5 = {};
  D3D11_FEATURE_DATA_SHADER_CACHE m_shaderCache = {};

  template <typename T>
  static HRESULT GetTypedFeatureData(UINT Size, void *pDstData,
                                     const T *pSrcData) {
    if (Size != sizeof(T))
      return E_INVALIDARG;

    *(reinterpret_cast<T *>(pDstData)) = *pSrcData;
    return S_OK;
  }
};
} // namespace dxmt