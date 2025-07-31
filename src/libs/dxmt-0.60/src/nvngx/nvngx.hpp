#pragma once

struct ID3D11Resource;
struct ID3D12Resource;

#define NVNGX_API extern "C"

enum NVNGX_RESULT : unsigned int {
  NVNGX_RESULT_OK = 1,
  NVNGX_RESULT_FAIL = 0xbad0000,
  NVNGX_RESULT_INVALID_PARAMETER = 0xbad0005,
};

#define NVNGX_FAILED(result) (result & NVNGX_RESULT_FAIL) == NVNGX_RESULT_FAIL

enum NVNGX_FEATURE : unsigned int {
  NVNGX_FEATURE_SUPERSAMPLING = 1,
};

enum NVNGX_DLSS_FLAG: unsigned int {
  NVNGX_DLSS_FLAG_MV_JITTERED = 1 << 2,
  NVNGX_DLSS_FLAG_DEPTH_INVERTED = 1 << 3,
  NVNGX_DLSS_FLAG_AUTO_EXPOSURE = 1 << 6,
};

enum NVNGX_PERFQUALITY: unsigned int {
  NVNGX_PERFQUALITY_MAXPERF,
  NVNGX_PERFQUALITY_BALANCED,
  NVNGX_PERFQUALITY_MAXQUALITY,
  NVNGX_PERFQUALITY_ULTRA_PERFORMANCE,
  NVNGX_PERFQUALITY_ULTRA_QUALITY,
  NVNGX_PERFQUALITY_DLAA,
};

class NVNGXParameter { // vtable seems to be inconsistent?
public:
  virtual void Set(const char *name, void *value) = 0;
  virtual void Set(const char *name, ID3D12Resource *value) = 0;
  virtual void Set(const char *name, ID3D11Resource *value) = 0;
  virtual void Set(const char *name, int value) = 0;
  virtual void Set(const char *name, unsigned int value) = 0;
  virtual void Set(const char *name, double value) = 0;
  virtual void Set(const char *name, float value) = 0;
  virtual void Set(const char *name, unsigned long long value) = 0;

  virtual NVNGX_RESULT Get(const char *name, void **out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, ID3D12Resource **out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, double *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, unsigned int *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, int *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, ID3D11Resource **out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, float *out) const = 0;
  virtual NVNGX_RESULT Get(const char *name, unsigned long long *out) const = 0;

  virtual void Reset() = 0;
};

struct NVNGX_FeatureDiscoveryInfo
{
    unsigned int SDKVersion;
    NVNGX_FEATURE FeatureID;
    char Identifier[32];
    const wchar_t* ApplicationDataPath;
    const void* FeatureInfo;
};

enum NVNGX_FEATURE_SUPPORT_RESULT: unsigned int {

  NVNGX_FEATURE_SUPPORT_RESULT_SUPPORTED = 0,
  NVNGX_FEATURE_SUPPORT_RESULT_UNSUPPORTED = 1,
};

struct NVNGX_FeatureRequirement {
  NVNGX_FEATURE_SUPPORT_RESULT FeatureSupported;
  unsigned int MinHWArchitecture;
  char MinOSVersion[255];
};

#define NVNGX_Parameter_Width "Width"
#define NVNGX_Parameter_Height "Height"
#define NVNGX_Parameter_OutWidth "OutWidth"
#define NVNGX_Parameter_OutHeight "OutHeight"
#define NVNGX_Parameter_PerfQualityValue "PerfQualityValue"
#define NVNGX_Parameter_DLSS_Feature_Create_Flags "DLSS.Feature.Create.Flags"
#define NVNGX_Parameter_Color "Color"
#define NVNGX_Parameter_Output "Output"
#define NVNGX_Parameter_MotionVectors "MotionVectors"
#define NVNGX_Parameter_Depth "Depth"
#define NVNGX_Parameter_Reset "Reset"
#define NVNGX_Parameter_ExposureTexture "ExposureTexture"
#define NVNGX_Parameter_DLSS_Pre_Exposure "DLSS.Pre.Exposure"
#define NVNGX_Parameter_MV_Scale_X "MV.Scale.X"
#define NVNGX_Parameter_MV_Scale_Y "MV.Scale.Y"
#define NVNGX_Parameter_Jitter_Offset_X "Jitter.Offset.X"
#define NVNGX_Parameter_Jitter_Offset_Y "Jitter.Offset.Y"
#define NVNGX_Parameter_DLSSOptimalSettingsCallback "DLSSOptimalSettingsCallback"
#define NVNGX_Parameter_SuperSampling_ScaleFactor "SuperSampling.ScaleFactor"
#define NVNGX_Parameter_Sharpness "Sharpness"
#define NVNGX_Parameter_Scale "Scale"

#define NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Width  "DLSS.Render.Subrect.Dimensions.Width"
#define NVNGX_Parameter_DLSS_Render_Subrect_Dimensions_Height "DLSS.Render.Subrect.Dimensions.Height"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Width "DLSS.Get.Dynamic.Max.Render.Width"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Max_Render_Height "DLSS.Get.Dynamic.Max.Render.Height"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Width "DLSS.Get.Dynamic.Min.Render.Width"
#define NVNGX_Parameter_DLSS_Get_Dynamic_Min_Render_Height "DLSS.Get.Dynamic.Min.Render.Height"

#define NVNGX_Parameter_DLSS_Hint_Render_Preset_DLAA "DLSS.Hint.Render.Preset.DLAA"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_Quality "DLSS.Hint.Render.Preset.Quality"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_Balanced "DLSS.Hint.Render.Preset.Balanced"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_Performance "DLSS.Hint.Render.Preset.Performance"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance "DLSS.Hint.Render.Preset.UltraPerformance"
#define NVNGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality "DLSS.Hint.Render.Preset.UltraQuality"

#define NVSDK_NGX_EParameter_SuperSampling_Available              "#\x01"
