/* $Id: vbox-img.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Standalone image manipulation tool
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <VBox/vd.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <iprt/initterm.h>
#include <iprt/asm-mem.h>
#include <iprt/buildconfig.h>
#include <iprt/fsvfs.h>
#include <iprt/fsisomaker.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/stream.h>
#include <iprt/message.h>
#include <iprt/getopt.h>
#include <iprt/assert.h>
#include <iprt/dvm.h>
#include <iprt/vfs.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char *g_pszProgName = "";



static void printUsage(PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm,
                 "Usage: %s\n"
                 "   setuuid      --filename <filename>\n"
                 "                [--format VDI|VMDK|VHD|...]\n"
                 "                [--uuid <uuid>]\n"
                 "                [--parentuuid <uuid>]\n"
                 "                [--zeroparentuuid]\n"
                 "\n"
                 "   geometry     --filename <filename>\n"
                 "                [--format VDI|VMDK|VHD|...]\n"
                 "                [--clearchs]\n"
                 "                [--cylinders <number>]\n"
                 "                [--heads <number>]\n"
                 "                [--sectors <number>]\n"
                 "\n"
                 "   convert      --srcfilename <filename>\n"
                 "                --dstfilename <filename>\n"
                 "                [--stdin]|[--stdout]\n"
                 "                [--srcformat VDI|VMDK|VHD|RAW|..]\n"
                 "                [--dstformat VDI|VMDK|VHD|RAW|..]\n"
                 "                [--variant Standard,Fixed,Split2G,Stream,ESX]\n"
                 "\n"
                 "   info         --filename <filename>\n"
                 "\n"
                 "   compact      --filename <filename>\n"
                 "                [--filesystemaware]\n"
                 "\n"
                 "   createcache  --filename <filename>\n"
                 "                --size <cache size>\n"
                 "\n"
                 "   createbase   --filename <filename>\n"
                 "                --size <size in bytes>\n"
                 "                [--format VDI|VMDK|VHD] (default: VDI)\n"
                 "                [--variant Standard,Fixed,Split2G,Stream,ESX]\n"
                 "                [--dataalignment <alignment in bytes>]\n"
                 "\n"
                 "   createfloppy --filename <filename>\n"
                 "                [--size <size in bytes>]\n"
                 "                [--root-dir-entries <value>]\n"
                 "                [--sector-size <bytes>]\n"
                 "                [--heads <value>]\n"
                 "                [--sectors-per-track <count>]\n"
                 "                [--media-byte <byte>]\n"
                 "\n"
                 "   createiso    [too-many-options]\n"
                 "\n"
                 "   repair       --filename <filename>\n"
                 "                [--dry-run]\n"
                 "                [--format VDI|VMDK|VHD] (default: autodetect)\n"
                 "\n"
                 "   clearcomment --filename <filename>\n"
                 "\n"
                 "   resize       --filename <filename>\n"
                 "                --size <new size>\n",
                 g_pszProgName);
}

static void showLogo(PRTSTREAM pStrm)
{
    static bool s_fShown; /* show only once */

    if (!s_fShown)
    {
        RTStrmPrintf(pStrm, VBOX_PRODUCT " Disk Utility " VBOX_VERSION_STRING "\n"
                     "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n");
        s_fShown = true;
    }
}

/** command handler argument */
struct HandlerArg
{
    int argc;
    char **argv;
};

static PVDINTERFACE pVDIfs;

static DECLCALLBACK(void) handleVDError(void *pvUser, int rc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    RT_NOREF2(pvUser, rc);
    RT_SRC_POS_NOREF();
    RTMsgErrorV(pszFormat, va);
}

static DECLCALLBACK(int) handleVDMessage(void *pvUser, const char *pszFormat, va_list va)
{
    NOREF(pvUser);
    RTPrintfV(pszFormat, va);
    return VINF_SUCCESS;
}

/**
 * Print a usage synopsis and the syntax error message.
 */
static int errorSyntax(const char *pszFormat, ...)
{
    va_list args;
    showLogo(g_pStdErr); // show logo even if suppressed
    va_start(args, pszFormat);
    RTStrmPrintf(g_pStdErr, "\nSyntax error: %N\n", pszFormat, &args);
    va_end(args);
    printUsage(g_pStdErr);
    return 1;
}

static int errorRuntime(const char *pszFormat, ...)
{
    va_list args;

    va_start(args, pszFormat);
    RTMsgErrorV(pszFormat, args);
    va_end(args);
    return 1;
}

static int parseDiskVariant(const char *psz, unsigned *puImageFlags)
{
    int rc = VINF_SUCCESS;
    unsigned uImageFlags = *puImageFlags;

    while (psz && *psz && RT_SUCCESS(rc))
    {
        size_t len;
        const char *pszComma = strchr(psz, ',');
        if (pszComma)
            len = pszComma - psz;
        else
            len = strlen(psz);
        if (len > 0)
        {
            /*
             * Parsing is intentionally inconsistent: "standard" resets the
             * variant, whereas the other flags are cumulative.
             */
            if (!RTStrNICmp(psz, "standard", len))
                uImageFlags = VD_IMAGE_FLAGS_NONE;
            else if (   !RTStrNICmp(psz, "fixed", len)
                     || !RTStrNICmp(psz, "static", len))
                uImageFlags |= VD_IMAGE_FLAGS_FIXED;
            else if (!RTStrNICmp(psz, "Diff", len))
                uImageFlags |= VD_IMAGE_FLAGS_DIFF;
            else if (!RTStrNICmp(psz, "split2g", len))
                uImageFlags |= VD_VMDK_IMAGE_FLAGS_SPLIT_2G;
            else if (   !RTStrNICmp(psz, "stream", len)
                     || !RTStrNICmp(psz, "streamoptimized", len))
                uImageFlags |= VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED;
            else if (!RTStrNICmp(psz, "esx", len))
                uImageFlags |= VD_VMDK_IMAGE_FLAGS_ESX;
            else
                rc = VERR_PARSE_ERROR;
        }
        if (pszComma)
            psz += len + 1;
        else
            psz += len;
    }

    if (RT_SUCCESS(rc))
        *puImageFlags = uImageFlags;
    return rc;
}


static int handleSetUUID(HandlerArg *a)
{
    const char *pszFilename = NULL;
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    RTUUID imageUuid;
    RTUUID parentUuid;
    bool fSetImageUuid = false;
    bool fSetParentUuid = false;
    RTUuidClear(&imageUuid);
    RTUuidClear(&parentUuid);
    int rc;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename", 'f', RTGETOPT_REQ_STRING },
        { "--format", 'o', RTGETOPT_REQ_STRING },
        { "--uuid", 'u', RTGETOPT_REQ_UUID },
        { "--parentuuid", 'p', RTGETOPT_REQ_UUID },
        { "--zeroparentuuid", 'P', RTGETOPT_REQ_NOTHING }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;
            case 'o':   // --format
                pszFormat = RTStrDup(ValueUnion.psz);
                break;
            case 'u':   // --uuid
                imageUuid = ValueUnion.Uuid;
                fSetImageUuid = true;
                break;
            case 'p':   // --parentuuid
                parentUuid = ValueUnion.Uuid;
                fSetParentUuid = true;
                break;
            case 'P':   // --zeroparentuuid
                RTUuidClear(&parentUuid);
                fSetParentUuid = true;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    /* Check for consistency of optional parameters. */
    if (fSetImageUuid && RTUuidIsNull(&imageUuid))
        return errorSyntax("Invalid parameter to --uuid option\n");

    /* Autodetect image format. */
    if (!pszFormat)
    {
        /* Don't pass error interface, as that would triggers error messages
         * because some backends fail to open the image. */
        rc = VDGetFormat(NULL, NULL, pszFilename, VDTYPE_INVALID, &pszFormat, &enmType);
        if (RT_FAILURE(rc))
            return errorRuntime("Format autodetect failed: %Rrc\n", rc);
    }

    PVDISK pVD = NULL;
    rc = VDCreate(pVDIfs, enmType, &pVD);
    if (RT_FAILURE(rc))
        return errorRuntime("Cannot create the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    /* Open in info mode to be able to open diff images without their parent. */
    rc = VDOpen(pVD, pszFormat, pszFilename, VD_OPEN_FLAGS_INFO, NULL);
    if (RT_FAILURE(rc))
        return errorRuntime("Cannot open the virtual disk image \"%s\": %Rrf (%Rrc)\n",
                            pszFilename, rc, rc);

    RTUUID oldImageUuid;
    rc = VDGetUuid(pVD, VD_LAST_IMAGE, &oldImageUuid);
    if (RT_FAILURE(rc))
        return errorRuntime("Cannot get UUID of virtual disk image \"%s\": %Rrc\n",
                            pszFilename, rc);

    RTPrintf("Old image UUID:  %RTuuid\n", &oldImageUuid);

    RTUUID oldParentUuid;
    rc = VDGetParentUuid(pVD, VD_LAST_IMAGE, &oldParentUuid);
    if (RT_FAILURE(rc))
        return errorRuntime("Cannot get parent UUID of virtual disk image \"%s\": %Rrc\n",
                            pszFilename, rc);

    RTPrintf("Old parent UUID: %RTuuid\n", &oldParentUuid);

    if (fSetImageUuid)
    {
        RTPrintf("New image UUID:  %RTuuid\n", &imageUuid);
        rc = VDSetUuid(pVD, VD_LAST_IMAGE, &imageUuid);
        if (RT_FAILURE(rc))
            return errorRuntime("Cannot set UUID of virtual disk image \"%s\": %Rrf (%Rrc)\n",
                                pszFilename, rc, rc);
    }

    if (fSetParentUuid)
    {
        RTPrintf("New parent UUID: %RTuuid\n", &parentUuid);
        rc = VDSetParentUuid(pVD, VD_LAST_IMAGE, &parentUuid);
        if (RT_FAILURE(rc))
            return errorRuntime("Cannot set parent UUID of virtual disk image \"%s\": %Rrf (%Rrc)\n",
                                pszFilename, rc, rc);
    }

    VDDestroy(pVD);

    if (pszFormat)
    {
        RTStrFree(pszFormat);
        pszFormat = NULL;
    }

    return 0;
}


static int handleGeometry(HandlerArg *a)
{
    const char *pszFilename = NULL;
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    uint16_t cCylinders = 0;
    uint8_t cHeads = 0;
    uint8_t cSectors = 0;
    bool fCylinders = false;
    bool fHeads = false;
    bool fSectors = false;
    int rc;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename", 'f', RTGETOPT_REQ_STRING },
        { "--format", 'o', RTGETOPT_REQ_STRING },
        { "--clearchs", 'C', RTGETOPT_REQ_NOTHING },
        { "--cylinders", 'c', RTGETOPT_REQ_UINT16 },
        { "--heads", 'e', RTGETOPT_REQ_UINT8 },
        { "--sectors", 's', RTGETOPT_REQ_UINT8 }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;
            case 'o':   // --format
                pszFormat = RTStrDup(ValueUnion.psz);
                break;
            case 'C':   // --clearchs
                cCylinders = 0;
                cHeads = 0;
                cSectors = 0;
                fCylinders = true;
                fHeads = true;
                fSectors = true;
                break;
            case 'c':   // --cylinders
                cCylinders = ValueUnion.u16;
                fCylinders = true;
                break;
            case 'e':   // --heads
                cHeads = ValueUnion.u8;
                fHeads = true;
                break;
            case 's':   // --sectors
                cSectors = ValueUnion.u8;
                fSectors = true;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    /* Autodetect image format. */
    if (!pszFormat)
    {
        /* Don't pass error interface, as that would triggers error messages
         * because some backends fail to open the image. */
        rc = VDGetFormat(NULL, NULL, pszFilename, VDTYPE_INVALID, &pszFormat, &enmType);
        if (RT_FAILURE(rc))
            return errorRuntime("Format autodetect failed: %Rrc\n", rc);
    }

    PVDISK pVD = NULL;
    rc = VDCreate(pVDIfs, enmType, &pVD);
    if (RT_FAILURE(rc))
        return errorRuntime("Cannot create the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    /* Open in info mode to be able to open diff images without their parent. */
    rc = VDOpen(pVD, pszFormat, pszFilename, VD_OPEN_FLAGS_INFO, NULL);
    if (RT_FAILURE(rc))
        return errorRuntime("Cannot open the virtual disk image \"%s\": %Rrf (%Rrc)\n",
                            pszFilename, rc, rc);

    VDGEOMETRY oldLCHSGeometry;
    rc = VDGetLCHSGeometry(pVD, VD_LAST_IMAGE, &oldLCHSGeometry);
    if (rc == VERR_VD_GEOMETRY_NOT_SET)
    {
        memset(&oldLCHSGeometry, 0, sizeof(oldLCHSGeometry));
        rc = VINF_SUCCESS;
    }
    if (RT_FAILURE(rc))
        return errorRuntime("Cannot get LCHS geometry of virtual disk image \"%s\": %Rrc\n",
                            pszFilename, rc);

    VDGEOMETRY newLCHSGeometry = oldLCHSGeometry;
    if (fCylinders)
        newLCHSGeometry.cCylinders = cCylinders;
    if (fHeads)
        newLCHSGeometry.cHeads = cHeads;
    if (fSectors)
        newLCHSGeometry.cSectors = cSectors;

    if (fCylinders || fHeads || fSectors)
    {
        RTPrintf("Old image LCHS: %u/%u/%u\n", oldLCHSGeometry.cCylinders, oldLCHSGeometry.cHeads, oldLCHSGeometry.cSectors);
        RTPrintf("New image LCHS: %u/%u/%u\n", newLCHSGeometry.cCylinders, newLCHSGeometry.cHeads, newLCHSGeometry.cSectors);

        rc = VDSetLCHSGeometry(pVD, VD_LAST_IMAGE, &newLCHSGeometry);
        if (RT_FAILURE(rc))
            return errorRuntime("Cannot set LCHS geometry of virtual disk image \"%s\": %Rrf (%Rrc)\n",
                                pszFilename, rc, rc);
    }
    else
        RTPrintf("Current image LCHS: %u/%u/%u\n", oldLCHSGeometry.cCylinders, oldLCHSGeometry.cHeads, oldLCHSGeometry.cSectors);


    VDDestroy(pVD);

    if (pszFormat)
    {
        RTStrFree(pszFormat);
        pszFormat = NULL;
    }

    return 0;
}


typedef struct FILEIOSTATE
{
    RTFILE file;
    /** Size of file. */
    uint64_t cb;
    /** Offset in the file. */
    uint64_t off;
    /** Offset where the buffer contents start. UINT64_MAX=buffer invalid. */
    uint64_t offBuffer;
    /** Size of valid data in the buffer. */
    uint32_t cbBuffer;
    /** Buffer for efficient I/O */
    uint8_t abBuffer[16 *_1M];
} FILEIOSTATE, *PFILEIOSTATE;

static DECLCALLBACK(int) convInOpen(void *pvUser, const char *pszLocation, uint32_t fOpen, PFNVDCOMPLETED pfnCompleted,
                                    void **ppStorage)
{
    RT_NOREF2(pvUser, pszLocation);

    /* Validate input. */
    AssertPtrReturn(ppStorage, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnCompleted, VERR_INVALID_PARAMETER);
    AssertReturn((fOpen & RTFILE_O_ACCESS_MASK) == RTFILE_O_READ, VERR_INVALID_PARAMETER);
    RTFILE file;
    int rc = RTFileFromNative(&file, RTFILE_NATIVE_STDIN);
    if (RT_FAILURE(rc))
        return rc;

    /* No need to clear the buffer, the data will be read from disk. */
    PFILEIOSTATE pFS = (PFILEIOSTATE)RTMemAlloc(sizeof(FILEIOSTATE));
    if (!pFS)
        return VERR_NO_MEMORY;

    pFS->file = file;
    pFS->cb   = 0;
    pFS->off = 0;
    pFS->offBuffer = UINT64_MAX;
    pFS->cbBuffer = 0;

    *ppStorage = pFS;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convInClose(void *pvUser, void *pStorage)
{
    NOREF(pvUser);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    PFILEIOSTATE pFS = (PFILEIOSTATE)pStorage;

    RTMemFree(pFS);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convInDelete(void *pvUser, const char *pcszFilename)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convInMove(void *pvUser, const char *pcszSrc, const char *pcszDst, unsigned fMove)
{
    NOREF(pvUser);
    NOREF(pcszSrc);
    NOREF(pcszDst);
    NOREF(fMove);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convInGetFreeSpace(void *pvUser, const char *pcszFilename, int64_t *pcbFreeSpace)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pcbFreeSpace, VERR_INVALID_POINTER);
    *pcbFreeSpace = 0;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convInGetModificationTime(void *pvUser, const char *pcszFilename, PRTTIMESPEC pModificationTime)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convInGetSize(void *pvUser, void *pStorage, uint64_t *pcbSize)
{
    NOREF(pvUser);
    NOREF(pStorage);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convInSetSize(void *pvUser, void *pStorage, uint64_t cbSize)
{
    NOREF(pvUser);
    NOREF(pStorage);
    NOREF(cbSize);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convInRead(void *pvUser, void *pStorage, uint64_t uOffset,
                                    void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    NOREF(pvUser);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuffer, VERR_INVALID_POINTER);
    PFILEIOSTATE pFS = (PFILEIOSTATE)pStorage;
    AssertReturn(uOffset >= pFS->off, VERR_INVALID_PARAMETER);
    int rc;

    /* Fill buffer if it is empty. */
    if (pFS->offBuffer == UINT64_MAX)
    {
        /* Repeat reading until buffer is full or EOF. */
        size_t cbRead;
        size_t cbSumRead = 0;
        uint8_t *pbTmp = (uint8_t *)&pFS->abBuffer[0];
        size_t cbTmp = sizeof(pFS->abBuffer);
        do
        {
            rc = RTFileRead(pFS->file, pbTmp, cbTmp, &cbRead);
            if (RT_FAILURE(rc))
                return rc;
            pbTmp += cbRead;
            cbTmp -= cbRead;
            cbSumRead += cbRead;
        } while (cbTmp && cbRead);

        pFS->offBuffer = 0;
        pFS->cbBuffer = (uint32_t)cbSumRead;
        if (!cbSumRead && !pcbRead) /* Caller can't handle partial reads. */
            return VERR_EOF;
    }

    /* Read several blocks and assemble the result if necessary */
    size_t cbTotalRead = 0;
    do
    {
        /* Skip over areas no one wants to read. */
        while (uOffset > pFS->offBuffer + pFS->cbBuffer - 1)
        {
            if (pFS->cbBuffer < sizeof(pFS->abBuffer))
            {
                if (pcbRead)
                    *pcbRead = cbTotalRead;
                return VERR_EOF;
            }

            /* Repeat reading until buffer is full or EOF. */
            size_t cbRead;
            size_t cbSumRead = 0;
            uint8_t *pbTmp = (uint8_t *)&pFS->abBuffer[0];
            size_t cbTmp = sizeof(pFS->abBuffer);
            do
            {
                rc = RTFileRead(pFS->file, pbTmp, cbTmp, &cbRead);
                if (RT_FAILURE(rc))
                    return rc;
                pbTmp += cbRead;
                cbTmp -= cbRead;
                cbSumRead += cbRead;
            } while (cbTmp && cbRead);

            pFS->offBuffer += pFS->cbBuffer;
            pFS->cbBuffer = (uint32_t)cbSumRead;
        }

        uint32_t cbThisRead = (uint32_t)RT_MIN(cbBuffer,
                                               pFS->cbBuffer - uOffset % sizeof(pFS->abBuffer));
        memcpy(pvBuffer, &pFS->abBuffer[uOffset % sizeof(pFS->abBuffer)],
               cbThisRead);
        uOffset += cbThisRead;
        pvBuffer = (uint8_t *)pvBuffer + cbThisRead;
        cbBuffer -= cbThisRead;
        cbTotalRead += cbThisRead;
        if (!cbTotalRead && !pcbRead) /* Caller can't handle partial reads. */
            return VERR_EOF;
    } while (cbBuffer > 0);

    if (pcbRead)
        *pcbRead = cbTotalRead;

    pFS->off = uOffset;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convInWrite(void *pvUser, void *pStorage, uint64_t uOffset, const void *pvBuffer, size_t cbBuffer,
                                     size_t *pcbWritten)
{
    NOREF(pvUser);
    NOREF(pStorage);
    NOREF(uOffset);
    NOREF(cbBuffer);
    NOREF(pcbWritten);
    AssertPtrReturn(pvBuffer, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convInFlush(void *pvUser, void *pStorage)
{
    NOREF(pvUser);
    NOREF(pStorage);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convStdOutOpen(void *pvUser, const char *pszLocation, uint32_t fOpen, PFNVDCOMPLETED pfnCompleted,
                                        void **ppStorage)
{
    RT_NOREF2(pvUser, pszLocation);

    /* Validate input. */
    AssertPtrReturn(ppStorage, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnCompleted, VERR_INVALID_PARAMETER);
    AssertReturn((fOpen & RTFILE_O_ACCESS_MASK) == RTFILE_O_WRITE, VERR_INVALID_PARAMETER);
    RTFILE file;
    int rc = RTFileFromNative(&file, RTFILE_NATIVE_STDOUT);
    if (RT_FAILURE(rc))
        return rc;

    /* Must clear buffer, so that skipped over data is initialized properly. */
    PFILEIOSTATE pFS = (PFILEIOSTATE)RTMemAllocZ(sizeof(FILEIOSTATE));
    if (!pFS)
        return VERR_NO_MEMORY;

    pFS->file = file;
    pFS->cb   = 0;
    pFS->off = 0;
    pFS->offBuffer = 0;
    pFS->cbBuffer = sizeof(FILEIOSTATE);

    *ppStorage = pFS;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convStdOutClose(void *pvUser, void *pStorage)
{
    NOREF(pvUser);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    PFILEIOSTATE pFS = (PFILEIOSTATE)pStorage;
    int rc = VINF_SUCCESS;

    /* Flush any remaining buffer contents. */
    if (pFS->cbBuffer)
        rc = RTFileWrite(pFS->file, &pFS->abBuffer[0], pFS->cbBuffer, NULL);
    if (   RT_SUCCESS(rc)
        && pFS->cb > pFS->off)
    {
        /* Write zeros if the set file size is not met. */
        uint64_t cbLeft = pFS->cb - pFS->off;
        RT_ZERO(pFS->abBuffer);

        while (cbLeft)
        {
            size_t cbThisWrite = RT_MIN(cbLeft, sizeof(pFS->abBuffer));
            rc = RTFileWrite(pFS->file, &pFS->abBuffer[0],
                             cbThisWrite, NULL);
            cbLeft -= cbThisWrite;
        }
    }

    RTMemFree(pFS);

    return rc;
}

static DECLCALLBACK(int) convStdOutDelete(void *pvUser, const char *pcszFilename)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convStdOutMove(void *pvUser, const char *pcszSrc, const char *pcszDst, unsigned fMove)
{
    NOREF(pvUser);
    NOREF(pcszSrc);
    NOREF(pcszDst);
    NOREF(fMove);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convStdOutGetFreeSpace(void *pvUser, const char *pcszFilename, int64_t *pcbFreeSpace)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pcbFreeSpace, VERR_INVALID_POINTER);
    *pcbFreeSpace = INT64_MAX;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convStdOutGetModificationTime(void *pvUser, const char *pcszFilename, PRTTIMESPEC pModificationTime)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convStdOutGetSize(void *pvUser, void *pStorage, uint64_t *pcbSize)
{
    NOREF(pvUser);
    NOREF(pStorage);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convStdOutSetSize(void *pvUser, void *pStorage, uint64_t cbSize)
{
    RT_NOREF2(pvUser, cbSize);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convStdOutRead(void *pvUser, void *pStorage, uint64_t uOffset, void *pvBuffer, size_t cbBuffer,
                                        size_t *pcbRead)
{
    NOREF(pvUser);
    NOREF(pStorage);
    NOREF(uOffset);
    NOREF(cbBuffer);
    NOREF(pcbRead);
    AssertPtrReturn(pvBuffer, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convStdOutWrite(void *pvUser, void *pStorage, uint64_t uOffset, const void *pvBuffer, size_t cbBuffer,
                                         size_t *pcbWritten)
{
    NOREF(pvUser);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuffer, VERR_INVALID_POINTER);
    PFILEIOSTATE pFS = (PFILEIOSTATE)pStorage;
    AssertReturn(uOffset >= pFS->off, VERR_INVALID_PARAMETER);

    /* Write the data to the buffer, flushing as required. */
    size_t cbTotalWritten = 0;
    do
    {
        /* Flush the buffer if we need a new one. */
        while (uOffset > pFS->offBuffer + sizeof(pFS->abBuffer) - 1)
        {
            int rc = RTFileWrite(pFS->file, &pFS->abBuffer[0],
                                 sizeof(pFS->abBuffer), NULL);
            if (RT_FAILURE(rc))
                return rc;

            RT_ZERO(pFS->abBuffer);
            pFS->offBuffer += sizeof(pFS->abBuffer);
            pFS->cbBuffer = 0;
        }

        uint32_t cbThisWrite = (uint32_t)RT_MIN(cbBuffer,
                                                sizeof(pFS->abBuffer) - uOffset % sizeof(pFS->abBuffer));
        memcpy(&pFS->abBuffer[uOffset % sizeof(pFS->abBuffer)], pvBuffer,
               cbThisWrite);
        uOffset += cbThisWrite;
        pvBuffer = (uint8_t *)pvBuffer + cbThisWrite;
        cbBuffer -= cbThisWrite;
        cbTotalWritten += cbThisWrite;
    } while (cbBuffer > 0);

    if (pcbWritten)
        *pcbWritten = cbTotalWritten;

    pFS->cbBuffer = uOffset % sizeof(pFS->abBuffer);
    if (!pFS->cbBuffer)
        pFS->cbBuffer = sizeof(pFS->abBuffer);
    pFS->off = uOffset;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convStdOutFlush(void *pvUser, void *pStorage)
{
    NOREF(pvUser);
    NOREF(pStorage);
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convFileOutOpen(void *pvUser, const char *pszLocation, uint32_t fOpen, PFNVDCOMPLETED pfnCompleted,
                                         void **ppStorage)
{
    RT_NOREF1(pvUser);

    /* Validate input. */
    AssertPtrReturn(ppStorage, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnCompleted, VERR_INVALID_PARAMETER);
    AssertReturn((fOpen & RTFILE_O_ACCESS_MASK) == RTFILE_O_WRITE, VERR_INVALID_PARAMETER);
    RTFILE file;
    int rc = RTFileOpen(&file, pszLocation, fOpen);
    if (RT_FAILURE(rc))
        return rc;

    /* Must clear buffer, so that skipped over data is initialized properly. */
    PFILEIOSTATE pFS = (PFILEIOSTATE)RTMemAllocZ(sizeof(FILEIOSTATE));
    if (!pFS)
        return VERR_NO_MEMORY;

    pFS->file      = file;
    pFS->cb        = 0;
    pFS->off       = 0;
    pFS->offBuffer = 0;
    pFS->cbBuffer  = sizeof(FILEIOSTATE);

    *ppStorage = pFS;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convFileOutClose(void *pvUser, void *pStorage)
{
    NOREF(pvUser);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    PFILEIOSTATE pFS = (PFILEIOSTATE)pStorage;
    int rc = VINF_SUCCESS;

    /* Flush any remaining buffer contents. */
    if (pFS->cbBuffer)
        rc = RTFileWriteAt(pFS->file, pFS->offBuffer, &pFS->abBuffer[0], pFS->cbBuffer, NULL);
    RTFileClose(pFS->file);

    RTMemFree(pFS);

    return rc;
}

static DECLCALLBACK(int) convFileOutDelete(void *pvUser, const char *pcszFilename)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convFileOutMove(void *pvUser, const char *pcszSrc, const char *pcszDst, unsigned fMove)
{
    NOREF(pvUser);
    NOREF(pcszSrc);
    NOREF(pcszDst);
    NOREF(fMove);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convFileOutGetFreeSpace(void *pvUser, const char *pcszFilename, int64_t *pcbFreeSpace)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pcbFreeSpace, VERR_INVALID_POINTER);
    *pcbFreeSpace = INT64_MAX;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convFileOutGetModificationTime(void *pvUser, const char *pcszFilename, PRTTIMESPEC pModificationTime)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convFileOutGetSize(void *pvUser, void *pStorage, uint64_t *pcbSize)
{
    NOREF(pvUser);
    NOREF(pStorage);
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convFileOutSetSize(void *pvUser, void *pStorage, uint64_t cbSize)
{
    NOREF(pvUser);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    PFILEIOSTATE pFS = (PFILEIOSTATE)pStorage;

    int rc = RTFileSetSize(pFS->file, cbSize);
    if (RT_SUCCESS(rc))
        pFS->cb = cbSize;
    return rc;
}

static DECLCALLBACK(int) convFileOutRead(void *pvUser, void *pStorage, uint64_t uOffset, void *pvBuffer, size_t cbBuffer,
                                         size_t *pcbRead)
{
    NOREF(pvUser);
    NOREF(pStorage);
    NOREF(uOffset);
    NOREF(cbBuffer);
    NOREF(pcbRead);
    AssertPtrReturn(pvBuffer, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

static DECLCALLBACK(int) convFileOutWrite(void *pvUser, void *pStorage, uint64_t uOffset, const void *pvBuffer, size_t cbBuffer,
                                          size_t *pcbWritten)
{
    NOREF(pvUser);
    AssertPtrReturn(pStorage, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuffer, VERR_INVALID_POINTER);
    PFILEIOSTATE pFS = (PFILEIOSTATE)pStorage;
    AssertReturn(uOffset >= pFS->off, VERR_INVALID_PARAMETER);

    /* Write the data to the buffer, flushing as required. */
    size_t cbTotalWritten = 0;
    do
    {
        /* Flush the buffer if we need a new one. */
        while (uOffset > pFS->offBuffer + sizeof(pFS->abBuffer) - 1)
        {
            if (!ASMMemIsZero(pFS->abBuffer, sizeof(pFS->abBuffer)))
            {
                int rc = RTFileWriteAt(pFS->file, pFS->offBuffer,
                                       &pFS->abBuffer[0],
                                       sizeof(pFS->abBuffer), NULL);
                if (RT_FAILURE(rc))
                    return rc;
            }
            RT_ZERO(pFS->abBuffer);
            pFS->offBuffer += sizeof(pFS->abBuffer);
            pFS->cbBuffer = 0;
        }

        uint32_t cbThisWrite = (uint32_t)RT_MIN(cbBuffer,
                                                sizeof(pFS->abBuffer) - uOffset % sizeof(pFS->abBuffer));
        memcpy(&pFS->abBuffer[uOffset % sizeof(pFS->abBuffer)], pvBuffer,
               cbThisWrite);
        uOffset += cbThisWrite;
        pvBuffer = (uint8_t *)pvBuffer + cbThisWrite;
        cbBuffer -= cbThisWrite;
        cbTotalWritten += cbThisWrite;
    } while (cbBuffer > 0);

    if (pcbWritten)
        *pcbWritten = cbTotalWritten;

    pFS->cbBuffer = uOffset % sizeof(pFS->abBuffer);
    if (!pFS->cbBuffer)
        pFS->cbBuffer = sizeof(pFS->abBuffer);
    pFS->off = uOffset;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) convFileOutFlush(void *pvUser, void *pStorage)
{
    NOREF(pvUser);
    NOREF(pStorage);
    return VINF_SUCCESS;
}

static int handleConvert(HandlerArg *a)
{
    const char *pszSrcFilename = NULL;
    const char *pszDstFilename = NULL;
    bool fStdIn = false;
    bool fStdOut = false;
    bool fCreateSparse = false;
    const char *pszSrcFormat = NULL;
    VDTYPE enmSrcType = VDTYPE_HDD;
    const char *pszDstFormat = NULL;
    const char *pszVariant = NULL;
    PVDISK pSrcDisk = NULL;
    PVDISK pDstDisk = NULL;
    unsigned uImageFlags = VD_IMAGE_FLAGS_NONE;
    PVDINTERFACE pIfsImageInput = NULL;
    PVDINTERFACE pIfsImageOutput = NULL;
    VDINTERFACEIO IfsInputIO;
    VDINTERFACEIO IfsOutputIO;
    int rc = VINF_SUCCESS;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--srcfilename", 'i', RTGETOPT_REQ_STRING },
        { "--dstfilename", 'o', RTGETOPT_REQ_STRING },
        { "--stdin", 'p', RTGETOPT_REQ_NOTHING },
        { "--stdout", 'P', RTGETOPT_REQ_NOTHING },
        { "--srcformat", 's', RTGETOPT_REQ_STRING },
        { "--dstformat", 'd', RTGETOPT_REQ_STRING },
        { "--variant", 'v', RTGETOPT_REQ_STRING },
        { "--create-sparse", 'c', RTGETOPT_REQ_NOTHING }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'i':   // --srcfilename
                pszSrcFilename = ValueUnion.psz;
                break;
            case 'o':   // --dstfilename
                pszDstFilename = ValueUnion.psz;
                break;
            case 'p':   // --stdin
                fStdIn = true;
                break;
            case 'P':   // --stdout
                fStdOut = true;
                break;
            case 's':   // --srcformat
                pszSrcFormat = ValueUnion.psz;
                break;
            case 'd':   // --dstformat
                pszDstFormat = ValueUnion.psz;
                break;
            case 'v':   // --variant
                pszVariant = ValueUnion.psz;
                break;
            case 'c':   // --create-sparse
                fCreateSparse = true;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters and handle dummies/defaults. */
    if (fStdIn && !pszSrcFormat)
        return errorSyntax("Mandatory --srcformat option missing\n");
    if (!pszDstFormat)
        pszDstFormat = "VDI";
    if (fStdIn && !pszSrcFilename)
    {
        /* Complete dummy, will be just passed to various calls to fulfill
         * the "must be non-NULL" requirement, and is completely ignored
         * otherwise. It shown in the stderr message below. */
        pszSrcFilename = "stdin";
    }
    if (fStdOut && !pszDstFilename)
    {
        /* Will be stored in the destination image if it is a streamOptimized
         * VMDK, but it isn't really relevant - use it for "branding". */
        if (!RTStrICmp(pszDstFormat, "VMDK"))
            pszDstFilename = "VirtualBoxStream.vmdk";
        else
            pszDstFilename = "stdout";
    }
    if (!pszSrcFilename)
        return errorSyntax("Mandatory --srcfilename option missing\n");
    if (!pszDstFilename)
        return errorSyntax("Mandatory --dstfilename option missing\n");

    if (fStdIn)
    {
        IfsInputIO.pfnOpen                = convInOpen;
        IfsInputIO.pfnClose               = convInClose;
        IfsInputIO.pfnDelete              = convInDelete;
        IfsInputIO.pfnMove                = convInMove;
        IfsInputIO.pfnGetFreeSpace        = convInGetFreeSpace;
        IfsInputIO.pfnGetModificationTime = convInGetModificationTime;
        IfsInputIO.pfnGetSize             = convInGetSize;
        IfsInputIO.pfnSetSize             = convInSetSize;
        IfsInputIO.pfnReadSync            = convInRead;
        IfsInputIO.pfnWriteSync           = convInWrite;
        IfsInputIO.pfnFlushSync           = convInFlush;
        VDInterfaceAdd(&IfsInputIO.Core, "stdin", VDINTERFACETYPE_IO,
                       NULL, sizeof(VDINTERFACEIO), &pIfsImageInput);
    }
    if (fStdOut)
    {
        IfsOutputIO.pfnOpen                   = convStdOutOpen;
        IfsOutputIO.pfnClose                  = convStdOutClose;
        IfsOutputIO.pfnDelete                 = convStdOutDelete;
        IfsOutputIO.pfnMove                   = convStdOutMove;
        IfsOutputIO.pfnGetFreeSpace           = convStdOutGetFreeSpace;
        IfsOutputIO.pfnGetModificationTime    = convStdOutGetModificationTime;
        IfsOutputIO.pfnGetSize                = convStdOutGetSize;
        IfsOutputIO.pfnSetSize                = convStdOutSetSize;
        IfsOutputIO.pfnReadSync               = convStdOutRead;
        IfsOutputIO.pfnWriteSync              = convStdOutWrite;
        IfsOutputIO.pfnFlushSync              = convStdOutFlush;
        VDInterfaceAdd(&IfsOutputIO.Core, "stdout", VDINTERFACETYPE_IO,
                       NULL, sizeof(VDINTERFACEIO), &pIfsImageOutput);
    }
    else if (fCreateSparse)
    {
        IfsOutputIO.pfnOpen                   = convFileOutOpen;
        IfsOutputIO.pfnClose                  = convFileOutClose;
        IfsOutputIO.pfnDelete                 = convFileOutDelete;
        IfsOutputIO.pfnMove                   = convFileOutMove;
        IfsOutputIO.pfnGetFreeSpace           = convFileOutGetFreeSpace;
        IfsOutputIO.pfnGetModificationTime    = convFileOutGetModificationTime;
        IfsOutputIO.pfnGetSize                = convFileOutGetSize;
        IfsOutputIO.pfnSetSize                = convFileOutSetSize;
        IfsOutputIO.pfnReadSync               = convFileOutRead;
        IfsOutputIO.pfnWriteSync              = convFileOutWrite;
        IfsOutputIO.pfnFlushSync              = convFileOutFlush;
        VDInterfaceAdd(&IfsOutputIO.Core, "fileout", VDINTERFACETYPE_IO,
                       NULL, sizeof(VDINTERFACEIO), &pIfsImageOutput);
    }

    /* check the variant parameter */
    if (pszVariant)
    {
        char *psz = (char*)pszVariant;
        while (psz && *psz && RT_SUCCESS(rc))
        {
            size_t len;
            const char *pszComma = strchr(psz, ',');
            if (pszComma)
                len = pszComma - psz;
            else
                len = strlen(psz);
            if (len > 0)
            {
                if (!RTStrNICmp(pszVariant, "standard", len))
                    uImageFlags |= VD_IMAGE_FLAGS_NONE;
                else if (!RTStrNICmp(pszVariant, "fixed", len))
                    uImageFlags |= VD_IMAGE_FLAGS_FIXED;
                else if (!RTStrNICmp(pszVariant, "split2g", len))
                    uImageFlags |= VD_VMDK_IMAGE_FLAGS_SPLIT_2G;
                else if (!RTStrNICmp(pszVariant, "stream", len))
                    uImageFlags |= VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED;
                else if (!RTStrNICmp(pszVariant, "esx", len))
                    uImageFlags |= VD_VMDK_IMAGE_FLAGS_ESX;
                else
                    return errorSyntax("Invalid --variant option\n");
            }
            if (pszComma)
                psz += len + 1;
            else
                psz += len;
        }
    }

    do
    {
        /* try to determine input format if not specified */
        if (!pszSrcFormat)
        {
            char *pszFormat = NULL;
            VDTYPE enmType = VDTYPE_INVALID;
            rc = VDGetFormat(NULL, NULL, pszSrcFilename, VDTYPE_INVALID, &pszFormat, &enmType);
            if (RT_FAILURE(rc))
            {
                errorSyntax("No file format specified, please specify format: %Rrc\n", rc);
                break;
            }
            pszSrcFormat = pszFormat;
            enmSrcType = enmType;
        }

        rc = VDCreate(pVDIfs, enmSrcType, &pSrcDisk);
        if (RT_FAILURE(rc))
        {
            errorRuntime("Error while creating source disk container: %Rrf (%Rrc)\n", rc, rc);
            break;
        }

        rc = VDOpen(pSrcDisk, pszSrcFormat, pszSrcFilename,
                    VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_SEQUENTIAL,
                    pIfsImageInput);
        if (RT_FAILURE(rc))
        {
            errorRuntime("Error while opening source image: %Rrf (%Rrc)\n", rc, rc);
            break;
        }

        rc = VDCreate(pVDIfs, VDTYPE_HDD, &pDstDisk);
        if (RT_FAILURE(rc))
        {
            errorRuntime("Error while creating the destination disk container: %Rrf (%Rrc)\n", rc, rc);
            break;
        }

        uint64_t cbSize = VDGetSize(pSrcDisk, VD_LAST_IMAGE);
        RTStrmPrintf(g_pStdErr, "Converting image \"%s\" with size %RU64 bytes (%RU64MB)...\n", pszSrcFilename, cbSize, (cbSize + _1M - 1) / _1M);

        /* Create the output image */
        rc = VDCopy(pSrcDisk, VD_LAST_IMAGE, pDstDisk, pszDstFormat,
                    pszDstFilename, false, 0, uImageFlags, NULL,
                    VD_OPEN_FLAGS_NORMAL | VD_OPEN_FLAGS_SEQUENTIAL, NULL,
                    pIfsImageOutput, NULL);
        if (RT_FAILURE(rc))
        {
            errorRuntime("Error while copying the image: %Rrf (%Rrc)\n", rc, rc);
            break;
        }

    }
    while (0);

    if (pDstDisk)
        VDDestroy(pDstDisk);
    if (pSrcDisk)
        VDDestroy(pSrcDisk);

    return RT_SUCCESS(rc) ? 0 : 1;
}


static int handleInfo(HandlerArg *a)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = NULL;
    const char *pszFilename = NULL;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename", 'f', RTGETOPT_REQ_STRING }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    /* just try it */
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    rc = VDGetFormat(NULL, NULL, pszFilename, VDTYPE_INVALID, &pszFormat, &enmType);
    if (RT_FAILURE(rc))
        return errorSyntax("Format autodetect failed: %Rrc\n", rc);

    rc = VDCreate(pVDIfs, enmType, &pDisk);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    /* Open the image */
    rc = VDOpen(pDisk, pszFormat, pszFilename, VD_OPEN_FLAGS_INFO | VD_OPEN_FLAGS_READONLY, NULL);
    RTStrFree(pszFormat);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while opening the image: %Rrf (%Rrc)\n", rc, rc);

    VDDumpImages(pDisk);

    VDDestroy(pDisk);

    return rc;
}


static DECLCALLBACK(int) vboximgQueryBlockStatus(void *pvUser, uint64_t off,
                                                 uint64_t cb, bool *pfAllocated)
{
    RTVFS hVfs = (RTVFS)pvUser;
    return RTVfsQueryRangeState(hVfs, off, cb, pfAllocated);
}


static DECLCALLBACK(int) vboximgQueryRangeUse(void *pvUser, uint64_t off, uint64_t cb,
                                              bool *pfUsed)
{
    RTDVM hVolMgr = (RTDVM)pvUser;
    return RTDvmMapQueryBlockStatus(hVolMgr, off, cb, pfUsed);
}


typedef struct VBOXIMGVFS
{
    /** Pointer to the next VFS handle. */
    struct VBOXIMGVFS *pNext;
    /** VFS handle. */
    RTVFS              hVfs;
} VBOXIMGVFS, *PVBOXIMGVFS;

static int handleCompact(HandlerArg *a)
{
    PVDISK pDisk = NULL;
    VDINTERFACEQUERYRANGEUSE VDIfQueryRangeUse;
    PVDINTERFACE pIfsCompact = NULL;
    RTDVM hDvm = NIL_RTDVM;
    PVBOXIMGVFS pVBoxImgVfsHead = NULL;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename",          'f', RTGETOPT_REQ_STRING },
        { "--filesystemaware",   'a', RTGETOPT_REQ_NOTHING },
        { "--file-system-aware", 'a', RTGETOPT_REQ_NOTHING },
    };

    const char *pszFilename      = NULL;
    bool        fFilesystemAware = false;
    bool        fVerbose         = true;

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            case 'a':
                fFilesystemAware = true;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    /* just try it */
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    int rc = VDGetFormat(NULL, NULL, pszFilename, VDTYPE_INVALID, &pszFormat, &enmType);
    if (RT_FAILURE(rc))
        return errorSyntax("Format autodetect failed: %Rrc\n", rc);

    rc = VDCreate(pVDIfs, enmType, &pDisk);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    /* Open the image */
    rc = VDOpen(pDisk, pszFormat, pszFilename, VD_OPEN_FLAGS_NORMAL, NULL);
    RTStrFree(pszFormat);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while opening the image: %Rrf (%Rrc)\n", rc, rc);

    /*
     * If --file-system-aware, we first ask the disk volume manager (DVM) to
     * find the volumes on the disk.
     */
    if (   RT_SUCCESS(rc)
        && fFilesystemAware)
    {
        RTVFSFILE hVfsDisk;
        rc = VDCreateVfsFileFromDisk(pDisk, 0 /*fFlags*/, &hVfsDisk);
        if (RT_SUCCESS(rc))
        {
            rc = RTDvmCreate(&hDvm, hVfsDisk, 512 /*cbSector*/, 0 /*fFlags*/);
            RTVfsFileRelease(hVfsDisk);
            if (RT_SUCCESS(rc))
            {
                rc = RTDvmMapOpen(hDvm);
                if (   RT_SUCCESS(rc)
                    && RTDvmMapGetValidVolumes(hDvm) > 0)
                {
                    /*
                     * Enumerate the volumes: Try finding a file system interpreter and
                     * set the block query status callback to work with the FS.
                     */
                    uint32_t    iVol = 0;
                    RTDVMVOLUME hVol;
                    rc = RTDvmMapQueryFirstVolume(hDvm, &hVol);
                    AssertRC(rc);

                    while (RT_SUCCESS(rc))
                    {
                        if (fVerbose)
                        {
                            char *pszVolName;
                            rc = RTDvmVolumeQueryName(hVol, &pszVolName);
                            if (RT_FAILURE(rc))
                                pszVolName = NULL;
                            RTMsgInfo("Vol%u: %Rhcb %s%s%s\n", iVol, RTDvmVolumeGetSize(hVol),
                                      RTDvmVolumeTypeGetDescr(RTDvmVolumeGetType(hVol)),
                                      pszVolName ? " " : "", pszVolName ? pszVolName : "");
                            RTStrFree(pszVolName);
                        }

                        RTVFSFILE hVfsFile;
                        rc = RTDvmVolumeCreateVfsFile(hVol, RTFILE_O_READWRITE, &hVfsFile);
                        if (RT_FAILURE(rc))
                        {
                            errorRuntime("RTDvmVolumeCreateVfsFile failed: %Rrc\n");
                            break;
                        }

                        /* Try to detect the filesystem in this volume. */
                        RTERRINFOSTATIC ErrInfo;
                        RTVFS hVfs;
                        rc = RTVfsMountVol(hVfsFile, RTVFSMNT_F_READ_ONLY | RTVFSMNT_F_FOR_RANGE_IN_USE, &hVfs,
                                           RTErrInfoInitStatic(&ErrInfo));
                        RTVfsFileRelease(hVfsFile);
                        if (RT_SUCCESS(rc))
                        {
                            PVBOXIMGVFS pVBoxImgVfs = (PVBOXIMGVFS)RTMemAllocZ(sizeof(VBOXIMGVFS));
                            if (!pVBoxImgVfs)
                            {
                                RTVfsRelease(hVfs);
                                rc = VERR_NO_MEMORY;
                                break;
                            }
                            pVBoxImgVfs->hVfs = hVfs;
                            pVBoxImgVfs->pNext = pVBoxImgVfsHead;
                            pVBoxImgVfsHead = pVBoxImgVfs;
                            RTDvmVolumeSetQueryBlockStatusCallback(hVol, vboximgQueryBlockStatus, hVfs);
                        }
                        else if (rc != VERR_NOT_SUPPORTED)
                        {
                            if (RTErrInfoIsSet(&ErrInfo.Core))
                                errorRuntime("RTVfsMountVol failed: %s\n", ErrInfo.Core.pszMsg);
                            break;
                        }
                        else if (fVerbose && RTErrInfoIsSet(&ErrInfo.Core))
                            RTMsgInfo("Unsupported file system: %s", ErrInfo.Core.pszMsg);

                        /*
                         * Advance.  (Releasing hVol here is fine since RTDvmVolumeCreateVfsFile
                         * retained a reference and the hVfs a reference of it again.)
                         */
                        RTDVMVOLUME hVolNext = NIL_RTDVMVOLUME;
                        if (RT_SUCCESS(rc))
                            rc = RTDvmMapQueryNextVolume(hDvm, hVol, &hVolNext);
                        RTDvmVolumeRelease(hVol);
                        hVol = hVolNext;
                        iVol++;
                    }

                    if (rc == VERR_DVM_MAP_NO_VOLUME)
                        rc = VINF_SUCCESS;

                    if (RT_SUCCESS(rc))
                    {
                        VDIfQueryRangeUse.pfnQueryRangeUse = vboximgQueryRangeUse;
                        VDInterfaceAdd(&VDIfQueryRangeUse.Core, "QueryRangeUse", VDINTERFACETYPE_QUERYRANGEUSE,
                                       hDvm, sizeof(VDINTERFACEQUERYRANGEUSE), &pIfsCompact);
                    }
                }
                else if (RT_SUCCESS(rc))
                    RTPrintf("There are no partitions in the volume map\n");
                else if (rc == VERR_NOT_FOUND)
                {
                    RTPrintf("No known volume format on disk found\n");
                    rc = VINF_SUCCESS;
                }
                else
                    errorRuntime("Error while opening the volume manager: %Rrf (%Rrc)\n", rc, rc);
            }
            else
                errorRuntime("Error creating the volume manager: %Rrf (%Rrc)\n", rc, rc);
        }
        else
            errorRuntime("Error while creating VFS interface for the disk: %Rrf (%Rrc)\n", rc, rc);
    }

    if (RT_SUCCESS(rc))
    {
        rc = VDCompact(pDisk, 0, pIfsCompact);
        if (RT_FAILURE(rc))
            errorRuntime("Error while compacting image: %Rrf (%Rrc)\n", rc, rc);
    }

    while (pVBoxImgVfsHead)
    {
        PVBOXIMGVFS pVBoxImgVfsFree = pVBoxImgVfsHead;

        pVBoxImgVfsHead = pVBoxImgVfsHead->pNext;
        RTVfsRelease(pVBoxImgVfsFree->hVfs);
        RTMemFree(pVBoxImgVfsFree);
    }

    if (hDvm)
        RTDvmRelease(hDvm);

    VDDestroy(pDisk);

    return rc;
}


static int handleCreateCache(HandlerArg *a)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = NULL;
    const char *pszFilename = NULL;
    uint64_t cbSize = 0;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename", 'f', RTGETOPT_REQ_STRING },
        { "--size",     's', RTGETOPT_REQ_UINT64 }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            case 's':   // --size
                cbSize = ValueUnion.u64;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    if (!cbSize)
        return errorSyntax("Mandatory --size option missing\n");

    /* just try it */
    rc = VDCreate(pVDIfs, VDTYPE_HDD, &pDisk);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    rc = VDCreateCache(pDisk, "VCI", pszFilename, cbSize, VD_IMAGE_FLAGS_DEFAULT,
                       NULL, NULL, VD_OPEN_FLAGS_NORMAL, NULL, NULL);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk cache: %Rrf (%Rrc)\n", rc, rc);

    VDDestroy(pDisk);

    return rc;
}

static DECLCALLBACK(bool) vdIfCfgCreateBaseAreKeysValid(void *pvUser, const char *pszzValid)
{
    RT_NOREF2(pvUser, pszzValid);
    return VINF_SUCCESS; /** @todo Implement. */
}

static DECLCALLBACK(int) vdIfCfgCreateBaseQuerySize(void *pvUser, const char *pszName, size_t *pcbValue)
{
    AssertPtrReturn(pcbValue, VERR_INVALID_POINTER);

    AssertPtrReturn(pvUser, VERR_GENERAL_FAILURE);

    if (RTStrCmp(pszName, "DataAlignment"))
        return VERR_CFGM_VALUE_NOT_FOUND;

    *pcbValue = strlen((const char *)pvUser) + 1 /* include terminator */;

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vdIfCfgCreateBaseQuery(void *pvUser, const char *pszName, char *pszValue, size_t cchValue)
{
    AssertPtrReturn(pszValue, VERR_INVALID_POINTER);

    AssertPtrReturn(pvUser, VERR_GENERAL_FAILURE);

    if (RTStrCmp(pszName, "DataAlignment"))
        return VERR_CFGM_VALUE_NOT_FOUND;

    if (strlen((const char *)pvUser) >= cchValue)
        return VERR_CFGM_NOT_ENOUGH_SPACE;

    memcpy(pszValue, pvUser, strlen((const char *)pvUser) + 1);

    return VINF_SUCCESS;

}

static int handleCreateBase(HandlerArg *a)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = NULL;
    const char *pszFilename = NULL;
    const char *pszBackend  = "VDI";
    const char *pszVariant  = NULL;
    unsigned uImageFlags = VD_IMAGE_FLAGS_NONE;
    uint64_t cbSize = 0;
    const char *pszDataAlignment = NULL;
    VDGEOMETRY LCHSGeometry, PCHSGeometry;
    PVDINTERFACE pVDIfsOperation = NULL;
    VDINTERFACECONFIG vdIfCfg;

    memset(&LCHSGeometry, 0, sizeof(LCHSGeometry));
    memset(&PCHSGeometry, 0, sizeof(PCHSGeometry));

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename",       'f', RTGETOPT_REQ_STRING },
        { "--size",           's', RTGETOPT_REQ_UINT64 },
        { "--format",         'b', RTGETOPT_REQ_STRING },
        { "--variant",        'v', RTGETOPT_REQ_STRING },
        { "--dataalignment",  'a', RTGETOPT_REQ_STRING }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            case 's':   // --size
                cbSize = ValueUnion.u64;
                break;

            case 'b':   // --format
                pszBackend = ValueUnion.psz;
                break;

            case 'v':   // --variant
                pszVariant = ValueUnion.psz;
                break;

            case 'a':   // --dataalignment
                pszDataAlignment = ValueUnion.psz;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    if (!cbSize)
        return errorSyntax("Mandatory --size option missing\n");

    if (pszVariant)
    {
        rc = parseDiskVariant(pszVariant, &uImageFlags);
        if (RT_FAILURE(rc))
            return errorSyntax("Invalid variant %s given\n", pszVariant);
    }

    /* Setup the config interface if required. */
    if (pszDataAlignment)
    {
        vdIfCfg.pfnAreKeysValid = vdIfCfgCreateBaseAreKeysValid;
        vdIfCfg.pfnQuerySize    = vdIfCfgCreateBaseQuerySize;
        vdIfCfg.pfnQuery        = vdIfCfgCreateBaseQuery;
        VDInterfaceAdd(&vdIfCfg.Core, "Config", VDINTERFACETYPE_CONFIG, (void *)pszDataAlignment,
                       sizeof(vdIfCfg), &pVDIfsOperation);
    }

    /* just try it */
    rc = VDCreate(pVDIfs, VDTYPE_HDD, &pDisk);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    rc = VDCreateBase(pDisk, pszBackend, pszFilename, cbSize, uImageFlags,
                      NULL, &PCHSGeometry, &LCHSGeometry, NULL, VD_OPEN_FLAGS_NORMAL,
                      NULL, pVDIfsOperation);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk: %Rrf (%Rrc)\n", rc, rc);

    VDDestroy(pDisk);

    return rc;
}


static int handleRepair(HandlerArg *a)
{
    int rc = VINF_SUCCESS;
    const char *pszFilename = NULL;
    char *pszBackend = NULL;
    const char *pszFormat  = NULL;
    bool fDryRun = false;
    VDTYPE enmType = VDTYPE_HDD;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename", 'f', RTGETOPT_REQ_STRING  },
        { "--dry-run",  'd', RTGETOPT_REQ_NOTHING },
        { "--format",   'b', RTGETOPT_REQ_STRING  }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            case 'd':   // --dry-run
                fDryRun = true;
                break;

            case 'b':   // --format
                pszFormat = ValueUnion.psz;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    /* just try it */
    if (!pszFormat)
    {
        rc = VDGetFormat(NULL, NULL, pszFilename, VDTYPE_INVALID, &pszBackend, &enmType);
        if (RT_FAILURE(rc))
            return errorSyntax("Format autodetect failed: %Rrc\n", rc);
        pszFormat = pszBackend;
    }

    rc = VDRepair(pVDIfs, NULL, pszFilename, pszFormat, fDryRun ? VD_REPAIR_DRY_RUN : 0);
    if (RT_FAILURE(rc))
        rc = errorRuntime("Error while repairing the virtual disk: %Rrf (%Rrc)\n", rc, rc);

    if (pszBackend)
        RTStrFree(pszBackend);
    return rc;
}


static int handleClearComment(HandlerArg *a)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = NULL;
    const char *pszFilename = NULL;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename", 'f', RTGETOPT_REQ_STRING  }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    /* just try it */
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    rc = VDGetFormat(NULL, NULL, pszFilename, VDTYPE_INVALID, &pszFormat, &enmType);
    if (RT_FAILURE(rc))
        return errorSyntax("Format autodetect failed: %Rrc\n", rc);

    rc = VDCreate(pVDIfs, enmType, &pDisk);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    /* Open the image */
    rc = VDOpen(pDisk, pszFormat, pszFilename, VD_OPEN_FLAGS_INFO, NULL);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while opening the image: %Rrf (%Rrc)\n", rc, rc);

    VDSetComment(pDisk, 0, NULL);

    VDDestroy(pDisk);
    return rc;
}


static int handleCreateFloppy(HandlerArg *a)
{
    const char *pszFilename         = NULL;
    uint64_t    cbFloppy            = 1474560;
    uint16_t    cbSector            = 0;
    uint8_t     cHeads              = 0;
    uint8_t     cSectorsPerCluster  = 0;
    uint8_t     cSectorsPerTrack    = 0;
    uint16_t    cRootDirEntries     = 0;
    uint8_t     bMedia              = 0;

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--sectors-per-cluster",  'c', RTGETOPT_REQ_UINT8  },
        { "--filename",             'f', RTGETOPT_REQ_STRING },
        { "--heads",                'h', RTGETOPT_REQ_UINT8  },
        { "--media-byte",           'm', RTGETOPT_REQ_UINT8  },
        { "--root-dir-entries",     'r', RTGETOPT_REQ_UINT16 },
        { "--size",                 's', RTGETOPT_REQ_UINT64 },
        { "--sector-size",          'S', RTGETOPT_REQ_UINT16 },
        { "--sectors-per-track",    't', RTGETOPT_REQ_UINT8  },
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'c': cSectorsPerCluster = ValueUnion.u8; break;
            case 'f': pszFilename        = ValueUnion.psz; break;
            case 'h': cHeads             = ValueUnion.u8; break;
            case 'm': bMedia             = ValueUnion.u8; break;
            case 'r': cRootDirEntries    = ValueUnion.u16; break;
            case 's': cbFloppy           = ValueUnion.u64; break;
            case 'S': cbSector           = ValueUnion.u16; break;
            case 't': cSectorsPerTrack   = ValueUnion.u8; break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    /*
     * Do the job.
     */
    uint32_t        offError;
    RTERRINFOSTATIC ErrInfo;
    RTVFSFILE       hVfsFile;
    int rc = RTVfsChainOpenFile(pszFilename,
                                 RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_ALL
                                | (0770 << RTFILE_O_CREATE_MODE_SHIFT),
                                 &hVfsFile, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        rc = RTFsFatVolFormat(hVfsFile, 0, cbFloppy, RTFSFATVOL_FMT_F_FULL, cbSector, cSectorsPerCluster, RTFSFATTYPE_INVALID,
                              cHeads, cSectorsPerTrack, bMedia, 0 /*cHiddenSectors*/, cRootDirEntries,
                              RTErrInfoInitStatic(&ErrInfo));
        RTVfsFileRelease(hVfsFile);
        if (RT_SUCCESS(rc))
            return RTEXITCODE_SUCCESS;

        if (RTErrInfoIsSet(&ErrInfo.Core))
            errorRuntime("Error %Rrc formatting floppy '%s': %s", rc, pszFilename, ErrInfo.Core.pszMsg);
        else
            errorRuntime("Error formatting floppy '%s': %Rrc", pszFilename, rc);
    }
    else
        RTVfsChainMsgError("RTVfsChainOpenFile", pszFilename, rc, offError, &ErrInfo.Core);
    return RTEXITCODE_FAILURE;
}


static int handleCreateIso(HandlerArg *a)
{
    return RTFsIsoMakerCmd(a->argc + 1, a->argv - 1);
}


static int handleClearResize(HandlerArg *a)
{
    int rc = VINF_SUCCESS;
    PVDISK pDisk = NULL;
    const char *pszFilename = NULL;
    uint64_t    cbNew = 0;
    VDGEOMETRY LCHSGeometry, PCHSGeometry;

    memset(&LCHSGeometry, 0, sizeof(LCHSGeometry));
    memset(&PCHSGeometry, 0, sizeof(PCHSGeometry));

    /* Parse the command line. */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--filename", 'f', RTGETOPT_REQ_STRING },
        { "--size",     's', RTGETOPT_REQ_UINT64 }
    };
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            case 's':   // --size
                cbNew = ValueUnion.u64;
                break;

            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                printUsage(g_pStdErr);
                return ch;
        }
    }

    /* Check for mandatory parameters. */
    if (!pszFilename)
        return errorSyntax("Mandatory --filename option missing\n");

    if (!cbNew)
        return errorSyntax("Mandatory --size option missing or invalid\n");

    /* just try it */
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    rc = VDGetFormat(NULL, NULL, pszFilename, VDTYPE_INVALID, &pszFormat, &enmType);
    if (RT_FAILURE(rc))
        return errorSyntax("Format autodetect failed: %Rrc\n", rc);

    rc = VDCreate(pVDIfs, enmType, &pDisk);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while creating the virtual disk container: %Rrf (%Rrc)\n", rc, rc);

    /* Open the image */
    rc = VDOpen(pDisk, pszFormat, pszFilename, VD_OPEN_FLAGS_NORMAL, NULL);
    if (RT_FAILURE(rc))
        return errorRuntime("Error while opening the image: %Rrf (%Rrc)\n", rc, rc);

    rc = VDResize(pDisk, cbNew, &PCHSGeometry, &LCHSGeometry, NULL);
    if (RT_FAILURE(rc))
        rc = errorRuntime("Error while resizing the virtual disk: %Rrf (%Rrc)\n", rc, rc);

    VDDestroy(pDisk);
    return rc;
}


int main(int argc, char *argv[])
{
    int exitcode = 0;

    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_STANDALONE_APP);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    g_pszProgName = RTPathFilename(argv[0]);

    bool fShowLogo = false;
    int  iCmd      = 1;
    int  iCmdArg;

    /* global options */
    for (int i = 1; i < argc || argc <= iCmd; i++)
    {
        if (    argc <= iCmd
            ||  !strcmp(argv[i], "help")
            ||  !strcmp(argv[i], "-?")
            ||  !strcmp(argv[i], "-h")
            ||  !strcmp(argv[i], "-help")
            ||  !strcmp(argv[i], "--help"))
        {
            showLogo(g_pStdOut);
            printUsage(g_pStdOut);
            return 0;
        }

        if (   !strcmp(argv[i], "-v")
            || !strcmp(argv[i], "-version")
            || !strcmp(argv[i], "-Version")
            || !strcmp(argv[i], "--version"))
        {
            /* Print version number, and do nothing else. */
            RTPrintf("%sr%d\n", VBOX_VERSION_STRING, RTBldCfgRevision());
            return 0;
        }

        if (   !strcmp(argv[i], "--nologo")
            || !strcmp(argv[i], "-nologo")
            || !strcmp(argv[i], "-q"))
        {
            /* suppress the logo */
            fShowLogo = false;
            iCmd++;
        }
        else
        {
            break;
        }
    }

    iCmdArg = iCmd + 1;

    if (fShowLogo)
        showLogo(g_pStdOut);

    /* initialize the VD backend with dummy handlers */
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = handleVDMessage;

    rc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                        NULL, sizeof(VDINTERFACEERROR), &pVDIfs);

    rc = VDInit();
    if (RT_FAILURE(rc))
    {
        errorSyntax("Initializing backends failed! rc=%Rrc\n", rc);
        return 1;
    }

    /*
     * All registered command handlers
     */
    static const struct
    {
        const char *command;
        int (*handler)(HandlerArg *a);
    } s_commandHandlers[] =
    {
        { "setuuid",      handleSetUUID      },
        { "geometry",     handleGeometry     },
        { "convert",      handleConvert      },
        { "info",         handleInfo         },
        { "compact",      handleCompact      },
        { "createcache",  handleCreateCache  },
        { "createbase",   handleCreateBase   },
        { "createfloppy", handleCreateFloppy },
        { "createiso",    handleCreateIso },
        { "repair",       handleRepair       },
        { "clearcomment", handleClearComment },
        { "resize",       handleClearResize  },
        { NULL,           NULL               }
    };

    HandlerArg handlerArg = { 0, NULL };
    int commandIndex;
    for (commandIndex = 0; s_commandHandlers[commandIndex].command != NULL; commandIndex++)
    {
        if (!strcmp(s_commandHandlers[commandIndex].command, argv[iCmd]))
        {
            handlerArg.argc = argc - iCmdArg;
            handlerArg.argv = &argv[iCmdArg];

            exitcode = s_commandHandlers[commandIndex].handler(&handlerArg);
            break;
        }
    }
    if (!s_commandHandlers[commandIndex].command)
    {
        errorSyntax("Invalid command '%s'", argv[iCmd]);
        return 1;
    }

    rc = VDShutdown();
    if (RT_FAILURE(rc))
    {
        errorSyntax("Unloading backends failed! rc=%Rrc\n", rc);
        return 1;
    }

    return exitcode;
}

/* dummy stub for RuntimeR3 */
#ifndef RT_OS_WINDOWS
RTDECL(bool) RTAssertShouldPanic(void)
{
    return true;
}
#endif
