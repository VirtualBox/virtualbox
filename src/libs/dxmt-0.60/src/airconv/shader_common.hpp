#pragma once

/*
 * This header file contains shader-related enumerations that are designed to be
 * implementation-independent in case we need these for other projects
 */

namespace dxmt::shader::common {

enum class ScalerDataType {
  Float,
  Uint,
  Int,
  /* Unsupported in Metal */
  Double,
};

enum class ResourceType {
  TextureBuffer,
  Texture1D,
  Texture1DArray,
  Texture2D,
  Texture2DArray,
  Texture2DMultisampled,
  Texture2DMultisampledArray,
  Texture3D,
  TextureCube,
  TextureCubeArray,
  NonApplicable,
};

/* It's often called System Generated Value in DirectX */
enum class InputAttribute {
  VertexId,
  InstanceId,

  /* For compute */

  /* uint? */
  ThreadId,
  /* uint3 */
  ThreadIdInGroup,
  /* uint? */
  ThreadGroupId,
  /* uint */
  ThreadIdInGroupFlatten,

  /* Fragment */
  PrimitiveId,
  CoverageMask,

  /* Hull */
  OutputControlPointId,
  ForkInstanceId,
  JoinInstanceId,

  /* Domain */
  Domain,

  /* Geometry */
  GSInstanceId,
};

} // namespace dxmt::shader::common