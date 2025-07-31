#pragma once

#include "adt.hpp"
#include "shader_common.hpp"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <variant>

namespace dxmt::air {

// static llvm::StructType *
// getOrCreateStructType(llvm::StringRef Name, llvm::ArrayRef<llvm::Type *>
// EltTys,
//                       llvm::LLVMContext &Ctx) {
//   using namespace llvm;
//   StructType *ST = StructType::getTypeByName(Ctx, Name);
//   if (ST)
//     return ST;

//   return StructType::create(Ctx, EltTys, Name);
// }

inline llvm::StructType *
getOrCreateStructType(llvm::StringRef Name, llvm::LLVMContext &Ctx) {
  using namespace llvm;
  StructType *ST = StructType::getTypeByName(Ctx, Name);
  if (ST)
    return ST;

  return StructType::create(Ctx, Name);
}

constexpr auto get_name = [](auto msl_type) {
  return std::visit(
    [](auto msl_type) { return msl_type.get_name(); }, msl_type
  );
};

constexpr auto get_llvm_type = [](auto msl_type, llvm::LLVMContext &context) {
  return std::visit([&](auto x) { return x.get_llvm_type(context); }, msl_type);
};

enum class MemoryAccess : uint32_t {
  sample = 0,
  read = 1,
  write = 2,
  read_write = 3
};

enum class AddressSpace : uint32_t {
  unknown,
  device,
  constant,
  threadgroup,
  object_data = 6
};

enum class Sign { inapplicable, with_sign, no_sign };

// I use kind to not confuse with type which often refers to component type
enum class TextureKind {
  texture_1d,
  texture_1d_array,
  texture_2d,
  texture_2d_array,
  texture_2d_ms,
  texture_2d_ms_array,
  texture_3d,
  texture_cube,
  texture_cube_array,
  texture_buffer, // 10
  depth_2d,
  depth_2d_array,
  depth_2d_ms,
  depth_2d_ms_array,
  depth_cube,
  depth_cube_array // 16
};

inline TextureKind lowering_texture_1d_to_2d(TextureKind kind) {
  switch (kind) {
  case TextureKind::texture_1d:
    return TextureKind::texture_2d;
  case TextureKind::texture_1d_array:
    return TextureKind::texture_2d_array;
  default:
    break;
  }
  return kind;
};

enum class Interpolation {
  center_perspective,
  center_no_perspective,
  centroid_perspective,
  centroid_no_perspective,
  sample_perspective,
  sample_no_perspective,
  flat
};

enum class PostTessellationPatch { triangle, quad };

struct MSLFloat {
  std::string get_name() const { return "float"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return llvm::Type::getFloatTy(context);
  };
};

struct MSLHalf {
  std::string get_name() const { return "half"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return llvm::Type::getHalfTy(context);
  };
};

struct MSLInt {
  std::string get_name() const { return "int"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return llvm::Type::getInt32Ty(context);
  };
};

struct MSLUint {
  std::string get_name() const { return "uint"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return llvm::Type::getInt32Ty(context);
  };
};

struct MSLUshort {
  std::string get_name() const { return "ushort"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return llvm::Type::getInt16Ty(context);
  };
};

struct MSLBool {
  std::string get_name() const { return "bool"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return llvm::Type::getInt1Ty(context);
  };
};

struct MSLUlong {
  std::string get_name() const { return "ulong"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return llvm::Type::getInt64Ty(context);
  };
};

struct MSLSampler {
  std::string get_name() const { return "sampler"; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    return getOrCreateStructType("struct._sampler_t", context)
      ->getPointerTo(2); // samplers are in constant addrspace
  };
};

constexpr auto msl_bool = MSLBool{};
constexpr auto msl_int = MSLInt{};
constexpr auto msl_uint = MSLUint{};
constexpr auto msl_float = MSLFloat{};
constexpr auto msl_ulong = MSLUlong{};

using MSLScalerType = std::variant<
  MSLFloat, MSLInt, MSLUint, MSLBool, MSLHalf, MSLUshort,
  MSLUlong>; // incomplete list

constexpr auto msl_sampler = MSLSampler{};

struct MSLVector {
  uint32_t dimension;
  MSLScalerType scaler;
  std::string get_name() const {
    auto scaler_name =
      std::visit([](auto scaler) { return scaler.get_name(); }, scaler);
    if (dimension == 1)
      return scaler_name;
    return scaler_name + std::to_string(dimension);
  };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    auto scaler_type =
      std::visit([&](auto x) { return x.get_llvm_type(context); }, scaler);
    if (dimension == 1) {
      return scaler_type;
    }
    return llvm::FixedVectorType::get(scaler_type, dimension);
  };
};

constexpr auto msl_int2 = MSLVector{2, msl_int};
constexpr auto msl_uint2 = MSLVector{2, msl_uint};
constexpr auto msl_float2 = MSLVector{2, msl_float};
constexpr auto msl_int3 = MSLVector{3, msl_int};
constexpr auto msl_uint3 = MSLVector{3, msl_uint};
constexpr auto msl_float3 = MSLVector{3, msl_float};
constexpr auto msl_int4 = MSLVector{4, msl_int};
constexpr auto msl_uint4 = MSLVector{4, msl_uint};
constexpr auto msl_float4 = MSLVector{4, msl_float};

struct MSLTexture {
  MSLScalerType component_type;
  MemoryAccess memory_access;
  TextureKind resource_kind;
  TextureKind resource_kind_logical;

  std::string get_name() const {
    auto component =
      std::visit([](auto scaler) { return scaler.get_name(); }, component_type);
    auto access = [](auto access) -> std::string {
      switch (access) {
      case MemoryAccess::read:
        return "read";
      case MemoryAccess::read_write:
        return "read_write";
      case MemoryAccess::write:
        return "write";
      case MemoryAccess::sample:
        return "sample";
      }
      assert(0 && "unhandled memory access");
    }(memory_access);

    switch (resource_kind) {
    case TextureKind::texture_1d:
      return "texture1d<" + component + "," + access + ">";
    case TextureKind::texture_1d_array:
      return "texture1d_array<" + component + "," + access + ">";
    case TextureKind::texture_2d:
      return "texture2d<" + component + "," + access + ">";
    case TextureKind::texture_2d_array:
      return "texture2d_array<" + component + "," + access + ">";
    case TextureKind::texture_2d_ms:
      return "texture2d_ms<" + component + "," + access + ">";
    case TextureKind::texture_2d_ms_array:
      return "texture2d_ms_array<" + component + "," + access + ">";
    case TextureKind::texture_3d:
      return "texture3d<" + component + "," + access + ">";
    case TextureKind::texture_buffer:
      return "texture_buffer<" + component + "," + access + ">";
    case TextureKind::texture_cube:
      return "texturecube<" + component + "," + access + ">";
    case TextureKind::texture_cube_array:
      return "texturecube_array<" + component + "," + access + ">";
    case TextureKind::depth_2d:
      return "depth2d<" + component + "," + access + ">";
    case TextureKind::depth_2d_array:
      return "depth2d_array<" + component + "," + access + ">";
    case TextureKind::depth_2d_ms:
      return "depth2d_ms<" + component + "," + access + ">";
    case TextureKind::depth_2d_ms_array:
      return "depth2d_ms_array<" + component + "," + access + ">";
    case TextureKind::depth_cube:
      return "depthcube<" + component + "," + access + ">";
    case TextureKind::depth_cube_array:
      return "depthcube_array<" + component + "," + access + ">";
    }
    assert(0 && "unreachable");
  };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) const {
    switch (resource_kind) {
    case TextureKind::texture_1d:
      return getOrCreateStructType("struct._texture_1d_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_1d_array:
      return getOrCreateStructType("struct._texture_1d_array_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_2d:
      return getOrCreateStructType("struct._texture_2d_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_2d_array:
      return getOrCreateStructType("struct._texture_2d_array_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_2d_ms:
      return getOrCreateStructType("struct._texture_2d_ms_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_2d_ms_array:
      return getOrCreateStructType("struct._texture_2d_ms_array_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_3d:
      return getOrCreateStructType("struct._texture_3d_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_cube:
      return getOrCreateStructType("struct._texture_cube_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_cube_array:
      return getOrCreateStructType("struct._texture_cube_array_t", context)
        ->getPointerTo(1);
    case TextureKind::texture_buffer:
      return getOrCreateStructType("struct._texture_buffer_1d_t", context)
        ->getPointerTo(1);
    case TextureKind::depth_2d:
      return getOrCreateStructType("struct._depth_2d_t", context)
        ->getPointerTo(1);
    case TextureKind::depth_2d_array:
      return getOrCreateStructType("struct._depth_2d_array_t", context)
        ->getPointerTo(1);
    case TextureKind::depth_2d_ms:
      return getOrCreateStructType("struct._depth_2d_ms_t", context)
        ->getPointerTo(1);
    case TextureKind::depth_2d_ms_array:
      return getOrCreateStructType("struct._depth_2d_ms_array_t", context)
        ->getPointerTo(1);
    case TextureKind::depth_cube:
      return getOrCreateStructType("struct._depth_cube_t", context)
        ->getPointerTo(1);
    case TextureKind::depth_cube_array:
      return getOrCreateStructType("struct._depth_cube_array_t", context)
        ->getPointerTo(1);
      break;
    };
  };
};

// I mean does metal really care about it?
struct MSLWhateverStruct {
  std::string name;
  llvm::Type *type;
  std::string get_name() { return name; };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) { return type; };
};

using MSLScalerOrVectorType =
  template_concat_t<MSLScalerType, std::variant<MSLVector>>;

using MSLRepresentableType = template_concat_t<
  MSLScalerOrVectorType,
  std::variant<MSLSampler, MSLTexture, MSLWhateverStruct>>;

struct MSLPointer {
  MSLRepresentableType pointee;
  AddressSpace address_space;
  std::string get_name() {
    // fking weird
    return std::visit([](auto x) { return x.get_name(); }, pointee);
  };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) {
    auto pointee_type =
      std::visit([&](auto x) { return x.get_llvm_type(context); }, pointee);
    return pointee_type->getPointerTo((uint32_t)address_space);
  };
};

struct MSLStaticArray {
  uint32_t array_size;
  MSLRepresentableType element_type;
  std::string get_name() {
    return "array<" +
           std::visit(
             [](auto scaler) { return scaler.get_name(); }, element_type
           ) +
           "," + std::to_string(array_size) + ">";
  };

  llvm::Type *get_llvm_type(llvm::LLVMContext &context) {
    return llvm::ArrayType::get(
      std::visit(
        [&](auto x) { return x.get_llvm_type(context); }, element_type
      ),
      array_size
    );
  }
};

using MSLRepresentableTypeWithArray = template_concat_t<
  MSLRepresentableType,
  std::variant<MSLStaticArray, MSLPointer /* why are you here? */>>;

struct ArgumentBindingBuffer {
  std::optional<uint32_t> buffer_size; // unbounded if not represented
  uint32_t location_index;
  uint32_t array_size;
  MemoryAccess memory_access;
  AddressSpace address_space;
  // std::string arg_type_name;
  MSLRepresentableType type;
  std::string arg_name;
  std::optional<uint32_t> raster_order_group;
};

struct ArgumentBindingSampler {
  uint32_t location_index;
  uint32_t array_size;
  std::string arg_name;
};

struct ArgumentBindingTexture {
  uint32_t location_index;
  uint32_t array_size;
  MemoryAccess memory_access;
  MSLTexture type; // why it's a variant!
  std::string arg_name;
  std::optional<uint32_t> raster_order_group;
};

/* is this in fact argument buffer? */
struct ArgumentBindingIndirectBuffer {
  uint32_t location_index;
  uint32_t array_size;
  MemoryAccess memory_access;
  AddressSpace address_space;
  llvm::StructType *struct_type;
  llvm::Metadata *struct_type_info;
  std::string arg_name;
};

struct ArgumentBindingIndirectConstant {
  uint32_t location_index;
  uint32_t array_size;
  MSLRepresentableTypeWithArray type;
  std::string arg_name;
};

using ArgumentBufferArguments = std::variant<
  ArgumentBindingBuffer, ArgumentBindingIndirectBuffer, ArgumentBindingTexture,
  ArgumentBindingIndirectConstant, ArgumentBindingSampler>;

using FunctionArguments = std::variant<
  ArgumentBindingBuffer, ArgumentBindingIndirectBuffer, ArgumentBindingTexture,
  ArgumentBindingSampler>;

class ArgumentBufferBuilder {
  /* the return value is the element index of structure (as well as in argument
   * buffer) */
public:
  uint32_t DefineBuffer(
    std::string name, AddressSpace addressp_space, MemoryAccess access,
    MSLRepresentableType type, uint32_t location_index = UINT32_MAX,
    std::optional<uint32_t> raster_order_group = std::nullopt
  );
  // uint32_t DefineIndirectBuffer(
  //   std::string name, llvm::StructType* struct_type, llvm::Metadata*
  //   struct_type_metadata
  // );
  uint32_t DefineTexture(
    std::string name, TextureKind kind, MemoryAccess access,
    MSLScalerType scaler_type, uint32_t location_index = UINT32_MAX,
    std::optional<uint32_t> raster_order_group = std::nullopt
  );
  uint32_t
  DefineSampler(std::string name, uint32_t location_index = UINT32_MAX);
  uint32_t
  DefineInteger64(std::string name, uint32_t location_index = UINT32_MAX);

  auto Build(llvm::LLVMContext &context, const llvm::DataLayout &layout) const
    -> std::tuple<llvm::StructType *, llvm::MDNode *>;

  auto Empty() const { return fieldsType.empty(); }

  auto Size() const { return fieldsType.size(); }

private:
  std::vector<ArgumentBufferArguments> fieldsType;
  std::unordered_set<std::string> fields;
};

enum class InputAttributeComponentType : uint32_t {
  Unknown,
  Uint = 1,
  Int = 2,
  Float = 3
};

struct InputVertexStageIn {
  uint32_t attribute;
  InputAttributeComponentType type;
  std::string name;
};

struct OutputVertex {
  std::string user;
  MSLScalerOrVectorType type;

  bool operator==(OutputVertex const& rhs) const { return user == rhs.user; }
};

struct OutputPosition {
  MSLScalerOrVectorType type;

  bool operator==(OutputPosition const& rhs) const { return true; }
};

struct OutputMeshData {
  std::string user;
  MSLScalerOrVectorType type;
  uint32_t index;

  bool operator==(OutputMeshData const& rhs) const { return user == rhs.user && index == rhs.index; }
};

struct InputFragmentStageIn {
  std::string user;
  MSLScalerOrVectorType type;
  Interpolation interpolation;
  bool pull_mode;
};

struct InputVertexID {};
struct InputBaseVertex {};
struct InputInstanceID {};
struct InputBaseInstance {};
struct InputViewportArrayIndex {};
struct InputRenderTargetArrayIndex {};

struct InputPrimitiveID {};
struct InputFrontFacing {};
struct InputInputCoverage {};
struct InputSampleIndex {};

struct InputPatchID {};
struct InputPositionInPatch {
  PostTessellationPatch patch;
};
struct InputPayload {
  uint32_t size;
};

enum class MeshOutputTopology : uint32_t {
  Point,
  Line,
  Triangle,
};

struct InputMesh {
  uint32_t vertex_count;
  uint32_t primitive_count;
  MeshOutputTopology topology;
};

struct InputMeshGridProperties {};

struct InputPosition {
  Interpolation interpolation;
};

struct InputThreadIndexInThreadgroup {};    // uint
struct InputThreadPositionInThreadgroup {}; // uint3
struct InputThreadPositionInGrid {};        // uint3
struct InputThreadgroupPositionInGrid {};   // uint3

struct InputThreadgroupsPerGrid {};         // uint3

struct OutputRenderTarget {
  bool dual_source_blending;
  uint32_t index;
  MSLScalerOrVectorType type;

  bool operator==(OutputRenderTarget const& rhs) const { return index == rhs.index; }
};

enum class DepthArgument { any = 0, greater = 1, less = 2 };

struct OutputDepth {
  DepthArgument depth_argument;

  bool operator==(OutputDepth const& rhs) const { return true; }
};

struct OutputCoverageMask {
  bool operator==(OutputCoverageMask const& rhs) const { return true; }
};

struct OutputClipDistance {
  size_t count;
  bool operator==(OutputClipDistance const& rhs) const { return true; }
};

struct OutputRenderTargetArrayIndex {
  bool operator==(OutputRenderTargetArrayIndex const& rhs) const { return true; }
};

struct OutputViewportArrayIndex {
  bool operator==(OutputViewportArrayIndex const& rhs) const { return true; }
};

using FunctionInput = template_concat_t<
  FunctionArguments,
  std::variant<
    /* vertex */
    InputVertexID, InputInstanceID,     //
    InputBaseVertex, InputBaseInstance, //
    InputVertexStageIn,
    /* fragment */
    InputPrimitiveID, InputViewportArrayIndex, InputRenderTargetArrayIndex,
    InputFrontFacing, InputPosition, InputSampleIndex, //
    InputFragmentStageIn, InputInputCoverage,
    /* post-tessellation */
    InputPatchID, InputPositionInPatch,
    /* object & mesh */
    InputPayload, InputMeshGridProperties, InputMesh,
    /* kernel */
    InputThreadIndexInThreadgroup, InputThreadPositionInThreadgroup,
    InputThreadPositionInGrid, InputThreadgroupPositionInGrid,
    InputThreadgroupsPerGrid>>;

using FunctionOutput = std::variant<
  /* vertex */
  OutputVertex, OutputPosition, OutputClipDistance,
  OutputRenderTargetArrayIndex, OutputViewportArrayIndex,
  /* fragment */
  OutputRenderTarget, OutputDepth, OutputCoverageMask>;

using MeshVertexOutput = std::variant<OutputMeshData, OutputPosition, OutputClipDistance>;

using MeshPrimitiveOutput = std::variant<OutputMeshData, OutputRenderTargetArrayIndex, OutputViewportArrayIndex>;

class FunctionSignatureBuilder {
public:
  /*
   * uniqueness is implied by variant type
   * for InputVertexStageIn: attribute
   * for InputFragmentStageIn: user
   * for ArgumentBinding*: no guarantee
   */
  uint32_t DefineInput(const FunctionInput &input);
  uint32_t DefineOutput(const FunctionOutput &output);
  uint32_t DefineMeshVertexOutput(const MeshVertexOutput &output);
  uint32_t DefineMeshPrimitiveOutput(const MeshPrimitiveOutput &output);
  void UseEarlyFragmentTests() { early_fragment_tests = true; }
  void UseMaxWorkgroupSize(uint32_t size) { max_work_group_size = size; }
  void UsePatch(PostTessellationPatch patch, uint32_t num_control_points) {
    this->patch = std::make_pair(patch, num_control_points);
  }

  PostTessellationPatch GetPatchType() {
    assert(patch.has_value() && "not a domain shader/unsupported isoline tessellation");
    return patch->first;
  };

  auto CreateFunction(
    std::string name, llvm::LLVMContext &context, llvm::Module &module,
    uint64_t sign_mask, bool skip_output
  ) -> std::pair<llvm::Function *, llvm::MDNode *>;

private:
  std::vector<FunctionInput> inputs;
  std::vector<FunctionOutput> outputs;
  std::vector<MeshVertexOutput> mesh_vertex_outputs;
  std::vector<MeshPrimitiveOutput> mesh_primitive_outputs;
  bool early_fragment_tests = false;
  uint32_t max_work_group_size = 0;
  std::optional<std::pair<PostTessellationPatch, uint32_t>> patch;
};

inline TextureKind to_air_resource_type(
  dxmt::shader::common::ResourceType type, bool use_depth = false
) {
  switch (type) {
  case shader::common::ResourceType::TextureBuffer:
    return TextureKind::texture_buffer;
  case shader::common::ResourceType::Texture1D:
    return TextureKind::texture_1d;
  case shader::common::ResourceType::Texture1DArray:
    return TextureKind::texture_1d_array;
  case shader::common::ResourceType::Texture2D:
    return use_depth ? TextureKind::depth_2d : TextureKind::texture_2d;
  case shader::common::ResourceType::Texture2DArray:
    return use_depth ? TextureKind::depth_2d_array
                     : TextureKind::texture_2d_array;
  case shader::common::ResourceType::Texture2DMultisampled:
    return use_depth ? TextureKind::depth_2d_ms : TextureKind::texture_2d_ms;
  case shader::common::ResourceType::Texture2DMultisampledArray:
    return use_depth ? TextureKind::depth_2d_ms_array
                     : TextureKind::texture_2d_ms_array;
  case shader::common::ResourceType::Texture3D:
    return TextureKind::texture_3d;
  case shader::common::ResourceType::TextureCube:
    return use_depth ? TextureKind::depth_cube : TextureKind::texture_cube;
  case shader::common::ResourceType::TextureCubeArray:
    return use_depth ? TextureKind::depth_cube_array
                     : TextureKind::texture_cube_array;
  default:
    break;
  };
  assert(0 && "unreachable");
};

inline MSLScalerType
to_air_scaler_type(dxmt::shader::common::ScalerDataType type) {
  switch (type) {
  case shader::common::ScalerDataType::Float:
    return msl_float;
  case shader::common::ScalerDataType::Uint:
    return msl_uint;
  case shader::common::ScalerDataType::Int:
    return msl_int;
  case shader::common::ScalerDataType::Double:
    assert(0 && "");
    break;
  }
}

} // namespace dxmt::air