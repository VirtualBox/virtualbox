#include "air_operations.hpp"
#include "air_signature.hpp"
#include "airconv_error.hpp"
#include "dxbc_converter.hpp"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <format>
#include <utility>

namespace dxmt::dxbc {

using namespace microsoft;

std::tuple<uint32_t, uint32_t, uint32_t>
get_vertex_primitive_count_in_warp(D3D10_SB_PRIMITIVE primitive, bool strip) {
  switch (primitive) {
  case D3D10_SB_PRIMITIVE_POINT:
    return {32, 32, 1};
  case D3D10_SB_PRIMITIVE_LINE:
    if (strip)
      return {31, 31, 2};
    return {32, 16, 2};
  case D3D10_SB_PRIMITIVE_TRIANGLE:
    if (strip)
      return {30, 30, 3};
    return {30, 10, 3};
  case D3D10_SB_PRIMITIVE_LINE_ADJ:
    if (strip)
      return {29, 29, 4};
    return {32, 8, 4};
  case D3D10_SB_PRIMITIVE_TRIANGLE_ADJ:
    if (strip)
      return {28, 14, 6};
    return {30, 5, 6};
  case D3D11_SB_PRIMITIVE_1_CONTROL_POINT_PATCH:
    return {32, 32, 1};
  case D3D11_SB_PRIMITIVE_2_CONTROL_POINT_PATCH:
    return {32, 16, 2};
  case D3D11_SB_PRIMITIVE_3_CONTROL_POINT_PATCH:
    return {30, 10, 3};
  case D3D11_SB_PRIMITIVE_4_CONTROL_POINT_PATCH:
    return {32, 8, 4};
  case D3D11_SB_PRIMITIVE_5_CONTROL_POINT_PATCH:
    return {30, 6, 5};
  case D3D11_SB_PRIMITIVE_6_CONTROL_POINT_PATCH:
    return {30, 5, 6};
  case D3D11_SB_PRIMITIVE_7_CONTROL_POINT_PATCH:
    return {28, 4, 7};
  case D3D11_SB_PRIMITIVE_8_CONTROL_POINT_PATCH:
    return {32, 4, 8};
  case D3D11_SB_PRIMITIVE_9_CONTROL_POINT_PATCH:
    return {27, 3, 9};
  case D3D11_SB_PRIMITIVE_10_CONTROL_POINT_PATCH:
    return {30, 3, 10};
  case D3D11_SB_PRIMITIVE_11_CONTROL_POINT_PATCH:
    return {22, 2, 11};
  case D3D11_SB_PRIMITIVE_12_CONTROL_POINT_PATCH:
    return {24, 2, 12};
  case D3D11_SB_PRIMITIVE_13_CONTROL_POINT_PATCH:
    return {26, 2, 13};
  case D3D11_SB_PRIMITIVE_14_CONTROL_POINT_PATCH:
    return {28, 2, 14};
  case D3D11_SB_PRIMITIVE_15_CONTROL_POINT_PATCH:
    return {30, 2, 15};
  case D3D11_SB_PRIMITIVE_16_CONTROL_POINT_PATCH:
    return {32, 2, 16};
  case D3D11_SB_PRIMITIVE_17_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_18_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_19_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_20_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_21_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_22_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_23_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_24_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_25_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_26_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_27_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_28_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_29_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_30_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_31_CONTROL_POINT_PATCH:
  case D3D11_SB_PRIMITIVE_32_CONTROL_POINT_PATCH:
    return {
        uint32_t(primitive) - uint32_t(D3D11_SB_PRIMITIVE_1_CONTROL_POINT_PATCH) + 1, 1,
        uint32_t(primitive) - uint32_t(D3D11_SB_PRIMITIVE_1_CONTROL_POINT_PATCH) + 1
    };
  default:
    break;
  }

  return {0, 0, 0};
}

llvm::Error
convert_dxbc_geometry_shader(
    SM50ShaderInternal *pShaderInternal, const char *name, SM50ShaderInternal *pVertexStage, llvm::LLVMContext &context,
    llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  bool is_strip = false;

  uint32_t max_output_register = pShaderInternal->max_output_register;
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    // case SM50_SHADER_DEBUG_IDENTITY:
    //   debug_id = ((SM50_SHADER_DEBUG_IDENTITY_DATA *)arg)->id;
    //   break;
    case SM50_SHADER_PSO_GEOMETRY_SHADER:
      is_strip = ((SM50_SHADER_PSO_GEOMETRY_SHADER_DATA *)arg)->strip_topology;
      break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue { return nullptr; });

  io_binding_map resource_map;
  air::AirType types(context);

  {
    SignatureContext sig_ctx(prologue, epilogue, func_signature, resource_map);
    for (auto &p : pShaderInternal->signature_handlers) {
      p(sig_ctx);
    }
  }
  auto& gs_output_handlers = pShaderInternal->gs_output_handlers;

  setup_binding_table(shader_info, resource_map, func_signature, module);
  setup_tgsm(shader_info, resource_map, types, module);

  auto gs_output_topology = pShaderInternal->gs_output_topology;
  int32_t max_vertex_out = pShaderInternal->gs_max_vertex_output;
  air::MeshOutputTopology topology = air::MeshOutputTopology::Point;
  switch (gs_output_topology) {
  case microsoft::D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
    topology = air::MeshOutputTopology::Triangle;
    break;
  case microsoft::D3D10_SB_PRIMITIVE_TOPOLOGY_LINESTRIP:
    topology = air::MeshOutputTopology::Line;
    break;
  case microsoft::D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST:
    break;
  default:
    return llvm::make_error<UnsupportedFeature>("unsupported geometry shader output topology");
  }

  uint32_t payload_idx = func_signature.DefineInput(air::InputPayload{.size = 16256});
  // it's intended to declare a bit more max primitive count
  auto mesh_idx = func_signature.DefineInput(air::InputMesh{(uint32_t)max_vertex_out, (uint32_t)max_vertex_out, topology});
  uint32_t tg_in_grid_idx = func_signature.DefineInput(air::InputThreadgroupPositionInGrid{});

  auto [function, function_metadata] = func_signature.CreateFunction(name, context, module, 0, false);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto active_ = llvm::BasicBlock::Create(context, "active", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);
  setup_fastmath_flag(module, builder);

  auto [warp_vertex_count, warp_primitive_count, vertex_per_primitive] =
      get_vertex_primitive_count_in_warp(pShaderInternal->gs_input_primitive, is_strip);
  auto tg_position_in_grid = function->getArg(tg_in_grid_idx);
  auto primitive_id_in_warp = builder.CreateExtractElement(tg_position_in_grid, (uint32_t)0);
  resource_map.gs_instance_id = builder.CreateExtractElement(tg_position_in_grid, (uint32_t)1);

  auto const zero_const = builder.getInt32(0);
  auto const one_const = builder.getInt32(1);
  auto const two_const = builder.getInt32(2);
  auto next_write_vertex = builder.CreateAlloca(types._int, nullptr, "next_write_veretx");
  builder.CreateStore(zero_const, next_write_vertex);
  auto vertex_offset = builder.CreateAlloca(types._int, nullptr, "vertex_offset");
  builder.CreateStore(zero_const, vertex_offset);
  auto primitive_count = builder.CreateAlloca(types._int, nullptr, "primitive_count");
  builder.CreateStore(zero_const, primitive_count);

  auto mesh_ptr = function->getArg(mesh_idx);

  resource_map.mesh = mesh_ptr;

  if (topology == air::MeshOutputTopology::Triangle) {
    resource_map.call_emit = [&]() -> IREffect {
      auto current_write_vertex = builder.CreateLoad(types._int, next_write_vertex);
      auto current_vertex_offset = builder.CreateLoad(types._int, vertex_offset);
      auto current_vertex_with_offset = builder.CreateAdd(current_vertex_offset, current_write_vertex);

      auto current_primitive_count = builder.CreateLoad(types._int, primitive_count);
      auto current_primitive_idx = builder.CreateAdd(current_primitive_count, current_write_vertex);

      auto even_winding = builder.CreateZExt(
          builder.CreateICmpEQ(zero_const, builder.CreateAnd(current_write_vertex, one_const)), types._int
      );

      GSOutputContext gs_out_ctx{current_vertex_with_offset, current_primitive_idx};
      for (auto &h : gs_output_handlers) {
        co_yield h(gs_out_ctx);
      }

      auto triple_primitive_idx = builder.CreateMul(current_primitive_idx, builder.getInt32(3));
      co_yield air::call_set_mesh_index(
          mesh_ptr, builder.CreateAdd(triple_primitive_idx, zero_const), current_vertex_with_offset
      );
      co_yield air::call_set_mesh_index(
          mesh_ptr, builder.CreateAdd(triple_primitive_idx, one_const),
          builder.CreateSub(builder.CreateAdd(current_vertex_with_offset, two_const), even_winding)
      );
      co_yield air::call_set_mesh_index(
          mesh_ptr, builder.CreateAdd(triple_primitive_idx, two_const),
          builder.CreateAdd(builder.CreateAdd(current_vertex_with_offset, one_const), even_winding)
      );

      builder.CreateStore(builder.CreateAdd(one_const, current_write_vertex), next_write_vertex);

      co_return {};
    };
    resource_map.call_cut = [&]() -> IREffect {
      auto current_write_vertex = builder.CreateLoad(types._int, next_write_vertex);
      builder.CreateStore(zero_const, next_write_vertex);

      auto has_valid_primitive = builder.CreateICmpUGT(current_write_vertex, builder.getInt32(2));
      auto add_primitive_count = builder.CreateSelect(
          has_valid_primitive, builder.CreateSub(current_write_vertex, builder.getInt32(2)), zero_const
      );
      auto add_vertex_count = builder.CreateSelect(has_valid_primitive, current_write_vertex, zero_const);
      builder.CreateStore(
          builder.CreateAdd(builder.CreateLoad(types._int, vertex_offset), add_vertex_count), vertex_offset
      );
      builder.CreateStore(
          builder.CreateAdd(builder.CreateLoad(types._int, primitive_count), add_primitive_count), primitive_count
      );
      co_return {};
    };
  } else if (topology == air::MeshOutputTopology::Line) {
    resource_map.call_emit = [&]() -> IREffect {
      auto current_write_vertex = builder.CreateLoad(types._int, next_write_vertex);
      builder.CreateStore(builder.CreateAdd(one_const, current_write_vertex), next_write_vertex);

      auto current_vertex_offset = builder.CreateLoad(types._int, vertex_offset);
      auto current_vertex_with_offset = builder.CreateAdd(current_vertex_offset, current_write_vertex);

      auto current_primitive_count = builder.CreateLoad(types._int, primitive_count);
      auto current_primitive_idx = builder.CreateAdd(current_primitive_count, current_write_vertex);

      GSOutputContext gs_out_ctx{current_vertex_with_offset, current_primitive_idx};
      for (auto &h : gs_output_handlers) {
        co_yield h(gs_out_ctx);
      }

      auto double_primitive_idx = builder.CreateMul(current_primitive_idx, builder.getInt32(2));
      co_yield air::call_set_mesh_index(
          mesh_ptr, builder.CreateAdd(double_primitive_idx, zero_const), current_vertex_with_offset
      );
      co_yield air::call_set_mesh_index(
          mesh_ptr, builder.CreateAdd(double_primitive_idx, one_const),
          builder.CreateAdd(current_vertex_with_offset, one_const)
      );

      co_return {};
    };
    resource_map.call_cut = [&]() -> IREffect {
      auto current_write_vertex = builder.CreateLoad(types._int, next_write_vertex);
      builder.CreateStore(zero_const, next_write_vertex);

      auto has_valid_primitive = builder.CreateICmpUGT(current_write_vertex, builder.getInt32(1));
      auto add_primitive_count = builder.CreateSelect(
          has_valid_primitive, builder.CreateSub(current_write_vertex, builder.getInt32(1)), zero_const
      );
      auto add_vertex_count = builder.CreateSelect(has_valid_primitive, current_write_vertex, zero_const);
      builder.CreateStore(
          builder.CreateAdd(builder.CreateLoad(types._int, vertex_offset), add_vertex_count), vertex_offset
      );
      builder.CreateStore(
          builder.CreateAdd(builder.CreateLoad(types._int, primitive_count), add_primitive_count), primitive_count
      );
      co_return {};
    };
  } else {
    resource_map.call_emit = [&]() -> IREffect {
      // only one accumulator to maintain, simple one ~
      auto current_write_vertex = builder.CreateLoad(types._int, next_write_vertex);
      builder.CreateStore(builder.CreateAdd(one_const, current_write_vertex), next_write_vertex);

      GSOutputContext gs_out_ctx{current_write_vertex, current_write_vertex};
      for (auto &h : gs_output_handlers) {
        co_yield h(gs_out_ctx);
      }
      co_yield air::call_set_mesh_index(mesh_ptr, current_write_vertex, current_write_vertex);
      co_return {};
    };
    resource_map.call_cut = []() -> IREffect {
      // there is nothing to cut!
      co_return {};
    };
  }

  auto payload = function->getArg(payload_idx);
  auto valid_primitive_mask =
      builder.CreateLoad(types._int, builder.CreateConstInBoundsGEP1_32(types._int, payload, 1));

  auto input_ptr_int4_type =
      llvm::ArrayType::get(types._int4, pVertexStage->max_output_register * vertex_per_primitive);
  resource_map.input.ptr_int4 = builder.CreateAlloca(input_ptr_int4_type);
  resource_map.input.ptr_float4 = builder.CreateBitCast(
      resource_map.input.ptr_int4,
      llvm::ArrayType::get(types._float4, pVertexStage->max_output_register * vertex_per_primitive)->getPointerTo()
  );
  resource_map.input_element_count = pVertexStage->max_output_register;

  resource_map.output.ptr_int4 = builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_output_register));
  resource_map.output.ptr_float4 = builder.CreateBitCast(
      resource_map.output.ptr_int4, llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo()
  );
  resource_map.output_element_count = max_output_register;

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(shader_info, resource_map, types, module, builder);

  builder.CreateCondBr(
      builder.CreateICmpNE(
          builder.CreateAnd(valid_primitive_mask, builder.CreateShl(one_const, primitive_id_in_warp)), zero_const
      ),
      active_, epilogue_bb
  );
  builder.SetInsertPoint(active_);

  auto vertices_ptr = builder.CreateBitCast(
      builder.CreateConstInBoundsGEP1_32(types._int, function->getArg(payload_idx), 4),
      types._int4->getPointerTo((uint32_t)air::AddressSpace::object_data)
  );

  auto load_vertex = [&](pvalue vertex_index, uint32_t vid) {
    for (unsigned reg = 0; reg < pVertexStage->max_output_register; reg++) {
      builder.CreateStore(
          builder.CreateLoad(
              types._int4, builder.CreateGEP(
                               types._int4, vertices_ptr,
                               {builder.CreateAdd(
                                   builder.CreateMul(vertex_index, builder.getInt32(pVertexStage->max_output_register)),
                                   builder.getInt32(reg)
                               )}
                           )
          ),
          builder.CreateGEP(
              input_ptr_int4_type, resource_map.input.ptr_int4,
              {zero_const, builder.getInt32(vid * pVertexStage->max_output_register + reg)}
          )
      );
    }
  };

  if (is_strip) {
    auto leading_vertex_index = primitive_id_in_warp;
    switch (pShaderInternal->gs_input_primitive) {
    case microsoft::D3D10_SB_PRIMITIVE_TRIANGLE: {
      /*
      primitive 0: {0, 1, 2}
      primitive 1: {1, 3, 2}
      ...
      primitive n: {n, n + 1 + (n & 1), n + 2 - (n & 1)}

      I know primitive restart can be broken! just assume the 1st triangle of the thunk is at correct winding
      */
      auto odd_bit = builder.CreateAnd(leading_vertex_index, builder.getInt32(1));
      load_vertex(leading_vertex_index, 0);
      load_vertex(builder.CreateAdd(builder.CreateAdd(leading_vertex_index, builder.getInt32(1)), odd_bit), 1);
      load_vertex(builder.CreateSub(builder.CreateAdd(leading_vertex_index, builder.getInt32(2)), odd_bit), 2);
      break;
    }
    case microsoft::D3D10_SB_PRIMITIVE_LINE: {
      /*
      primitive 0: {0, 1}
      primitive 1: {1, 2}
      ...
      primitive n: {n, n+1}
      */
      load_vertex(leading_vertex_index, 0);
      load_vertex(builder.CreateAdd(leading_vertex_index, builder.getInt32(1)), 1);
      break;
    }
    case microsoft::D3D10_SB_PRIMITIVE_LINE_ADJ: {
      /*
      primitive 0: {0, 1, 2, 3}
      primitive 1: {1, 2, 3, 4}
      ...
      primitive n: {n, n+1, n+2, n+3}
      */
      load_vertex(leading_vertex_index, 0);
      load_vertex(builder.CreateAdd(leading_vertex_index, builder.getInt32(1)), 1);
      load_vertex(builder.CreateAdd(leading_vertex_index, builder.getInt32(2)), 2);
      load_vertex(builder.CreateAdd(leading_vertex_index, builder.getInt32(3)), 3);
      break;
    }
    case microsoft::D3D10_SB_PRIMITIVE_TRIANGLE_ADJ: {
      /*
      assume 16 vertices for 6 primitives:
      primitive 0: {0, (-2), 2, 6, 4, 3}
      primitive 1: {2, 5, 6, 8, 4, 0}
      primitive 2: {4, 2, 6,10, 8, 7}
      primitive 3: {6, 9,10,12, 8, 4}
      primitive 4: {8, 6, 10,14,12,11}
      primitive 5: {10,13,14,(16),12,8}
      ...
      primitive n (inter): {2n, -, 2(n + 1 + (n & 1)), -, 2(n + 2 - (n & 1)), -}
      primitive n (outer1): {-, is_even(n) ? 2n - 2 :2n + 3 : , -, -, -, -} mod 17 (-2 mapped to 15)
      primitive n (outer2): {-, -, -, 6 + 2n, -, -} mod 15 (16 mapped to 1)
      primitive n (outer3): {-, -, -, -, -, is_even(n) ? 2n + 3: 2n - 2}
      */
      auto double_ = [&](pvalue val) { return builder.CreateMul(two_const, val);};
      auto odd_bit = builder.CreateAnd(leading_vertex_index, one_const);
      auto _2n = double_(leading_vertex_index);
      load_vertex(_2n, 0);
      load_vertex(double_(builder.CreateAdd(builder.CreateAdd(leading_vertex_index, one_const), odd_bit)), 2);
      load_vertex(double_(builder.CreateSub(builder.CreateAdd(leading_vertex_index, two_const), odd_bit)), 4);
      auto _2np3 = builder.CreateAdd(_2n, builder.getInt32(3));
      auto _2nm2 = builder.CreateSub(_2n, two_const);
      auto is_odd = builder.CreateICmpEQ(odd_bit, one_const);
      load_vertex(builder.CreateURem(builder.CreateSelect(is_odd, _2np3, _2nm2), builder.getInt32(33)), 1);
      load_vertex(builder.CreateURem(builder.CreateAdd(_2n, builder.getInt32(6)), builder.getInt32(31)), 3);
      load_vertex(builder.CreateSelect(is_odd, _2nm2, _2np3), 5);
      break;
    }
    default:
      return llvm::make_error<UnsupportedFeature>(std::format(
          "unhandled geometry shader input primitive strip: {}", (uint32_t)pShaderInternal->gs_input_primitive
      ));
    }
  } else {
    auto leading_vertex_index = builder.CreateMul(builder.getInt32(vertex_per_primitive), primitive_id_in_warp);
    for (uint32_t vid = 0; vid < vertex_per_primitive; vid++) {
      load_vertex(builder.CreateAdd(leading_vertex_index, builder.getInt32(vid)), vid);
    }
  }

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function, .resource = resource_map,
    .types = types, .pso_sample_mask = 0xffffffff, .shader_type = pShaderInternal->shader_type,
  };

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }

  auto real_entry = convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());
  builder.SetInsertPoint(epilogue_bb);

  if (auto err = epilogue.build(ctx).takeError()) {
    return err;
  }
  if (auto err = resource_map.call_cut().build(ctx).takeError()) {
    return err;
  }
  if (auto err = IRValue(air::call_set_mesh_primitive_count(mesh_ptr, builder.CreateLoad(types._int, primitive_count)))
                     .build(ctx)
                     .takeError()) {
    return err;
  }

  builder.CreateRetVoid();
  module.getOrInsertNamedMetadata("air.mesh")->addOperand(function_metadata);

  return llvm::Error::success();
}

llvm::Error
convert_dxbc_vertex_for_geometry_shader(
    const SM50ShaderInternal *pShaderInternal, const char *name, const SM50ShaderInternal *pGeometryStage,
    llvm::LLVMContext &context, llvm::Module &module, SM50_SHADER_COMPILATION_ARGUMENT_DATA *pArgs
) {
  auto func_signature = pShaderInternal->func_signature; // copy
  auto shader_info = &(pShaderInternal->shader_info);

  bool is_strip = false;

  uint32_t max_input_register = pShaderInternal->max_input_register;
  uint32_t max_output_register = pShaderInternal->max_output_register;
  SM50_SHADER_COMPILATION_ARGUMENT_DATA *arg = pArgs;
  SM50_SHADER_IA_INPUT_LAYOUT_DATA *ia_layout = nullptr;
  // uint64_t debug_id = ~0u;
  while (arg) {
    switch (arg->type) {
    case SM50_SHADER_IA_INPUT_LAYOUT:
      ia_layout = ((SM50_SHADER_IA_INPUT_LAYOUT_DATA *)arg);
      break;    
    case SM50_SHADER_PSO_GEOMETRY_SHADER:
      is_strip = ((SM50_SHADER_PSO_GEOMETRY_SHADER_DATA *)arg)->strip_topology;
      break;
    default:
      break;
    }
    arg = (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)arg->next;
  }

  bool is_triadj_strip = is_strip && pGeometryStage->gs_input_primitive == D3D10_SB_PRIMITIVE_TRIANGLE_ADJ;
  bool is_indexed_draw = ia_layout && ia_layout->index_buffer_format > 0;

  IREffect prologue([](auto) { return std::monostate(); });
  IRValue epilogue([](struct context ctx) -> pvalue {
    auto retTy = ctx.function->getReturnType();
    if (retTy->isVoidTy()) {
      return nullptr;
    }
    return llvm::UndefValue::get(retTy);
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

  uint32_t payload_idx = func_signature.DefineInput(air::InputPayload{.size = 16256});
  // (warp_size, 1, 1)
  uint32_t thread_id_idx = func_signature.DefineInput(air::InputThreadPositionInThreadgroup{});
  // (warp_count, instance_count, 1)
  // warp_count = ceil(index_count / warp_size)
  uint32_t tg_id_idx = func_signature.DefineInput(air::InputThreadgroupPositionInGrid{});
  uint32_t mesh_props_idx = func_signature.DefineInput(air::InputMeshGridProperties{});
  uint32_t draw_argument_idx = func_signature.DefineInput(air::ArgumentBindingBuffer{
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
        .type = ia_layout->index_buffer_format == 1 ? air::MSLRepresentableType(air::MSLUshort{})
                                                    : air::MSLRepresentableType(air::MSLUint{}),
        .raster_order_group = {}
    });
  }

  auto [function, function_metadata] = func_signature.CreateFunction(name, context, module, 0, true);

  auto entry_bb = llvm::BasicBlock::Create(context, "entry", function);
  auto epilogue_bb = llvm::BasicBlock::Create(context, "epilogue", function);
  llvm::IRBuilder<> builder(entry_bb);

  setup_fastmath_flag(module, builder);

  auto index_check = llvm::BasicBlock::Create(context, "index_check", function);
  auto active = llvm::BasicBlock::Create(context, "active", function);
  auto will_dispatch = llvm::BasicBlock::Create(context, "will_dispatch", function);
  auto dispatch = llvm::BasicBlock::Create(context, "dispatch", function);
  auto return_ = llvm::BasicBlock::Create(context, "return", function);

  auto payload = function->getArg(payload_idx);
  auto thread_position_in_group = function->getArg(thread_id_idx);
  auto warp_vertex_id = builder.CreateExtractElement(thread_position_in_group, (uint32_t)0);

  auto threadgroup_position_in_grid = function->getArg(tg_id_idx);
  auto warp_id = builder.CreateExtractElement(threadgroup_position_in_grid, (uint32_t)0);
  auto instance_id = builder.CreateExtractElement(threadgroup_position_in_grid, (uint32_t)1);

  auto draw_arguments = builder.CreateLoad(
    is_indexed_draw ? types._dxmt_draw_indexed_arguments
                    : types._dxmt_draw_arguments,
    function->getArg(draw_argument_idx)
  );

  auto vertex_count = builder.CreateExtractValue(draw_arguments, 0);

  resource_map.input.ptr_int4 = builder.CreateAlloca(llvm::ArrayType::get(types._int4, max_input_register));
  resource_map.input.ptr_float4 = builder.CreateBitCast(
      resource_map.input.ptr_int4, llvm::ArrayType::get(types._float4, max_input_register)->getPointerTo()
  );
  resource_map.input_element_count = max_input_register;

  auto [warp_vertex_count, warp_primitive_count, vertex_per_primitive] =
      get_vertex_primitive_count_in_warp(pGeometryStage->gs_input_primitive, is_strip);

  auto payload_output_ptr = builder.CreateConstInBoundsGEP1_32(
      types._int, payload, 4
  );

  resource_map.output.ptr_int4 = builder.CreateBitCast(
      payload_output_ptr,
      llvm::ArrayType::get(types._int4, max_output_register)->getPointerTo((uint32_t)air::AddressSpace::object_data)
  );
  resource_map.output.ptr_int4 = builder.CreateGEP(
      resource_map.output.ptr_int4->getType()->getNonOpaquePointerElementType(), resource_map.output.ptr_int4,
      {warp_vertex_id}
  );
  resource_map.output.ptr_float4 = builder.CreateBitCast(
      payload_output_ptr,
      llvm::ArrayType::get(types._float4, max_output_register)->getPointerTo((uint32_t)air::AddressSpace::object_data)
  );
  resource_map.output.ptr_float4 = builder.CreateGEP(
      resource_map.output.ptr_float4->getType()->getNonOpaquePointerElementType(), resource_map.output.ptr_float4,
      {warp_vertex_id}
  );

  resource_map.output_element_count = max_output_register;

  llvm::GlobalVariable *valid_vertex_mask = new llvm::GlobalVariable(
      module, types._int, false, llvm::GlobalValue::InternalLinkage, llvm::Constant::getNullValue(types._int),
      "valid_vertex_mask", nullptr, llvm::GlobalValue::NotThreadLocal, (uint32_t)air::AddressSpace::threadgroup
  );

  setup_temp_register(shader_info, resource_map, types, module, builder);
  setup_immediate_constant_buffer(shader_info, resource_map, types, module, builder);

  struct context ctx {
    .builder = builder, .llvm = context, .module = module, .function = function, .resource = resource_map,
    .types = types, .pso_sample_mask = 0xffffffff, .shader_type = pShaderInternal->shader_type,
  };

  auto global_index_id =
      builder.CreateAdd(builder.CreateMul(warp_id, builder.getInt32(warp_vertex_count)), warp_vertex_id);

  if (is_triadj_strip) {
    // 2nd and 32th vertex are not read but 33th and -2th (or 1st) are.
    auto const one_const = builder.getInt32(1);
    auto signed_max = [&](pvalue a, pvalue b) {
      return builder.CreateSelect(builder.CreateICmpSLT(a, b), b, a);
    };
    auto signed_min = [&](pvalue a, pvalue b) {
      return builder.CreateSelect(builder.CreateICmpSLT(a, b), a, b);
    };

    auto is_second_thread = builder.CreateICmpEQ(warp_vertex_id, one_const);
    global_index_id = builder.CreateSelect(
      is_second_thread,
      builder.CreateAdd(global_index_id, builder.getInt32(31)), global_index_id
    );
    auto is_last_thread = builder.CreateICmpEQ(warp_vertex_id, builder.getInt32(31));
    global_index_id = builder.CreateSelect(
      is_last_thread,
      signed_max(one_const, builder.CreateSub(global_index_id, builder.getInt32(33))),
      signed_min(global_index_id, builder.CreateSub(vertex_count, one_const)) // may degenerate
    );
  }

  // explicit initialization
  builder.CreateAtomicRMW(
      llvm::AtomicRMWInst::BinOp::And, valid_vertex_mask, builder.getInt32(0), llvm::Align(4),
      llvm::AtomicOrdering::Monotonic
  );

  builder.CreateCondBr(
      builder.CreateICmp(llvm::CmpInst::ICMP_ULT, global_index_id, vertex_count), index_check, will_dispatch
  );
  builder.SetInsertPoint(index_check);

  if (index_buffer_idx != ~0u) {
    auto start_index = builder.CreateExtractValue(draw_arguments, 2);
    auto index_buffer = function->getArg(index_buffer_idx);
    auto index_buffer_element_type = index_buffer->getType()->getNonOpaquePointerElementType();
    auto vertex_id = builder.CreateLoad(
        index_buffer_element_type,
        builder.CreateGEP(index_buffer_element_type, index_buffer, {builder.CreateAdd(start_index, global_index_id)})
    );
    resource_map.vertex_id = builder.CreateZExt(vertex_id, types._int);

    builder.CreateCondBr(
        builder.CreateICmp(llvm::CmpInst::ICMP_NE, builder.getInt32(-1), resource_map.vertex_id), active, will_dispatch
    );
  } else {
    resource_map.vertex_id = global_index_id;
    builder.CreateBr(active);
  }

  builder.SetInsertPoint(active);

  builder.CreateAtomicRMW(
      llvm::AtomicRMWInst::BinOp::Or, valid_vertex_mask, builder.CreateShl(builder.getInt32(1), warp_vertex_id),
      llvm::Align(4), llvm::AtomicOrdering::Monotonic
  );

  resource_map.base_vertex_id = builder.CreateExtractValue(draw_arguments, is_indexed_draw ? 3 : 2);
  resource_map.instance_id = instance_id;
  resource_map.vertex_id_with_base = builder.CreateAdd(resource_map.vertex_id, resource_map.base_vertex_id);
  resource_map.base_instance_id = builder.CreateExtractValue(draw_arguments, is_indexed_draw ? 4 : 3);
  resource_map.instance_id_with_base = builder.CreateAdd(resource_map.instance_id, resource_map.base_instance_id);

  if (auto err = prologue.build(ctx).takeError()) {
    return err;
  }

  auto real_entry = convert_basicblocks(pShaderInternal->entry, ctx, epilogue_bb);
  if (auto err = real_entry.takeError()) {
    return err;
  }
  builder.CreateBr(real_entry.get());

  builder.SetInsertPoint(epilogue_bb);
  auto epilogue_result = epilogue.build(ctx);
  if (auto err = epilogue_result.takeError()) {
    return err;
  }

  builder.CreateBr(will_dispatch);

  builder.SetInsertPoint(will_dispatch);

  if (auto err = call_threadgroup_barrier(mem_flags::threadgroup).build(ctx).takeError()) {
    return err;
  }

  builder.CreateCondBr(
      builder.CreateICmp(llvm::CmpInst::ICMP_EQ, warp_vertex_id, builder.getInt32(0)), dispatch, return_
  );
  builder.SetInsertPoint(dispatch);

  // TODO: dispatch phase

  /*
   * calculate valid primitive mask
   * calculate primitive indices
   * pass instance id
   * pass primitive_id_base
   */

  pvalue valid_primitive_mask = builder.getInt32(0);
  auto valid_vertex_result = builder.CreateLoad(types._int, valid_vertex_mask);

  for (unsigned primitive_id = 0; primitive_id < warp_primitive_count; primitive_id++) {
    pvalue primitive_valid_result = builder.getInt1(1);
    uint32_t primitive_vertices_mask = 0;
    // TODO: check primitive is valid
    if (is_strip) {
      switch (pGeometryStage->gs_input_primitive) {
      case microsoft::D3D10_SB_PRIMITIVE_TRIANGLE: {
        primitive_vertices_mask = 0b111 << primitive_id;
        break;
      }
      case microsoft::D3D10_SB_PRIMITIVE_LINE: {
        primitive_vertices_mask = 0b11 << primitive_id;
        break;
      }
      case microsoft::D3D10_SB_PRIMITIVE_LINE_ADJ: {
        primitive_vertices_mask = 0b1111 << primitive_id;
        break;
      }
      case microsoft::D3D10_SB_PRIMITIVE_TRIANGLE_ADJ: {
        auto odd_primitive = primitive_id & 1;
        auto _2n = primitive_id << 1;
        primitive_vertices_mask |= 1 << 2 * primitive_id;
        primitive_vertices_mask |= 1 << 2 * (primitive_id + 1 + odd_primitive);
        primitive_vertices_mask |= 1 << 2 * (primitive_id + 2 - odd_primitive);
        primitive_vertices_mask |= 1 << ((odd_primitive ? _2n + 3 : _2n - 2) % 33);
        primitive_vertices_mask |= 1 << ((6 + 2 * primitive_id) % 31);
        primitive_vertices_mask |= 1 << (odd_primitive ? _2n - 2 : _2n + 3);
        break;
      }
      default:
        return llvm::make_error<UnsupportedFeature>(std::format(
            "unhandled geometry shader input primitive strip: {}", (uint32_t)pGeometryStage->gs_input_primitive
        ));
      }
    } else {
      // primitive is valid if all vertex is valid
      for (unsigned vid_in_prim = 0; vid_in_prim < vertex_per_primitive; vid_in_prim++) {
        primitive_vertices_mask |= (1 << (vid_in_prim + vertex_per_primitive * primitive_id));
      }
    }
    primitive_valid_result = builder.CreateICmpEQ(
        builder.CreateAnd(builder.getInt32(primitive_vertices_mask), valid_vertex_result),
        builder.getInt32(primitive_vertices_mask)
    );

    valid_primitive_mask = builder.CreateOr(
        valid_primitive_mask,
        builder.CreateShl(builder.CreateZExt(primitive_valid_result, ctx.types._int), builder.getInt32(primitive_id))
    );
  }

  builder.CreateStore(instance_id, builder.CreateConstInBoundsGEP1_32(types._int, payload, 0));

  builder.CreateStore(valid_primitive_mask, builder.CreateConstInBoundsGEP1_32(types._int, payload, 1));

  if (auto err = air::call_set_mesh_properties(
                     function->getArg(mesh_props_idx),
                     llvm::ConstantVector::get(
                         {llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, warp_primitive_count}),
                          llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, pGeometryStage->gs_instance_count}),
                          llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, 1})}
                     )
      )
                     .build({.llvm = context, .module = module, .builder = builder, .types = types})
                     .takeError()) {
    return err;
  }

  builder.CreateBr(return_);

  builder.SetInsertPoint(return_);

  builder.CreateRetVoid();

  module.getOrInsertNamedMetadata("air.object")->addOperand(function_metadata);
  return llvm::Error::success();
};

} // namespace dxmt::dxbc

template <> struct environment_cast<::dxmt::dxbc::context, ::dxmt::air::AIRBuilderContext> {
  ::dxmt::air::AIRBuilderContext
  cast(const ::dxmt::dxbc::context &src) {
    return {src.llvm, src.module, src.builder, src.types};
  };
};
