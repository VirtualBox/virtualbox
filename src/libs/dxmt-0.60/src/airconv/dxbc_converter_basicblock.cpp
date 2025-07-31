#include "air_operations.hpp"
#include "air_signature.hpp"
#include "air_type.hpp"
#include "airconv_error.hpp"
#include "airconv_public.h"
#include "dxbc_converter.hpp"
#include "ftl.hpp"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <stack>

char dxmt::UnsupportedFeature::ID;

namespace dxmt::dxbc {

template <typename T = std::monostate>
ReaderIO<context, T> throwUnsupported(llvm::StringRef ref) {
  return ReaderIO<context, T>(
    [msg = std::string(ref)](struct context ctx) mutable -> llvm::Expected<T> {
      // CAUTION: create llvm::Error as lazy as possible
      // DON'T create it and store somewhere like plain data
      // ^ which make fking perfect sense but NO DONT DO THAT!
      // BECAUSE IT IS STATEFUL! IT PRESEVED A 'CHECKED' STATE
      // to enforce programmer handle any created error
      // ^ imaging you move-capture the error in a lambda
      //   but it's never called and eventually deconstructed
      //   then BOOM
      // what the fuck why should I use it
      // `Monadic error handling w/ a gun shoot yourself in the foot`
      // ^ never claimed to be a monad actually 👆🤓
      // DO I REALLY DESERVE THIS?
      return llvm::Expected<T>(llvm::make_error<UnsupportedFeature>(msg));
    }
  );
};

ReaderIO<context, context> get_context() {
  return ReaderIO<context, context>([](struct context ctx) { return ctx; });
};

auto bitcast_float4(pvalue vec4) {
  return make_irvalue([=](context s) {
    if (vec4->getType() == s.types._float4)
      return vec4;
    return s.builder.CreateBitCast(vec4, s.types._float4);
  });
}

auto bitcast_int4(pvalue vec4) {
  return make_irvalue([=](context s) {
    if (vec4->getType() == s.types._int4)
      return vec4;
    return s.builder.CreateBitCast(vec4, s.types._int4);
  });
}

auto truncate_vec(uint8_t elements) {
  assert(elements > 1 && "use extract_element to extract single element");
  return [=](pvalue vec) {
    assert((vec->getType()->getTypeID() == llvm::Type::FixedVectorTyID));
    auto origin_element_count =
      cast<llvm::FixedVectorType>(vec->getType())->getNumElements();
    assert(origin_element_count >= elements);
    return make_irvalue([=](struct context ctx) {
      switch (elements) {
      case 2:
        return ctx.builder.CreateShuffleVector(vec, {0, 1});
      case 3:
        return ctx.builder.CreateShuffleVector(vec, {0, 1, 2});
      case 4:
        return ctx.builder.CreateShuffleVector(vec, {0, 1, 2, 3});
      default:
        assert(0 && "unexpected element count");
      }
    });
  };
};

auto read_int(pvalue vec4, Swizzle swizzle) {
  return bitcast_int4(vec4) >>= [=](auto bitcasted) {
    return make_irvalue([=](context s) {
      return s.builder.CreateExtractElement(bitcasted, swizzle.r);
    });
  };
};

auto read_int2(pvalue vec4, Swizzle swizzle) {
  return bitcast_int4(vec4) >>= [=](auto bitcasted) {
    return make_irvalue([=](context s) {
      return s.builder.CreateShuffleVector(bitcasted, {swizzle.r, swizzle.g});
    });
  };
};

auto read_int3(pvalue vec4, Swizzle swizzle) {
  return bitcast_int4(vec4) >>= [=](auto bitcasted) {
    return make_irvalue([=](context s) {
      return s.builder.CreateShuffleVector(
        bitcasted, {swizzle.r, swizzle.g, swizzle.b}
      );
    });
  };
};

auto read_int4(pvalue vec4, Swizzle swizzle) {
  if (swizzle == swizzle_identity) {
    return bitcast_int4(vec4);
  }
  return bitcast_int4(vec4) >>= [=](auto bitcasted) {
    return make_irvalue([=](context s) {
      return s.builder.CreateShuffleVector(
        bitcasted, {swizzle.r, swizzle.g, swizzle.b, swizzle.a}
      );
    });
  };
};

auto get_int2(uint32_t value0, uint32_t value1) -> IRValue {
  return make_irvalue([=](context ctx) {
    return llvm::ConstantVector::get(
      {llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value0, true}),
       llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value1, true})}
    );
  });
};

auto get_float2(float value0, float value1) -> IRValue {
  return make_irvalue([=](context ctx) {
    return llvm::ConstantVector::get(
      {llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value0}),
       llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value1})}
    );
  });
};

auto get_int3(uint32_t value0, uint32_t value1, uint32_t value2) -> IRValue {
  return make_irvalue([=](context ctx) {
    return llvm::ConstantVector::get(
      {llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value0, true}),
       llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value1, true}),
       llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value2, true})}
    );
  });
};

auto get_splat_constant(uint32_t value, uint32_t mask) -> IRValue {
  assert(mask);
  return make_irvalue([=](context ctx) -> pvalue {
    switch (mask) {
    case 0b1:
    case 0b10:
    case 0b100:
    case 0b1000: {
      return llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true});
    }
    case 0b11:
    case 0b110:
    case 0b101:
    case 0b1100:
    case 0b1010:
    case 0b1001: {
      return llvm::ConstantVector::get(
        {llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true}),
         llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true})}
      );
    }
    case 0b111:
    case 0b1101:
    case 0b1011:
    case 0b1110: {
      return llvm::ConstantVector::get(
        {llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true}),
         llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true}),
         llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true})}
      );
    }
    default: {
      return llvm::ConstantVector::get(
        {llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true}),
         llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true}),
         llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true}),
         llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, value, true})}
      );
    }
    }
  });
};

auto get_splat_type(llvm::Type *scalar, uint32_t mask) -> llvm::Type * {
  assert(mask);
  switch (mask) {
  case 0b1:
  case 0b10:
  case 0b100:
  case 0b1000: {
    return scalar;
  }
  case 0b11:
  case 0b110:
  case 0b101:
  case 0b1100:
  case 0b1010:
  case 0b1001: {
    return llvm::FixedVectorType::get(scalar, 2);
  }
  case 0b111:
  case 0b1101:
  case 0b1011:
  case 0b1110: {
    return llvm::FixedVectorType::get(scalar, 3);
  }
  default: {
    return llvm::FixedVectorType::get(scalar, 4);
  }
  }
};

auto get_int(uint32_t value) -> IRValue {
  return make_irvalue([=](context ctx) { return ctx.builder.getInt32(value); });
};

auto get_float(float value) -> IRValue {
  return make_irvalue([=](context ctx) {
    return llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value});
  });
};

auto get_float4_splat(float value) -> IRValue {
  return make_irvalue([=](context ctx) {
    return llvm::ConstantVector::get(
      {llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value}),
       llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value}),
       llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value}),
       llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{value})}
    );
  });
};

auto extend_to_vec4(pvalue value) {
  return make_irvalue([=](context ctx) {
    auto ty = value->getType();
    if (ty->isVectorTy()) {
      auto vecTy = llvm::cast<llvm::FixedVectorType>(ty);
      if (vecTy->getNumElements() == 4)
        return ctx.builder.CreateShuffleVector(value, {0, 1, 2, 3});
      if (vecTy->getNumElements() == 3)
        return ctx.builder.CreateShuffleVector(value, {0, 1, 2, 2});
      if (vecTy->getNumElements() == 2)
        return ctx.builder.CreateShuffleVector(value, {0, 1, 1, 1});
      if (vecTy->getNumElements() == 1)
        return ctx.builder.CreateShuffleVector(value, {0, 0, 0, 0});
      assert(0 && "?");
    } else {
      return ctx.builder.CreateVectorSplat(4, value);
    }
  });
}

auto extract_element(uint32_t index) {
  return [=](pvalue vec) {
    return make_irvalue([=](context ctx) -> llvm::Value * {
      if (auto constant_vec = llvm::dyn_cast<llvm::ConstantVector>(vec)) {
        return constant_vec->getAggregateElement(index);
      };
      if (auto data_vec = llvm::dyn_cast<llvm::ConstantDataVector>(vec)) {
        return data_vec->getAggregateElement(index);
      };
      return ctx.builder.CreateExtractElement(vec, index);
    });
  };
}

auto extract_value(uint32_t index) {
  return [=](pvalue aggregate) {
    return make_irvalue([=](context ctx) {
      return ctx.builder.CreateExtractValue(aggregate, {index});
    });
  };
}

auto swizzle(Swizzle swizzle) {
  return [=](pvalue vec) {
    return make_irvalue([=](context ctx) -> llvm::Value * {
      if (auto constant_vec = llvm::dyn_cast<llvm::ConstantVector>(vec)) {
        return llvm::ConstantVector::get(
          {constant_vec->getAggregateElement(swizzle.x),
           constant_vec->getAggregateElement(swizzle.y),
           constant_vec->getAggregateElement(swizzle.z),
           constant_vec->getAggregateElement(swizzle.w)}
        );
      };
      if (auto data_vec = llvm::dyn_cast<llvm::ConstantDataVector>(vec)) {
        if (data_vec->getElementType() == ctx.types._int) {
          return llvm::ConstantDataVector::get(
            ctx.llvm,
            llvm::ArrayRef<uint32_t>{
              (uint32_t)data_vec->getElementAsInteger(swizzle.x),
              (uint32_t)data_vec->getElementAsInteger(swizzle.y),
              (uint32_t)data_vec->getElementAsInteger(swizzle.z),
              (uint32_t)data_vec->getElementAsInteger(swizzle.w)
            }
          );
        }
        if (data_vec->getElementType() == ctx.types._float) {
          return llvm::ConstantDataVector::get(
            ctx.llvm,
            llvm::ArrayRef<float>{
              data_vec->getElementAsFloat(swizzle.x),
              data_vec->getElementAsFloat(swizzle.y),
              data_vec->getElementAsFloat(swizzle.z),
              data_vec->getElementAsFloat(swizzle.w)
            }
          );
        }
      };
      // FIXME: should guarantee vec is of 4 elements
      if (auto is_vec = llvm::dyn_cast<llvm::VectorType>(vec->getType())) {
        return ctx.builder.CreateShuffleVector(
          vec, (std::array<int, 4>)swizzle
        );
      }
      return ctx.builder.CreateVectorSplat(4, vec); // effective
    });
  };
}

auto to_desired_type_from_int_vec4(pvalue vec4, llvm::Type *desired, uint32_t mask) {
  return make_irvalue([=](context ctx) {
    assert(vec4->getType() == ctx.types._int4);
    std::function<pvalue(pvalue, llvm::Type *)> convert =
      [&ctx, &convert, mask](pvalue vec4, llvm::Type *desired) {
        auto masked = [mask](int i) {
          return (mask & (1 << i)) ? i : llvm::UndefMaskElem;
        };
        if (desired == ctx.types._int4)
          return ctx.builder.CreateShuffleVector(
            vec4, {masked(0), masked(1), masked(2), masked(3)}
          );
        if (desired == ctx.types._float4)
          return ctx.builder.CreateShuffleVector(
            ctx.builder.CreateBitCast(vec4, ctx.types._float4),
            {masked(0), masked(1), masked(2), masked(3)}
          );
        // FIXME: 3d/2d vector implementations are unused and probably wrong!
        if (desired == ctx.types._int3)
          return ctx.builder.CreateShuffleVector(
            vec4, {masked(0), masked(1), masked(2)}
          );
        if (desired == ctx.types._float3)
          return ctx.builder.CreateShuffleVector(
            convert(vec4, ctx.types._float4), {masked(0), masked(1), masked(2)}
          );
        if (desired == ctx.types._int2)
          return ctx.builder.CreateShuffleVector(vec4, {masked(0), masked(1)});
        if (desired == ctx.types._float2)
          return ctx.builder.CreateShuffleVector(
            convert(vec4, ctx.types._float4), {masked(0), masked(1)}
          );
        if (desired == ctx.types._int)
          return ctx.builder.CreateExtractElement(vec4, (uint64_t)__builtin_ctz(mask));
        if (desired == ctx.types._float)
          return ctx.builder.CreateExtractElement(
            ctx.builder.CreateBitCast(vec4, ctx.types._float4), (uint64_t)__builtin_ctz(mask)
          );
        assert(0 && "unhandled vec4");
      };
    return convert(vec4, desired);
  });
};

auto get_function_arg(uint32_t arg_index) {
  return make_irvalue([=](context ctx) {
    return ctx.function->getArg(arg_index);
  });
};

auto get_shuffle_mask(uint32_t writeMask) {
  // original value: 0 1 2 3
  // new value: 4 5 6 7
  // if writeMask at specific component is not set, use original value
  std::array<int, 4> mask = {0, 1, 2, 3}; // identity selection
  if (writeMask & 1) {
    mask[0] = 4;
  }
  if (writeMask & 2) {
    mask[1] = 5;
  }
  if (writeMask & 4) {
    mask[2] = 6;
  }
  if (writeMask & 8) {
    mask[3] = 7;
  }
  return mask;
}

/* it should return sign extended uint4 (all bits 0 or all bits 1) */
auto cmp_integer(llvm::CmpInst::Predicate cmp, pvalue a, pvalue b) {
  return make_irvalue([=](context ctx) {
    return ctx.builder.CreateSExt(
      ctx.builder.CreateICmp(cmp, a, b),
      isa<llvm::FixedVectorType>(a->getType())
        ? llvm::FixedVectorType::get(
            ctx.types._int,
            cast<llvm::FixedVectorType>(a->getType())->getNumElements()
          )
        : ctx.types._int
    );
  });
};

bool is_constant_zero(pvalue v) {
  if (llvm::isa<llvm::Constant>(v)) {
    return llvm::cast<llvm::Constant>(v)->isZeroValue();
  }
  return false;
}

/* it should return sign extended uint4 (all bits 0 or all bits 1) */
auto cmp_float(llvm::CmpInst::Predicate cmp, pvalue a, pvalue b) {
  return make_irvalue([=](context ctx) {
    bool should_comparison_be_precise = is_constant_zero(a) || is_constant_zero(b);
    pvalue comparison_result;
    if (should_comparison_be_precise) {
      bool previous_fastmath_state = ctx.builder.getFastMathFlags().isFast();
      ctx.builder.getFastMathFlags().setFast(false);
      comparison_result = ctx.builder.CreateFCmp(cmp, a, b);
      ctx.builder.getFastMathFlags().setFast(previous_fastmath_state);
    } else {
      comparison_result = ctx.builder.CreateFCmp(cmp, a, b);
    }
    return ctx.builder.CreateSExt(
      comparison_result,
      isa<llvm::FixedVectorType>(a->getType())
        ? llvm::FixedVectorType::get(
            ctx.types._int,
            cast<llvm::FixedVectorType>(a->getType())->getNumElements()
          )
        : ctx.types._int
    );
  });
};

auto load_from_array_at(llvm::Value *array, pvalue index) -> IRValue {
  return make_irvalue([=](context ctx) {
    auto array_ty = llvm::cast<llvm::ArrayType>( // force line break
      llvm::cast<llvm::PointerType>(array->getType())
        ->getNonOpaquePointerElementType()
    );
    auto ptr = ctx.builder.CreateGEP(
      array_ty, array, {ctx.builder.getInt32(0), index}, "",
      llvm::isa<llvm::ConstantInt>(index)
    );
    return ctx.builder.CreateLoad(array_ty->getElementType(), ptr);
  });
};

IRValue
load_from_vec4_array_masked(llvm::Value *array, pvalue index, uint32_t mask) {
  if (mask == 0b1111) {
    return load_from_array_at(array, index);
  }
  return make_irvalue([=](context ctx) {
    auto array_type = llvm::cast<llvm::PointerType>(array->getType())->getNonOpaquePointerElementType();
    auto vec4_type = llvm::cast<llvm::ArrayType>(array_type)->getArrayElementType();
    auto ele_type = llvm::cast<llvm::VectorType>(vec4_type)->getElementType();
    pvalue value = llvm::ConstantAggregateZero::get(vec4_type);
    for (unsigned i = 0; i < 4; i++) {
      if ((mask & (1 << i)) == 0)
        continue;
      auto component_ptr =
          ctx.builder.CreateGEP(array_type, array, {ctx.builder.getInt32(0), index, ctx.builder.getInt32(i)});
      value = ctx.builder.CreateInsertElement(value, ctx.builder.CreateLoad(ele_type, component_ptr), uint64_t(i));
    }
    return value;
  });
};

auto store_to_array_at(
  llvm::Value *array, pvalue index, pvalue vec4_type_matched
) -> IREffect {
  return make_effect([=](context ctx) {
    auto ptr = ctx.builder.CreateInBoundsGEP(
      llvm::cast<llvm::PointerType>(array->getType())
        ->getNonOpaquePointerElementType(),
      array, {ctx.builder.getInt32(0), index}
    );
    ctx.builder.CreateStore(vec4_type_matched, ptr);
    return std::monostate();
  });
};

IREffect store_at_vec4_array_masked(
  llvm::Value *array, pvalue index, pvalue maybe_vec4, uint32_t mask
) {
  return extend_to_vec4(maybe_vec4) >>= [=](pvalue vec4) {
    if (mask == 0b1111) {
      return store_to_array_at(array, index, vec4);
    }
    return make_effect([=](context ctx) {
      for (unsigned i = 0; i < 4; i++) {
        if ((mask & (1 << i)) == 0)
          continue;
        auto component_ptr =  ctx.builder.CreateGEP(
          llvm::cast<llvm::PointerType>(array->getType())
            ->getNonOpaquePointerElementType(),
          array, {ctx.builder.getInt32(0), index, ctx.builder.getInt32(i)}
        );
        ctx.builder.CreateStore(
          ctx.builder.CreateExtractElement(vec4, i), component_ptr
        );
      }
      return std::monostate();
    });
  };
};

auto store_at_vec_array_masked(
  llvm::Value *array, pvalue index, pvalue maybe_vec4, uint32_t mask
) -> IREffect {
  auto array_ty = llvm::cast<llvm::ArrayType>( // force line break
    llvm::cast<llvm::PointerType>(array->getType())
      ->getNonOpaquePointerElementType()
  );
  auto components =
    cast<llvm::FixedVectorType>(array_ty->getElementType())->getNumElements();
  return make_effect([=](context ctx) {
    for (unsigned i = 0; i < components; i++) {
      if ((mask & (1 << i)) == 0)
        continue;
      auto component_ptr = ctx.builder.CreateGEP(
        array_ty,
        array, {ctx.builder.getInt32(0), index, ctx.builder.getInt32(i)}
      );
      ctx.builder.CreateStore(
        ctx.builder.CreateExtractElement(maybe_vec4, i), component_ptr
      );
    }
    return std::monostate();
  });
};

IREffect init_input_reg(
  uint32_t with_fnarg_at, uint32_t to_reg, uint32_t mask,
  bool fix_w_component
) {
  // no it doesn't work like this
  // regular input can be masked like .zw
  // assert((mask & 1) && "todo: handle input register sharing correctly");
  // FIXME: it's buggy as hell
  return make_effect_bind([=](context ctx) {
    pvalue arg = ctx.function->getArg(with_fnarg_at);
    auto const_index =
      llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, to_reg, false});
    if (arg->getType()->getScalarType()->isIntegerTy()) {
      if (arg->getType()->getScalarType() == ctx.types._bool) {
        // front_facing
        return store_at_vec4_array_masked(
          ctx.resource.input.ptr_int4, const_index,
          ctx.builder.CreateSExt(arg, ctx.types._int), mask
        );
      }
      if (arg->getType()->getScalarType() != ctx.types._int) {
        return throwUnsupported("TODO: handle non 32 bit integer");
      }
      return store_at_vec4_array_masked(
        ctx.resource.input.ptr_int4, const_index, arg, mask
      );
    } else if (arg->getType()->getScalarType()->isFloatTy()) {
      if (fix_w_component && isa<llvm::FixedVectorType>(arg->getType()) &&
          cast<llvm::FixedVectorType>(arg->getType())->getNumElements() == 4) {
        auto w_component = ctx.builder.CreateExtractElement(arg, 3);
        auto rcp_w = ctx.builder.CreateFDiv(
          llvm::ConstantFP::get(ctx.types._float, 1), w_component
        );
        arg = ctx.builder.CreateInsertElement(arg, rcp_w, 3);
      }
      return store_at_vec4_array_masked(
        ctx.resource.input.ptr_float4, const_index, arg, mask
      );
    } else {
      return throwUnsupported(
        "TODO: unhandled input type? might be interpolant..."
      );
    }
  });
};

IREffect init_input_reg_with_interpolation(
  uint32_t with_fnarg_at, uint32_t to_reg, uint32_t mask,
  air::Interpolation interpolation, uint32_t sampleidx_at
) {
  return make_effect_bind([=](context ctx) -> IREffect {
    pvalue interpolant = ctx.function->getArg(with_fnarg_at);
    auto const_index = llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, to_reg, false});
    pvalue interpolated_val = nullptr;
    switch(interpolation) {
    case air::Interpolation::center_perspective:
      interpolated_val = co_yield air::call_interpolate_at_center(interpolant, true);
      break;
    case air::Interpolation::center_no_perspective:
      interpolated_val = co_yield air::call_interpolate_at_center(interpolant, false);
      break;
    case air::Interpolation::centroid_perspective:
      interpolated_val = co_yield air::call_interpolate_at_centroid(interpolant, true);
      break;
    case air::Interpolation::centroid_no_perspective:
      interpolated_val = co_yield air::call_interpolate_at_centroid(interpolant, false);
      break;
    case air::Interpolation::sample_perspective:
      assert(~sampleidx_at);
      interpolated_val = co_yield air::call_interpolate_at_sample(interpolant, true, ctx.function->getArg(sampleidx_at));
      break;
    case air::Interpolation::sample_no_perspective:
      assert(~sampleidx_at);
      interpolated_val = co_yield air::call_interpolate_at_sample(interpolant, false, ctx.function->getArg(sampleidx_at));
      break;
    case air::Interpolation::flat:
      assert(0 && "unhandled interpolant mode");
      break;
    }
    co_return co_yield store_at_vec4_array_masked(
        ctx.resource.input.ptr_float4, const_index, interpolated_val, mask
    );
  });
}

std::function<IRValue(pvalue)>
pop_output_reg(uint32_t from_reg, uint32_t mask, uint32_t to_element) {
  return [=](pvalue ret) {
    return make_irvalue_bind([=](context ctx) {
      auto const_index =
        llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, from_reg, false});
      return load_from_array_at(
               ctx.resource.output.ptr_int4, const_index
             ) >>= [=, &ctx](auto ivec4) {
        auto desired_type =
          ctx.function->getReturnType()->getStructElementType(to_element);
        return to_desired_type_from_int_vec4(ivec4, desired_type, mask) |
               [=, &ctx](auto value) {
                 return ctx.builder.CreateInsertValue(ret, value, {to_element});
               };
      };
    });
  };
}

std::function<IRValue(pvalue)> pop_output_reg_fix_unorm(uint32_t from_reg, uint32_t mask, uint32_t to_element) {
  return [=](pvalue ret) {
    return make_irvalue_bind([=](context ctx) -> IRValue {
      auto const_index = llvm::ConstantInt::get(ctx.llvm, llvm::APInt{32, from_reg, false});
      auto fvec4 = co_yield load_from_array_at(ctx.resource.output.ptr_float4, const_index);
      auto fixed = ctx.builder.CreateFSub(fvec4, co_yield get_float4_splat(1.0f / 127500.0f) /* magic delta ?! */);
      co_return ctx.builder.CreateInsertValue(ret, fixed, {to_element});
    });
  };
}

IREffect init_tess_factor_patch_constant(uint32_t to_reg, uint32_t mask, uint32_t factor_index, uint32_t factor_count) {
  return make_effect_bind([=](context ctx) -> IREffect {
    auto src_ptr = ctx.builder.CreateGEP(
      ctx.types._half, ctx.resource.tess_factor_buffer,
      {ctx.builder.CreateAdd(ctx.builder.CreateMul(
         ctx.resource.instanced_patch_id, ctx.builder.getInt32(factor_count)
       ),
       ctx.builder.getInt32(factor_index))}
    );
    auto src_val = ctx.builder.CreateLoad(ctx.types._half, src_ptr);
    auto to_float = co_yield call_convert(
      src_val, ctx.types._float,
      air::Sign::with_sign /* intended */
    );
    auto array = ctx.resource.patch_constant_output.ptr_float4;
    auto array_ty = llvm::cast<llvm::ArrayType>( // force line break
      llvm::cast<llvm::PointerType>(array->getType())
        ->getNonOpaquePointerElementType()
    );
    auto component_ptr = ctx.builder.CreateGEP(
      array_ty, array,
      {ctx.builder.getInt32(0), ctx.builder.getInt32(to_reg),
       ctx.builder.getInt32(__builtin_ctz(mask))}
    );
    ctx.builder.CreateStore(to_float, component_ptr);
    co_return {};
  });
}

std::function<IRValue(pvalue)> pop_output_tess_factor(
  uint32_t from_reg, uint32_t mask, uint32_t factor_index, uint32_t factor_count
) {
  return [=](pvalue ret) -> IRValue {
    auto ctx = co_yield get_context();
    auto array = ctx.resource.patch_constant_output.ptr_float4;
    auto array_ty = llvm::cast<llvm::ArrayType>( // force line break
      llvm::cast<llvm::PointerType>(array->getType())
        ->getNonOpaquePointerElementType()
    );
    auto component_ptr = ctx.builder.CreateGEP(
      array_ty, array,
      {ctx.builder.getInt32(0), ctx.builder.getInt32(from_reg),
       ctx.builder.getInt32(__builtin_ctz(mask))}
    );
    auto to_half = co_yield call_convert(
      ctx.builder.CreateLoad(ctx.types._float, component_ptr), ctx.types._half,
      air::Sign::with_sign /* intended */
    );
    auto dst_ptr = ctx.builder.CreateGEP(
      ctx.types._half, ctx.resource.tess_factor_buffer,
      {ctx.builder.CreateAdd(ctx.builder.CreateMul(
         ctx.resource.instanced_patch_id, ctx.builder.getInt32(factor_count)
       ),
       ctx.builder.getInt32(factor_index))}
    );
    ctx.builder.CreateStore(to_half, dst_ptr);

    co_return nullptr;
  };
}

IREffect
pop_mesh_output_render_taget_array_index(uint32_t from_reg, uint32_t mask, pvalue primitive_id) {
  auto ctx = co_yield get_context();
  auto result = co_yield to_desired_type_from_int_vec4(
      co_yield load_from_array_at(ctx.resource.output.ptr_int4, ctx.builder.getInt32(from_reg)), ctx.types._int, mask
  );
  co_yield air::call_set_mesh_render_target_array_index(ctx.resource.mesh, primitive_id, result);
  co_return {};
}

IREffect
pop_mesh_output_viewport_array_index(uint32_t from_reg, uint32_t mask, pvalue primitive_id) {
  auto ctx = co_yield get_context();
  auto result = co_yield to_desired_type_from_int_vec4(
      co_yield load_from_array_at(ctx.resource.output.ptr_int4, ctx.builder.getInt32(from_reg)), ctx.types._int, mask
  );
  co_yield air::call_set_mesh_viewport_array_index(ctx.resource.mesh, primitive_id, result);
  co_return {};
}

IREffect
pop_mesh_output_position(uint32_t from_reg, uint32_t mask, pvalue vertex_id) {
  auto ctx = co_yield get_context();
  auto result = co_yield load_from_array_at(ctx.resource.output.ptr_float4, ctx.builder.getInt32(from_reg));
  co_yield air::call_set_mesh_position(ctx.resource.mesh, vertex_id, result);
  co_return {};
}

IREffect
pop_mesh_output_vertex_data(
    uint32_t from_reg, uint32_t mask, uint32_t idx, pvalue vertex_id, air::MSLScalerOrVectorType desired_type
) {
  auto ctx = co_yield get_context();
  auto result = co_yield to_desired_type_from_int_vec4(
      co_yield load_from_vec4_array_masked(ctx.resource.output.ptr_int4, ctx.builder.getInt32(from_reg), mask),
      air::get_llvm_type(desired_type, ctx.llvm), mask
  );
  co_yield air::call_set_mesh_vertex_data(ctx.resource.mesh, idx, vertex_id, result);
  co_return {};
}

IREffect pull_vertex_input(
  air::FunctionSignatureBuilder &func_signature, uint32_t to_reg, uint32_t mask,
  SM50_IA_INPUT_ELEMENT element_info, uint32_t slot_mask
) {
  auto vbuf_table = func_signature.DefineInput(air::ArgumentBindingBuffer{
    .buffer_size = {},
    .location_index = 16,
    .array_size = 0,
    .memory_access = air::MemoryAccess::read,
    .address_space = air::AddressSpace::constant,
    .type = air::msl_uint,
    .arg_name = "vertex_buffers",
    .raster_order_group = {},
  });
  return make_effect_bind([=](context ctx) -> IREffect {
    auto &types = ctx.types;
    auto &builder = ctx.builder;
    pvalue index;                   // uint
    if (element_info.step_function) // per_instance
    {
      if (element_info.step_rate) {
        index = builder.CreateAdd(
          ctx.resource.base_instance_id,
          builder.CreateUDiv(
            ctx.resource.instance_id,
            builder.getInt32(element_info.step_rate)
          )
        );
      } else {
        // 0 takes special meaning, that the instance data should never be
        // stepped at all
        index = ctx.resource.base_instance_id;
      }
    } else {
      index = ctx.resource.vertex_id_with_base;
    }
    if (!ctx.resource.vertex_buffer_table) {
      ctx.resource.vertex_buffer_table = ctx.builder.CreateBitCast(
        ctx.function->getArg(vbuf_table),
        ctx.types._dxmt_vertex_buffer_entry->getPointerTo((uint32_t
        )air::AddressSpace::constant)
      );
    }
    unsigned int shift = 32u - element_info.slot;
    unsigned int vertex_buffer_entry_index =
      element_info.slot ? __builtin_popcount((slot_mask << shift) >> shift) : 0;
    auto vertex_buffer_table = ctx.resource.vertex_buffer_table;
    auto vertex_buffer_entry = builder.CreateLoad(
      types._dxmt_vertex_buffer_entry,
      builder.CreateConstGEP1_32(
        types._dxmt_vertex_buffer_entry, vertex_buffer_table,
        vertex_buffer_entry_index
      )
    );
    auto base_addr = builder.CreateExtractValue(vertex_buffer_entry, {0});
    auto stride = builder.CreateExtractValue(vertex_buffer_entry, {1});
    auto byte_offset = builder.CreateAdd(
      builder.CreateMul(stride, index),
      builder.getInt32(element_info.aligned_byte_offset)
    );
    auto vec4 = co_yield air::pull_vec4_from_addr(
      (air::MTLAttributeFormat)element_info.format, base_addr, byte_offset
    );
    if (vec4->getType() == types._float4) {
      co_yield store_at_vec4_array_masked(
        ctx.resource.input.ptr_float4, builder.getInt32(element_info.reg), vec4,
        mask
      );
    } else {
      assert(vec4->getType() == types._int4);
      co_yield store_at_vec4_array_masked(
        ctx.resource.input.ptr_int4, builder.getInt32(element_info.reg), vec4,
        mask
      );
    }
    co_return {};
  });
};

auto saturate(bool sat) {
  return [sat](pvalue floaty) -> IRValue {
    if (sat) {
      return air::call_float_unary_op("saturate", floaty);
    } else {
      return air::pure(floaty);
    }
  };
}

IREffect call_discard_fragment() {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &module = ctx.module;
  auto att = AttributeList::get(
    context, {{~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)}}
  );
  auto fn = (module.getOrInsertFunction(
    "air.discard_fragment",
    llvm::FunctionType::get(Type::getVoidTy(context), {}, false), att
  ));
  ctx.builder.CreateCall(fn, {});
  co_return {};
}

IREffect call_threadgroup_barrier(mem_flags mem_flag) {
  using namespace llvm;
  auto ctx = co_yield get_context();
  auto &context = ctx.llvm;
  auto &types = ctx.types;
  auto &module = ctx.module;
  auto att = AttributeList::get(
    context, {{~0U, Attribute::get(context, Attribute::AttrKind::Convergent)},
              {~0U, Attribute::get(context, Attribute::AttrKind::NoUnwind)},
              {~0U, Attribute::get(context, Attribute::AttrKind::WillReturn)}}
  );

  auto fn = (module.getOrInsertFunction(
    "air.wg.barrier",
    llvm::FunctionType::get(
      Type::getVoidTy(context),
      {
        types._int, // mem_flag
        types._int, // scope, should be always 1?
      },
      false
    ),
    att
  ));
  ctx.builder.CreateCall(
    fn, {co_yield get_int((uint8_t)mem_flag), co_yield get_int(1)}
  );
  co_return {};
}

auto implicit_float_to_int(pvalue num) -> IRValue {
  auto &types = (co_yield get_context()).types;
  auto rounded = co_yield air::call_float_unary_op("rint", num);
  co_return co_yield call_convert(rounded, types._int, air::Sign::with_sign);
};

auto extend_to_int2(pvalue value) -> IRValue {
  return make_irvalue([=](context ctx) {
    return ctx.builder.CreateInsertElement(llvm::ConstantAggregateZero::get(ctx.types._int2), value, uint64_t(0));
  });
};

auto extend_to_float2(pvalue value) -> IRValue {
  return make_irvalue([=](context ctx) {
    return ctx.builder.CreateInsertElement(llvm::ConstantAggregateZero::get(ctx.types._float2), value, uint64_t(0));
  });
};

SrcOperandModifier get_modifier(SrcOperand src) {
  return std::visit(
    patterns{
      [](SrcOperandImmediate32) {
        return SrcOperandModifier{
          .swizzle = swizzle_identity, .abs = false, .neg = false
        };
      },
      [](SrcOperandAttribute src) { return src._; },
      [](SrcOperandConstantBuffer src) { return src._; },
      [](SrcOperandImmediateConstantBuffer src) { return src._; },
      [](SrcOperandInput src) { return src._; },
      [](SrcOperandTemp src) { return src._; },
      [](SrcOperandIndexableTemp src) { return src._; },
      [](SrcOperandIndexableInput src) { return src._; },
      [](SrcOperandInputICP src) { return src._; },
      [](SrcOperandInputOCP src) { return src._; },
      [](SrcOperandInputPC src) { return src._; },
      [](auto s) {
        llvm::outs() << "get_modifier: unhandled src operand type "
                     << decltype(s)::debug_name << "\n";
        assert(0 && "get_modifier: unhandled src operand type");
        return SrcOperandModifier{};
      }
    },
    src
  );
};

IRValue get_valid_components(pvalue ret, uint32_t mask) {
  auto ctx = co_yield get_context();
  switch (mask) {
  case 0b1:
    ret = co_yield extract_element(0)(ret);
    break;
  case 0b10:
    ret = co_yield extract_element(1)(ret);
    break;
  case 0b100:
    ret = co_yield extract_element(2)(ret);
    break;
  case 0b1000:
    ret = co_yield extract_element(3)(ret);
    break;
  case 0b1100:
    ret = ctx.builder.CreateShuffleVector(ret, {2, 3});
    break;
  case 0b0110:
    ret = ctx.builder.CreateShuffleVector(ret, {1, 2});
    break;
  case 0b0011:
    ret = ctx.builder.CreateShuffleVector(ret, {0, 1});
    break;
  case 0b1010:
    ret = ctx.builder.CreateShuffleVector(ret, {1, 3});
    break;
  case 0b0101:
    ret = ctx.builder.CreateShuffleVector(ret, {0, 2});
    break;
  case 0b1001:
    ret = ctx.builder.CreateShuffleVector(ret, {0, 3});
    break;
  case 0b1101:
    ret = ctx.builder.CreateShuffleVector(ret, {0, 2, 3});
    break;
  case 0b1011:
    ret = ctx.builder.CreateShuffleVector(ret, {0, 1, 3});
    break;
  case 0b1110:
    ret = ctx.builder.CreateShuffleVector(ret, {1, 2, 3});
    break;
  case 0b0111:
    ret = ctx.builder.CreateShuffleVector(ret, {0, 1, 2});
    break;
  case 0b1111:
  default:
    break;
  }
  co_return ret;
}

IRValue apply_float_src_operand_modifier(
  SrcOperandModifier c, llvm::Value *fvec4, uint32_t mask
) {
  auto ctx = co_yield get_context();
  assert(fvec4->getType() == ctx.types._float4);

  auto ret = fvec4;
  if (c.swizzle != swizzle_identity) {
    ret = ctx.builder.CreateShuffleVector(
      ret, {c.swizzle.x, c.swizzle.y, c.swizzle.z, c.swizzle.w}
    );
  }
  /* optimization for scaler */
  ret = co_yield get_valid_components(ret, mask);
  if (c.abs) {
    ret = co_yield air::call_float_unary_op("fabs", ret);
  }
  if (c.neg) {
    ret = ctx.builder.CreateFNeg(ret);
  }
  co_return ret;
};

IRValue apply_integer_src_operand_modifier(
  SrcOperandModifier c, llvm::Value *ivec4, uint32_t mask
) {
  auto ctx = co_yield get_context();
  assert(ivec4->getType() == ctx.types._int4);
  auto ret = ivec4;
  if (c.swizzle != swizzle_identity) {
    ret = ctx.builder.CreateShuffleVector(
      ret, {c.swizzle.x, c.swizzle.y, c.swizzle.z, c.swizzle.w}
    );
  }
  /* optimization for scaler */
  ret = co_yield get_valid_components(ret, mask);
  if (c.abs) {
    assert(0 && "otherwise dxbc spec is wrong");
  }
  if (c.neg) {
    ret = ctx.builder.CreateNeg(ret);
  }
  co_return ret;
};

uint32_t get_dst_mask(DstOperand dst) {
  return std::visit(
    patterns{
      [](DstOperandNull) { return (uint32_t)0b1111; },
      [](DstOperandSideEffect) { return (uint32_t)0b1111; },
      [](DstOperandOutput dst) { return dst._.mask; },
      [](DstOperandTemp dst) { return dst._.mask; },
      [](DstOperandIndexableTemp dst) { return dst._.mask; },
      [](DstOperandOutputDepth) { return (uint32_t)1; },
      [](DstOperandOutputCoverageMask) { return (uint32_t)1; },
      [](DstOperandIndexableOutput dst) { return dst._.mask; },
      [](auto s) {
        llvm::outs() << "get_dst_mask: unhandled dst operand type "
                     << decltype(s)::debug_name << "\n";
        assert(0 && "get_dst_mask: unhandled dst operand type");
        return (uint32_t)0;
      }
    },
    dst
  );
};

/* should provide single i32 value */
auto load_operand_index(OperandIndex idx) {
  return std::visit(
    patterns{
      [&](uint32_t v) {
        return make_irvalue([=](context ctx) { return ctx.builder.getInt32(v); }
        );
      },
      [&](IndexByTempComponent ot) {
        return make_irvalue([=](context ctx) -> llvm::Expected<pvalue> {
          pvalue reg = ctx.resource.temp.ptr_int4;
          if (ot.phase != ~0u) {
            reg = ctx.resource.phases[ot.phase].temp.ptr_int4;
          }
          auto temp =
            load_from_array_at(reg, ctx.builder.getInt32(ot.regid)).build(ctx);
          if (auto err = temp.takeError()) {
            return std::move(err);
          }
          return ctx.builder.CreateAdd(
            ctx.builder.CreateExtractElement(temp.get(), ot.component),
            ctx.builder.getInt32(ot.offset)
          );
        });
      },
      [&](IndexByIndexableTempComponent it) {
        return throwUnsupported<pvalue>(
          "TODO: IndexByIndexableTempComponent not implemented yet"
        );
      }
    },
    idx
  );
}

template <typename Operand, bool ReadFloat> IRValue load_src(Operand) {
  std::string s;
  llvm::raw_string_ostream o(s);
  o << "register load of " << Operand::debug_name << "<"
    << (ReadFloat ? "float" : "int") << "> is not implemented\n";
  return throwUnsupported<pvalue>(s);
};

template <bool ReadFloat>
IRValue load_src_op(SrcOperand src, uint32_t mask = 0b1111) {
  return std::visit(
           [](auto src) { return load_src<decltype(src), ReadFloat>(src); }, src
         ) >>= [mask, src](auto vec) {
    auto modifier = get_modifier(src);
    if (ReadFloat) {
      return apply_float_src_operand_modifier(modifier, vec, mask);
    } else {
      return apply_integer_src_operand_modifier(modifier, vec, mask);
    }
  };
};

template <>
IRValue load_src<SrcOperandConstantBuffer, false>(SrcOperandConstantBuffer cb) {
  auto ctx = co_yield get_context();
  auto cb_handle = co_yield ctx.resource.cb_range_map[cb.rangeid](nullptr);
  assert(cb_handle);
  auto ptr = ctx.builder.CreateGEP(
    ctx.types._int4, cb_handle, {co_yield load_operand_index(cb.regindex)}
  );
  co_return ctx.builder.CreateLoad(ctx.types._int4, ptr);
};

template <>
IRValue load_src<SrcOperandConstantBuffer, true>(SrcOperandConstantBuffer cb) {
  auto ctx = co_yield get_context();
  auto cb_handle = co_yield ctx.resource.cb_range_map[cb.rangeid](nullptr);
  assert(cb_handle);
  auto ptr = ctx.builder.CreateGEP(
    ctx.types._int4, cb_handle, {co_yield load_operand_index(cb.regindex)}
  );
  auto vec = ctx.builder.CreateBitCast(
    ctx.builder.CreateLoad(ctx.types._int4, ptr), ctx.types._float4
  );
  co_return vec;
};

template <>
IRValue load_src<SrcOperandImmediateConstantBuffer, false>(
  SrcOperandImmediateConstantBuffer cb
) {
  auto ctx = co_yield get_context();
  auto vec = co_yield load_from_array_at(
    ctx.resource.icb, co_yield load_operand_index(cb.regindex)
  );
  co_return vec;
};

template <>
IRValue load_src<SrcOperandImmediateConstantBuffer, true>(
  SrcOperandImmediateConstantBuffer cb
) {
  auto ctx = co_yield get_context();
  auto vec = co_yield load_from_array_at(
    ctx.resource.icb_float, co_yield load_operand_index(cb.regindex)
  );
  co_return vec;
};

template <>
IRValue load_src<SrcOperandImmediate32, false>(SrcOperandImmediate32 imm) {
  return make_irvalue([=](context ctx) {
    return llvm::ConstantDataVector::get(ctx.llvm, imm.uvalue);
  });
};

template <>
IRValue load_src<SrcOperandImmediate32, true>(SrcOperandImmediate32 imm) {
  return make_irvalue([=](context ctx) {
    return llvm::ConstantDataVector::get(ctx.llvm, imm.fvalue);
  });
};

template <>
IRValue load_src<SrcOperandInputOCP, true>(SrcOperandInputOCP input_ocp) {

  auto ctx = co_yield get_context();
  co_return co_yield load_from_array_at(
    ctx.resource.output.ptr_float4,
    ctx.builder.CreateAdd(
      ctx.builder.CreateMul(
        co_yield load_operand_index(input_ocp.cpid),
        ctx.builder.getInt32(ctx.resource.output_element_count)
      ),
      ctx.builder.getInt32(input_ocp.regid)
    )
  );
};

template <>
IRValue load_src<SrcOperandInputOCP, false>(SrcOperandInputOCP input_ocp) {

  auto ctx = co_yield get_context();
  co_return co_yield load_from_array_at(
    ctx.resource.output.ptr_int4,
    ctx.builder.CreateAdd(
      ctx.builder.CreateMul(
        co_yield load_operand_index(input_ocp.cpid),
        ctx.builder.getInt32(ctx.resource.output_element_count)
      ),
      ctx.builder.getInt32(input_ocp.regid)
    )
  );
};

template <>
IRValue load_src<SrcOperandInputICP, true>(SrcOperandInputICP input2d) {
  auto ctx = co_yield get_context();
  /* applies to both hull and domain shader */
  co_return co_yield load_from_array_at(
    ctx.resource.input.ptr_float4,
    ctx.builder.CreateAdd(
      ctx.builder.CreateMul(
        co_yield load_operand_index(input2d.cpid),
        ctx.builder.getInt32(ctx.resource.input_element_count)
      ),
      ctx.builder.getInt32(input2d.regid)
    )
  );
};

template <>
IRValue load_src<SrcOperandInputICP, false>(SrcOperandInputICP input2d) {
  auto ctx = co_yield get_context();
  /* applies to both hull and domain shader */
  co_return co_yield load_from_array_at(
    ctx.resource.input.ptr_int4,
    ctx.builder.CreateAdd(
      ctx.builder.CreateMul(
        co_yield load_operand_index(input2d.cpid),
        ctx.builder.getInt32(ctx.resource.input_element_count)
      ),
      ctx.builder.getInt32(input2d.regid)
    )
  );
};

template <>
IRValue load_src<SrcOperandInputPC, true>(SrcOperandInputPC input_patch_constant
) {
  auto ctx = co_yield get_context();
  co_return co_yield load_from_array_at(
    ctx.resource.patch_constant_output.ptr_float4,
    co_yield load_operand_index(input_patch_constant.regindex)
  );
};

template<>
IRValue load_src<SrcOperandInputPC, false>(SrcOperandInputPC input_patch_constant
) {
  auto ctx = co_yield get_context();
  co_return co_yield load_from_array_at(
    ctx.resource.patch_constant_output.ptr_int4,
    co_yield load_operand_index(input_patch_constant.regindex)
  );
};

template <> IRValue load_src<SrcOperandTemp, true>(SrcOperandTemp temp) {
  auto ctx = co_yield get_context();
  if (temp.phase != ~0u) {
    assert(temp.phase < ctx.resource.phases.size());
    co_return co_yield load_from_array_at(
      ctx.resource.phases[temp.phase].temp.ptr_float4,
      ctx.builder.getInt32(temp.regid)
    );
  }
  co_return co_yield load_from_array_at(
    ctx.resource.temp.ptr_float4, ctx.builder.getInt32(temp.regid)
  );
};

template <> IRValue load_src<SrcOperandTemp, false>(SrcOperandTemp temp) {
  auto ctx = co_yield get_context();  
  if (temp.phase != ~0u) {
    assert(temp.phase < ctx.resource.phases.size());
    co_return co_yield load_from_array_at(
      ctx.resource.phases[temp.phase].temp.ptr_int4,
      ctx.builder.getInt32(temp.regid)
    );
  }
  co_return co_yield load_from_array_at(
    ctx.resource.temp.ptr_int4, ctx.builder.getInt32(temp.regid)
  );
};

template <>
IRValue load_src<SrcOperandIndexableTemp, true>(SrcOperandIndexableTemp itemp) {
  auto ctx = co_yield get_context();
  indexable_register_file regfile;
  if (itemp.phase != ~0u) {
    assert(itemp.phase < ctx.resource.phases.size());
    regfile =
      ctx.resource.phases[itemp.phase].indexable_temp_map[itemp.regfile];
  } else {
    regfile = ctx.resource.indexable_temp_map[itemp.regfile];
  }
  auto s = co_yield load_from_array_at(
    regfile.ptr_float_vec, co_yield load_operand_index(itemp.regindex)
  );
  co_return co_yield extend_to_vec4(s);
};

template <>
IRValue load_src<SrcOperandIndexableTemp, false>(SrcOperandIndexableTemp itemp
) {
  auto ctx = co_yield get_context();
  indexable_register_file regfile;
  if (itemp.phase != ~0u) {
    assert(itemp.phase < ctx.resource.phases.size());
    regfile =
      ctx.resource.phases[itemp.phase].indexable_temp_map[itemp.regfile];
  } else {
    regfile = ctx.resource.indexable_temp_map[itemp.regfile];
  }
  auto s = co_yield load_from_array_at(
    regfile.ptr_int_vec, co_yield load_operand_index(itemp.regindex)
  );
  co_return co_yield extend_to_vec4(s);
};

template <> IRValue load_src<SrcOperandInput, true>(SrcOperandInput input) {
  auto ctx = co_yield get_context();
  auto s = co_yield load_from_array_at(
    ctx.resource.input.ptr_float4, ctx.builder.getInt32(input.regid)
  );
  co_return s;
};

template <> IRValue load_src<SrcOperandInput, false>(SrcOperandInput input) {
  auto ctx = co_yield get_context();
  auto s = co_yield load_from_array_at(
    ctx.resource.input.ptr_int4, ctx.builder.getInt32(input.regid)
  );
  co_return s;
};

template <>
IRValue load_src<SrcOperandIndexableInput, true>(SrcOperandIndexableInput input
) {
  auto ctx = co_yield get_context();
  auto s = co_yield load_from_array_at(
    ctx.resource.input.ptr_float4, co_yield load_operand_index(input.regindex)
  );
  co_return co_yield extend_to_vec4(s);
};

template <>
IRValue load_src<SrcOperandIndexableInput, false>(SrcOperandIndexableInput input
) {
  auto ctx = co_yield get_context();
  auto s = co_yield load_from_array_at(
    ctx.resource.input.ptr_int4, co_yield load_operand_index(input.regindex)
  );
  co_return co_yield extend_to_vec4(s);
};

template <>
IRValue load_src<SrcOperandAttribute, false>(SrcOperandAttribute attr) {
  auto ctx = co_yield get_context();
  pvalue vec = nullptr;
  switch (attr.attribute) {
  case shader::common::InputAttribute::VertexId:
  case shader::common::InputAttribute::InstanceId:
    assert(0 && "never reached: should be handled separately");
    break;
  case shader::common::InputAttribute::ThreadId: {
    assert(ctx.resource.thread_id_arg);
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_arg);
    break;
  }
  case shader::common::InputAttribute::ThreadIdInGroup: {
    assert(ctx.resource.thread_id_in_group_arg);
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_in_group_arg);
    break;
  }
  case shader::common::InputAttribute::ThreadGroupId: {
    assert(ctx.resource.thread_group_id_arg);
    vec = co_yield extend_to_vec4(ctx.resource.thread_group_id_arg);
    break;
  }
  case shader::common::InputAttribute::ThreadIdInGroupFlatten: {
    assert(ctx.resource.thread_id_in_group_flat_arg);
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_in_group_flat_arg);
    break;
  }
  
  case shader::common::InputAttribute::OutputControlPointId:
  case shader::common::InputAttribute::ForkInstanceId:
  case shader::common::InputAttribute::JoinInstanceId: {
    assert(ctx.resource.thread_id_in_patch);
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_in_patch);
    break;
  }
  case shader::common::InputAttribute::CoverageMask: {
    assert(ctx.resource.coverage_mask_arg);
    vec = co_yield extend_to_vec4(
      ctx.pso_sample_mask != 0xffffffff
        ? ctx.builder.CreateAnd(
            ctx.resource.coverage_mask_arg, ctx.pso_sample_mask
          )
        : ctx.resource.coverage_mask_arg
    );
    break;
  }
  case shader::common::InputAttribute::Domain: {
    vec = co_yield extend_to_vec4(ctx.resource.domain) >>= bitcast_int4;
    break;
  }
  case shader::common::InputAttribute::PrimitiveId: {
    assert(ctx.resource.patch_id);
    vec = co_yield extend_to_vec4(ctx.resource.patch_id);
    break;
  }
  case shader::common::InputAttribute::GSInstanceId: {
    assert(ctx.resource.gs_instance_id);
    vec = co_yield extend_to_vec4(ctx.resource.gs_instance_id);
    break;
  }
  }
  co_return vec;
};

template <>
IRValue load_src<SrcOperandAttribute, true>(SrcOperandAttribute attr) {
  auto ctx = co_yield get_context();
  pvalue vec = nullptr;
  switch (attr.attribute) {
  case shader::common::InputAttribute::VertexId:
  case shader::common::InputAttribute::InstanceId:
    assert(0 && "never reached: should be handled separately");
    break;
  case shader::common::InputAttribute::ThreadId: {
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_arg) >>=
      bitcast_float4;
    break;
  }
  case shader::common::InputAttribute::ThreadIdInGroup: {
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_in_group_arg) >>=
      bitcast_float4;
    break;
  }
  case shader::common::InputAttribute::ThreadGroupId: {
    vec = co_yield extend_to_vec4(ctx.resource.thread_group_id_arg) >>=
      bitcast_float4;
    break;
  }
  case shader::common::InputAttribute::ThreadIdInGroupFlatten: {
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_in_group_flat_arg) >>=
      bitcast_float4;
    break;
  }
  case shader::common::InputAttribute::OutputControlPointId:
  case shader::common::InputAttribute::ForkInstanceId:
  case shader::common::InputAttribute::JoinInstanceId: {
    vec = co_yield extend_to_vec4(ctx.resource.thread_id_in_patch) >>=
      bitcast_float4;
    break;
  }
  case shader::common::InputAttribute::CoverageMask: {
    assert(ctx.resource.coverage_mask_arg);
    vec = co_yield extend_to_vec4(
      ctx.pso_sample_mask != 0xffffffff
        ? ctx.builder.CreateAnd(
            ctx.resource.coverage_mask_arg, ctx.pso_sample_mask
          )
        : ctx.resource.coverage_mask_arg
    ) >>= bitcast_float4;
    break;
  }
  case shader::common::InputAttribute::Domain: {
    vec = co_yield extend_to_vec4(ctx.resource.domain);
    break;
  }
  case shader::common::InputAttribute::PrimitiveId: {
    assert(ctx.resource.patch_id);
    vec = co_yield extend_to_vec4(ctx.resource.patch_id) >>= bitcast_float4;
    break;
  }
  case shader::common::InputAttribute::GSInstanceId: {
    assert(ctx.resource.gs_instance_id);
    vec = co_yield extend_to_vec4(ctx.resource.gs_instance_id) >>= bitcast_float4;
    break;
  }
  }
  co_return vec;
};

template <typename Operand, bool StoreFloat>
IREffect store_dst(Operand, IRValue &&value) {
  llvm::outs() << "register store of " << Operand::debug_name << "<"
               << (StoreFloat ? "float" : "int") << "> is not implemented\n";
  assert(0 && "operation not implemented");
};

template <bool StoreFloat>
IREffect store_dst_op(DstOperand dst, IRValue &&value) {
  return std::visit(
    [value = std::move(value)](auto dst) mutable {
      return store_dst<decltype(dst), StoreFloat>(dst, std::move(value));
    },
    dst
  );
};

auto recover_mask(uint32_t mask) {
  return [=](pvalue value) -> IRValue {
    return make_irvalue([=](context ctx) -> pvalue {
      auto ty = value->getType();
      if (!ty->isVectorTy()) {
        switch (mask) {
        case 0b1000:
        case 0b0100:
        case 0b0010:
        case 0b0001:
        case 0b1111:
          return ctx.builder.CreateVectorSplat(4, value);
        default:
          assert(0 && "invalid mask");
        }
      }
      int p = llvm::UndefMaskElem;
      switch (mask) {
      case 0b1000:
        return ctx.builder.CreateShuffleVector(value, {p, p, p, 0});
      case 0b0100:
        return ctx.builder.CreateShuffleVector(value, {p, p, 0, p});
      case 0b0010:
        return ctx.builder.CreateShuffleVector(value, {p, 0, p, p});
      case 0b0001:
        return ctx.builder.CreateShuffleVector(value, {0, p, p, p});
      case 0b1100:
        return ctx.builder.CreateShuffleVector(value, {p, p, 0, 1});
      case 0b1010:
        return ctx.builder.CreateShuffleVector(value, {p, 0, p, 1});
      case 0b1001:
        return ctx.builder.CreateShuffleVector(value, {0, p, p, 1});
      case 0b0110:
        return ctx.builder.CreateShuffleVector(value, {p, 0, 1, p});
      case 0b0101:
        return ctx.builder.CreateShuffleVector(value, {0, p, 1, p});
      case 0b0011:
        return ctx.builder.CreateShuffleVector(value, {0, 1, p, p});
      case 0b1110:
        return ctx.builder.CreateShuffleVector(value, {p, 0, 1, 2});
      case 0b1011:
        return ctx.builder.CreateShuffleVector(value, {0, 1, p, 2});
      case 0b1101:
        return ctx.builder.CreateShuffleVector(value, {0, p, 1, 2});
      case 0b0111:
        return ctx.builder.CreateShuffleVector(value, {0, 1, 2, p});
      case 0b1111:
        return value;
      default:
        break;
      }
      assert(0 && "invalid mask");
      return nullptr;
    });
  };
}

template <bool StoreFloat>
IREffect store_dst_op_masked(DstOperand dst, IRValue &&value) {
  auto mask = get_dst_mask(dst);
  return std::visit(
    [value = std::move(value), mask](auto dst) mutable {
      return store_dst<decltype(dst), StoreFloat>(
        dst, std::move(value) >>= recover_mask(mask)
      );
    },
    dst
  );
};

template <>
IREffect store_dst<DstOperandNull, false>(DstOperandNull, IRValue &&) {
  co_return {};
};

template <>
IREffect store_dst<DstOperandNull, true>(DstOperandNull, IRValue &&) {
  co_return {};
};

template <>
IREffect
store_dst<DstOperandSideEffect, false>(DstOperandSideEffect, IRValue &&value) {
  return make_effect_bind(
    [value = std::move(value)](auto ctx) mutable -> IREffect {
      co_yield std::move(value);
      co_return {};
    }
  );
};

template <>
IREffect
store_dst<DstOperandSideEffect, true>(DstOperandSideEffect, IRValue &&value) {
  return make_effect_bind(
    [value = std::move(value)](auto ctx) mutable -> IREffect {
      co_yield std::move(value);
      co_return {};
    }
  );
};

template <>
IREffect
store_dst<DstOperandTemp, false>(DstOperandTemp temp, IRValue &&value) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value), temp](auto ctx) mutable -> IREffect {
      if (temp.phase != ~0u) {
        assert(temp.phase < ctx.resource.phases.size());
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.phases[temp.phase].temp.ptr_int4,
          co_yield get_int(temp.regid), co_yield std::move(value), temp._.mask
        );
      }
      co_return co_yield store_at_vec4_array_masked(
        ctx.resource.temp.ptr_int4, co_yield get_int(temp.regid),
        co_yield std::move(value), temp._.mask
      );
    }
  );
};

template <>
IREffect store_dst<DstOperandTemp, true>(DstOperandTemp temp, IRValue &&value) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value), temp](auto ctx) mutable -> IREffect {
      if (temp.phase != ~0u) {
        assert(temp.phase < ctx.resource.phases.size());
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.phases[temp.phase].temp.ptr_float4,
          co_yield get_int(temp.regid), co_yield std::move(value), temp._.mask
        );
      }
      co_return co_yield store_at_vec4_array_masked(
        ctx.resource.temp.ptr_float4, co_yield get_int(temp.regid),
        co_yield std::move(value), temp._.mask
      );
    }
  );
};

template <>
IREffect store_dst<DstOperandIndexableTemp, false>(
  DstOperandIndexableTemp itemp, IRValue &&value
) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value), itemp](auto ctx) mutable -> IREffect {
      indexable_register_file regfile;
      if (itemp.phase != ~0u) {
        assert(itemp.phase < ctx.resource.phases.size());
        regfile =
          ctx.resource.phases[itemp.phase].indexable_temp_map[itemp.regfile];
      } else {
        regfile = ctx.resource.indexable_temp_map[itemp.regfile];
      }
      co_return co_yield store_at_vec_array_masked(
        regfile.ptr_int_vec, co_yield load_operand_index(itemp.regindex),
        co_yield std::move(value), itemp._.mask
      );
    }
  );
};

template <>
IREffect store_dst<DstOperandIndexableTemp, true>(
  DstOperandIndexableTemp itemp, IRValue &&value
) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value), itemp](auto ctx) mutable -> IREffect {
      indexable_register_file regfile;
      if (itemp.phase != ~0u) {
        assert(itemp.phase < ctx.resource.phases.size());
        regfile =
          ctx.resource.phases[itemp.phase].indexable_temp_map[itemp.regfile];
      } else {
        regfile = ctx.resource.indexable_temp_map[itemp.regfile];
      }
      co_return co_yield store_at_vec_array_masked(
        regfile.ptr_float_vec, co_yield load_operand_index(itemp.regindex),
        co_yield std::move(value), itemp._.mask
      );
    }
  );
};

template <>
IREffect
store_dst<DstOperandOutput, false>(DstOperandOutput output, IRValue &&value) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value), output](context ctx) mutable -> IREffect {
      if (output.phase != ~0u) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.patch_constant_output.ptr_int4,
          co_yield get_int(output.regid), co_yield std::move(value),
          output._.mask
        );
      }
      if (ctx.shader_type == microsoft::D3D11_SB_HULL_SHADER) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.output.ptr_int4,
          ctx.builder.CreateAdd(
            ctx.builder.CreateMul(
              ctx.resource.thread_id_in_patch,
              ctx.builder.getInt32(ctx.resource.output_element_count)
            ),
            ctx.builder.getInt32(output.regid)
          ),
          co_yield std::move(value), output._.mask
        );
      }
      co_return co_yield store_at_vec4_array_masked(
        ctx.resource.output.ptr_int4, co_yield get_int(output.regid),
        co_yield std::move(value), output._.mask
      );
    }
  );
};

template <>
IREffect
store_dst<DstOperandOutput, true>(DstOperandOutput output, IRValue &&value) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value), output](auto ctx) mutable -> IREffect {
      if (output.phase != ~0u) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.patch_constant_output.ptr_float4,
          co_yield get_int(output.regid), co_yield std::move(value),
          output._.mask
        );
      }
      if (ctx.shader_type == microsoft::D3D11_SB_HULL_SHADER) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.output.ptr_float4,
          ctx.builder.CreateAdd(
            ctx.builder.CreateMul(
              ctx.resource.thread_id_in_patch,
              ctx.builder.getInt32(ctx.resource.output_element_count)
            ),
            ctx.builder.getInt32(output.regid)
          ),
          co_yield std::move(value), output._.mask
        );
      }
      co_return co_yield store_at_vec4_array_masked(
        ctx.resource.output.ptr_float4, co_yield get_int(output.regid),
        co_yield std::move(value), output._.mask
      );
    }
  );
};

template <>
IREffect
store_dst<DstOperandOutputDepth, true>(DstOperandOutputDepth, IRValue &&value) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value)](context ctx) mutable -> IREffect {
      // FIXME: extend_to_vec4 is kinda silly
      pvalue depth = co_yield (std::move(value) >>= extend_to_vec4) >>=
        extract_element(0);
      auto ptr = ctx.builder.CreateConstGEP1_32(
        ctx.types._float, ctx.resource.depth_output_reg, 0
      );
      ctx.builder.CreateStore(depth, ptr);
      co_return {};
    }
  );
};

template <>
IREffect
store_dst<DstOperandOutputDepth, false>(DstOperandOutputDepth, IRValue &&value) {
  return make_effect_bind(
    [value = std::move(value)](context ctx) mutable -> IREffect {
      // FIXME: extend_to_vec4 is kinda silly
      pvalue depth = co_yield (std::move(value) >>= extend_to_vec4) >>=
        extract_element(0);
      auto ptr = ctx.builder.CreateConstGEP1_32(
        ctx.types._float, ctx.resource.depth_output_reg, 0
      );
      ctx.builder.CreateStore(ctx.builder.CreateBitCast(depth, ctx.types._float), ptr);
      co_return {};
    }
  );
};

template <>
IREffect store_dst<DstOperandOutputCoverageMask, false>(
  DstOperandOutputCoverageMask, IRValue &&value
) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value)](context ctx) mutable -> IREffect {
      // FIXME: extend_to_vec4 is kinda silly
      pvalue sample_mask = co_yield (std::move(value) >>= extend_to_vec4) >>=
        extract_element(0);
      auto ptr = ctx.builder.CreateConstGEP1_32(
        ctx.types._int, ctx.resource.coverage_mask_reg, 0
      );
      ctx.builder.CreateStore(sample_mask, ptr);
      co_return {};
    }
  );
};

template <>
IREffect store_dst<DstOperandOutputCoverageMask, true>(
  DstOperandOutputCoverageMask, IRValue &&value
) {
  // coroutine + rvalue reference = SHOOT YOURSELF IN THE FOOT
  return make_effect_bind(
    [value = std::move(value)](context ctx) mutable -> IREffect {
      // FIXME: extend_to_vec4 is kinda silly
      pvalue sample_mask = co_yield (std::move(value) >>= extend_to_vec4) >>=
        extract_element(0);
      auto ptr = ctx.builder.CreateConstGEP1_32(
        ctx.types._int, ctx.resource.coverage_mask_reg, 0
      );
      ctx.builder.CreateStore(
        ctx.builder.CreateBitCast(sample_mask, ctx.types._int), ptr
      );
      co_return {};
    }
  );
};

template <>
IREffect store_dst<
  DstOperandIndexableOutput, true>(DstOperandIndexableOutput output, IRValue && value) {
  return make_effect_bind(
    [value = std::move(value), output](context ctx) mutable -> IREffect {
      auto index = co_yield load_operand_index(output.regindex);
      if (output.phase != ~0u) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.patch_constant_output.ptr_float4, index,
          co_yield std::move(value), output._.mask
        );
      }
      if (ctx.shader_type == microsoft::D3D11_SB_HULL_SHADER) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.output.ptr_float4,
          ctx.builder.CreateAdd(
            ctx.builder.CreateMul(
              ctx.resource.thread_id_in_patch,
              ctx.builder.getInt32(ctx.resource.output_element_count)
            ),
            index
          ),
          co_yield std::move(value), output._.mask
        );
      }
      co_return co_yield store_at_vec4_array_masked(
        ctx.resource.output.ptr_float4, index, co_yield std::move(value),
        output._.mask
      );
    }
  );
};

template <>
IREffect store_dst<
  DstOperandIndexableOutput, false>(DstOperandIndexableOutput output, IRValue && value) {
  return make_effect_bind(
    [value = std::move(value), output](context ctx) mutable -> IREffect {
      auto index = co_yield load_operand_index(output.regindex);
      if (output.phase != ~0u) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.patch_constant_output.ptr_int4, index,
          co_yield std::move(value), output._.mask
        );
      }
      if (ctx.shader_type == microsoft::D3D11_SB_HULL_SHADER) {
        co_return co_yield store_at_vec4_array_masked(
          ctx.resource.output.ptr_int4,
          ctx.builder.CreateAdd(
            ctx.builder.CreateMul(
              ctx.resource.thread_id_in_patch,
              ctx.builder.getInt32(ctx.resource.output_element_count)
            ),
            index
          ),
          co_yield std::move(value), output._.mask
        );
      }
      co_return co_yield store_at_vec4_array_masked(
        ctx.resource.output.ptr_int4, index, co_yield std::move(value),
        output._.mask
      );
    }
  );
};

auto load_condition(SrcOperand src, bool non_zero_test) {
  return load_src_op<false>(src) >>= [=](pvalue condition_src) {
    return make_irvalue([=](context ctx) {
      auto element =
        ctx.builder.CreateExtractElement(condition_src, (uint64_t)0);
      if (non_zero_test) {
        return ctx.builder.CreateICmpNE(element, ctx.builder.getInt32(0));
      } else {
        return ctx.builder.CreateICmpEQ(element, ctx.builder.getInt32(0));
      }
    });
  };
};

auto metadata_get_sampler_bias(pvalue metadata) -> IRValue {
  auto ctx = co_yield get_context();
  co_return ctx.builder.CreateBitCast(
    ctx.builder.CreateTrunc(metadata, ctx.types._int), ctx.types._float
  );
};

auto metadata_get_min_lod_clamp(pvalue metadata) -> IRValue {
  // unintentional but they have the same representation
  return metadata_get_sampler_bias(metadata);
};

auto metadata_get_texture_buffer_offset(pvalue metadata) -> IRValue {
  auto ctx = co_yield get_context();
  co_return ctx.builder.CreateTrunc(metadata, ctx.types._int);
};

auto metadata_get_texture_buffer_length(pvalue metadata) -> IRValue {
  auto ctx = co_yield get_context();
  co_return ctx.builder.CreateTrunc(
    ctx.builder.CreateLShr(metadata, 32uLL), ctx.types._int
  );
};

auto metadata_get_raw_buffer_length(pvalue metadata) -> IRValue {
  // unintentional but they have the same representation
  return metadata_get_texture_buffer_offset(metadata);
};

/**
it might be a constant, device or threadgroup buffer of `uint*`
*/
auto load_from_uint_bufptr(
  pvalue uint_buf_ptr, uint32_t read_components, pvalue oob = nullptr
) -> IRValue {
  auto ctx = co_yield get_context();
  pvalue vec = llvm::UndefValue::get(ctx.types._int4);
  for (uint32_t i = 0; i < read_components; i++) {
    auto ptr = ctx.builder.CreateGEP(
      ctx.types._int, uint_buf_ptr, {ctx.builder.getInt32(i)}
    );
    bool is_volatile =
      cast<llvm::PointerType>(ptr->getType())->getAddressSpace() != 2;
    vec = ctx.builder.CreateInsertElement(
      vec,
      oob ? ctx.builder.CreateSelect(
              oob, ctx.builder.getInt32(0),
              ctx.builder.CreateLoad(ctx.types._int, ptr, is_volatile)
            )
          : ctx.builder.CreateLoad(ctx.types._int, ptr, is_volatile),
      i
    );
  }
  co_return vec;
};

auto store_to_uint_bufptr(
  pvalue uint_buf_ptr, uint32_t written_components, pvalue ivec4_to_write
) -> IREffect {
  auto ctx = co_yield get_context();
  for (uint32_t i = 0; i < written_components; i++) {
    auto ptr = ctx.builder.CreateGEP(
      ctx.types._int, uint_buf_ptr, {ctx.builder.getInt32(i)}
    );
    ctx.builder.CreateStore(
      ctx.builder.CreateExtractElement(ivec4_to_write, i), ptr,
      cast<llvm::PointerType>(ptr->getType())->getAddressSpace() != 2
    );
  }
  co_return {};
};

auto mask_to_linear_components_num(uint32_t mask) {
  switch (mask) {
  case 0b1:
    return 1;
  case 0b11:
    return 2;
  case 0b111:
    return 3;
  case 0b1111:
    return 4;
  default:
    llvm::outs() << mask << '\n';
    assert(0 && "invalid write mask");
    return 0;
  }
}

auto store_tgsm(
  llvm::GlobalValue *g, pvalue offset_in_4bytes, uint32_t written_components,
  pvalue ivec4_to_write
) -> IREffect {
  auto ctx = co_yield get_context();
  for (uint32_t i = 0; i < written_components; i++) {
    auto ptr = ctx.builder.CreateInBoundsGEP(
      llvm::cast<llvm::PointerType>(g->getType())
        ->getNonOpaquePointerElementType(),
      g,
      {ctx.builder.getInt32(0),
       ctx.builder.CreateAdd(offset_in_4bytes, ctx.builder.getInt32(i))}
    );
    ctx.builder.CreateStore(co_yield extract_element(i)(ivec4_to_write), ptr);
  }
  co_return {};
};

auto read_uint_buf_addr(AtomicOperandTGSM tgsm, pvalue index = 0) -> IRValue {
  auto ctx = co_yield get_context();
  assert(ctx.resource.tgsm_map.contains(tgsm.id));
  auto gv = ctx.resource.tgsm_map[tgsm.id].second;
  llvm::Constant *Zero =
    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx.llvm), 0);
  if (index) {
    co_return ctx.builder.CreateGEP(gv->getValueType(), gv, {Zero, index});
  }
  co_return llvm::ConstantExpr::getInBoundsGetElementPtr(
    gv->getValueType(), gv, (llvm::ArrayRef<llvm::Constant *>){Zero, Zero}
  );
}

// the same as above
auto read_uint_buf_addr(SrcOperandTGSM tgsm, pvalue index = 0) -> IRValue {
  auto ctx = co_yield get_context();
  assert(ctx.resource.tgsm_map.contains(tgsm.id));
  auto gv = ctx.resource.tgsm_map[tgsm.id].second;
  llvm::Constant *Zero =
    llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx.llvm), 0);
  if (index) {
    co_return ctx.builder.CreateGEP(gv->getValueType(), gv, {Zero, index});
  }
  co_return llvm::ConstantExpr::getInBoundsGetElementPtr(
    gv->getValueType(), gv, (llvm::ArrayRef<llvm::Constant *>){Zero, Zero}
  );
}

auto read_uint_buf_addr(
  AtomicDstOperandUAV uav, pvalue index = 0, pvalue *oob = nullptr
) -> IRValue {
  auto ctx = co_yield get_context();
  assert(ctx.resource.uav_buf_range_map.contains(uav.range_id));
  auto &[_, handle, metadata] = ctx.resource.uav_buf_range_map[uav.range_id];
  if (index) {
    auto bound =
      co_yield metadata_get_raw_buffer_length(co_yield metadata(nullptr));
    auto _bound = ctx.builder.CreateLShr(bound, ctx.builder.getInt32(2));

    auto is_oob = ctx.builder.CreateICmpUGE(index, _bound);
    if (oob) {
      *oob = is_oob;
    };
    auto final_index = ctx.builder.CreateSelect(is_oob, _bound, index);
    co_return ctx.builder.CreateGEP(
      ctx.types._int, co_yield handle(nullptr), {final_index}
    );
  }
  // can't be even oob at 0?
  co_return co_yield handle(nullptr);
}

// the same as above
auto read_uint_buf_addr(
  SrcOperandUAV uav, pvalue index = 0, pvalue *oob = nullptr
) -> IRValue {
  auto ctx = co_yield get_context();
  assert(ctx.resource.uav_buf_range_map.contains(uav.range_id));
  auto &[_, handle, metadata] = ctx.resource.uav_buf_range_map[uav.range_id];
  if (index) {
    auto bound =
      co_yield metadata_get_raw_buffer_length(co_yield metadata(nullptr));
    auto _bound = ctx.builder.CreateLShr(bound, ctx.builder.getInt32(2));

    auto is_oob = ctx.builder.CreateICmpUGE(index, _bound);
    if (oob) {
      *oob = is_oob;
    };
    auto final_index =
      ctx.builder.CreateSelect(is_oob, ctx.builder.getInt32(0), index);
    co_return ctx.builder.CreateGEP(
      ctx.types._int, co_yield handle(nullptr), {final_index}
    );
  }
  // can't be even oob at 0?
  co_return co_yield handle(nullptr);
}

auto read_uint_buf_addr(
  SrcOperandResource srv, pvalue index = 0, pvalue *oob = nullptr
) -> IRValue {
  auto ctx = co_yield get_context();
  assert(ctx.resource.srv_buf_range_map.contains(srv.range_id));
  auto &[_, handle, metadata] = ctx.resource.srv_buf_range_map[srv.range_id];
  if (index) {
    auto bound =
      co_yield metadata_get_raw_buffer_length(co_yield metadata(nullptr));
    auto _bound = ctx.builder.CreateLShr(bound, ctx.builder.getInt32(2));

    auto is_oob = ctx.builder.CreateICmpUGE(index, _bound);
    if (oob) {
      *oob = is_oob;
    };
    auto final_index =
      ctx.builder.CreateSelect(is_oob, ctx.builder.getInt32(0), index);
    co_return ctx.builder.CreateGEP(
      ctx.types._int, co_yield handle(nullptr), {final_index}
    );
  }
  co_return co_yield handle(nullptr);
}

auto read_atomic_index(AtomicOperandTGSM tgsm, pvalue vec4) -> IRValue {
  auto ctx = co_yield get_context();
  assert(ctx.resource.tgsm_map.contains(tgsm.id));
  auto stride = ctx.resource.tgsm_map[tgsm.id].first;
  auto &builder = ctx.builder;
  co_return builder.CreateLShr(
    stride > 0 ? builder.CreateAdd(
                   builder.CreateMul(
                     builder.getInt32(stride), co_yield extract_element(0)(vec4)
                   ),
                   co_yield extract_element(1)(vec4)
                 )
               : co_yield extract_element(0)(vec4),
    2
  );
};

auto read_atomic_index(AtomicDstOperandUAV uav, pvalue addr_vec4) -> IRValue {
  auto ctx = co_yield get_context();
  assert(ctx.resource.uav_buf_range_map.contains(uav.range_id));
  auto &[stride, _, __] = ctx.resource.uav_buf_range_map[uav.range_id];
  auto &builder = ctx.builder;
  co_return builder.CreateLShr(
    stride > 0
      ? builder.CreateAdd(
          builder.CreateMul(
            builder.getInt32(stride), co_yield extract_element(0)(addr_vec4)
          ),
          co_yield extract_element(1)(addr_vec4)
        )
      : co_yield extract_element(0)(addr_vec4),
    2
  );
};

auto calc_uint_ptr_index(
  uint32_t stride, SrcOperand structure_idx, SrcOperand byte_offset
) -> IRValue {
  auto ctx = co_yield get_context();
  auto &builder = ctx.builder;
  co_return builder.CreateLShr(
    builder.CreateAdd(
      builder.CreateMul(
        builder.getInt32(stride),
        co_yield load_src_op<false>(structure_idx) >>= extract_element(0)
      ),
      co_yield load_src_op<false>(byte_offset) >>= extract_element(0)
    ),
    2
  );
};

auto calc_uint_ptr_index(uint32_t zero, uint32_t unused, SrcOperand byte_offset)
  -> IRValue {
  assert(zero == 0);
  auto ctx = co_yield get_context();
  auto &builder = ctx.builder;
  co_return builder.CreateLShr(
    co_yield load_src_op<false>(byte_offset) >>= extract_element(0), 2
  );
};

llvm::Expected<llvm::BasicBlock *> convert_basicblocks(
  std::shared_ptr<BasicBlock> entry, context &ctx, llvm::BasicBlock *return_bb
) {
  auto &context = ctx.llvm;
  auto &builder = ctx.builder;
  auto function = ctx.function;
  std::unordered_map<BasicBlock *, llvm::BasicBlock *> visited;
  std::vector<std::pair<BasicBlock *, llvm::BasicBlock *>> visit_order;

  std::stack<BasicBlock *> block_to_visit;
  block_to_visit.push(entry.get());

  while (!block_to_visit.empty()) {
    auto current = block_to_visit.top();
    block_to_visit.pop();
    if (visited.contains(current)) continue;
    auto inserted = visited.insert({current, llvm::BasicBlock::Create(context, current->debug_name, function)});
    visit_order.push_back(*inserted.first);
    std::visit(
      patterns{
        [](BasicBlockUndefined) {
          return;
        },
        [&](BasicBlockReturn ret) {
          return;
        },
        [&](BasicBlockUnconditionalBranch uncond) {
          block_to_visit.push(uncond.target.get());
        },
        [&](BasicBlockConditionalBranch cond) {
          block_to_visit.push(cond.true_branch.get());
          block_to_visit.push(cond.false_branch.get());
        },
        [&](BasicBlockSwitch swc)  {
          block_to_visit.push(swc.case_default.get());
          for (auto &[val, case_bb] : swc.cases) {
            block_to_visit.push(case_bb.get());
          }
        },
        [&](BasicBlockInstanceBarrier instance)  {
          block_to_visit.push(instance.active.get());
          block_to_visit.push(instance.sync.get());
        },
        [&](BasicBlockHullShaderWriteOutput hull_end)  {
          block_to_visit.push(hull_end.epilogue.get());
        },
      },
      current->target
    );
  }

  auto bb_pop = builder.GetInsertBlock();
  for (auto &[current, bb] : visit_order) {
    IREffect effect([](auto) { return std::monostate(); });
    for (auto &inst : current->instructions) {
      std::visit(
        patterns{
          [&effect](InstMov mov) {
            auto mask = get_dst_mask(mov.dst);
            effect << store_dst_op_masked<true>(
              mov.dst,
              load_src_op<true>(mov.src, mask) >>= saturate(mov._.saturate)
            );
          },
          [&effect](InstMovConditional movc) {
            auto mask = get_dst_mask(movc.dst);
            effect << store_dst_op_masked<true>(
              movc.dst,
              make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto src0 = co_yield load_src_op<true>(movc.src0, mask);
                auto src1 = co_yield load_src_op<true>(movc.src1, mask);
                auto cond = co_yield load_src_op<false>(movc.src_cond, mask);
                co_return ctx.builder.CreateSelect(
                  ctx.builder.CreateICmpNE(
                    cond, llvm::Constant::getNullValue(cond->getType())
                  ),
                  src0, src1
                );
              }) >>= saturate(movc._.saturate)
            );
          },
          [&effect](InstSwapConditional swapc) {
            effect << make_effect_bind([=](struct context ctx) -> IREffect {
              auto src0 = co_yield load_src_op<true>(swapc.src0);
              auto src1 = co_yield load_src_op<true>(swapc.src1);
              auto cond = co_yield load_src_op<false>(swapc.src_cond);
              auto mask_dst0 = get_dst_mask(swapc.dst0);
              co_yield store_dst_op_masked<true>(
                swapc.dst0, air::pure(ctx.builder.CreateSelect(
                              ctx.builder.CreateICmpNE(
                                co_yield get_valid_components(cond, mask_dst0),
                                co_yield get_splat_constant(0, mask_dst0)
                              ),
                              co_yield get_valid_components(src1, mask_dst0),
                              co_yield get_valid_components(src0, mask_dst0)
                            ))
              );
              auto mask_dst1 = get_dst_mask(swapc.dst1);
              co_yield store_dst_op_masked<true>(
                swapc.dst1, air::pure(ctx.builder.CreateSelect(
                              ctx.builder.CreateICmpNE(
                                co_yield get_valid_components(cond, mask_dst1),
                                co_yield get_splat_constant(0, mask_dst1)
                              ),
                              co_yield get_valid_components(src0, mask_dst1),
                              co_yield get_valid_components(src1, mask_dst1)
                            ))
              );
              co_return {};
            });
          },
          [&effect](InstSample sample) {
            effect << store_dst_op<true>(
              sample.dst,
              (make_irvalue_bind([=](struct context ctx) -> IRValue {
                 assert(ctx.resource.srv_range_map.contains(
                   sample.src_resource.range_id
                 ));
                 auto offset_const = llvm::ConstantVector::get(
                   {llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[0], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[1], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[2], true}
                    )}
                 );
                 auto &[res, res_handle_fn, res_metadata] =
                   ctx.resource.srv_range_map[sample.src_resource.range_id];
                 auto &[sampler_handle_fn, sampler_metadata] =
                   ctx.resource.sampler_range_map[sample.src_sampler.range_id];
                 auto res_h = co_yield res_handle_fn(nullptr);
                 auto sampler_h = co_yield sampler_handle_fn(nullptr);
                 auto res_min_lod_clamp = co_yield metadata_get_min_lod_clamp(
                   co_yield res_metadata(nullptr)
                 );
                 auto sampler_bias = co_yield metadata_get_sampler_bias(
                   co_yield sampler_metadata(nullptr)
                 );
                 auto coord = co_yield load_src_op<true>(sample.src_address);
                 switch (res.resource_kind_logical) {
                 case air::TextureKind::texture_1d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h, co_yield extract_element(0)(coord) >>= extend_to_float2, nullptr,
                     co_yield extract_element(0)(offset_const) >>= extend_to_int2
                   );
                 }
                 case air::TextureKind::texture_1d_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield extract_element(0)(coord) >>= extend_to_float2,      // .r
                     co_yield extract_element(1)(coord) >>= implicit_float_to_int, // .g
                     co_yield extract_element(0)(offset_const) >>= extend_to_int2  // 1d offset
                   );
                 }
                 case air::TextureKind::depth_2d:
                 case air::TextureKind::texture_2d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     nullptr,
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     sampler_bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::depth_2d_array:
                 case air::TextureKind::texture_2d_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     co_yield extract_element(2)(coord) >>=
                     implicit_float_to_int,                 // .b
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     sampler_bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::texture_3d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr,
                     co_yield truncate_vec(3)(offset_const), // 3d offset
                     sampler_bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::depth_cube:
                 case air::TextureKind::texture_cube: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr
                   );
                 }
                 case air::TextureKind::depth_cube_array:
                 case air::TextureKind::texture_cube_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     co_yield extract_element(3)(coord) >>=
                     implicit_float_to_int // .a
                   );
                 } break;
                 default: {
                   assert(0 && "invalid sample resource type");
                 }
                 }
               }) >>= extract_value(0)) >>=
              swizzle(sample.src_resource.read_swizzle)
            );
          },
          [&effect](InstSampleLOD sample) {
            effect << store_dst_op<true>(
              sample.dst,
              (make_irvalue_bind([=](struct context ctx) -> IRValue {
                 auto offset_const = llvm::ConstantVector::get(
                   {llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[0], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[1], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[2], true}
                    )}
                 );
                 auto &[res, res_handle_fn, _] =
                   ctx.resource.srv_range_map[sample.src_resource.range_id];
                 // Sampler states MIPLODBIAS and MAX/MINMIPLEVEL are honored.
                 auto &[sampler_handle_fn, sampler_metadata] =
                   ctx.resource.sampler_range_map[sample.src_sampler.range_id];
                 auto res_h = co_yield res_handle_fn(nullptr);
                 auto sampler_h = co_yield sampler_handle_fn(nullptr);
                 auto coord = co_yield load_src_op<true>(sample.src_address);
                 auto sampler_bias = co_yield metadata_get_sampler_bias(
                   co_yield sampler_metadata(nullptr)
                 );
                 auto LOD = ctx.builder.CreateFAdd(
                   co_yield load_src_op<true>(sample.src_lod) >>=
                   extract_element(0),
                   sampler_bias
                 );
                 switch (res.resource_kind_logical) {
                 case air::TextureKind::texture_1d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h, co_yield extract_element(0)(coord) >>= extend_to_float2, nullptr,
                     co_yield extract_element(0)(offset_const) >>= extend_to_int2, nullptr, nullptr, LOD
                   );
                 }
                 case air::TextureKind::texture_1d_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield extract_element(0)(coord) >>= extend_to_float2,      // .r
                     co_yield extract_element(1)(coord) >>= implicit_float_to_int, // .g
                     co_yield extract_element(0)(offset_const) >>= extend_to_int2, // 1d offset
                     nullptr, nullptr, LOD
                   );
                 }
                 case air::TextureKind::depth_2d:
                 case air::TextureKind::texture_2d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     nullptr,
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     nullptr, nullptr, LOD
                   );
                 }
                 case air::TextureKind::depth_2d_array:
                 case air::TextureKind::texture_2d_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     co_yield extract_element(2)(coord) >>=
                     implicit_float_to_int,                  // .b
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     nullptr, nullptr, LOD
                   );
                 }
                 case air::TextureKind::texture_3d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr,
                     co_yield truncate_vec(3)(offset_const), // 3d offset
                     nullptr, nullptr, LOD
                   );
                 }
                 case air::TextureKind::depth_cube:
                 case air::TextureKind::texture_cube: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr, nullptr, nullptr, nullptr,
                     LOD
                   );
                 }
                 case air::TextureKind::depth_cube_array:
                 case air::TextureKind::texture_cube_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     co_yield extract_element(3)(coord) >>=
                     implicit_float_to_int, // .a
                     nullptr, nullptr, nullptr, LOD
                   );
                 } break;
                 default: {
                   assert(0 && "invalid sample resource type");
                 }
                 }
               }) >>= extract_value(0)) >>=
              swizzle(sample.src_resource.read_swizzle)
            );
          },
          [&effect](InstSampleBias sample) {
            effect << store_dst_op<true>(
              sample.dst,
              (make_irvalue_bind([=](struct context ctx) -> IRValue {
                 auto offset_const = llvm::ConstantVector::get(
                   {llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[0], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[1], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[2], true}
                    )}
                 );
                 auto& [res, res_handle_fn, res_metadata] =
                   ctx.resource.srv_range_map[sample.src_resource.range_id];
                 auto& [sampler_handle_fn, sampler_metadata] =
                   ctx.resource.sampler_range_map[sample.src_sampler.range_id];
                 auto res_h = co_yield res_handle_fn(nullptr);
                 auto sampler_h = co_yield sampler_handle_fn(nullptr);
                 auto coord = co_yield load_src_op<true>(sample.src_address);
                 
                 auto sampler_bias = co_yield metadata_get_sampler_bias(
                   co_yield sampler_metadata(nullptr)
                 );
                 auto bias = ctx.builder.CreateFAdd(
                   co_yield load_src_op<true>(sample.src_bias) >>=
                   extract_element(0),
                   sampler_bias
                 );
                 auto res_min_lod_clamp = co_yield metadata_get_min_lod_clamp(
                   co_yield res_metadata(nullptr)
                 );
                 switch (res.resource_kind_logical) {
                 case air::TextureKind::texture_1d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h, co_yield extract_element(0)(coord) >>= extend_to_float2, nullptr,
                     co_yield extract_element(0)(offset_const) >>= extend_to_int2, bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::texture_1d_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield extract_element(0)(coord) >>= extend_to_float2,      // .r
                     co_yield extract_element(1)(coord) >>= implicit_float_to_int, // .g
                     co_yield extract_element(0)(offset_const) >>= extend_to_int2, // 1d offset
                     bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::depth_2d:
                 case air::TextureKind::texture_2d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     nullptr,
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::depth_2d_array:
                 case air::TextureKind::texture_2d_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     co_yield extract_element(2)(coord) >>=
                     implicit_float_to_int,                  // .b
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::texture_3d: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr,
                     co_yield truncate_vec(3)(offset_const), // 3d offset
                     bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::depth_cube:
                 case air::TextureKind::texture_cube: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr, nullptr, bias, res_min_lod_clamp
                   );
                 }
                 case air::TextureKind::depth_cube_array:
                 case air::TextureKind::texture_cube_array: {
                   co_return co_yield call_sample(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     co_yield extract_element(3)(coord) >>=
                     implicit_float_to_int, // .a
                     nullptr, bias, res_min_lod_clamp
                   );
                 } break;
                 default: {
                   assert(0 && "invalid sample resource type");
                 }
                 }
               }) >>= extract_value(0)) >>=
              swizzle(sample.src_resource.read_swizzle)
            );
          },
          [&effect](InstSampleDerivative sample) {
            effect << store_dst_op<true>(
              sample.dst,
              (make_irvalue_bind([=](struct context ctx) -> IRValue {
                 auto offset_const = llvm::ConstantVector::get(
                   {llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[0], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[1], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[2], true}
                    )}
                 );

                 // min_lod_clamp ignored
                 auto &[res, res_handle_fn, __] =
                   ctx.resource.srv_range_map[sample.src_resource.range_id];
                 // sampler bias ignored
                 auto &[sampler_handle_fn, _] =
                   ctx.resource.sampler_range_map[sample.src_sampler.range_id];
                 auto res_h = co_yield res_handle_fn(nullptr);
                 auto sampler_h = co_yield sampler_handle_fn(nullptr);
                 auto coord = co_yield load_src_op<true>(sample.src_address);
                 auto dpdx =
                   co_yield load_src_op<true>(sample.src_x_derivative);
                 auto dpdy =
                   co_yield load_src_op<true>(sample.src_y_derivative);
                 switch (res.resource_kind_logical) {
                 case air::TextureKind::depth_2d:
                 case air::TextureKind::texture_2d: {
                   co_return co_yield call_sample_grad(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     nullptr,                         // array_index
                     co_yield truncate_vec(2)(dpdx),
                     co_yield truncate_vec(2)(dpdy), nullptr,
                     co_yield truncate_vec(2)(offset_const) // 2d offset
                   );
                 }
                 case air::TextureKind::depth_2d_array:
                 case air::TextureKind::texture_2d_array: {
                   co_return co_yield call_sample_grad(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     co_yield extract_element(2)(coord) >>=
                     implicit_float_to_int, // array_index
                     co_yield truncate_vec(2)(dpdx),
                     co_yield truncate_vec(2)(dpdy), nullptr,
                     co_yield truncate_vec(2)(offset_const) // 2d offset
                   );
                 }
                 case air::TextureKind::texture_3d: {
                   co_return co_yield call_sample_grad(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr,                         // array_index
                     co_yield truncate_vec(3)(dpdx),
                     co_yield truncate_vec(3)(dpdy), nullptr,
                     co_yield truncate_vec(3)(offset_const) // 3d offset
                   );
                 }
                 case air::TextureKind::depth_cube:
                 case air::TextureKind::texture_cube: {
                   co_return co_yield call_sample_grad(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     nullptr,                         // array_index
                     co_yield truncate_vec(3)(dpdx),
                     co_yield truncate_vec(3)(dpdy), nullptr, nullptr
                   );
                 }
                 case air::TextureKind::depth_cube_array:
                 case air::TextureKind::texture_cube_array: {
                   co_return co_yield call_sample_grad(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rgb
                     co_yield extract_element(3)(coord) >>=
                     implicit_float_to_int, // array_index
                     co_yield truncate_vec(3)(dpdx),
                     co_yield truncate_vec(3)(dpdy), nullptr, nullptr
                   );
                 } break;
                 default: {
                   assert(0 && "invalid sample resource type");
                 }
                 }
               }) >>= extract_value(0)) >>=
              swizzle(sample.src_resource.read_swizzle)
            );
          },
          [&effect](InstSampleCompare sample) {
            effect << store_dst_op<true>(
              sample.dst,
              (make_irvalue_bind([=](struct context ctx) -> IRValue {
                 auto offset_const = llvm::ConstantVector::get(
                   {llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[0], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[1], true}
                    ),
                    llvm::ConstantInt::get(
                      ctx.llvm,
                      llvm::APInt{32, (uint64_t)sample.offsets[2], true}
                    )}
                 );
                 auto &[res, res_handle_fn, res_metadata] =
                   ctx.resource.srv_range_map[sample.src_resource.range_id];
                 auto &[sampler_handle_fn, sampler_metadata] =
                   ctx.resource.sampler_range_map[sample.src_sampler.range_id];
                 auto res_h = co_yield res_handle_fn(nullptr);
                 auto sampler_h = co_yield sampler_handle_fn(nullptr);
                 auto coord = co_yield load_src_op<true>(sample.src_address);
                 auto reference =
                   co_yield load_src_op<true>(sample.src_reference);
                 auto res_min_lod_clamp = co_yield metadata_get_min_lod_clamp(
                   co_yield res_metadata(nullptr)
                 );
                 auto sampler_bias = co_yield metadata_get_sampler_bias(
                   co_yield sampler_metadata(nullptr)
                 );
                 switch (res.resource_kind_logical) {
                 case air::TextureKind::depth_2d: {
                   co_return co_yield call_sample_compare(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord),        // .rg
                     nullptr,                                // array_index
                     co_yield extract_element(0)(reference), // reference
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     sampler_bias, res_min_lod_clamp,
                     sample.level_zero
                       ? llvm::ConstantFP::getZero(ctx.types._float)
                       : nullptr
                   );
                 }
                 case air::TextureKind::depth_2d_array: {
                   co_return co_yield call_sample_compare(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(2)(coord), // .rg
                     co_yield extract_element(2)(coord) >>=
                     implicit_float_to_int,                  // array_index
                     co_yield extract_element(0)(reference), // reference
                     co_yield truncate_vec(2)(offset_const), // 2d offset
                     sampler_bias, res_min_lod_clamp,
                     sample.level_zero
                       ? llvm::ConstantFP::getZero(ctx.types._float)
                       : nullptr
                   );
                 }
                 case air::TextureKind::depth_cube: {
                   co_return co_yield call_sample_compare(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord),        // .rgb
                     nullptr,                                // array_index
                     co_yield extract_element(0)(reference), // reference
                     nullptr, sampler_bias, res_min_lod_clamp,
                     sample.level_zero
                       ? llvm::ConstantFP::getZero(ctx.types._float)
                       : nullptr
                   );
                 }
                 case air::TextureKind::depth_cube_array: {
                   co_return co_yield call_sample_compare(
                     res, res_h, sampler_h,
                     co_yield truncate_vec(3)(coord), // .rgb
                     co_yield extract_element(3)(coord) >>=
                     implicit_float_to_int,                  // array_index
                     co_yield extract_element(0)(reference), // reference
                     nullptr, sampler_bias, res_min_lod_clamp,
                     sample.level_zero
                       ? llvm::ConstantFP::getZero(ctx.types._float)
                       : nullptr
                   );
                 } break;
                 default: {
                   assert(0 && "invalid sample_compare resource type");
                 }
                 }
               }) >>= extract_value(0)) >>=
              swizzle(sample.src_resource.read_swizzle)
            );
          },
          [&effect](InstGather sample) {
            effect << make_effect_bind([=](struct context ctx) -> IREffect {
              // min_lod_clamp ignored
              auto &[res, res_handle_fn, __] =
                ctx.resource.srv_range_map[sample.src_resource.range_id];
              // sampler bias ignored
              auto &[sampler_handle_fn, _] =
                ctx.resource.sampler_range_map[sample.src_sampler.range_id];
              auto res_h = co_yield res_handle_fn(nullptr);
              auto sampler_h = co_yield sampler_handle_fn(nullptr);
              auto coord = co_yield load_src_op<true>(sample.src_address);
              auto offset = co_yield load_src_op<false>(sample.offset);
              auto component =
                co_yield get_int(sample.src_sampler.gather_channel);
              pvalue ret;
              switch (res.resource_kind_logical) {
              case air::TextureKind::depth_2d:
              case air::TextureKind::texture_2d: {
                ret = co_yield call_gather(
                  res, res_h, sampler_h, co_yield truncate_vec(2)(coord),
                  nullptr, co_yield truncate_vec(2)(offset), component
                );
                break;
              }
              case air::TextureKind::depth_2d_array:
              case air::TextureKind::texture_2d_array: {
                ret = co_yield call_gather(
                  res, res_h, sampler_h, co_yield truncate_vec(2)(coord),
                  co_yield extract_element(2)(coord) >>=
                  implicit_float_to_int, // .b
                  co_yield truncate_vec(2)(offset), component
                );
                break;
              }
              case air::TextureKind::depth_cube:
              case air::TextureKind::texture_cube: {
                ret = co_yield call_gather(
                  res, res_h, sampler_h, co_yield truncate_vec(3)(coord),
                  nullptr, nullptr, component
                );
                break;
              }
              case air::TextureKind::depth_cube_array:
              case air::TextureKind::texture_cube_array: {
                ret = co_yield call_gather(
                  res, res_h, sampler_h, co_yield truncate_vec(3)(coord),
                  co_yield extract_element(3)(coord) >>=
                  implicit_float_to_int, // .b
                  nullptr, component
                );
                break;
              }
              default: {
                assert(0 && "invalid sample resource type");
              }
              }
              ret = co_yield extract_value(0)(ret) >>=
                swizzle(sample.src_resource.read_swizzle);
              co_return co_yield std::visit(
                patterns{
                  [&](air::MSLFloat) {
                    return store_dst_op<true>(sample.dst, air::pure(ret));
                  },
                  [&](auto) {
                    return store_dst_op<false>(sample.dst, air::pure(ret));
                  }
                },
                res.component_type
              );
            });
          },
          [&effect](InstGatherCompare sample) {
            effect << store_dst_op<true>(
              sample.dst,
              (make_irvalue_bind([=](struct context ctx) -> IRValue {
                 // min_lod_clamp ignored
                 auto &[res, res_handle_fn, __] =
                   ctx.resource.srv_range_map[sample.src_resource.range_id];
                 // sampler bias ignored
                 auto &[sampler_handle_fn, sampler_metadata] =
                   ctx.resource.sampler_range_map[sample.src_sampler.range_id];
                 auto res_h = co_yield res_handle_fn(nullptr);
                 auto sampler_h = co_yield sampler_handle_fn(nullptr);
                 auto coord = co_yield load_src_op<true>(sample.src_address);
                 auto offset = co_yield load_src_op<false>(sample.offset);
                 auto reference =
                   co_yield load_src_op<true>(sample.src_reference);
                 switch (res.resource_kind_logical) {
                 case air::TextureKind::depth_2d: {
                   co_return co_yield call_gather_compare(
                     res, res_h, sampler_h, co_yield truncate_vec(2)(coord),
                     nullptr, co_yield extract_element(0)(reference),
                     co_yield truncate_vec(2)(offset)
                   );
                 }
                 case air::TextureKind::depth_2d_array: {
                   co_return co_yield call_gather_compare(
                     res, res_h, sampler_h, co_yield truncate_vec(2)(coord),
                     co_yield extract_element(2)(coord) >>=
                     implicit_float_to_int, // .b
                     co_yield extract_element(0)(reference),
                     co_yield truncate_vec(2)(offset)
                   );
                 }
                 case air::TextureKind::depth_cube: {
                   co_return co_yield call_gather_compare(
                     res, res_h, sampler_h, co_yield truncate_vec(3)(coord),
                     nullptr, co_yield extract_element(0)(reference), nullptr
                   );
                 }
                 case air::TextureKind::depth_cube_array: {
                   co_return co_yield call_gather_compare(
                     res, res_h, sampler_h, co_yield truncate_vec(3)(coord),
                     co_yield extract_element(3)(coord) >>=
                     implicit_float_to_int, // .a
                     co_yield extract_element(0)(reference), nullptr
                   );
                 } break;
                 default: {
                   assert(0 && "invalid sample resource type");
                 }
                 }
               }) >>= extract_value(0)) >>=
              swizzle(sample.src_resource.read_swizzle)
            );
          },

          [&effect](InstLoad load) {
            effect << make_effect_bind([=](struct context ctx) -> IREffect {
              auto &[res, res_handle_fn, metadata] =
                ctx.resource.srv_range_map[load.src_resource.range_id];
              auto res_h = co_yield res_handle_fn(nullptr);
              auto coord = co_yield load_src_op<false>(load.src_address);
              pvalue ret;
              switch (res.resource_kind_logical) {
              case air::TextureKind::texture_buffer: {
                auto offset = co_yield metadata_get_texture_buffer_offset(
                  co_yield metadata(nullptr)
                );
                ret = co_yield call_read(
                  res, res_h,
                  ctx.builder.CreateAdd(
                    co_yield extract_element(0)(coord), offset
                  ),
                  co_yield get_int(load.offsets[0]), nullptr, nullptr, nullptr,
                  co_yield extract_element(3)(coord)
                );
                break;
              }
              case air::TextureKind::texture_1d: {
                ret = co_yield call_read(
                  res, res_h, co_yield extract_element(0)(coord) >>= extend_to_int2,
                  co_yield get_int2(load.offsets[0], 0), nullptr, nullptr, nullptr, co_yield extract_element(3)(coord)
                );
                break;
              }
              case air::TextureKind::texture_1d_array: {
                ret = co_yield call_read(
                  res, res_h, co_yield extract_element(0)(coord) >>= extend_to_int2,
                  co_yield get_int2(load.offsets[0], 0), nullptr, co_yield extract_element(1)(coord), nullptr,
                  co_yield extract_element(3)(coord)
                );
                break;
              }
              case air::TextureKind::depth_2d:
              case air::TextureKind::texture_2d: {
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(2)(coord),
                  co_yield get_int2(load.offsets[0], load.offsets[1]), nullptr,
                  nullptr, nullptr, co_yield extract_element(3)(coord)
                );
                break;
              }
              case air::TextureKind::depth_2d_array:
              case air::TextureKind::texture_2d_array: {
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(2)(coord),
                  co_yield get_int2(load.offsets[0], load.offsets[1]), nullptr,
                  co_yield extract_element(2)(coord), nullptr,
                  co_yield extract_element(3)(coord)
                );
                break;
              }
              case air::TextureKind::texture_3d: {
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(3)(coord),
                  co_yield get_int3(
                    load.offsets[0], load.offsets[1], load.offsets[2]
                  ),
                  nullptr, nullptr, nullptr, co_yield extract_element(3)(coord)
                );
                break;
              }
              case air::TextureKind::texture_2d_ms:
              case air::TextureKind::depth_2d_ms: {
                auto sample_index =
                  co_yield load_src_op<false>(load.src_sample_index.value()) >>=
                  extract_element(0);
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(2)(coord),
                  co_yield get_int2(load.offsets[0], load.offsets[1]), nullptr,
                  nullptr, sample_index
                );
                break;
              }
              case air::TextureKind::texture_2d_ms_array:
              case air::TextureKind::depth_2d_ms_array: {
                auto sample_index =
                  co_yield load_src_op<false>(load.src_sample_index.value()) >>=
                  extract_element(0);
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(2)(coord),
                  co_yield get_int2(load.offsets[0], load.offsets[1]), nullptr,
                  co_yield extract_element(2)(coord), sample_index
                );
                break;
              }
              default: {
                assert(0 && "invalid load resource type");
              }
              }
              ret = co_yield extract_value(0)(ret) >>=
                swizzle(load.src_resource.read_swizzle);
              co_return co_yield std::visit(
                patterns{
                  [&](air::MSLFloat) {
                    return store_dst_op<true>(load.dst, air::pure(ret));
                  },
                  [&](auto) {
                    return store_dst_op<false>(load.dst, air::pure(ret));
                  }
                },
                res.component_type
              );
            });
          },
          [&effect](InstLoadUAVTyped load) {
            effect << make_effect_bind([=](struct context ctx) -> IREffect {
              auto &[res, res_handle_fn, metadata] =
                ctx.resource.uav_range_map[load.src_uav.range_id];
              auto res_h = co_yield res_handle_fn(nullptr);
              auto coord = co_yield load_src_op<false>(load.src_address);
              pvalue ret;
              switch (res.resource_kind_logical) {
              case air::TextureKind::texture_buffer: {
                auto offset = co_yield metadata_get_texture_buffer_offset(
                  co_yield metadata(nullptr)
                );
                ret = co_yield call_read(
                  res, res_h,
                  ctx.builder.CreateAdd(
                    co_yield extract_element(0)(coord), offset
                  )
                );
                break;
              }
              case air::TextureKind::texture_1d: {
                ret = co_yield call_read(res, res_h, co_yield extract_element(0)(coord) >>= extend_to_int2);
                break;
              }
              case air::TextureKind::texture_1d_array: {
                ret = co_yield call_read(
                  res, res_h, co_yield extract_element(0)(coord) >>= extend_to_int2, nullptr, nullptr,
                  co_yield extract_element(1)(coord)
                );
                break;
              }
              case air::TextureKind::depth_2d:
              case air::TextureKind::texture_2d: {
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(2)(coord)
                );
                break;
              }
              case air::TextureKind::depth_2d_array:
              case air::TextureKind::texture_2d_array: {
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(2)(coord), nullptr, nullptr,
                  co_yield extract_element(2)(coord)
                );
                break;
              }
              case air::TextureKind::texture_3d: {
                ret = co_yield call_read(
                  res, res_h, co_yield truncate_vec(3)(coord)
                );
                break;
              }
              default: {
                assert(0 && "invalid load resource type");
              }
              }
              ret = co_yield extract_value(0)(ret) >>=
                swizzle(load.src_uav.read_swizzle);
              co_return co_yield std::visit(
                patterns{
                  [&](air::MSLFloat) {
                    return store_dst_op<true>(load.dst, air::pure(ret));
                  },
                  [&](auto) {
                    return store_dst_op<false>(load.dst, air::pure(ret));
                  }
                },
                res.component_type
              );
            });
          },
          [&effect](InstStoreUAVTyped store) {
            effect << make_effect_bind([=](struct context ctx) -> IREffect {
              auto &[res, res_handle_fn, metadata] =
                ctx.resource.uav_range_map[store.dst.range_id];
              auto res_h = co_yield res_handle_fn(nullptr);
              auto address = co_yield load_src_op<false>(store.src_address);
              auto value = co_yield std::visit(
                patterns{
                  [&](air::MSLFloat) { return load_src_op<true>(store.src); },
                  [&](auto) { return load_src_op<false>(store.src); }
                },
                res.component_type
              );

              switch (res.resource_kind_logical) {
              case air::TextureKind::texture_buffer: {
                auto offset = co_yield metadata_get_texture_buffer_offset(
                  co_yield metadata(nullptr)
                );
                co_yield call_write(
                  res, res_h,
                  ctx.builder.CreateAdd(
                    co_yield extract_element(0)(address), offset
                  ),
                  nullptr, nullptr, value, ctx.builder.getInt32(0)
                );
                break;
              }
              case air::TextureKind::texture_1d:
                co_yield call_write(
                  res, res_h, co_yield extract_element(0)(address) >>= extend_to_int2, nullptr, nullptr, value,
                  ctx.builder.getInt32(0)
                );
                break;
              case air::TextureKind::texture_1d_array:
                co_yield call_write(
                  res, res_h, co_yield extract_element(0)(address) >>= extend_to_int2, nullptr,
                  co_yield extract_element(1)(address), value, ctx.builder.getInt32(0)
                );
                break;
              case air::TextureKind::texture_2d:
                co_yield call_write(
                  res, res_h, co_yield truncate_vec(2)(address), nullptr,
                  nullptr, value, ctx.builder.getInt32(0)
                );
                break;
              case air::TextureKind::texture_2d_array: {
                co_yield call_write(
                  res, res_h, co_yield truncate_vec(2)(address), nullptr,
                  co_yield extract_element(2)(address), value,
                  ctx.builder.getInt32(0)
                );
                break;
              }
              case air::TextureKind::texture_3d:
                co_yield call_write(
                  res, res_h, co_yield truncate_vec(3)(address), nullptr,
                  nullptr, value, ctx.builder.getInt32(0)
                );
                break;
              default:
                assert(0 && "invalid texture kind for uav store");
              }
              co_return {};
            });
          },
          [&effect](InstLoadStructured load) {
            effect << std::visit(
              patterns{
                [&](SrcOperandTGSM tgsm) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto [stride, _] = ctx.resource.tgsm_map[tgsm.id];
                    co_return co_yield store_dst_op<false>(
                      load.dst,
                      load_from_uint_bufptr(
                        co_yield read_uint_buf_addr(
                          tgsm, co_yield calc_uint_ptr_index(
                                  stride, load.src_address, load.src_byte_offset
                                )
                        ),
                        std::max(
                          {tgsm.read_swizzle.x, tgsm.read_swizzle.y,
                           tgsm.read_swizzle.z, tgsm.read_swizzle.w}
                        ) +
                          1
                      ) >>= swizzle(tgsm.read_swizzle)
                    );
                  });
                },
                [&](SrcOperandResource srv) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto& [stride, __, _] =
                      ctx.resource.srv_buf_range_map[srv.range_id];
                    pvalue oob = nullptr;
                    co_return co_yield store_dst_op<false>(
                      load.dst,
                      load_from_uint_bufptr(
                        co_yield read_uint_buf_addr(
                          srv,
                          co_yield calc_uint_ptr_index(
                            stride, load.src_address, load.src_byte_offset
                          ),
                          &oob
                        ),
                        std::max(
                          {srv.read_swizzle.x, srv.read_swizzle.y,
                           srv.read_swizzle.z, srv.read_swizzle.w}
                        ) +
                          1,
                        oob
                      ) >>= swizzle(srv.read_swizzle)
                    );
                  });
                },
                [&](SrcOperandUAV uav) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto &[stride, __, _] =
                      ctx.resource.uav_buf_range_map[uav.range_id];
                    pvalue oob = nullptr;
                    co_return co_yield store_dst_op<false>(
                      load.dst,
                      load_from_uint_bufptr(
                        co_yield read_uint_buf_addr(
                          uav,
                          co_yield calc_uint_ptr_index(
                            stride, load.src_address, load.src_byte_offset
                          ),
                          &oob
                        ),
                        std::max(
                          {uav.read_swizzle.x, uav.read_swizzle.y,
                           uav.read_swizzle.z, uav.read_swizzle.w}
                        ) +
                          1,
                        oob
                      ) >>= swizzle(uav.read_swizzle)
                    );
                  });
                }
              },
              load.src
            );
          },
          [&effect](InstStoreStructured store) {
            effect << std::visit(
              patterns{
                [&](AtomicOperandTGSM tgsm) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto& [stride, _] = ctx.resource.tgsm_map[tgsm.id];
                    co_return co_yield store_to_uint_bufptr(
                      co_yield read_uint_buf_addr(
                        tgsm, co_yield calc_uint_ptr_index(
                                stride, store.dst_address, store.dst_byte_offset
                              )
                      ),
                      mask_to_linear_components_num(tgsm.mask),
                      co_yield load_src_op<false>(store.src)
                    );
                  });
                },
                [&](AtomicDstOperandUAV uav) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto& [stride, __, _] =
                      ctx.resource.uav_buf_range_map[uav.range_id];
                    co_return co_yield store_to_uint_bufptr(
                      co_yield read_uint_buf_addr(
                        uav, co_yield calc_uint_ptr_index(
                               stride, store.dst_address, store.dst_byte_offset
                             )
                      ),
                      mask_to_linear_components_num(uav.mask),
                      co_yield load_src_op<false>(store.src)
                    );
                  });
                }
              },
              store.dst
            );
          },
          [&effect](InstLoadRaw load) {
            effect << std::visit(
              patterns{
                [&](SrcOperandTGSM tgsm) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    co_return co_yield store_dst_op<false>(
                      load.dst, load_from_uint_bufptr(
                                  co_yield read_uint_buf_addr(
                                    tgsm, co_yield calc_uint_ptr_index(
                                            0, 0, load.src_byte_offset
                                          )
                                  ),
                                  std::max(
                                    {tgsm.read_swizzle.x, tgsm.read_swizzle.y,
                                     tgsm.read_swizzle.z, tgsm.read_swizzle.w}
                                  ) +
                                    1
                                ) >>= swizzle(tgsm.read_swizzle)
                    );
                  });
                },
                [&](SrcOperandResource srv) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    pvalue oob = nullptr;
                    co_return co_yield store_dst_op<false>(
                      load.dst, load_from_uint_bufptr(
                                  co_yield read_uint_buf_addr(
                                    srv,
                                    co_yield calc_uint_ptr_index(
                                      0, 0, load.src_byte_offset
                                    ),
                                    &oob
                                  ),
                                  std::max(
                                    {srv.read_swizzle.x, srv.read_swizzle.y,
                                     srv.read_swizzle.z, srv.read_swizzle.w}
                                  ) +
                                    1,
                                  oob
                                ) >>= swizzle(srv.read_swizzle)
                    );
                  });
                },
                [&](SrcOperandUAV uav) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    pvalue oob = nullptr;
                    co_return co_yield store_dst_op<false>(
                      load.dst, load_from_uint_bufptr(
                                  co_yield read_uint_buf_addr(
                                    uav,
                                    co_yield calc_uint_ptr_index(
                                      0, 0, load.src_byte_offset
                                    ),
                                    &oob
                                  ),
                                  std::max(
                                    {uav.read_swizzle.x, uav.read_swizzle.y,
                                     uav.read_swizzle.z, uav.read_swizzle.w}
                                  ) +
                                    1,
                                  oob
                                ) >>= swizzle(uav.read_swizzle)
                    );
                  });
                }
              },
              load.src
            );
          },
          [&effect](InstStoreRaw store) {
            effect << std::visit(
              patterns{
                [&](AtomicOperandTGSM tgsm) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto [stride, tgsm_h] = ctx.resource.tgsm_map[tgsm.id];
                    co_return co_yield store_tgsm(
                      tgsm_h,
                      co_yield calc_uint_ptr_index(0, 0, store.dst_byte_offset),
                      mask_to_linear_components_num(tgsm.mask),
                      co_yield load_src_op<false>(store.src)
                    );
                  });
                },
                [&](AtomicDstOperandUAV uav) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    co_return co_yield store_to_uint_bufptr(
                      co_yield read_uint_buf_addr(
                        uav, co_yield calc_uint_ptr_index(
                               0, 0, store.dst_byte_offset
                             )
                      ),
                      mask_to_linear_components_num(uav.mask),
                      co_yield load_src_op<false>(store.src)
                    );
                  });
                }
              },
              store.dst
            );
          },
          [&effect](InstIntegerCompare icmp) {
            llvm::CmpInst::Predicate pred;
            switch (icmp.cmp) {
            case IntegerComparison::Equal:
              pred = llvm::CmpInst::ICMP_EQ;
              break;
            case IntegerComparison::NotEqual:
              pred = llvm::CmpInst::ICMP_NE;
              break;
            case IntegerComparison::SignedLessThan:
              pred = llvm::CmpInst::ICMP_SLT;
              break;
            case IntegerComparison::SignedGreaterEqual:
              pred = llvm::CmpInst::ICMP_SGE;
              break;
            case IntegerComparison::UnsignedLessThan:
              pred = llvm::CmpInst::ICMP_ULT;
              break;
            case IntegerComparison::UnsignedGreaterEqual:
              pred = llvm::CmpInst::ICMP_UGE;
              break;
            }
            auto mask = get_dst_mask(icmp.dst);
            effect << store_dst_op_masked<false>(
              icmp.dst,
              lift(
                load_src_op<false>(icmp.src0, mask),
                load_src_op<false>(icmp.src1, mask),
                [=](auto a, auto b) { return cmp_integer(pred, a, b); }
              )
            );
          },
          [&effect](InstIntegerUnaryOp unary) {
            std::function<IRValue(pvalue)> fn;
            switch (unary.op) {
            case IntegerUnaryOp::Neg:
              fn = [=](pvalue a) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateNeg(a);
                });
              };
              break;
            case IntegerUnaryOp::Not:
              fn = [=](pvalue a) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateNot(a);
                });
              };
              break;
            case IntegerUnaryOp::ReverseBits:
              fn = [=](pvalue a) {
                // metal shader spec doesn't make it clear
                // if this is component-wise...
                return air::call_integer_unary_op("reverse_bits", a);
              };
              break;
            case IntegerUnaryOp::CountBits:
              fn = [=](pvalue a) {
                // metal shader spec doesn't make it clear
                // if this is component-wise...
                return air::call_integer_unary_op("popcount", a);
              };
              break;
            case IntegerUnaryOp::FirstHiBitSigned:
              fn = [=](pvalue a) {
                return make_irvalue_bind([=](struct context ctx) -> IRValue {
                  co_return ctx.builder.CreateSelect(
                    ctx.builder.CreateICmpSGE(
                      a, llvm::ConstantInt::get(a->getType(), 0)
                    ),
                    co_yield air::call_count_zero(false, a),
                    co_yield air::call_count_zero(
                      false, ctx.builder.CreateNot(a)
                    )
                  );
                });
              };
              break;
            case IntegerUnaryOp::FirstHiBit:
              fn = [=](pvalue a) { return air::call_count_zero(false, a); };
              break;
            case IntegerUnaryOp::FirstLowBit:
              fn = [=](pvalue a) { return air::call_count_zero(true, a); };
              break;
            }
            auto mask = get_dst_mask(unary.dst);
            effect << store_dst_op_masked<false>(
              unary.dst, load_src_op<false>(unary.src, mask) >>= fn
            );
          },
          [&effect](InstIntegerBinaryOp bin) {
            std::function<IRValue(pvalue, pvalue)> fn;
            switch (bin.op) {
            case IntegerBinaryOp::IShl:
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateShl(
                    a, ctx.builder.CreateAnd(b, 0x1f)
                  );
                });
              };
              break;
            case IntegerBinaryOp::IShr:
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateAShr(
                    a, ctx.builder.CreateAnd(b, 0x1f)
                  );
                });
              };
              break;
            case IntegerBinaryOp::UShr:
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateLShr(
                    a, ctx.builder.CreateAnd(b, 0x1f)
                  );
                });
              };
              break;
            case IntegerBinaryOp::Xor:
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateXor(a, b);
                });
              };
              break;
            case IntegerBinaryOp::And:
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateAnd(a, b);
                });
              };
              break;
            case IntegerBinaryOp::Or:
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateOr(a, b);
                });
              };
              break;
            case IntegerBinaryOp::Add:
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateAdd(a, b);
                });
              };
              break;
            case IntegerBinaryOp::UMin:
              fn = [=](pvalue a, pvalue b) {
                return air::call_integer_binop("min", a, b, false);
              };
              break;
            case IntegerBinaryOp::UMax:
              fn = [=](pvalue a, pvalue b) {
                return air::call_integer_binop("max", a, b, false);
              };
              break;
            case IntegerBinaryOp::IMin:
              fn = [=](pvalue a, pvalue b) {
                return air::call_integer_binop("min", a, b, true);
              };
              break;
            case IntegerBinaryOp::IMax:
              fn = [=](pvalue a, pvalue b) {
                return air::call_integer_binop("max", a, b, true);
              };
              break;
            }
            auto mask = get_dst_mask(bin.dst);
            effect << store_dst_op_masked<false>(
              bin.dst, lift(
                         load_src_op<false>(bin.src0, mask),
                         load_src_op<false>(bin.src1, mask), fn
                       )
            );
          },
          [&effect](InstFloatCompare fcmp) {
            llvm::CmpInst::Predicate pred;
            switch (fcmp.cmp) {
            case FloatComparison::Equal:
              pred = llvm::CmpInst::FCMP_OEQ;
              break;
            case FloatComparison::NotEqual:
              pred = llvm::CmpInst::FCMP_UNE;
              break;
            case FloatComparison::GreaterEqual:
              pred = llvm::CmpInst::FCMP_OGE;
              break;
            case FloatComparison::LessThan:
              pred = llvm::CmpInst::FCMP_OLT;
              break;
            }
            auto mask = get_dst_mask(fcmp.dst);
            effect << store_dst_op_masked<false>(
              fcmp.dst, lift(
                          load_src_op<true>(fcmp.src0, mask),
                          load_src_op<true>(fcmp.src1, mask),
                          [=](auto a, auto b) { return cmp_float(pred, a, b); }
                        )
            );
          },
          [&effect](InstDotProduct dp) {
            switch (dp.dimension) {
            case 4:
              effect << lift(
                load_src_op<true>(dp.src0), load_src_op<true>(dp.src1),
                [=](auto a, auto b) {
                  return store_dst_op<true>(
                    dp.dst, air::call_dot_product(4, a, b) >>=
                            air::saturate(dp._.saturate)
                  );
                }
              );
              break;
            case 3:
              effect << lift(
                load_src_op<true>(dp.src0) >>= truncate_vec(3),
                load_src_op<true>(dp.src1) >>= truncate_vec(3),
                [=](auto a, auto b) {
                  return store_dst_op<true>(
                    dp.dst, air::call_dot_product(3, a, b) >>=
                            air::saturate(dp._.saturate)
                  );
                }
              );
              break;
            case 2:
              effect << lift(
                load_src_op<true>(dp.src0) >>= truncate_vec(2),
                load_src_op<true>(dp.src1) >>= truncate_vec(2),
                [=](auto a, auto b) {
                  return store_dst_op<true>(
                    dp.dst, air::call_dot_product(2, a, b) >>=
                            air::saturate(dp._.saturate)
                  );
                }
              );
              break;
            default:
              assert(0 && "wrong dot product dimension");
            }
          },
          [&effect](InstFloatMAD mad) {
            auto mask = get_dst_mask(mad.dst);
            effect << lift(
              load_src_op<true>(mad.src0, mask),
              load_src_op<true>(mad.src1, mask),
              load_src_op<true>(mad.src2, mask),
              [=](auto a, auto b, auto c) {
                return store_dst_op_masked<true>(
                  mad.dst,
                  air::call_float_mad(a, b, c) >>= air::saturate(mad._.saturate)
                );
              }
            );
          },
          [&effect](InstIntegerMAD mad) {
            auto mask = get_dst_mask(mad.dst);
            effect << lift(
              load_src_op<false>(mad.src0, mask),
              load_src_op<false>(mad.src1, mask),
              load_src_op<false>(mad.src2, mask),
              [=](auto a, auto b, auto c) {
                return store_dst_op_masked<false>(
                  mad.dst, make_irvalue([=](struct context ctx) {
                    return ctx.builder.CreateAdd(
                      ctx.builder.CreateMul(a, b), c
                    );
                  })
                );
              }
            );
          },
          [&effect](InstFloatUnaryOp unary) {
            std::function<IRValue(pvalue)> fn;
            switch (unary.op) {
            case FloatUnaryOp::Log2: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("log2", a);
              };
              break;
            }
            case FloatUnaryOp::Exp2: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("exp2", a);
              };
              break;
            }
            case FloatUnaryOp::Rcp: {
              fn = [=](pvalue a) {
                return make_irvalue([=](struct context ctx) {
                  if (!llvm::isa<llvm::FixedVectorType>(a->getType())) {
                    // it's a scalar
                    return ctx.builder.CreateFDiv(
                      llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{1.0f}), a
                    );
                  }
                  return ctx.builder.CreateFDiv(
                    ctx.builder.CreateVectorSplat(
                      cast<llvm::FixedVectorType>(a->getType())
                        ->getNumElements(),
                      llvm::ConstantFP::get(ctx.llvm, llvm::APFloat{1.0f})
                    ),
                    a
                  );
                });
              };
              break;
            }
            case FloatUnaryOp::Rsq: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("rsqrt", a);
              };
              break;
            }
            case FloatUnaryOp::Sqrt: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("sqrt", a);
              };
              break;
            }
            case FloatUnaryOp::Fraction: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("fract", a);
              };
              break;
            }
            case FloatUnaryOp::RoundNearestEven: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("rint", a);
              };
              break;
            }
            case FloatUnaryOp::RoundNegativeInf: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("floor", a);
              };
              break;
            }
            case FloatUnaryOp::RoundPositiveInf: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("ceil", a);
              };
              break;
            }
            case FloatUnaryOp::RoundZero: {
              fn = [=](pvalue a) {
                return air::call_float_unary_op("trunc", a);
              };
              break;
            } break;
            }
            auto mask = get_dst_mask(unary.dst);
            effect << store_dst_op_masked<true>(
              unary.dst, (load_src_op<true>(unary.src, mask) >>= fn) >>=
                         saturate(unary._.saturate)
            );
          },
          [&effect](InstFloatBinaryOp bin) {
            std::function<IRValue(pvalue, pvalue)> fn;
            switch (bin.op) {
            case FloatBinaryOp::Add: {
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateFAdd(a, b);
                });
              };
              break;
            }
            case FloatBinaryOp::Mul: {
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateFMul(a, b);
                });
              };
              break;
            }
            case FloatBinaryOp::Div: {
              fn = [=](pvalue a, pvalue b) {
                return make_irvalue([=](struct context ctx) {
                  return ctx.builder.CreateFDiv(a, b);
                });
              };
              break;
            }
            case FloatBinaryOp::Min: {
              fn = [=](pvalue a, pvalue b) {
                return air::call_float_binop("fmin", a, b, llvm::isa<llvm::Constant>(a) || llvm::isa<llvm::Constant>(b));
              };
              break;
            }
            case FloatBinaryOp::Max: {
              fn = [=](pvalue a, pvalue b) {
                return air::call_float_binop("fmax", a, b, llvm::isa<llvm::Constant>(a) || llvm::isa<llvm::Constant>(b));
              };
              break;
            }
            }
            auto mask = get_dst_mask(bin.dst);
            effect << store_dst_op_masked<true>(
              bin.dst, lift(
                         load_src_op<true>(bin.src0, mask),
                         load_src_op<true>(bin.src1, mask), fn
                       ) >>= saturate(bin._.saturate)
            );
          },
          [&effect](InstSinCos sincos) {
            auto mask_cos = get_dst_mask(sincos.dst_cos);
            auto mask_sin = get_dst_mask(sincos.dst_sin);
            effect << make_effect_bind([=](struct context ctx) -> IREffect {
              auto src = co_yield load_src_op<true>(sincos.src);
              co_yield store_dst_op_masked<true>(
                sincos.dst_cos,
                air::call_float_unary_op(
                  "cos", co_yield get_valid_components(src, mask_cos)
                ) >>= air::saturate(sincos._.saturate)
              );
              co_yield store_dst_op_masked<true>(
                sincos.dst_sin,
                air::call_float_unary_op(
                  "sin", co_yield get_valid_components(src, mask_sin)
                ) >>= air::saturate(sincos._.saturate)
              );
              co_return {};
            });
          },
          [&effect](InstConvert convert) {
            auto mask = get_dst_mask(convert.dst);
            switch (convert.op) {
            case ConversionOp::HalfToFloat: {
              effect << store_dst_op_masked<true>(
                convert.dst,
                make_irvalue_bind([=](struct context ctx) -> IRValue {
                  // FIXME: neg modifier might be wrong?!
                  auto src = co_yield load_src_op<false>(convert.src, mask);
                  auto src_type = get_splat_type(
                    llvm::IntegerType::getInt16Ty(ctx.llvm), mask
                  );
                  auto dst_type = get_splat_type(ctx.types._half, mask);
                  auto half4 = ctx.builder.CreateBitCast(
                    ctx.builder.CreateTrunc(src, src_type), dst_type
                  );
                  co_return co_yield call_convert(
                    half4, ctx.types._float, air::Sign::with_sign /* intended */
                  );
                })
              );
              break;
            }
            case ConversionOp::FloatToHalf: {
              effect << store_dst_op_masked<false>(
                convert.dst,
                make_irvalue_bind([=](struct context ctx) -> IRValue {
                  // FIXME: neg modifier might be wrong?!
                  auto src = co_yield load_src_op<true>(convert.src, mask);
                  auto half_src = co_yield call_convert(
                    src, ctx.types._half, air::Sign::with_sign /* intended */
                  );
                  auto src_type = get_splat_type(
                    llvm::IntegerType::getInt16Ty(ctx.llvm), mask
                  );
                  auto dst_type = get_splat_type(ctx.types._int, mask);
                  co_return ctx.builder.CreateZExt(
                    ctx.builder.CreateBitCast(half_src, src_type), dst_type
                  );
                })
              );
              break;
            }
            case ConversionOp::FloatToSigned: {
              effect << store_dst_op_masked<false>(
                convert.dst,
                make_irvalue_bind([=](struct context ctx) -> IRValue {
                  auto src = co_yield load_src_op<true>(convert.src, mask);
                  co_return co_yield call_convert(
                    src, ctx.types._int, air::Sign::with_sign
                  );
                })
              );
              break;
            }
            case ConversionOp::SignedToFloat: {
              effect << store_dst_op_masked<true>(
                convert.dst,
                make_irvalue_bind([=](struct context ctx) -> IRValue {
                  auto src = co_yield load_src_op<false>(convert.src, mask);
                  co_return co_yield call_convert(
                    src, ctx.types._float, air::Sign::with_sign
                  );
                })
              );
              break;
            }
            case ConversionOp::FloatToUnsigned: {
              effect << store_dst_op_masked<false>(
                convert.dst,
                make_irvalue_bind([=](struct context ctx) -> IRValue {
                  auto src = co_yield load_src_op<true>(convert.src, mask);
                  co_return co_yield call_convert(
                    src, ctx.types._int, air::Sign::no_sign
                  );
                })
              );
              break;
            }
            case ConversionOp::UnsignedToFloat: {
              effect << store_dst_op_masked<true>(
                convert.dst,
                make_irvalue_bind([=](struct context ctx) -> IRValue {
                  auto src = co_yield load_src_op<false>(convert.src, mask);
                  co_return co_yield call_convert(
                    src, ctx.types._float, air::Sign::no_sign
                  );
                })
              );
              break;
            }
            }
          },
          [&effect](InstIntegerBinaryOpWithTwoDst bin) {
            auto dst_hi_mask = get_dst_mask(bin.dst_hi);
            auto dst_lo_mask = get_dst_mask(bin.dst_low);
            switch (bin.op) {
            case IntegerBinaryOpWithTwoDst::IMul:
            case IntegerBinaryOpWithTwoDst::UMul: {
              effect << make_effect_bind([=](struct context ctx) -> IREffect {
                auto a = co_yield load_src_op<false>(bin.src0);
                auto b = co_yield load_src_op<false>(bin.src1);
                co_yield store_dst_op_masked<false>(
                  bin.dst_hi,
                  air::call_integer_binop(
                    "mul_hi", co_yield get_valid_components(a, dst_hi_mask),
                    co_yield get_valid_components(b, dst_hi_mask),
                    bin.op == IntegerBinaryOpWithTwoDst::IMul
                  )
                );
                co_yield store_dst_op_masked<false>(
                  bin.dst_low, air::pure(ctx.builder.CreateMul(
                                 co_yield get_valid_components(a, dst_lo_mask),
                                 co_yield get_valid_components(b, dst_lo_mask)
                               ))
                );
                co_return {};
              });
              break;
            }
            case IntegerBinaryOpWithTwoDst::UAddCarry:
            case IntegerBinaryOpWithTwoDst::USubBorrow: {
              assert(0 && "todo");
              break;
            }
            case IntegerBinaryOpWithTwoDst::UDiv: {
              effect << make_effect_bind([=](struct context ctx) -> IREffect {
                auto a = co_yield load_src_op<false>(bin.src0);
                auto b = co_yield load_src_op<false>(bin.src1);
                co_yield store_dst_op_masked<false>(
                  bin.dst_quot,
                  make_irvalue_bind([=](struct context ctx) -> IRValue {
                    co_return ctx.builder.CreateUDiv(
                      co_yield get_valid_components(a, dst_hi_mask),
                      co_yield get_valid_components(b, dst_hi_mask)
                    );
                  })
                );
                co_yield store_dst_op_masked<false>(
                  bin.dst_rem,
                  make_irvalue_bind([=](struct context ctx) -> IRValue {
                    co_return ctx.builder.CreateURem(
                      co_yield get_valid_components(a, dst_lo_mask),
                      co_yield get_valid_components(b, dst_lo_mask)
                    );
                  })
                );
                co_return {};
              });
              break;
            }
            }
          },
          [&effect](InstExtractBits extract) {
            auto mask = get_dst_mask(extract.dst);
            effect << store_dst_op_masked<false>(
              extract.dst,
              make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto src0 = co_yield load_src_op<false>(extract.src0, mask);
                auto src1 = co_yield load_src_op<false>(extract.src1, mask);
                auto src2 = co_yield load_src_op<false>(extract.src2, mask);
                auto width = ctx.builder.CreateAnd(
                  src0, co_yield get_splat_constant(0b11111, mask)
                );
                auto offset = ctx.builder.CreateAnd(
                  src1, co_yield get_splat_constant(0b11111, mask)
                );
                auto width_is_zero = ctx.builder.CreateICmpEQ(
                  width, co_yield get_splat_constant(0, mask)
                );
                auto width_offset_sum = ctx.builder.CreateAdd(width, offset);
                auto width_cp = ctx.builder.CreateSub(
                  co_yield get_splat_constant(32, mask), width
                );
                auto clamp_src = ctx.builder.CreateShl(
                  src2,
                  ctx.builder.CreateSub(
                    co_yield get_splat_constant(32, mask), width_offset_sum
                  )
                );
                if (!extract.is_signed) {
                  auto need_clamp = ctx.builder.CreateICmpULT(
                    width_offset_sum, co_yield get_splat_constant(32, mask)
                  );
                  auto no_clamp = ctx.builder.CreateLShr(src2, offset);
                  auto clamped = ctx.builder.CreateLShr(clamp_src, width_cp);
                  co_return ctx.builder.CreateSelect(
                    width_is_zero, co_yield get_splat_constant(0, mask),
                    ctx.builder.CreateSelect(need_clamp, clamped, no_clamp)
                  );
                } else {
                  auto need_clamp = ctx.builder.CreateICmpSLT(
                    width_offset_sum, co_yield get_splat_constant(32, mask)
                  );
                  auto no_clamp = ctx.builder.CreateAShr(src2, offset);
                  auto clamped = ctx.builder.CreateAShr(clamp_src, width_cp);
                  co_return ctx.builder.CreateSelect(
                    width_is_zero, co_yield get_splat_constant(0, mask),
                    ctx.builder.CreateSelect(need_clamp, clamped, no_clamp)
                  );
                }
              })
            );
          },
          [&effect](InstBitFiledInsert bfi) {
            auto mask = get_dst_mask(bfi.dst);
            effect << store_dst_op_masked<false>(
              bfi.dst, make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto src0 = co_yield load_src_op<false>(bfi.src0, mask);
                auto src1 = co_yield load_src_op<false>(bfi.src1, mask);
                auto src2 = co_yield load_src_op<false>(bfi.src2, mask);
                auto src3 = co_yield load_src_op<false>(bfi.src3, mask);
                auto width = ctx.builder.CreateAnd(
                  src0, co_yield get_splat_constant(0b11111, mask)
                );
                auto offset = ctx.builder.CreateAnd(
                  src1, co_yield get_splat_constant(0b11111, mask)
                );
                auto bitmask = ctx.builder.CreateAnd(
                  co_yield get_splat_constant(
                    0xffffffff, mask
                  ), // is this necessary?
                  ctx.builder.CreateShl(
                    ctx.builder.CreateSub(
                      ctx.builder.CreateShl(
                        co_yield get_splat_constant(1, mask), width
                      ),
                      co_yield get_splat_constant(1, mask)
                    ),
                    offset
                  )
                );
                co_return ctx.builder.CreateOr(
                  ctx.builder.CreateAnd(
                    ctx.builder.CreateShl(src2, offset), bitmask
                  ),
                  ctx.builder.CreateAnd(src3, ctx.builder.CreateNot(bitmask))
                );
              })
            );
          },
          [&effect](InstSync sync) {
            mem_flags mem_flag = sync.tgsm_memory_barrier ? mem_flags::threadgroup : mem_flags::none;
            if (sync.uav_boundary != InstSync::UAVBoundary::none) {
              mem_flag |= mem_flags::device | mem_flags::texture;
            }
            effect << call_threadgroup_barrier(mem_flag);
          },
          [&effect](InstPixelDiscard) { effect << call_discard_fragment(); },
          [&effect](InstPartialDerivative df) {
            effect << store_dst_op<true>(
              df.dst, make_irvalue_bind([=](struct context ctx) -> IRValue {
                        auto fvec4 = co_yield load_src_op<true>(df.src);
                        co_return co_yield air::call_derivative(fvec4, df.ddy);
                      }) >>= saturate(df._.saturate)
            );
          },
          [&effect](InstCalcLOD calc) {
            effect << store_dst_op<true>(
              calc.dst,
              make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto &[res, res_handle_fn, _] =
                  ctx.resource.srv_range_map[calc.src_resource.range_id];
                // FIXME: sampelr bias is not respected
                auto &[sampler_handle_fn, __] =
                  ctx.resource.sampler_range_map[calc.src_sampler.range_id];
                auto res_h = co_yield res_handle_fn(nullptr);
                auto sampler_h = co_yield sampler_handle_fn(nullptr);
                auto coord = co_yield load_src_op<true>(calc.src_address);
                pvalue clamped_float = nullptr, unclamped_float = nullptr;
                switch (res.resource_kind_logical) {
                case air::TextureKind::texture_1d:
                case air::TextureKind::texture_1d_array: {
                  clamped_float = co_yield call_calc_lod(
                    res, res_h, sampler_h, co_yield extract_element(0)(coord) >>= extend_to_float2, false
                  );
                  unclamped_float = co_yield call_calc_lod(
                    res, res_h, sampler_h, co_yield extract_element(0)(coord) >>= extend_to_float2, true
                  );
                  break;
                }
                case air::TextureKind::depth_2d:
                case air::TextureKind::texture_2d:
                case air::TextureKind::depth_2d_array:
                case air::TextureKind::texture_2d_array: {
                  clamped_float = co_yield call_calc_lod(
                    res, res_h, sampler_h, co_yield truncate_vec(2)(coord),
                    false
                  );
                  unclamped_float = co_yield call_calc_lod(
                    res, res_h, sampler_h, co_yield truncate_vec(2)(coord), true
                  );
                  break;
                }
                case air::TextureKind::texture_3d:
                case air::TextureKind::depth_cube:
                case air::TextureKind::texture_cube:
                case air::TextureKind::depth_cube_array:
                case air::TextureKind::texture_cube_array: {
                  clamped_float = co_yield call_calc_lod(
                    res, res_h, sampler_h, co_yield truncate_vec(3)(coord),
                    false
                  );
                  unclamped_float = co_yield call_calc_lod(
                    res, res_h, sampler_h, co_yield truncate_vec(3)(coord), true
                  );
                  break;
                }
                default: {
                  assert(0 && "invalid calc_lod resource type");
                }
                }
                co_return ctx.builder.CreateInsertElement(
                  ctx.builder.CreateInsertElement(
                    llvm::ConstantAggregateZero::get(ctx.types._float4),
                    clamped_float, (uint64_t)0
                  ),
                  unclamped_float, 1
                );
              }) >>= swizzle(calc.src_resource.read_swizzle)
            );
          },
          [](InstNop) {}, // nop
          [](InstMaskedSumOfAbsDiff) { assert(0 && "unhandled msad"); },

          [&effect](InstAtomicBinOp bin) {
            std::string op;
            bool is_signed = false;
            switch (bin.op) {
            case AtomicBinaryOp::And:
              op = "and";
              break;
            case AtomicBinaryOp::Or:
              op = "or";
              break;
            case AtomicBinaryOp::Xor:
              op = "xor";
              break;
            case AtomicBinaryOp::Add:
              op = "add";
              break;
            case AtomicBinaryOp::IMax:
              op = "max";
              is_signed = true;
              break;
            case AtomicBinaryOp::IMin:
              op = "min";
              is_signed = true;
              break;
            case AtomicBinaryOp::UMax:
              op = "max";
              break;
            case AtomicBinaryOp::UMin:
              op = "min";
              break;
            }
            effect << std::visit(
              patterns{
                [&](AtomicOperandTGSM tgsm) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto ptr = co_yield read_uint_buf_addr(
                      tgsm, co_yield read_atomic_index(
                              tgsm, co_yield load_src_op<false>(bin.dst_address)
                            )
                    );
                    co_return co_yield store_dst_op<false>(
                      bin.dst_original, air::call_atomic_fetch_explicit(
                                          ptr,
                                          co_yield load_src_op<false>(bin.src
                                          ) >>= extract_element(0),
                                          op, is_signed, false
                                        )
                    );
                  });
                },
                [&](AtomicDstOperandUAV uav) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    if (ctx.resource.uav_buf_range_map.contains(uav.range_id)) {
                      auto ptr = co_yield read_uint_buf_addr(
                        uav, co_yield read_atomic_index(
                               uav, co_yield load_src_op<false>(bin.dst_address)
                             )
                      );
                      co_return co_yield store_dst_op<false>(
                        bin.dst_original, air::call_atomic_fetch_explicit(
                                            ptr,
                                            co_yield load_src_op<false>(bin.src
                                            ) >>= extract_element(0),
                                            op, is_signed, true
                                          )
                      );
                    }
                    auto &[res, res_handle_fn, metadata] =
                      ctx.resource.uav_range_map.at(uav.range_id);
                    auto res_h = co_yield res_handle_fn(nullptr);
                    auto address = co_yield load_src_op<false>(bin.dst_address);
                    auto value = co_yield load_src_op<false>(bin.src);
                    switch (res.resource_kind_logical) {
                    case air::TextureKind::texture_buffer: {
                      auto offset = co_yield metadata_get_texture_buffer_offset(
                        co_yield metadata(nullptr)
                      );
                      co_return co_yield store_dst_op<false>(
                        bin.dst_original,
                        call_texture_atomic_fetch_explicit(
                          res, res_h, op, is_signed,
                          ctx.builder.CreateAdd(
                            co_yield extract_element(0)(address), offset
                          ),
                          nullptr, value
                        )
                      );
                    }
                    case air::TextureKind::texture_1d:
                      co_return co_yield store_dst_op<false>(
                        bin.dst_original, call_texture_atomic_fetch_explicit(
                                            res, res_h, op, is_signed,
                                            co_yield extract_element(0)(address) >>= extend_to_int2, nullptr, value
                                          )
                      );
                    case air::TextureKind::texture_1d_array:
                      co_return co_yield store_dst_op<false>(
                        bin.dst_original,
                        call_texture_atomic_fetch_explicit(
                          res, res_h, op, is_signed, co_yield extract_element(0)(address) >>= extend_to_int2,
                          co_yield extract_element(1)(address), value
                        )
                      );
                    case air::TextureKind::texture_2d:
                      co_return co_yield store_dst_op<false>(
                        bin.dst_original,
                        call_texture_atomic_fetch_explicit(
                          res, res_h, op, is_signed,
                          co_yield truncate_vec(2)(address), nullptr, value
                        )
                      );
                    case air::TextureKind::texture_2d_array: {
                      co_return co_yield store_dst_op<false>(
                        bin.dst_original,
                        call_texture_atomic_fetch_explicit(
                          res, res_h, op, is_signed,
                          co_yield truncate_vec(2)(address),
                          co_yield extract_element(2)(address), value
                        )
                      );
                    }
                    case air::TextureKind::texture_3d:
                      co_return co_yield store_dst_op<false>(
                        bin.dst_original,
                        call_texture_atomic_fetch_explicit(
                          res, res_h, op, is_signed,
                          co_yield truncate_vec(3)(address), nullptr, value
                        )
                      );
                    default:
                      assert(0 && "invalid texture kind for typed uav atomic");
                    }

                    co_return {};
                  });
                }
              },
              bin.dst
            );
          },
          [&effect](InstAtomicImmExchange exch) {
            effect << std::visit(
              patterns{
                [=](AtomicOperandTGSM tgsm) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto ptr = co_yield read_uint_buf_addr(
                      tgsm,
                      co_yield read_atomic_index(
                        tgsm, co_yield load_src_op<false>(exch.dst_address)
                      )
                    );
                    co_return co_yield store_dst_op<false>(
                      exch.dst, air::call_atomic_exchange_explicit(
                                  ptr,
                                  co_yield load_src_op<false>(exch.src) >>=
                                  extract_element(0),
                                  false
                                )
                    );
                  });
                },
                [&](AtomicDstOperandUAV uav) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    if (ctx.resource.uav_buf_range_map.contains(uav.range_id)) {
                      auto ptr = co_yield read_uint_buf_addr(
                        uav,
                        co_yield read_atomic_index(
                          uav, co_yield load_src_op<false>(exch.dst_address)
                        )
                      );
                      co_return co_yield store_dst_op<false>(
                        exch.dst, air::call_atomic_exchange_explicit(
                                    ptr,
                                    co_yield load_src_op<false>(exch.src) >>=
                                    extract_element(0),
                                    true
                                  )
                      );
                    }
                    auto &[res, res_handle_fn, metadata] =
                      ctx.resource.uav_range_map.at(uav.range_id);
                    auto res_h = co_yield res_handle_fn(nullptr);
                    auto address =
                      co_yield load_src_op<false>(exch.dst_address);
                    auto value = co_yield load_src_op<false>(exch.src);
                    switch (res.resource_kind_logical) {
                    case air::TextureKind::texture_buffer: {
                      auto offset = co_yield metadata_get_texture_buffer_offset(
                        co_yield metadata(nullptr)
                      );
                      co_return co_yield store_dst_op<false>(
                        exch.dst,
                        call_texture_atomic_exchange(
                          res, res_h,
                          ctx.builder.CreateAdd(
                            co_yield extract_element(0)(address), offset
                          ),
                          nullptr, value
                        )
                      );
                    }
                    case air::TextureKind::texture_1d:
                      co_return co_yield store_dst_op<false>(
                        exch.dst, call_texture_atomic_exchange(
                                    res, res_h, co_yield extract_element(0)(address) >>= extend_to_int2, nullptr, value
                                  )
                      );
                    case air::TextureKind::texture_1d_array:
                      co_return co_yield store_dst_op<false>(
                        exch.dst, call_texture_atomic_exchange(
                                    res, res_h, co_yield extract_element(0)(address) >>= extend_to_int2,
                                    co_yield extract_element(1)(address), value
                                  )
                      );
                    case air::TextureKind::texture_2d:
                      co_return co_yield store_dst_op<false>(
                        exch.dst,
                        call_texture_atomic_exchange(
                          res, res_h, co_yield truncate_vec(2)(address),
                          nullptr, value
                        )
                      );
                    case air::TextureKind::texture_2d_array: {
                      co_return co_yield store_dst_op<false>(
                        exch.dst,
                        call_texture_atomic_exchange(
                          res, res_h, co_yield truncate_vec(2)(address),
                          co_yield extract_element(2)(address), value
                        )
                      );
                    }
                    case air::TextureKind::texture_3d:
                      co_return co_yield store_dst_op<false>(
                        exch.dst,
                        call_texture_atomic_exchange(
                          res, res_h, co_yield truncate_vec(3)(address),
                          nullptr, value
                        )
                      );
                    default:
                      assert(0 && "invalid texture kind for typed uav atomic");
                    }

                    co_return {};
                  });
                }
              },
              exch.dst_resource
            );
          },
          [&effect](InstAtomicImmCmpExchange cmpexch) {
            /**
            Don't define coroutine in visitor function UNLESS it has no capture
            list OTHERWISE ENJOY YOUR SEGFAULT
             */
            effect << std::visit(
              patterns{
                [=](AtomicOperandTGSM tgsm) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    auto ptr = co_yield read_uint_buf_addr(
                      tgsm,
                      co_yield read_atomic_index(
                        tgsm, co_yield load_src_op<false>(cmpexch.dst_address)
                      )
                    );
                    auto context = co_yield get_context();
                    co_return co_yield store_dst_op<false>(
                      cmpexch.dst, air::call_atomic_cmp_exchange(
                                     ptr,
                                     co_yield load_src_op<false>(cmpexch.src0
                                     ) >>= extract_element(0),
                                     co_yield load_src_op<false>(cmpexch.src1
                                     ) >>= extract_element(0),
                                     context.resource.cmp_exch_temp, false
                                   )
                    );
                  });
                },
                [&](AtomicDstOperandUAV uav) {
                  return make_effect_bind([=](struct context ctx) -> IREffect {
                    if (ctx.resource.uav_buf_range_map.contains(uav.range_id)) {
                      auto ptr = co_yield read_uint_buf_addr(
                        uav,
                        co_yield read_atomic_index(
                          uav, co_yield load_src_op<false>(cmpexch.dst_address)
                        )
                      );
                      auto context = co_yield get_context();
                      co_return co_yield store_dst_op<false>(
                        cmpexch.dst, air::call_atomic_cmp_exchange(
                                       ptr,
                                       co_yield load_src_op<false>(cmpexch.src0
                                       ) >>= extract_element(0),
                                       co_yield load_src_op<false>(cmpexch.src1
                                       ) >>= extract_element(0),
                                       context.resource.cmp_exch_temp, true
                                     )
                      );
                    }

                    auto &[res, res_handle_fn, metadata] = ctx.resource.uav_range_map.at(uav.range_id);
                    auto res_h = co_yield res_handle_fn(nullptr);
                    auto address = co_yield load_src_op<false>(cmpexch.dst_address);
                    auto src0 = co_yield load_src_op<false>(cmpexch.src0);
                    auto src1 = co_yield load_src_op<false>(cmpexch.src1);
                    switch (res.resource_kind_logical) {
                    case air::TextureKind::texture_buffer: {
                      auto offset = co_yield metadata_get_texture_buffer_offset(co_yield metadata(nullptr));
                      co_return co_yield store_dst_op<false>(
                        cmpexch.dst, call_texture_atomic_compare_exchange(
                                       res, res_h, ctx.builder.CreateAdd(co_yield extract_element(0)(address), offset),
                                       nullptr, src0, src1
                                     )
                      );
                    }
                    case air::TextureKind::texture_1d:
                      co_return co_yield store_dst_op<false>(
                        cmpexch.dst,
                        call_texture_atomic_compare_exchange(
                          res, res_h, co_yield extract_element(0)(address) >>= extend_to_int2, nullptr, src0, src1
                        )
                      );
                    case air::TextureKind::texture_1d_array:
                      co_return co_yield store_dst_op<false>(
                        cmpexch.dst, call_texture_atomic_compare_exchange(
                                       res, res_h, co_yield extract_element(0)(address) >>= extend_to_int2,
                                       co_yield extract_element(1)(address), src0, src1
                                     )
                      );
                    case air::TextureKind::texture_2d:
                      co_return co_yield store_dst_op<false>(
                        cmpexch.dst, call_texture_atomic_compare_exchange(
                                       res, res_h, co_yield truncate_vec(2)(address), nullptr, src0, src1
                                     )
                      );
                    case air::TextureKind::texture_2d_array: {
                      co_return co_yield store_dst_op<false>(
                        cmpexch.dst, call_texture_atomic_compare_exchange(
                                       res, res_h, co_yield truncate_vec(2)(address),
                                       co_yield extract_element(2)(address), src0, src1
                                     )
                      );
                    }
                    case air::TextureKind::texture_3d:
                      co_return co_yield store_dst_op<false>(
                        cmpexch.dst, call_texture_atomic_compare_exchange(
                                       res, res_h, co_yield truncate_vec(3)(address), nullptr, src0, src1
                                     )
                      );
                    default:
                      assert(0 && "invalid texture kind for typed uav atomic");
                    }

                    co_return {};
                  });
                }
              },
              cmpexch.dst_resource
            );
          },
          [&effect](InstAtomicImmIncrement alloc) {
            effect << store_dst_op<false>(
              alloc.dst, make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto ptr =
                  co_yield ctx.resource
                    .uav_counter_range_map[alloc.uav.range_id](nullptr);
                co_return co_yield air::call_atomic_fetch_explicit(
                  ptr, co_yield get_int(1), "add", false, true
                );
              })
            );
          },
          [&effect](InstAtomicImmDecrement consume) {
            effect << store_dst_op<false>(
              consume.dst,
              make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto ptr =
                  co_yield ctx.resource
                    .uav_counter_range_map[consume.uav.range_id](nullptr);
                co_return ctx.builder.CreateSub(
                  co_yield air::call_atomic_fetch_explicit(
                    ptr, co_yield get_int(1), "sub", false, true
                  ),
                  co_yield get_int(1)
                );
              })
            );
          },
          [&effect](InstBufferInfo bufinfo) {
            auto count = std::visit(
              patterns{
                [](SrcOperandResource srv) -> IRValue {
                  auto ctx = co_yield get_context();
                  if (ctx.resource.srv_buf_range_map.contains(srv.range_id)) {
                    auto &[stride, _, metadata] =
                      ctx.resource.srv_buf_range_map[srv.range_id];
                    auto byte_width = co_yield metadata_get_raw_buffer_length(
                      co_yield metadata(nullptr)
                    );
                    if (stride) {
                      co_return ctx.builder.CreateUDiv(
                        byte_width, ctx.builder.getInt32(stride)
                      );
                    }
                    co_return byte_width;
                  } else {
                    auto &[tex, srv_h, metadata] =
                      ctx.resource.srv_range_map[srv.range_id];
                    co_return co_yield metadata_get_texture_buffer_length(
                      co_yield metadata(nullptr)
                    );
                  }
                },
                [](SrcOperandUAV uav) -> IRValue {
                  auto ctx = co_yield get_context();
                  if (ctx.resource.uav_buf_range_map.contains(uav.range_id)) {
                    auto &[stride, _, metadata] =
                      ctx.resource.uav_buf_range_map[uav.range_id];
                    auto byte_width = co_yield metadata_get_raw_buffer_length(
                      co_yield metadata(nullptr)
                    );
                    if (stride) {
                      co_return ctx.builder.CreateUDiv(
                        byte_width, ctx.builder.getInt32(stride)
                      );
                    }
                    co_return byte_width;
                  } else {
                    auto &[tex, uav_h, metadata] =
                      ctx.resource.uav_range_map[uav.range_id];
                    co_return co_yield metadata_get_texture_buffer_length(
                      co_yield metadata(nullptr)
                    );
                  }
                }
              },
              bufinfo.src
            );
            effect << store_dst_op<false>(bufinfo.dst, std::move(count));
          },
          [&effect](InstResourceInfo resinfo) {
            using T = std::tuple<air::MSLTexture, pvalue, Swizzle>;
            using V = ReaderIO<struct context, T>;
            /*
            If the a per-resource mip clamp has been specified on srcResource,
            resinfo always returns the total number of mipmaps in the view for
            the mip count, regardless of the clamp.
            */
            effect
              << (std::visit(
                    patterns{
                      [](SrcOperandResource srv) -> V {
                        auto ctx = co_yield get_context();
                        assert(ctx.resource.srv_range_map.contains(srv.range_id)
                        );
                        auto &[tex, res_h, _] =
                          ctx.resource.srv_range_map[srv.range_id];
                        co_return {
                          tex, co_yield res_h(nullptr), srv.read_swizzle
                        };
                      },
                      [](SrcOperandUAV uav) -> V {
                        auto ctx = co_yield get_context();
                        assert(ctx.resource.uav_range_map.contains(uav.range_id)
                        );
                        auto &[tex, res_h, _] =
                          ctx.resource.uav_range_map[uav.range_id];
                        co_return {
                          tex, co_yield res_h(nullptr), uav.read_swizzle
                        };
                      }
                    },
                    resinfo.src_resource
                  ) >>= [=](T w) -> IREffect {
                   using namespace dxmt::air;
                   auto &[tex, res_h, swiz] = w;
                   return make_effect_bind([=](struct context ctx) -> IREffect {
                     pvalue x = nullptr;
                     pvalue y = nullptr;
                     pvalue z = nullptr;
                     pvalue zero = co_yield get_int(0);
                     pvalue miplevel =
                       co_yield load_src_op<false>(resinfo.src_mip_level) >>=
                       extract_element(0);
                     switch (tex.resource_kind_logical) {
                     case air::TextureKind::texture_1d:
                       x = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::width, miplevel
                       );
                       break;
                     case air::TextureKind::texture_1d_array:
                       x = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::width, miplevel
                       );
                       y = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::array_length, zero
                       );
                       break;
                     case air::TextureKind::texture_2d:
                     case air::TextureKind::depth_2d:
                     case air::TextureKind::texture_cube:
                     case air::TextureKind::depth_cube:
                     case air::TextureKind::texture_2d_ms:
                     case air::TextureKind::depth_2d_ms:
                       x = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::width, miplevel
                       );
                       y = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::height, miplevel
                       );
                       break;
                     case air::TextureKind::texture_2d_array:
                     case air::TextureKind::depth_2d_array:
                     case air::TextureKind::texture_cube_array:
                     case air::TextureKind::depth_cube_array:
                     case air::TextureKind::texture_2d_ms_array:
                     case air::TextureKind::depth_2d_ms_array:
                       x = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::width, miplevel
                       );
                       y = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::height, miplevel
                       );
                       z = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::array_length, zero
                       );
                       break;
                     case air::TextureKind::texture_3d:
                       x = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::width, miplevel
                       );
                       y = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::height, miplevel
                       );
                       z = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::depth, miplevel
                       );
                       break;
                     case air::TextureKind::texture_buffer: {
                       assert(0 && "resinfo: texture buffer not supported");
                       break;
                     }
                     }
                     pvalue mip_count = nullptr;
                     switch (tex.resource_kind_logical) {
                     case air::TextureKind::texture_buffer:
                     case air::TextureKind::texture_2d_ms:
                     case air::TextureKind::texture_2d_ms_array:
                     case air::TextureKind::depth_2d_ms:
                     case air::TextureKind::depth_2d_ms_array:
                       mip_count = co_yield get_int(1);
                       break;
                     default:
                       mip_count = co_yield call_get_texture_info(
                         tex, res_h, TextureInfoType::num_mip_levels, zero
                       );
                       break;
                     }
                     pvalue vec_ret = llvm::PoisonValue::get(ctx.types._float4);
                     vec_ret = ctx.builder.CreateInsertElement(
                       vec_ret,
                       resinfo.modifier == InstResourceInfo::M::uint
                         ? ctx.builder.CreateBitCast(
                             mip_count, ctx.types._float
                           )
                         : co_yield call_convert(
                             mip_count, ctx.types._float, air::Sign::no_sign
                           ),
                       3
                     );
                     switch (resinfo.modifier) {
                     case InstResourceInfo::M::none: {
                       auto to_float_cast = [&](pvalue v) -> IRValue {
                         co_return (co_yield call_convert(
                           v, ctx.types._float, air::Sign::no_sign
                         ));
                       };
                       x = co_yield to_float_cast(x ? x : zero);
                       y = co_yield to_float_cast(y ? y : zero);
                       z = co_yield to_float_cast(z ? z : zero);
                       break;
                     }
                     case InstResourceInfo::M::uint: {
                       x = ctx.builder.CreateBitCast(
                         x ? x : zero, ctx.types._float
                       );
                       y = ctx.builder.CreateBitCast(
                         y ? y : zero, ctx.types._float
                       );
                       z = ctx.builder.CreateBitCast(
                         z ? z : zero, ctx.types._float
                       );
                       break;
                     }
                     case InstResourceInfo::M::rcp: {
                       auto to_rcp_cast = [&](pvalue v) -> IRValue {
                         co_return (ctx.builder.CreateFDiv(
                           co_yield get_float(1.0f),
                           co_yield call_convert(
                             v, ctx.types._float, air::Sign::no_sign
                           )
                         ));
                       };
                       x = co_yield to_rcp_cast(x ? x : zero);
                       y = co_yield to_rcp_cast(y ? y : zero);
                       z = co_yield to_rcp_cast(z ? z : zero);
                       break;
                     }
                     }

                     vec_ret =
                       ctx.builder.CreateInsertElement(vec_ret, x, (uint64_t)0);
                     vec_ret = ctx.builder.CreateInsertElement(vec_ret, y, 1);
                     vec_ret = ctx.builder.CreateInsertElement(vec_ret, z, 2);
                     co_return co_yield store_dst_op<true>(
                       resinfo.dst, swizzle(swiz)(vec_ret)
                     );
                   });
                 });
          },
          [&](InstSampleInfo sample_info) {
            effect << make_effect_bind([=](struct context ctx) -> IREffect {
              using namespace dxmt::air;
              pvalue sample_count = nullptr;
              dxbc::Swizzle swiz = swizzle_identity;
              if (sample_info.src.has_value()) {
                auto &[tex, res_h, _] = ctx.resource.srv_range_map[sample_info.src->range_id];
                sample_count = co_yield call_get_texture_info(
                  tex, co_yield res_h(nullptr), TextureInfoType::num_samples, ctx.builder.getInt32(0)
                );
                swiz = sample_info.src->read_swizzle;
              } else {
                sample_count = co_yield call_get_num_samples();
              }

              if (sample_info.uint_result) {
                pvalue vec_ret = ctx.builder.CreateInsertElement(
                  llvm::ConstantAggregateZero::get(ctx.types._int4), sample_count, (uint64_t)0
                );
                co_return co_yield store_dst_op<false>(sample_info.dst, swizzle(swiz)(vec_ret));
              } else {
                pvalue vec_ret = ctx.builder.CreateInsertElement(
                  llvm::ConstantAggregateZero::get(ctx.types._float4),
                  co_yield call_convert(sample_count, ctx.types._float, air::Sign::no_sign),
                  (uint64_t)0
                );
                co_return co_yield store_dst_op<true>(sample_info.dst, swizzle(swiz)(vec_ret));
              }
            });
          },
          [&](InstSamplePos sample_pos) {
            // FIXME: stub
            effect << store_dst_op<true>(sample_pos.dst, make_irvalue([](struct context s) {
              return llvm::ConstantAggregateZero::get(s.types._float4);
            }));
          },
          [&](InstInterpolateCentroid eval_centroid) {
            assert(0 && "unhandled eval_centroid"); // remove this line if it's verified to work
            effect << store_dst_op<true>(
              eval_centroid.dst,
              make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto desc = ctx.resource.interpolant_map.at(eval_centroid.regid);
                co_return co_yield air::call_interpolate_at_centroid(
                  co_yield desc.interpolant(nullptr), desc.perspective
                );
              }) >>= swizzle(eval_centroid.read_swizzle)
            );
          },
          [&](InstInterpolateSample eval_sample_index) {
            effect << store_dst_op<true>(
              eval_sample_index.dst,
              make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto desc = ctx.resource.interpolant_map.at(eval_sample_index.regid);
                co_return co_yield air::call_interpolate_at_sample(
                  co_yield desc.interpolant(nullptr), desc.perspective,
                  co_yield load_src_op<false>(eval_sample_index.sample_index) >>= extract_element(0)
                );
              }) >>= swizzle(eval_sample_index.read_swizzle)
            );
          },
          [&](InstInterpolateOffset eval_snapped) {
            assert(0 && "unhandled eval_snapped"); // remove this line if it's verified to work
            effect << store_dst_op<true>(
              eval_snapped.dst,
              make_irvalue_bind([=](struct context ctx) -> IRValue {
                auto desc = ctx.resource.interpolant_map.at(eval_snapped.regid);
                auto offset = co_yield load_src_op<false>(eval_snapped.offset);
                // truncated = (offset.xy + 8) & 0b1111
                auto truncated = ctx.builder.CreateAnd(
                  ctx.builder.CreateAdd(
                    ctx.builder.CreateShuffleVector(offset, {0, 1}),
                    co_yield get_int2(8, 8)
                  ),
                  co_yield get_int2(0b1111, 0b1111)
                );
                auto offset_f = ctx.builder.CreateFMul(
                  co_yield call_convert(
                    truncated, ctx.types._float, air::Sign::no_sign
                  ),
                  co_yield get_float2(1.0f / 16.0f, 1.0f / 16.0f)
                );
                co_return co_yield air::call_interpolate_at_offset(
                  co_yield desc.interpolant(nullptr), desc.perspective, offset_f
                );
              }) >>= swizzle(eval_snapped.read_swizzle)
            );
          },
          [&](InstEmit) { effect << ctx.resource.call_emit(); },
          [&](InstCut) { effect << ctx.resource.call_cut(); },
        },
        inst
      );
    }
    builder.SetInsertPoint(bb);
    if (auto err = effect.build(ctx).takeError()) {
      return err;
    }
    if (auto err = std::visit(
          patterns{
            [](BasicBlockUndefined) -> llvm::Error {
              return llvm::make_error<UnsupportedFeature>(
                "unexpected undefined basicblock"
              );
            },
            [&](BasicBlockUnconditionalBranch uncond) -> llvm::Error {
              auto target_bb = visited[uncond.target.get()];
              builder.CreateBr(target_bb);
              return llvm::Error::success();
            },
            [&](BasicBlockConditionalBranch cond) -> llvm::Error {
              auto target_true_bb = visited[cond.true_branch.get()];
              auto target_false_bb = visited[cond.false_branch.get()];
              auto test =
                load_condition(cond.cond.operand, cond.cond.test_nonzero)
                  .build(ctx);
              if (auto err = test.takeError()) {
                return err;
              }
              builder.CreateCondBr(test.get(), target_true_bb, target_false_bb);
              return llvm::Error::success();
            },
            [&](BasicBlockSwitch swc) -> llvm::Error {
              auto value =
                (load_src_op<false>(swc.value) >>= extract_element(0))
                  .build(ctx);
              if (auto err = value.takeError()) {
                return err;
              }
              auto switch_inst = builder.CreateSwitch(
                value.get(), visited[swc.case_default.get()], swc.cases.size()
              );
              for (auto &[val, case_bb] : swc.cases) {
                switch_inst->addCase(
                  llvm::ConstantInt::get(context, llvm::APInt(32, val)),
                  visited[case_bb.get()]
                );
              }
              return llvm::Error::success();
            },
            [&](BasicBlockReturn ret) -> llvm::Error {
              // we have done here!
              // unconditional jump to return bb
              builder.CreateBr(return_bb);
              return llvm::Error::success();
            },
            [&](BasicBlockInstanceBarrier instance) -> llvm::Error {
              auto target_true_bb = visited[instance.active.get()];
              auto target_false_bb = visited[instance.sync.get()];
              builder.CreateCondBr(
                ctx.builder.CreateICmp(
                  llvm::CmpInst::ICMP_ULT,
                  ctx.resource.thread_id_in_patch,
                  ctx.builder.getInt32(instance.instance_count)
                ),
                target_true_bb, target_false_bb
              );
              return llvm::Error::success();
            },
            [&](BasicBlockHullShaderWriteOutput hull_end) -> llvm::Error {
              auto active = llvm::BasicBlock::Create(
                context, "write_control_point", function
              );
              auto sync = llvm::BasicBlock::Create(
                context, "write_control_point_end", function
              );

              builder.CreateCondBr(
                ctx.builder.CreateICmp(
                  llvm::CmpInst::ICMP_ULT,
                  ctx.resource.thread_id_in_patch,
                  ctx.builder.getInt32(hull_end.instance_count)
                ),
                active, sync
              );
              builder.SetInsertPoint(active);
              auto dst_ptr = builder.CreateGEP(
                ctx.types._int4, ctx.resource.control_point_buffer,
                {builder.CreateMul(
                  ctx.resource.instanced_patch_id, builder.getInt32(
                                           ctx.resource.output_element_count *
                                           hull_end.instance_count
                                         )
                )}
              );
              for (unsigned i = 0; i < ctx.resource.output_element_count; i++) {
                auto dst_ptr_offset = builder.CreateGEP(
                  ctx.types._int4, dst_ptr,
                  {builder.CreateAdd(
                    builder.CreateMul(
                      ctx.resource.thread_id_in_patch,
                      builder.getInt32(ctx.resource.output_element_count)
                    ),
                    builder.getInt32(i)
                  )}
                );
                auto src_ptr_offset = builder.CreateGEP(
                  llvm::ArrayType::get(ctx.types._int4, ctx.resource.output_element_count * hull_end.instance_count),
                  ctx.resource.output.ptr_int4,
                  {builder.getInt32(0),
                   builder.CreateAdd(
                     builder.CreateMul(
                       ctx.resource.thread_id_in_patch,
                       builder.getInt32(ctx.resource.output_element_count)
                     ),
                     builder.getInt32(i)
                   )}
                );
                builder.CreateStore(
                  builder.CreateLoad(ctx.types._int4, src_ptr_offset),
                  dst_ptr_offset
                );
              }
              builder.CreateBr(sync);
              builder.SetInsertPoint(sync);
              if (auto err = call_threadgroup_barrier(mem_flags::threadgroup)
                               .build(ctx)
                               .takeError()) {
                return err;
              }

              auto target_bb = visited[hull_end.epilogue.get()];
              builder.CreateBr(target_bb);
              return llvm::Error::success();
            },
          },
          current->target
        )) {
      return err;
    };
  };
  assert(visited.count(entry.get()) == 1);
  builder.SetInsertPoint(bb_pop);
  return visited[entry.get()];
}

} // namespace dxmt::dxbc

template <>
struct environment_cast<::dxmt::dxbc::context, ::dxmt::air::AIRBuilderContext> {
  ::dxmt::air::AIRBuilderContext cast(const ::dxmt::dxbc::context &src) {
    return {src.llvm, src.module, src.builder, src.types};
  };
};