#include "air_operations.hpp"

namespace dxmt::air {

std::string
type_overload_suffix(llvm::Type *type, Sign sign = Sign::inapplicable) {
  if (type->isFloatTy()) {
    if (sign == Sign::inapplicable)
      return ".f32";
    return ".f.f32";
  }
  if (type->isHalfTy()) {
    if (sign == Sign::inapplicable)
      return ".f16";
    return ".f.f16";
  }
  if (type->isIntegerTy()) {
    assert(llvm::cast<llvm::IntegerType>(type)->getBitWidth() == 32);
    if (sign == Sign::inapplicable)
      return ".i32";
    return sign == Sign::with_sign ? ".s.i32" : ".u.i32";
  }
  if (type->getTypeID() == llvm::Type::FixedVectorTyID) {
    auto fixedVecTy = llvm::cast<llvm::FixedVectorType>(type);
    auto elementTy = fixedVecTy->getElementType();
    if (elementTy->isFloatTy()) {
      return (sign == Sign::inapplicable ? ".v" : ".f.v") +
             std::to_string(fixedVecTy->getNumElements()) + "f32";
    } else if (elementTy->isHalfTy()) {
      return (sign == Sign::inapplicable ? ".v" : ".f.v") +
             std::to_string(fixedVecTy->getNumElements()) + "f16";
    } else if (elementTy->isIntegerTy()) {
      assert(llvm::cast<llvm::IntegerType>(elementTy)->getBitWidth() == 32);
      if (sign == Sign::inapplicable)
        return ".v" + std::to_string(fixedVecTy->getNumElements()) + "i32";
      return sign == Sign::with_sign
               ? ".s.v" + std::to_string(fixedVecTy->getNumElements()) + "i32"
               : ".u.v" + std::to_string(fixedVecTy->getNumElements()) + "i32";
    }
  }

  assert(0 && "unexpected or unhandled type");
};

std::string fastmath_variant(AIRBuilderContext &ctx, std::string name, bool force_precise = false) {
  if (ctx.builder.getFastMathFlags().isFast() && !force_precise) {
    return "fast_" + name;
  } else {
    return name;
  }
};

ReaderIO<AIRBuilderContext, AIRBuilderContext> get_context() {
  return ReaderIO<AIRBuilderContext, AIRBuilderContext>(
    [](struct AIRBuilderContext ctx) { return ctx; }
  );
};

AIRBuilderResult get_int(uint32_t value) {
  return make_op([=](auto ctx) { return ctx.builder.getInt32(value); });
};

AIRBuilderResult get_float(float value) {
  return make_op([=](auto ctx) {
    return llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value});
  });
};

AIRBuilderResult call_integer_unary_op(std::string op, pvalue a) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    assert(a->getType()->getScalarType()->isIntegerTy());
    auto att = AttributeList::get(
      context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)}}
    );
    auto operand_type = a->getType();
    auto fn = (module.getOrInsertFunction(
      "air." + op + type_overload_suffix(operand_type, Sign::inapplicable),
      llvm::FunctionType::get(operand_type, {operand_type}, false), att
    ));
    return ctx.builder.CreateCall(fn, {a});
  });
};

AIRBuilderResult call_float_unary_op(std::string op, pvalue a) {
  return make_op([=](AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    assert(a->getType()->getScalarType()->isFloatTy());
    auto att = AttributeList::get(
      context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)}}
    );
    auto operand_type = a->getType();
    auto fn = (module.getOrInsertFunction(
      "air." + fastmath_variant(ctx, op) + type_overload_suffix(operand_type),
      llvm::FunctionType::get(operand_type, {operand_type}, false), att
    ));
    return ctx.builder.CreateCall(fn, {a});
  });
};

AIRBuilderResult
call_integer_binop(std::string op, pvalue a, pvalue b, bool is_signed) {
  return make_op([=](AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    assert(a->getType()->getScalarType()->isIntegerTy());
    assert(b->getType()->getScalarType()->isIntegerTy());
    assert(a->getType() == b->getType());
    auto att = AttributeList::get(
      context, {std::make_pair<unsigned int, Attribute>(
                  ~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)
                ),
                std::make_pair<unsigned int, Attribute>(
                  ~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)
                ),
                std::make_pair<unsigned int, Attribute>(
                  ~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)
                )}
    );
    auto operand_type = a->getType();
    auto fn = (module.getOrInsertFunction(
      "air." + op +
        type_overload_suffix(
          operand_type, is_signed ? Sign::with_sign : Sign::no_sign
        ),
      llvm::FunctionType::get(
        operand_type, {operand_type, operand_type}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {a, b});
  });
};

AIRBuilderResult call_float_binop(std::string op, pvalue a, pvalue b, bool force_precise) {
  return make_op([=](AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    assert(a->getType()->getScalarType()->isFloatTy());
    assert(b->getType()->getScalarType()->isFloatTy());
    assert(a->getType() == b->getType());
    auto att = AttributeList::get(
      context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)}}
    );
    auto operand_type = a->getType();
    auto fn = (module.getOrInsertFunction(
      "air." + fastmath_variant(ctx, op, force_precise) + type_overload_suffix(operand_type),
      llvm::FunctionType::get(
        operand_type, {operand_type, operand_type}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {a, b});
  });
};

AIRBuilderResult call_dot_product(uint32_t dimension, pvalue a, pvalue b) {
  return make_op([=](AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto &types = ctx.types;
    auto att = AttributeList::get(
      context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)}}
    );
    auto operand_type = dimension == 4   ? types._float4
                        : dimension == 3 ? types._float3
                                         : types._float2;
    auto fn = (module.getOrInsertFunction(
      "air.dot" + type_overload_suffix(operand_type),
      llvm::FunctionType::get(
        types._float, {operand_type, operand_type}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {a, b});
  });
};

AIRBuilderResult call_float_mad(pvalue a, pvalue b, pvalue c) {
  return make_op([=](AIRBuilderContext ctx) -> pvalue {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    assert(a->getType() == b->getType());
    assert(a->getType() == c->getType());
    if (!ctx.builder.getFastMathFlags().isFast()) {
      return ctx.builder.CreateFAdd(ctx.builder.CreateFMul(a, b), c);
    }
    auto att = AttributeList::get(
      context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)}}
    );
    auto operand_type = a->getType();
    auto fn = (module.getOrInsertFunction(
      "air.fma" + type_overload_suffix(operand_type),
      llvm::FunctionType::get(
        operand_type, {operand_type, operand_type, operand_type}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {a, b, c});
  });
};

AIRBuilderResult call_count_zero(bool trail, pvalue a) {
  return make_op([=](AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    assert(a->getType()->getScalarType()->isIntegerTy());
    auto att = AttributeList::get(
      context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)}}
    );
    auto operand_type = a->getType();
    auto fn = (module.getOrInsertFunction(
      "air." + (trail ? std::string("ctz") : std::string("clz")) +
        type_overload_suffix(operand_type, Sign::inapplicable),
      llvm::FunctionType::get(
        operand_type, {operand_type, ctx.types._bool}, false
      ),
      att
    ));
    auto ret = ctx.builder.CreateCall(fn, {a, ctx.builder.getInt1(false)});
    if (isa<VectorType>(operand_type)) {
      auto vec_type = cast<VectorType>(operand_type);
      assert(isa<IntegerType>(vec_type->getElementType()));
      auto comp_type = cast<IntegerType>(vec_type->getElementType());
      auto comp_num = vec_type->getElementCount();
      return ctx.builder.CreateSelect(
        ctx.builder.CreateICmpEQ(
          ret, ctx.builder.CreateVectorSplat(
                 comp_num, ctx.builder.getIntN(
                             comp_type->getBitWidth(), comp_type->getBitWidth()
                           )
               )
        ),
        llvm::ConstantInt::getAllOnesValue(vec_type), ret
      );
    } else {
      assert(isa<IntegerType>(operand_type));
      auto int_type = cast<IntegerType>(operand_type);
      return ctx.builder.CreateSelect(
        ctx.builder.CreateICmpEQ(
          ret,
          ctx.builder.getIntN(int_type->getBitWidth(), int_type->getBitWidth())
        ),
        llvm::ConstantInt::getAllOnesValue(int_type), ret
      );
    }
  });
};

struct TextureOperationInfo {
  // it seems to satisty: offset vector length > 1
  bool sample_unknown_bit = false;
  bool is_depth = false;
  bool is_cube = false;
  bool is_array = false;
  bool is_ms = false;
  bool is_1d_or_1d_array = false;
  bool is_texture_buffer = false;
  Sign sign = Sign::inapplicable;
  std::string air_symbol_suffix = {};
  llvm::Type *coord_type = nullptr;
  llvm::Type *address_type = nullptr;
  llvm::Type *offset_type = nullptr;
  llvm::Type *sample_type = nullptr;
  llvm::Type *gather_type = nullptr;
  llvm::Type *read_type = nullptr;
  llvm::Type *write_type = nullptr;
  llvm::Type *gradient_type = nullptr;
};

auto get_operation_info(MSLTexture texture
) -> ReaderIO<AIRBuilderContext, TextureOperationInfo> {
  using namespace dxmt::air;
  auto ctx = co_yield get_context();
  auto &types = ctx.types;
  auto sign = std::visit(
    patterns{
      [](MSLUint) { return Sign::no_sign; },
      [](MSLInt) { return Sign::with_sign; },
      [](auto) { return Sign::inapplicable; }
    },
    texture.component_type
  );
  TextureOperationInfo ret;
  ret.sign = sign;
  ret.sample_type =
    types.to_vec4_type(get_llvm_type(texture.component_type, ctx.llvm));
  ret.gather_type = ret.sample_type; // handle exceptions separately
  ret.read_type = ret.sample_type;   // handle exceptions separately
  ret.write_type = ret.sample_type;  // handle exceptions separately
  switch (texture.resource_kind) {
  case TextureKind::texture_1d: {
    ret.coord_type = types._float;
    ret.air_symbol_suffix = "texture_1d";
    ret.offset_type = types._int;
    ret.is_1d_or_1d_array = true;
    ret.gather_type = nullptr;
    ret.address_type = types._int;
    break;
  };
  case TextureKind::texture_2d: {
    ret.coord_type = types._float2;
    ret.sample_unknown_bit = true;
    ret.air_symbol_suffix = "texture_2d";
    ret.offset_type = types._int2;
    ret.gradient_type = types._float2;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::texture_1d_array: {
    ret.coord_type = types._float;
    ret.air_symbol_suffix = "texture_1d_array";
    ret.is_array = true;
    ret.offset_type = types._int;
    ret.is_1d_or_1d_array = true;
    ret.gather_type = nullptr;
    ret.address_type = types._int;
    break;
  }
  case TextureKind::texture_2d_array: {
    ret.coord_type = types._float2;
    ret.sample_unknown_bit = true;
    ret.air_symbol_suffix = "texture_2d_array";
    ret.is_array = true;
    ret.offset_type = types._int2;
    ret.gradient_type = types._float2;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::texture_3d: {
    ret.coord_type = types._float3;
    ret.sample_unknown_bit = true;
    ret.air_symbol_suffix = "texture_3d";
    ret.offset_type = types._int3;
    ret.gradient_type = types._float3;
    ret.gather_type = nullptr;
    ret.address_type = types._int3;
    break;
  }
  case TextureKind::texture_cube: {
    ret.coord_type = types._float3;
    ret.air_symbol_suffix = "texture_cube";
    ret.gradient_type = types._float3;
    ret.is_cube = true;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::texture_cube_array: {
    ret.coord_type = types._float3;
    ret.air_symbol_suffix = "texture_cube_array";
    ret.is_array = true;
    ret.gradient_type = types._float3;
    ret.is_cube = true;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::texture_buffer: {
    ret.sample_type = nullptr;
    ret.air_symbol_suffix = "texture_buffer_1d";
    ret.gather_type = nullptr;
    ret.address_type = types._int;
    ret.is_texture_buffer = true;
    break;
  }
  case TextureKind::depth_2d: {
    ret.coord_type = types._float2;
    ret.sample_unknown_bit = true;
    ret.air_symbol_suffix = "depth_2d";
    ret.is_depth = true;
    ret.offset_type = types._int2;
    ret.sample_type = types._float;
    ret.read_type = types._float;
    ret.write_type = nullptr;
    ret.gradient_type = types._float2;
    ret.gather_type = types._float4; // gather returns float4 instead of float
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::depth_2d_array: {
    ret.coord_type = types._float2;
    ret.air_symbol_suffix = "depth_2d_array";
    ret.is_array = true;
    ret.is_depth = true;
    ret.sample_unknown_bit = true;
    ret.offset_type = types._int2;
    ret.sample_type = types._float;
    ret.read_type = types._float;
    ret.write_type = nullptr;
    ret.gradient_type = types._float2;
    ret.gather_type = types._float4;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::depth_cube: {
    ret.coord_type = types._float3;
    ret.air_symbol_suffix = "depth_cube";
    ret.is_depth = true;
    ret.sample_unknown_bit = true;
    ret.sample_type = types._float;
    ret.read_type = types._float;
    ret.write_type = nullptr;
    ret.gradient_type = types._float3;
    ret.gather_type = types._float4;
    ret.is_cube = true;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::depth_cube_array: {
    ret.coord_type = types._float3;
    ret.air_symbol_suffix = "depth_cube_array";
    ret.is_array = true;
    ret.is_depth = true;
    ret.sample_type = types._float;
    ret.read_type = types._float;
    ret.write_type = nullptr;
    ret.gradient_type = types._float3;
    ret.gather_type = types._float4;
    ret.is_cube = true;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::texture_2d_ms: {
    ret.sample_type = nullptr;
    ret.air_symbol_suffix = "texture_2d_ms";
    ret.is_ms = true;
    ret.gather_type = nullptr;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::texture_2d_ms_array: {
    ret.sample_type = nullptr;
    ret.air_symbol_suffix = "texture_2d_ms_array";
    ret.is_array = true;
    ret.is_ms = true;
    ret.gather_type = nullptr;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::depth_2d_ms: {
    ret.air_symbol_suffix = "depth_2d_ms";
    ret.is_depth = true;
    ret.sample_type = nullptr;
    ret.read_type = types._float;
    ret.write_type = nullptr;
    ret.is_ms = true;
    ret.gather_type = nullptr;
    ret.address_type = types._int2;
    break;
  }
  case TextureKind::depth_2d_ms_array: {
    ret.air_symbol_suffix = "depth_2d_ms_array";
    ret.is_array = true;
    ret.is_depth = true;
    ret.sample_type = nullptr;
    ret.read_type = types._float;
    ret.write_type = nullptr;
    ret.is_ms = true;
    ret.gather_type = nullptr;
    ret.address_type = types._int2;
    break;
  }
  }
  co_return ret;
}

AIRBuilderResult call_sample(
  MSLTexture texture_type, pvalue handle, pvalue sampler, pvalue coord,
  pvalue array_index, pvalue offset, pvalue bias, pvalue min_lod_clamp,
  pvalue lod_level
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {2U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {2U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::Convergent)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(!op_info.is_ms);
  auto fn_name = "air.sample_" + op_info.air_symbol_suffix +
                 type_overload_suffix(op_info.sample_type, op_info.sign);
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  args_type.push_back(types._sampler->getPointerTo(2));
  args_value.push_back(sampler);

  if (op_info.is_depth) {
    args_type.push_back(types._int);
    args_value.push_back(ctx.builder.getInt32(1));
  }

  args_type.push_back(op_info.coord_type);
  args_value.push_back(coord);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  if (op_info.offset_type != nullptr) {
    args_type.push_back(types._bool);
    args_value.push_back(ctx.builder.getInt1(op_info.sample_unknown_bit));
    args_type.push_back(op_info.offset_type);
    args_value.push_back(
      offset != nullptr ? offset
                        : ConstantAggregateZero::get(op_info.offset_type)
    );
  }

  /* parameters */
  args_type.push_back(types._bool);
  args_type.push_back(types._float);
  args_type.push_back(types._float);

  if (lod_level != nullptr) {
    args_value.push_back(ctx.builder.getInt1(true));
    args_value.push_back(lod_level);
    args_value.push_back(ConstantFP::get(context, APFloat{0.0f}));
  } else {
    args_value.push_back(ctx.builder.getInt1(false));
    args_value.push_back(
      bias != nullptr ? bias : ConstantFP::get(context, APFloat{0.0f})
    );
    args_value.push_back(
      min_lod_clamp != nullptr ? min_lod_clamp
                               : ConstantFP::get(context, APFloat{0.0f})
    );
  }

  /* access: always 0 = sample */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0));

  auto return_type =
    StructType::get(context, {op_info.sample_type, ctx.types._byte});
  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(return_type, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_sample_grad(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue dpdx, pvalue dpdy, pvalue minlod, pvalue offset
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {2U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {2U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(!op_info.is_ms);
  assert(!op_info.is_1d_or_1d_array);
  auto fn_name = "air.sample_" + op_info.air_symbol_suffix + "_grad" +
                 type_overload_suffix(op_info.sample_type, op_info.sign);
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  args_type.push_back(types._sampler->getPointerTo(2));
  args_value.push_back(sampler_handle);

  if (op_info.is_depth) {
    args_type.push_back(types._int);
    args_value.push_back(ctx.builder.getInt32(1));
  }

  args_type.push_back(op_info.coord_type);
  args_value.push_back(coord);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }
  assert(op_info.gradient_type);
  args_type.push_back(op_info.gradient_type);
  args_value.push_back(dpdx);
  args_type.push_back(op_info.gradient_type);
  args_value.push_back(dpdy);

  args_type.push_back(types._float);
  args_value.push_back(minlod ? minlod : ConstantFP::get(types._float, 0));

  if (op_info.offset_type != nullptr) {
    args_type.push_back(types._bool);
    args_value.push_back(ctx.builder.getInt1(op_info.sample_unknown_bit)
    ); // = 1
    args_type.push_back(op_info.offset_type);
    args_value.push_back(
      offset != nullptr ? offset
                        : ConstantAggregateZero::get(op_info.offset_type)
    );
  }

  /* access: always 0 = sample */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0));

  auto return_type =
    StructType::get(context, {op_info.sample_type, ctx.types._byte});
  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(return_type, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_sample_compare(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue reference, pvalue offset, pvalue bias,
  pvalue min_lod_clamp, pvalue lod_level
) {

  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {2U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {2U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::Convergent)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(op_info.is_depth);
  auto fn_name =
    "air.sample_compare_" + op_info.air_symbol_suffix +
    type_overload_suffix(op_info.sample_type, op_info.sign) /* = .f32*/;
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  args_type.push_back(types._sampler->getPointerTo(2));
  args_value.push_back(sampler_handle);

  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(1));

  args_type.push_back(op_info.coord_type);
  args_value.push_back(coord);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  /* reference */
  args_type.push_back(types._float);
  args_value.push_back(
    reference ? reference : ConstantFP::get(types._float, 0)
  );

  if (op_info.offset_type != nullptr) {
    args_type.push_back(types._bool);
    args_value.push_back(ctx.builder.getInt1(op_info.sample_unknown_bit)
    ); // = 1
    args_type.push_back(op_info.offset_type);
    args_value.push_back(
      offset != nullptr ? offset
                        : ConstantAggregateZero::get(op_info.offset_type)
    );
  }

  /* parameters */
  args_type.push_back(types._bool);
  args_type.push_back(types._float);
  args_type.push_back(types._float);

  if (lod_level != nullptr) {
    args_value.push_back(ctx.builder.getInt1(true));
    args_value.push_back(lod_level);
    args_value.push_back(ConstantFP::get(context, APFloat{0.0f}));
  } else {
    args_value.push_back(ctx.builder.getInt1(false));
    args_value.push_back(
      bias != nullptr ? bias : ConstantFP::get(context, APFloat{0.0f})
    );
    args_value.push_back(
      min_lod_clamp != nullptr ? min_lod_clamp
                               : ConstantFP::get(context, APFloat{0.0f})
    );
  }

  /* access: always 0 = sample */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0));

  auto return_type =
    StructType::get(context, {op_info.sample_type, ctx.types._byte});
  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(return_type, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_gather(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue offset, pvalue component
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {2U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {2U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(op_info.gather_type);
  auto fn_name = "air.gather_" + op_info.air_symbol_suffix +
                 type_overload_suffix(op_info.gather_type, op_info.sign);
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  args_type.push_back(types._sampler->getPointerTo(2));
  args_value.push_back(sampler_handle);

  if (op_info.is_depth) {
    args_type.push_back(types._int);
    args_value.push_back(ctx.builder.getInt32(1));
  }

  args_type.push_back(op_info.coord_type);
  args_value.push_back(coord);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  if (op_info.offset_type != nullptr) {
    args_type.push_back(types._bool);
    args_value.push_back(ctx.builder.getInt1(op_info.sample_unknown_bit)
    ); // = 1
    args_type.push_back(op_info.offset_type);
    args_value.push_back(
      offset != nullptr ? offset
                        : ConstantAggregateZero::get(op_info.offset_type)
    );
  }

  /* component */
  if (!op_info.is_depth) {
    args_type.push_back(types._int);
    args_value.push_back(component ? component : ctx.builder.getInt32(0));
  }

  /* access: always 0 = sample */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0));

  auto return_type =
    StructType::get(context, {op_info.gather_type, ctx.types._byte});
  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(return_type, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_gather_compare(
  MSLTexture texture_type, pvalue handle, pvalue sampler_handle, pvalue coord,
  pvalue array_index, pvalue reference, pvalue offset
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {2U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {2U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(op_info.is_depth);
  auto fn_name = "air.gather_compare_" + op_info.air_symbol_suffix + ".f32";
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  args_type.push_back(types._sampler->getPointerTo(2));
  args_value.push_back(sampler_handle);

  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(1));

  args_type.push_back(op_info.coord_type);
  args_value.push_back(coord);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  // reference

  args_type.push_back(types._float);
  args_value.push_back(
    reference ? reference : ConstantFP::get(types._float, 0)
  );

  if (op_info.offset_type != nullptr) {
    args_type.push_back(types._bool);
    args_value.push_back(ctx.builder.getInt1(op_info.sample_unknown_bit)
    ); // = 1
    args_type.push_back(op_info.offset_type);
    args_value.push_back(
      offset != nullptr ? offset
                        : ConstantAggregateZero::get(op_info.offset_type)
    );
  }

  /* access: always 0 = sample */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0));

  auto return_type =
    StructType::get(context, {op_info.gather_type, ctx.types._byte});
  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(return_type, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_read(
  MSLTexture texture_type, pvalue handle, pvalue address, pvalue offset,
  pvalue cube_face, pvalue array_index, pvalue sample_index, pvalue lod
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  auto fn_name = "air.read_" + op_info.air_symbol_suffix +
                 type_overload_suffix(op_info.read_type, op_info.sign);
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  if (op_info.is_depth) {
    args_type.push_back(types._int);
    args_value.push_back(ctx.builder.getInt32(1));
  }

  assert(op_info.address_type);
  args_type.push_back(op_info.address_type);
  if (Constant *c = offset ? dyn_cast<Constant>(offset) : nullptr) {
    if (c->isZeroValue()) {
      args_value.push_back(address);
    } else {
      args_value.push_back(ctx.builder.CreateAdd(address, offset));
    }
  } else {
    args_value.push_back(address);
  }

  if (op_info.is_cube) {
    assert(cube_face);
    args_type.push_back(types._int);
    args_value.push_back(cube_face);
  }

  if (op_info.is_array) {
    assert(array_index);
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  if (op_info.is_ms) {
    assert(sample_index);
    args_type.push_back(types._int);
    args_value.push_back(sample_index);
  }

  if (!op_info.is_ms &&
      texture_type.resource_kind != TextureKind::texture_buffer) {
    args_type.push_back(types._int);
    if (op_info.is_1d_or_1d_array) {
      args_value.push_back(ctx.builder.getInt32(0));
    } else {
      args_value.push_back(lod ? lod : ctx.builder.getInt32(0));
    }
  }

  /* access */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32((uint32_t)texture_type.memory_access
  ));

  auto return_type =
    StructType::get(context, {op_info.read_type, ctx.types._byte});
  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(return_type, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_write(
  MSLTexture texture_type, pvalue handle, pvalue address, pvalue cube_face,
  pvalue array_index, pvalue vec4, pvalue lod
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(!op_info.is_ms);
  assert(!op_info.is_depth);
  auto fn_name = "air.write_" + op_info.air_symbol_suffix +
                 type_overload_suffix(op_info.read_type, op_info.sign);
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  assert(op_info.address_type);
  args_type.push_back(op_info.address_type);
  args_value.push_back(address);

  if (op_info.is_cube) {
    args_type.push_back(types._int);
    args_value.push_back(cube_face);
  }

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  args_type.push_back(op_info.write_type);
  args_value.push_back(vec4);

  if (texture_type.resource_kind != TextureKind::texture_buffer) {
    args_type.push_back(types._int);
    if (op_info.is_1d_or_1d_array) {
      args_value.push_back(ctx.builder.getInt32(0));
    } else {
      args_value.push_back(lod ? lod : ctx.builder.getInt32(0));
    }
  }

  /* access */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32((uint32_t)texture_type.memory_access
  ));

  auto return_type = Type::getVoidTy(context);
  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(return_type, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_calc_lod(
  MSLTexture texture_type, pvalue handle, pvalue sampler, pvalue coord,
  bool is_unclamped
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {2U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {2U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::Convergent)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(!op_info.is_ms);
  assert(!op_info.is_1d_or_1d_array);
  auto fn_name = (is_unclamped ? "air.calculate_unclamped_lod_"
                               : "air.calculate_clamped_lod_") +
                 op_info.air_symbol_suffix;
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  args_type.push_back(types._sampler->getPointerTo(2));
  args_value.push_back(sampler);

  args_type.push_back(op_info.coord_type);
  args_value.push_back(coord);

  /* access: always 0 = sample */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0));

  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(types._float, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_get_texture_info(
  air::MSLTexture texture_type, pvalue handle, TextureInfoType type, pvalue lod
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  if (type == TextureInfoType::array_length) {
    assert(op_info.is_array);
  }
  if (type == TextureInfoType::num_mip_levels) {
    assert(!op_info.is_ms);
  }
  if (type == TextureInfoType::num_samples) {
    assert(op_info.is_ms);
  }
  auto fn_name =
    (type == TextureInfoType::width          ? "air.get_width_"
     : type == TextureInfoType::height       ? "air.get_height_"
     : type == TextureInfoType::depth        ? "air.get_depth_"
     : type == TextureInfoType::array_length ? "air.get_array_size_"
     : type == TextureInfoType::num_samples  ? "air.get_num_samples_"
                                             : "air.get_num_mip_levels_") +
    op_info.air_symbol_suffix;
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  if (type == TextureInfoType::width || type == TextureInfoType::height ||
      type == TextureInfoType::depth) {
    if (!op_info.is_ms && !op_info.is_texture_buffer) {
      args_type.push_back(types._int);
      args_value.push_back(lod);
    }
  }

  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(types._int, args_type, false), att
  );
  co_return ctx.builder.CreateCall(fn, args_value);
}

AIRBuilderResult call_get_num_samples() {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
      {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)},
    }
  );

  auto fn = module.getOrInsertFunction(
    "air.get_num_samples.i32",
    llvm::FunctionType::get(types._int, {types._int}, false), att
  );
  co_return ctx.builder.CreateCall(fn, ctx.builder.getInt32(0));
}

/* are we assume that vec4 can only be integer? */
AIRBuilderResult call_texture_atomic_fetch_explicit(
  air::MSLTexture texture_type, pvalue handle, std::string op, bool is_signed,
  pvalue address, pvalue array_index, pvalue vec4
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(!op_info.is_depth);
  assert(!op_info.is_cube);
  assert(!op_info.is_ms);
  assert(op_info.write_type == types._int4);
  auto fn_name =
    "air.atomic_fetch_" + op + "_explicit_" + op_info.air_symbol_suffix +
    type_overload_suffix(
      vec4->getType(), is_signed ? air::Sign::with_sign : air::Sign::no_sign
    );
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  assert(op_info.address_type);
  args_type.push_back(op_info.address_type);
  args_value.push_back(address);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  args_type.push_back(op_info.write_type);
  args_value.push_back(vec4);

  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0)); // memory order relaxed

  /* access */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32((uint32_t)texture_type.memory_access
  ));

  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(op_info.write_type, args_type, false), att
  );
  co_return ctx.builder.CreateExtractElement(
    ctx.builder.CreateCall(fn, args_value), uint64_t(0)
  );
};

AIRBuilderResult call_texture_atomic_exchange(
  air::MSLTexture texture_type, pvalue handle, pvalue address,
  pvalue array_index, pvalue vec4
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(!op_info.is_depth);
  assert(!op_info.is_cube);
  assert(!op_info.is_ms);
  assert(op_info.write_type == types._int4);
  auto fn_name = "air.atomic_exchange_explicit_" + op_info.air_symbol_suffix +
                 type_overload_suffix(vec4->getType(), air::Sign::no_sign);
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  assert(op_info.address_type);
  args_type.push_back(op_info.address_type);
  args_value.push_back(address);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  args_type.push_back(op_info.write_type);
  args_value.push_back(vec4);

  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32(0)); // memory order relaxed

  /* access */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32((uint32_t)texture_type.memory_access
  ));

  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(op_info.write_type, args_type, false), att
  );
  co_return ctx.builder.CreateExtractElement(
    ctx.builder.CreateCall(fn, args_value), uint64_t(0)
  );
};

AIRBuilderResult call_texture_atomic_compare_exchange(
  air::MSLTexture texture_type, pvalue handle, pvalue address,
  pvalue array_index, pvalue compared, pvalue operand
) {
  auto ctx = co_yield get_context();
  using namespace llvm;
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context,
    {
      {1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
      {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
      {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
    }
  );
  auto op_info = co_yield get_operation_info(texture_type);
  assert(!op_info.is_depth);
  assert(!op_info.is_cube);
  assert(!op_info.is_ms);
  assert(op_info.write_type == types._int4);
  auto fn_name = "air.atomic_compare_exchange_weak_explicit_" + op_info.air_symbol_suffix +
                 type_overload_suffix(operand->getType(), air::Sign::no_sign);
  std::vector<llvm::Type *> args_type;
  std::vector<pvalue> args_value;

  args_type.push_back(texture_type.get_llvm_type(context));
  args_value.push_back(handle);

  assert(op_info.address_type);
  args_type.push_back(op_info.address_type);
  args_value.push_back(address);

  if (op_info.is_array) {
    args_type.push_back(types._int);
    args_value.push_back(array_index);
  }

  auto alloca = ctx.builder.CreateAlloca(op_info.write_type, uint32_t(0));
  auto ptr = ctx.builder.CreateConstGEP1_64(op_info.write_type, alloca, 0);
  ctx.builder.CreateStore(compared, ptr);

  args_type.push_back(op_info.write_type->getPointerTo());
  args_value.push_back(alloca);

  args_type.push_back(op_info.write_type);
  args_value.push_back(operand);

  args_type.push_back(types._int); // mem order success
  args_value.push_back(ctx.builder.getInt32(0)); // memory order relaxed

  args_type.push_back(types._int); // mem order failure
  args_value.push_back(ctx.builder.getInt32(0)); // memory order relaxed

  /* access */
  args_type.push_back(types._int);
  args_value.push_back(ctx.builder.getInt32((uint32_t)texture_type.memory_access
  ));

  auto fn = module.getOrInsertFunction(
    fn_name, llvm::FunctionType::get(ctx.types._bool, args_type, false), att
  );
  // comparison result is ignored
  ctx.builder.CreateCall(fn, args_value);
  auto expected = ctx.builder.CreateLoad(op_info.write_type, ptr);
  // this should be always the original value at ptr
  co_return ctx.builder.CreateExtractElement(expected, uint64_t(0));
}

AIRBuilderResult
call_convert(pvalue src, llvm::Type *dst_scaler_type, Sign sign) {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto att = AttributeList::get(
    context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)}}
  );
  auto src_type = src->getType();
  llvm::Type *dst_type = nullptr;
  if (isa<llvm::FixedVectorType>(src_type)) {
    dst_type = llvm::FixedVectorType::get(
      dst_scaler_type, cast<llvm::FixedVectorType>(src_type)->getNumElements()
    );
  } else {
    dst_type = dst_scaler_type;
  }
  auto dst_suffix = type_overload_suffix(dst_type, sign);
  auto src_suffix = type_overload_suffix(src_type, sign);

  auto fn = (module.getOrInsertFunction(
    "air.convert" + dst_suffix + src_suffix,
    llvm::FunctionType::get(dst_type, {src_type}, false), att
  ));
  co_return ctx.builder.CreateCall(fn, {src});
};

AIRBuilderResult call_atomic_fetch_explicit(
  pvalue pointer, pvalue operand, std::string op, bool is_signed, bool device
) {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &types = ctx.types;
  auto &module = ctx.module;
  auto att = AttributeList::get(
    context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
              {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)}}
  );

  assert(operand->getType() == types._int);

  auto fn = (module.getOrInsertFunction(
    "air.atomic." + std::string(device ? "global." : "local.") + op +
      std::string(is_signed ? ".s.i32" : ".u.i32"),
    llvm::FunctionType::get(
      types._int,
      {
        types._int->getPointerTo(device ? 1 : 3),
        types._int, // operand
        types._int, // order 0 relaxed
        types._int, // scope 1 threadgroup 2 device (type: memflag?)
        types._bool // volatile? should be true all the time?
      },
      false
    ),
    att
  ));
  co_return ctx.builder.CreateCall(
    fn, {pointer, operand, co_yield get_int(0),
         co_yield get_int(device ? 2 : 1), ConstantInt::getBool(context, true)}
  );
};

AIRBuilderResult
call_atomic_exchange_explicit(pvalue pointer, pvalue operand, bool device) {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &types = ctx.types;
  auto &module = ctx.module;
  auto att = AttributeList::get(
    context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
              {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)}}
  );

  assert(operand->getType() == types._int);

  auto fn = (module.getOrInsertFunction(
    "air.atomic." + std::string(device ? "global." : "local.") + "xchg.i32",
    llvm::FunctionType::get(
      types._int,
      {
        types._int->getPointerTo(device ? 1 : 3),
        types._int, // desired
        types._int, // order 0 relaxed
        types._int, // scope 1 threadgroup 2 device (type: memflag?)
        types._bool // volatile? should be true all the time?
      },
      false
    ),
    att
  ));
  co_return ctx.builder.CreateCall(
    fn, {pointer, operand, co_yield get_int(0),
         co_yield get_int(device ? 2 : 1), ConstantInt::getBool(context, true)}
  );
}

AIRBuilderResult call_atomic_cmp_exchange(
  pvalue pointer, pvalue compared, pvalue operand, pvalue tmp_mem, bool device
) {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &types = ctx.types;
  auto &module = ctx.module;
  auto att = AttributeList::get(
    context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
              {2U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
              {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
              {~0U, Attribute::get(context, Attribute::AttrKind::MustProgress)}}
  );

  assert(operand->getType() == types._int);

  auto fn = module.getOrInsertFunction(
    "air.atomic." + std::string(device ? "global." : "local.") +
      "cmpxchg.weak.i32",
    llvm::FunctionType::get(
      types._int,
      {
        types._int->getPointerTo(device ? 1 : 3),
        types._int->getPointerTo(), // expected
        types._int,                 // desired
        types._int,                 // order 0 relaxed
        types._int,                 // order 0 relaxed
        types._int, // scope 1 threadgroup 2 device (type: memflag?)
        types._bool // volatile? should be true all the time?
      },
      false
    ),
    att
  );
  // auto alloca = ctx.resource.cmp_exch_temp;
  auto alloca = tmp_mem;
  auto ptr = ctx.builder.CreateConstGEP1_64(types._int, alloca, 0);
  ctx.builder.CreateStore(compared, ptr);
  ctx.builder.CreateCall(
    fn, {pointer, alloca, operand, co_yield get_int(0), co_yield get_int(0),
         co_yield get_int(device ? 2 : 1), ConstantInt::getBool(context, true)}
  );
  auto expected = ctx.builder.CreateLoad(types._int, ptr);
  // this should be always the original value at ptr
  co_return expected;
}

AIRBuilderResult call_derivative(pvalue fvec4, bool dfdy) {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto &types = ctx.types;
  auto att = AttributeList::get(
    context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)}}
  );
  auto fn = (module.getOrInsertFunction(
    "air." + std::string(dfdy ? "dfdy" : "dfdx") + ".v4f32",
    llvm::FunctionType::get(types._float4, {types._float4}, false), att
  ));
  co_return ctx.builder.CreateCall(fn, {fvec4});
}

AIRBuilderResult call_unpack_impl(
  std::string op, pvalue src, llvm::Type *src_type, llvm::Type *dst_type
) {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto att = AttributeList::get(
    context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
              {~0U, Attribute::get(context, Attribute::AttrKind::MustProgress)},
              {~0U, Attribute::get(context, Attribute::AttrKind::NoFree)},
              {~0U, Attribute::get(context, Attribute::AttrKind::NoSync)},
              {~0U, Attribute::get(context, Attribute::AttrKind::ReadNone)}}
  );
  auto fn = (module.getOrInsertFunction(
    "air.unpack." + op, llvm::FunctionType::get(dst_type, {src_type}, false),
    att
  ));
  co_return ctx.builder.CreateCall(fn, {src});
};

AIRBuilderResult load_from_device_buffer(
  llvm::Type *value_type, pvalue base_addr, pvalue aligned_offest,
  uint32_t element_offset, uint32_t align
) {
  auto ctx = co_yield get_context();
  co_return ctx.builder.CreateAlignedLoad(
    value_type,
    ctx.builder.CreateConstGEP1_32(
      value_type,
      ctx.builder.CreateBitCast(
        ctx.builder.CreateGEP(ctx.types._byte, base_addr, {aligned_offest}),
        value_type->getPointerTo((uint32_t)AddressSpace::device)
      ),
      element_offset
    ),
    llvm::MaybeAlign(align)
  );
};

AIRBuilderResult unpack_fvec4_from_addr(
  MTLAttributeFormat format, pvalue base_addr, pvalue aligned_offest
) {
  auto ctx = co_yield get_context();
  auto &types = ctx.types;
  auto &builder = ctx.builder;

  std::string op;
  llvm::Type *src_type = nullptr;
  uint32_t align = 0;
  llvm::Type *dst_type = nullptr;
  switch (format) {
  case MTLAttributeFormat::UCharNormalized:
    op = "unorm1x8.f32";
    src_type = types._byte;
    align = 1;
    dst_type = types._float;
    break;
  case MTLAttributeFormat::UChar2Normalized:
    op = "unorm2x8.v2f32";
    src_type = types._short;
    align = 1;
    dst_type = types._float2;
    break;
  case MTLAttributeFormat::UChar4Normalized:
  case MTLAttributeFormat::UChar4Normalized_BGRA:
    op = "unorm4x8.v4f32";
    src_type = types._int;
    align = 1;
    dst_type = types._float4;
    break;
  case MTLAttributeFormat::CharNormalized:
    op = "snorm1x8.f32";
    src_type = types._byte;
    align = 1;
    dst_type = types._float;
    break;
  case MTLAttributeFormat::Char2Normalized:
    op = "snorm2x8.v2f32";
    src_type = types._short;
    align = 1;
    dst_type = types._float2;
    break;
  case MTLAttributeFormat::Char4Normalized:
    op = "snorm4x8.v4f32";
    src_type = types._int;
    align = 1;
    dst_type = types._float4;
    break;
  case MTLAttributeFormat::UShortNormalized:
    op = "unorm1x16.f32";
    src_type = types._short;
    align = 2;
    dst_type = types._float;
    break;
  case MTLAttributeFormat::UShort2Normalized:
    op = "unorm2x16.v2f32";
    src_type = types._int;
    align = 2;
    dst_type = types._float2;
    break;
  case MTLAttributeFormat::UShort4Normalized:
    op = "unorm4x16.v4f32";
    src_type = types._long;
    align = 2;
    dst_type = types._float4;
    break;
  case MTLAttributeFormat::ShortNormalized:
    op = "snorm1x16.f32";
    src_type = types._short;
    align = 2;
    dst_type = types._float;
    break;
  case MTLAttributeFormat::Short2Normalized:
    op = "snorm2x16.v2f32";
    src_type = types._int;
    align = 2;
    dst_type = types._float2;
    break;
  case MTLAttributeFormat::Short4Normalized:
    op = "snorm4x16.v4f32";
    src_type = types._long;
    align = 2;
    dst_type = types._float4;
    break;
  case MTLAttributeFormat::UInt1010102Normalized:
    op = "unorm.rgb10a2.v4f32";
    src_type = types._int;
    align = 4;
    dst_type = types._float4;
    break;
  case MTLAttributeFormat::Int1010102Normalized:
    assert(0 && "unused");
    // op = "unorm.rgb10a2.v4f32";
    // src_type = types._int;
    // align = 4;
    // dst_type = types._float4;
    break;
  case MTLAttributeFormat::FloatRG11B10:
    op = "unorm.rg11b10f.v3f32";
    src_type = types._int;
    align = 4;
    dst_type = types._float3;
    break;
  case MTLAttributeFormat::FloatRGB9E5:
    op = "unorm.rgb9e5.v3f32";
    src_type = types._int;
    align = 4;
    dst_type = types._float3;
    break;
  default:
    break;
  }
  assert(src_type && dst_type);
  pvalue ret = co_yield call_unpack_impl(
    op,
    co_yield load_from_device_buffer(
      src_type, base_addr, aligned_offest, 0, align
    ),
    src_type, dst_type
  );
  if (format == MTLAttributeFormat::UChar4Normalized_BGRA) {
    ret = builder.CreateShuffleVector(ret, {2, 1, 0, 3});
  }
  if (dst_type == types._float) {
    ret = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._float4), ret, (int)0
    );
    ret = builder.CreateInsertElement(ret, co_yield get_float(0), (int)1);
    ret = builder.CreateInsertElement(ret, co_yield get_float(0), (int)2);
    ret = builder.CreateInsertElement(ret, co_yield get_float(1), (int)3);
  } else if (dst_type == types._float2) {
    ret = builder.CreateShuffleVector(ret, {0, 1, -1, -1});
    ret = builder.CreateInsertElement(ret, co_yield get_float(0), (int)2);
    ret = builder.CreateInsertElement(ret, co_yield get_float(1), (int)3);
  } else if (dst_type == types._float3) {
    ret = builder.CreateShuffleVector(ret, {0, 1, 2, -1});
    ret = builder.CreateInsertElement(ret, co_yield get_float(1), (int)3);
  }
  co_return ret;
};

AIRBuilderResult pull_vec4_from_addr_checked(
  MTLAttributeFormat format, pvalue base_addr, pvalue byte_offset
) {
  auto ctx = co_yield get_context();
  auto &types = ctx.types;
  auto &builder = ctx.builder;

  pvalue value = nullptr;

  switch (format) {
  case MTLAttributeFormat::Char:
    value = co_yield load_from_device_buffer(
      types._byte, base_addr, byte_offset, 0, 1
    );
    value = builder.CreateSExt(value, types._int);
    value = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._int4), value, (int)0
    );
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)1);
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::Char2:
    value = co_yield load_from_device_buffer(
      types._char2, base_addr, byte_offset, 0, 1
    );
    value = builder.CreateSExt(value, types._int2);
    value = builder.CreateShuffleVector(value, {0, 1, -1, -1});
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::Char4:
    value = co_yield load_from_device_buffer(
      types._char4, base_addr, byte_offset, 0, 1
    );
    value = builder.CreateSExt(value, types._int4);
    break;
  case MTLAttributeFormat::UChar:
    value = co_yield load_from_device_buffer(
      types._byte, base_addr, byte_offset, 0, 1
    );
    value = builder.CreateZExt(value, types._int);
    value = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._int4), value, (int)0
    );
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)1);
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::UChar2:
    value = co_yield load_from_device_buffer(
      types._char2, base_addr, byte_offset, 0, 1
    );
    value = builder.CreateZExt(value, types._int2);
    value = builder.CreateShuffleVector(value, {0, 1, -1, -1});
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::UChar4:
    value = co_yield load_from_device_buffer(
      types._char4, base_addr, byte_offset, 0, 1
    );
    value = builder.CreateZExt(value, types._int4);
    break;
  case MTLAttributeFormat::Short:
    value = co_yield load_from_device_buffer(
      types._short, base_addr, byte_offset, 0, 2
    );
    value = builder.CreateSExt(value, types._int);
    value = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._int4), value, (int)0
    );
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)1);
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::Short2:
    value = co_yield load_from_device_buffer(
      types._short2, base_addr, byte_offset, 0, 2
    );
    value = builder.CreateSExt(value, types._int2);
    value = builder.CreateShuffleVector(value, {0, 1, -1, -1});
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::Short4:
    value = co_yield load_from_device_buffer(
      types._short4, base_addr, byte_offset, 0, 2
    );
    value = builder.CreateSExt(value, types._int4);
    break;
  case MTLAttributeFormat::UShort:
    value = co_yield load_from_device_buffer(
      types._short, base_addr, byte_offset, 0, 2
    );
    value = builder.CreateZExt(value, types._int);
    value = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._int4), value, (int)0
    );
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)1);
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::UShort2:
    value = co_yield load_from_device_buffer(
      types._short2, base_addr, byte_offset, 0, 2
    );
    value = builder.CreateZExt(value, types._int2);
    value = builder.CreateShuffleVector(value, {0, 1, -1, -1});
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::UShort4:
    value = co_yield load_from_device_buffer(
      types._short4, base_addr, byte_offset, 0, 2
    );
    value = builder.CreateZExt(value, types._int4);
    break;
  case MTLAttributeFormat::Half:
    value = co_yield load_from_device_buffer(
      types._half, base_addr, byte_offset, 0, 2
    );
    value = co_yield call_convert(value, types._float, Sign::with_sign);
    value = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._float4), value, (int)0
    );
    value = builder.CreateInsertElement(value, co_yield get_float(0), (int)1);
    value = builder.CreateInsertElement(value, co_yield get_float(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_float(1), (int)3);
    break;
  case MTLAttributeFormat::Half2:
    value = co_yield load_from_device_buffer(
      types._half2, base_addr, byte_offset, 0, 2
    );
    value = co_yield call_convert(value, types._float, Sign::with_sign);
    value = builder.CreateShuffleVector(value, {0, 1, -1, -1});
    value = builder.CreateInsertElement(value, co_yield get_float(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_float(1), (int)3);
    break;
  case MTLAttributeFormat::Half4:
    value = co_yield load_from_device_buffer(
      types._half4, base_addr, byte_offset, 0, 2
    );
    value = co_yield call_convert(value, types._float, Sign::with_sign);
    break;
  case MTLAttributeFormat::Float:
    value = co_yield load_from_device_buffer(
      types._float, base_addr, byte_offset, 0, 4
    );
    value = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._float4), value, (int)0
    );
    value = builder.CreateInsertElement(value, co_yield get_float(0), (int)1);
    value = builder.CreateInsertElement(value, co_yield get_float(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_float(1), (int)3);
    break;
  case MTLAttributeFormat::Float2:
    value = co_yield load_from_device_buffer(
      types._float2, base_addr, byte_offset, 0, 4
    );
    value = builder.CreateShuffleVector(value, {0, 1, -1, -1});
    value = builder.CreateInsertElement(value, co_yield get_float(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_float(1), (int)3);
    break;
  case MTLAttributeFormat::Float3:
    value = co_yield load_from_device_buffer(
      types._float3, base_addr, byte_offset, 0, 4
    );
    value = builder.CreateShuffleVector(value, {0, 1, 2, -1});
    value = builder.CreateInsertElement(value, co_yield get_float(1), (int)3);
    break;
  case MTLAttributeFormat::Float4:
    value = co_yield load_from_device_buffer(
      types._float4, base_addr, byte_offset, 0, 4
    );
    break;
  case MTLAttributeFormat::UInt:
  case MTLAttributeFormat::Int:
    value = co_yield load_from_device_buffer(
      types._int, base_addr, byte_offset, 0, 4
    );
    value = builder.CreateInsertElement(
      llvm::PoisonValue::get(types._int4), value, (int)0
    );
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)1);
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::UInt2:
  case MTLAttributeFormat::Int2:
    value = co_yield load_from_device_buffer(
      types._int2, base_addr, byte_offset, 0, 4
    );
    value = builder.CreateShuffleVector(value, {0, 1, -1, -1});
    value = builder.CreateInsertElement(value, co_yield get_int(0), (int)2);
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::UInt3:
  case MTLAttributeFormat::Int3:
    value = co_yield load_from_device_buffer(
      types._int3, base_addr, byte_offset, 0, 4
    );
    value = builder.CreateShuffleVector(value, {0, 1, 2, -1});
    value = builder.CreateInsertElement(value, co_yield get_int(1), (int)3);
    break;
  case MTLAttributeFormat::UInt4:
  case MTLAttributeFormat::Int4:
    value = co_yield load_from_device_buffer(
      types._int4, base_addr, byte_offset, 0, 4
    );
    break;
  case MTLAttributeFormat::UCharNormalized:
  case MTLAttributeFormat::UChar2Normalized:
  case MTLAttributeFormat::UChar4Normalized:
  case MTLAttributeFormat::UChar4Normalized_BGRA:
  case MTLAttributeFormat::CharNormalized:
  case MTLAttributeFormat::Char2Normalized:
  case MTLAttributeFormat::Char4Normalized:
  case MTLAttributeFormat::UShortNormalized:
  case MTLAttributeFormat::UShort2Normalized:
  case MTLAttributeFormat::UShort4Normalized:
  case MTLAttributeFormat::ShortNormalized:
  case MTLAttributeFormat::Short2Normalized:
  case MTLAttributeFormat::Short4Normalized:
  case MTLAttributeFormat::FloatRG11B10:
  case MTLAttributeFormat::FloatRGB9E5:
  case MTLAttributeFormat::Int1010102Normalized:
  case MTLAttributeFormat::UInt1010102Normalized:
    value = co_yield unpack_fvec4_from_addr(format, base_addr, byte_offset);
    break;
  case MTLAttributeFormat::Invalid:
    break;
  }
  assert(value);

  co_return value;
};

AIRBuilderResult pull_vec4_from_addr(
  MTLAttributeFormat format, pvalue base_addr, pvalue byte_offset
) {
  return make_op([=](struct AIRBuilderContext ctx) -> llvm::Expected<pvalue> {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &builder = ctx.builder;

    auto current = builder.GetInsertBlock();

    auto is_null_binding = builder.CreateICmpEQ(
      builder.CreatePtrToInt(base_addr, ctx.types._long), builder.getInt64(0)
    );

    auto pull_vertex =
      llvm::BasicBlock::Create(context, "", current->getParent());
    auto continuation =
      llvm::BasicBlock::Create(context, "", current->getParent());

    builder.CreateCondBr(is_null_binding, continuation, pull_vertex);
    builder.SetInsertPoint(pull_vertex);

    auto ret =
      pull_vec4_from_addr_checked(format, base_addr, byte_offset).build(ctx);
    if (auto err = ret.takeError()) {
      return err;
    }
    auto value = ret.get();

    builder.CreateBr(continuation);
    builder.SetInsertPoint(continuation);

    auto phi = builder.CreatePHI(value->getType(), 2);
    phi->addIncoming(ConstantAggregateZero::get(value->getType()), current);
    phi->addIncoming(value, pull_vertex);

    return phi;
  });
};

AIRBuilderResult
call_set_mesh_properties(pvalue mesh_grid_props, pvalue grid_size) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
      context,
      {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
       {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
       {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
       {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
       {~0U, Attribute::get(context, Attribute::AttrKind::MustProgress)}}
    );
    auto fn = (module.getOrInsertFunction(
      "air.set_threadgroups_per_grid_mesh_properties",
      llvm::FunctionType::get(
        llvm::Type::getVoidTy(context),
        {ctx.types._mesh_grid_properties->getPointerTo(3), ctx.types._int3}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {mesh_grid_props, grid_size});
  });
};

AIRBuilderResult
call_interpolate_at_center(pvalue interpolant, bool perspective) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
      context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
                {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
      perspective ? "air.interpolate_center_perspective.v4f32"
                  : "air.interpolate_center_no_perspective.v4f32",
      llvm::FunctionType::get(
        ctx.types._float4, {ctx.types._interpolant->getPointerTo(1)}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {interpolant});
  });
}

AIRBuilderResult
call_interpolate_at_centroid(pvalue interpolant, bool perspective) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
      context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
                {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
      perspective ? "air.interpolate_centroid_perspective.v4f32"
                  : "air.interpolate_centroid_no_perspective.v4f32",
      llvm::FunctionType::get(
        ctx.types._float4, {ctx.types._interpolant->getPointerTo(1)}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {interpolant});
  });
}

AIRBuilderResult call_interpolate_at_offset(
  pvalue interpolant, bool perspective, pvalue offset
) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
      context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
                {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ReadOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
      perspective ? "air.interpolate_offset_perspective.v4f32"
                  : "air.interpolate_offset_no_perspective.v4f32",
      llvm::FunctionType::get(
        ctx.types._float4,
        {ctx.types._interpolant->getPointerTo(1), ctx.types._float2}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {interpolant, offset});
  });
}

AIRBuilderResult
call_interpolate_at_sample(pvalue interpolant, bool perspective, pvalue index) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
      context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                {1U, Attribute::get(context, Attribute::AttrKind::ReadOnly)},
                {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
      perspective ? "air.interpolate_sample_perspective.v4f32"
                  : "air.interpolate_sample_no_perspective.v4f32",
      llvm::FunctionType::get(
        ctx.types._float4,
        {ctx.types._interpolant->getPointerTo(1), ctx.types._int}, false
      ),
      att
    ));
    return ctx.builder.CreateCall(fn, {interpolant, index});
  });
}

AIRBuilderResult
call_set_mesh_render_target_array_index(pvalue mesh, pvalue vid, pvalue render_target_array_index) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_render_target_array_index_mesh.i32",
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int, ctx.types._int}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, vid, render_target_array_index});
  });
}

AIRBuilderResult call_set_mesh_viewport_array_index(pvalue mesh, pvalue vid, pvalue viewport_array_index) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_viewport_array_index_mesh.i32",
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int, ctx.types._int}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, vid, viewport_array_index});
  });
}

AIRBuilderResult call_set_mesh_position(pvalue mesh, pvalue vid, pvalue position) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_position_mesh",
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int, ctx.types._float4}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, vid, position});
  });
}

AIRBuilderResult call_set_mesh_clip_distance(pvalue mesh, uint32_t idx, pvalue vid, pvalue value) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_clip_distance_mesh",
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int, ctx.types._int, ctx.types._float}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, ctx.builder.getInt32(idx), vid, value});
  });
}

AIRBuilderResult
call_set_mesh_vertex_data(pvalue mesh, uint32_t idx, pvalue vid, pvalue value) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_vertex_data_mesh" + type_overload_suffix(value->getType()),
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int, ctx.types._int, value->getType()}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, ctx.builder.getInt32(idx), vid, value});
  });
}

AIRBuilderResult
call_set_mesh_primitive_data(pvalue mesh, uint32_t idx, pvalue pid, pvalue value) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_primitive_data_mesh" + type_overload_suffix(value->getType()),
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int, ctx.types._int, value->getType()}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, ctx.builder.getInt32(idx), pid, value});
  });
}

AIRBuilderResult call_set_mesh_index(pvalue mesh, pvalue index, pvalue vertex) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_index_mesh",
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int, ctx.types._byte}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, index, ctx.builder.CreateZExtOrTrunc(vertex, ctx.types._byte)});
  });
}

AIRBuilderResult call_set_mesh_primitive_count(pvalue mesh, pvalue count) {
  return make_op([=](struct AIRBuilderContext ctx) {
    using namespace llvm;
    auto &context = ctx.llvm;
    auto &module = ctx.module;
    auto att = AttributeList::get(
        context, {{1U, Attribute::get(context, Attribute::AttrKind::NoCapture)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)},
                  {~0U, Attribute::get(context, Attribute::AttrKind::ArgMemOnly)}}
    );
    auto fn = (module.getOrInsertFunction(
        "air.set_primitive_count_mesh",
        llvm::FunctionType::get(
            llvm::Type::getVoidTy(context),
            {ctx.types._mesh->getPointerTo(7 /* mesh */), ctx.types._int}, false
        ),
        att
    ));
    return ctx.builder.CreateCall(fn, {mesh, count});
  });
}

}; // namespace dxmt::air