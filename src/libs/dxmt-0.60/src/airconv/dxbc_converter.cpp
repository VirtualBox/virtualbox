#include "dxbc_converter.hpp"
#include "DXBCParser/BlobContainer.h"
#include "DXBCParser/ShaderBinary.h"
#include "DXBCParser/winerror.h"
#include "airconv_error.hpp"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <bit>
#include <memory>
#include <string>
#include <vector>

#include "metallib_writer.hpp"
#include "shader_common.hpp"

#include "airconv_context.hpp"

#include "ftl.hpp"

class SM50CompiledBitcodeInternal {
public:
  llvm::SmallVector<char, 0> vec;
};

class SM50ErrorInternal {
public:
  llvm::SmallVector<char, 0> buf;
};

namespace dxmt::dxbc {

inline dxmt::shader::common::ResourceType
to_shader_resource_type(microsoft::D3D10_SB_RESOURCE_DIMENSION dim) {
  switch (dim) {
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_UNKNOWN:
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_BUFFER:
  case microsoft::D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER:
  case microsoft::D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER:
    return shader::common::ResourceType::TextureBuffer;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D:
    return shader::common::ResourceType::Texture1D;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D:
    return shader::common::ResourceType::Texture2D;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMS:
    return shader::common::ResourceType::Texture2DMultisampled;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D:
    return shader::common::ResourceType::Texture3D;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE:
    return shader::common::ResourceType::TextureCube;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY:
    return shader::common::ResourceType::Texture1DArray;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY:
    return shader::common::ResourceType::Texture2DArray;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY:
    return shader::common::ResourceType::Texture2DMultisampledArray;
  case microsoft::D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY:
    return shader::common::ResourceType::TextureCubeArray;
  }
  assert(0 && "invalid D3D10_SB_RESOURCE_DIMENSION");
};

inline dxmt::shader::common::ScalerDataType
to_shader_scaler_type(microsoft::D3D10_SB_RESOURCE_RETURN_TYPE type) {
  switch (type) {

  case microsoft::D3D10_SB_RETURN_TYPE_UNORM:
  case microsoft::D3D10_SB_RETURN_TYPE_SNORM:
  case microsoft::D3D10_SB_RETURN_TYPE_FLOAT:
    return shader::common::ScalerDataType::Float;
  case microsoft::D3D10_SB_RETURN_TYPE_SINT:
    return shader::common::ScalerDataType::Int;
  case microsoft::D3D10_SB_RETURN_TYPE_UINT:
    return shader::common::ScalerDataType::Uint;
  case microsoft::D3D10_SB_RETURN_TYPE_MIXED:
    return shader::common::ScalerDataType::Uint; // kinda weird but ok
  case microsoft::D3D11_SB_RETURN_TYPE_DOUBLE:
  case microsoft::D3D11_SB_RETURN_TYPE_CONTINUED:
  case microsoft::D3D11_SB_RETURN_TYPE_UNUSED:
    break;
  }
  assert(0 && "invalid D3D10_SB_RESOURCE_RETURN_TYPE");
}

uint32_t next_pow2(uint32_t x) {
  return x == 1 ? 1 : 1 << (32 - __builtin_clz(x - 1));
}

auto get_item_in_argbuf_binding_table(uint32_t argbuf_index, uint32_t index) {
  return make_irvalue([=](context ctx) {
    auto argbuf = ctx.function->getArg(argbuf_index);
    auto argbuf_struct_type = llvm::cast<llvm::StructType>(
      llvm::cast<llvm::PointerType>(argbuf->getType())
        ->getNonOpaquePointerElementType()
    );
    return ctx.builder.CreateLoad(
      argbuf_struct_type->getElementType(index),
      ctx.builder.CreateStructGEP(
        llvm::cast<llvm::PointerType>(argbuf->getType())
          ->getNonOpaquePointerElementType(),
        argbuf, index
      )
    );
  });
};

void setup_binding_table(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::FunctionSignatureBuilder &func_signature, llvm::Module &module
) {
  uint32_t binding_table_index = ~0u;
  uint32_t cbuf_table_index = ~0u;
  if (!shader_info->binding_table.Empty()) {
    auto [type, metadata] = shader_info->binding_table.Build(
      module.getContext(), module.getDataLayout()
    );
    binding_table_index =
      func_signature.DefineInput(air::ArgumentBindingIndirectBuffer{
        .location_index = 30, // kArgumentBufferBindIndex
        .array_size = 1,
        .memory_access = air::MemoryAccess::read,
        .address_space = air::AddressSpace::constant,
        .struct_type = type,
        .struct_type_info = metadata,
        .arg_name = "binding_table",
      });
  }
  if (!shader_info->binding_table_cbuffer.Empty()) {
    auto [type, metadata] = shader_info->binding_table_cbuffer.Build(
      module.getContext(), module.getDataLayout()
    );
    cbuf_table_index =
      func_signature.DefineInput(air::ArgumentBindingIndirectBuffer{
        .location_index = 29, // kConstantBufferBindIndex
        .array_size = 1,
        .memory_access = air::MemoryAccess::read,
        .address_space = air::AddressSpace::constant,
        .struct_type = type,
        .struct_type_info = metadata,
        .arg_name = "cbuffer_table",
      });
  }

  for (auto &[range_id, cbv] : shader_info->cbufferMap) {
    // TODO: abstract SM 5.0 binding
    auto index = cbv.arg_index;
    resource_map.cb_range_map[range_id] = [=](pvalue) {
      // ignore index in SM 5.0
      return get_item_in_argbuf_binding_table(cbuf_table_index, index);
    };
  }
  for (auto &[range_id, sampler] : shader_info->samplerMap) {
    // TODO: abstract SM 5.0 binding
    resource_map.sampler_range_map[range_id] = {
      [=, index = sampler.arg_index](pvalue) {
        // ignore index in SM 5.0
        return get_item_in_argbuf_binding_table(binding_table_index, index);
      },
      [=, index = sampler.arg_metadata_index](pvalue) {
        // ignore index in SM 5.0
        return get_item_in_argbuf_binding_table(binding_table_index, index);
      }
    };
  }
  for (auto &[range_id, srv] : shader_info->srvMap) {
    if (srv.resource_type != shader::common::ResourceType::NonApplicable) {
      // TODO: abstract SM 5.0 binding
      auto access =
        srv.sampled ? air::MemoryAccess::sample : air::MemoryAccess::read;
      auto texture_kind_logical = air::to_air_resource_type(srv.resource_type, srv.compared);
      auto scaler_type = air::to_air_scaler_type(srv.scaler_type);
      resource_map.srv_range_map[range_id] = {
        air::MSLTexture{
          .component_type = scaler_type,
          .memory_access = access,
          .resource_kind = air::lowering_texture_1d_to_2d(texture_kind_logical),
          .resource_kind_logical = texture_kind_logical,
        },
        [=, index = srv.arg_index](pvalue) {
          // ignore index in SM 5.0
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        },
        [=, index = srv.arg_metadata_index](pvalue) {
          // ignore index in SM 5.0
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        }
      };
    } else {
      resource_map.srv_buf_range_map[range_id] = {
        srv.strucure_stride,
        [=, index = srv.arg_index](pvalue) {
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        },
        [=, index = srv.arg_metadata_index](pvalue) {
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        }
      };
    }
  }
  for (auto &[range_id, uav] : shader_info->uavMap) {
    auto access = uav.written ? (uav.read ? air::MemoryAccess::read_write
                                          : air::MemoryAccess::write)
                              : air::MemoryAccess::read;
    if (uav.resource_type != shader::common::ResourceType::NonApplicable) {
      auto texture_kind_logical = air::to_air_resource_type(uav.resource_type);
      auto scaler_type = air::to_air_scaler_type(uav.scaler_type);
      resource_map.uav_range_map[range_id] = {
        air::MSLTexture{
          .component_type = scaler_type,
          .memory_access = access,
          .resource_kind = air::lowering_texture_1d_to_2d(texture_kind_logical),
          .resource_kind_logical = texture_kind_logical,
        },
        [=, index = uav.arg_index](pvalue) {
          // ignore index in SM 5.0
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        },
        [=, index = uav.arg_metadata_index](pvalue) {
          // ignore index in SM 5.0
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        }
      };
    } else {
      resource_map.uav_buf_range_map[range_id] = {
        uav.strucure_stride,
        [=, index = uav.arg_index](pvalue) {
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        },
        [=, index = uav.arg_metadata_index](pvalue) {
          return get_item_in_argbuf_binding_table(binding_table_index, index);
        }
      };
      if (uav.with_counter) {
        auto argbuf_index_counterptr = uav.arg_counter_index;
        resource_map.uav_counter_range_map[range_id] = [=](pvalue) {
          return get_item_in_argbuf_binding_table(
            binding_table_index, argbuf_index_counterptr
          );
        };
      }
    }
  }
};

void setup_immediate_constant_buffer(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module, llvm::IRBuilder<> &builder
) {
  if (!shader_info->immConstantBufferData.size()) {
    return;
  }
  auto &context = module.getContext();
  auto type = llvm::ArrayType::get(
    types._int4, shader_info->immConstantBufferData.size()
  );
  auto const_data = llvm::ConstantArray::get(
    type,
    shader_info->immConstantBufferData |
      [&](auto data) {
        return llvm::ConstantVector::get(
          {llvm::ConstantInt::get(context, llvm::APInt{32, data[0], false}),
           llvm::ConstantInt::get(context, llvm::APInt{32, data[1], false}),
           llvm::ConstantInt::get(context, llvm::APInt{32, data[2], false}),
           llvm::ConstantInt::get(context, llvm::APInt{32, data[3], false})}
        );
      }
  );
  llvm::GlobalVariable *icb = new llvm::GlobalVariable(
    module, type, true, llvm::GlobalValue::InternalLinkage, const_data, "icb",
    nullptr, llvm::GlobalValue::NotThreadLocal, 2
  );
  icb->setAlignment(llvm::Align(4));
  resource_map.icb = icb;
  resource_map.icb_float = builder.CreateBitCast(
    resource_map.icb, llvm::ArrayType::get(
                        types._float4, shader_info->immConstantBufferData.size()
                      )
                        ->getPointerTo(2)
  );
}

void setup_tgsm(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module
) {
  for (auto &[id, tgsm] : shader_info->tgsmMap) {
    auto type = llvm::ArrayType::get(types._int, tgsm.size_in_uint);
    llvm::GlobalVariable *tgsm_h = new llvm::GlobalVariable(
      module, type, false, llvm::GlobalValue::InternalLinkage,
      llvm::UndefValue::get(type), "g" + std::to_string(id), nullptr,
      llvm::GlobalValue::NotThreadLocal, 3
    );
    tgsm_h->setAlignment(llvm::Align(4));
    resource_map.tgsm_map[id] = {tgsm.structured ? tgsm.stride : 0, tgsm_h};
  }
}

void setup_temp_register(
  const ShaderInfo *shader_info, io_binding_map &resource_map,
  air::AirType &types, llvm::Module &module, llvm::IRBuilder<> &builder
) {
  resource_map.temp.ptr_int4 = builder.CreateAlloca(
    llvm::ArrayType::get(types._int4, shader_info->tempRegisterCount)
  );
  resource_map.temp.ptr_float4 = builder.CreateBitCast(
    resource_map.temp.ptr_int4,
    llvm::ArrayType::get(types._float4, shader_info->tempRegisterCount)
      ->getPointerTo()
  );
  for (auto &phase : shader_info->phases) {
    resource_map.phases.push_back({});
    auto &phase_temp = resource_map.phases.back();

    phase_temp.temp.ptr_int4 = builder.CreateAlloca(
      llvm::ArrayType::get(types._int4, phase.tempRegisterCount)
    );
    phase_temp.temp.ptr_float4 = builder.CreateBitCast(
      phase_temp.temp.ptr_int4,
      llvm::ArrayType::get(types._float4, phase.tempRegisterCount)
        ->getPointerTo()
    );

    for (auto &[idx, info] : phase.indexableTempRegisterCounts) {
      auto &[numRegisters, mask] = info;
      auto channel_count = std::bit_width(mask);
      auto ptr_int_vec = builder.CreateAlloca(llvm::ArrayType::get(
        llvm::FixedVectorType::get(types._int, channel_count), numRegisters
      ));
      auto ptr_float_vec = builder.CreateBitCast(
        ptr_int_vec,
        llvm::ArrayType::get(
          llvm::FixedVectorType::get(types._float, channel_count), numRegisters
        )
          ->getPointerTo()
      );
      phase_temp.indexable_temp_map[idx] = {
        ptr_int_vec, ptr_float_vec, (uint32_t)channel_count
      };
    }
  }
  for (auto &[idx, info] : shader_info->indexableTempRegisterCounts) {
    auto &[numRegisters, mask] = info;
    auto channel_count = std::bit_width(mask);
    auto ptr_int_vec = builder.CreateAlloca(llvm::ArrayType::get(
      llvm::FixedVectorType::get(types._int, channel_count), numRegisters
    ));
    auto ptr_float_vec = builder.CreateBitCast(
      ptr_int_vec,
      llvm::ArrayType::get(
        llvm::FixedVectorType::get(types._float, channel_count), numRegisters
      )
        ->getPointerTo()
    );
    resource_map.indexable_temp_map[idx] = {
      ptr_int_vec, ptr_float_vec, (uint32_t)channel_count
    };
  }
  if (shader_info->use_cmp_exch) {
    resource_map.cmp_exch_temp = builder.CreateAlloca(types._int);
  }
}

void setup_fastmath_flag(llvm::Module &module, llvm::IRBuilder<> &builder) {
  if (auto options = module.getNamedMetadata("air.compile_options")) {
    for (auto operand : options->operands()) {
      if (isa<llvm::MDTuple>(operand) &&
          cast<llvm::MDTuple>(operand)->getNumOperands() == 1 &&
          isa<llvm::MDString>(cast<llvm::MDTuple>(operand)->getOperand(0)) &&
          cast<llvm::MDString>(cast<llvm::MDTuple>(operand)->getOperand(0))
              ->getString()
              .compare("air.compile.fast_math_enable") == 0) {
        builder.getFastMathFlags().setFast(true);
      }
    }
  }
}

llvm::Error convert_dxbc_hull_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  SM50ShaderInternal *pVertexStage, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  uint32_t vertex_max_output_register = 0;
  for (auto &out : pVertexStage->output_signature) {
    vertex_max_output_register =
      std::max(vertex_max_output_register, out.reg() + 1);
  }
  uint32_t max_output_register = pShaderInternal->max_output_register;
  uint32_t max_patch_constant_output_register =
    pShaderInternal->max_patch_constant_output_register;
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    // case SM50_SHADER_DEBUG_IDENTITY:
    //   debug_id = ((SM50_SHADER_DEBUG_IDENTITY_DATA *)arg)->id;
    //   break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }
  }

  setup_binding_table(shader_info, resource_map, func_signature, module);
  setup_tgsm(shader_info, resource_map, types, module);

  uint32_t payload_idx =
    func_signature.DefineInput(air::InputPayload{.size = 16256});
  uint32_t domain_cp_buffer_idx =
    func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 20,
      .array_size = 0,
      .memory_access = air::MemoryAccess::write,
      .address_space = air::AddressSpace::device,
      .type = air::msl_int4,
      .arg_name = "hull_cp_buffer",
      .raster_order_group = {}
    });
  uint32_t domain_pc_buffer_idx =
    func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 21,
      .array_size = 0,
      .memory_access = air::MemoryAccess::write,
      .address_space = air::AddressSpace::device,
      .type = air::MSLUint{},
      .arg_name = "hull_pc_buffer",
      .raster_order_group = {}
    });
  uint32_t domain_tessfactor_buffer_idx =
    func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 22,
      .array_size = 0,
      .memory_access = air::MemoryAccess::write,
      .address_space = air::AddressSpace::device,
      .type = air::MSLHalf{},
      .arg_name = "hull_tess_factor_buffer",
      .raster_order_group = {}
    });
  uint32_t thread_id_in_group_idx =
    func_signature.DefineInput(air::InputThreadPositionInThreadgroup{});

  auto [function, function_metadata] =
    func_signature.CreateFunction(name, context, module, 0, false);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);

  setup_fastmath_flag(module, builder);

  auto thread_position_in_group = function->getArg(thread_id_in_group_idx);
  auto thread_id_in_patch =
    builder.CreateExtractElement(thread_position_in_group, (uint32_t)0);
  auto patch_offset_in_group =
    builder.CreateExtractElement(thread_position_in_group, 1);

  assert(pShaderInternal->input_control_point_count != ~0u);
  auto input_control_point_ptr = builder.CreateConstInBoundsGEP1_32(
    types._int, function->getArg(payload_idx), 4
  );
  auto input_ptr_int4_type = llvm::ArrayType::get(
    types._int4, vertex_max_output_register *
                   pShaderInternal->input_control_point_count
  );
  resource_map.input.ptr_int4 = builder.CreateBitCast(
    input_control_point_ptr,
    input_ptr_int4_type->getPointerTo((uint32_t)air::AddressSpace::object_data)
  );
  resource_map.input.ptr_int4 = builder.CreateGEP(
    input_ptr_int4_type, resource_map.input.ptr_int4, {patch_offset_in_group}
  );
  auto input_ptr_float4_type = llvm::ArrayType::get(
    types._float4, vertex_max_output_register *
                     pShaderInternal->input_control_point_count
  );
  resource_map.input.ptr_float4 = builder.CreateBitCast(
    input_control_point_ptr, input_ptr_float4_type->getPointerTo((uint32_t
                             )air::AddressSpace::object_data)
  );
  resource_map.input.ptr_float4 = builder.CreateGEP(
    input_ptr_float4_type, resource_map.input.ptr_float4,
    {patch_offset_in_group}
  );
  resource_map.input_element_count = vertex_max_output_register;

  resource_map.instance_id = builder.CreateLoad(
    types._int, builder.CreateConstInBoundsGEP1_32(
                  types._int, function->getArg(payload_idx), 0
                )
  );

  auto batched_patch_start = builder.CreateLoad(
    types._int, builder.CreateConstInBoundsGEP1_32(
                  types._int, function->getArg(payload_idx), 1
                )
  );

  auto patch_count = builder.CreateLoad(
    types._int, builder.CreateConstInBoundsGEP1_32(
                  types._int, function->getArg(payload_idx), 2
                )
  );

  resource_map.patch_id =
    builder.CreateAdd(batched_patch_start, patch_offset_in_group);

  resource_map.instanced_patch_id = builder.CreateAdd(
    builder.CreateMul(patch_count, resource_map.instance_id),
    resource_map.patch_id
  );

  resource_map.control_point_buffer = function->getArg(domain_cp_buffer_idx);
  resource_map.patch_constant_buffer = function->getArg(domain_pc_buffer_idx);
  resource_map.tess_factor_buffer =
    function->getArg(domain_tessfactor_buffer_idx);
  resource_map.thread_id_in_patch = builder.CreateSelect(
    builder.CreateICmp(
      llvm::CmpInst::ICMP_ULT, resource_map.patch_id, patch_count
    ),
    thread_id_in_patch, builder.getInt32(32) // so all phases are effectively skipped
  );


  uint32_t threads_per_patch =
    next_pow2(pShaderInternal->hull_maximum_threads_per_patch);

  uint32_t patch_per_group =
    next_pow2(32 / threads_per_patch);

  if (shader_info->no_control_point_phase_passthrough) {
    assert(pShaderInternal->output_control_point_count != ~0u);

    auto control_point_output_size =
      max_output_register * pShaderInternal->output_control_point_count;
    auto type = llvm::ArrayType::get(types._int4, control_point_output_size);
    if (shader_info->output_control_point_read) {
      auto type_all_patch = llvm::ArrayType::get(types._int4, control_point_output_size * patch_per_group);
      llvm::GlobalVariable *control_point_phase_out = new llvm::GlobalVariable(
        module, type_all_patch, false, llvm::GlobalValue::InternalLinkage,
        llvm::UndefValue::get(type_all_patch), "hull_control_point_phase_out", nullptr,
        llvm::GlobalValue::NotThreadLocal,
        (uint32_t)air::AddressSpace::threadgroup
      );
      control_point_phase_out->setAlignment(llvm::Align(4));
      resource_map.output.ptr_int4 = builder.CreateGEP(
        type,
        builder.CreateBitCast(control_point_phase_out, type->getPointerTo(3)),
        {patch_offset_in_group}
      );
    } else {
      resource_map.output.ptr_int4 = builder.CreateGEP(
        type,
        builder.CreateBitCast(
          resource_map.control_point_buffer,
          type->getPointerTo(cast<llvm::PointerType>(
                               resource_map.control_point_buffer->getType()
          )
                               ->getPointerAddressSpace())
        ),
        {resource_map.instanced_patch_id}
      );
    }
    resource_map.output.ptr_float4 = builder.CreateBitCast(
      resource_map.output.ptr_int4,
      llvm::ArrayType::get(types._float4, control_point_output_size)
        ->getPointerTo(
          cast<llvm::PointerType>(resource_map.output.ptr_int4->getType())
            ->getPointerAddressSpace()
        )
    );
    resource_map.output_element_count = max_output_register;
  } else {
    /* since no control point phase */
    if (vertex_max_output_register != max_output_register) {
      return llvm::make_error<UnsupportedFeature>(
        "Hull shader has control point phase pass-through, but vertex shader "
        "output signature doesn't match hull shader input signature."
      );
    }
    resource_map.output.ptr_float4 = resource_map.input.ptr_float4;
    resource_map.output.ptr_int4 = resource_map.input.ptr_int4;
    resource_map.output_element_count = resource_map.input_element_count;
  }

  if (max_patch_constant_output_register) {
    /* all instances write to the same output (threadgroup memory) */
    auto type_all_pc =
      llvm::ArrayType::get(types._int4, max_patch_constant_output_register * patch_per_group);
    auto type =
      llvm::ArrayType::get(types._int4, max_patch_constant_output_register);
    llvm::GlobalVariable *patch_constant_out = new llvm::GlobalVariable(
      module, type_all_pc, false, llvm::GlobalValue::InternalLinkage,
      llvm::UndefValue::get(type_all_pc), "hull_patch_constant_out", nullptr,
      llvm::GlobalValue::NotThreadLocal,
      (uint32_t)air::AddressSpace::threadgroup
    );
    patch_constant_out->setAlignment(llvm::Align(4));
    resource_map.patch_constant_output.ptr_int4 = builder.CreateGEP(
      type, builder.CreateBitCast(patch_constant_out, type->getPointerTo(3)),
      {patch_offset_in_group}
    );
    resource_map.patch_constant_output.ptr_float4 = builder.CreateBitCast(
      resource_map.patch_constant_output.ptr_int4,
      llvm::ArrayType::get(types._float4, max_patch_constant_output_register)
        ->getPointerTo((uint32_t)air::AddressSpace::threadgroup)
    );
  }
  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function,
    .resource = resource_map, .types = types, .pso_sample_mask = 0xffffffff,
    .shader_type = pShaderInternal->shader_type,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry =
    convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);

  /* populate patch constant output */

  auto write_patch_constant =
    llvm::BasicBlock::Create(context, "write_patch_constant", function);
  auto real_return = llvm::BasicBlock::Create(context, "real_return", function);
  builder.CreateCondBr(
    builder.CreateICmp(
      llvm::CmpInst::ICMP_EQ, resource_map.thread_id_in_patch,
      builder.getInt32(0)
    ),
    write_patch_constant, real_return
  );

  builder.SetInsertPoint(write_patch_constant);

  if (auto err = epilogue.build(ctx).takeError()) {
    return err;
  }

  auto pc_out = builder.CreateGEP(
    types._int, resource_map.patch_constant_buffer,
    {builder.CreateMul(
      builder.getInt32(pShaderInternal->patch_constant_scalars.size()),
      resource_map.instanced_patch_id
    )}
  );
  for (unsigned i = 0; i < pShaderInternal->patch_constant_scalars.size();
       i++) {
    auto pc_scalar = pShaderInternal->patch_constant_scalars[i];
    auto dst_ptr = builder.CreateConstGEP1_32(types._int, pc_out, i);
    auto src_ptr = builder.CreateGEP(
      resource_map.patch_constant_output.ptr_int4->getType()
        ->getNonOpaquePointerElementType(),
      resource_map.patch_constant_output.ptr_int4,
      {builder.getInt32(0), builder.getInt32(pc_scalar.reg),
       builder.getInt32(pc_scalar.component)}
    );
    builder.CreateStore(builder.CreateLoad(types._int, src_ptr), dst_ptr);
  };

  builder.CreateBr(real_return);
  builder.SetInsertPoint(real_return);

  builder.CreateRetVoid();

  module.getOrInsertNamedMetadata("air.mesh")->addOperand(function_metadata);

  return llvm::Error::success();
}

llvm::Error convert_dxbc_domain_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  SM50ShaderInternal *pHullStage, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  uint32_t max_input_register = pShaderInternal->max_input_register;
  uint32_t max_output_register = pShaderInternal->max_output_register;
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  MTL_GEOMETRY_SHADER_PASS_THROUGH *gs_passthrough = nullptr;
  bool rasterization_disabled = false;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    case SM50_SHADER_DEBUG_IDENTITY:
      // debug_id = ((SM50_SHADER_DEBUG_IDENTITY_DATA *)arg)->id;
      break;
    case SM50_SHADER_GS_PASS_THROUGH:
      gs_passthrough = &((SM50_SHADER_GS_PASS_THROUGH_DATA *)arg)->Data;
      rasterization_disabled = ((SM50_SHADER_GS_PASS_THROUGH_DATA *)arg)->RasterizationDisabled;
      break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    sig_ctx.skip_vertex_output = rasterization_disabled;
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }

    for (auto& out : pShaderInternal->output_signature) {
      if(out.isSystemValue()) continue;
      func_signature.DefineOutput(air::OutputVertex{
        .user = out.fullSemanticString(),
        .type = to_msl_type(out.componentType()),
      });
    }
  }

  uint32_t clip_distance_out_idx = ~0u;
  if (pShaderInternal->clip_distance_scalars.size()) {
    clip_distance_out_idx = func_signature.DefineOutput(
      air::OutputClipDistance{pShaderInternal->clip_distance_scalars.size()}
    );
  };

  setup_binding_table(shader_info, resource_map, func_signature, module);
  setup_tgsm(shader_info, resource_map, types, module);
  uint32_t patch_id_idx = func_signature.DefineInput(air::InputPatchID{});
  uint32_t domain_cp_buffer_idx =
    func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 20,
      .array_size = 0,
      .memory_access = air::MemoryAccess::read,
      .address_space = air::AddressSpace::device,
      .type = air::msl_int4,
      .arg_name = "domain_cp_buffer",
      .raster_order_group = {}
    });
  uint32_t domain_pc_buffer_idx =
    func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 21,
      .array_size = 0,
      .memory_access = air::MemoryAccess::read,
      .address_space = air::AddressSpace::device,
      .type = air::MSLUint{},
      .arg_name = "domain_pc_buffer",
      .raster_order_group = {}
    });
  uint32_t domain_tessfactor_buffer_idx =
    func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 22,
      .array_size = 0,
      .memory_access = air::MemoryAccess::read,
      .address_space = air::AddressSpace::device,
      .type = air::MSLHalf{},
      .arg_name = "domain_tess_factor_buffer",
      .raster_order_group = {}
    });
  uint32_t draw_vertex_count_idx =
  func_signature.DefineInput(air::ArgumentBindingBuffer{
    .buffer_size = {},
    .location_index = 23,
    .array_size = 0,
    .memory_access = air::MemoryAccess::read,
    .address_space = air::AddressSpace::constant,
    .type = air::MSLUint{},
    .arg_name = "draw_vertex_count",
    .raster_order_group = {}
  });

  uint32_t rta_idx_out = ~0u;
  if (gs_passthrough && gs_passthrough->RenderTargetArrayIndexReg != 255) {
    rta_idx_out =
      func_signature.DefineOutput(air::OutputRenderTargetArrayIndex{});
  }
  uint32_t va_idx_out = ~0u;
  if (gs_passthrough && gs_passthrough->ViewportArrayIndexReg != 255) {
    va_idx_out = func_signature.DefineOutput(air::OutputViewportArrayIndex{});
  }

  auto [function, function_metadata] =
    func_signature.CreateFunction(name, context, module, 0, rasterization_disabled);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);

  setup_fastmath_flag(module, builder);

  auto vertex_count = builder.CreateLoad(
    types._int, function->getArg(draw_vertex_count_idx)
  );

  auto patch_count = builder.CreateUDiv(
    vertex_count, builder.getInt32(pHullStage->input_control_point_count)
  );

  resource_map.instanced_patch_id = function->getArg(patch_id_idx);
  resource_map.patch_id = builder.CreateURem(resource_map.instanced_patch_id, patch_count);
  resource_map.control_point_buffer = function->getArg(domain_cp_buffer_idx);
  resource_map.patch_constant_buffer = function->getArg(domain_pc_buffer_idx);
  resource_map.tess_factor_buffer =
    function->getArg(domain_tessfactor_buffer_idx);

  auto control_point_input_array_type_float4 = llvm::ArrayType::get(
    types._float4,
    pHullStage->max_output_register * pShaderInternal->input_control_point_count
  );
  resource_map.input.ptr_float4 = builder.CreateGEP(
    control_point_input_array_type_float4,
    builder.CreateBitCast(
      resource_map.control_point_buffer,
      control_point_input_array_type_float4->getPointerTo((uint32_t
      )air::AddressSpace::device)
    ),
    {resource_map.instanced_patch_id}
  );
  auto control_point_input_array_type_int4 = llvm::ArrayType::get(
    types._int4,
    pHullStage->max_output_register * pShaderInternal->input_control_point_count
  );
  resource_map.input.ptr_int4 = builder.CreateGEP(
    control_point_input_array_type_int4,
    builder.CreateBitCast(
      resource_map.control_point_buffer,
      control_point_input_array_type_int4->getPointerTo((uint32_t
      )air::AddressSpace::device)
    ),
    {resource_map.instanced_patch_id}
  );
  resource_map.input_element_count = pHullStage->max_output_register;

  resource_map.patch_constant_output.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_input_register));
  resource_map.patch_constant_output.ptr_float4 = builder.CreateBitCast(
    resource_map.patch_constant_output.ptr_int4,
    llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
  );

  resource_map.output.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_output_register)
    );
  resource_map.output.ptr_float4 = builder.CreateBitCast(
    resource_map.output.ptr_int4,
    llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo()
  );
  resource_map.output_element_count = max_output_register;

  /* setup patch constant register */
  for (auto x : llvm::enumerate(pHullStage->patch_constant_scalars)) {
    auto src_ptr = builder.CreateGEP(
      types._int, resource_map.patch_constant_buffer,
      {builder.CreateAdd(
        builder.CreateMul(
          resource_map.instanced_patch_id,
          builder.getInt32(pHullStage->patch_constant_scalars.size())
        ),
        builder.getInt32(x.index())
      )}
    );
    auto dst_ptr = builder.CreateGEP(
      llvm::ArrayType::get(types._int4, max_input_register),
      resource_map.patch_constant_output.ptr_int4,
      {builder.getInt32(0), builder.getInt32(x.value().reg),
       builder.getInt32(x.value().component)}
    );
    builder.CreateStore(builder.CreateLoad(types._int, src_ptr), dst_ptr);
  }

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function,
    .resource = resource_map, .types = types, .pso_sample_mask = 0xffffffff,
    .shader_type = pShaderInternal->shader_type,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry =
    convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);
  auto epilogue_result = epilogue.build(ctx);
  if (auto err = epilogue_result.takeError()) {
    return err;
  }
  auto value = epilogue_result.get();

  if (pShaderInternal->clip_distance_scalars.size()) {
    auto clip_distance_ty = llvm::ArrayType::get(
      types._float, pShaderInternal->clip_distance_scalars.size()
    );
    pvalue clip_distance_array = llvm::ConstantAggregateZero::get(clip_distance_ty);
    unsigned clip_distance_idx = 0;
    for (auto &scalar : pShaderInternal->clip_distance_scalars) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_float4->getType()
          ->getNonOpaquePointerElementType(),
        resource_map.output.ptr_float4,
        {builder.getInt32(0), builder.getInt32(scalar.reg),
         builder.getInt32(scalar.component)}
      );
      clip_distance_array = builder.CreateInsertValue(
        clip_distance_array, builder.CreateLoad(types._float, src_ptr), {clip_distance_idx++}
      );
    }
    value = builder.CreateInsertValue(
      value, clip_distance_array, {clip_distance_out_idx}
    );
  };

  if (value == nullptr) {
    builder.CreateRetVoid();
  } else {
    if (rta_idx_out != ~0u) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(
        ),
        resource_map.output.ptr_int4,
        {builder.getInt32(0),
         builder.getInt32(gs_passthrough->RenderTargetArrayIndexReg),
         builder.getInt32(gs_passthrough->RenderTargetArrayIndexComponent)}
      );
      value = builder.CreateInsertValue(
        value, builder.CreateLoad(types._int, src_ptr), {rta_idx_out}
      );
    }
    if (va_idx_out != ~0u) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(
        ),
        resource_map.output.ptr_int4,
        {builder.getInt32(0),
         builder.getInt32(gs_passthrough->ViewportArrayIndexReg),
         builder.getInt32(gs_passthrough->ViewportArrayIndexComponent)}
      );
      value = builder.CreateInsertValue(
        value, builder.CreateLoad(types._int, src_ptr), {va_idx_out}
      );
    }
    builder.CreateRet(value);
  }

  module.getOrInsertNamedMetadata("air.vertex")->addOperand(function_metadata);

  return llvm::Error::success();
};

llvm::Error convert_dxbc_pixel_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  llvm::LLVMContext &context, llvm::Module &module,
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  uint32_t max_input_register = pShaderInternal->max_input_register;
  uint32_t max_output_register = pShaderInternal->max_output_register;
  uint32_t pso_sample_mask = 0xffffffff;
  bool pso_dual_source_blending = false;
  bool pso_disable_depth_output = false;
  uint32_t pso_unorm_output_reg_mask = 0;
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    case SM50_SHADER_DEBUG_IDENTITY:
      // debug_id = ((SM50_SHADER_DEBUG_IDENTITY_DATA *)arg)->id;
      break;
    case SM50_SHADER_PSO_PIXEL_SHADER:
      pso_sample_mask = ((SM50_SHADER_PSO_PIXEL_SHADER_DATA *)arg)->sample_mask;
      pso_dual_source_blending =
        ((SM50_SHADER_PSO_PIXEL_SHADER_DATA *)arg)->dual_source_blending;
      pso_disable_depth_output =
        ((SM50_SHADER_PSO_PIXEL_SHADER_DATA *)arg)->disable_depth_output;
      pso_unorm_output_reg_mask =
        ((SM50_SHADER_PSO_PIXEL_SHADER_DATA *)arg)->unorm_output_reg_mask;
      break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    sig_ctx.dual_source_blending = pso_dual_source_blending;
    sig_ctx.disable_depth_output = pso_disable_depth_output;
    sig_ctx.pull_mode_reg_mask = shader_info->pull_mode_reg_mask;
    sig_ctx.unorm_output_reg_mask = pso_unorm_output_reg_mask;
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }
  }
  if (pso_sample_mask != 0xffffffff) {
    auto assigned_index =
      func_signature.DefineOutput(air::OutputCoverageMask{});
    epilogue >> [=](pvalue value) -> IRValue {
      return make_irvalue([=](struct context ctx) {
        auto &builder = ctx.builder;
        if (ctx.resource.coverage_mask_reg)
          return value;
        return builder.CreateInsertValue(
          value, builder.getInt32(ctx.pso_sample_mask), {assigned_index}
        );
      });
    };
  }

  setup_binding_table(shader_info, resource_map, func_signature, module);
  setup_tgsm(shader_info, resource_map, types, module);

  auto [function, function_metadata] =
    func_signature.CreateFunction(name, context, module, 0, false);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);

  setup_fastmath_flag(module, builder);

  resource_map.input.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_input_register));
  resource_map.input.ptr_float4 = builder.CreateBitCast(
    resource_map.input.ptr_int4,
    llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
  );
  resource_map.input_element_count = max_input_register;
  resource_map.output.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_output_register)
    );
  resource_map.output.ptr_float4 = builder.CreateBitCast(
    resource_map.output.ptr_int4,
    llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo()
  );
  resource_map.output_element_count = max_output_register;

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function,
    .resource = resource_map, .types = types,
    .pso_sample_mask = pso_sample_mask,
    .shader_type = pShaderInternal->shader_type,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry =
    convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);
  auto epilogue_result = epilogue.build(ctx);
  if (auto err = epilogue_result.takeError()) {
    return err;
  }
  auto value = epilogue_result.get();
  if (value == nullptr) {
    builder.CreateRetVoid();
  } else {
    builder.CreateRet(value);
  }

  module.getOrInsertNamedMetadata("air.fragment")
    ->addOperand(function_metadata);

  return llvm::Error::success();
};

llvm::Error convert_dxbc_compute_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  llvm::LLVMContext &context, llvm::Module &module,
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    case SM50_SHADER_DEBUG_IDENTITY:
      // debug_id = ((SM50_SHADER_DEBUG_IDENTITY_DATA *)arg)->id;
      break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }
  }

  setup_binding_table(shader_info, resource_map, func_signature, module);
  setup_tgsm(shader_info, resource_map, types, module);

  auto [function, function_metadata] =
    func_signature.CreateFunction(name, context, module, 0, false);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);

  setup_fastmath_flag(module, builder);
  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function,
    .resource = resource_map, .types = types, .pso_sample_mask = 0xffffffff,
    .shader_type = pShaderInternal->shader_type,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry =
    convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);

  if (auto err = epilogue.build(ctx).takeError()) {
    return err;
  }
  builder.CreateRetVoid();

  module.getOrInsertNamedMetadata("air.kernel")->addOperand(function_metadata);

  return llvm::Error::success();
};

llvm::Error convert_dxbc_vertex_shader(
  SM50ShaderInternal *pShaderInternal, const char *name,
  llvm::LLVMContext &context, llvm::Module &module,
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);
  auto shader_type = pShaderInternal->shader_type;

  uint32_t max_input_register = pShaderInternal->max_input_register;
  uint32_t max_output_register = pShaderInternal->max_output_register;
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *vertex_so = nullptr;
  SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia_layout = nullptr;
  MTL_GEOMETRY_SHADER_PASS_THROUGH *gs_passthrough = nullptr;
  bool rasterization_disabled = false;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    case SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT:
      if (shader_type != microsoft::D3D10_SB_VERTEX_SHADER)
        break;
      vertex_so = (SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA *)arg;
      break;
    case SM50_SHADER_DEBUG_IDENTITY:
      // debug_id = ((SM50_SHADER_DEBUG_IDENTITY_DATA *)arg)->id;
      break;
    case SM50_SHADER_IA_INPUT_LAYOUT:
      ia_layout = ((SM50_SHADER_IA_INPUT_LAYOUT_DATA *)arg);
      break;
    case SM50_SHADER_GS_PASS_THROUGH:
      gs_passthrough = &((SM50_SHADER_GS_PASS_THROUGH_DATA *)arg)->Data;
      rasterization_disabled = ((SM50_SHADER_GS_PASS_THROUGH_DATA *)arg)->RasterizationDisabled;
      break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }
  rasterization_disabled = rasterization_disabled || (vertex_so != nullptr);

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    sig_ctx.ia_layout = ia_layout;
    sig_ctx.skip_vertex_output = rasterization_disabled;
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }

    for (auto& out : pShaderInternal->output_signature) {
      if(out.isSystemValue()) continue;
      func_signature.DefineOutput(air::OutputVertex{
        .user = out.fullSemanticString(),
        .type = to_msl_type(out.componentType()),
      });
    }
  }
  if (vertex_so) {
    auto bv = func_signature.DefineInput(air::InputBaseVertex{});
    auto vid = func_signature.DefineInput(air::InputVertexID{});
    auto slot_0 = func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 20,
      .array_size = 0,
      .memory_access = air::MemoryAccess::write,
      .address_space = air::AddressSpace::device,
      .type = air::MSLUint{},
      .arg_name = "so_slot0",
      .raster_order_group = {}
    });
    epilogue << make_irvalue([=](struct context ctx) {
      auto &builder = ctx.builder;
      auto base_vertex = ctx.function->getArg(bv);
      auto vertex_id = ctx.function->getArg(vid);
      auto slot0 = ctx.function->getArg(slot_0);
      auto adjusted_vertex_id = builder.CreateSub(vertex_id, base_vertex);
      auto output_regs = builder.CreateBitOrPointerCast(
        ctx.resource.output.ptr_int4, llvm::PointerType::get(ctx.types._int, 0)
      );
      for (unsigned i = 0; i < vertex_so->num_elements; i++) {
        auto &element = vertex_so->elements[i];
        if (element.reg_id == 0xffffffff)
          continue;
        auto ptr = ctx.builder.CreateConstGEP1_32(
          ctx.types._int, output_regs,
          (unsigned)(element.reg_id * 4 + element.component)
        );
        auto target_offset = ctx.builder.CreateAdd(
          ctx.builder.CreateMul(
            adjusted_vertex_id,
            ctx.builder.getInt32(vertex_so->strides[element.output_slot])
          ),
          ctx.builder.getInt32(element.offset)
        );
        auto target_ptr = ctx.builder.CreateGEP(
          ctx.types._int, slot0, {ctx.builder.CreateLShr(target_offset, 2)}
        );
        ctx.builder.CreateStore(
          ctx.builder.CreateLoad(ctx.types._int, ptr), target_ptr, true
        );
      }
      return nullptr;
    });
  }

  uint32_t vertex_idx = func_signature.DefineInput(air::InputVertexID{});
  uint32_t base_vertex_idx = func_signature.DefineInput(air::InputBaseVertex{});
  uint32_t instance_idx = func_signature.DefineInput(air::InputInstanceID{});
  uint32_t base_instance_idx =
    func_signature.DefineInput(air::InputBaseInstance{});

  uint32_t clip_distance_out_idx = ~0u;
  if (pShaderInternal->clip_distance_scalars.size()) {
    clip_distance_out_idx = func_signature.DefineOutput(
      air::OutputClipDistance{pShaderInternal->clip_distance_scalars.size()}
    );
  };

  setup_binding_table(shader_info, resource_map, func_signature, module);
  setup_tgsm(shader_info, resource_map, types, module);

  uint32_t rta_idx_out = ~0u;
  if (gs_passthrough && gs_passthrough->RenderTargetArrayIndexReg != 255) {
    rta_idx_out =
      func_signature.DefineOutput(air::OutputRenderTargetArrayIndex{});
  }
  uint32_t va_idx_out = ~0u;
  if (gs_passthrough && gs_passthrough->ViewportArrayIndexReg != 255) {
    va_idx_out = func_signature.DefineOutput(air::OutputViewportArrayIndex{});
  }

  auto [function, function_metadata] = func_signature.CreateFunction(
    name, context, module, 0,
    rasterization_disabled
  );

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);

  setup_fastmath_flag(module, builder);

  resource_map.vertex_id_with_base = function->getArg(vertex_idx);
  resource_map.base_vertex_id = function->getArg(base_vertex_idx);
  resource_map.instance_id_with_base = function->getArg(instance_idx);
  resource_map.base_instance_id = function->getArg(base_instance_idx);
  resource_map.vertex_id = builder.CreateSub(
    resource_map.vertex_id_with_base, resource_map.base_vertex_id
  );
  resource_map.instance_id = builder.CreateSub(
    resource_map.instance_id_with_base, resource_map.base_instance_id
  );

  resource_map.input.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_input_register));
  resource_map.input.ptr_float4 = builder.CreateBitCast(
    resource_map.input.ptr_int4,
    llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
  );
  resource_map.input_element_count = max_input_register;
  resource_map.output.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_output_register)
    );
  resource_map.output.ptr_float4 = builder.CreateBitCast(
    resource_map.output.ptr_int4,
    llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo()
  );
  resource_map.output_element_count = max_output_register;

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function,
    .resource = resource_map, .types = types, .pso_sample_mask = 0xffffffff,
    .shader_type = pShaderInternal->shader_type,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry =
    convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);
  auto epilogue_result = epilogue.build(ctx);
  if (auto err = epilogue_result.takeError()) {
    return err;
  }
  auto value = epilogue_result.get();

  if (pShaderInternal->clip_distance_scalars.size()) {
    auto clip_distance_ty = llvm::ArrayType::get(
      types._float, pShaderInternal->clip_distance_scalars.size()
    );
    pvalue clip_distance_array = llvm::ConstantAggregateZero::get(clip_distance_ty);
    unsigned clip_distance_idx = 0;
    for (auto &scalar : pShaderInternal->clip_distance_scalars) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_float4->getType()
          ->getNonOpaquePointerElementType(),
        resource_map.output.ptr_float4,
        {builder.getInt32(0), builder.getInt32(scalar.reg),
         builder.getInt32(scalar.component)}
      );
      clip_distance_array = builder.CreateInsertValue(
        clip_distance_array, builder.CreateLoad(types._float, src_ptr), {clip_distance_idx++}
      );
    }
    value = builder.CreateInsertValue(
      value, clip_distance_array, {clip_distance_out_idx}
    );
  };

  if (value == nullptr) {
    builder.CreateRetVoid();
  } else {
    if (rta_idx_out != ~0u) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(
        ),
        resource_map.output.ptr_int4,
        {builder.getInt32(0),
         builder.getInt32(gs_passthrough->RenderTargetArrayIndexReg),
         builder.getInt32(gs_passthrough->RenderTargetArrayIndexComponent)}
      );
      value = builder.CreateInsertValue(
        value, builder.CreateLoad(types._int, src_ptr), {rta_idx_out}
      );
    }
    if (va_idx_out != ~0u) {
      auto src_ptr = builder.CreateGEP(
        resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(
        ),
        resource_map.output.ptr_int4,
        {builder.getInt32(0),
         builder.getInt32(gs_passthrough->ViewportArrayIndexReg),
         builder.getInt32(gs_passthrough->ViewportArrayIndexComponent)}
      );
      value = builder.CreateInsertValue(
        value, builder.CreateLoad(types._int, src_ptr), {va_idx_out}
      );
    }
    builder.CreateRet(value);
  }

  module.getOrInsertNamedMetadata("air.vertex")->addOperand(function_metadata);
  return llvm::Error::success();
};

llvm::Error convert_dxbc_vertex_for_hull_shader(
  const SM50ShaderInternal *pShaderInternal, const char *name,
  const SM50ShaderInternal *pHullStage, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  uint32_t max_input_register = pShaderInternal->max_input_register;
  uint32_t max_output_register = 0;
  for (auto& out : pShaderInternal->output_signature) {
    max_output_register = std::max(max_output_register, out.reg() + 1);
  }
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia_layout = nullptr;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    case SM50_SHADER_DEBUG_IDENTITY:
      // debug_id = ((SM50_SHADER_DEBUG_IDENTITY_DATA *)arg)->id;
      break;
    case SM50_SHADER_IA_INPUT_LAYOUT:
      ia_layout = ((SM50_SHADER_IA_INPUT_LAYOUT_DATA *)arg);
      break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }

  bool is_indexed_draw = ia_layout && ia_layout->index_buffer_format > 0;

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::ConstantAggregateZero::get(retTy);
  });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    sig_ctx.ia_layout = ia_layout;
    sig_ctx.skip_vertex_output = true;
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }
  }

  setup_binding_table(shader_info, resource_map, func_signature, module);

  uint32_t threads_per_patch =
    next_pow2(pHullStage->hull_maximum_threads_per_patch);

  uint32_t payload_idx =
    func_signature.DefineInput(air::InputPayload{.size = 16256});
  uint32_t thread_id_idx =
    func_signature.DefineInput(air::InputThreadPositionInThreadgroup{});
  // (batched_patch_id_start, instance_id, 0)
  uint32_t tg_id_idx =
    func_signature.DefineInput(air::InputThreadgroupPositionInGrid{});
  uint32_t mesh_props_idx =
    func_signature.DefineInput(air::InputMeshGridProperties{});
  uint32_t draw_argument_idx =
    func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 21,
      .array_size = 0,
      .memory_access = air::MemoryAccess::read,
      .address_space = air::AddressSpace::constant,
      .type = is_indexed_draw //
          ? air::MSLWhateverStruct{"draw_indexed_arguments", types._dxmt_draw_indexed_arguments}
          : air::MSLWhateverStruct{"draw_arguments", types._dxmt_draw_arguments},
      .arg_name = "draw_arguments",
      .raster_order_group = {}
    });

  uint32_t index_buffer_idx = ~0u;
  if (is_indexed_draw) {
    index_buffer_idx = func_signature.DefineInput(air::ArgumentBindingBuffer{
      .buffer_size = {},
      .location_index = 20,
      .array_size = 0,
      .memory_access = air::MemoryAccess::read,
      .address_space = air::AddressSpace::device,
      .type = ia_layout->index_buffer_format == 1
                ? air::MSLRepresentableType(air::MSLUshort{})
                : air::MSLRepresentableType(air::MSLUint{}),
      .raster_order_group = {}
    });
  }

  auto [function, function_metadata] =
    func_signature.CreateFunction(name, context, module, 0, true);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);

  setup_fastmath_flag(module, builder);

  auto active = llvm::BasicBlock::Create(context, "active", function);
  auto dispatch = llvm::BasicBlock::Create(context, "dispatch", function);
  auto return_ = llvm::BasicBlock::Create(context, "return", function);

  auto thread_position_in_group = function->getArg(thread_id_idx);
  auto control_point_id_in_patch =
    builder.CreateExtractElement(thread_position_in_group, (uint32_t)0);
  auto patch_offset_in_group =
    builder.CreateExtractElement(thread_position_in_group, 1);

  auto threadgroup_position_in_grid = function->getArg(tg_id_idx);
  auto batched_patch_start = builder.CreateMul(
    builder.CreateExtractElement(threadgroup_position_in_grid, (uint32_t)0),
    builder.getInt32(32 / threads_per_patch)
  );
  auto patch_id = builder.CreateAdd(batched_patch_start, patch_offset_in_group);
  auto instance_id =
    builder.CreateExtractElement(threadgroup_position_in_grid, (uint32_t)1);
  auto control_point_index = builder.CreateAdd(
    builder.CreateMul(
      patch_id, builder.getInt32(pHullStage->input_control_point_count)
    ),
    control_point_id_in_patch
  );
  auto control_point_index_in_threadgroup = builder.CreateAdd(
    builder.CreateMul(
      patch_offset_in_group,
      builder.getInt32(pHullStage->input_control_point_count)
    ),
    control_point_id_in_patch
  );

  auto draw_arguments = builder.CreateLoad(
    is_indexed_draw ? types._dxmt_draw_indexed_arguments
                    : types._dxmt_draw_arguments,
    function->getArg(draw_argument_idx)
  );

  auto patch_count = builder.CreateUDiv(
    builder.CreateExtractValue(draw_arguments, 0),
    builder.getInt32(pHullStage->input_control_point_count)
  );

  resource_map.input.ptr_int4 =
    builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_input_register));
  resource_map.input.ptr_float4 = builder.CreateBitCast(
    resource_map.input.ptr_int4,
    llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
  );
  resource_map.input_element_count = max_input_register;

  auto payload_ptr = builder.CreateConstInBoundsGEP1_32(
    types._int, function->getArg(payload_idx), 4
  );
  resource_map.output.ptr_int4 = builder.CreateBitCast(
    payload_ptr, llvm::ArrayType::get(types._int4, max_output_register)
                   ->getPointerTo((uint32_t)air::AddressSpace::object_data)
  );
  resource_map.output.ptr_int4 = builder.CreateGEP(
    resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(),
    resource_map.output.ptr_int4, {control_point_index_in_threadgroup}
  );
  resource_map.output.ptr_float4 = builder.CreateBitCast(
    payload_ptr, llvm::ArrayType::get(types._float4, max_output_register)
                   ->getPointerTo((uint32_t)air::AddressSpace::object_data)
  );
  resource_map.output.ptr_float4 = builder.CreateGEP(
    resource_map.output.ptr_float4->getType()->getNonOpaquePointerElementType(),
    resource_map.output.ptr_float4, {control_point_index_in_threadgroup}
  );

  resource_map.output_element_count = max_output_register;

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(
    shader_info, resource_map, types, module, builder
  );

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function,
    .resource = resource_map, .types = types, .pso_sample_mask = 0xffffffff,
    .shader_type = pShaderInternal->shader_type,
  };

  builder.CreateCondBr(
    builder.CreateLogicalAnd(
      builder.CreateICmp(
        llvm::CmpInst::ICMP_ULT, control_point_id_in_patch,
        builder.getInt32(pHullStage->input_control_point_count)
      ),
      builder.CreateICmp(llvm::CmpInst::ICMP_ULT, patch_id, patch_count)
    ),
    active, return_
  );
  builder.SetInsertPoint(active);

  if (index_buffer_idx != ~0u) {
    auto start_index = builder.CreateExtractValue(draw_arguments, 2);
    auto index_buffer = function->getArg(index_buffer_idx);
    auto index_buffer_element_type =
      index_buffer->getType()->getNonOpaquePointerElementType();
    auto vertex_id = builder.CreateLoad(
      index_buffer_element_type,
      builder.CreateGEP(
        index_buffer_element_type, index_buffer,
        {builder.CreateAdd(start_index, control_point_index)}
      )
    );
    resource_map.vertex_id = builder.CreateZExt(vertex_id, types._int);
  } else {
    resource_map.vertex_id = control_point_index;
  }
  resource_map.base_vertex_id = builder.CreateExtractValue(draw_arguments, is_indexed_draw ? 3 : 2);
  resource_map.instance_id = instance_id;
  resource_map.vertex_id_with_base =
    builder.CreateAdd(resource_map.vertex_id, resource_map.base_vertex_id);
  resource_map.base_instance_id = builder.CreateExtractValue(draw_arguments, is_indexed_draw ? 4 : 3);
  resource_map.instance_id_with_base =
    builder.CreateAdd(resource_map.instance_id, resource_map.base_instance_id);

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }
  auto real_entry =
    convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);
  auto epilogue_result = epilogue.build(ctx);
  if (auto err = epilogue_result.takeError()) {
    return err;
  }

  builder.CreateCondBr(
    builder.CreateLogicalAnd(
      builder.CreateICmp(
        llvm::CmpInst::ICMP_EQ, control_point_id_in_patch, builder.getInt32(0)
      ),
      builder.CreateICmp(
        llvm::CmpInst::ICMP_EQ, patch_offset_in_group, builder.getInt32(0)
      )
    ),
    dispatch, return_
  );
  builder.SetInsertPoint(dispatch);

  builder.CreateStore(
    instance_id, builder.CreateConstInBoundsGEP1_32(
                   types._int, function->getArg(payload_idx), 0
                 )
  );

  builder.CreateStore(
    batched_patch_start, builder.CreateConstInBoundsGEP1_32(
                           types._int, function->getArg(payload_idx), 1
                         )
  );

  builder.CreateStore(
    patch_count, builder.CreateConstInBoundsGEP1_32(
                   types._int, function->getArg(payload_idx), 2
                 )
  );

  if (auto err =
        air::call_set_mesh_properties(
          ctx.builder.CreateBitCast(
            function->getArg(mesh_props_idx),
            types._mesh_grid_properties->getPointerTo(3)
          ),
          llvm::ConstantVector::getIntegerValue(types._int3, llvm::APInt{32, 1})
        )
          .build(
            {.llvm = context,
             .module = module,
             .builder = builder,
             .types = types}
          )
          .takeError()) {
    return err;
  }

  builder.CreateBr(return_);

  builder.SetInsertPoint(return_);

  builder.CreateRetVoid();

  module.getOrInsertNamedMetadata("air.object")->addOperand(function_metadata);
  return llvm::Error::success();
};

llvm::Error convertDXBC(
  sm50_shader_t pShader, const char *name, llvm::LLVMContext &context,
  llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  using namespace microsoft;

  auto pShaderInternal = (SM50ShaderInternal *)pShader;

  switch (pShaderInternal->shader_type) {
  case microsoft::D3D10_SB_PIXEL_SHADER:
    return convert_dxbc_pixel_shader(
      pShaderInternal, name, context, module, pArgs
    );
  case microsoft::D3D10_SB_VERTEX_SHADER:
    return convert_dxbc_vertex_shader(
      pShaderInternal, name, context, module, pArgs
    );
  case microsoft::D3D11_SB_COMPUTE_SHADER:
    return convert_dxbc_compute_shader(
      pShaderInternal, name, context, module, pArgs
    );
  case microsoft::D3D11_SB_HULL_SHADER:
  case microsoft::D3D11_SB_DOMAIN_SHADER:
    return llvm::make_error<UnsupportedFeature>(
      "Hull and domain shader cannot be independently converted."
    );
  case microsoft::D3D10_SB_GEOMETRY_SHADER:
  case microsoft::D3D12_SB_MESH_SHADER:
  case microsoft::D3D12_SB_AMPLIFICATION_SHADER:
  case microsoft::D3D11_SB_RESERVED0:
    break;
  }
  return llvm::make_error<UnsupportedFeature>("Not supported shader type");
};
} // namespace dxmt::dxbc

bool CheckGSBBIsPassThrough(dxmt::dxbc::BasicBlock *bb) {
  using namespace dxmt::dxbc;
  for (auto &inst : bb->instructions) {
    bool matched = std::visit(
      patterns{
        [](InstNop &) { return true; }, [](InstMov &mov) { return true; },
        [](InstEmit &) { return true; }, [](InstCut &) { return true; },
        [](auto &) { return false; }
      },
      inst
    );
    if (matched)
      continue;
    return matched;
  }

  return std::visit(
    patterns{
      [](dxmt::dxbc::BasicBlockReturn) { return true; },
      [](dxmt::dxbc::BasicBlockUnconditionalBranch uncond) {
        return CheckGSBBIsPassThrough(uncond.target.get());
      },
      [](auto) { return false; }
    },
    bb->target
  );
}

bool CheckGSSignatureIsPassThrough(
  microsoft::CSignatureParser &input, microsoft::CSignatureParser5 &output,
  MTL_GEOMETRY_SHADER_PASS_THROUGH &data
) {
  using namespace microsoft;

  data.RenderTargetArrayIndexComponent = 255;
  data.RenderTargetArrayIndexReg = 255;
  data.ViewportArrayIndexComponent = 255;
  data.ViewportArrayIndexReg = 255;

  if (output.NumStreams() > 1) {
    return false;
  }
  if (output.RasterizedStream()) {
    return false;
  }

  const D3D11_SIGNATURE_PARAMETER *input_parameters, *output_parameters;
  input.GetParameters(&input_parameters);
  output.Signature(0)->GetParameters(&output_parameters);
  size_t num_input, num_output;
  num_input = input.GetNumParameters();
  num_output = output.Signature(0)->GetNumParameters();
  if (num_output > num_input) {
    // at least it is a non-trival shader that generates new data
    return false;
  }
  bool has_match_record;
  for (unsigned i = 0; i < num_output; i++) {
    has_match_record = false;
    const D3D11_SIGNATURE_PARAMETER &out = output_parameters[i];
    for (unsigned j = 0; j < num_input; j++) {
      const D3D11_SIGNATURE_PARAMETER &in = input_parameters[j];
      if (out.Register == in.Register && (out.Mask & in.Mask) == out.Mask) {
        if (out.SystemValue == D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX) {
          data.RenderTargetArrayIndexReg = out.Register;
          data.RenderTargetArrayIndexComponent = __builtin_ctz(out.Mask);
        } else if (out.SystemValue == D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX) {
          data.ViewportArrayIndexReg = out.Register;
          data.ViewportArrayIndexComponent = __builtin_ctz(out.Mask);
        } else {
          if (out.SemanticIndex != in.SemanticIndex) {
            return false;
          }
          if (strcasecmp(out.SemanticName, in.SemanticName)) {
            return false;
          }
        }
        has_match_record = true;
        break;
      }
    }
    if (!has_match_record) {
      return false;
    }
  }

  return true;
};

AIRCONV_API int SM50Initialize(
  const void *pBytecode, size_t BytecodeSize, sm50_shader_t *ppShader,
  MTL_SHADER_REFLECTION *pRefl, sm50_error_t *ppError
) {
  using namespace microsoft;
  using namespace dxmt::dxbc;
  using namespace dxmt::air;
  using namespace dxmt::shader::common;
  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = new SM50ErrorInternal();
  llvm::raw_svector_ostream errorOut(errorObj->buf);

  if (ppShader == nullptr) {
    errorOut << "ppShader can not be null\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  CDXBCParser DXBCParser;
  if (DXBCParser.ReadDXBC(pBytecode, BytecodeSize) != S_OK) {
    errorOut << "Invalid DXBC bytecode\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  UINT codeBlobIdx = DXBCParser.FindNextMatchingBlob(DXBC_GenericShaderEx);
  if (codeBlobIdx == DXBC_BLOB_NOT_FOUND) {
    codeBlobIdx = DXBCParser.FindNextMatchingBlob(DXBC_GenericShader);
  }
  if (codeBlobIdx == DXBC_BLOB_NOT_FOUND) {
    errorOut << "Invalid DXBC bytecode: shader blob not found\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }
  const void *codeBlob = DXBCParser.GetBlob(codeBlobIdx);

  CShaderToken *ShaderCode = (CShaderToken *)(BYTE *)codeBlob;
  // 1. Collect information about the shader.
  D3D10ShaderBinary::CShaderCodeParser CodeParser(ShaderCode);
  CSignatureParser inputParser;
  if (DXBCGetInputSignature(pBytecode, &inputParser) != S_OK) {
    errorOut << "Invalid DXBC bytecode: input signature not found\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }
  CSignatureParser5 outputParser;
  if (DXBCGetOutputSignature(pBytecode, &outputParser) != S_OK) {
    errorOut << "Invalid DXBC bytecode: output signature not found\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  std::function<std::shared_ptr<BasicBlock>(
    const std::shared_ptr<BasicBlock> &ctx,
    const std::shared_ptr<BasicBlock> &block_after_endif,
    const std::shared_ptr<BasicBlock> &continue_point,
    const std::shared_ptr<BasicBlock> &break_point,
    const std::shared_ptr<BasicBlock> &return_point,
    std::shared_ptr<BasicBlockSwitch> &switch_context,
    std::shared_ptr<BasicBlockInstanceBarrier> &instance_barrier_context
  )>
    readControlFlow;

  std::shared_ptr<BasicBlock> null_bb;
  std::shared_ptr<BasicBlockSwitch> null_switch_context;
  std::shared_ptr<BasicBlockInstanceBarrier> null_instance_barrier_context;

  bool sm_ver_5_1_ = CodeParser.ShaderMajorVersion() == 5 &&
                     CodeParser.ShaderMinorVersion() >= 1;

  auto sm50_shader = new SM50ShaderInternal();
  sm50_shader->shader_type = CodeParser.ShaderType();
  auto shader_info = &(sm50_shader->shader_info);
  auto &func_signature = sm50_shader->func_signature;


  {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    outputParser.RastSignature()->GetParameters(&parameters);
    for (unsigned i = 0; i < outputParser.RastSignature()->GetNumParameters(); i++) {
      sm50_shader->output_signature.push_back(Signature(parameters[i]));
    }
  }

  uint32_t phase = ~0u;

  readControlFlow =
    [&](
      const std::shared_ptr<BasicBlock> &ctx,
      const std::shared_ptr<BasicBlock> &block_after_endif,
      const std::shared_ptr<BasicBlock> &continue_point,
      const std::shared_ptr<BasicBlock> &break_point,
      const std::shared_ptr<BasicBlock> &return_point,
      std::shared_ptr<BasicBlockSwitch> &switch_context,
      std::shared_ptr<BasicBlockInstanceBarrier> &instance_barrier_context
    ) -> std::shared_ptr<BasicBlock> {
    while (!CodeParser.EndOfShader()) {
      D3D10ShaderBinary::CInstruction Inst;
      CodeParser.ParseInstruction(&Inst);
      switch (Inst.m_OpCode) {
#pragma region control flow
      case D3D10_SB_OPCODE_IF: {
        // scope start: if-else-endif
        auto true_ = std::make_shared<BasicBlock>("if_true");
        auto alternative_ = std::make_shared<BasicBlock>("if_alternative");
        // alternative_ might be the block after ENDIF, but ELSE is possible
        ctx->target = BasicBlockConditionalBranch{
          readCondition(Inst, 0, phase), true_, alternative_
        };
        auto after_endif = readControlFlow(
          true_, alternative_, continue_point, break_point, return_point,
          null_switch_context, null_instance_barrier_context
        ); // read till ENDIF
        // scope end
        return readControlFlow(
          after_endif, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_ELSE: {
        assert(block_after_endif.get() && "");
        auto real_exit = std::make_shared<BasicBlock>("endif");
        ctx->target = BasicBlockUnconditionalBranch{real_exit};
        return readControlFlow(
          block_after_endif, real_exit, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_ENDIF: {
        assert(block_after_endif.get() && "");
        ctx->target = BasicBlockUnconditionalBranch{block_after_endif};
        return block_after_endif;
      }
      case D3D10_SB_OPCODE_LOOP: {
        auto loop_entrance = std::make_shared<BasicBlock>("loop_entrance");
        auto after_endloop = std::make_shared<BasicBlock>("endloop");
        // scope start: loop
        ctx->target = BasicBlockUnconditionalBranch{loop_entrance};
        auto _ = readControlFlow(
          loop_entrance, null_bb, loop_entrance, after_endloop, return_point,
          null_switch_context, null_instance_barrier_context
        ); // return from ENDLOOP
        assert(_.get() == after_endloop.get());
        // scope end
        return readControlFlow(
          after_endloop, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_BREAK: {
        ctx->target = BasicBlockUnconditionalBranch{break_point};
        auto after_break = std::make_shared<BasicBlock>("after_break");
        return readControlFlow(
          after_break, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        ); // ?
      }
      case D3D10_SB_OPCODE_BREAKC: {
        auto after_break = std::make_shared<BasicBlock>("after_breakc");
        ctx->target = BasicBlockConditionalBranch{
          readCondition(Inst, 0, phase), break_point, after_break
        };
        return readControlFlow(
          after_break, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        ); // ?
      }
      case D3D10_SB_OPCODE_CONTINUE: {
        ctx->target = BasicBlockUnconditionalBranch{continue_point};
        auto after_continue = std::make_shared<BasicBlock>("after_continue");
        return readControlFlow(
          after_continue, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        ); // ?
      }
      case D3D10_SB_OPCODE_CONTINUEC: {
        auto after_continue = std::make_shared<BasicBlock>("after_continuec");
        ctx->target = BasicBlockConditionalBranch{
          readCondition(Inst, 0, phase), continue_point, after_continue
        };
        return readControlFlow(
          after_continue, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        ); // ?
      }
      case D3D10_SB_OPCODE_ENDLOOP: {
        ctx->target = BasicBlockUnconditionalBranch{continue_point};
        return break_point;
      }
      case D3D10_SB_OPCODE_SWITCH: {
        auto after_endswitch = std::make_shared<BasicBlock>("endswitch");
        // scope start: switch
        auto local_switch_context = std::make_shared<BasicBlockSwitch>();
        local_switch_context->value = readSrcOperand(Inst.Operand(0), phase);
        // ensure the existence of default case
        local_switch_context->case_default = after_endswitch;
        auto empty_body = std::make_shared<BasicBlock>("switch_empty"
        ); // it will unconditional jump to
           // first case (and then ignored)
        auto _ = readControlFlow(
          empty_body, null_bb, continue_point, after_endswitch, return_point,
          local_switch_context, null_instance_barrier_context
        );
        assert(_.get() == after_endswitch.get());
        ctx->target = std::move(*local_switch_context);
        // scope end
        return readControlFlow(
          after_endswitch, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_CASE: {
        auto case_body = std::make_shared<BasicBlock>("switch_case");
        // always fallthrough
        ctx->target = BasicBlockUnconditionalBranch{case_body};

        const D3D10ShaderBinary::COperandBase &O = Inst.m_Operands[0];
        DXASSERT_DXBC(
          O.m_Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32 &&
          O.m_NumComponents == D3D10_SB_OPERAND_1_COMPONENT
        );
        uint32_t case_value = O.m_Value[0];

        switch_context->cases.insert(std::make_pair(case_value, case_body));
        return readControlFlow(
          case_body, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_DEFAULT: {
        ctx->target = BasicBlockUnconditionalBranch{break_point};
        auto case_body = std::make_shared<BasicBlock>("switch_default");
        switch_context->case_default = case_body;
        return readControlFlow(
          case_body, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_ENDSWITCH: {
        ctx->target = BasicBlockUnconditionalBranch{break_point};
        return break_point;
      }
      case D3D10_SB_OPCODE_RET: {
        ctx->target = BasicBlockUnconditionalBranch{return_point};
        if (
            // if it's inside a loop or switch, break_point is not null
            break_point.get() == nullptr &&
            // if it's inside if-else, block_after_endif is not null
            block_after_endif.get() == nullptr) {
          // not inside any scope, this is the final ret
          return return_point;
        }
        auto after_ret = std::make_shared<BasicBlock>("after_ret");
        // if it's inside a scope, then return is not the end
        return readControlFlow(
          after_ret, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_RETC: {
        auto after_retc = std::make_shared<BasicBlock>("after_retc");
        ctx->target = BasicBlockConditionalBranch{
          readCondition(Inst, 0, phase), return_point, after_retc
        };
        return readControlFlow(
          after_retc, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D10_SB_OPCODE_DISCARD: {
        auto fulfilled_ = std::make_shared<BasicBlock>("discard_fulfilled");
        auto otherwise_ = std::make_shared<BasicBlock>("discard_otherwise");
        ctx->target = BasicBlockConditionalBranch{
          readCondition(Inst, 0, phase), fulfilled_, otherwise_
        };
        fulfilled_->target = BasicBlockUnconditionalBranch{otherwise_};
        fulfilled_->instructions.push_back(InstPixelDiscard{});
        return readControlFlow(
          otherwise_, block_after_endif, continue_point, break_point,
          return_point, switch_context, null_instance_barrier_context
        );
      }
      case D3D11_SB_OPCODE_HS_CONTROL_POINT_PHASE: {
        shader_info->no_control_point_phase_passthrough = true;
        auto control_point_active =
          std::make_shared<BasicBlock>("control_point_active");
        auto control_point_end =
          std::make_shared<BasicBlock>("control_point_end");
        control_point_end->instructions.push_back(InstSync{
          .uav_boundary = InstSync::UAVBoundary::none,
          .tgsm_memory_barrier = true,
          .tgsm_execution_barrier = true,
        });

        auto local_context =
          std::make_shared<BasicBlockInstanceBarrier>(BasicBlockInstanceBarrier{
            sm50_shader->output_control_point_count, control_point_active,
            control_point_end
          });
        auto _ = readControlFlow(
          control_point_active, null_bb, null_bb, null_bb, control_point_end,
          null_switch_context, local_context
        );
        assert(_.get() == control_point_end.get());
        ctx->target = std::move(*local_context);
        return readControlFlow(
          control_point_end, null_bb, null_bb, null_bb, return_point,
          null_switch_context, null_instance_barrier_context
        );
      }
      case D3D11_SB_OPCODE_HS_JOIN_PHASE:
      case D3D11_SB_OPCODE_HS_FORK_PHASE: {
        phase++;
        shader_info->phases.push_back(PhaseInfo{});

        auto fork_join_active =
          std::make_shared<BasicBlock>("fork_join_active");
        auto fork_join_end = std::make_shared<BasicBlock>("fork_join_end");
        fork_join_end->instructions.push_back(InstSync{
          .uav_boundary = InstSync::UAVBoundary::none,
          .tgsm_memory_barrier = true,
          .tgsm_execution_barrier = true,
        });

        auto local_context = std::make_shared<BasicBlockInstanceBarrier>(
          BasicBlockInstanceBarrier{1, fork_join_active, fork_join_end}
        );
        auto _ = readControlFlow(
          fork_join_active, null_bb, null_bb, null_bb, fork_join_end,
          null_switch_context, local_context
        );
        assert(_.get() == fork_join_end.get());
        ctx->target = std::move(*local_context);
        return readControlFlow(
          fork_join_end, null_bb, null_bb, null_bb, return_point,
          null_switch_context, null_instance_barrier_context
        );
      }
      case D3D11_SB_OPCODE_DCL_HS_JOIN_PHASE_INSTANCE_COUNT:
      case D3D11_SB_OPCODE_DCL_HS_FORK_PHASE_INSTANCE_COUNT: {
        assert(instance_barrier_context.get());
        instance_barrier_context->instance_count =
          Inst.m_HSForkPhaseInstanceCountDecl.InstanceCount;
        sm50_shader->hull_maximum_threads_per_patch = std::max(
          sm50_shader->hull_maximum_threads_per_patch,
          Inst.m_HSForkPhaseInstanceCountDecl.InstanceCount
        );
        break;
      }
#pragma endregion
#pragma region declaration
      case D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER: {
        unsigned RangeID = Inst.m_Operands[0].m_Index[0].m_RegIndex;
        unsigned CBufferSize = Inst.m_ConstantBufferDecl.Size;
        unsigned LB, RangeSize;
        switch (Inst.m_Operands[0].m_IndexDimension) {
        case D3D10_SB_OPERAND_INDEX_2D: // SM 5.0-
          LB = RangeID;
          RangeSize = 1;
          break;
        case D3D10_SB_OPERAND_INDEX_3D: // SM 5.1
          LB = Inst.m_Operands[0].m_Index[1].m_RegIndex;
          RangeSize = Inst.m_Operands[0].m_Index[2].m_RegIndex != UINT_MAX
                        ? Inst.m_Operands[0].m_Index[2].m_RegIndex - LB + 1
                        : UINT_MAX;
          break;
        default:
          DXASSERT_DXBC(false);
        }
        shader_info->cbufferMap[RangeID] = {
          .range =
            {.range_id = RangeID,
             .lower_bound = LB,
             .size = RangeSize,
             .space = Inst.m_ConstantBufferDecl.Space},
          .size_in_vec4 = CBufferSize,
          .arg_index = 0, // set it later
        };
        break;
      }
      case D3D10_SB_OPCODE_DCL_SAMPLER: {
        // Root signature bindings.
        unsigned RangeID = Inst.m_Operands[0].m_Index[0].m_RegIndex;
        unsigned LB, RangeSize;
        switch (Inst.m_Operands[0].m_IndexDimension) {
        case D3D10_SB_OPERAND_INDEX_1D: // SM 5.0-
          LB = RangeID;
          RangeSize = 1;
          break;
        case D3D10_SB_OPERAND_INDEX_3D: // SM 5.1
          LB = Inst.m_Operands[0].m_Index[1].m_RegIndex;
          RangeSize = Inst.m_Operands[0].m_Index[2].m_RegIndex != UINT_MAX
                        ? Inst.m_Operands[0].m_Index[2].m_RegIndex - LB + 1
                        : UINT_MAX;
          break;
        default:
          DXASSERT_DXBC(false);
        }
        shader_info->samplerMap[RangeID] = {
          .range =
            {.range_id = RangeID,
             .lower_bound = LB,
             .size = RangeSize,
             .space = Inst.m_SamplerDecl.Space},
          .arg_index = 0, // set it later
        };
        // FIXME: SamplerMode ignored?
        break;
      }
      case D3D10_SB_OPCODE_DCL_RESOURCE:
      case D3D11_SB_OPCODE_DCL_RESOURCE_RAW:
      case D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED: {
        // Root signature bindings.
        unsigned RangeID = Inst.m_Operands[0].m_Index[0].m_RegIndex;
        unsigned LB, RangeSize;
        if (sm_ver_5_1_) {
          LB = Inst.m_Operands[0].m_Index[1].m_RegIndex;
          RangeSize = Inst.m_Operands[0].m_Index[2].m_RegIndex != UINT_MAX
                        ? Inst.m_Operands[0].m_Index[2].m_RegIndex - LB + 1
                        : UINT_MAX;
        } else {
          LB = RangeID;
          RangeSize = 1;
        }
        ShaderResourceViewInfo srv;
        srv.range = {
          .range_id = RangeID,
          .lower_bound = LB,
          .size = RangeSize,
          .space = 0,
        };
        switch (Inst.OpCode()) {
        case D3D10_SB_OPCODE_DCL_RESOURCE: {
          srv.range.space = (Inst.m_ResourceDecl.Space);
          srv.resource_type =
            to_shader_resource_type(Inst.m_ResourceDecl.Dimension);
          srv.scaler_type =
            to_shader_scaler_type(Inst.m_ResourceDecl.ReturnType[0]);
          srv.strucure_stride = -1;
          // Inst.m_ResourceDecl.SampleCount
          break;
        }
        case D3D11_SB_OPCODE_DCL_RESOURCE_RAW: {
          srv.resource_type = ResourceType::NonApplicable;
          srv.range.space = Inst.m_RawSRVDecl.Space;
          srv.scaler_type = ScalerDataType::Uint;
          srv.strucure_stride = 0;
          break;
        }
        case D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED: {
          srv.resource_type = ResourceType::NonApplicable;
          srv.range.space = (Inst.m_StructuredSRVDecl.Space);
          srv.scaler_type = ScalerDataType::Uint;
          srv.strucure_stride = Inst.m_StructuredSRVDecl.ByteStride;
          break;
        }
        default:;
        }

        shader_info->srvMap[RangeID] = srv;

        break;
      }
      case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED:
      case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW:
      case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED: {
        // Root signature bindings.
        unsigned RangeID = Inst.m_Operands[0].m_Index[0].m_RegIndex;
        unsigned LB, RangeSize;
        if (sm_ver_5_1_) {
          LB = Inst.m_Operands[0].m_Index[1].m_RegIndex;
          RangeSize = Inst.m_Operands[0].m_Index[2].m_RegIndex != UINT_MAX
                        ? Inst.m_Operands[0].m_Index[2].m_RegIndex - LB + 1
                        : UINT_MAX;
        } else {
          LB = RangeID;
          RangeSize = 1;
        }

        UnorderedAccessViewInfo uav;
        uav.range = {
          .range_id = RangeID,
          .lower_bound = LB,
          .size = RangeSize,
          .space = 0,
        };
        unsigned Flags = 0;
        switch (Inst.OpCode()) {
        case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED: {
          uav.range.space = (Inst.m_TypedUAVDecl.Space);
          Flags = Inst.m_TypedUAVDecl.Flags;
          uav.resource_type =
            to_shader_resource_type(Inst.m_TypedUAVDecl.Dimension);
          uav.scaler_type =
            to_shader_scaler_type(Inst.m_TypedUAVDecl.ReturnType[0]);
          uav.strucure_stride = -1;
          break;
        }
        case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW: {
          uav.range.space = (Inst.m_RawUAVDecl.Space);
          uav.resource_type = ResourceType::NonApplicable;
          Flags = Inst.m_RawUAVDecl.Flags;
          uav.scaler_type = ScalerDataType::Uint;
          uav.strucure_stride = 0;
          break;
        }
        case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED: {
          uav.range.space = (Inst.m_StructuredUAVDecl.Space);
          uav.resource_type = ResourceType::NonApplicable;
          Flags = Inst.m_StructuredUAVDecl.Flags;
          uav.scaler_type = ScalerDataType::Uint;
          uav.strucure_stride = Inst.m_StructuredUAVDecl.ByteStride;
          break;
        }
        default:;
        }

        uav.global_coherent =
          ((Flags & D3D11_SB_GLOBALLY_COHERENT_ACCESS) != 0);
        uav.rasterizer_order =
          ((Flags & D3D11_SB_RASTERIZER_ORDERED_ACCESS) != 0);

        shader_info->uavMap[RangeID] = uav;
        break;
      }
      case D3D10_SB_OPCODE_DCL_TEMPS: {
        if (phase != ~0u) {
          assert(shader_info->phases.size() > phase);
          shader_info->phases[phase].tempRegisterCount =
            Inst.m_TempsDecl.NumTemps;
          break;
        }
        shader_info->tempRegisterCount = Inst.m_TempsDecl.NumTemps;
        break;
      }
      case D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP: {
        if (phase != ~0u) {
          assert(shader_info->phases.size() > phase);
          shader_info->phases[phase].indexableTempRegisterCounts
            [Inst.m_IndexableTempDecl.IndexableTempNumber] = std::make_pair(
            Inst.m_IndexableTempDecl.NumRegisters,
            Inst.m_IndexableTempDecl.Mask >> 4
          );
          break;
        }
        shader_info->indexableTempRegisterCounts[Inst.m_IndexableTempDecl
                                                   .IndexableTempNumber] =
          std::make_pair(
            Inst.m_IndexableTempDecl.NumRegisters,
            Inst.m_IndexableTempDecl.Mask >> 4
          );
        break;
      }
      case D3D11_SB_OPCODE_DCL_THREAD_GROUP: {
        sm50_shader->threadgroup_size[0] = Inst.m_ThreadGroupDecl.x;
        sm50_shader->threadgroup_size[1] = Inst.m_ThreadGroupDecl.y;
        sm50_shader->threadgroup_size[2] = Inst.m_ThreadGroupDecl.z;
        func_signature.UseMaxWorkgroupSize(
          Inst.m_ThreadGroupDecl.x * Inst.m_ThreadGroupDecl.y *
          Inst.m_ThreadGroupDecl.z
        );
        break;
      }
      case D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW:
      case D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_STRUCTURED: {
        ThreadgroupBufferInfo tgsm;
        if (Inst.OpCode() ==
            D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW) {
          tgsm.stride = 0;
          tgsm.size = Inst.m_RawTGSMDecl.ByteCount;
          tgsm.size_in_uint = tgsm.size / 4;
          tgsm.structured = false;
          assert(
            (Inst.m_RawTGSMDecl.ByteCount & 0b11) == 0
          ); // is multiple of 4
        } else {
          tgsm.stride = Inst.m_StructuredTGSMDecl.StructByteStride;
          tgsm.size = Inst.m_StructuredTGSMDecl.StructCount;
          tgsm.size_in_uint = tgsm.stride * tgsm.size / 4;
          tgsm.structured = true;
          assert(
            (Inst.m_StructuredTGSMDecl.StructByteStride & 0b11) == 0
          ); // is multiple of 4
        }

        shader_info->tgsmMap[Inst.m_Operands[0].m_Index[0].m_RegIndex] = tgsm;
        break;
      }
      case D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS: {
        if (Inst.m_GlobalFlagsDecl.Flags &
            D3D11_SB_GLOBAL_FLAG_FORCE_EARLY_DEPTH_STENCIL) {
          func_signature.UseEarlyFragmentTests();
        }
        if (Inst.m_GlobalFlagsDecl.Flags &
            D3D11_1_SB_GLOBAL_FLAG_SKIP_OPTIMIZATION) {
          shader_info->skipOptimization = true;
        }
        if ((Inst.m_GlobalFlagsDecl.Flags &
             D3D10_SB_GLOBAL_FLAG_REFACTORING_ALLOWED) == 0) {
          shader_info->refactoringAllowed = false;
        }
        break;
      }
      case D3D10_SB_OPCODE_DCL_INPUT_SIV:
      case D3D10_SB_OPCODE_DCL_INPUT_SGV:
      case D3D10_SB_OPCODE_DCL_INPUT:
      case D3D10_SB_OPCODE_DCL_INPUT_PS_SIV:
      case D3D10_SB_OPCODE_DCL_INPUT_PS_SGV:
      case D3D10_SB_OPCODE_DCL_INPUT_PS:
      case D3D10_SB_OPCODE_DCL_OUTPUT_SGV:
      case D3D10_SB_OPCODE_DCL_OUTPUT_SIV:
      case D3D10_SB_OPCODE_DCL_OUTPUT: {
        handle_signature(inputParser, outputParser, Inst, sm50_shader, phase);
        break;
      }
      case D3D10_SB_OPCODE_CUSTOMDATA: {
        if (Inst.m_CustomData.Type ==
            D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER) {
          // must be list of 4-tuples
          unsigned size_in_vec4 = Inst.m_CustomData.DataSizeInBytes >> 4;
          DXASSERT_DXBC(Inst.m_CustomData.DataSizeInBytes == size_in_vec4 * 16);
          shader_info->immConstantBufferData.assign(
            (std::array<uint32_t, 4> *)Inst.m_CustomData.pData,
            ((std::array<uint32_t, 4> *)Inst.m_CustomData.pData) + size_in_vec4
          );
        }
        break;
      }
      case D3D10_SB_OPCODE_DCL_INDEX_RANGE:
        break; // ignore, and it turns out backend compiler can handle alloca
      case D3D10_SB_OPCODE_DCL_GS_INPUT_PRIMITIVE:
        sm50_shader->gs_input_primitive = Inst.m_InputPrimitiveDecl.Primitive;
        break;
      case D3D10_SB_OPCODE_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY:
        sm50_shader->gs_output_topology = Inst.m_OutputTopologyDecl.Topology;
        break;
      case D3D10_SB_OPCODE_DCL_MAX_OUTPUT_VERTEX_COUNT:
        sm50_shader->gs_max_vertex_output = Inst.m_GSMaxOutputVertexCountDecl.MaxOutputVertexCount;
        break;
      case D3D11_SB_OPCODE_DCL_GS_INSTANCE_COUNT:
        sm50_shader->gs_instance_count = Inst.m_GSInstanceCountDecl.InstanceCount;
        break;
      case D3D11_SB_OPCODE_DCL_STREAM:
      case D3D11_SB_OPCODE_DCL_INTERFACE:
      case D3D11_SB_OPCODE_DCL_FUNCTION_TABLE:
      case D3D11_SB_OPCODE_DCL_FUNCTION_BODY:
      case D3D10_SB_OPCODE_LABEL:
      case D3D11_SB_OPCODE_HS_DECLS:
        // ignore atm
        break;
      case D3D11_SB_OPCODE_DCL_TESS_PARTITIONING: {
        sm50_shader->tessellation_partition =
          Inst.m_TessellatorPartitioningDecl.TessellatorPartitioning;
        break;
      }
      case D3D11_SB_OPCODE_DCL_TESS_OUTPUT_PRIMITIVE: {
        switch (Inst.m_TessellatorOutputPrimitiveDecl.TessellatorOutputPrimitive
        ) {
        case microsoft::D3D11_SB_TESSELLATOR_OUTPUT_TRIANGLE_CW:
        case microsoft::D3D11_SB_TESSELLATOR_OUTPUT_TRIANGLE_CCW:
        case microsoft::D3D11_SB_TESSELLATOR_OUTPUT_POINT:
        case microsoft::D3D11_SB_TESSELLATOR_OUTPUT_LINE:
          sm50_shader->tessellator_output_primitive =
            Inst.m_TessellatorOutputPrimitiveDecl.TessellatorOutputPrimitive;
          break;
        default:
          assert(0 && "unexpected tessellator output primitive");
          break;
        }
        break;
      }
      case D3D11_SB_OPCODE_DCL_INPUT_CONTROL_POINT_COUNT: {
        sm50_shader->input_control_point_count =
          Inst.m_InputControlPointCountDecl.InputControlPointCount;
        sm50_shader->hull_maximum_threads_per_patch = std::max(
          sm50_shader->hull_maximum_threads_per_patch,
          Inst.m_InputControlPointCountDecl.InputControlPointCount
        );
        break;
      }
      case D3D11_SB_OPCODE_DCL_OUTPUT_CONTROL_POINT_COUNT: {
        sm50_shader->output_control_point_count =
          Inst.m_OutputControlPointCountDecl.OutputControlPointCount;
        sm50_shader->hull_maximum_threads_per_patch = std::max(
          sm50_shader->hull_maximum_threads_per_patch,
          Inst.m_OutputControlPointCountDecl.OutputControlPointCount
        );
        break;
      }
      case D3D11_SB_OPCODE_DCL_TESS_DOMAIN: {
        if (sm50_shader->shader_type != D3D11_SB_DOMAIN_SHADER) {
          break;
        }
        assert(sm50_shader->input_control_point_count != ~0u);
        switch (Inst.m_TessellatorDomainDecl.TessellatorDomain) {
        case microsoft::D3D11_SB_TESSELLATOR_DOMAIN_UNDEFINED:
        case microsoft::D3D11_SB_TESSELLATOR_DOMAIN_ISOLINE:
          // nop;
          break;
        case microsoft::D3D11_SB_TESSELLATOR_DOMAIN_TRI:
          sm50_shader->func_signature.UsePatch(
            dxmt::air::PostTessellationPatch::triangle,
            sm50_shader->input_control_point_count
          );
          break;
        case microsoft::D3D11_SB_TESSELLATOR_DOMAIN_QUAD:
          sm50_shader->func_signature.UsePatch(
            dxmt::air::PostTessellationPatch::quad,
            sm50_shader->input_control_point_count
          );
          break;
        }
        break;
      }
      case D3D11_SB_OPCODE_DCL_HS_MAX_TESSFACTOR: {
        sm50_shader->max_tesselation_factor =
          Inst.m_HSMaxTessFactorDecl.MaxTessFactor;
        break;
      }
      case microsoft::D3D10_SB_OPCODE_EMIT: {
        ctx->instructions.push_back(InstEmit{});
        break;
      }
      case microsoft::D3D10_SB_OPCODE_EMITTHENCUT: {
        ctx->instructions.push_back(InstEmit{});
        ctx->instructions.push_back(InstCut{});
        break;
      }
      case microsoft::D3D10_SB_OPCODE_CUT: {
        ctx->instructions.push_back(InstCut{});
        break;
      }
      case microsoft::D3D11_SB_OPCODE_EMIT_STREAM: {
        assert(Inst.m_Operands[0].m_Value[0] == 0 && "multiple streams not supported");
        ctx->instructions.push_back(InstEmit{});
        break;
      }
      case microsoft::D3D11_SB_OPCODE_EMITTHENCUT_STREAM: {
        assert(Inst.m_Operands[0].m_Value[0] == 0 && "multiple streams not supported");
        ctx->instructions.push_back(InstEmit{});
        ctx->instructions.push_back(InstCut{});
        break;
      }
      case microsoft::D3D11_SB_OPCODE_CUT_STREAM: {
        assert(Inst.m_Operands[0].m_Value[0] == 0 && "multiple streams not supported");
        ctx->instructions.push_back(InstCut{});
        break;
      }
#pragma endregion
      default: {
        // insert instruction into BasicBlock
        ctx->instructions.push_back(
          dxmt::dxbc::readInstruction(Inst, *shader_info, phase)
        );
        break;
      }
      }
    }
    if (sm50_shader->shader_type == D3D11_SB_HULL_SHADER) {
      assert(return_point && sm50_shader->shader_type == D3D11_SB_HULL_SHADER);
      if (shader_info->output_control_point_read ||
          !shader_info->no_control_point_phase_passthrough) {
        ctx->target = BasicBlockHullShaderWriteOutput{
          sm50_shader->output_control_point_count, return_point
        };
      } else {
        ctx->target = BasicBlockUnconditionalBranch{return_point};
      }
      return return_point;
    } else {
      assert(0 && "Unexpected end of shader instructions.");
    }
  };

  auto entry = std::make_shared<BasicBlock>("entrybb");
  auto return_point = std::make_shared<BasicBlock>("returnbb");
  return_point->target = BasicBlockReturn{};
  auto _ = readControlFlow(
    entry, null_bb, null_bb, null_bb, return_point, null_switch_context,
    null_instance_barrier_context
  );
  assert(_.get() == return_point.get());

  sm50_shader->entry = entry;

  auto &binding_table = shader_info->binding_table;
  auto &binding_table_cbuffer = shader_info->binding_table_cbuffer;

  uint16_t binding_cbuffer_mask = 0;
  uint16_t binding_sampler_mask = 0;
  uint64_t binding_uav_mask = 0;
  uint64_t binding_srv_hi_mask = 0;
  uint64_t binding_srv_lo_mask = 0;

  for (auto &[range_id, cbv] : shader_info->cbufferMap) {
    // TODO: abstract SM 5.0 binding
    cbv.arg_index = binding_table_cbuffer.DefineBuffer(
      "cb" + std::to_string(range_id), AddressSpace::constant,
      MemoryAccess::read, msl_uint4,
      GetArgumentIndex(SM50BindingType::ConstantBuffer, range_id)
    );
    sm50_shader->args_reflection_cbuffer.push_back({
      .Type = SM50BindingType::ConstantBuffer,
      .SM50BindingSlot = range_id,
      .Flags =
        MTL_SM50_SHADER_ARGUMENT_BUFFER | MTL_SM50_SHADER_ARGUMENT_READ_ACCESS,
      .StructurePtrOffset = cbv.arg_index,
    });
    binding_cbuffer_mask |= (1 << range_id);
  }
  for (auto &[range_id, sampler] : shader_info->samplerMap) {
    // TODO: abstract SM 5.0 binding
    auto attr_index = GetArgumentIndex(SM50BindingType::Sampler, range_id);
    sampler.arg_index =
      binding_table.DefineSampler("s" + std::to_string(range_id), attr_index);
    sampler.arg_metadata_index = binding_table.DefineInteger64(
      "meta_s" + std::to_string(range_id), attr_index + 1
    );
    sm50_shader->args_reflection.push_back({
      .Type = SM50BindingType::Sampler,
      .SM50BindingSlot = range_id,
      .Flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)0,
      .StructurePtrOffset = sampler.arg_index,
    });
    binding_sampler_mask |= (1 << range_id);
  }
  for (auto &[range_id, srv] : shader_info->srvMap) {
    auto attr_index = GetArgumentIndex(SM50BindingType::SRV, range_id);
    if (srv.resource_type != ResourceType::NonApplicable) {
      // TODO: abstract SM 5.0 binding
      auto access = srv.sampled ? MemoryAccess::sample : MemoryAccess::read;
      auto texture_kind = to_air_resource_type(srv.resource_type, srv.compared);
      auto scaler_type = to_air_scaler_type(srv.scaler_type);
      srv.arg_index = binding_table.DefineTexture(
        "t" + std::to_string(range_id), texture_kind, access, scaler_type,
        attr_index
      );
    } else {
      srv.arg_index = binding_table.DefineBuffer(
        "t" + std::to_string(range_id),
        AddressSpace::device, // it needs to be `device` since `constant` has
                              // size and alignment restriction
        MemoryAccess::read, msl_uint, attr_index
      );
    }
    srv.arg_metadata_index = binding_table.DefineInteger64(
      "meta_t" + std::to_string(range_id), attr_index + 1
    );
    MTL_SM50_SHADER_ARGUMENT_FLAG flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)0;
    if (srv.resource_type == ResourceType::NonApplicable) {
      flags |= MTL_SM50_SHADER_ARGUMENT_ELEMENT_WIDTH |
               MTL_SM50_SHADER_ARGUMENT_BUFFER;
    } else if (srv.resource_type == ResourceType::TextureBuffer) {
      flags |= MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
    } else {
      flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_MINLOD_CLAMP |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
      switch (srv.resource_type) {
      case ResourceType::Texture1DArray:
      case ResourceType::Texture2DArray:
      case ResourceType::Texture2DMultisampledArray:
      case ResourceType::TextureCubeArray:
        flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_ARRAY;
        break;
      default:
        break;
      }
    }
    if (srv.read || srv.sampled || srv.compared) {
      flags |= MTL_SM50_SHADER_ARGUMENT_READ_ACCESS;
    }
    sm50_shader->args_reflection.push_back({
      .Type = SM50BindingType::SRV,
      .SM50BindingSlot = range_id,
      .Flags = flags,
      .StructurePtrOffset = srv.arg_index,
    });
    if (range_id & 64) {
      binding_srv_hi_mask |= (1ULL << (range_id - 64));
    } else {
      binding_srv_lo_mask |= (1ULL << range_id);
    }
  }
  for (auto &[range_id, uav] : shader_info->uavMap) {
    auto access =
      uav.written ? (uav.read ? MemoryAccess::read_write : MemoryAccess::write)
                  : MemoryAccess::read;
    auto attr_index = GetArgumentIndex(SM50BindingType::UAV, range_id);
    if (uav.resource_type != ResourceType::NonApplicable) {
      auto texture_kind = to_air_resource_type(uav.resource_type);
      auto scaler_type = to_air_scaler_type(uav.scaler_type);
      uav.arg_index = binding_table.DefineTexture(
        "u" + std::to_string(range_id), texture_kind, access, scaler_type,
        attr_index, uav.rasterizer_order ? std::optional(1) : std::nullopt
      );
    } else {
      uav.arg_index = binding_table.DefineBuffer(
        "u" + std::to_string(range_id), AddressSpace::device, access, msl_uint,
        attr_index, uav.rasterizer_order ? std::optional(1) : std::nullopt
      );
    }
    uav.arg_metadata_index = binding_table.DefineInteger64(
      "meta_u" + std::to_string(range_id), attr_index + 1
    );
    if (uav.with_counter) {
      uav.arg_counter_index = binding_table.DefineBuffer(
        "counter" + std::to_string(range_id), AddressSpace::device,
        MemoryAccess::read_write, msl_uint, attr_index + 2,
        uav.rasterizer_order ? std::optional(1) : std::nullopt
      );
    }
    MTL_SM50_SHADER_ARGUMENT_FLAG flags = (MTL_SM50_SHADER_ARGUMENT_FLAG)0;
    if (uav.resource_type == ResourceType::NonApplicable) {
      flags |= MTL_SM50_SHADER_ARGUMENT_ELEMENT_WIDTH |
               MTL_SM50_SHADER_ARGUMENT_BUFFER;
    } else if (uav.resource_type == ResourceType::TextureBuffer) {
      flags |= MTL_SM50_SHADER_ARGUMENT_TBUFFER_OFFSET |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
    } else {
      flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_MINLOD_CLAMP |
               MTL_SM50_SHADER_ARGUMENT_TEXTURE;
      switch (uav.resource_type) {
      case ResourceType::Texture1DArray:
      case ResourceType::Texture2DArray:
      case ResourceType::Texture2DMultisampledArray:
      case ResourceType::TextureCubeArray:
        flags |= MTL_SM50_SHADER_ARGUMENT_TEXTURE_ARRAY;
        break;
      default:
        break;
      }
    }
    if (uav.read) {
      flags |= MTL_SM50_SHADER_ARGUMENT_READ_ACCESS;
    }
    if (uav.written) {
      flags |= MTL_SM50_SHADER_ARGUMENT_WRITE_ACCESS;
    }
    if (uav.with_counter) {
      flags |= MTL_SM50_SHADER_ARGUMENT_UAV_COUNTER;
    }
    sm50_shader->args_reflection.push_back({
      .Type = SM50BindingType::UAV,
      .SM50BindingSlot = range_id,
      .Flags = flags,
      .StructurePtrOffset = uav.arg_index,
    });
    binding_uav_mask |= (1ULL << range_id);
  }

  if (sm50_shader->shader_type == microsoft::D3D11_SB_HULL_SHADER &&
      !sm50_shader->shader_info.no_control_point_phase_passthrough) {
    assert(sm50_shader->max_output_register == 0);
    uint32_t num_input_params = inputParser.GetNumParameters();
    D3D11_SIGNATURE_PARAMETER const * params;
    inputParser.GetParameters(&params);
    for (unsigned i = 0; i < num_input_params; i++) {
      sm50_shader->max_output_register = std::max(
        sm50_shader->max_output_register,
        params[i].Register + 1
      );
    }
  }

  if (pRefl) {
    pRefl->ConstanttBufferTableBindIndex =
      sm50_shader->args_reflection_cbuffer.size() > 0 ? 29 : ~0u;
    pRefl->ArgumentBufferBindIndex =
      sm50_shader->args_reflection.size() > 0 ? 30 : ~0u;
    pRefl->ConstantBufferSlotMask = binding_cbuffer_mask;
    pRefl->SamplerSlotMask = binding_sampler_mask;
    pRefl->SRVSlotMaskHi = binding_srv_hi_mask;
    pRefl->SRVSlotMaskLo = binding_srv_lo_mask;
    pRefl->UAVSlotMask = binding_uav_mask;
    pRefl->NumConstantBuffers = sm50_shader->args_reflection_cbuffer.size();
    pRefl->NumArguments = sm50_shader->args_reflection.size();
    if (sm50_shader->shader_type == microsoft::D3D11_SB_COMPUTE_SHADER) {
      pRefl->ThreadgroupSize[0] = sm50_shader->threadgroup_size[0];
      pRefl->ThreadgroupSize[1] = sm50_shader->threadgroup_size[1];
      pRefl->ThreadgroupSize[2] = sm50_shader->threadgroup_size[2];
    }
    if (sm50_shader->shader_type == microsoft::D3D11_SB_HULL_SHADER) {
      pRefl->Tessellator = {
        .Partition = sm50_shader->tessellation_partition,
        .MaxFactor = sm50_shader->max_tesselation_factor,
        .OutputPrimitive = (MTL_TESSELLATOR_OUTPUT_PRIMITIVE
        )sm50_shader->tessellator_output_primitive,
      };
    }
    if (sm50_shader->shader_type == microsoft::D3D10_SB_GEOMETRY_SHADER) {
      if (binding_cbuffer_mask || binding_sampler_mask || binding_uav_mask ||
          binding_srv_hi_mask || binding_srv_lo_mask ||
          !CheckGSBBIsPassThrough(entry.get())) {
        pRefl->GeometryShader.GSPassThrough = ~0u;
      } else {
        CheckGSSignatureIsPassThrough(
          inputParser, outputParser, pRefl->GeometryShader.Data
        );
      }
      pRefl->GeometryShader.Primitive = sm50_shader->gs_input_primitive;
    }
    pRefl->NumOutputElement = sm50_shader->max_output_register;
    pRefl->NumPatchConstantOutputScalar =
      sm50_shader->patch_constant_scalars.size();
    pRefl->ThreadsPerPatch =
      next_pow2(sm50_shader->hull_maximum_threads_per_patch);
    pRefl->ArgumentTableQwords = binding_table.Size();
  }

  *ppShader = sm50_shader;
  return 0;
};

AIRCONV_API void SM50GetArgumentsInfo(
  sm50_shader_t pShader, struct MTL_SM50_SHADER_ARGUMENT *pConstantBuffers,
  struct MTL_SM50_SHADER_ARGUMENT *pArguments
) {
  auto sm50_shader = (dxmt::dxbc::SM50ShaderInternal *)pShader;
  if (pConstantBuffers && sm50_shader->args_reflection_cbuffer.size())
    memcpy(
      pConstantBuffers, sm50_shader->args_reflection_cbuffer.data(),
      sm50_shader->args_reflection_cbuffer.size() *
        sizeof(struct MTL_SM50_SHADER_ARGUMENT)
    );
  if (pArguments && sm50_shader->args_reflection.size())
    memcpy(
      pArguments, sm50_shader->args_reflection.data(),
      sm50_shader->args_reflection.size() *
        sizeof(struct MTL_SM50_SHADER_ARGUMENT)
    );
}

AIRCONV_API void SM50Destroy(sm50_shader_t pShader) {
  delete (dxmt::dxbc::SM50ShaderInternal *)pShader;
}

AIRCONV_API int SM50Compile(
  sm50_shader_t pShader, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = new SM50ErrorInternal();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info = ((dxmt::dxbc::SM50ShaderInternal *)pShader)->shader_info;
  auto shader_type = ((dxmt::dxbc::SM50ShaderInternal *)pShader)->shader_type;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(
    *pModule,
    {.enableFastMath =
       (!shader_info.skipOptimization && shader_info.refactoringAllowed &&
        // this is by design: vertex functions are usually not the
        // bottle-neck of pipeline, and precise calculation on pixel can reduce
        // flickering
        shader_type != microsoft::D3D10_SB_VERTEX_SHADER)}
  );

  if (auto err = dxmt::dxbc::convertDXBC(
        pShader, FunctionName, context, *pModule, pArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileTessellationPipelineVertex(
  sm50_shader_t pVertexShader, sm50_shader_t pHullShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pVertexShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = new SM50ErrorInternal();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(*pModule, {.enableFastMath = false});

  if (auto err = dxmt::dxbc::convert_dxbc_vertex_for_hull_shader(
        (dxbc::SM50ShaderInternal *)pVertexShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pHullShader, context, *pModule,
        pVertexShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileTessellationPipelineHull(
  sm50_shader_t pVertexShader, sm50_shader_t pHullShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pHullShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = new SM50ErrorInternal();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info =
    ((dxmt::dxbc::SM50ShaderInternal *)pHullShader)->shader_info;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(
    *pModule,
    {.enableFastMath =
       (!shader_info.skipOptimization && shader_info.refactoringAllowed)}
  );

  if (auto err = dxmt::dxbc::convert_dxbc_hull_shader(
        (dxbc::SM50ShaderInternal *)pHullShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pVertexShader, context, *pModule,
        pHullShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileTessellationPipelineDomain(
  sm50_shader_t pHullShader, sm50_shader_t pDomainShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pDomainShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = new SM50ErrorInternal();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info =
    ((dxmt::dxbc::SM50ShaderInternal *)pDomainShader)->shader_info;
  auto shader_type =
    ((dxmt::dxbc::SM50ShaderInternal *)pDomainShader)->shader_type;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(
    *pModule,
    {.enableFastMath =
       (!shader_info.skipOptimization && shader_info.refactoringAllowed &&
        // this is by design: vertex functions are usually not the
        // bottle-neck of pipeline, and precise calculation on pixel can reduce
        // flickering
        shader_type != microsoft::D3D10_SB_VERTEX_SHADER)}
  );

  if (auto err = dxmt::dxbc::convert_dxbc_domain_shader(
        (dxbc::SM50ShaderInternal *)pDomainShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pHullShader, context, *pModule,
        pDomainShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileGeometryPipelineVertex(
  sm50_shader_t pVertexShader, sm50_shader_t pGeometryShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pVertexShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = new SM50ErrorInternal();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(*pModule, {.enableFastMath = false});

  if (auto err = dxmt::dxbc::convert_dxbc_vertex_for_geometry_shader(
        (dxbc::SM50ShaderInternal *)pVertexShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pGeometryShader, context, *pModule,
        pVertexShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API int SM50CompileGeometryPipelineGeometry(
  sm50_shader_t pVertexShader, sm50_shader_t pGeometryShader,
  struct SM50_SHADER_COMPILATION_ARGUMENT_DATA *pGeometryShaderArgs,
  const char *FunctionName, sm50_bitcode_t *ppBitcode, sm50_error_t *ppError
) {
  using namespace llvm;
  using namespace dxmt;

  if (ppError) {
    *ppError = nullptr;
  }
  auto errorObj = new SM50ErrorInternal();
  llvm::raw_svector_ostream errorOut(errorObj->buf);
  if (ppBitcode == nullptr) {
    errorOut << "ppBitcode can not be null\0";
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // pArgs is ignored for now
  LLVMContext context;

  context.setOpaquePointers(false); // I suspect Metal uses LLVM 14...

  auto &shader_info =
    ((dxmt::dxbc::SM50ShaderInternal *)pGeometryShader)->shader_info;

  auto pModule = std::make_unique<Module>("shader.air", context);
  initializeModule(
    *pModule,
    {.enableFastMath =
       (!shader_info.skipOptimization && shader_info.refactoringAllowed)}
  );

  if (auto err = dxmt::dxbc::convert_dxbc_geometry_shader(
        (dxbc::SM50ShaderInternal *)pGeometryShader, FunctionName,
        (dxbc::SM50ShaderInternal *)pVertexShader, context, *pModule,
        pGeometryShaderArgs
      )) {
    llvm::handleAllErrors(std::move(err), [&](const UnsupportedFeature &u) {
      errorOut << u.msg;
    });
    *ppError = (sm50_error_t)errorObj;
    return 1;
  }

  // Serialize AIR
  auto compiled = new SM50CompiledBitcodeInternal();

  raw_svector_ostream OS(compiled->vec);

  metallib::MetallibWriter writer;

  writer.Write(*pModule, OS);

  pModule.reset();

  *ppBitcode = (sm50_bitcode_t)compiled;
  return 0;
}

AIRCONV_API void SM50GetCompiledBitcode(
  sm50_bitcode_t pBitcode, MTL_SHADER_BITCODE *pData
) {
  auto pBitcodeInternal = (SM50CompiledBitcodeInternal *)pBitcode;
  pData->Data = pBitcodeInternal->vec.data();
  pData->Size = pBitcodeInternal->vec.size();
}

AIRCONV_API void SM50DestroyBitcode(sm50_bitcode_t pBitcode) {
  auto pBitcodeInternal = (SM50CompiledBitcodeInternal *)pBitcode;
  delete pBitcodeInternal;
}

AIRCONV_API size_t SM50GetErrorMessage(sm50_error_t pError, char *pBuffer, size_t BufferSize) {
  auto pInternal = (SM50ErrorInternal *)pError;
  auto str_len = std::min(pInternal->buf.size(), BufferSize - 1);
  memcpy(pBuffer, pInternal->buf.data(), str_len);
  pBuffer[str_len] = '\0';
  return str_len;
}

AIRCONV_API void SM50FreeError(sm50_error_t pError) {
  if (pError == nullptr)
    return;
  auto pInternal = (SM50ErrorInternal *)pError;
  delete pInternal;
}
