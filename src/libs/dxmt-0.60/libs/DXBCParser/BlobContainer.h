// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#pragma once 
#include <stddef.h>
#include "minwindef.h"

#define DXBC_MAJOR_VERSION 1
#define DXBC_MINOR_VERSION 0
#define DXBC_1_0_MAX_SIZE_IN_BYTES 0x02000000
#define DXBC_MAX_SIZE_IN_BYTES 0x80000000

#define DXBC_FOURCC(ch0, ch1, ch2, ch3)                              \
            ((UINT)(BYTE)(ch0) | ((UINT)(BYTE)(ch1) << 8) |   \
            ((UINT)(BYTE)(ch2) << 16) | ((UINT)(BYTE)(ch3) << 24 ))

namespace microsoft {

const UINT  DXBC_FOURCC_NAME = DXBC_FOURCC('D','X','B','C');

typedef enum DXBCFourCC
{
    DXBC_GenericShader              = DXBC_FOURCC('S','H','D','R'),
    // same as SHDR, but this will fail on D3D10.x runtimes and not on D3D11+.
    DXBC_GenericShaderEx            = DXBC_FOURCC('S','H','E','X'),
    DXBC_InputSignature             = DXBC_FOURCC('I','S','G','N'),
    DXBC_InputSignature11_1         = DXBC_FOURCC('I','S','G','1'),
    DXBC_PatchConstantSignature     = DXBC_FOURCC('P','C','S','G'),
    DXBC_PatchConstantSignature11_1 = DXBC_FOURCC('P','S','G','1'),
    DXBC_OutputSignature            = DXBC_FOURCC('O','S','G','N'),
    DXBC_OutputSignature5           = DXBC_FOURCC('O','S','G','5'),
    DXBC_OutputSignature11_1        = DXBC_FOURCC('O','S','G','1'),
    DXBC_InterfaceData              = DXBC_FOURCC('I','F','C','E'),
    DXBC_ShaderFeatureInfo          = DXBC_FOURCC('S','F','I','0'),
} DXBCFourCC;
#undef DXBC_FOURCC

#define DXBC_HASH_SIZE 16
typedef struct DXBCHash
{
    unsigned char Digest[DXBC_HASH_SIZE];
} DXBCHash;

typedef struct DXBCVersion
{
    UINT16 Major;
    UINT16 Minor;
} DXBCVersion;

typedef struct DXBCHeader
{
    UINT        DXBCHeaderFourCC;
    DXBCHash    Hash;
    DXBCVersion Version;
    UINT32      ContainerSizeInBytes; // Count from start of this header, including all blobs
    UINT32      BlobCount;
    // Structure is followed by UINT32[BlobCount] (the blob index, storing offsets from start of container in bytes 
    //                                             to the start of each blob's header)
} DXBCHeader;

const UINT32 DXBCHashStartOffset = offsetof(struct DXBCHeader,Version); // hash the container from this offset to the end.
const UINT32 DXBCSizeOffset = offsetof(struct DXBCHeader,ContainerSizeInBytes); 

typedef struct DXBCBlobHeader
{
    DXBCFourCC  BlobFourCC;
    UINT32      BlobSize;    // Byte count for BlobData
    // Structure is followed by BYTE[BlobSize] (the blob's data)
} DXBCBlobHeader;

class CDXBCParser;

//=================================================================================================================================
// CDXBCParser
//
// Parse a DXBC (DX Blob Container) that you provide as input.
//
// Basic usage:
// (1) Call ReadDXBC() or ReadDXBCAssumingValidSize() (latter if you don't know the size, but trust the pointer)
// (2) Call various Get*() commands to retrieve information about the container such as 
//     how many blobs are in it, the hash of the container, the version #, and most importantly
//     retrieve all of the Blobs.  You can retrieve blobs by searching for the FourCC, or
//     enumerate through all of them.  Multiple blobs can even have the same FourCC, if you choose to 
//     create the DXBC that way, and this parser will let you discover all of them.
// (3) You can parse a new container by calling ReadDXBC() again, or just get rid of the class.
// 
// Read comments inline below for full detail.
//
// (kuttas) IMPORTANT: This class should be kept lightweight and destructor-free;
//      other systems assume that this class is relatively small in size (less than a couple of pointers)
//      and that it does not need cleaning up.  If this ceases to be the case, the FX10
//      system will need some minor modifications to cope with this.
//
class CDXBCParser
{
public:
    CDXBCParser();

    // Sets the container to be parsed, and does some
    // basic integrity checking, such as ensuring the version is:
    //     Major = DXBC_MAJOR_VERSION
    //     Minor = DXBC_MAJOR_VERSION
    // 
    // Returns S_OK, or E_FAIL
    //
    // Note, if you don't know ContainerSize and are willing to 
    // assume your pointer pContainer is valid, you can call
    // ReadDXBCAssumingValidSize
    HRESULT ReadDXBC(const void* pContainer, UINT32 ContainerSizeInBytes); 
                                    

    // Same as ReadDXBC(), except this assumes the size field stored inside 
    // pContainer is valid.
    HRESULT ReadDXBCAssumingValidSize(const void* pContainer);                              

    // returns NULL if no valid container is set
    const DXBCVersion* GetVersion(); 
    // returns NULL if no valid container is set
    const DXBCHash* GetHash();                    
    UINT32 GetBlobCount();
    const void* GetBlob(UINT32 BlobIndex);
    UINT32 GetBlobSize(UINT32 BlobIndex);
    UINT   GetBlobFourCC(UINT32 BlobIndex);
    // fixes up internal pointers given that the original bytecode has been moved by ByteOffset bytes
    HRESULT RelocateBytecode(UINT ByteOffset); 
#define DXBC_BLOB_NOT_FOUND 0xffffffffu
    // Note: search INCLUDES entry at startindex
    // returns blob index if found, otherwise returns DXBC_BLOB_NOT_FOUND
    UINT32 FindNextMatchingBlob( DXBCFourCC SearchFourCC, UINT32 SearchStartBlobIndex = 0 ); 
                                    
private:
    const DXBCHeader*    m_pHeader;
    const UINT32*        m_pIndex;
};

}