// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "DXBCUtils.h"
#include "minwindef.h"
#include "winerror.h"
#include "BlobContainer.h"

namespace microsoft {

//=================================================================================================================================
// CDXBCParser

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::CDXBCParser
CDXBCParser::CDXBCParser()
{
    m_pHeader = NULL;
    m_pIndex = NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::ReadDXBC()
HRESULT CDXBCParser::ReadDXBC( const void* pContainer, UINT ContainerSizeInBytes )
{
    if (!pContainer)
    {
        return E_FAIL;
    }
    if (ContainerSizeInBytes < sizeof( DXBCHeader ))
    {
        return E_FAIL;
    }
    DXBCHeader* pHeader = (DXBCHeader*)pContainer;
    if (pHeader->ContainerSizeInBytes != ContainerSizeInBytes)
    {
        return E_FAIL;
    }
    if ((pHeader->DXBCHeaderFourCC != DXBC_FOURCC_NAME) ||
         (pHeader->Version.Major != DXBC_MAJOR_VERSION) ||
         (pHeader->Version.Minor != DXBC_MINOR_VERSION))
    {
        return E_FAIL;
    }
    const void* pContainerEnd = ((const BYTE*)pHeader + ContainerSizeInBytes);
    if (pContainerEnd < pContainer)
    {
        return E_FAIL;
    }
    UINT* pIndex = (UINT*)((BYTE*)pHeader + sizeof( DXBCHeader ));
    if ((const BYTE*)pContainer + sizeof( UINT ) * pHeader->BlobCount < (const BYTE*)pContainer)
    {
        return E_FAIL; // overflow would break the calculation of OffsetOfCurrentSegmentEnd below
    }
    UINT OffsetOfCurrentSegmentEnd = (UINT)((BYTE*)pIndex - (const BYTE*)pContainer + sizeof( UINT ) * pHeader->BlobCount - 1);
    // Is the entire index within the container?
    if (OffsetOfCurrentSegmentEnd > ContainerSizeInBytes)
    {
        return E_FAIL;
    }
    // Is each blob in the index directly after the previous entry and not past the end of the container?
    UINT OffsetOfLastSegmentEnd = OffsetOfCurrentSegmentEnd;
    for (UINT b = 0; b < pHeader->BlobCount; b++)
    {
        DXBCBlobHeader* pBlobHeader = (DXBCBlobHeader*)((const BYTE*)pContainer + pIndex[b]);
        DXBCBlobHeader* pAfterBlobHeader = pBlobHeader + 1;

        if (pAfterBlobHeader < pBlobHeader || pAfterBlobHeader > pContainerEnd)
        {
            return E_FAIL;
        }
        if (((BYTE*)pBlobHeader < (const BYTE*)pContainer) || (pIndex[b] + sizeof( DXBCBlobHeader ) < pIndex[b]))
        {
            return E_FAIL; // overflow because of bad pIndex[b] value
        }
        if (pIndex[b] + sizeof( DXBCBlobHeader ) + pBlobHeader->BlobSize < pIndex[b])
        {
            return E_FAIL; // overflow because of bad pBlobHeader->BlobSize value
        }
        OffsetOfCurrentSegmentEnd = pIndex[b] + sizeof( DXBCBlobHeader ) + pBlobHeader->BlobSize - 1;
        if (OffsetOfCurrentSegmentEnd > ContainerSizeInBytes)
        {
            return E_FAIL;
        }
        if (OffsetOfLastSegmentEnd != pIndex[b] - 1)
        {
            return E_FAIL;
        }
        OffsetOfLastSegmentEnd = OffsetOfCurrentSegmentEnd;
    }

    // Ok, satisfied with integrity of container, store info.
    m_pHeader = pHeader;
    m_pIndex = pIndex;
    return S_OK;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::ReadDXBCAssumingValidSize()
HRESULT CDXBCParser::ReadDXBCAssumingValidSize( const void* pContainer )
{
    if (!pContainer)
    {
        return E_FAIL;
    }
    return ReadDXBC( (const BYTE*)pContainer, DXBCGetSizeAssumingValidPointer( (const BYTE*)pContainer ) );
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::FindNextMatchingBlob
UINT CDXBCParser::FindNextMatchingBlob( DXBCFourCC SearchFourCC, UINT SearchStartBlobIndex )
{
    if (!m_pHeader || !m_pIndex)
    {
        return (UINT)DXBC_BLOB_NOT_FOUND;
    }
    for (UINT b = SearchStartBlobIndex; b < m_pHeader->BlobCount; b++)
    {
        DXBCBlobHeader* pBlob = (DXBCBlobHeader*)((BYTE*)m_pHeader + m_pIndex[b]);
        if (pBlob->BlobFourCC == SearchFourCC)
        {
            return b;
        }
    }
    return (UINT)DXBC_BLOB_NOT_FOUND;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetVersion()
const DXBCVersion* CDXBCParser::GetVersion()
{
    return m_pHeader ? &m_pHeader->Version : NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetHash()
const DXBCHash* CDXBCParser::GetHash()
{
    return m_pHeader ? &m_pHeader->Hash : NULL;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobCount()
UINT CDXBCParser::GetBlobCount()
{
    return m_pHeader ? m_pHeader->BlobCount : 0;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlob()
const void* CDXBCParser::GetBlob( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return NULL;
    }
    return (BYTE*)m_pHeader + m_pIndex[BlobIndex] + sizeof( DXBCBlobHeader );
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobSize()
UINT CDXBCParser::GetBlobSize( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return 0;
    }
    return ((DXBCBlobHeader*)((BYTE*)m_pHeader + m_pIndex[BlobIndex]))->BlobSize;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::GetBlobFourCC()
UINT CDXBCParser::GetBlobFourCC( UINT BlobIndex )
{
    if (!m_pHeader || !m_pIndex || m_pHeader->BlobCount <= BlobIndex)
    {
        return 0;
    }
    return ((DXBCBlobHeader*)((BYTE*)m_pHeader + m_pIndex[BlobIndex]))->BlobFourCC;
}

//---------------------------------------------------------------------------------------------------------------------------------
// CDXBCParser::RelocateBytecode()
HRESULT CDXBCParser::RelocateBytecode( UINT ByteOffset )
{
    if (!m_pHeader || !m_pIndex)
    {
        // bad -- has not been initialized yet
        return E_FAIL;
    }
    m_pHeader = (const DXBCHeader*)((const BYTE*)m_pHeader + ByteOffset);
    m_pIndex = (const UINT32*)((const BYTE*)m_pIndex + ByteOffset);
    return S_OK;
}

}