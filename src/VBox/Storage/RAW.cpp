/* $Id: RAW.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * RawHDDCore - Raw Disk image, Core Code.
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
#define LOG_GROUP LOG_GROUP_VD_RAW
#include <VBox/vd-plugin.h>
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/path.h>
#include <iprt/formats/iso9660.h>
#include <iprt/formats/udf.h>

#include "VDBackends.h"
#include "VDBackendsInline.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/**
 * Raw image data structure.
 */
typedef struct RAWIMAGE
{
    /** Image name. */
    const char          *pszFilename;
    /** Storage handle. */
    PVDIOSTORAGE        pStorage;

    /** Pointer to the per-disk VD interface list. */
    PVDINTERFACE        pVDIfsDisk;
    /** Pointer to the per-image VD interface list. */
    PVDINTERFACE        pVDIfsImage;
    /** Error interface. */
    PVDINTERFACEERROR   pIfError;
    /** I/O interface. */
    PVDINTERFACEIOINT   pIfIo;

    /** Open flags passed by VBoxHD layer. */
    unsigned            uOpenFlags;
    /** Image flags defined during creation or determined during open. */
    unsigned            uImageFlags;
    /** Total size of the image. */
    uint64_t            cbSize;
    /** Position in the image (only truly used for sequential access). */
    uint64_t            offAccess;
    /** Flag if this is a newly created image. */
    bool                fCreate;
    /** Physical geometry of this image. */
    VDGEOMETRY          PCHSGeometry;
    /** Logical geometry of this image. */
    VDGEOMETRY          LCHSGeometry;
    /** Sector size of the image. */
    uint32_t            cbSector;
    /** The static region list. */
    VDREGIONLIST        RegionList;
} RAWIMAGE, *PRAWIMAGE;


/** Size of write operations when filling an image with zeroes. */
#define RAW_FILL_SIZE (128 * _1K)

#if 1
/** The maximum reasonable size of a floppy image - (fake 63.5MB valkit floppies). */
# define RAW_MAX_FLOPPY_IMG_SIZE (255 * 2 * 255 * 512)
#else
/** The maximum reasonable size of a floppy image (big format 2.88MB medium). */
# define RAW_MAX_FLOPPY_IMG_SIZE (512 * 82 * 48 * 2)
#endif


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const VDFILEEXTENSION s_aRawFileExtensions[] =
{
    {"iso", VDTYPE_OPTICAL_DISC},
    {"cdr", VDTYPE_OPTICAL_DISC},
    {"img", VDTYPE_FLOPPY},
    {"ima", VDTYPE_FLOPPY},
    {"dsk", VDTYPE_FLOPPY},
    {"flp", VDTYPE_FLOPPY},
    {"vfd", VDTYPE_FLOPPY},
    {NULL, VDTYPE_INVALID}
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Internal. Flush image data to disk.
 */
static int rawFlushImage(PRAWIMAGE pImage)
{
    int rc = VINF_SUCCESS;

    if (   pImage->pStorage
        && !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        rc = vdIfIoIntFileFlushSync(pImage->pIfIo, pImage->pStorage);

    return rc;
}

/**
 * Internal. Free all allocated space for representing an image except pImage,
 * and optionally delete the image from disk.
 */
static int rawFreeImage(PRAWIMAGE pImage, bool fDelete)
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
            {
                /* For newly created images in sequential mode fill it to
                 * the nominal size. */
                if (   pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL
                    && !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                    && pImage->fCreate)
                {
                    /* Fill rest of image with zeroes, a must for sequential
                     * images to reach the nominal size. */
                    uint64_t uOff;
                    void *pvBuf = RTMemTmpAllocZ(RAW_FILL_SIZE);
                    if (RT_LIKELY(pvBuf))
                    {
                        uOff = pImage->offAccess;
                        /* Write data to all image blocks. */
                        while (uOff < pImage->cbSize)
                        {
                            unsigned cbChunk = (unsigned)RT_MIN(pImage->cbSize - uOff,
                                                                RAW_FILL_SIZE);

                            rc = vdIfIoIntFileWriteSync(pImage->pIfIo, pImage->pStorage,
                                                        uOff, pvBuf, cbChunk);
                            if (RT_FAILURE(rc))
                                break;

                            uOff += cbChunk;
                        }

                        RTMemTmpFree(pvBuf);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                rawFlushImage(pImage);
            }

            rc = vdIfIoIntFileClose(pImage->pIfIo, pImage->pStorage);
            pImage->pStorage = NULL;
        }

        if (fDelete && pImage->pszFilename)
            vdIfIoIntFileDelete(pImage->pIfIo, pImage->pszFilename);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Internal: Open an image, constructing all necessary data structures.
 */
static int rawOpenImage(PRAWIMAGE pImage, unsigned uOpenFlags)
{
    pImage->uOpenFlags = uOpenFlags;
    pImage->fCreate = false;

    pImage->pIfError = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo = VDIfIoIntGet(pImage->pVDIfsImage);
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    /* Open the image. */
    int rc = vdIfIoIntFileOpen(pImage->pIfIo, pImage->pszFilename,
                               VDOpenFlagsToFileOpenFlags(uOpenFlags,
                                                          false /* fCreate */),
                               &pImage->pStorage);
    if (RT_SUCCESS(rc))
    {
        rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &pImage->cbSize);
        if (   RT_SUCCESS(rc)
            && !(pImage->cbSize % 512))
            pImage->uImageFlags |= VD_IMAGE_FLAGS_FIXED;
        else if (RT_SUCCESS(rc))
            rc = VERR_VD_RAW_SIZE_MODULO_512;
    }
    /* else: Do NOT signal an appropriate error here, as the VD layer has the
     *       choice of retrying the open if it failed. */

    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = pImage->cbSector;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = pImage->cbSector;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;
    }
    else
        rawFreeImage(pImage, false);
    return rc;
}

/**
 * Internal: Create a raw image.
 */
static int rawCreateImage(PRAWIMAGE pImage, uint64_t cbSize,
                          unsigned uImageFlags, const char *pszComment,
                          PCVDGEOMETRY pPCHSGeometry,
                          PCVDGEOMETRY pLCHSGeometry, unsigned uOpenFlags,
                          PVDINTERFACEPROGRESS pIfProgress,
                          unsigned uPercentStart, unsigned uPercentSpan)
{
    RT_NOREF1(pszComment);
    int rc = VINF_SUCCESS;

    pImage->fCreate      = true;
    pImage->uOpenFlags   = uOpenFlags & ~VD_OPEN_FLAGS_READONLY;
    pImage->uImageFlags  = uImageFlags | VD_IMAGE_FLAGS_FIXED;
    pImage->PCHSGeometry = *pPCHSGeometry;
    pImage->LCHSGeometry = *pLCHSGeometry;
    pImage->pIfError     = VDIfErrorGet(pImage->pVDIfsDisk);
    pImage->pIfIo        = VDIfIoIntGet(pImage->pVDIfsImage);
    AssertPtrReturn(pImage->pIfIo, VERR_INVALID_PARAMETER);

    if (!(pImage->uImageFlags & VD_IMAGE_FLAGS_DIFF))
    {
        /* Create image file. */
        uint32_t fOpen = VDOpenFlagsToFileOpenFlags(pImage->uOpenFlags, true /* fCreate */);
        if (uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL)
            fOpen &= ~RTFILE_O_READ;
        rc = vdIfIoIntFileOpen(pImage->pIfIo, pImage->pszFilename, fOpen, &pImage->pStorage);
        if (RT_SUCCESS(rc))
        {
            if (!(uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL))
            {
                RTFOFF cbFree = 0;

                /* Check the free space on the disk and leave early if there is not
                 * sufficient space available. */
                rc = vdIfIoIntFileGetFreeSpace(pImage->pIfIo, pImage->pszFilename, &cbFree);
                if (RT_FAILURE(rc) /* ignore errors */ || ((uint64_t)cbFree >= cbSize))
                {
                    rc = vdIfIoIntFileSetAllocationSize(pImage->pIfIo, pImage->pStorage, cbSize, 0 /* fFlags */,
                                                        pIfProgress, uPercentStart, uPercentSpan);
                    if (RT_SUCCESS(rc))
                    {
                        vdIfProgress(pIfProgress, uPercentStart + uPercentSpan * 98 / 100);

                        pImage->cbSize = cbSize;
                        rc = rawFlushImage(pImage);
                    }
                }
                else
                    rc = vdIfError(pImage->pIfError, VERR_DISK_FULL, RT_SRC_POS, N_("Raw: disk would overflow creating image '%s'"), pImage->pszFilename);
            }
            else
            {
                rc = vdIfIoIntFileSetSize(pImage->pIfIo, pImage->pStorage, cbSize);
                if (RT_SUCCESS(rc))
                    pImage->cbSize = cbSize;
            }
        }
        else
            rc = vdIfError(pImage->pIfError, rc, RT_SRC_POS, N_("Raw: cannot create image '%s'"), pImage->pszFilename);
    }
    else
        rc = vdIfError(pImage->pIfError, VERR_VD_RAW_INVALID_TYPE, RT_SRC_POS, N_("Raw: cannot create diff image '%s'"), pImage->pszFilename);

    if (RT_SUCCESS(rc))
    {
        PVDREGIONDESC pRegion = &pImage->RegionList.aRegions[0];
        pImage->RegionList.fFlags   = 0;
        pImage->RegionList.cRegions = 1;

        pRegion->offRegion            = 0; /* Disk start. */
        pRegion->cbBlock              = pImage->cbSector;
        pRegion->enmDataForm          = VDREGIONDATAFORM_RAW;
        pRegion->enmMetadataForm      = VDREGIONMETADATAFORM_NONE;
        pRegion->cbData               = pImage->cbSector;
        pRegion->cbMetadata           = 0;
        pRegion->cRegionBlocksOrBytes = pImage->cbSize;

        vdIfProgress(pIfProgress, uPercentStart + uPercentSpan);
    }

    if (RT_FAILURE(rc))
        rawFreeImage(pImage, rc != VERR_ALREADY_EXISTS);
    return rc;
}

/**
 * Worker for rawProbe that checks if the file looks like it contains an ISO
 * 9660 or UDF descriptor sequence at the expected offset.
 *
 * Caller already checked if the size is suitable for ISOs.
 *
 * @returns IPRT status code.  Success if detected ISO 9660 or UDF, failure if
 *          not.
 *
 * @note Code is a modified version of rtFsIsoVolTryInit() IPRT (isovfs.cpp).
 */
static int rawProbeIsIso9660OrUdf(PVDINTERFACEIOINT pIfIo, PVDIOSTORAGE pStorage)
{
    PRTERRINFO                  pErrInfo = NULL;
    const uint32_t              cbSector = _2K;

    union
    {
        uint8_t                 ab[_2K];
        ISO9660VOLDESCHDR       VolDescHdr;
    } Buf;
    Assert(cbSector <= sizeof(Buf));
    RT_ZERO(Buf);

    uint8_t         uUdfLevel               = 0;
    uint64_t        offUdfBootVolDesc       = UINT64_MAX;

    uint32_t        cPrimaryVolDescs        = 0;
    uint32_t        cSupplementaryVolDescs  = 0;
    uint32_t        cBootRecordVolDescs     = 0;
    uint32_t        offVolDesc              = 16 * cbSector;
    enum
    {
        kStateStart = 0,
        kStateNoSeq,
        kStateCdSeq,
        kStateUdfSeq
    }               enmState = kStateStart;
    for (uint32_t iVolDesc = 0; ; iVolDesc++, offVolDesc += cbSector)
    {
        if (iVolDesc > 32)
            return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "More than 32 volume descriptors, doesn't seem right...");

        /* Read the next one and check the signature. */
        int rc = vdIfIoIntFileReadSync(pIfIo, pStorage, offVolDesc, &Buf, cbSector);
        if (RT_FAILURE(rc))
            return RTERRINFO_LOG_SET_F(pErrInfo, rc, "Unable to read volume descriptor #%u", iVolDesc);

#define MATCH_STD_ID(a_achStdId1, a_szStdId2) \
            (   (a_achStdId1)[0] == (a_szStdId2)[0] \
             && (a_achStdId1)[1] == (a_szStdId2)[1] \
             && (a_achStdId1)[2] == (a_szStdId2)[2] \
             && (a_achStdId1)[3] == (a_szStdId2)[3] \
             && (a_achStdId1)[4] == (a_szStdId2)[4] )
#define MATCH_HDR(a_pStd, a_bType2, a_szStdId2, a_bVer2) \
            (    MATCH_STD_ID((a_pStd)->achStdId, a_szStdId2) \
             && (a_pStd)->bDescType    == (a_bType2) \
             && (a_pStd)->bDescVersion == (a_bVer2) )

        /*
         * ISO 9660 ("CD001").
         */
        if (   (   enmState == kStateStart
                || enmState == kStateCdSeq
                || enmState == kStateNoSeq)
            && MATCH_STD_ID(Buf.VolDescHdr.achStdId, ISO9660VOLDESC_STD_ID) )
        {
            enmState = kStateCdSeq;

            /* Do type specific handling. */
            Log(("RAW/ISO9660: volume desc #%u: type=%#x\n", iVolDesc, Buf.VolDescHdr.bDescType));
            if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_PRIMARY)
            {
                cPrimaryVolDescs++;
                if (Buf.VolDescHdr.bDescVersion != ISO9660PRIMARYVOLDESC_VERSION)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                               "Unsupported primary volume descriptor version: %#x", Buf.VolDescHdr.bDescVersion);
                if (cPrimaryVolDescs == 1)
                { /*rc = rtFsIsoVolHandlePrimaryVolDesc(pThis, &Buf.PrimaryVolDesc, offVolDesc, &RootDir, &offRootDirRec, pErrInfo);*/ }
                else if (cPrimaryVolDescs == 2)
                    Log(("RAW/ISO9660: ignoring 2nd primary descriptor\n")); /* so we can read the w2k3 ifs kit */
                else
                    return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT, "More than one primary volume descriptor");
            }
            else if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_SUPPLEMENTARY)
            {
                cSupplementaryVolDescs++;
                if (Buf.VolDescHdr.bDescVersion != ISO9660SUPVOLDESC_VERSION)
                    return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                               "Unsupported supplemental volume descriptor version: %#x", Buf.VolDescHdr.bDescVersion);
                /*rc = rtFsIsoVolHandleSupplementaryVolDesc(pThis, &Buf.SupVolDesc, offVolDesc, &bJolietUcs2Level, &JolietRootDir,
                                                          &offJolietRootDirRec, pErrInfo);*/
            }
            else if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_BOOT_RECORD)
            {
                cBootRecordVolDescs++;
            }
            else if (Buf.VolDescHdr.bDescType == ISO9660VOLDESC_TYPE_TERMINATOR)
            {
                if (!cPrimaryVolDescs)
                    return RTERRINFO_LOG_SET(pErrInfo, VERR_VFS_BOGUS_FORMAT, "No primary volume descriptor");
                enmState = kStateNoSeq;
            }
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNSUPPORTED_FORMAT,
                                           "Unknown volume descriptor: %#x", Buf.VolDescHdr.bDescType);
        }
        /*
         * UDF volume recognition sequence (VRS).
         */
        else if (   (   enmState == kStateNoSeq
                     || enmState == kStateStart)
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_BEGIN, UDF_EXT_VOL_DESC_VERSION) )
        {
            if (uUdfLevel == 0)
                enmState = kStateUdfSeq;
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Only one BEA01 sequence is supported");
        }
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_NSR_02, UDF_EXT_VOL_DESC_VERSION) )
            uUdfLevel = 2;
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_NSR_03, UDF_EXT_VOL_DESC_VERSION) )
            uUdfLevel = 3;
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_BOOT, UDF_EXT_VOL_DESC_VERSION) )
        {
            if (offUdfBootVolDesc == UINT64_MAX)
                offUdfBootVolDesc = iVolDesc * cbSector;
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Only one BOOT2 descriptor is supported");
        }
        else if (   enmState == kStateUdfSeq
                 && MATCH_HDR(&Buf.VolDescHdr, UDF_EXT_VOL_DESC_TYPE, UDF_EXT_VOL_DESC_STD_ID_TERM, UDF_EXT_VOL_DESC_VERSION) )
        {
            if (uUdfLevel != 0)
                enmState = kStateNoSeq;
            else
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT, "Found BEA01 & TEA01, but no NSR02 or NSR03 descriptors");
        }
        /*
         * Unknown, probably the end.
         */
        else if (enmState == kStateNoSeq)
            break;
        else if (enmState == kStateStart)
                return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT,
                                           "Not ISO? Unable to recognize volume descriptor signature: %.5Rhxs", Buf.VolDescHdr.achStdId);
        else if (enmState == kStateCdSeq)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "Missing ISO 9660 terminator volume descriptor? (Found %.5Rhxs)", Buf.VolDescHdr.achStdId);
        else if (enmState == kStateUdfSeq)
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_BOGUS_FORMAT,
                                       "Missing UDF terminator volume descriptor? (Found %.5Rhxs)", Buf.VolDescHdr.achStdId);
        else
            return RTERRINFO_LOG_SET_F(pErrInfo, VERR_VFS_UNKNOWN_FORMAT,
                                       "Unknown volume descriptor signature found at sector %u: %.5Rhxs",
                                       16 + iVolDesc, Buf.VolDescHdr.achStdId);
    }

    return VINF_SUCCESS;
}

/**
 * Checks the given extension array for the given suffix and type.
 *
 * @returns true if found in the list, false if not.
 * @param   paExtensions        The extension array to check against.
 * @param   pszSuffix           The suffix to look for.  Can be NULL.
 * @param   enmType             The image type to look for.
 */
static bool rawProbeContainsExtension(const VDFILEEXTENSION *paExtensions, const char *pszSuffix, VDTYPE enmType)
{
    if (pszSuffix)
    {
        if (*pszSuffix == '.')
            pszSuffix++;
        if (*pszSuffix != '\0')
        {
            for (size_t i = 0;; i++)
            {
                if (!paExtensions[i].pszExtension)
                    break;
                if (   paExtensions[i].enmType == enmType
                    && RTStrICmpAscii(paExtensions[i].pszExtension, pszSuffix) == 0)
                    return true;
            }
        }
    }
    return false;
}


/** @copydoc VDIMAGEBACKEND::pfnProbe */
static DECLCALLBACK(int) rawProbe(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                                  PVDINTERFACE pVDIfsImage, VDTYPE enmDesiredType, VDTYPE *penmType)
{
    RT_NOREF(pVDIfsDisk, enmDesiredType);
    LogFlowFunc(("pszFilename=\"%s\" pVDIfsDisk=%#p pVDIfsImage=%#p\n", pszFilename, pVDIfsDisk, pVDIfsImage));
    PVDIOSTORAGE pStorage = NULL;
    PVDINTERFACEIOINT pIfIo = VDIfIoIntGet(pVDIfsImage);

    AssertPtrReturn(pIfIo, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);

    /*
     * Open the file and read the footer.
     */
    int rc = vdIfIoIntFileOpen(pIfIo, pszFilename,
                               VDOpenFlagsToFileOpenFlags(VD_OPEN_FLAGS_READONLY,
                                                          false /* fCreate */),
                               &pStorage);
    if (RT_SUCCESS(rc))
    {
        uint64_t cbFile;
        rc = vdIfIoIntFileGetSize(pIfIo, pStorage, &cbFile);
        if (RT_SUCCESS(rc))
        {
            /*
             * Detecting raw ISO and floppy images and keeping them apart isn't all
             * that simple.
             *
             * - Both must be a multiple of their sector sizes, though
             *   that means that any ISO can also be a floppy, since 2048 is 512 * 4.
             * - The ISO images must be 32KB and floppies are generally not larger
             *   than 2.88MB, but that leaves quite a bit of size overlap,
             *
             * So, the size cannot conclusively say whether something is one or the other.
             *
             * - The content of a normal ISO image is detectable, but not all ISO
             *   images need to follow that spec to work in a DVD ROM drive.
             * - It is common for ISO images to start like a floppy with a boot sector
             *   at the very start of the image.
             * - Floppies doesn't need to contain a DOS-style boot sector, it depends
             *   on the system it is formatted and/or intended for.
             *
             * So, the content cannot conclusively determine the type either.
             *
             * However, there are a number of cases, especially for ISOs, where we can
             * say we a deal of confidence that something is an ISO image.
             */
            const char * const pszSuffix = RTPathSuffix(pszFilename);

            /*
             * Start by checking for sure signs of an ISO 9660 / UDF image.
             */
            rc = VERR_VD_RAW_INVALID_HEADER;
            if (   (enmDesiredType == VDTYPE_INVALID || enmDesiredType == VDTYPE_OPTICAL_DISC)
                && (cbFile % 2048) == 0
                &&  cbFile > 32768)
            {
                int rc2 = rawProbeIsIso9660OrUdf(pIfIo, pStorage);
                if (RT_SUCCESS(rc2))
                {
                    /* *puScore = VDPROBE_SCORE_HIGH; */
                    *penmType = VDTYPE_OPTICAL_DISC;
                    rc = VINF_SUCCESS;
                }
                /* If that didn't work out, check by extension (the old way): */
                else if (rawProbeContainsExtension(s_aRawFileExtensions, pszSuffix, VDTYPE_OPTICAL_DISC))
                {
                    /* *puScore = VDPROBE_SCORE_LOW; */
                    *penmType = VDTYPE_OPTICAL_DISC;
                    rc = VINF_SUCCESS;
                }
            }

            /*
             * We could do something similar for floppies, i.e. check for a
             * DOS'ish boot sector and thereby get a good match on most of the
             * relevant floppy images out there.
             */
            if (   RT_FAILURE(rc)
                && (enmDesiredType == VDTYPE_INVALID || enmDesiredType == VDTYPE_FLOPPY)
                && (cbFile % 512) == 0
                && cbFile >= 512
                && cbFile <= RAW_MAX_FLOPPY_IMG_SIZE)
            {
                /** @todo check if the content is DOSish.  */
                if (false)
                {
                    /* *puScore = VDPROBE_SCORE_HIGH; */
                    *penmType = VDTYPE_FLOPPY;
                    rc = VINF_SUCCESS;
                }
                else if (rawProbeContainsExtension(s_aRawFileExtensions, pszSuffix, VDTYPE_FLOPPY))
                {
                    /* *puScore = VDPROBE_SCORE_LOW; */
                    *penmType = VDTYPE_FLOPPY;
                    rc = VINF_SUCCESS;
                }
            }

            /*
             * No luck?  Go exclusively by extension like we've done since
             * for ever and complain about the size if it doesn't fit expectations.
             * We can get here if the desired type doesn't match the extension and such.
             */
            if (RT_FAILURE(rc))
            {
                if (rawProbeContainsExtension(s_aRawFileExtensions, pszSuffix, VDTYPE_OPTICAL_DISC))
                {
                    if (cbFile % 2048)
                        rc = VERR_VD_RAW_SIZE_MODULO_2048;
                    else if (cbFile <= 32768)
                        rc = VERR_VD_RAW_SIZE_OPTICAL_TOO_SMALL;
                    else
                    {
                        Assert(enmDesiredType != VDTYPE_OPTICAL_DISC);
                        *penmType = VDTYPE_OPTICAL_DISC;
                        rc = VINF_SUCCESS;
                    }
                }
                else if (rawProbeContainsExtension(s_aRawFileExtensions, pszSuffix, VDTYPE_FLOPPY))
                {
                    if (cbFile % 512)
                        rc = VERR_VD_RAW_SIZE_MODULO_512;
                    else if (cbFile > RAW_MAX_FLOPPY_IMG_SIZE)
                        rc = VERR_VD_RAW_SIZE_FLOPPY_TOO_BIG;
                    else
                    {
                        Assert(cbFile == 0 || enmDesiredType != VDTYPE_FLOPPY);
                        *penmType = VDTYPE_FLOPPY;
                        rc = VINF_SUCCESS;
                    }
                }
                else
                    rc = VERR_VD_RAW_INVALID_HEADER;
            }
        }
        else
            rc = VERR_VD_RAW_INVALID_HEADER;
    }

    if (pStorage)
        vdIfIoIntFileClose(pIfIo, pStorage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnOpen */
static DECLCALLBACK(int) rawOpen(const char *pszFilename, unsigned uOpenFlags,
                                 PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                 VDTYPE enmType, void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p enmType=%u ppBackendData=%#p\n",
                 pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, enmType, ppBackendData));
    int rc;
    PRAWIMAGE pImage;

    /* Check open flags. All valid flags are supported. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);

    pImage = (PRAWIMAGE)RTMemAllocZ(RT_UOFFSETOF(RAWIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pStorage = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        if (enmType == VDTYPE_OPTICAL_DISC)
            pImage->cbSector = 2048;
        else
            pImage->cbSector = 512;

        rc = rawOpenImage(pImage, uOpenFlags);
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
static DECLCALLBACK(int) rawCreate(const char *pszFilename, uint64_t cbSize,
                                   unsigned uImageFlags, const char *pszComment,
                                   PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                                   PCRTUUID pUuid, unsigned uOpenFlags,
                                   unsigned uPercentStart, unsigned uPercentSpan,
                                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                                   PVDINTERFACE pVDIfsOperation, VDTYPE enmType,
                                   void **ppBackendData)
{
    RT_NOREF1(pUuid);
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" pPCHSGeometry=%#p pLCHSGeometry=%#p Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p enmType=%u ppBackendData=%#p",
                 pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, enmType, ppBackendData));

    /* Check the VD container type. Yes, hard disk must be allowed, otherwise
     * various tools using this backend for hard disk images will fail. */
    if (enmType != VDTYPE_HDD && enmType != VDTYPE_OPTICAL_DISC && enmType != VDTYPE_FLOPPY)
        return VERR_VD_INVALID_TYPE;

    int rc = VINF_SUCCESS;
    PVDINTERFACEPROGRESS pIfProgress = VDIfProgressGet(pVDIfsOperation);

    /* Check arguments. */
    AssertReturn(!(uOpenFlags & ~VD_OPEN_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertReturn(*pszFilename != '\0', VERR_INVALID_PARAMETER);
    AssertPtrReturn(pPCHSGeometry, VERR_INVALID_POINTER);
    AssertPtrReturn(pLCHSGeometry, VERR_INVALID_POINTER);

    PRAWIMAGE pImage = (PRAWIMAGE)RTMemAllocZ(RT_UOFFSETOF(RAWIMAGE, RegionList.aRegions[1]));
    if (RT_LIKELY(pImage))
    {
        pImage->pszFilename = pszFilename;
        pImage->pStorage = NULL;
        pImage->pVDIfsDisk = pVDIfsDisk;
        pImage->pVDIfsImage = pVDIfsImage;

        rc = rawCreateImage(pImage, cbSize, uImageFlags, pszComment,
                            pPCHSGeometry, pLCHSGeometry, uOpenFlags,
                            pIfProgress, uPercentStart, uPercentSpan);
        if (RT_SUCCESS(rc))
        {
            /* So far the image is opened in read/write mode. Make sure the
             * image is opened in read-only mode if the caller requested that. */
            if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
            {
                rawFreeImage(pImage, false);
                rc = rawOpenImage(pImage, uOpenFlags);
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
static DECLCALLBACK(int) rawRename(void *pBackendData, const char *pszFilename)
{
    LogFlowFunc(("pBackendData=%#p pszFilename=%#p\n", pBackendData, pszFilename));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    AssertReturn((pImage && pszFilename && *pszFilename), VERR_INVALID_PARAMETER);

    /* Close the image. */
    int rc = rawFreeImage(pImage, false);
    if (RT_SUCCESS(rc))
    {
        /* Rename the file. */
        rc = vdIfIoIntFileMove(pImage->pIfIo, pImage->pszFilename, pszFilename, 0);
        if (RT_SUCCESS(rc))
        {
            /* Update pImage with the new information. */
            pImage->pszFilename = pszFilename;

            /* Open the old image with new name. */
            rc = rawOpenImage(pImage, pImage->uOpenFlags);
        }
        else
        {
            /* The move failed, try to reopen the original image. */
            int rc2 = rawOpenImage(pImage, pImage->uOpenFlags);
            if (RT_FAILURE(rc2))
                rc = rc2;
        }
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnClose */
static DECLCALLBACK(int) rawClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;
    int rc = rawFreeImage(pImage, fDelete);
    RTMemFree(pImage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnRead */
static DECLCALLBACK(int) rawRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                                 PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    int rc = VINF_SUCCESS;
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    /* For sequential access do not allow to go back. */
    if (   pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL
        && uOffset < pImage->offAccess)
    {
        *pcbActuallyRead = 0;
        return VERR_INVALID_PARAMETER;
    }

    rc = vdIfIoIntFileReadUser(pImage->pIfIo, pImage->pStorage, uOffset,
                               pIoCtx, cbToRead);
    if (RT_SUCCESS(rc))
    {
        *pcbActuallyRead = cbToRead;
        pImage->offAccess = uOffset + cbToRead;
    }

    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnWrite */
static DECLCALLBACK(int) rawWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                                  PVDIOCTX pIoCtx, size_t *pcbWriteProcess, size_t *pcbPreRead,
                                  size_t *pcbPostRead, unsigned fWrite)
{
    RT_NOREF1(fWrite);
    int rc = VINF_SUCCESS;
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    /* For sequential access do not allow to go back. */
    if (   pImage->uOpenFlags & VD_OPEN_FLAGS_SEQUENTIAL
        && uOffset < pImage->offAccess)
    {
        *pcbWriteProcess = 0;
        *pcbPostRead = 0;
        *pcbPreRead  = 0;
        return VERR_INVALID_PARAMETER;
    }

    rc = vdIfIoIntFileWriteUser(pImage->pIfIo, pImage->pStorage, uOffset,
                                pIoCtx, cbToWrite, NULL, NULL);
    if (RT_SUCCESS(rc))
    {
        *pcbWriteProcess = cbToWrite;
        *pcbPostRead = 0;
        *pcbPreRead  = 0;
        pImage->offAccess = uOffset + cbToWrite;
    }

    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnFlush */
static DECLCALLBACK(int) rawFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        rc = vdIfIoIntFileFlush(pImage->pIfIo, pImage->pStorage, pIoCtx,
                                NULL, NULL);

    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetVersion */
static DECLCALLBACK(unsigned) rawGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    return 1;
}

/** @copydoc VDIMAGEBACKEND::pfnGetFileSize */
static DECLCALLBACK(uint64_t) rawGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    uint64_t cbFile = 0;
    if (pImage->pStorage)
    {
        int rc = vdIfIoIntFileGetSize(pImage->pIfIo, pImage->pStorage, &cbFile);
        if (RT_FAILURE(rc))
            cbFile = 0; /* Make sure it is 0 */
    }

    LogFlowFunc(("returns %lld\n", cbFile));
    return cbFile;
}

/** @copydoc VDIMAGEBACKEND::pfnGetPCHSGeometry */
static DECLCALLBACK(int) rawGetPCHSGeometry(void *pBackendData,
                                            PVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;
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
static DECLCALLBACK(int) rawSetPCHSGeometry(void *pBackendData,
                                            PCVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n",
                 pBackendData, pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;
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
static DECLCALLBACK(int) rawGetLCHSGeometry(void *pBackendData,
                                            PVDGEOMETRY pLCHSGeometry)
{
     LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;
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
static DECLCALLBACK(int) rawSetLCHSGeometry(void *pBackendData,
                                            PCVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n",
                 pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;
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
static DECLCALLBACK(int) rawQueryRegions(void *pBackendData, PCVDREGIONLIST *ppRegionList)
{
    LogFlowFunc(("pBackendData=%#p ppRegionList=%#p\n", pBackendData, ppRegionList));
    PRAWIMAGE pThis = (PRAWIMAGE)pBackendData;

    AssertPtrReturn(pThis, VERR_VD_NOT_OPENED);

    *ppRegionList = &pThis->RegionList;
    LogFlowFunc(("returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
}

/** @copydoc VDIMAGEBACKEND::pfnRegionListRelease */
static DECLCALLBACK(void) rawRegionListRelease(void *pBackendData, PCVDREGIONLIST pRegionList)
{
    RT_NOREF1(pRegionList);
    LogFlowFunc(("pBackendData=%#p pRegionList=%#p\n", pBackendData, pRegionList));
    PRAWIMAGE pThis = (PRAWIMAGE)pBackendData;
    AssertPtr(pThis); RT_NOREF(pThis);

    /* Nothing to do here. */
}

/** @copydoc VDIMAGEBACKEND::pfnGetImageFlags */
static DECLCALLBACK(unsigned) rawGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uImageFlags));
    return pImage->uImageFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnGetOpenFlags */
static DECLCALLBACK(unsigned) rawGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    AssertPtrReturn(pImage, 0);

    LogFlowFunc(("returns %#x\n", pImage->uOpenFlags));
    return pImage->uOpenFlags;
}

/** @copydoc VDIMAGEBACKEND::pfnSetOpenFlags */
static DECLCALLBACK(int) rawSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p\n uOpenFlags=%#x", pBackendData, uOpenFlags));
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;
    int rc = VINF_SUCCESS;

    /* Image must be opened and the new flags must be valid. */
    if (!pImage || (uOpenFlags & ~(  VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO
                                   | VD_OPEN_FLAGS_ASYNC_IO | VD_OPEN_FLAGS_SHAREABLE
                                   | VD_OPEN_FLAGS_SEQUENTIAL | VD_OPEN_FLAGS_SKIP_CONSISTENCY_CHECKS)))
        rc = VERR_INVALID_PARAMETER;
    else
    {
        /* Implement this operation via reopening the image. */
        rc = rawFreeImage(pImage, false);
        if (RT_SUCCESS(rc))
            rc = rawOpenImage(pImage, uOpenFlags);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VDIMAGEBACKEND::pfnGetComment */
VD_BACKEND_CALLBACK_GET_COMMENT_DEF_NOT_SUPPORTED(rawGetComment);

/** @copydoc VDIMAGEBACKEND::pfnSetComment */
VD_BACKEND_CALLBACK_SET_COMMENT_DEF_NOT_SUPPORTED(rawSetComment, PRAWIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(rawGetUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(rawSetUuid, PRAWIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(rawGetModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(rawSetModificationUuid, PRAWIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(rawGetParentUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(rawSetParentUuid, PRAWIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnGetParentModificationUuid */
VD_BACKEND_CALLBACK_GET_UUID_DEF_NOT_SUPPORTED(rawGetParentModificationUuid);

/** @copydoc VDIMAGEBACKEND::pfnSetParentModificationUuid */
VD_BACKEND_CALLBACK_SET_UUID_DEF_NOT_SUPPORTED(rawSetParentModificationUuid, PRAWIMAGE);

/** @copydoc VDIMAGEBACKEND::pfnDump */
static DECLCALLBACK(void) rawDump(void *pBackendData)
{
    PRAWIMAGE pImage = (PRAWIMAGE)pBackendData;

    AssertPtrReturnVoid(pImage);
    vdIfErrorMessage(pImage->pIfError, "Header: Geometry PCHS=%u/%u/%u LCHS=%u/%u/%u cbSector=%llu\n",
                     pImage->PCHSGeometry.cCylinders, pImage->PCHSGeometry.cHeads, pImage->PCHSGeometry.cSectors,
                     pImage->LCHSGeometry.cCylinders, pImage->LCHSGeometry.cHeads, pImage->LCHSGeometry.cSectors,
                     pImage->cbSize / 512);
}



const VDIMAGEBACKEND g_RawBackend =
{
    /* u32Version */
    VD_IMGBACKEND_VERSION,
    /* pszBackendName */
    "RAW",
    /* uBackendCaps */
    VD_CAP_CREATE_FIXED | VD_CAP_FILE | VD_CAP_ASYNC | VD_CAP_VFS,
    /* paFileExtensions */
    s_aRawFileExtensions,
    /* paConfigInfo */
    NULL,
    /* pfnProbe */
    rawProbe,
    /* pfnOpen */
    rawOpen,
    /* pfnCreate */
    rawCreate,
    /* pfnRename */
    rawRename,
    /* pfnClose */
    rawClose,
    /* pfnRead */
    rawRead,
    /* pfnWrite */
    rawWrite,
    /* pfnFlush */
    rawFlush,
    /* pfnDiscard */
    NULL,
    /* pfnGetVersion */
    rawGetVersion,
    /* pfnGetFileSize */
    rawGetFileSize,
    /* pfnGetPCHSGeometry */
    rawGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    rawSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    rawGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    rawSetLCHSGeometry,
    /* pfnQueryRegions */
    rawQueryRegions,
    /* pfnRegionListRelease */
    rawRegionListRelease,
    /* pfnGetImageFlags */
    rawGetImageFlags,
    /* pfnGetOpenFlags */
    rawGetOpenFlags,
    /* pfnSetOpenFlags */
    rawSetOpenFlags,
    /* pfnGetComment */
    rawGetComment,
    /* pfnSetComment */
    rawSetComment,
    /* pfnGetUuid */
    rawGetUuid,
    /* pfnSetUuid */
    rawSetUuid,
    /* pfnGetModificationUuid */
    rawGetModificationUuid,
    /* pfnSetModificationUuid */
    rawSetModificationUuid,
    /* pfnGetParentUuid */
    rawGetParentUuid,
    /* pfnSetParentUuid */
    rawSetParentUuid,
    /* pfnGetParentModificationUuid */
    rawGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    rawSetParentModificationUuid,
    /* pfnDump */
    rawDump,
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
