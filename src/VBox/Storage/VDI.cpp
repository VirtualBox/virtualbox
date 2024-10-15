/* $Id: VDI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual Disk Image (VDI), Core Code.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_VD_VDI
#include <VBox/vd-plugin.h>
#include "VDICore.h"
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/asm.h>

#include "VDBackends.h"

#define VDI_IMAGE_DEFAULT_BLOCK_SIZE _1M

/** Macros for endianess conversion. */
#define SET_ENDIAN_U32(conv, u32) (conv == VDIECONV_H2F ? RT_H2LE_U32(u32) : RT_LE2H_U32(u32))
#define SET_ENDIAN_U64(conv, u64) (conv == VDIECONV_H2F ? RT_H2LE_U64(u64) : RT_LE2H_U64(u64))

static const char *vdiAllocationBlockSize = "1048576";

static const VDCONFIGINFO vdiConfigInfo[] =
{
    { "AllocationBlockSize",            vdiAllocationBlockSize,           VDCFGVALUETYPE_INTEGER,      VD_CFGKEY_CREATEONLY },
    { NULL,                             NULL,                             VDCFGVALUETYPE_INTEGER,      0 }
};


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const VDFILEEXTENSION s_aVdiFileExtensions[] =
{
    {"vdi", VDTYPE_HDD},
    {NULL, VDTYPE_INVALID}
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static unsigned getPowerOfTwo(unsigned uNumber);
static void vdiInitPreHeader(PVDIPREHEADER pPreHdr);
static int  vdiValidatePreHeader(PVDIPREHEADER pPreHdr);
static int  vdiValidateHeader(PVDIHEADER pHeader);
static void vdiSetupImageDesc(PVDIIMAGEDESC pImage);
static int  vdiUpdateHeader(PVDIIMAGEDESC pImage);
static int  vdiUpdateBlockInfo(PVDIIMAGEDESC pImage, unsigned uBlock);
static int  vdiUpdateHeaderAsync(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx);
static int  vdiUpdateBlockInfoAsync(PVDIIMAGEDESC pImage, unsigned uBlock, PVDIOCTX pIoCtx,
                                    bool fUpdateHdr);

/**
 * Internal: Convert the PreHeader fields to the appropriate endianess.
 * @param   enmConv     Direction of the conversion.
 * @param   pPreHdrConv Where to store the converted pre header.
 * @param   pPreHdr     PreHeader pointer.
 */
static void vdiConvPreHeaderEndianess(VDIECONV enmConv, PVDIPREHEADER pPreHdrConv,
                                      PVDIPREHEADER pPreHdr)
{
    memcpy(pPreHdrConv->szFileInfo, pPreHdr->szFileInfo, sizeof(pPreHdr->szFileInfo));
    pPreHdrConv->u32Signature = SET_ENDIAN_U32(enmConv, pPreHdr->u32Signature);
    pPreHdrConv->u32Version   = SET_ENDIAN_U32(enmConv, pPreHdr->u32Version);
}

/**
 * Internal: Convert the VDIDISKGEOMETRY fields to the appropriate endianess.
 * @param   enmConv      Direction of the conversion.
 * @param   pDiskGeoConv Where to store the converted geometry.
 * @param   pDiskGeo     Pointer to the disk geometry to convert.
 */
static void vdiConvGeometryEndianess(VDIECONV enmConv, PVDIDISKGEOMETRY pDiskGeoConv,
                                     PVDIDISKGEOMETRY pDiskGeo)
{
    pDiskGeoConv->cCylinders = SET_ENDIAN_U32(enmConv, pDiskGeo->cCylinders);
    pDiskGeoConv->cHeads     = SET_ENDIAN_U32(enmConv, pDiskGeo->cHeads);
    pDiskGeoConv->cSectors   = SET_ENDIAN_U32(enmConv, pDiskGeo->cSectors);
    pDiskGeoConv->cbSector   = SET_ENDIAN_U32(enmConv, pDiskGeo->cbSector);
}

/**
 * Internal: Convert the Header - version 0 fields to the appropriate endianess.
 * @param   enmConv      Direction of the conversion.
 * @param   pHdrConv     Where to store the converted header.
 * @param   pHdr         Pointer to the version 0 header.
 */
static void vdiConvHeaderEndianessV0(VDIECONV enmConv, PVDIHEADER0 pHdrConv,
                                     PVDIHEADER0 pHdr)
{
    memmove(pHdrConv->szComment, pHdr->szComment, sizeof(pHdr->szComment));
    pHdrConv->u32Type          = SET_ENDIAN_U32(enmConv, pHdr->u32Type);
    pHdrConv->fFlags           = SET_ENDIAN_U32(enmConv, pHdr->fFlags);
    vdiConvGeometryEndianess(enmConv, &pHdrConv->LegacyGeometry, &pHdr->LegacyGeometry);
    pHdrConv->cbDisk           = SET_ENDIAN_U64(enmConv, pHdr->cbDisk);
    pHdrConv->cbBlock          = SET_ENDIAN_U32(enmConv, pHdr->cbBlock);
    pHdrConv->cBlocks          = SET_ENDIAN_U32(enmConv, pHdr->cBlocks);
    pHdrConv->cBlocksAllocated = SET_ENDIAN_U32(enmConv, pHdr->cBlocksAllocated);
    /* Don't convert the RTUUID fields. */
    pHdrConv->uuidCreate       = pHdr->uuidCreate;
    pHdrConv->uuidModify       = pHdr->uuidModify;
    pHdrConv->uuidLinkage      = pHdr->uuidLinkage;
}

/**
 * Internal: Set the Header - version 1 fields to the appropriate endianess.
 * @param   enmConv      Direction of the conversion.
 * @param   pHdrConv     Where to store the converted header.
 * @param   pHdr         Version 1 Header pointer.
 */
static void vdiConvHeaderEndianessV1(VDIECONV enmConv, PVDIHEADER1 pHdrConv,
                                     PVDIHEADER1 pHdr)
{
    memmove(pHdrConv->szComment, pHdr->szComment, sizeof(pHdr->szComment));
    pHdrConv->cbHeader         = SET_ENDIAN_U32(enmConv, pHdr->cbHeader);
    pHdrConv->u32Type          = SET_ENDIAN_U32(enmConv, pHdr->u32Type);
    pHdrConv->fFlags           = SET_ENDIAN_U32(enmConv, pHdr->fFlags);
    pHdrConv->offBlocks        = SET_ENDIAN_U32(enmConv, pHdr->offBlocks);
    pHdrConv->offData          = SET_ENDIAN_U32(enmConv, pHdr->offData);
    vdiConvGeometryEndianess(enmConv, &pHdrConv->LegacyGeometry, &pHdr->LegacyGeometry);
    pHdrConv->u32Dummy         = SET_ENDIAN_U32(enmConv, pHdr->u32Dummy);
    pHdrConv->cbDisk           = SET_ENDIAN_U64(enmConv, pHdr->cbDisk);
    pHdrConv->cbBlock          = SET_ENDIAN_U32(enmConv, pHdr->cbBlock);
    pHdrConv->cbBlockExtra     = SET_ENDIAN_U32(enmConv, pHdr->cbBlockExtra);
    pHdrConv->cBlocks          = SET_ENDIAN_U32(enmConv, pHdr->cBlocks);
    pHdrConv->cBlocksAllocated = SET_ENDIAN_U32(enmConv, pHdr->cBlocksAllocated);
    /* Don't convert the RTUUID fields. */
    pHdrConv->uuidCreate       = pHdr->uuidCreate;
    pHdrConv->uuidModify       = pHdr->uuidModify;
    pHdrConv->uuidLinkage      = pHdr->uuidLinkage;
    pHdrConv->uuidParentModify = pHdr->uuidParentModify;
}

/**
 * Internal: Set the Header - version 1plus fields to the appropriate endianess.
 * @param   enmConv      Direction of the conversion.
 * @param   pHdrConv     Where to store the converted header.
 * @param   pHdr         Version 1+ Header pointer.
 */
static void vdiConvHeaderEndianessV1p(VDIECONV enmConv, PVDIHEADER1PLUS pHdrConv,
                                      PVDIHEADER1PLUS pHdr)
{
    memmove(pHdrConv->szComment, pHdr->szComment, sizeof(pHdr->szComment));
    pHdrConv->cbHeader         = SET_ENDIAN_U32(enmConv, pHdr->cbHeader);
    pHdrConv->u32Type          = SET_ENDIAN_U32(enmConv, pHdr->u32Type);
    pHdrConv->fFlags           = SET_ENDIAN_U32(enmConv, pHdr->fFlags);
    pHdrConv->offBlocks        = SET_ENDIAN_U32(enmConv, pHdr->offBlocks);
    pHdrConv->offData          = SET_ENDIAN_U32(enmConv, pHdr->offData);
    vdiConvGeometryEndianess(enmConv, &pHdrConv->LegacyGeometry, &pHdr->LegacyGeometry);
    pHdrConv->u32Dummy         = SET_ENDIAN_U32(enmConv, pHdr->u32Dummy);
    pHdrConv->cbDisk           = SET_ENDIAN_U64(enmConv, pHdr->cbDisk);
    pHdrConv->cbBlock          = SET_ENDIAN_U32(enmConv, pHdr->cbBlock);
    pHdrConv->cbBlockExtra     = SET_ENDIAN_U32(enmConv, pHdr->cbBlockExtra);
    pHdrConv->cBlocks          = SET_ENDIAN_U32(enmConv, pHdr->cBlocks);
    pHdrConv->cBlocksAllocated = SET_ENDIAN_U32(enmConv, pHdr->cBlocksAllocated);
    /* Don't convert the RTUUID fields. */
    pHdrConv->uuidCreate       = pHdr->uuidCreate;
    pHdrConv->uuidModify       = pHdr->uuidModify;
    pHdrConv->uuidLinkage      = pHdr->uuidLinkage;
    pHdrConv->uuidParentModify = pHdr->uuidParentModify;
    vdiConvGeometryEndianess(enmConv, &pHdrConv->LCHSGeometry, &pHdr->LCHSGeometry);
}


/**
 * Internal: Set the appropriate endianess on all the Blocks pointed.
 * @param   enmConv      Direction of the conversion.
 * @param   paBlocks     Pointer to the block array.
 * @param   cEntries     Number of entries in the block array.
 *
 * @note Unlike the other conversion functions this method does an in place conversion
 *       to avoid temporary memory allocations when writing the block array.
 */
static void vdiConvBlocksEndianess(VDIECONV enmConv, PVDIIMAGEBLOCKPOINTER paBlocks,
                                   unsigned cEntries)
{
    for (unsigned i = 0; i < cEntries; i++)
        paBlocks[i] = SET_ENDIAN_U32(enmConv, paBlocks[i]);
}

/**
 * Internal: Flush the image file to disk.
 */
static void vdiFlushImage(PVDIIMAGEDESC pImage)
{
    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        /* Save header. */
        int rc = vdiUpdateHeader(pImage);
        AssertMsgRC(rc, ("vdiUpdateHeader() failed, filename=\"%s\", rc=%Rrc\n",
                         pImage->pszFilename, rc));
        vdIfIoIntFileFlushSync(pImage->pIfIo, pImage->pStorage);
    }
}

/**
 * Internal: Free all allocated space for representing an image, and optionally
 * delete the image from disk.
 */
static int vdiFreeImage(PVDIIMAGEDESC pImage, bool fDelete)
{
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
    {
        if (pImage->pStorage)
        {
            /* No point updating the file that is deleted anyway. */
            if (!fDelete)
                vdiFlushImage(pImage);

            rc = vdIfIoIntFileClose(pImage->pIfIo, pImage->pStorage);
            pImage->pStorage = NULL;
        }

        if (pImage->paBlocks)
        {
            RTMemFree(pImage->paBlocks);
            pImage->paBlocks = NULL;
        }

        if (pImage->paBlocksRev)
        {
            RTMemFree(pImage->paBlocksRev);
            pImage->paBlocksRev = NULL;
        }

        if (fDelete && pImage->pszFilename)
        {
            int rc2 = vdIfIoIntFileDelete(pImage->pIfIo, pImage->pszFilename);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * internal: return power of 2 or 0 if num error.
 */
static unsigned getPowerOfTwo(unsigned uNumber)
{
    if (uNumber == 0)
        return 0;
    unsigned uPower2 = 0;
    while ((uNumber & 1) == 0)
    {
        uNumber >>= 1;
        uPower2++;
    }
    return uNumber == 1 ? uPower2 : 0;
}

/**
 * Internal: Init VDI preheader.
 */
static void vdiInitPreHeader(PVDIPREHEADER pPreHdr)
{
    pPreHdr->u32Signature = VDI_IMAGE_SIGNATURE;
    pPreHdr->u32Version = VDI_IMAGE_VERSION;
    memset(pPreHdr->szFileInfo, 0, sizeof(pPreHdr->szFileInfo));
    strncat(pPreHdr->szFileInfo, VDI_IMAGE_FILE_INFO, sizeof(pPreHdr->szFileInfo)-1);
}

/**
 * Internal: check VDI preheader.
 */
static int vdiValidatePreHeader(PVDIPREHEADER pPreHdr)
{
    if (pPreHdr->u32Signature != VDI_IMAGE_SIGNATURE)
        return VERR_VD_VDI_INVALID_HEADER;

    if (    VDI_GET_VERSION_MAJOR(pPreHdr->u32Version) != VDI_IMAGE_VERSION_MAJOR
        &&  pPreHdr->u32Version != 0x00000002)    /* old version. */
        return VERR_VD_VDI_UNSUPPORTED_VERSION;

    return VINF_SUCCESS;
}

/**
 * Internal: translate VD image flags to VDI image type enum.
 */
static VDIIMAGETYPE vdiTranslateImageFlags2VDI(unsigned uImageFlags)
{
    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
        return VDI_IMAGE_TYPE_FIXED;
    else if (uImageFlags & VD_IMAGE_FLAGS_DIFF)
        return VDI_IMAGE_TYPE_DIFF;
    else
        return VDI_IMAGE_TYPE_NORMAL;
}

/**
 * Internal: translate VDI image type enum to VD image type enum.
 */
static unsigned vdiTranslateVDI2ImageFlags(VDIIMAGETYPE enmType)
{
    switch (enmType)
    {
        case VDI_IMAGE_TYPE_NORMAL:
            return VD_IMAGE_FLAGS_NONE;
        case VDI_IMAGE_TYPE_FIXED:
            return VD_IMAGE_FLAGS_FIXED;
        case VDI_IMAGE_TYPE_DIFF:
            return VD_IMAGE_FLAGS_DIFF;
        default:
            AssertMsgFailed(("invalid VDIIMAGETYPE enmType=%d\n", (int)enmType));
            return VD_IMAGE_FLAGS_NONE;
    }
}

/**
 * Internal: Init VDI header. Always use latest header version.
 *
 * @param   pHeader      Assumes it was initially initialized to all zeros.
 * @param   uImageFlags  Flags for this image.
 * @param   pszComment   Optional comment to set for the image.
 * @param   cbDisk       Size of the disk in bytes.
 * @param   cbBlock      Size of one block in the image.
 * @param   cbBlockExtra Extra data for one block private to the image.
 * @param   cbDataAlign  The alignment for all data structures.
 */
static void vdiInitHeader(PVDIHEADER pHeader, uint32_t uImageFlags,
                          const char *pszComment, uint64_t cbDisk,
                          uint32_t cbBlock, uint32_t cbBlockExtra,
                          uint32_t cbDataAlign)
{
    pHeader->uVersion = VDI_IMAGE_VERSION;
    pHeader->u.v1plus.cbHeader = sizeof(VDIHEADER1PLUS);
    pHeader->u.v1plus.u32Type = (uint32_t)vdiTranslateImageFlags2VDI(uImageFlags);
    pHeader->u.v1plus.fFlags = (uImageFlags & VD_VDI_IMAGE_FLAGS_ZERO_EXPAND) ? 1 : 0;
#ifdef VBOX_STRICT
    char achZero[VDI_IMAGE_COMMENT_SIZE] = {0};
    Assert(!memcmp(pHeader->u.v1plus.szComment, achZero, VDI_IMAGE_COMMENT_SIZE));
#endif
    pHeader->u.v1plus.szComment[0] = '\0';
    if (pszComment)
    {
        AssertMsg(strlen(pszComment) < sizeof(pHeader->u.v1plus.szComment),
                  ("HDD Comment is too long, cb=%d\n", strlen(pszComment)));
        strncat(pHeader->u.v1plus.szComment, pszComment, sizeof(pHeader->u.v1plus.szComment)-1);
    }

    /* Mark the legacy geometry not-calculated. */
    pHeader->u.v1plus.LegacyGeometry.cCylinders = 0;
    pHeader->u.v1plus.LegacyGeometry.cHeads = 0;
    pHeader->u.v1plus.LegacyGeometry.cSectors = 0;
    pHeader->u.v1plus.LegacyGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;
    pHeader->u.v1plus.u32Dummy = 0; /* used to be the translation value */

    pHeader->u.v1plus.cbDisk = cbDisk;
    pHeader->u.v1plus.cbBlock = cbBlock;
    pHeader->u.v1plus.cBlocks = (uint32_t)(cbDisk / cbBlock);
    if (cbDisk % cbBlock)
        pHeader->u.v1plus.cBlocks++;
    pHeader->u.v1plus.cbBlockExtra = cbBlockExtra;
    pHeader->u.v1plus.cBlocksAllocated = 0;

    /* Init offsets. */
    pHeader->u.v1plus.offBlocks = RT_ALIGN_32(sizeof(VDIPREHEADER) + sizeof(VDIHEADER1PLUS), cbDataAlign);
    pHeader->u.v1plus.offData = RT_ALIGN_32(pHeader->u.v1plus.offBlocks + (pHeader->u.v1plus.cBlocks * sizeof(VDIIMAGEBLOCKPOINTER)), cbDataAlign);

    /* Init uuids. */
#ifdef _MSC_VER
# pragma warning(disable:4366) /* (harmless "misalignment") */
#endif
    RTUuidCreate(&pHeader->u.v1plus.uuidCreate);
    RTUuidClear(&pHeader->u.v1plus.uuidModify);
    RTUuidClear(&pHeader->u.v1plus.uuidLinkage);
    RTUuidClear(&pHeader->u.v1plus.uuidParentModify);
#ifdef _MSC_VER
# pragma warning(default:4366)
#endif

    /* Mark LCHS geometry not-calculated. */
    pHeader->u.v1plus.LCHSGeometry.cCylinders = 0;
    pHeader->u.v1plus.LCHSGeometry.cHeads = 0;
    pHeader->u.v1plus.LCHSGeometry.cSectors = 0;
    pHeader->u.v1plus.LCHSGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;
}

/**
 * Internal: Check VDI header.
 */
static int vdiValidateHeader(PVDIHEADER pHeader)
{
    /* Check version-dependent header parameters. */
    switch (GET_MAJOR_HEADER_VERSION(pHeader))
    {
        case 0:
        {
            /* Old header version. */
            break;
        }
        case 1:
        {
            /* Current header version. */

            if (pHeader->u.v1.cbHeader < sizeof(VDIHEADER1))
            {
                LogRel(("VDI: v1 header size wrong (%d < %d)\n",
                       pHeader->u.v1.cbHeader, sizeof(VDIHEADER1)));
                return VERR_VD_VDI_INVALID_HEADER;
            }

            if (getImageBlocksOffset(pHeader) < (sizeof(VDIPREHEADER) + sizeof(VDIHEADER1)))
            {
                LogRel(("VDI: v1 blocks offset wrong (%d < %d)\n",
                       getImageBlocksOffset(pHeader), sizeof(VDIPREHEADER) + sizeof(VDIHEADER1)));
                return VERR_VD_VDI_INVALID_HEADER;
            }

            if (getImageDataOffset(pHeader) < (getImageBlocksOffset(pHeader) + getImageBlocks(pHeader) * sizeof(VDIIMAGEBLOCKPOINTER)))
            {
                LogRel(("VDI: v1 image data offset wrong (%d < %d)\n",
                       getImageDataOffset(pHeader), getImageBlocksOffset(pHeader) + getImageBlocks(pHeader) * sizeof(VDIIMAGEBLOCKPOINTER)));
                return VERR_VD_VDI_INVALID_HEADER;
            }

            break;
        }
        default:
            /* Unsupported. */
            return VERR_VD_VDI_UNSUPPORTED_VERSION;
    }

    /* Check common header parameters. */

    bool fFailed = false;

    if (    getImageType(pHeader) < VDI_IMAGE_TYPE_FIRST
        ||  getImageType(pHeader) > VDI_IMAGE_TYPE_LAST)
    {
        LogRel(("VDI: bad image type %d\n", getImageType(pHeader)));
        fFailed = true;
    }

    if (getImageFlags(pHeader) & ~VD_VDI_IMAGE_FLAGS_MASK)
    {
        LogRel(("VDI: bad image flags %08x\n", getImageFlags(pHeader)));
        fFailed = true;
    }

    PVDIDISKGEOMETRY pLCHSGeom = getImageLCHSGeometry(pHeader);
    if (   pLCHSGeom
        && pLCHSGeom->cbSector != VDI_GEOMETRY_SECTOR_SIZE)
    {
        LogRel(("VDI: wrong sector size (%d != %d)\n", pLCHSGeom->cbSector, VDI_GEOMETRY_SECTOR_SIZE));
        fFailed = true;
    }

    if (    getImageDiskSize(pHeader) == 0
        ||  getImageBlockSize(pHeader) == 0
        ||  getImageBlocks(pHeader) == 0
        ||  getPowerOfTwo(getImageBlockSize(pHeader)) == 0)
    {
        LogRel(("VDI: wrong size (%lld, %d, %d, %d)\n",
              getImageDiskSize(pHeader), getImageBlockSize(pHeader),
              getImageBlocks(pHeader), getPowerOfTwo(getImageBlockSize(pHeader))));
        fFailed = true;
    }

    if (getImageBlocksAllocated(pHeader) > getImageBlocks(pHeader))
    {
        LogRel(("VDI: too many blocks allocated (%d > %d)\n"
                "     blocksize=%d disksize=%lld\n",
              getImageBlocksAllocated(pHeader), getImageBlocks(pHeader),
              getImageBlockSize(pHeader), getImageDiskSize(pHeader)));
        fFailed = true;
    }

    if (    getImageExtraBlockSize(pHeader) != 0
        &&  getPowerOfTwo(getImageExtraBlockSize(pHeader)) == 0)
    {
        LogRel(("VDI: wrong extra size (%d, %d)\n",
               getImageExtraBlockSize(pHeader), getPowerOfTwo(getImageExtraBlockSize(pHeader))));
        fFailed = true;
    }

    if ((uint64_t)getImageBlockSize(pHeader) * getImageBlocks(pHeader) < getImageDiskSize(pHeader))
    {
        LogRel(("VDI: wrong disk size (%d, %d, %lld)\n",
               getImageBlockSize(pHeader), getImageBlocks(pHeader), getImageDiskSize(pHeader)));
        fFailed = true;
    }

    if (RTUuidIsNull(getImageCreationUUID(pHeader)))
    {
        LogRel(("VDI: uuid of creator is 0\n"));
        fFailed = true;
    }

    if (RTUuidIsNull(getImageModificationUUID(pHeader)))
    {
        LogRel(("VDI: uuid of modifier is 0\n"));
        fFailed = true;
    }

    return fFailed ? VERR_VD_VDI_INVALID_HEADER : VINF_SUCCESS;
}

/**
 * Internal: Set up VDIIMAGEDESC structure by image header.
 */
static void vdiSetupImageDesc(PVDIIMAGEDESC pImage)
{
    pImage->uImageFlags        = getImageFlags(&pImage->Header);
    pImage->uImageFlags       |= vdiTranslateVDI2ImageFlags(getImageType(&pImage->Header));
    pImage->offStartBlocks     = getImageBlocksOffset(&pImage->Header);
    pImage->offStartData       = getImageDataOffset(&pImage->Header);
    pImage->uBlockMask         = getImageBlockSize(&pImage->Header) - 1;
    pImage->uShiftOffset2Index = getPowerOfTwo(getImageBlockSize(&pImage->Header));
    pImage->offStartBlockData  = getImageExtraBlockSize(&pImage->Header);
    pImage->cbAllocationBlock  = getImageBlockSize(&pImage->Header);
    pImage->cbTotalBlockData   = pImage->offStartBlockData
                                 + getImageBlockSize(&pImage->Header);
}

/**
 * Sets up the complete image state from the given parameters.
 *
 * @returns VBox status code.
 * @param   pImage            The VDI image descriptor.
 * @param   uImageFlags       Image flags.
 * @param   pszComment        The comment for the image (optional).
 * @param   cbSize            Size of the resulting image in bytes.
 * @param   cbAllocationBlock Size of blocks allocated
 * @param   cbDataAlign       Data alignment in bytes.
 * @param   pPCHSGeometry     Physical CHS geometry for the image.
 * @param   pLCHSGeometry     Logical CHS geometry for the image.
 */
static int vdiSetupImageState(PVDIIMAGEDESC pImage, unsigned uImageFlags, const char *pszComment,
                              uint64_t cbSize, uint32_t cbAllocationBlock, uint32_t cbDataAlign, PCVDGEOMETRY pPCHSGeometry,
                              PCVDGEOMETRY pLCHSGeometry)
{
    int rc = VINF_SUCCESS;

    vdiInitPreHeader(&pImage->PreHeader);
    vdiInitHeader(&pImage->Header, uImageFlags, pszComment, cbSize, cbAllocationBlock, 0,
                  cbDataAlign);
    /* Save PCHS geometry. Not much work, and makes the flow of information
     * quite a bit clearer - relying on the higher level isn't obvious. */
    pImage->PCHSGeometry = *pPCHSGeometry;
    /* Set LCHS geometry (legacy geometry is ignored for the current 1.1+). */
    pImage->Header.u.v1plus.LCHSGeometry.cCylinders = pLCHSGeometry->cCylinders;
    pImage->Header.u.v1plus.LCHSGeometry.cHeads = pLCHSGeometry->cHeads;
    pImage->Header.u.v1plus.LCHSGeometry.cSectors = pLCHSGeometry->cSectors;
    pImage->Header.u.v1plus.LCHSGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;

    pImage->paBlocks = (PVDIIMAGEBLOCKPOINTER)RTMemAlloc(sizeof(VDIIMAGEBLOCKPOINTER) * getImageBlocks(&pImage->Header));
    if (RT_LIKELY(pImage->paBlocks))
    {
        if (!(uImageFlags & VD_IMAGE_FLAGS_FIXED))
        {
            /* for growing images mark all blocks in paBlocks as free. */
            for (unsigned i = 0; i < pImage->Header.u.v1.cBlocks; i++)
                pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
        }
        else
        {
            /* for fixed images mark all blocks in paBlocks as allocated */
            for (unsigned i = 0; i < pImage->Header.u.v1.cBlocks; i++)
                pImage->paBlocks[i] = i;
            pImage->Header.u.v1.cBlocksAllocated = pImage->Header.u.v1.cBlocks;
        }

        /* Setup image parameters. */
        vdiSetupImageDesc(pImage);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Creates the image file from the given descriptor.
 *
 * @returns VBox status code.
 * @param   pImage          The VDI image descriptor.
 * @param   uOpenFlags      Open flags.
 * @param   pIfProgress     The progress interface.
 * @param   uPercentStart   Progress starting point.
 * @param   uPercentSpan    How many percent for this part of the operation is used.
 */
static int vdiImageCreateFile(PVDIIMAGEDESC pImage, unsigned uOpenFlags,
                              PVDINTERFACEPROGRESS pIfProgress, unsigned uPercentStart,
                              unsigned uPercentSpan)
{
    int rc = vdIfIoIntFileOpen(pImage->pIfIo, pImage->pszFilename,
                               VDOpenFlagsToFileOpenFlags(uOpenFlags & ~VD_OPEN_FLAGS_READONLY,
                                                          true /* fCreate */),
                               &pImage->pStorage);
    if (RT_SUCCESS(rc))
    {
        if (pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED)
        {
            uint64_t cbTotal =   pImage->offStartData
                               + (uint64_t)getImageBlocks(&pImage->Header) * pImage->cbTotalBlockData;

            /* Check the free space on the disk and leave early if there is not
             * sufficient space available. */
            int64_t cbFree = 0;
            rc = vdIfIoIntFileGetFreeSpace(pImage->pIfIo, pImage->pszFilename, &cbFree);
            if (RT_SUCCESS(rc) /* ignore errors */ && ((uint64_t)cbFree < cbTotal))
                rc = vdIfError(pImage->pIfError, VERR_DISK_FULL, RT_SRC_POS,
                               N_("VDI: disk would overflow creating image '%s'"), pImage->pszFilename);
            else
            {
                /*
                 * Allocate & commit whole file if fixed image, it must be more
                 * effective than expanding file by write operations.
                 */
                rc = vdIfIoIntFileSetAllocationSize(pImage->pIfIo, pImage->pStorage, cbTotal, 0 /* fFlags */,
                                                    pIfProgress, uPercentStart, uPercentSpan);
                pImage->cbImage = cbTotal;
            }
        }
        else
        {
            /* Set file size to hold header and blocks array. */
            rc = vdIfIoIntFileSetSize(pImage->pIfIo, pImage->pStorage, pImage->offStartData);
            pImage->cbImage = pImage->offStartData;
        }
        if (RT_SUCCESS(rc))
        {
            /* Write pre-header. */
            VDIPREHEADER PreHeader;
            vdiConvPreHeaderEndianess(VDIECONV_H2F, &PreHeader, &pImage->PreHeader);
            rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, 0,
                                        &PreHeader, sizeof(PreHeader));
            if (RT_SUCCESS(rc))
            {
                /* Write header. */
                VDIHEADER1PLUS Hdr;
                vdiConvHeaderEndianessV1p(VDIECONV_H2F, &Hdr, &pImage->Header.u.v1plus);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, sizeof(pImage->PreHeader),
                                            &Hdr, sizeof(Hdr));
                if (RT_SUCCESS(rc))
                {
                    vdiConvBlocksEndianess(VDIECONV_H2F, pImage->paBlocks, getImageBlocks(&pImage->Header));
                    rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, pImage->offStartBlocks, pImage->paBlocks,
                                                getImageBlocks(&pImage->Header) * sizeof(VDIIMAGEBLOCKPOINTER));
                    vdiConvBlocksEndianess(VDIECONV_F2H, pImage->paBlocks, getImageBlocks(&pImage->Header));
                    if (RT_FAILURE(rc))
                        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: writing block pointers failed for '%s'"),
                                       pImage->pszFilename);
                }
                else
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: writing header failed for '%s'"),
                                   pImage->pszFilename);
            }
            else
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: writing pre-header failed for '%s'"),
                               pImage->pszFilename);
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: setting image size failed for '%s'"),
                           pImage->pszFilename);
    }
    else
        rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: cannot create image '%s'"),
                       pImage->pszFilename);

    return rc;
}

/**
 * Internal: Create VDI image file.
 */
static int vdiCreateImage(PVDIIMAGEDESC pImage, uint64_t cbSize,
                          unsigned uImageFlags, const char *pszComment,
                          PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                          PCRTUUID pUuid, unsigned uOpenFlags,
                          PVDINTERFACEPROGRESS pIfProgress, unsigned uPercentStart,
                          unsigned uPercentSpan, PVDINTERFACECONFIG pIfCfg)
{
    int rc = VINF_SUCCESS;
    uint32_t cbDataAlign = VDI_DATA_ALIGN;
    AssertPtr(pPCHSGeometry);
    AssertPtr(pLCHSGeometry);

    pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    /* Special check for comment length. */
    if (   RT_VALID_PTR(pszComment)
        && strlen(pszComment) >= VDI_IMAGE_COMMENT_SIZE)
        rc = vdIfError(pImage->pIfError, VERR_VD_VDI_COMMENT_TOO_LONG, RT_SRC_POS,
                       N_("VDI: comment is too long for '%s'"), pImage->pszFilename);

    PVDINTERFACECONFIG pImgCfg = VDIfConfigGet(pImage->pVDIfsImage);
    if (pImgCfg)
    {
        rc = VDCFGQueryU32Def(pImgCfg, "AllocationBlockSize",
                &pImage->cbAllocationBlock, VDI_IMAGE_DEFAULT_BLOCK_SIZE);
        if (RT_FAILURE(rc))
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           N_("VDI: Getting AllocationBlockSize for '%s' failed (%Rrc)"), pImage->pszFilename, rc);
    } else
        pImage->cbAllocationBlock = VDI_IMAGE_DEFAULT_BLOCK_SIZE;

    if (pIfCfg)
    {
        rc = VDCFGQueryU32Def(pIfCfg, "DataAlignment", &cbDataAlign, VDI_DATA_ALIGN);
        if (RT_FAILURE(rc))
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS,
                           N_("VDI: Getting data alignment for '%s' failed (%Rrc)"), pImage->pszFilename, rc);
    }

    if (RT_SUCCESS(rc))
    {

        rc = vdiSetupImageState(pImage, uImageFlags, pszComment, cbSize,
                pImage->cbAllocationBlock, cbDataAlign, pPCHSGeometry, pLCHSGeometry);

        if (RT_SUCCESS(rc))
        {
            /* Use specified image uuid */
            *getImageCreationUUID(&pImage->Header) = *pUuid;
            /* Generate image last-modify uuid */
            RTUuidCreate(getImageModificationUUID(&pImage->Header));

            rc = vdiImageCreateFile(pImage, uOpenFlags, pIfProgress,
                                    uPercentStart, uPercentSpan);
        }
    }

    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = 512;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = 512;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = getImageDiskSize(&pImage->Header);

        vdIfProgress(pIfProgress, uPercentStart + uPercentSpan);
    }

    if (RT_FAILURE(rc))
        vdiFreeImage(pImage, rc != VERR_ALREADY_EXISTS);
    return rc;
}

/**
 * Reads and validates the header for the given image descriptor.
 *
 * @returns VBox status code.
 * @param   pImage          The VDI image descriptor.
 */
static int vdiImageReadHeader(PVDIIMAGEDESC pImage)
{
    /* Get file size. */
    int rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage,
                                  &pImage->cbImage);
    if (RT_SUCCESS(rc))
    {
        /* Read pre-header. */
        VDIPREHEADER PreHeader;
        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, 0,
                                   &PreHeader, sizeof(PreHeader));
        if (RT_SUCCESS(rc))
        {
            vdiConvPreHeaderEndianess(VDIECONV_F2H, &pImage->PreHeader, &PreHeader);
            rc = vdiValidatePreHeader(&pImage->PreHeader);
            if (RT_SUCCESS(rc))
            {
                /* Read header. */
                pImage->Header.uVersion = pImage->PreHeader.u32Version;
                switch (GET_MAJOR_HEADER_VERSION(&pImage->Header))
                {
                    case 0:
                        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, sizeof(pImage->PreHeader),
                                                   &pImage->Header.u.v0, sizeof(pImage->Header.u.v0));
                        if (RT_SUCCESS(rc))
                            vdiConvHeaderEndianessV0(VDIECONV_F2H, &pImage->Header.u.v0, &pImage->Header.u.v0);
                        else
                            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: error reading v0 header in '%s'"), pImage->pszFilename);
                        break;
                    case 1:
                        rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, sizeof(pImage->PreHeader),
                                                   &pImage->Header.u.v1, sizeof(pImage->Header.u.v1));
                        if (RT_SUCCESS(rc))
                        {
                            vdiConvHeaderEndianessV1(VDIECONV_F2H, &pImage->Header.u.v1, &pImage->Header.u.v1);
                            /* Convert VDI 1.1 images to VDI 1.1+ on open in read/write mode.
                             * Conversion is harmless, as any VirtualBox version supporting VDI
                             * 1.1 doesn't touch fields it doesn't know about. */
                            if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                                && GET_MINOR_HEADER_VERSION(&pImage->Header) == 1
                                && pImage->Header.u.v1.cbHeader < sizeof(pImage->Header.u.v1plus))
                            {
                                pImage->Header.u.v1plus.cbHeader = sizeof(pImage->Header.u.v1plus);
                                /* Mark LCHS geometry not-calculated. */
                                pImage->Header.u.v1plus.LCHSGeometry.cCylinders = 0;
                                pImage->Header.u.v1plus.LCHSGeometry.cHeads = 0;
                                pImage->Header.u.v1plus.LCHSGeometry.cSectors = 0;
                                pImage->Header.u.v1plus.LCHSGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;
                            }
                            else if (pImage->Header.u.v1.cbHeader >= sizeof(pImage->Header.u.v1plus))
                            {
                                /* Read the actual VDI 1.1+ header completely. */
                                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, sizeof(pImage->PreHeader),
                                                           &pImage->Header.u.v1plus,
                                                           sizeof(pImage->Header.u.v1plus));
                                if (RT_SUCCESS(rc))
                                    vdiConvHeaderEndianessV1p(VDIECONV_F2H, &pImage->Header.u.v1plus, &pImage->Header.u.v1plus);
                                else
                                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: error reading v1.1+ header in '%s'"), pImage->pszFilename);
                            }
                        }
                        else
                            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: error reading v1 header in '%s'"), pImage->pszFilename);
                        break;
                    default:
                        rc = vdIfError(pImage->pIfError, VERR_VD_VDI_UNSUPPORTED_VERSION, RT_SRC_POS,
                                       N_("VDI: unsupported major version %u in '%s'"), GET_MAJOR_HEADER_VERSION(&pImage->Header), pImage->pszFilename);
                }

                if (RT_SUCCESS(rc))
                {
                    rc = vdiValidateHeader(&pImage->Header);
                    if (RT_SUCCESS(rc))
                    {
                        /* Setup image parameters by header. */
                        vdiSetupImageDesc(pImage);

                        /*
                         * Until revision r111992 there was no check that the size was sector aligned
                         * when creating a new image and a bug in the VirtualBox GUI on OS X resulted
                         * in such images being created which caused issues when writing to the
                         * end of the image.
                         *
                         * Detect such images and repair the small damage by rounding down to the next
                         * aligned size. This is no problem as the guest would see a sector count
                         * only anyway from the device emulations so it already sees only the smaller
                         * size as result of the integer division of the size and sector size.
                         *
                         * This might not be written to the image if it is opened readonly
                         * which is not much of a problem because only writing to the last block
                         * causes trouble.
                         */
                        uint64_t cbDisk = getImageDiskSize(&pImage->Header);
                        if (cbDisk & 0x1ff)
                            setImageDiskSize(&pImage->Header, cbDisk & ~UINT64_C(0x1ff));
                    }
                    else
                        rc = vdIfError(pImage->pIfError, VERR_VD_VDI_INVALID_HEADER, RT_SRC_POS,
                                       N_("VDI: invalid header in '%s'"), pImage->pszFilename);
                }
            }
            else
                rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: invalid pre-header in '%s'"), pImage->pszFilename);
        }
        else
        {
            vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: error reading pre-header in '%s'"), pImage->pszFilename);
            rc = VERR_VD_VDI_INVALID_HEADER;
        }
    }
    else
    {
        vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: error getting the image size in '%s'"), pImage->pszFilename);
        rc = VERR_VD_VDI_INVALID_HEADER;
    }

    return rc;
}

/**
 * Creates the back resolving table for the image for the discard operation.
 *
 * @returns VBox status code.
 * @param   pImage          The VDI image descriptor.
 */
static int vdiImageBackResolvTblCreate(PVDIIMAGEDESC pImage)
{
    int rc = VINF_SUCCESS;

    /*
     * Any error or inconsistency results in a fail because this might
     * get us into trouble later on.
     */
    pImage->paBlocksRev = (unsigned *)RTMemAllocZ(sizeof(unsigned) * getImageBlocks(&pImage->Header));
    if (pImage->paBlocksRev)
    {
        unsigned cBlocksAllocated = getImageBlocksAllocated(&pImage->Header);
        unsigned cBlocks = getImageBlocks(&pImage->Header);

        for (unsigned i = 0; i < cBlocks; i++)
            pImage->paBlocksRev[i] = VDI_IMAGE_BLOCK_FREE;

        for (unsigned i = 0; i < cBlocks; i++)
        {
            VDIIMAGEBLOCKPOINTER ptrBlock = pImage->paBlocks[i];
            if (IS_VDI_IMAGE_BLOCK_ALLOCATED(ptrBlock))
            {
                if (ptrBlock < cBlocksAllocated)
                {
                    if (pImage->paBlocksRev[ptrBlock] == VDI_IMAGE_BLOCK_FREE)
                        pImage->paBlocksRev[ptrBlock] = i;
                    else
                    {
                        rc = VERR_VD_VDI_INVALID_HEADER;
                        break;
                    }
                }
                else
                {
                    rc = VERR_VD_VDI_INVALID_HEADER;
                    break;
                }
            }
        }
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Internal: Open a VDI image.
 */
static int vdiOpenImage(PVDIIMAGEDESC pImage, unsigned uOpenFlags)
{
    pImage->uOpenFlags = uOpenFlags;

    pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    /*
     * Open the image.
     */
    int rc = vdIfIoIntFileOpen(pImage->pIfIo, pImage->pszFilename,
                               VDOpenFlagsToFileOpenFlags(uOpenFlags, false /* fCreate */),
                               &pImage->pStorage);
    if (RT_SUCCESS(rc))
    {
        rc = vdiImageReadHeader(pImage);
        if (RT_SUCCESS(rc))
        {
            /* Allocate memory for blocks array. */
            pImage->paBlocks = (PVDIIMAGEBLOCKPOINTER)RTMemAlloc(sizeof(VDIIMAGEBLOCKPOINTER) * getImageBlocks(&pImage->Header));
            if (RT_LIKELY(pImage->paBlocks))
            {
                /* Read blocks array. */
                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, pImage->offStartBlocks, pImage->paBlocks,
                                           getImageBlocks(&pImage->Header) * sizeof(VDIIMAGEBLOCKPOINTER));
                if (RT_SUCCESS(rc))
                {
                    vdiConvBlocksEndianess(VDIECONV_F2H, pImage->paBlocks, getImageBlocks(&pImage->Header));

                    if (uOpenFlags & VD_OPEN_FLAGS_DISCARD)
                        rc = vdiImageBackResolvTblCreate(pImage);
                }
                else
                    rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("VDI: Error reading the block table in '%s'"), pImage->pszFilename);
            }
            else
                rc = vdIfError(pImage->pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                               N_("VDI: Error allocating memory for the block table in '%s'"), pImage->pszFilename);;
        }
    }
    /* else: Do NOT signal an appropriate error here, as the VD layer has the
     *       choice of retrying the open if it failed. */

    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = 512;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = 512;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = getImageDiskSize(&pImage->Header);
        if (uOpenFlags & VD_OPEN_FLAGS_INFO)
        {
            PVDINTERFACECONFIG pImgCfg = VDIfConfigGet(pImage->pVDIfsImage);
            if (pImgCfg)
            {
                rc = VDCFGUpdateU64(pImgCfg, true, "AllocationBlockSize", pImage->cbAllocationBlock);
                if (RT_FAILURE(rc))
                    return rc;
            }
        }
    }
    else
        vdiFreeImage(pImage, false);
    return rc;
}

/**
 * Internal: Save header to file.
 */
static int vdiUpdateHeader(PVDIIMAGEDESC pImage)
{
    int rc;
    switch (GET_MAJOR_HEADER_VERSION(&pImage->Header))
    {
        case 0:
        {
            VDIHEADER0 Hdr;
            vdiConvHeaderEndianessV0(VDIECONV_H2F, &Hdr, &pImage->Header.u.v0);
            rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, sizeof(VDIPREHEADER),
                                        &Hdr, sizeof(Hdr));
            break;
        }
        case 1:
            if (pImage->Header.u.v1plus.cbHeader < sizeof(pImage->Header.u.v1plus))
            {
                VDIHEADER1 Hdr;
                vdiConvHeaderEndianessV1(VDIECONV_H2F, &Hdr, &pImage->Header.u.v1);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, sizeof(VDIPREHEADER),
                                            &Hdr, sizeof(Hdr));
            }
            else
            {
                VDIHEADER1PLUS Hdr;
                vdiConvHeaderEndianessV1p(VDIECONV_H2F, &Hdr, &pImage->Header.u.v1plus);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, sizeof(VDIPREHEADER),
                                            &Hdr, sizeof(Hdr));
            }
            break;
        default:
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            break;
    }
    AssertMsgRC(rc, ("vdiUpdateHeader failed, filename=\"%s\" rc=%Rrc\n", pImage->pszFilename, rc));
    return rc;
}

/**
 * Internal: Save header to file - async version.
 */
static int vdiUpdateHeaderAsync(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx)
{
    int rc;
    switch (GET_MAJOR_HEADER_VERSION(&pImage->Header))
    {
        case 0:
        {
            VDIHEADER0 Hdr;
            vdiConvHeaderEndianessV0(VDIECONV_H2F, &Hdr, &pImage->Header.u.v0);
            rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pImage->pStorage,
                                        sizeof(VDIPREHEADER), &Hdr, sizeof(Hdr),
                                        pIoCtx, NULL, NULL);
            break;
        }
        case 1:
            if (pImage->Header.u.v1plus.cbHeader < sizeof(pImage->Header.u.v1plus))
            {
                VDIHEADER1 Hdr;
                vdiConvHeaderEndianessV1(VDIECONV_H2F, &Hdr, &pImage->Header.u.v1);
                rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pImage->pStorage,
                                            sizeof(VDIPREHEADER), &Hdr, sizeof(Hdr),
                                            pIoCtx, NULL, NULL);
            }
            else
            {
                VDIHEADER1PLUS Hdr;
                vdiConvHeaderEndianessV1p(VDIECONV_H2F, &Hdr, &pImage->Header.u.v1plus);
                rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pImage->pStorage,
                                            sizeof(VDIPREHEADER), &Hdr, sizeof(Hdr),
                                            pIoCtx, NULL, NULL);
            }
            break;
        default:
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            break;
    }
    AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
              ("vdiUpdateHeader failed, filename=\"%s\" rc=%Rrc\n", pImage->pszFilename, rc));
    return rc;
}

/**
 * Internal: Save block pointer to file, save header to file.
 */
static int vdiUpdateBlockInfo(PVDIIMAGEDESC pImage, unsigned uBlock)
{
    /* Update image header. */
    int rc = vdiUpdateHeader(pImage);
    if (RT_SUCCESS(rc))
    {
        /* write only one block pointer. */
        VDIIMAGEBLOCKPOINTER ptrBlock = RT_H2LE_U32(pImage->paBlocks[uBlock]);
        rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage,
                                    pImage->offStartBlocks + uBlock * sizeof(VDIIMAGEBLOCKPOINTER),
                                    &ptrBlock, sizeof(VDIIMAGEBLOCKPOINTER));
        AssertMsgRC(rc, ("vdiUpdateBlockInfo failed to update block=%u, filename=\"%s\", rc=%Rrc\n",
                         uBlock, pImage->pszFilename, rc));
    }
    return rc;
}

/**
 * Internal: Save block pointer to file, save header to file - async version.
 */
static int vdiUpdateBlockInfoAsync(PVDIIMAGEDESC pImage, unsigned uBlock,
                                   PVDIOCTX pIoCtx, bool fUpdateHdr)
{
    int rc = VINF_SUCCESS;

    /* Update image header. */
    if (fUpdateHdr)
        rc = vdiUpdateHeaderAsync(pImage, pIoCtx);

    if (RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        /* write only one block pointer. */
        VDIIMAGEBLOCKPOINTER ptrBlock = RT_H2LE_U32(pImage->paBlocks[uBlock]);
        rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pImage->pStorage,
                                    pImage->offStartBlocks + uBlock * sizeof(VDIIMAGEBLOCKPOINTER),
                                    &ptrBlock, sizeof(VDIIMAGEBLOCKPOINTER),
                                    pIoCtx, NULL, NULL);
        AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
                  ("vdiUpdateBlockInfo failed to update block=%u, filename=\"%s\", rc=%Rrc\n",
                  uBlock, pImage->pszFilename, rc));
    }
    return rc;
}

/**
 * Internal: Flush the image file to disk - async version.
 */
static int vdiFlushImageIoCtx(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        /* Save header. */
        rc = vdiUpdateHeaderAsync(pImage, pIoCtx);
        AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
                  ("vdiUpdateHeaderAsync() failed, filename=\"%s\", rc=%Rrc\n",
                  pImage->pszFilename, rc));
        rc = vdIfIoIntFileFlush(pImage->pIfIo, pImage->pStorage, pIoCtx, NULL, NULL);
        AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
                  ("Flushing data to disk failed rc=%Rrc\n", rc));
    }

    return rc;
}

/**
 * Completion callback for meta/userdata reads or writes.
 *
 * @return  VBox status code.
 *          VINF_SUCCESS if everything was successful and the transfer can continue.
 *          VERR_VD_ASYNC_IO_IN_PROGRESS if there is another data transfer pending.
 * @param   pBackendData    The opaque backend data.
 * @param   pIoCtx          I/O context associated with this request.
 * @param   pvUser          Opaque user data passed during a read/write request.
 * @param   rcReq           Status code for the completed request.
 */
static DECLCALLBACK(int) vdiDiscardBlockAsyncUpdate(void *pBackendData, PVDIOCTX pIoCtx, void *pvUser, int rcReq)
{
    RT_NOREF1(rcReq);
    int rc = VINF_SUCCESS;
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    PVDIBLOCKDISCARDASYNC pDiscardAsync = (PVDIBLOCKDISCARDASYNC)pvUser;

    switch (pDiscardAsync->enmState)
    {
        case VDIBLOCKDISCARDSTATE_READ_BLOCK:
        {
            PVDMETAXFER pMetaXfer;
            uint64_t u64Offset = (uint64_t)pDiscardAsync->idxLastBlock * pImage->cbTotalBlockData + pImage->offStartData;
            rc = vdIfIoIntFileReadMeta(pImage->pIfIo, pImage->pStorage, u64Offset,
                                       pDiscardAsync->pvBlock, pImage->cbTotalBlockData, pIoCtx,
                                       &pMetaXfer, vdiDiscardBlockAsyncUpdate, pDiscardAsync);
            if (RT_FAILURE(rc))
                break;

            /* Release immediately and go to next step. */
            vdIfIoIntMetaXferRelease(pImage->pIfIo, pMetaXfer);
            pDiscardAsync->enmState = VDIBLOCKDISCARDSTATE_WRITE_BLOCK;
        }
        RT_FALL_THRU();
        case VDIBLOCKDISCARDSTATE_WRITE_BLOCK:
        {
            /* Block read complete. Write to the new location (discarded block). */
            uint64_t u64Offset = (uint64_t)pDiscardAsync->ptrBlockDiscard * pImage->cbTotalBlockData + pImage->offStartData;
            rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pImage->pStorage, u64Offset,
                                        pDiscardAsync->pvBlock, pImage->cbTotalBlockData, pIoCtx,
                                        vdiDiscardBlockAsyncUpdate, pDiscardAsync);

            pDiscardAsync->enmState = VDIBLOCKDISCARDSTATE_UPDATE_METADATA;
            if (RT_FAILURE(rc))
                break;
        }
        RT_FALL_THRU();
        case VDIBLOCKDISCARDSTATE_UPDATE_METADATA:
        {
            int rc2;

            /* Block write complete. Update metadata. */
            pImage->paBlocksRev[pDiscardAsync->idxLastBlock] = VDI_IMAGE_BLOCK_FREE;
            pImage->paBlocks[pDiscardAsync->uBlock] = VDI_IMAGE_BLOCK_ZERO;

            if (pDiscardAsync->idxLastBlock != pDiscardAsync->ptrBlockDiscard)
            {
                pImage->paBlocks[pDiscardAsync->uBlockLast] = pDiscardAsync->ptrBlockDiscard;
                pImage->paBlocksRev[pDiscardAsync->ptrBlockDiscard] = pDiscardAsync->uBlockLast;

                rc = vdiUpdateBlockInfoAsync(pImage, pDiscardAsync->uBlockLast, pIoCtx, false /* fUpdateHdr */);
                if (   RT_FAILURE(rc)
                    && rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
                    break;
            }

            setImageBlocksAllocated(&pImage->Header, pDiscardAsync->idxLastBlock);
            rc = vdiUpdateBlockInfoAsync(pImage, pDiscardAsync->uBlock, pIoCtx, true /* fUpdateHdr */);
            if (   RT_FAILURE(rc)
                && rc != VERR_VD_ASYNC_IO_IN_PROGRESS)
                break;

            pImage->cbImage -= pImage->cbTotalBlockData;
            LogFlowFunc(("Set new size %llu\n", pImage->cbImage));
            rc2 = vdIfIoIntFileSetSize(pImage->pIfIo, pImage->pStorage, pImage->cbImage);
            if (RT_FAILURE(rc2))
                rc = rc2;

            /* Free discard state. */
            RTMemFree(pDiscardAsync->pvBlock);
            RTMemFree(pDiscardAsync);
            break;
        }
        default:
            AssertMsgFailed(("Invalid state %d\n", pDiscardAsync->enmState));
    }

    if (rc == VERR_VD_NOT_ENOUGH_METADATA)
        rc = VERR_VD_ASYNC_IO_IN_PROGRESS;

    return rc;
}

/**
 * Internal: Discard a whole block from the image filling the created hole with
 * data from another block - async I/O version.
 *
 * @returns VBox status code.
 * @param   pImage    VDI image instance data.
 * @param   pIoCtx    I/O context associated with this request.
 * @param   uBlock    The block to discard.
 * @param   pvBlock   Memory to use for the I/O.
 */
static int vdiDiscardBlockAsync(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx,
                                unsigned uBlock, void *pvBlock)
{
    int rc = VINF_SUCCESS;
    PVDIBLOCKDISCARDASYNC pDiscardAsync = NULL;

    LogFlowFunc(("pImage=%#p uBlock=%u pvBlock=%#p\n",
                 pImage, uBlock, pvBlock));

    pDiscardAsync = (PVDIBLOCKDISCARDASYNC)RTMemAllocZ(sizeof(VDIBLOCKDISCARDASYNC));
    if (RT_UNLIKELY(!pDiscardAsync))
        return VERR_NO_MEMORY;

    /* Init block discard state. */
    pDiscardAsync->uBlock  = uBlock;
    pDiscardAsync->pvBlock = pvBlock;
    pDiscardAsync->ptrBlockDiscard = pImage->paBlocks[uBlock];
    pDiscardAsync->idxLastBlock = getImageBlocksAllocated(&pImage->Header) - 1;
    pDiscardAsync->uBlockLast = pImage->paBlocksRev[pDiscardAsync->idxLastBlock];

    /*
     * The block is empty, remove it.
     * Read the last block of the image first.
     */
    if (pDiscardAsync->idxLastBlock != pDiscardAsync->ptrBlockDiscard)
    {
        LogFlowFunc(("Moving block [%u]=%u into [%u]=%u\n",
                     pDiscardAsync->uBlockLast, pDiscardAsync->idxLastBlock,
                     uBlock, pImage->paBlocks[uBlock]));
        pDiscardAsync->enmState = VDIBLOCKDISCARDSTATE_READ_BLOCK;
    }
    else
    {
        pDiscardAsync->enmState = VDIBLOCKDISCARDSTATE_UPDATE_METADATA; /* Start immediately to shrink the image. */
        LogFlowFunc(("Discard last block [%u]=%u\n", uBlock, pImage->paBlocks[uBlock]));
    }

    /* Call the update callback directly. */
    rc = vdiDiscardBlockAsyncUpdate(pImage, pIoCtx, pDiscardAsync, VINF_SUCCESS);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Internal: Creates a allocation bitmap from the given data.
 * Sectors which contain only 0 are marked as unallocated and sectors with
 * other data as allocated.
 *
 * @returns Pointer to the allocation bitmap or NULL on failure.
 * @param   pvData    The data to create the allocation bitmap for.
 * @param   cbData    Number of bytes in the buffer.
 */
static void *vdiAllocationBitmapCreate(void *pvData, size_t cbData)
{
    Assert(cbData <= UINT32_MAX / 8);
    uint32_t cSectors = (uint32_t)(cbData / 512);
    uint32_t uSectorCur = 0;
    void *pbmAllocationBitmap = NULL;

    Assert(!(cbData % 512));
    Assert(!(cSectors % 8));

    pbmAllocationBitmap = RTMemAllocZ(cSectors / 8);
    if (!pbmAllocationBitmap)
        return NULL;

    while (uSectorCur < cSectors)
    {
        int idxSet = ASMBitFirstSet((uint8_t *)pvData + uSectorCur * 512, (uint32_t)cbData * 8);

        if (idxSet != -1)
        {
            unsigned idxSectorAlloc = idxSet / 8 / 512;
            ASMBitSet(pbmAllocationBitmap, uSectorCur + idxSectorAlloc);

            uSectorCur += idxSectorAlloc + 1;
            cbData     -= (idxSectorAlloc + 1) * 512;
        }
        else
            break;
    }

    return pbmAllocationBitmap;
}


/**
 * Updates the state of the async cluster allocation.
 *
 * @returns VBox status code.
 * @param   pBackendData    The opaque backend data.
 * @param   pIoCtx          I/O context associated with this request.
 * @param   pvUser          Opaque user data passed during a read/write request.
 * @param   rcReq           Status code for the completed request.
 */
static DECLCALLBACK(int) vdiBlockAllocUpdate(void *pBackendData, PVDIOCTX pIoCtx, void *pvUser, int rcReq)
{
    int rc = VINF_SUCCESS;
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    PVDIASYNCBLOCKALLOC pBlockAlloc = (PVDIASYNCBLOCKALLOC)pvUser;

    if (RT_SUCCESS(rcReq))
    {
        pImage->cbImage += pImage->cbTotalBlockData;
        pImage->paBlocks[pBlockAlloc->uBlock] = pBlockAlloc->cBlocksAllocated;

        if (pImage->paBlocksRev)
            pImage->paBlocksRev[pBlockAlloc->cBlocksAllocated] = pBlockAlloc->uBlock;

        setImageBlocksAllocated(&pImage->Header, pBlockAlloc->cBlocksAllocated + 1);
        rc = vdiUpdateBlockInfoAsync(pImage, pBlockAlloc->uBlock, pIoCtx,
                                     true /* fUpdateHdr */);
    }
    /* else: I/O error don't update the block table. */

    RTMemFree(pBlockAlloc);
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnProbe */
static DECLCALLBACK(int) vdiProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                  PVDINTERFACE pVDIfsImage, VDTYPE enmDesiredType, VDTYPE *penmType)
{
    RT_NOREF(enmDesiredType);
    LogFlowFunc(("pszFilename=\"%s\"\n", pszFilename));
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);


    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)RTMemAllocZ(RT_UOFFSETOF(VDIIMAGEDESC, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pStorage = NULL;
        pImage->paBlocks = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        rc = vdiOpenImage(pImage, VD_OPEN_FLAGS_INFO | VD_OPEN_FLAGS_READONLY);
        vdiFreeImage(pImage, false);
        RTMemFree(pImage);

        if (RT_SUCCESS(rc))
            *penmType = VDTYPE_HDD;
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnOpen */
static DECLCALLBACK(int) vdiOpen(const char *pszFilename, unsigned uOpenFlags,
                                 PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                 VDTYPE enmType, void **ppBackendData)
{
    RT_NOREF1(enmType); /**< @todo r=klaus make use of the type info. */

    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p enmType=%u ppBackendData=%#p\n",
                 pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, enmType, ppBackendData));
    int rc;

    /* Check open flags. All valid flags are supported. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);


    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)RTMemAllocZ(RT_UOFFSETOF(VDIIMAGEDESC, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pStorage = NULL;
        pImage->paBlocks = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        rc = vdiOpenImage(pImage, uOpenFlags);
        if (RT_SUCCESS(rc))
            *ppBackendData = pImage;
        else
            RTMemFree(pImage);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnCreate */
static DECLCALLBACK(int) vdiCreate(const char *pszFilename, uint64_t cbSize,
                                   unsigned uImageFlags, const char *pszComment,
                                   PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                                   PCRTUUID pUuid, unsigned uOpenFlags,
                                   unsigned uPercentStart, unsigned uPercentSpan,
                                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                   PVDINTERFACE pVDIfsOperation, VDTYPE enmType,
                                   void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" pPCHSGeometry=%#p pLCHSGeometry=%#p Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p enmType=%u ppBackendData=%#p\n",
                 pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData));
    int rc;

    /* Check the VD container type and image flags. */
    if (   enmType != VDTYPE_HDD
        || (uImageFlags & ~VD_VDI_IMAGE_FLAGS_MASK) != 0)
        return VERR_VD_INVALID_TYPE;

    /* Check size. Maximum 4PB-3M. No tricks with adjusting the 1M block size
     * so far, which would extend the size. */
    if (   cbSize >= _1P * 4 - _1M * 3
        || cbSize < VDI_IMAGE_DEFAULT_BLOCK_SIZE
        || (cbSize % 512))
        return VERR_VD_INVALID_SIZE;

    /* Check open flags. All valid flags are supported. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pPCHSGeometry, VERR_INVALID_POINTER);
    AssertPtrReturn(pLCHSGeometry, VERR_INVALID_POINTER);

    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)RTMemAllocZ(RT_UOFFSETOF(VDIIMAGEDESC, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);
        PVDINTERFACECONFIG pIfCfg = VDIfConfigGet(pVDIfsOperation);
        pImage->pszFilename = pszFilename;
        pImage->pStorage = NULL;
        pImage->paBlocks = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        rc = vdiCreateImage(pImage, cbSize, uImageFlags, pszComment,
                            pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags,
                            pIfProgress, uPercentStart, uPercentSpan, pIfCfg);
        if (RT_SUCCESS(rc))
        {
            /* So far the image is opened in read/write mode. Make sure the
             * image is opened in read-only mode if the caller requested that. */
            if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
            {
                vdiFreeImage(pImage, false);
                rc = vdiOpenImage(pImage, uOpenFlags);
            }

            if (RT_SUCCESS(rc))
                *ppBackendData = pImage;
        }

        if (RT_FAILURE(rc))
            RTMemFree(pImage);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRename */
static DECLCALLBACK(int) vdiRename(void *pBackendData, const char *pszFilename)
{
    LogFlowFunc(("pBackendData=%#p pszFilename=%#p\n", pBackendData, pszFilename));
    int rc = VINF_SUCCESS;
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    /* Check arguments. */
    AssertReturn((pImage && pszFilename && *pszFilename), VERR_INVALID_PARAMETER);

    /* Close the image. */
    rc = vdiFreeImage(pImage, false);
    if (RT_SUCCESS(rc))
    {
        /* Rename the file. */
        rc = vdIfIoIntFileMove(pImage->pIfIo, pImage->pszFilename, pszFilename, 0);
        if (RT_SUCCESS(rc))
        {
            /* Update pImage with the new information. */
            pImage->pszFilename = pszFilename;

            /* Open the new image. */
            rc = vdiOpenImage(pImage, pImage->uOpenFlags);
        }
        else
        {
            /* The move failed, try to reopen the original image. */
            int rc2 = vdiOpenImage(pImage, pImage->uOpenFlags);
            if (RT_FAILURE(rc2))
                rc = rc2;
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnClose */
static DECLCALLBACK(int) vdiClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    int rc = vdiFreeImage(pImage, fDelete);
    RTMemFree(pImage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdiRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                 PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToRead=%zu pcbActuallyRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToRead, pcbActuallyRead));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uBlock;
    unsigned offRead;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);
    Assert(!(uOffset % 512));
    Assert(!(cbToRead % 512));
    AssertPtrReturn(pIoCtx, VERR_INVALID_POINTER);
    AssertReturn(cbToRead, VERR_INVALID_PARAMETER);
    AssertReturn(uOffset + cbToRead <= getImageDiskSize(&pImage->Header), VERR_INVALID_PARAMETER);

    /* Calculate starting block number and offset inside it. */
    uBlock = (unsigned)(uOffset >> pImage->uShiftOffset2Index);
    offRead = (unsigned)uOffset & pImage->uBlockMask;

    /* Clip read range to at most the rest of the block. */
    size_t cbBlockRem = getImageBlockSize(&pImage->Header) - offRead;
    cbToRead = RT_MIN(cbToRead, cbBlockRem);
    Assert(!(cbToRead % 512));

    if (pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_FREE)
        rc = VERR_VD_BLOCK_FREE;
    else if (pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_ZERO)
    {
        size_t cbSet = vdIfIoIntIoCtxSet(pImage->pIfIo, pIoCtx, 0, cbToRead);
        Assert(cbSet == cbToRead); RT_NOREF(cbSet);
    }
    else
    {
        /* Block present in image file, read relevant data. */
        uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData
                           + (pImage->offStartData + pImage->offStartBlockData + offRead);

        if (u64Offset + cbToRead <= pImage->cbImage)
            rc = vdIfIoIntFileReadUser(pImage->pIfIo, pImage->pStorage, u64Offset,
                                       pIoCtx, cbToRead);
        else
        {
            LogRel(("VDI: Out of range access (%llu) in image %s, image size %llu\n",
                    u64Offset, pImage->pszFilename, pImage->cbImage));
            vdIfIoIntIoCtxSet(pImage->pIfIo, pIoCtx, 0, cbToRead);
            rc = VERR_VD_READ_OUT_OF_RANGE;
        }
    }

    if (pcbActuallyRead)
        *pcbActuallyRead = cbToRead;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdiWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                                  PVDIOCTX pIoCtx, size_t *pcbWriteProcess, size_t *pcbPreRead,
                                  size_t *pcbPostRead, unsigned fWrite)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToWrite=%zu pcbWriteProcess=%#p pcbPreRead=%#p pcbPostRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uBlock;
    unsigned offWrite;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);
    Assert(!(uOffset % 512));
    Assert(!(cbToWrite % 512));
    AssertPtrReturn(pIoCtx, VERR_INVALID_POINTER);
    AssertReturn(cbToWrite, VERR_INVALID_PARAMETER);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        /* No size check here, will do that later.  For dynamic images which are
         * not multiples of the block size in length, this would prevent writing to
         * the last block. */

        /* Calculate starting block number and offset inside it. */
        uBlock = (unsigned)(uOffset >> pImage->uShiftOffset2Index);
        offWrite = (unsigned)uOffset & pImage->uBlockMask;

        /* Clip write range to at most the rest of the block. */
        size_t cbBlockRem = getImageBlockSize(&pImage->Header) - offWrite;
        cbToWrite = RT_MIN(cbToWrite, cbBlockRem);
        Assert(!(cbToWrite % 512));

        do
        {
            if (!IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[uBlock]))
            {
                /* Block is either free or zero. */
                if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_ZEROES)
                    && (   pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_ZERO
                        || cbToWrite == getImageBlockSize(&pImage->Header)))
                {
                    /* If the destination block is unallocated at this point, it's
                     * either a zero block or a block which hasn't been used so far
                     * (which also means that it's a zero block. Don't need to write
                     * anything to this block  if the data consists of just zeroes. */
                    if (vdIfIoIntIoCtxIsZero(pImage->pIfIo, pIoCtx, cbToWrite, true))
                    {
                        pImage->paBlocks[uBlock] = VDI_IMAGE_BLOCK_ZERO;
                        *pcbPreRead = 0;
                        *pcbPostRead = 0;
                        break;
                    }
                }

                if (   cbToWrite == getImageBlockSize(&pImage->Header)
                    && !(fWrite & VD_WRITE_NO_ALLOC))
                {
                    /* Full block write to previously unallocated block.
                     * Allocate block and write data. */
                    Assert(!offWrite);
                    PVDIASYNCBLOCKALLOC pBlockAlloc = (PVDIASYNCBLOCKALLOC)RTMemAllocZ(sizeof(VDIASYNCBLOCKALLOC));
                    if (!pBlockAlloc)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }

                    unsigned cBlocksAllocated = getImageBlocksAllocated(&pImage->Header);
                    uint64_t u64Offset = (uint64_t)cBlocksAllocated * pImage->cbTotalBlockData
                                       + (pImage->offStartData + pImage->offStartBlockData);

                    pBlockAlloc->cBlocksAllocated = cBlocksAllocated;
                    pBlockAlloc->uBlock           = uBlock;

                    *pcbPreRead = 0;
                    *pcbPostRead = 0;

                    rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pImage->pStorage,
                                                u64Offset, pIoCtx, cbToWrite,
                                                vdiBlockAllocUpdate, pBlockAlloc);
                    if (rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
                        break;
                    else if (RT_FAILURE(rc))
                    {
                        RTMemFree(pBlockAlloc);
                        break;
                    }

                    rc = vdiBlockAllocUpdate(pImage, pIoCtx, pBlockAlloc, rc);
                }
                else
                {
                    /* Trying to do a partial write to an unallocated block. Don't do
                     * anything except letting the upper layer know what to do. */
                    *pcbPreRead = offWrite % getImageBlockSize(&pImage->Header);
                    *pcbPostRead = getImageBlockSize(&pImage->Header) - cbToWrite - *pcbPreRead;
                    rc = VERR_VD_BLOCK_FREE;
                }
            }
            else
            {
                /* Block present in image file, write relevant data. */
                uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData
                                   + (pImage->offStartData + pImage->offStartBlockData + offWrite);
                rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pImage->pStorage,
                                            u64Offset, pIoCtx, cbToWrite, NULL, NULL);
            }
        } while (0);

        if (pcbWriteProcess)
            *pcbWriteProcess = cbToWrite;
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdiFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    Assert(pImage);

    rc = vdiFlushImageIoCtx(pImage, pIoCtx);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) vdiGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->PreHeader.u32Version));
    return pImage->PreHeader.u32Version;
}

/** @copydoc VDIMAGEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) vdiGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    uint64_t cb = 0;

    AssertPtrReturn(pImage, 0);

    if (pImage->pStorage)
    {
        uint64_t cbFile;
        int rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &cbFile);
        if (RT_SUCCESS(rc))
            cb += cbFile;
    }

    LogFlowFunc(("returns %lld\n", cb));
    return cb;
}

/** @copydoc VDIMAGEBACKEND::pfnGetPCHSGeometry */
static DECLCALLBACK(int) vdiGetPCHSGeometry(void *pBackendData, PVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (pImage->PCHSGeometry.cCylinders)
        *pPCHSGeometry = pImage->PCHSGeometry;
    else
        rc = VERR_VD_GEOMETRY_NOT_SET;

    LogFlowFunc(("returns %Rrc (PCHS=%u/%u/%u)\n", rc, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetPCHSGeometry */
static DECLCALLBACK(int) vdiSetPCHSGeometry(void *pBackendData, PCVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n",
                 pBackendData, pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        pImage->PCHSGeometry = *pPCHSGeometry;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetLCHSGeometry */
static DECLCALLBACK(int) vdiGetLCHSGeometry(void *pBackendData, PVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc = VINF_SUCCESS;
    VDIDISKGEOMETRY DummyGeo = { 0, 0, 0, VDI_GEOMETRY_SECTOR_SIZE };
    PVDIDISKGEOMETRY pGeometry = getImageLCHSGeometry(&pImage->Header);
    if (!pGeometry)
        pGeometry = &DummyGeo;

    if (    pGeometry->cCylinders > 0
        &&  pGeometry->cHeads > 0
        &&  pGeometry->cSectors > 0)
    {
        pLCHSGeometry->cCylinders = pGeometry->cCylinders;
        pLCHSGeometry->cHeads = pGeometry->cHeads;
        pLCHSGeometry->cSectors = pGeometry->cSectors;
    }
    else
        rc = VERR_VD_GEOMETRY_NOT_SET;

    LogFlowFunc(("returns %Rrc (LCHS=%u/%u/%u)\n", rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetLCHSGeometry */
static DECLCALLBACK(int) vdiSetLCHSGeometry(void *pBackendData, PCVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n",
                 pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    PVDIDISKGEOMETRY pGeometry;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        pGeometry = getImageLCHSGeometry(&pImage->Header);
        if (pGeometry)
        {
            pGeometry->cCylinders = pLCHSGeometry->cCylinders;
            pGeometry->cHeads = pLCHSGeometry->cHeads;
            pGeometry->cSectors = pLCHSGeometry->cSectors;
            pGeometry->cbSector = VDI_GEOMETRY_SECTOR_SIZE;

            /* Update header information in base image file. */
            vdiFlushImage(pImage);
        }
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnQueryRegions */
static DECLCALLBACK(int) vdiQueryRegions(void *pBackendData, PCVDREGIONLIST *ppRegionList)
{
    LogFlowFunc(("pBackendData=%#p ppRegionList=%#p\n", pBackendData, ppRegionList));
    PVDIIMAGEDESC pThis = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    *ppRegionList = &pThis->RegionList;
    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnRegionListRelease */
static DECLCALLBACK(void) vdiRegionListRelease(void *pBackendData, PCVDREGIONLIST pRegionList)
{
    RT_NOREF1(pRegionList);
    LogFlowFunc(("pBackendData=%#p pRegionList=%#p\n", pBackendData, pRegionList));
    PVDIIMAGEDESC pThis = (PVDIIMAGEDESC)pBackendData;
    AssertPtr(pThis); RT_NOREF(pThis);

    /* Nothing to do here. */
}

/** @copydoc VDIMAGEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) vdiGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uImageFlags));
    return pImage->uImageFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) vdiGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uOpenFlags));
    return pImage->uOpenFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) vdiSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p uOpenFlags=%#x\n", pBackendData, uOpenFlags));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    /* Image must be opened and the new flags must be valid. */
    if (!pImage || (uOpenFlags & ~(  VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO
                                   | VD_OPEN_FLAGS_ASYNC_IO | VD_OPEN_FLAGS_SHAREABLE
                                   | VD_OPEN_FLAGS_SEQUENTIAL | VD_OPEN_FLAGS_DISCARD
                                   | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)))
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /* Implement this operation via reopening the image. */
        rc = vdiFreeImage(pImage, false);
        if (RT_SUCCESS(rc))
            rc = vdiOpenImage(pImage, uOpenFlags);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetComment */
static DECLCALLBACK(int) vdiGetComment(void *pBackendData, char *pszComment,
                                       size_t cbComment)
{
    LogFlowFunc(("pBackendData=%#p pszComment=%#p cbComment=%zu\n", pBackendData, pszComment, cbComment));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc = VINF_SUCCESS;
    char *pszTmp = getImageComment(&pImage->Header);
    /* Make this foolproof even if the image doesn't have the zero
     * termination. With some luck the repaired header will be saved. */
    size_t cb = RTStrNLen(pszTmp, VDI_IMAGE_COMMENT_SIZE);
    if (cb == VDI_IMAGE_COMMENT_SIZE)
    {
        pszTmp[VDI_IMAGE_COMMENT_SIZE-1] = '\0';
        cb--;
    }
    if (cb < cbComment)
    {
        /* memcpy is much better than strncpy. */
        memcpy(pszComment, pszTmp, cb + 1);
    }
    else
        rc = VERR_BUFFER_OVERFLOW;

    LogFlowFunc(("returns %Rrc comment=\"%s\"\n", rc, pszComment));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetComment */
static DECLCALLBACK(int) vdiSetComment(void *pBackendData, const char *pszComment)
{
    LogFlowFunc(("pBackendData=%#p pszComment=\"%s\"\n", pBackendData, pszComment));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        size_t cchComment = pszComment ? strlen(pszComment) : 0;
        if (cchComment < VDI_IMAGE_COMMENT_SIZE)
        {
            /* we don't support old style images */
            if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
            {
                /*
                 * Update the comment field, making sure to zero out all of the previous comment.
                 */
                memset(pImage->Header.u.v1.szComment, '\0', VDI_IMAGE_COMMENT_SIZE);
                memcpy(pImage->Header.u.v1.szComment, pszComment, cchComment);

                /* write out new the header */
                rc = vdiUpdateHeader(pImage);
            }
            else
                rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
        }
        else
        {
            LogFunc(("pszComment is too long, %d bytes!\n", cchComment));
            rc = VERR_VD_VDI_COMMENT_TOO_LONG;
        }
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetUuid */
static DECLCALLBACK(int) vdiGetUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = *getImageCreationUUID(&pImage->Header);

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetUuid */
static DECLCALLBACK(int) vdiSetUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc = VINF_SUCCESS;
    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
            pImage->Header.u.v1.uuidCreate = *pUuid;
        /* Make it possible to clone old VDIs. */
        else if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0)
            pImage->Header.u.v0.uuidCreate = *pUuid;
        else
        {
            LogFunc(("Version is not supported!\n"));
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
        }
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetModificationUuid */
static DECLCALLBACK(int) vdiGetModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = *getImageModificationUUID(&pImage->Header);

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetModificationUuid */
static DECLCALLBACK(int) vdiSetModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc = VINF_SUCCESS;
    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
            pImage->Header.u.v1.uuidModify = *pUuid;
        /* Make it possible to clone old VDIs. */
        else if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0)
            pImage->Header.u.v0.uuidModify = *pUuid;
        else
        {
            LogFunc(("Version is not supported!\n"));
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
        }
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetParentUuid */
static DECLCALLBACK(int) vdiGetParentUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = *getImageParentUUID(&pImage->Header);

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetParentUuid */
static DECLCALLBACK(int) vdiSetParentUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc = VINF_SUCCESS;
    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
            pImage->Header.u.v1.uuidLinkage = *pUuid;
        /* Make it possible to clone old VDIs. */
        else if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0)
            pImage->Header.u.v0.uuidLinkage = *pUuid;
        else
        {
            LogFunc(("Version is not supported!\n"));
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
        }
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetParentModificationUuid */
static DECLCALLBACK(int) vdiGetParentModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    *pUuid = *getImageParentModificationUUID(&pImage->Header);

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VINF_SUCCESS, pUuid));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnSetParentModificationUuid */
static DECLCALLBACK(int) vdiSetParentModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc = VINF_SUCCESS;
    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
            pImage->Header.u.v1.uuidParentModify = *pUuid;
        else
        {
            LogFunc(("Version is not supported!\n"));
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
        }
    }
    else
        rc = VERR_VD_IMAGE_READ_ONLY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnDump */
static DECLCALLBACK(void) vdiDump(void *pBackendData)
{
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    AssertPtrReturnVoid(pImage);
    vdIfErrorMessage(pImage->pIfError, "Dumping VDI image \"%s\" mode=%s uOpenFlags=%X File=%#p\n",
                     pImage->pszFilename,
                     (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY) ? "r/o" : "r/w",
                     pImage->uOpenFlags,
                     pImage->pStorage);
    vdIfErrorMessage(pImage->pIfError, "Header: Version=%08X Type=%X Flags=%X Size=%llu\n",
                     pImage->PreHeader.u32Version,
                     getImageType(&pImage->Header),
                     getImageFlags(&pImage->Header),
                     getImageDiskSize(&pImage->Header));
    vdIfErrorMessage(pImage->pIfError, "Header: cbBlock=%u cbBlockExtra=%u cBlocks=%u cBlocksAllocated=%u\n",
                     getImageBlockSize(&pImage->Header),
                     getImageExtraBlockSize(&pImage->Header),
                     getImageBlocks(&pImage->Header),
                     getImageBlocksAllocated(&pImage->Header));
    vdIfErrorMessage(pImage->pIfError, "Header: offBlocks=%u offData=%u\n",
                     getImageBlocksOffset(&pImage->Header),
                     getImageDataOffset(&pImage->Header));
    PVDIDISKGEOMETRY pg = getImageLCHSGeometry(&pImage->Header);
    if (pg)
        vdIfErrorMessage(pImage->pIfError, "Header: Geometry: C/H/S=%u/%u/%u cbSector=%u\n",
                         pg->cCylinders, pg->cHeads, pg->cSectors, pg->cbSector);
    vdIfErrorMessage(pImage->pIfError, "Header: uuidCreation={%RTuuid}\n", getImageCreationUUID(&pImage->Header));
    vdIfErrorMessage(pImage->pIfError, "Header: uuidModification={%RTuuid}\n", getImageModificationUUID(&pImage->Header));
    vdIfErrorMessage(pImage->pIfError, "Header: uuidParent={%RTuuid}\n", getImageParentUUID(&pImage->Header));
    if (GET_MAJOR_HEADER_VERSION(&pImage->Header) >= 1)
        vdIfErrorMessage(pImage->pIfError, "Header: uuidParentModification={%RTuuid}\n", getImageParentModificationUUID(&pImage->Header));
    vdIfErrorMessage(pImage->pIfError, "Image:  fFlags=%08X offStartBlocks=%u offStartData=%u\n",
                     pImage->uImageFlags, pImage->offStartBlocks, pImage->offStartData);
    vdIfErrorMessage(pImage->pIfError, "Image:  uBlockMask=%08X cbTotalBlockData=%u uShiftOffset2Index=%u offStartBlockData=%u\n",
                     pImage->uBlockMask,
                     pImage->cbTotalBlockData,
                     pImage->uShiftOffset2Index,
                     pImage->offStartBlockData);

    unsigned uBlock, cBlocksNotFree, cBadBlocks, cBlocks = getImageBlocks(&pImage->Header);
    for (uBlock=0, cBlocksNotFree=0, cBadBlocks=0; uBlock<cBlocks; uBlock++)
    {
        if (IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[uBlock]))
        {
            cBlocksNotFree++;
            if (pImage->paBlocks[uBlock] >= cBlocks)
                cBadBlocks++;
        }
    }
    if (cBlocksNotFree != getImageBlocksAllocated(&pImage->Header))
    {
        vdIfErrorMessage(pImage->pIfError, "!! WARNING: %u blocks actually allocated (cBlocksAllocated=%u) !!\n",
                         cBlocksNotFree, getImageBlocksAllocated(&pImage->Header));
    }
    if (cBadBlocks)
    {
        vdIfErrorMessage(pImage->pIfError, "!! WARNING: %u bad blocks found !!\n",
                         cBadBlocks);
    }
}

/** @copydoc VDIMAGEBACKEND::pfnCompact */
static DECLCALLBACK(int) vdiCompact(void *pBackendData, unsigned uPercentStart,
                                    unsigned uPercentSpan, PVDINTERFACE pVDIfsDisk,
                                    PVDINTERFACE pVDIfsImage, PVDINTERFACE pVDIfsOperation)
{
    RT_NOREF2(pVDIfsDisk, pVDIfsImage);
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;
    void *pvBuf = NULL, *pvTmp = NULL;
    unsigned *paBlocks2 = NULL;

    PFNVDPARENTREAD pfnParentRead = NULL;
    void *pvParent = NULL;
    PVDINTERFACEPARENTSTATE pIfParentState = VDIfParentStateGet(pVDIfsOperation);
    if (pIfParentState)
    {
        pfnParentRead = pIfParentState->pfnParentRead;
        pvParent = pIfParentState->Core.pvUser;
    }

    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);
    PVDINTERFACEQUERYRANGEUSE pIfQueryRangeUse = VDIfQueryRangeUseGet(pVDIfsOperation);

    do
    {
        AssertBreakStmt(pImage, rc = VERR_INVALID_PARAMETER);

        AssertBreakStmt(!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY),
                        rc = VERR_VD_IMAGE_READ_ONLY);

        unsigned cBlocks;
        unsigned cBlocksToMove = 0;
        size_t cbBlock;
        cBlocks = getImageBlocks(&pImage->Header);
        cbBlock = getImageBlockSize(&pImage->Header);
        if (pfnParentRead)
        {
            pvBuf = RTMemTmpAlloc(cbBlock);
            AssertBreakStmt(pvBuf, rc = VERR_NO_MEMORY);
        }
        pvTmp = RTMemTmpAlloc(cbBlock);
        AssertBreakStmt(pvTmp, rc = VERR_NO_MEMORY);

        uint64_t cbFile;
        rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &cbFile);
        AssertRCBreak(rc);
        unsigned cBlocksAllocated = (unsigned)((cbFile - pImage->offStartData - pImage->offStartBlockData) >> pImage->uShiftOffset2Index);
        if (cBlocksAllocated == 0)
        {
            /* No data blocks in this image, no need to compact. */
            rc = VINF_SUCCESS;
            break;
        }

        /* Allocate block array for back resolving. */
        paBlocks2 = (unsigned *)RTMemAlloc(sizeof(unsigned *) * cBlocksAllocated);
        AssertBreakStmt(paBlocks2, rc = VERR_NO_MEMORY);
        /* Fill out back resolving, check/fix allocation errors before
         * compacting the image, just to be on the safe side. Update the
         * image contents straight away, as this enables cancelling. */
        for (unsigned i = 0; i < cBlocksAllocated; i++)
            paBlocks2[i] = VDI_IMAGE_BLOCK_FREE;
        rc = VINF_SUCCESS;
        for (unsigned i = 0; i < cBlocks; i++)
        {
            VDIIMAGEBLOCKPOINTER ptrBlock = pImage->paBlocks[i];
            if (IS_VDI_IMAGE_BLOCK_ALLOCATED(ptrBlock))
            {
                if (ptrBlock < cBlocksAllocated)
                {
                    if (paBlocks2[ptrBlock] == VDI_IMAGE_BLOCK_FREE)
                        paBlocks2[ptrBlock] = i;
                    else
                    {
                        LogFunc(("Freed cross-linked block %u in file \"%s\"\n",
                                 i, pImage->pszFilename));
                        pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                        rc = vdiUpdateBlockInfo(pImage, i);
                        if (RT_FAILURE(rc))
                            break;
                    }
                }
                else
                {
                    LogFunc(("Freed out of bounds reference for block %u in file \"%s\"\n",
                             i, pImage->pszFilename));
                    pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                    rc = vdiUpdateBlockInfo(pImage, i);
                    if (RT_FAILURE(rc))
                        break;
                }
            }
        }
        if (RT_FAILURE(rc))
            break;

        /* Find redundant information and update the block pointers
         * accordingly, creating bubbles. Keep disk up to date, as this
         * enables cancelling. */
        for (unsigned i = 0; i < cBlocks; i++)
        {
            VDIIMAGEBLOCKPOINTER ptrBlock = pImage->paBlocks[i];
            if (IS_VDI_IMAGE_BLOCK_ALLOCATED(ptrBlock))
            {
                /* Block present in image file, read relevant data. */
                uint64_t u64Offset = (uint64_t)ptrBlock * pImage->cbTotalBlockData
                                   + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, u64Offset, pvTmp, cbBlock);
                if (RT_FAILURE(rc))
                    break;

                if (ASMBitFirstSet((volatile void *)pvTmp, (uint32_t)cbBlock * 8) == -1)
                {
                    pImage->paBlocks[i] = VDI_IMAGE_BLOCK_ZERO;
                    rc = vdiUpdateBlockInfo(pImage, i);
                    if (RT_FAILURE(rc))
                        break;
                    paBlocks2[ptrBlock] = VDI_IMAGE_BLOCK_FREE;
                    /* Adjust progress info, one block to be relocated. */
                    cBlocksToMove++;
                }
                else if (pfnParentRead)
                {
                    rc = pfnParentRead(pvParent, (uint64_t)i * cbBlock, pvBuf, cbBlock);
                    if (RT_FAILURE(rc))
                        break;
                    if (!memcmp(pvTmp, pvBuf, cbBlock))
                    {
                        pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                        rc = vdiUpdateBlockInfo(pImage, i);
                        if (RT_FAILURE(rc))
                            break;
                        paBlocks2[ptrBlock] = VDI_IMAGE_BLOCK_FREE;
                        /* Adjust progress info, one block to be relocated. */
                        cBlocksToMove++;
                    }
                }
            }

            /* Check if the range is in use if the block is still allocated. */
            ptrBlock = pImage->paBlocks[i];
            if (   IS_VDI_IMAGE_BLOCK_ALLOCATED(ptrBlock)
                && pIfQueryRangeUse)
            {
                bool fUsed = true;

                rc = vdIfQueryRangeUse(pIfQueryRangeUse, (uint64_t)i * cbBlock, cbBlock, &fUsed);
                if (RT_FAILURE(rc))
                    break;
                if (!fUsed)
                {
                    pImage->paBlocks[i] = VDI_IMAGE_BLOCK_ZERO;
                    rc = vdiUpdateBlockInfo(pImage, i);
                    if (RT_FAILURE(rc))
                        break;
                    paBlocks2[ptrBlock] = VDI_IMAGE_BLOCK_FREE;
                    /* Adjust progress info, one block to be relocated. */
                    cBlocksToMove++;
                }
            }

            vdIfProgress(pIfProgress, (uint64_t)i * uPercentSpan / (cBlocks + cBlocksToMove) + uPercentStart);
            if (RT_FAILURE(rc))
                break;
        }
        if (RT_FAILURE(rc))
            break;

        /* Fill bubbles with other data (if available). */
        unsigned cBlocksMoved = 0;
        unsigned uBlockUsedPos = cBlocksAllocated;
        for (unsigned i = 0; i < cBlocksAllocated; i++)
        {
            unsigned uBlock = paBlocks2[i];
            if (uBlock == VDI_IMAGE_BLOCK_FREE)
            {
                unsigned uBlockData = VDI_IMAGE_BLOCK_FREE;
                while (uBlockUsedPos > i && uBlockData == VDI_IMAGE_BLOCK_FREE)
                {
                    uBlockUsedPos--;
                    uBlockData = paBlocks2[uBlockUsedPos];
                }
                /* Terminate early if there is no block which needs copying. */
                if (uBlockUsedPos == i)
                    break;
                uint64_t u64Offset = (uint64_t)uBlockUsedPos * pImage->cbTotalBlockData
                                   + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, u64Offset,
                                           pvTmp, cbBlock);
                if (RT_FAILURE(rc))
                    break;

                u64Offset = (uint64_t)i * pImage->cbTotalBlockData
                          + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, u64Offset,
                                            pvTmp, cbBlock);
                if (RT_FAILURE(rc))
                    break;

                pImage->paBlocks[uBlockData] = i;
                setImageBlocksAllocated(&pImage->Header, cBlocksAllocated - cBlocksMoved);
                rc = vdiUpdateBlockInfo(pImage, uBlockData);
                if (RT_FAILURE(rc))
                    break;
                paBlocks2[i] = uBlockData;
                paBlocks2[uBlockUsedPos] = VDI_IMAGE_BLOCK_FREE;
                cBlocksMoved++;
            }

            rc = vdIfProgress(pIfProgress, (uint64_t)(cBlocks + cBlocksMoved) * uPercentSpan / (cBlocks + cBlocksToMove) + uPercentStart);
            if (RT_FAILURE(rc))
                break;
        }
        if (RT_FAILURE(rc))
            break;

        /* Update image header. */
        setImageBlocksAllocated(&pImage->Header, uBlockUsedPos);
        vdiUpdateHeader(pImage);

        /* Truncate the image to the proper size to finish compacting. */
        rc = vdIfIoIntFileSetSize(pImage->pIfIo, pImage->pStorage,
                                  (uint64_t)uBlockUsedPos * pImage->cbTotalBlockData
                                  + pImage->offStartData + pImage->offStartBlockData);
    } while (0);

    if (paBlocks2)
        RTMemTmpFree(paBlocks2);
    if (pvTmp)
        RTMemTmpFree(pvTmp);
    if (pvBuf)
        RTMemTmpFree(pvBuf);

    if (RT_SUCCESS(rc))
        vdIfProgress(pIfProgress, uPercentStart + uPercentSpan);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/** @copydoc VDIMAGEBACKEND::pfnResize */
static DECLCALLBACK(int) vdiResize(void *pBackendData, uint64_t cbSize,
                                   PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                                   unsigned uPercentStart, unsigned uPercentSpan,
                                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                   PVDINTERFACE pVDIfsOperation)
{
    RT_NOREF5(uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation);
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    /* Check size. Maximum 4PB-3M. No tricks with adjusting the 1M block size
     * so far, which would extend the size. */
    if (   cbSize >= _1P * 4 - _1M * 3
        || cbSize < VDI_IMAGE_DEFAULT_BLOCK_SIZE)
        return VERR_VD_INVALID_SIZE;

    /*
     * Making the image smaller is not supported at the moment.
     * Resizing is also not supported for fixed size images and
     * very old images.
     */
    /** @todo implement making the image smaller, it is the responsibility of
     * the user to know what he's doing. */
    if (cbSize < getImageDiskSize(&pImage->Header))
        rc = VERR_VD_SHRINK_NOT_SUPPORTED;
    else if (   GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0
             || pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED)
        rc = VERR_NOT_SUPPORTED;
    else if (cbSize > getImageDiskSize(&pImage->Header))
    {
        unsigned cBlocksAllocated = getImageBlocksAllocated(&pImage->Header); /** < Blocks currently allocated, doesn't change during resize */
        unsigned const cbBlock = getImageBlockSize(&pImage->Header);
        uint32_t cBlocksNew = cbSize / cbBlock;                               /** < New number of blocks in the image after the resize */
        if (cbSize % cbBlock)
            cBlocksNew++;

        uint32_t cBlocksOld      = getImageBlocks(&pImage->Header);           /** < Number of blocks before the resize. */
        uint64_t cbBlockspaceNew = cBlocksNew * sizeof(VDIIMAGEBLOCKPOINTER); /** < Required space for the block array after the resize. */
        uint64_t offStartDataNew = RT_ALIGN_32(pImage->offStartBlocks + cbBlockspaceNew, VDI_DATA_ALIGN); /** < New start offset for block data after the resize */

        if (pImage->offStartData < offStartDataNew)
        {
            if (cBlocksAllocated > 0)
            {
                /* Calculate how many sectors need to be relocated. */
                uint64_t cbOverlapping = offStartDataNew - pImage->offStartData;
                unsigned cBlocksReloc = cbOverlapping / cbBlock;
                if (cbOverlapping % cbBlock)
                    cBlocksReloc++;

                /* Since only full blocks can be relocated the new data start is
                 * determined by moving it block by block. */
                cBlocksReloc = RT_MIN(cBlocksReloc, cBlocksAllocated);
                offStartDataNew = pImage->offStartData;

                /* Do the relocation. */
                LogFlow(("Relocating %u blocks\n", cBlocksReloc));

                /*
                 * Get the blocks we need to relocate first, they are appended to the end
                 * of the image.
                 */
                void *pvBuf = NULL, *pvZero = NULL;
                do
                {
                    /* Allocate data buffer. */
                    pvBuf = RTMemAllocZ(pImage->cbTotalBlockData);
                    if (!pvBuf)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }

                    /* Allocate buffer for overwriting with zeroes. */
                    pvZero = RTMemAllocZ(pImage->cbTotalBlockData);
                    if (!pvZero)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }

                    for (unsigned i = 0; i < cBlocksReloc; i++)
                    {
                        /* Search the index in the block table. */
                        for (unsigned idxBlock = 0; idxBlock < cBlocksOld; idxBlock++)
                        {
                            if (!pImage->paBlocks[idxBlock])
                            {
                                /* Read data and append to the end of the image. */
                                rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage,
                                                           offStartDataNew, pvBuf,
                                                           pImage->cbTotalBlockData);
                                if (RT_FAILURE(rc))
                                    break;

                                uint64_t offBlockAppend;
                                rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &offBlockAppend);
                                if (RT_FAILURE(rc))
                                    break;

                                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage,
                                                            offBlockAppend, pvBuf,
                                                            pImage->cbTotalBlockData);
                                if (RT_FAILURE(rc))
                                    break;

                                /* Zero out the old block area. */
                                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage,
                                                            offStartDataNew, pvZero,
                                                            pImage->cbTotalBlockData);
                                if (RT_FAILURE(rc))
                                    break;

                                /* Update block counter. */
                                pImage->paBlocks[idxBlock] = cBlocksAllocated - 1;

                                /*
                                 * Decrease the block number of all other entries in the array.
                                 * They were moved one block to the front.
                                 * Doing it as a separate step iterating over the array again
                                 * because an error while relocating the block might end up
                                 * in a corrupted image otherwise.
                                 */
                                for (unsigned idxBlock2 = 0; idxBlock2 < cBlocksOld; idxBlock2++)
                                {
                                    if (   idxBlock2 != idxBlock
                                        && IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[idxBlock2]))
                                        pImage->paBlocks[idxBlock2]--;
                                }

                                /* Continue with the next block. */
                                break;
                            }
                        }

                        if (RT_FAILURE(rc))
                            break;

                        offStartDataNew += pImage->cbTotalBlockData;
                    }
                } while (0);

                if (pvBuf)
                    RTMemFree(pvBuf);
                if (pvZero)
                    RTMemFree(pvZero);
            }

            /*
             * We need to update the new offsets for the image data in the out of memory
             * case too because we relocated the blocks already.
             */
            pImage->offStartData = offStartDataNew;
            setImageDataOffset(&pImage->Header, offStartDataNew);
        }

        /*
         * Relocation done, expand the block array and update the header with
         * the new data.
         */
        if (RT_SUCCESS(rc))
        {
            PVDIIMAGEBLOCKPOINTER paBlocksNew = (PVDIIMAGEBLOCKPOINTER)RTMemRealloc(pImage->paBlocks, cbBlockspaceNew);
            if (paBlocksNew)
            {
                pImage->paBlocks = paBlocksNew;

                /* Mark the new blocks as unallocated. */
                for (unsigned idxBlock = cBlocksOld; idxBlock < cBlocksNew; idxBlock++)
                    pImage->paBlocks[idxBlock] = VDI_IMAGE_BLOCK_FREE;

                /* Write the block array before updating the rest. */
                vdiConvBlocksEndianess(VDIECONV_H2F, pImage->paBlocks, cBlocksNew);
                rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, pImage->offStartBlocks,
                                            pImage->paBlocks, cbBlockspaceNew);
                vdiConvBlocksEndianess(VDIECONV_F2H, pImage->paBlocks, cBlocksNew);

                if (RT_SUCCESS(rc))
                {
                    /* Update size and new block count. */
                    setImageDiskSize(&pImage->Header, cbSize);
                    setImageBlocks(&pImage->Header, cBlocksNew);
                    /* Update geometry. */
                    pImage->PCHSGeometry = *pPCHSGeometry;
                    pImage->cbImage = cbSize;

                    PVDIDISKGEOMETRY pGeometry = getImageLCHSGeometry(&pImage->Header);
                    if (pGeometry)
                    {
                        pGeometry->cCylinders = pLCHSGeometry->cCylinders;
                        pGeometry->cHeads = pLCHSGeometry->cHeads;
                        pGeometry->cSectors = pLCHSGeometry->cSectors;
                        pGeometry->cbSector = VDI_GEOMETRY_SECTOR_SIZE;
                    }
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }

        /* Update header information in base image file. */
        vdiFlushImage(pImage);
    }
    /* Same size doesn't change the image at all. */

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnDiscard */
static DECLCALLBACK(int) vdiDiscard(void *pBackendData, PVDIOCTX pIoCtx,
                                    uint64_t uOffset, size_t cbDiscard,
                                    size_t *pcbPreAllocated, size_t *pcbPostAllocated,
                                    size_t *pcbActuallyDiscarded, void **ppbmAllocationBitmap,
                                    unsigned fDiscard)
{
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uBlock;
    unsigned offDiscard;
    int rc = VINF_SUCCESS;
    void *pvBlock = NULL;

    LogFlowFunc(("pBackendData=%#p pIoCtx=%#p uOffset=%llu cbDiscard=%zu pcbPreAllocated=%#p pcbPostAllocated=%#p pcbActuallyDiscarded=%#p ppbmAllocationBitmap=%#p fDiscard=%#x\n",
                 pBackendData, pIoCtx, uOffset, cbDiscard, pcbPreAllocated, pcbPostAllocated, pcbActuallyDiscarded, ppbmAllocationBitmap, fDiscard));

    AssertPtr(pImage);
    Assert(!(uOffset % 512));
    Assert(!(cbDiscard % 512));

    AssertMsgReturn(!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY),
                    ("Image is readonly\n"), VERR_VD_IMAGE_READ_ONLY);
    AssertMsgReturn(   uOffset + cbDiscard <= getImageDiskSize(&pImage->Header)
                    && cbDiscard,
                    ("Invalid parameters uOffset=%llu cbDiscard=%zu\n",
                     uOffset, cbDiscard),
                     VERR_INVALID_PARAMETER);

    do
    {
        AssertMsgBreakStmt(!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY),
                           ("Image is opened readonly\n"),
                           rc = VERR_VD_IMAGE_READ_ONLY);

        AssertMsgBreakStmt(cbDiscard,
                           ("cbDiscard=%u\n", cbDiscard),
                           rc = VERR_INVALID_PARAMETER);

        /* Calculate starting block number and offset inside it. */
        uBlock = (unsigned)(uOffset >> pImage->uShiftOffset2Index);
        offDiscard = (unsigned)uOffset & pImage->uBlockMask;

        /* Clip range to at most the rest of the block. */
        size_t cbBlockRem = getImageBlockSize(&pImage->Header) - offDiscard;
        cbDiscard = RT_MIN(cbDiscard, cbBlockRem);
        Assert(!(cbDiscard % 512));

        if (pcbPreAllocated)
            *pcbPreAllocated = 0;

        if (pcbPostAllocated)
            *pcbPostAllocated = 0;

        if (IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[uBlock]))
        {
            unsigned const cbBlock = getImageBlockSize(&pImage->Header);
            size_t const cbPreAllocated = offDiscard % RT_MAX(cbBlock, 1); /* MSVC thinks it is clever and says there is a potential mod by 0 here, hence the RT_MAX... */
            size_t const cbPostAllocated = getImageBlockSize(&pImage->Header) - cbDiscard - cbPreAllocated;
            uint8_t *pbBlockData;

            /* Read the block data. */
            pvBlock = RTMemAlloc(pImage->cbTotalBlockData);
            if (!pvBlock)
            {
                rc = VERR_NO_MEMORY;
                break;
            }

            if (!cbPreAllocated && !cbPostAllocated)
            {
                /*
                 * Discarding a whole block, don't check for allocated sectors.
                 * It is possible to just remove the whole block which avoids
                 * one read and checking the whole block for data.
                 */
                rc = vdiDiscardBlockAsync(pImage, pIoCtx, uBlock, pvBlock);
            }
            else if (fDiscard & VD_DISCARD_MARK_UNUSED)
            {
                /* Just zero out the given range. */
                memset(pvBlock, 0, cbDiscard);

                uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData + pImage->offStartData + offDiscard;
                rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pImage->pStorage,
                                            u64Offset, pvBlock, cbDiscard, pIoCtx,
                                            NULL, NULL);
                RTMemFree(pvBlock);
            }
            else
            {
                /*
                 * Read complete block as metadata, the I/O context has no memory buffer
                 * and we need to access the content directly anyway.
                 */
                PVDMETAXFER pMetaXfer;
                pbBlockData = (uint8_t *)pvBlock + pImage->offStartBlockData;

                uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData + pImage->offStartData;
                rc = vdIfIoIntFileReadMeta(pImage->pIfIo, pImage->pStorage, u64Offset,
                                           pbBlockData, pImage->cbTotalBlockData,
                                           pIoCtx, &pMetaXfer, NULL, NULL);
                if (RT_FAILURE(rc))
                {
                    RTMemFree(pvBlock);
                    break;
                }

                vdIfIoIntMetaXferRelease(pImage->pIfIo, pMetaXfer);

                /* Clear data. */
                memset(pbBlockData + offDiscard , 0, cbDiscard);

                Assert(!(cbDiscard % 4));
                Assert(getImageBlockSize(&pImage->Header) * 8 <= UINT32_MAX);
                if (ASMBitFirstSet((volatile void *)pbBlockData, getImageBlockSize(&pImage->Header) * 8) == -1)
                    rc = vdiDiscardBlockAsync(pImage, pIoCtx, uBlock, pvBlock);
                else
                {
                    /* Block has data, create allocation bitmap. */
                    *pcbPreAllocated = cbPreAllocated;
                    *pcbPostAllocated = cbPostAllocated;
                    *ppbmAllocationBitmap = vdiAllocationBitmapCreate(pbBlockData, getImageBlockSize(&pImage->Header));
                    if (RT_UNLIKELY(!*ppbmAllocationBitmap))
                        rc = VERR_NO_MEMORY;
                    else
                        rc = VERR_VD_DISCARD_ALIGNMENT_NOT_MET;

                    RTMemFree(pvBlock);
                }
            } /* if: no complete block discarded */
        } /* if: Block is allocated. */
        /* else: nothing to do. */
    } while (0);

    if (pcbActuallyDiscarded)
        *pcbActuallyDiscarded = cbDiscard;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRepair */
static DECLCALLBACK(int) vdiRepair(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                   PVDINTERFACE pVDIfsImage, uint32_t fFlags)
{
    LogFlowFunc(("pszFilename=\"%s\" pVDIfsDisk=%#p pVDIfsImage=%#p\n", pszFilename, pVDIfsDisk, pVDIfsImage));
    int rc;
    PVDINTERFACEERROR pIfError;
    PVDINTERFACEIOINT pIfIo;
    PVDIOSTORAGE pStorage = NULL;
    uint64_t cbFile;
    PVDIIMAGEBLOCKPOINTER paBlocks = NULL;
    uint32_t *pu32BlockBitmap = NULL;
    VDIPREHEADER PreHdr;
    VDIHEADER    Hdr;

    pIfIo = VDIfIoIntGet(pVDIfsImage);
    AssertPtrReturn(pIfIo, VERR_INVALID_PARAMETER);

    pIfError = VDIfErrorGet(pVDIfsDisk);

    do
    {
        bool fRepairBlockArray = false;
        bool fRepairHdr = false;

        rc = vdIfIoIntFileOpen(pIfIo, pszFilename,
                               VDOpenFlagsToFileOpenFlags(  fFlags & VD_REPAIR_DRY_RUN
                                                          ? VD_OPEN_FLAGS_READONLY
                                                          : 0,
                                                          false /* fCreate */),
                               &pStorage);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pIfError, rc, RT_SRC_POS, "VDI: Failed to open image \"%s\"", pszFilename);
            break;
        }

        rc = vdIfIoIntFileGetSize(pIfIo, pStorage, &cbFile);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pIfError, rc, RT_SRC_POS, "VDI: Failed to query image size");
            break;
        }

        /* Read pre-header. */
        rc = vdIfIoIntFileReadSync(pIfIo, pStorage, 0, &PreHdr, sizeof(PreHdr));
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pIfError, rc, RT_SRC_POS, N_("VDI: Error reading pre-header in '%s'"), pszFilename);
            break;
        }
        vdiConvPreHeaderEndianess(VDIECONV_F2H, &PreHdr, &PreHdr);
        rc = vdiValidatePreHeader(&PreHdr);
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pIfError, VERR_VD_IMAGE_REPAIR_IMPOSSIBLE, RT_SRC_POS,
                           N_("VDI: invalid pre-header in '%s'"), pszFilename);
            break;
        }

        /* Read header. */
        Hdr.uVersion = PreHdr.u32Version;
        switch (GET_MAJOR_HEADER_VERSION(&Hdr))
        {
            case 0:
                rc = vdIfIoIntFileReadSync(pIfIo, pStorage, sizeof(PreHdr),
                                           &Hdr.u.v0, sizeof(Hdr.u.v0));
                if (RT_FAILURE(rc))
                    rc = vdIfError(pIfError, rc, RT_SRC_POS, N_("VDI: error reading v0 header in '%s'"),
                                   pszFilename);
                vdiConvHeaderEndianessV0(VDIECONV_F2H, &Hdr.u.v0, &Hdr.u.v0);
                break;
            case 1:
                rc = vdIfIoIntFileReadSync(pIfIo, pStorage, sizeof(PreHdr),
                                           &Hdr.u.v1, sizeof(Hdr.u.v1));
                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pIfError, rc, RT_SRC_POS, N_("VDI: error reading v1 header in '%s'"),
                                   pszFilename);
                }
                vdiConvHeaderEndianessV1(VDIECONV_F2H, &Hdr.u.v1, &Hdr.u.v1);
                if (Hdr.u.v1.cbHeader >= sizeof(Hdr.u.v1plus))
                {
                    /* Read the VDI 1.1+ header completely. */
                    rc = vdIfIoIntFileReadSync(pIfIo, pStorage, sizeof(PreHdr),
                                               &Hdr.u.v1plus, sizeof(Hdr.u.v1plus));
                    if (RT_FAILURE(rc))
                        rc = vdIfError(pIfError, rc, RT_SRC_POS, N_("VDI: error reading v1.1+ header in '%s'"),
                                       pszFilename);
                    vdiConvHeaderEndianessV1p(VDIECONV_F2H, &Hdr.u.v1plus, &Hdr.u.v1plus);
                }
                break;
            default:
                rc = vdIfError(pIfError, VERR_VD_IMAGE_REPAIR_IMPOSSIBLE, RT_SRC_POS,
                               N_("VDI: unsupported major version %u in '%s'"),
                               GET_MAJOR_HEADER_VERSION(&Hdr), pszFilename);
                break;
        }

        if (RT_SUCCESS(rc))
        {
            rc = vdiValidateHeader(&Hdr);
            if (RT_FAILURE(rc))
            {
                rc = vdIfError(pIfError, VERR_VD_IMAGE_REPAIR_IMPOSSIBLE, RT_SRC_POS,
                               N_("VDI: invalid header in '%s'"), pszFilename);
                break;
            }
        }

        /*
         * Check that the disk size is correctly aligned,
         * see comment above the same check in vdiImageReadHeader().
         */
        uint64_t cbDisk = getImageDiskSize(&Hdr);
        if (cbDisk & 0x1ff)
        {
            uint64_t cbDiskNew = cbDisk & ~UINT64_C(0x1ff);
            vdIfErrorMessage(pIfError, "Disk size in the header is not sector aligned, rounding down (%llu -> %llu)\n",
                             cbDisk, cbDiskNew);
            setImageDiskSize(&Hdr, cbDiskNew);
            fRepairHdr = true;
        }

        /* Setup image parameters by header. */
        uint64_t offStartBlocks, offStartData;
        size_t cbTotalBlockData;

        offStartBlocks     = getImageBlocksOffset(&Hdr);
        offStartData       = getImageDataOffset(&Hdr);
        cbTotalBlockData   = getImageExtraBlockSize(&Hdr) + getImageBlockSize(&Hdr);

        /* Allocate memory for blocks array. */
        paBlocks = (PVDIIMAGEBLOCKPOINTER)RTMemAlloc(sizeof(VDIIMAGEBLOCKPOINTER) * getImageBlocks(&Hdr));
        if (!paBlocks)
        {
            rc = vdIfError(pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                           "Failed to allocate memory for block array");
            break;
        }

        /* Read blocks array. */
        rc = vdIfIoIntFileReadSync(pIfIo, pStorage, offStartBlocks, paBlocks,
                                   getImageBlocks(&Hdr) * sizeof(VDIIMAGEBLOCKPOINTER));
        if (RT_FAILURE(rc))
        {
            rc = vdIfError(pIfError, VERR_VD_IMAGE_REPAIR_IMPOSSIBLE, RT_SRC_POS,
                           "Failed to read block array (at %llu), %Rrc",
                           offStartBlocks, rc);
            break;
        }
        vdiConvBlocksEndianess(VDIECONV_F2H, paBlocks, getImageBlocks(&Hdr));

        pu32BlockBitmap = (uint32_t *)RTMemAllocZ(RT_ALIGN_Z(getImageBlocks(&Hdr) / 8, 4));
        if (!pu32BlockBitmap)
        {
            rc = vdIfError(pIfError, VERR_NO_MEMORY, RT_SRC_POS,
                           "Failed to allocate memory for block bitmap");
            break;
        }

        for (uint32_t i = 0; i < getImageBlocks(&Hdr); i++)
        {
            if (IS_VDI_IMAGE_BLOCK_ALLOCATED(paBlocks[i]))
            {
                uint64_t offBlock =   (uint64_t)paBlocks[i] * cbTotalBlockData
                                    + offStartData;

                /*
                 * Check that the offsets are valid (inside of the image) and
                 * that there are no double references.
                 */
                if (offBlock + cbTotalBlockData > cbFile)
                {
                    vdIfErrorMessage(pIfError, "Entry %u points to invalid offset %llu, clearing\n",
                                     i, offBlock);
                    paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                    fRepairBlockArray = true;
                }
                else if (ASMBitTestAndSet(pu32BlockBitmap, paBlocks[i]))
                {
                    vdIfErrorMessage(pIfError, "Entry %u points to an already referenced data block, clearing\n",
                                     i);
                    paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                    fRepairBlockArray = true;
                }
            }
        }

        /* Write repaired structures now. */
        if (!fRepairBlockArray && !fRepairHdr)
            vdIfErrorMessage(pIfError, "VDI image is in a consistent state, no repair required\n");
        else if (!(fFlags & VD_REPAIR_DRY_RUN))
        {
            if (fRepairHdr)
            {
                switch (GET_MAJOR_HEADER_VERSION(&Hdr))
                {
                    case 0:
                    {
                        VDIHEADER0 Hdr0;
                        vdiConvHeaderEndianessV0(VDIECONV_H2F, &Hdr0, &Hdr.u.v0);
                        rc = vdIfIoIntFileWriteSync(pIfIo, pStorage, sizeof(VDIPREHEADER),
                                                    &Hdr0, sizeof(Hdr0));
                        break;
                    }
                    case 1:
                        if (Hdr.u.v1plus.cbHeader < sizeof(Hdr.u.v1plus))
                        {
                            VDIHEADER1 Hdr1;
                            vdiConvHeaderEndianessV1(VDIECONV_H2F, &Hdr1, &Hdr.u.v1);
                            rc = vdIfIoIntFileWriteSync(pIfIo, pStorage, sizeof(VDIPREHEADER),
                                                        &Hdr1, sizeof(Hdr1));
                        }
                        else
                        {
                            VDIHEADER1PLUS Hdr1plus;
                            vdiConvHeaderEndianessV1p(VDIECONV_H2F, &Hdr1plus, &Hdr.u.v1plus);
                            rc = vdIfIoIntFileWriteSync(pIfIo, pStorage, sizeof(VDIPREHEADER),
                                                        &Hdr1plus, sizeof(Hdr1plus));
                        }
                        break;
                    default:
                        AssertMsgFailed(("Header indicates unsupported version which should not happen here!\n"));
                        rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
                        break;
                }
            }

            if (fRepairBlockArray)
            {
                vdIfErrorMessage(pIfError, "Writing repaired block allocation table...\n");

                vdiConvBlocksEndianess(VDIECONV_H2F, paBlocks, getImageBlocks(&Hdr));
                rc = vdIfIoIntFileWriteSync(pIfIo, pStorage, offStartBlocks, paBlocks,
                                            getImageBlocks(&Hdr) * sizeof(VDIIMAGEBLOCKPOINTER));
                if (RT_FAILURE(rc))
                {
                    rc = vdIfError(pIfError, VERR_VD_IMAGE_REPAIR_IMPOSSIBLE, RT_SRC_POS,
                                   "Could not write repaired block allocation table (at %llu), %Rrc",
                                   offStartBlocks, rc);
                    break;
                }
            }
        }

        vdIfErrorMessage(pIfError, "Corrupted VDI image repaired successfully\n");
    } while(0);

    if (paBlocks)
        RTMemFree(paBlocks);

    if (pu32BlockBitmap)
        RTMemFree(pu32BlockBitmap);

    if (pStorage)
    {
        int rc2 = vdIfIoIntFileClose(pIfIo, pStorage);
        if (RT_SUCCESS(rc))
            rc = rc2; /* Propagate error code only if repairing was successful. */
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

const VDIMAGEBACKEND g_VDIBackend =
{
    /* u32Version */
    VD_IMGBACKEND_VERSION,
    /* pszBackendName */
    "VDI",
    /* uBackendCaps */
      VD_CAP_UUID | VD_CAP_CREATE_FIXED | VD_CAP_CREATE_DYNAMIC
    | VD_CAP_DIFF | VD_CAP_FILE | VD_CAP_ASYNC | VD_CAP_VFS | VD_CAP_DISCARD
    | VD_CAP_PREFERRED,
    /* paFileExtensions */
    s_aVdiFileExtensions,
    /* paConfigInfo */
    vdiConfigInfo,
    /* pfnProbe */
    vdiProbe,
    /* pfnOpen */
    vdiOpen,
    /* pfnCreate */
    vdiCreate,
    /* pfnRename */
    vdiRename,
    /* pfnClose */
    vdiClose,
    /* pfnRead */
    vdiRead,
    /* pfnWrite */
    vdiWrite,
    /* pfnFlush */
    vdiFlush,
    /* pfnDiscard */
    vdiDiscard,
    /* pfnGetVersion */
    vdiGetVersion,
    /* pfnGetFileSize */
    vdiGetFileSize,
    /* pfnGetPCHSGeometry */
    vdiGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    vdiSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    vdiGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    vdiSetLCHSGeometry,
    /* pfnQueryRegions */
    vdiQueryRegions,
    /* pfnRegionListRelease */
    vdiRegionListRelease,
    /* pfnGetImageFlags */
    vdiGetImageFlags,
    /* pfnGetOpenFlags */
    vdiGetOpenFlags,
    /* pfnSetOpenFlags */
    vdiSetOpenFlags,
    /* pfnGetComment */
    vdiGetComment,
    /* pfnSetComment */
    vdiSetComment,
    /* pfnGetUuid */
    vdiGetUuid,
    /* pfnSetUuid */
    vdiSetUuid,
    /* pfnGetModificationUuid */
    vdiGetModificationUuid,
    /* pfnSetModificationUuid */
    vdiSetModificationUuid,
    /* pfnGetParentUuid */
    vdiGetParentUuid,
    /* pfnSetParentUuid */
    vdiSetParentUuid,
    /* pfnGetParentModificationUuid */
    vdiGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    vdiSetParentModificationUuid,
    /* pfnDump */
    vdiDump,
    /* pfnGetTimestamp */
    NULL,
    /* pfnGetParentTimestamp */
    NULL,
    /* pfnSetParentTimestamp */
    NULL,
    /* pfnGetParentFilename */
    NULL,
    /* pfnSetParentFilename */
    NULL,
    /* pfnComposeLocation */
    genericFileComposeLocation,
    /* pfnComposeName */
    genericFileComposeName,
    /* pfnCompact */
    vdiCompact,
    /* pfnResize */
    vdiResize,
    /* pfnRepair */
    vdiRepair,
    /* pfnTraverseMetadata */
    NULL,
    /* u32VersionEnd */
    VD_IMGBACKEND_VERSION
};
