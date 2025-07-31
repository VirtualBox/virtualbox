#include "air_type.hpp"
#include "air_signature.hpp"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

using namespace llvm;

namespace dxmt::air {

llvm::StructType *
get_or_create_struct(llvm::LLVMContext &Ctx, llvm::StringRef Name) {
  using namespace llvm;
  StructType *ST = StructType::getTypeByName(Ctx, Name);
  if (ST)
    return ST;

  return StructType::create(Ctx, Name);
}

AirType::AirType(LLVMContext &context) : context(context) {

  _int = Type::getInt32Ty(context);
  _float = Type::getFloatTy(context);
  _bool = Type::getInt1Ty(context);
  _byte = Type::getInt8Ty(context);
  _long = Type::getInt64Ty(context);
  _long4 = FixedVectorType::get(_long, 4);

  _half = Type::getHalfTy(context);
  _half2 = FixedVectorType::get(_half, 2);
  _half3 = FixedVectorType::get(_half, 3);
  _half4 = FixedVectorType::get(_half, 4);

  _short = Type::getInt16Ty(context);

  _int4 = FixedVectorType::get(_int, 4);
  _int8 = FixedVectorType::get(_int, 8);
  _float4 = FixedVectorType::get(_float, 4);
  _short4 = FixedVectorType::get(_short, 4);
  _char4 = FixedVectorType::get(_byte, 4);

  _int3 = FixedVectorType::get(_int, 3);
  _short3 = FixedVectorType::get(_short, 3);
  _float3 = FixedVectorType::get(_float, 3);
  _char3 = FixedVectorType::get(_byte, 3);

  _int2 = FixedVectorType::get(_int, 2);
  _short2 = FixedVectorType::get(_short, 2);
  _float2 = FixedVectorType::get(_float, 2);
  _char2 = FixedVectorType::get(_byte, 2);

  _dxmt_vertex_buffer_entry = StructType::create(
    context,
    {
      _byte->getPointerTo((uint32_t)AddressSpace::device),
      _int, // stride
      _int, // length
    },
    "dxmt_vertex_buffer_entry"
  );

  _dxmt_draw_arguments = StructType::create(
    context,
    {
      _int, // vertex count
      _int, // instance count
      _int, // start vertex
      _int, // start instance
    },
    "dxmt_draw_arguments"
  );

  _dxmt_draw_indexed_arguments = StructType::create(
    context,
    {
      _int, // index count
      _int, // instance count
      _int, // start index
      _int, // base vertex
      _int, // base instance
    },
    "dxmt_draw_indexed_arguments"
  );

  auto tyOpaque = get_or_create_struct(context, "opaque");
  _ptr_device = PointerType::get(tyOpaque, 1);
  _ptr_constant = PointerType::get(tyOpaque, 2);
  _ptr_threadgroup = PointerType::get(tyOpaque, 3);

  _texture1d = get_or_create_struct(context, "struct._texture_1d_t");
  _texture1d_array =
    get_or_create_struct(context, "struct._texture_1d_array_t");
  _texture2d = get_or_create_struct(context, "struct._texture_2d_t");
  _texture2d_array =
    get_or_create_struct(context, "struct._texture_2d_array_t");
  _texture2d_ms = get_or_create_struct(context, "struct._texture_2d_ms_t");
  _texture2d_ms_array =
    get_or_create_struct(context, "struct._texture_2d_ms_array_t");
  _texture_cube = get_or_create_struct(context, "struct._texture_cube_t");
  _texture_cube_array =
    get_or_create_struct(context, "struct._texture_cube_array_t");
  _texture3d = get_or_create_struct(context, "struct._texture_3d_t");
  _texture_buffer =
    get_or_create_struct(context, "struct._texture_buffer_1d_t");

  _texture2d = get_or_create_struct(context, "struct._depth_2d_t");
  _texture2d_array = get_or_create_struct(context, "struct._depth_2d_array_t");
  _texture2d_ms = get_or_create_struct(context, "struct._depth_2d_ms_t");
  _texture2d_ms_array =
    get_or_create_struct(context, "struct._depth_2d_ms_array_t");
  _texture_cube = get_or_create_struct(context, "struct._depth_cube_t");
  _texture_cube_array =
    get_or_create_struct(context, "struct._depth_cube_array_t");

  _sampler = get_or_create_struct(context, "struct._sampler_t");
  _mesh_grid_properties = get_or_create_struct(context, "struct._mesh_grid_properties_t");
  _interpolant = get_or_create_struct(context, "struct._interpolant_t");

  _mesh = get_or_create_struct(context, "struct._mesh_t");

  typeContext = {
    {"bool", _bool},     {"int", _int},        {"uint", _int},
    {"short", _short},   {"float", _float},    {"int2", _int2},
    {"short2", _short2}, {"uint2", _int2},     {"ushort2", _short2},
    {"float2", _float2}, {"int3", _int3},      {"short3", _short3},
    {"uint3", _int3},    {"ushort3", _short3}, {"float3", _float3},
    {"int4", _int4},     {"uint4", _int4},     {"float4", _float4},
    {"short4", _short4}, {"ushort4", _short4}, {"half", _half},
    {"half4", _half4},
  };
};

Type *AirType::getTypeByMetalTypeName(const std::string &name) {
  return typeContext[name];
}

} // namespace dxmt::air