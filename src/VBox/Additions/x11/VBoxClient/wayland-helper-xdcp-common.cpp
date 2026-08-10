/* $Id: wayland-helper-xdcp-common.cpp 114745 2026-07-21 18:40:35Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Common code for Data Control Protocol (DCP) family helper for Wayland.
 *
 * @note Obsolete. Compiled with 'kmk VBOX_WITH_WAYLAND_ADDITIONS_LEGACY=1'.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#include <errno.h>

#include <iprt/env.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/GuestHost/mime-type-converter.h>

#include "VBoxClient.h"
#include "clipboard.h"
#include "wayland-helper.h"
#include "wayland-helper-ipc.h"
#include "wayland-helper-xdcp-common.h"

#include "wayland-client-protocol.h"


/**********************************************************************************************************************************
 * Wayland low level operations.
 *********************************************************************************************************************************/


/**
 * A helper function which reallocates buffer to bigger size.
 *
 * This function will attempt to re-allocate specified buffer by cbChunk bytes.
 * If failed, caller is responsible for freeing input buffer. On success, output
 * buffer must be freed by caller.
 *
 * @returns IPRT status code.
 * @param   pvBufIn             Previously allocated buffer which size needs to be increased.
 * @param   cbBufIn             Size of input buffer.
 * @param   cbChunk             Amount of bytes by which caller wants to increase buffer size.
 * @param   cbMax               Maximum size of output buffer.
 * @param   ppBufOut            Output buffer (must be freed by caller).
 * @param   pcbBufOut           Size of output buffer.
 */
static int vbcl_wayland_hlp_dcp_grow_buffer(void *pvBufIn, size_t cbBufIn, size_t cbChunk, size_t cbMax,
                                            void **ppBufOut, size_t *pcbBufOut)
{
    int rc = VERR_NO_MEMORY;

    /* How many chunks were already added to the buffer. */
    int cChunks = cbBufIn / cbChunk;
    /* Size of a chunk to be added to already allocated buffer. */
    size_t cbCurrentChunk = 0;

    AssertPtrReturn(pvBufIn, VERR_INVALID_PARAMETER);
    AssertReturn(cbBufIn > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cbChunk > 0, VERR_INVALID_PARAMETER);
    AssertReturn(cbMax > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppBufOut, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbBufOut, VERR_INVALID_PARAMETER);

    if (cbBufIn < cbMax)
    {
        void *pvBuf;

        /* Calculate size of a chunk which can be added to already allocated memory
         * in a way that resulting buffer size will not exceed cbMax. Always add
         * the extra '\0' byte to the end of allocated area for safety reasons. */
        cbCurrentChunk = RT_MIN(cbMax, cbChunk * (cChunks + 1)) - cbBufIn + 1;
        pvBuf = RTMemReallocZ(pvBufIn, cbBufIn, cbBufIn + cbCurrentChunk);
        if (RT_VALID_PTR(pvBuf))
        {
            LogRel(("Wayland: buffer size increased from %u to %u bytes\n", cbBufIn, cbBufIn + cbCurrentChunk));
            *ppBufOut = pvBuf;
            *pcbBufOut = cbBufIn + cbCurrentChunk;
            rc = VINF_SUCCESS;
        }
        else
        {
            LogRel(("Wayland: unable to allocate buffer of size of %u bytes: no memory\n", cbBufIn + cbCurrentChunk));
            rc = VERR_NO_MEMORY;
        }
    }
    else
    {
        LogRel(("Shared Clipboard: unable to re-allocate buffer: size of %u bytes exceeded\n", cbMax));
        rc = VERR_BUFFER_OVERFLOW;
    }

    return rc;
}

/**
 * A helper function for reading from file descriptor until EOF.
 *
 * Reads clipboard data from Wayland via file descriptor.
 *
 * @returns IPRT status code.
 * @param   fd                  A file descriptor to read data from.
 * @param   ppvBuf              Newly allocated output buffer (must be freed by caller).
 * @param   pcbBuf              Size of output buffer.
 */
static int vbcl_wayland_hlp_dcp_read_wl_fd(int fd, void **ppvBuf, size_t *pcbBuf)
{
    int rc = VERR_NO_MEMORY;

    struct timeval tv;
    fd_set rfds;

    /* Amount of payload actually read from Wayland fd in bytes. */
    size_t cbDst = 0;
    /* Dynamically growing buffer to store Wayland clipboard. */
    void *pvDst = NULL;
    /* Number of bytes currently allocated to read entire
     * Wayland buffer content (actual size of pvDst). */
    size_t cbGrowingBuffer = 0;
    /* Number of bytes read from Wayland fd per attempt. */
    ssize_t cbRead = 0;

    AssertPtrReturn(ppvBuf, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbBuf, VERR_INVALID_PARAMETER);

    /* Start with allocating one chunk and grow buffer later if needed. */
    cbGrowingBuffer = VBOX_WAYLAND_BUFFER_CHUNK_INC_SIZE + 1 /* '\0' */;
    pvDst = RTMemAllocZ(cbGrowingBuffer);
    if (RT_VALID_PTR(pvDst))
    {
        /* Read everything from given fd. */
        while (1)
        {
            tv.tv_sec  = 0;
            tv.tv_usec = VBCL_WAYLAND_IO_TIMEOUT_MS * 1000;

            FD_ZERO(&rfds);
            FD_SET(fd, &rfds);

            /* Wait until data is available. */
            if (select(fd + 1, &rfds, NULL, NULL, &tv) > 0)
            {
                /* Check if backing buffer size is big enough to store one more data chunk
                 * read from fd. If not, try to increase buffer by size of chunk x 2. */
                if (cbDst + VBOX_WAYLAND_BUFFER_CHUNK_SIZE > cbGrowingBuffer)
                {
                    void *pBufTmp = NULL;

                    rc = vbcl_wayland_hlp_dcp_grow_buffer(
                        pvDst, cbGrowingBuffer, VBOX_WAYLAND_BUFFER_CHUNK_INC_SIZE,
                        VBOX_WAYLAND_BUFFER_MAX, &pBufTmp, &cbGrowingBuffer);

                    if (RT_FAILURE(rc))
                    {
                        RTMemFree(pvDst);
                        break;
                    }
                    else
                        pvDst = pBufTmp;
                }

                /* Read all data from fd until EOF. */
                cbRead = read(fd, (void *)((uint8_t *)pvDst + cbDst), VBOX_WAYLAND_BUFFER_CHUNK_SIZE);
                if (cbRead < 0)
                {
                    rc = RTErrConvertFromErrno(errno);
                    RTMemFree(pvDst);
                    break;
                }
                else if (cbRead > 0)
                {
                    VBClLogVerbose(5, "Wayland: read chunk of %d bytes from Wayland\n", cbRead);
                    cbDst += (size_t)cbRead;
                }
                else
                {
                    /* EOF has been reached. */
                    VBClLogVerbose(5, "Wayland: read %u bytes from Wayland\n", cbDst);

                    if (cbDst > 0)
                    {
                        rc = VINF_SUCCESS;
                        *ppvBuf = pvDst;
                        *pcbBuf = cbDst;
                    }
                    else
                    {
                        rc = VERR_NO_DATA;
                        RTMemFree(pvDst);
                    }

                    break;
                }
            }
            else
            {
                rc = VERR_TIMEOUT;
                RTMemFree(pvDst);
                break;
            }
        }
    }

    return rc;
}

/**
 * A helper function for writing to a file descriptor provided by Wayland.
 *
 * @returns IPRT status code.
 * @param   fd                  A file descriptor to write data to.
 * @param   pvBuf               Data buffer.
 * @param   cbBuf               Size of data buffer.
 */
static int vbcl_wayland_hlp_dcp_write_wl_fd(int fd, void *pvBuf, size_t cbBuf)
{
    AssertPtrReturn(pvBuf, VERR_INVALID_PARAMETER);
    AssertReturn(cbBuf > 0, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    size_t cbWritten = 0;
    for (;;)
    {
        /* Wait until data is available. */
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = VBCL_WAYLAND_IO_TIMEOUT_MS * 1000;

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);

        /** @todo r=bird: why not use poll? */
        if (select(fd + 1, NULL, &wfds, NULL, &tv) > 0)
        {
            if (FD_ISSET(fd, &wfds))
            {
                /** @todo r=bird: There is no explanation why we're selecting/polling the fd
                 *        before writing. Iff fd is blocking, we may easily block here writing
                 *        large blocks of data that exceeds the pipe buffer size. */
                ssize_t const cbTmp = write(fd, (void *)((uint8_t *)pvBuf + cbWritten), cbBuf - (size_t)cbWritten);
                if (cbTmp > 0)
                {
                    cbWritten += (size_t)cbTmp;
                    if (cbWritten >= cbBuf)
                    {
                        VBClLogVerbose(5, "Wayland: wrote %zu bytes to Wayland\n", cbWritten);
                        break;
                    }
                    VBClLogVerbose(6, "Wayland: wrote chunk of %zd bytes to Wayland\n", cbTmp);
                }
                else
                {
                    if (cbTmp < 0)
                        rc = RTErrConvertFromErrno(errno);
                    else /*if (cbWritten != cbBuf) */
                    {
                        /** @todo r=bird: cbTmp == 0 here, which is unlikely in the extreme given the
                         *        select, even for a non-blocking 'fd'. Treating anything here as error
                         *        is one way to go, but it doesn't seem quite correct... */
                        VBClLogVerbose(5, "Wayland: wrote %u bytes (out of %u) to Wayland (partial write)\n",
                                       cbWritten, cbBuf);
                        rc = VERR_WRITE_ERROR;
                    }
                    /*else
                        VBClLogVerbose(5, "Wayland: zero write detected\n"); - bird: impossible */
                    break;
                }
            }
            else
            {
                VBClLogError("cannot write fd\n");
                rc = VERR_PIPE_NOT_CONNECTED;
                break;
            }
        }
        else
        {
            rc = VERR_TIMEOUT;
            break;
        }
    }

    return rc;
}

/**
 * Read the next event from Wayland compositor.
 *
 * Implements custom reader function which can be interrupted
 * on service termination request.
 *
 * @returns IPRT status code.
 * @param   pCtx                Context data.
 */
int vbcl_wayland_xdcp_next_event(vbox_wl_xdcp_base_ctx_t *pCtx)
{
    int rc = VINF_SUCCESS;

    struct timeval tv;
    fd_set rfds, efds;
    int fd;

    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);

    /* Instead of using wl_display_dispatch() directly, implement
     * custom event loop handling as recommended in Wayland documentation.
     * Thus, we can have a control over Wayland fd polling and in turn
     * can request event loop thread to shutdown when needed. */

    tv.tv_sec  = 0;
    tv.tv_usec = VBCL_WAYLAND_IO_TIMEOUT_MS * 1000;

    fd = wl_display_get_fd(pCtx->pDisplay);

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);

    FD_ZERO(&efds);
    FD_SET(fd, &efds);

    while (wl_display_prepare_read(pCtx->pDisplay) != 0)
        wl_display_dispatch(pCtx->pDisplay);

    wl_display_flush(pCtx->pDisplay);

    if (select(fd + 1, &rfds, NULL, &efds, &tv) > 0)
        wl_display_read_events(pCtx->pDisplay);
    else
    {
        wl_display_cancel_read(pCtx->pDisplay);
        rc = VERR_TIMEOUT;
    }

    wl_display_dispatch_pending(pCtx->pDisplay);

    return rc;
}

/**
 * Release session resources.
 *
 * @param   pCtx            Context data.
 */
static void vbcl_wayland_xdcp_session_release(vbox_wl_xdcp_base_ctx_t *pCtx)
{
    AssertPtrReturnVoid(pCtx);
}

/**
 * Initialize session.
 *
 * @param   pSession        Session data.
 */
static void vbcl_wayland_xdcp_session_init(vbox_wl_dcp_session_t *pSession)
{
    AssertPtrReturnVoid(pSession);

    pSession->clip.fFmts.init(VBOX_SHCL_FMT_NONE, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
}

void vbcl_wayland_xdcp_session_prepare(vbox_wl_xdcp_base_ctx_t *pCtx)
{
    AssertPtrReturnVoid(pCtx);

    vbcl_wayland_xdcp_session_release(pCtx);
    vbcl_wayland_xdcp_session_init(&pCtx->Session);
}

int VBClWaylandXdcpCtxInit(vbox_wl_xdcp_base_ctx_t *pCtx)
{
    pCtx->Thread = NIL_RTTHREAD;
    pCtx->fShutdown = false;
    pCtx->fIngnoreWlClipIn = false;
    pCtx->fSendToGuest.init(false, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pCtx->pShClCtx = NULL;
    pCtx->pDisplay = NULL;
    pCtx->pRegistry = NULL;
    pCtx->pSeat = NULL;

    vbcl_wayland_xdcp_session_init(&pCtx->Session);
    vbcl_wayland_session_init(&pCtx->Session.Base); /** @todo illogical, fix later */

    return VINF_SUCCESS;
}

void VBClWaylandXdcpCtxReInit(vbox_wl_xdcp_base_ctx_t *pCtx)
{
    pCtx->Thread = NIL_RTTHREAD;
    pCtx->fShutdown = false;
    pCtx->fIngnoreWlClipIn = false;
    pCtx->fSendToGuest.init(false, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pCtx->pShClCtx = NULL;
    pCtx->pDisplay = NULL;
    pCtx->pRegistry = NULL;
    pCtx->pSeat = NULL;

    vbcl_wayland_xdcp_session_init(&pCtx->Session);
}

void VBClWaylandXdcpCtxTerm(vbox_wl_xdcp_base_ctx_t *pCtx)
{
    pCtx->Thread = NIL_RTTHREAD;
    pCtx->fShutdown = false;
    pCtx->fIngnoreWlClipIn = false;
    pCtx->fSendToGuest.init(false, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pCtx->pShClCtx = NULL;
    pCtx->pDisplay = NULL;
    pCtx->pRegistry = NULL;
    pCtx->pSeat = NULL;

    vbcl_wayland_xdcp_session_release(pCtx);
    vbcl_wayland_xdcp_session_init(&pCtx->Session); /** @todo needed? */
}

/**
 * Reads guest clipboard data into our cache entry for @a uVBoxFmt.
 *
 * @returns VBox status code.
 * @param   pShClCtx            The VBoxClient Shared Clipboard context.
 * @param   fdSrc               The source file descriptor (pipe).
 * @param   idxVBoxFmt          The log2 of the VBox format / cache index.
 * @param   uRevision           The state/cache revision.
 * @param   pszMimeType         The MIME type being read (for logging).
 */
static int vbclWaylandXdcpReadGuestClipboardDataIntoCache(PSHCLCONTEXT pShClCtx, int fdSrc, unsigned idxVBoxFmt,
                                                          uint64_t uRevision, const char *pszMimeType)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pShClCtx, VERR_INVALID_POINTER);
    AssertReturn(idxVBoxFmt < RT_ELEMENTS(pShClCtx->Wl.aOurMimeTypes), VERR_INVALID_PARAMETER);
    SHCLFORMAT const fVBoxFmt = RT_BIT_32(idxVBoxFmt);
    AssertReturn(fdSrc >= 0, VERR_INVALID_HANDLE);

    /*
     * Read the stuff into a temporary buffer (allocated by worker).
     */
    void  *pvSrcData = NULL;
    size_t cbSrcData = 0;
    int rc = vbcl_wayland_hlp_dcp_read_wl_fd(fdSrc, &pvSrcData, &cbSrcData);
    if (RT_SUCCESS(rc))
    {
        /*
         * Convert it into VBox format.
         */
        void  *pvVBoxData = NULL;
        size_t cbVBoxData = 0;
        rc = VbghMimeConvToVBox(pszMimeType, pvSrcData, cbSrcData, &pvVBoxData, &cbVBoxData);
        if (RT_SUCCESS(rc))
        {
            /*
             * Enter it into the cache.
             */
            RTCritSectEnter(&pShClCtx->Wl.CritSect);
            if (pShClCtx->Wl.uRevision == uRevision)
            {
                /** @todo one copy too many here, but it has the benefit of heap api
                 * consistency (e.g. drop-in electric heap fencing in a source file). */
                rc = ShClCacheSet(&pShClCtx->Wl.OurCache, fVBoxFmt, pvVBoxData, cbVBoxData);
                RTCritSectLeave(&pShClCtx->Wl.CritSect);
                if (RT_SUCCESS(rc))
                    VBClLogVerbose(5, "Put %zu bytes into cache for %#x (from %s)\n", cbVBoxData, fVBoxFmt, pszMimeType);
                else
                    VBClLogVerbose(2, "Failed to put %zu bytes into cache for %#x (from %s): %Rrc\n",
                                   cbVBoxData, fVBoxFmt, pszMimeType, rc);
            }
            else
            {
                rc = VERR_VERSION_MISMATCH;
                RTCritSectLeave(&pShClCtx->Wl.CritSect);
                VBClLogVerbose(2, "Failed to put %zu bytes into cache for %#x (from %s): version changed\n",
                               cbVBoxData, fVBoxFmt, pszMimeType);
            }
            RTMemFree(pvVBoxData);
        }
        else
            VBClLogError("Failed to convert %zu bytes of clipboard data from %s to VBox format %#x: %Rrc\n",
                         cbSrcData, pszMimeType, rc);
        RTMemFree(pvSrcData);
    }
    return rc;
}

/**
 * @callback_method_impl{FNVBCLWAYLANDSESSIONJOIN,
 *      Session callback: Advertise clipboard to the host.}
 *
 * This callback must be executed in context of Wayland event thread
 * in order to be able to access Wayland clipboard content.
 *
 * This callback (1) coverts Wayland clipboard formats into VBox
 * representation, (2) sets formats to the session, (3) waits for
 * host to request clipboard data in certain format, and (4)
 * receives Wayland clipboard in requested format.
 */
DECLCALLBACK(int) VBClWaylandXdcpFillOurCacheFromOfferAndReport(void *pvUser)
{
    /*
     * Validate input.
     */
    VBCL_LOG_CALLBACK;
    VBCLWLHLP_XDCP_FILL_OUR_CACHE_FROM_OFFER_AND_REPORT_ARGS_T * const pArgs
        = (VBCLWLHLP_XDCP_FILL_OUR_CACHE_FROM_OFFER_AND_REPORT_ARGS_T *)pvUser;
    AssertPtrReturn(pArgs, VERR_INVALID_POINTER);
    PSHCLCONTEXT const pShClCtx = pArgs->pShClCtx;
    AssertPtrReturn(pShClCtx, VERR_INVALID_POINTER);

    /*
     * Get the data for each VBox format.
     */
    int rcRet = VINF_SUCCESS;
    RTCritSectEnter(&pShClCtx->Wl.CritSect);
    SHCLFORMATS fOurFormats  = pShClCtx->Wl.fOurFormats;
    SHCLFORMATS fFormatsLeft = fOurFormats;
    while (fFormatsLeft && pShClCtx->Wl.uRevision == pShClCtx->Wl.uRevision)
    {
        /* Get the index of the first format in the mask. */
        AssertCompile(sizeof(fFormatsLeft) == sizeof(uint32_t));
        unsigned const idxFmt = ASMBitFirstSetU32(fFormatsLeft) - 1;
        AssertBreak(idxFmt < 32); /* paranoia */
        SHCLFORMAT const fThisFmt = RT_BIT_32(idxFmt);

        /* It simplifies error handling if we remove it early. */
        fFormatsLeft &= ~fThisFmt;

        /* Get the mime type before leaving the critsect. */
        const char * const pszMimeType = pShClCtx->Wl.aOurMimeTypes[idxFmt].pszMimeType;
        AssertContinue(pszMimeType);

        RTCritSectLeave(&pShClCtx->Wl.CritSect);

        /* Read the data for this MIME type. */
        int rc;
        int aFds[2] = {-1,-1};
        if (pipe(aFds) == 0)
        {
            pArgs->pfnOfferReceive(pArgs, pszMimeType, aFds[1]);
            close(aFds[1]);

            /* ?? */
            wl_display_flush(pArgs->pDisplay);

            rc = vbclWaylandXdcpReadGuestClipboardDataIntoCache(pShClCtx, aFds[0], idxFmt, pArgs->uRevision, pszMimeType);
            close(aFds[0]);
        }
        else
        {
            rc = RTErrConvertFromErrno(errno);
            VBClLogError("vbclWaylandHlpEdcpGhClipFillCacheAndReport: pipe failed: %Rrc\n", rc);
        }

        if (RT_FAILURE(rc))
        {
            fOurFormats &= ~fThisFmt;
            VBClLogVerbose(2, "vbclWaylandHlpEdcpGhClipFillCacheAndReport: dropping %#x due to %Rrc\n", fThisFmt, rc);
            rcRet = rc;
        }

        RTCritSectEnter(&pShClCtx->Wl.CritSect);
    }

    /*
     * Now that we've cached all the data, report it to the host if
     * everything is still fine.
     */
    if (pShClCtx->Wl.uRevision == pShClCtx->Wl.uRevision)
    {
        pShClCtx->Wl.fOurFormats = fOurFormats;
        int rc = RTSemEventMultiSignal(pShClCtx->Wl.hOurCacheFilledEvent);
        if (RT_FAILURE(rc))
        {
            VBClLogError("vbclWaylandHlpEdcpGhClipFillCacheAndReport: RTSemEventMultiSignal failed: %Rrc\n", rc);
            rcRet = rc;
        }

        rc = VbglR3ClipboardReportFormats(pShClCtx->CmdCtx.idClient, fOurFormats);
        if (RT_SUCCESS(rc))
            VBClLogVerbose(2, "announced fFmts=0x%x to the host\n", fOurFormats);
        else
        {
            VBClLogError("vbclWaylandHlpEdcpGhClipFillCacheAndReport: VbglR3ClipboardReportFormats/%#x failed: %Rrc\n",
                         fOurFormats, rc);
            rcRet = rc;
        }
    }

    RTCritSectLeave(&pShClCtx->Wl.CritSect);

    VBClLogVerbose(5, "vbclWaylandHlpEdcpGhClipFillCacheAndReport/%#x returns: %Rrc\n", fOurFormats, rcRet);
    return rcRet;
}

/**
 * Write clipboard data to Wayland.
 *
 * @returns IPRT status code.
 * @param   fd                  File descriptor provided by Wayland to write data to.
 * @param   pCtx                Context data.
 * @param   pcszMimeType        Clipboard data format in string representation.
 */
int VBClWaylandXdcpSetGuestClipboard(int fd, vbox_wl_xdcp_base_ctx_t *pCtx, const char *pcszMimeType)
{
    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcszMimeType, VERR_INVALID_PARAMETER);

    int rc;
    PSHCLCONTEXT const pShClCtx = pCtx->pShClCtx;
    if (RT_VALID_PTR(pShClCtx))
    {
        if (RTStrICmpAscii(pcszMimeType, VBOX_CLIPBOARD_MIME_TYPE_REVISION_NO) == 0)
        {
            uint64_t uRevision = pShClCtx->Wl.uRevision;
            if (SHCLWLCTX_REV_IS_OTHER(uRevision))
                rc = vbcl_wayland_hlp_dcp_write_wl_fd(fd, &uRevision, sizeof(uRevision));
            else
                rc = VINF_SUCCESS;
        }
        else
        {
            void  *pvWaylandData = NULL;
            size_t cbWaylandData = 0;
            rc = VBClWaylandClipboardQueryHostData(pShClCtx, pcszMimeType, &pvWaylandData, &cbWaylandData);
            if (RT_SUCCESS(rc))
            {
                rc = vbcl_wayland_hlp_dcp_write_wl_fd(fd, pvWaylandData, cbWaylandData);
                RTMemFree(pvWaylandData);
            }
        }
    }
    else
    {
        VBClLogVerbose(2, "cannot send to guest, no host service connection yet\n");
        rc = VERR_TRY_AGAIN;
    }

    return rc;
}

int VBClWaylandXdcpReportHostClipboardFormats(vbox_wl_xdcp_base_ctx_t *pCtx, SHCLFORMATS fFmts)
{
    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);

    /* Set list of host clipboard formats to the session. */
    pCtx->Session.clip.fFmts.set(fFmts);

    /* Ask Wayland event thread to advertise formats to the guest. */
    pCtx->fSendToGuest.set(true);
    RTThreadPoke(pCtx->Thread);

    return VINF_SUCCESS;
}

