#include "dxbc_converter.hpp"
#include <format>

using namespace microsoft;
using namespace dxmt::air;
using namespace dxmt::shader::common;

namespace dxmt::dxbc {

inline air::Interpolation
to_air_interpolation(microsoft::D3D10_SB_INTERPOLATION_MODE mode) {
  using namespace microsoft;
  switch (mode) {
  case D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_SAMPLE:
    return air::Interpolation::sample_no_perspective;
  case D3D10_SB_INTERPOLATION_LINEAR_SAMPLE:
    return air::Interpolation::sample_perspective;
  case D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE_CENTROID:
    return air::Interpolation::centroid_no_perspective;
  case D3D10_SB_INTERPOLATION_LINEAR_CENTROID:
    return air::Interpolation::centroid_perspective;
  case D3D10_SB_INTERPOLATION_CONSTANT:
    return air::Interpolation::flat;
  case D3D10_SB_INTERPOLATION_LINEAR:
    return air::Interpolation::center_perspective;
  case D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE:
    return air::Interpolation::center_no_perspective;
  default:
    assert(0 && "Unexpected D3D10_SB_INTERPOLATION_MODE");
  }
}

void handle_signature_vs(
  CSignatureParser &inputParser, const CSignatureParser &outputParser,
  D3D10ShaderBinary::CInstruction &Inst, SM50ShaderInternal *sm50_shader,
  uint32_t phase_unused
) {
  uint32_t &max_input_register = sm50_shader->max_input_register;
  uint32_t &max_output_register = sm50_shader->max_output_register;
  auto &signature_handlers = sm50_shader->signature_handlers;
  auto &func_signature = sm50_shader->func_signature;

  auto findInputElement = [&](auto matcher) -> Signature {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    inputParser.GetParameters(&parameters);
    for (unsigned i = 0; i < inputParser.GetNumParameters(); i++) {
      auto sig = Signature(parameters[i]);
      if (matcher(sig)) {
        return sig;
      }
    }
    assert(inputParser.GetNumParameters());
    assert(0 && "try to access an undefined input");
  };
  auto findOutputElement = [&](auto matcher) -> Signature {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    outputParser.GetParameters(&parameters);
    for (unsigned i = 0; i < outputParser.GetNumParameters(); i++) {
      auto sig = Signature(parameters[i]);
      if (matcher(sig)) {
        return sig;
      }
    }
    assert(0 && "try to access an undefined output");
  };

  switch (Inst.m_OpCode) {
#ifdef VBOX
  case D3D10_SB_OPCODE_DCL_INPUT_SIV:
#endif
  case D3D10_SB_OPCODE_DCL_INPUT_SGV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    // auto MinPrecision = Inst.m_Operands[0].m_MinPrecision; // not used
    auto sgv = Inst.m_InputDeclSGV.Name;
    switch (sgv) {
    case D3D10_SB_NAME_VERTEX_ID: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect_bind([=](struct context ctx) {
          return store_at_vec4_array_masked(
            ctx.resource.input.ptr_int4, ctx.builder.getInt32(reg),
            ctx.resource.vertex_id, mask
          );
        });
      });
      break;
    }
    case D3D10_SB_NAME_INSTANCE_ID: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect_bind([=](struct context ctx) {
          return store_at_vec4_array_masked(
            ctx.resource.input.ptr_int4, ctx.builder.getInt32(reg),
            ctx.resource.instance_id, mask
          );
        });
      });
      break;
    }
    default:
      assert(0 && "Unexpected/unhandled input system value");
      break;
    }
    max_input_register = std::max(reg + 1, max_input_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;

    switch (RegType) {
    case D3D10_SB_OPERAND_TYPE_INPUT: {
      unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
      assert(Inst.m_Operands[0].m_IndexDimension == D3D10_SB_OPERAND_INDEX_1D);

      auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
      auto sig = findInputElement([=](Signature &sig) {
        return (sig.reg() == reg) && ((sig.mask() & mask) != 0);
      });
      signature_handlers.push_back(
        [=, type = (InputAttributeComponentType)sig.componentType(),
         name = sig.fullSemanticString()](SignatureContext &ctx) {
          if (ctx.ia_layout) {
            for (unsigned i = 0; i < ctx.ia_layout->num_elements; i++) {
              if (ctx.ia_layout->elements[i].reg == reg) {
                ctx.prologue << pull_vertex_input(
                  ctx.func_signature, reg, mask, ctx.ia_layout->elements[i],
                  ctx.ia_layout->slot_mask
                );
                break;
              }
            }
          } else {
            auto assigned_index = ctx.func_signature.DefineInput(
              InputVertexStageIn{.attribute = reg, .type = type, .name = name}
            );
            ctx.prologue << init_input_reg(assigned_index, reg, mask);
          }
        }
      );
      max_input_register = std::max(reg + 1, max_input_register);
      break;
    }
    default:
      assert(0 && "unhandled vertex shader input");
      break;
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SIV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_OutputDeclSIV.Name;
    switch (siv) {
    case D3D10_SB_NAME_CLIP_DISTANCE: {
      for (unsigned i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
          sm50_shader->clip_distance_scalars.push_back(
            {.component = (uint8_t)(i), .reg = (uint8_t)reg}
          );
        }
      }
      uint32_t assigned_index = func_signature.DefineOutput(OutputVertex{
        .user = std::format("SV_ClipDistance{}", reg),
        .type = msl_float4,
      });
      signature_handlers.push_back([=](SignatureContext &ctx) {
        if (ctx.skip_vertex_output)
          return;
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      max_output_register = std::max(reg + 1, max_output_register);
      break;
    }
    case D3D10_SB_NAME_CULL_DISTANCE:
      assert(0 && "Metal doesn't support shader output: cull distance");
      break;
    case D3D10_SB_NAME_POSITION: {
      auto assigned_index =
        func_signature.DefineOutput(OutputPosition{.type = msl_float4});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        if (ctx.skip_vertex_output)
          return;
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      break;
    }
    case D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX: {
      auto assigned_index =
        func_signature.DefineOutput(OutputRenderTargetArrayIndex{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        if (ctx.skip_vertex_output)
          return;
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      break;
    }
    case D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX: {
      auto assigned_index =
        func_signature.DefineOutput(OutputViewportArrayIndex{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        if (ctx.skip_vertex_output)
          return;
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      break;
    }
    default:
      assert(0 && "Unexpected/unhandled input system value");
      break;
    }
    max_output_register = std::max(reg + 1, max_output_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;
    switch (RegType) {
    case D3D10_SB_OPERAND_TYPE_OUTPUT: {
      // normal output register
      auto reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
      auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
      auto sig = findOutputElement([=](Signature sig) {
        return (sig.reg() == reg) && ((sig.mask() & mask) != 0);
      });
      uint32_t assigned_index = func_signature.DefineOutput(OutputVertex{
        .user = sig.fullSemanticString(),
        .type = to_msl_type(sig.componentType()),
      });
      signature_handlers.push_back([=](SignatureContext &ctx) {
        if (ctx.skip_vertex_output)
          return;
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      max_output_register = std::max(reg + 1, max_output_register);
      break;
    }
    default:
      assert(0 && "unhandled vertex shader output");
      break;
    }
    break;
  }
  default:
    assert(0);
    break;
  }
};

void handle_signature_ps(
  CSignatureParser &inputParser, const CSignatureParser &outputParser,
  D3D10ShaderBinary::CInstruction &Inst, SM50ShaderInternal *sm50_shader,
  uint32_t phase_unused
) {
  uint32_t &max_input_register = sm50_shader->max_input_register;
  uint32_t &max_output_register = sm50_shader->max_output_register;
  auto &signature_handlers = sm50_shader->signature_handlers;
  auto &func_signature = sm50_shader->func_signature;

  auto findInputElement = [&](auto matcher) -> Signature {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    inputParser.GetParameters(&parameters);
    for (unsigned i = 0; i < inputParser.GetNumParameters(); i++) {
      auto sig = Signature(parameters[i]);
      if (matcher(sig)) {
        return sig;
      }
    }
    assert(inputParser.GetNumParameters());
    assert(0 && "try to access an undefined input");
  };
  auto findOutputElement = [&](auto matcher) -> Signature {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    outputParser.GetParameters(&parameters);
    for (unsigned i = 0; i < outputParser.GetNumParameters(); i++) {
      auto sig = Signature(parameters[i]);
      if (matcher(sig)) {
        return sig;
      }
    }
    assert(0 && "try to access an undefined output");
  };

  switch (Inst.m_OpCode) {
  case D3D10_SB_OPCODE_DCL_INPUT_SIV: {
    assert(0);
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT_SGV:
    assert(0);
    break;
  case D3D10_SB_OPCODE_DCL_INPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;

    switch (RegType) {
    case D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK: {
      auto assigned_index = func_signature.DefineInput(InputInputCoverage{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect([=](struct context ctx) {
          auto attr = ctx.function->getArg(assigned_index);
          ctx.resource.coverage_mask_arg = attr;
          return std::monostate{};
        });
      });
      break;
    }
    case D3D11_SB_OPERAND_TYPE_INNER_COVERAGE:
      assert(0);
      break;
    default:
      assert(0 && "unimplemented vertex input");
      break;
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT_PS_SIV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_InputPSDeclSIV.Name;
    auto interpolation =
      to_air_interpolation(Inst.m_InputPSDeclSIV.InterpolationMode);
    uint32_t assigned_index;
    switch (siv) {
    case D3D10_SB_NAME_POSITION:
      // assert(
      //   interpolation == Interpolation::center_no_perspective ||
      //   // in case it's per-sample, FIXME: will this cause problem?
      //   interpolation == Interpolation::sample_no_perspective
      // );
      assigned_index = func_signature.DefineInput(
        // the only supported interpolation for [[position]]
        InputPosition{.interpolation = interpolation}
      );
      break;
    case D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX:
      assert(interpolation == Interpolation::flat);
      assigned_index =
        func_signature.DefineInput(InputRenderTargetArrayIndex{});
      break;
    case D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX:
      assert(interpolation == Interpolation::flat);
      assigned_index = func_signature.DefineInput(InputViewportArrayIndex{});
      break;
    case D3D10_SB_NAME_CLIP_DISTANCE:
      assigned_index = func_signature.DefineInput(InputFragmentStageIn{
        .user = std::format("SV_ClipDistance{}", reg),
        .type = msl_float4,
        .interpolation = interpolation,
        .pull_mode = false
      });
      break;
    default:
      assert(0 && "Unexpected/unhandled input system value");
      break;
    }
    signature_handlers.push_back([=](SignatureContext &ctx) {
      ctx.prologue << init_input_reg(
        assigned_index, reg, mask, siv == D3D10_SB_NAME_POSITION
      );
    });
    max_input_register = std::max(reg + 1, max_input_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT_PS_SGV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_InputPSDeclSIV.Name;
    auto interpolation =
      to_air_interpolation(Inst.m_InputPSDeclSGV.InterpolationMode);
    uint32_t assigned_index;
    switch (siv) {
    case microsoft::D3D10_SB_NAME_IS_FRONT_FACE:
      assert(interpolation == Interpolation::flat);
      assigned_index = func_signature.DefineInput(InputFrontFacing{});
      break;
    case microsoft::D3D10_SB_NAME_SAMPLE_INDEX:
      assert(interpolation == Interpolation::flat);
      assigned_index = func_signature.DefineInput(InputSampleIndex{});
      break;
    case microsoft::D3D10_SB_NAME_PRIMITIVE_ID:
      assigned_index = func_signature.DefineInput(InputPrimitiveID{});
      break;
#ifdef VBOX
    case microsoft::D3D10_SB_NAME_POSITION:
      assigned_index = func_signature.DefineInput(InputPosition{ .interpolation = interpolation});
      break;
#endif
    default:
      assert(0 && "Unexpected/unhandled input system value");
      break;
    }
    signature_handlers.push_back([=](SignatureContext &ctx) {
      ctx.prologue << init_input_reg(assigned_index, reg, mask);
    });
    max_input_register = std::max(reg + 1, max_input_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT_PS: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto interpolation =
      to_air_interpolation(Inst.m_InputPSDecl.InterpolationMode);
    auto sig = findInputElement([=](Signature sig) {
      return (sig.reg() == reg) && ((sig.mask() & mask) != 0);
    });
    signature_handlers.push_back([=, type = sig.componentType(), name = sig.fullSemanticString()]
    (SignatureContext &ctx) {
      bool pull_mode = bool(ctx.pull_mode_reg_mask & (1 << reg)) && interpolation != air::Interpolation::flat;
      auto assigned_index = ctx.func_signature.DefineInput(InputFragmentStageIn{
        .user = name, .type = to_msl_type(type), .interpolation = interpolation, .pull_mode = pull_mode
      });
      if (pull_mode) {
        assert(type == RegisterComponentType::Float && "otherwise the input register contains mixed data type");
        ctx.resource.interpolant_map[reg] = interpolant_descriptor{
          [=](auto) {
            return make_irvalue([=](struct context ctx) {
              return ctx.function->getArg(assigned_index);
            });
          },
          (interpolation == air::Interpolation::sample_perspective ||
           interpolation == air::Interpolation::center_perspective ||
           interpolation == air::Interpolation::centroid_perspective)
        };
        uint32_t sampleidx_at = ~0u;
        switch (interpolation) {
        case Interpolation::sample_perspective:
        case Interpolation::sample_no_perspective:
          sampleidx_at = ctx.func_signature.DefineInput(InputSampleIndex{});
          break;
        default:
          break;
        }
        ctx.prologue << init_input_reg_with_interpolation(assigned_index, reg, mask, interpolation, sampleidx_at);
      } else {
        ctx.prologue << init_input_reg(assigned_index, reg, mask);
      }
    });
    max_input_register = std::max(reg + 1, max_input_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SGV: {
    assert(0 && "dcl_output_sgv should not happen for now");
    // only GS PrimitiveID uses this, but we don't support GS for now
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SIV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    // auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_OutputDeclSIV.Name;
    switch (siv) {
    default:
      assert(0 && "Unexpected/unhandled input system value");
      break;
    }
    max_output_register = std::max(reg + 1, max_output_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;
    switch (RegType) {
    case D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH:
    case D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_GREATER_EQUAL:
    case D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_LESS_EQUAL: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect([](struct context ctx) -> std::monostate {
          assert(
            ctx.resource.depth_output_reg == nullptr &&
            "otherwise oDepth is defined twice"
          );
          ctx.resource.depth_output_reg =
            ctx.builder.CreateAlloca(ctx.types._float);
          return {};
        });
      });
      signature_handlers.push_back([=](SignatureContext &ctx) {
        if (ctx.disable_depth_output)
          return;
        auto assigned_index = ctx.func_signature.DefineOutput(OutputDepth{
          .depth_argument =
            RegType == D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_GREATER_EQUAL
              ? DepthArgument::greater
            : RegType == D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_LESS_EQUAL
              ? DepthArgument::less
              : DepthArgument::any
        });
        ctx.epilogue >> [=](pvalue v) {
          return make_irvalue([=](struct context ctx) {
            return ctx.builder.CreateInsertValue(
              v,
              ctx.builder.CreateLoad(
                ctx.types._float,
                ctx.builder.CreateConstInBoundsGEP1_32(
                  ctx.types._float, ctx.resource.depth_output_reg, 0
                )
              ),
              {assigned_index}
            );
          });
        };
      });
      break;
    }
    case D3D11_SB_OPERAND_TYPE_OUTPUT_STENCIL_REF: {
      assert(0 && "todo");
      break;
    }
    case D3D10_SB_OPERAND_TYPE_OUTPUT_COVERAGE_MASK: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect([](struct context ctx) -> std::monostate {
          assert(
            ctx.resource.coverage_mask_reg == nullptr &&
            "otherwise oMask is defined twice"
          );
          ctx.resource.coverage_mask_reg =
            ctx.builder.CreateAlloca(ctx.types._int);
          return {};
        });
      });
      auto assigned_index = func_signature.DefineOutput(OutputCoverageMask{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >> [=](pvalue v) {
          return make_irvalue([=](struct context ctx) {
            auto odepth = ctx.builder.CreateLoad(
              ctx.types._int,
              ctx.builder.CreateConstInBoundsGEP1_32(
                ctx.types._int, ctx.resource.coverage_mask_reg, 0
              )
            );
            return ctx.builder.CreateInsertValue(
              v,
              ctx.pso_sample_mask != 0xffffffff
                ? ctx.builder.CreateAnd(odepth, ctx.pso_sample_mask)
                : odepth,
              {assigned_index}
            );
          });
        };
      });
      break;
    }

    case D3D10_SB_OPERAND_TYPE_OUTPUT: {
      // normal output register
      auto reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
      auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
      auto sig = findOutputElement([=](Signature sig) {
        return (sig.reg() == reg) && ((sig.mask() & mask) != 0);
      });
      auto type = sig.componentType();
      signature_handlers.push_back([=](SignatureContext &ctx) {
        uint32_t assigned_index;
        if (ctx.dual_source_blending) {
          if (reg > 1 || reg < 0)
            return;
          assigned_index = ctx.func_signature.DefineOutput(OutputRenderTarget{
            .dual_source_blending = true,
            .index = reg,
            .type = to_msl_type(type),
          });
        } else {
          assigned_index = ctx.func_signature.DefineOutput(OutputRenderTarget{
            .dual_source_blending = false,
            .index = reg,
            .type = to_msl_type(type),
          });
        }
        if (type == RegisterComponentType::Float && ctx.unorm_output_reg_mask & (1 << reg))
          ctx.epilogue >> pop_output_reg_fix_unorm(reg, mask, assigned_index);
        else
          ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      max_output_register = std::max(reg + 1, max_output_register);
      break;
    }
    default:
      assert(0 && "unhandled pixel shader output");
      break;
    }
    break;
  }
  default:
    assert(0 && "unhandled pixel shader output");
    break;
  }
};

void handle_signature_hs(
  CSignatureParser &inputParser, const CSignatureParser &outputParser,
  D3D10ShaderBinary::CInstruction &Inst, SM50ShaderInternal *sm50_shader,
  uint32_t phase
) {
  uint32_t &max_input_register = sm50_shader->max_input_register;
  uint32_t &max_output_register = sm50_shader->max_output_register;
  uint32_t &max_patch_constant_output_register =
    sm50_shader->max_patch_constant_output_register;
  auto &signature_handlers = sm50_shader->signature_handlers;

  switch (Inst.m_OpCode) {
  case D3D10_SB_OPCODE_DCL_INPUT_SGV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    // auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    // auto MinPrecision = Inst.m_Operands[0].m_MinPrecision; // not used
    auto sgv = Inst.m_InputDeclSGV.Name;
    switch (sgv) {
    default:
      assert(0 && "Unexpected/unhandled hull shader sgv");
      break;
    }
    max_input_register = std::max(reg + 1, max_input_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;

    switch (RegType) {
    case D3D11_SB_OPERAND_TYPE_OUTPUT_CONTROL_POINT_ID:
    case D3D11_SB_OPERAND_TYPE_INPUT_FORK_INSTANCE_ID:
    case D3D11_SB_OPERAND_TYPE_INPUT_JOIN_INSTANCE_ID:
    case D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID: {
      break;
    }
    case D3D10_SB_OPERAND_TYPE_INPUT: {
      break;
    }
    case D3D11_SB_OPERAND_TYPE_INPUT_PATCH_CONSTANT: {
      break;
    }
    case D3D11_SB_OPERAND_TYPE_INPUT_CONTROL_POINT: {
      break;
    }
    case D3D11_SB_OPERAND_TYPE_OUTPUT_CONTROL_POINT: {
      sm50_shader->shader_info.output_control_point_read = true;
      break;
    }

    default:
      assert(0 && "unhandled hull shader input");
      break;
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SGV: {
    assert(0 && "dcl_output_sgv should not happen for now");
    // only GS PrimitiveID uses this, but we don't support GS for now
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SIV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_OutputDeclSIV.Name;
    switch (siv) {
    case D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_V_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_U_EQ_1_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_V_EQ_1_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_U_INSIDE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_V_INSIDE_TESSFACTOR: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >>
          pop_output_tess_factor(
            reg, mask, (siv - D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR),
            6
          );
      });
      break;
    }
    case D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_TRI_V_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_TRI_W_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_TRI_INSIDE_TESSFACTOR: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >>
          pop_output_tess_factor(
            reg, mask, (siv - D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR), 4
          );
      });
      break;
    }
    case D3D11_SB_NAME_FINAL_LINE_DENSITY_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_LINE_DETAIL_TESSFACTOR: {
      // TODO: isoline tessellation
      break;
    }
    default:
      assert(0 && "Unexpected/unhandled hull shader output siv");
      break;
    }
    if (phase != ~0u) {
      max_patch_constant_output_register =
        std::max(reg + 1, max_patch_constant_output_register);
    } else {
      max_output_register = std::max(reg + 1, max_output_register);
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;
    switch (RegType) {
    case D3D10_SB_OPERAND_TYPE_OUTPUT: {
      // normal output register
      auto reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
      auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
      if (phase != ~0u) {
        assert(
          Inst.m_Operands[0].m_IndexDimension == D3D10_SB_OPERAND_INDEX_1D
        );
        for (unsigned i = 0; i < 4; i++) {
          if (mask & (1 << i)) {
            sm50_shader->patch_constant_scalars.push_back(
              {.component = (uint8_t)(i), .reg = (uint8_t)reg}
            );
          }
        }
        max_patch_constant_output_register =
          std::max(reg + 1, max_patch_constant_output_register);
      } else {
        max_output_register = std::max(reg + 1, max_output_register);
      }
      break;
    }
    default:
      assert(0 && "unhandled hull shader output");
      break;
    }
    break;
  }
  default:
    assert(0);
    break;
  }
};

void handle_signature_ds(
  CSignatureParser &inputParser, const CSignatureParser &outputParser,
  D3D10ShaderBinary::CInstruction &Inst, SM50ShaderInternal *sm50_shader,
  uint32_t phase_unused
) {
  uint32_t &max_input_register = sm50_shader->max_input_register;
  uint32_t &max_output_register = sm50_shader->max_output_register;
  auto &signature_handlers = sm50_shader->signature_handlers;
  auto &func_signature = sm50_shader->func_signature;

  auto findOutputElement = [&](auto matcher) -> Signature {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    outputParser.GetParameters(&parameters);
    for (unsigned i = 0; i < outputParser.GetNumParameters(); i++) {
      auto sig = Signature(parameters[i]);
      if (matcher(sig)) {
        return sig;
      }
    }
    assert(0 && "try to access an undefined output");
  };

  switch (Inst.m_OpCode) {
  case D3D10_SB_OPCODE_DCL_INPUT_SIV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_OutputDeclSIV.Name;
    switch (siv) {
    case D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_V_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_U_EQ_1_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_V_EQ_1_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_U_INSIDE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_QUAD_V_INSIDE_TESSFACTOR: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << init_tess_factor_patch_constant(
          reg, mask, (siv - D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR), 6
        );
      });
      max_input_register = std::max(reg + 1, max_input_register);
      break;
    }
    case D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_TRI_V_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_TRI_W_EQ_0_EDGE_TESSFACTOR:
    case D3D11_SB_NAME_FINAL_TRI_INSIDE_TESSFACTOR: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << init_tess_factor_patch_constant(
          reg, mask, (siv - D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR), 4
        );
      });
      max_input_register = std::max(reg + 1, max_input_register);
      break;
    }
    default:
      assert(0 && "unhandled dcl_input_siv in domain shader");
    }

    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT_SGV: {
    assert(0);
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;

    switch (RegType) {
    case D3D11_SB_OPERAND_TYPE_INPUT_DOMAIN_POINT: {
      signature_handlers.push_back([=](SignatureContext &ctx) {
        auto assigned_index = ctx.func_signature.DefineInput(
          InputPositionInPatch{.patch = ctx.func_signature.GetPatchType()}
        );
        ctx.prologue << make_effect([=](struct context ctx) {
          auto attr = ctx.function->getArg(assigned_index);
          ctx.resource.domain = attr;
          return std::monostate{};
        });
      });
      break;
    }
    case D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID: {
      // `convert_dxbc_domain_shader()` will always define [[patch_id]] and initialize `io_binding_map.patch_id` field
      break;
    }
    case D3D10_SB_OPERAND_TYPE_INPUT:
      assert(0);
      break;
    case D3D11_SB_OPERAND_TYPE_INPUT_PATCH_CONSTANT: {
      unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
      assert(Inst.m_Operands[0].m_IndexDimension == D3D10_SB_OPERAND_INDEX_1D);
      max_input_register = std::max(reg + 1, max_input_register);
      break;
    }
    case D3D11_SB_OPERAND_TYPE_INPUT_CONTROL_POINT:
      break;
    default:
      assert(0 && "unhandeld domain shader input");
      break;
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SGV: {
    assert(0 && "dcl_output_sgv should not happen for now");
    // only GS PrimitiveID uses this, but we don't support GS for now
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SIV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_OutputDeclSIV.Name;
    switch (siv) {
    case D3D10_SB_NAME_CLIP_DISTANCE: {
      for (unsigned i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
          sm50_shader->clip_distance_scalars.push_back(
            {.component = (uint8_t)(i), .reg = (uint8_t)reg}
          );
        }
      }
      uint32_t assigned_index = func_signature.DefineOutput(OutputVertex{
        .user = std::format("SV_ClipDistance{}", reg),
        .type = msl_float4,
      });
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      max_output_register = std::max(reg + 1, max_output_register);
      break;
    }
    case D3D10_SB_NAME_CULL_DISTANCE:
      assert(0 && "Metal doesn't support shader output: cull distance");
      break;
    case D3D10_SB_NAME_POSITION: {
      auto assigned_index =
        func_signature.DefineOutput(OutputPosition{.type = msl_float4});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      break;
    }
    case D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX: {
      auto assigned_index =
        func_signature.DefineOutput(OutputRenderTargetArrayIndex{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      break;
    }
    case D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX: {
      auto assigned_index =
        func_signature.DefineOutput(OutputViewportArrayIndex{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      break;
    }
    default:
      assert(0 && "Unexpected/unhandled input system value");
      break;
    }
    max_output_register = std::max(reg + 1, max_output_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;
    switch (RegType) {
    case D3D10_SB_OPERAND_TYPE_OUTPUT: {
      // normal output register
      auto reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
      auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
      auto sig = findOutputElement([=](Signature sig) {
        return (sig.reg() == reg) && ((sig.mask() & mask) != 0);
      });
      uint32_t assigned_index = func_signature.DefineOutput(OutputVertex{
        .user = sig.fullSemanticString(),
        .type = to_msl_type(sig.componentType()),
      });
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.epilogue >> pop_output_reg(reg, mask, assigned_index);
      });
      max_output_register = std::max(reg + 1, max_output_register);
      break;
    }
    default:
      assert(0 && "unhandled domain shader output");
      break;
    }
    break;
  }
  default:
    assert(0);
    break;
  }
};

void handle_signature_cs(
  CSignatureParser &inputParser_unused, const CSignatureParser &outputParser_unused,
  D3D10ShaderBinary::CInstruction &Inst, SM50ShaderInternal *sm50_shader,
  uint32_t phase_unused
) {
  auto &signature_handlers = sm50_shader->signature_handlers;
  auto &func_signature = sm50_shader->func_signature;

  switch (Inst.m_OpCode) {
  case D3D10_SB_OPCODE_DCL_INPUT_SGV: {
    assert(0 && "unhandled compute shader sgv");
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;

    switch (RegType) {
    case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID: {
      auto assigned_index =
        func_signature.DefineInput(InputThreadPositionInGrid{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect([=](struct context ctx) {
          auto attr = ctx.function->getArg(assigned_index);
          ctx.resource.thread_id_arg = attr;
          return std::monostate{};
        });
      });
      break;
    }
    case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_GROUP_ID: {
      auto assigned_index =
        func_signature.DefineInput(InputThreadgroupPositionInGrid{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect([=](struct context ctx) {
          auto attr = ctx.function->getArg(assigned_index);
          ctx.resource.thread_group_id_arg = attr;
          return std::monostate{};
        });
      });
      break;
    }
    case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID_IN_GROUP: {
      auto assigned_index =
        func_signature.DefineInput(InputThreadPositionInThreadgroup{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect([=](struct context ctx) {
          auto attr = ctx.function->getArg(assigned_index);
          ctx.resource.thread_id_in_group_arg = attr;
          return std::monostate{};
        });
      });
      break;
    }
    case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID_IN_GROUP_FLATTENED: {
      auto assigned_index =
        func_signature.DefineInput(InputThreadIndexInThreadgroup{});
      signature_handlers.push_back([=](SignatureContext &ctx) {
        ctx.prologue << make_effect([=](struct context ctx) {
          auto attr = ctx.function->getArg(assigned_index);
          ctx.resource.thread_id_in_group_flat_arg = attr;
          return std::monostate{};
        });
      });
      break;
    }
    default:
      assert(0 && "unhandled compute shader input");
      break;
    }
    break;
  }
  default:
    assert(0);
    break;
  }
};

void
handle_signature_gs(
    CSignatureParser &inputParser, const CSignatureParser &outputParser, D3D10ShaderBinary::CInstruction &Inst,
    SM50ShaderInternal *sm50_shader, uint32_t phase
) {
  uint32_t &max_output_register = sm50_shader->max_output_register;
  auto &func_signature = sm50_shader->func_signature;
  auto &gs_output_handlers = sm50_shader->gs_output_handlers;
  uint32_t &num_mesh_vertex_data = sm50_shader->num_mesh_vertex_data;

  auto findOutputElement = [&](auto matcher) -> Signature {
    const D3D11_SIGNATURE_PARAMETER *parameters;
    outputParser.GetParameters(&parameters);
    for (unsigned i = 0; i < outputParser.GetNumParameters(); i++) {
      auto sig = Signature(parameters[i]);
      if (matcher(sig)) {
        return sig;
      }
    }
    assert(0 && "try to access an undefined output");
  };

  switch (Inst.m_OpCode) {
  case D3D10_SB_OPCODE_DCL_INPUT_SGV: {
    // auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    // auto MinPrecision = Inst.m_Operands[0].m_MinPrecision; // not used
    auto sgv = Inst.m_InputDeclSGV.Name;
    switch (sgv) {
    default:
      assert(0 && "Unexpected/unhandled geometry shader sgv");
      break;
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT_SIV: {
    D3D10_SB_NAME siv = Inst.m_InputDeclSIV.Name;

    switch (siv) {
    case D3D10_SB_NAME_POSITION:
      break;
    default:
      assert(0 && "Unexpected/unhandled geometry shader siv");
      break;
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_INPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;

    switch (RegType) {
    case microsoft::D3D10_SB_OPERAND_TYPE_INPUT: {
      assert(Inst.m_Operands[0].m_IndexDimension == microsoft::D3D10_SB_OPERAND_INDEX_2D);
      break;
    }
    case microsoft::D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID:
      break;
    default:
      assert(0 && "unhandled geometry shader input");
      break;
    }
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SGV: {
    assert(0 && "unhandled geometry shader output sgv");
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT_SIV: {
    unsigned reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
    auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
    auto siv = Inst.m_OutputDeclSIV.Name;
    switch (siv) {
      case D3D10_SB_NAME_CLIP_DISTANCE:
      assert(0 && "unhandled geometry shader clip distance");
      break;
    case D3D10_SB_NAME_CULL_DISTANCE:
      assert(0 && "Metal doesn't support shader output: cull distance");
      break;
    case D3D10_SB_NAME_POSITION: {
      func_signature.DefineMeshVertexOutput(OutputPosition{.type = msl_float4});
      gs_output_handlers.push_back([=](GSOutputContext& output) -> IREffect {
        return pop_mesh_output_position(reg, mask, output.vertex_id);
      });
      break;
    }
    case D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX: {
      func_signature.DefineMeshPrimitiveOutput(OutputRenderTargetArrayIndex{});
      gs_output_handlers.push_back([=](GSOutputContext& output) -> IREffect {
        return pop_mesh_output_render_taget_array_index(reg, mask, output.primitive_id);
      });
      break;
    }
    case D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX: {
      func_signature.DefineMeshPrimitiveOutput(OutputViewportArrayIndex{});
      gs_output_handlers.push_back([=](GSOutputContext& output) -> IREffect {
        return pop_mesh_output_viewport_array_index(reg, mask, output.primitive_id);
      });
      break;
    }
    default:
      assert(0 && "Unexpected/unhandled geometry shader output siv");
      break;
    }
    max_output_register = std::max(reg + 1, max_output_register);
    break;
  }
  case D3D10_SB_OPCODE_DCL_OUTPUT: {
    D3D10_SB_OPERAND_TYPE RegType = Inst.m_Operands[0].m_Type;
    switch (RegType) {
    case D3D10_SB_OPERAND_TYPE_OUTPUT: {
      // normal output register
      auto reg = Inst.m_Operands[0].m_Index[0].m_RegIndex;
      auto mask = Inst.m_Operands[0].m_WriteMask >> 4;
      auto sig = findOutputElement([=](Signature sig) {
        return (sig.reg() == reg) && ((sig.mask() & mask) != 0);
      });
      auto const mesh_vertex_data_index = num_mesh_vertex_data++;
      auto const type = to_msl_type(sig.componentType());
      func_signature.DefineMeshVertexOutput(OutputMeshData{
        .user = sig.fullSemanticString(),
        .type = type,
        .index = mesh_vertex_data_index
      });
      gs_output_handlers.push_back([=](GSOutputContext& output) -> IREffect {
        return pop_mesh_output_vertex_data(reg, mask, mesh_vertex_data_index, output.vertex_id, type);
      });
      max_output_register = std::max(reg + 1, max_output_register);
      break;
    }
    default:
      assert(0 && "unhandled geometry shader output");
      break;
    }
    break;
  }
  default:
    assert(0);
    break;
  }
};

void handle_signature(
  microsoft::CSignatureParser &inputParser,
  microsoft::CSignatureParser5 &outputParser,
  microsoft::D3D10ShaderBinary::CInstruction &Inst, SM50ShaderInternal *shader,
  uint32_t phase
) {
  switch (shader->shader_type) {
  case D3D10_SB_PIXEL_SHADER:
    return handle_signature_ps(inputParser, *outputParser.Signature(0), Inst, shader, phase);
  case D3D10_SB_VERTEX_SHADER:
    return handle_signature_vs(inputParser, *outputParser.Signature(0), Inst, shader, phase);
  case D3D11_SB_HULL_SHADER:
    return handle_signature_hs(inputParser, *outputParser.Signature(0), Inst, shader, phase);
  case D3D11_SB_DOMAIN_SHADER:
    return handle_signature_ds(inputParser, *outputParser.Signature(0), Inst, shader, phase);
  case D3D11_SB_COMPUTE_SHADER:
    return handle_signature_cs(inputParser, *outputParser.Signature(0), Inst, shader, phase);
  case D3D10_SB_GEOMETRY_SHADER:
    return handle_signature_gs(inputParser, *outputParser.Signature(0), Inst, shader, phase);
  default:
    assert(0);
    break;
  }
}

}; // namespace dxmt::dxbc