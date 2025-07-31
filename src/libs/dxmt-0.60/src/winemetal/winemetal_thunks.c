#ifndef DXMT_NATIVE
#define WINEMETAL_API __declspec(dllexport)
#else
#define WINEMETAL_API
#endif

#include "winemetal_thunks.h"
#include <wineunixlib.h>
#include "assert.h"

#ifdef NDEBUG
#define UNIX_CALL(code, params) WINE_UNIX_CALL(code, params)
#else
#define UNIX_CALL(code, params)                                                                                        \
  {                                                                                                                    \
    NTSTATUS status = WINE_UNIX_CALL(code, params);                                                                    \
    assert(!status && "unix call failed");                                                                             \
  }
#endif

#define PtrToUInt64(v) ((uint64_t)(uintptr_t)(v))

WINEMETAL_API void
NSObject_retain(obj_handle_t obj) {
  UNIX_CALL(0, &obj);
}

WINEMETAL_API void
NSObject_release(obj_handle_t obj) {
  UNIX_CALL(1, &obj);
}

WINEMETAL_API obj_handle_t
NSArray_object(obj_handle_t array, uint64_t index) {
  struct unixcall_generic_obj_uint64_obj_ret params;
  params.handle = array;
  params.arg = index;
  params.ret = 0;
  UNIX_CALL(2, &params);
  return params.ret;
}

WINEMETAL_API uint64_t
NSArray_count(obj_handle_t array) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = array;
  params.ret = 0;
  UNIX_CALL(3, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
WMTCopyAllDevices() {
  struct unixcall_generic_obj_ret params;
  params.ret = 0;
  UNIX_CALL(4, &params);
  return params.ret;
}

WINEMETAL_API uint64_t
MTLDevice_recommendedMaxWorkingSetSize(obj_handle_t device) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(5, &params);
  return params.ret;
}

WINEMETAL_API uint64_t
MTLDevice_currentAllocatedSize(obj_handle_t device) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(6, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_name(obj_handle_t device) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(7, &params);
  return params.ret;
}

WINEMETAL_API uint32_t
NSString_getCString(obj_handle_t str, char *buffer, uint64_t maxLength, enum WMTStringEncoding encoding) {
  struct unixcall_nsstring_getcstring params;
  params.str = str;
  params.buffer_ptr = PtrToUInt64(buffer);
  params.max_length = maxLength;
  params.encoding = encoding;
  UNIX_CALL(8, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newCommandQueue(obj_handle_t device, uint64_t maxCommandBufferCount) {
  struct unixcall_generic_obj_uint64_obj_ret params;
  params.handle = device;
  params.arg = maxCommandBufferCount;
  params.ret = 0;
  UNIX_CALL(9, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
NSAutoreleasePool_alloc_init() {
  struct unixcall_generic_obj_ret params;
  params.ret = 0;
  UNIX_CALL(10, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLCommandQueue_commandBuffer(obj_handle_t queue) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = queue;
  params.ret = 0;
  UNIX_CALL(11, &params);
  return params.ret;
}

WINEMETAL_API void
MTLCommandBuffer_commit(obj_handle_t cmdbuf) {
  struct unixcall_generic_obj_noret params;
  params.handle = cmdbuf;
  UNIX_CALL(12, &params);
  return;
}

WINEMETAL_API void
MTLCommandBuffer_waitUntilCompleted(obj_handle_t cmdbuf) {
  struct unixcall_generic_obj_noret params;
  params.handle = cmdbuf;
  UNIX_CALL(13, &params);
  return;
}

WINEMETAL_API enum WMTCommandBufferStatus
MTLCommandBuffer_status(obj_handle_t cmdbuf) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = cmdbuf;
  params.ret = 0;
  UNIX_CALL(14, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newSharedEvent(obj_handle_t device) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(15, &params);
  return params.ret;
}

WINEMETAL_API uint64_t
MTLSharedEvent_signaledValue(obj_handle_t event) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = event;
  params.ret = 0;
  UNIX_CALL(16, &params);
  return params.ret;
}

WINEMETAL_API void
MTLCommandBuffer_encodeSignalEvent(obj_handle_t cmdbuf, obj_handle_t event, uint64_t value) {
  struct unixcall_generic_obj_obj_uint64_noret params;
  params.handle = cmdbuf;
  params.arg0 = event;
  params.arg1 = value;
  UNIX_CALL(17, &params);
  return;
}

WINEMETAL_API obj_handle_t
MTLDevice_newBuffer(obj_handle_t device, struct WMTBufferInfo *info) {
  struct unixcall_mtldevice_newbuffer params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  UNIX_CALL(18, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newSamplerState(obj_handle_t device, struct WMTSamplerInfo *info) {
  struct unixcall_mtldevice_newsamplerstate params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  UNIX_CALL(19, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newDepthStencilState(obj_handle_t device, const struct WMTDepthStencilInfo *info) {
  struct unixcall_mtldevice_newdepthstencilstate params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  UNIX_CALL(20, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newTexture(obj_handle_t device, struct WMTTextureInfo *info) {
  struct unixcall_mtldevice_newtexture params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  UNIX_CALL(21, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLBuffer_newTexture(obj_handle_t buffer, struct WMTTextureInfo *info, uint64_t offset, uint64_t bytes_per_row) {
  struct unixcall_mtlbuffer_newtexture params;
  params.buffer = buffer;
  WMT_MEMPTR_SET(params.info, info);
  params.offset = offset;
  params.bytes_per_row = bytes_per_row;
  UNIX_CALL(22, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLTexture_newTextureView(
    obj_handle_t texture, enum WMTPixelFormat format, enum WMTTextureType texture_type, uint16_t level_start,
    uint16_t level_count, uint16_t slice_start, uint16_t slice_count, struct WMTTextureSwizzleChannels swizzle,
    uint64_t *out_gpu_resource_id
) {
  struct unixcall_mtltexture_newtextureview params;
  params.texture = texture;
  params.format = format;
  params.texture_type = texture_type;
  params.level_start = level_start;
  params.level_count = level_count;
  params.slice_start = slice_start;
  params.slice_count = slice_count;
  params.swizzle = swizzle;
  UNIX_CALL(23, &params);
  *out_gpu_resource_id = params.gpu_resource_id;
  return params.ret;
}

WINEMETAL_API uint64_t
MTLDevice_minimumLinearTextureAlignmentForPixelFormat(obj_handle_t device, enum WMTPixelFormat format) {
  struct unixcall_generic_obj_uint64_uint64_ret params;
  params.handle = device;
  params.arg = (uint64_t)format;
  params.ret = 0;
  UNIX_CALL(24, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newLibrary(
    obj_handle_t device, struct WMTMemoryPointer bytecode, uint64_t bytecode_length, obj_handle_t *err_out
) {
  struct unixcall_mtldevice_newlibrary params;
  params.device = device;
  params.bytecode = bytecode;
  params.bytecode_length = bytecode_length;
  params.ret_error = 0;
  params.ret_library = 0;
  UNIX_CALL(25, &params);
  if (err_out)
    *err_out = params.ret_error;
  return params.ret_library;
}

WINEMETAL_API obj_handle_t
MTLLibrary_newFunction(obj_handle_t library, const char *name) {
  struct unixcall_generic_obj_uint64_obj_ret params;
  params.handle = library;
  params.arg = PtrToUInt64(name);
  params.ret = 0;
  UNIX_CALL(26, &params);
  return params.ret;
}

WINEMETAL_API uint64_t
NSString_lengthOfBytesUsingEncoding(obj_handle_t str, enum WMTStringEncoding encoding) {
  struct unixcall_generic_obj_uint64_uint64_ret params;
  params.handle = str;
  params.arg = (uint64_t)(encoding);
  params.ret = 0;
  UNIX_CALL(27, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
NSObject_description(obj_handle_t nserror) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = nserror;
  UNIX_CALL(28, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newComputePipelineState(obj_handle_t device, obj_handle_t function, obj_handle_t *err_out) {
  struct unixcall_mtldevice_newcomputepso params;
  params.device = device;
  params.function = function;
  params.ret_error = 0;
  params.ret_pso = 0;
  UNIX_CALL(29, &params);
  if (err_out)
    *err_out = params.ret_error;
  return params.ret_pso;
}

WINEMETAL_API obj_handle_t
MTLCommandBuffer_blitCommandEncoder(obj_handle_t cmdbuf) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = cmdbuf;
  params.ret = 0;
  UNIX_CALL(30, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLCommandBuffer_computeCommandEncoder(obj_handle_t cmdbuf, bool concurrent) {
  struct unixcall_generic_obj_uint64_obj_ret params;
  params.handle = cmdbuf;
  params.arg = concurrent;
  params.ret = 0;
  UNIX_CALL(31, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLCommandBuffer_renderCommandEncoder(obj_handle_t cmdbuf, struct WMTRenderPassInfo *info) {
  struct unixcall_generic_obj_uint64_obj_ret params;
  params.handle = cmdbuf;
  params.arg = PtrToUInt64(info);
  params.ret = 0;
  UNIX_CALL(32, &params);
  return params.ret;
}

WINEMETAL_API void
MTLCommandEncoder_endEncoding(obj_handle_t encoder) {
  struct unixcall_generic_obj_noret params;
  params.handle = encoder;
  UNIX_CALL(33, &params);
  return;
}

WINEMETAL_API obj_handle_t
MTLDevice_newRenderPipelineState(obj_handle_t device, const struct WMTRenderPipelineInfo *info, obj_handle_t *err_out) {
  struct unixcall_mtldevice_newrenderpso params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  params.ret_error = 0;
  params.ret_pso = 0;
  UNIX_CALL(34, &params);
  if (err_out)
    *err_out = params.ret_error;
  return params.ret_pso;
}

WINEMETAL_API obj_handle_t
MTLDevice_newMeshRenderPipelineState(
    obj_handle_t device, const struct WMTMeshRenderPipelineInfo *info, obj_handle_t *err_out
) {
  struct unixcall_mtldevice_newmeshrenderpso params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  params.ret_error = 0;
  params.ret_pso = 0;
  UNIX_CALL(35, &params);
  if (err_out)
    *err_out = params.ret_error;
  return params.ret_pso;
}

WINEMETAL_API void
MTLBlitCommandEncoder_encodeCommands(obj_handle_t encoder, const struct wmtcmd_base *cmd_head) {
  struct unixcall_generic_obj_cmd_noret params;
  params.encoder = encoder;
  WMT_MEMPTR_SET(params.cmd_head, cmd_head);
  UNIX_CALL(36, &params);
}

WINEMETAL_API void
MTLComputeCommandEncoder_encodeCommands(obj_handle_t encoder, const struct wmtcmd_base *cmd_head) {
  struct unixcall_generic_obj_cmd_noret params;
  params.encoder = encoder;
  WMT_MEMPTR_SET(params.cmd_head, cmd_head);
  UNIX_CALL(37, &params);
}

WINEMETAL_API void
MTLRenderCommandEncoder_encodeCommands(obj_handle_t encoder, const struct wmtcmd_base *cmd_head) {
  struct unixcall_generic_obj_cmd_noret params;
  params.encoder = encoder;
  WMT_MEMPTR_SET(params.cmd_head, cmd_head);
  UNIX_CALL(38, &params);
}

WINEMETAL_API enum WMTPixelFormat
MTLTexture_pixelFormat(obj_handle_t texture) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = texture;
  params.ret = 0;
  UNIX_CALL(39, &params);
  return (enum WMTPixelFormat)params.ret;
}

WINEMETAL_API uint64_t
MTLTexture_width(obj_handle_t texture) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = texture;
  params.ret = 0;
  UNIX_CALL(40, &params);
  return params.ret;
}
WINEMETAL_API uint64_t
MTLTexture_height(obj_handle_t texture) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = texture;
  params.ret = 0;
  UNIX_CALL(41, &params);
  return params.ret;
}
WINEMETAL_API uint64_t
MTLTexture_depth(obj_handle_t texture) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = texture;
  params.ret = 0;
  UNIX_CALL(42, &params);
  return params.ret;
}
WINEMETAL_API uint64_t
MTLTexture_arrayLength(obj_handle_t texture) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = texture;
  params.ret = 0;
  UNIX_CALL(43, &params);
  return params.ret;
}
WINEMETAL_API uint64_t
MTLTexture_mipmapLevelCount(obj_handle_t texture) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = texture;
  params.ret = 0;
  UNIX_CALL(44, &params);
  return params.ret;
}
WINEMETAL_API void
MTLTexture_replaceRegion(
    obj_handle_t texture, struct WMTOrigin origin, struct WMTSize size, uint64_t level, uint64_t slice,
    struct WMTMemoryPointer data, uint64_t bytes_per_row, uint64_t bytes_per_image
) {
  struct unixcall_mtltexture_replaceregion params;
  params.texture = texture;
  params.origin = origin;
  params.size = size;
  params.level = level;
  params.slice = slice;
  params.data = data;
  params.bytes_per_row = bytes_per_row;
  params.bytes_per_image = bytes_per_image;
  UNIX_CALL(45, &params);
}

WINEMETAL_API void
MTLBuffer_didModifyRange(obj_handle_t buffer, uint64_t start, uint64_t length) {
  struct unixcall_generic_obj_uint64_uint64_ret params;
  params.handle = buffer;
  params.arg = start;
  params.ret = length;
  UNIX_CALL(46, &params);
}

WINEMETAL_API void
MTLCommandBuffer_presentDrawable(obj_handle_t cmdbuf, obj_handle_t drawable) {
  struct unixcall_generic_obj_obj_noret params;
  params.handle = cmdbuf;
  params.arg = drawable;
  UNIX_CALL(47, &params);
};

WINEMETAL_API void
MTLCommandBuffer_presentDrawableAfterMinimumDuration(obj_handle_t cmdbuf, obj_handle_t drawable, double after) {
  struct unixcall_generic_obj_obj_double_noret params;
  params.handle = cmdbuf;
  params.arg0 = drawable;
  params.arg1 = after;
  UNIX_CALL(48, &params);
};

WINEMETAL_API bool
MTLDevice_supportsFamily(obj_handle_t device, enum WMTGPUFamily gpu_family) {
  struct unixcall_generic_obj_uint64_uint64_ret params;
  params.handle = device;
  params.arg = gpu_family;
  params.ret = 0;
  UNIX_CALL(49, &params);
  return params.ret;
}

WINEMETAL_API bool
MTLDevice_supportsBCTextureCompression(obj_handle_t device) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(50, &params);
  return params.ret;
};

WINEMETAL_API bool
MTLDevice_supportsTextureSampleCount(obj_handle_t device, uint8_t sample_count) {
  struct unixcall_generic_obj_uint64_uint64_ret params;
  params.handle = device;
  params.arg = sample_count;
  params.ret = 0;
  UNIX_CALL(51, &params);
  return params.ret;
}

WINEMETAL_API bool
MTLDevice_hasUnifiedMemory(obj_handle_t device) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(52, &params);
  return params.ret;
};

WINEMETAL_API obj_handle_t
MTLCaptureManager_sharedCaptureManager() {
  struct unixcall_generic_obj_ret params;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(53, &params);
  return params.ret;
}

WINEMETAL_API bool
MTLCaptureManager_startCapture(obj_handle_t mgr, struct WMTCaptureInfo *info) {
  struct unixcall_mtlcapturemanager_startcapture params;
  params.capture_manager = mgr;
  WMT_MEMPTR_SET(params.info, info);
  UNIX_CALL(54, &params);
  return params.ret;
}

WINEMETAL_API void
MTLCaptureManager_stopCapture(obj_handle_t mgr) {
  struct unixcall_generic_obj_noret params;
  params.handle = mgr;
  UNIX_CALL(55, &params);
}

WINEMETAL_API obj_handle_t
MTLDevice_newTemporalScaler(obj_handle_t device, const struct WMTFXTemporalScalerInfo *info) {
  struct unixcall_mtldevice_newfxtemporalscaler params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  UNIX_CALL(56, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newSpatialScaler(obj_handle_t device, const struct WMTFXSpatialScalerInfo *info) {
  struct unixcall_mtldevice_newfxspatialscaler params;
  params.device = device;
  WMT_MEMPTR_SET(params.info, info);
  UNIX_CALL(57, &params);
  return params.ret;
}

WINEMETAL_API void
MTLCommandBuffer_encodeTemporalScale(
    obj_handle_t cmdbuf, obj_handle_t scaler, obj_handle_t color, obj_handle_t output, obj_handle_t depth,
    obj_handle_t motion, obj_handle_t exposure, obj_handle_t fence, const struct WMTFXTemporalScalerProps *props
) {
  struct unixcall_mtlcommandbuffer_temporal_scale params;
  params.cmdbuf = cmdbuf;
  params.scaler = scaler;
  params.color = color;
  params.output = output;
  params.depth = depth;
  params.motion = motion;
  params.exposure = exposure;
  params.fence = fence;
  WMT_MEMPTR_SET(params.props, props);
  UNIX_CALL(58, &params);
}

WINEMETAL_API void
MTLCommandBuffer_encodeSpatialScale(
    obj_handle_t cmdbuf, obj_handle_t scaler, obj_handle_t color, obj_handle_t output, obj_handle_t fence
) {
  struct unixcall_mtlcommandbuffer_spatial_scale params;
  params.cmdbuf = cmdbuf;
  params.scaler = scaler;
  params.color = color;
  params.output = output;
  params.fence = fence;
  UNIX_CALL(59, &params);
}

WINEMETAL_API obj_handle_t
NSString_string(const char *data, enum WMTStringEncoding encoding) {
  struct unixcall_nsstring_string params;
  WMT_MEMPTR_SET(params.buffer_ptr, data);
  params.encoding = encoding;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(60, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
NSString_alloc_init(const char *data, enum WMTStringEncoding encoding) {
  struct unixcall_nsstring_string params;
  WMT_MEMPTR_SET(params.buffer_ptr, data);
  params.encoding = encoding;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(61, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
DeveloperHUDProperties_instance() {
  struct unixcall_generic_obj_ret params;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(62, &params);
  return params.ret;
}

WINEMETAL_API bool
DeveloperHUDProperties_addLabel(obj_handle_t obj, obj_handle_t label, obj_handle_t after) {
  struct unixcall_generic_obj_obj_obj_uint64_ret params;
  params.handle = obj;
  params.arg0 = label;
  params.arg1 = after;
  params.ret = 0;
  UNIX_CALL(63, &params);
  return params.ret;
}

WINEMETAL_API void
DeveloperHUDProperties_updateLabel(obj_handle_t obj, obj_handle_t label, obj_handle_t value) {
  struct unixcall_generic_obj_obj_obj_noret params;
  params.handle = obj;
  params.arg0 = label;
  params.arg1 = value;
  UNIX_CALL(64, &params);
}

WINEMETAL_API void
DeveloperHUDProperties_remove(obj_handle_t obj, obj_handle_t label) {
  struct unixcall_generic_obj_obj_noret params;
  params.handle = obj;
  params.arg = label;
  UNIX_CALL(65, &params);
}

WINEMETAL_API obj_handle_t
MetalDrawable_texture(obj_handle_t drawable) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = drawable;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(66, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MetalLayer_nextDrawable(obj_handle_t layer) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = layer;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(67, &params);
  return params.ret;
}

WINEMETAL_API bool
MTLDevice_supportsFXSpatialScaler(obj_handle_t device) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(68, &params);
  return params.ret;
};

WINEMETAL_API bool
MTLDevice_supportsFXTemporalScaler(obj_handle_t device) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(69, &params);
  return params.ret;
};

WINEMETAL_API void
MetalLayer_setProps(obj_handle_t layer, const struct WMTLayerProps *props) {
  struct unixcall_generic_obj_constptr_noret params;
  params.handle = layer;
  WMT_MEMPTR_SET(params.arg, props);
  UNIX_CALL(70, &params);
}

WINEMETAL_API void
MetalLayer_getProps(obj_handle_t layer, struct WMTLayerProps *props) {
  struct unixcall_generic_obj_ptr_noret params;
  params.handle = layer;
  WMT_MEMPTR_SET(params.arg, props);
  UNIX_CALL(71, &params);
}

WINEMETAL_API obj_handle_t
CreateMetalViewFromHWND(intptr_t hwnd, obj_handle_t device, obj_handle_t *layer) {
  struct unixcall_create_metal_view_from_hwnd params;
  params.hwnd = (uint64_t)(uintptr_t)hwnd;
  params.device = device;
  params.ret_layer = NULL_OBJECT_HANDLE;
  params.ret_view = NULL_OBJECT_HANDLE;
  UNIX_CALL(72, &params);
  *layer = params.ret_layer;
  return params.ret_view;
}

WINEMETAL_API void
ReleaseMetalView(obj_handle_t view) {
  struct unixcall_generic_obj_noret params;
  params.handle = view;
  UNIX_CALL(73, &params);
}

WINEMETAL_API void
MTLCommandEncoder_setLabel(obj_handle_t encoder, obj_handle_t label) {
  struct unixcall_generic_obj_obj_noret params;
  params.handle = encoder;
  params.arg = label;
  UNIX_CALL(86, &params);
}

WINEMETAL_API void
MTLDevice_setShouldMaximizeConcurrentCompilation(obj_handle_t device, bool value) {
  struct unixcall_generic_obj_uint64_noret params;
  params.handle = device;
  params.arg = value;
  UNIX_CALL(87, &params);
}

WINEMETAL_API obj_handle_t
MTLCommandBuffer_error(obj_handle_t cmdbuf) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = cmdbuf;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(89, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLCommandBuffer_logs(obj_handle_t cmdbuf) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = cmdbuf;
  params.ret = NULL_OBJECT_HANDLE;
  UNIX_CALL(90, &params);
  return params.ret;
}

WINEMETAL_API uint64_t
MTLLogContainer_enumerate(obj_handle_t logs, uint64_t start, uint64_t buffer_size, obj_handle_t *buffer) {
  struct unixcall_enumerate params;
  params.enumeratable = logs;
  params.start = start;
  params.buffer_size = buffer_size;
  WMT_MEMPTR_SET(params.buffer, buffer);
  params.ret_read = 0;
  UNIX_CALL(91, &params);
  return params.ret_read;
}

WINEMETAL_API bool
CGColorSpace_checkColorSpaceSupported(enum WMTColorSpace colorspace) {
  struct unixcall_generic_obj_uint64_ret params;
  params.handle = colorspace;
  params.ret = 0;
  UNIX_CALL(92, &params);
  return params.ret;
}

WINEMETAL_API bool
MetalLayer_setColorSpace(obj_handle_t layer, enum WMTColorSpace colorpsace) {
  struct unixcall_generic_obj_uint64_uint64_ret params;
  params.handle = layer;
  params.arg = colorpsace;
  params.ret = 0;
  UNIX_CALL(93, &params);
  return params.ret;
}

WINEMETAL_API uint32_t
WMTGetPrimaryDisplayId() {
  struct unixcall_generic_obj_ret params;
  params.ret = 0;
  UNIX_CALL(94, &params);
  return params.ret;
}

WINEMETAL_API uint32_t
WMTGetSecondaryDisplayId() {
  struct unixcall_generic_obj_ret params;
  params.ret = 0;
  UNIX_CALL(95, &params);
  return params.ret;
}

WINEMETAL_API void
WMTGetDisplayDescription(uint32_t display_id, struct WMTDisplayDescription *desc) {
  struct unixcall_generic_obj_ptr_noret params;
  params.handle = display_id;
  WMT_MEMPTR_SET(params.arg, desc);
  UNIX_CALL(96, &params);
}

WINEMETAL_API void
MetalLayer_getEDRValue(obj_handle_t layer, struct WMTEDRValue *value) {
  struct unixcall_generic_obj_constptr_noret params;
  params.handle = layer;
  WMT_MEMPTR_SET(params.arg, value);
  UNIX_CALL(97, &params);
}

WINEMETAL_API obj_handle_t
MTLLibrary_newFunctionWithConstants(
    obj_handle_t library, const char *name, const struct WMTFunctionConstant *constants, uint32_t num_constants,
    obj_handle_t *err_out
) {
  struct unixcall_mtllibrary_newfunction_with_constants params;
  params.library = library;
  WMT_MEMPTR_SET(params.name, name);
  WMT_MEMPTR_SET(params.constants, constants);
  params.num_constants = num_constants;
  params.ret = 0;
  params.ret_error = 0;
  UNIX_CALL(98, &params);
  if (err_out)
    *err_out = params.ret_error;
  return params.ret;
}

WINEMETAL_API bool
WMTQueryDisplaySetting(uint32_t display_id, enum WMTColorSpace *colorspace, struct WMTHDRMetadata *metadata) {
  struct unixcall_query_display_setting params;
  params.display_id = display_id;
  WMT_MEMPTR_SET(params.hdr_metadata, metadata);
  UNIX_CALL(99, &params);
  *colorspace = params.colorspace;
  return params.ret;
}

WINEMETAL_API void
WMTUpdateDisplaySetting(uint32_t display_id, enum WMTColorSpace colorspace, const struct WMTHDRMetadata *metadata) {
  struct unixcall_update_display_setting params;
  params.display_id = display_id;
  params.colorspace = colorspace;
  WMT_MEMPTR_SET(params.hdr_metadata, metadata);
  UNIX_CALL(100, &params);
}

WINEMETAL_API void
WMTQueryDisplaySettingForLayer(
    obj_handle_t layer, uint64_t *version, enum WMTColorSpace *colorspace, struct WMTHDRMetadata *metadata,
    struct WMTEDRValue *edr_value
) {
  struct unixcall_query_display_setting_for_layer params;
  params.layer = layer;
  WMT_MEMPTR_SET(params.hdr_metadata, metadata);
  UNIX_CALL(101, &params);
  *version = params.version;
  *colorspace = params.colorspace;
  *edr_value = params.edr_value;
}

WINEMETAL_API void
MTLCommandBuffer_encodeWaitForEvent(obj_handle_t cmdbuf, obj_handle_t event, uint64_t value) {
  struct unixcall_generic_obj_obj_uint64_noret params;
  params.handle = cmdbuf;
  params.arg0 = event;
  params.arg1 = value;
  UNIX_CALL(102, &params);
  return;
}

WINEMETAL_API void
MTLSharedEvent_signalValue(obj_handle_t event, uint64_t value) {
  struct unixcall_generic_obj_uint64_noret params;
  params.handle = event;
  params.arg = value;
  UNIX_CALL(103, &params);
}

WINEMETAL_API void
MTLSharedEvent_setWin32EventAtValue(obj_handle_t event, void *nt_event_handle, uint64_t at_value) {
  struct unixcall_generic_obj_obj_uint64_noret params;
  params.handle = event;
  params.arg0 = (obj_handle_t)PtrToUInt64(nt_event_handle);
  params.arg1 = at_value;
  UNIX_CALL(104, &params);
}

WINEMETAL_API obj_handle_t
MTLDevice_newFence(obj_handle_t device) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(105, &params);
  return params.ret;
}

WINEMETAL_API obj_handle_t
MTLDevice_newEvent(obj_handle_t device) {
  struct unixcall_generic_obj_obj_ret params;
  params.handle = device;
  params.ret = 0;
  UNIX_CALL(106, &params);
  return params.ret;
}