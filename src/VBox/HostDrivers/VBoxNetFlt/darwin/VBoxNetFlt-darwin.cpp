/* $Id: VBoxNetFlt-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFlt - Network Filter Driver (Host), Darwin Specific Code.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_NET_FLT_DRV
#include "../../../Runtime/r0drv/darwin/the-darwin-kernel.h"

#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/intnetinline.h>
#include <VBox/version.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/spinlock.h>
#include <iprt/semaphore.h>
#include <iprt/process.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/time.h>
#include <iprt/net.h>
#include <iprt/thread.h>

#include "../../darwin/VBoxNetSend.h"

#include <mach/kmod.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/ioccom.h>
#include <sys/filio.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/kern_event.h>
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 101500 /* The 10.15 SDK has a slightly butchered API deprecation attempt. */
# pragma clang diagnostic push
# pragma clang diagnostic ignored "-Wmacro-redefined"      /* Each header redefines __NKE_API_DEPRECATED. */
# pragma clang diagnostic ignored "-Wmissing-declarations" /* Misplaced __NKE_API_DEPRECATED; in kpi_mbuf.h. */
# include <sys/kpi_socket.h>
# include <net/kpi_interface.h>
# include <sys/kpi_mbuf.h>
# include <net/kpi_interfacefilter.h>
# pragma clang diagnostic pop
#else /* < 10.15*/
# include <sys/kpi_socket.h>
# include <net/kpi_interface.h>
RT_C_DECLS_BEGIN /* Buggy 10.4 headers, fixed in 10.5. */
# include <sys/kpi_mbuf.h>
# include <net/kpi_interfacefilter.h>
RT_C_DECLS_END
#endif /* < 10.15*/


#include <net/if.h>
#include <net/if_var.h>
RT_C_DECLS_BEGIN
#include <net/bpf.h>
RT_C_DECLS_END
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/in6_var.h>

#define VBOXNETFLT_OS_SPECFIC 1
#include "../VBoxNetFltInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The maximum number of SG segments.
 * Used to prevent stack overflow and similar bad stuff. */
#define VBOXNETFLT_DARWIN_MAX_SEGS      32

#if 0
/** For testing extremely segmented frames. */
#define VBOXNETFLT_DARWIN_TEST_SEG_SIZE 14
#endif

/* XXX: hidden undef #ifdef __APPLE__ */
#define VBOX_IN_LOOPBACK(addr)  (((addr) & IN_CLASSA_NET) == 0x7f000000)
#define VBOX_IN_LINKLOCAL(addr) (((addr) & IN_CLASSB_NET) == 0xa9fe0000)



/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
static kern_return_t    VBoxNetFltDarwinStart(struct kmod_info *pKModInfo, void *pvData);
static kern_return_t    VBoxNetFltDarwinStop(struct kmod_info *pKModInfo, void *pvData);

static void vboxNetFltDarwinSysSockUpcall(socket_t pSysSock, void *pvData, int fWait);
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The mbuf tag data.
 *
 * We have to associate the ethernet header with each packet we're sending
 * because things like icmp will inherit the tag it self so the tag along
 * isn't sufficient to identify our mbufs. For the icmp scenario the ethernet
 * header naturally changes before the packet is send pack, so let check it.
 */
typedef struct VBOXNETFLTTAG
{
    /** The ethernet header of the outgoing frame. */
    RTNETETHERHDR EthHdr;
} VBOXNETFLTTAG;
/** Pointer to a VBoxNetFlt mbuf tag. */
typedef VBOXNETFLTTAG *PVBOXNETFLTTAG;
/** Pointer to a const VBoxNetFlt mbuf tag. */
typedef VBOXNETFLTTAG const *PCVBOXNETFLTTAG;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Declare the module stuff.
 */
RT_C_DECLS_BEGIN
extern kern_return_t _start(struct kmod_info *pKModInfo, void *pvData);
extern kern_return_t _stop(struct kmod_info *pKModInfo, void *pvData);

KMOD_EXPLICIT_DECL(VBoxNetFlt, VBOX_VERSION_STRING, _start, _stop)
DECL_HIDDEN_DATA(kmod_start_func_t *) _realmain = VBoxNetFltDarwinStart;
DECL_HIDDEN_DATA(kmod_stop_func_t  *) _antimain = VBoxNetFltDarwinStop;
DECL_HIDDEN_DATA(int)                 _kext_apple_cc = __APPLE_CC__;
RT_C_DECLS_END


/**
 * The (common) global data.
 */
static VBOXNETFLTGLOBALS g_VBoxNetFltGlobals;

/** The unique tag id for this module.
 * This is basically a unique string hash that lives on until reboot.
 * It is used for tagging mbufs. */
static mbuf_tag_id_t g_idTag;

/** The offset of the struct ifnet::if_pcount variable.
 * @remarks Initial value is valid for Lion and earlier. We adjust it on attach
 *          for later releases.  */
static unsigned g_offIfNetPCount = sizeof(void *) * (1 /*if_softc*/ + 1 /*if_name*/ + 2 /*if_link*/ + 2 /*if_addrhead*/ + 1 /*if_check_multi*/)
                                 + sizeof(u_long) /*if_refcnt*/;
/** Macro for accessing ifnet::if_pcount. */
#define VBOX_GET_PCOUNT(pIfNet) ( *(int *)((uintptr_t)pIfNet + g_offIfNetPCount) )
/** The size of area of ifnet structure we try to locate if_pcount in. */
#define VBOXNETFLT_DARWIN_IFNET_SIZE 256
/** Indicates whether g_offIfNetPCount has been adjusted already (no point in
 * doing it more than once). */
static bool g_fNetPCountFound  = false;


/**
 * Change the promiscuous setting and try spot the changed in @a pIfNet.
 *
 * @returns Offset of potential p_count field.
 * @param   pIfNet      The interface we're attaching to.
 * @param   iPromisc    Whether to enable (1) or disable (0) promiscuous mode.
 *
 * @note    This implementation relies on if_pcount to be aligned on sizeof(int).
 */
static unsigned vboxNetFltDarwinSetAndDiff(ifnet_t pIfNet, int iPromisc)
{
    int aiSavedState[VBOXNETFLT_DARWIN_IFNET_SIZE / sizeof(int)];
    memcpy(aiSavedState, pIfNet, sizeof(aiSavedState));

    ifnet_set_promiscuous(pIfNet, iPromisc);

    int const iDiff = iPromisc ? 1 : -1;

    /*
     * We assume that ifnet structure will never have less members in front of if_pcount
     * than it used to have in Lion. If this turns out to be false assumption we will
     * have to start from zero offset.
     */
    for (unsigned i = g_offIfNetPCount / sizeof(int); i < RT_ELEMENTS(aiSavedState); i++)
        if (((int*)pIfNet)[i] - aiSavedState[i] == iDiff)
            return i * sizeof(int);

    return 0;
}


/**
 * Detect and adjust the offset of ifnet::if_pcount.
 *
 * @param   pIfNet      The interface we're attaching to.
 */
static void vboxNetFltDarwinDetectPCountOffset(ifnet_t pIfNet)
{
    if (g_fNetPCountFound)
        return;

    /*
     * It would be nice to use locking at this point, but it is not available via KPI.
     * This is why we try several times. At each attempt we modify if_pcount four times
     * to rule out false detections.
     */
    unsigned offTry1, offTry2, offTry3, offTry4;
    for (int iAttempt = 0; iAttempt < 3; iAttempt++)
    {
        offTry1 = vboxNetFltDarwinSetAndDiff(pIfNet, 1);
        offTry2 = vboxNetFltDarwinSetAndDiff(pIfNet, 1);
        offTry3 = vboxNetFltDarwinSetAndDiff(pIfNet, 0);
        offTry4 = vboxNetFltDarwinSetAndDiff(pIfNet, 0);
        if (offTry1 == offTry2 && offTry2 == offTry3 && offTry3 == offTry4)
        {
            if (g_offIfNetPCount != offTry1)
            {
                Log(("VBoxNetFltDarwinDetectPCountOffset: Adjusted if_pcount offset to %x from %x.\n", offTry1, g_offIfNetPCount));
                g_offIfNetPCount = offTry1;
                g_fNetPCountFound = true;
            }
            break;
        }
    }

    if (g_offIfNetPCount != offTry1)
        LogRel(("VBoxNetFlt: Failed to detect promiscuous count, all traffic may reach wire (%x != %x).\n", g_offIfNetPCount, offTry1));
}


/**
 * Start the kernel module.
 */
static kern_return_t    VBoxNetFltDarwinStart(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);

    /*
     * Initialize IPRT and find our module tag id.
     * (IPRT is shared with VBoxDrv, it creates the loggers.)
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        Log(("VBoxNetFltDarwinStart\n"));
        errno_t err = mbuf_tag_id_find("org.VirtualBox.kext.VBoxFltDrv", &g_idTag);
        if (!err)
        {
            /*
             * Initialize the globals and connect to the support driver.
             *
             * This will call back vboxNetFltOsOpenSupDrv (and maybe vboxNetFltOsCloseSupDrv)
             * for establishing the connect to the support driver.
             */
            memset(&g_VBoxNetFltGlobals, 0, sizeof(g_VBoxNetFltGlobals));
            rc = vboxNetFltInitGlobalsAndIdc(&g_VBoxNetFltGlobals);
            if (RT_SUCCESS(rc))
            {
                LogRel(("VBoxFltDrv: version " VBOX_VERSION_STRING " r%d\n", VBOX_SVN_REV));
                return KMOD_RETURN_SUCCESS;
            }

            LogRel(("VBoxFltDrv: failed to initialize device extension (rc=%d)\n", rc));
        }
        else
            LogRel(("VBoxFltDrv: mbuf_tag_id_find failed, err=%d\n", err));
        RTR0Term();
    }
    else
        printf("VBoxFltDrv: failed to initialize IPRT (rc=%d)\n", rc);

    memset(&g_VBoxNetFltGlobals, 0, sizeof(g_VBoxNetFltGlobals));
    return KMOD_RETURN_FAILURE;
}


/**
 * Stop the kernel module.
 */
static kern_return_t VBoxNetFltDarwinStop(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);
    Log(("VBoxNetFltDarwinStop\n"));

    /*
     * Refuse to unload if anyone is currently using the filter driver.
     * This is important as I/O kit / xnu will to be able to do usage
     * tracking for us!
     */
    int rc = vboxNetFltTryDeleteIdcAndGlobals(&g_VBoxNetFltGlobals);
    if (RT_FAILURE(rc))
    {
        Log(("VBoxNetFltDarwinStop - failed, busy.\n"));
        return KMOD_RETURN_FAILURE;
    }

    /*
     * Undo the work done during start (in reverse order).
     */
    memset(&g_VBoxNetFltGlobals, 0, sizeof(g_VBoxNetFltGlobals));

    RTR0Term();

    return KMOD_RETURN_SUCCESS;
}


/**
 * Reads and retains the host interface handle.
 *
 * @returns The handle, NULL if detached.
 * @param   pThis
 */
DECLINLINE(ifnet_t) vboxNetFltDarwinRetainIfNet(PVBOXNETFLTINS pThis)
{
    ifnet_t pIfNet = NULL;

    /*
     * Be careful here to avoid problems racing the detached callback.
     */
    RTSpinlockAcquire(pThis->hSpinlock);
    if (!ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost))
    {
        pIfNet = ASMAtomicUoReadPtrT(&pThis->u.s.pIfNet, ifnet_t);
        if (pIfNet)
            ifnet_reference(pIfNet);
    }
    RTSpinlockRelease(pThis->hSpinlock);

    return pIfNet;
}


/**
 * Release the host interface handle previously retained
 * by vboxNetFltDarwinRetainIfNet.
 *
 * @param   pThis           The instance.
 * @param   pIfNet          The vboxNetFltDarwinRetainIfNet return value, NULL is fine.
 */
DECLINLINE(void) vboxNetFltDarwinReleaseIfNet(PVBOXNETFLTINS pThis, ifnet_t pIfNet)
{
    NOREF(pThis);
    if (pIfNet)
        ifnet_release(pIfNet);
}


/**
 * Checks whether this is an mbuf created by vboxNetFltDarwinMBufFromSG,
 * i.e. a buffer which we're pushing and should be ignored by the filter callbacks.
 *
 * @returns true / false accordingly.
 * @param   pThis           The instance.
 * @param   pMBuf           The mbuf.
 * @param   pvFrame         The frame pointer, optional.
 */
DECLINLINE(bool) vboxNetFltDarwinMBufIsOur(PVBOXNETFLTINS pThis, mbuf_t pMBuf, void *pvFrame)
{
    NOREF(pThis);

    /*
     * Lookup the tag set by vboxNetFltDarwinMBufFromSG.
     */
    PCVBOXNETFLTTAG pTagData;
    size_t cbTagData;
    errno_t err = mbuf_tag_find(pMBuf, g_idTag, 0 /* type */, &cbTagData, (void **)&pTagData);
    if (err)
        return false;
    AssertReturn(cbTagData == sizeof(*pTagData), false);

    /*
     * Dig out the ethernet header from the mbuf.
     */
    PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pvFrame;
    if (!pEthHdr)
        pEthHdr = (PCRTNETETHERHDR)mbuf_pkthdr_header(pMBuf);
    if (!pEthHdr)
        pEthHdr = (PCRTNETETHERHDR)mbuf_data(pMBuf);
    /* ASSUMING that there is enough data to work on! */
    if (    pEthHdr->DstMac.au8[0] != pTagData->EthHdr.DstMac.au8[0]
        ||  pEthHdr->DstMac.au8[1] != pTagData->EthHdr.DstMac.au8[1]
        ||  pEthHdr->DstMac.au8[2] != pTagData->EthHdr.DstMac.au8[2]
        ||  pEthHdr->DstMac.au8[3] != pTagData->EthHdr.DstMac.au8[3]
        ||  pEthHdr->DstMac.au8[4] != pTagData->EthHdr.DstMac.au8[4]
        ||  pEthHdr->DstMac.au8[5] != pTagData->EthHdr.DstMac.au8[5]
        ||  pEthHdr->SrcMac.au8[0] != pTagData->EthHdr.SrcMac.au8[0]
        ||  pEthHdr->SrcMac.au8[1] != pTagData->EthHdr.SrcMac.au8[1]
        ||  pEthHdr->SrcMac.au8[2] != pTagData->EthHdr.SrcMac.au8[2]
        ||  pEthHdr->SrcMac.au8[3] != pTagData->EthHdr.SrcMac.au8[3]
        ||  pEthHdr->SrcMac.au8[4] != pTagData->EthHdr.SrcMac.au8[4]
        ||  pEthHdr->SrcMac.au8[5] != pTagData->EthHdr.SrcMac.au8[5]
        ||  pEthHdr->EtherType     != pTagData->EthHdr.EtherType)
    {
        Log3(("tagged, but the ethernet header has changed\n"));
        return false;
    }

    return true;
}


/**
 * Internal worker that create a darwin mbuf for a (scatter/)gather list.
 *
 * @returns Pointer to the mbuf.
 * @param   pThis           The instance.
 * @param   pSG             The (scatter/)gather list.
 */
static mbuf_t vboxNetFltDarwinMBufFromSG(PVBOXNETFLTINS pThis, PINTNETSG pSG)
{
    /// @todo future? mbuf_how_t How = preemption enabled ? MBUF_DONTWAIT : MBUF_WAITOK;
    mbuf_how_t How = MBUF_WAITOK;

    /*
     * We need some way of getting back to our instance data when
     * the mbuf is freed, so use pvUserData for this.
     *  -- this is not relevant anylonger! --
     */
    Assert(!pSG->pvUserData || pSG->pvUserData == pThis);
    Assert(!pSG->pvUserData2);
    pSG->pvUserData = pThis;

    /*
     * Allocate a packet and copy over the data.
     *
     * Using mbuf_attachcluster() here would've been nice but there are two
     * issues with it: (1) it's 10.5.x only, and (2) the documentation indicates
     * that it's not supposed to be used for really external buffers. The 2nd
     * point might be argued against considering that the only m_clattach user
     * is mallocs memory for the ext mbuf and not doing what's stated in the docs.
     * However, it's hard to tell if these m_clattach buffers actually makes it
     * to the NICs or not, and even if they did, the NIC would need the physical
     * addresses for the pages they contain and might end up copying the data
     * to a new mbuf anyway.
     *
     * So, in the end it's better to just do it the simple way that will work
     * 100%, even if it involves some extra work (alloc + copy) we really wished
     * to avoid.
     *
     * Note. We can't make use of the physical addresses on darwin because the
     *       way the mbuf / cluster stuff works (see mbuf_data_to_physical and
     *       mcl_to_paddr).
     */
    mbuf_t pPkt = NULL;
    errno_t err = mbuf_allocpacket(How, pSG->cbTotal, NULL, &pPkt);
    if (!err)
    {
        /* Skip zero sized memory buffers (paranoia). */
        mbuf_t pCur = pPkt;
        while (pCur && !mbuf_maxlen(pCur))
            pCur = mbuf_next(pCur);
        Assert(pCur);

        /* Set the required packet header attributes. */
        mbuf_pkthdr_setlen(pPkt, pSG->cbTotal);
        mbuf_pkthdr_setheader(pPkt, mbuf_data(pCur));

        /* Special case the single buffer copy. */
        if (    mbuf_next(pCur)
            &&  mbuf_maxlen(pCur) >= pSG->cbTotal)
        {
            mbuf_setlen(pCur, pSG->cbTotal);
            IntNetSgRead(pSG, mbuf_data(pCur));
        }
        else
        {
            /* Multi buffer copying. */
            size_t  cbLeft = pSG->cbTotal;
            size_t  offSrc = 0;
            while (cbLeft > 0 && pCur)
            {
                size_t cb = mbuf_maxlen(pCur);
                if (cb > cbLeft)
                    cb = cbLeft;
                mbuf_setlen(pCur, cb);
                IntNetSgReadEx(pSG, offSrc, cb, mbuf_data(pCur));

                /* advance */
                offSrc += cb;
                cbLeft -= cb;
                pCur = mbuf_next(pCur);
            }
            Assert(cbLeft == 0);
        }
        if (!err)
        {
            /*
             * Tag the packet and return successfully.
             */
            PVBOXNETFLTTAG pTagData;
            err = mbuf_tag_allocate(pPkt, g_idTag, 0 /* type */, sizeof(VBOXNETFLTTAG) /* tag len */, How, (void **)&pTagData);
            if (!err)
            {
                Assert(pSG->aSegs[0].cb >= sizeof(pTagData->EthHdr));
                memcpy(&pTagData->EthHdr, pSG->aSegs[0].pv, sizeof(pTagData->EthHdr));
                return pPkt;
            }

            /* bailout: */
            AssertMsg(err == ENOMEM || err == EWOULDBLOCK, ("err=%d\n", err));
        }

        mbuf_freem(pPkt);
    }
    else
        AssertMsg(err == ENOMEM || err == EWOULDBLOCK, ("err=%d\n", err));
    pSG->pvUserData = NULL;

    return NULL;
}


/**
 * Calculates the number of segments required to represent the mbuf.
 *
 * @returns Number of segments.
 * @param   pThis               The instance.
 * @param   pMBuf               The mbuf.
 * @param   pvFrame             The frame pointer, optional.
 */
DECLINLINE(unsigned) vboxNetFltDarwinMBufCalcSGSegs(PVBOXNETFLTINS pThis, mbuf_t pMBuf, void *pvFrame)
{
    NOREF(pThis);

    /*
     * Count the buffers in the chain.
     */
    unsigned cSegs = 0;
    for (mbuf_t pCur = pMBuf; pCur; pCur = mbuf_next(pCur))
        if (mbuf_len(pCur))
            cSegs++;
        else if (   !cSegs
                 && pvFrame
                 && (uintptr_t)pvFrame - (uintptr_t)mbuf_datastart(pMBuf) < mbuf_maxlen(pMBuf))
            cSegs++;

#ifdef PADD_RUNT_FRAMES_FROM_HOST
    /*
     * Add one buffer if the total is less than the ethernet minimum 60 bytes.
     * This may allocate a segment too much if the ethernet header is separated,
     * but that shouldn't harm us much.
     */
    if (mbuf_pkthdr_len(pMBuf) < 60)
        cSegs++;
#endif

#ifdef VBOXNETFLT_DARWIN_TEST_SEG_SIZE
    /* maximize the number of segments. */
    cSegs = RT_MAX(VBOXNETFLT_DARWIN_MAX_SEGS - 1, cSegs);
#endif

    return cSegs ? cSegs : 1;
}


/**
 * Initializes a SG list from an mbuf.
 *
 * @param   pThis               The instance.
 * @param   pMBuf               The mbuf.
 * @param   pSG                 The SG.
 * @param   pvFrame             The frame pointer, optional.
 * @param   cSegs               The number of segments allocated for the SG.
 *                              This should match the number in the mbuf exactly!
 * @param   fSrc                The source of the frame.
 */
DECLINLINE(void) vboxNetFltDarwinMBufToSG(PVBOXNETFLTINS pThis, mbuf_t pMBuf, void *pvFrame, PINTNETSG pSG, unsigned cSegs,
                                          uint32_t fSrc)
{
    RT_NOREF(pThis, fSrc);

    /*
     * Walk the chain and convert the buffers to segments.  Works INTNETSG::cbTotal.
     */
    unsigned iSeg = 0;
    IntNetSgInitTempSegs(pSG, 0 /*cbTotal*/, cSegs, 0 /*cSegsUsed*/);
    for (mbuf_t pCur = pMBuf; pCur; pCur = mbuf_next(pCur))
    {
        size_t cbSeg = mbuf_len(pCur);
        if (cbSeg)
        {
            void *pvSeg = mbuf_data(pCur);

            /* deal with pvFrame */
            if (!iSeg && pvFrame && pvFrame != pvSeg)
            {
                void     *pvStart   = mbuf_datastart(pMBuf);
                uintptr_t offSeg    = (uintptr_t)pvSeg - (uintptr_t)pvStart;
                uintptr_t offSegEnd = offSeg + cbSeg;
                Assert(pvStart && pvSeg && offSeg < mbuf_maxlen(pMBuf) && offSegEnd <= mbuf_maxlen(pMBuf)); NOREF(offSegEnd);
                uintptr_t offFrame  = (uintptr_t)pvFrame - (uintptr_t)pvStart;
                if (RT_LIKELY(offFrame < offSeg))
                {
                    pvSeg = pvFrame;
                    cbSeg += offSeg - offFrame;
                }
                else
                    AssertMsgFailed(("pvFrame=%p pvStart=%p pvSeg=%p offSeg=%p cbSeg=%#zx offSegEnd=%p offFrame=%p maxlen=%#zx\n",
                                     pvFrame, pvStart, pvSeg, offSeg, cbSeg, offSegEnd, offFrame, mbuf_maxlen(pMBuf)));
                pvFrame = NULL;
            }

            AssertBreak(iSeg < cSegs);
            pSG->cbTotal += cbSeg;
            pSG->aSegs[iSeg].cb = cbSeg;
            pSG->aSegs[iSeg].pv = pvSeg;
            pSG->aSegs[iSeg].Phys = NIL_RTHCPHYS;
            iSeg++;
        }
        /* The pvFrame might be in a now empty buffer. */
        else if (   !iSeg
                 && pvFrame
                 && (uintptr_t)pvFrame - (uintptr_t)mbuf_datastart(pMBuf) < mbuf_maxlen(pMBuf))
        {
            cbSeg = (uintptr_t)mbuf_datastart(pMBuf) + mbuf_maxlen(pMBuf) - (uintptr_t)pvFrame;
            pSG->cbTotal += cbSeg;
            pSG->aSegs[iSeg].cb = cbSeg;
            pSG->aSegs[iSeg].pv = pvFrame;
            pSG->aSegs[iSeg].Phys = NIL_RTHCPHYS;
            iSeg++;
            pvFrame = NULL;
        }
    }

    Assert(iSeg && iSeg <= cSegs);
    pSG->cSegsUsed = iSeg;

#ifdef PADD_RUNT_FRAMES_FROM_HOST
    /*
     * Add a trailer if the frame is too small.
     *
     * Since we're getting to the packet before it is framed, it has not
     * yet been padded. The current solution is to add a segment pointing
     * to a buffer containing all zeros and pray that works for all frames...
     */
    if (pSG->cbTotal < 60 && (fSrc == INTNETTRUNKDIR_HOST))
    {
        AssertReturnVoid(iSeg < cSegs);

        static uint8_t const s_abZero[128] = {0};
        pSG->aSegs[iSeg].Phys = NIL_RTHCPHYS;
        pSG->aSegs[iSeg].pv = (void *)&s_abZero[0];
        pSG->aSegs[iSeg].cb = 60 - pSG->cbTotal;
        pSG->cbTotal = 60;
        pSG->cSegsUsed++;
    }
#endif

#ifdef VBOXNETFLT_DARWIN_TEST_SEG_SIZE
    /*
     * Redistribute the segments.
     */
    if (pSG->cSegsUsed < pSG->cSegsAlloc)
    {
        /* copy the segments to the end. */
        int iSrc = pSG->cSegsUsed;
        int iDst = pSG->cSegsAlloc;
        while (iSrc > 0)
        {
            iDst--;
            iSrc--;
            pSG->aSegs[iDst] = pSG->aSegs[iSrc];
        }

        /* create small segments from the start. */
        pSG->cSegsUsed = pSG->cSegsAlloc;
        iSrc = iDst;
        iDst = 0;
        while (     iDst < iSrc
               &&   iDst < pSG->cSegsAlloc)
        {
            pSG->aSegs[iDst].Phys = NIL_RTHCPHYS;
            pSG->aSegs[iDst].pv = pSG->aSegs[iSrc].pv;
            pSG->aSegs[iDst].cb = RT_MIN(pSG->aSegs[iSrc].cb, VBOXNETFLT_DARWIN_TEST_SEG_SIZE);
            if (pSG->aSegs[iDst].cb != pSG->aSegs[iSrc].cb)
            {
                pSG->aSegs[iSrc].cb -= pSG->aSegs[iDst].cb;
                pSG->aSegs[iSrc].pv = (uint8_t *)pSG->aSegs[iSrc].pv + pSG->aSegs[iDst].cb;
            }
            else if (++iSrc >= pSG->cSegsAlloc)
            {
                pSG->cSegsUsed = iDst + 1;
                break;
            }
            iDst++;
        }
    }
#endif

    AssertMsg(!pvFrame, ("pvFrame=%p pMBuf=%p iSeg=%d\n", pvFrame, pMBuf, iSeg));
}


/**
 * Helper for determining whether the host wants the interface to be
 * promiscuous.
 */
static bool vboxNetFltDarwinIsPromiscuous(PVBOXNETFLTINS pThis)
{
    bool fRc = false;
    ifnet_t pIfNet = vboxNetFltDarwinRetainIfNet(pThis);
    if (pIfNet)
    {
        /* gather the data */
        uint16_t fIf = ifnet_flags(pIfNet);
        unsigned cPromisc = VBOX_GET_PCOUNT(pIfNet);
        bool fSetPromiscuous = ASMAtomicUoReadBool(&pThis->u.s.fSetPromiscuous);
        vboxNetFltDarwinReleaseIfNet(pThis, pIfNet);

        /* calc the return. */
        fRc = (fIf & IFF_PROMISC)
           && cPromisc > fSetPromiscuous;
    }
    return fRc;
}



/**
 *
 * @see iff_detached_func in the darwin kpi.
 */
static void vboxNetFltDarwinIffDetached(void *pvThis, ifnet_t pIfNet)
{
    PVBOXNETFLTINS pThis = (PVBOXNETFLTINS)pvThis;
    uint64_t NanoTS = RTTimeSystemNanoTS();
    LogFlow(("vboxNetFltDarwinIffDetached: pThis=%p NanoTS=%RU64 (%d)\n",
             pThis, NanoTS, RT_VALID_PTR(pIfNet) ? VBOX_GET_PCOUNT(pIfNet) :  -1));

    Assert(!pThis->fDisconnectedFromHost);
    Assert(!pThis->fRediscoveryPending);

    /*
     * If we've put it into promiscuous mode, undo that now. If we don't
     * the if_pcount will go all wrong when it's replugged.
     */
    if (ASMAtomicXchgBool(&pThis->u.s.fSetPromiscuous, false))
        ifnet_set_promiscuous(pIfNet, 0);

    /*
     * We carefully take the spinlock and increase the interface reference
     * behind it in order to avoid problematic races with the detached callback.
     */
    RTSpinlockAcquire(pThis->hSpinlock);

    pIfNet = ASMAtomicUoReadPtrT(&pThis->u.s.pIfNet, ifnet_t);
    int cPromisc = RT_VALID_PTR(pIfNet) ? VBOX_GET_PCOUNT(pIfNet) : - 1;

    ASMAtomicUoWriteNullPtr(&pThis->u.s.pIfNet);
    ASMAtomicUoWriteNullPtr(&pThis->u.s.pIfFilter);
    ASMAtomicWriteBool(&pThis->u.s.fNeedSetPromiscuous, false);
    pThis->u.s.fSetPromiscuous = false;
    ASMAtomicUoWriteU64(&pThis->NanoTSLastRediscovery, NanoTS);
    ASMAtomicUoWriteBool(&pThis->fRediscoveryPending, false);
    ASMAtomicWriteBool(&pThis->fDisconnectedFromHost, true);

    RTSpinlockRelease(pThis->hSpinlock);

    if (pIfNet)
        ifnet_release(pIfNet);
    LogRel(("VBoxNetFlt: was detached from '%s' (%d)\n", pThis->szName, cPromisc));
}


/**
 *
 * @see iff_ioctl_func in the darwin kpi.
 */
static errno_t vboxNetFltDarwinIffIoCtl(void *pvThis, ifnet_t pIfNet, protocol_family_t eProtocol, u_long uCmd, void *pvArg)
{
    RT_NOREF(pIfNet);
    PVBOXNETFLTINS pThis = (PVBOXNETFLTINS)pvThis;
    LogFlow(("vboxNetFltDarwinIffIoCtl: pThis=%p uCmd=%lx\n", pThis, uCmd));

    /*
     * Update fOtherPromiscuous.
     */
    /** @todo we'll have to find the offset of if_pcount to get this right! */
    //if (uCmd == SIOCSIFFLAGS)
    //{
    //
    //}

    /*
     * We didn't handle it, continue processing.
     */
    NOREF(pThis);
    NOREF(eProtocol);
    NOREF(uCmd);
    NOREF(pvArg);
    return EOPNOTSUPP;
}


/**
 *
 * @see iff_event_func in the darwin kpi.
 */
static void vboxNetFltDarwinIffEvent(void *pvThis, ifnet_t pIfNet, protocol_family_t eProtocol, const struct kev_msg *pEvMsg)
{
    PVBOXNETFLTINS pThis = (PVBOXNETFLTINS)pvThis;
    LogFlow(("vboxNetFltDarwinIffEvent: pThis=%p\n", pThis));

    NOREF(pThis);
    NOREF(pIfNet);
    NOREF(eProtocol);
    NOREF(pEvMsg);

    /*
     * Watch out for the interface going online / offline.
     */
    if (   RT_VALID_PTR(pThis)
        && RT_VALID_PTR(pEvMsg)
        && pEvMsg->vendor_code  == KEV_VENDOR_APPLE
        && pEvMsg->kev_class    == KEV_NETWORK_CLASS
        && pEvMsg->kev_subclass == KEV_DL_SUBCLASS)
    {
        if (pThis->u.s.pIfNet    == pIfNet)
        {
            if (pEvMsg->event_code == KEV_DL_LINK_ON)
            {
                if (ASMAtomicUoReadBool(&pThis->u.s.fNeedSetPromiscuous))
                {
                    /* failed to bring it online. */
                    errno_t err = ifnet_set_promiscuous(pIfNet, 1);
                    if (!err)
                    {
                        ASMAtomicWriteBool(&pThis->u.s.fSetPromiscuous, true);
                        ASMAtomicWriteBool(&pThis->u.s.fNeedSetPromiscuous, false);
                        Log(("vboxNetFltDarwinIffEvent: enabled promiscuous mode on %s (%d)\n", pThis->szName, VBOX_GET_PCOUNT(pIfNet)));
                    }
                    else
                        Log(("vboxNetFltDarwinIffEvent: ifnet_set_promiscuous failed on %s, err=%d (%d)\n", pThis->szName, err, VBOX_GET_PCOUNT(pIfNet)));
                }
                else if (   ASMAtomicUoReadBool(&pThis->u.s.fSetPromiscuous)
                         && !(ifnet_flags(pIfNet) & IFF_PROMISC))
                {
                    /* Try fix the inconsistency. */
                    errno_t err = ifnet_set_flags(pIfNet, IFF_PROMISC, IFF_PROMISC);
                    if (!err)
                        err = ifnet_ioctl(pIfNet, 0, SIOCSIFFLAGS, NULL);
                    if (!err && (ifnet_flags(pIfNet) & IFF_PROMISC))
                        Log(("vboxNetFltDarwinIffEvent: fixed IFF_PROMISC on %s (%d)\n", pThis->szName, VBOX_GET_PCOUNT(pIfNet)));
                    else
                        Log(("vboxNetFltDarwinIffEvent: failed to fix IFF_PROMISC on %s, err=%d flags=%#x (%d)\n",
                             pThis->szName, err, ifnet_flags(pIfNet), VBOX_GET_PCOUNT(pIfNet)));
                }
                else
                    Log(("vboxNetFltDarwinIffEvent: online, '%s'. flags=%#x (%d)\n", pThis->szName, ifnet_flags(pIfNet), VBOX_GET_PCOUNT(pIfNet)));
            }
            else if (pEvMsg->event_code == KEV_DL_LINK_OFF)
                Log(("vboxNetFltDarwinIffEvent: %s goes down (%d)\n", pThis->szName, VBOX_GET_PCOUNT(pIfNet)));
/** @todo KEV_DL_LINK_ADDRESS_CHANGED  -> pfnReportMacAddress */
/** @todo KEV_DL_SIFFLAGS              -> pfnReportPromiscuousMode */
        }
        else
            Log(("vboxNetFltDarwinIffEvent: pThis->u.s.pIfNet=%p pIfNet=%p (%d)\n", pThis->u.s.pIfNet, pIfNet, RT_VALID_PTR(pIfNet) ? VBOX_GET_PCOUNT(pIfNet) : -1));
    }
    else if (RT_VALID_PTR(pEvMsg))
        Log(("vboxNetFltDarwinIffEvent: vendor_code=%#x kev_class=%#x kev_subclass=%#x event_code=%#x\n",
             pEvMsg->vendor_code, pEvMsg->kev_class, pEvMsg->kev_subclass, pEvMsg->event_code));
}


/**
 * Internal worker for  vboxNetFltDarwinIffInput and vboxNetFltDarwinIffOutput,
 *
 * @returns 0 or EJUSTRETURN.
 * @param   pThis           The instance.
 * @param   pMBuf           The mbuf.
 * @param   pvFrame         The start of the frame, optional.
 * @param   fSrc            Where the packet (allegedly) comes from, one INTNETTRUNKDIR_* value.
 * @param   eProtocol       The protocol.
 */
static errno_t vboxNetFltDarwinIffInputOutputWorker(PVBOXNETFLTINS pThis, mbuf_t pMBuf, void *pvFrame,
                                                    uint32_t fSrc, protocol_family_t eProtocol)
{
    /*
     * Drop it immediately?
     */
    Log2(("vboxNetFltDarwinIffInputOutputWorker: pThis=%p pMBuf=%p pvFrame=%p fSrc=%#x cbPkt=%x\n",
          pThis, pMBuf, pvFrame, fSrc, pMBuf ? mbuf_pkthdr_len(pMBuf) : -1));
    if (!pMBuf)
        return 0;
#if 0 /* debugging lost icmp packets */
    if (mbuf_pkthdr_len(pMBuf) > 0x300)
    {
        uint8_t *pb = (uint8_t *)(pvFrame ? pvFrame : mbuf_data(pMBuf));
        Log3(("D=%.6Rhxs  S=%.6Rhxs  T=%04x IFF\n", pb, pb + 6, RT_BE2H_U16(*(uint16_t *)(pb + 12))));
    }
#endif
    if (vboxNetFltDarwinMBufIsOur(pThis, pMBuf, pvFrame))
        return 0;

    /*
     * Active? Retain the instance and increment the busy counter.
     */
    if (!vboxNetFltTryRetainBusyActive(pThis))
        return 0;

    /*
     * Finalize out-bound packets since the stack puts off finalizing
     * TCP/IP checksums as long as possible.
     * ASSUMES this only applies to outbound IP packets.
     */
    if (fSrc == INTNETTRUNKDIR_HOST)
    {
        Assert(!pvFrame);
        mbuf_outbound_finalize(pMBuf, eProtocol, sizeof(RTNETETHERHDR));
    }

    /*
     * Create a (scatter/)gather list for the mbuf and feed it to the internal network.
     */
    bool fDropIt = false;
    unsigned cSegs = vboxNetFltDarwinMBufCalcSGSegs(pThis, pMBuf, pvFrame);
    if (cSegs < VBOXNETFLT_DARWIN_MAX_SEGS)
    {
        PINTNETSG pSG = (PINTNETSG)alloca(RT_UOFFSETOF_DYN(INTNETSG, aSegs[cSegs]));
        vboxNetFltDarwinMBufToSG(pThis, pMBuf, pvFrame, pSG, cSegs, fSrc);

        fDropIt = pThis->pSwitchPort->pfnRecv(pThis->pSwitchPort, NULL /* pvIf */, pSG, fSrc);
        if (fDropIt)
        {
            /*
             * If the interface is in promiscuous mode we should let
             * all inbound packets (this one was for a bridged guest)
             * reach the driver as it passes them to tap callbacks in
             * order for BPF to work properly.
             */
            if (   fSrc == INTNETTRUNKDIR_WIRE
                && vboxNetFltDarwinIsPromiscuous(pThis))
            {
                fDropIt = false;
            }

            /*
             * A packet from the host to a guest.  As we won't pass it
             * to the drvier/wire we need to feed it to bpf ourselves.
             *
             * XXX: TODO: bpf should be done before; use pfnPreRecv?
             */
            if (fSrc == INTNETTRUNKDIR_HOST)
            {
                bpf_tap_out(pThis->u.s.pIfNet, DLT_EN10MB, pMBuf, NULL, 0);
                ifnet_stat_increment_out(pThis->u.s.pIfNet, 1, mbuf_len(pMBuf), 0);
            }
        }
    }

    vboxNetFltRelease(pThis, true /* fBusy */);

    if (fDropIt)
    {
        mbuf_freem(pMBuf);
        return EJUSTRETURN;
    }
    return 0;
}


/**
 * From the host.
 *
 * @see iff_output_func in the darwin kpi.
 */
static errno_t vboxNetFltDarwinIffOutput(void *pvThis, ifnet_t pIfNet, protocol_family_t eProtocol, mbuf_t *ppMBuf)
{
    /** @todo there was some note about the ethernet header here or something like that... */

    NOREF(eProtocol);
    NOREF(pIfNet);
    return vboxNetFltDarwinIffInputOutputWorker((PVBOXNETFLTINS)pvThis, *ppMBuf, NULL, INTNETTRUNKDIR_HOST, eProtocol);
}


/**
 * From the wire.
 *
 * @see iff_input_func in the darwin kpi.
 */
static errno_t vboxNetFltDarwinIffInput(void *pvThis, ifnet_t pIfNet, protocol_family_t eProtocol, mbuf_t *ppMBuf, char **ppchFrame)
{
    RT_NOREF(eProtocol, pIfNet);
    return vboxNetFltDarwinIffInputOutputWorker((PVBOXNETFLTINS)pvThis, *ppMBuf, *ppchFrame, INTNETTRUNKDIR_WIRE, eProtocol);
}


/** A worker thread for vboxNetFltSendDummy(). */
static DECLCALLBACK(int) vboxNetFltSendDummyWorker(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    Assert(pvUser);
    ifnet_t pIfNet = (ifnet_t)pvUser;
    return VBoxNetSendDummy(pIfNet);
}


/**
 * Prevent GUI icon freeze issue when VirtualBoxVM process terminates.
 *
 * This function is a workaround for stuck-in-dock issue.  The idea here is to
 * send a dummy packet to an interface from the context of a kernel thread.
 * Therefore, an XNU's receive thread (which is created as a result if we are
 * the first who is communicating with the interface) will be associated with
 * the kernel thread instead of VirtualBoxVM process.
 *
 * @param pIfNet    Interface to be used to send data.
 */
static void vboxNetFltSendDummy(ifnet_t pIfNet)
{
    RTTHREAD hThread;
    int rc = RTThreadCreate(&hThread, vboxNetFltSendDummyWorker, (void *)pIfNet, 0,
                            RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "DummyThread");
    if (RT_SUCCESS(rc))
    {
        RTThreadWait(hThread, RT_INDEFINITE_WAIT, NULL);
        LogFlow(("vboxNetFltSendDummy: a dummy packet has been successfully sent in order to prevent stuck-in-dock issue\n"));
    }
    else
        LogFlow(("vboxNetFltSendDummy: unable to send dummy packet in order to prevent stuck-in-dock issue\n"));
}


/**
 * Internal worker for vboxNetFltOsInitInstance and vboxNetFltOsMaybeRediscovered.
 *
 * @returns VBox status code.
 * @param   pThis           The instance.
 * @param   fRediscovery    If set we're doing a rediscovery attempt, so, don't
 *                          flood the release log.
 */
static int vboxNetFltDarwinAttachToInterface(PVBOXNETFLTINS pThis, bool fRediscovery)
{
    LogFlow(("vboxNetFltDarwinAttachToInterface: pThis=%p (%s)\n", pThis, pThis->szName));
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Locate the interface first.
     *
     * The pIfNet member is updated before iflt_attach is called and used
     * to deal with the hypothetical case where someone rips out the
     * interface immediately after our iflt_attach call.
     */
    ifnet_t pIfNet = NULL;
    errno_t err = ifnet_find_by_name(pThis->szName, &pIfNet);
    if (err)
    {
        Assert(err == ENXIO);
        if (!fRediscovery)
            LogRel(("VBoxFltDrv: failed to find ifnet '%s' (err=%d)\n", pThis->szName, err));
        else
            Log(("VBoxFltDrv: failed to find ifnet '%s' (err=%d)\n", pThis->szName, err));
        IPRT_DARWIN_RESTORE_EFL_AC();
        return VERR_INTNET_FLT_IF_NOT_FOUND;
    }

    AssertCompileMemberAlignment(VBOXNETFLTINS, u.s.pIfNet, ARCH_BITS / 8);
    AssertMsg(!((uintptr_t)&pThis->u.s.pIfNet & (ARCH_BITS / 8 - 1)), ("pThis=%p\n", pThis));
    RTSpinlockAcquire(pThis->hSpinlock);
    ASMAtomicUoWritePtr(&pThis->u.s.pIfNet, pIfNet);
    RTSpinlockRelease(pThis->hSpinlock);

    /* Adjust g_offIfNetPCount as it varies for different versions of xnu. */
    vboxNetFltDarwinDetectPCountOffset(pIfNet);

    /* Prevent stuck-in-dock issue by associating interface receive thread with kernel thread. */
    vboxNetFltSendDummy(pIfNet);

    /*
     * Get the mac address while we still have a valid ifnet reference.
     */
    err = ifnet_lladdr_copy_bytes(pIfNet, &pThis->u.s.MacAddr, sizeof(pThis->u.s.MacAddr));
    if (!err)
    {
        /*
         * Try attach the filter.
         */
        struct iff_filter RegRec;
        RegRec.iff_cookie   = pThis;
        RegRec.iff_name     = "VBoxNetFlt";
        RegRec.iff_protocol = 0;
        RegRec.iff_input    = vboxNetFltDarwinIffInput;
        RegRec.iff_output   = vboxNetFltDarwinIffOutput;
        RegRec.iff_event    = vboxNetFltDarwinIffEvent;
        RegRec.iff_ioctl    = vboxNetFltDarwinIffIoCtl;
        RegRec.iff_detached = vboxNetFltDarwinIffDetached;
        interface_filter_t pIfFilter = NULL;
        err = iflt_attach(pIfNet, &RegRec, &pIfFilter);
        Assert(err || pIfFilter);

        RTSpinlockAcquire(pThis->hSpinlock);
        pIfNet = ASMAtomicUoReadPtrT(&pThis->u.s.pIfNet, ifnet_t);
        if (pIfNet && !err)
        {
            ASMAtomicUoWriteBool(&pThis->fDisconnectedFromHost, false);
            ASMAtomicUoWritePtr(&pThis->u.s.pIfFilter, pIfFilter);
            pIfNet = NULL; /* don't dereference it */
        }
        RTSpinlockRelease(pThis->hSpinlock);

        /* Report capabilities. */
        if (   !pIfNet
            && vboxNetFltTryRetainBusyNotDisconnected(pThis))
        {
            Assert(pThis->pSwitchPort);
            pThis->pSwitchPort->pfnReportMacAddress(pThis->pSwitchPort, &pThis->u.s.MacAddr);
#if 0
            /*
             * XXX: Don't tell SrvIntNetR0 if the interface is
             * promiscuous, because there's no code yet to update that
             * information and we don't want it stuck, spamming all
             * traffic to the host.
             */
            pThis->pSwitchPort->pfnReportPromiscuousMode(pThis->pSwitchPort, vboxNetFltDarwinIsPromiscuous(pThis));
#endif
            pThis->pSwitchPort->pfnReportGsoCapabilities(pThis->pSwitchPort, 0,  INTNETTRUNKDIR_WIRE | INTNETTRUNKDIR_HOST);
            pThis->pSwitchPort->pfnReportNoPreemptDsts(pThis->pSwitchPort, 0 /* none */);
            vboxNetFltRelease(pThis, true /*fBusy*/);
        }
    }

    /* Release the interface on failure. */
    if (pIfNet)
        ifnet_release(pIfNet);

    int rc = RTErrConvertFromErrno(err);
    if (RT_SUCCESS(rc))
        LogRel(("VBoxFltDrv: attached to '%s' / %RTmac\n", pThis->szName, &pThis->u.s.MacAddr));
    else
        LogRel(("VBoxFltDrv: failed to attach to ifnet '%s' (err=%d)\n", pThis->szName, err));
    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
}


bool vboxNetFltOsMaybeRediscovered(PVBOXNETFLTINS pThis)
{
    vboxNetFltDarwinAttachToInterface(pThis, true /* fRediscovery */);
    return !ASMAtomicUoReadBool(&pThis->fDisconnectedFromHost);
}


int  vboxNetFltPortOsXmit(PVBOXNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst)
{
    IPRT_DARWIN_SAVE_EFL_AC();
    NOREF(pvIfData);

    int rc = VINF_SUCCESS;
    ifnet_t pIfNet = vboxNetFltDarwinRetainIfNet(pThis);
    if (pIfNet)
    {
        /*
         * Create a mbuf for the gather list and push it onto the wire.
         * BPF tap and stats will be taken care of by the driver.
         */
        if (fDst & INTNETTRUNKDIR_WIRE)
        {
            mbuf_t pMBuf = vboxNetFltDarwinMBufFromSG(pThis, pSG);
            if (pMBuf)
            {
                errno_t err = ifnet_output_raw(pIfNet, PF_LINK, pMBuf);
                if (err)
                    rc = RTErrConvertFromErrno(err);
            }
            else
                rc = VERR_NO_MEMORY;
        }

        /*
         * Create a mbuf for the gather list and push it onto the host stack.
         * BPF tap and stats are on us.
         */
        if (fDst & INTNETTRUNKDIR_HOST)
        {
            mbuf_t pMBuf = vboxNetFltDarwinMBufFromSG(pThis, pSG);
            if (pMBuf)
            {
                void *pvEthHdr = mbuf_data(pMBuf);
                unsigned const cbEthHdr = 14;
                struct ifnet_stat_increment_param stats;

                RT_ZERO(stats);
                stats.packets_in = 1;
                stats.bytes_in = mbuf_len(pMBuf); /* full ethernet frame */

                mbuf_pkthdr_setrcvif(pMBuf, pIfNet);
                mbuf_pkthdr_setheader(pMBuf, pvEthHdr); /* link-layer header */
                mbuf_adj(pMBuf, cbEthHdr);              /* move to payload */

#if 0 /* XXX: disabled since we don't request promiscuous from intnet */
                /*
                 * TODO: Since intnet knows whether it forwarded us
                 * this packet because it's for us or because we are
                 * promiscuous, it can perhaps set a flag for us in
                 * INTNETSG::fFlags so that we don't have to re-check
                 * it here.
                 */
                PCRTNETETHERHDR pcEthHdr = (PCRTNETETHERHDR)pvEthHdr;
                if (   (pcEthHdr->DstMac.au8[0] & 1) == 0 /* unicast? */
                    && memcmp(&pcEthHdr->DstMac, &pThis->u.s.MacAddr, sizeof(RTMAC)) != 0)
                {
                    mbuf_setflags_mask(pMBuf, MBUF_PROMISC, MBUF_PROMISC);
                }
#endif

                bpf_tap_in(pIfNet, DLT_EN10MB, pMBuf, pvEthHdr, cbEthHdr);
                errno_t err = ifnet_input(pIfNet, pMBuf, &stats);
                if (err)
                    rc = RTErrConvertFromErrno(err);
            }
            else
                rc = VERR_NO_MEMORY;
        }

        vboxNetFltDarwinReleaseIfNet(pThis, pIfNet);
    }

    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
}


void vboxNetFltPortOsSetActive(PVBOXNETFLTINS pThis, bool fActive)
{
    IPRT_DARWIN_SAVE_EFL_AC();
    ifnet_t pIfNet = vboxNetFltDarwinRetainIfNet(pThis);
    if (pIfNet)
    {
        if (pThis->fDisablePromiscuous)
        {
            /*
             * Promiscuous mode should not be used (wireless), we just need to
             * make sure the interface is up.
             */
            if (fActive)
            {
                u_int16_t fIf = ifnet_flags(pIfNet);
                if ((fIf & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
                {
                    ifnet_set_flags(pIfNet, IFF_UP, IFF_UP);
                    ifnet_ioctl(pIfNet, 0, SIOCSIFFLAGS, NULL);
                }
            }
        }
        else
        {
            /*
             * This api is a bit weird, the best reference is the code.
             *
             * Also, we have a bit or race conditions wrt the maintenance of
             * host the interface promiscuity for vboxNetFltPortOsIsPromiscuous.
             */
            unsigned const cPromiscBefore = VBOX_GET_PCOUNT(pIfNet);
            u_int16_t fIf;
            if (fActive)
            {
                Assert(!pThis->u.s.fSetPromiscuous);
                errno_t err = ENETDOWN;
                ASMAtomicWriteBool(&pThis->u.s.fNeedSetPromiscuous, true);

                /*
                 * Try bring the interface up and running if it's down.
                 */
                fIf = ifnet_flags(pIfNet);
                if ((fIf & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING))
                {
                    err = ifnet_set_flags(pIfNet, IFF_UP, IFF_UP);
                    errno_t err2 = ifnet_ioctl(pIfNet, 0, SIOCSIFFLAGS, NULL);
                    if (!err)
                        err = err2;
                    fIf = ifnet_flags(pIfNet);
                }

                /*
                 * Is it already up?  If it isn't, leave it to the link event or
                 * we'll upset if_pcount (as stated above, ifnet_set_promiscuous is weird).
                 */
                if ((fIf & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
                {
                    err = ifnet_set_promiscuous(pIfNet, 1);
                    pThis->u.s.fSetPromiscuous = err == 0;
                    if (!err)
                    {
                        ASMAtomicWriteBool(&pThis->u.s.fNeedSetPromiscuous, false);

                        /* check if it actually worked, this stuff is not always behaving well. */
                        if (!(ifnet_flags(pIfNet) & IFF_PROMISC))
                        {
                            err = ifnet_set_flags(pIfNet, IFF_PROMISC, IFF_PROMISC);
                            if (!err)
                                err = ifnet_ioctl(pIfNet, 0, SIOCSIFFLAGS, NULL);
                            if (!err)
                                Log(("vboxNetFlt: fixed IFF_PROMISC on %s (%d->%d)\n", pThis->szName, cPromiscBefore, VBOX_GET_PCOUNT(pIfNet)));
                            else
                                Log(("VBoxNetFlt: failed to fix IFF_PROMISC on %s, err=%d (%d->%d)\n",
                                     pThis->szName, err, cPromiscBefore, VBOX_GET_PCOUNT(pIfNet)));
                        }
                    }
                    else
                        Log(("VBoxNetFlt: ifnet_set_promiscuous -> err=%d grr! (%d->%d)\n", err, cPromiscBefore, VBOX_GET_PCOUNT(pIfNet)));
                }
                else if (!err)
                    Log(("VBoxNetFlt: Waiting for the link to come up... (%d->%d)\n", cPromiscBefore, VBOX_GET_PCOUNT(pIfNet)));
                if (err)
                    LogRel(("VBoxNetFlt: Failed to put '%s' into promiscuous mode, err=%d (%d->%d)\n", pThis->szName, err, cPromiscBefore, VBOX_GET_PCOUNT(pIfNet)));
            }
            else
            {
                ASMAtomicWriteBool(&pThis->u.s.fNeedSetPromiscuous, false);
                if (pThis->u.s.fSetPromiscuous)
                {
                    errno_t err = ifnet_set_promiscuous(pIfNet, 0);
                    AssertMsg(!err, ("%d\n", err)); NOREF(err);
                }
                pThis->u.s.fSetPromiscuous = false;

                fIf = ifnet_flags(pIfNet);
                Log(("VBoxNetFlt: fIf=%#x; %d->%d\n", fIf, cPromiscBefore, VBOX_GET_PCOUNT(pIfNet)));
            }
        }

        vboxNetFltDarwinReleaseIfNet(pThis, pIfNet);
    }
    IPRT_DARWIN_RESTORE_EFL_AC();
}


int vboxNetFltOsDisconnectIt(PVBOXNETFLTINS pThis)
{
    /* Nothing to do here. */
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


int  vboxNetFltOsConnectIt(PVBOXNETFLTINS pThis)
{
    /* Nothing to do here. */
    RT_NOREF(pThis);
    return VINF_SUCCESS;
}


void vboxNetFltOsDeleteInstance(PVBOXNETFLTINS pThis)
{
    IPRT_DARWIN_SAVE_EFL_AC();

    /*
     * Carefully obtain the interface filter reference and detach it.
     */
    RTSpinlockAcquire(pThis->hSpinlock);
    interface_filter_t pIfFilter = ASMAtomicUoReadPtrT(&pThis->u.s.pIfFilter, interface_filter_t);
    if (pIfFilter)
        ASMAtomicUoWriteNullPtr(&pThis->u.s.pIfFilter);
    RTSpinlockRelease(pThis->hSpinlock);

    if (pIfFilter)
        iflt_detach(pIfFilter);

    if (pThis->u.s.pSysSock != NULL)
    {
        RT_GCC_NO_WARN_DEPRECATED_BEGIN

        sock_close(pThis->u.s.pSysSock);
        pThis->u.s.pSysSock = NULL;

        RT_GCC_NO_WARN_DEPRECATED_END
    }

    IPRT_DARWIN_RESTORE_EFL_AC();
}


int  vboxNetFltOsInitInstance(PVBOXNETFLTINS pThis, void *pvContext)
{
    NOREF(pvContext);

    int rc = vboxNetFltDarwinAttachToInterface(pThis, false /* fRediscovery */);
    if (RT_FAILURE(rc))
        return rc;

    if (pThis->pSwitchPort->pfnNotifyHostAddress == NULL)
        return rc;

    /*
     * XXX: uwe
     *
     * Learn host's IP addresses and set up notifications for changes.
     * To avoid racing, set up notifications first.
     *
     * XXX: This should probably be global, since the only thing
     * specific to ifnet here is its IPv6 link-local address.
     */
    IPRT_DARWIN_SAVE_EFL_AC();
    errno_t error;

    /** @todo Figure out how to replace the socket stuff we use to detect
     *        addresses here as 10.5 deprecates it. */
    RT_GCC_NO_WARN_DEPRECATED_BEGIN

    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */
    /** @todo reorg code to not have numerous returns with duplicate code... */

    error = sock_socket(PF_SYSTEM, SOCK_RAW, SYSPROTO_EVENT,
                        vboxNetFltDarwinSysSockUpcall, pThis,
                        &pThis->u.s.pSysSock);
    if (error != 0)
    {
        LogRel(("sock_socket(SYSPROTO_EVENT): error %d\n", error));
        IPRT_DARWIN_RESTORE_EFL_AC();
        return rc;
    }

    int nbio = 1;
    error = sock_ioctl(pThis->u.s.pSysSock, FIONBIO, &nbio);
    if (error != 0)
    {
        LogRel(("FIONBIO: error %d\n", error));
        sock_close(pThis->u.s.pSysSock);
        IPRT_DARWIN_RESTORE_EFL_AC();
        return rc;
    }

    if (!sock_isnonblocking(pThis->u.s.pSysSock))
    {
        LogRel(("FIONBIO ok, but socket is blocking?!\n"));
        sock_close(pThis->u.s.pSysSock);
        IPRT_DARWIN_RESTORE_EFL_AC();
        return rc;
    }

    struct kev_request req;
    req.vendor_code = KEV_VENDOR_APPLE;
    req.kev_class = KEV_NETWORK_CLASS;
    req.kev_subclass = KEV_ANY_SUBCLASS; /* need both INET and INET6, so have to request all */

    error = sock_ioctl(pThis->u.s.pSysSock, SIOCSKEVFILT, &req);
    if (error != 0)
    {
        LogRel(("SIOCSKEVFILT: error %d\n", error));
        sock_close(pThis->u.s.pSysSock);
        IPRT_DARWIN_RESTORE_EFL_AC();
        return rc;
    }
    RT_GCC_NO_WARN_DEPRECATED_END

    ifnet_t pIfNet = pThis->u.s.pIfNet; /* already retained */

    ifaddr_t *pIfAddrList;
    error = ifnet_get_address_list(/* all interfaces*/ NULL, &pIfAddrList);
    if (error != 0)
    {
        LogRel(("ifnet_get_address_list: error %d\n", error));
        IPRT_DARWIN_RESTORE_EFL_AC();
        return rc;
    }

    for (ifaddr_t *pIfAddr = pIfAddrList; *pIfAddr != NULL; ++pIfAddr)
    {
        ifaddr_t ifa = *pIfAddr;
        sa_family_t family = ifaddr_address_family(ifa);
        struct sockaddr_storage ss;

        error = ifaddr_address(ifa, (struct sockaddr *)&ss, sizeof(ss));
        if (error != 0)
        {
            LogRel(("getting address family %d: error %d\n", family, error));
            continue;
        }

        if (family == AF_INET)
        {
            struct sockaddr_in *sin = (struct sockaddr_in *)&ss;
            u_int32_t u32Addr = ntohl(sin->sin_addr.s_addr);

            if (VBOX_IN_LOOPBACK(u32Addr))
                continue;

            if (ifaddr_ifnet(ifa) != pIfNet && VBOX_IN_LINKLOCAL(u32Addr))
                continue;

            Log(("> inet %RTnaipv4\n", sin->sin_addr.s_addr));
            pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort,
                /* :fAdded */ true, kIntNetAddrType_IPv4, &sin->sin_addr);
        }
        else if (family == AF_INET6)
        {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&ss;

            if (IN6_IS_ADDR_LOOPBACK(&sin6->sin6_addr))
                continue;

            /* link-local from other interfaces are out of scope */
            if (ifaddr_ifnet(ifa) != pIfNet && IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
                continue;

            Log(("> inet6 %RTnaipv6\n", &sin6->sin6_addr));
            pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort,
                /* :fAdded */ true, kIntNetAddrType_IPv6, &sin6->sin6_addr);
        }
    }

    ifnet_free_address_list(pIfAddrList);

    /*
     * Now that we've got current addresses, check for events that
     * might have happened while we were working.
     */
    vboxNetFltDarwinSysSockUpcall(pThis->u.s.pSysSock, pThis, MBUF_DONTWAIT);

    IPRT_DARWIN_RESTORE_EFL_AC();
    return rc;
}


static void vboxNetFltDarwinSysSockUpcall(socket_t pSysSock, void *pvData, int fWait)
{
    PVBOXNETFLTINS pThis = (PVBOXNETFLTINS)pvData;
    errno_t error;

    NOREF(fWait);

    if (RT_UNLIKELY(pSysSock != pThis->u.s.pSysSock))
    {
        Log(("vboxNetFltDarwinSysSockUpcall: %p != %p?\n", pSysSock, pThis->u.s.pSysSock));
        return;
    }

    ifnet_t pIfNet = pThis->u.s.pIfNet; /* XXX: retain? */
    ifnet_family_t if_family = ifnet_family(pIfNet);
    u_int32_t if_unit = ifnet_unit(pIfNet);

    for (;;)
    {
        mbuf_t m;
        size_t len = sizeof(struct kern_event_msg) - sizeof(u_int32_t) + sizeof(struct kev_in6_data);

        RT_GCC_NO_WARN_DEPRECATED_BEGIN
        error = sock_receivembuf(pSysSock, NULL, &m, 0, &len);
        RT_GCC_NO_WARN_DEPRECATED_END
        if (error != 0)
        {
            if (error == EWOULDBLOCK)
            {
                Log(("vboxNetFltDarwinSysSockUpcall: EWOULDBLOCK - we are done\n"));
                error = 0;
            }
            else
                Log(("sock_receivembuf: error %d\n", error));
            break;
        }

        if (len < sizeof(struct kern_event_msg) - sizeof(u_int32_t))
        {
            Log(("vboxNetFltDarwinSysSockUpcall: %u bytes is too short\n", (unsigned int)len));
            mbuf_freem(m);
            return;
        }

        struct kern_event_msg *msg = (struct kern_event_msg *)mbuf_data(m);
        if (msg->kev_subclass == KEV_INET_SUBCLASS)
        {
            if (len - (sizeof(struct kern_event_msg) - sizeof(u_int32_t)) < sizeof(struct kev_in_data))
            {
                Log(("vboxNetFltDarwinSysSockUpcall: %u bytes is too short for KEV_INET_SUBCLASS\n", (unsigned int)len));
                mbuf_freem(m);
                return;
            }

            struct kev_in_data *iev = (struct kev_in_data *)msg->event_data;
            struct net_event_data *link = &iev->link_data;
            PCRTNETADDRU pAddr = (PCRTNETADDRU)&iev->ia_addr;
            u_int32_t u32Addr = ntohl(pAddr->IPv4.u);

            if (VBOX_IN_LOOPBACK(u32Addr))
            {
                mbuf_freem(m);
                continue;
            }

            if (   (link->if_family != if_family || link->if_unit != if_unit)
                && VBOX_IN_LINKLOCAL(u32Addr))
            {
                mbuf_freem(m);
                continue;
            }

            switch (msg->event_code)
            {
                case KEV_INET_NEW_ADDR:
                    Log(("KEV_INET_NEW_ADDR %.*s%d: %RTnaipv4\n", IFNAMSIZ, link->if_name, link->if_unit, pAddr->IPv4.u));
                    pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, true /*fAdded*/, kIntNetAddrType_IPv4, pAddr);
                    break;

                case KEV_INET_ADDR_DELETED:
                    Log(("KEV_INET_ADDR_DELETED %.*s%d: %RTnaipv4\n", IFNAMSIZ, link->if_name, link->if_unit, pAddr->IPv4.u));
                    pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, false /*fAdded*/, kIntNetAddrType_IPv4, pAddr);
                    break;

                default:
                    Log(("KEV INET event %u %.*s%d: addr %RTnaipv4\n",
                         msg->event_code, IFNAMSIZ, link->if_name, link->if_unit, pAddr->IPv4.u));
                    break;
            }
        }
        else if (msg->kev_subclass == KEV_INET6_SUBCLASS)
        {
            if (len - (sizeof(struct kern_event_msg) - sizeof(u_int32_t)) < sizeof(struct kev_in6_data))
            {
                Log(("vboxNetFltDarwinSysSockUpcall: %u bytes is too short for KEV_INET6_SUBCLASS\n",
                     (unsigned int)len));
                mbuf_freem(m);
                return;
            }

            struct kev_in6_data *iev6 = (struct kev_in6_data *)msg->event_data;
            struct net_event_data *link = &iev6->link_data;
            PCRTNETADDRU pAddr = (PCRTNETADDRU)&iev6->ia_addr.sin6_addr;

            if (IN6_IS_ADDR_LOOPBACK(&iev6->ia_addr.sin6_addr))
            {
                mbuf_freem(m);
                continue;
            }

            if (   (link->if_family != if_family || link->if_unit != if_unit)
                && IN6_IS_ADDR_LINKLOCAL(&iev6->ia_addr.sin6_addr))
            {
                mbuf_freem(m);
                continue;
            }

            switch (msg->event_code)
            {
                case KEV_INET6_NEW_USER_ADDR:
                    Log(("KEV_INET6_NEW_USER_ADDR %.*s%d: %RTnaipv6\n",
                         IFNAMSIZ, link->if_name, link->if_unit, pAddr));
                    goto kev_inet6_new;

                case KEV_INET6_NEW_LL_ADDR:
                    Log(("KEV_INET6_NEW_LL_ADDR %.*s%d: %RTnaipv6\n",
                         IFNAMSIZ, link->if_name, link->if_unit, pAddr));
                    goto kev_inet6_new;

                case KEV_INET6_NEW_RTADV_ADDR:
                    Log(("KEV_INET6_NEW_RTADV_ADDR %.*s%d: %RTnaipv6\n",
                         IFNAMSIZ, link->if_name, link->if_unit, pAddr));
                    goto kev_inet6_new;

                kev_inet6_new:
                    pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, true /*fAdded*/, kIntNetAddrType_IPv6, pAddr);
                    break;

                case KEV_INET6_ADDR_DELETED:
                    Log(("KEV_INET6_ADDR_DELETED %.*s%d: %RTnaipv6\n",
                         IFNAMSIZ, link->if_name, link->if_unit, pAddr));

                    pThis->pSwitchPort->pfnNotifyHostAddress(pThis->pSwitchPort, false /*fAdded*/, kIntNetAddrType_IPv6, pAddr);
                    break;

                default:
                    Log(("KEV INET6 event %u %.*s%d: addr %RTnaipv6\n",
                         msg->event_code, IFNAMSIZ, link->if_name, link->if_unit, pAddr));
                    break;
            }
        }
        else
            Log(("vboxNetFltDarwinSysSockUpcall: subclass %u ignored\n", (unsigned)msg->kev_subclass));

        mbuf_freem(m);
    }
}


int  vboxNetFltOsPreInitInstance(PVBOXNETFLTINS pThis)
{
    /*
     * Init the darwin specific members.
     */
    pThis->u.s.pIfNet = NULL;
    pThis->u.s.pIfFilter = NULL;
    pThis->u.s.fSetPromiscuous = false;
    pThis->u.s.fNeedSetPromiscuous = false;
    //pThis->u.s.MacAddr = {0};
    pThis->u.s.pSysSock = NULL;

    return VINF_SUCCESS;
}


void vboxNetFltPortOsNotifyMacAddress(PVBOXNETFLTINS pThis, void *pvIfData, PCRTMAC pMac)
{
    NOREF(pThis); NOREF(pvIfData); NOREF(pMac);
}


int vboxNetFltPortOsConnectInterface(PVBOXNETFLTINS pThis, void *pvIf, void **ppvIfData)
{
    /* Nothing to do */
    NOREF(pThis); NOREF(pvIf); NOREF(ppvIfData);
    return VINF_SUCCESS;
}


int vboxNetFltPortOsDisconnectInterface(PVBOXNETFLTINS pThis, void *pvIfData)
{
    /* Nothing to do */
    NOREF(pThis); NOREF(pvIfData);
    return VINF_SUCCESS;
}

