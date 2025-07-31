#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "DXBCParser/DXBCUtils.h"
#include "air_operations.hpp"
#include "air_signature.hpp"
#include "dxbc_constants.hpp"
#include "dxbc_instructions.hpp"
#include "shader_common.hpp"

#include "airconv_public.h"

namespace dxmt::dxbc {

struct ResourceRange {
  uint32_t range_id;
  uint32_t lower_bound;
  uint32_t size;
  uint32_t space;
};

struct ShaderResourceViewInfo {
  ResourceRange range;
  shader::common::ScalerDataType scaler_type;
  shader::common::ResourceType resource_type;
  bool read = false;
  bool sampled = false;
  bool compared = false; // therefore we use depth texture!

  uint32_t strucure_stride = 0;
  uint32_t arg_index;
  uint32_t arg_metadata_index;
};
struct UnorderedAccessViewInfo {
  ResourceRange range;
  shader::common::ScalerDataType scaler_type;
  shader::common::ResourceType resource_type;
  bool read = false;
  bool written = false;
  bool global_coherent = false;
  bool rasterizer_order = false;
  bool with_counter = false;

  uint32_t strucure_stride = 0;
  uint32_t arg_index;
  uint32_t arg_metadata_index;
  uint32_t arg_counter_index;
};
struct ConstantBufferInfo {
  ResourceRange range;
  uint32_t size_in_vec4;
  uint32_t arg_index;
};
struct SamplerInfo {
  ResourceRange range;
  uint32_t arg_index;
  uint32_t arg_metadata_index;
};

struct ThreadgroupBufferInfo {
  uint32_t size_in_uint;
  uint32_t size;
  uint32_t stride;
  bool structured;
};

struct PhaseInfo {
  uint32_t tempRegisterCount = 0;
  std::map<
    uint32_t, std::pair<uint32_t /* count */, uint32_t /* mask */>>
    indexableTempRegisterCounts;
};

class ShaderInfo {
public:
  std::vector<std::array<uint32_t, 4>> immConstantBufferData;
  std::map<uint32_t, ShaderResourceViewInfo> srvMap;
  std::map<uint32_t, UnorderedAccessViewInfo> uavMap;
  std::map<uint32_t, ConstantBufferInfo> cbufferMap;
  std::map<uint32_t, SamplerInfo> samplerMap;
  std::map<uint32_t, ThreadgroupBufferInfo> tgsmMap;
  uint32_t tempRegisterCount = 0;
  std::map<
    uint32_t, std::pair<uint32_t /* count */, uint32_t /* mask */>>
    indexableTempRegisterCounts;
  air::ArgumentBufferBuilder binding_table_cbuffer;
  air::ArgumentBufferBuilder binding_table;
  bool skipOptimization = false;
  bool refactoringAllowed = true;
  bool use_cmp_exch = false;
  bool no_control_point_phase_passthrough = false;
  bool output_control_point_read = false;
  std::vector<PhaseInfo> phases;
  uint32_t pull_mode_reg_mask = 0;
};

Instruction readInstruction(
  const microsoft::D3D10ShaderBinary::CInstruction &Inst,
  ShaderInfo &shader_info, uint32_t phase
);

using pvalue = dxmt::air::pvalue;
using epvalue = llvm::Expected<pvalue>;
using dxbc::Swizzle;
using dxbc::swizzle_identity;

struct context;
using IRValue = ReaderIO<context, pvalue>;
using IREffect = ReaderIO<context, std::monostate>;
using IndexedIRValue = std::function<IRValue(pvalue)>;

struct register_file {
  llvm::Value *ptr_int4 = nullptr;
  llvm::Value *ptr_float4 = nullptr;
};

struct indexable_register_file {
  llvm::Value *ptr_int_vec = nullptr;
  llvm::Value *ptr_float_vec = nullptr;
  uint32_t vec_size = 0;
};

struct phase_temp {
  register_file temp{};
  std::unordered_map<uint32_t, indexable_register_file> indexable_temp_map{};
};

struct sampler_descriptor {
  IndexedIRValue handle;
  IndexedIRValue bias;
};

struct texture_descriptor {
  air::MSLTexture texture_info;
  IndexedIRValue resource_id;
  IndexedIRValue metadata;
};

struct buffer_descriptor {
  uint32_t structure_stride;
  IndexedIRValue resource_id;
  IndexedIRValue metadata;
};

struct interpolant_descriptor {
  IndexedIRValue interpolant;
  bool perspective;
};

struct io_binding_map {
  llvm::GlobalVariable *icb = nullptr;
  llvm::Value *icb_float = nullptr;
  std::unordered_map<uint32_t, IndexedIRValue> cb_range_map{};
  std::unordered_map<uint32_t, sampler_descriptor> sampler_range_map{};
  std::unordered_map<uint32_t, texture_descriptor> srv_range_map{};
  std::unordered_map<uint32_t, buffer_descriptor> srv_buf_range_map{};
  std::unordered_map<uint32_t, texture_descriptor> uav_range_map{};
  std::unordered_map<uint32_t, buffer_descriptor> uav_buf_range_map{};
  std::unordered_map<uint32_t, std::pair<uint32_t, llvm::GlobalVariable *>>
    tgsm_map{};
  std::unordered_map<uint32_t, IndexedIRValue> uav_counter_range_map{};
  std::unordered_map<uint32_t, interpolant_descriptor> interpolant_map{};

  register_file input{};
  register_file output{};
  register_file temp{};
  std::unordered_map<uint32_t, indexable_register_file> indexable_temp_map{};
  std::vector<phase_temp> phases;
  register_file patch_constant_output{};
  uint32_t input_element_count = 0;
  uint32_t output_element_count = 0;

  // special registers (input)
  llvm::Value *thread_id_arg = nullptr;
  llvm::Value *thread_group_id_arg = nullptr;
  llvm::Value *thread_id_in_group_arg = nullptr;
  llvm::Value *thread_id_in_group_flat_arg = nullptr;
  llvm::Value *coverage_mask_arg = nullptr;

  llvm::Value *domain = nullptr;
  llvm::Value *patch_id = nullptr;
  llvm::Value *instanced_patch_id = nullptr;

  llvm::Value *thread_id_in_patch = nullptr;

  llvm::Value *gs_instance_id = nullptr;

  // special registers (output)
  llvm::AllocaInst *depth_output_reg = nullptr;
  llvm::AllocaInst *stencil_ref_reg = nullptr;
  llvm::AllocaInst *coverage_mask_reg = nullptr;

  llvm::AllocaInst *cmp_exch_temp = nullptr;

  // special buffers for tessellation
  llvm::Value *control_point_buffer;  // int4*
  llvm::Value *patch_constant_buffer; // int*
  llvm::Value *tess_factor_buffer;    // half*

  // temp for fast look-up
  llvm::Value *vertex_id = nullptr;
  llvm::Value *vertex_id_with_base = nullptr;
  llvm::Value *instance_id = nullptr;
  llvm::Value *instance_id_with_base = nullptr;
  llvm::Value *base_vertex_id = nullptr;
  llvm::Value *base_instance_id = nullptr;
  llvm::Value *vertex_buffer_table = nullptr;

  // geometry shader ops
  llvm::Value *mesh = nullptr;
  std::function<IREffect()> call_emit;
  std::function<IREffect()> call_cut;
};

struct context {
  llvm::IRBuilder<> &builder;
  llvm::LLVMContext &llvm;
  llvm::Module &module;
  llvm::Function *function;
  io_binding_map &resource;
  air::AirType &types; // hmmm
  uint32_t pso_sample_mask;
  microsoft::D3D10_SB_TOKENIZED_PROGRAM_TYPE shader_type;
};

template <typename S> IRValue make_irvalue(S &&fs) {
  return IRValue(std::forward<S>(fs));
}

template <typename S> IRValue make_irvalue_bind(S &&fs) {
  return IRValue([fs = std::forward<S>(fs)](auto ctx) {
    return fs(ctx).build(ctx);
  });
}

template <typename S> IREffect make_effect(S &&fs) {
  return IREffect(std::forward<S>(fs));
}

template <typename S> IREffect make_effect_bind(S &&fs) {
  return IREffect([fs = std::forward<S>(fs)](auto ctx) mutable {
    return fs(ctx).build(ctx);
  });
}

IREffect store_at_vec4_array_masked(
  llvm::Value *array, pvalue index, pvalue maybe_vec4, uint32_t mask
);

IREffect init_input_reg(
  uint32_t with_fnarg_at, uint32_t to_reg, uint32_t mask,
  bool fix_w_component = false
);

IREffect init_input_reg_with_interpolation(
  uint32_t with_fnarg_at, uint32_t to_reg, uint32_t mask,
  air::Interpolation interpolation, uint32_t sampleidx_at
);

std::function<IRValue(pvalue)>
pop_output_reg(uint32_t from_reg, uint32_t mask, uint32_t to_element);

std::function<IRValue(pvalue)>
pop_output_reg_fix_unorm(uint32_t from_reg, uint32_t mask, uint32_t to_element);

IREffect init_tess_factor_patch_constant(uint32_t to_reg, uint32_t mask, uint32_t factor_index, uint32_t factor_count);

std::function<IRValue(pvalue)> pop_output_tess_factor(
  uint32_t from_reg, uint32_t mask, uint32_t to_factor_indx, uint32_t factor_num
);

IREffect pull_vertex_input(
  air::FunctionSignatureBuilder &func_signature, uint32_t to_reg, uint32_t mask,
  SM50_IA_INPUT_ELEMENT element_info, uint32_t slot_mask
);

IREffect pop_mesh_output_render_taget_array_index(uint32_t from_reg, uint32_t mask, pvalue primitive_id);
IREffect pop_mesh_output_viewport_array_index(uint32_t from_reg, uint32_t mask, pvalue primitive_id);
IREffect pop_mesh_output_position(uint32_t from_reg, uint32_t mask, pvalue vertex_id);
IREffect
pop_mesh_output_vertex_data(uint32_t from_reg, uint32_t mask, uint32_t idx, pvalue vertex_id, air::MSLScalerOrVectorType desired_type);

enum class mem_flags : uint8_t {
  none = 0,
  device = 1,
  threadgroup = 2,
  texture = 4,
};

IREffect call_threadgroup_barrier(mem_flags mem_flag);

llvm::Expected<llvm::BasicBlock *> convert_basicblocks(
  std::shared_ptr<BasicBlock> entry, context &ctx, llvm::BasicBlock *return_bb
);

constexpr air::MSLScalerOrVectorType to_msl_type(RegisterComponentType type) {
  switch (type) {
  case RegisterComponentType::Unknown: {
    assert(0 && "unknown component type");
    break;
  }
  case RegisterComponentType::Uint:
    return air::msl_uint4;
  case RegisterComponentType::Int:
    return air::msl_int4;
  case RegisterComponentType::Float:
    return air::msl_float4;
    break;
  }
}

struct ScalarInfo {
  uint8_t component : 2;
  uint8_t reg : 6;
};

struct SignatureContext {
  IREffect &prologue;
  IRValue &epilogue;
  air::FunctionSignatureBuilder &func_signature;
  io_binding_map &resource;
  SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia_layout;
  bool dual_source_blending;
  bool disable_depth_output;
  bool skip_vertex_output;
  uint32_t pull_mode_reg_mask;
  uint32_t unorm_output_reg_mask;

  SignatureContext(
    IREffect &prologue, IRValue &epilogue, air::FunctionSignatureBuilder &func_signature, io_binding_map &resource
  )
      : prologue(prologue), epilogue(epilogue), func_signature(func_signature), resource(resource), ia_layout(nullptr),
        dual_source_blending(false), disable_depth_output(false), skip_vertex_output(false), pull_mode_reg_mask(0),
        unorm_output_reg_mask(0){};
};

struct GSOutputContext {
  llvm::Value *vertex_id;
  llvm::Value *primitive_id;
};

class Signature {

public:
  Signature(const microsoft::D3D11_SIGNATURE_PARAMETER &parameter)
      : semantic_name_(parameter.SemanticName),
        semantic_index_(parameter.SemanticIndex), stream_(parameter.Stream),
        mask_(parameter.Mask), register_(parameter.Register),
        system_value_(parameter.SystemValue),
        component_type_((RegisterComponentType)parameter.ComponentType) {}

  std::string_view semanticName() const { return semantic_name_; }

  uint32_t semanticIndex() const { return semantic_index_; }

  std::string fullSemanticString() const {
    return semantic_name_ + std::to_string(semantic_index_);
  }

  uint32_t stream() const { return stream_; }

  uint32_t mask() const { return mask_; }

  uint32_t reg() const { return register_; }

  bool isSystemValue() const {
    return system_value_ != microsoft::D3D10_SB_NAME_UNDEFINED;
  }

  RegisterComponentType componentType() const {
    return (RegisterComponentType)component_type_;
  }

private:
  std::string semantic_name_;
  uint8_t semantic_index_;
  uint8_t stream_;
  uint8_t mask_;
  uint8_t register_;
  microsoft::D3D10_SB_NAME system_value_;
  RegisterComponentType component_type_;
};

class SM50ShaderInternal {
public:
  dxmt::dxbc::ShaderInfo shader_info;
  dxmt::air::FunctionSignatureBuilder func_signature;
  std::vector<Signature> output_signature;
  std::shared_ptr<dxmt::dxbc::BasicBlock> entry;
  std::vector<std::function<void(SignatureContext &)>> signature_handlers;
  microsoft::D3D10_SB_TOKENIZED_PROGRAM_TYPE shader_type;
  /* for domain shader, it refers to patch constant input count */
  uint32_t max_input_register = 0;
  uint32_t max_output_register = 0;
  uint32_t max_patch_constant_output_register = 0;
  std::vector<MTL_SM50_SHADER_ARGUMENT> args_reflection_cbuffer;
  std::vector<MTL_SM50_SHADER_ARGUMENT> args_reflection;
  uint32_t threadgroup_size[3] = {0};
  uint32_t input_control_point_count = ~0u;
  uint32_t output_control_point_count = ~0u;
  uint32_t tessellation_partition = 0;
  float max_tesselation_factor = 64.0f;
  microsoft::D3D11_SB_TESSELLATOR_OUTPUT_PRIMITIVE tessellator_output_primitive = {};
  std::vector<ScalarInfo> patch_constant_scalars;
  uint32_t hull_maximum_threads_per_patch = 0;
  std::vector<ScalarInfo> clip_distance_scalars;
  microsoft::D3D10_SB_PRIMITIVE gs_input_primitive = {};
  std::vector<std::function<IREffect(GSOutputContext &)>> gs_output_handlers;
  uint32_t num_mesh_vertex_data = 0;
  microsoft::D3D10_SB_PRIMITIVE_TOPOLOGY gs_output_topology = {};
  uint32_t gs_max_vertex_output = 0;
  uint32_t gs_instance_count = 1;
};

void handle_signature(
  microsoft::CSignatureParser &inputParser,
  microsoft::CSignatureParser5 &outputParser,
  microsoft::D3D10ShaderBinary::CInstruction &Inst, SM50ShaderInternal *sm50_shader,
  uint32_t phase
);

void setup_binding_table(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::FunctionSignatureBuilder &func_signature, llvm::Module &module
);

void setup_tgsm(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module
);

void setup_fastmath_flag(llvm::Module &module, llvm::IRBuilder<> &builder);

void setup_temp_register(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module, llvm::IRBuilder<> &builder
);

void setup_immediate_constant_buffer(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module, llvm::IRBuilder<> &builder
);

llvm::Error convert_dxbc_hull_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  SM50ShaderInternal *pVertexStage, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
);

llvm::Error convert_dxbc_domain_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  SM50ShaderInternal *pHullStage, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
);

llvm::Error convert_dxbc_vertex_for_hull_shader(
  const SM50ShaderInternal *pShaderInternal, const char *name,
  const SM50ShaderInternal *pHullStage,
  llvm::LLVMContext &context, llvm::Module &module,
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
);

llvm::Error convert_dxbc_geometry_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  SM50ShaderInternal *pVertexStage, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
);

llvm::Error convert_dxbc_vertex_for_geometry_shader(
  const SM50ShaderInternal *pShaderInternal, const char *name,
  const SM50ShaderInternal *pGeometryStage,
  llvm::LLVMContext &context, llvm::Module &module,
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
);
} // namespace dxmt::dxbc
