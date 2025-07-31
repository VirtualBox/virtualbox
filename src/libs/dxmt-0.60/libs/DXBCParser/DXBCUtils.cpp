// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "DXBCUtils.h"
#include "BlobContainer.h"
#include "winerror.h"
#include <cassert>
#include <cstring>
#include <climits>
#include <ctype.h>

namespace microsoft {

//---------------------------------------------------------------------------------------------------------------------------------
// DXBCGetSizeAssumingValidPointer()
UINT DXBCGetSizeAssumingValidPointer(const void* pDXBC)
{
    if( !pDXBC ) return 0;

    return *(UINT*)((const BYTE*)pDXBC + DXBCSizeOffset);
}

//---------------------------------------------------------------------------------------------------------------------------------
// DXBCGetInputSignature()
HRESULT DXBCGetInputSignature( const void* pBlobContainer, CSignatureParser* pParserToUse, bool bForceStringReference )
{
    CDXBCParser DXBCParser;
    HRESULT hr = S_OK;
    if( FAILED(hr = DXBCParser.ReadDXBCAssumingValidSize(pBlobContainer) ) )
    {
        return hr;
    }
    UINT BlobIndex = DXBCParser.FindNextMatchingBlob(DXBC_InputSignature11_1, 0);
    if( BlobIndex != DXBC_BLOB_NOT_FOUND  )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature11_1(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    BlobIndex = DXBCParser.FindNextMatchingBlob(DXBC_InputSignature, 0);
    if( BlobIndex != DXBC_BLOB_NOT_FOUND  )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature4(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    return E_FAIL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// DXBCGetOutputSignature()
HRESULT DXBCGetOutputSignature( const void* pBlobContainer, CSignatureParser5* pParserToUse, bool bForceStringReference )
{
    CDXBCParser DXBCParser;
    HRESULT hr = S_OK;
    if( FAILED(hr = DXBCParser.ReadDXBCAssumingValidSize(pBlobContainer) ) )
    {
        return hr;
    }
    UINT BlobIndex = DXBCParser.FindNextMatchingBlob( DXBC_OutputSignature11_1, 0 );
    if( BlobIndex != DXBC_BLOB_NOT_FOUND  )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature11_1(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    BlobIndex = DXBCParser.FindNextMatchingBlob( DXBC_OutputSignature, 0 );
    if( BlobIndex != DXBC_BLOB_NOT_FOUND  )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature4(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    BlobIndex = DXBCParser.FindNextMatchingBlob( DXBC_OutputSignature5, 0 );
    if( BlobIndex != DXBC_BLOB_NOT_FOUND  )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature5(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    return E_FAIL;
}

HRESULT DXBCGetOutputSignature( const void* pBlobContainer, CSignatureParser* pParserToUse, bool bForceStringReference)
{
    CDXBCParser DXBCParser;
    HRESULT hr = S_OK;
    if( FAILED(hr = DXBCParser.ReadDXBCAssumingValidSize(pBlobContainer) ) )
    {
        return hr;
    }
    UINT BlobIndex = DXBCParser.FindNextMatchingBlob( DXBC_OutputSignature11_1, 0 );
    if( BlobIndex != DXBC_BLOB_NOT_FOUND  )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature11_1(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    BlobIndex = DXBCParser.FindNextMatchingBlob( DXBC_OutputSignature, 0 );
    if( BlobIndex != DXBC_BLOB_NOT_FOUND  )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature4(pUnParsedSignature, BlobSize, bForceStringReference);
    }
    return E_FAIL;
}

HRESULT DXBCGetPatchConstantSignature( const void* pBlobContainer, CSignatureParser* pParserToUse, bool bForceStringReference)
{
    CDXBCParser DXBCParser;
    HRESULT hr = S_OK;
    if( FAILED(hr = DXBCParser.ReadDXBCAssumingValidSize(pBlobContainer) ) )
    {
        return hr;
    }

    UINT BlobIndex = DXBCParser.FindNextMatchingBlob( DXBC_PatchConstantSignature11_1, 0 );
    if( BlobIndex != DXBC_BLOB_NOT_FOUND )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature11_1(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    BlobIndex = DXBCParser.FindNextMatchingBlob( DXBC_PatchConstantSignature, 0 );
    if( BlobIndex != DXBC_BLOB_NOT_FOUND )
    {
        const void* pUnParsedSignature = DXBCParser.GetBlob(BlobIndex);
        UINT BlobSize = DXBCParser.GetBlobSize(BlobIndex);
        if( !BlobSize )
        {
            return E_FAIL;
        }
        assert(pUnParsedSignature);
        return pParserToUse->ReadSignature4(pUnParsedSignature, BlobSize, bForceStringReference);
    }

    return E_FAIL;
}

//=================================================================================================================================
// CSignatureParser

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::Init()
void CSignatureParser::Init()
{
    m_pSignatureParameters = NULL;
    m_pSemanticNameCharSums = NULL;
    m_cParameters = 0;
    m_OwnParameters = TRUE;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::Cleanup()
void CSignatureParser::Cleanup()
{
    if( m_pSignatureParameters )
    {
        if( m_OwnParameters )
        {
            free(m_pSignatureParameters);
        }
        m_pSignatureParameters = NULL;
    }
    m_pSemanticNameCharSums = NULL;
    m_cParameters = 0;
    m_OwnParameters = TRUE;
}

//---------------------------------------------------------------------------------------------------------------------------------
// BoundedStringLength
//
// Safely returns length of null-terminated string pointed to by pBegin (not including null)
// given that the memory [ pBegin, pEnd ) is readable.
//
// Returns an error if pBegin doesn't point to a valid NT string.
//
HRESULT BoundedStringLength(const char *pBegin,
                            const char *pEnd,
                            UINT *pLength)
{
    if (pBegin >= pEnd)
    {
        // No readable memory!
        return E_FAIL;
    }

    const char *pc = pBegin;
    
    while (pc < pEnd && *pc)
    {
        ++pc;
    }

    if (pc == pEnd)
    {
        return E_FAIL;
    }

    assert(!*pc);

    *pLength = (UINT)(pc-pBegin);
    return S_OK;
}

typedef struct _D3D10_INTERNALSHADER_SIGNATURE
{
    UINT Parameters;      // Number of parameters
    UINT ParameterInfo;   // Offset to D3D10_INTERNALSHADER_PARAMETER[Parameters]
} D3D10_INTERNALSHADER_SIGNATURE, *LPD3D10_INTERNALSHADER_SIGNATURE;

typedef struct _D3D10_INTERNALSHADER_PARAMETER
{
    UINT SemanticName;                              // Offset to LPCSTR
    UINT SemanticIndex;                             // Semantic Index
    D3D10_NAME SystemValue;                         // Internally defined enumeration
    D3D10_REGISTER_COMPONENT_TYPE  ComponentType;   // Type of  of bits
    UINT Register;                                  // Register Index
    BYTE Mask;                                      // Combination of D3D10_COMPONENT_MASK values

    // The following unioned fields, NeverWrites_Mask and AlwaysReads_Mask, are exclusively used for 
    // output signatures or input signatures, respectively.
    //
    // For an output signature, NeverWrites_Mask indicates that the shader the signature belongs to never 
    // writes to the masked components of the output register.  Meaningful bits are the ones set in Mask above.
    //
    // For an input signature, AlwaysReads_Mask indicates that the shader the signature belongs to always
    // reads the masked components of the input register.  Meaningful bits are the ones set in the Mask above.
    //
    // This allows many shaders to share similar signatures even though some of them may not happen to use
    // all of the inputs/outputs - something which may not be obvious when authored.  The NeverWrites_Mask
    // and AlwaysReads_Mask can be checked in a debug layer at runtime for the one interesting case: that a 
    // shader that always reads a value is fed by a shader that always writes it.  Cases where shaders may
    // read values or may not cannot be validated unfortunately.  
    //
    // In scenarios where a signature is being passed around standalone (so it isn't tied to input or output 
    // of a given shader), this union can be zeroed out in the absence of more information.  This effectively
    // forces off linkage validation errors with the signature, since if interpreted as a input or output signature
    // somehow, since the meaning on output would be "everything is always written" and on input it would be 
    // "nothing is always read".
    union
    {
        BYTE NeverWrites_Mask;  // For an output signature, the shader the signature belongs to never 
                                // writes the masked components of the output register.
        BYTE AlwaysReads_Mask;  // For an input signature, the shader the signature belongs to always
                                // reads the masked components of the input register.
    };
} D3D10_INTERNALSHADER_PARAMETER, *LPD3D10_INTERNALSHADER_PARAMETER;

typedef struct _D3D11_INTERNALSHADER_PARAMETER_11_1
{
    UINT Stream;                                    // Stream index (parameters must appear in non-decreasing stream order)
    UINT SemanticName;                              // Offset to LPCSTR
    UINT SemanticIndex;                             // Semantic Index
    D3D10_NAME SystemValue;                         // Internally defined enumeration
    D3D10_REGISTER_COMPONENT_TYPE  ComponentType;   // Type of  of bits
    UINT Register;                                  // Register Index
    BYTE Mask;                                      // Combination of D3D10_COMPONENT_MASK values

    // The following unioned fields, NeverWrites_Mask and AlwaysReads_Mask, are exclusively used for 
    // output signatures or input signatures, respectively.
    //
    // For an output signature, NeverWrites_Mask indicates that the shader the signature belongs to never 
    // writes to the masked components of the output register.  Meaningful bits are the ones set in Mask above.
    //
    // For an input signature, AlwaysReads_Mask indicates that the shader the signature belongs to always
    // reads the masked components of the input register.  Meaningful bits are the ones set in the Mask above.
    //
    // This allows many shaders to share similar signatures even though some of them may not happen to use
    // all of the inputs/outputs - something which may not be obvious when authored.  The NeverWrites_Mask
    // and AlwaysReads_Mask can be checked in a debug layer at runtime for the one interesting case: that a 
    // shader that always reads a value is fed by a shader that always writes it.  Cases where shaders may
    // read values or may not cannot be validated unfortunately.  
    //
    // In scenarios where a signature is being passed around standalone (so it isn't tied to input or output 
    // of a given shader), this union can be zeroed out in the absence of more information.  This effectively
    // forces off linkage validation errors with the signature, since if interpreted as a input or output signature
    // somehow, since the meaning on output would be "everything is always written" and on input it would be 
    // "nothing is always read".
    union
    {
        BYTE NeverWrites_Mask;  // For an output signature, the shader the signature belongs to never 
                                // writes the masked components of the output register.
        BYTE AlwaysReads_Mask;  // For an input signature, the shader the signature belongs to always
                                // reads the masked components of the input register.
    };

    D3D_MIN_PRECISION MinPrecision;                 // Minimum precision of input/output data
} D3D11_INTERNALSHADER_PARAMETER_11_1, *LPD3D11_INTERNALSHADER_PARAMETER_11_1;

typedef struct _D3D11_INTERNALSHADER_PARAMETER_FOR_GS
{
    UINT Stream;                                    // Stream index (parameters must appear in non-decreasing stream order)
    UINT SemanticName;                              // Offset to LPCSTR
    UINT SemanticIndex;                             // Semantic Index
    D3D10_NAME SystemValue;                         // Internally defined enumeration
    D3D10_REGISTER_COMPONENT_TYPE  ComponentType;   // Type of  of bits
    UINT Register;                                  // Register Index
    BYTE Mask;                                      // Combination of D3D10_COMPONENT_MASK values

    // The following unioned fields, NeverWrites_Mask and AlwaysReads_Mask, are exclusively used for 
    // output signatures or input signatures, respectively.
    //
    // For an output signature, NeverWrites_Mask indicates that the shader the signature belongs to never 
    // writes to the masked components of the output register.  Meaningful bits are the ones set in Mask above.
    //
    // For an input signature, AlwaysReads_Mask indicates that the shader the signature belongs to always
    // reads the masked components of the input register.  Meaningful bits are the ones set in the Mask above.
    //
    // This allows many shaders to share similar signatures even though some of them may not happen to use
    // all of the inputs/outputs - something which may not be obvious when authored.  The NeverWrites_Mask
    // and AlwaysReads_Mask can be checked in a debug layer at runtime for the one interesting case: that a 
    // shader that always reads a value is fed by a shader that always writes it.  Cases where shaders may
    // read values or may not cannot be validated unfortunately.  
    //
    // In scenarios where a signature is being passed around standalone (so it isn't tied to input or output 
    // of a given shader), this union can be zeroed out in the absence of more information.  This effectively
    // forces off linkage validation errors with the signature, since if interpreted as a input or output signature
    // somehow, since the meaning on output would be "everything is always written" and on input it would be 
    // "nothing is always read".
    union
    {
        BYTE NeverWrites_Mask;  // For an output signature, the shader the signature belongs to never 
                                // writes the masked components of the output register.
        BYTE AlwaysReads_Mask;  // For an input signature, the shader the signature belongs to always
                                // reads the masked components of the input register.
    };
} D3D11_INTERNALSHADER_PARAMETER_FOR_GS, *LPD3D11_INTERNALSHADER_PARAMETER_FOR_GS;

inline D3D10_SB_NAME ConvertToSB(D3D10_NAME Value, UINT SemanticIndex)
{
    switch (Value)
    {
    case D3D10_NAME_TARGET:
    case D3D10_NAME_DEPTH:
    case D3D10_NAME_COVERAGE:
    case D3D10_NAME_UNDEFINED:
        return D3D10_SB_NAME_UNDEFINED;
    case D3D10_NAME_POSITION:
        return D3D10_SB_NAME_POSITION;
    case D3D10_NAME_CLIP_DISTANCE:
        return D3D10_SB_NAME_CLIP_DISTANCE;
    case D3D10_NAME_CULL_DISTANCE:
        return D3D10_SB_NAME_CULL_DISTANCE;
    case D3D10_NAME_RENDER_TARGET_ARRAY_INDEX:
        return D3D10_SB_NAME_RENDER_TARGET_ARRAY_INDEX;
    case D3D10_NAME_VIEWPORT_ARRAY_INDEX:
        return D3D10_SB_NAME_VIEWPORT_ARRAY_INDEX;
    case D3D10_NAME_VERTEX_ID:
        return D3D10_SB_NAME_VERTEX_ID;
    case D3D10_NAME_PRIMITIVE_ID:
        return D3D10_SB_NAME_PRIMITIVE_ID;
    case D3D10_NAME_INSTANCE_ID:
        return D3D10_SB_NAME_INSTANCE_ID;
    case D3D10_NAME_IS_FRONT_FACE:
        return D3D10_SB_NAME_IS_FRONT_FACE;
    case D3D10_NAME_SAMPLE_INDEX:
        return D3D10_SB_NAME_SAMPLE_INDEX;
    case D3D11_NAME_FINAL_QUAD_EDGE_TESSFACTOR:
        switch (SemanticIndex)
        {
        case 0:
            return D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR;
        case 1:
            return D3D11_SB_NAME_FINAL_QUAD_V_EQ_0_EDGE_TESSFACTOR;
        case 2:
            return D3D11_SB_NAME_FINAL_QUAD_U_EQ_1_EDGE_TESSFACTOR;
        case 3:
            return D3D11_SB_NAME_FINAL_QUAD_V_EQ_1_EDGE_TESSFACTOR;
        }
        assert("Invalid D3D10_NAME");
        break;
    case D3D11_NAME_FINAL_QUAD_INSIDE_TESSFACTOR:
        switch (SemanticIndex)
        {
        case 0:
            return D3D11_SB_NAME_FINAL_QUAD_U_INSIDE_TESSFACTOR;
        case 1:
            return D3D11_SB_NAME_FINAL_QUAD_V_INSIDE_TESSFACTOR;
        }
        assert("Invalid D3D10_NAME");
        break;
    case D3D11_NAME_FINAL_TRI_EDGE_TESSFACTOR:
        switch (SemanticIndex)
        {
        case 0:
            return D3D11_SB_NAME_FINAL_TRI_U_EQ_0_EDGE_TESSFACTOR;
        case 1:
            return D3D11_SB_NAME_FINAL_TRI_V_EQ_0_EDGE_TESSFACTOR;
        case 2:
            return D3D11_SB_NAME_FINAL_TRI_W_EQ_0_EDGE_TESSFACTOR;
        }
        assert("Invalid D3D10_NAME");
        break;
    case D3D11_NAME_FINAL_TRI_INSIDE_TESSFACTOR:
        switch (SemanticIndex)
        {
        case 0:
            return D3D11_SB_NAME_FINAL_TRI_INSIDE_TESSFACTOR;
        }
        assert("Invalid D3D10_NAME");
        break;
    case D3D11_NAME_FINAL_LINE_DETAIL_TESSFACTOR:
        switch (SemanticIndex)
        {
        case 1:
            return D3D11_SB_NAME_FINAL_LINE_DETAIL_TESSFACTOR;
        }
        assert("Invalid D3D10_NAME");
        break;
    case D3D11_NAME_FINAL_LINE_DENSITY_TESSFACTOR:
        switch (SemanticIndex)
        {
        case 0:
            return D3D11_SB_NAME_FINAL_LINE_DENSITY_TESSFACTOR;
        }
        assert("Invalid D3D10_NAME");
        break;
    default:
        assert("Invalid D3D10_NAME");
    }
    // in retail the assert won't get hit and we'll just pass through anything bad.
    return (D3D10_SB_NAME)Value;
}
inline D3D10_SB_RESOURCE_DIMENSION ConvertToSB(D3D11_SRV_DIMENSION Value)
{

    switch (Value)
    {
    case D3D11_SRV_DIMENSION_UNKNOWN:
        return D3D10_SB_RESOURCE_DIMENSION_UNKNOWN;
    case D3D11_SRV_DIMENSION_BUFFER:
    case D3D11_SRV_DIMENSION_BUFFEREX:
        return D3D10_SB_RESOURCE_DIMENSION_BUFFER;
    case D3D11_SRV_DIMENSION_TEXTURE1D:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D;
    case D3D11_SRV_DIMENSION_TEXTURE2D:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMS;
    case D3D11_SRV_DIMENSION_TEXTURE3D:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY;
    default:
        assert("Invalid D3D11_RESOURCE_DIMENSION");
    }
    // in retail the assert won't get hit and we'll just pass through anything bad.
    return (D3D10_SB_RESOURCE_DIMENSION)Value;
}

inline D3D10_SB_RESOURCE_DIMENSION ConvertToSB(D3D11_UAV_DIMENSION Value)
{

    switch (Value)
    {
    case D3D11_UAV_DIMENSION_UNKNOWN:
        return D3D10_SB_RESOURCE_DIMENSION_UNKNOWN;
    case D3D11_UAV_DIMENSION_BUFFER:
        return D3D10_SB_RESOURCE_DIMENSION_BUFFER;
    case D3D11_UAV_DIMENSION_TEXTURE1D:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D;
    case D3D11_UAV_DIMENSION_TEXTURE2D:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D;
    case D3D11_UAV_DIMENSION_TEXTURE3D:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D;
    case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
        return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY;
    default:
        assert("Invalid D3D11_RESOURCE_DIMENSION");
    }
    // in retail the assert won't get hit and we'll just pass through anything bad.
    return (D3D10_SB_RESOURCE_DIMENSION)Value;
}

inline D3D10_SB_REGISTER_COMPONENT_TYPE ConvertToSB(D3D10_REGISTER_COMPONENT_TYPE Value)
{

    switch (Value)
    {
    case D3D10_REGISTER_COMPONENT_UNKNOWN:
        return D3D10_SB_REGISTER_COMPONENT_UNKNOWN;
    case D3D10_REGISTER_COMPONENT_UINT32:
        return D3D10_SB_REGISTER_COMPONENT_UINT32;
    case D3D10_REGISTER_COMPONENT_SINT32:
        return D3D10_SB_REGISTER_COMPONENT_SINT32;
    case D3D10_REGISTER_COMPONENT_FLOAT32:
        return D3D10_SB_REGISTER_COMPONENT_FLOAT32;
    default:
        assert("Invalid D3D10_REGISTER_COMPONENT_TYPE");
    }
    // in retail the assert won't get hit and we'll just pass through anything bad.
    return (D3D10_SB_REGISTER_COMPONENT_TYPE)Value;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::ReadSignature()
HRESULT CSignatureParser::ReadSignature11_1(const void* pSignature, UINT BlobSize, bool bForceStringReference )
{
    if( m_cParameters )
    {
        Cleanup();
    }
    if( !pSignature || BlobSize < sizeof(D3D10_INTERNALSHADER_SIGNATURE) )
    {
        return E_FAIL;
    }
    D3D10_INTERNALSHADER_SIGNATURE* pHeader = (D3D10_INTERNALSHADER_SIGNATURE*)pSignature;
    if( pHeader->Parameters == 0 )
    {
        m_cParameters = 0;
        return S_OK;
    }

    // If parameter count is less than MaxParameters calculation of SignatureAndParameterInfoSize will not overflow.
    const UINT MaxParameters = ((UINT_MAX - sizeof(D3D10_INTERNALSHADER_SIGNATURE)) / sizeof(D3D10_INTERNALSHADER_PARAMETER));

    if (pHeader->Parameters > MaxParameters)
    {
        return E_FAIL;
    }
    
    UINT ParameterInfoSize = pHeader->Parameters * sizeof(D3D11_INTERNALSHADER_PARAMETER_11_1);
    UINT SignatureAndParameterInfoSize = ParameterInfoSize + sizeof(D3D10_INTERNALSHADER_SIGNATURE);
    
    if (BlobSize < SignatureAndParameterInfoSize)
    {
        return E_FAIL;
    }

    // Keep end pointer around for checking
    const void *pEnd = static_cast<const BYTE*>(pSignature) + BlobSize;
    const D3D11_INTERNALSHADER_PARAMETER_11_1* pParameterInfo = (D3D11_INTERNALSHADER_PARAMETER_11_1*)((const BYTE*)pSignature + pHeader->ParameterInfo);
    UINT cParameters = pHeader->Parameters;
    UINT TotalStringLength = 0;
    UINT LastRegister = 0;
    for( UINT i = 0; i < cParameters; i++ )
    {
        UINT StringLength;
        if (FAILED(BoundedStringLength((const char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName),
                                       (const char*) pEnd,
                                        &StringLength)))
        {
            return E_FAIL;
        }
        if (!bForceStringReference)
        {
            TotalStringLength += StringLength + 1;
        }
        if( i > 0 )
        {
            // registers must show up in nondecreasing order in a signature
            if( LastRegister > pParameterInfo[i].Register )
            {
                return E_FAIL;
            }
        }
        LastRegister = pParameterInfo[i].Register;
    }
    UINT TotalParameterSize = pHeader->Parameters*sizeof(D3D11_SIGNATURE_PARAMETER);
    UINT TotalCharSumsSize = sizeof(UINT)*cParameters; // char sums for each SemanticName
    m_pSignatureParameters = (D3D11_SIGNATURE_PARAMETER*)malloc((TotalParameterSize + TotalCharSumsSize + TotalStringLength)*sizeof(BYTE));
    if( !m_pSignatureParameters )
    {
        return E_OUTOFMEMORY;
    }
    m_OwnParameters = TRUE;
    m_pSemanticNameCharSums = (UINT*)((BYTE*)m_pSignatureParameters + TotalParameterSize);
    char* pNextDstString = (char*)((BYTE*)m_pSemanticNameCharSums + TotalCharSumsSize);
    for( UINT i = 0; i < cParameters; i++ )
    {
        char* pNextSrcString = (char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName);

        m_pSignatureParameters[i].Stream = 0;
        m_pSignatureParameters[i].ComponentType = ConvertToSB(pParameterInfo[i].ComponentType);
        m_pSignatureParameters[i].Mask = pParameterInfo[i].Mask;
        m_pSignatureParameters[i].Register = pParameterInfo[i].Register;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].SystemValue = ConvertToSB(pParameterInfo[i].SystemValue,pParameterInfo[i].SemanticIndex);
        m_pSignatureParameters[i].SemanticName = bForceStringReference ? pNextSrcString : pNextDstString;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].NeverWrites_Mask = pParameterInfo[i].NeverWrites_Mask; // union with AlwaysReadMask
        m_pSignatureParameters[i].MinPrecision = pParameterInfo[i].MinPrecision;

        // This strlen was checked with BoundedStringLength in the first loop.
#pragma prefast( suppress : __WARNING_PRECONDITION_NULLTERMINATION_VIOLATION )
        UINT length = (UINT)strlen(pNextSrcString) + 1;

        if (!bForceStringReference)
        {
            // Calculation of TotalStringLength ensures that we have space in pNextDstString
#pragma prefast( suppress : __WARNING_POTENTIAL_BUFFER_OVERFLOW_LOOP_DEPENDENT )
            memcpy(pNextDstString, pNextSrcString, length);
            pNextDstString += length;
        }

        m_pSemanticNameCharSums[i] = 0;
        for( UINT j = 0; j < length; j++ )
        {
            m_pSemanticNameCharSums[i] += tolower(pNextSrcString[j]);
        }
    }
    m_cParameters = cParameters;
    return S_OK;
}


HRESULT CSignatureParser::ReadSignature4(const void* pSignature, UINT BlobSize, bool bForceStringReference )
{
    if( m_cParameters )
    {
        Cleanup();
    }
    if( !pSignature || BlobSize < sizeof(D3D10_INTERNALSHADER_SIGNATURE) )
    {
        return E_FAIL;
    }
    D3D10_INTERNALSHADER_SIGNATURE* pHeader = (D3D10_INTERNALSHADER_SIGNATURE*)pSignature;
    if( pHeader->Parameters == 0 )
    {
        m_cParameters = 0;
        return S_OK;
    }

    // If parameter count is less than MaxParameters calculation of SignatureAndParameterInfoSize will not overflow.
    const UINT MaxParameters = ((UINT_MAX - sizeof(D3D10_INTERNALSHADER_SIGNATURE)) / sizeof(D3D10_INTERNALSHADER_PARAMETER));

    if (pHeader->Parameters > MaxParameters)
    {
        return E_FAIL;
    }
    
    UINT ParameterInfoSize = pHeader->Parameters * sizeof(D3D10_INTERNALSHADER_PARAMETER);
    UINT SignatureAndParameterInfoSize = ParameterInfoSize + sizeof(D3D10_INTERNALSHADER_SIGNATURE);
    
    if (BlobSize < SignatureAndParameterInfoSize)
    {
        return E_FAIL;
    }

    // Keep end pointer around for checking
    const void *pEnd = static_cast<const BYTE*>(pSignature) + BlobSize;
    const D3D10_INTERNALSHADER_PARAMETER* pParameterInfo = (D3D10_INTERNALSHADER_PARAMETER*)((const BYTE*)pSignature + pHeader->ParameterInfo);
    UINT cParameters = pHeader->Parameters;
    UINT TotalStringLength = 0;
    UINT LastRegister = 0;
    for( UINT i = 0; i < cParameters; i++ )
    {
        UINT StringLength;
        if (FAILED(BoundedStringLength((const char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName),
                                       (const char*) pEnd,
                                        &StringLength)))
        {
            return E_FAIL;
        }
        if (!bForceStringReference)
        {
            TotalStringLength += StringLength + 1;
        }
        if( i > 0 )
        {
            // registers must show up in nondecreasing order in a signature
            if( LastRegister > pParameterInfo[i].Register )
            {
                return E_FAIL;
            }
        }
        LastRegister = pParameterInfo[i].Register;
    }
    UINT TotalParameterSize = pHeader->Parameters*sizeof(D3D11_SIGNATURE_PARAMETER);
    UINT TotalCharSumsSize = sizeof(UINT)*cParameters; // char sums for each SemanticName
    m_pSignatureParameters = (D3D11_SIGNATURE_PARAMETER*)malloc((TotalParameterSize + TotalCharSumsSize + TotalStringLength)*sizeof(BYTE));
    if( !m_pSignatureParameters )
    {
        return E_OUTOFMEMORY;
    }
    m_OwnParameters = TRUE;
    m_pSemanticNameCharSums = (UINT*)((BYTE*)m_pSignatureParameters + TotalParameterSize);
    char* pNextDstString = (char*)((BYTE*)m_pSemanticNameCharSums + TotalCharSumsSize);
    for( UINT i = 0; i < cParameters; i++ )
    {
        char* pNextSrcString = (char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName);

        m_pSignatureParameters[i].Stream = 0;
        m_pSignatureParameters[i].ComponentType = ConvertToSB(pParameterInfo[i].ComponentType);
        m_pSignatureParameters[i].Mask = pParameterInfo[i].Mask;
        m_pSignatureParameters[i].Register = pParameterInfo[i].Register;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].SystemValue = ConvertToSB(pParameterInfo[i].SystemValue,pParameterInfo[i].SemanticIndex);
        m_pSignatureParameters[i].SemanticName = bForceStringReference ? pNextSrcString : pNextDstString;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].NeverWrites_Mask = pParameterInfo[i].NeverWrites_Mask; // union with AlwaysReadMask
        m_pSignatureParameters[i].MinPrecision = D3D_MIN_PRECISION_DEFAULT;

        // This strlen was checked with BoundedStringLength in the first loop.
#pragma prefast( suppress : __WARNING_PRECONDITION_NULLTERMINATION_VIOLATION )
        UINT length = (UINT)strlen(pNextSrcString) + 1;

        if (!bForceStringReference)
        {
            // Calculation of TotalStringLength ensures that we have space in pNextDstString
#pragma prefast( suppress : __WARNING_POTENTIAL_BUFFER_OVERFLOW_LOOP_DEPENDENT )
            memcpy(pNextDstString, pNextSrcString, length);
            pNextDstString += length;
        }

        m_pSemanticNameCharSums[i] = 0;
        for( UINT j = 0; j < length; j++ )
        {
            m_pSemanticNameCharSums[i] += tolower(pNextSrcString[j]);
        }
    }
    m_cParameters = cParameters;
    return S_OK;
}

HRESULT CSignatureParser::ReadSignature11_1( D3D11_SIGNATURE_PARAMETER* pParamList, UINT* pCharSums, UINT NumParameters )
{
    if( m_cParameters )
    {
        Cleanup();
    }
    if( !pParamList ) 
    {
        return S_OK;
    }

    m_pSignatureParameters = pParamList;
    m_pSemanticNameCharSums = pCharSums;
    m_cParameters = NumParameters;
    m_OwnParameters = FALSE;
    return S_OK;
}


HRESULT CSignatureParser::ReadSignature5( D3D11_SIGNATURE_PARAMETER* pParamList, UINT* pCharSums, UINT NumParameters )
{
    if( m_cParameters )
    {
        Cleanup();
    }
    if( !pParamList ) 
    {
        return S_OK;
    }

    m_pSignatureParameters = pParamList;
    m_pSemanticNameCharSums = pCharSums;
    m_cParameters = NumParameters;
    m_OwnParameters = FALSE;
    return S_OK;
}


//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser5::Init()
void CSignatureParser5::Init()
{
    m_NumSigs = 0;
    m_cParameters = 0;
    m_pSignatureParameters = NULL;
    m_RasterizedStream = 0;
    
    for( UINT i=0; i < D3D11_SO_STREAM_COUNT; i++ ) 
    {
        m_Sig[i].Init(); 
    }
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser5::Cleanup()
void CSignatureParser5::Cleanup()
{ 
    for( UINT i = 0; i < D3D11_SO_STREAM_COUNT; i++ ) 
    {
        if( m_Sig[i].m_cParameters )
        {
            m_Sig[i].Cleanup(); 
        }
    }
    if( m_pSignatureParameters )
    {
        free( m_pSignatureParameters );
        m_pSignatureParameters = NULL;
    }
    m_NumSigs = 0;
    m_RasterizedStream = 0;
    m_cParameters = 0;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::ReadSignature11_1()
HRESULT CSignatureParser5::ReadSignature11_1(const void* pSignature, UINT BlobSize, bool bForceStringReference )
{
    Cleanup();
    if( !pSignature || BlobSize < sizeof(D3D10_INTERNALSHADER_SIGNATURE) )
    {
        return E_FAIL;
    }

    D3D10_INTERNALSHADER_SIGNATURE* pHeader = (D3D10_INTERNALSHADER_SIGNATURE*)pSignature;
    if( pHeader->Parameters == 0 )
    {
        m_cParameters = 0;
        return S_OK;
    }

    // If parameter count is less than MaxParameters calculation of SignatureAndParameterInfoSize will not overflow.
    const UINT MaxParameters = ((UINT_MAX - sizeof(D3D10_INTERNALSHADER_SIGNATURE)) / sizeof(D3D11_INTERNALSHADER_PARAMETER_11_1));

    if (pHeader->Parameters > MaxParameters)
    {
        return E_FAIL;
    }
    
    UINT ParameterInfoSize = pHeader->Parameters * sizeof(D3D11_INTERNALSHADER_PARAMETER_11_1);
    UINT SignatureAndParameterInfoSize = ParameterInfoSize + sizeof(D3D10_INTERNALSHADER_SIGNATURE);
    
    if (BlobSize < SignatureAndParameterInfoSize)
    {
        return E_FAIL;
    }

    // Keep end pointer around for checking
    const void *pEnd = static_cast<const BYTE*>(pSignature) + BlobSize;
    
    const D3D11_INTERNALSHADER_PARAMETER_11_1* pParameterInfo = (D3D11_INTERNALSHADER_PARAMETER_11_1*)((const BYTE*)pSignature + pHeader->ParameterInfo);
    UINT cParameters = pHeader->Parameters;
    UINT TotalStringLength = 0;
    UINT StreamParameters[D3D11_SO_STREAM_COUNT] = {0};
    UINT i = 0;
    for( UINT s = 0; s < D3D11_SO_STREAM_COUNT; s++ )
    {
        UINT LastRegister = 0;
        UINT FirstParameter = i;

        for( ; i < cParameters; i++ )
        {
            if( pParameterInfo[i].Stream != s )
            {
                break;
            }

            StreamParameters[s]++;
            UINT StringLength;
            if (FAILED(BoundedStringLength((const char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName),
                                           (const char*) pEnd,
                                            &StringLength)))
            {
                return E_FAIL;
            }
            if (!bForceStringReference)
            {
                TotalStringLength += StringLength + 1;
            }
            if( i > FirstParameter )
            {
                // registers must show up in nondecreasing order in a signature
                if( LastRegister > pParameterInfo[i].Register )
                {
                    return E_FAIL;
                }
            }
            LastRegister = pParameterInfo[i].Register;
            m_NumSigs = s + 1;
        }
    }
    UINT TotalParameterSize = pHeader->Parameters*sizeof(D3D11_SIGNATURE_PARAMETER);
    UINT TotalCharSumsSize = sizeof(UINT)*cParameters; // char sums for each SemanticName
    m_pSignatureParameters = (D3D11_SIGNATURE_PARAMETER*)malloc((TotalParameterSize + TotalCharSumsSize + TotalStringLength)*sizeof(BYTE));
    if( !m_pSignatureParameters )
    {
        return E_OUTOFMEMORY;
    }
    m_pSemanticNameCharSums = (UINT*)((BYTE*)m_pSignatureParameters + TotalParameterSize);
    char* pNextDstString = (char*)((BYTE*)m_pSemanticNameCharSums + TotalCharSumsSize);
    for( i = 0; i < cParameters; i++ )
    {
        char* pNextSrcString = (char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName);

        m_pSignatureParameters[i].Stream = pParameterInfo[i].Stream;
        m_pSignatureParameters[i].ComponentType = ConvertToSB(pParameterInfo[i].ComponentType);
        m_pSignatureParameters[i].Mask = pParameterInfo[i].Mask;
        m_pSignatureParameters[i].Register = pParameterInfo[i].Register;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].SystemValue = ConvertToSB(pParameterInfo[i].SystemValue,pParameterInfo[i].SemanticIndex);
        m_pSignatureParameters[i].SemanticName = bForceStringReference ? pNextSrcString : pNextDstString;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].NeverWrites_Mask = pParameterInfo[i].NeverWrites_Mask; // union with AlwaysReadMask
        m_pSignatureParameters[i].MinPrecision = pParameterInfo[i].MinPrecision;

        // This strlen was checked with BoundedStringLength in the first loop.
#pragma prefast( suppress : __WARNING_PRECONDITION_NULLTERMINATION_VIOLATION )
        UINT length = (UINT)strlen(pNextSrcString) + 1;

        if (!bForceStringReference)
        {
            // __analysis_assume((char*)pNextDstString + length < (char*)m_pSignatureParameters + (TotalParameterSize + TotalCharSumsSize + TotalStringLength) * sizeof(BYTE));
            // Calculation of TotalStringLength ensures that we have space in pNextDstString
#pragma prefast( suppress : __WARNING_POTENTIAL_BUFFER_OVERFLOW_LOOP_DEPENDENT )
            memcpy(pNextDstString, pNextSrcString, length);
            pNextDstString += length;
        }

        m_pSemanticNameCharSums[i] = 0;
        for( UINT j = 0; j < length; j++ )
        {
            m_pSemanticNameCharSums[i] += tolower(pNextSrcString[j]);
        }
    }
    m_cParameters = cParameters;

    UINT PreviousParams = 0;
    for( i = 0; i < m_NumSigs; i++ )
    {
        m_Sig[i].ReadSignature11_1( &m_pSignatureParameters[PreviousParams], &m_pSemanticNameCharSums[PreviousParams], StreamParameters[i] );
        PreviousParams += StreamParameters[i];
    }

    return S_OK;
}


//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::ReadSignature5()
HRESULT CSignatureParser5::ReadSignature5(const void* pSignature, UINT BlobSize, bool bForceStringReference )
{
    Cleanup();
    if( !pSignature || BlobSize < sizeof(D3D10_INTERNALSHADER_SIGNATURE) )
    {
        return E_FAIL;
    }

    D3D10_INTERNALSHADER_SIGNATURE* pHeader = (D3D10_INTERNALSHADER_SIGNATURE*)pSignature;
    if( pHeader->Parameters == 0 )
    {
        m_cParameters = 0;
        return S_OK;
    }

    // If parameter count is less than MaxParameters calculation of SignatureAndParameterInfoSize will not overflow.
    const UINT MaxParameters = ((UINT_MAX - sizeof(D3D10_INTERNALSHADER_SIGNATURE)) / sizeof(D3D11_INTERNALSHADER_PARAMETER_FOR_GS));

    if (pHeader->Parameters > MaxParameters)
    {
        return E_FAIL;
    }
    
    UINT ParameterInfoSize = pHeader->Parameters * sizeof(D3D11_INTERNALSHADER_PARAMETER_FOR_GS);
    UINT SignatureAndParameterInfoSize = ParameterInfoSize + sizeof(D3D10_INTERNALSHADER_SIGNATURE);
    
    if (BlobSize < SignatureAndParameterInfoSize)
    {
        return E_FAIL;
    }

    // Keep end pointer around for checking
    const void *pEnd = static_cast<const BYTE*>(pSignature) + BlobSize;
    
    const D3D11_INTERNALSHADER_PARAMETER_FOR_GS* pParameterInfo = (D3D11_INTERNALSHADER_PARAMETER_FOR_GS*)((const BYTE*)pSignature + pHeader->ParameterInfo);
    UINT cParameters = pHeader->Parameters;
    UINT TotalStringLength = 0;
    UINT StreamParameters[D3D11_SO_STREAM_COUNT] = {0};
    UINT i = 0;
    for( UINT s = 0; s < D3D11_SO_STREAM_COUNT; s++ )
    {
        UINT LastRegister = 0;
        UINT FirstParameter = i;

        for( ; i < cParameters; i++ )
        {
            if( pParameterInfo[i].Stream != s )
            {
                break;
            }

            StreamParameters[s]++;
            UINT StringLength;
            if (FAILED(BoundedStringLength((const char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName),
                                           (const char*) pEnd,
                                           &StringLength)))
            {
                return E_FAIL;
            }
            if (!bForceStringReference)
            {
                TotalStringLength += StringLength + 1;
            }
            if( i > FirstParameter )
            {
                // registers must show up in nondecreasing order in a signature
                if( LastRegister > pParameterInfo[i].Register )
                {
                    return E_FAIL;
                }
            }
            LastRegister = pParameterInfo[i].Register;
            m_NumSigs = s + 1;
        }
    }
    UINT TotalParameterSize = pHeader->Parameters*sizeof(D3D11_SIGNATURE_PARAMETER);
    UINT TotalCharSumsSize = sizeof(UINT)*cParameters; // char sums for each SemanticName
    m_pSignatureParameters = (D3D11_SIGNATURE_PARAMETER*)malloc((TotalParameterSize + TotalCharSumsSize + TotalStringLength)*sizeof(BYTE));
    if( !m_pSignatureParameters )
    {
        return E_OUTOFMEMORY;
    }
    m_pSemanticNameCharSums = (UINT*)((BYTE*)m_pSignatureParameters + TotalParameterSize);
    char* pNextDstString = (char*)((BYTE*)m_pSemanticNameCharSums + TotalCharSumsSize);
    for( i = 0; i < cParameters; i++ )
    {
        char* pNextSrcString = (char*)((const BYTE*)pSignature + pParameterInfo[i].SemanticName);

        m_pSignatureParameters[i].Stream = pParameterInfo[i].Stream;
        m_pSignatureParameters[i].ComponentType = ConvertToSB(pParameterInfo[i].ComponentType);
        m_pSignatureParameters[i].Mask = pParameterInfo[i].Mask;
        m_pSignatureParameters[i].Register = pParameterInfo[i].Register;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].SystemValue = ConvertToSB(pParameterInfo[i].SystemValue,pParameterInfo[i].SemanticIndex);
        m_pSignatureParameters[i].SemanticName = pNextDstString;
        m_pSignatureParameters[i].SemanticIndex = pParameterInfo[i].SemanticIndex;
        m_pSignatureParameters[i].NeverWrites_Mask = pParameterInfo[i].NeverWrites_Mask; // union with AlwaysReadMask
        m_pSignatureParameters[i].MinPrecision = D3D_MIN_PRECISION_DEFAULT;

        // This strlen was checked with BoundedStringLength in the first loop.
#pragma prefast( suppress : __WARNING_PRECONDITION_NULLTERMINATION_VIOLATION )
        UINT length = (UINT)strlen(pNextSrcString) + 1;

        if (!bForceStringReference)
        {
            // __analysis_assume((char*)pNextDstString + length < (char*)m_pSignatureParameters + (TotalParameterSize + TotalCharSumsSize + TotalStringLength) * sizeof(BYTE));
            // Calculation of TotalStringLength ensures that we have space in pNextDstString
#pragma prefast( suppress : __WARNING_POTENTIAL_BUFFER_OVERFLOW_LOOP_DEPENDENT )
            memcpy(pNextDstString, pNextSrcString, length);
            pNextDstString += length;
        }

        m_pSemanticNameCharSums[i] = 0;
        for( UINT j = 0; j < length; j++ )
        {
            m_pSemanticNameCharSums[i] += tolower(pNextSrcString[j]);
        }
    }
    m_cParameters = cParameters;

    UINT PreviousParams = 0;
    for( i = 0; i < m_NumSigs; i++ )
    {
        m_Sig[i].ReadSignature5( &m_pSignatureParameters[PreviousParams], &m_pSemanticNameCharSums[PreviousParams], StreamParameters[i] );
        PreviousParams += StreamParameters[i];
    }

    return S_OK;
}

HRESULT CSignatureParser5::ReadSignature4( const void* pSignature, UINT BlobSize, bool bForceStringReference )
{
    Cleanup();
    if( !pSignature ) 
    {
        return E_FAIL;
    }

    if( FAILED( m_Sig[0].ReadSignature4( pSignature, BlobSize, bForceStringReference ) ) )
    {
        return E_FAIL;
    }

    m_pSignatureParameters = m_Sig[0].m_pSignatureParameters;
    m_Sig[0].m_OwnParameters = FALSE; // steal the signature from our child
    m_pSemanticNameCharSums = m_Sig[0].m_pSemanticNameCharSums;
    m_cParameters = m_Sig[0].m_cParameters;
    m_RasterizedStream = 0;
    m_NumSigs = 1;

    return S_OK;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::GetParameters()
UINT CSignatureParser::GetParameters( D3D11_SIGNATURE_PARAMETER const** ppParameters ) const
{
    if( ppParameters )
    {
        *ppParameters = m_pSignatureParameters;
    }
    return m_cParameters;
}

//---------------------------------------------------------------------------------------------------------------------------------
// LowerCaseCharSum
static UINT LowerCaseCharSum(LPCSTR pStr)
{
    if (!pStr)
        return 0;

    UINT sum = 0;
    while (*pStr != '\0')
    {
        sum += tolower(*pStr);
        pStr++;
    }
    return sum;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::FindParameter()
HRESULT CSignatureParser::FindParameter( LPCSTR SemanticName, UINT  SemanticIndex, 
                                         D3D11_SIGNATURE_PARAMETER** ppFoundParameter ) const
{
    UINT InputNameCharSum = LowerCaseCharSum(SemanticName);
    for(UINT i = 0; i < m_cParameters; i++ )
    {
        if( (SemanticIndex == m_pSignatureParameters[i].SemanticIndex) &&
            (InputNameCharSum == m_pSemanticNameCharSums[i]) &&
            (strcasecmp(SemanticName,m_pSignatureParameters[i].SemanticName) == 0) )
        {
            if(ppFoundParameter) *ppFoundParameter = &m_pSignatureParameters[i];
            return S_OK;
        }
    }
    if(ppFoundParameter) *ppFoundParameter = NULL;
    return E_FAIL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::FindParameterRegister()
HRESULT CSignatureParser::FindParameterRegister( LPCSTR SemanticName, UINT  SemanticIndex, 
                                                 UINT* pFoundParameterRegister )
{
    UINT InputNameCharSum = LowerCaseCharSum(SemanticName);
    for(UINT i = 0; i < m_cParameters; i++ )
    {
        if( (SemanticIndex == m_pSignatureParameters[i].SemanticIndex) &&
            (InputNameCharSum == m_pSemanticNameCharSums[i]) &&
            (strcasecmp(SemanticName,m_pSignatureParameters[i].SemanticName) == 0) )
        {
            if(pFoundParameterRegister) *pFoundParameterRegister = m_pSignatureParameters[i].Register;
            return S_OK;
        }
    }
    return E_FAIL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::GetSemanticNameCharSum()
UINT CSignatureParser::GetSemanticNameCharSum( UINT parameter )
{
    if( parameter >= m_cParameters )
    {
        return 0;
    }
    return m_pSemanticNameCharSums[parameter];
}


//---------------------------------------------------------------------------------------------------------------------------------
// CSignatureParser::CanOutputTo()
bool CSignatureParser::CanOutputTo( CSignatureParser* pTargetSignature )
{
    if( !pTargetSignature )
    {
        return false;
    }
    const D3D11_SIGNATURE_PARAMETER* pDstParam;
    UINT cTargetParameters = pTargetSignature->GetParameters(&pDstParam);
    if( cTargetParameters > m_cParameters )
    {
        return false;
    }
    assert(cTargetParameters <= 64); // if cTargetParameters is allowed to be much larger, rethink this n^2 algorithm.
    for( UINT i = 0; i < cTargetParameters; i++ )
    {
        bool bFoundMatch = false;
        for( UINT j = 0; j < m_cParameters; j++ )
        {
            UINT srcIndex = (i + j) % m_cParameters; // start at the same location in the src as the dest, but loop through all.
            D3D11_SIGNATURE_PARAMETER* pSrcParam = &(m_pSignatureParameters[srcIndex]);
            if( ( m_pSemanticNameCharSums[srcIndex] == pTargetSignature->m_pSemanticNameCharSums[i] ) &&
                ( strcasecmp(pSrcParam->SemanticName, pDstParam->SemanticName) == 0 ) &&
                ( pSrcParam->SemanticIndex == pDstParam->SemanticIndex ) &&
                ( pSrcParam->Register == pDstParam->Register ) &&
                ( pSrcParam->SystemValue == pDstParam->SystemValue ) &&
                ( pSrcParam->ComponentType == pDstParam->ComponentType ) &&
                ( ( pSrcParam->Mask & pDstParam->Mask ) == pDstParam->Mask ) &&
                // Check shader dependent read/write of input/output...
                // If the output shader never writes a value and the input always reads it, that's a problem:
                !((pSrcParam->NeverWrites_Mask & pSrcParam->Mask) & (pDstParam->AlwaysReads_Mask & pDstParam->Mask))
              )
            {
                bFoundMatch = true;
                break;
            }
        }
        if( !bFoundMatch )
        {
            return false;
        }
        pDstParam++;
    }

    return true;
}

//---------------------------------------------------------------------------------------------------------------------------------
// Clear out AlwaysReads_Mask / NeverWrites_Mask for all elements in the 
// signature to force the signature not to cause linkage errors when
// passing the signature around after extracting it from a shader.
void CSignatureParser::ClearAlwaysReadsNeverWritesMask()
{
    for( UINT i = 0; i < m_cParameters; i++ )
    {
        m_pSignatureParameters[i].NeverWrites_Mask = 0; // union with AlwaysReads_Mask
    }
}

}