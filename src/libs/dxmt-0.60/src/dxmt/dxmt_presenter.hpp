#pragma once
#include "Metal.hpp"
#include "dxmt_command.hpp"
#include "rc/util_rc.hpp"
#include "winemetal.h"
#include <atomic>

namespace dxmt {
class Presenter : public RcObject {
public:
  Presenter(WMT::Device device, WMT::MetalLayer layer, InternalCommandLibrary &lib, float scale_factor);

  bool changeLayerProperties(WMTPixelFormat format, WMTColorSpace colorspace, unsigned width, unsigned height);

  bool changeLayerColorSpace(WMTColorSpace colorspace);

  void checkDisplaySetting();

  void changeHDRMetadata(const WMTHDRMetadata *metadata);

  WMT::MetalDrawable encodeCommands(WMT::CommandBuffer cmdbuf, WMT::Fence fence, WMT::Texture backbuffer);

private:
  void buildRenderPipelineState(bool is_pq, bool with_hdr_metadata);

  WMT::Device device_;
  WMT::MetalLayer layer_;
  InternalCommandLibrary &lib_;
  WMTLayerProps layer_props_;
  WMTColorSpace colorspace_ = WMTColorSpaceSRGB;
  WMTHDRMetadata hdr_metadata_;
  bool has_hdr_metadata_ = false;
  WMTPixelFormat source_format_ = WMTPixelFormatInvalid;
  uint64_t display_setting_version_ = 0;
  WMTColorSpace display_colorspace_ = WMTColorSpaceSRGB;
  WMTHDRMetadata display_hdr_metadata_;
  WMTEDRValue display_edr_value_{0.0, 1.0};
  WMT::Reference<WMT::RenderPipelineState> present_blit_;
  WMT::Reference<WMT::RenderPipelineState> present_scale_;
  std::atomic_flag pso_valid = 0;
};
} // namespace dxmt