#ifndef _WINEMETAL_H
#define _WINEMETAL_H

#ifdef __cplusplus
#include <cstdint>
#include <cassert>
#define STATIC_ASSERT(x) static_assert(x)
#else
#include <stdint.h>
#include <stdbool.h>
#define STATIC_ASSERT(x)
#endif

#ifndef DXMT_NATIVE
#define WINEMETAL_IMPORT __declspec(dllimport)
#else
#define WINEMETAL_IMPORT
#endif

#ifndef WINEMETAL_API
#ifdef __cplusplus
#define WINEMETAL_API extern "C" WINEMETAL_IMPORT
#else
#define WINEMETAL_API WINEMETAL_IMPORT
#endif
#endif

typedef uint64_t obj_handle_t;

#define NULL_OBJECT_HANDLE 0

WINEMETAL_API void NSObject_retain(obj_handle_t obj);

WINEMETAL_API void NSObject_release(obj_handle_t obj);

WINEMETAL_API obj_handle_t NSArray_object(obj_handle_t array, uint64_t index);

WINEMETAL_API uint64_t NSArray_count(obj_handle_t array);

WINEMETAL_API obj_handle_t WMTCopyAllDevices();

enum WMTStringEncoding : uint64_t {
  WMTASCIIStringEncoding = 1,
  WMTNEXTSTEPStringEncoding = 2,
  WMTJapaneseEUCStringEncoding = 3,
  WMTUTF8StringEncoding = 4,
  WMTWMTISOLatin1StringEncoding = 5,
  SymbolStringEncoding = 6,
  WMTNonLossyASCIIStringEncoding = 7,
  WMTShiftJISStringEncoding = 8,
  WMTISOLatin2StringEncoding = 9,
  WMTUnicodeStringEncoding = 10,
  WMTWindowsCP1251StringEncoding = 11,
  WMTWindowsCP1252StringEncoding = 12,
  WMTWindowsCP1253StringEncoding = 13,
  WMTWindowsCP1254StringEncoding = 14,
  WMTWindowsCP1250StringEncoding = 15,
  WMTISO2022JPStringEncoding = 21,
  WMTMacOSRomanStringEncoding = 30,

  WMTUTF16StringEncoding = WMTUnicodeStringEncoding,

  WMTUTF16BigEndianStringEncoding = 0x90000100,
  WMTUTF16LittleEndianStringEncoding = 0x94000100,

  WMTUTF32StringEncoding = 0x8c000100,
  WMTUTF32BigEndianStringEncoding = 0x98000100,
  WMTUTF32LittleEndianStringEncoding = 0x9c000100
};

WINEMETAL_API uint64_t MTLDevice_recommendedMaxWorkingSetSize(obj_handle_t device);

WINEMETAL_API uint64_t MTLDevice_currentAllocatedSize(obj_handle_t device);

WINEMETAL_API obj_handle_t MTLDevice_name(obj_handle_t device);

WINEMETAL_API uint32_t
NSString_getCString(obj_handle_t str, char *buffer, uint64_t maxLength, enum WMTStringEncoding encoding);

WINEMETAL_API obj_handle_t MTLDevice_newCommandQueue(obj_handle_t device, uint64_t maxCommandBufferCount);

WINEMETAL_API obj_handle_t NSAutoreleasePool_alloc_init();

WINEMETAL_API obj_handle_t MTLCommandQueue_commandBuffer(obj_handle_t queue);

WINEMETAL_API void MTLCommandBuffer_commit(obj_handle_t cmdbuf);

WINEMETAL_API void MTLCommandBuffer_waitUntilCompleted(obj_handle_t cmdbuf);

enum WMTCommandBufferStatus : uint64_t {
  WMTCommandBufferStatusNotEnqueued = 0,
  WMTCommandBufferStatusEnqueued = 1,
  WMTCommandBufferStatusCommitted = 2,
  WMTCommandBufferStatusScheduled = 3,
  WMTCommandBufferStatusCompleted = 4,
  WMTCommandBufferStatusError = 5,
};

WINEMETAL_API enum WMTCommandBufferStatus MTLCommandBuffer_status(obj_handle_t cmdbuf);

WINEMETAL_API obj_handle_t MTLDevice_newSharedEvent(obj_handle_t device);

WINEMETAL_API uint64_t MTLSharedEvent_signaledValue(obj_handle_t event);

WINEMETAL_API void MTLCommandBuffer_encodeSignalEvent(obj_handle_t cmdbuf, obj_handle_t event, uint64_t value);

enum WMTResourceOptions : uint64_t {
  WMTResourceCPUCacheModeDefaultCache = 0,
  WMTResourceCPUCacheModeWriteCombined = 1,
  WMTResourceStorageModeShared = 0,
  WMTResourceStorageModeManaged = 16,
  WMTResourceStorageModePrivate = 32,
  WMTResourceStorageModeMemoryless = 48,
  WMTResourceHazardTrackingModeDefault = 0,
  WMTResourceHazardTrackingModeUntracked = 256,
  WMTResourceHazardTrackingModeTracked = 512,
  WMTResourceOptionCPUCacheModeDefault = 0,
  WMTResourceOptionCPUCacheModeWriteCombined = 1,
};

struct WMTMemoryPointer {
  void *ptr;
#if defined(__i386__)
  uint32_t high_part;
#endif

#ifdef __cplusplus
  void
  set(void *p) {
#if defined(__i386__)
    high_part = 0;
#endif
    ptr = p;
  }

  void *
  get() {
#if defined(__i386__)
    assert(!high_part && "inaccessible 64-bit pointer");
#endif
    return ptr;
  }
#endif
};

struct WMTConstMemoryPointer {
  const void *ptr;
#if defined(__i386__)
  uint32_t high_part;
#endif

#ifdef __cplusplus
  void
  set(const void *p) {
#if defined(__i386__)
    high_part = 0;
#endif
    ptr = p;
  }

  const void *
  get() {
#if defined(__i386__)
    assert(!high_part && "inaccessible 64-bit pointer");
#endif
    return ptr;
  }
#endif
};

#if defined(__i386__)
#define WMT_MEMPTR_SET(obj, value)                                                                                     \
  obj.high_part = 0;                                                                                                   \
  obj.ptr = value
#else
#define WMT_MEMPTR_SET(obj, value) obj.ptr = value
#endif

STATIC_ASSERT(sizeof(WMTMemoryPointer) == 8);
STATIC_ASSERT(sizeof(WMTConstMemoryPointer) == 8);

struct WMTBufferInfo {
  uint64_t length;                 // in
  enum WMTResourceOptions options; // in
  struct WMTMemoryPointer memory;  // inout
  uint64_t gpu_address;            // out
};

STATIC_ASSERT(sizeof(WMTBufferInfo) == 32);

WINEMETAL_API obj_handle_t MTLDevice_newBuffer(obj_handle_t device, struct WMTBufferInfo *info);

enum WMTSamplerBorderColor : uint8_t {
  WMTSamplerBorderColorTransparentBlack = 0,
  WMTSamplerBorderColorOpaqueBlack = 1,
  WMTSamplerBorderColorOpaqueWhite = 2,
};

enum WMTSamplerAddressMode : uint8_t {
  WMTSamplerAddressModeClampToEdge = 0,
  WMTSamplerAddressModeMirrorClampToEdge = 1,
  WMTSamplerAddressModeRepeat = 2,
  WMTSamplerAddressModeMirrorRepeat = 3,
  WMTSamplerAddressModeClampToZero = 4,
  WMTSamplerAddressModeClampToBorderColor = 5,
};

enum WMTSamplerMipFilter : uint8_t {
  WMTSamplerMipFilterNotMipmapped = 0,
  WMTSamplerMipFilterNearest = 1,
  WMTSamplerMipFilterLinear = 2,
};

enum WMTSamplerMinMagFilter : uint8_t {
  WMTSamplerMinMagFilterNearest = 0,
  WMTSamplerMinMagFilterLinear = 1,
};

enum WMTCompareFunction : uint8_t {
  WMTCompareFunctionNever = 0,
  WMTCompareFunctionLess = 1,
  WMTCompareFunctionEqual = 2,
  WMTCompareFunctionLessEqual = 3,
  WMTCompareFunctionGreater = 4,
  WMTCompareFunctionNotEqual = 5,
  WMTCompareFunctionGreaterEqual = 6,
  WMTCompareFunctionAlways = 7,
};

struct WMTSamplerInfo {
  enum WMTSamplerMinMagFilter min_filter;
  enum WMTSamplerMinMagFilter mag_filter;
  enum WMTSamplerMipFilter mip_filter;
  enum WMTSamplerAddressMode r_address_mode;
  enum WMTSamplerAddressMode s_address_mode;
  enum WMTSamplerAddressMode t_address_mode;
  enum WMTSamplerBorderColor border_color;
  enum WMTCompareFunction compare_function;
  float lod_min_clamp;
  float lod_max_clamp;
  uint32_t max_anisotroy;
  bool normalized_coords;
  bool lod_average;
  bool support_argument_buffers;
  uint64_t gpu_resource_id; // out
};

STATIC_ASSERT(sizeof(WMTSamplerInfo) == 32);

WINEMETAL_API obj_handle_t MTLDevice_newSamplerState(obj_handle_t device, struct WMTSamplerInfo *info);

enum WMTStencilOperation : uint8_t {
  WMTStencilOperationKeep = 0,
  WMTStencilOperationZero = 1,
  WMTStencilOperationReplace = 2,
  WMTStencilOperationIncrementClamp = 3,
  WMTStencilOperationDecrementClamp = 4,
  WMTStencilOperationInvert = 5,
  WMTStencilOperationIncrementWrap = 6,
  WMTStencilOperationDecrementWrap = 7,
};

struct WMTStencilInfo {
  bool enabled;
  enum WMTStencilOperation depth_stencil_pass_op;
  enum WMTStencilOperation stencil_fail_op;
  enum WMTStencilOperation depth_fail_op;
  enum WMTCompareFunction stencil_compare_function;
  uint8_t write_mask;
  uint8_t read_mask;
};

struct WMTDepthStencilInfo {
  enum WMTCompareFunction depth_compare_function;
  bool depth_write_enabled;
  struct WMTStencilInfo front_stencil;
  struct WMTStencilInfo back_stencil;
};

WINEMETAL_API obj_handle_t MTLDevice_newDepthStencilState(obj_handle_t device, const struct WMTDepthStencilInfo *info);

enum WMTPixelFormat : uint32_t {
  WMTPixelFormatInvalid = 0,
  WMTPixelFormatA8Unorm = 1,
  WMTPixelFormatR8Unorm = 10,
  WMTPixelFormatR8Unorm_sRGB = 11,
  WMTPixelFormatR8Snorm = 12,
  WMTPixelFormatR8Uint = 13,
  WMTPixelFormatR8Sint = 14,
  WMTPixelFormatR16Unorm = 20,
  WMTPixelFormatR16Snorm = 22,
  WMTPixelFormatR16Uint = 23,
  WMTPixelFormatR16Sint = 24,
  WMTPixelFormatR16Float = 25,
  WMTPixelFormatRG8Unorm = 30,
  WMTPixelFormatRG8Unorm_sRGB = 31,
  WMTPixelFormatRG8Snorm = 32,
  WMTPixelFormatRG8Uint = 33,
  WMTPixelFormatRG8Sint = 34,
  WMTPixelFormatB5G6R5Unorm = 40,
  WMTPixelFormatA1BGR5Unorm = 41,
  WMTPixelFormatABGR4Unorm = 42,
  WMTPixelFormatBGR5A1Unorm = 43,
  WMTPixelFormatR32Uint = 53,
  WMTPixelFormatR32Sint = 54,
  WMTPixelFormatR32Float = 55,
  WMTPixelFormatRG16Unorm = 60,
  WMTPixelFormatRG16Snorm = 62,
  WMTPixelFormatRG16Uint = 63,
  WMTPixelFormatRG16Sint = 64,
  WMTPixelFormatRG16Float = 65,
  WMTPixelFormatRGBA8Unorm = 70,
  WMTPixelFormatRGBA8Unorm_sRGB = 71,
  WMTPixelFormatRGBA8Snorm = 72,
  WMTPixelFormatRGBA8Uint = 73,
  WMTPixelFormatRGBA8Sint = 74,
  WMTPixelFormatBGRA8Unorm = 80,
  WMTPixelFormatBGRA8Unorm_sRGB = 81,
  WMTPixelFormatRGB10A2Unorm = 90,
  WMTPixelFormatRGB10A2Uint = 91,
  WMTPixelFormatRG11B10Float = 92,
  WMTPixelFormatRGB9E5Float = 93,
  WMTPixelFormatBGR10A2Unorm = 94,
  WMTPixelFormatBGR10_XR = 554,
  WMTPixelFormatBGR10_XR_sRGB = 555,
  WMTPixelFormatRG32Uint = 103,
  WMTPixelFormatRG32Sint = 104,
  WMTPixelFormatRG32Float = 105,
  WMTPixelFormatRGBA16Unorm = 110,
  WMTPixelFormatRGBA16Snorm = 112,
  WMTPixelFormatRGBA16Uint = 113,
  WMTPixelFormatRGBA16Sint = 114,
  WMTPixelFormatRGBA16Float = 115,
  WMTPixelFormatBGRA10_XR = 552,
  WMTPixelFormatBGRA10_XR_sRGB = 553,
  WMTPixelFormatRGBA32Uint = 123,
  WMTPixelFormatRGBA32Sint = 124,
  WMTPixelFormatRGBA32Float = 125,
  WMTPixelFormatBC1_RGBA = 130,
  WMTPixelFormatBC1_RGBA_sRGB = 131,
  WMTPixelFormatBC2_RGBA = 132,
  WMTPixelFormatBC2_RGBA_sRGB = 133,
  WMTPixelFormatBC3_RGBA = 134,
  WMTPixelFormatBC3_RGBA_sRGB = 135,
  WMTPixelFormatBC4_RUnorm = 140,
  WMTPixelFormatBC4_RSnorm = 141,
  WMTPixelFormatBC5_RGUnorm = 142,
  WMTPixelFormatBC5_RGSnorm = 143,
  WMTPixelFormatBC6H_RGBFloat = 150,
  WMTPixelFormatBC6H_RGBUfloat = 151,
  WMTPixelFormatBC7_RGBAUnorm = 152,
  WMTPixelFormatBC7_RGBAUnorm_sRGB = 153,
  WMTPixelFormatPVRTC_RGB_2BPP = 160,
  WMTPixelFormatPVRTC_RGB_2BPP_sRGB = 161,
  WMTPixelFormatPVRTC_RGB_4BPP = 162,
  WMTPixelFormatPVRTC_RGB_4BPP_sRGB = 163,
  WMTPixelFormatPVRTC_RGBA_2BPP = 164,
  WMTPixelFormatPVRTC_RGBA_2BPP_sRGB = 165,
  WMTPixelFormatPVRTC_RGBA_4BPP = 166,
  WMTPixelFormatPVRTC_RGBA_4BPP_sRGB = 167,
  WMTPixelFormatEAC_R11Unorm = 170,
  WMTPixelFormatEAC_R11Snorm = 172,
  WMTPixelFormatEAC_RG11Unorm = 174,
  WMTPixelFormatEAC_RG11Snorm = 176,
  WMTPixelFormatEAC_RGBA8 = 178,
  WMTPixelFormatEAC_RGBA8_sRGB = 179,
  WMTPixelFormatETC2_RGB8 = 180,
  WMTPixelFormatETC2_RGB8_sRGB = 181,
  WMTPixelFormatETC2_RGB8A1 = 182,
  WMTPixelFormatETC2_RGB8A1_sRGB = 183,
  WMTPixelFormatASTC_4x4_sRGB = 186,
  WMTPixelFormatASTC_5x4_sRGB = 187,
  WMTPixelFormatASTC_5x5_sRGB = 188,
  WMTPixelFormatASTC_6x5_sRGB = 189,
  WMTPixelFormatASTC_6x6_sRGB = 190,
  WMTPixelFormatASTC_8x5_sRGB = 192,
  WMTPixelFormatASTC_8x6_sRGB = 193,
  WMTPixelFormatASTC_8x8_sRGB = 194,
  WMTPixelFormatASTC_10x5_sRGB = 195,
  WMTPixelFormatASTC_10x6_sRGB = 196,
  WMTPixelFormatASTC_10x8_sRGB = 197,
  WMTPixelFormatASTC_10x10_sRGB = 198,
  WMTPixelFormatASTC_12x10_sRGB = 199,
  WMTPixelFormatASTC_12x12_sRGB = 200,
  WMTPixelFormatASTC_4x4_LDR = 204,
  WMTPixelFormatASTC_5x4_LDR = 205,
  WMTPixelFormatASTC_5x5_LDR = 206,
  WMTPixelFormatASTC_6x5_LDR = 207,
  WMTPixelFormatASTC_6x6_LDR = 208,
  WMTPixelFormatASTC_8x5_LDR = 210,
  WMTPixelFormatASTC_8x6_LDR = 211,
  WMTPixelFormatASTC_8x8_LDR = 212,
  WMTPixelFormatASTC_10x5_LDR = 213,
  WMTPixelFormatASTC_10x6_LDR = 214,
  WMTPixelFormatASTC_10x8_LDR = 215,
  WMTPixelFormatASTC_10x10_LDR = 216,
  WMTPixelFormatASTC_12x10_LDR = 217,
  WMTPixelFormatASTC_12x12_LDR = 218,
  WMTPixelFormatASTC_4x4_HDR = 222,
  WMTPixelFormatASTC_5x4_HDR = 223,
  WMTPixelFormatASTC_5x5_HDR = 224,
  WMTPixelFormatASTC_6x5_HDR = 225,
  WMTPixelFormatASTC_6x6_HDR = 226,
  WMTPixelFormatASTC_8x5_HDR = 228,
  WMTPixelFormatASTC_8x6_HDR = 229,
  WMTPixelFormatASTC_8x8_HDR = 230,
  WMTPixelFormatASTC_10x5_HDR = 231,
  WMTPixelFormatASTC_10x6_HDR = 232,
  WMTPixelFormatASTC_10x8_HDR = 233,
  WMTPixelFormatASTC_10x10_HDR = 234,
  WMTPixelFormatASTC_12x10_HDR = 235,
  WMTPixelFormatASTC_12x12_HDR = 236,
  WMTPixelFormatGBGR422 = 240,
  WMTPixelFormatBGRG422 = 241,
  WMTPixelFormatDepth16Unorm = 250,
  WMTPixelFormatDepth32Float = 252,
  WMTPixelFormatStencil8 = 253,
  WMTPixelFormatDepth24Unorm_Stencil8 = 255,
  WMTPixelFormatDepth32Float_Stencil8 = 260,
  WMTPixelFormatX32_Stencil8 = 261,
  WMTPixelFormatX24_Stencil8 = 262,
};

enum WMTTextureType : uint8_t {
  WMTTextureType1D = 0,
  WMTTextureType1DArray = 1,
  WMTTextureType2D = 2,
  WMTTextureType2DArray = 3,
  WMTTextureType2DMultisample = 4,
  WMTTextureTypeCube = 5,
  WMTTextureTypeCubeArray = 6,
  WMTTextureType3D = 7,
  WMTTextureType2DMultisampleArray = 8,
  WMTTextureTypeTextureBuffer = 9,
};

enum WMTTextureUsage : uint8_t {
  WMTTextureUsageUnknown = 0,
  WMTTextureUsageShaderRead = 1,
  WMTTextureUsageShaderWrite = 2,
  WMTTextureUsageRenderTarget = 4,
  WMTTextureUsagePixelFormatView = 16,
  WMTTextureUsageShaderAtomic = 32,
};

enum WMTTextureSwizzle : uint8_t {
  WMTTextureSwizzleZero = 0,
  WMTTextureSwizzleOne = 1,
  WMTTextureSwizzleRed = 2,
  WMTTextureSwizzleGreen = 3,
  WMTTextureSwizzleBlue = 4,
  WMTTextureSwizzleAlpha = 5,
};

struct WMTTextureSwizzleChannels {
  enum WMTTextureSwizzle r;
  enum WMTTextureSwizzle g;
  enum WMTTextureSwizzle b;
  enum WMTTextureSwizzle a;
};

struct WMTTextureInfo {
  enum WMTPixelFormat pixel_format;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t array_length;
  enum WMTTextureType type;
  uint8_t mipmap_level_count;
  uint8_t sample_count;
  enum WMTTextureUsage usage;
  enum WMTResourceOptions options;
  uint64_t gpu_resource_id; // out
};

WINEMETAL_API obj_handle_t MTLDevice_newTexture(obj_handle_t device, struct WMTTextureInfo *info);

WINEMETAL_API obj_handle_t
MTLBuffer_newTexture(obj_handle_t buffer, struct WMTTextureInfo *info, uint64_t offset, uint64_t bytes_per_row);

WINEMETAL_API obj_handle_t MTLTexture_newTextureView(
    obj_handle_t texture, enum WMTPixelFormat format, enum WMTTextureType texture_type, uint16_t level_start,
    uint16_t level_count, uint16_t slice_start, uint16_t slice_count, struct WMTTextureSwizzleChannels swizzle,
    uint64_t *out_gpu_resource_id
);

WINEMETAL_API uint64_t
MTLDevice_minimumLinearTextureAlignmentForPixelFormat(obj_handle_t device, enum WMTPixelFormat format);

enum WMTAttributeFormat : uint32_t {
  WMTAttributeFormatInvalid = 0,
  WMTAttributeFormatUChar2 = 1,
  WMTAttributeFormatUChar3 = 2,
  WMTAttributeFormatUChar4 = 3,
  WMTAttributeFormatChar2 = 4,
  WMTAttributeFormatChar3 = 5,
  WMTAttributeFormatChar4 = 6,
  WMTAttributeFormatUChar2Normalized = 7,
  WMTAttributeFormatUChar3Normalized = 8,
  WMTAttributeFormatUChar4Normalized = 9,
  WMTAttributeFormatChar2Normalized = 10,
  WMTAttributeFormatChar3Normalized = 11,
  WMTAttributeFormatChar4Normalized = 12,
  WMTAttributeFormatUShort2 = 13,
  WMTAttributeFormatUShort3 = 14,
  WMTAttributeFormatUShort4 = 15,
  WMTAttributeFormatShort2 = 16,
  WMTAttributeFormatShort3 = 17,
  WMTAttributeFormatShort4 = 18,
  WMTAttributeFormatUShort2Normalized = 19,
  WMTAttributeFormatUShort3Normalized = 20,
  WMTAttributeFormatUShort4Normalized = 21,
  WMTAttributeFormatShort2Normalized = 22,
  WMTAttributeFormatShort3Normalized = 23,
  WMTAttributeFormatShort4Normalized = 24,
  WMTAttributeFormatHalf2 = 25,
  WMTAttributeFormatHalf3 = 26,
  WMTAttributeFormatHalf4 = 27,
  WMTAttributeFormatFloat = 28,
  WMTAttributeFormatFloat2 = 29,
  WMTAttributeFormatFloat3 = 30,
  WMTAttributeFormatFloat4 = 31,
  WMTAttributeFormatInt = 32,
  WMTAttributeFormatInt2 = 33,
  WMTAttributeFormatInt3 = 34,
  WMTAttributeFormatInt4 = 35,
  WMTAttributeFormatUInt = 36,
  WMTAttributeFormatUInt2 = 37,
  WMTAttributeFormatUInt3 = 38,
  WMTAttributeFormatUInt4 = 39,
  WMTAttributeFormatInt1010102Normalized = 40,
  WMTAttributeFormatUInt1010102Normalized = 41,
  WMTAttributeFormatUChar4Normalized_BGRA = 42,
  WMTAttributeFormatUChar = 45,
  WMTAttributeFormatChar = 46,
  WMTAttributeFormatUCharNormalized = 47,
  WMTAttributeFormatCharNormalized = 48,
  WMTAttributeFormatUShort = 49,
  WMTAttributeFormatShort = 50,
  WMTAttributeFormatUShortNormalized = 51,
  WMTAttributeFormatShortNormalized = 52,
  WMTAttributeFormatHalf = 53,
  WMTAttributeFormatFloatRG11B10 = 54,
  WMTAttributeFormatFloatRGB9E5 = 55,
};

WINEMETAL_API obj_handle_t MTLDevice_newLibrary(
    obj_handle_t device, struct WMTMemoryPointer bytecode, uint64_t bytecode_length, obj_handle_t *err_out
);

WINEMETAL_API obj_handle_t MTLLibrary_newFunction(obj_handle_t library, const char *name);

WINEMETAL_API uint64_t NSString_lengthOfBytesUsingEncoding(obj_handle_t str, enum WMTStringEncoding encoding);

WINEMETAL_API obj_handle_t NSObject_description(obj_handle_t nserror);

WINEMETAL_API obj_handle_t
MTLDevice_newComputePipelineState(obj_handle_t device, obj_handle_t function, obj_handle_t *err_out);

WINEMETAL_API obj_handle_t MTLCommandBuffer_blitCommandEncoder(obj_handle_t cmdbuf);

WINEMETAL_API obj_handle_t MTLCommandBuffer_computeCommandEncoder(obj_handle_t cmdbuf, bool concurrent);

enum WMTLoadAction {
  WMTLoadActionDontCare = 0,
  WMTLoadActionLoad = 1,
  WMTLoadActionClear = 2,
};

enum WMTStoreAction {
  WMTStoreActionDontCare = 0,
  WMTStoreActionStore = 1,
  WMTStoreActionMultisampleResolve = 2,
  WMTStoreActionStoreAndMultisampleResolve = 3,
  WMTStoreActionUnknown = 4,
  WMTStoreActionCustomSampleDepthStore = 5,
};

struct WMTClearColor {
  double r;
  double g;
  double b;
  double a;
};

struct WMTColorAttachmentInfo {
  obj_handle_t texture;
  enum WMTLoadAction load_action;
  enum WMTStoreAction store_action;
  uint16_t level;
  uint16_t slice;
  uint32_t depth_plane;
  struct WMTClearColor clear_color;
  obj_handle_t resolve_texture;
  uint16_t resolve_level;
  uint16_t resolve_slice;
  uint32_t resolve_depth_plane;
};

struct WMTDepthAttachmentInfo {
  obj_handle_t texture;
  enum WMTLoadAction load_action;
  enum WMTStoreAction store_action;
  uint16_t level;
  uint16_t slice;
  uint32_t depth_plane;
  float clear_depth;
};

struct WMTStencilAttachmentInfo {
  obj_handle_t texture;
  enum WMTLoadAction load_action;
  enum WMTStoreAction store_action;
  uint16_t level;
  uint16_t slice;
  uint32_t depth_plane;
  uint8_t clear_stencil;
};

struct WMTRenderPassInfo {
  struct WMTColorAttachmentInfo colors[8];
  struct WMTDepthAttachmentInfo depth;
  struct WMTStencilAttachmentInfo stencil;
  uint8_t default_raster_sample_count;
  uint16_t render_target_array_length;
  uint32_t render_target_height;
  uint32_t render_target_width;
  obj_handle_t visibility_buffer;
};

WINEMETAL_API obj_handle_t MTLCommandBuffer_renderCommandEncoder(obj_handle_t cmdbuf, struct WMTRenderPassInfo *info);

WINEMETAL_API void MTLCommandEncoder_endEncoding(obj_handle_t encoder);

enum WMTTessellationPartitionMode : uint8_t {
  WMTTessellationPartitionModePow2 = 0,
  WMTTessellationPartitionModeInteger = 1,
  WMTTessellationPartitionModeFractionalOdd = 2,
  WMTTessellationPartitionModeFractionalEven = 3,
};

enum WMTTessellationFactorStepFunction : uint8_t {
  WMTTessellationFactorStepFunctionConstant = 0,
  WMTTessellationFactorStepFunctionPerPatch = 1,
  WMTTessellationFactorStepFunctionPerInstance = 2,
  WMTTessellationFactorStepFunctionPerPatchAndPerInstance = 3,
};

enum WMTWinding : uint8_t {
  WMTWindingClockwise = 0,
  WMTWindingCounterClockwise = 1,
};

enum WMTBlendOperation : uint8_t {
  WMTBlendOperationAdd = 0,
  WMTBlendOperationSubtract = 1,
  WMTBlendOperationReverseSubtract = 2,
  WMTBlendOperationMin = 3,
  WMTBlendOperationMax = 4,
};

enum WMTLogicOperation : uint8_t {
  WMTLogicOperationClear = 0,
  WMTLogicOperationSet = 1,
  WMTLogicOperationCopy = 2,
  WMTLogicOperationCopyInverted = 3,
  WMTLogicOperationNoOp = 4,
  WMTLogicOperationInvert = 5,
  WMTLogicOperationAnd = 6,
  WMTLogicOperationNand = 7,
  WMTLogicOperationOr = 8,
  WMTLogicOperationNor = 9,
  WMTLogicOperationXor = 10,
  WMTLogicOperationEquiv = 11,
  WMTLogicOperationAndReverse = 12,
  WMTLogicOperationAndInverted = 13,
  WMTLogicOperationOrReverse = 14,
  WMTLogicOperationOrInverted = 15,
};

enum WMTBlendFactor : uint8_t {
  WMTBlendFactorZero = 0,
  WMTBlendFactorOne = 1,
  WMTBlendFactorSourceColor = 2,
  WMTBlendFactorOneMinusSourceColor = 3,
  WMTBlendFactorSourceAlpha = 4,
  WMTBlendFactorOneMinusSourceAlpha = 5,
  WMTBlendFactorDestinationColor = 6,
  WMTBlendFactorOneMinusDestinationColor = 7,
  WMTBlendFactorDestinationAlpha = 8,
  WMTBlendFactorOneMinusDestinationAlpha = 9,
  WMTBlendFactorSourceAlphaSaturated = 10,
  WMTBlendFactorBlendColor = 11,
  WMTBlendFactorOneMinusBlendColor = 12,
  WMTBlendFactorBlendAlpha = 13,
  WMTBlendFactorOneMinusBlendAlpha = 14,
  WMTBlendFactorSource1Color = 15,
  WMTBlendFactorOneMinusSource1Color = 16,
  WMTBlendFactorSource1Alpha = 17,
  WMTBlendFactorOneMinusSource1Alpha = 18,
};

enum WMTColorWriteMask : uint8_t {
  WMTColorWriteMaskNone = 0,
  WMTColorWriteMaskRed = 8,
  WMTColorWriteMaskGreen = 4,
  WMTColorWriteMaskBlue = 2,
  WMTColorWriteMaskAlpha = 1,
  WMTColorWriteMaskAll = 15,
};

struct WMTColorAttachmentBlendInfo {
  enum WMTPixelFormat pixel_format;
  enum WMTBlendOperation rgb_blend_operation;
  enum WMTBlendOperation alpha_blend_operation;
  enum WMTBlendFactor src_rgb_blend_factor;
  enum WMTBlendFactor dst_rgb_blend_factor;
  enum WMTBlendFactor src_alpha_blend_factor;
  enum WMTBlendFactor dst_alpha_blend_factor;
  uint8_t write_mask;
  bool blending_enabled;
};

enum WMTPrimitiveTopologyClass {
  WMTPrimitiveTopologyClassUnspecified = 0,
  WMTPrimitiveTopologyClassPoint = 1,
  WMTPrimitiveTopologyClassLine = 2,
  WMTPrimitiveTopologyClassTriangle = 3,
};

struct WMTRenderPipelineBlendInfo {
  struct WMTColorAttachmentBlendInfo colors[8];
  bool alpha_to_coverage_enabled;
  bool logic_operation_enabled;
  enum WMTLogicOperation logic_operation;
};

struct WMTRenderPipelineInfo {
  struct WMTColorAttachmentBlendInfo colors[8];
  bool alpha_to_coverage_enabled;
  bool logic_operation_enabled;
  enum WMTLogicOperation logic_operation;
  bool rasterization_enabled;
  uint8_t raster_sample_count;
  enum WMTPixelFormat depth_pixel_format;
  enum WMTPixelFormat stencil_pixel_format;
  obj_handle_t vertex_function;
  obj_handle_t fragment_function;
  uint32_t immutable_vertex_buffers;
  uint32_t immutable_fragment_buffers;
  enum WMTPrimitiveTopologyClass input_primitive_topology;
  enum WMTTessellationPartitionMode tessellation_partition_mode;
  uint8_t max_tessellation_factor;
  enum WMTWinding tessellation_output_winding_order;
  enum WMTTessellationFactorStepFunction tessellation_factor_step;
};

struct WMTMeshRenderPipelineInfo {
  struct WMTColorAttachmentBlendInfo colors[8];
  bool alpha_to_coverage_enabled;
  bool logic_operation_enabled;
  enum WMTLogicOperation logic_operation;
  bool rasterization_enabled;
  uint8_t raster_sample_count;
  enum WMTPixelFormat depth_pixel_format;
  enum WMTPixelFormat stencil_pixel_format;
  obj_handle_t object_function;
  obj_handle_t mesh_function;
  obj_handle_t fragment_function;
  uint32_t immutable_object_buffers;
  uint32_t immutable_mesh_buffers;
  uint32_t immutable_fragment_buffers;
  uint16_t payload_memory_length;
};

WINEMETAL_API obj_handle_t
MTLDevice_newRenderPipelineState(obj_handle_t device, const struct WMTRenderPipelineInfo *info, obj_handle_t *err_out);

WINEMETAL_API obj_handle_t MTLDevice_newMeshRenderPipelineState(
    obj_handle_t device, const struct WMTMeshRenderPipelineInfo *info, obj_handle_t *err_out
);

struct WMTSize {
  uint64_t width;
  uint64_t height;
  uint64_t depth;
};

struct WMTOrigin {
  uint64_t x;
  uint64_t y;
  uint64_t z;
};

enum WMTBlitCommandType : uint16_t {
  WMTBlitCommandNop,
  WMTBlitCommandCopyFromBufferToBuffer,
  WMTBlitCommandCopyFromBufferToTexture,
  WMTBlitCommandCopyFromTextureToBuffer,
  WMTBlitCommandCopyFromTextureToTexture,
  WMTBlitCommandGenerateMipmaps,
  WMTBlitCommandWaitForFence,
  WMTBlitCommandUpdateFence,
};

struct wmtcmd_base {
  uint16_t type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
};

struct wmtcmd_blit_nop {
  enum WMTBlitCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
};

struct wmtcmd_blit_copy_from_buffer_to_buffer {
  enum WMTBlitCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t src;
  uint64_t src_offset;
  obj_handle_t dst;
  uint64_t dst_offset;
  uint64_t copy_length;
};

struct wmtcmd_blit_copy_from_buffer_to_texture {
  enum WMTBlitCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t src;
  uint64_t src_offset;
  uint32_t bytes_per_row;
  uint32_t bytes_per_image;
  struct WMTSize size;
  obj_handle_t dst;
  uint32_t slice;
  uint32_t level;
  struct WMTOrigin origin;
};

struct wmtcmd_blit_copy_from_texture_to_texture {
  enum WMTBlitCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t src;
  uint32_t src_slice;
  uint32_t src_level;
  struct WMTOrigin src_origin;
  struct WMTSize src_size;
  obj_handle_t dst;
  uint32_t dst_slice;
  uint32_t dst_level;
  struct WMTOrigin dst_origin;
};

struct wmtcmd_blit_copy_from_texture_to_buffer {
  enum WMTBlitCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t src;
  uint32_t slice;
  uint32_t level;
  struct WMTOrigin origin;
  struct WMTSize size;
  obj_handle_t dst;
  uint64_t offset;
  uint32_t bytes_per_row;
  uint32_t bytes_per_image;
};

struct wmtcmd_blit_generate_mipmaps {
  enum WMTBlitCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t texture;
};

struct wmtcmd_blit_fence_op {
  enum WMTBlitCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t fence;
};

WINEMETAL_API void MTLBlitCommandEncoder_encodeCommands(obj_handle_t encoder, const struct wmtcmd_base *cmd_head);

enum WMTComputeCommandType : uint16_t {
  WMTComputeCommandNop,
  WMTComputeCommandDispatch,
  WMTComputeCommandDispatchIndirect,
  WMTComputeCommandSetPSO,
  WMTComputeCommandSetBuffer,
  WMTComputeCommandSetBufferOffset,
  WMTComputeCommandUseResource,
  WMTComputeCommandSetBytes,
  WMTComputeCommandSetTexture,
  WMTComputeCommandDispatchThreads,
  WMTComputeCommandWaitForFence,
  WMTComputeCommandUpdateFence,
};

struct wmtcmd_compute_nop {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
};

struct wmtcmd_compute_dispatch {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTSize size;
};

struct wmtcmd_compute_dispatch_indirect {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t indirect_args_buffer;
  uint64_t indirect_args_offset;
};

struct wmtcmd_compute_setpso {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t pso;
  struct WMTSize threadgroup_size;
};

struct wmtcmd_compute_setbuffer {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t buffer;
  uint64_t offset;
  uint8_t index;
};

struct wmtcmd_compute_setbufferoffset {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t offset;
  uint8_t index;
};

enum WMTResourceUsage {
  WMTResourceUsageRead = 1,
  WMTResourceUsageWrite = 2,
  WMTResourceUsageSample = 4,
};

struct wmtcmd_compute_useresource {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t resource;
  enum WMTResourceUsage usage;
};

struct wmtcmd_compute_setbytes {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTMemoryPointer bytes;
  uint64_t length;
  uint8_t index;
};

struct wmtcmd_compute_settexture {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t texture;
  uint8_t index;
};

struct wmtcmd_compute_fence_op {
  enum WMTComputeCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t fence;
};

WINEMETAL_API void MTLComputeCommandEncoder_encodeCommands(obj_handle_t encoder, const struct wmtcmd_base *cmd_head);

enum WMTRenderCommandType : uint16_t {
  WMTRenderCommandNop,
  WMTRenderCommandUseResource,
  WMTRenderCommandSetVertexBuffer,
  WMTRenderCommandSetVertexBufferOffset,
  WMTRenderCommandSetFragmentBuffer,
  WMTRenderCommandSetFragmentBufferOffset,
  WMTRenderCommandSetMeshBuffer,
  WMTRenderCommandSetMeshBufferOffset,
  WMTRenderCommandSetObjectBuffer,
  WMTRenderCommandSetObjectBufferOffset,
  WMTRenderCommandSetFragmentTexture,
  WMTRenderCommandSetFragmentBytes,
  WMTRenderCommandSetRasterizerState,
  WMTRenderCommandSetViewports,
  WMTRenderCommandSetScissorRects,
  WMTRenderCommandSetPSO,
  WMTRenderCommandSetDSSO,
  WMTRenderCommandSetBlendFactorAndStencilRef,
  WMTRenderCommandSetVisibilityMode,
  WMTRenderCommandDraw,
  WMTRenderCommandDrawIndexed,
  WMTRenderCommandDrawIndirect,
  WMTRenderCommandDrawIndexedIndirect,
  WMTRenderCommandDrawMeshThreadgroups,
  WMTRenderCommandDrawMeshThreadgroupsIndirect,
  WMTRenderCommandMemoryBarrier,
  WMTRenderCommandDXMTTessellationMeshDispatch,
  WMTRenderCommandDXMTTessellationMeshDispatchIndexed,
  WMTRenderCommandDXMTTessellationDraw,
  WMTRenderCommandDXMTGeometryDraw,
  WMTRenderCommandDXMTGeometryDrawIndexed,
  WMTRenderCommandDXMTGeometryDrawIndirect,
  WMTRenderCommandDXMTGeometryDrawIndexedIndirect,
  WMTRenderCommandWaitForFence,
  WMTRenderCommandUpdateFence,
  WMTRenderCommandSetViewport,
  WMTRenderCommandSetScissorRect,
};

struct wmtcmd_render_nop {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
};

enum WMTRenderStages : uint8_t {
  WMTRenderStageVertex = 1,
  WMTRenderStageFragment = 2,
  WMTRenderStageTile = 4,
  WMTRenderStageObject = 8,
  WMTRenderStageMesh = 16,
};

struct wmtcmd_render_useresource {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t resource;
  enum WMTResourceUsage usage;
  enum WMTRenderStages stages;
};

struct wmtcmd_render_setbuffer {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t buffer;
  uint64_t offset;
  uint8_t index;
};

struct wmtcmd_render_setbytes {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTMemoryPointer bytes;
  uint64_t length;
  uint8_t index;
};

struct wmtcmd_render_settexture {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t texture;
  uint8_t index;
};

struct wmtcmd_render_setbufferoffset {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t offset;
  uint8_t index;
};

enum WMTTriangleFillMode : uint8_t {
  WMTTriangleFillModeFill = 0,
  WMTTriangleFillModeLines = 1,
};

enum WMTCullMode : uint8_t {
  WMTCullModeNone = 0,
  WMTCullModeFront = 1,
  WMTCullModeBack = 2,
};

enum WMTDepthClipMode : uint8_t {
  WMTDepthClipModeClip = 0,
  WMTDepthClipModeClamp = 1,
};

struct wmtcmd_render_setrasterizerstate {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  enum WMTTriangleFillMode fill_mode;
  enum WMTCullMode cull_mode;
  enum WMTDepthClipMode depth_clip_mode;
  enum WMTWinding winding;
  float depth_bias;
  float scole_scale;
  float depth_bias_clamp;
};

struct wmtcmd_render_setpso {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t pso;
};

enum WMTVisibilityResultMode : uint8_t {
  WMTVisibilityResultModeDisabled = 0,
  WMTVisibilityResultModeBoolean = 1,
  WMTVisibilityResultModeCounting = 2,
};

struct wmtcmd_render_setvisibilitymode {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t offset;
  enum WMTVisibilityResultMode mode;
};

enum WMTIndexType : uint8_t {
  WMTIndexTypeUInt16 = 0,
  WMTIndexTypeUInt32 = 1,
};

enum WMTPrimitiveType : uint8_t {
  WMTPrimitiveTypePoint = 0,
  WMTPrimitiveTypeLine = 1,
  WMTPrimitiveTypeLineStrip = 2,
  WMTPrimitiveTypeTriangle = 3,
  WMTPrimitiveTypeTriangleStrip = 4,
};

struct wmtcmd_render_draw {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  enum WMTPrimitiveType primitive_type;
  uint64_t vertex_start;
  uint64_t vertex_count;
  uint32_t instance_count;
  uint32_t base_instance;
};

struct wmtcmd_render_draw_indirect {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  enum WMTPrimitiveType primitive_type;
  obj_handle_t indirect_args_buffer;
  uint64_t indirect_args_offset;
};

struct wmtcmd_render_draw_indexed {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  enum WMTPrimitiveType primitive_type;
  enum WMTIndexType index_type;
  uint64_t index_count;
  obj_handle_t index_buffer;
  uint64_t index_buffer_offset;
  uint32_t instance_count;
  uint32_t base_vertex;
  uint32_t base_instance;
};

struct wmtcmd_render_draw_indexed_indirect {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  enum WMTPrimitiveType primitive_type;
  enum WMTIndexType index_type;
  obj_handle_t index_buffer;
  uint64_t index_buffer_offset;
  obj_handle_t indirect_args_buffer;
  uint64_t indirect_args_offset;
};

struct wmtcmd_render_draw_meshthreadgroups {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTSize threadgroup_per_grid;
  struct WMTSize object_threadgroup_size;
  struct WMTSize mesh_threadgroup_size;
};
struct wmtcmd_render_draw_meshthreadgroups_indirect {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t indirect_args_buffer;
  uint64_t indirect_args_offset;
  struct WMTSize object_threadgroup_size;
  struct WMTSize mesh_threadgroup_size;
};

enum WMTBarrierScope : uint8_t {
  WMTBarrierScopeBuffers = 1,
  WMTBarrierScopeTextures = 2,
  WMTBarrierScopeRenderTargets = 4,
};

struct wmtcmd_render_memory_barrier {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  enum WMTBarrierScope scope;
  enum WMTRenderStages stages_before;
  enum WMTRenderStages stages_after;
};

struct WMTViewport {
  double originX;
  double originY;
  double width;
  double height;
  double znear;
  double zfar;
};

struct wmtcmd_render_setviewports {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTMemoryPointer viewports;
  uint8_t viewport_count;
};

struct wmtcmd_render_setviewport {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTViewport viewport;
};

struct WMTScissorRect {
  uint64_t x;
  uint64_t y;
  uint64_t width;
  uint64_t height;
};

struct wmtcmd_render_setscissorrects {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTMemoryPointer scissor_rects;
  uint8_t rect_count;
};

struct wmtcmd_render_setscissorrect {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  struct WMTScissorRect scissor_rect;
};

struct wmtcmd_render_setdsso {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t dsso;
  uint8_t stencil_ref;
};

struct wmtcmd_render_setblendcolor {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  float red;
  float green;
  float blue;
  float alpha;
  uint8_t stencil_ref;
};

struct wmtcmd_render_dxmt_tess_mesh_dispatch {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t draw_arguments_offset;
  obj_handle_t control_point_buffer;
  uint64_t control_point_buffer_offset;
  obj_handle_t patch_constant_buffer;
  uint64_t patch_constant_buffer_offset;
  obj_handle_t tessellation_factor_buffer;
  uint64_t tessellation_factor_buffer_offset;
  uint32_t patch_per_mesh_instance;
  uint32_t instance_count;
  uint32_t threads_per_patch;
  uint32_t patch_per_group;
};

struct wmtcmd_render_dxmt_tess_draw {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t draw_arguments_offset;
  obj_handle_t control_point_buffer;
  uint64_t control_point_buffer_offset;
  obj_handle_t patch_constant_buffer;
  uint64_t patch_constant_buffer_offset;
  obj_handle_t tessellation_factor_buffer;
  uint64_t tessellation_factor_buffer_offset;
  uint32_t patch_count_per_instance;
  uint32_t instance_count;
};

struct wmtcmd_render_dxmt_tess_mesh_dispatch_indexed {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t draw_arguments_offset;
  obj_handle_t index_buffer;
  uint64_t index_buffer_offset;
  obj_handle_t control_point_buffer;
  uint64_t control_point_buffer_offset;
  obj_handle_t patch_constant_buffer;
  uint64_t patch_constant_buffer_offset;
  obj_handle_t tessellation_factor_buffer;
  uint64_t tessellation_factor_buffer_offset;
  uint32_t patch_per_mesh_instance;
  uint32_t instance_count;
  uint32_t threads_per_patch;
  uint32_t patch_per_group;
};

struct wmtcmd_render_dxmt_geometry_draw {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t draw_arguments_offset;
  uint32_t warp_count;
  uint32_t instance_count;
  uint32_t vertex_per_warp;
};

struct wmtcmd_render_dxmt_geometry_draw_indexed {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  uint64_t draw_arguments_offset;
  obj_handle_t index_buffer;
  uint64_t index_buffer_offset;
  uint32_t warp_count;
  uint32_t instance_count;
  uint32_t vertex_per_warp;
};

struct wmtcmd_render_dxmt_geometry_draw_indirect {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t indirect_args_buffer;
  uint64_t indirect_args_offset;
  obj_handle_t dispatch_args_buffer;
  uint64_t dispatch_args_offset;
  uint32_t vertex_per_warp;
};

struct wmtcmd_render_dxmt_geometry_draw_indexed_indirect {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t index_buffer;
  uint64_t index_buffer_offset;
  obj_handle_t indirect_args_buffer;
  uint64_t indirect_args_offset;
  obj_handle_t dispatch_args_buffer;
  uint64_t dispatch_args_offset;
  uint32_t vertex_per_warp;
};

struct wmtcmd_render_fence_op {
  enum WMTRenderCommandType type;
  uint16_t reserved[3];
  struct WMTMemoryPointer next;
  obj_handle_t fence;
  enum WMTRenderStages stages;
};

WINEMETAL_API void MTLRenderCommandEncoder_encodeCommands(obj_handle_t encoder, const struct wmtcmd_base *cmd_head);

WINEMETAL_API enum WMTPixelFormat MTLTexture_pixelFormat(obj_handle_t texture);
WINEMETAL_API uint64_t MTLTexture_width(obj_handle_t texture);
WINEMETAL_API uint64_t MTLTexture_height(obj_handle_t texture);
WINEMETAL_API uint64_t MTLTexture_depth(obj_handle_t texture);
WINEMETAL_API uint64_t MTLTexture_arrayLength(obj_handle_t texture);
WINEMETAL_API uint64_t MTLTexture_mipmapLevelCount(obj_handle_t texture);
WINEMETAL_API void MTLTexture_replaceRegion(
    obj_handle_t texture, struct WMTOrigin origin, struct WMTSize size, uint64_t level, uint64_t slice,
    struct WMTMemoryPointer data, uint64_t bytes_per_row, uint64_t bytes_per_image
);

WINEMETAL_API void MTLBuffer_didModifyRange(obj_handle_t buffer, uint64_t start, uint64_t length);

WINEMETAL_API void MTLCommandBuffer_presentDrawable(obj_handle_t cmdbuf, obj_handle_t drawable);

WINEMETAL_API void
MTLCommandBuffer_presentDrawableAfterMinimumDuration(obj_handle_t cmdbuf, obj_handle_t drawable, double after);

enum WMTGPUFamily {
  WMTGPUFamilyApple1 = 1001,
  WMTGPUFamilyApple2 = 1002,
  WMTGPUFamilyApple3 = 1003,
  WMTGPUFamilyApple4 = 1004,
  WMTGPUFamilyApple5 = 1005,
  WMTGPUFamilyApple6 = 1006,
  WMTGPUFamilyApple7 = 1007,
  WMTGPUFamilyApple8 = 1008,
  WMTGPUFamilyApple9 = 1009,
  WMTGPUFamilyMac1 = 2001,
  WMTGPUFamilyMac2 = 2002,
  WMTGPUFamilyCommon1 = 3001,
  WMTGPUFamilyCommon2 = 3002,
  WMTGPUFamilyCommon3 = 3003,
  WMTGPUFamilyMacCatalyst1 = 4001,
  WMTGPUFamilyMacCatalyst2 = 4002,
  WMTGPUFamilyMetal3 = 5001,
};

WINEMETAL_API bool MTLDevice_supportsFamily(obj_handle_t device, enum WMTGPUFamily gpu_family);

WINEMETAL_API bool MTLDevice_supportsBCTextureCompression(obj_handle_t device);

WINEMETAL_API bool MTLDevice_supportsTextureSampleCount(obj_handle_t device, uint8_t sample_count);

WINEMETAL_API bool MTLDevice_hasUnifiedMemory(obj_handle_t device);

enum WMTCaptureDestination : uint8_t {
  WMTCaptureDestinationDeveloperTools = 1,
  WMTCaptureDestinationGPUTraceDocument = 2,
};

struct WMTCaptureInfo {
  obj_handle_t capture_object;
  struct WMTConstMemoryPointer output_url;
  enum WMTCaptureDestination destination;
};

WINEMETAL_API obj_handle_t MTLCaptureManager_sharedCaptureManager();

WINEMETAL_API bool MTLCaptureManager_startCapture(obj_handle_t mgr, struct WMTCaptureInfo *info);

WINEMETAL_API void MTLCaptureManager_stopCapture(obj_handle_t mgr);

struct WMTFXTemporalScalerInfo {
  enum WMTPixelFormat color_format;
  enum WMTPixelFormat output_format;
  enum WMTPixelFormat depth_format;
  enum WMTPixelFormat motion_format;
  uint32_t input_width;
  uint32_t input_height;
  uint32_t output_width;
  uint32_t output_height;
  float input_content_min_scale;
  float input_content_max_scale;
  bool auto_exposure;
  bool input_content_properties_enabled;
  bool requires_synchronous_initialization;
};

struct WMTFXSpatialScalerInfo {
  enum WMTPixelFormat color_format;
  enum WMTPixelFormat output_format;
  uint32_t input_width;
  uint32_t input_height;
  uint32_t output_width;
  uint32_t output_height;
};

WINEMETAL_API obj_handle_t MTLDevice_newTemporalScaler(obj_handle_t device, const struct WMTFXTemporalScalerInfo *info);

WINEMETAL_API obj_handle_t MTLDevice_newSpatialScaler(obj_handle_t device, const struct WMTFXSpatialScalerInfo *info);

struct WMTFXTemporalScalerProps {
  uint32_t input_content_width;
  uint32_t input_content_height;
  bool reset;
  bool depth_reversed;
  float motion_vector_scale_x;
  float motion_vector_scale_y;
  float jitter_offset_x;
  float jitter_offset_y;
  float pre_exposure;
};

WINEMETAL_API void MTLCommandBuffer_encodeTemporalScale(
    obj_handle_t cmdbuf, obj_handle_t scaler, obj_handle_t color, obj_handle_t output, obj_handle_t depth,
    obj_handle_t motion, obj_handle_t exposure, obj_handle_t fence, const struct WMTFXTemporalScalerProps *props
);

WINEMETAL_API void MTLCommandBuffer_encodeSpatialScale(
    obj_handle_t cmdbuf, obj_handle_t scaler, obj_handle_t color, obj_handle_t output, obj_handle_t fence
);

WINEMETAL_API obj_handle_t NSString_string(const char *data, enum WMTStringEncoding encoding);

WINEMETAL_API obj_handle_t NSString_alloc_init(const char *data, enum WMTStringEncoding encoding);

WINEMETAL_API obj_handle_t DeveloperHUDProperties_instance();

WINEMETAL_API bool DeveloperHUDProperties_addLabel(obj_handle_t obj, obj_handle_t label, obj_handle_t after);

WINEMETAL_API void DeveloperHUDProperties_updateLabel(obj_handle_t obj, obj_handle_t label, obj_handle_t value);

WINEMETAL_API void DeveloperHUDProperties_remove(obj_handle_t obj, obj_handle_t label);

WINEMETAL_API obj_handle_t MetalDrawable_texture(obj_handle_t drawable);

WINEMETAL_API obj_handle_t MetalLayer_nextDrawable(obj_handle_t layer);

WINEMETAL_API bool MTLDevice_supportsFXSpatialScaler(obj_handle_t device);

WINEMETAL_API bool MTLDevice_supportsFXTemporalScaler(obj_handle_t device);

struct WMTLayerProps {
  obj_handle_t device;
  double contents_scale;
  double drawable_width;
  double drawable_height;
  bool opaque;
  bool display_sync_enabled;
  bool framebuffer_only;
  enum WMTPixelFormat pixel_format;
};

WINEMETAL_API void MetalLayer_setProps(obj_handle_t layer, const struct WMTLayerProps *props);

WINEMETAL_API void MetalLayer_getProps(obj_handle_t layer, struct WMTLayerProps *props);

WINEMETAL_API obj_handle_t CreateMetalViewFromHWND(intptr_t hwnd, obj_handle_t device, obj_handle_t *layer);

WINEMETAL_API void ReleaseMetalView(obj_handle_t view);

WINEMETAL_API void MTLCommandEncoder_setLabel(obj_handle_t encoder, obj_handle_t label);

WINEMETAL_API void MTLDevice_setShouldMaximizeConcurrentCompilation(obj_handle_t device, bool value);

WINEMETAL_API obj_handle_t MTLCommandBuffer_error(obj_handle_t cmdbuf);

WINEMETAL_API obj_handle_t MTLCommandBuffer_logs(obj_handle_t cmdbuf);

WINEMETAL_API uint64_t
MTLLogContainer_enumerate(obj_handle_t logs, uint64_t start, uint64_t buffer_size, obj_handle_t *buffer);

enum WMTColorSpace : uint64_t {
  WMTColorSpaceSRGB = 0b000,
  WMTColorSpaceSRGBLinear = 0b001,
  WMTColorSpaceBT2020 = 0b010,
  WMTColorSpaceHDR_PQ = 0b100,
  WMTColorSpaceHDR10 = WMTColorSpaceHDR_PQ,
  WMTColorSpaceHDR_scRGB = 0b101,
  WMTColorSpaceInvalid = 0xFFFFFFFFFFFFFFFF,
};

#if !defined(WMT_COLORSPACE_IS_HDR)
#define WMT_COLORSPACE_IS_HDR(x) x & 0b100
#endif

WINEMETAL_API bool CGColorSpace_checkColorSpaceSupported(enum WMTColorSpace colorspace);

WINEMETAL_API bool MetalLayer_setColorSpace(obj_handle_t layer, enum WMTColorSpace colorpsace);

WINEMETAL_API uint32_t WMTGetPrimaryDisplayId();

WINEMETAL_API uint32_t WMTGetSecondaryDisplayId();

struct WMTDisplayDescription {
  float red_primaries[2];
  float blue_primaries[2];
  float green_primaries[2];
  float white_points[2];
  float maximum_edr_color_component_value;
  float maximum_potential_edr_color_component_value;
  float maximum_reference_edr_color_component_value;
};

WINEMETAL_API void WMTGetDisplayDescription(uint32_t display_id, struct WMTDisplayDescription *desc);

struct WMTEDRValue {
  float maximum_edr_color_component_value;
  float maximum_potential_edr_color_component_value;
};

WINEMETAL_API void MetalLayer_getEDRValue(obj_handle_t layer, struct WMTEDRValue *value);

enum WMTDataType : uint16_t {
  WMTDataTypeNone = 0,

  WMTDataTypeStruct = 1,
  WMTDataTypeArray = 2,

  WMTDataTypeFloat = 3,
  WMTDataTypeFloat2 = 4,
  WMTDataTypeFloat3 = 5,
  WMTDataTypeFloat4 = 6,

  WMTDataTypeFloat2x2 = 7,
  WMTDataTypeFloat2x3 = 8,
  WMTDataTypeFloat2x4 = 9,

  WMTDataTypeFloat3x2 = 10,
  WMTDataTypeFloat3x3 = 11,
  WMTDataTypeFloat3x4 = 12,

  WMTDataTypeFloat4x2 = 13,
  WMTDataTypeFloat4x3 = 14,
  WMTDataTypeFloat4x4 = 15,

  WMTDataTypeHalf = 16,
  WMTDataTypeHalf2 = 17,
  WMTDataTypeHalf3 = 18,
  WMTDataTypeHalf4 = 19,

  WMTDataTypeHalf2x2 = 20,
  WMTDataTypeHalf2x3 = 21,
  WMTDataTypeHalf2x4 = 22,

  WMTDataTypeHalf3x2 = 23,
  WMTDataTypeHalf3x3 = 24,
  WMTDataTypeHalf3x4 = 25,

  WMTDataTypeHalf4x2 = 26,
  WMTDataTypeHalf4x3 = 27,
  WMTDataTypeHalf4x4 = 28,

  WMTDataTypeInt = 29,
  WMTDataTypeInt2 = 30,
  WMTDataTypeInt3 = 31,
  WMTDataTypeInt4 = 32,

  WMTDataTypeUInt = 33,
  WMTDataTypeUInt2 = 34,
  WMTDataTypeUInt3 = 35,
  WMTDataTypeUInt4 = 36,

  WMTDataTypeShort = 37,
  WMTDataTypeShort2 = 38,
  WMTDataTypeShort3 = 39,
  WMTDataTypeShort4 = 40,

  WMTDataTypeUShort = 41,
  WMTDataTypeUShort2 = 42,
  WMTDataTypeUShort3 = 43,
  WMTDataTypeUShort4 = 44,

  WMTDataTypeChar = 45,
  WMTDataTypeChar2 = 46,
  WMTDataTypeChar3 = 47,
  WMTDataTypeChar4 = 48,

  WMTDataTypeUChar = 49,
  WMTDataTypeUChar2 = 50,
  WMTDataTypeUChar3 = 51,
  WMTDataTypeUChar4 = 52,

  WMTDataTypeBool = 53,
  WMTDataTypeBool2 = 54,
  WMTDataTypeBool3 = 55,
  WMTDataTypeBool4 = 56,
};

struct WMTFunctionConstant {
  struct WMTConstMemoryPointer data;
  enum WMTDataType type;
  uint16_t index;
  uint32_t reserved;
};

WINEMETAL_API obj_handle_t MTLLibrary_newFunctionWithConstants(
    obj_handle_t library, const char *name, const struct WMTFunctionConstant *constants, uint32_t num_constants,
    obj_handle_t *err_out
);

struct WMTHDRMetadata {
  uint16_t red_primary[2];
  uint16_t green_primary[2];
  uint16_t blue_primary[2];
  uint16_t white_point[2];
  uint32_t max_mastering_luminance;
  uint32_t min_mastering_luminance;
  uint16_t max_content_light_level;
  uint16_t max_frame_average_light_level;
};

WINEMETAL_API bool
WMTQueryDisplaySetting(uint32_t display_id, enum WMTColorSpace *colorspace, struct WMTHDRMetadata *metadata);

WINEMETAL_API void
WMTUpdateDisplaySetting(uint32_t display_id, enum WMTColorSpace colorspace, const struct WMTHDRMetadata *metadata);

WINEMETAL_API void WMTQueryDisplaySettingForLayer(
    obj_handle_t layer, uint64_t *version, enum WMTColorSpace *colorspace, struct WMTHDRMetadata *metadata,
    struct WMTEDRValue *edr_value
);

WINEMETAL_API void MTLCommandBuffer_encodeWaitForEvent(obj_handle_t cmdbuf, obj_handle_t event, uint64_t value);

WINEMETAL_API void MTLSharedEvent_signalValue(obj_handle_t event, uint64_t value);

WINEMETAL_API void MTLSharedEvent_setWin32EventAtValue(obj_handle_t event, void *nt_event_handle, uint64_t at_value);

WINEMETAL_API obj_handle_t MTLDevice_newFence(obj_handle_t device);

WINEMETAL_API obj_handle_t MTLDevice_newEvent(obj_handle_t device);

#endif