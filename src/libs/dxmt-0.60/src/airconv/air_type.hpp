#pragma once

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include <cassert>
#include <map>

namespace dxmt::air {

class AirType {

public:
  llvm::Type *getTypeByMetalTypeName(const std::string &name);
  AirType(llvm::LLVMContext &context);

  llvm::Type *_bool;
  llvm::Type *_byte;
  llvm::Type *_char2;
  llvm::Type *_char3;
  llvm::Type *_char4;
  llvm::Type *_short;
  llvm::Type *_short2;
  llvm::Type *_short3;
  llvm::Type *_short4;
  llvm::Type *_float;
  llvm::Type *_float2;
  llvm::Type *_float3;
  llvm::Type *_float4;
  llvm::Type *_int;
  llvm::Type *_int2;
  llvm::Type *_int3;
  llvm::Type *_int4;
  llvm::Type *_int8;
  llvm::Type *_half;
  llvm::Type *_half2;
  llvm::Type *_half3;
  llvm::Type *_half4;
  llvm::Type *_long;
  llvm::Type *_long4;

  llvm::Type *_dxmt_vertex_buffer_entry;
  llvm::Type *_dxmt_draw_arguments;
  llvm::Type *_dxmt_draw_indexed_arguments;

  llvm::Type *_sampler;

  llvm::Type *_mesh_grid_properties;

  llvm::Type *_texture1d;
  llvm::Type *_texture1d_array;
  llvm::Type *_texture2d;
  llvm::Type *_texture2d_array;
  llvm::Type *_texture2d_ms;
  llvm::Type *_texture2d_ms_array;
  llvm::Type *_texture3d;
  llvm::Type *_texture_cube;
  llvm::Type *_texture_cube_array;
  llvm::Type *_texture_buffer;

  llvm::Type *_depth2d;
  llvm::Type *_depth2d_array;
  llvm::Type *_depth2d_ms;
  llvm::Type *_depth2d_ms_array;
  llvm::Type *_depth_cube;
  llvm::Type *_depth_cube_array;

  llvm::Type* _ptr_constant;
  llvm::Type* _ptr_device;
  llvm::Type* _ptr_threadgroup;
  llvm::Type *_interpolant;

  llvm::Type *_mesh;

  std::map<std::string, llvm::Type *> typeContext;

  llvm::Type* to_vec4_type(llvm::Type* scalar) {
    if(scalar == _int) {
      return _int4;
    }
    if(scalar == _float) {
      return _float4;
    }
    assert(0 && "input is not a scalar or not implemented.");
  };

  llvm::LLVMContext &context;
};
} // namespace dxmt