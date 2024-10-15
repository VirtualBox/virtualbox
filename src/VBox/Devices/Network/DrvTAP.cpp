/* $Id: DrvTAP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvTAP - Universal TAP network transport driver.
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
#define LOG_GROUP LOG_GROUP_DRV_TUN
#include <VBox/log.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmnetinline.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#ifdef RT_OS_SOLARIS
# include <iprt/process.h>
# include <iprt/env.h>
#endif

#include <sys/ioctl.h>
#include <sys/poll.h>
#ifdef RT_OS_SOLARIS
# include <sys/stat.h>
# include <sys/ethernet.h>
# include <sys/sockio.h>
# include <netinet/in.h>
# include <netinet/in_systm.h>
# include <netinet/ip.h>
# include <netinet/ip_icmp.h>
# include <netinet/udp.h>
# include <netinet/tcp.h>
# include <net/if.h>
# include <stropts.h>
# include <fcntl.h>
# include <stdlib.h>
# include <stdio.h>
#else
# include <sys/fcntl.h>
#endif
#include <errno.h>
#include <unistd.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * TAP driver instance data.
 *
 * @implements PDMINETWORKUP
 */
typedef struct DRVTAP
{
    /** The network interface. */
    PDMINETWORKUP           INetworkUp;
    /** The network interface. */
    PPDMINETWORKDOWN        pIAboveNet;
    /** Pointer to the driver instance. */
    PPDMDRVINS              pDrvIns;
    /** TAP device file handle. */
    RTFILE                  hFileDevice;
    /** The configured TAP device name. */
    char                   *pszDeviceName;
#ifdef RT_OS_SOLARIS
    /** IP device file handle (/dev/udp). */
    int                     iIPFileDes;
    /** Whether device name is obtained from setup application. */
    bool                    fStatic;
#endif
    /** TAP setup application. */
    char                   *pszSetupApplication;
    /** TAP terminate application. */
    char                   *pszTerminateApplication;
    /** The write end of the control pipe. */
    RTPIPE                  hPipeWrite;
    /** The read end of the control pipe. */
    RTPIPE                  hPipeRead;
    /** Reader thread. */
    PPDMTHREAD              pThread;

    /** @todo The transmit thread. */
    /** Transmit lock used by drvTAPNetworkUp_BeginXmit. */
    RTCRITSECT              XmitLock;

#ifdef VBOX_WITH_STATISTICS
    /** Number of sent packets. */
    STAMCOUNTER             StatPktSent;
    /** Number of sent bytes. */
    STAMCOUNTER             StatPktSentBytes;
    /** Number of received packets. */
    STAMCOUNTER             StatPktRecv;
    /** Number of received bytes. */
    STAMCOUNTER             StatPktRecvBytes;
    /** Profiling packet transmit runs. */
    STAMPROFILE             StatTransmit;
    /** Profiling packet receive runs. */
    STAMPROFILEADV          StatReceive;
#endif /* VBOX_WITH_STATISTICS */

#ifdef LOG_ENABLED
    /** The nano ts of the last transfer. */
    uint64_t                u64LastTransferTS;
    /** The nano ts of the last receive. */
    uint64_t                u64LastReceiveTS;
#endif
} DRVTAP, *PDRVTAP;


/** Converts a pointer to TAP::INetworkUp to a PRDVTAP. */
#define PDMINETWORKUP_2_DRVTAP(pInterface) ( (PDRVTAP)((uintptr_t)pInterface - RT_UOFFSETOF(DRVTAP, INetworkUp)) )


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef RT_OS_SOLARIS
static int              SolarisTAPAttach(PDRVTAP pThis);
#endif



/**
 * @interface_method_impl{PDMINETWORKUP,pfnBeginXmit}
 */
static DECLCALLBACK(int) drvTAPNetworkUp_BeginXmit(PPDMINETWORKUP pInterface, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVTAP pThis = PDMINETWORKUP_2_DRVTAP(pInterface);
    int rc = RTCritSectTryEnter(&pThis->XmitLock);
    if (RT_FAILURE(rc))
    {
        /** @todo XMIT thread */
        rc = VERR_TRY_AGAIN;
    }
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnAllocBuf}
 */
static DECLCALLBACK(int) drvTAPNetworkUp_AllocBuf(PPDMINETWORKUP pInterface, size_t cbMin,
                                                  PCPDMNETWORKGSO pGso, PPPDMSCATTERGATHER ppSgBuf)
{
    RT_NOREF(pInterface);
#ifdef VBOX_STRICT
    PDRVTAP pThis = PDMINETWORKUP_2_DRVTAP(pInterface);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));
#endif

    /*
     * Allocate a scatter / gather buffer descriptor that is immediately
     * followed by the buffer space of its single segment.  The GSO context
     * comes after that again.
     */
    PPDMSCATTERGATHER pSgBuf = (PPDMSCATTERGATHER)RTMemAlloc(  RT_ALIGN_Z(sizeof(*pSgBuf), 16)
                                                             + RT_ALIGN_Z(cbMin, 16)
                                                             + (pGso ? RT_ALIGN_Z(sizeof(*pGso), 16) : 0));
    if (!pSgBuf)
        return VERR_NO_MEMORY;

    /*
     * Initialize the S/G buffer and return.
     */
    pSgBuf->fFlags         = PDMSCATTERGATHER_FLAGS_MAGIC | PDMSCATTERGATHER_FLAGS_OWNER_1;
    pSgBuf->cbUsed         = 0;
    pSgBuf->cbAvailable    = RT_ALIGN_Z(cbMin, 16);
    pSgBuf->pvAllocator    = NULL;
    if (!pGso)
        pSgBuf->pvUser     = NULL;
    else
    {
        pSgBuf->pvUser     = (uint8_t *)(pSgBuf + 1) + pSgBuf->cbAvailable;
        *(PPDMNETWORKGSO)pSgBuf->pvUser = *pGso;
    }
    pSgBuf->cSegs          = 1;
    pSgBuf->aSegs[0].cbSeg = pSgBuf->cbAvailable;
    pSgBuf->aSegs[0].pvSeg = pSgBuf + 1;

#if 0 /* poison */
    memset(pSgBuf->aSegs[0].pvSeg, 'F', pSgBuf->aSegs[0].cbSeg);
#endif
    *ppSgBuf = pSgBuf;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnFreeBuf}
 */
static DECLCALLBACK(int) drvTAPNetworkUp_FreeBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf)
{
    RT_NOREF(pInterface);
#ifdef VBOX_STRICT
    PDRVTAP pThis = PDMINETWORKUP_2_DRVTAP(pInterface);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));
#endif

    if (pSgBuf)
    {
        Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
        pSgBuf->fFlags = 0;
        RTMemFree(pSgBuf);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSendBuf}
 */
static DECLCALLBACK(int) drvTAPNetworkUp_SendBuf(PPDMINETWORKUP pInterface, PPDMSCATTERGATHER pSgBuf, bool fOnWorkerThread)
{
    RT_NOREF(fOnWorkerThread);
    PDRVTAP pThis = PDMINETWORKUP_2_DRVTAP(pInterface);
    STAM_COUNTER_INC(&pThis->StatPktSent);
    STAM_COUNTER_ADD(&pThis->StatPktSentBytes, pSgBuf->cbUsed);
    STAM_PROFILE_START(&pThis->StatTransmit, a);

    AssertPtr(pSgBuf);
    Assert((pSgBuf->fFlags & PDMSCATTERGATHER_FLAGS_MAGIC_MASK) == PDMSCATTERGATHER_FLAGS_MAGIC);
    Assert(RTCritSectIsOwner(&pThis->XmitLock));

    int rc;
    if (!pSgBuf->pvUser)
    {
#ifdef LOG_ENABLED
        uint64_t u64Now = RTTimeProgramNanoTS();
        LogFlow(("drvTAPSend: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                 pSgBuf->cbUsed, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
        pThis->u64LastTransferTS = u64Now;
#endif
        Log2(("drvTAPSend: pSgBuf->aSegs[0].pvSeg=%p pSgBuf->cbUsed=%#x\n"
              "%.*Rhxd\n",
              pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, pSgBuf->cbUsed, pSgBuf->aSegs[0].pvSeg));

        rc = RTFileWrite(pThis->hFileDevice, pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, NULL);
    }
    else
    {
        uint8_t         abHdrScratch[256];
        uint8_t const  *pbFrame = (uint8_t const *)pSgBuf->aSegs[0].pvSeg;
        PCPDMNETWORKGSO pGso    = (PCPDMNETWORKGSO)pSgBuf->pvUser;
        uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, pSgBuf->cbUsed);  Assert(cSegs > 1);
        rc = VINF_SUCCESS;
        for (size_t iSeg = 0; iSeg < cSegs; iSeg++)
        {
            uint32_t cbSegFrame;
            void *pvSegFrame = PDMNetGsoCarveSegmentQD(pGso, (uint8_t *)pbFrame, pSgBuf->cbUsed, abHdrScratch,
                                                       iSeg, cSegs, &cbSegFrame);
            rc = RTFileWrite(pThis->hFileDevice, pvSegFrame, cbSegFrame, NULL);
            if (RT_FAILURE(rc))
                break;
        }
    }

    pSgBuf->fFlags = 0;
    RTMemFree(pSgBuf);

    STAM_PROFILE_STOP(&pThis->StatTransmit, a);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        rc = rc == VERR_NO_MEMORY ? VERR_NET_NO_BUFFER_SPACE : VERR_NET_DOWN;
    return rc;
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnEndXmit}
 */
static DECLCALLBACK(void) drvTAPNetworkUp_EndXmit(PPDMINETWORKUP pInterface)
{
    PDRVTAP pThis = PDMINETWORKUP_2_DRVTAP(pInterface);
    RTCritSectLeave(&pThis->XmitLock);
}


/**
 * @interface_method_impl{PDMINETWORKUP,pfnSetPromiscuousMode}
 */
static DECLCALLBACK(void) drvTAPNetworkUp_SetPromiscuousMode(PPDMINETWORKUP pInterface, bool fPromiscuous)
{
    RT_NOREF(pInterface, fPromiscuous);
    LogFlow(("drvTAPNetworkUp_SetPromiscuousMode: fPromiscuous=%d\n", fPromiscuous));
    /* nothing to do */
}


/**
 * Notification on link status changes.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   enmLinkState    The new link state.
 * @thread  EMT
 */
static DECLCALLBACK(void) drvTAPNetworkUp_NotifyLinkChanged(PPDMINETWORKUP pInterface, PDMNETWORKLINKSTATE enmLinkState)
{
    RT_NOREF(pInterface, enmLinkState);
    LogFlow(("drvTAPNetworkUp_NotifyLinkChanged: enmLinkState=%d\n", enmLinkState));
    /** @todo take action on link down and up. Stop the polling and such like. */
}


/**
 * Asynchronous I/O thread for handling receive.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   Thread          Thread handle.
 * @param   pvUser          Pointer to a DRVTAP structure.
 */
static DECLCALLBACK(int) drvTAPAsyncIoThread(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVTAP pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);
    LogFlow(("drvTAPAsyncIoThread: pThis=%p\n", pThis));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

    /*
     * Polling loop.
     */
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * Wait for something to become available.
         */
        struct pollfd aFDs[2];
        aFDs[0].fd      = RTFileToNative(pThis->hFileDevice);
        aFDs[0].events  = POLLIN | POLLPRI;
        aFDs[0].revents = 0;
        aFDs[1].fd      = RTPipeToNative(pThis->hPipeRead);
        aFDs[1].events  = POLLIN | POLLPRI | POLLERR | POLLHUP;
        aFDs[1].revents = 0;
        STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
        errno=0;
        int rc = poll(&aFDs[0], RT_ELEMENTS(aFDs), -1 /* infinite */);

        /* this might have changed in the meantime */
        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            break;

        STAM_PROFILE_ADV_START(&pThis->StatReceive, a);
        if (    rc > 0
            &&  (aFDs[0].revents & (POLLIN | POLLPRI))
            &&  !aFDs[1].revents)
        {
            /*
             * Read the frame.
             */
            char achBuf[16384];
            size_t cbRead = 0;
            /** @note At least on Linux we will never receive more than one network packet
             *        after poll() returned successfully. I don't know why but a second
             *        RTFileRead() operation will return with VERR_TRY_AGAIN in any case. */
            rc = RTFileRead(pThis->hFileDevice, achBuf, sizeof(achBuf), &cbRead);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Wait for the device to have space for this frame.
                 * Most guests use frame-sized receive buffers, hence non-zero cbMax
                 * automatically means there is enough room for entire frame. Some
                 * guests (eg. Solaris) use large chains of small receive buffers
                 * (each 128 or so bytes large). We will still start receiving as soon
                 * as cbMax is non-zero because:
                 *  - it would be quite expensive for pfnCanReceive to accurately
                 *    determine free receive buffer space
                 *  - if we were waiting for enough free buffers, there is a risk
                 *    of deadlocking because the guest could be waiting for a receive
                 *    overflow error to allocate more receive buffers
                 */
                STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
                int rc1 = pThis->pIAboveNet->pfnWaitReceiveAvail(pThis->pIAboveNet, RT_INDEFINITE_WAIT);
                STAM_PROFILE_ADV_START(&pThis->StatReceive, a);

                /*
                 * A return code != VINF_SUCCESS means that we were woken up during a VM
                 * state transition. Drop the packet and wait for the next one.
                 */
                if (RT_FAILURE(rc1))
                    continue;

                /*
                 * Pass the data up.
                 */
#ifdef LOG_ENABLED
                uint64_t u64Now = RTTimeProgramNanoTS();
                LogFlow(("drvTAPAsyncIoThread: %-4d bytes at %llu ns  deltas: r=%llu t=%llu\n",
                         cbRead, u64Now, u64Now - pThis->u64LastReceiveTS, u64Now - pThis->u64LastTransferTS));
                pThis->u64LastReceiveTS = u64Now;
#endif
                Log2(("drvTAPAsyncIoThread: cbRead=%#x\n" "%.*Rhxd\n", cbRead, cbRead, achBuf));
                STAM_COUNTER_INC(&pThis->StatPktRecv);
                STAM_COUNTER_ADD(&pThis->StatPktRecvBytes, cbRead);
                rc1 = pThis->pIAboveNet->pfnReceive(pThis->pIAboveNet, achBuf, cbRead);
                AssertRC(rc1);
            }
            else
            {
                LogFlow(("drvTAPAsyncIoThread: RTFileRead -> %Rrc\n", rc));
                if (rc == VERR_INVALID_HANDLE)
                    break;
                RTThreadYield();
            }
        }
        else if (   rc > 0
                 && aFDs[1].revents)
        {
            LogFlow(("drvTAPAsyncIoThread: Control message: enmState=%d revents=%#x\n", pThread->enmState, aFDs[1].revents));
            if (aFDs[1].revents & (POLLHUP | POLLERR | POLLNVAL))
                break;

            /* drain the pipe */
            char ch;
            size_t cbRead;
            RTPipeRead(pThis->hPipeRead, &ch, 1, &cbRead);
        }
        else
        {
            /*
             * poll() failed for some reason. Yield to avoid eating too much CPU.
             *
             * EINTR errors have been seen frequently. They should be harmless, even
             * if they are not supposed to occur in our setup.
             */
            if (errno == EINTR)
                Log(("rc=%d revents=%#x,%#x errno=%p %s\n", rc, aFDs[0].revents, aFDs[1].revents, errno, strerror(errno)));
            else
                AssertMsgFailed(("rc=%d revents=%#x,%#x errno=%p %s\n", rc, aFDs[0].revents, aFDs[1].revents, errno, strerror(errno)));
            RTThreadYield();
        }
    }


    LogFlow(("drvTAPAsyncIoThread: returns %Rrc\n", VINF_SUCCESS));
    STAM_PROFILE_ADV_STOP(&pThis->StatReceive, a);
    return VINF_SUCCESS;
}


/**
 * Unblock the send thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The pcnet device instance.
 * @param   pThread     The send thread.
 */
static DECLCALLBACK(int) drvTapAsyncIoWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    PDRVTAP pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);

    size_t cbIgnored;
    int rc = RTPipeWrite(pThis->hPipeWrite, "", 1, &cbIgnored);
    AssertRC(rc);

    return VINF_SUCCESS;
}


#if defined(RT_OS_SOLARIS)
/**
 * Calls OS-specific TAP setup application/script.
 *
 * @returns VBox error code.
 * @param   pThis           The instance data.
 */
static int drvTAPSetupApplication(PDRVTAP pThis)
{
    char szCommand[4096];

    RTStrPrintf(szCommand, sizeof(szCommand), "%s %s", pThis->pszSetupApplication,
                pThis->fStatic ? pThis->pszDeviceName : "");

    /* Pipe open the setup application. */
    Log2(("Starting TAP setup application: %s\n", szCommand));
    FILE* pfSetupHandle = popen(szCommand, "r");
    if (pfSetupHandle == 0)
    {
        LogRel(("TAP#%d: Failed to run TAP setup application: %s\n", pThis->pDrvIns->iInstance,
              pThis->pszSetupApplication, strerror(errno)));
        return VERR_HOSTIF_INIT_FAILED;
    }
    if (!pThis->fStatic)
    {
        /* Obtain device name from setup application. */
        char acBuffer[64];
        size_t cBufSize;
        fgets(acBuffer, sizeof(acBuffer), pfSetupHandle);
        cBufSize = strlen(acBuffer);
        /* The script must return the name of the interface followed by a carriage return as the
          first line of its output.  We need a null-terminated string. */
        if ((cBufSize < 2) || (acBuffer[cBufSize - 1] != '\n'))
        {
            pclose(pfSetupHandle);
            LogRel(("The TAP interface setup script did not return the name of a TAP device.\n"));
            return VERR_HOSTIF_INIT_FAILED;
        }
        /* Overwrite the terminating newline character. */
        acBuffer[cBufSize - 1] = 0;
        RTStrAPrintf(&pThis->pszDeviceName, "%s", acBuffer);
    }
    int rc = pclose(pfSetupHandle);
    if (!WIFEXITED(rc))
    {
        LogRel(("The TAP interface setup script terminated abnormally.\n"));
        return VERR_HOSTIF_INIT_FAILED;
    }
    if (WEXITSTATUS(rc) != 0)
    {
        LogRel(("The TAP interface setup script returned a non-zero exit code.\n"));
        return VERR_HOSTIF_INIT_FAILED;
    }
    return VINF_SUCCESS;
}


/**
 * Calls OS-specific TAP terminate application/script.
 *
 * @returns VBox error code.
 * @param   pThis           The instance data.
 */
static int drvTAPTerminateApplication(PDRVTAP pThis)
{
    char *pszArgs[3];
    pszArgs[0] = pThis->pszTerminateApplication;
    pszArgs[1] = pThis->pszDeviceName;
    pszArgs[2] = NULL;

    Log2(("Starting TAP terminate application: %s %s\n", pThis->pszTerminateApplication, pThis->pszDeviceName));
    RTPROCESS pid = NIL_RTPROCESS;
    int rc = RTProcCreate(pszArgs[0], pszArgs, RTENV_DEFAULT, 0, &pid);
    if (RT_SUCCESS(rc))
    {
        RTPROCSTATUS Status;
        rc = RTProcWait(pid, 0, &Status);
        if (RT_SUCCESS(rc))
        {
            if (    Status.iStatus == 0
                &&  Status.enmReason == RTPROCEXITREASON_NORMAL)
                return VINF_SUCCESS;

            LogRel(("TAP#%d: Error running TAP terminate application: %s\n", pThis->pDrvIns->iInstance, pThis->pszTerminateApplication));
        }
        else
            LogRel(("TAP#%d: RTProcWait failed for: %s\n", pThis->pDrvIns->iInstance, pThis->pszTerminateApplication));
    }
    else
    {
        /* Bad. RTProcCreate() failed! */
        LogRel(("TAP#%d: Failed to fork() process for running TAP terminate application: %s\n", pThis->pDrvIns->iInstance,
              pThis->pszTerminateApplication, strerror(errno)));
    }
    return VERR_HOSTIF_TERM_FAILED;
}

#endif /* RT_OS_SOLARIS */


#ifdef RT_OS_SOLARIS
/** From net/if_tun.h, installed by Universal TUN/TAP driver */
# define TUNNEWPPA                   (('T'<<16) | 0x0001)
/** Whether to enable ARP for TAP. */
# define VBOX_SOLARIS_TAP_ARP        1

/**
 * Creates/Attaches TAP device to IP.
 *
 * @returns VBox error code.
 * @param   pThis            The instance data.
 */
static DECLCALLBACK(int) SolarisTAPAttach(PDRVTAP pThis)
{
    LogFlow(("SolarisTapAttach: pThis=%p\n", pThis));


    int IPFileDes = open("/dev/udp", O_RDWR, 0);
    if (IPFileDes < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to open /dev/udp. errno=%d"), errno);

    int TapFileDes = open("/dev/tap", O_RDWR, 0);
    if (TapFileDes < 0)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to open /dev/tap for TAP. errno=%d"), errno);

    /* Use the PPA from the ifname if possible (e.g "tap2", then use 2 as PPA) */
    int iPPA = -1;
    if (pThis->pszDeviceName)
    {
        size_t cch = strlen(pThis->pszDeviceName);
        if (cch > 1 && RT_C_IS_DIGIT(pThis->pszDeviceName[cch - 1]) != 0)
            iPPA = pThis->pszDeviceName[cch - 1] - '0';
    }

    struct strioctl ioIF;
    ioIF.ic_cmd = TUNNEWPPA;
    ioIF.ic_len = sizeof(iPPA);
    ioIF.ic_dp = (char *)(&iPPA);
    ioIF.ic_timout = 0;
    iPPA = ioctl(TapFileDes, I_STR, &ioIF);
    if (iPPA < 0)
    {
        close(TapFileDes);
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Failed to get new interface. errno=%d"), errno);
    }

    int InterfaceFD = open("/dev/tap", O_RDWR, 0);
    if (!InterfaceFD)
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_PDM_HIF_OPEN_FAILED, RT_SRC_POS,
                                   N_("Failed to open interface /dev/tap. errno=%d"), errno);

    if (ioctl(InterfaceFD, I_PUSH, "ip") == -1)
    {
        close(InterfaceFD);
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Failed to push IP. errno=%d"), errno);
    }

    struct lifreq ifReq;
    memset(&ifReq, 0, sizeof(ifReq));
    if (ioctl(InterfaceFD, SIOCGLIFFLAGS, &ifReq) == -1)
        LogRel(("TAP#%d: Failed to get interface flags.\n", pThis->pDrvIns->iInstance));

    ifReq.lifr_ppa = iPPA;
    RTStrCopy(ifReq.lifr_name, sizeof(ifReq.lifr_name), pThis->pszDeviceName);

    if (ioctl(InterfaceFD, SIOCSLIFNAME, &ifReq) == -1)
        LogRel(("TAP#%d: Failed to set PPA. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    if (ioctl(InterfaceFD, SIOCGLIFFLAGS, &ifReq) == -1)
        LogRel(("TAP#%d: Failed to get interface flags after setting PPA. errno=%d\n", pThis->pDrvIns->iInstance, errno));

# ifdef VBOX_SOLARIS_TAP_ARP
    /* Interface */
    if (ioctl(InterfaceFD, I_PUSH, "arp") == -1)
        LogRel(("TAP#%d: Failed to push ARP to Interface FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    /* IP */
    if (ioctl(IPFileDes, I_POP, NULL) == -1)
        LogRel(("TAP#%d: Failed I_POP from IP FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    if (ioctl(IPFileDes, I_PUSH, "arp") == -1)
        LogRel(("TAP#%d: Failed to push ARP to IP FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    /* ARP */
    int ARPFileDes = open("/dev/tap", O_RDWR, 0);
    if (ARPFileDes < 0)
        LogRel(("TAP#%d: Failed to open for /dev/tap for ARP. errno=%d", pThis->pDrvIns->iInstance, errno));

    if (ioctl(ARPFileDes, I_PUSH, "arp") == -1)
        LogRel(("TAP#%d: Failed to push ARP to ARP FD. errno=%d\n", pThis->pDrvIns->iInstance, errno));

    ioIF.ic_cmd = SIOCSLIFNAME;
    ioIF.ic_timout = 0;
    ioIF.ic_len = sizeof(ifReq);
    ioIF.ic_dp = (char *)&ifReq;
    if (ioctl(ARPFileDes, I_STR, &ioIF) == -1)
        LogRel(("TAP#%d: Failed to set interface name to ARP.\n", pThis->pDrvIns->iInstance));
# endif

    /* We must use I_LINK and not I_PLINK as I_PLINK makes the link persistent.
     * Then we would not be able unlink the interface if we reuse it.
     * Even 'unplumb' won't work after that.
     */
    int IPMuxID = ioctl(IPFileDes, I_LINK, InterfaceFD);
    if (IPMuxID == -1)
    {
        close(InterfaceFD);
# ifdef VBOX_SOLARIS_TAP_ARP
        close(ARPFileDes);
# endif
        LogRel(("TAP#%d: Cannot link TAP device to IP.\n", pThis->pDrvIns->iInstance));
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                    N_("Failed to link TAP device to IP. Check TAP interface name. errno=%d"), errno);
    }

# ifdef VBOX_SOLARIS_TAP_ARP
    int ARPMuxID = ioctl(IPFileDes, I_LINK, ARPFileDes);
    if (ARPMuxID == -1)
        LogRel(("TAP#%d: Failed to link TAP device to ARP\n", pThis->pDrvIns->iInstance));

    close(ARPFileDes);
# endif
    close(InterfaceFD);

    /* Reuse ifReq */
    memset(&ifReq, 0, sizeof(ifReq));
    RTStrCopy(ifReq.lifr_name, sizeof(ifReq.lifr_name), pThis->pszDeviceName);
    ifReq.lifr_ip_muxid  = IPMuxID;
# ifdef VBOX_SOLARIS_TAP_ARP
    ifReq.lifr_arp_muxid = ARPMuxID;
# endif

    if (ioctl(IPFileDes, SIOCSLIFMUXID, &ifReq) == -1)
    {
# ifdef VBOX_SOLARIS_TAP_ARP
        ioctl(IPFileDes, I_PUNLINK, ARPMuxID);
# endif
        ioctl(IPFileDes, I_PUNLINK, IPMuxID);
        close(IPFileDes);
        LogRel(("TAP#%d: Failed to set Mux ID.\n", pThis->pDrvIns->iInstance));
        return PDMDrvHlpVMSetError(pThis->pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Failed to set Mux ID. Check TAP interface name. errno=%d"), errno);
    }

    int rc = RTFileFromNative(&pThis->hFileDevice, TapFileDes);
    AssertLogRelRC(rc);
    if (RT_FAILURE(rc))
    {
        close(IPFileDes);
        close(TapFileDes);
    }
    pThis->iIPFileDes = IPFileDes;

    return VINF_SUCCESS;
}

#endif  /* RT_OS_SOLARIS */

/* -=-=-=-=- PDMIBASE -=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvTAPQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS  pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVTAP     pThis   = PDMINS_2_DATA(pDrvIns, PDRVTAP);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKUP, &pThis->INetworkUp);
    return NULL;
}

/* -=-=-=-=- PDMDRVREG -=-=-=-=- */

/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvTAPDestruct(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvTAPDestruct\n"));
    PDRVTAP pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

    /*
     * Terminate the control pipe.
     */
    int rc;
    if (pThis->hPipeWrite != NIL_RTPIPE)
    {
        rc = RTPipeClose(pThis->hPipeWrite); AssertRC(rc);
        pThis->hPipeWrite = NIL_RTPIPE;
    }
    if (pThis->hPipeRead != NIL_RTPIPE)
    {
        rc = RTPipeClose(pThis->hPipeRead); AssertRC(rc);
        pThis->hPipeRead = NIL_RTPIPE;
    }

#ifdef RT_OS_SOLARIS
    /** @todo r=bird: This *does* need checking against ConsoleImpl2.cpp if used on non-solaris systems. */
    if (pThis->hFileDevice != NIL_RTFILE)
    {
        int rc = RTFileClose(pThis->hFileDevice); AssertRC(rc);
        pThis->hFileDevice = NIL_RTFILE;
    }

    /*
     * Call TerminateApplication after closing the device otherwise
     * TerminateApplication would not be able to unplumb it.
     */
    if (pThis->pszTerminateApplication)
        drvTAPTerminateApplication(pThis);

#endif  /* RT_OS_SOLARIS */

#ifdef RT_OS_SOLARIS
    if (!pThis->fStatic)
        RTStrFree(pThis->pszDeviceName);    /* allocated by drvTAPSetupApplication */
    else
        PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszDeviceName);
#else
    PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszDeviceName);
#endif
    pThis->pszDeviceName = NULL;
    PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszSetupApplication);
    pThis->pszSetupApplication = NULL;
    PDMDrvHlpMMHeapFree(pDrvIns, pThis->pszTerminateApplication);
    pThis->pszTerminateApplication = NULL;

    /*
     * Kill the xmit lock.
     */
    if (RTCritSectIsInitialized(&pThis->XmitLock))
        RTCritSectDelete(&pThis->XmitLock);

#ifdef VBOX_WITH_STATISTICS
    /*
     * Deregister statistics.
     */
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktSent);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktSentBytes);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktRecv);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatPktRecvBytes);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatTransmit);
    PDMDrvHlpSTAMDeregister(pDrvIns, &pThis->StatReceive);
#endif /* VBOX_WITH_STATISTICS */
}


/**
 * Construct a TAP network transport driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvTAPConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVTAP         pThis = PDMINS_2_DATA(pDrvIns, PDRVTAP);
    PCPDMDRVHLPR3   pHlp  = pDrvIns->pHlpR3;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                      = pDrvIns;
    pThis->hFileDevice                  = NIL_RTFILE;
    pThis->hPipeWrite                   = NIL_RTPIPE;
    pThis->hPipeRead                    = NIL_RTPIPE;
    pThis->pszDeviceName                = NULL;
#ifdef RT_OS_SOLARIS
    pThis->iIPFileDes                   = -1;
    pThis->fStatic                      = true;
#endif
    pThis->pszSetupApplication          = NULL;
    pThis->pszTerminateApplication      = NULL;

    /* IBase */
    pDrvIns->IBase.pfnQueryInterface    = drvTAPQueryInterface;
    /* INetwork */
    pThis->INetworkUp.pfnBeginXmit              = drvTAPNetworkUp_BeginXmit;
    pThis->INetworkUp.pfnAllocBuf               = drvTAPNetworkUp_AllocBuf;
    pThis->INetworkUp.pfnFreeBuf                = drvTAPNetworkUp_FreeBuf;
    pThis->INetworkUp.pfnSendBuf                = drvTAPNetworkUp_SendBuf;
    pThis->INetworkUp.pfnEndXmit                = drvTAPNetworkUp_EndXmit;
    pThis->INetworkUp.pfnSetPromiscuousMode     = drvTAPNetworkUp_SetPromiscuousMode;
    pThis->INetworkUp.pfnNotifyLinkChanged      = drvTAPNetworkUp_NotifyLinkChanged;

#ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSent,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of sent packets.",          "/Drivers/TAP%d/Packets/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktSentBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of sent bytes.",            "/Drivers/TAP%d/Bytes/Sent", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecv,       STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_OCCURENCES,        "Number of received packets.",      "/Drivers/TAP%d/Packets/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatPktRecvBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,             "Number of received bytes.",        "/Drivers/TAP%d/Bytes/Received", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatTransmit,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet transmit runs.",  "/Drivers/TAP%d/Transmit", pDrvIns->iInstance);
    PDMDrvHlpSTAMRegisterF(pDrvIns, &pThis->StatReceive,       STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,    "Profiling packet receive runs.",   "/Drivers/TAP%d/Receive", pDrvIns->iInstance);
#endif /* VBOX_WITH_STATISTICS */

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "Device"
                                           "|FileHandle"
                                           "|TAPSetupApplication"
                                           "|TAPTerminateApplication"
                                           "|MAC",
                                           "");

    /*
     * Check that no-one is attached to us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Query the network port interface.
     */
    pThis->pIAboveNet = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMINETWORKDOWN);
    if (!pThis->pIAboveNet)
        return PDMDRV_SET_ERROR(pDrvIns, VERR_PDM_MISSING_INTERFACE_ABOVE,
                                N_("Configuration error: The above device/driver didn't export the network port interface"));

    /*
     * Read the configuration.
     */
    int rc;
#if defined(RT_OS_SOLARIS)   /** @todo Other platforms' TAP code should be moved here from ConsoleImpl. */
    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "TAPSetupApplication", &pThis->pszSetupApplication);
    if (RT_SUCCESS(rc))
    {
        if (!RTPathExists(pThis->pszSetupApplication))
            return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                       N_("Invalid TAP setup program path: %s"), pThis->pszSetupApplication);
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Configuration error: failed to query \"TAPTerminateApplication\""));

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "TAPTerminateApplication", &pThis->pszTerminateApplication);
    if (RT_SUCCESS(rc))
    {
        if (!RTPathExists(pThis->pszTerminateApplication))
            return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                       N_("Invalid TAP terminate program path: %s"), pThis->pszTerminateApplication);
    }
    else if (rc != VERR_CFGM_VALUE_NOT_FOUND)
        return PDMDRV_SET_ERROR(pDrvIns, rc, N_("Configuration error: failed to query \"TAPTerminateApplication\""));

    rc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "Device", &pThis->pszDeviceName);
    if (RT_FAILURE(rc))
        pThis->fStatic = false;

    /* Obtain the device name from the setup application (if none was specified). */
    if (pThis->pszSetupApplication)
    {
        rc = drvTAPSetupApplication(pThis);
        if (RT_FAILURE(rc))
            return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_INIT_FAILED, RT_SRC_POS,
                                       N_("Error running TAP setup application. rc=%d"), rc);
    }

    /*
     * Do the setup.
     */
    rc = SolarisTAPAttach(pThis);
    if (RT_FAILURE(rc))
        return rc;

#else /* !RT_OS_SOLARIS */

    uint64_t u64File;
    rc = pHlp->pfnCFGMQueryU64(pCfg, "FileHandle", &u64File);
    if (RT_FAILURE(rc))
        return PDMDRV_SET_ERROR(pDrvIns, rc,
                                N_("Configuration error: Query for \"FileHandle\" 32-bit signed integer failed"));
    pThis->hFileDevice = (RTFILE)(uintptr_t)u64File;
    if (!RTFileIsValid(pThis->hFileDevice))
        return PDMDrvHlpVMSetError(pDrvIns, VERR_INVALID_HANDLE, RT_SRC_POS,
                                   N_("The TAP file handle %RTfile is not valid"), pThis->hFileDevice);
#endif /* !RT_OS_SOLARIS */

    /*
     * Create the transmit lock.
     */
    rc = RTCritSectInit(&pThis->XmitLock);
    AssertRCReturn(rc, rc);

    /*
     * Make sure the descriptor is non-blocking and valid.
     *
     * We should actually query if it's a TAP device, but I haven't
     * found any way to do that.
     */
    if (fcntl(RTFileToNative(pThis->hFileDevice), F_SETFL, O_NONBLOCK) == -1)
        return PDMDrvHlpVMSetError(pDrvIns, VERR_HOSTIF_IOCTL, RT_SRC_POS,
                                   N_("Configuration error: Failed to configure /dev/net/tun. errno=%d"), errno);
    /** @todo determine device name. This can be done by reading the link /proc/<pid>/fd/<fd> */
    Log(("drvTAPContruct: %d (from fd)\n", (intptr_t)pThis->hFileDevice));
    rc = VINF_SUCCESS;

    /*
     * Create the control pipe.
     */
    rc = RTPipeCreate(&pThis->hPipeRead, &pThis->hPipeWrite, 0 /*fFlags*/);
    AssertRCReturn(rc, rc);

    /*
     * Create the async I/O thread.
     */
    rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pThread, pThis, drvTAPAsyncIoThread, drvTapAsyncIoWakeup, 128 * _1K, RTTHREADTYPE_IO, "TAP");
    AssertRCReturn(rc, rc);

    return rc;
}


/**
 * TAP network transport driver registration record.
 */
const PDMDRVREG g_DrvHostInterface =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "HostInterface",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "TAP Network Transport Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_NETWORK,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVTAP),
    /* pfnConstruct */
    drvTAPConstruct,
    /* pfnDestruct */
    drvTAPDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL, /** @todo Do power on, suspend and resume handlers! */
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

