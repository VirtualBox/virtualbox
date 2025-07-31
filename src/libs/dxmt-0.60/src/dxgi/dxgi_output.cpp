#include <algorithm>
#include <numeric>

#include "com/com_guid.hpp"
#include "com/com_pointer.hpp"
#include "dxgi_interfaces.h"
#include "dxgi_object.hpp"
#include "dxmt_format.hpp"
#include "log/log.hpp"
#include "util_string.hpp"
#include "wsi_monitor.hpp"

namespace dxmt {

/*
 * \see
 * https://github.com/microsoft/DirectXTex/blob/main/DirectXTex/DirectXTexUtil.cpp
 */
uint32_t GetMonitorFormatBpp(DXGI_FORMAT Format) {
  switch (Format) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    return 32;

  case DXGI_FORMAT_R16G16B16A16_FLOAT:
    return 64;

  default:
    Logger::warn(str::format("GetMonitorFormatBpp: Unknown format: ", Format));
    return 32;
  }
}

DXGI_MODE_DESC1 ConvertDisplayMode(const wsi::WsiMode &WsiMode) {
  DXGI_MODE_DESC1 dxgiMode = {};
  dxgiMode.Width = WsiMode.width;
  dxgiMode.Height = WsiMode.height;
  dxgiMode.RefreshRate = DXGI_RATIONAL{WsiMode.refreshRate.numerator,
                                       WsiMode.refreshRate.denominator};
  dxgiMode.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // FIXME
  dxgiMode.ScanlineOrdering = WsiMode.interlaced
                                  ? DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST
                                  : DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
  dxgiMode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
  dxgiMode.Stereo = FALSE;
  return dxgiMode;
}

void FilterModesByDesc(std::vector<DXGI_MODE_DESC1> &Modes,
                       const DXGI_MODE_DESC1 &TargetMode) {
  // Filter modes based on format properties
  bool testScanlineOrder = false;
  bool testScaling = false;
  bool testFormat = false;

  for (const auto &mode : Modes) {
    testScanlineOrder |=
        TargetMode.ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED &&
        TargetMode.ScanlineOrdering == mode.ScanlineOrdering;
    testScaling |= TargetMode.Scaling != DXGI_MODE_SCALING_UNSPECIFIED &&
                   TargetMode.Scaling == mode.Scaling;
    testFormat |= TargetMode.Format != DXGI_FORMAT_UNKNOWN &&
                  TargetMode.Format == mode.Format;
  }

  for (auto it = Modes.begin(); it != Modes.end();) {
    bool skipMode = it->Stereo != TargetMode.Stereo;

    if (testScanlineOrder)
      skipMode |= it->ScanlineOrdering != TargetMode.ScanlineOrdering;

    if (testScaling)
      skipMode |= it->Scaling != TargetMode.Scaling;

    if (testFormat)
      skipMode |= it->Format != TargetMode.Format;

    it = skipMode ? Modes.erase(it) : ++it;
  }

  // Filter by closest resolution
  uint32_t minDiffResolution = 0;

  if (TargetMode.Width) {
    minDiffResolution = std::accumulate(
        Modes.begin(), Modes.end(), std::numeric_limits<uint32_t>::max(),
        [&TargetMode](uint32_t current, const DXGI_MODE_DESC1 &mode) {
          uint32_t diff = std::abs(int32_t(TargetMode.Width - mode.Width)) +
                          std::abs(int32_t(TargetMode.Height - mode.Height));
          return std::min(current, diff);
        });

    for (auto it = Modes.begin(); it != Modes.end();) {
      uint32_t diff = std::abs(int32_t(TargetMode.Width - it->Width)) +
                      std::abs(int32_t(TargetMode.Height - it->Height));

      bool skipMode = diff != minDiffResolution;
      it = skipMode ? Modes.erase(it) : ++it;
    }
  }

  // Filter by closest refresh rate
  uint32_t minDiffRefreshRate = 0;

  if (TargetMode.RefreshRate.Numerator && TargetMode.RefreshRate.Denominator) {
    minDiffRefreshRate = std::accumulate(
        Modes.begin(), Modes.end(), std::numeric_limits<uint64_t>::max(),
        [&TargetMode](uint64_t current, const DXGI_MODE_DESC1 &mode) {
          uint64_t rate = uint64_t(mode.RefreshRate.Numerator) *
                          uint64_t(TargetMode.RefreshRate.Denominator) /
                          uint64_t(mode.RefreshRate.Denominator);
          uint64_t diff = std::abs(
              int64_t(rate - uint64_t(TargetMode.RefreshRate.Numerator)));
          return std::min(current, diff);
        });

    for (auto it = Modes.begin(); it != Modes.end();) {
      uint64_t rate = uint64_t(it->RefreshRate.Numerator) *
                      uint64_t(TargetMode.RefreshRate.Denominator) /
                      uint64_t(it->RefreshRate.Denominator);
      uint64_t diff =
          std::abs(int64_t(rate - uint64_t(TargetMode.RefreshRate.Numerator)));

      bool skipMode = diff != minDiffRefreshRate;
      it = skipMode ? Modes.erase(it) : ++it;
    }
  }
}

class MTLDXGIOutput : public MTLDXGIObject<IDXGIOutput6> {
public:
  MTLDXGIOutput(IMTLDXGIAdapter *adapter, HMONITOR monitor)
      : adapter_(adapter), monitor_(monitor) {
    WMTGetDisplayDescription(monitor_ == wsi::getDefaultMonitor()
                                 ? WMTGetPrimaryDisplayId()
                                 : WMTGetSecondaryDisplayId(),
                             &native_desc_);
  }

  ~MTLDXGIOutput() {}

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) final {

    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIOutput) || riid == __uuidof(IDXGIOutput1) ||
        riid == __uuidof(IDXGIOutput2) || riid == __uuidof(IDXGIOutput3) ||
        riid == __uuidof(IDXGIOutput4) || riid == __uuidof(IDXGIOutput5) ||
        riid == __uuidof(IDXGIOutput6)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IDXGIOutput), riid)) {
      WARN("DXGIOutput: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetParent(REFIID riid, void **ppParent) final {
    return adapter_->QueryInterface(riid, ppParent);
  }

  HRESULT
  STDMETHODCALLTYPE
  GetDesc(DXGI_OUTPUT_DESC *pDesc) final {
    if (pDesc == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    if (!wsi::getDesktopCoordinates(monitor_, &pDesc->DesktopCoordinates)) {
      ERR("Failed to query monitor coords");
      return E_FAIL;
    }

    if (!wsi::getDisplayName(monitor_, pDesc->DeviceName)) {
      ERR("Failed to query monitor name");
      return E_FAIL;
    }

    pDesc->AttachedToDesktop = 1;
    pDesc->Rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
    pDesc->Monitor = monitor_;
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetDisplayModeList(DXGI_FORMAT EnumFormat, UINT Flags, UINT *pNumModes,
                     DXGI_MODE_DESC *pDesc) final {
    if (pNumModes == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    std::vector<DXGI_MODE_DESC1> modes;

    if (pDesc)
      modes.resize(std::max(1u, *pNumModes));

    HRESULT hr = GetDisplayModeList1(EnumFormat, Flags, pNumModes,
                                     pDesc ? modes.data() : nullptr);

    if (pDesc) {
      for (uint32_t i = 0; i < *pNumModes && i < modes.size(); i++) {
        pDesc[i].Width = modes[i].Width;
        pDesc[i].Height = modes[i].Height;
        pDesc[i].RefreshRate = modes[i].RefreshRate;
        pDesc[i].Format = modes[i].Format;
        pDesc[i].ScanlineOrdering = modes[i].ScanlineOrdering;
        pDesc[i].Scaling = modes[i].Scaling;
      }
    }

    return hr;
  }

  HRESULT
  STDMETHODCALLTYPE
  FindClosestMatchingMode(const DXGI_MODE_DESC *pModeToMatch,
                          DXGI_MODE_DESC *pClosestMatch,
                          IUnknown *pConcernedDevice) final {
    if (!pModeToMatch || !pClosestMatch)
      return DXGI_ERROR_INVALID_CALL;

    DXGI_MODE_DESC1 modeToMatch;
    modeToMatch.Width = pModeToMatch->Width;
    modeToMatch.Height = pModeToMatch->Height;
    modeToMatch.RefreshRate = pModeToMatch->RefreshRate;
    modeToMatch.Format = pModeToMatch->Format;
    modeToMatch.ScanlineOrdering = pModeToMatch->ScanlineOrdering;
    modeToMatch.Scaling = pModeToMatch->Scaling;
    modeToMatch.Stereo = FALSE;

    DXGI_MODE_DESC1 closestMatch = {};

    HRESULT hr =
        FindClosestMatchingMode1(&modeToMatch, &closestMatch, pConcernedDevice);

    if (FAILED(hr))
      return hr;

    pClosestMatch->Width = closestMatch.Width;
    pClosestMatch->Height = closestMatch.Height;
    pClosestMatch->RefreshRate = closestMatch.RefreshRate;
    pClosestMatch->Format = closestMatch.Format;
    pClosestMatch->ScanlineOrdering = closestMatch.ScanlineOrdering;
    pClosestMatch->Scaling = closestMatch.Scaling;
    return hr;
  }

  HRESULT
  STDMETHODCALLTYPE
  WaitForVBlank() final { return S_OK; }

  HRESULT
  STDMETHODCALLTYPE
  TakeOwnership(IUnknown *device, BOOL exclusive) final {
    WARN("TakeOwnership: Stub");
    return S_OK;
  }

  void STDMETHODCALLTYPE ReleaseOwnership() final {
    WARN("ReleaseOwnership: Stub");
  }

  HRESULT
  STDMETHODCALLTYPE
  GetGammaControlCapabilities(
      DXGI_GAMMA_CONTROL_CAPABILITIES *gamma_caps) final {
    if (gamma_caps == nullptr)
      return E_NOTIMPL;

    gamma_caps->ScaleAndOffsetSupported = false;
    gamma_caps->MaxConvertedValue = 1.0f;
    gamma_caps->MinConvertedValue = 0.0f;
    gamma_caps->NumGammaControlPoints = 1;
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  SetGammaControl(const DXGI_GAMMA_CONTROL *gamma_control) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetGammaControl(DXGI_GAMMA_CONTROL *gamma_control) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE
  SetDisplaySurface(IDXGISurface *surface) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetDisplaySurfaceData(IDXGISurface *surface) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetFrameStatistics(DXGI_FRAME_STATISTICS *stats) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetDisplayModeList1(DXGI_FORMAT EnumFormat, UINT Flags, UINT *pNumModes,
                      DXGI_MODE_DESC1 *pDesc) final {
    if (pNumModes == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    // Special case, just return zero modes
    if (EnumFormat == DXGI_FORMAT_UNKNOWN) {
      *pNumModes = 0;
      return S_OK;
    }
    MTL_DXGI_FORMAT_DESC formatDesc;
    if (FAILED(MTLQueryDXGIFormat(this->adapter_->GetMTLDevice(), EnumFormat, formatDesc))
      || !(formatDesc.Flag & MTL_DXGI_FORMAT_BACKBUFFER)) {
      *pNumModes = 0;
      return S_OK;
    }

    // Walk over all modes that the display supports and
    // return those that match the requested format etc.
    wsi::WsiMode devMode = {};

    uint32_t srcModeId = 0;
    uint32_t dstModeId = 0;

    std::vector<DXGI_MODE_DESC1> modeList;

    while (wsi::getDisplayMode(monitor_, srcModeId++, &devMode)) {
      // Only enumerate interlaced modes if requested.
      if (devMode.interlaced && !(Flags & DXGI_ENUM_MODES_INTERLACED))
        continue;

      // Skip modes with incompatible formats
      if (devMode.bitsPerPixel != GetMonitorFormatBpp(EnumFormat))
        continue;

      if (pDesc != nullptr) {
        DXGI_MODE_DESC1 mode = ConvertDisplayMode(devMode);
        // Fix up the DXGI_FORMAT to match what we were enumerating.
        mode.Format = EnumFormat;

        modeList.push_back(mode);
      }

      dstModeId += 1;
    }

    // Sort display modes by width, height and refresh rate,
    // in that order. Some games rely on correct ordering.
    std::sort(modeList.begin(), modeList.end(),
              [](const DXGI_MODE_DESC1 &a, const DXGI_MODE_DESC1 &b) {
                if (a.Width < b.Width)
                  return true;
                if (a.Width > b.Width)
                  return false;

                if (a.Height < b.Height)
                  return true;
                if (a.Height > b.Height)
                  return false;

                return (a.RefreshRate.Numerator / a.RefreshRate.Denominator) <
                       (b.RefreshRate.Numerator / b.RefreshRate.Denominator);
              });

    // If requested, write out the first set of display
    // modes to the destination array.
    if (pDesc != nullptr) {
      for (uint32_t i = 0; i < *pNumModes && i < dstModeId; i++)
        pDesc[i] = modeList[i];

      if (dstModeId > *pNumModes)
        return DXGI_ERROR_MORE_DATA;
    }

    *pNumModes = dstModeId;
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  FindClosestMatchingMode1(const DXGI_MODE_DESC1 *pModeToMatch,
                           DXGI_MODE_DESC1 *pClosestMatch,
                           IUnknown *pConcernedDevice) final {
    if (!pModeToMatch || !pClosestMatch)
      return DXGI_ERROR_INVALID_CALL;

    if (pModeToMatch->Format == DXGI_FORMAT_UNKNOWN && !pConcernedDevice)
      return DXGI_ERROR_INVALID_CALL;

    // Both or neither must be zero
    if ((pModeToMatch->Width == 0) ^ (pModeToMatch->Height == 0))
      return DXGI_ERROR_INVALID_CALL;

    wsi::WsiMode activeWsiMode = {};
    wsi::getCurrentDisplayMode(monitor_, &activeWsiMode);

    DXGI_MODE_DESC1 activeMode = ConvertDisplayMode(activeWsiMode);

    DXGI_MODE_DESC1 defaultMode;
    defaultMode.Width = 0;
    defaultMode.Height = 0;
    defaultMode.RefreshRate = {0, 0};
    defaultMode.Format = DXGI_FORMAT_UNKNOWN;
    defaultMode.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    defaultMode.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    defaultMode.Stereo = pModeToMatch->Stereo;

    if (pModeToMatch->ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED)
      defaultMode.ScanlineOrdering = activeMode.ScanlineOrdering;

    if (pModeToMatch->Scaling == DXGI_MODE_SCALING_UNSPECIFIED)
      defaultMode.Scaling = activeMode.Scaling;

    DXGI_FORMAT targetFormat = pModeToMatch->Format;

    if (pModeToMatch->Format == DXGI_FORMAT_UNKNOWN) {
      defaultMode.Format = activeMode.Format;
      targetFormat = activeMode.Format;
    }

    if (!pModeToMatch->Width) {
      defaultMode.Width = activeMode.Width;
      defaultMode.Height = activeMode.Height;
    }

    if (!pModeToMatch->RefreshRate.Numerator ||
        !pModeToMatch->RefreshRate.Denominator) {
      defaultMode.RefreshRate.Numerator = activeMode.RefreshRate.Numerator;
      defaultMode.RefreshRate.Denominator = activeMode.RefreshRate.Denominator;
    }

    UINT modeCount = 0;
    GetDisplayModeList1(targetFormat, DXGI_ENUM_MODES_SCALING, &modeCount,
                        nullptr);

    if (modeCount == 0) {
      ERR("No modes found");
      return DXGI_ERROR_NOT_FOUND;
    }

    std::vector<DXGI_MODE_DESC1> modes(modeCount);
    GetDisplayModeList1(targetFormat, DXGI_ENUM_MODES_SCALING, &modeCount,
                        modes.data());

    FilterModesByDesc(modes, *pModeToMatch);
    FilterModesByDesc(modes, defaultMode);

    if (modes.empty())
      return DXGI_ERROR_NOT_FOUND;

    *pClosestMatch = modes[0];

    Logger::debug(str::format("DXGI: For mode ", pModeToMatch->Width, "x",
                              pModeToMatch->Height, "@",
                              pModeToMatch->RefreshRate.Denominator
                                  ? (pModeToMatch->RefreshRate.Numerator /
                                     pModeToMatch->RefreshRate.Denominator)
                                  : 0,
                              " found closest mode ", pClosestMatch->Width, "x",
                              pClosestMatch->Height, "@",
                              pClosestMatch->RefreshRate.Denominator
                                  ? (pClosestMatch->RefreshRate.Numerator /
                                     pClosestMatch->RefreshRate.Denominator)
                                  : 0));
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetDisplaySurfaceData1(IDXGIResource *destination) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE
  DuplicateOutput(IUnknown *pDevice,
                  IDXGIOutputDuplication **ppOutputDuplication) final {
    InitReturnPtr(ppOutputDuplication);

    if (!pDevice)
      return E_INVALIDARG;

    ERR("Not implemented");

    // At least return a valid error code
    return DXGI_ERROR_UNSUPPORTED;
  }

  HRESULT
  STDMETHODCALLTYPE
  CheckOverlaySupport(DXGI_FORMAT enum_format,
                              IUnknown *concerned_device,
                              UINT *flags) override {
    if (flags) {
      *flags = 0;
    }
    return S_OK;
  }

  WINBOOL STDMETHODCALLTYPE SupportsOverlays() override { return FALSE; }

  HRESULT STDMETHODCALLTYPE CheckOverlayColorSpaceSupport(DXGI_FORMAT format,
                                        DXGI_COLOR_SPACE_TYPE colour_space,
                                        IUnknown *device,
                                        UINT *flags) override {
    if (flags) {
      *flags = 0;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_OUTPUT_DESC1 *pDesc) override {
    if (pDesc == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    if (!wsi::getDesktopCoordinates(monitor_, &pDesc->DesktopCoordinates)) {
      ERR("Failed to query monitor coords");
      return E_FAIL;
    }

    if (!wsi::getDisplayName(monitor_, pDesc->DeviceName)) {
      ERR("Failed to query monitor name");
      return E_FAIL;
    }

    pDesc->AttachedToDesktop = 1;
    pDesc->Rotation = DXGI_MODE_ROTATION_UNSPECIFIED;
    pDesc->Monitor = monitor_;
    pDesc->BitsPerColor = native_desc_.maximum_potential_edr_color_component_value > 1 ? 10: 8;
    pDesc->ColorSpace = native_desc_.maximum_potential_edr_color_component_value > 1
            ? DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020
            : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    memcpy(pDesc->RedPrimary, native_desc_.red_primaries, 8);
    memcpy(pDesc->GreenPrimary, native_desc_.green_primaries, 8);
    memcpy(pDesc->BluePrimary, native_desc_.blue_primaries, 8);
    memcpy(pDesc->WhitePoint, native_desc_.white_points, 8);
    pDesc->MinLuminance = 0.0f;
    pDesc->MaxLuminance = native_desc_.maximum_potential_edr_color_component_value * 100.0f;
    pDesc->MaxFullFrameLuminance = pDesc->MaxLuminance;
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  CheckHardwareCompositionSupport(UINT *flags) override {
    if (flags) {
      *flags = 0;
    }
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  DuplicateOutput1(IUnknown *pDevice, UINT flags, UINT format_count,
                   const DXGI_FORMAT *formats,
                   IDXGIOutputDuplication **ppOutputDuplication) override {
    InitReturnPtr(ppOutputDuplication);
    if (!pDevice)
      return E_INVALIDARG;

    ERR("Not implemented");

    return DXGI_ERROR_UNSUPPORTED;
  }

private:
  Com<IMTLDXGIAdapter> adapter_ = nullptr;
  HMONITOR monitor_ = nullptr;
  WMTDisplayDescription native_desc_;
};

Com<IDXGIOutput> CreateOutput(IMTLDXGIAdapter *pAadapter, HMONITOR monitor) {
  return Com<IDXGIOutput>::transfer(new MTLDXGIOutput(pAadapter, monitor));
};

} // namespace dxmt