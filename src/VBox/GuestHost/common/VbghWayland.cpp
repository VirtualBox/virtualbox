/* $Id: VbghWayland.cpp 114738 2026-07-21 13:40:26Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest / Host common code - Wayland Core.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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
#include <VBox/GuestHost/Wayland.h>

#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/poll.h>

#include <wayland-client-protocol.h>

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/pipe.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <VBox/log.h>



/**
 * Wayland registry global object handler.
 *
 * This callback is triggered when Wayland Registry listener is registered.
 * Wayland client library will trigger it individually for each available global
 * object.
 *
 * @param pvUser            Context data.
 * @param pRegistry         Wayland Registry object.
 * @param uName             Numeric name of the global object.
 * @param pszIface          Name of interface implemented by the object.
 * @param uVersion          Interface version.
 */
static void vbghWaylandRegistryListener_Global(void *pvUser, struct wl_registry *pRegistry, uint32_t uObjName,
                                               const char *pszIfaceName, uint32_t uIfaceVersion)
{
    PVBGHWAYLANDCORE const pThis = (PVBGHWAYLANDCORE)pvUser;
    AssertPtrReturnVoid(pThis);
    AssertPtrReturnVoid(pszIfaceName);

    /*
     * Do the callback first so the matching code below can return.
     */
    if (pThis->Int.pfnRegEnum)
        pThis->Int.pfnRegEnum(pThis, pRegistry, uObjName, pszIfaceName, uIfaceVersion, pThis->Int.pErrInfo);

    /*
     * Collect all seats.
     */
    if (RTStrCmp(pszIfaceName, wl_seat_interface.name) == 0)
    {
        PVBGHWAYLANDSEAT const pEntry = (PVBGHWAYLANDSEAT)RTMemAllocZ(sizeof(*pEntry));
        AssertLogRelReturnVoid(pEntry);
        uint32_t const uVer = 5;
        pEntry->pSeat = (struct wl_seat *)wl_registry_bind(pRegistry, uObjName, &wl_seat_interface, uVer);
        if (RT_VALID_PTR(pEntry->pSeat))
        {
            LogRel4(("%s: binding to Wayland object %u interface '%s' v%u(/%u/%u) -> %p\n", __func__, uObjName,
                     wl_seat_interface.name, wl_proxy_get_version((struct wl_proxy *)pEntry->pSeat),
                     uIfaceVersion, uVer, pEntry->pSeat));
            RTListAppend(&pThis->SeatList, &pEntry->ListEntry);
        }
        else
        {
            RTMemFree(pEntry);
            LogRel(("error: %s: failed binding to Wayland object %u interface '%s' v%u/%un", __func__, uObjName,
                    wl_seat_interface.name, uIfaceVersion, uVer));
        }
    }
}

/**
 * Wayland registry global object remove handler.
 *
 * Triggered when global object is removed from Wayland registry.
 *
 * @param pvUser            Context data.
 * @param pRegistry         Wayland Registry object.
 * @param uName             Numeric name of the global object.
 */
static void vbghWaylandRegistryListener_GlobalRemove(void *pvUser, struct wl_registry *pRegistry, uint32_t uName)
{
    RT_NOREF(pvUser, pRegistry, uName);
}

/** Wayland global registry listener callbacks. */
static struct wl_registry_listener const g_vbghWaylandRegistryListener =
{
    /* .global = */         vbghWaylandRegistryListener_Global,
    /* .global_remove = */  vbghWaylandRegistryListener_GlobalRemove,
};


/**
 * Wayland seat capabilities callback.
 */
static void vbghWaylandSeatListener_Capabilites(void *pvUser, struct wl_seat *pSeat, uint32_t fCapabilities)
{
    PVBGHWAYLANDSEAT const pSeatEntry = (PVBGHWAYLANDSEAT)pvUser;
    AssertPtrReturnVoid(pSeatEntry);
    Assert(pSeat == pSeatEntry->pSeat);
    RT_NOREF(pSeat);

    pSeatEntry->fCaps = fCapabilities;
}

/**
 * Wayland seat name callback.
 */
static void vbghWaylandSeatListener_Name(void *pvUser, struct wl_seat *pSeat, const char *pszName)
{
    PVBGHWAYLANDSEAT const pSeatEntry = (PVBGHWAYLANDSEAT)pvUser;
    AssertPtrReturnVoid(pSeatEntry);
    Assert(pSeat == pSeatEntry->pSeat);
    RT_NOREF(pSeat);

    if (pSeatEntry->pszName)
        RTStrFree(pSeatEntry->pszName);
    pSeatEntry->pszName = RTStrDup(pszName);
}

/** Wayland seat listener callbacks. */
static struct wl_seat_listener const g_vbghWaylandSeatListener =
{
    /* .capabilities = */   vbghWaylandSeatListener_Capabilites,
    /* .name = */           vbghWaylandSeatListener_Name,
};


VBGH_DECL(int) VbghWaylandConnect(PVBGHWAYLANDCORE pThis, PFNVBGHWAYLANDREGENUM pfnRegEnum, PRTERRINFO pErrInfo)
{
    AssertPtrReturn(pThis, false);

    pThis->pDisplay       = NULL;
    pThis->pRegistry      = NULL;
    RTListInit(&pThis->SeatList);
    pThis->Int.pErrInfo   = NULL;
    pThis->Int.pfnRegEnum = NULL;

    /*
     * Connect to the display.
     */
    const char *pszWaylandDisplay = RTEnvGet(VBGH_WAYLAND_DISPLAY_ENV_VAR);
    if (!pszWaylandDisplay)
        return RTErrInfoSet(pErrInfo, VERR_ENV_VAR_NOT_FOUND,
                            "Failed to get the " VBGH_WAYLAND_DISPLAY_ENV_VAR " environment variable");

    pThis->pDisplay = wl_display_connect(pszWaylandDisplay);
    if (!RT_VALID_PTR(pThis->pDisplay))
        return RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE, "wl_display_connect(%s) failed", pszWaylandDisplay);

    /*
     * Get the registry and enumerate global objects.
     */
    pThis->pRegistry = wl_display_get_registry(pThis->pDisplay);
    if (RT_VALID_PTR(pThis->pRegistry))
    {
        pThis->Int.pErrInfo    = pErrInfo;
        pThis->Int.pfnRegEnum  = pfnRegEnum;

        int rc = wl_registry_add_listener(pThis->pRegistry, &g_vbghWaylandRegistryListener, pThis);
        if (!rc)
        {
            wl_display_roundtrip(pThis->pDisplay);

            pThis->Int.pErrInfo    = NULL;
            pThis->Int.pfnRegEnum  = NULL;

            /*
             * Get the capabilities of each seat.
             */
            PVBGHWAYLANDSEAT pSeatEntry;
            RTListForEach(&pThis->SeatList, pSeatEntry, VBGHWAYLANDSEAT, ListEntry)
            {
                wl_seat_add_listener(pSeatEntry->pSeat, &g_vbghWaylandSeatListener, pSeatEntry);
            }
            wl_display_roundtrip(pThis->pDisplay);

            if (LogRelIs3Enabled())
                RTListForEach(&pThis->SeatList, pSeatEntry, VBGHWAYLANDSEAT, ListEntry)
                {
                    LogRel3(("%s: seat: %#x %s\n", __func__, pSeatEntry->fCaps, pSeatEntry->pszName));
                }

            return VINF_SUCCESS;
        }

        RTErrInfoSetF(pErrInfo, VERR_GENERAL_FAILURE, "wl_registry_add_listener failed: %d", rc);
    }
    else
        RTErrInfoSet(pErrInfo, VERR_GENERAL_FAILURE, "wl_display_get_registry failed");
    wl_display_disconnect(pThis->pDisplay);
    pThis->pDisplay = NULL;
    return VERR_GENERAL_FAILURE;
}


VBGH_DECL(void) VbghWaylandDisconnect(PVBGHWAYLANDCORE pThis)
{
    AssertPtrReturnVoid(pThis);

    PVBGHWAYLANDSEAT pSeatEntry, pSeatEntryNext;
    RTListForEachSafe(&pThis->SeatList, pSeatEntry, pSeatEntryNext, VBGHWAYLANDSEAT, ListEntry)
    {
        wl_seat_destroy(pSeatEntry->pSeat);
        pSeatEntry->pSeat = NULL;
        RTStrFree(pSeatEntry->pszName);
        pSeatEntry->pszName = NULL;
    }
    if (pThis->pRegistry)
    {
        wl_registry_destroy(pThis->pRegistry);
        pThis->pRegistry = NULL;
    }

    if (pThis->pDisplay)
    {

        wl_display_disconnect(pThis->pDisplay);
        pThis->pDisplay = NULL;
    }
}


/**
 * Gets the a seat entry, preferably with keyboard or pointer.
 */
VBGH_DECL(PVBGHWAYLANDSEAT) VbghWaylandGetBestSeatEntry(PVBGHWAYLANDCORE pThis)
{
    AssertPtr(pThis);
    PVBGHWAYLANDSEAT pAlt = NULL;
    PVBGHWAYLANDSEAT pSeatEntry;
    RTListForEach(&pThis->SeatList, pSeatEntry, VBGHWAYLANDSEAT, ListEntry)
    {
        if (pSeatEntry->fCaps & WL_SEAT_CAPABILITY_KEYBOARD)
            return pSeatEntry;
        if (pSeatEntry->fCaps & WL_SEAT_CAPABILITY_POINTER)
            pAlt = pSeatEntry;
    }
    return pAlt ? pAlt : RTListGetFirst(&pThis->SeatList, VBGHWAYLANDSEAT, ListEntry);
}


/**
 * Reads all the data from @a fd into an allocated buffer.
 *
 * @returns VBox status code.
 * @param   fd          A file descriptor to read data from.
 * @param   cMsTimeout  Approximate timeout in milliseconds.
 * @param   ppvBuf      Where to return pointer to the allocated buffer. Call
 *                      VbghWaylandReadFdToBufferFree() to free it when done.
 * @param   pcbBuf      Where to return the number of bytes read.
 *
 * @note    There will always be an extra zero byte trailing the reported buffer
 *          size, so it can safely be converted to a zero terminated string.
 */
VBGH_DECL(int) VbghWaylandReadFdToBuffer(int fd, RTMSINTERVAL cMsTimeout, void **ppvBuf, size_t *pcbBuf)
{
    AssertPtrReturn(ppvBuf, VERR_INVALID_PARAMETER);
    *ppvBuf = NULL;
    AssertPtrReturn(pcbBuf, VERR_INVALID_PARAMETER);
    *pcbBuf = 0;

    /*
     * Start with a modest initial buffer allocation.
     * We'll double the size till we get up to s_cbGrowChunk size.
     */
    static size_t const s_cbInitial   = _1K;
    static size_t const s_cbGrowChunk = _4M;
#if RT_ARCH_BITS == 32
    static size_t const s_cbMax       =  _64M;
#else
    static size_t const s_cbMax       =  _512M;
#endif
    size_t   offBuf = 0;
    size_t   cbBuf  = s_cbInitial;
    uint8_t *pbBuf  = (uint8_t *)RTMemAllocZ(cbBuf);
    if (!pbBuf)
        return VERR_NO_MEMORY;

    /*
     * Read loop.
     */
    int rc;
    for (;;)
    {
        struct pollfd aPollFds[1];
        aPollFds[0].fd      = fd;
        aPollFds[0].events  = POLLIN | POLLHUP | POLLERR;
        aPollFds[0].revents = 0;
        rc = poll(aPollFds, 1, cMsTimeout < (RTMSINTERVAL)INT_MAX ? (int)cMsTimeout : -1);
        if (rc > 0)
        {
            if (aPollFds[0].revents & (POLLIN | POLLHUP))
            {
                /* Grow the buffer if necessary. */
                if (offBuf + /*zero terminator*/ 1 >= cbBuf)
                {
                    size_t const cbNew = cbBuf < s_cbGrowChunk ? cbBuf * 4 : cbBuf + s_cbGrowChunk;
                    if (cbNew > s_cbMax)
                    {
                        rc = VERR_BUFFER_OVERFLOW;
                        break;
                    }
                    void * const pvNew = RTMemReallocZ(pbBuf, cbBuf, cbNew);
                    if (!pvNew)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    pbBuf = (uint8_t *)pvNew;
                    cbBuf = cbNew;
                }

                /* read data */
                Assert(offBuf + /*zero terminator*/ 1 < cbBuf);
                ssize_t const cbRead = read(fd, &pbBuf[offBuf], cbBuf - offBuf - /*zero terminator*/ 1);
                if (cbRead > 0)
                {
                    offBuf += (size_t)cbRead;
                    Assert(offBuf < cbBuf);
                    continue;
                }
                if (cbRead == 0 && offBuf > 0)
                {
                    *ppvBuf = pbBuf;
                    *pcbBuf = offBuf;
                    LogRel5(("%s: returning %zu bytes\n", __func__, offBuf));
                    return VINF_SUCCESS;
                }
                if (cbRead == 0)
                {
                    LogRel5(("%s: returning VERR_NO_DATA\n", __func__));
                    rc = VERR_NO_DATA;
                }
                else
                {
                    rc = RTErrConvertFromErrno(errno);
                    LogRel(("error: %s: read -> %Rrc (errno=%d)\n", __func__, rc, errno));
                }
            }
            else
            {
                LogRel2(("error: %s: revents=%#x -> VERR_READ_ERROR\n", __func__, aPollFds[0].revents));
                rc = VERR_READ_ERROR;
            }
        }
        else if (rc == 0)
        {
            rc = VERR_TIMEOUT;
            LogRel2(("error: %s: poll -> timeout\n", __func__));
        }
        else if (errno == EINTR)
        {
            LogRel2(("error: %s: poll -> EINTR\n", __func__));
            continue;
        }
        else
        {
            rc = RTErrConvertFromErrno(errno);
            LogRel(("error: %s: poll -> %Rrc (errno=%d)\n", __func__, rc, errno));
        }
        break;
    }
    RTMemFree(pbBuf);
    return rc;
}


/**
 * Frees a buffer returned by VbghWaylandReadFdToBuffer().
 */
VBGH_DECL(void) VbghWaylandReadFdToBufferFree(void *pvBuf)
{
    RTMemFree(pvBuf);
}


/**
 * Writes a buffer to @a fd with a completion timeout.
 *
 * @returns VBox status code.
 * @param   pvBuf       Pointer to buffer containing the data to write.
 * @param   cbBuf       Number of byte to write.
 * @param   fdDst       The file descriptor to write the data to.
 * @param   cMsTimeout  Approximate timeout in milliseconds.
 */
VBGH_DECL(int) VbghWaylandWriteBufferToFd(void const *pvBuf, size_t cbBuf, int fdDst, RTMSINTERVAL cMsTimeout)
{
    /*
     * Validate input.
     */
    if (cbBuf == 0) /* Ignore zero length buffers. */
        return VINF_SUCCESS;
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);

    /*
     * Write loop.
     */
    size_t offBuf = 0;
    for (;;)
    {
        struct pollfd aPollFds[1];
        aPollFds[0].fd      = fdDst;
        aPollFds[0].events  = POLLOUT | POLLHUP | POLLERR;
        aPollFds[0].revents = 0;
        int rc = poll(aPollFds, 1, cMsTimeout < (RTMSINTERVAL)INT_MAX ? (int)cMsTimeout : -1);
        if (rc > 0)
        {
            if (aPollFds[0].revents & (POLLOUT | POLLHUP))
            {
                ssize_t const cbWritten = write(fdDst, &((uint8_t const *)pvBuf)[offBuf], cbBuf - offBuf);
                if (cbWritten > 0)
                {
                    offBuf += (size_t)cbWritten;
                    if (offBuf >= cbBuf)
                    {
                        Assert(offBuf == cbBuf);
                        LogRel5(("%s: returning VINF_SUCCESS (wrote %zu bytes)\n", __func__, offBuf));
                        return VINF_SUCCESS;
                    }
                }
                else
                {
                    rc = RTErrConvertFromErrno(errno);
                    LogRel(("error: %s: write -> %Rrc (errno=%d%s)\n", __func__, rc, errno, cbWritten == 0 ? ", ret zero" : ""));
                    return RT_FAILURE_NP(rc) ? rc : VERR_WRITE_ERROR;
                }
            }
            else
            {
                LogRel2(("error: %s: revents=%#x -> VERR_WRITE_ERROR\n", __func__, aPollFds[0].revents));
                return VERR_WRITE_ERROR;
            }
        }
        else if (rc == 0)
        {
            LogRel2(("error: %s: poll -> timeout\n", __func__));
            return VERR_TIMEOUT;
        }
        else if (errno == EINTR)
            LogRel2(("error: %s: poll -> EINTR\n", __func__));
        else
        {
            rc = RTErrConvertFromErrno(errno);
            LogRel(("error: %s: poll -> %Rrc (errno=%d)\n", __func__, rc, errno));
            return rc;
        }
    }
}


/** @callback_method_impl{FNVBGHWAYLANDRLWAKEUPPIPE} */
static DECLCALLBACK(int) vbghWaylandRunloopWakeupPipeDefault(RTPIPE hPipe, bool volatile *pfReturn, void *pvUser)
{
    RT_NOREF(pfReturn, pvUser);

    char    szTmp[64];
    size_t  cbIgnore;
    int const rc = RTPipeRead(hPipe, szTmp, sizeof(szTmp), &cbIgnore);
    if (rc == VERR_BROKEN_PIPE)
    {
        LogRel(("%s: VERR_BROKEN_PIPE!\n", __func__));
        return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Runloop function for servicing a display, with optional wakeup pipe and
 * return indicator.
 *
 * @returns VBox status code.
 * @param   pDisplay        The display to dispatch events for.
 * @param   hPipeWakeup     The wakeup pipe. Optional.
 * @param   pfnWakeup       Callback for servicing the wakekup pipe.  If NULL,
 *                          the pipe buffer is just drained.
 * @param   pvWakeupUser    User argument for the wakeup pipe callback.
 * @param   hPipeMonClose   Pipe to monitor for closing. Optional.
 * @param   cMsPollInterval The polling interval for checking pfReturn.
 * @param   pfReturn        Pointer to return indicator.  Will return
 *                          VINF_SUCCESS when it is set to true.  Optional.
 */
VBGH_DECL(int) VbghWaylandRunloopForDisplay(struct wl_display *pDisplay,
                                            RTPIPE hPipeWakeup, PFNVBGHWAYLANDRLWAKEUPPIPE pfnWakeup, void *pvWakeupUser,
                                            RTPIPE hPipeMonClose, RTMSINTERVAL cMsPollInterval, bool volatile *pfReturn)
{
    int const fdPipeWakeup   = hPipeWakeup   != NIL_RTPIPE ? (int)RTPipeToNative(hPipeWakeup)   : -1;
    int const fdPipeMonClose = hPipeMonClose != NIL_RTPIPE ? (int)RTPipeToNative(hPipeMonClose) : -1;
    if (!pfnWakeup)
        pfnWakeup = vbghWaylandRunloopWakeupPipeDefault;

    for (;;)
    {
        /*
         * Populate the poll array.
         */
        struct pollfd aPollFds[2];
        aPollFds[0].fd      = wl_display_get_fd(pDisplay);
        aPollFds[0].events  = POLLIN | POLLHUP | POLLERR;
        aPollFds[0].revents = 0;
        int cFds = 1;

        if (fdPipeWakeup >= 0)
        {
            aPollFds[cFds].fd      = fdPipeWakeup;
            aPollFds[cFds].events  = POLLIN | POLLHUP | POLLERR;
            aPollFds[cFds].revents = 0;
            cFds += 1;
        }

        if (fdPipeMonClose >= 0)
        {
            aPollFds[cFds].fd      = fdPipeMonClose;
            aPollFds[cFds].events  = POLLHUP | POLLERR;
            aPollFds[cFds].revents = 0;
            cFds += 1;
        }

        /*
         * Prepare the read/poll.
         */
        for (;;)
        {
            if (pfReturn && *pfReturn)
            {
                LogRel4(("%s: returns (pre)\n", __func__));
                return VINF_SUCCESS;
            }
            if (wl_display_prepare_read(pDisplay) == 0)
                break;
            int rc = wl_display_dispatch_pending(pDisplay);
            AssertLogRelMsg(rc >= 0, ("%s: wl_display_dispatch_pending -> %d, errno=%d\n", __func__, rc, errno));
            LogRel6(("%s: %d events dispatched (pre)\n", __func__, rc));
        }
        wl_display_flush(pDisplay);

        /*
         * Do the polling, unless we're asked to return.
         */
        int rcPoll = 0;
        if (!pfReturn || !*pfReturn)
            rcPoll = poll(aPollFds, cFds, cMsPollInterval < (RTMSINTERVAL)INT_MAX ? (int)cMsPollInterval : -1);
        if (pfReturn && *pfReturn)
        {
            LogRel4(("%s: returns (post)\n", __func__));
            wl_display_cancel_read(pDisplay);
            return VINF_SUCCESS;
        }
        AssertLogRelMsgStmt(rcPoll >= 0 || errno == EINTR, ("%s: poll(%d,,) -> %d, errno=%d\n", __func__, cFds, rcPoll, errno),
                            RTThreadSleep(500));

        /*
         * Drain the notification pipe before doing anything else.
         */
        if (rcPoll > 0 && cFds > 1 && fdPipeWakeup >= 0 && (aPollFds[1].revents & (POLLIN | POLLHUP)))
        {
            int rc = pfnWakeup(hPipeWakeup, pfReturn, pvWakeupUser);
            if (RT_FAILURE(rc))
                hPipeWakeup = NIL_RTPIPE;
        }

        /*
         * Dispatch pending events, if any.
         */
        if (rcPoll > 0 && (aPollFds[0].revents & (POLLIN | POLLHUP | POLLERR)))
        {
            int rc = wl_display_read_events(pDisplay);
            AssertLogRelMsg(rc == 0, ("%s: wl_display_read_events -> %d, errno=%d\n", __func__, rc, errno));
            rc = wl_display_dispatch_pending(pDisplay);
            AssertLogRelMsg(rc >= 0, ("%s: wl_display_dispatch_pending -> %d, errno=%d\n", __func__, rc, errno));
            LogRel6(("%s: %d events dispatched (post)\n", __func__, rc));
        }
        else
            wl_display_cancel_read(pDisplay);

        /*
         * Quit if the monitor close pipe closed.
         */
        if (rcPoll > 0 && cFds > 1 && fdPipeMonClose >= 0 && (aPollFds[cFds - 1].revents & (POLLERR | POLLHUP)))
        {
            LogRel2(("%s: returns VERR_BROKEN_PIPE (revents=%#x)\n", __func__, aPollFds[cFds - 1].revents));
            return VERR_BROKEN_PIPE;
        }
    }
}

