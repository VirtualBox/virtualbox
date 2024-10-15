/* $Id: Parallels.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * Parallels hdd disk image, core code.
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

#define LOG_GROUP LOG_GROUP_VD_PARALLELS
#include <VBox/vd-plugin.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/uuid.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/asm.h>

#include "VDBackends.h"

#define PARALLELS_HEADER_MAGIC "WithoutFreeSpace"
#define PARALLELS_DISK_VERSION 2

/** The header of the parallels disk. */
#pragma pack(1)
typedef struct ParallelsHeader
{
    /** The magic header to identify a parallels hdd image. */
    char        HeaderIdentifier[16];
    /** The version of the disk image. */
    uint32_t    uVersion;
    /** The number of heads the hdd has. */
    uint32_t    cHeads;
    /** Number of cylinders. */
    uint32_t    cCylinders;
    /** Number of sectors per track. */
    uint32_t    cSectorsPerTrack;
    /** Number of entries in the allocation bitmap. */
    uint32_t    cEntriesInAllocationBitmap;
    /** Total number of sectors. */
    uint32_t    cSectors;
    /** Padding. */
    char        Padding[24];
} ParallelsHeader;
#pragma pack()

/**
 * Parallels image structure.
 */
typedef struct PARALLELSIMAGE
{
    /** Image file name. */
    const char         *pszFilename;
    /** Opaque storage handle. */
    PVDIOSTORAGE        pStorage;

    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE        pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE        pVDIfsImage;
    /** Error interface. */
    PVDINTERFACEERROR   pIfError;
    /** I/O interface. */
    PVDINTERFACEIOINT   pIfIo;

    /** Open flags passed by VBoxHDD layer. */
    unsigned            uOpenFlags;
    /** Image flags defined during creation or determined during open. */
    unsigned            uImageFlags;
    /** Total size of the image. */
    uint64_t            cbSize;

    /** Physical geometry of this image. */
    VDGEOMETRY          PCHSGeometry;
    /** Logical geometry of this image. */
    VDGEOMETRY          LCHSGeometry;

    /** Pointer to the allocation bitmap. */
    uint32_t           *pAllocationBitmap;
    /** Entries in the allocation bitmap. */
    uint64_t            cAllocationBitmapEntries;
    /** Flag whether the allocation bitmap was changed. */
    bool                fAllocationBitmapChanged;
    /** Current file size. */
    uint64_t            cbFileCurrent;
    /** The static region list. */
    VDREGIONLIST        RegionList;
} PARALLELSIMAGE, *PPARALLELSIMAGE;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const VDFILEEXTENSION s_aParallelsFileExtensions[] =
{
    {"hdd", VDTYPE_HDD},
    {NULL, VDTYPE_INVALID}
};

/***************************************************
 * Internal functions                              *
 **************************************************/

/**
 * Internal. Flush image data to disk.
 */
static int parallelsFlushImage(PPARALLELSIMAGE pImage)
{
    int rc = VINF_SUCCESS;

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        return VINF_SUCCESS;

    if (   !(pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED)
        && (pImage->fAllocationBitmapChanged))
    {
        pImage->fAllocationBitmapChanged = false;
        /* Write the allocation bitmap to the file. */
        rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage,
                                    sizeof(ParallelsHeader), pImage->pAllocationBitmap,
                                    pImage->cAllocationBitmapEntries * sizeof(uint32_t));
        if (RT_FAILURE(rc))
            return rc;
    }

    /* Flush file. */
    rc = vdIfIoIntFileFlushSync(pImage->pIfIo, pImage->pStorage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Internal. Free all allocated space for representing an image except pImage,
 * and optionally delete the image from disk.
 */
static int parallelsFreeImage(PPARALLELSIMAGE pImage, bool fDelete)
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
                parallelsFlushImage(pImage);

            rc = vdIfIoIntFileClose(pImage->pIfIo, pImage->pStorage);
            pImage->pStorage = NULL;
        }

        if (pImage->pAllocationBitmap)
        {
            RTMemFree(pImage->pAllocationBitmap);
            pImage->pAllocationBitmap = NULL;
        }

        if (fDelete && pImage->pszFilename)
            vdIfIoIntFileDelete(pImage->pIfIo, pImage->pszFilename);
    }

    return rc;
}

static int parallelsOpenImage(PPARALLELSIMAGE pImage, unsigned uOpenFlags)
{
    pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
    pImage->uOpenFlags = uOpenFlags;
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    int rc = vdIfIoIntFileOpen(pImage->pIfIo, pImage->pszFilename,
                               VDOpenFlagsToFileOpenFlags(uOpenFlags,
                                                          false /* fCreate */),
                               &pImage->pStorage);
    if (RT_SUCCESS(rc))
    {
        rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &pImage->cbFileCurrent);
        if (RT_SUCCESS(rc)
            && !(pImage->cbFileCurrent % 512))
        {
            ParallelsHeader parallelsHeader;

            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage, 0,
                                       &parallelsHeader, sizeof(parallelsHeader));
            if (RT_SUCCESS(rc))
            {
                if (memcmp(parallelsHeader.HeaderIdentifier, PARALLELS_HEADER_MAGIC, 16))
                {
                    /* Check if the file has hdd as extension. It is a fixed size raw image then. */
                    char *pszSuffix = RTPathSuffix(pImage->pszFilename);
                    if (!strcmp(pszSuffix, ".hdd"))
                    {
                        /* This is a fixed size image. */
                        pImage->uImageFlags |= VD_IMAGE_FLAGS_FIXED;
                        pImage->cbSize = pImage->cbFileCurrent;

                        pImage->PCHSGeometry.cHeads     = 16;
                        pImage->PCHSGeometry.cSectors   = 63;
                        uint64_t cCylinders = pImage->cbSize / (512 * pImage->PCHSGeometry.cSectors * pImage->PCHSGeometry.cHeads);
                        pImage->PCHSGeometry.cCylinders = (uint32_t)cCylinders;
                    }
                    else
                        rc = VERR_VD_PARALLELS_INVALID_HEADER;
                }
                else
                {
                    if (   parallelsHeader.uVersion == PARALLELS_DISK_VERSION
                        && parallelsHeader.cEntriesInAllocationBitmap <= (1 << 30))
                    {
                        Log(("cSectors=%u\n", parallelsHeader.cSectors));
                        pImage->cbSize = ((uint64_t)parallelsHeader.cSectors) * 512;
                        pImage->uImageFlags = VD_IMAGE_FLAGS_NONE;
                        pImage->PCHSGeometry.cCylinders = parallelsHeader.cCylinders;
                        pImage->PCHSGeometry.cHeads     = parallelsHeader.cHeads;
                        pImage->PCHSGeometry.cSectors   = parallelsHeader.cSectorsPerTrack;
                        pImage->cAllocationBitmapEntries = parallelsHeader.cEntriesInAllocationBitmap;
                        pImage->pAllocationBitmap = (uint32_t *)RTMemAllocZ((uint32_t)pImage->cAllocationBitmapEntries * sizeof(uint32_t));
                        if (RT_LIKELY(pImage->pAllocationBitmap))
                            rc = vdIfIoIntFileReadSync(pImage->pIfIo, pImage->pStorage,
                                                       sizeof(ParallelsHeader), pImage->pAllocationBitmap,
                                                       pImage->cAllocationBitmapEntries * sizeof(uint32_t));
                        else
                            rc = VERR_NO_MEMORY;
                    }
                    else
                        rc = VERR_NOT_SUPPORTED;
                }
            }
        }
        else if (RT_SUCCESS(rc))
            rc = VERR_VD_PARALLELS_INVALID_HEADER;
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
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;
    }
    else
        parallelsFreeImage(pImage, false);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Internal: Create a parallels image.
 */
static int parallelsCreateImage(PPARALLELSIMAGE pImage, uint64_t cbSize,
                                unsigned uImageFlags, const char *pszComment,
                                PCVDGEOMETRY pPCHSGeometry,
                                PCVDGEOMETRY pLCHSGeometry, unsigned uOpenFlags,
                                PFNVDPROGRESS pfnProgress, void *pvUser,
                                unsigned uPercentStart, unsigned uPercentSpan)
{
    RT_NOREF1(pszComment);
    int rc = VINF_SUCCESS;
    int32_t fOpen;

    if (!(uImageFlags & VD_IMAGE_FLAGS_FIXED))
    {
        pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);
        pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
        AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

        pImage->uOpenFlags   = uOpenFlags & ~VD_OPEN_FLAGS_READONLY;
        pImage->uImageFlags  = uImageFlags;
        pImage->PCHSGeometry = *pPCHSGeometry;
        pImage->LCHSGeometry = *pLCHSGeometry;
        if (!pImage->PCHSGeometry.cCylinders)
        {
            /* Set defaults. */
            pImage->PCHSGeometry.cSectors   = 63;
            pImage->PCHSGeometry.cHeads     = 16;
            pImage->PCHSGeometry.cCylinders = pImage->cbSize / (512 * pImage->PCHSGeometry.cSectors * pImage->PCHSGeometry.cHeads);
        }

        /* Create image file. */
        fOpen = VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags, true /* fCreate */);
        rc = vdIfIoIntFileOpen(pImage->pIfIo, pImage->pszFilename, fOpen, &pImage->pStorage);
        if (RT_SUCCESS(rc))
        {
            if (pfnProgress)
                pfnProgress(pvUser, uPercentStart + uPercentSpan * 98 / 100);

            /* Setup image state. */
            pImage->cbSize                   = cbSize;
            pImage->cAllocationBitmapEntries = cbSize / 512 / pImage->PCHSGeometry.cSectors;
            if (pImage->cAllocationBitmapEntries * pImage->PCHSGeometry.cSectors * 512 < cbSize)
                pImage->cAllocationBitmapEntries++;
            pImage->fAllocationBitmapChanged = true;
            pImage->cbFileCurrent            = sizeof(ParallelsHeader) + pImage->cAllocationBitmapEntries * sizeof(uint32_t);
            /* Round to next sector boundary. */
            pImage->cbFileCurrent           += 512 - pImage->cbFileCurrent % 512;
            Assert(!(pImage->cbFileCurrent % 512));
            pImage->pAllocationBitmap        = (uint32_t *)RTMemAllocZ(pImage->cAllocationBitmapEntries * sizeof(uint32_t));
            if (pImage->pAllocationBitmap)
            {
                ParallelsHeader Header;

                memcpy(Header.HeaderIdentifier, PARALLELS_HEADER_MAGIC, sizeof(Header.HeaderIdentifier));
                Header.uVersion                   = RT_H2LE_U32(PARALLELS_DISK_VERSION);
                Header.cHeads                     = RT_H2LE_U32(pImage->PCHSGeometry.cHeads);
                Header.cCylinders                 = RT_H2LE_U32(pImage->PCHSGeometry.cCylinders);
                Header.cSectorsPerTrack           = RT_H2LE_U32(pImage->PCHSGeometry.cSectors);
                Header.cEntriesInAllocationBitmap = RT_H2LE_U32(pImage->cAllocationBitmapEntries);
                Header.cSectors                   = RT_H2LE_U32(pImage->cbSize / 512);
                memset(Header.Padding, 0, sizeof(Header.Padding));

                /* Write header and allocation bitmap. */
                rc = vdIfIoIntFileSetSize(pImage->pIfIo, pImage->pStorage, pImage->cbFileCurrent);
                if (RT_SUCCESS(rc))
                    rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage, 0,
                                                &Header, sizeof(Header));
                if (RT_SUCCESS(rc))
                    rc = parallelsFlushImage(pImage); /* Writes the allocation bitmap. */
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("Parallels: cannot create image '%s'"), pImage->pszFilename);
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_VD_INVALID_TYPE, RT_SRC_POS, N_("Parallels: cannot create fixed image '%s'. Create a raw image"), pImage->pszFilename);

    if (RT_SUCCESS(rc) && pfnProgress)
        pfnProgress(pvUser, uPercentStart + uPercentSpan);

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
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;
    }
    else
        parallelsFreeImage(pImage, rc != VERR_ALREADY_EXISTS);
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnProbe */
static DECLCALLBACK(int) parallelsProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                        PVDINTERFACE pVDIfsImage, VDTYPE enmDesiredType, VDTYPE *penmType)
{
    RT_NOREF(pVDIfsDisk, enmDesiredType);
    int rc;
    PVDIOSTORAGE pStorage;
    ParallelsHeader parallelsHeader;

    PVDINTERFACEIOINT pIfIo = VDIfIoIntGet(pVDIfsImage);
    AssertPtrReturn(pIfIo, VERR_INVALID_PARAMETER);

    rc = vdIfIoIntFileOpen(pIfIo, pszFilename,
                           VDOpenFlagsToFileOpenFlags(VD_OPEN_FLAGS_READONLY,
                                                      false /* fCreate */),
                           &pStorage);
    if (RT_FAILURE(rc))
        return rc;

    rc = vdIfIoIntFileReadSync(pIfIo, pStorage, 0, &parallelsHeader,
                               sizeof(ParallelsHeader));
    if (RT_SUCCESS(rc))
    {
        if (   !memcmp(parallelsHeader.HeaderIdentifier, PARALLELS_HEADER_MAGIC, 16)
            && (parallelsHeader.uVersion == PARALLELS_DISK_VERSION))
            rc = VINF_SUCCESS;
        else
        {
            /*
             * The image may be an fixed size image.
             * Unfortunately fixed sized parallels images
             * are just raw files hence no magic header to
             * check for.
             * The code succeeds if the file is a multiple
             * of 512 and if the file extensions is *.hdd
             */
            uint64_t cbFile;
            char *pszSuffix;

            rc = vdIfIoIntFileGetSize(pIfIo, pStorage, &cbFile);
            if (RT_FAILURE(rc) || ((cbFile % 512) != 0))
            {
                vdIfIoIntFileClose(pIfIo, pStorage);
                return VERR_VD_PARALLELS_INVALID_HEADER;
            }

            pszSuffix = RTPathSuffix(pszFilename);
            if (!pszSuffix || strcmp(pszSuffix, ".hdd"))
                rc = VERR_VD_PARALLELS_INVALID_HEADER;
            else
                rc = VINF_SUCCESS;
        }
    }

    if (RT_SUCCESS(rc))
        *penmType = VDTYPE_HDD;

    vdIfIoIntFileClose(pIfIo, pStorage);
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnOpen */
static DECLCALLBACK(int) parallelsOpen(const char *pszFilename, unsigned uOpenFlags,
                                       PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                       VDTYPE enmType, void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p enmType=%u ppBackendData=%#p\n", pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, enmType, ppBackendData));
    int rc;
    PPARALLELSIMAGE pImage;

    NOREF(enmType); /**< @todo r=klaus make use of the type info. */

    /* Check parameters. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);


    pImage = (PPARALLELSIMAGE)RTMemAllocZ(RT_UOFFSETOF(PARALLELSIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pStorage = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;
        pImage->fAllocationBitmapChanged = false;

        rc = parallelsOpenImage(pImage, uOpenFlags);
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
static DECLCALLBACK(int) parallelsCreate(const char *pszFilename, uint64_t cbSize,
                                         unsigned uImageFlags, const char *pszComment,
                                         PCVDGEOMETRY pPCHSGeometry,
                                         PCVDGEOMETRY pLCHSGeometry, PCRTUUID pUuid,
                                         unsigned uOpenFlags, unsigned uPercentStart,
                                         unsigned uPercentSpan, PVDINTERFACE pVDIfsDisk,
                                         PVDINTERFACE pVDIfsImage,
                                         PVDINTERFACE pVDIfsOperation, VDTYPE enmType,
                                         void **ppBackendData)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" pPCHSGeometry=%#p pLCHSGeometry=%#p Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p enmType=%u ppBackendData=%#p",
                 pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData));

    /* Check the VD container type. */
    if (enmType != VDTYPE_HDD)
        return VERR_VD_INVALID_TYPE;

    /* Check arguments. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pPCHSGeometry, VERR_INVALID_POINTER);
    AssertPtrReturn(pLCHSGeometry, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PPARALLELSIMAGE pImage;
    PFNVDPROGRESS pfnProgress = NULL;
    void *pvUser = NULL;
    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);
    if (pIfProgress)
    {
        pfnProgress = pIfProgress->pfnProgress;
        pvUser = pIfProgress->Core.pvUser;
    }

    pImage = (PPARALLELSIMAGE)RTMemAllocZ(RT_UOFFSETOF(PARALLELSIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pStorage = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        rc = parallelsCreateImage(pImage, cbSize, uImageFlags, pszComment,
                                  pPCHSGeometry, pLCHSGeometry, uOpenFlags,
                                  pfnProgress, pvUser, uPercentStart, uPercentSpan);
        if (RT_SUCCESS(rc))
        {
            /* So far the image is opened in read/write mode. Make sure the
             * image is opened in read-only mode if the caller requested that. */
            if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
            {
                parallelsFreeImage(pImage, false);
                rc = parallelsOpenImage(pImage, uOpenFlags);
            }

            if (RT_SUCCESS(rc))
                *ppBackendData = pImage;
        }

        if (RT_FAILURE(rc))
            RTMemFree(pImage);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRename */
static DECLCALLBACK(int) parallelsRename(void *pBackendData, const char *pszFilename)
{
    LogFlowFunc(("pBackendData=%#p pszFilename=%#p\n", pBackendData, pszFilename));
    int rc = VINF_SUCCESS;
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    /* Check arguments. */
    AssertReturn((pImage && pszFilename && *pszFilename), VERR_INVALID_PARAMETER);

    /* Close the image. */
    rc = parallelsFreeImage(pImage, false);
    if (RT_SUCCESS(rc))
    {
        /* Rename the file. */
        rc = vdIfIoIntFileMove(pImage->pIfIo, pImage->pszFilename, pszFilename, 0);
        if (RT_SUCCESS(rc))
        {
            /* Update pImage with the new information. */
            pImage->pszFilename = pszFilename;

            /* Open the old image with new name. */
            rc = parallelsOpenImage(pImage, pImage->uOpenFlags);
        }
        else
        {
            /* The move failed, try to reopen the original image. */
            int rc2 = parallelsOpenImage(pImage, pImage->uOpenFlags);
            if (RT_FAILURE(rc2))
                rc = rc2;
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnClose */
static DECLCALLBACK(int) parallelsClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
    int rc = parallelsFreeImage(pImage, fDelete);
    RTMemFree(pImage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRead */
static DECLCALLBACK(int) parallelsRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                       PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToRead=%zu pcbActuallyRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToRead, pcbActuallyRead));
    int rc = VINF_SUCCESS;
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
    uint64_t uSector;
    uint64_t uOffsetInFile;
    uint32_t iIndexInAllocationTable;

    AssertPtr(pImage);
    Assert(uOffset % 512 == 0);
    Assert(cbToRead % 512 == 0);

    if (pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED)
        rc = vdIfIoIntFileReadUser(pImage->pIfIo, pImage->pStorage, uOffset,
                                   pIoCtx, cbToRead);
    else
    {
        /* Calculate offset in the real file. */
        uSector = uOffset / 512;
        /* One chunk in the file is always one track big. */
        iIndexInAllocationTable = (uint32_t)(uSector / pImage->PCHSGeometry.cSectors);
        uSector = uSector % pImage->PCHSGeometry.cSectors;

        cbToRead = RT_MIN(cbToRead, (pImage->PCHSGeometry.cSectors - uSector)*512);

        if (pImage->pAllocationBitmap[iIndexInAllocationTable] == 0)
            rc = VERR_VD_BLOCK_FREE;
        else
        {
            uOffsetInFile = (pImage->pAllocationBitmap[iIndexInAllocationTable] + uSector) * 512;
            rc = vdIfIoIntFileReadUser(pImage->pIfIo, pImage->pStorage, uOffsetInFile,
                                       pIoCtx, cbToRead);
        }
    }

    *pcbActuallyRead = cbToRead;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnWrite */
static DECLCALLBACK(int) parallelsWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                                        PVDIOCTX pIoCtx, size_t *pcbWriteProcess, size_t *pcbPreRead,
                                        size_t *pcbPostRead, unsigned fWrite)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToWrite=%zu pcbWriteProcess=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToWrite, pcbWriteProcess));
    int rc = VINF_SUCCESS;
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
    uint64_t uSector;
    uint64_t uOffsetInFile;
    uint32_t iIndexInAllocationTable;

    AssertPtr(pImage);
    Assert(uOffset % 512 == 0);
    Assert(cbToWrite % 512 == 0);

    if (pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED)
        rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pImage->pStorage, uOffset,
                                    pIoCtx, cbToWrite, NULL, NULL);
    else
    {
        /* Calculate offset in the real file. */
        uSector = uOffset / 512;
        /* One chunk in the file is always one track big. */
        iIndexInAllocationTable = (uint32_t)(uSector / pImage->PCHSGeometry.cSectors);
        uSector = uSector % pImage->PCHSGeometry.cSectors;

        cbToWrite = RT_MIN(cbToWrite, (pImage->PCHSGeometry.cSectors - uSector)*512);

        if (pImage->pAllocationBitmap[iIndexInAllocationTable] == 0)
        {
            if (fWrite & VD_WRITE_NO_ALLOC)
            {
                *pcbPreRead  = uSector * 512;
                *pcbPostRead = pImage->PCHSGeometry.cSectors * 512 - cbToWrite - *pcbPreRead;

                if (pcbWriteProcess)
                    *pcbWriteProcess = cbToWrite;
                return VERR_VD_BLOCK_FREE;
            }

            /* Allocate new chunk in the file. */
            Assert(uSector == 0);
            AssertMsg(pImage->cbFileCurrent % 512 == 0, ("File size is not a multiple of 512\n"));
            pImage->pAllocationBitmap[iIndexInAllocationTable] = (uint32_t)(pImage->cbFileCurrent / 512);
            pImage->cbFileCurrent += pImage->PCHSGeometry.cSectors * 512;
            pImage->fAllocationBitmapChanged = true;
            uOffsetInFile = (uint64_t)pImage->pAllocationBitmap[iIndexInAllocationTable] * 512;

            /*
             * Write the new block at the current end of the file.
             */
            rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pImage->pStorage,
                                        uOffsetInFile, pIoCtx, cbToWrite, NULL, NULL);
            if (RT_SUCCESS(rc) || (rc == VERR_VD_ASYNC_IO_IN_PROGRESS))
            {
                /* Write the changed allocation bitmap entry. */
                /** @todo Error handling. */
                rc = vdIfIoIntFileWriteMeta(pImage->pIfIo, pImage->pStorage,
                                            sizeof(ParallelsHeader) + iIndexInAllocationTable * sizeof(uint32_t),
                                            &pImage->pAllocationBitmap[iIndexInAllocationTable],
                                            sizeof(uint32_t), pIoCtx,
                                            NULL, NULL);
            }

            *pcbPreRead  = 0;
            *pcbPostRead = 0;
        }
        else
        {
            uOffsetInFile = (pImage->pAllocationBitmap[iIndexInAllocationTable] + uSector) * 512;
            rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pImage->pStorage,
                                        uOffsetInFile, pIoCtx, cbToWrite, NULL, NULL);
        }
    }

    if (pcbWriteProcess)
        *pcbWriteProcess = cbToWrite;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnFlush */
static DECLCALLBACK(int) parallelsFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    LogFlowFunc(("pImage=#%p\n", pImage));

    /* Flush the file, everything is up to date already. */
    rc = vdIfIoIntFileFlush(pImage->pIfIo, pImage->pStorage, pIoCtx, NULL, NULL);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) parallelsGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    return PARALLELS_DISK_VERSION;
}

/** @copydoc VDIMAGEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) parallelsGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
    uint64_t cb = 0;

    AssertPtrReturn(pImage, 0);

    if (pImage->pStorage)
        cb = pImage->cbFileCurrent;

    LogFlowFunc(("returns %lld\n", cb));
    return cb;
}

/** @copydoc VDIMAGEBACKEND::pfnGetPCHSGeometry */
static DECLCALLBACK(int) parallelsGetPCHSGeometry(void *pBackendData,
                                                  PVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
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
static DECLCALLBACK(int) parallelsSetPCHSGeometry(void *pBackendData,
                                                  PCVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n", pBackendData,
                 pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
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
static DECLCALLBACK(int) parallelsGetLCHSGeometry(void *pBackendData,
                                                  PVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (pImage->LCHSGeometry.cCylinders)
        *pLCHSGeometry = pImage->LCHSGeometry;
    else
        rc = VERR_VD_GEOMETRY_NOT_SET;

    LogFlowFunc(("returns %Rrc (LCHS=%u/%u/%u)\n", rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetLCHSGeometry */
static DECLCALLBACK(int) parallelsSetLCHSGeometry(void *pBackendData,
                                                  PCVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n", pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        pImage->LCHSGeometry = *pLCHSGeometry;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnQueryRegions */
static DECLCALLBACK(int) parallelsQueryRegions(void *pBackendData, PCVDREGIONLIST *ppRegionList)
{
    LogFlowFunc(("pBackendData=%#p ppRegionList=%#p\n", pBackendData, ppRegionList));
    PPARALLELSIMAGE pThis = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    *ppRegionList = &pThis->RegionList;
    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnRegionListRelease */
static DECLCALLBACK(void) parallelsRegionListRelease(void *pBackendData, PCVDREGIONLIST pRegionList)
{
    RT_NOREF1(pRegionList);
    LogFlowFunc(("pBackendData=%#p pRegionList=%#p\n", pBackendData, pRegionList));
    PPARALLELSIMAGE pThis = (PPARALLELSIMAGE)pBackendData;
    AssertPtr(pThis); RT_NOREF(pThis);

    /* Nothing to do here. */
}

/** @copydoc VDIMAGEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) parallelsGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uImageFlags));
    return pImage->uImageFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) parallelsGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uOpenFlags));
    return pImage->uOpenFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) parallelsSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p\n uOpenFlags=%#x", pBackendData, uOpenFlags));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    /* Image must be opened and the new flags must be valid. */
    if (!pImage || (uOpenFlags & ~(  VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO
                                   | VD_OPEN_FLAGS_ASYNC_IO | VD_OPEN_FLAGS_SHAREABLE
                                   | VD_OPEN_FLAGS_SEQUENTIAL | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)))
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /* Implement this operation via reopening the image. */
        parallelsFreeImage(pImage, false);
        rc = parallelsOpenImage(pImage, uOpenFlags);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetComment */
static DECLCALLBACK(int) parallelsGetComment(void *pBackendData, char *pszComment,
                                             size_t cbComment)
{
    RT_NOREF2(pszComment, cbComment);
    LogFlowFunc(("pBackendData=%#p pszComment=%#p cbComment=%zu\n", pBackendData, pszComment, cbComment));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    LogFlowFunc(("returns %Rrc comment='%s'\n", VERR_NOT_SUPPORTED, pszComment));
    return VERR_NOT_SUPPORTED;
}

/** @copydoc VDIMAGEBACKEND::pfnSetComment */
static DECLCALLBACK(int) parallelsSetComment(void *pBackendData, const char *pszComment)
{
    RT_NOREF1(pszComment);
    LogFlowFunc(("pBackendData=%#p pszComment=\"%s\"\n", pBackendData, pszComment));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetUuid */
static DECLCALLBACK(int) parallelsGetUuid(void *pBackendData, PRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VERR_NOT_SUPPORTED, pUuid));
    return VERR_NOT_SUPPORTED;
}

/** @copydoc VDIMAGEBACKEND::pfnSetUuid */
static DECLCALLBACK(int) parallelsSetUuid(void *pBackendData, PCRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetModificationUuid */
static DECLCALLBACK(int) parallelsGetModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VERR_NOT_SUPPORTED, pUuid));
    return VERR_NOT_SUPPORTED;
}

/** @copydoc VDIMAGEBACKEND::pfnSetModificationUuid */
static DECLCALLBACK(int) parallelsSetModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetParentUuid */
static DECLCALLBACK(int) parallelsGetParentUuid(void *pBackendData, PRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", VERR_NOT_SUPPORTED, pUuid));
    return VERR_NOT_SUPPORTED;
}

/** @copydoc VDIMAGEBACKEND::pfnSetParentUuid */
static DECLCALLBACK(int) parallelsSetParentUuid(void *pBackendData, PCRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetParentModificationUuid */
static DECLCALLBACK(int) parallelsGetParentModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnSetParentModificationUuid */
static DECLCALLBACK(int) parallelsSetParentModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturn(pImage, VERR_VD_NOT_OPENED);

    int rc;
    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        rc = VERR_VD_IMAGE_READ_ONLY;
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnDump */
static DECLCALLBACK(void) parallelsDump(void *pBackendData)
{
    PPARALLELSIMAGE pImage = (PPARALLELSIMAGE)pBackendData;

    AssertPtrReturnVoid(pImage);
    vdIfErrorMessage(pImage->pIfError, "Header: Geometry PCHS=%u/%u/%u LCHS=%u/%u/%u\n",
                     pImage->PCHSGeometry.cCylinders, pImage->PCHSGeometry.cHeads, pImage->PCHSGeometry.cSectors,
                     pImage->LCHSGeometry.cCylinders, pImage->LCHSGeometry.cHeads, pImage->LCHSGeometry.cSectors);
}



const VDIMAGEBACKEND g_ParallelsBackend =
{
    /* u32Version */
    VD_IMGBACKEND_VERSION,
    /* pszBackendName */
    "Parallels",
    /* uBackendCaps */
    VD_CAP_FILE | VD_CAP_ASYNC | VD_CAP_VFS | VD_CAP_CREATE_DYNAMIC | VD_CAP_DIFF,
    /* paFileExtensions */
    s_aParallelsFileExtensions,
    /* paConfigInfo */
    NULL,
    /* pfnProbe */
    parallelsProbe,
    /* pfnOpen */
    parallelsOpen,
    /* pfnCreate */
    parallelsCreate,
    /* pfnRename */
    parallelsRename,
    /* pfnClose */
    parallelsClose,
    /* pfnRead */
    parallelsRead,
    /* pfnWrite */
    parallelsWrite,
    /* pfnFlush */
    parallelsFlush,
    /* pfnDiscard */
    NULL,
    /* pfnGetVersion */
    parallelsGetVersion,
    /* pfnGetFileSize */
    parallelsGetFileSize,
    /* pfnGetPCHSGeometry */
    parallelsGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    parallelsSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    parallelsGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    parallelsSetLCHSGeometry,
    /* pfnQueryRegions */
    parallelsQueryRegions,
    /* pfnRegionListRelease */
    parallelsRegionListRelease,
    /* pfnGetImageFlags */
    parallelsGetImageFlags,
    /* pfnGetOpenFlags */
    parallelsGetOpenFlags,
    /* pfnSetOpenFlags */
    parallelsSetOpenFlags,
    /* pfnGetComment */
    parallelsGetComment,
    /* pfnSetComment */
    parallelsSetComment,
    /* pfnGetUuid */
    parallelsGetUuid,
    /* pfnSetUuid */
    parallelsSetUuid,
    /* pfnGetModificationUuid */
    parallelsGetModificationUuid,
    /* pfnSetModificationUuid */
    parallelsSetModificationUuid,
    /* pfnGetParentUuid */
    parallelsGetParentUuid,
    /* pfnSetParentUuid */
    parallelsSetParentUuid,
    /* pfnGetParentModificationUuid */
    parallelsGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    parallelsSetParentModificationUuid,
    /* pfnDump */
    parallelsDump,
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
    NULL,
    /* pfnResize */
    NULL,
    /* pfnRepair */
    NULL,
    /* pfnTraverseMetadata */
    NULL,
    /* u32VersionEnd */
    VD_IMGBACKEND_VERSION
};
