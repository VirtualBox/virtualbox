/* $Id: wayland-helper-xdcp-common.cpp 114771 2026-07-25 21:20:37Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - Common code for Data Control Protocol (DCP) family helper for Wayland.
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
RTDECL(int) vbcl_wayland_xdcp_next_event(vbox_wl_xdcp_base_ctx_t *pCtx)
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
    int rc;
    void *pvData;

    AssertPtrReturnVoid(pCtx);

    vbox_wl_dcp_session_t *pSession = &pCtx->Session;

    rc = VbghMimeConvCacheClear(pCtx->hCache);
    if (RT_FAILURE(rc))
        VBClLogVerbose(5, "unable to clear clipboard cache, rc=%Rrc", rc);

    vbox_wl_dcp_mime_t *pEntry, *pNextEntry;
    RTListForEachSafe(&pSession->clip.mimeTypesList, pEntry, pNextEntry, vbox_wl_dcp_mime_t, Node)
    {
        RTListNodeRemove(&pEntry->Node);
        RTMemFree(pEntry);
    }

    pvData = (void *)pSession->clip.pvDataBuf.reset();
    if (RT_VALID_PTR(pvData))
        RTMemFree(pvData);
}

/**
 * Initialize session.
 *
 * @param   pSession        Session data.
 */
static void vbcl_wayland_xdcp_session_init(vbox_wl_dcp_session_t *pSession)
{
    AssertPtrReturnVoid(pSession);

    RTListInit(&pSession->clip.mimeTypesList);

    pSession->clip.fFmts.init(VBOX_SHCL_FMT_NONE, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pSession->clip.uFmt.init(VBOX_SHCL_FMT_NONE, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pSession->clip.pvDataBuf.init(0, VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS);
    pSession->clip.cbDataBuf.init(0, VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS);
}

RTDECL(void) vbcl_wayland_xdcp_session_prepare(vbox_wl_xdcp_base_ctx_t *pCtx)
{
    AssertPtrReturnVoid(pCtx);

    vbcl_wayland_xdcp_session_release(pCtx);
    vbcl_wayland_xdcp_session_init(&pCtx->Session);
}

RTDECL(int) vbcl_wayland_xdcp_add_fmt(struct vbcl_wl_dcp_enumerate_ctx *pEnmCtx)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pEnmCtx, VERR_INVALID_PARAMETER);

    vbox_wl_dcp_session_t * const pSession = pEnmCtx->pSession;
    AssertPtrReturn(pSession, VERR_INVALID_PARAMETER);

    const char * const pcszMimeType = pEnmCtx->pcszMimeType;
    AssertPtrReturn(pcszMimeType, VERR_INVALID_PARAMETER);
    size_t const cchMimeType = strlen(pcszMimeType);

    /*
     * Allocate, initialize and add a new node to the MIME type list.
     */
    int rc;
    SHCLFORMAT uFmt = VbghMimeConvGetVBoxFormatByMime(pcszMimeType, NULL /*pfFlagsAndPriority*/);
    if (uFmt != VBOX_SHCL_FMT_NONE)
    {
        vbox_wl_dcp_mime_t * const pNode = (vbox_wl_dcp_mime_t *)RTMemAllocZVar(RT_UOFFSETOF_DYN(vbox_wl_dcp_mime_t,
                                                                                                 szMimeType[cchMimeType + 1]));
        if (pNode)
        {
            memcpy(pNode->szMimeType, pcszMimeType, cchMimeType);
            pNode->szMimeType[cchMimeType] = '\0';

            RTListAppend(&pSession->clip.mimeTypesList, &pNode->Node);
            VBClLogVerbose(5, "Wayland announces MIME type: %s\n", pNode->szMimeType);

            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_NO_DATA;

    return rc;
}

RTDECL(void) vbcl_wayland_xdcp_reset_ctx(vbox_wl_xdcp_base_ctx_t *pCtx, bool fShutdown)
{
    int rc;

    AssertPtrReturnVoid(pCtx);

    pCtx->Thread = NIL_RTTHREAD;
    pCtx->fShutdown = false;
    pCtx->fIngnoreWlClipIn = false;
    pCtx->fSendToGuest.init(false, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
    pCtx->pClipboardCtx = NULL;
    pCtx->pDisplay = NULL;
    pCtx->pRegistry = NULL;
    pCtx->pSeat = NULL;

    if (!fShutdown)
    {
        rc = VbghMimeConvCacheCreate(&pCtx->hCache);
        if (RT_FAILURE(rc))
        {
            VBClLogVerbose(1, "Unable to create MIME cache, rc=%Rrc\n", rc);
            pCtx->hCache = NIL_VBGHMIMECONVCACHE;
        }
    }
    else
    {
        vbcl_wayland_xdcp_session_release(pCtx);

        rc = VbghMimeConvCacheDestroy(pCtx->hCache);
        if (RT_FAILURE(rc))
            VBClLogVerbose(1, "VbghMimeConvCacheDelete failed: %Rrc\n", rc);
        pCtx->hCache = NIL_VBGHMIMECONVCACHE;
    }

    vbcl_wayland_xdcp_session_init(&pCtx->Session);
}

RTDECL(int) vbcl_wayland_xdcp_get_guest_clipboard(int fd, vbox_wl_xdcp_base_ctx_t *pCtx, const char *pszMimeType)
{
    int rc;

    void *pvBuf = NULL;
    size_t cbBuf = 0;

    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszMimeType, VERR_INVALID_PARAMETER);

    wl_display_flush(pCtx->pDisplay);

    rc = vbcl_wayland_hlp_dcp_read_wl_fd(fd, &pvBuf, &cbBuf);
    if (RT_SUCCESS(rc))
    {
        void *pvBufOut = NULL;
        size_t cbBufOut = 0;

        rc = VbghMimeConvToVBox(pszMimeType, pvBuf, cbBuf, &pvBufOut, &cbBufOut);
        if (RT_SUCCESS(rc))
        {
            pCtx->Session.clip.pvDataBuf.set((uint64_t)pvBufOut);
            pCtx->Session.clip.cbDataBuf.set((uint64_t)cbBufOut);

            rc = VbghMimeConvCacheSetByMime(pCtx->hCache, pszMimeType, pvBufOut, cbBufOut);
            VBClLogVerbose(5, "Put %u bytes into cache for MIME type: %s\n", cbBufOut, pszMimeType);
        }

        RTMemFree(pvBuf);
    }

    return rc;
}

RTDECL(int) vbcl_wayland_xdcp_set_guest_clipboard(int fd, vbox_wl_xdcp_base_ctx_t *pCtx, const char *pcszMimeType)
{
    int rc;

    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcszMimeType, VERR_INVALID_PARAMETER);

    if (RT_VALID_PTR(pCtx->pClipboardCtx))
    {
        /* Set requested format  the session. */
        SHCLFORMAT const uVBoxFmt = VbghMimeConvGetVBoxFormatByMime(pcszMimeType, NULL /*pfFlagsAndPriority*/);
        VBClLogVerbose(5, "vbcl_wayland_xdcp_set_guest_clipboard: %s -> %#x\n", pcszMimeType, uVBoxFmt);
        pCtx->Session.clip.uFmt.set(uVBoxFmt);

        /* Wait for data in requested format. */
        void * const   pvBuf = (void *)pCtx->Session.clip.pvDataBuf.wait(); /** @todo not 32-bit clean! */
        uint32_t const cbBuf = pCtx->Session.clip.cbDataBuf.wait();
        if (   cbBuf != pCtx->Session.clip.cbDataBuf.defaults()
            && pvBuf != (void *)pCtx->Session.clip.pvDataBuf.defaults())
        {
            /* Convert clipboard data from VBox representation into guest format. */
            void *pvBufOut = NULL;
            size_t cbOut = 0;
            rc = VbghMimeConvFromVBox(pcszMimeType, pvBuf, cbBuf, &pvBufOut, &cbOut);
            if (RT_SUCCESS(rc))
            {
                rc = vbcl_wayland_hlp_dcp_write_wl_fd(fd, pvBufOut, cbOut);
                RTMemFree(pvBufOut);
            }
            else
            {
                VBClLogError("Failed to convert uVBoxFmt=%#x to '%s': %Rrc\n", uVBoxFmt, pcszMimeType, rc);
                VBClLogVerbose(6, "Bogus input data uVBoxFmt=%#x cbBuf=%#x\n%.*Rhxd\n", uVBoxFmt, cbBuf, cbBuf, pvBuf);
            }
        }
        else
        {
            VBClLogVerbose(4, "vbcl_wayland_xdcp_set_guest_clipboard: timed out waiting for host data (%#x/%s)\n",
                           uVBoxFmt, pcszMimeType);
            rc = VERR_TIMEOUT;
        }
    }
    else
    {
        VBClLogVerbose(2, "cannot send to guest, no host service connection yet\n");
        rc = VERR_TRY_AGAIN;
    }

    return rc;
}

RTDECL(int) vbcl_wayland_xdcp_get_host_clipboard(vbox_wl_xdcp_base_ctx_t *pCtx, SHCLFORMATS fFmts)
{
    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);

    /* Set list of host clipboard formats to the session. */
    pCtx->Session.clip.fFmts.set(fFmts);

    /* Ask Wayland event thread to advertise formats to the guest. */
    pCtx->fSendToGuest.set(true);
    RTThreadPoke(pCtx->Thread);

    int rc;

    /* Wait for the guest to request certain clipboard format. */
    /** @todo r=bird: The following makes no sense as there is no guarantee that
     *        the clipboard will be read... */
    SHCLFORMAT const uFmt = pCtx->Session.clip.uFmt.wait();
    if (uFmt != pCtx->Session.clip.uFmt.defaults())
    {
        /* Read host clipboard in specified format. */
        void    *pvData = NULL;
        uint32_t cbData = 0;
        rc = VBClClipboardReadHostClipboard(pCtx->pClipboardCtx, uFmt, &pvData, &cbData);
        if (RT_SUCCESS(rc))
        {
            /* Set clipboard data to the session. */
            VBClLogVerbose(5, "vbcl_wayland_xdcp_get_host_clipboard: Setting pvDataBuf=%p cbDataBuf=%#x (uFmt=%#x)...\n",
                           pvData, cbData, uFmt);
            pCtx->Session.clip.pvDataBuf.set((uint64_t)pvData);
            pCtx->Session.clip.cbDataBuf.set((uint64_t)cbData);
        }
        else
            VBClLogError("VBClClipboardReadHostClipboard failed in vbcl_wayland_xdcp_get_host_clipboard getting %#x: %Rrc\n",
                         uFmt, rc);
    }
    else
        rc = VERR_TIMEOUT;

    return rc;
}

RTDECL(int) vbcl_wayland_xdcp_set_host_clipboard(vbox_wl_xdcp_base_ctx_t *pCtx, SHCLFORMAT uFmt)
{
    int rc;

    void *pvData;
    size_t cbData = 0;

    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);

    /* Take data from cache. */
    rc = VbghMimeConvCacheGetByVBoxFormat(pCtx->hCache, uFmt, &pvData, &cbData);
    if (RT_SUCCESS(rc))
    {
        /* Send clipboard data to the host. */
        rc = VbglR3ClipboardWriteDataEx(pCtx->pClipboardCtx, uFmt, pvData, cbData);
    }
    else
        rc = VERR_TIMEOUT;

    VBClLogVerbose(5, "Sent %u bytes to host in format 0x%x, rc=%Rrc\n", cbData, uFmt, rc);

    return rc;
}
