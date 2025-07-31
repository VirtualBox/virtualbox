#include "ftl.hpp"
#include "dxmt_format.hpp"
#include "util_error.hpp"
#include "dxgi.h"
#include <cassert>
#include <cstdint>

#define APPEND_CAP(format, caps) textureCapabilities[format] = textureCapabilities[format] | caps;

namespace dxmt {

constexpr FormatCapability ALL_CAP = static_cast<FormatCapability>(
    FormatCapability::Filter | FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA |
    FormatCapability::Blend | FormatCapability::Sparse | FormatCapability::Resolve
);
constexpr FormatCapability TEXTURE_BUFFER_ALL_CAP = static_cast<FormatCapability>(
    FormatCapability::TextureBufferRead | FormatCapability::TextureBufferWrite |
    FormatCapability::TextureBufferReadWrite
);
constexpr FormatCapability TEXTURE_BUFFER_READ_OR_WRITE =
    static_cast<FormatCapability>(FormatCapability::TextureBufferRead | FormatCapability::TextureBufferWrite);
constexpr FormatCapability NO_ATOMIC_RESOLVE = static_cast<FormatCapability>(
    FormatCapability::Filter | FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA |
    FormatCapability::Blend | FormatCapability::Sparse
);
constexpr FormatCapability APPLE_INT_FORMAT_CAP =
    FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA | FormatCapability::Sparse;
constexpr FormatCapability NONAPPLE_INT_FORMAT_CAP =
    FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA | FormatCapability::Sparse;

constexpr FormatCapability APPLE_INT_FORMAT_CAP_32 =
    FormatCapability::Write | FormatCapability::Color | FormatCapability::Sparse | FormatCapability::Atomic;

void
FormatCapabilityInspector::Inspect(WMT::Device device) {
  if (device.supportsFamily(WMTGPUFamilyApple7)) {
    // Apple 7: M1
    APPEND_CAP(WMTPixelFormatA8Unorm, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR8Unorm, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR8Unorm_sRGB, ALL_CAP)
    APPEND_CAP(WMTPixelFormatR8Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatR8Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR8Sint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)

    APPEND_CAP(WMTPixelFormatR16Unorm, NO_ATOMIC_RESOLVE | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatR16Snorm, NO_ATOMIC_RESOLVE | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatR16Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR16Sint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR16Float, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRG8Unorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG8Unorm_sRGB, ALL_CAP)
    APPEND_CAP(WMTPixelFormatRG8Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG8Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG8Sint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)

    APPEND_CAP(WMTPixelFormatB5G6R5Unorm, NO_ATOMIC_RESOLVE)
    APPEND_CAP(WMTPixelFormatA1BGR5Unorm, NO_ATOMIC_RESOLVE)
    APPEND_CAP(WMTPixelFormatABGR4Unorm, NO_ATOMIC_RESOLVE)
    APPEND_CAP(WMTPixelFormatBGR5A1Unorm, NO_ATOMIC_RESOLVE)

    APPEND_CAP(WMTPixelFormatR32Uint, APPLE_INT_FORMAT_CAP_32 | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR32Sint, APPLE_INT_FORMAT_CAP_32 | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(
        WMTPixelFormatR32Float, FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA |
                                      FormatCapability::Blend | FormatCapability::Sparse | TEXTURE_BUFFER_ALL_CAP
    )
    APPEND_CAP(WMTPixelFormatRG16Unorm, NO_ATOMIC_RESOLVE | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Snorm, NO_ATOMIC_RESOLVE | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Sint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Float, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGBA8Unorm, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA8Unorm_sRGB, ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA8Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGBA8Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA8Sint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatBGRA8Unorm, ALL_CAP | FormatCapability::TextureBufferRead)
    APPEND_CAP(WMTPixelFormatBGRA8Unorm_sRGB, ALL_CAP)

    // 32-bit packed
    APPEND_CAP(WMTPixelFormatRGB10A2Unorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatBGR10A2Unorm, ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGB10A2Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG11B10Float, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGB9E5Float, ALL_CAP)

    // 64-bit
    APPEND_CAP(WMTPixelFormatRG32Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG32Sint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(
        WMTPixelFormatRG32Float, FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA |
                                       FormatCapability::Blend | FormatCapability::Sparse | TEXTURE_BUFFER_READ_OR_WRITE
    )
    APPEND_CAP(WMTPixelFormatRGBA16Unorm, NO_ATOMIC_RESOLVE | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGBA16Snorm, NO_ATOMIC_RESOLVE | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGBA16Uint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA16Sint, APPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA16Float, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)

    // 128-bit
    APPEND_CAP(
        WMTPixelFormatRGBA32Uint,
        FormatCapability::Write | FormatCapability::Color | FormatCapability::Sparse | TEXTURE_BUFFER_ALL_CAP
    )
    APPEND_CAP(
        WMTPixelFormatRGBA32Sint,
        FormatCapability::Write | FormatCapability::Color | FormatCapability::Sparse | TEXTURE_BUFFER_ALL_CAP
    )
    APPEND_CAP(
        WMTPixelFormatRGBA32Float, FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA |
                                         FormatCapability::Sparse | FormatCapability::Blend | TEXTURE_BUFFER_ALL_CAP
    )

    // compressed
    if (device.supportsBCTextureCompression()) {
      // BC
      APPEND_CAP(WMTPixelFormatBC1_RGBA, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC1_RGBA_sRGB, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC2_RGBA, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC2_RGBA_sRGB, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC3_RGBA, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC3_RGBA_sRGB, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC4_RUnorm, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC4_RSnorm, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC5_RGSnorm, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC5_RGUnorm, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC6H_RGBUfloat, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC6H_RGBFloat, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC7_RGBAUnorm, FormatCapability::Filter | FormatCapability::Sparse)
      APPEND_CAP(WMTPixelFormatBC7_RGBAUnorm_sRGB, FormatCapability::Filter | FormatCapability::Sparse)
    }

    // YUV
    APPEND_CAP(WMTPixelFormatGBGR422, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBGRG422, FormatCapability::Filter)

    // depth & stencil
    APPEND_CAP(
        WMTPixelFormatDepth16Unorm,
        FormatCapability::Filter | FormatCapability::MSAA | FormatCapability::Resolve | FormatCapability::DepthStencil
    )
    APPEND_CAP(
        WMTPixelFormatDepth32Float,
        FormatCapability::MSAA | FormatCapability::Resolve | FormatCapability::DepthStencil
    )
    APPEND_CAP(WMTPixelFormatStencil8, FormatCapability::MSAA | FormatCapability::DepthStencil)
    // APPEND_CAP(WMTPixelFormatDepth24Unorm_Stencil8, 0); // not available
    APPEND_CAP(
        WMTPixelFormatDepth32Float_Stencil8,
        FormatCapability::MSAA | FormatCapability::Resolve | FormatCapability::DepthStencil
    )
    // APPEND_CAP(WMTPixelFormatX24_Stencil8, 0) // not available
    APPEND_CAP(WMTPixelFormatX32_Stencil8, FormatCapability::MSAA | FormatCapability::DepthStencil)

    // extended range
    APPEND_CAP(WMTPixelFormatBGRA10_XR, ALL_CAP)
    APPEND_CAP(WMTPixelFormatBGRA10_XR_sRGB, ALL_CAP)
    APPEND_CAP(WMTPixelFormatBGR10_XR, ALL_CAP)
    APPEND_CAP(WMTPixelFormatBGR10_XR_sRGB, ALL_CAP)

    if (!device.supportsFamily(WMTGPUFamilyApple8)) {
      return;
    }
    // Apple 8: M2

    APPEND_CAP(WMTPixelFormatRG32Uint, FormatCapability::Atomic)
    APPEND_CAP(WMTPixelFormatRG32Sint, FormatCapability::Atomic)

    APPEND_CAP(WMTPixelFormatDepth16Unorm, FormatCapability::Sparse)
    APPEND_CAP(WMTPixelFormatStencil8, FormatCapability::Sparse)

    if (!device.supportsFamily(WMTGPUFamilyApple9)) {
      return;
    }
    // Apple 9: M3+

    APPEND_CAP(WMTPixelFormatR32Float, ALL_CAP)
    APPEND_CAP(WMTPixelFormatRG32Float, ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA32Float, ALL_CAP)
  } else if (device.supportsFamily(WMTGPUFamilyMac2)) {

    APPEND_CAP(WMTPixelFormatA8Unorm, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR8Unorm, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR8Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatR8Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR8Sint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)

    APPEND_CAP(WMTPixelFormatR16Unorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatR16Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatR16Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR16Sint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatR16Float, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)

    APPEND_CAP(WMTPixelFormatRG8Unorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG8Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG8Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG8Sint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)

    APPEND_CAP(WMTPixelFormatR32Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP | FormatCapability::Atomic)
    APPEND_CAP(WMTPixelFormatR32Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP | FormatCapability::Atomic)
    APPEND_CAP(WMTPixelFormatR32Float, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)

    APPEND_CAP(WMTPixelFormatRG16Unorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Sint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG16Float, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)

    APPEND_CAP(WMTPixelFormatRGBA8Unorm, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA8Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(
        WMTPixelFormatRGBA8Unorm_sRGB, FormatCapability::Filter | FormatCapability::Color | FormatCapability::MSAA |
                                             FormatCapability::Resolve | FormatCapability::Blend
    )
    APPEND_CAP(WMTPixelFormatRGBA8Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA8Sint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatBGRA8Unorm, ALL_CAP | FormatCapability::TextureBufferRead)
    APPEND_CAP(
        WMTPixelFormatBGRA8Unorm_sRGB, FormatCapability::Filter | FormatCapability::Color | FormatCapability::MSAA |
                                             FormatCapability::Resolve | FormatCapability::Blend
    )

    // 32-bit packed
    APPEND_CAP(WMTPixelFormatRGB10A2Unorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatBGR10A2Unorm, ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGB10A2Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG11B10Float, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGB9E5Float, FormatCapability::Filter)

    // 64-bit
    APPEND_CAP(WMTPixelFormatRG32Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG32Sint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRG32Float, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGBA16Unorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGBA16Snorm, ALL_CAP | TEXTURE_BUFFER_READ_OR_WRITE)
    APPEND_CAP(WMTPixelFormatRGBA16Uint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA16Sint, NONAPPLE_INT_FORMAT_CAP | TEXTURE_BUFFER_ALL_CAP)
    APPEND_CAP(WMTPixelFormatRGBA16Float, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)

    // 128-bit
    APPEND_CAP(
        WMTPixelFormatRGBA32Uint,
        FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA | TEXTURE_BUFFER_ALL_CAP
    )
    APPEND_CAP(
        WMTPixelFormatRGBA32Sint,
        FormatCapability::Write | FormatCapability::Color | FormatCapability::MSAA | TEXTURE_BUFFER_ALL_CAP
    )
    APPEND_CAP(WMTPixelFormatRGBA32Float, ALL_CAP | TEXTURE_BUFFER_ALL_CAP)

    // compressed : TODO

    // BC
    APPEND_CAP(WMTPixelFormatBC1_RGBA, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC1_RGBA_sRGB, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC2_RGBA, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC2_RGBA_sRGB, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC3_RGBA, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC3_RGBA_sRGB, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC4_RUnorm, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC4_RSnorm, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC5_RGSnorm, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC5_RGUnorm, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC6H_RGBUfloat, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC6H_RGBFloat, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC7_RGBAUnorm, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBC7_RGBAUnorm_sRGB, FormatCapability::Filter)

    // YUV
    APPEND_CAP(WMTPixelFormatGBGR422, FormatCapability::Filter)
    APPEND_CAP(WMTPixelFormatBGRG422, FormatCapability::Filter)

    // depth & stencil
    APPEND_CAP(
        WMTPixelFormatDepth16Unorm,
        FormatCapability::Filter | FormatCapability::MSAA | FormatCapability::Resolve | FormatCapability::DepthStencil
    )
    APPEND_CAP(
        WMTPixelFormatDepth32Float,
        FormatCapability::Filter | FormatCapability::MSAA | FormatCapability::Resolve | FormatCapability::DepthStencil
    )
    APPEND_CAP(WMTPixelFormatStencil8, FormatCapability::MSAA | FormatCapability::DepthStencil)
    APPEND_CAP(
        WMTPixelFormatDepth24Unorm_Stencil8,
        FormatCapability::Filter | FormatCapability::MSAA | FormatCapability::Resolve | FormatCapability::DepthStencil
    );
    APPEND_CAP(
        WMTPixelFormatDepth32Float_Stencil8,
        FormatCapability::MSAA | FormatCapability::Resolve | FormatCapability::DepthStencil
    )
    APPEND_CAP(WMTPixelFormatX24_Stencil8, FormatCapability::MSAA | FormatCapability::DepthStencil)
    APPEND_CAP(WMTPixelFormatX32_Stencil8, FormatCapability::MSAA | FormatCapability::DepthStencil)

    // extended range
    // APPEND_CAP(WMTPixelFormatBGRA10_XR, ALL_CAP) // not available
    // APPEND_CAP(WMTPixelFormatBGRA10_XR_sRGB, ALL_CAP)  // not available
    // APPEND_CAP(WMTPixelFormatBGR10_XR, ALL_CAP)  // not available
    // APPEND_CAP(WMTPixelFormatBGR10_XR_sRGB, ALL_CAP)  // not available

    return;
  } else {
    throw MTLD3DError("Invalid MTLDevice");
  }
};

WMTPixelFormat
Forget_sRGB(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatR8Unorm_sRGB:
    return WMTPixelFormatR8Unorm;
  case WMTPixelFormatRG8Unorm_sRGB:
    return WMTPixelFormatRG8Unorm;
  case WMTPixelFormatRGBA8Unorm_sRGB:
    return WMTPixelFormatRGBA8Unorm;
  case WMTPixelFormatBGRA8Unorm_sRGB:
    return WMTPixelFormatBGRA8Unorm;
  case WMTPixelFormatBGR10_XR_sRGB:
    return WMTPixelFormatBGR10_XR;
  case WMTPixelFormatBGRA10_XR_sRGB:
    return WMTPixelFormatBGRA10_XR;
  case WMTPixelFormatBC1_RGBA_sRGB:
    return WMTPixelFormatBC1_RGBA;
  case WMTPixelFormatBC2_RGBA_sRGB:
    return WMTPixelFormatBC2_RGBA;
  case WMTPixelFormatBC3_RGBA_sRGB:
    return WMTPixelFormatBC3_RGBA;
  case WMTPixelFormatBC7_RGBAUnorm_sRGB:
    return WMTPixelFormatBC7_RGBAUnorm;
  case WMTPixelFormatPVRTC_RGBA_2BPP_sRGB:
    return WMTPixelFormatPVRTC_RGBA_2BPP;
  case WMTPixelFormatPVRTC_RGBA_4BPP_sRGB:
    return WMTPixelFormatPVRTC_RGBA_4BPP;
  case WMTPixelFormatPVRTC_RGB_2BPP_sRGB:
    return WMTPixelFormatPVRTC_RGB_2BPP;
  case WMTPixelFormatPVRTC_RGB_4BPP_sRGB:
    return WMTPixelFormatPVRTC_RGB_4BPP;
  case WMTPixelFormatEAC_RGBA8_sRGB:
    return WMTPixelFormatEAC_RGBA8;
  case WMTPixelFormatETC2_RGB8_sRGB:
    return WMTPixelFormatETC2_RGB8;
  case WMTPixelFormatETC2_RGB8A1_sRGB:
    return WMTPixelFormatETC2_RGB8A1;
  default:
    return format;
  }
}

WMTPixelFormat
Recall_sRGB(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatR8Unorm:
    return WMTPixelFormatR8Unorm_sRGB;
  case WMTPixelFormatRG8Unorm:
    return WMTPixelFormatRG8Unorm_sRGB;
  case WMTPixelFormatRGBA8Unorm:
    return WMTPixelFormatRGBA8Unorm_sRGB;
  case WMTPixelFormatBGRA8Unorm:
    return WMTPixelFormatBGRA8Unorm_sRGB;
  case WMTPixelFormatBGR10_XR:
    return WMTPixelFormatBGR10_XR_sRGB;
  case WMTPixelFormatBGRA10_XR:
    return WMTPixelFormatBGRA10_XR_sRGB;
  case WMTPixelFormatBC1_RGBA:
    return WMTPixelFormatBC1_RGBA_sRGB;
  case WMTPixelFormatBC2_RGBA:
    return WMTPixelFormatBC2_RGBA_sRGB;
  case WMTPixelFormatBC3_RGBA:
    return WMTPixelFormatBC3_RGBA_sRGB;
  case WMTPixelFormatBC7_RGBAUnorm:
    return WMTPixelFormatBC7_RGBAUnorm_sRGB;
  case WMTPixelFormatPVRTC_RGBA_2BPP:
    return WMTPixelFormatPVRTC_RGBA_2BPP_sRGB;
  case WMTPixelFormatPVRTC_RGBA_4BPP:
    return WMTPixelFormatPVRTC_RGBA_4BPP_sRGB;
  case WMTPixelFormatPVRTC_RGB_2BPP:
    return WMTPixelFormatPVRTC_RGB_2BPP_sRGB;
  case WMTPixelFormatPVRTC_RGB_4BPP:
    return WMTPixelFormatPVRTC_RGB_4BPP_sRGB;
  case WMTPixelFormatEAC_RGBA8:
    return WMTPixelFormatEAC_RGBA8_sRGB;
  case WMTPixelFormatETC2_RGB8:
    return WMTPixelFormatETC2_RGB8_sRGB;
  case WMTPixelFormatETC2_RGB8A1:
    return WMTPixelFormatETC2_RGB8A1_sRGB;
  default:
    return format;
  }
}

bool
IsBlockCompressionFormat(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatBC1_RGBA:
  case WMTPixelFormatBC1_RGBA_sRGB:
  case WMTPixelFormatBC2_RGBA:
  case WMTPixelFormatBC2_RGBA_sRGB:
  case WMTPixelFormatBC3_RGBA:
  case WMTPixelFormatBC3_RGBA_sRGB:
  case WMTPixelFormatBC4_RUnorm:
  case WMTPixelFormatBC4_RSnorm:
  case WMTPixelFormatBC5_RGUnorm:
  case WMTPixelFormatBC5_RGSnorm:
  case WMTPixelFormatBC6H_RGBUfloat:
  case WMTPixelFormatBC6H_RGBFloat:
  case WMTPixelFormatBC7_RGBAUnorm:
  case WMTPixelFormatBC7_RGBAUnorm_sRGB:
    return true;
  default:
    break;
  }
  return false;
}

size_t
FormatBytesPerTexel(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatInvalid:
    return 0;
  case WMTPixelFormatA8Unorm:
  case WMTPixelFormatR8Unorm:
  case WMTPixelFormatR8Unorm_sRGB:
  case WMTPixelFormatR8Snorm:
  case WMTPixelFormatR8Uint:
  case WMTPixelFormatR8Sint:
    return 1;
  case WMTPixelFormatR16Unorm:
  case WMTPixelFormatR16Snorm:
  case WMTPixelFormatR16Uint:
  case WMTPixelFormatR16Sint:
  case WMTPixelFormatR16Float:
  case WMTPixelFormatRG8Unorm:
  case WMTPixelFormatRG8Unorm_sRGB:
  case WMTPixelFormatRG8Snorm:
  case WMTPixelFormatRG8Uint:
  case WMTPixelFormatRG8Sint:
  case WMTPixelFormatB5G6R5Unorm:
  case WMTPixelFormatA1BGR5Unorm:
  case WMTPixelFormatABGR4Unorm:
  case WMTPixelFormatBGR5A1Unorm:
    return 2;
  case WMTPixelFormatR32Uint:
  case WMTPixelFormatR32Sint:
  case WMTPixelFormatR32Float:
  case WMTPixelFormatRG16Unorm:
  case WMTPixelFormatRG16Snorm:
  case WMTPixelFormatRG16Uint:
  case WMTPixelFormatRG16Sint:
  case WMTPixelFormatRG16Float:
  case WMTPixelFormatRGBA8Unorm:
  case WMTPixelFormatRGBA8Unorm_sRGB:
  case WMTPixelFormatRGBA8Snorm:
  case WMTPixelFormatRGBA8Uint:
  case WMTPixelFormatRGBA8Sint:
  case WMTPixelFormatBGRA8Unorm:
  case WMTPixelFormatBGRA8Unorm_sRGB:
  case WMTPixelFormatRGB10A2Unorm:
  case WMTPixelFormatRGB10A2Uint:
  case WMTPixelFormatRG11B10Float:
  case WMTPixelFormatRGB9E5Float:
  case WMTPixelFormatBGR10A2Unorm:
  case WMTPixelFormatBGR10_XR:
  case WMTPixelFormatBGR10_XR_sRGB:
    return 4;
  case WMTPixelFormatRG32Uint:
  case WMTPixelFormatRG32Sint:
  case WMTPixelFormatRG32Float:
  case WMTPixelFormatRGBA16Unorm:
  case WMTPixelFormatRGBA16Snorm:
  case WMTPixelFormatRGBA16Uint:
  case WMTPixelFormatRGBA16Sint:
  case WMTPixelFormatRGBA16Float:
    return 8;
  case WMTPixelFormatBGRA10_XR:
  case WMTPixelFormatBGRA10_XR_sRGB:
    return 8;
  case WMTPixelFormatRGBA32Uint:
  case WMTPixelFormatRGBA32Sint:
  case WMTPixelFormatRGBA32Float:
    return 16;
  case WMTPixelFormatBC1_RGBA:
  case WMTPixelFormatBC1_RGBA_sRGB:
  case WMTPixelFormatBC4_RUnorm:
  case WMTPixelFormatBC4_RSnorm:
    return 8;
  case WMTPixelFormatBC2_RGBA:
  case WMTPixelFormatBC2_RGBA_sRGB:
  case WMTPixelFormatBC3_RGBA:
  case WMTPixelFormatBC3_RGBA_sRGB:
  case WMTPixelFormatBC5_RGUnorm:
  case WMTPixelFormatBC5_RGSnorm:
  case WMTPixelFormatBC6H_RGBFloat:
  case WMTPixelFormatBC6H_RGBUfloat:
  case WMTPixelFormatBC7_RGBAUnorm:
  case WMTPixelFormatBC7_RGBAUnorm_sRGB:
    return 16;
  case WMTPixelFormatPVRTC_RGB_2BPP:
  case WMTPixelFormatPVRTC_RGB_2BPP_sRGB:
  case WMTPixelFormatPVRTC_RGB_4BPP:
  case WMTPixelFormatPVRTC_RGB_4BPP_sRGB:
  case WMTPixelFormatPVRTC_RGBA_2BPP:
  case WMTPixelFormatPVRTC_RGBA_2BPP_sRGB:
  case WMTPixelFormatPVRTC_RGBA_4BPP:
  case WMTPixelFormatPVRTC_RGBA_4BPP_sRGB:
  case WMTPixelFormatEAC_R11Unorm:
  case WMTPixelFormatEAC_R11Snorm:
  case WMTPixelFormatEAC_RG11Unorm:
  case WMTPixelFormatEAC_RG11Snorm:
  case WMTPixelFormatEAC_RGBA8:
  case WMTPixelFormatEAC_RGBA8_sRGB:
  case WMTPixelFormatETC2_RGB8:
  case WMTPixelFormatETC2_RGB8_sRGB:
  case WMTPixelFormatETC2_RGB8A1:
  case WMTPixelFormatETC2_RGB8A1_sRGB:
  case WMTPixelFormatASTC_4x4_sRGB:
  case WMTPixelFormatASTC_5x4_sRGB:
  case WMTPixelFormatASTC_5x5_sRGB:
  case WMTPixelFormatASTC_6x5_sRGB:
  case WMTPixelFormatASTC_6x6_sRGB:
  case WMTPixelFormatASTC_8x5_sRGB:
  case WMTPixelFormatASTC_8x6_sRGB:
  case WMTPixelFormatASTC_8x8_sRGB:
  case WMTPixelFormatASTC_10x5_sRGB:
  case WMTPixelFormatASTC_10x6_sRGB:
  case WMTPixelFormatASTC_10x8_sRGB:
  case WMTPixelFormatASTC_10x10_sRGB:
  case WMTPixelFormatASTC_12x10_sRGB:
  case WMTPixelFormatASTC_12x12_sRGB:
  case WMTPixelFormatASTC_4x4_LDR:
  case WMTPixelFormatASTC_5x4_LDR:
  case WMTPixelFormatASTC_5x5_LDR:
  case WMTPixelFormatASTC_6x5_LDR:
  case WMTPixelFormatASTC_6x6_LDR:
  case WMTPixelFormatASTC_8x5_LDR:
  case WMTPixelFormatASTC_8x6_LDR:
  case WMTPixelFormatASTC_8x8_LDR:
  case WMTPixelFormatASTC_10x5_LDR:
  case WMTPixelFormatASTC_10x6_LDR:
  case WMTPixelFormatASTC_10x8_LDR:
  case WMTPixelFormatASTC_10x10_LDR:
  case WMTPixelFormatASTC_12x10_LDR:
  case WMTPixelFormatASTC_12x12_LDR:
  case WMTPixelFormatASTC_4x4_HDR:
  case WMTPixelFormatASTC_5x4_HDR:
  case WMTPixelFormatASTC_5x5_HDR:
  case WMTPixelFormatASTC_6x5_HDR:
  case WMTPixelFormatASTC_6x6_HDR:
  case WMTPixelFormatASTC_8x5_HDR:
  case WMTPixelFormatASTC_8x6_HDR:
  case WMTPixelFormatASTC_8x8_HDR:
  case WMTPixelFormatASTC_10x5_HDR:
  case WMTPixelFormatASTC_10x6_HDR:
  case WMTPixelFormatASTC_10x8_HDR:
  case WMTPixelFormatASTC_10x10_HDR:
  case WMTPixelFormatASTC_12x10_HDR:
  case WMTPixelFormatASTC_12x12_HDR:
    return 0; // invalid
  case WMTPixelFormatGBGR422:
  case WMTPixelFormatBGRG422:
    return 1;
  case WMTPixelFormatDepth16Unorm:
    return 2;
  case WMTPixelFormatDepth32Float:
    return 4;
  case WMTPixelFormatStencil8:
    return 1;
  case WMTPixelFormatDepth24Unorm_Stencil8:
    return 4;
  case WMTPixelFormatDepth32Float_Stencil8:
    return 8;
  case WMTPixelFormatX32_Stencil8:
    return 8;
  case WMTPixelFormatX24_Stencil8:
    return 4;
  default:
    return 0;
  }
}

uint32_t
DepthStencilPlanarFlags(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatDepth32Float_Stencil8:
  case WMTPixelFormatDepth24Unorm_Stencil8:
    return 3;
  case WMTPixelFormatDepth32Float:
  case WMTPixelFormatDepth16Unorm:
    return 1;
  case WMTPixelFormatStencil8:
    return 2;
  default:
    return 0;
  }
}

int32_t
MTLQueryDXGIFormat(WMT::Device device, uint32_t format, MTL_DXGI_FORMAT_DESC &description) {
  description.PixelFormat = WMTPixelFormatInvalid;
  description.AttributeFormat = WMTAttributeFormatInvalid;
  description.BytesPerTexel = 0;
  description.Flag = 0;

  switch (format) {
  case DXGI_FORMAT_R32G32B32A32_TYPELESS: {
    description.PixelFormat = WMTPixelFormatRGBA32Uint;
    description.BytesPerTexel = 16;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R32G32B32A32_UINT: {
    description.PixelFormat = WMTPixelFormatRGBA32Uint;
    description.AttributeFormat = WMTAttributeFormatUInt4;
    description.BytesPerTexel = 16;
    break;
  }
  case DXGI_FORMAT_R32G32B32A32_SINT: {
    description.PixelFormat = WMTPixelFormatRGBA32Sint;
    description.AttributeFormat = WMTAttributeFormatInt4;
    description.BytesPerTexel = 16;
    break;
  }
  case DXGI_FORMAT_R32G32B32A32_FLOAT: {
    description.PixelFormat = WMTPixelFormatRGBA32Float;
    description.AttributeFormat = WMTAttributeFormatFloat4;
    description.BytesPerTexel = 16;
    break;
  }
  case DXGI_FORMAT_R32G32B32_TYPELESS: {
    description.PixelFormat = WMTPixelFormatR32Uint;
    description.BytesPerTexel = 12;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R32G32B32_UINT: {
    description.PixelFormat = WMTPixelFormatR32Uint;
    description.AttributeFormat = WMTAttributeFormatUInt3;
    description.BytesPerTexel = 12;
    break;
  }
  case DXGI_FORMAT_R32G32B32_SINT: {
    description.PixelFormat = WMTPixelFormatR32Sint;
    description.AttributeFormat = WMTAttributeFormatInt3;
    description.BytesPerTexel = 12;
    break;
  }
  case DXGI_FORMAT_R32G32B32_FLOAT: {
    description.PixelFormat = WMTPixelFormatR32Float;
    description.AttributeFormat = WMTAttributeFormatFloat3;
    description.BytesPerTexel = 12;
    break;
  }
  case DXGI_FORMAT_R16G16B16A16_TYPELESS: {
    description.PixelFormat = WMTPixelFormatRGBA16Float;
    description.BytesPerTexel = 8;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R16G16B16A16_FLOAT: {
    description.PixelFormat = WMTPixelFormatRGBA16Float;
    description.AttributeFormat = WMTAttributeFormatHalf4;
    description.BytesPerTexel = 8;
    description.Flag = MTL_DXGI_FORMAT_BACKBUFFER; // 11.1
    break;
  }
  case DXGI_FORMAT_R16G16B16A16_UNORM: {
    description.PixelFormat = WMTPixelFormatRGBA16Unorm;
    description.AttributeFormat = WMTAttributeFormatUShort4Normalized;
    description.BytesPerTexel = 8;
    break;
  }
  case DXGI_FORMAT_R16G16B16A16_UINT: {
    description.PixelFormat = WMTPixelFormatRGBA16Uint;
    description.AttributeFormat = WMTAttributeFormatUShort4;
    description.BytesPerTexel = 8;
    break;
  }
  case DXGI_FORMAT_R16G16B16A16_SNORM: {
    description.PixelFormat = WMTPixelFormatRGBA16Snorm;
    description.AttributeFormat = WMTAttributeFormatShort4Normalized;
    description.BytesPerTexel = 8;
    break;
  }
  case DXGI_FORMAT_R16G16B16A16_SINT: {
    description.PixelFormat = WMTPixelFormatRGBA16Sint;
    description.AttributeFormat = WMTAttributeFormatShort4;
    description.BytesPerTexel = 8;
    break;
  }
  case DXGI_FORMAT_R32G32_TYPELESS: {
    description.PixelFormat = WMTPixelFormatRG32Uint;
    description.BytesPerTexel = 8;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R32G32_FLOAT: {
    description.PixelFormat = WMTPixelFormatRG32Float;
    description.AttributeFormat = WMTAttributeFormatFloat2;
    description.BytesPerTexel = 8;
    break;
  }
  case DXGI_FORMAT_R32G32_UINT: {
    description.PixelFormat = WMTPixelFormatRG32Uint;
    description.AttributeFormat = WMTAttributeFormatUInt2;
    description.BytesPerTexel = 8;
    break;
  }
  case DXGI_FORMAT_R32G32_SINT: {
    description.PixelFormat = WMTPixelFormatRG32Sint;
    description.AttributeFormat = WMTAttributeFormatInt2;
    description.BytesPerTexel = 8;
    break;
  }
  case DXGI_FORMAT_R32G8X24_TYPELESS: {
    description.PixelFormat = WMTPixelFormatDepth32Float_Stencil8;
    description.BytesPerTexel = 8;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER;
    break;
  }
  case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: {
    description.PixelFormat = WMTPixelFormatDepth32Float_Stencil8;
    description.BytesPerTexel = 8;
    description.Flag = MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER;
    break;
  }
  case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS: {
    description.PixelFormat = WMTPixelFormatDepth32Float_Stencil8;
    description.BytesPerTexel = 8;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_DEPTH_PLANER;
    break;
  }
  case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT: {
    description.PixelFormat = WMTPixelFormatX32_Stencil8;
    description.BytesPerTexel = 8;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_STENCIL_PLANER;
    break;
  }
  case DXGI_FORMAT_R10G10B10A2_TYPELESS: {
    description.PixelFormat = WMTPixelFormatRGB10A2Uint;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R10G10B10A2_UNORM: {
    description.PixelFormat = WMTPixelFormatRGB10A2Unorm;
    description.AttributeFormat = WMTAttributeFormatUInt1010102Normalized;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_BACKBUFFER; // 11.1
    break;
  }
  case DXGI_FORMAT_R10G10B10A2_UINT: {
    description.PixelFormat = WMTPixelFormatRGB10A2Uint;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R11G11B10_FLOAT: {
    description.PixelFormat = WMTPixelFormatRG11B10Float;
    description.AttributeFormat = WMTAttributeFormatFloatRG11B10;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R8G8B8A8_TYPELESS: {
    description.PixelFormat = WMTPixelFormatRGBA8Unorm;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R8G8B8A8_UNORM: {
    description.PixelFormat = WMTPixelFormatRGBA8Unorm;
    description.AttributeFormat = WMTAttributeFormatUChar4Normalized;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_BACKBUFFER; // 11.1, swizzle from BGRA8Unorm
    // FIXME: backbuffer format not supported!
    // need some workaround for GetDisplayModeList
    break;
  }
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: {
    description.PixelFormat = WMTPixelFormatRGBA8Unorm_sRGB;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_BACKBUFFER; // 11.1, swizzle from BGRA8Unorm
    break;
  }
  case DXGI_FORMAT_R8G8B8A8_UINT: {
    description.PixelFormat = WMTPixelFormatRGBA8Uint;
    description.AttributeFormat = WMTAttributeFormatUChar4;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R8G8B8A8_SNORM: {
    description.PixelFormat = WMTPixelFormatRGBA8Snorm;
    description.AttributeFormat = WMTAttributeFormatChar4Normalized;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R8G8B8A8_SINT: {
    description.PixelFormat = WMTPixelFormatRGBA8Sint;
    description.AttributeFormat = WMTAttributeFormatChar4;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R16G16_TYPELESS: {
    description.PixelFormat = WMTPixelFormatRG16Uint;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R16G16_FLOAT: {
    description.PixelFormat = WMTPixelFormatRG16Float;
    description.AttributeFormat = WMTAttributeFormatHalf2;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R16G16_UNORM: {
    description.PixelFormat = WMTPixelFormatRG16Unorm;
    description.AttributeFormat = WMTAttributeFormatUShort2Normalized;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R16G16_UINT: {
    description.PixelFormat = WMTPixelFormatRG16Uint;
    description.AttributeFormat = WMTAttributeFormatUShort2;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R16G16_SNORM: {
    description.PixelFormat = WMTPixelFormatRG16Snorm;
    description.AttributeFormat = WMTAttributeFormatShort2Normalized;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R16G16_SINT: {
    description.PixelFormat = WMTPixelFormatRG16Sint;
    description.AttributeFormat = WMTAttributeFormatShort2;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R32_TYPELESS: {
    description.PixelFormat = WMTPixelFormatR32Uint;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_D32_FLOAT: {
    description.PixelFormat = WMTPixelFormatDepth32Float;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R32_FLOAT: {
    description.PixelFormat = WMTPixelFormatR32Float;
    description.AttributeFormat = WMTAttributeFormatFloat;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R32_UINT: {
    description.PixelFormat = WMTPixelFormatR32Uint;
    description.AttributeFormat = WMTAttributeFormatUInt;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R32_SINT: {
    description.PixelFormat = WMTPixelFormatR32Sint;
    description.AttributeFormat = WMTAttributeFormatInt;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R24G8_TYPELESS: {
    // if (device->depth24Stencil8PixelFormatSupported()) {
      if (false) {
      description.PixelFormat = WMTPixelFormatDepth24Unorm_Stencil8;
    } else {
      description.Flag |= MTL_DXGI_FORMAT_EMULATED_D24;
      description.PixelFormat = WMTPixelFormatDepth32Float_Stencil8;
#ifdef VBOX
      description.BytesPerTexel = 5;
#endif
    }
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER;
    break;
  }
  case DXGI_FORMAT_D24_UNORM_S8_UINT: {
    description.Flag = MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER;
    // if (device->depth24Stencil8PixelFormatSupported()) {
    if (false) {
      description.PixelFormat = WMTPixelFormatDepth24Unorm_Stencil8;
    } else {
      description.Flag |= MTL_DXGI_FORMAT_EMULATED_D24;
      description.PixelFormat = WMTPixelFormatDepth32Float_Stencil8;
#ifdef VBOX
      description.BytesPerTexel = 5;
#endif
    }
    description.Flag = MTL_DXGI_FORMAT_DEPTH_PLANER | MTL_DXGI_FORMAT_STENCIL_PLANER;
    break;
  }
  case DXGI_FORMAT_R24_UNORM_X8_TYPELESS: {
    description.Flag = MTL_DXGI_FORMAT_DEPTH_PLANER;
    // if (device->depth24Stencil8PixelFormatSupported()) {
      if (false) {
      description.PixelFormat = WMTPixelFormatDepth24Unorm_Stencil8;
    } else {
      description.Flag |= MTL_DXGI_FORMAT_EMULATED_D24;
      description.PixelFormat = WMTPixelFormatDepth32Float_Stencil8;
#ifdef VBOX
      description.BytesPerTexel = 5;
#endif
    }
    description.Flag = MTL_DXGI_FORMAT_DEPTH_PLANER;
    break;
  }
  case DXGI_FORMAT_X24_TYPELESS_G8_UINT: {
    description.Flag = MTL_DXGI_FORMAT_STENCIL_PLANER;
    // if (device->depth24Stencil8PixelFormatSupported()) {
      if (false) {
      description.PixelFormat = WMTPixelFormatX24_Stencil8;
    } else {
      description.Flag |= MTL_DXGI_FORMAT_EMULATED_D24;
      description.PixelFormat = WMTPixelFormatX32_Stencil8;
    }
    description.Flag = MTL_DXGI_FORMAT_STENCIL_PLANER;
    break;
  }
  case DXGI_FORMAT_R8G8_TYPELESS: {
    description.PixelFormat = WMTPixelFormatRG8Uint;
    description.BytesPerTexel = 2;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R8G8_UNORM: {
    description.PixelFormat = WMTPixelFormatRG8Unorm;
    description.AttributeFormat = WMTAttributeFormatUChar2Normalized;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R8G8_UINT: {
    description.PixelFormat = WMTPixelFormatRG8Uint;
    description.AttributeFormat = WMTAttributeFormatUChar2;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R8G8_SNORM: {
    description.PixelFormat = WMTPixelFormatRG8Snorm;
    description.AttributeFormat = WMTAttributeFormatChar2Normalized;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R8G8_SINT: {
    description.PixelFormat = WMTPixelFormatRG8Sint;
    description.AttributeFormat = WMTAttributeFormatChar2;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R16_TYPELESS: {
    description.PixelFormat = WMTPixelFormatR16Uint;
    description.BytesPerTexel = 2;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R16_FLOAT: {
    description.PixelFormat = WMTPixelFormatR16Float;
    description.AttributeFormat = WMTAttributeFormatHalf;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_D16_UNORM: {
    description.PixelFormat = WMTPixelFormatDepth16Unorm;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R16_UNORM: {
    description.PixelFormat = WMTPixelFormatR16Unorm;
    description.AttributeFormat = WMTAttributeFormatUShortNormalized;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R16_UINT: {
    description.PixelFormat = WMTPixelFormatR16Uint;
    description.AttributeFormat = WMTAttributeFormatUShort;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R16_SNORM: {
    description.PixelFormat = WMTPixelFormatR16Snorm;
    description.AttributeFormat = WMTAttributeFormatShortNormalized;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R16_SINT: {
    description.PixelFormat = WMTPixelFormatR16Sint;
    description.AttributeFormat = WMTAttributeFormatShort;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_R8_TYPELESS: {
    description.PixelFormat = WMTPixelFormatR8Uint;
    description.BytesPerTexel = 1;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_R8_UNORM: {
    description.PixelFormat = WMTPixelFormatR8Unorm;
    description.AttributeFormat = WMTAttributeFormatUCharNormalized;
    description.BytesPerTexel = 1;
    break;
  }
  case DXGI_FORMAT_R8_UINT: {
    description.PixelFormat = WMTPixelFormatR8Uint;
    description.AttributeFormat = WMTAttributeFormatUChar;
    description.BytesPerTexel = 1;
    break;
  }
  case DXGI_FORMAT_R8_SNORM: {
    description.PixelFormat = WMTPixelFormatR8Snorm;
    description.AttributeFormat = WMTAttributeFormatCharNormalized;
    description.BytesPerTexel = 1;
    break;
  }
  case DXGI_FORMAT_R8_SINT: {
    description.PixelFormat = WMTPixelFormatR8Sint;
    description.AttributeFormat = WMTAttributeFormatChar;
    description.BytesPerTexel = 1;
    break;
  }
  case DXGI_FORMAT_A8_UNORM: {
    description.PixelFormat = WMTPixelFormatA8Unorm;
    description.BytesPerTexel = 1;
    break;
  }
  case DXGI_FORMAT_R1_UNORM: {
    return E_FAIL;
  }
  case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: {
    description.PixelFormat = WMTPixelFormatRGB9E5Float;
    description.AttributeFormat = WMTAttributeFormatFloatRGB9E5;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R8G8_B8G8_UNORM: {
    description.PixelFormat = WMTPixelFormatBGRG422;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_G8R8_G8B8_UNORM: {
    description.PixelFormat = WMTPixelFormatGBGR422;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_BC1_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBC1_RGBA;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_BC;
    description.BlockSize = 8;
    break;
  }
  /**
  from doc:
  Reinterpretation of image data between pixel formats is supported:
  - sRGB and non-sRGB forms of the same compressed format
  */
  case DXGI_FORMAT_BC1_UNORM: {
    description.PixelFormat = WMTPixelFormatBC1_RGBA;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 8;
    break;
  }
  case DXGI_FORMAT_BC1_UNORM_SRGB: {
    description.PixelFormat = WMTPixelFormatBC1_RGBA_sRGB;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 8;
    break;
  }
  case DXGI_FORMAT_BC2_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBC2_RGBA;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC2_UNORM: {
    description.PixelFormat = WMTPixelFormatBC2_RGBA;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC2_UNORM_SRGB: {
    description.PixelFormat = WMTPixelFormatBC2_RGBA_sRGB;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC3_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBC3_RGBA;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC3_UNORM: {
    description.PixelFormat = WMTPixelFormatBC3_RGBA;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC3_UNORM_SRGB: {
    description.PixelFormat = WMTPixelFormatBC3_RGBA_sRGB;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC4_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBC4_RUnorm;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_BC;
    description.BlockSize = 8;
    break;
  }
  case DXGI_FORMAT_BC4_UNORM: {
    description.PixelFormat = WMTPixelFormatBC4_RUnorm;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 8;
    break;
  }
  case DXGI_FORMAT_BC4_SNORM: {
    description.PixelFormat = WMTPixelFormatBC4_RSnorm;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 8;
    break;
  }
  case DXGI_FORMAT_BC5_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBC5_RGUnorm;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC5_UNORM: {
    description.PixelFormat = WMTPixelFormatBC5_RGUnorm;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC5_SNORM: {
    description.PixelFormat = WMTPixelFormatBC5_RGSnorm;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_B5G6R5_UNORM: {
    description.PixelFormat = WMTPixelFormatB5G6R5Unorm;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_B5G5R5A1_UNORM: {
    description.PixelFormat = WMTPixelFormatBGR5A1Unorm;
    description.BytesPerTexel = 2;
    break;
  }
  case DXGI_FORMAT_B8G8R8A8_UNORM: {
    description.PixelFormat = WMTPixelFormatBGRA8Unorm;
    description.AttributeFormat = WMTAttributeFormatUChar4Normalized_BGRA;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_BACKBUFFER; // 11.1
    break;
  }
  case DXGI_FORMAT_B8G8R8X8_UNORM: {
    description.PixelFormat = WMTPixelFormatBGRA8Unorm;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    return E_FAIL;
  case DXGI_FORMAT_B8G8R8A8_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBGRA8Unorm;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: {
    description.PixelFormat = WMTPixelFormatBGRA8Unorm_sRGB;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_BACKBUFFER; // 11.1
    break;
  }
  case DXGI_FORMAT_B8G8R8X8_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBGRA8Unorm;
    description.BytesPerTexel = 4;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS;
    break;
  }
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: {
    description.PixelFormat = WMTPixelFormatBGRA8Unorm_sRGB;
    description.BytesPerTexel = 4;
    break;
  }
  case DXGI_FORMAT_BC6H_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBC6H_RGBUfloat;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC6H_UF16: {
    description.PixelFormat = WMTPixelFormatBC6H_RGBUfloat;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC6H_SF16: {
    description.PixelFormat = WMTPixelFormatBC6H_RGBFloat;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC7_TYPELESS: {
    description.PixelFormat = WMTPixelFormatBC7_RGBAUnorm;
    description.Flag = MTL_DXGI_FORMAT_TYPELESS | MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC7_UNORM: {
    description.PixelFormat = WMTPixelFormatBC7_RGBAUnorm;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_BC7_UNORM_SRGB: {
    description.PixelFormat = WMTPixelFormatBC7_RGBAUnorm_sRGB;
    description.Flag = MTL_DXGI_FORMAT_BC;
    description.BlockSize = 16;
    break;
  }
  case DXGI_FORMAT_AYUV:
  case DXGI_FORMAT_Y410:
  case DXGI_FORMAT_Y416:
  case DXGI_FORMAT_NV12:
  case DXGI_FORMAT_P010:
  case DXGI_FORMAT_P016:
  case DXGI_FORMAT_420_OPAQUE:
  case DXGI_FORMAT_YUY2:
  case DXGI_FORMAT_Y210:
  case DXGI_FORMAT_Y216:
  case DXGI_FORMAT_NV11:
  case DXGI_FORMAT_AI44:
  case DXGI_FORMAT_IA44:
  case DXGI_FORMAT_P8:
  case DXGI_FORMAT_A8P8:
  case DXGI_FORMAT_B4G4R4A4_UNORM:
  case DXGI_FORMAT_P208:
  case DXGI_FORMAT_V208:
  case DXGI_FORMAT_V408:
  case DXGI_FORMAT_FORCE_UINT:
  case DXGI_FORMAT_UNKNOWN:
  default:
    return E_FAIL;
  }

  return S_OK;
}

uint32_t
MTLGetTexelSize(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatInvalid:
    return 0;
  case WMTPixelFormatA8Unorm:
  case WMTPixelFormatR8Unorm:
  case WMTPixelFormatR8Unorm_sRGB:
  case WMTPixelFormatR8Snorm:
  case WMTPixelFormatR8Uint:
  case WMTPixelFormatR8Sint:
    return 1;
  case WMTPixelFormatR16Unorm:
  case WMTPixelFormatR16Snorm:
  case WMTPixelFormatR16Uint:
  case WMTPixelFormatR16Sint:
  case WMTPixelFormatR16Float:
  case WMTPixelFormatRG8Unorm:
  case WMTPixelFormatRG8Unorm_sRGB:
  case WMTPixelFormatRG8Snorm:
  case WMTPixelFormatRG8Uint:
  case WMTPixelFormatRG8Sint:
  case WMTPixelFormatB5G6R5Unorm:
  case WMTPixelFormatA1BGR5Unorm:
  case WMTPixelFormatABGR4Unorm:
  case WMTPixelFormatBGR5A1Unorm:
    return 2;
  case WMTPixelFormatR32Uint:
  case WMTPixelFormatR32Sint:
  case WMTPixelFormatR32Float:
  case WMTPixelFormatRG16Unorm:
  case WMTPixelFormatRG16Snorm:
  case WMTPixelFormatRG16Uint:
  case WMTPixelFormatRG16Sint:
  case WMTPixelFormatRG16Float:
  case WMTPixelFormatRGBA8Unorm:
  case WMTPixelFormatRGBA8Unorm_sRGB:
  case WMTPixelFormatRGBA8Snorm:
  case WMTPixelFormatRGBA8Uint:
  case WMTPixelFormatRGBA8Sint:
  case WMTPixelFormatBGRA8Unorm:
  case WMTPixelFormatBGRA8Unorm_sRGB:
  case WMTPixelFormatRGB10A2Unorm:
  case WMTPixelFormatRGB10A2Uint:
  case WMTPixelFormatRG11B10Float:
  case WMTPixelFormatRGB9E5Float:
  case WMTPixelFormatBGR10A2Unorm:
  case WMTPixelFormatBGR10_XR:
  case WMTPixelFormatBGR10_XR_sRGB:
    return 4;
  case WMTPixelFormatRG32Uint:
  case WMTPixelFormatRG32Sint:
  case WMTPixelFormatRG32Float:
  case WMTPixelFormatRGBA16Unorm:
  case WMTPixelFormatRGBA16Snorm:
  case WMTPixelFormatRGBA16Uint:
  case WMTPixelFormatRGBA16Sint:
  case WMTPixelFormatRGBA16Float:
  case WMTPixelFormatBGRA10_XR:
  case WMTPixelFormatBGRA10_XR_sRGB:
    return 8;
  case WMTPixelFormatRGBA32Uint:
  case WMTPixelFormatRGBA32Sint:
  case WMTPixelFormatRGBA32Float:
  case WMTPixelFormatBC1_RGBA:
  case WMTPixelFormatBC1_RGBA_sRGB:
  case WMTPixelFormatBC2_RGBA:
  case WMTPixelFormatBC2_RGBA_sRGB:
  case WMTPixelFormatBC3_RGBA:
  case WMTPixelFormatBC3_RGBA_sRGB:
  case WMTPixelFormatBC4_RUnorm:
  case WMTPixelFormatBC4_RSnorm:
  case WMTPixelFormatBC5_RGUnorm:
  case WMTPixelFormatBC5_RGSnorm:
  case WMTPixelFormatBC6H_RGBFloat:
  case WMTPixelFormatBC6H_RGBUfloat:
  case WMTPixelFormatBC7_RGBAUnorm:
  case WMTPixelFormatBC7_RGBAUnorm_sRGB:
    return 16;
  case WMTPixelFormatPVRTC_RGB_2BPP:
  case WMTPixelFormatPVRTC_RGB_2BPP_sRGB:
  case WMTPixelFormatPVRTC_RGB_4BPP:
  case WMTPixelFormatPVRTC_RGB_4BPP_sRGB:
  case WMTPixelFormatPVRTC_RGBA_2BPP:
  case WMTPixelFormatPVRTC_RGBA_2BPP_sRGB:
  case WMTPixelFormatPVRTC_RGBA_4BPP:
  case WMTPixelFormatPVRTC_RGBA_4BPP_sRGB:
  case WMTPixelFormatEAC_R11Unorm:
  case WMTPixelFormatEAC_R11Snorm:
  case WMTPixelFormatEAC_RG11Unorm:
  case WMTPixelFormatEAC_RG11Snorm:
  case WMTPixelFormatEAC_RGBA8:
  case WMTPixelFormatEAC_RGBA8_sRGB:
  case WMTPixelFormatETC2_RGB8:
  case WMTPixelFormatETC2_RGB8_sRGB:
  case WMTPixelFormatETC2_RGB8A1:
  case WMTPixelFormatETC2_RGB8A1_sRGB:
  case WMTPixelFormatASTC_4x4_sRGB:
  case WMTPixelFormatASTC_5x4_sRGB:
  case WMTPixelFormatASTC_5x5_sRGB:
  case WMTPixelFormatASTC_6x5_sRGB:
  case WMTPixelFormatASTC_6x6_sRGB:
  case WMTPixelFormatASTC_8x5_sRGB:
  case WMTPixelFormatASTC_8x6_sRGB:
  case WMTPixelFormatASTC_8x8_sRGB:
  case WMTPixelFormatASTC_10x5_sRGB:
  case WMTPixelFormatASTC_10x6_sRGB:
  case WMTPixelFormatASTC_10x8_sRGB:
  case WMTPixelFormatASTC_10x10_sRGB:
  case WMTPixelFormatASTC_12x10_sRGB:
  case WMTPixelFormatASTC_12x12_sRGB:
  case WMTPixelFormatASTC_4x4_LDR:
  case WMTPixelFormatASTC_5x4_LDR:
  case WMTPixelFormatASTC_5x5_LDR:
  case WMTPixelFormatASTC_6x5_LDR:
  case WMTPixelFormatASTC_6x6_LDR:
  case WMTPixelFormatASTC_8x5_LDR:
  case WMTPixelFormatASTC_8x6_LDR:
  case WMTPixelFormatASTC_8x8_LDR:
  case WMTPixelFormatASTC_10x5_LDR:
  case WMTPixelFormatASTC_10x6_LDR:
  case WMTPixelFormatASTC_10x8_LDR:
  case WMTPixelFormatASTC_10x10_LDR:
  case WMTPixelFormatASTC_12x10_LDR:
  case WMTPixelFormatASTC_12x12_LDR:
  case WMTPixelFormatASTC_4x4_HDR:
  case WMTPixelFormatASTC_5x4_HDR:
  case WMTPixelFormatASTC_5x5_HDR:
  case WMTPixelFormatASTC_6x5_HDR:
  case WMTPixelFormatASTC_6x6_HDR:
  case WMTPixelFormatASTC_8x5_HDR:
  case WMTPixelFormatASTC_8x6_HDR:
  case WMTPixelFormatASTC_8x8_HDR:
  case WMTPixelFormatASTC_10x5_HDR:
  case WMTPixelFormatASTC_10x6_HDR:
  case WMTPixelFormatASTC_10x8_HDR:
  case WMTPixelFormatASTC_10x10_HDR:
  case WMTPixelFormatASTC_12x10_HDR:
  case WMTPixelFormatASTC_12x12_HDR:
    assert(0 && "");
    return 0;
  case WMTPixelFormatGBGR422:
  case WMTPixelFormatBGRG422:
    return 1;
  case WMTPixelFormatDepth16Unorm:
    return 2;
  case WMTPixelFormatDepth32Float:
    return 4;
  case WMTPixelFormatStencil8:
  case WMTPixelFormatDepth24Unorm_Stencil8:
  case WMTPixelFormatX24_Stencil8:
    return 4;
  case WMTPixelFormatDepth32Float_Stencil8:
  case WMTPixelFormatX32_Stencil8:
    return 8;
  default:
    break;
  }
  return 0;
};

WMTPixelFormat
MTLGetUnsignedIntegerFormat(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatA8Unorm:
  case WMTPixelFormatR8Unorm:
  case WMTPixelFormatR8Unorm_sRGB:
  case WMTPixelFormatR8Snorm:
  case WMTPixelFormatR8Uint:
  case WMTPixelFormatR8Sint:
    return WMTPixelFormatR8Uint;
  case WMTPixelFormatR16Unorm:
  case WMTPixelFormatR16Snorm:
  case WMTPixelFormatR16Uint:
  case WMTPixelFormatR16Sint:
  case WMTPixelFormatR16Float:
    return WMTPixelFormatR16Uint;
  case WMTPixelFormatRG8Unorm:
  case WMTPixelFormatRG8Unorm_sRGB:
  case WMTPixelFormatRG8Snorm:
  case WMTPixelFormatRG8Uint:
  case WMTPixelFormatRG8Sint:
    return WMTPixelFormatRG8Uint;
  case WMTPixelFormatB5G6R5Unorm:
  case WMTPixelFormatA1BGR5Unorm:
  case WMTPixelFormatABGR4Unorm:
  case WMTPixelFormatBGR5A1Unorm:
    break;
  case WMTPixelFormatR32Uint:
  case WMTPixelFormatR32Sint:
  case WMTPixelFormatR32Float:
    return WMTPixelFormatR32Uint;
  case WMTPixelFormatRG16Unorm:
  case WMTPixelFormatRG16Snorm:
  case WMTPixelFormatRG16Uint:
  case WMTPixelFormatRG16Sint:
  case WMTPixelFormatRG16Float:
    return WMTPixelFormatRG16Uint;
  case WMTPixelFormatRGBA8Unorm:
  case WMTPixelFormatRGBA8Unorm_sRGB:
  case WMTPixelFormatRGBA8Snorm:
  case WMTPixelFormatRGBA8Uint:
  case WMTPixelFormatRGBA8Sint:
  case WMTPixelFormatBGRA8Unorm:
  case WMTPixelFormatBGRA8Unorm_sRGB:
    return WMTPixelFormatRGBA8Uint;
  case WMTPixelFormatRGB10A2Unorm:
  case WMTPixelFormatRGB10A2Uint:
    return WMTPixelFormatRGB10A2Uint;
  case WMTPixelFormatRG11B10Float:
  case WMTPixelFormatRGB9E5Float:
  case WMTPixelFormatBGR10A2Unorm:
  case WMTPixelFormatBGR10_XR:
  case WMTPixelFormatBGR10_XR_sRGB:
    break;
  case WMTPixelFormatRG32Uint:
  case WMTPixelFormatRG32Sint:
  case WMTPixelFormatRG32Float:
    return WMTPixelFormatRG32Uint;
  case WMTPixelFormatRGBA16Unorm:
  case WMTPixelFormatRGBA16Snorm:
  case WMTPixelFormatRGBA16Uint:
  case WMTPixelFormatRGBA16Sint:
  case WMTPixelFormatRGBA16Float:
    return WMTPixelFormatRGBA16Uint;
  case WMTPixelFormatBGRA10_XR:
  case WMTPixelFormatBGRA10_XR_sRGB:
    break;
  case WMTPixelFormatRGBA32Uint:
  case WMTPixelFormatRGBA32Sint:
  case WMTPixelFormatRGBA32Float:
    return WMTPixelFormatRGBA32Uint;
  case WMTPixelFormatBC1_RGBA:
  case WMTPixelFormatBC1_RGBA_sRGB:
  case WMTPixelFormatBC2_RGBA:
  case WMTPixelFormatBC2_RGBA_sRGB:
  case WMTPixelFormatBC3_RGBA:
  case WMTPixelFormatBC3_RGBA_sRGB:
  case WMTPixelFormatBC4_RUnorm:
  case WMTPixelFormatBC4_RSnorm:
  case WMTPixelFormatBC5_RGUnorm:
  case WMTPixelFormatBC5_RGSnorm:
  case WMTPixelFormatBC6H_RGBFloat:
  case WMTPixelFormatBC6H_RGBUfloat:
  case WMTPixelFormatBC7_RGBAUnorm:
  case WMTPixelFormatBC7_RGBAUnorm_sRGB:
  case WMTPixelFormatPVRTC_RGB_2BPP:
  case WMTPixelFormatPVRTC_RGB_2BPP_sRGB:
  case WMTPixelFormatPVRTC_RGB_4BPP:
  case WMTPixelFormatPVRTC_RGB_4BPP_sRGB:
  case WMTPixelFormatPVRTC_RGBA_2BPP:
  case WMTPixelFormatPVRTC_RGBA_2BPP_sRGB:
  case WMTPixelFormatPVRTC_RGBA_4BPP:
  case WMTPixelFormatPVRTC_RGBA_4BPP_sRGB:
  case WMTPixelFormatEAC_R11Unorm:
  case WMTPixelFormatEAC_R11Snorm:
  case WMTPixelFormatEAC_RG11Unorm:
  case WMTPixelFormatEAC_RG11Snorm:
  case WMTPixelFormatEAC_RGBA8:
  case WMTPixelFormatEAC_RGBA8_sRGB:
  case WMTPixelFormatETC2_RGB8:
  case WMTPixelFormatETC2_RGB8_sRGB:
  case WMTPixelFormatETC2_RGB8A1:
  case WMTPixelFormatETC2_RGB8A1_sRGB:
  case WMTPixelFormatASTC_4x4_sRGB:
  case WMTPixelFormatASTC_5x4_sRGB:
  case WMTPixelFormatASTC_5x5_sRGB:
  case WMTPixelFormatASTC_6x5_sRGB:
  case WMTPixelFormatASTC_6x6_sRGB:
  case WMTPixelFormatASTC_8x5_sRGB:
  case WMTPixelFormatASTC_8x6_sRGB:
  case WMTPixelFormatASTC_8x8_sRGB:
  case WMTPixelFormatASTC_10x5_sRGB:
  case WMTPixelFormatASTC_10x6_sRGB:
  case WMTPixelFormatASTC_10x8_sRGB:
  case WMTPixelFormatASTC_10x10_sRGB:
  case WMTPixelFormatASTC_12x10_sRGB:
  case WMTPixelFormatASTC_12x12_sRGB:
  case WMTPixelFormatASTC_4x4_LDR:
  case WMTPixelFormatASTC_5x4_LDR:
  case WMTPixelFormatASTC_5x5_LDR:
  case WMTPixelFormatASTC_6x5_LDR:
  case WMTPixelFormatASTC_6x6_LDR:
  case WMTPixelFormatASTC_8x5_LDR:
  case WMTPixelFormatASTC_8x6_LDR:
  case WMTPixelFormatASTC_8x8_LDR:
  case WMTPixelFormatASTC_10x5_LDR:
  case WMTPixelFormatASTC_10x6_LDR:
  case WMTPixelFormatASTC_10x8_LDR:
  case WMTPixelFormatASTC_10x10_LDR:
  case WMTPixelFormatASTC_12x10_LDR:
  case WMTPixelFormatASTC_12x12_LDR:
  case WMTPixelFormatASTC_4x4_HDR:
  case WMTPixelFormatASTC_5x4_HDR:
  case WMTPixelFormatASTC_5x5_HDR:
  case WMTPixelFormatASTC_6x5_HDR:
  case WMTPixelFormatASTC_6x6_HDR:
  case WMTPixelFormatASTC_8x5_HDR:
  case WMTPixelFormatASTC_8x6_HDR:
  case WMTPixelFormatASTC_8x8_HDR:
  case WMTPixelFormatASTC_10x5_HDR:
  case WMTPixelFormatASTC_10x6_HDR:
  case WMTPixelFormatASTC_10x8_HDR:
  case WMTPixelFormatASTC_10x10_HDR:
  case WMTPixelFormatASTC_12x10_HDR:
  case WMTPixelFormatASTC_12x12_HDR:
  case WMTPixelFormatGBGR422:
  case WMTPixelFormatBGRG422:
  case WMTPixelFormatDepth16Unorm:
  case WMTPixelFormatDepth32Float:
  case WMTPixelFormatStencil8:
  case WMTPixelFormatDepth24Unorm_Stencil8:
  case WMTPixelFormatDepth32Float_Stencil8:
  case WMTPixelFormatX32_Stencil8:
  case WMTPixelFormatX24_Stencil8:
  default:
    break;
  };
  return WMTPixelFormatInvalid;
};

bool
IsUnorm8RenderTargetFormat(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatA8Unorm:
  case WMTPixelFormatR8Unorm:
  case WMTPixelFormatRG8Unorm:
  case WMTPixelFormatRGBA8Unorm:
  case WMTPixelFormatBGRA8Unorm:
  // case WMTPixelFormatRG8Snorm: // not sure how they work, be conservative.
  // case WMTPixelFormatRGBA8Snorm:
  // case WMTPixelFormatR8Unorm_sRGB:
  // case WMTPixelFormatRG8Unorm_sRGB:
  // case WMTPixelFormatRGBA8Unorm_sRGB:
  // case WMTPixelFormatBGRA8Unorm_sRGB:
    return true;
  default:
    break;
  }
  return false;
}

bool
IsIntegerFormat(WMTPixelFormat format) {
  switch (format) {
  case WMTPixelFormatR8Uint:
  case WMTPixelFormatR8Sint:
  case WMTPixelFormatR16Uint:
  case WMTPixelFormatR16Sint:
  case WMTPixelFormatRG8Uint:
  case WMTPixelFormatRG8Sint:
  case WMTPixelFormatR32Uint:
  case WMTPixelFormatR32Sint:
  case WMTPixelFormatRG16Uint:
  case WMTPixelFormatRG16Sint:
  case WMTPixelFormatRGBA8Uint:
  case WMTPixelFormatRGBA8Sint:
  case WMTPixelFormatRGB10A2Uint:
  case WMTPixelFormatRG32Uint:
  case WMTPixelFormatRG32Sint:
  case WMTPixelFormatRGBA16Uint:
  case WMTPixelFormatRGBA16Sint:
  case WMTPixelFormatRGBA32Uint:
  case WMTPixelFormatRGBA32Sint:
  case WMTPixelFormatStencil8:
    return true;
  default:
    break;
  }
  return false;
}

} // namespace dxmt