/* $Id: VBoxNetSlirpNATTest.cpp 114884 2026-08-07 07:22:52Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxNetSlirpNAT - Wrapper for guest-side tests.
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
#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>
# include <iprt/win/ws2tcpip.h>
# include <iprt/win/windows.h>
#else
# include <errno.h>
# include <poll.h>
#endif

#include <limits.h>

#include <slirp/libslirp.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/net.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "tstVBoxNatGuestSideInternal.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** One classic libslirp timer. */
typedef struct VBOXNETSLIRPNATTESTTIMER
{
    /** The next timer. */
    struct VBOXNETSLIRPNATTESTTIMER *pNext;
    /** Absolute expiration time in milliseconds; zero means stopped. */
    int64_t                           msExpire;
    /** The libslirp timer callback. */
    SlirpTimerCb                      pfnCallback;
    /** The callback argument. */
    void                             *pvCallback;
    /** Poll generation in which this timer was most recently dispatched. */
    uint64_t                          uRunGeneration;
} VBOXNETSLIRPNATTESTTIMER;
/** Pointer to a classic libslirp timer. */
typedef VBOXNETSLIRPNATTESTTIMER *PVBOXNETSLIRPNATTESTTIMER;

/** Restricted, single-thread-affine libslirp test instance. */
struct VBOXNETSLIRPNATTEST
{
    /** The libslirp instance. */
    Slirp                           *pSlirp;
    /** Callbacks retained for the complete libslirp lifetime. */
    SlirpCb                          Callbacks;
    /** Frame callback supplied by the testcase. */
    PFNVBOXNETSLIRPNATTESTOUTPUT     pfnOutput;
    /** Frame callback argument. */
    void                            *pvOutputUser;
    /** First output callback error from the current operation. */
    int                              rcOutput;
    /** First poll-array construction error from the current poll. */
    int                              rcPoll;
    /** Classic libslirp timers. */
    PVBOXNETSLIRPNATTESTTIMER        pTimerHead;
    /** Generation counter used to dispatch timers safely across list changes. */
    uint64_t                          uTimerRunGeneration;
    /** Poll descriptors. */
    struct pollfd                   *paPollFds;
    /** Number of allocated poll descriptors. */
    uint32_t                         cPollFdsAllocated;
    /** Number of descriptors used by the current poll. */
    uint32_t                         cPollFds;
#ifdef RT_OS_WINDOWS
    /** Whether this instance owns one WSAStartup reference. */
    bool                             fWsaStarted;
#endif
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/** Converts libslirp poll events to the host poll representation. */
static short vboxNetSlirpNATTestPollEventsToHost(int fEvents)
{
    short fRet = 0;
#ifndef RT_OS_WINDOWS
    if (fEvents & SLIRP_POLL_IN)  fRet |= POLLIN;
    if (fEvents & SLIRP_POLL_OUT) fRet |= POLLOUT;
    if (fEvents & SLIRP_POLL_PRI) fRet |= POLLPRI;
    if (fEvents & SLIRP_POLL_ERR) fRet |= POLLERR;
    if (fEvents & SLIRP_POLL_HUP) fRet |= POLLHUP;
#else
    if (fEvents & SLIRP_POLL_IN)  fRet |= POLLRDNORM | POLLRDBAND;
    if (fEvents & SLIRP_POLL_OUT) fRet |= POLLWRNORM;
    if (fEvents & SLIRP_POLL_PRI) fRet |= POLLIN;
#endif
    return fRet;
}


/** Converts host poll results to the libslirp representation. */
static int vboxNetSlirpNATTestPollEventsFromHost(short fEvents)
{
    int fRet = 0;
#ifndef RT_OS_WINDOWS
    if (fEvents & POLLIN)  fRet |= SLIRP_POLL_IN;
    if (fEvents & POLLOUT) fRet |= SLIRP_POLL_OUT;
    if (fEvents & POLLPRI) fRet |= SLIRP_POLL_PRI;
#else
    if (fEvents & (POLLRDNORM | POLLRDBAND)) fRet |= SLIRP_POLL_IN;
    if (fEvents & POLLWRNORM)                fRet |= SLIRP_POLL_OUT;
    if (fEvents & POLLPRI)                   fRet |= SLIRP_POLL_PRI;
#endif
    if (fEvents & POLLERR) fRet |= SLIRP_POLL_ERR;
    if (fEvents & POLLHUP) fRet |= SLIRP_POLL_HUP;
    return fRet;
}


/** Delivers one libslirp frame to the testcase. */
static DECLCALLBACK(slirp_ssize_t)
vboxNetSlirpNATTestSendPacket(const void *pvFrame, ssize_t cbFrame, void *pvUser)
{
    PVBOXNETSLIRPNATTEST pThis = (PVBOXNETSLIRPNATTEST)pvUser;
    AssertPtrReturn(pThis, -1);
    AssertPtrReturn(pvFrame, -1);
    AssertReturn(cbFrame > 0, -1);

    int const rc = pThis->pfnOutput(pvFrame, (size_t)cbFrame, pThis->pvOutputUser);
    if (RT_FAILURE(rc))
    {
        if (RT_SUCCESS(pThis->rcOutput))
            pThis->rcOutput = rc;
        return -1;
    }
    return cbFrame;
}


/** Ignores libslirp diagnostics caused by deliberately malformed guest input. */
static DECLCALLBACK(void) vboxNetSlirpNATTestGuestError(const char *pszMessage, void *pvUser)
{
    RT_NOREF(pszMessage, pvUser);
}


/** Returns libslirp's monotonic virtual clock. */
static DECLCALLBACK(int64_t) vboxNetSlirpNATTestClockGetNs(void *pvUser)
{
    RT_NOREF(pvUser);
    return (int64_t)RTTimeNanoTS();
}


/** Allocates and links a classic libslirp timer. */
static DECLCALLBACK(void *)
vboxNetSlirpNATTestTimerNew(SlirpTimerCb pfnCallback, void *pvCallback, void *pvUser)
{
    PVBOXNETSLIRPNATTEST pThis = (PVBOXNETSLIRPNATTEST)pvUser;
    AssertPtrReturn(pThis, NULL);
    AssertPtrReturn(pfnCallback, NULL);

    PVBOXNETSLIRPNATTESTTIMER pTimer = (PVBOXNETSLIRPNATTESTTIMER)RTMemAllocZ(sizeof(*pTimer));
    if (pTimer)
    {
        pTimer->pfnCallback = pfnCallback;
        pTimer->pvCallback  = pvCallback;
        pTimer->pNext       = pThis->pTimerHead;
        pThis->pTimerHead   = pTimer;
    }
    return pTimer;
}


/** Unlinks and frees a classic libslirp timer. */
static DECLCALLBACK(void) vboxNetSlirpNATTestTimerFree(void *pvTimer, void *pvUser)
{
    PVBOXNETSLIRPNATTEST      pThis = (PVBOXNETSLIRPNATTEST)pvUser;
    PVBOXNETSLIRPNATTESTTIMER pTimer = (PVBOXNETSLIRPNATTESTTIMER)pvTimer;
    AssertPtrReturnVoid(pThis);

    PVBOXNETSLIRPNATTESTTIMER *ppCur = &pThis->pTimerHead;
    while (*ppCur)
    {
        if (*ppCur == pTimer)
        {
            *ppCur = pTimer->pNext;
            RTMemFree(pTimer);
            return;
        }
        ppCur = &(*ppCur)->pNext;
    }
    AssertFailed();
}


/** Arms or stops a classic libslirp timer. */
static DECLCALLBACK(void) vboxNetSlirpNATTestTimerMod(void *pvTimer, int64_t msExpire, void *pvUser)
{
    PVBOXNETSLIRPNATTESTTIMER pTimer = (PVBOXNETSLIRPNATTESTTIMER)pvTimer;
    AssertPtrReturnVoid(pTimer);
    pTimer->msExpire = msExpire;
    RT_NOREF(pvUser);
}


/** Notification is unnecessary because Input and Poll are serialized. */
static DECLCALLBACK(void) vboxNetSlirpNATTestNotify(void *pvUser)
{
    RT_NOREF(pvUser);
}


/** Socket registration is handled by slirp_pollfds_fill_socket. */
static DECLCALLBACK(void) vboxNetSlirpNATTestRegisterSocket(slirp_os_socket hSocket, void *pvUser)
{
    RT_NOREF(hSocket, pvUser);
}


/** Socket unregistration is handled by slirp_pollfds_fill_socket. */
static DECLCALLBACK(void) vboxNetSlirpNATTestUnregisterSocket(slirp_os_socket hSocket, void *pvUser)
{
    RT_NOREF(hSocket, pvUser);
}


/** Adds a descriptor requested by slirp_pollfds_fill_socket. */
static DECLCALLBACK(int) vboxNetSlirpNATTestAddPollFd(slirp_os_socket hSocket, int fEvents, void *pvUser)
{
    PVBOXNETSLIRPNATTEST pThis = (PVBOXNETSLIRPNATTEST)pvUser;
    AssertPtrReturn(pThis, -1);

    if (pThis->cPollFds == pThis->cPollFdsAllocated)
    {
        uint32_t const cNew = pThis->cPollFdsAllocated ? pThis->cPollFdsAllocated * 2 : 64;
        void *pvNew = RTMemRealloc(pThis->paPollFds, cNew * sizeof(pThis->paPollFds[0]));
        if (!pvNew)
        {
            pThis->rcPoll = VERR_NO_MEMORY;
            return -1;
        }
        pThis->paPollFds          = (struct pollfd *)pvNew;
        pThis->cPollFdsAllocated = cNew;
    }

    uint32_t const iPoll = pThis->cPollFds++;
    AssertReturn(iPoll < INT_MAX, -1);
    pThis->paPollFds[iPoll].fd      = hSocket;
    pThis->paPollFds[iPoll].events  = vboxNetSlirpNATTestPollEventsToHost(fEvents);
    pThis->paPollFds[iPoll].revents = 0;
    return (int)iPoll;
}


/** Returns the events observed for a descriptor. */
static DECLCALLBACK(int) vboxNetSlirpNATTestGetPollEvents(int iPoll, void *pvUser)
{
    PVBOXNETSLIRPNATTEST pThis = (PVBOXNETSLIRPNATTEST)pvUser;
    AssertPtrReturn(pThis, SLIRP_POLL_ERR);
    AssertReturn(iPoll >= 0 && (uint32_t)iPoll < pThis->cPollFds, SLIRP_POLL_ERR);
    return vboxNetSlirpNATTestPollEventsFromHost(pThis->paPollFds[iPoll].revents);
}


/** Lowers a poll timeout to the earliest classic timer deadline. */
static uint32_t vboxNetSlirpNATTestAdjustTimeout(PVBOXNETSLIRPNATTEST pThis, uint32_t cMsTimeout)
{
    int64_t msDeadline = INT64_MAX;
    for (PVBOXNETSLIRPNATTESTTIMER pTimer = pThis->pTimerHead; pTimer; pTimer = pTimer->pNext)
        if (pTimer->msExpire > 0 && pTimer->msExpire < msDeadline)
            msDeadline = pTimer->msExpire;

    if (msDeadline != INT64_MAX)
    {
        int64_t const msNow = (int64_t)(RTTimeNanoTS() / RT_NS_1MS);
        if (msDeadline <= msNow)
            return 0;
        uint64_t const cMsToDeadline = (uint64_t)(msDeadline - msNow);
        if (cMsToDeadline < cMsTimeout)
            cMsTimeout = (uint32_t)cMsToDeadline;
    }
    return cMsTimeout;
}


/** Runs all expired classic timers. */
static void vboxNetSlirpNATTestRunExpiredTimers(PVBOXNETSLIRPNATTEST pThis)
{
    int64_t const msNow = (int64_t)(RTTimeNanoTS() / RT_NS_1MS);
    uint64_t const uGeneration = ++pThis->uTimerRunGeneration;
    for (PVBOXNETSLIRPNATTESTTIMER pTimer = pThis->pTimerHead; pTimer; pTimer = pTimer->pNext)
        if (pTimer->msExpire > 0 && pTimer->msExpire <= msNow)
            pTimer->uRunGeneration = uGeneration;

    for (;;)
    {
        PVBOXNETSLIRPNATTESTTIMER pTimer = pThis->pTimerHead;
        while (pTimer && pTimer->uRunGeneration != uGeneration)
            pTimer = pTimer->pNext;
        if (!pTimer)
            break;

        SlirpTimerCb const pfnCallback = pTimer->pfnCallback;
        void * const       pvCallback  = pTimer->pvCallback;
        pTimer->uRunGeneration = 0;
        pTimer->msExpire       = 0;
        pfnCallback(pvCallback);
    }
}


/** Validates that an address belongs to the configured IPv4 network. */
static bool vboxNetSlirpNATTestAddrIsInNetwork(PCVBOXNETSLIRPNATTESTCFG pCfg, RTNETADDRIPV4 Addr)
{
    return (Addr.u & pCfg->IPv4Netmask.u) == pCfg->IPv4Network.u;
}


/** Validates the restricted IPv4 configuration accepted by the wrapper. */
static int vboxNetSlirpNATTestValidateConfig(PCVBOXNETSLIRPNATTESTCFG pCfg)
{
    AssertPtrReturn(pCfg, VERR_INVALID_POINTER);
    if (!pCfg->fRestricted)
        return VERR_ACCESS_DENIED;

    int iPrefix = 0;
    int rc = RTNetMaskToPrefixIPv4(&pCfg->IPv4Netmask, &iPrefix);
    if (RT_FAILURE(rc) || iPrefix < 1 || iPrefix > 30)
        return VERR_INVALID_PARAMETER;
    if (   (pCfg->IPv4Network.u & pCfg->IPv4Netmask.u) != pCfg->IPv4Network.u
        || !vboxNetSlirpNATTestAddrIsInNetwork(pCfg, pCfg->IPv4Host)
        || !vboxNetSlirpNATTestAddrIsInNetwork(pCfg, pCfg->IPv4DhcpStart)
        || !vboxNetSlirpNATTestAddrIsInNetwork(pCfg, pCfg->IPv4Nameserver))
        return VERR_INVALID_PARAMETER;

    uint32_t const uBroadcast = pCfg->IPv4Network.u | ~pCfg->IPv4Netmask.u;
    if (   pCfg->IPv4Host.u == pCfg->IPv4Network.u
        || pCfg->IPv4Host.u == uBroadcast
        || pCfg->IPv4DhcpStart.u == pCfg->IPv4Network.u
        || pCfg->IPv4DhcpStart.u == uBroadcast
        || pCfg->IPv4Nameserver.u == pCfg->IPv4Network.u
        || pCfg->IPv4Nameserver.u == uBroadcast)
        return VERR_INVALID_PARAMETER;
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Exported Test Functions                                                                                                      *
*********************************************************************************************************************************/
DECLHIDDEN(int) VBoxNetSlirpNATTestCreate(PCVBOXNETSLIRPNATTESTCFG pCfg,
                                          PFNVBOXNETSLIRPNATTESTOUTPUT pfnOutput,
                                          void *pvOutputUser, PVBOXNETSLIRPNATTEST *phNat)
{
    AssertPtrReturn(phNat, VERR_INVALID_POINTER);
    *phNat = NULL;
    AssertPtrReturn(pfnOutput, VERR_INVALID_POINTER);

    int rc = vboxNetSlirpNATTestValidateConfig(pCfg);
    if (RT_FAILURE(rc))
        return rc;

    PVBOXNETSLIRPNATTEST pThis = (PVBOXNETSLIRPNATTEST)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    pThis->pfnOutput    = pfnOutput;
    pThis->pvOutputUser = pvOutputUser;
    pThis->rcOutput     = VINF_SUCCESS;
    pThis->rcPoll       = VINF_SUCCESS;

#ifdef RT_OS_WINDOWS
    WSADATA WsaData;
    int const iWsaErr = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (iWsaErr != 0)
    {
        RTMemFree(pThis);
        return RTErrConvertFromWin32(iWsaErr);
    }
    pThis->fWsaStarted = true;
#endif

    pThis->Callbacks.send_packet            = vboxNetSlirpNATTestSendPacket;
    pThis->Callbacks.guest_error            = vboxNetSlirpNATTestGuestError;
    pThis->Callbacks.clock_get_ns           = vboxNetSlirpNATTestClockGetNs;
    pThis->Callbacks.timer_new              = vboxNetSlirpNATTestTimerNew;
    pThis->Callbacks.timer_free             = vboxNetSlirpNATTestTimerFree;
    pThis->Callbacks.timer_mod              = vboxNetSlirpNATTestTimerMod;
    pThis->Callbacks.notify                 = vboxNetSlirpNATTestNotify;
    pThis->Callbacks.register_poll_socket   = vboxNetSlirpNATTestRegisterSocket;
    pThis->Callbacks.unregister_poll_socket = vboxNetSlirpNATTestUnregisterSocket;

    SlirpConfig Cfg;
    RT_ZERO(Cfg);
    Cfg.version               = SLIRP_CONFIG_VERSION_MAX;
    Cfg.restricted            = 1;
    Cfg.in_enabled            = true;
    Cfg.vnetwork.s_addr       = pCfg->IPv4Network.u;
    Cfg.vnetmask.s_addr       = pCfg->IPv4Netmask.u;
    Cfg.vhost.s_addr          = pCfg->IPv4Host.u;
    Cfg.vdhcp_start.s_addr    = pCfg->IPv4DhcpStart.u;
    Cfg.vnameserver.s_addr    = pCfg->IPv4Nameserver.u;
    Cfg.in6_enabled           = false;
    Cfg.if_mtu                = 1500;
    Cfg.if_mru                = 1500;
    Cfg.if_mtu_v6             = 1280;
    Cfg.if_mru_v6             = 1280;
    Cfg.disable_host_loopback = true;
    Cfg.enable_emu            = false;
    Cfg.disable_dns           = !pCfg->fDns;
    Cfg.disable_dhcp          = !pCfg->fDhcp;
    Cfg.fForwardBroadcast     = false;
    Cfg.iSoMaxConn            = 10;
    Cfg.fDisableIPv6RA        = true;

    pThis->pSlirp = slirp_new(&Cfg, &pThis->Callbacks, pThis);
    if (!pThis->pSlirp)
    {
#ifdef RT_OS_WINDOWS
        WSACleanup();
#endif
        RTMemFree(pThis);
        return VERR_NO_MEMORY;
    }

    *phNat = pThis;
    return VINF_SUCCESS;
}


DECLHIDDEN(void) VBoxNetSlirpNATTestDestroy(PVBOXNETSLIRPNATTEST hNat)
{
    if (!hNat)
        return;

    if (hNat->pSlirp)
    {
        slirp_cleanup(hNat->pSlirp);
        hNat->pSlirp = NULL;
    }
    while (hNat->pTimerHead)
    {
        PVBOXNETSLIRPNATTESTTIMER pFree = hNat->pTimerHead;
        hNat->pTimerHead = pFree->pNext;
        RTMemFree(pFree);
    }
    RTMemFree(hNat->paPollFds);
#ifdef RT_OS_WINDOWS
    if (hNat->fWsaStarted)
        WSACleanup();
#endif
    RTMemFree(hNat);
}


DECLHIDDEN(int) VBoxNetSlirpNATTestInput(PVBOXNETSLIRPNATTEST hNat,
                                         const void *pvFrame, size_t cbFrame)
{
    AssertPtrReturn(hNat, VERR_INVALID_HANDLE);
    if (!pvFrame || cbFrame == 0 || cbFrame > _64K)
        return VERR_INVALID_PARAMETER;

    /* Keep the testcase seam aligned with VBoxNetSlirpNAT::processFrame. */
    if (cbFrame < sizeof(RTNETETHERHDR) || cbFrame > 1522)
        return VINF_SUCCESS;

    hNat->rcOutput = VINF_SUCCESS;
    slirp_input(hNat->pSlirp, (uint8_t const *)pvFrame, (int)cbFrame);
    return hNat->rcOutput;
}


DECLHIDDEN(int) VBoxNetSlirpNATTestPoll(PVBOXNETSLIRPNATTEST hNat, uint32_t cMsMax)
{
    AssertPtrReturn(hNat, VERR_INVALID_HANDLE);

    hNat->rcOutput = VINF_SUCCESS;
    hNat->rcPoll   = VINF_SUCCESS;
    hNat->cPollFds = 0;

    uint32_t cMsTimeout = RT_MIN(cMsMax, (uint32_t)INT_MAX);
    slirp_pollfds_fill_socket(hNat->pSlirp, &cMsTimeout, vboxNetSlirpNATTestAddPollFd, hNat);
    if (RT_FAILURE(hNat->rcPoll))
        return hNat->rcPoll;
    cMsTimeout = vboxNetSlirpNATTestAdjustTimeout(hNat, cMsTimeout);

    int cReady = 0;
    if (hNat->cPollFds == 0)
    {
        if (cMsTimeout > 0)
            RTThreadSleep(cMsTimeout);
    }
    else
    {
#ifdef RT_OS_WINDOWS
        cReady = WSAPoll(hNat->paPollFds, hNat->cPollFds, (int)cMsTimeout);
        if (cReady == SOCKET_ERROR)
        {
            int const rc = RTErrConvertFromWin32(WSAGetLastError());
            slirp_pollfds_poll(hNat->pSlirp, true, vboxNetSlirpNATTestGetPollEvents, hNat);
            return rc;
        }
#else
        cReady = poll(hNat->paPollFds, hNat->cPollFds, (int)cMsTimeout);
        if (cReady < 0)
        {
            if (errno == EINTR)
                cReady = 0;
            else
            {
                int const rc = RTErrConvertFromErrno(errno);
                slirp_pollfds_poll(hNat->pSlirp, true, vboxNetSlirpNATTestGetPollEvents, hNat);
                return rc;
            }
        }
#endif
    }

    RT_NOREF(cReady);
    slirp_pollfds_poll(hNat->pSlirp, false, vboxNetSlirpNATTestGetPollEvents, hNat);
    vboxNetSlirpNATTestRunExpiredTimers(hNat);
    return hNat->rcOutput;
}
