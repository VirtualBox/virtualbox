/* $Id: VUSBSniffer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Virtual USB - Sniffer facility.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DRV_VUSB
#include <VBox/log.h>
#include <iprt/file.h>
#include <iprt/errcore.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>

#include "VUSBSnifferInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * The internal VUSB sniffer state.
 */
typedef struct VUSBSNIFFERINT
{
    /** The file handle to dump to. */
    RTFILE            hFile;
    /** Fast Mutex protecting the state against concurrent access. */
    RTSEMFASTMUTEX    hMtx;
    /** File stream. */
    VUSBSNIFFERSTRM   Strm;
    /** Pointer to the used format. */
    PCVUSBSNIFFERFMT  pFmt;
    /** Format specific state - variable in size. */
    uint8_t           abFmt[1];
} VUSBSNIFFERINT;
/** Pointer to the internal VUSB sniffer state. */
typedef VUSBSNIFFERINT *PVUSBSNIFFERINT;


/*********************************************************************************************************************************
*   Static Variables                                                                                                             *
*********************************************************************************************************************************/

static PCVUSBSNIFFERFMT s_aVUsbSnifferFmts[] =
{
    &g_VUsbSnifferFmtPcapNg,
    &g_VUsbSnifferFmtUsbMon,
    &g_VUsbSnifferFmtVmx,
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/** @interface_method_impl{VUSBSNIFFERSTRM,pfnWrite} */
static DECLCALLBACK(int) vusbSnifferStrmWrite(PVUSBSNIFFERSTRM pStrm, const void *pvBuf, size_t cbBuf)
{
    PVUSBSNIFFERINT pThis = RT_FROM_MEMBER(pStrm, VUSBSNIFFERINT, Strm);

    return RTFileWrite(pThis->hFile, pvBuf, cbBuf, NULL);
}

/**
 * Returns a supporting format writer taken from the given format name.
 *
 * @returns Pointer to the format structure or NULL if none was found.
 * @param   pszFmt    The format to use.
 */
static PCVUSBSNIFFERFMT vusbSnifferGetFmtFromString(const char *pszFmt)
{
    for (unsigned i = 0; i < RT_ELEMENTS(s_aVUsbSnifferFmts); i++)
    {
        if (!RTStrICmp(pszFmt, s_aVUsbSnifferFmts[i]->szName))
            return s_aVUsbSnifferFmts[i];
    }

    return NULL;
}

/**
 * Returns a supporting format writer taken from the file suffix.
 *
 * @returns Pointer to the format structure or NULL if none was found.
 * @param   pszFilename    The file name to take the suffix from.
 */
static PCVUSBSNIFFERFMT vusbSnifferGetFmtFromFilename(const char *pszFilename)
{
    const char *pszFileExt = RTPathSuffix(pszFilename);
    if (!pszFileExt)
        return NULL;

    pszFileExt++; /* Skip the dot. */

    for (unsigned i = 0; i < RT_ELEMENTS(s_aVUsbSnifferFmts); i++)
    {
        unsigned idxFileExt = 0;

        while (s_aVUsbSnifferFmts[i]->papszFileExts[idxFileExt])
        {
            if (!RTStrICmp(pszFileExt, s_aVUsbSnifferFmts[i]->papszFileExts[idxFileExt]))
                return s_aVUsbSnifferFmts[i];

            idxFileExt++;
        }
    }

    return NULL;
}


DECLHIDDEN(int) VUSBSnifferCreate(PVUSBSNIFFER phSniffer, uint32_t fFlags,
                                  const char *pszCaptureFilename, const char *pszFmt,
                                  const char *pszDesc)
{
    RT_NOREF(pszDesc);
    int rc = VINF_SUCCESS;
    PVUSBSNIFFERINT pThis = NULL;
    PCVUSBSNIFFERFMT pFmt = NULL;

    if (pszFmt)
        pFmt = vusbSnifferGetFmtFromString(pszFmt);
    else
        pFmt = vusbSnifferGetFmtFromFilename(pszCaptureFilename);

    if (!pFmt)
        return VERR_NOT_FOUND;

    pThis = (PVUSBSNIFFERINT)RTMemAllocZ(RT_UOFFSETOF_DYN(VUSBSNIFFERINT, abFmt[pFmt->cbFmt]));
    if (pThis)
    {
        pThis->hFile         = NIL_RTFILE;
        pThis->hMtx          = NIL_RTSEMFASTMUTEX;
        pThis->pFmt          = pFmt;
        pThis->Strm.pfnWrite = vusbSnifferStrmWrite;

        rc = RTSemFastMutexCreate(&pThis->hMtx);
        if (RT_SUCCESS(rc))
        {
            uint32_t fFileFlags = RTFILE_O_DENY_NONE | RTFILE_O_WRITE | RTFILE_O_READ;
            if (fFlags & VUSBSNIFFER_F_NO_REPLACE)
                fFileFlags |= RTFILE_O_CREATE;
            else
                fFileFlags |= RTFILE_O_CREATE_REPLACE;

            rc = RTFileOpen(&pThis->hFile, pszCaptureFilename, fFileFlags);
            if (RT_SUCCESS(rc))
            {
                rc = pThis->pFmt->pfnInit((PVUSBSNIFFERFMTINT)&pThis->abFmt[0], &pThis->Strm);
                if (RT_SUCCESS(rc))
                {
                    *phSniffer = pThis;
                    return VINF_SUCCESS;
                }

                RTFileClose(pThis->hFile);
                pThis->hFile = NIL_RTFILE;
                RTFileDelete(pszCaptureFilename);
            }
            RTSemFastMutexDestroy(pThis->hMtx);
            pThis->hMtx = NIL_RTSEMFASTMUTEX;
        }

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Destroys the given VUSB sniffer instance.
 *
 * @param   hSniffer              The sniffer instance to destroy.
 */
DECLHIDDEN(void) VUSBSnifferDestroy(VUSBSNIFFER hSniffer)
{
    PVUSBSNIFFERINT pThis = hSniffer;

    int rc = RTSemFastMutexRequest(pThis->hMtx);
    AssertRC(rc);

    pThis->pFmt->pfnDestroy((PVUSBSNIFFERFMTINT)&pThis->abFmt[0]);

    if (pThis->hFile != NIL_RTFILE)
        RTFileClose(pThis->hFile);

    RTSemFastMutexRelease(pThis->hMtx);
    RTSemFastMutexDestroy(pThis->hMtx);
    RTMemFree(pThis);
}

/**
 * Records an VUSB event.
 *
 * @returns VBox status code.
 * @param   hSniffer              The sniffer instance.
 * @param   pUrb                  The URB triggering the event.
 * @param   enmEvent              The type of event to record.
 */
DECLHIDDEN(int) VUSBSnifferRecordEvent(VUSBSNIFFER hSniffer, PVUSBURB pUrb, VUSBSNIFFEREVENT enmEvent)
{
    int rc = VINF_SUCCESS;
    PVUSBSNIFFERINT pThis = hSniffer;

    /* Write the packet to the capture file. */
    rc = RTSemFastMutexRequest(pThis->hMtx);
    if (RT_SUCCESS(rc))
    {
        rc = pThis->pFmt->pfnRecordEvent((PVUSBSNIFFERFMTINT)&pThis->abFmt[0], pUrb, enmEvent);
        RTSemFastMutexRelease(pThis->hMtx);
    }

    return rc;
}

