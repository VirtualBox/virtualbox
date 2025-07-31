// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once
#include "d3d12tokenizedprogramformat.hpp"
#include "mind3dcommon.h"
#include "minwindef.h"
#include <cstddef>

namespace microsoft {

class CSignatureParser;
class CSignatureParser5;

UINT32 DXBCGetSizeAssumingValidPointer(const void *pDXBC);
HRESULT DXBCGetInputSignature(const void *pBlobContainer,
                              CSignatureParser *pParserToUse,
                              bool bForceStringReference = false);
HRESULT DXBCGetOutputSignature(const void *pBlobContainer,
                               CSignatureParser *pParserToUse,
                               bool bForceStringReference = false);
HRESULT DXBCGetOutputSignature(const void *pBlobContainer,
                               CSignatureParser5 *pParserToUse,
                               bool bForceStringReference = false);
HRESULT DXBCGetPatchConstantSignature(const void *pBlobContainer,
                                      CSignatureParser *pParserToUse,
                                      bool bForceStringReference = false);

//=================================================================================================================================
//
// Signature Parser (not an encoder though)
//
//---------------------------------------------------------------------------------------------------------------------------------

typedef struct D3D11_SIGNATURE_PARAMETER {
  UINT Stream;
  char *SemanticName;
  UINT SemanticIndex;
  D3D10_SB_NAME SystemValue; // Internally defined enumeration
  D3D10_SB_REGISTER_COMPONENT_TYPE ComponentType;
  UINT Register;
  BYTE Mask; // (D3D10_SB_OPERAND_4_COMPONENT_MASK >> 4), meaning 4 LSBs are
             // xyzw respectively.

  // The following unioned fields, NeverWrites_Mask and AlwaysReads_Mask, are
  // exclusively used for output signatures or input signatures, respectively.
  //
  // For an output signature, NeverWrites_Mask indicates that the shader the
  // signature belongs to never writes to the masked components of the output
  // register.  Meaningful bits are the ones set in Mask above.
  //
  // For an input signature, AlwaysReads_Mask indicates that the shader the
  // signature belongs to always reads the masked components of the input
  // register.  Meaningful bits are the ones set in the Mask above.
  //
  // This allows many shaders to share similar signatures even though some of
  // them may not happen to use all of the inputs/outputs - something which may
  // not be obvious when authored.  The NeverWrites_Mask and AlwaysReads_Mask
  // can be checked in a debug layer at runtime for the one interesting case:
  // that a shader that always reads a value is fed by a shader that always
  // writes it.  Cases where shaders may read values or may not cannot be
  // validated unfortunately.
  //
  // In scenarios where a signature is being passed around standalone (so it
  // isn't tied to input or output of a given shader), this union can be zeroed
  // out in the absence of more information.  This effectively forces off
  // linkage validation errors with the signature, since if interpreted as a
  // input or output signature somehow, since the meaning on output would be
  // "everything is always written" and on input it would be "nothing is always
  // read".
  union {
    BYTE NeverWrites_Mask; // For an output signature, the shader the signature
                           // belongs to never writes the masked components of
                           // the output register.
    BYTE AlwaysReads_Mask; // For an input signature, the shader the signature
                           // belongs to always reads the masked components of
                           // the input register.
  };

  D3D_MIN_PRECISION MinPrecision;
} D3D11_SIGNATURE_PARAMETER;

class CSignatureParser {
  friend class CSignatureParser5;

public:
  CSignatureParser() { Init(); }
  ~CSignatureParser() { Cleanup(); }
  HRESULT ReadSignature11_1(D3D11_SIGNATURE_PARAMETER *pParamList,
                            UINT *pCharSums,
                            UINT NumParameters); // returns S_OK or E_FAIL
  HRESULT ReadSignature11_1(
      const void *pSignature, UINT BlobSize,
      bool bForceStringReference = false); // returns S_OK or E_FAIL
  HRESULT
  ReadSignature4(const void *pSignature, UINT BlobSize,
                 bool bForceStringReference = false); // returns S_OK or E_FAIL
  HRESULT ReadSignature5(D3D11_SIGNATURE_PARAMETER *pParamList, UINT *pCharSums,
                         UINT NumParameters); // returns S_OK or E_FAIL
  UINT GetParameters(D3D11_SIGNATURE_PARAMETER const **ppParameters) const;
  // Returns element count and array of parameters.  Returned memory is
  // deleted when the class is destroyed or ReadSignature() is called again.

  UINT GetNumParameters() const { return m_cParameters; }
  HRESULT FindParameter(LPCSTR SemanticName, UINT SemanticIndex,
                        D3D11_SIGNATURE_PARAMETER **pFoundParameter) const;
  // Returned memory is deleted when the class is destroyed or ReadSignature is
  // called again. Returns S_OK if found, E_FAIL if not found, E_OUTOFMEMORY if
  // out of memory

  HRESULT FindParameterRegister(LPCSTR SemanticName, UINT SemanticIndex,
                                UINT *pFoundParameterRegister);
  // Returns S_OK if found, E_FAIL if not found, E_OUTOFMEMORY if out of memory

  UINT GetSemanticNameCharSum(
      UINT parameter); // Get the character sum for the name of a parameter to
                       // speed comparisons.

  bool CanOutputTo(CSignatureParser *pTargetSignature);
  // (1) Target signature must be identical to beginning of source signature
  // (source signature can have
  //     more entries at the end)
  // (2) CanOutputTo also accounts for if the target always reads some of the
  // values but the source never
  //     writes them. This can arise when signatures are being reused with many
  //     shaders, but some don't use all the inputs/outputs (the signatures
  //     still remain intact).

  void ClearAlwaysReadsNeverWritesMask();
  // Clear out AlwaysReads_Mask / NeverWrites_Mask for all elements in the
  // signature to force the signature not to cause linkage errors when
  // passing the signature around after extracting it from a shader.
private:
  void Init();
  void Cleanup();

  D3D11_SIGNATURE_PARAMETER *m_pSignatureParameters;
  UINT *m_pSemanticNameCharSums;
  UINT m_cParameters;
  BOOL m_OwnParameters;
};

class CSignatureParser5 {
public:
  CSignatureParser5() { Init(); }
  ~CSignatureParser5() { Cleanup(); }
  HRESULT ReadSignature11_1(
      const void *pSignature, UINT BlobSize,
      bool bForceStringReference = false); // returns S_OK or E_FAIL
  HRESULT
  ReadSignature5(const void *pSignature, UINT BlobSize,
                 bool bForceStringReference = false); // returns S_OK or E_FAIL
  HRESULT
  ReadSignature4(const void *pSignature, UINT BlobSize,
                 bool bForceStringReference = false); // returns S_OK or E_FAIL
  UINT NumStreams() { return m_NumSigs; }
  UINT GetTotalParameters() const { return m_cParameters; }
  const CSignatureParser *Signature(UINT stream) const {
    return &m_Sig[stream];
  }
  const CSignatureParser *RastSignature() const {
    return (m_RasterizedStream < D3D11_SO_STREAM_COUNT)
               ? &m_Sig[m_RasterizedStream]
               : NULL;
  }
  void SetRasterizedStream(UINT stream) { m_RasterizedStream = stream; }
  UINT RasterizedStream() { return m_RasterizedStream; };

private:
  void Init();
  void Cleanup();

  D3D11_SIGNATURE_PARAMETER *m_pSignatureParameters;
  UINT *m_pSemanticNameCharSums;
  UINT m_cParameters;
  CSignatureParser m_Sig[D3D11_SO_STREAM_COUNT];
  UINT m_NumSigs;
  UINT m_RasterizedStream;
};

//=================================================================================================================================
//
// Shader Feature Info blob
//
//  Structure:
//      A SShaderFeatureInfo.
//---------------------------------------------------------------------------------------------------------------------------------

#define SHADER_FEATURE_DOUBLES 0x0001
#define SHADER_FEATURE_COMPUTE_SHADERS_PLUS_RAW_AND_STRUCTURED_BUFFERS_VIA_SHADER_4_X \
  0x0002
#define SHADER_FEATURE_UAVS_AT_EVERY_STAGE 0x0004
#define SHADER_FEATURE_64_UAVS 0x0008
#define SHADER_FEATURE_MINIMUM_PRECISION 0x0010
#define SHADER_FEATURE_11_1_DOUBLE_EXTENSIONS 0x0020
#define SHADER_FEATURE_11_1_SHADER_EXTENSIONS 0x0040
#define SHADER_FEATURE_LEVEL_9_COMPARISON_FILTERING 0x0080
#define SHADER_FEATURE_TILED_RESOURCES 0x0100
#define SHADER_FEATURE_STENCIL_REF 0x0200
#define SHADER_FEATURE_INNER_COVERAGE 0x0400
#define SHADER_FEATURE_TYPED_UAV_LOAD_ADDITIONAL_FORMATS 0x0800
#define SHADER_FEATURE_ROVS 0x1000
#define SHADER_FEATURE_VIEWPORT_AND_RT_ARRAY_INDEX_FROM_ANY_SHADER_FEEDING_RASTERIZER \
  0x2000

// The bitfield below defines a set of optional bits for future use at the top
// end.  If a bit is set that is not in the optional range, the runtime fill
// fail shader creation.
#define D3D11_OPTIONAL_FEATURE_FLAGS 0x7FFFFF0000000000

struct SShaderFeatureInfo {
  UINT64 FeatureFlags;
};

} // namespace microsoft