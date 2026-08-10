/* $Id: clipboard-cmds.cpp 114744 2026-07-21 18:37:21Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Clipboard commands.
 */

/*
 * Copyright (C) 2007-2026 Oracle and/or its affiliates.
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
#include "VBoxClient.h"
#include "clipboard.h"

#include <VBox/GuestHost/Wayland.h>
#include <VBox/GuestHost/mime-type-converter.h>

#include <iprt/crc.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/handle.h>
#include <iprt/message.h>
#include <iprt/pipe.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct
{
    RTEXITCODE      rcExit;
    bool volatile   fShutdown;
    RTPIPE          hPipeRead;
    RTPIPE          hPipeWrite;

    SHCLWAYLANDCTX  Wl;
} VBCLCMDCLIPBOARDSTATE;


DECLINLINE(int) vbclHandleClose(PCRTHANDLE pHandle)
{
    switch (pHandle->enmType)
    {
        case RTHANDLETYPE_PIPE:
            return RTPipeClose(pHandle->u.hPipe);
        case RTHANDLETYPE_FILE:
            return RTFileClose(pHandle->u.hFile);
#if 0
        case RTHANDLETYPE_SOCKET:
            return RTSocketClose(pHandle->u.hSocket);
#endif
        default:
            return VERR_NOT_IMPLEMENTED;
    }
}


static int vbclHandleWrite(PCRTHANDLE pHandle, void const *pvBuf, size_t cbToWrite, RTMSINTERVAL cMsTimeout)
{
    switch (pHandle->enmType)
    {
        case RTHANDLETYPE_PIPE:
        {
            /* Use blocking timeout if no timeout. */
            if (cMsTimeout == RT_INDEFINITE_WAIT)
                return RTPipeWriteBlocking(pHandle->u.hPipe, pvBuf, cbToWrite, NULL);

            /* Try a non-blocking write first before we start selecting... */
            size_t cbWritten = 0;
            int rc = RTPipeWrite(pHandle->u.hPipe, pvBuf, cbToWrite, &cbWritten);
            if ((RT_SUCCESS(rc) && cbWritten == cbToWrite) || RT_FAILURE(rc))
                return rc;

            /* Poll & write loop. */
            uint64_t const msStart = RTTimeMilliTS();
            size_t         off     = cbWritten;
            while (off < cbToWrite)
            {
                uint64_t cMsElapsed = RTTimeMilliTS() - msStart;
                rc = RTPipeSelectOne(pHandle->u.hPipe, cMsElapsed < cMsTimeout ? cMsTimeout - cMsElapsed : 64);
                if (RT_FAILURE(rc))
                    return rc;

                cbWritten = 0;
                rc = RTPipeWrite(pHandle->u.hPipe, &((uint8_t const *)pvBuf)[off], cbToWrite - off, &cbWritten);
                if (RT_FAILURE(rc))
                    return rc;
                off += cbWritten;
            }
            return VINF_SUCCESS;
        }

        case RTHANDLETYPE_FILE:
            return RTFileWrite(pHandle->u.hFile, pvBuf, cbToWrite, NULL);
#if 0
        case RTHANDLETYPE_SOCKET:
            return RTSocketWrite(pHandle->u.hSocket, pvBuf, cbToWrite);
#endif
        default:
            return VERR_NOT_IMPLEMENTED;
    }
}


/** This will be non-blocking for pipes, but blocking for files. */
DECLINLINE(int) vbclHandleReadNB(PCRTHANDLE pHandle, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    switch (pHandle->enmType)
    {
        case RTHANDLETYPE_PIPE:
            return RTPipeRead(pHandle->u.hPipe, pvBuf, cbToRead, pcbRead);
        case RTHANDLETYPE_FILE:
            return RTFileRead(pHandle->u.hFile, pvBuf, cbToRead, pcbRead);
#if 0
        case RTHANDLETYPE_SOCKET:
            return RTSocketReadNB(pHandle->u.hSocket, pvBuf, cbToRead, pcbRead);
#endif
        default:
            return VERR_NOT_IMPLEMENTED;
    }
}


/** Poll for the handle to become ready for reading. */
DECLINLINE(int) vbclHandleSelectOne(PCRTHANDLE pHandle, RTMSINTERVAL cMsTimeout)
{
    switch (pHandle->enmType)
    {
        case RTHANDLETYPE_PIPE:
            return RTPipeSelectOne(pHandle->u.hPipe, cMsTimeout);

        case RTHANDLETYPE_FILE:
            return VINF_SUCCESS;
#if 0
        case RTHANDLETYPE_SOCKET:
            return RTSocketSelectOne(pHandle->u.hSocket, cMsTimeout);
#endif
        default:
            return VERR_NOT_IMPLEMENTED;
    }
}


/**
 * Helper for vbclWaylandHlpGtkClipReadIntoCache.
 */
static bool vbclWaylandDeserializeCacheReadDataCheckCrc(void const *pvData, size_t cbData,
                                                        uint64_t u64CrcExpected, uint32_t fFormat)
{
    uint64_t const u64CrcActual = RTCrc64(pvData, cbData);
    if (u64CrcActual == u64CrcExpected)
        return true;
    VBClLogError("CRC mismatch for %#x (%#zx bytes): %#RX64, expected %#RX64\n", fFormat, cbData, u64CrcActual, u64CrcExpected);
    return false;
}


/**
 * Reads --clipboard-get data of the given format into the specified cache.
 *
 * The @a pszLine and @a pcbLine will be adjust (typcially contains the start or
 * all of the data).
 */
static bool vbclWaylandDeserializeCacheReadData(PCRTHANDLE pHandleSrc, char *pszLine, size_t *pcbLine, PSHCLCACHE pDstCache,
                                                uint32_t fFormat, uint32_t cbData, uint64_t u64CrcExpected,
                                                uint64_t msStart, RTMSINTERVAL cMsMax)
{
    LogRel6(("%s: Reading fFormat=%#x cbData=%#x crc64=%#RX64 *pcbLine=%#zx...\n",
             __func__, fFormat, cbData, u64CrcExpected, *pcbLine));

    /*
     * Allocate a cache buffer.
     */
    uint8_t *pbData;
    int rc = ShClCachePrep(pDstCache, fFormat, cbData, (void **)&pbData);
    if (RT_FAILURE(rc))
    {
        VBClLogError("ShClCachePrep failed for %#x: %Rrc\n", fFormat, rc);
        return false;
    }

    /*
     * Copy data from pszLine into the buffer.
     */
    size_t offData = 0;
    if (*pcbLine > 0 && cbData > 0)
    {
        size_t cbToCopy = RT_MIN(cbData, *pcbLine);
        memcpy(pbData, pszLine, cbToCopy);
        if (cbToCopy >= *pcbLine)
        {
            *pcbLine = 0;
            *pszLine = '\0';
        }
        else
        {
            memmove(pszLine, &pszLine[cbToCopy], *pcbLine - cbToCopy);
            *pcbLine -= cbToCopy;
            pszLine[*pcbLine] = '\0';
        }
        offData += cbToCopy;
        if (offData >= cbData)
        {
            //LogRel6(("%s: copied all %#zx bytes from the line buffer (leaving %#zx)\n", __func__, cbToCopy, *pcbLine));
            return vbclWaylandDeserializeCacheReadDataCheckCrc(pbData, cbData, u64CrcExpected, fFormat);
        }
        //LogRel6(("%s: copied %#zx bytes from the line buffer (zero left)\n", __func__, cbToCopy));
    }

    /*
     * Read more data from the pipe.
     */
    for (;;)
    {
        size_t cbRead = 0;
        rc = vbclHandleReadNB(pHandleSrc, &pbData[offData], cbData - offData, &cbRead);
        //LogRel6(("%s: vbclHandleReadNB returns %Rrc cbRead=%#zx\n", __func__, rc, cbRead));
        if (RT_FAILURE(rc))
        {
            VBClLogError("read error for %#x at %#zx of %#zx: %Rrc\n", fFormat, offData, cbData, rc);
            return false;
        }
        offData += cbRead;
        if (offData >= cbData)
        {
            //LogRel6(("%s: done reading %#zx bytes, checking crc...\n", __func__, offData));
            return vbclWaylandDeserializeCacheReadDataCheckCrc(pbData, cbData, u64CrcExpected, fFormat);
        }

        /* Wait for more data to become available. */
        if (pHandleSrc->enmType != RTHANDLETYPE_FILE)
        {
            uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
            rc = vbclHandleSelectOne(pHandleSrc, cMsElapsed < cMsMax ? cMsMax - cMsElapsed : RT_MS_1SEC);
            if (RT_FAILURE(rc))
            {
                VBClLogError("vbclHandleSelectOne failed after %RX64 ms for %#x at %#zx of %#zx: %Rrc\n",
                             RTTimeMilliTS() - msStart, fFormat, offData, cbData, rc);
                return false;
            }
        }
    }
}


/**
 * Parses and validates a --clipboard-get data header line.
 */
static bool vbclClipboardDeserializeCacheParseOtherLine(const char *pszLine, uint32_t fFormats, uint32_t fCachedFmts,
                                                        uint32_t *pfFormat, uint32_t *pcbExpected, uint64_t *pu64CrcExpected)
{
    /* Data header (/ 2nd) line: 'Format=%#x cbData=%#x crc64=%#RX64' */
    static char const s_szItem1[] = "Format=0x";
    if (memcmp(pszLine, s_szItem1, sizeof(s_szItem1) - 1) != 0)
    {
        LogRel5(("%s: fail #1\n", __func__));
        return false;
    }
    pszLine += sizeof(s_szItem1) - 1;

    *pfFormat = 0;
    char *pszNext = NULL;
    int rc = RTStrToUInt32Ex(pszLine, &pszNext, 16, pfFormat);
    if (rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
    {
        LogRel5(("%s: fail #2 - rc=%Rrc\n", __func__, rc));
        return false;
    }

    pszLine = pszNext;
    while (RT_C_IS_BLANK(*pszLine))
        pszLine++;

    static char const s_szItem2[] = "cbData=0x";
    if (memcmp(pszLine, s_szItem2, sizeof(s_szItem2) - 1) != 0)
    {
        LogRel5(("%s: fail #3\n", __func__));
        return false;
    }
    pszLine += sizeof(s_szItem2) - 1;
    rc = RTStrToUInt32Ex(pszLine, &pszNext, 16, pcbExpected);
    if (rc != VWRN_TRAILING_SPACES && rc != VWRN_TRAILING_CHARS)
    {
        LogRel5(("%s: fail #4 - %Rrc\n", __func__, rc));
        return false;
    }

    pszLine = pszNext;
    while (RT_C_IS_BLANK(*pszLine))
        pszLine++;

    static char const s_szItem3[] = "crc64=0x";
    if (memcmp(pszLine, s_szItem3, sizeof(s_szItem3) - 1) != 0)
    {
        LogRel5(("%s: fail #5\n", __func__));
        return false;
    }
    pszLine += sizeof(s_szItem3) - 1;
    rc = RTStrToUInt64Full(pszLine, 16, pu64CrcExpected);
    if (RT_FAILURE(rc))
    {
        LogRel5(("%s: fail #6 - %Rrc\n", __func__, rc));
        return false;
    }

    /*
     * Additional checks.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    if (   *pfFormat == 0
        || !RT_IS_POWER_OF_TWO(*pfFormat))
        rcExit = VBClLogError("fFormat=%#x, expected non-zero power of two value\n", *pfFormat);
    else if (!(*pfFormat & fFormats))
        rcExit = VBClLogError("fFormat=%#x is not in advertised formats (%#x)\n", *pfFormat, fFormats);
    else if (*pfFormat & fCachedFmts)
        rcExit = VBClLogError("Already read fFormat=%#x\n", *pfFormat);

#if RT_ARCH_BITS == 32
    static size_t const s_cbMax = _64M;
#else
    static size_t const s_cbMax = _512M;
#endif
    if (*pcbExpected > s_cbMax)
        rcExit = VBClLogError("cbData=%#x, max is %#zx\n", *pcbExpected, s_cbMax);

    return rcExit == RTEXITCODE_SUCCESS;
}


/**
 * Parses and validates the initial --clipboard-get line.
 */
static bool vbclClipboardDeserializeCacheParseFirstLine(const char *pszLine, uint32_t *pfFormats)
{
    /* 1st line: 'Formats=%#x' */
    static char const s_szFirstLinePrefix[] = "Formats=0x";
    if (memcmp(pszLine, s_szFirstLinePrefix, sizeof(s_szFirstLinePrefix) - 1) != 0)
    {
        LogRel5(("%s: fail #1\n", __func__));
        return false;
    }

    int rc = RTStrToUInt32Full(&pszLine[sizeof(s_szFirstLinePrefix) - 1], 16, pfFormats);
    if (RT_FAILURE(rc))
    {
        LogRel5(("%s: fail #2 - %Rrc\n", __func__, rc));
        return false;
    }

    if (*pfFormats & ~VBOX_SHCL_FMT_VALID_MASK)
    {
        VBClLogError("fFormats=%#x is not valid (valid mask %#x)\n", *pfFormats, VBOX_SHCL_FMT_VALID_MASK);
        return false;
    }

    return true;
}


/**
 * Deserializes clipboard dump into the given cache and format mask.
 * @sa VBClClipboardSerializeCache
 */
int VBClClipboardDeserializeCache(PCRTHANDLE pHandleSrc, PSHCLCACHE pCache, SHCLFORMATS *pfFormats, RTMSINTERVAL cMsTimeout)
{
    *pfFormats = UINT32_MAX;
    ShClCacheInvalidate(pCache);

    char                    szLine[128];
    size_t                  offLine         = 0;
    uint32_t                fCachedFmts     = 0;
    uint64_t const          msStart         = RTTimeMilliTS();
    static uint32_t const   s_cMsMax        = RT_MS_30SEC;
    for (;;)
    {
        /*
         * Read more if we haven't got a full line.
         */
        char *pszNewline = strchr(szLine, '\n');
        if (!pszNewline)
        {
            size_t cbRead = 0;
            int rc = vbclHandleReadNB(pHandleSrc, &szLine[offLine], sizeof(szLine) - offLine - 1, &cbRead);
            if (  pHandleSrc->enmType != RTHANDLETYPE_FILE
                ? rc == VERR_BROKEN_PIPE
                : rc == VERR_EOF || (RT_SUCCESS(rc) && cbRead == 0))
            {
                if (offLine == 0)
                {
                    if (fCachedFmts == *pfFormats)
                        return VINF_SUCCESS;
                    return VBClLogError("%s: Incomplete. Received formats %#x, expected %#x!\n",
                                        __func__, fCachedFmts, *pfFormats);
                }
                return VBClLogError("%s: premature end of %s: %Rrc offLine=%#x %.*Rhxs\n", __func__,
                                    pHandleSrc->enmType != RTHANDLETYPE_FILE ? "steam" : "file", rc, offLine, offLine, szLine);
            }
            if (RT_FAILURE(rc))
                return VBClLogError("%s: read failed: %Rrc (offLine=%#x %.*Rhxs)\n", __func__, rc, offLine, offLine, szLine);

            offLine += cbRead; /* (cbRead can be zero with rc == VINF_TRY_AGAIN) */
            szLine[offLine] = '\0';

            pszNewline = strchr(szLine, '\n');
        }
        if (pszNewline)
        {
            /*
             * Terminate and parse the line.
             */
            *pszNewline = '\0';

            uint32_t   fFormat    = 0;
            uint32_t   cbData     = 0;
            uint64_t   u64Crc     = 0;
            bool const fFirstLine = *pfFormats == UINT32_MAX;
            bool const fRcParse   = fFirstLine
                                  ? vbclClipboardDeserializeCacheParseFirstLine(szLine, pfFormats)
                                  : vbclClipboardDeserializeCacheParseOtherLine(szLine, *pfFormats, fCachedFmts,
                                                                                &fFormat, &cbData, &u64Crc);
            if (!fRcParse)
                return VBClLogError("%s: bogus %s:\n%.*Rhxd\n", __func__, fFirstLine ? "1st line" : "data hdr", offLine, szLine);

            /* Shift the any content left in the line buffer to the start. */
            size_t const cbLine = &pszNewline[1] - szLine;
            memmove(szLine, &pszNewline[1], offLine - cbLine);
            offLine -= cbLine;
            szLine[offLine] = '\0';

            /*
             * If data header, read the data info the cache.
             */
            if (!fFirstLine)
            {
                if (!vbclWaylandDeserializeCacheReadData(pHandleSrc, szLine, &offLine, pCache,
                                                         fFormat, cbData, u64Crc, msStart, cMsTimeout))
                    return RTEXITCODE_FAILURE;
                fCachedFmts |= fFormat;
            }
        }
        /*
         * Overflow check.
         */
        else if (offLine + 1 >= sizeof(szLine))
            return VBClLogError("%s: bogus output:\n%.*Rhxd\n", __func__, offLine, szLine);
        /*
         * Wait for more data to become available.
         */
        else if (pHandleSrc->enmType != RTHANDLETYPE_FILE)
        {
            uint64_t const cMsElapsed = RTTimeMilliTS() - msStart;
            int rc = vbclHandleSelectOne(pHandleSrc, cMsElapsed < s_cMsMax ? s_cMsMax - cMsElapsed : RT_MS_1SEC);
            if (RT_FAILURE(rc))
                return VBClLogError("%s: RTPipeSelectOne failed after %RX64 ms: %Rrc\n", __func__, RTTimeMilliTS() - msStart, rc);
        }
    }
}


/**
 * Serializes the given cache and format mask to the an output handle.
 * @sa VBClClipboardDeserializeCache
 * @note The timeout is reused without adjustment for each write,
 *       so it is anything but accurate at the moment...
 */
int VBClClipboardSerializeCache(SHCLCACHE const *pCache, SHCLFORMATS fFormats, PCRTHANDLE pHandleDst, RTMSINTERVAL cMsTimeout)
{
#define CHECK_RC_RET(a_rc, a_MsgArgs) do { \
            if (RT_SUCCESS(rc)) break;\
            VBClLogError a_MsgArgs; \
            return rc; \
        } while (0)

    char szBuf[128];

    /* Print the header. */
    size_t cch = RTStrPrintf(szBuf, sizeof(szBuf), "Formats=%#010x\n", fFormats);
    int rc = vbclHandleWrite(pHandleDst, szBuf, cch, cMsTimeout);
    CHECK_RC_RET(rc, ("Initial write failed: %Rrc; rcExit=FAILURE\n", rc));

    /* Print the data for each format. */
    if (fFormats)
        for (uint32_t i = 0; i < RT_ELEMENTS(pCache->aEntries); i++)
            if (RT_BIT_32(i) & fFormats)
            {
                cch = RTStrPrintf(szBuf, sizeof(szBuf), "Format=%#010x cbData=%#010x crc64=%#018RX64\n",
                                  RT_BIT_32(i), pCache->aEntries[i].cbData,
                                  RTCrc64(pCache->aEntries[i].pvData, pCache->aEntries[i].cbData));
                rc = vbclHandleWrite(pHandleDst, szBuf, cch, cMsTimeout);
                CHECK_RC_RET(rc, ("Writing header for #%u failed: %Rrc; rcExit=FAILURE\n", i, rc));

                rc = vbclHandleWrite(pHandleDst, pCache->aEntries[i].pvData, pCache->aEntries[i].cbData, cMsTimeout);
                CHECK_RC_RET(rc, ("Writing data for #%u failed: %Rrc; rcExit=FAILURE\n", i, rc));
            }

#undef CHECK_RC_RET
    return rc;
}


/**
 * @interface_method_impl{SHCLWAYLANDCTX,pfnDataOfferComplete}
 */
static DECLCALLBACK(void) vbclCmdClipbordGet_DataOfferComplete(PSHCLWAYLANDCTX pWlCtx, SHCLWLOFFERCOMPLETE enmReason)
{
    VBCLCMDCLIPBOARDSTATE *pThis = RT_FROM_MEMBER(pWlCtx, VBCLCMDCLIPBOARDSTATE, Wl);
    LogRel2(("%s: enmReason=%d; fShutdown %d -> 1; rcExit=%d\n", __func__, enmReason, pThis->fShutdown, pThis->rcExit));

    /*
     * Translate completion reason to exit code, adjust the cached content
     * and output so the parent got something to work with.
     */
    if (enmReason == SHCLWLOFFERCOMPLETE_REPORTED)
        pThis->rcExit = RTEXITCODE_SUCCESS;
    else
    {
        if (enmReason == SHCLWLOFFERCOMPLETE_OUR_OWN)
            pThis->rcExit = RTEXITCODE_SKIPPED;
        else
            pThis->rcExit = RTEXITCODE_FAILURE;

        /* Make sure we return fFormats=0 to avoid confusion in the logging, as
           the parent is reading the pipe first before checking the process status. */
        pThis->Wl.fOurFormats = 0;
        ShClCacheInvalidate(&pThis->Wl.OurCache);
    }

    RTHANDLE hStdOut;
    int rc = RTHandleGetStandard(RTHANDLESTD_OUTPUT, false /*fLeaveOpen*/, &hStdOut);
    if (RT_SUCCESS(rc))
    {
        rc = VBClClipboardSerializeCache(&pThis->Wl.OurCache, pThis->Wl.fOurFormats, &hStdOut, RT_INDEFINITE_WAIT);
        if (RT_SUCCESS(rc))
        {
            rc = vbclHandleClose(&hStdOut);
            if (RT_FAILURE(rc))
                VBClLogError("failed to properly close stdout: %Rrc\n", rc);
        }
    }
    else
        VBClLogError("RTHandleGetStandard failed for stdout: %Rrc\n", rc);
    if (RT_FAILURE(rc))
        pThis->rcExit = RTEXITCODE_FAILURE;

    /*
     * Ensure that the event loop exits.
     */
    pThis->fShutdown = true;

    size_t cbWritten = 0;
    rc = RTPipeWrite(pThis->hPipeWrite, "Q", 1, &cbWritten);
    AssertLogRelMsg(RT_SUCCESS(rc) && cbWritten == 1, ("rc=%Rrc cbWritten=%zu\n", rc, cbWritten));

    LogRel2(("%s:  rcExit=%d\n", __func__, pThis->rcExit));
}


/**
 * --clipboard-get
 */
static DECLCALLBACK(RTEXITCODE) vbclCmdClipboardGet(void)
{
    VBClLogCreateNoStdOut();

    /*
     * Only wayland for now.
     */
    VBGHDISPLAYSERVERTYPE const enmDisplayServerType = VBClGetDisplayServerTypeResolveAuto();
    if (   enmDisplayServerType != VBGHDISPLAYSERVERTYPE_PURE_WAYLAND
        && enmDisplayServerType != VBGHDISPLAYSERVERTYPE_XWAYLAND)
        return VBClLogError("%s: Only available under Wayland.\n");
    int rc = VBClExplicitLoadClientLibrariesForDisplayServer(enmDisplayServerType, true /*fXWaylandAsPureWayland*/);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /*
     * Init the wayland clipboard machinery (connects to wayland and finds the
     * necessary global objects).
     */
    VBCLCMDCLIPBOARDSTATE State = { RTEXITCODE_SKIPPED, false, NIL_RTPIPE, NIL_RTPIPE };
    RTERRINFOSTATIC       ErrInfo;
    rc = VbghWaylandClipboardInit(&State.Wl, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        VBClLogVerbose(2, "VbghWaylandClipboardInit succeeded. protocol %s\n", State.Wl.pszProtocol);
        rc = RTPipeCreate(&State.hPipeRead, &State.hPipeWrite, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            State.Wl.pfnDataOfferComplete = vbclCmdClipbordGet_DataOfferComplete;

            /*
             * To get the clipboard content, register the listening notifications.
             */
            rc = VbghWaylandClipboardSetupListening(&State.Wl, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc))
            {
                VBClLogVerbose(2, "VbghWaylandClipboardSetupListening succeeded.\n");

                /* Popup? */
                if (State.Wl.enmProtocol == SHCLWAYLANDPROTO_WL)
                {
                    rc = VbghWaylandPopupShow(&State.Wl.Popup, State.Wl.pSeatEntry, "VBoxClient-clipboard-get",
                                              "org.virtualbox.VBoxClient.clipboard.get", NULL, 0, RTErrInfoInitStatic(&ErrInfo));
                    if (RT_FAILURE(rc))
                        VBClLogError("VbghWaylandPopupShow failed: %Rrc%#RTeim\n", rc, &ErrInfo.Core);
                }

                VbghWaylandRunloopForDisplay(State.Wl.GhCore.pDisplay, State.hPipeRead, NULL, NULL, NIL_RTPIPE,
                                             RT_SUCCESS(rc) ? RT_MS_5SEC : RT_MS_1SEC, &State.fShutdown);

                VBClLogVerbose(2, "done (rcExit=%d).\n", State.rcExit);
                return State.rcExit;
            }
            VBClLogError("VbghWaylandClipboardSetupListening failed %Rrc%#RTeim\n", rc, &ErrInfo.Core);
        }
        VbghWaylandClipboardTerm(&State.Wl);
    }
    else
        VBClLogError("VbghWaylandClipboardInit failed %Rrc%#RTeim\n", rc, &ErrInfo.Core);
    return RTEXITCODE_FAILURE;
}

/** --clipboard-get */
VBCLCOMMAND const g_CmdClipboardGet =
{
    /* .pszName = */        "--clipboard-get",
    /* .pszDesc = */        "",
    /* .pszLogPrefix = */   "cl-get:",
    /* .pszOptions = */     NULL,
    /* .pfnOption = */      NULL,
    /* .pfnExecute = */     vbclCmdClipboardGet,
};



/** The termination pipe for the --clipboard-set. */
static RTPIPE g_hPipeTerm = NIL_RTPIPE;


/**
 * @callback_method_impl{FNVBCLOPTPARSE}
 */
static DECLCALLBACK(int) vbclCmdClipboardSetOption(PRTGETOPTSTATE pGetOptState)
{
    static RTGETOPTDEF const s_aOptions[] =
    {
        { "--termination-pipe", 'T', RTGETOPT_REQ_UINT32 },
    };

    RTGETOPTUNION ValueUnion;
    int rcGetOpt = RTGetOptEx(pGetOptState, &ValueUnion, s_aOptions, RT_ELEMENTS(s_aOptions));
    if (rcGetOpt == 'T')
    {
        g_hPipeTerm = NIL_RTPIPE;
        int rc = RTPipeFromNative(&g_hPipeTerm, ValueUnion.u32, RTPIPE_N_READ);
        if (RT_FAILURE(rc))
            return RTMsgErrorRc(VERR_GENERAL_FAILURE,
                                "RTPipeFromNative failed on %u (%#x): %Rrc\n", ValueUnion.u32, ValueUnion.u32, rc);
        return VINF_SUCCESS;
    }
    return rcGetOpt;
}


/**
 * @interface_method_impl{SHCLWAYLANDCTX,pfnDataSourceCancelled}
 */
static DECLCALLBACK(void) vbclCmdClipbordSet_DataSourceCancelled(PSHCLWAYLANDCTX pWlCtx)
{
    VBCLCMDCLIPBOARDSTATE *pThis = RT_FROM_MEMBER(pWlCtx, VBCLCMDCLIPBOARDSTATE, Wl);
    LogRel2(("%s: fShutdown %d -> 1; rcExit=%d\n", __func__, pThis->fShutdown, pThis->rcExit));
    pThis->fShutdown = true;

    size_t cbWritten = 0;
    int rc = RTPipeWrite(pThis->hPipeWrite, "Q", 1, &cbWritten);
    AssertLogRelMsg(RT_SUCCESS(rc) && cbWritten == 1, ("rc=%Rrc cbWritten=%zu\n", rc, cbWritten));
}


/**
 * --clipboard-set
 */
static DECLCALLBACK(RTEXITCODE) vbclCmdClipboardSet(void)
{
    VBClLogCreateNoStdOut();

    /*
     * Only wayland for now.
     */
    VBGHDISPLAYSERVERTYPE const enmDisplayServerType = VBClGetDisplayServerTypeResolveAuto();
    if (   enmDisplayServerType != VBGHDISPLAYSERVERTYPE_PURE_WAYLAND
        && enmDisplayServerType != VBGHDISPLAYSERVERTYPE_XWAYLAND)
        return VBClLogError("%s: Only available under Wayland.\n");
    int rc = VBClExplicitLoadClientLibrariesForDisplayServer(enmDisplayServerType, true /*fXWaylandAsPureWayland*/);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;

    /*
     * Init the basic wayland clipboard machinery.
     */
    VBCLCMDCLIPBOARDSTATE State = { RTEXITCODE_FAILURE, false, NIL_RTPIPE, NIL_RTPIPE };
    RTERRINFOSTATIC       ErrInfo;
    rc = VbghWaylandClipboardInit(&State.Wl, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(rc))
    {
        VBClLogVerbose(2, "VbghWaylandClipboardInit succeeded. protocol %s\n", State.Wl.pszProtocol);
        rc = RTPipeCreate(&State.hPipeRead, &State.hPipeWrite, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            /*
             * Read the content into the host (other) cache so we're ready to offer
             * clipboard data as we bring up the wayland bits.
             */
            RTHANDLE hStdIn;
            rc = RTHandleGetStandard(RTHANDLESTD_INPUT, false /*fLeaveOpen*/, &hStdIn);
            if (RT_SUCCESS(rc))
            {
                rc = VBClClipboardDeserializeCache(&hStdIn, &State.Wl.OtherCache, &State.Wl.fOtherFormats, RT_MS_30SEC);
                if (RT_SUCCESS(rc))
                {
                    uint64_t const uRevision = (State.Wl.uRevision + 2) & ~(uint64_t)1; /* even numbers for host ownership */
                    State.Wl.uRevision = uRevision;
                    Assert(SHCLWLCTX_REV_IS_OTHER(State.Wl.uRevision));

                    /*
                     * Setup and make the offering.
                     */
                    rc = VbghWaylandClipboardSetupOffer(&State.Wl, RTErrInfoInitStatic(&ErrInfo));
                    if (RT_SUCCESS(rc))
                    {
                        VBClLogVerbose(2, "VbghWaylandClipboardSetupOffer succeeded.\n");

                        State.Wl.pfnDataSourceCancelled = vbclCmdClipbordSet_DataSourceCancelled;
                        State.rcExit = RTEXITCODE_SUCCESS; /** @todo some 'offered' callback for setting rcExit = success? */

                        rc = VbghWaylandClipboardMakeDataOffering(&State.Wl, "VBoxClient-clipboard-get",
                                                                  "org.virtualbox.VBoxClient.clipboard.get",
                                                                  RTErrInfoInitStatic(&ErrInfo));
                        if (RT_SUCCESS(rc))
                        {
                            VbghWaylandRunloopForDisplay(State.Wl.GhCore.pDisplay, State.hPipeRead, NULL, NULL, g_hPipeTerm,
                                                         RT_SUCCESS(rc) ? RT_MS_5SEC : RT_MS_1SEC, &State.fShutdown);

                            VBClLogVerbose(2, "done (rcExit=%d).\n", State.rcExit);
                            return State.rcExit;
                        }
                        VBClLogError("VbghWaylandClipboardMakeDataOffering failed %Rrc%#RTeim\n", rc, &ErrInfo.Core);
                    }
                    else
                        VBClLogError("VbghWaylandClipboardSetupOffer failed %Rrc%#RTeim\n", rc, &ErrInfo.Core);
                }
                else
                    VBClLogError("VBClClipboardDeserializeCache failed: %Rrc\n", rc);
            }
            else
                VBClLogError("RTHandleGetStandard failed for stdin: %Rrc\n", rc);
        }
        VbghWaylandClipboardTerm(&State.Wl);
    }
    else
        VBClLogError("VbghWaylandClipboardInit failed %Rrc%#RTeim\n", rc, &ErrInfo.Core);
    return RTEXITCODE_FAILURE;
}

/** --clipboard-set */
VBCLCOMMAND const g_CmdClipboardSet =
{
    /* .pszName = */        "--clipboard-set",
    /* .pszDesc = */        "",
    /* .pszLogPrefix = */   "cl-set:",
    /* .pszOptions = */     NULL,
    /* .pfnOption = */      vbclCmdClipboardSetOption,
    /* .pfnExecute = */     vbclCmdClipboardSet,
};

