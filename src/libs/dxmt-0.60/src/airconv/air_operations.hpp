#pragma once

#include "air_signature.hpp"
#include "air_type.hpp"
#include "monad.hpp"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Error.h"

namespace dxmt::air {

using pvalue = llvm::Value *;
using epvalue = llvm::Expected<pvalue>;

struct AIRBuilderContext;

using AIRBuilderResult = ReaderIO<AIRBuilderContext, pvalue>;

struct AIRBuilderContext {
  llvm::LLVMContext &llvm;
  llvm::Module &module;
  llvm::IRBuilder<> &builder;
  AirType &types;
};

template <typename S> AIRBuilderResult make_op(S &&fs) {
  return AIRBuilderResult(std::forward<S>(fs));
}

enum class TextureInfoType {
  width,
  height,
  depth,
  array_length,
  num_mip_levels,
  num_samples
};

AIRBuilderResult call_integer_unary_op(std::string op, pvalue a);
AIRBuilderResult call_float_unary_op(std::string op, pvalue a);
AIRBuilderResult
call_integer_binop(std::string op, pvalue a, pvalue b, bool is_signed = false);
AIRBuilderResult call_float_binop(std::string op, pvalue a, pvalue b, bool force_precise = false);

AIRBuilderResult call_dot_product(uint32_t dimension, pvalue a, pvalue b);
AIRBuilderResult call_float_mad(pvalue a, pvalue b, pvalue c);
AIRBuilderResult call_count_zero(bool trail, pvalue a);

AIRBuilderResult call_sample(
  MSLTexture texture_type, pvalue handle, pvalue sampler, pvalue coord,
  pvalue array_index, pvalue offset = nullptr, pvalue bias = nullptr,
  pvalue min_lod_clamp = nullptr, pvalue lod_level = nullptr
);

AIRBuilderResult call_sample_grad(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue dpdx, pvalue dpdy, pvalue minlod = nullptr,
  pvalue offset = nullptr
);

AIRBuilderResult call_sample_compare(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue reference = nullptr, pvalue offset = nullptr,
  pvalue bias = nullptr, pvalue min_lod_clamp = nullptr,
  pvalue lod_level = nullptr
);

AIRBuilderResult call_gather(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue offset = nullptr, pvalue component = nullptr
);

AIRBuilderResult call_gather_compare(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue reference = nullptr, pvalue offset = nullptr
);

AIRBuilderResult call_read(
  MSLTexture texture_type, pvalue handle, pvalue address,
  pvalue offset = nullptr, pvalue cube_face = nullptr,
  pvalue array_index = nullptr, pvalue sample_index = nullptr,
  pvalue lod = nullptr
);

AIRBuilderResult call_write(
  MSLTexture texture_type, pvalue handle, pvalue address, pvalue cube_face,
  pvalue array_index, pvalue vec4, pvalue lod = nullptr
);

AIRBuilderResult call_calc_lod(
  MSLTexture texture_type, pvalue handle, pvalue sampler, pvalue coord,
  bool is_unclamped
);

AIRBuilderResult call_get_texture_info(
  air::MSLTexture texture_type, pvalue handle, TextureInfoType type, pvalue lod
);

AIRBuilderResult call_get_num_samples();

AIRBuilderResult call_texture_atomic_fetch_explicit(
  air::MSLTexture texture_type, pvalue handle, std::string op, bool is_signed,
  pvalue address, pvalue array_index, pvalue vec4
);

AIRBuilderResult call_texture_atomic_exchange(
  air::MSLTexture texture_type, pvalue handle, pvalue address,
  pvalue array_index, pvalue vec4
);

AIRBuilderResult call_texture_atomic_compare_exchange(
  air::MSLTexture texture_type, pvalue handle, pvalue address,
  pvalue array_index, pvalue compared, pvalue operand
);

// TODO: not good, expose too much detail
AIRBuilderResult
call_convert(pvalue src, llvm::Type *dst_scaler_type, air::Sign sign);

AIRBuilderResult call_atomic_fetch_explicit(
  pvalue pointer, pvalue operand, std::string op, bool is_signed = false,
  bool device = false
);

AIRBuilderResult call_atomic_exchange_explicit(
  pvalue pointer, pvalue operand, bool device = false
);

AIRBuilderResult call_atomic_cmp_exchange(
  pvalue pointer, pvalue compared, pvalue operand, pvalue tmp_mem,
  bool device = false
);

AIRBuilderResult call_derivative(pvalue fvec4, bool dfdy);

AIRBuilderResult
call_set_mesh_properties(pvalue mesh_grid_props, pvalue grid_size);

AIRBuilderResult call_interpolate_at_center(pvalue interpolant, bool perspective);
AIRBuilderResult call_interpolate_at_centroid(pvalue interpolant, bool perspective);
AIRBuilderResult call_interpolate_at_offset(pvalue interpolant, bool perspective, pvalue offset);
AIRBuilderResult call_interpolate_at_sample(pvalue interpolant, bool perspective, pvalue index);

AIRBuilderResult call_set_mesh_render_target_array_index(pvalue mesh, pvalue vid, pvalue render_target_array_index);
AIRBuilderResult call_set_mesh_viewport_array_index(pvalue mesh, pvalue vid, pvalue viewport_array_index);
AIRBuilderResult call_set_mesh_position(pvalue mesh, pvalue vid, pvalue position);
AIRBuilderResult call_set_mesh_clip_distance(pvalue mesh, uint32_t idx, pvalue vid, pvalue value);
AIRBuilderResult call_set_mesh_vertex_data(pvalue mesh, uint32_t idx, pvalue vid, pvalue value);
AIRBuilderResult call_set_mesh_primitive_data(pvalue mesh, uint32_t idx, pvalue pid, pvalue value);

AIRBuilderResult call_set_mesh_index(pvalue mesh, pvalue index, pvalue vertex);
AIRBuilderResult call_set_mesh_primitive_count(pvalue mesh, pvalue count);

enum class MTLAttributeFormat {
  Invalid = 0,
  UChar2 = 1,
  // UChar3 = 2,
  UChar4 = 3,
  Char2 = 4,
  // Char3 = 5,
  Char4 = 6,
  UChar2Normalized = 7,
  // UChar3Normalized = 8,
  UChar4Normalized = 9,
  Char2Normalized = 10,
  // Char3Normalized = 11,
  Char4Normalized = 12,
  UShort2 = 13,
  // UShort3 = 14,
  UShort4 = 15,
  Short2 = 16,
  // Short3 = 17,
  Short4 = 18,
  UShort2Normalized = 19,
  // UShort3Normalized = 20,
  UShort4Normalized = 21,
  Short2Normalized = 22,
  // Short3Normalized = 23,
  Short4Normalized = 24,
  Half2 = 25,
  // Half3 = 26,
  Half4 = 27,
  Float = 28,
  Float2 = 29,
  Float3 = 30,
  Float4 = 31,
  Int = 32,
  Int2 = 33,
  Int3 = 34,
  Int4 = 35,
  UInt = 36,
  UInt2 = 37,
  UInt3 = 38,
  UInt4 = 39,
  Int1010102Normalized = 40,
  UInt1010102Normalized = 41,
  UChar4Normalized_BGRA = 42,
  UChar = 45,
  Char = 46,
  UCharNormalized = 47,
  CharNormalized = 48,
  UShort = 49,
  Short = 50,
  UShortNormalized = 51,
  ShortNormalized = 52,
  Half = 53,
  FloatRG11B10 = 54,
  FloatRGB9E5 = 55,
};

AIRBuilderResult pull_vec4_from_addr(
  MTLAttributeFormat format, pvalue base_addr, pvalue byte_offset
);

inline auto pure(pvalue value) {
  return make_op([=](auto) { return value; });
}

inline auto saturate(bool sat) {
  return [sat](pvalue floaty) -> AIRBuilderResult {
    if (sat) {
      return call_float_unary_op("saturate", floaty);
    } else {
      return pure(floaty);
    }
  };
}
}; // namespace dxmt::air