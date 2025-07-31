#include "d3d11.h"
#include "nvngx.hpp"
#include "log/log.hpp"
#include "nvngx_feature.hpp"
#include "nvngx_parameter.hpp"
#include "../d3d11/d3d11_interfaces.hpp"
#include "../dxgi/dxgi_interfaces.h"
#include <cmath>

namespace dxmt {
Logger Logger::s_instance("nvngx.log");

#define IMPLEMENT_ME                                                                                                   \
  Logger::err(str::format(__FILE__, ":", __FUNCTION__, " is not implemented."));                                       \
  abort();                                                                                                             \
  __builtin_unreachable();

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_Ext(
    unsigned long long id, const wchar_t *path, ID3D11Device *device, unsigned int sdk_version, const void *feature_info
) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init(
    unsigned long long id, const wchar_t *path, ID3D11Device *device, const void *feature_info, unsigned int sdk_version
) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D11Device *device, unsigned int sdk_version, const void *feature_info
) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Init_with_ProjectID(
    const char *project, unsigned int engine_type, const char *engine_version, const wchar_t *path,
    ID3D11Device *device, const void *feature_info, unsigned int sdk_version
) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_CreateFeature(
    ID3D11DeviceContext *context, unsigned int feature, NVNGXParameter *params, unsigned int **out_handle
) {
  auto parameters = static_cast<ParametersImpl *>(params);
  if (feature == NVNGX_FEATURE_SUPERSAMPLING) {
    auto dlss = std::make_unique<DLSSFeature>();
    dlss->feature = NVNGX_FEATURE_SUPERSAMPLING;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Width, &dlss->width)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Height, &dlss->height)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_OutWidth, &dlss->target_width)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_OutHeight, &dlss->target_height)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_PerfQualityValue, &dlss->quality)))
      dlss->quality = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Feature_Create_Flags, &dlss->flag)))
      dlss->flag = 0;
    if (NVNGX_FAILED(parameters->Get("DLSS.Enable.Output.Subrects", &dlss->enable_output_subrects)))
      dlss->enable_output_subrects = 0;

    *out_handle = &dlss.release()->handle;
    return NVNGX_RESULT_OK;
  }

  ERR("NVSDK_NGX_D3D11_CreateFeature: feature ", feature, " not handled");
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_EvaluateFeature(
    ID3D11DeviceContext *context, const unsigned int *handle, NVNGXParameter *params, void *callback
) {
  auto parameters = static_cast<ParametersImpl *>(params);
  switch (static_cast<CommonFeature *>((void *)handle)->feature) {
  case NVNGX_FEATURE_SUPERSAMPLING: {
    auto dlss = static_cast<DLSSFeature *>((void *)handle);

    IMTLD3D11ContextExt *pCtxExt = nullptr;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&pCtxExt))))
      return NVNGX_RESULT_INVALID_PARAMETER;

    ID3D11Texture2D *input, *output, *depth, *mv, *exposure = nullptr;

    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Color, (ID3D11Resource **)&input)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Output, (ID3D11Resource **)&output)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Depth, (ID3D11Resource **)&depth)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_MotionVectors, (ID3D11Resource **)&mv)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    parameters->Get(NVNGX_Parameter_ExposureTexture, (ID3D11Resource **)&exposure);

    uint32_t width = 0, height = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, &width)))
      return NVNGX_RESULT_INVALID_PARAMETER;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, &height)))
      return NVNGX_RESULT_INVALID_PARAMETER;

    MTL_TEMPORAL_UPSCALE_D3D11_DESC desc = {};
    desc.InputContentWidth = width;
    desc.InputContentHeight = height;

    desc.Color = input;
    desc.Output = output;
    desc.Depth = depth;
    desc.MotionVector = mv;
    desc.ExposureTexture = exposure;
    desc.DepthReversed = bool(dlss->flag & NVNGX_DLSS_FLAG_DEPTH_INVERTED);
    desc.AutoExposure = bool(dlss->flag & NVNGX_DLSS_FLAG_AUTO_EXPOSURE);

    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Jitter_Offset_X, &desc.JitterOffsetX)))
      desc.JitterOffsetX = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_Jitter_Offset_Y, &desc.JitterOffsetY)))
      desc.JitterOffsetY = 0;

    int reset = 0;
    parameters->Get(NVNGX_Parameter_Reset, &reset);
    desc.InReset = reset;

    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_MV_Scale_X, &desc.MotionVectorScaleX)))
      desc.MotionVectorScaleX = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_MV_Scale_Y, &desc.MotionVectorScaleY)))
      desc.MotionVectorScaleY = 0;
    if (NVNGX_FAILED(parameters->Get(NVNGX_Parameter_DLSS_Pre_Exposure, &desc.PreExposure)))
      desc.PreExposure = 0;

    pCtxExt->TemporalUpscale(&desc);

    pCtxExt->Release();

    return NVNGX_RESULT_OK;
    break;
  }
  default:
    break;
  }
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Shutdown() {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_Shutdown1(ID3D11Device *device) {
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetScratchBufferSize(unsigned int feature, const NVNGXParameter *params, size_t *out_size) {
  IMPLEMENT_ME
  return NVNGX_RESULT_FAIL;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_ReleaseFeature(unsigned int *handle) {
  switch (static_cast<CommonFeature *>((void *)handle)->feature) {
  case NVNGX_FEATURE_SUPERSAMPLING:
    delete static_cast<DLSSFeature *>((void *)handle);
    break;
  default:
    break;
  }
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_DestroyParameters(NVNGXParameter *params) {
  delete static_cast<ParametersImpl *>(params);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_AllocateParameters(NVNGXParameter **out_params) {
  *out_params = new ParametersImpl();
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_UpdateFeature(const void *app_id, const unsigned int feature) {
  // do nothing
  return NVNGX_RESULT_OK;
}

inline static NVNGX_RESULT
NVNGX_DLSS_GetOptimalSettingsCallback(NVNGXParameter *params) {
  unsigned int width;
  unsigned int height;
  unsigned int out_width;
  unsigned int out_height;
  float scale = 0.0f;
  NVNGX_PERFQUALITY perf_quality_value;

  if (params->Get(NVNGX_Parameter_Width, &width) != NVNGX_RESULT_OK ||
      params->Get(NVNGX_Parameter_Height, &height) != NVNGX_RESULT_OK ||
      params->Get(NVNGX_Parameter_PerfQualityValue, (int *)&perf_quality_value) != NVNGX_RESULT_OK)
    return NVNGX_RESULT_FAIL;

  switch (perf_quality_value) {
  case NVNGX_PERFQUALITY_ULTRA_PERFORMANCE:
    scale = 1.0f / 3.0f;
    break;
  case NVNGX_PERFQUALITY_MAXPERF:
    scale = 0.5f;
    break;
  case NVNGX_PERFQUALITY_MAXQUALITY:
    scale = 1.0f / 1.5f;
    break;
  case NVNGX_PERFQUALITY_ULTRA_QUALITY:
    scale = 1.0f / 1.3f;
    break;
  case NVNGX_PERFQUALITY_DLAA:
    scale = 1.0f;
    break;
  default:
    scale = 58.0f / 100.0f;
    break;
  }

  out_width = std::ceil(width * scale);
  out_height = std::ceil(height * scale);

  params->Set(NVNGX_Parameter_Scale, scale);
  params->Set(NVNGX_Parameter_SuperSampling_ScaleFactor, scale);
  params->Set(NVNGX_Parameter_OutWidth, out_width);
  params->Set(NVNGX_Parameter_OutHeight, out_height);
  params->Set(NVNGX_Parameter_Sharpness, 0.0f);

  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width, (unsigned int)std::ceil(width / 3));
  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height, (unsigned int)std::ceil(height / 3));

  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width, width);
  params->Set(NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height, height);

  params->Set("DLSSMode", 1);

  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_DLAA, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Quality, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Balanced, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_Performance, (unsigned int)0);
  params->Set(NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, (unsigned int)0);

  return NVNGX_RESULT_OK;
}

inline static NVNGX_RESULT
NVNGX_DLSS_GetStatsCallback(NVNGXParameter *params) {
  params->Set("SizeInBytes", 0ull);
  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetCapabilityParameters(NVNGXParameter **out_params) {
  auto out_parameters = new ParametersImpl();

  out_parameters->Set("SuperSampling.Available", 1u);
  out_parameters->Set(NVSDK_NGX_EParameter_SuperSampling_Available, 1u);
  out_parameters->Set("SuperSampling.MinDriverVersionMajor", 0);
  out_parameters->Set("SuperSampling.MinDriverVersionMinor", 0);

  out_parameters->Set("SuperSampling.NeedsUpdatedDriver", 0);
  out_parameters->Set("SuperSampling.FeatureInitResult", 1u);
  out_parameters->Set("Snippet.OptLevel", 0);
  out_parameters->Set("Snippet.IsDevBranch", 0);
  out_parameters->Set(NVNGX_Parameter_DLSSOptimalSettingsCallback, (void *)&NVNGX_DLSS_GetOptimalSettingsCallback);
  out_parameters->Set("DLSSGetStatsCallback", (void *)&NVNGX_DLSS_GetStatsCallback);
  out_parameters->Set(NVNGX_Parameter_Sharpness, 0.0f);
  out_parameters->Set(NVNGX_Parameter_MV_Scale_X, 1.0f);
  out_parameters->Set(NVNGX_Parameter_MV_Scale_Y, 1.0f);
  out_parameters->Set("MV.Offset.X", 0.0f);
  out_parameters->Set("MV.Offset.Y", 0.0f);
  out_parameters->Set("DLSS.Exposure.Scale", 1.0f);
  out_parameters->Set(NVNGX_Parameter_PerfQualityValue, 2);

  out_parameters->Set("CreationNodeMask", 1);
  out_parameters->Set("VisibilityNodeMask", 1);
  out_parameters->Set("DLSS.Enable.Output.Subrects", 1);
  out_parameters->Set("RTXValue", 0);

  *out_params = out_parameters;

  return NVNGX_RESULT_OK;
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetParameters(NVNGXParameter **out_params) {
  // FIXME: leak!
  return NVSDK_NGX_D3D11_GetCapabilityParameters(out_params);
}

NVNGX_API NVNGX_RESULT
NVSDK_NGX_D3D11_GetFeatureRequirements(
    IDXGIAdapter *adapter, const NVNGX_FeatureDiscoveryInfo *discovery_info,
    NVNGX_FeatureRequirement *requirement
) {
  if (!requirement)
    return NVNGX_RESULT_INVALID_PARAMETER;
  if (discovery_info->FeatureID == NVNGX_FEATURE_SUPERSAMPLING) {
    requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_SUPPORTED;
    requirement->MinHWArchitecture = 0;
    strcpy_s(requirement->MinOSVersion, "10.0.16299.0"); // v1709
    return NVNGX_RESULT_OK;
  }
  WARN("NVSDK_NGX_D3D11_GetFeatureRequirements: unsupported feature ", discovery_info->FeatureID);
  requirement->FeatureSupported = NVNGX_FEATURE_SUPPORT_RESULT_UNSUPPORTED;
  return NVNGX_RESULT_FAIL;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason,
                               LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);

  if (FAILED(DXGIGetDebugInterface1(0, DXMT_NVEXT_GUID, nullptr))) {
    return FALSE;
  }

  return TRUE;
}

}; // namespace dxmt