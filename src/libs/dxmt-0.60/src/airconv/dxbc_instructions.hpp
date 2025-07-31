#pragma once
#include "DXBCParser/ShaderBinary.h"
#include "shader_common.hpp"
#include <array>
#include <map>
#include <string_view>
#include <variant>

#define DXASSERT_DXBC(x) assert(x);

namespace dxmt::dxbc {

struct Swizzle {
  union {
    uint8_t x;
    uint8_t r;
  };
  union {
    uint8_t y;
    uint8_t g;
  };
  union {
    uint8_t z;
    uint8_t b;
  };
  union {
    uint8_t w;
    uint8_t a;
  };
  bool operator==(Swizzle const &o) const {
    return r == o.r && g == o.g && b == o.b && a == o.a;
  };

  operator std::array<int, 4>() const { return std::array<int, 4>{x, y, z, w}; }
};

constexpr Swizzle swizzle_identity = {0, 1, 2, 3};

#pragma region operand index
struct IndexByTempComponent {
  uint32_t regid;
  uint32_t phase;
  uint8_t component;
  uint32_t offset;
};

struct IndexByIndexableTempComponent {
  uint32_t regfile;
  uint32_t regid;
  uint32_t phase;
  uint8_t component;
  uint32_t offset;
};

using OperandIndex =
  std::variant<uint32_t, IndexByIndexableTempComponent, IndexByTempComponent>;
#pragma endregion

#pragma region source operand

struct SrcOperandModifier {
  Swizzle swizzle;
  bool abs;
  bool neg;
};

struct SrcOperandImmediate32 {
  static constexpr std::string_view debug_name = "immediate_32";
  union {
    int32_t ivalue[4];
    uint32_t uvalue[4];
    float fvalue[4];
  };
};

struct SrcOperandTemp {
  static constexpr std::string_view debug_name = "temp";
  SrcOperandModifier _;
  uint32_t regid;
  uint32_t phase;
};

struct SrcOperandIndexableTemp {
  static constexpr std::string_view debug_name = "indexable_temp";
  SrcOperandModifier _;
  uint32_t regfile;
  OperandIndex regindex;
  uint32_t phase;
};

struct SrcOperandInput {
  static constexpr std::string_view debug_name = "input";
  SrcOperandModifier _;
  uint32_t regid;
};

struct SrcOperandInputOCP {
  static constexpr std::string_view debug_name = "input_vocp";
  SrcOperandModifier _;
  OperandIndex cpid;
  uint32_t regid;
};

struct SrcOperandInputICP {
  static constexpr std::string_view debug_name = "input_vicp";
  SrcOperandModifier _;
  OperandIndex cpid;
  uint32_t regid;
};

struct SrcOperandInputPC {
  static constexpr std::string_view debug_name = "input_vpc";
  SrcOperandModifier _;
  OperandIndex regindex;
};

struct SrcOperandIndexableInput {
  static constexpr std::string_view debug_name = "indexable_input";
  SrcOperandModifier _;
  OperandIndex regindex;
};

struct SrcOperandConstantBuffer {
  static constexpr std::string_view debug_name = "constant_buffer";
  SrcOperandModifier _;
  uint32_t rangeid;
  OperandIndex rangeindex;
  OperandIndex regindex;
};

struct SrcOperandImmediateConstantBuffer {
  static constexpr std::string_view debug_name = "immediate_constant_buffer";
  SrcOperandModifier _;
  OperandIndex regindex;
};

struct SrcOperandAttribute {
  static constexpr std::string_view debug_name = "attribute";
  SrcOperandModifier _;
  shader::common::InputAttribute attribute;
};

#pragma endregion

#pragma region dest operand

struct DstOperandCommon {
  uint32_t mask;
};

struct DstOperandNull {
  static constexpr std::string_view debug_name = "null";
  DstOperandCommon _;
};

struct DstOperandSideEffect {
  static constexpr std::string_view debug_name = "side_effect";
  DstOperandCommon _;
};

struct DstOperandTemp {
  static constexpr std::string_view debug_name = "temp";
  DstOperandCommon _;
  uint32_t regid;
  uint32_t phase;
};

struct DstOperandIndexableTemp {
  static constexpr std::string_view debug_name = "indexable_temp";
  DstOperandCommon _;
  uint32_t regfile;
  OperandIndex regindex;
  uint32_t phase;
};

struct DstOperandOutput {
  static constexpr std::string_view debug_name = "output";
  DstOperandCommon _;
  uint32_t regid;
  uint32_t phase;
};

struct DstOperandIndexableOutput {
  static constexpr std::string_view debug_name = "indexable_output";
  DstOperandCommon _;
  OperandIndex regindex;
  uint32_t phase;
};

struct DstOperandOutputDepth {
  static constexpr std::string_view debug_name = "odepth";
  DstOperandCommon _;
};

struct DstOperandOutputCoverageMask {
  static constexpr std::string_view debug_name = "omask";
  DstOperandCommon _;
};

#pragma endregion

#pragma region instructions
struct InstructionCommon {
  bool saturate;
};

struct DclConstantBuffer {};

struct DclResource {};

struct DclRawResource {};

struct DclStructuredResource {};

struct DclInput {};

struct DclOutput {};

#pragma endregion

using SrcOperand = std::variant<
  SrcOperandImmediate32, SrcOperandTemp, SrcOperandIndexableTemp,
  SrcOperandInput, SrcOperandConstantBuffer, SrcOperandImmediateConstantBuffer,
  SrcOperandAttribute, SrcOperandIndexableInput, SrcOperandInputICP,
  SrcOperandInputOCP, SrcOperandInputPC>;

struct SrcOperandResource {
  uint32_t range_id;
  OperandIndex index;
  Swizzle read_swizzle;
};

struct SrcOperandSampler {
  uint32_t range_id;
  OperandIndex index;
  uint8_t gather_channel;
};

struct SrcOperandUAV {
  uint32_t range_id;
  OperandIndex index;
  Swizzle read_swizzle;
};

struct SrcOperandTGSM {
  uint32_t id;
  Swizzle read_swizzle;
};

struct AtomicDstOperandUAV {
  uint32_t range_id;
  OperandIndex index;
  uint32_t mask;
};

/**
It's a UAV implemented as metal buffer
*/
struct AtomicDstOperandUAVBuffer {
  uint32_t range_id;
  OperandIndex index;
  uint32_t mask;
};

struct AtomicOperandTGSM {
  uint32_t id;
  uint32_t mask;
};

using DstOperand = std::variant<
  DstOperandNull, DstOperandSideEffect, DstOperandTemp, DstOperandIndexableTemp,
  DstOperandOutput, DstOperandOutputDepth, DstOperandOutputCoverageMask,
  DstOperandIndexableOutput>;

#pragma mark mov instructions

struct InstMov {
  InstructionCommon _;
  DstOperand dst;
  SrcOperand src;
};

struct InstMovConditional {
  InstructionCommon _;
  DstOperand dst;
  SrcOperand src_cond;
  SrcOperand src0;
  SrcOperand src1;
};

struct InstSwapConditional {
  DstOperand dst0;
  DstOperand dst1;
  SrcOperand src_cond;
  SrcOperand src0;
  SrcOperand src1;
};

#pragma mark resource instructions

struct InstSample {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  SrcOperandSampler src_sampler;
  int32_t offsets[3];
  std::optional<SrcOperand> min_lod_clamp;
  std::optional<DstOperand> feedback;
};

struct InstSampleCompare {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource; // swizzle must be .r channel
  SrcOperandSampler src_sampler;
  SrcOperand src_reference;
  int32_t offsets[3];
  std::optional<SrcOperand> min_lod_clamp;
  std::optional<DstOperand> feedback;
  bool level_zero;
};

struct InstSampleBias {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  SrcOperandSampler src_sampler;
  SrcOperand src_bias;
  int32_t offsets[3];
};

struct InstSampleDerivative {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  SrcOperandSampler src_sampler;
  SrcOperand src_x_derivative;
  SrcOperand src_y_derivative;
  int32_t offsets[3];
};

struct InstSampleLOD {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  SrcOperandSampler src_sampler;
  SrcOperand src_lod;
  int32_t offsets[3];
};

struct InstGather {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  SrcOperandSampler src_sampler;
  SrcOperand offset;
  std::optional<DstOperand> feedback;
};

struct InstGatherCompare {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  SrcOperandSampler src_sampler;
  SrcOperand src_reference; // single component
  SrcOperand offset;
  std::optional<DstOperand> feedback;
};

struct InstBufferInfo {
  DstOperand dst;
  std::variant<SrcOperandResource, SrcOperandUAV> src;
};

struct InstResourceInfo {
  DstOperand dst;
  SrcOperand src_mip_level;
  std::variant<SrcOperandResource, SrcOperandUAV> src_resource;
  enum class M { none, uint, rcp } modifier;
};
struct InstSampleInfo {
  DstOperand dst;
  std::optional<SrcOperandResource> src; // if null, get rasterizer info
  bool uint_result;
};

struct InstSamplePos {
  DstOperand dst;
  std::optional<SrcOperandResource> src; // if null, get rasterizer info
  SrcOperand src_sample_index;
};

struct InstLoad {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  std::optional<SrcOperand> src_sample_index;
  int32_t offsets[3];
};

struct InstLoadRaw {
  DstOperand dst;
  SrcOperand src_byte_offset;
  std::variant<SrcOperandResource, SrcOperandUAV, SrcOperandTGSM> src;
  // then we can load 4 components at once
  bool opt_flag_offset_is_vec4_aligned = false;
};

struct InstLoadStructured {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperand src_byte_offset;
  std::variant<SrcOperandResource, SrcOperandUAV, SrcOperandTGSM> src;
  // 2nd condtion: stride is also vec4 aligned
  bool opt_flag_offset_is_vec4_aligned = false;
};

struct InstLoadUAVTyped {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandUAV src_uav;
};

struct InstStoreUAVTyped {
  AtomicDstOperandUAV dst;
  SrcOperand src_address;
  SrcOperand src;
};

struct InstStoreRaw {
  std::variant<AtomicDstOperandUAV, AtomicOperandTGSM> dst;
  SrcOperand dst_byte_offset;
  SrcOperand src;
};

struct InstStoreStructured {
  std::variant<AtomicDstOperandUAV, AtomicOperandTGSM> dst;
  SrcOperand dst_address;
  SrcOperand dst_byte_offset;
  SrcOperand src;
};

struct InstDotProduct {
  InstructionCommon _;
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
  uint8_t dimension;
};

struct InstSinCos {
  InstructionCommon _;
  DstOperand dst_sin;
  DstOperand dst_cos;
  SrcOperand src;
};

enum class FloatComparison { Equal, NotEqual, GreaterEqual, LessThan };

struct InstFloatCompare {
  InstructionCommon _;
  FloatComparison cmp;
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
};

enum class IntegerComparison {
  Equal,
  NotEqual,
  SignedLessThan,
  SignedGreaterEqual,
  UnsignedLessThan,
  UnsignedGreaterEqual,
};

struct InstIntegerCompare {
  IntegerComparison cmp;
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
};

enum class FloatBinaryOp {
  Add,
  Mul,
  Div,
  Min,
  Max,
};

struct InstFloatBinaryOp {
  InstructionCommon _;
  FloatBinaryOp op;
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
};

enum class FloatUnaryOp {
  Log2,
  Exp2,
  Rcp,
  Rsq,
  Sqrt,
  Fraction,
  RoundNearestEven,
  RoundNegativeInf,
  RoundPositiveInf,
  RoundZero,
};

struct InstFloatUnaryOp {
  InstructionCommon _;
  FloatUnaryOp op;
  DstOperand dst;
  SrcOperand src;
};

enum class IntegerUnaryOp {
  Neg,
  Not,
  ReverseBits,
  CountBits,
  FirstHiBitSigned,
  FirstHiBit,
  FirstLowBit,
};

struct InstIntegerUnaryOp {
  IntegerUnaryOp op;
  DstOperand dst;
  SrcOperand src;
};

struct InstFloatMAD {
  InstructionCommon _;
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
  SrcOperand src2;
};

struct InstIntegerMAD {
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
  SrcOperand src2;
  bool is_signed;
};

struct InstMaskedSumOfAbsDiff {
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
  SrcOperand src2;
};

enum class IntegerBinaryOp {
  UMin,
  UMax,
  IMin,
  IMax,
  IShl,
  IShr,
  UShr,
  Xor,
  Or,
  And,
  Add,
};

struct InstIntegerBinaryOp {
  IntegerBinaryOp op;
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
};

enum class IntegerBinaryOpWithTwoDst {
  IMul,
  UAddCarry,
  UDiv,
  UMul,
  USubBorrow,
};

struct InstIntegerBinaryOpWithTwoDst {
  InstructionCommon _;
  IntegerBinaryOpWithTwoDst op;
  union {
    DstOperand dst_hi;
    DstOperand dst_quot;
  };
  union {
    DstOperand dst_low;
    DstOperand dst_rem;
    DstOperand dst_carry; // 1 if a carry is produced, 0 otherwise.
  };
  SrcOperand src0;
  SrcOperand src1;
};

struct InstExtractBits {
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
  SrcOperand src2;
  bool is_signed;
};

struct InstBitFiledInsert {
  DstOperand dst;
  SrcOperand src0;
  SrcOperand src1;
  SrcOperand src2;
  SrcOperand src3;
};

enum class ConversionOp {
  HalfToFloat,
  FloatToHalf,
  FloatToSigned,
  SignedToFloat,
  FloatToUnsigned,
  UnsignedToFloat,
};

struct InstConvert {
  ConversionOp op;
  DstOperand dst;
  SrcOperand src;
};

struct InstNop {};

struct InstPixelDiscard {};

struct InstPartialDerivative {
  InstructionCommon _;
  DstOperand dst;
  SrcOperand src;
  bool ddy;    // use ddx if false
  bool coarse; // use fine if false
};

struct InstCalcLOD {
  DstOperand dst;
  SrcOperand src_address;
  SrcOperandResource src_resource;
  SrcOperandSampler src_sampler;
};

struct InstSync {
  enum class UAVBoundary { global, group, none } uav_boundary;
  bool tgsm_memory_barrier;
  // unfortunately in Metal we always apply an execution barrier
  bool tgsm_execution_barrier;
};

enum class AtomicBinaryOp { And, Or, Xor, Add, IMax, IMin, UMax, UMin };

struct InstAtomicBinOp {
  AtomicBinaryOp op;
  std::variant<AtomicDstOperandUAV, AtomicOperandTGSM> dst;
  SrcOperand dst_address;
  SrcOperand src;          // select one component
  DstOperand dst_original; // store single component, original value
};

struct InstAtomicImmIncrement {
  DstOperand dst; // store single component, original value
  AtomicDstOperandUAV uav;
};

struct InstAtomicImmDecrement {
  DstOperand dst; // store single component, new value
  AtomicDstOperandUAV uav;
};

struct InstAtomicImmExchange {
  DstOperand dst; // store single component, original value
  std::variant<AtomicDstOperandUAV, AtomicOperandTGSM> dst_resource;
  SrcOperand dst_address;
  SrcOperand src; // select one component
};

struct InstAtomicImmCmpExchange {
  DstOperand dst; // store single component, original value,
  // note it's always written
  std::variant<AtomicDstOperandUAV, AtomicOperandTGSM> dst_resource;
  SrcOperand dst_address;
  SrcOperand src0; // select one component
  SrcOperand src1; // select one component
};

struct InstInterpolateCentroid {
  DstOperand dst;
  uint32_t regid;
  Swizzle read_swizzle;
};

struct InstInterpolateSample {
  DstOperand dst;
  SrcOperand sample_index;
  uint32_t regid;
  Swizzle read_swizzle;
};

struct InstInterpolateOffset {
  DstOperand dst;
  SrcOperand offset;
  uint32_t regid;
  Swizzle read_swizzle;
};

struct InstEmit {};

struct InstCut {};

using Instruction = std::variant<
  /* Generic */
  InstMov, InstMovConditional, InstSwapConditional,                      //
  InstDotProduct, InstSinCos,                                            //
  InstConvert,                                                           //
  InstIntegerCompare, InstFloatCompare,                                  //
  InstFloatBinaryOp, InstIntegerBinaryOp, InstIntegerBinaryOpWithTwoDst, //
  InstFloatUnaryOp, InstIntegerUnaryOp,                                  //
  InstFloatMAD, InstIntegerMAD, InstMaskedSumOfAbsDiff,                  //
  InstExtractBits, InstBitFiledInsert,                                   //
  InstSample, InstSampleCompare, InstGather, InstGatherCompare,          //
  InstSampleBias, InstSampleDerivative, InstSampleLOD,                   //
  InstSamplePos, InstSampleInfo, InstBufferInfo, InstResourceInfo,       //
  InstLoad, InstLoadUAVTyped, InstStoreUAVTyped,                         //
  InstLoadRaw, InstLoadStructured, InstStoreRaw, InstStoreStructured,    //
  InstNop,
  /* Geometry Shader */
  InstEmit, InstCut,
  /* Pixel Shader */
  InstPixelDiscard, InstPartialDerivative, InstCalcLOD,
  InstInterpolateCentroid, InstInterpolateSample, InstInterpolateOffset,
  /* Atomics */
  InstSync, InstAtomicBinOp,                       //
  InstAtomicImmCmpExchange, InstAtomicImmExchange, //
  InstAtomicImmIncrement, InstAtomicImmDecrement>;

#pragma region basicblock

class BasicBlock;

struct BasicBlockCondition {
  SrcOperand operand;
  bool test_nonzero; //
};

struct BasicBlockConditionalBranch {
  BasicBlockCondition cond;
  std::shared_ptr<BasicBlock> true_branch;
  std::shared_ptr<BasicBlock> false_branch;
};

struct BasicBlockUnconditionalBranch {
  std::shared_ptr<BasicBlock> target;
};

struct BasicBlockHullShaderWriteOutput {
  uint32_t instance_count;
  std::shared_ptr<BasicBlock> epilogue;
};

struct BasicBlockSwitch {
  SrcOperand value;
  std::map<uint32_t, std::shared_ptr<BasicBlock>> cases;
  std::shared_ptr<BasicBlock> case_default;
};

struct BasicBlockReturn {};

struct BasicBlockUndefined {};

struct BasicBlockInstanceBarrier {
  uint32_t instance_count;
  std::shared_ptr<BasicBlock> active;
  std::shared_ptr<BasicBlock> sync;
};

using BasicBlockTarget = std::variant<
  BasicBlockConditionalBranch, BasicBlockUnconditionalBranch, BasicBlockSwitch,
  BasicBlockReturn, BasicBlockInstanceBarrier, BasicBlockHullShaderWriteOutput,
  BasicBlockUndefined>;

class BasicBlock {
public:
  std::vector<Instruction> instructions;
  BasicBlockTarget target;
  std::string debug_name;

  BasicBlock(const char *name)
      : instructions(), target(BasicBlockUndefined{}), debug_name(name) {}
};

#pragma endregion

SrcOperand readSrcOperand(
  const microsoft::D3D10ShaderBinary::COperandBase &O, uint32_t phase
);
BasicBlockCondition readCondition(
  const microsoft::D3D10ShaderBinary::CInstruction &Inst, uint32_t OpIdx,
  uint32_t phase
);
} // namespace dxmt::dxbc