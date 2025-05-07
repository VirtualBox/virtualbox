/* $Id: VirtioCore.cpp 109452 2025-05-07 08:16:10Z aleksey.ilyushin@oracle.com $ */

/** @file
 * VirtioCore - Virtio Core (PCI, feature & config mgt, queue mgt & proxy, notification mgt)
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_VIRTIO

#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/mem.h>
#include <iprt/sg.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/param.h>
#include <iprt/types.h>
#include <VBox/log.h>
#include <VBox/msi.h>
#include <iprt/types.h>
#include <VBox/AssertGuest.h>
#include <VBox/vmm/pdmdev.h>
#include "VirtioCore.h"

#ifdef VIRTIO_REL_INFO_DUMP
#include <iprt/trace.h>
#endif /* VIRTIO_REL_INFO_DUMP */

/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

#define INSTANCE(a_pVirtio)                 ((a_pVirtio)->szInstance)
#define VIRTQNAME(a_pVirtio, a_uVirtq)      ((a_pVirtio)->aVirtqueues[(a_uVirtq)].szName)

#define IS_VIRTQ_EMPTY(pDevIns, pVirtio, pVirtq) \
            (virtioCoreVirtqAvailCnt(pDevIns, pVirtio, pVirtq) == 0)

#define IS_DRIVER_OK(a_pVirtio)             ((a_pVirtio)->fDeviceStatus & VIRTIO_STATUS_DRIVER_OK)
#define WAS_DRIVER_OK(a_pVirtio)            ((a_pVirtio)->fPrevDeviceStatus & VIRTIO_STATUS_DRIVER_OK)

/**
 * These defines are used to track  guest virtio-net driver writing driver features accepted flags
 * in two 32-bit operations (in arbitrary order), and one bit dedicated to ensured 'features complete'
 * is handled once.
 */
#define DRIVER_FEATURES_0_WRITTEN                        1   /**< fDriverFeatures[0]  written by guest virtio-net */
#define DRIVER_FEATURES_1_WRITTEN                        2   /**< fDriverFeatures[1]  written by guest virtio-net */
#define DRIVER_FEATURES_0_AND_1_WRITTEN                  3   /**< Both 32-bit parts of fDriverFeatures[] written  */
#define DRIVER_FEATURES_COMPLETE_HANDLED                 4   /**< Features negotiation complete handler called    */

/**
 * This macro returns true if the @a a_offAccess and access length (@a
 * a_cbAccess) are within the range of the mapped capability struct described by
 * @a a_LocCapData.
 *
 * @param[in]  a_offAccess      Input:  The offset into the MMIO bar of the access.
 * @param[in]  a_cbAccess       Input:  The access size.
 * @param[out] a_offsetIntoCap  Output: uint32_t variable to return the intra-capability offset into.
 * @param[in]  a_LocCapData     Input:  The capability location info.
 */
#define MATCHES_VIRTIO_CAP_STRUCT(a_offAccess, a_cbAccess, a_offsetIntoCap, a_LocCapData) \
    (   ((a_offsetIntoCap) = (uint32_t)((a_offAccess) - (a_LocCapData).offMmio)) < (uint32_t)(a_LocCapData).cbMmio \
     && (a_offsetIntoCap) + (uint32_t)(a_cbAccess) <= (uint32_t)(a_LocCapData).cbMmio )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** @name virtq related flags
 * @{ */
#define VIRTQ_DESC_F_NEXT                               1        /**< Indicates this descriptor chains to next  */
#define VIRTQ_DESC_F_WRITE                              2        /**< Marks buffer as write-only (default ro)   */
#define VIRTQ_DESC_F_INDIRECT                           4        /**< Buffer is list of buffer descriptors      */

#define VIRTQ_USED_F_NO_NOTIFY                          1        /**< Dev to Drv: Don't notify when buf added   */
#define VIRTQ_AVAIL_F_NO_INTERRUPT                      1        /**< Drv to Dev: Don't notify when buf eaten   */
/** @} */

/**
 * virtq-related structs
 * (struct names follow VirtIO 1.0 spec, field names use VBox styled naming, w/respective spec'd name in comments)
 */
typedef struct virtq_desc
{
    uint64_t  GCPhysBuf;                                         /**< addr       GC Phys. address of buffer     */
    uint32_t  cb;                                                /**< len        Buffer length                  */
    uint16_t  fFlags;                                            /**< flags      Buffer specific flags          */
    uint16_t  uDescIdxNext;                                      /**< next       Idx set if VIRTIO_DESC_F_NEXT  */
} VIRTQ_DESC_T, *PVIRTQ_DESC_T;

typedef struct virtq_avail
{
    uint16_t  fFlags;                                            /**< flags      avail ring guest-to-host flags */
    uint16_t  uIdx;                                              /**< idx        Index of next free ring slot   */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint16_t  auRing[RT_FLEXIBLE_ARRAY];                         /**< ring       Ring: avail drv to dev bufs    */
    //uint16_t  uUsedEventIdx;                                   /**< used_event (if VIRTQ_USED_F_EVENT_IDX)    */
} VIRTQ_AVAIL_T, *PVIRTQ_AVAIL_T;

typedef struct virtq_used_elem
{
    uint32_t  uDescIdx;                                          /**< idx         Start of used desc chain      */
    uint32_t  cbElem;                                            /**< len         Total len of used desc chain  */
} VIRTQ_USED_ELEM_T;

typedef struct virt_used
{
    uint16_t  fFlags;                                            /**< flags       used ring host-to-guest flags */
    uint16_t  uIdx;                                              /**< idx         Index of next ring slot       */
    RT_FLEXIBLE_ARRAY_EXTENSION
    VIRTQ_USED_ELEM_T aRing[RT_FLEXIBLE_ARRAY];                  /**< ring        Ring: used dev to drv bufs    */
    //uint16_t  uAvailEventIdx;                                  /**< avail_event if (VIRTQ_USED_F_EVENT_IDX)   */
} VIRTQ_USED_T, *PVIRTQ_USED_T;

DECLHIDDEN(const char *) virtioCoreGetStateChangeText(VIRTIOVMSTATECHANGED enmState)
{
    switch (enmState)
    {
        case kvirtIoVmStateChangedReset:                return "VM RESET";
        case kvirtIoVmStateChangedSuspend:              return "VM SUSPEND";
        case kvirtIoVmStateChangedPowerOff:             return "VM POWER OFF";
        case kvirtIoVmStateChangedResume:               return "VM RESUME";
        default:                                        return "<BAD ENUM>";
    }
}

/* Internal Functions */

static void virtioCoreNotifyGuestDriver(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq);
static int  virtioNudgeGuest(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint8_t uCause, uint16_t uVec);

#ifdef IN_RING3
#  ifdef LOG_ENABLED
DECLINLINE(uint16_t) virtioCoreR3CountPendingBufs(uint16_t uRingIdx, uint16_t uShadowIdx, uint16_t uQueueSize)
{
    if (uShadowIdx == uRingIdx)
        return 0;
    else
    if (uShadowIdx > uRingIdx)
        return uShadowIdx - uRingIdx;
    return uQueueSize - (uRingIdx - uShadowIdx);
}
#  endif
#endif
/** @name Internal queue operations
 * @{ */

/**
 * Accessor for virtq descriptor
 */
#ifdef IN_RING3
DECLINLINE(void) virtioReadDesc(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq,
                                uint32_t idxDesc, PVIRTQ_DESC_T pDesc)
{
    /*
     * Shut up assertion for legacy virtio-net driver in FreeBSD up to 12.3 (see virtioCoreR3VirtqUsedBufPut()
     * for more information).
     */
    AssertMsg(   IS_DRIVER_OK(pVirtio)
              || (   pVirtio->fLegacyDriver
                  && pVirtq->GCPhysVirtqDesc),
              ("Called with guest driver not ready\n"));
    uint16_t const cVirtqItems = RT_MAX(pVirtq->uQueueSize, 1); /* Make sure to avoid div-by-zero. */

    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqDesc + sizeof(VIRTQ_DESC_T) * (idxDesc % cVirtqItems),
                         pDesc, sizeof(VIRTQ_DESC_T));
}
#endif

/**
 * Accessors for virtq avail ring
 */
#ifdef IN_RING3
DECLINLINE(uint16_t) virtioReadAvailDescIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq, uint32_t availIdx)
{
    uint16_t uDescIdx;

    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    uint16_t const cVirtqItems = RT_MAX(pVirtq->uQueueSize, 1); /* Make sure to avoid div-by-zero. */
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[availIdx % cVirtqItems]),
                         &uDescIdx, sizeof(uDescIdx));
    return uDescIdx;
}

DECLINLINE(uint16_t) virtioReadAvailUsedEvent(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    uint16_t uUsedEventIdx;
    /* VirtIO 1.0 uUsedEventIdx (used_event) immediately follows ring */
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[pVirtq->uQueueSize]),
                         &uUsedEventIdx, sizeof(uUsedEventIdx));
    return uUsedEventIdx;
}
#endif

DECLINLINE(uint16_t) virtioReadAvailRingIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    uint16_t uIdx = 0;
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF(VIRTQ_AVAIL_T, uIdx),
                         &uIdx, sizeof(uIdx));
    return uIdx;
}

DECLINLINE(uint16_t) virtioReadAvailRingFlags(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    uint16_t fFlags = 0;
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF(VIRTQ_AVAIL_T, fFlags),
                         &fFlags, sizeof(fFlags));
    return fFlags;
}

/** @} */

/** @name Accessors for virtq used ring
 * @{
 */

#ifdef IN_RING3
DECLINLINE(void) virtioWriteUsedElem(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq,
                                     uint32_t usedIdx, uint32_t uDescIdx, uint32_t uLen)
{
    VIRTQ_USED_ELEM_T elem = { uDescIdx,  uLen };
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    uint16_t const cVirtqItems = RT_MAX(pVirtq->uQueueSize, 1); /* Make sure to avoid div-by-zero. */
    virtioCoreGCPhysWrite(pVirtio, pDevIns,
                          pVirtq->GCPhysVirtqUsed
                        + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[usedIdx % cVirtqItems]),
                          &elem, sizeof(elem));
}

DECLINLINE(void) virtioWriteUsedRingFlags(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq, uint16_t fFlags)
{
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    RT_UNTRUSTED_VALIDATED_FENCE(); /* VirtIO 1.0, Section 3.2.1.4.1 */
    virtioCoreGCPhysWrite(pVirtio, pDevIns,
                          pVirtq->GCPhysVirtqUsed + RT_UOFFSETOF(VIRTQ_USED_T, fFlags),
                          &fFlags, sizeof(fFlags));
}
#endif

DECLINLINE(void) virtioWriteUsedRingIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq, uint16_t uIdx)
{
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    RT_UNTRUSTED_VALIDATED_FENCE(); /* VirtIO 1.0, Section 3.2.1.4.1 */
    virtioCoreGCPhysWrite(pVirtio, pDevIns,
                          pVirtq->GCPhysVirtqUsed + RT_UOFFSETOF(VIRTQ_USED_T, uIdx),
                          &uIdx, sizeof(uIdx));
}

#ifdef IN_RING3
DECLINLINE(uint16_t) virtioReadUsedRingIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    uint16_t uIdx = 0;
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqUsed + RT_UOFFSETOF(VIRTQ_USED_T, uIdx),
                         &uIdx, sizeof(uIdx));
    return uIdx;
}

DECLINLINE(uint16_t) virtioReadUsedRingFlags(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    uint16_t fFlags = 0;
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqUsed + RT_UOFFSETOF(VIRTQ_USED_T, fFlags),
                         &fFlags, sizeof(fFlags));
    return fFlags;
}

DECLINLINE(void) virtioWriteUsedAvailEvent(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq, uint32_t uAvailEventIdx)
{
    /** VirtIO 1.0 uAvailEventIdx (avail_event) immediately follows ring */
    AssertMsg(pVirtio->fLegacyDriver || IS_DRIVER_OK(pVirtio), ("Called with guest driver not ready\n"));
    virtioCoreGCPhysWrite(pVirtio, pDevIns,
                          pVirtq->GCPhysVirtqUsed
                        + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[pVirtq->uQueueSize]),
                          &uAvailEventIdx, sizeof(uAvailEventIdx));
}
#endif
/** @} */


DECLINLINE(uint16_t) virtioCoreVirtqAvailCnt(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    uint16_t uIdxActual = virtioReadAvailRingIdx(pDevIns, pVirtio, pVirtq);
    uint16_t uIdxShadow = pVirtq->uAvailIdxShadow;
    uint16_t uIdxDelta;

    if (uIdxActual < uIdxShadow)
        uIdxDelta = (uIdxActual + pVirtq->uQueueSize) - uIdxShadow;
    else
        uIdxDelta = uIdxActual - uIdxShadow;

    return uIdxDelta;
}
/**
 * Get count of new (e.g. pending) elements in available ring.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtq      Virtq number
 *
 * @returns how many entries have been added to ring as a delta of the consumer's
 *          avail index and the queue's guest-side current avail index.
 */
DECLHIDDEN(uint16_t) virtioCoreVirtqAvailBufCount(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq)
{
    AssertMsgReturn(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues), ("uVirtq out of range"), 0);
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    if (!IS_DRIVER_OK(pVirtio))
    {
        LogRelFunc(("Driver not ready\n"));
        return 0;
    }
    if (!pVirtio->fLegacyDriver && !pVirtq->uEnable)
    {
        LogRelFunc(("virtq: %s not enabled\n", VIRTQNAME(pVirtio, uVirtq)));
        return 0;
    }
    return virtioCoreVirtqAvailCnt(pDevIns, pVirtio, pVirtq);
}

#ifdef IN_RING3

static void virtioCoreR3FeatureDump(VIRTIOCORE *pVirtio, PCDBGFINFOHLP pHlp, const VIRTIO_FEATURES_LIST *s_aFeatures, int cFeatures, int fBanner)
{
#define MAXLINE 80
    /* Display as a single buf to prevent interceding log messages */
    uint16_t cbBuf = cFeatures * 132;
    char *pszBuf = (char *)RTMemAllocZ(cbBuf);
    Assert(pszBuf);
    char *cp = pszBuf;
    for (int i = 0; i < cFeatures; ++i)
    {
        bool isOffered    = RT_BOOL(pVirtio->uDeviceFeatures & s_aFeatures[i].fFeatureBit);
        bool isNegotiated = RT_BOOL(pVirtio->uDriverFeatures & s_aFeatures[i].fFeatureBit);
        cp += RTStrPrintf(cp, cbBuf - (cp - pszBuf), "        %s       %s   %s",
                          isOffered ? "+" : "-", isNegotiated ? "x" : " ", s_aFeatures[i].pcszDesc);
    }
    if (pHlp) {
        if (fBanner)
            pHlp->pfnPrintf(pHlp, "VirtIO Features Configuration\n\n"
                  "    Offered  Accepted  Feature              Description\n"
                  "    -------  --------  -------              -----------\n");
        pHlp->pfnPrintf(pHlp, "%s\n", pszBuf);
    }
#ifdef LOG_ENABLED
    else
    {
        if (fBanner)
            Log(("VirtIO Features Configuration\n\n"
                  "    Offered  Accepted  Feature              Description\n"
                  "    -------  --------  -------              -----------\n"));
        Log(("%s\n", pszBuf));
    }
#endif
    RTMemFree(pszBuf);
}

/** API Function: See header file*/
DECLHIDDEN(void) virtioCorePrintDeviceFeatures(VIRTIOCORE *pVirtio, PCDBGFINFOHLP pHlp,
    const VIRTIO_FEATURES_LIST *s_aDevSpecificFeatures, int cFeatures) {
    virtioCoreR3FeatureDump(pVirtio, pHlp, s_aCoreFeatures, RT_ELEMENTS(s_aCoreFeatures), 1 /*fBanner */);
    virtioCoreR3FeatureDump(pVirtio, pHlp, s_aDevSpecificFeatures, cFeatures, 0 /*fBanner */);
}

#endif

#ifdef LOG_ENABLED

/** API Function: See header file */
DECLHIDDEN(void) virtioCoreHexDump(uint8_t *pv, uint32_t cb, uint32_t uBase, const char *pszTitle)
{
#define ADJCURSOR(cb) pszOut += cb; cbRemain -= cb;
    size_t cbPrint = 0, cbRemain = ((cb / 16) + 1) * 80;
    char *pszBuf = (char *)RTMemAllocZ(cbRemain), *pszOut = pszBuf;
    AssertMsgReturnVoid(pszBuf, ("Out of Memory"));
    if (pszTitle)
    {
        cbPrint = RTStrPrintf(pszOut, cbRemain, "%s [%d bytes]:\n", pszTitle, cb);
        ADJCURSOR(cbPrint);
    }
    for (uint32_t row = 0; row < RT_MAX(1, (cb / 16) + 1) && row * 16 < cb; row++)
    {
        cbPrint = RTStrPrintf(pszOut, cbRemain, "%04x: ", row * 16 + uBase); /* line address */
        ADJCURSOR(cbPrint);
        for (uint8_t col = 0; col < 16; col++)
        {
           uint32_t idx = row * 16 + col;
           if (idx >= cb)
               cbPrint = RTStrPrintf(pszOut, cbRemain, "-- %s", (col + 1) % 8 ? "" : "  ");
           else
               cbPrint = RTStrPrintf(pszOut, cbRemain, "%02x %s", pv[idx], (col + 1) % 8 ? "" : "  ");
            ADJCURSOR(cbPrint);
        }
        for (uint32_t idx = row * 16; idx < row * 16 + 16; idx++)
        {
           cbPrint = RTStrPrintf(pszOut, cbRemain, "%c", (idx >= cb) ? ' ' : (pv[idx] >= 0x20 && pv[idx] <= 0x7e ? pv[idx] : '.'));
           ADJCURSOR(cbPrint);
        }
        *pszOut++ = '\n';
        --cbRemain;
    }
    Log(("%s\n", pszBuf));
    RTMemFree(pszBuf);
    RT_NOREF2(uBase, pv);
#undef ADJCURSOR
}

/* API FUnction: See header file */
DECLHIDDEN(void) virtioCoreGCPhysHexDump(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint16_t cb, uint32_t uBase, const char *pszTitle)
{
    PVIRTIOCORE pVirtio = PDMDEVINS_2_DATA(pDevIns, PVIRTIOCORE);
#define ADJCURSOR(cb) pszOut += cb; cbRemain -= cb;
    size_t cbPrint = 0, cbRemain = ((cb / 16) + 1) * 80;
    char *pszBuf = (char *)RTMemAllocZ(cbRemain), *pszOut = pszBuf;
    AssertMsgReturnVoid(pszBuf, ("Out of Memory"));
    if (pszTitle)
    {
        cbPrint = RTStrPrintf(pszOut, cbRemain, "%s [%d bytes]:\n", pszTitle, cb);
        ADJCURSOR(cbPrint);
    }
    for (uint16_t row = 0; row < (uint16_t)RT_MAX(1, (cb / 16) + 1) && row * 16 < cb; row++)
    {
        uint8_t c;
        cbPrint = RTStrPrintf(pszOut, cbRemain, "%04x: ", row * 16 + uBase); /* line address */
        ADJCURSOR(cbPrint);
        for (uint8_t col = 0; col < 16; col++)
        {
           uint32_t idx = row * 16 + col;
           virtioCoreGCPhysRead(pVirtio, pDevIns, GCPhys + idx, &c, 1);
           if (idx >= cb)
               cbPrint = RTStrPrintf(pszOut, cbRemain, "-- %s", (col + 1) % 8 ? "" : "  ");
           else
               cbPrint = RTStrPrintf(pszOut, cbRemain, "%02x %s", c, (col + 1) % 8 ? "" : "  ");
           ADJCURSOR(cbPrint);
        }
        for (uint16_t idx = row * 16; idx < row * 16 + 16; idx++)
        {
           virtioCoreGCPhysRead(pVirtio, pDevIns, GCPhys + idx, &c, 1);
           cbPrint = RTStrPrintf(pszOut, cbRemain, "%c", (idx >= cb) ? ' ' : (c >= 0x20 && c <= 0x7e ? c : '.'));
           ADJCURSOR(cbPrint);
        }
        *pszOut++ = '\n';
        --cbRemain;
     }
    Log(("%s\n", pszBuf));
    RTMemFree(pszBuf);
    RT_NOREF(uBase);
#undef ADJCURSOR
}


/** API function: See header file */
DECLHIDDEN(void) virtioCoreLogMappedIoValue(const char *pszFunc, const char *pszMember, uint32_t uMemberSize,
                                            const void *pv, uint32_t cb, uint32_t uOffset, int fWrite,
                                            int fHasIndex, uint32_t idx)
{
    if (LogIs6Enabled())
    {
        char szIdx[16];
        if (fHasIndex)
            RTStrPrintf(szIdx, sizeof(szIdx), "[%d]", idx);
        else
            szIdx[0] = '\0';

        if (cb == 1 || cb == 2 || cb == 4 || cb == 8)
        {
            char szDepiction[64];
            size_t cchDepiction;
            if (uOffset != 0 || cb != uMemberSize) /* display bounds if partial member access */
                cchDepiction = RTStrPrintf(szDepiction, sizeof(szDepiction), "%s%s[%d:%d]",
                                           pszMember, szIdx, uOffset, uOffset + cb - 1);
            else
                cchDepiction = RTStrPrintf(szDepiction, sizeof(szDepiction), "%s%s", pszMember, szIdx);

            /* padding */
            if (cchDepiction < 30)
                szDepiction[cchDepiction++] = ' ';
            while (cchDepiction < 30)
                szDepiction[cchDepiction++] = '.';
            szDepiction[cchDepiction] = '\0';

            RTUINT64U uValue;
            uValue.u = 0;
            memcpy(uValue.au8, pv, cb);
            Log6(("%-23s: Guest %s %s %#0*RX64\n",
                      pszFunc, fWrite ? "wrote" : "read ", szDepiction, 2 + cb * 2, uValue.u));
        }
        else /* odd number or oversized access, ... log inline hex-dump style */
        {
            Log6(("%-23s: Guest %s %s%s[%d:%d]: %.*Rhxs\n",
                      pszFunc, fWrite ? "wrote" : "read ", pszMember,
                      szIdx, uOffset, uOffset + cb, cb, pv));
        }
    }
    RT_NOREF2(fWrite, pszFunc);
}

/**
 * Log MMIO-mapped Virtio fDeviceStatus register bitmask, naming the bits
 */
DECLINLINE(void) virtioCoreFormatDeviceStatus(uint8_t bStatus, char *pszBuf, size_t uSize)
{
#   define ADJCURSOR(len) { cp += len; uSize -= len; sep = (char *)" | "; }
    memset(pszBuf, 0, uSize);
    char *cp = pszBuf, *sep = (char *)"";
    size_t len;
    if (bStatus == 0)
        RTStrPrintf(cp, uSize, "RESET");
    else
    {
        if (bStatus & VIRTIO_STATUS_ACKNOWLEDGE)
        {
            len = RTStrPrintf(cp, uSize, "ACKNOWLEDGE");
            ADJCURSOR(len);
        }
        if (bStatus & VIRTIO_STATUS_DRIVER)
        {
            len = RTStrPrintf(cp, uSize, "%sDRIVER", sep);
            ADJCURSOR(len);
        }
        if (bStatus & VIRTIO_STATUS_FEATURES_OK)
        {
            len = RTStrPrintf(cp, uSize, "%sFEATURES_OK", sep);
            ADJCURSOR(len);
        }
        if (bStatus & VIRTIO_STATUS_DRIVER_OK)
        {
            len = RTStrPrintf(cp, uSize, "%sDRIVER_OK", sep);
            ADJCURSOR(len);
        }
        if (bStatus & VIRTIO_STATUS_FAILED)
        {
            len = RTStrPrintf(cp, uSize, "%sFAILED", sep);
            ADJCURSOR(len);
        }
        if (bStatus & VIRTIO_STATUS_DEVICE_NEEDS_RESET)
            RTStrPrintf(cp, uSize, "%sNEEDS_RESET", sep);
    }
#   undef ADJCURSOR
}

#endif /* LOG_ENABLED */

/** API function: See header file */
DECLHIDDEN(int) virtioCoreIsLegacyMode(PVIRTIOCORE pVirtio)
{
    return pVirtio->fLegacyDriver;
}

#ifdef IN_RING3

DECLHIDDEN(int) virtioCoreR3VirtqAttach(PVIRTIOCORE pVirtio, uint16_t uVirtq, const char *pcszName)
{
    LogFunc(("Attaching %s to VirtIO core\n", pcszName));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];
    pVirtq->uVirtq = uVirtq;
    pVirtq->fUsedRingEvent = false;
    pVirtq->fAttached = true;
    RTStrCopy(pVirtq->szName, sizeof(pVirtq->szName), pcszName);
    return VINF_SUCCESS;
}

DECLHIDDEN(int) virtioCoreR3VirtqDetach(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr)
{
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtqNbr];
    pVirtq->uVirtq          = 0;
    pVirtq->uAvailIdxShadow = 0;
    pVirtq->uUsedIdxShadow  = 0;
    pVirtq->fUsedRingEvent  = false;
    pVirtq->fAttached       = false;
    memset(pVirtq->szName, 0, sizeof(pVirtq->szName));
    return VINF_SUCCESS;
}

DECLHIDDEN(bool) virtioCoreR3VirtqIsAttached(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr)
{
    return pVirtio->aVirtqueues[uVirtqNbr].fAttached;
}

DECLHIDDEN(bool) virtioCoreR3VirtqIsEnabled(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr)
{
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtqNbr];
    return (bool)pVirtq->uEnable && pVirtq->GCPhysVirtqDesc;
}

DECLINLINE(void) virtioCoreR3DescInfo(PCDBGFINFOHLP pHlp, PVIRTQ_DESC_T pDesc, uint16_t iDesc, const char *cszTail)
{
    if (pDesc->fFlags & VIRTQ_DESC_F_NEXT)
        pHlp->pfnPrintf(pHlp, "          [%4d]%c%c %5d bytes @ %p  [%4d] %s\n",
                        iDesc, pDesc->fFlags & VIRTQ_DESC_F_INDIRECT ? 'I' : ' ',
                        pDesc->fFlags & VIRTQ_DESC_F_WRITE ? 'W' : 'R',
                        pDesc->cb, pDesc->GCPhysBuf, pDesc->uDescIdxNext, cszTail);
    else
        pHlp->pfnPrintf(pHlp, "          [%4d]%c%c %5d bytes @ %p         %s\n",
                        iDesc, pDesc->fFlags & VIRTQ_DESC_F_INDIRECT ? 'I' : ' ',
                        pDesc->fFlags & VIRTQ_DESC_F_WRITE ? 'W' : 'R',
                        pDesc->cb, pDesc->GCPhysBuf, cszTail);
}

#ifdef VIRTIO_REL_INFO_DUMP
DECLHIDDEN(void) virtioCoreR3DumpAvailRing(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    uint16_t auTmp[VIRTQ_SIZE];
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[0]),
                         auTmp, pVirtq->uQueueSize * sizeof(uint16_t));
    pHlp->pfnPrintf(pHlp, "       avail ring dump:\n%.*RhXd\n", pVirtq->uQueueSize * sizeof(uint16_t), auTmp,
                    pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[0]));
}

DECLHIDDEN(void) virtioCoreR3DumpUsedRing(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq)
{
    VIRTQ_USED_ELEM_T aTmp[VIRTQ_SIZE];
    virtioCoreGCPhysRead(pVirtio, pDevIns,
                         pVirtq->GCPhysVirtqUsed + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[0]),
                         aTmp, pVirtq->uQueueSize * sizeof(VIRTQ_USED_ELEM_T));
    pHlp->pfnPrintf(pHlp, "       used ring dump:\n%.*RhXd\n", pVirtq->uQueueSize * sizeof(VIRTQ_USED_ELEM_T), aTmp,
                    pVirtq->GCPhysVirtqUsed + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[0]));
}

#define VIRTIO_CORE_EVENT_INVALID    0
#define VIRTIO_CORE_EVENT_AVAIL_GET  1  // pVirtq->uAvailIdxShadow, pVirtqBuf->uHeadIdx
#define VIRTIO_CORE_EVENT_AVAIL_PEEK 2  // pVirtq->uAvailIdxShadow, pVirtqBuf->uHeadIdx
#define VIRTIO_CORE_EVENT_AVAIL_NEXT 3  // pVirtq->uAvailIdxShadow, N/A
#define VIRTIO_CORE_EVENT_USED_PUT   4  // pVirtq->uUsedIdxShadow, pVirtqBuf->uHeadIdx
#define VIRTIO_CORE_EVENT_USED_SYNC  5  // pVirtq->uUsedIdxShadow, N/A

static const char *virtioCoreEventText[] = {
    "none <n/a>",
    " get avail",
    "peek avail",
    "next avail",
    " put  used",
    "sync  used"
};

static void virtioCoreTraceEvent(PVIRTIOCORE pVirtio, PVIRTQUEUE pVirtq, const uint8_t event, uint16_t ringIdx, uint16_t descIdx)
{
    if (pVirtq->uQueueSize)
    {
        if (event == VIRTIO_CORE_EVENT_AVAIL_NEXT || event == VIRTIO_CORE_EVENT_USED_SYNC)
            RTTraceBufAddMsgF(pVirtio->hTraceBuf, "%s: %s[%u]\n", pVirtq->szName, virtioCoreEventText[event], ringIdx % pVirtq->uQueueSize);
        else
            RTTraceBufAddMsgF(pVirtio->hTraceBuf, "%s: %s[%u]=%u\n", pVirtq->szName, virtioCoreEventText[event], ringIdx % pVirtq->uQueueSize, descIdx);
    }
    else
    {
        if (event == VIRTIO_CORE_EVENT_AVAIL_NEXT || event == VIRTIO_CORE_EVENT_USED_SYNC)
            RTTraceBufAddMsgF(pVirtio->hTraceBuf, "%s: %s[%u]\n", pVirtq->szName, virtioCoreEventText[event], ringIdx);
        else
            RTTraceBufAddMsgF(pVirtio->hTraceBuf, "%s: %s[%u]=%u\n", pVirtq->szName, virtioCoreEventText[event], ringIdx, descIdx);
    }
}

static DECLCALLBACK(int) dumpOneEntryToRelLog(RTTRACEBUF hTraceBuf, uint32_t iEntry, uint64_t NanoTS, RTCPUID idCpu, const char *pszMsg, void *pvUser)
{
    RT_NOREF(hTraceBuf, pvUser);
    LogRel(("%03u / %llu / %02u / %s", iEntry, NanoTS, idCpu, pszMsg));
    return VINF_SUCCESS;
}

static inline void virtioCoreDumpTraceBufToRelLog(RTTRACEBUF hTraceBuf)
{
    RTTraceBufEnumEntries(hTraceBuf, dumpOneEntryToRelLog, NULL);
}
#endif /* VIRTIO_REL_INFO_DUMP */

/** API Fuunction: See header file */
DECLHIDDEN(void) virtioCoreR3VirtqInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, PVIRTIOCORE pVirtio, const char *pszArgs, int uVirtq)
{
    RT_NOREF(pszArgs);
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    /** @todo add ability to dump physical contents described by any descriptor (using existing VirtIO core API function) */
//  bool fDump      = pszArgs && (*pszArgs == 'd' || *pszArgs == 'D'); /* "dump" (avail phys descriptor)"

    uint16_t uAvailIdx       = virtioReadAvailRingIdx(pDevIns, pVirtio, pVirtq);
    uint16_t uAvailIdxShadow = pVirtq->uAvailIdxShadow;

    uint16_t uUsedIdx        = virtioReadUsedRingIdx(pDevIns, pVirtio, pVirtq);
    uint16_t uUsedIdxShadow  = pVirtq->uUsedIdxShadow;

    uint16_t uAvailEventIdx = 0;
    uint16_t uUsedEventIdx = 0;
    bool     fNotify = !!(pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX);
    if (fNotify)
    {
        uUsedEventIdx = virtioReadAvailUsedEvent(pDevIns, pVirtio, pVirtq);
        /* There is no helper for reading AvailEvent since the device is not supposed to read it. */
        virtioCoreGCPhysRead(pVirtio, pDevIns,
                            pVirtq->GCPhysVirtqUsed
                            + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[pVirtq->uQueueSize]),
                            &uAvailEventIdx, sizeof(uAvailEventIdx));
    }

#ifndef VIRTIO_REL_INFO_DUMP
    VIRTQBUF_T VirtqBuf;
    PVIRTQBUF pVirtqBuf = &VirtqBuf;
    RT_ZERO(VirtqBuf);  /* Make sure pSgPhysSend and pSgPhysReturn are initialized. */
    bool fEmpty = IS_VIRTQ_EMPTY(pDevIns, pVirtio, pVirtq);

    LogFunc(("%s, empty = %s\n", pVirtq->szName, fEmpty ? "true" : "false"));

    int cSendSegs = 0, cReturnSegs = 0;
    if (!fEmpty)
    {
        virtioCoreR3VirtqAvailBufPeek(pDevIns,  pVirtio, uVirtq, pVirtqBuf);
        cSendSegs   = pVirtqBuf->pSgPhysSend ? pVirtqBuf->pSgPhysSend->cSegs : 0;
        cReturnSegs = pVirtqBuf->pSgPhysReturn ? pVirtqBuf->pSgPhysReturn->cSegs : 0;
    }
#endif /* !VIRTIO_REL_INFO_DUMP */

    bool fAvailNoInterrupt   = virtioReadAvailRingFlags(pDevIns, pVirtio, pVirtq) & VIRTQ_AVAIL_F_NO_INTERRUPT;
    bool fUsedNoNotify       = virtioReadUsedRingFlags(pDevIns, pVirtio, pVirtq) & VIRTQ_USED_F_NO_NOTIFY;

    pHlp->pfnPrintf(pHlp, "       queue enabled: ........... %s\n", pVirtq->uEnable ? "true" : "false");
    pHlp->pfnPrintf(pHlp, "       size: .................... %d\n", pVirtq->uQueueSize);
    pHlp->pfnPrintf(pHlp, "       notify offset: ........... %d\n", pVirtq->uNotifyOffset);
    if (pVirtio->fMsiSupport)
        pHlp->pfnPrintf(pHlp, "       MSIX vector: ....... %4.4x\n", pVirtq->uMsixVector);
    pHlp->pfnPrintf(pHlp, "\n");
    pHlp->pfnPrintf(pHlp, "       avail ring (%d entries):\n", uAvailIdx - uAvailIdxShadow);
    pHlp->pfnPrintf(pHlp, "          index: ................ %d (%d)\n", pVirtq->uQueueSize ? uAvailIdx % pVirtq->uQueueSize : uAvailIdx, uAvailIdx);
    pHlp->pfnPrintf(pHlp, "          shadow: ............... %d (%d)\n", pVirtq->uQueueSize ? uAvailIdxShadow % pVirtq->uQueueSize : uAvailIdxShadow, uAvailIdxShadow);
    pHlp->pfnPrintf(pHlp, "          flags: ................ %s\n", fAvailNoInterrupt ? "NO_INTERRUPT" : "");
    pHlp->pfnPrintf(pHlp, "\n");
    pHlp->pfnPrintf(pHlp, "       used ring (%d entries):\n",  uUsedIdxShadow - uUsedIdx);
    pHlp->pfnPrintf(pHlp, "          index: ................ %d (%d)\n", pVirtq->uQueueSize ? uUsedIdx % pVirtq->uQueueSize : uUsedIdx, uUsedIdx);
    pHlp->pfnPrintf(pHlp, "          shadow: ............... %d (%d)\n", pVirtq->uQueueSize ? uUsedIdxShadow % pVirtq->uQueueSize : uUsedIdxShadow, uUsedIdxShadow);
    pHlp->pfnPrintf(pHlp, "          flags: ................ %s\n", fUsedNoNotify ? "NO_NOTIFY" : "");
    pHlp->pfnPrintf(pHlp, "\n");
#ifndef VIRTIO_REL_INFO_DUMP
    if (!fEmpty)
    {
        pHlp->pfnPrintf(pHlp, "       desc chain:\n");
        pHlp->pfnPrintf(pHlp, "          head idx: ............. %d (%d)\n", pVirtq->uQueueSize ? uUsedIdx % pVirtq->uQueueSize : uUsedIdx, uUsedIdx);
        pHlp->pfnPrintf(pHlp, "          segs: ................. %d\n", cSendSegs + cReturnSegs);
        pHlp->pfnPrintf(pHlp, "          refCnt ................ %d\n", pVirtqBuf->cRefs);
        pHlp->pfnPrintf(pHlp, "\n");
        pHlp->pfnPrintf(pHlp, "          host-to-guest (%d bytes):\n",      pVirtqBuf->cbPhysSend);
        pHlp->pfnPrintf(pHlp,     "             segs: .............. %d\n", cSendSegs);
        if (cSendSegs)
        {
            pHlp->pfnPrintf(pHlp, "             index: ............. %d\n", pVirtqBuf->pSgPhysSend->idxSeg);
            pHlp->pfnPrintf(pHlp, "             unsent ............. %d\n", pVirtqBuf->pSgPhysSend->cbSegLeft);
        }
        pHlp->pfnPrintf(pHlp, "\n");
        pHlp->pfnPrintf(pHlp,     "          guest-to-host (%d bytes):\n",  pVirtqBuf->cbPhysReturn);
        pHlp->pfnPrintf(pHlp,     "             segs: .............. %d\n", cReturnSegs);
        if (cReturnSegs)
        {
            pHlp->pfnPrintf(pHlp, "             index: ............. %d\n", pVirtqBuf->pSgPhysReturn->idxSeg);
            pHlp->pfnPrintf(pHlp, "             unsent ............. %d\n", pVirtqBuf->pSgPhysReturn->cbSegLeft);
        }
    } else
        pHlp->pfnPrintf(pHlp,     "       no desc chains available\n");
#endif /* !VIRTIO_REL_INFO_DUMP */
    pHlp->pfnPrintf(pHlp, "\n");

    /* Avoid handling zero-sized queues, there is nothing to show anyway. */
    if (pVirtq->uQueueSize == 0)
        return;

    pHlp->pfnPrintf(pHlp, "       desc table:\n");
    /*
     * Each line in the descriptor table output consists of two parts: a fixed part and a variable "tail".
     * The fixed part shows the descriptor index, its writability, size, physical address, and optionally
     * which descriptor is next the chain. The tail shows which elements of avail/used rings point to
     * this descriptor.
     */
    VIRTQ_DESC_T descTable[VIRTQ_SIZE];
    char aszTails[VIRTQ_SIZE][32];
    virtioCoreGCPhysRead(pVirtio, pDevIns, pVirtq->GCPhysVirtqDesc,
                        &descTable, sizeof(VIRTQ_DESC_T) * pVirtq->uQueueSize);
    RT_BZERO(aszTails, sizeof(aszTails));   /* No tails by default */

    /* Fill avail tail fields. */

    /* The first available descriptor gets outer reverse angle brackets. */
    char chOuterLeft = '>', chOuterRight = '<';
    char chLeft = '[', chRight = ']';
    /* Use 'not-equal' instead of 'less' because of uint16_t wrapping! */
    for (uint16_t i = uAvailIdxShadow; i != uAvailIdx; i++)
    {
        /* The last descriptor gets inner curly braces, inner square brackets for the rest. */
        if (i + 1 == uAvailIdx) { chLeft = '{'; chRight = '}'; }
        uint16_t uDescIdx = virtioReadAvailDescIdx(pDevIns, pVirtio, pVirtq, i);
        /* Print an exclamation sign instead of outer right bracket if this descriptor triggers notification. */
        RTStrPrintf(aszTails[uDescIdx], sizeof(aszTails[0]), "%c%c%4d%c%c ",
                    chOuterLeft, chLeft, i % pVirtq->uQueueSize, chRight,
                    fNotify ? ((i % pVirtq->uQueueSize) == (uAvailEventIdx % pVirtq->uQueueSize) ? '!' : chOuterRight) : chOuterRight);
        chOuterLeft = chOuterRight = ' ';
    }

    /* Fill used tail fields, see comments in the similar loop above. */

    chOuterLeft = '>'; chOuterRight = '<';
    chLeft = '['; chRight = ']';
    for (uint16_t i = uUsedIdx; i != uUsedIdxShadow; i++)
    {
        VIRTQ_USED_ELEM_T elem;
        virtioCoreGCPhysRead(pVirtio, pDevIns,
                             pVirtq->GCPhysVirtqUsed
                             + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[i % pVirtq->uQueueSize]),
                             &elem, sizeof(elem));
        if (i + 1 == uUsedIdxShadow) { chLeft = '{'; chRight = '}'; }
        char *szTail = aszTails[elem.uDescIdx % pVirtq->uQueueSize];
        /* Add empty avail field if none is present, 9 spaces + terminating zero. */
        if (*szTail == '\0')
            RTStrCopy(szTail, 10, "          ");
        RTStrPrintf(szTail + 9, sizeof(aszTails[0]) - 9, " %c%c%4d%c%c %d bytes",
                    chOuterLeft, chLeft, i % pVirtq->uQueueSize, chRight,
                    fNotify ? ((i % pVirtq->uQueueSize) == (uUsedEventIdx % pVirtq->uQueueSize) ? '!' : chOuterRight) : chOuterRight,
                    elem.cbElem);
        chOuterLeft = chOuterRight = ' ';
    }

    pHlp->pfnPrintf(pHlp, "          index w/r size         phys addr        next    @avail   @used\n");
    pHlp->pfnPrintf(pHlp, "          ------ - -----------   ---------------- ------- -------- ------------------\n");
    for (uint16_t i = 0; i < pVirtq->uQueueSize; i++)
        virtioCoreR3DescInfo(pHlp, &descTable[i], i, aszTails[i]);
#ifdef VIRTIO_REL_INFO_DUMP
    pHlp->pfnPrintf(pHlp, "\n");
    virtioCoreR3DumpAvailRing(pDevIns, pHlp, pVirtio, pVirtq);
    pHlp->pfnPrintf(pHlp, "\n");
    virtioCoreR3DumpUsedRing(pDevIns, pHlp, pVirtio, pVirtq);
#endif /* VIRTIO_REL_INFO_DUMP */
}


/** API Function: See header file */
DECLHIDDEN(PVIRTQBUF) virtioCoreR3VirtqBufAlloc(void)
{
    PVIRTQBUF pVirtqBuf = (PVIRTQBUF)RTMemAllocZ(sizeof(VIRTQBUF_T));
    AssertReturn(pVirtqBuf, NULL);
    pVirtqBuf->u32Magic  = VIRTQBUF_MAGIC;
    pVirtqBuf->cRefs     = 1;
    return pVirtqBuf;
}


/** API Function: See header file */
DECLHIDDEN(uint32_t) virtioCoreR3VirtqBufRetain(PVIRTQBUF pVirtqBuf)
{
    AssertReturn(pVirtqBuf, UINT32_MAX);
    AssertReturn(pVirtqBuf->u32Magic == VIRTQBUF_MAGIC, UINT32_MAX);
    uint32_t cRefs = ASMAtomicIncU32(&pVirtqBuf->cRefs);
    Assert(cRefs > 1);
    Assert(cRefs < 16);
    return cRefs;
}

/** API Function: See header file */
DECLHIDDEN(uint32_t) virtioCoreR3VirtqBufRelease(PVIRTIOCORE pVirtio, PVIRTQBUF pVirtqBuf)
{
    if (!pVirtqBuf)
        return 0;
    AssertReturn(pVirtqBuf, 0);
    AssertReturn(pVirtqBuf->u32Magic == VIRTQBUF_MAGIC, 0);
    uint32_t cRefs = ASMAtomicDecU32(&pVirtqBuf->cRefs);
    Assert(cRefs < 16);
    if (cRefs == 0)
    {
        pVirtqBuf->u32Magic = ~VIRTQBUF_MAGIC;
        RTMemFree(pVirtqBuf);
#ifdef VBOX_WITH_STATISTICS
        STAM_REL_COUNTER_INC(&pVirtio->StatDescChainsFreed);
#endif
    }
    RT_NOREF(pVirtio);
    return cRefs;
}

/** API Function: See header file */
DECLHIDDEN(void) virtioCoreNotifyConfigChanged(PVIRTIOCORE pVirtio)
{
    virtioNudgeGuest(pVirtio->pDevInsR3, pVirtio, VIRTIO_ISR_DEVICE_CONFIG, pVirtio->uMsixConfig);
}


/** API Function: See header file */
DECLHIDDEN(void) virtioCoreVirtqEnableNotify(PVIRTIOCORE pVirtio, uint16_t uVirtq, bool fEnable)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    if (IS_DRIVER_OK(pVirtio))
    {
        uint16_t fFlags = virtioReadUsedRingFlags(pVirtio->pDevInsR3, pVirtio, pVirtq);

        if (fEnable)
            fFlags &= ~VIRTQ_USED_F_NO_NOTIFY;
        else
            fFlags |= VIRTQ_USED_F_NO_NOTIFY;

        virtioWriteUsedRingFlags(pVirtio->pDevInsR3, pVirtio, pVirtq, fFlags);
    }
}

/** API function: See Header file  */
DECLHIDDEN(void) virtioCoreResetAll(PVIRTIOCORE pVirtio)
{
    LogFunc(("\n"));
    pVirtio->fDeviceStatus |= VIRTIO_STATUS_DEVICE_NEEDS_RESET;
    if (IS_DRIVER_OK(pVirtio))
    {
        if (!pVirtio->fLegacyDriver)
            pVirtio->fGenUpdatePending = true;
        virtioNudgeGuest(pVirtio->pDevInsR3, pVirtio, VIRTIO_ISR_DEVICE_CONFIG, pVirtio->uMsixConfig);
    }
}

/** API function: See Header file  */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufPeek(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq, PVIRTQBUF pVirtqBuf)
{
    return virtioCoreR3VirtqAvailBufGet(pDevIns, pVirtio, uVirtq, pVirtqBuf, false);
}


/** API function: See Header file  */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufNext(PVIRTIOCORE pVirtio, uint16_t uVirtq)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    if (!pVirtio->fLegacyDriver)
        AssertMsgReturn((pVirtio->fDeviceStatus & VIRTIO_STATUS_DRIVER_OK) && pVirtq->uEnable,
            ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    if (IS_VIRTQ_EMPTY(pVirtio->pDevInsR3, pVirtio, pVirtq))
        return VERR_NOT_AVAILABLE;

    Log6Func(("%s avail shadow idx: %u\n", pVirtq->szName, pVirtq->uAvailIdxShadow));
    pVirtq->uAvailIdxShadow++;
#ifdef VIRTIO_REL_INFO_DUMP
    virtioCoreTraceEvent(pVirtio, pVirtq, VIRTIO_CORE_EVENT_AVAIL_NEXT, pVirtq->uAvailIdxShadow, 0);
#endif /* VIRTIO_REL_INFO_DUMP */

    return VINF_SUCCESS;
}

#ifdef VIRTIO_REL_INFO_DUMP
DECLCALLBACK(void) virtioNetR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs);

static DECLCALLBACK(void) dbgVirtio_Printf(PCDBGFINFOHLP pHlp, const char *pszFormat, ...)
{
    RT_NOREF(pHlp);
    va_list va;
    va_start(va, pszFormat);
    RTLogRelPrintfV(pszFormat, va);
    va_end(va);
}


static DECLCALLBACK(void) dbgVirtio_PrintfV(PCDBGFINFOHLP pHlp, const char *pszFormat, va_list args)
{
    RT_NOREF(pHlp);
    RTLogRelPrintfV(pszFormat, args);
}


/**
 * @interface_method_impl{DBGCCMDHLP,pfnGetDbgfOutputHlp}
 */
static void dbgVirtioDump(PPDMDEVINS pDevIns)
{
    PVIRTIOCORE pVirtio = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    LogRel(("dbgVirtioDump(%s)\n", pVirtio->szInstance));
    if (RTStrNCmp("virtio-net", pVirtio->szInstance, 10) == 0)
    {
        DBGFINFOHLP DbgHlp;

        DbgHlp.pfnPrintf      = dbgVirtio_Printf;
        DbgHlp.pfnPrintfV     = dbgVirtio_PrintfV;
        DbgHlp.pfnGetOptError = NULL;

        virtioNetR3Info(pDevIns, &DbgHlp, "a"); // Print everything!
    }
}

/** Temporary API function: See Header file  */
void virtioCorePutAllAvailBufsToUsedRing(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq)
{
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];
    uint16_t uStartAvailIdx = pVirtq->uAvailIdxShadow;
    uint16_t uStartUsedIdx  = pVirtq->uUsedIdxShadow;

    /*
     * Copy chain head indices from avail to used, specifying zero processed length, which should be ok
     * for TX queue of virtio-net only!
     */
    while (pVirtq->uAvailIdxShadow != virtioReadAvailRingIdx(pDevIns, pVirtio, pVirtq))
        virtioWriteUsedElem(pDevIns, pVirtio, pVirtq, pVirtq->uUsedIdxShadow++,
                            virtioReadAvailDescIdx(pDevIns, pVirtio, pVirtq, pVirtq->uAvailIdxShadow++), 0);

    if (pVirtq->uAvailIdxShadow != uStartAvailIdx)
    {
        LogRel(("[%s] Copied indices for %s [avail(%u:%u) -> used(%u:%u)]\n", pVirtio->szInstance, pVirtq->szName,
                uStartAvailIdx, pVirtq->uAvailIdxShadow - 1, uStartUsedIdx, pVirtq->uUsedIdxShadow - 1));
        virtioWriteUsedRingIdx(pDevIns, pVirtio, pVirtq, pVirtq->uUsedIdxShadow);
        virtioCoreNotifyGuestDriver(pDevIns, pVirtio, uVirtq);
    }
    else
        LogRel(("[%s] Avail ring is empty for %s, nothing to copy.\n", pVirtio->szInstance, pVirtq->szName));
}
#endif /* VIRTIO_REL_INFO_DUMP */
/** API Function: See header file */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufGet(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq,
                                             uint16_t uHeadIdx, PVIRTQBUF pVirtqBuf)
{
    AssertMsgReturn(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues),
                        ("uVirtq out of range"), VERR_INVALID_PARAMETER);

    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    if (!pVirtio->fLegacyDriver)
        AssertMsgReturn((pVirtio->fDeviceStatus & VIRTIO_STATUS_DRIVER_OK) && pVirtq->uEnable,
            ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    uint16_t uDescIdx = uHeadIdx;

    Log6Func(("%s DESC CHAIN: (head idx = %u)\n", pVirtio->aVirtqueues[uVirtq].szName, uHeadIdx));

    /*
     * Allocate and initialize the descriptor chain structure.
     */
    pVirtqBuf->u32Magic  = VIRTQBUF_MAGIC;
    pVirtqBuf->cRefs     = 1;
    pVirtqBuf->uHeadIdx  = uHeadIdx;
    pVirtqBuf->uVirtq    = uVirtq;

    /*
     * Gather segments.
     */
    VIRTQ_DESC_T desc;

    uint32_t cbIn     = 0;
    uint32_t cbOut    = 0;
    uint32_t cSegsIn  = 0;
    uint32_t cSegsOut = 0;

    PVIRTIOSGSEG paSegsIn  = pVirtqBuf->aSegsIn;
    PVIRTIOSGSEG paSegsOut = pVirtqBuf->aSegsOut;

    do
    {
        PVIRTIOSGSEG pSeg;
        /*
         * Malicious guests may go beyond paSegsIn or paSegsOut boundaries by linking
         * several descriptors into a loop. Since there is no legitimate way to get a sequences of
         * linked descriptors exceeding the total number of descriptors in the ring (see @bugref{8620}),
         * the following aborts I/O if breach and employs a simple log throttling algorithm to notify.
         */
#ifdef VIRTIO_REL_INFO_DUMP
        if (cSegsIn + cSegsOut >= pVirtq->uQueueSize || ASMAtomicCmpXchgBool(&pVirtio->fTestRecovery, false, true))
        {
            static volatile uint32_t s_cMessages  = 0;
            if (ASMAtomicIncU32(&s_cMessages) <= 10)
            {
                LogRel(("Too many linked descriptors; check if the guest arranges descriptors in a loop "
                        "(cSegsIn=%u cSegsOut=%u uQueueSize=%u uDescIdx=%u uHeadIdx=%u uAvailIdxShadow=%u queue=%s).\n",
                        cSegsIn, cSegsOut, pVirtq->uQueueSize, uDescIdx, pVirtqBuf->uHeadIdx, pVirtq->uAvailIdxShadow, pVirtq->szName));
                virtioCoreDumpTraceBufToRelLog(pVirtio->hTraceBuf);
                dbgVirtioDump(pDevIns);
            }
            /* Disable the queue to prevent its operation until it is re-initialized. */
            pVirtq->uEnable = false;
            return VERR_INVALID_STATE;
        }
#else /* !VIRTIO_REL_INFO_DUMP */
        if (cSegsIn + cSegsOut >= pVirtq->uQueueSize)
        {
            static volatile uint32_t s_cMessages  = 0;
            static volatile uint32_t s_cThreshold = 1;
            if (ASMAtomicIncU32(&s_cMessages) == ASMAtomicReadU32(&s_cThreshold))
            {
                LogRelMax(64, ("Too many linked descriptors; check if the guest arranges descriptors in a loop "
                               "(cSegsIn=%u cSegsOut=%u uQueueSize=%u uDescIdx=%u uHeadIdx=%u uAvailIdxShadow=%u queue=%s).\n",
                               cSegsIn, cSegsOut, pVirtq->uQueueSize, uDescIdx, pVirtqBuf->uHeadIdx, pVirtq->uAvailIdxShadow, pVirtq->szName));
                if (ASMAtomicReadU32(&s_cMessages) != 1)
                    LogRelMax(64, ("(the above error has occured %u times so far)\n", ASMAtomicReadU32(&s_cMessages)));
                ASMAtomicWriteU32(&s_cThreshold, ASMAtomicReadU32(&s_cThreshold) * 10);
            }
            break;
        }
#endif /* !VIRTIO_REL_INFO_DUMP */
        /* Check if the limit has been reached for input chain (see section 2.4.4.1 of virtio 1.0 spec). */
        if (cSegsIn >= RT_ELEMENTS(pVirtqBuf->aSegsIn))
        {
            LogRelMax(64, ("Too many input descriptors (cSegsIn=%u).\n", cSegsIn));
            break;
        }
        /* Check if the limit has been reached for output chain (see section 2.4.4.1 of virtio 1.0 spec). */
        if (cSegsOut >= RT_ELEMENTS(pVirtqBuf->aSegsOut))
        {
            LogRelMax(64, ("Too many output descriptors (cSegsOut=%u).\n", cSegsOut));
            break;
        }
        RT_UNTRUSTED_VALIDATED_FENCE();

        virtioReadDesc(pDevIns, pVirtio, pVirtq, uDescIdx, &desc);

        if (desc.fFlags & VIRTQ_DESC_F_WRITE)
        {
            Log6Func(("%s IN  idx=%-4u seg=%-3u addr=%RGp cb=%u\n", pVirtq->szName, uDescIdx, cSegsIn, desc.GCPhysBuf, desc.cb));
            cbIn += desc.cb;
            pSeg = &paSegsIn[cSegsIn++];
        }
        else
        {
            Log6Func(("%s OUT desc_idx=%-4u seg=%-3u addr=%RGp cb=%u\n", pVirtq->szName, uDescIdx, cSegsOut, desc.GCPhysBuf, desc.cb));
            cbOut += desc.cb;
            pSeg = &paSegsOut[cSegsOut++];
#ifdef DEEP_DEBUG
            if (LogIs11Enabled())
            {
                virtioCoreGCPhysHexDump(pDevIns, desc.GCPhysBuf, desc.cb, 0, NULL);
                Log(("\n"));
            }
#endif
        }
        pSeg->GCPhys = desc.GCPhysBuf;
        pSeg->cbSeg = desc.cb;
        uDescIdx = desc.uDescIdxNext;
    } while (desc.fFlags & VIRTQ_DESC_F_NEXT);

    /*
     * Add segments to the descriptor chain structure.
     */
    if (cSegsIn)
    {
        virtioCoreGCPhysChainInit(&pVirtqBuf->SgBufIn, paSegsIn, cSegsIn);
        pVirtqBuf->pSgPhysReturn = &pVirtqBuf->SgBufIn;
        pVirtqBuf->cbPhysReturn  = cbIn;
#ifdef VBOX_WITH_STATISTICS
        STAM_REL_COUNTER_ADD(&pVirtio->StatDescChainsSegsIn, cSegsIn);
#endif
    }

    if (cSegsOut)
    {
        virtioCoreGCPhysChainInit(&pVirtqBuf->SgBufOut, paSegsOut, cSegsOut);
        pVirtqBuf->pSgPhysSend   = &pVirtqBuf->SgBufOut;
        pVirtqBuf->cbPhysSend    = cbOut;
#ifdef VBOX_WITH_STATISTICS
        STAM_REL_COUNTER_ADD(&pVirtio->StatDescChainsSegsOut, cSegsOut);
#endif
    }

#ifdef VBOX_WITH_STATISTICS
    STAM_REL_COUNTER_INC(&pVirtio->StatDescChainsAllocated);
#endif
    Log6Func(("%s -- segs OUT: %u (%u bytes)   IN: %u (%u bytes) --\n",
        pVirtq->szName, cSegsOut, cbOut, cSegsIn, cbIn));

    return VINF_SUCCESS;
}

/** API function: See Header file  */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufGet(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq,
                                             PVIRTQBUF pVirtqBuf, bool fRemove)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    if (IS_VIRTQ_EMPTY(pDevIns, pVirtio, pVirtq))
        return VERR_NOT_AVAILABLE;

    uint16_t uHeadIdx = virtioReadAvailDescIdx(pDevIns, pVirtio, pVirtq, pVirtq->uAvailIdxShadow);

    if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
        virtioWriteUsedAvailEvent(pDevIns,pVirtio, pVirtq, pVirtq->uAvailIdxShadow + 1);

#ifdef VIRTIO_REL_INFO_DUMP
    virtioCoreTraceEvent(pVirtio, pVirtq, fRemove ? VIRTIO_CORE_EVENT_AVAIL_GET : VIRTIO_CORE_EVENT_AVAIL_PEEK, pVirtq->uAvailIdxShadow, uHeadIdx);
#endif /* VIRTIO_REL_INFO_DUMP */
    if (fRemove)
        pVirtq->uAvailIdxShadow++;

    return virtioCoreR3VirtqAvailBufGet(pDevIns, pVirtio, uVirtq, uHeadIdx, pVirtqBuf);
}

/** API function: See Header file  */
DECLHIDDEN(int) virtioCoreR3VirtqUsedBufPut(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq, PRTSGBUF pSgVirtReturn,
                                            PVIRTQBUF pVirtqBuf, bool fFence)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    PVIRTIOSGBUF pSgPhysReturn = pVirtqBuf->pSgPhysReturn;

    Assert(pVirtqBuf->u32Magic == VIRTQBUF_MAGIC);
    Assert(pVirtqBuf->cRefs > 0);

    /*
     * Workaround for a bug in FreeBSD's virtio-net driver up until 12.3 which supports only the legacy style devive.
     * When the device is re-initialized from the driver it violates the spec and posts commands to the control queue
     * before setting the DRIVER_OK flag, breaking the following check and rendering the device non-functional.
     * The queues are properly set up at this stage however so no real harm is done and we can safely continue here,
     * for the legacy device only of course after making sure the queue is properly set up.
     */
    AssertMsgReturn(   IS_DRIVER_OK(pVirtio)
                    || (   pVirtio->fLegacyDriver
                        && pVirtq->GCPhysVirtqDesc),
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    Log6Func(("    Copying device data to %s, [desc:%u -> used ring:%u]\n",
              VIRTQNAME(pVirtio, uVirtq), pVirtqBuf->uHeadIdx, pVirtq->uUsedIdxShadow));

    /* Copy s/g buf (virtual memory) to guest phys mem (VirtIO "IN" direction). */

    size_t cbCopy = 0, cbTotal = 0, cbRemain = 0;

    /** @todo r=aeichner Check whether VirtIO should return an error if the device wants to return data but
     *                   the guest didn't set up an IN buffer. */
    if (   pSgVirtReturn
        && pSgPhysReturn)
    {
        size_t cbTarget = virtioCoreGCPhysChainCalcBufSize(pSgPhysReturn);
        cbRemain = cbTotal = RTSgBufCalcTotalLength(pSgVirtReturn);
        AssertMsgReturn(cbTarget >= cbRemain, ("No space to write data to phys memory"), VERR_BUFFER_OVERFLOW);
        virtioCoreGCPhysChainReset(pSgPhysReturn);
        while (cbRemain)
        {
            cbCopy = RT_MIN(pSgVirtReturn->cbSegLeft,  pSgPhysReturn->cbSegLeft);
            AssertReturn(cbCopy > 0, VERR_INVALID_PARAMETER);
            virtioCoreGCPhysWrite(pVirtio, pDevIns, (RTGCPHYS)pSgPhysReturn->GCPhysCur, pSgVirtReturn->pvSegCur, cbCopy);
            RTSgBufAdvance(pSgVirtReturn, cbCopy);
            virtioCoreGCPhysChainAdvance(pSgPhysReturn, cbCopy);
            cbRemain -= cbCopy;
        }

        if (fFence)
            RT_UNTRUSTED_NONVOLATILE_COPY_FENCE(); /* needed? */

        Assert(!(cbCopy >> 32));
    }

    /* Flag if write-ahead crosses threshold where guest driver indicated it wants event notification */
    if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
        if (pVirtq->uUsedIdxShadow == virtioReadAvailUsedEvent(pDevIns, pVirtio, pVirtq))
            pVirtq->fUsedRingEvent = true;

    /*
     * Place used buffer's descriptor in used ring but don't update used ring's slot index.
     * That will be done with a subsequent client call to virtioCoreVirtqUsedRingSync()
     *
     * @todo r=aeichner: The increment of the shadow index is not atomic but this code can be called
     *                   concurrently!!
     */
#ifdef VIRTIO_REL_INFO_DUMP
    virtioCoreTraceEvent(pVirtio, pVirtq, VIRTIO_CORE_EVENT_USED_PUT, pVirtq->uUsedIdxShadow, pVirtqBuf->uHeadIdx);
#endif /* VIRTIO_REL_INFO_DUMP */
    virtioWriteUsedElem(pDevIns, pVirtio, pVirtq, pVirtq->uUsedIdxShadow++, pVirtqBuf->uHeadIdx, (uint32_t)cbTotal);

#ifdef LOG_ENABLED
    if (   LogIs6Enabled()
        && pSgVirtReturn
        && pSgPhysReturn)
    {

        LogFunc(("     ... %d segs, %zu bytes, copied to %u byte buf@offset=%u. Residual: %zu bytes\n",
             pSgVirtReturn->cSegs,  cbTotal - cbRemain,  pVirtqBuf->cbPhysReturn,
              ((virtioCoreGCPhysChainCalcBufSize(pVirtqBuf->pSgPhysReturn) -
                virtioCoreGCPhysChainCalcLengthLeft(pVirtqBuf->pSgPhysReturn)) - (cbTotal - cbRemain)),
                virtioCoreGCPhysChainCalcLengthLeft(pVirtqBuf->pSgPhysReturn) ));

        uint16_t uPending = virtioCoreR3CountPendingBufs(
                                virtioReadUsedRingIdx(pDevIns, pVirtio, pVirtq),
                                pVirtq->uUsedIdxShadow, pVirtq->uQueueSize);

        LogFunc(("    %u used buf%s not synced in %s\n", uPending, uPending == 1 ? "" : "s ",
                    VIRTQNAME(pVirtio, uVirtq)));
    }
#endif
    return VINF_SUCCESS;
}

/** API function: See Header file  */
DECLHIDDEN(int) virtioCoreR3VirtqUsedBufPut(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq,
                                            size_t cb, void const *pv, PVIRTQBUF pVirtqBuf, size_t cbEnqueue, bool fFence)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    Assert(pv);

    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];
    PVIRTIOSGBUF pSgPhysReturn = pVirtqBuf->pSgPhysReturn;

    Assert(pVirtqBuf->u32Magic == VIRTQBUF_MAGIC);
    Assert(pVirtqBuf->cRefs > 0);

    AssertMsgReturn(IS_DRIVER_OK(pVirtio), ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    Log6Func(("    Copying device data to %s, [desc chain head idx:%u]\n",
              VIRTQNAME(pVirtio, uVirtq), pVirtqBuf->uHeadIdx));
    /*
     * Convert virtual memory simple buffer to guest physical memory (VirtIO descriptor chain)
     */
    uint8_t *pvBuf = (uint8_t *)pv;
    size_t cbRemain = cb, cbCopy = 0;
    while (cbRemain)
    {
        cbCopy = RT_MIN(pSgPhysReturn->cbSegLeft, cbRemain);
        Assert(cbCopy > 0);
        virtioCoreGCPhysWrite(pVirtio, pDevIns, (RTGCPHYS)pSgPhysReturn->GCPhysCur, pvBuf, cbCopy);
        virtioCoreGCPhysChainAdvance(pSgPhysReturn, cbCopy);
        pvBuf += cbCopy;
        cbRemain -= cbCopy;
    }
    LogFunc(("     ...%zu bytes, copied to %u byte buf@offset=%u. Residual: %zu bytes\n",
              cb ,  pVirtqBuf->cbPhysReturn,
              ((virtioCoreGCPhysChainCalcBufSize(pVirtqBuf->pSgPhysReturn) -
                 virtioCoreGCPhysChainCalcLengthLeft(pVirtqBuf->pSgPhysReturn)) - cb),
                 virtioCoreGCPhysChainCalcLengthLeft(pVirtqBuf->pSgPhysReturn)));

    if (cbEnqueue)
    {
        if (fFence)
        {
            RT_UNTRUSTED_NONVOLATILE_COPY_FENCE(); /* needed? */
            Assert(!(cbCopy >> 32));
        }
        /* Flag if write-ahead crosses threshold where guest driver indicated it wants event notification */
        if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
            if (pVirtq->uUsedIdxShadow == virtioReadAvailUsedEvent(pDevIns, pVirtio, pVirtq))
                pVirtq->fUsedRingEvent = true;
        /*
         * Place used buffer's descriptor in used ring but don't update used ring's slot index.
         * That will be done with a subsequent client call to virtioCoreVirtqUsedRingSync()
         */
        Log6Func(("    Enqueue desc chain head idx %u to %s used ring @ %u\n", pVirtqBuf->uHeadIdx,
                VIRTQNAME(pVirtio, uVirtq), pVirtq->uUsedIdxShadow));

#ifdef VIRTIO_REL_INFO_DUMP
        virtioCoreTraceEvent(pVirtio, pVirtq, VIRTIO_CORE_EVENT_USED_PUT, pVirtq->uUsedIdxShadow, pVirtqBuf->uHeadIdx);
#endif /* VIRTIO_REL_INFO_DUMP */
        virtioWriteUsedElem(pDevIns, pVirtio, pVirtq, pVirtq->uUsedIdxShadow++, pVirtqBuf->uHeadIdx, (uint32_t)cbEnqueue);

#ifdef LOG_ENABLED
        if (LogIs6Enabled())
        {
            uint16_t uPending = virtioCoreR3CountPendingBufs(
                                    virtioReadUsedRingIdx(pDevIns, pVirtio, pVirtq),
                                    pVirtq->uUsedIdxShadow, pVirtq->uQueueSize);

            LogFunc(("    %u used buf%s not synced in %s\n",
                    uPending, uPending == 1 ? "" : "s ", VIRTQNAME(pVirtio, uVirtq)));
        }
#endif
    } /* fEnqueue */

    return VINF_SUCCESS;
}


#endif /* IN_RING3 */

/** API function: See Header file  */
DECLHIDDEN(int) virtioCoreVirtqUsedRingSync(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    if (!pVirtio->fLegacyDriver)
        AssertMsgReturn((pVirtio->fDeviceStatus & VIRTIO_STATUS_DRIVER_OK) && pVirtq->uEnable,
            ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    Log6Func(("    Sync %s used ring (%u -> idx)\n",
                        pVirtq->szName, pVirtq->uUsedIdxShadow));

#ifdef IN_RING3
    // I believe virtioCoreVirtqUsedRingSync is only called from ring 3 in virtio-net
#ifdef VIRTIO_REL_INFO_DUMP
    virtioCoreTraceEvent(pVirtio, pVirtq, VIRTIO_CORE_EVENT_USED_SYNC, pVirtq->uUsedIdxShadow, 0);
#endif /* VIRTIO_REL_INFO_DUMP */
#endif /* IN_RING3 */
    virtioWriteUsedRingIdx(pDevIns, pVirtio, pVirtq, pVirtq->uUsedIdxShadow);
    virtioCoreNotifyGuestDriver(pDevIns, pVirtio, uVirtq);

    return VINF_SUCCESS;
}

/**
 * This is called from the MMIO callback code when the guest does an MMIO access to the
 * mapped queue notification capability area corresponding to a particular queue, to notify
 * the queue handler of available data in the avail ring of the queue (VirtIO 1.0, 4.1.4.4.1)
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtq      Virtq to check for guest interrupt handling preference
 * @param   uNotifyIdx  Notification index
 */
static void virtioCoreVirtqNotified(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq, uint16_t uNotifyIdx)
{
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);

    /* VirtIO 1.0, section 4.1.5.2 implies uVirtq and uNotifyIdx should match. Disregarding any of
     * these notifications (if those indicies disagree) may break device/driver synchronization,
     * causing eternal throughput starvation, yet there's no specified way to disambiguate
     * which queue to wake-up in any awkward situation where the two parameters differ.
     */
    AssertMsg(uNotifyIdx == uVirtq,
              ("Guest kicked virtq %d's notify addr w/non-corresponding virtq idx %d\n",
              uVirtq, uNotifyIdx));
    RT_NOREF(uNotifyIdx);

    AssertReturnVoid(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    Log6Func(("%s: (desc chains: %u)\n", *pVirtq->szName ? pVirtq->szName : "?UNAMED QUEUE?",
        virtioCoreVirtqAvailCnt(pDevIns, pVirtio, pVirtq)));

    /* Inform client */
    pVirtioCC->pfnVirtqNotified(pDevIns, pVirtio, uVirtq);
    RT_NOREF2(pVirtio, pVirtq);
}

/**
 * Trigger MSI-X or INT# interrupt to notify guest of data added to used ring of
 * the specified virtq, depending on the interrupt configuration of the device
 * and depending on negotiated and realtime constraints flagged by the guest driver.
 *
 * See VirtIO 1.0 specification (section 2.4.7).
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtq      Virtq to check for guest interrupt handling preference
 */
static void virtioCoreNotifyGuestDriver(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    if (!IS_DRIVER_OK(pVirtio))
    {
        LogFunc(("Guest driver not in ready state.\n"));
        return;
    }

    if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
    {
        if (pVirtq->fUsedRingEvent)
        {
#ifdef IN_RING3
            Log6Func(("...kicking guest %s, VIRTIO_F_EVENT_IDX set and threshold (%d) reached\n",
                   pVirtq->szName, (uint16_t)virtioReadAvailUsedEvent(pDevIns, pVirtio, pVirtq)));
#endif
            virtioNudgeGuest(pDevIns, pVirtio, VIRTIO_ISR_VIRTQ_INTERRUPT, pVirtq->uMsixVector);
            pVirtq->fUsedRingEvent = false;
            return;
        }
#ifdef IN_RING3
        Log6Func(("...skip interrupt %s, VIRTIO_F_EVENT_IDX set but threshold (%d) not reached (%d)\n",
                   pVirtq->szName,(uint16_t)virtioReadAvailUsedEvent(pDevIns, pVirtio, pVirtq), pVirtq->uUsedIdxShadow));
#endif
    }
    else
    {
        /** If guest driver hasn't suppressed interrupts, interrupt  */
        if (!(virtioReadAvailRingFlags(pDevIns, pVirtio, pVirtq) & VIRTQ_AVAIL_F_NO_INTERRUPT))
        {
            virtioNudgeGuest(pDevIns, pVirtio, VIRTIO_ISR_VIRTQ_INTERRUPT, pVirtq->uMsixVector);
            return;
        }
        Log6Func(("...skipping interrupt for %s (guest set VIRTQ_AVAIL_F_NO_INTERRUPT)\n", pVirtq->szName));
    }
}

/**
 * Raise interrupt or MSI-X
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uCause      Interrupt cause bit mask to set in PCI ISR port.
 * @param   uVec        MSI-X vector, if enabled
 */
static int virtioNudgeGuest(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint8_t uCause, uint16_t uMsixVector)
{
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pVirtio->fRecovering))
        LogRel(("[%s] Raising%s interrupt, because of %s\n", pVirtio->szInstance, pVirtio->fMsiSupport ? " MSI" : "",
                uCause == VIRTIO_ISR_VIRTQ_INTERRUPT ? "added buffer" : "config change"));
#endif /* VIRTIO_REL_INFO_DUMP */
    if (uCause == VIRTIO_ISR_VIRTQ_INTERRUPT)
        Log6Func(("Reason for interrupt - buffer added to 'used' ring.\n"));
    else
    if (uCause == VIRTIO_ISR_DEVICE_CONFIG)
       Log6Func(("Reason for interrupt - device config change\n"));

    if (pVirtio->uIrqMmio)
    {
        pVirtio->uISR |= uCause;
        PDMDevHlpISASetIrq(pDevIns, pVirtio->uIrqMmio, PDM_IRQ_LEVEL_HIGH);
    }
    else if (!pVirtio->fMsiSupport)
    {
        pVirtio->uISR |= uCause;
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);
    }
    else if (uMsixVector != VIRTIO_MSI_NO_VECTOR)
        PDMDevHlpPCISetIrq(pDevIns, uMsixVector, 1);
    return VINF_SUCCESS;
}

/**
 * Lower interrupt (Called when guest reads ISR and when resetting)
 *
 * @param   pDevIns     The device instance.
 */
static void virtioLowerInterrupt(PPDMDEVINS pDevIns, uint16_t uMsixVector)
{
    PVIRTIOCORE pVirtio = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    if (pVirtio->uIrqMmio)
        PDMDevHlpISASetIrq(pDevIns, pVirtio->uIrqMmio, PDM_IRQ_LEVEL_LOW);
    else if (!pVirtio->fMsiSupport)
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
    else if (uMsixVector != VIRTIO_MSI_NO_VECTOR)
        PDMDevHlpPCISetIrq(pDevIns, pVirtio->uMsixConfig, PDM_IRQ_LEVEL_LOW);
}

#ifdef IN_RING3
static void virtioResetVirtq(PVIRTIOCORE pVirtio, uint16_t uVirtq)
{
    Assert(uVirtq < RT_ELEMENTS(pVirtio->aVirtqueues));
    PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

    pVirtq->uQueueSize       = VIRTQ_SIZE;
    pVirtq->uEnable          = false;
    pVirtq->uNotifyOffset    = uVirtq;
    pVirtq->fUsedRingEvent   = false;
    pVirtq->uAvailIdxShadow  = 0;
    pVirtq->uUsedIdxShadow   = 0;
    pVirtq->uMsixVector      = uVirtq + 2;

    if (!pVirtio->fMsiSupport) /* VirtIO 1.0, 4.1.4.3 and 4.1.5.1.2 */
        pVirtq->uMsixVector = VIRTIO_MSI_NO_VECTOR;

    virtioLowerInterrupt(pVirtio->pDevInsR3, pVirtq->uMsixVector);
}

static void virtioResetDevice(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio)
{
    LogFunc(("Resetting device VirtIO state\n"));
    pVirtio->fLegacyDriver          = pVirtio->fOfferLegacy;   /* Cleared if VIRTIO_F_VERSION_1 feature ack'd */
    pVirtio->fDriverFeaturesWritten = 0;    /* Features can be re-negotiated after reset! */
    pVirtio->uDeviceFeaturesSelect  = 0;
    pVirtio->uDriverFeaturesSelect  = 0;
    pVirtio->uConfigGeneration      = 0;
    pVirtio->fDeviceStatus          = 0;
    pVirtio->uISR                   = 0;

    if (!pVirtio->fMsiSupport)
        virtioLowerInterrupt(pDevIns, 0);
    else
    {
        virtioLowerInterrupt(pDevIns, pVirtio->uMsixConfig);
        for (int i = 0; i < VIRTQ_MAX_COUNT; i++)
            virtioLowerInterrupt(pDevIns, pVirtio->aVirtqueues[i].uMsixVector);
    }

    if (!pVirtio->fMsiSupport)  /* VirtIO 1.0, 4.1.4.3 and 4.1.5.1.2 */
        pVirtio->uMsixConfig = VIRTIO_MSI_NO_VECTOR;

    for (uint16_t uVirtq = 0; uVirtq < VIRTQ_MAX_COUNT; uVirtq++)
        virtioResetVirtq(pVirtio, uVirtq);
}

/**
 * Invoked by this implementation when guest driver resets the device.
 * The driver itself will not until the device has read the status change.
 */
static void virtioGuestR3WasReset(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC)
{
    Log(("%-23s: Guest reset the device\n", __FUNCTION__));

    /* Let the client know */
    pVirtioCC->pfnStatusChanged(pVirtio, pVirtioCC, 0 /* fDriverOk */);
    virtioResetDevice(pDevIns, pVirtio);
}

DECLHIDDEN(void) virtioCoreR3ResetDevice(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC)
{
    virtioGuestR3WasReset(pDevIns, pVirtio, pVirtioCC);
}
#endif /* IN_RING3 */

/*
 * Determines whether guest virtio driver is modern or legacy and does callback
 * informing device-specific code that feature negotiation is complete.
 * Should be called only once (coordinated via the 'toggle' flag)
 */
#ifdef IN_RING3
DECLINLINE(void) virtioR3DoFeaturesCompleteOnceOnly(PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC)
{
        if (pVirtio->uDriverFeatures & VIRTIO_F_VERSION_1)
        {
            LogFunc(("VIRTIO_F_VERSION_1 feature ack'd by guest\n"));
            pVirtio->fLegacyDriver = 0;
        }
        else
        {
            if (pVirtio->fOfferLegacy)
            {
                pVirtio->fLegacyDriver = 1;
                LogFunc(("VIRTIO_F_VERSION_1 feature was NOT set by guest\n"));
            }
            else
                AssertMsgFailed(("Guest didn't accept VIRTIO_F_VERSION_1, but fLegacyOffered flag not set.\n"));
        }
        if (pVirtioCC->pfnFeatureNegotiationComplete)
            pVirtioCC->pfnFeatureNegotiationComplete(pVirtio, pVirtio->uDriverFeatures, pVirtio->fLegacyDriver);
        pVirtio->fDriverFeaturesWritten |= DRIVER_FEATURES_COMPLETE_HANDLED;
}
#endif


/**
 * Handles a write to the device status register from the driver.
 *
 * @returns VBox status code
 *
 * @param   pDevIns          The device instance.
 * @param   pVirtio          Pointer to the shared virtio state.
 * @param   pVirtioCC        Pointer to the current context virtio state.
 * @param   fDeviceStatus    The device status to be written.
 */
DECLINLINE(int) virtioDeviceStatusWrite(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC,
                                        uint8_t fDeviceStatus)
{
    pVirtio->fDeviceStatus = fDeviceStatus;
    bool fDeviceReset = pVirtio->fDeviceStatus == 0;
#ifdef LOG_ENABLED
    if (LogIs7Enabled())
    {
        char szOut[80] = { 0 };
        virtioCoreFormatDeviceStatus(pVirtio->fDeviceStatus, szOut, sizeof(szOut));
        Log(("%-23s: Guest wrote fDeviceStatus ................ (%s)\n", __FUNCTION__, szOut));
    }
#endif
    bool const fStatusChanged = IS_DRIVER_OK(pVirtio) != WAS_DRIVER_OK(pVirtio);

    if (fDeviceReset || fStatusChanged)
    {
#ifdef IN_RING0
        /* Since VirtIO status changes are cumbersome by nature, e.g. not a benchmark priority,
         * handle the rest in R3 to facilitate logging or whatever dev-specific client needs to do */
        Log6(("%-23s: RING0 => RING3 (demote)\n", __FUNCTION__));
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }

#ifdef IN_RING3
    /*
     * Notify client only if status actually changed from last time and when we're reset.
     */
    if (fDeviceReset)
        virtioGuestR3WasReset(pDevIns, pVirtio, pVirtioCC);

    if (fStatusChanged)
        pVirtioCC->pfnStatusChanged(pVirtio, pVirtioCC, IS_DRIVER_OK(pVirtio));
#else
    RT_NOREF(pDevIns, pVirtioCC);
#endif
    /*
     * Save the current status for the next write so we can see what changed.
     */
    pVirtio->fPrevDeviceStatus = pVirtio->fDeviceStatus;
    return VINF_SUCCESS;
}


/**
 * Handles a read from the device status register from the driver.
 *
 * @returns The device status register value.
 *
 * @param   pVirtio          Pointer to the shared virtio state.
 */
DECLINLINE(uint8_t) virtioDeviceStatusRead(PVIRTIOCORE pVirtio)
{
#ifdef LOG_ENABLED
    if (LogIs7Enabled())
    {
        char szOut[80] = { 0 };
        virtioCoreFormatDeviceStatus(pVirtio->fDeviceStatus, szOut, sizeof(szOut));
        LogFunc(("Guest read  fDeviceStatus ................ (%s)\n", szOut));
    }
#endif
    return pVirtio->fDeviceStatus;
}


/**
 * Handle accesses to Common Configuration capability
 *
 * @returns VBox status code
 *
 * @param   pDevIns          The device instance.
 * @param   pVirtio          Pointer to the shared virtio state.
 * @param   pVirtioCC        Pointer to the current context virtio state.
 * @param   fWrite           Set if write access, clear if read access.
 * @param   uOffsetOfAccess  The common configuration capability offset.
 * @param   cb               Number of bytes to read or write
 * @param   pv               Pointer to location to write to or read from
 */
static int virtioCommonCfgAccessed(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC,
                                   int fWrite, uint32_t uOffsetOfAccess, unsigned cb, void *pv)
{
    uint16_t uVirtq = pVirtio->uVirtqSelect;
    int rc = VINF_SUCCESS;
    uint64_t val;
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pVirtio->fRecovering))
    {
        if (fWrite)
            LogRel(("[%s] writes %u bytes @ cmn cfg + %u: %.*Rhxs\n", pVirtio->szInstance, cb, uOffsetOfAccess, cb, pv));
        else
            LogRel(("[%s] attempts to read %u bytes @ cmn cfg + %u\n", pVirtio->szInstance, cb, uOffsetOfAccess));
    }
#endif /* VIRTIO_REL_INFO_DUMP */
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uDeviceFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
    {
        if (fWrite) /* Guest WRITE pCommonCfg>uDeviceFeatures */
        {
            /* VirtIO 1.0, 4.1.4.3 states device_feature is a (guest) driver readonly field,
             * yet the linux driver attempts to write/read it back twice */
            VIRTIO_DEV_CONFIG_LOG_ACCESS(uDeviceFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess);
            LogFunc(("... WARNING: Guest attempted to write readonly virtio_pci_common_cfg.device_feature (ignoring)\n"));
            return VINF_IOM_MMIO_UNUSED_00;
        }
        else /* Guest READ pCommonCfg->uDeviceFeatures */
        {
            switch (pVirtio->uDeviceFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDeviceFeatures & UINT32_C(0xffffffff);
                    memcpy(pv, &val, cb);
                    VIRTIO_DEV_CONFIG_LOG_ACCESS(uDeviceFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess);
                    break;
                case 1:
                    val = pVirtio->uDeviceFeatures >> 32;
                    memcpy(pv, &val, cb);
                    VIRTIO_DEV_CONFIG_LOG_ACCESS(uDeviceFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess + sizeof(uint32_t));
                    break;
                default:
                    LogFunc(("Guest read uDeviceFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDeviceFeaturesSelect));
                    return VINF_IOM_MMIO_UNUSED_00;
            }
        }
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uDriverFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->udriverFeatures */
        {
            switch (pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    memcpy(&pVirtio->uDriverFeatures, pv, cb);
                    pVirtio->fDriverFeaturesWritten |= DRIVER_FEATURES_0_WRITTEN;
            LogFunc(("Set DRIVER_FEATURES_0_WRITTEN. pVirtio->fDriverFeaturesWritten=%d\n", pVirtio->fDriverFeaturesWritten));
                    if (     (pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_0_AND_1_WRITTEN) == DRIVER_FEATURES_0_AND_1_WRITTEN
                        && !(pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_COMPLETE_HANDLED))
#ifdef IN_RING0
                        return VINF_IOM_R3_MMIO_WRITE;
#endif
#ifdef IN_RING3
                        virtioR3DoFeaturesCompleteOnceOnly(pVirtio, pVirtioCC);
#endif
                    VIRTIO_DEV_CONFIG_LOG_ACCESS(uDriverFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess);
                    break;
                case 1:
                    memcpy((char *)&pVirtio->uDriverFeatures + sizeof(uint32_t), pv, cb);
                    pVirtio->fDriverFeaturesWritten |= DRIVER_FEATURES_1_WRITTEN;
            LogFunc(("Set DRIVER_FEATURES_1_WRITTEN. pVirtio->fDriverFeaturesWritten=%d\n", pVirtio->fDriverFeaturesWritten));
                    if (     (pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_0_AND_1_WRITTEN) == DRIVER_FEATURES_0_AND_1_WRITTEN
                        && !(pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_COMPLETE_HANDLED))
#ifdef IN_RING0
                        return VINF_IOM_R3_MMIO_WRITE;
#endif
#ifdef IN_RING3
                        virtioR3DoFeaturesCompleteOnceOnly(pVirtio, pVirtioCC);
#endif
                    VIRTIO_DEV_CONFIG_LOG_ACCESS(uDriverFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess + sizeof(uint32_t));
                    break;
                default:
                    LogFunc(("Guest wrote uDriverFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDriverFeaturesSelect));
                    return VINF_SUCCESS;
            }
        }
        else /* Guest READ pCommonCfg->udriverFeatures */
        {
            switch (pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDriverFeatures & 0xffffffff;
                    memcpy(pv, &val, cb);
                    VIRTIO_DEV_CONFIG_LOG_ACCESS(uDriverFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess);
                    break;
                case 1:
                    val = (pVirtio->uDriverFeatures >> 32) & 0xffffffff;
                    memcpy(pv, &val, cb);
                    VIRTIO_DEV_CONFIG_LOG_ACCESS(uDriverFeatures, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess + 4);
                    break;
                default:
                    LogFunc(("Guest read uDriverFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDriverFeaturesSelect));
                    return VINF_IOM_MMIO_UNUSED_00;
            }
        }
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uNumVirtqs, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
    {
        if (fWrite)
        {
            Log2Func(("Guest attempted to write readonly virtio_pci_common_cfg.num_queues\n"));
            return VINF_SUCCESS;
        }
        *(uint16_t *)pv = VIRTQ_MAX_COUNT;
        VIRTIO_DEV_CONFIG_LOG_ACCESS(uNumVirtqs, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess);
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(fDeviceStatus, VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->fDeviceStatus */
            rc = virtioDeviceStatusWrite(pDevIns, pVirtio, pVirtioCC, *(uint8_t *)pv);
        else /* Guest READ pCommonCfg->fDeviceStatus */
            *(uint8_t *)pv = virtioDeviceStatusRead(pVirtio);
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uMsixConfig,                VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS(         uMsixConfig,                VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uDeviceFeaturesSelect,      VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS(         uDeviceFeaturesSelect,      VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uDriverFeaturesSelect,      VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS(         uDriverFeaturesSelect,      VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uConfigGeneration,          VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS(         uConfigGeneration,          VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uVirtqSelect,               VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
    {
        if (fWrite) {
            uint16_t uVirtqNew = *(uint16_t *)pv;

            if (uVirtqNew < RT_ELEMENTS(pVirtio->aVirtqueues))
                VIRTIO_DEV_CONFIG_ACCESS( uVirtqSelect,               VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio);
            else
                LogFunc(("... WARNING: Guest attempted to write invalid virtq selector (ignoring)\n"));
        }
        else
            VIRTIO_DEV_CONFIG_ACCESS(     uVirtqSelect,               VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio);
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   GCPhysVirtqDesc,            VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( GCPhysVirtqDesc,   uVirtq,  VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio->aVirtqueues);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   GCPhysVirtqAvail,           VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( GCPhysVirtqAvail,  uVirtq,  VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio->aVirtqueues);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   GCPhysVirtqUsed,            VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( GCPhysVirtqUsed,   uVirtq,  VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio->aVirtqueues);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uQueueSize,                 VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( uQueueSize,        uVirtq,  VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio->aVirtqueues);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uEnable,                    VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( uEnable,           uVirtq,  VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio->aVirtqueues);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uNotifyOffset,              VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( uNotifyOffset,     uVirtq,  VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio->aVirtqueues);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uMsixVector,                VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( uMsixVector,       uVirtq,  VIRTIO_PCI_COMMON_CFG_T, uOffsetOfAccess, pVirtio->aVirtqueues);
    else
    {
        Log2Func(("Bad guest %s access to virtio_pci_common_cfg: uOffsetOfAccess=%#x (%d), cb=%d\n",
                  fWrite ? "write" : "read ", uOffsetOfAccess, uOffsetOfAccess, cb));
        return fWrite ? VINF_SUCCESS : VINF_IOM_MMIO_UNUSED_00;
    }
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pVirtio->fRecovering) && !fWrite)
        LogRel(("[%s] read %u bytes @ cmn cfg + %u: %.*Rhxs\n", pVirtio->szInstance, cb, uOffsetOfAccess, cb, pv));
#endif /* VIRTIO_REL_INFO_DUMP */

#ifndef IN_RING3
    RT_NOREF(pDevIns, pVirtioCC);
#endif
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN)
 *
 * This I/O handler exists only to handle access from legacy drivers.
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioLegacyIOPortIn(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PVIRTIOCORE   pVirtio = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    STAM_PROFILE_ADV_START(&pVirtio->CTX_SUFF(StatRead), a);

    RT_NOREF(pvUser);
    Log(("%-23s: Port read at offset=%RTiop, cb=%#x%s",
        __FUNCTION__, offPort, cb,
        VIRTIO_DEV_CONFIG_MATCH_MEMBER(fIsrStatus, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort) ? "" : "\n"));

    void *pv = pu32; /* To use existing macros */
    int fWrite = 0;  /* To use existing macros */

    uint16_t uVirtq = pVirtio->uVirtqSelect;

    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uDeviceFeatures, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        uint32_t val = pVirtio->uDeviceFeatures & UINT32_C(0xffffffff);
        memcpy(pu32, &val, cb);
        VIRTIO_DEV_CONFIG_LOG_ACCESS(uDeviceFeatures,   VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort);
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uDriverFeatures, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        uint32_t val = pVirtio->uDriverFeatures &  UINT32_C(0xffffffff);
        memcpy(pu32, &val, cb);
        VIRTIO_DEV_CONFIG_LOG_ACCESS(uDriverFeatures, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort);
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(fDeviceStatus, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        *(uint8_t *)pu32 = pVirtio->fDeviceStatus;
#ifdef LOG_ENABLED
        if (LogIs7Enabled())
        {
            char szOut[80] = { 0 };
            virtioCoreFormatDeviceStatus(pVirtio->fDeviceStatus, szOut, sizeof(szOut));
            Log(("%-23s: Guest read  fDeviceStatus ................ (%s)\n", __FUNCTION__, szOut));
        }
#endif
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(fIsrStatus, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        ASSERT_GUEST_MSG(cb == 1, ("%d\n", cb));
        *(uint8_t *)pu32 = pVirtio->uISR;
        pVirtio->uISR = 0;
        virtioLowerInterrupt( pDevIns,  0);
        Log((" (ISR read and cleared)\n"));
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uVirtqSelect,               VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
        VIRTIO_DEV_CONFIG_ACCESS(         uVirtqSelect,               VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uVirtqPfn,                  VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[uVirtq];
        *pu32 = pVirtQueue->GCPhysVirtqDesc >> GUEST_PAGE_SHIFT;
        Log(("%-23s: Guest read  uVirtqPfn .................... %#x\n",   __FUNCTION__, *pu32));
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uQueueSize,                 VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( uQueueSize,        uVirtq,  VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio->aVirtqueues);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uQueueNotify,               VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
        VIRTIO_DEV_CONFIG_ACCESS(         uQueueNotify,               VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio);
#ifdef LEGACY_MSIX_SUPPORTED
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uMsixConfig,                VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
        VIRTIO_DEV_CONFIG_ACCESS(         uMsixConfig,                VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uMsixVector,                VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( uMsixVector,       uVirtq,  VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio->aVirtqueues);
#endif
    else if (offPort >= sizeof(VIRTIO_LEGACY_PCI_COMMON_CFG_T))
    {
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatRead), a);
#ifdef IN_RING3
        /* Access device-specific configuration */
        PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
        int rc = pVirtioCC->pfnDevCapRead(pDevIns, offPort - sizeof(VIRTIO_LEGACY_PCI_COMMON_CFG_T), pv, cb);
        return rc;
#else
        return VINF_IOM_R3_IOPORT_READ;
#endif
    }
    else
    {
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatRead), a);
        Log2Func(("Bad guest read access to virtio_legacy_pci_common_cfg: offset=%#x, cb=%x\n",
                   offPort, cb));
        int rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                    "virtioLegacyIOPortIn: no valid port at offset offset=%RTiop cb=%#x\n", offPort, cb);
        return rc;
    }
    STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatRead), a);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{ * @callback_method_impl{FNIOMIOPORTNEWOUT}
 *
 * This I/O Port interface exists only to handle access from legacy drivers.
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioLegacyIOPortOut(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVIRTIOCORE pVirtio = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    STAM_PROFILE_ADV_START(&pVirtio->CTX_SUFF(StatWrite), a);
    RT_NOREF(pvUser);

    uint16_t uVirtq = pVirtio->uVirtqSelect;
    uint32_t u32OnStack = u32;  /* allows us to use this impl's MMIO parsing macros */
    void *pv = &u32OnStack; /* To use existing macros */
    int fWrite = 1;         /* To use existing macros */

    Log(("%-23s: Port written at offset=%RTiop, cb=%#x, u32=%#x\n",  __FUNCTION__, offPort, cb, u32));

    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uVirtqSelect,        VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        if (u32 < RT_ELEMENTS(pVirtio->aVirtqueues))
            VIRTIO_DEV_CONFIG_ACCESS(     uVirtqSelect,        VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio);
        else
            LogFunc(("... WARNING: Guest attempted to write invalid virtq selector (ignoring)\n"));
    }
    else
#ifdef LEGACY_MSIX_SUPPORTED
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uMsixConfig,         VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
        VIRTIO_DEV_CONFIG_ACCESS(         uMsixConfig,         VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio);
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(   uMsixVector,         VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
        VIRTIO_DEV_CONFIG_ACCESS_INDEXED( uMsixVector, uVirtq, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort, pVirtio->aVirtqueues);
    else
#endif
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uDeviceFeatures,        VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        /* Check to see if guest acknowledged unsupported features */
        VIRTIO_DEV_CONFIG_LOG_ACCESS(uDeviceFeatures,          VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort);
        LogFunc(("... WARNING: Guest attempted to write readonly virtio_pci_common_cfg.device_feature (ignoring)\n"));
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
        return VINF_SUCCESS;
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uDriverFeatures,        VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        memcpy(&pVirtio->uDriverFeatures, pv, cb);
        if ((pVirtio->uDriverFeatures & ~VIRTIO_DEV_INDEPENDENT_LEGACY_FEATURES_OFFERED) == 0)
        {
                Log(("Guest asked for features host does not support! (host=%x guest=%x)\n",
                      VIRTIO_DEV_INDEPENDENT_LEGACY_FEATURES_OFFERED, pVirtio->uDriverFeatures));
                pVirtio->uDriverFeatures &= VIRTIO_DEV_INDEPENDENT_LEGACY_FEATURES_OFFERED;
        }
        if (!(pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_COMPLETE_HANDLED))
        {
#ifdef IN_RING0
            Log6(("%-23s: RING0 => RING3 (demote)\n", __FUNCTION__));
            return VINF_IOM_R3_IOPORT_WRITE;
#endif
#ifdef IN_RING3
            PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
            virtioR3DoFeaturesCompleteOnceOnly(pVirtio, pVirtioCC);
#endif
        }
        VIRTIO_DEV_CONFIG_LOG_ACCESS(uDriverFeatures,          VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort);
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uQueueSize,             VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        VIRTIO_DEV_CONFIG_LOG_ACCESS(uQueueSize,               VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort);
        LogFunc(("... WARNING: Guest attempted to write readonly device_feature (queue size) (ignoring)\n"));
        return VINF_SUCCESS;
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(fDeviceStatus,          VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        bool const fDriverInitiatedReset = (pVirtio->fDeviceStatus = (uint8_t)u32) == 0;
        bool const fDriverStateImproved = IS_DRIVER_OK(pVirtio) && !WAS_DRIVER_OK(pVirtio);
#ifdef LOG_ENABLED
        if (LogIs7Enabled())
        {
            char szOut[80] = { 0 };
            virtioCoreFormatDeviceStatus(pVirtio->fDeviceStatus, szOut, sizeof(szOut));
            Log(("%-23s: Guest wrote fDeviceStatus ................ (%s)\n", __FUNCTION__, szOut));
        }
#endif
        if (fDriverStateImproved  || fDriverInitiatedReset)
        {
#ifdef IN_RING0
            Log6(("%-23s: RING0 => RING3 (demote)\n", __FUNCTION__));
            STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
            return VINF_IOM_R3_IOPORT_WRITE;
#endif
        }

#ifdef IN_RING3
        PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
        if (fDriverInitiatedReset)
            virtioGuestR3WasReset(pDevIns, pVirtio, pVirtioCC);

        else if (fDriverStateImproved)
            pVirtioCC->pfnStatusChanged(pVirtio, pVirtioCC, 1 /* fDriverOk */);

#endif
        pVirtio->fPrevDeviceStatus = pVirtio->fDeviceStatus;
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uVirtqPfn, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];
        uint64_t uVirtqPfn = (uint64_t)u32;

        if (uVirtqPfn)
        {
            /* Transitional devices calculate ring physical addresses using rigid spec-defined formulae,
             * instead of guest conveying respective address of each ring, as "modern" VirtIO drivers do,
             * thus there is no virtq PFN or single base queue address stored in instance data for
             * this transitional device, but rather it is derived, when read back, from GCPhysVirtqDesc */

            pVirtq->GCPhysVirtqDesc  = uVirtqPfn * VIRTIO_PAGE_SIZE;
            pVirtq->GCPhysVirtqAvail = pVirtq->GCPhysVirtqDesc + sizeof(VIRTQ_DESC_T) * pVirtq->uQueueSize;
            pVirtq->GCPhysVirtqUsed  =
                RT_ALIGN(pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[pVirtq->uQueueSize]), VIRTIO_PAGE_SIZE);
        }
        else
        {
            /* Don't set ring addresses for queue (to meaningless values), when guest resets the virtq's PFN */
            pVirtq->GCPhysVirtqDesc  = 0;
            pVirtq->GCPhysVirtqAvail = 0;
            pVirtq->GCPhysVirtqUsed  = 0;
        }
        Log(("%-23s: Guest wrote uVirtqPfn .................... %#x:\n"
             "%68s... %p -> GCPhysVirtqDesc\n%68s... %p -> GCPhysVirtqAvail\n%68s... %p -> GCPhysVirtqUsed\n",
                __FUNCTION__, u32, " ", pVirtq->GCPhysVirtqDesc, " ", pVirtq->GCPhysVirtqAvail, " ", pVirtq->GCPhysVirtqUsed));
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(uQueueNotify, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
#ifdef IN_RING3
        ASSERT_GUEST_MSG(cb == 2, ("cb=%u\n", cb));
        pVirtio->uQueueNotify =  u32 & 0xFFFF;
        if (uVirtq < VIRTQ_MAX_COUNT)
        {
            RT_UNTRUSTED_VALIDATED_FENCE();

            /* Need to check that queue is configured. Legacy spec didn't have a queue enabled flag */
            if (pVirtio->aVirtqueues[pVirtio->uQueueNotify].GCPhysVirtqDesc)
                    virtioCoreVirtqNotified(pDevIns, pVirtio, pVirtio->uQueueNotify, pVirtio->uQueueNotify /* uNotifyIdx */);
            else
                Log(("The queue (#%d) being notified has not been initialized.\n", pVirtio->uQueueNotify));
        }
        else
            Log(("Invalid queue number (%d)\n", pVirtio->uQueueNotify));
#else
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
        return VINF_IOM_R3_IOPORT_WRITE;
#endif
    }
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(fIsrStatus, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort))
    {
        VIRTIO_DEV_CONFIG_LOG_ACCESS( fIsrStatus, VIRTIO_LEGACY_PCI_COMMON_CFG_T, offPort);
        LogFunc(("... WARNING: Guest attempted to write readonly device_feature (ISR status) (ignoring)\n"));
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
        return VINF_SUCCESS;
    }
    else if (offPort >= sizeof(VIRTIO_LEGACY_PCI_COMMON_CFG_T))
    {
#ifdef IN_RING3

        /* Access device-specific configuration */
        PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
        return pVirtioCC->pfnDevCapWrite(pDevIns, offPort - sizeof(VIRTIO_LEGACY_PCI_COMMON_CFG_T), pv, cb);
#else
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
        return VINF_IOM_R3_IOPORT_WRITE;
#endif
    }
    else
    {
        Log2Func(("Bad guest write access to virtio_legacy_pci_common_cfg: offset=%#x, cb=0x%x\n",
                  offPort, cb));
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
        int rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                    "virtioLegacyIOPortOut: no valid port at offset offset=%RTiop cb=0x%#x\n", offPort, cb);
        return rc;
    }

    RT_NOREF(uVirtq);
    STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
    return VINF_SUCCESS;
}


/**
 * Read from the device specific configuration at the given offset.
 *
 * @returns VBox status code.
 * @param   pDevIns
 */
DECLINLINE(VBOXSTRICTRC) virtioDeviceCfgRead(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC,
                                              uint32_t offDevCfg, void *pv, unsigned cb)
{
#ifdef IN_RING3
        /*
         * Callback to client to manage device-specific configuration.
         */
        VBOXSTRICTRC rcStrict = pVirtioCC->pfnDevCapRead(pDevIns, offDevCfg, pv, cb);

        /*
         * Anytime any part of the dev-specific dev config (which this virtio core implementation sees
         * as a blob, and virtio dev-specific code separates into fields) is READ, it must be compared
         * for deltas from previous read to maintain a config gen. seq. counter (VirtIO 1.0, section 4.1.4.3.1)
         */
        bool fDevSpecificFieldChanged = RT_BOOL(memcmp(pVirtioCC->pbDevSpecificCfg + offDevCfg,
                                                 pVirtioCC->pbPrevDevSpecificCfg + offDevCfg,
                                                 RT_MIN(cb, pVirtioCC->cbDevSpecificCfg - offDevCfg)));

        memcpy(pVirtioCC->pbPrevDevSpecificCfg, pVirtioCC->pbDevSpecificCfg, pVirtioCC->cbDevSpecificCfg);

        if (pVirtio->fGenUpdatePending || fDevSpecificFieldChanged)
        {
            ++pVirtio->uConfigGeneration;
            Log6Func(("Bumped cfg. generation to %d because %s%s\n", pVirtio->uConfigGeneration,
                      fDevSpecificFieldChanged ? "<dev cfg changed> " : "",
                      pVirtio->fGenUpdatePending ? "<update was pending>" : ""));
            pVirtio->fGenUpdatePending = false;
        }

        virtioLowerInterrupt(pDevIns, 0);
        return rcStrict;
#else
        RT_NOREF(pDevIns, pVirtio, pVirtioCC, offDevCfg, pv, cb);
        return VINF_IOM_R3_MMIO_READ;
#endif
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Memory mapped I/O Handler for PCI Capabilities read operations.}
 *
 * This MMIO handler specifically supports the VIRTIO_PCI_CAP_PCI_CFG capability defined
 * in the VirtIO 1.0 specification, section 4.1.4.7, and as such is restricted to reads
 * of 1, 2 or 4 bytes, only.
 *
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    AssertReturn(cb == 1 || cb == 2 || cb == 4, VINF_IOM_MMIO_UNUSED_FF);
    Assert(pVirtio == (PVIRTIOCORE)pvUser); RT_NOREF(pvUser);

    STAM_PROFILE_ADV_START(&pVirtio->CTX_SUFF(StatRead), a);

    VBOXSTRICTRC rcStrict;
    uint32_t uOffset;
    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, uOffset, pVirtio->LocDeviceCap))
        rcStrict = virtioDeviceCfgRead(pDevIns, pVirtio, pVirtioCC, uOffset, pv, cb);
    else if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, uOffset, pVirtio->LocCommonCfgCap))
        rcStrict = virtioCommonCfgAccessed(pDevIns, pVirtio, pVirtioCC, false /* fWrite */, uOffset, cb, pv);
    else if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, uOffset, pVirtio->LocIsrCap))
    {
        *(uint8_t *)pv = pVirtio->uISR;
        Log6Func(("Read and clear ISR\n"));
        pVirtio->uISR = 0; /* VirtIO spec requires reads of ISR to clear it */
        virtioLowerInterrupt(pDevIns, 0);
        rcStrict = VINF_SUCCESS;
    }
    else
    {
        ASSERT_GUEST_MSG_FAILED(("Bad read access to mapped capabilities region: off=%RGp cb=%u\n", off, cb));
        memset(pv, 0xFF, cb);
        rcStrict = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                     "virtioMmioRead: Bad MMIO access to capabilities, offset=%RTiop cb=%08x\n", off, cb);
    }

    STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatRead), a);
    return rcStrict;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Memory mapped I/O Handler for PCI Capabilities write operations.}
 *
 * This MMIO handler specifically supports the VIRTIO_PCI_CAP_PCI_CFG capability defined
 * in the VirtIO 1.0 specification, section 4.1.4.7, and as such is restricted to writes
 * of 1, 2 or 4 bytes, only.
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    AssertReturn(cb == 1 || cb == 2 || cb == 4, VERR_INVALID_PARAMETER);
    Assert(pVirtio == (PVIRTIOCORE)pvUser); RT_NOREF(pvUser);
    STAM_PROFILE_ADV_START(&pVirtio->CTX_SUFF(StatWrite), a);

    VBOXSTRICTRC rcStrict;
    uint32_t uOffset;
    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, uOffset, pVirtio->LocDeviceCap))
    {
#ifdef IN_RING3
        /*
         * Foreward this MMIO write access for client to deal with.
         */
        rcStrict = pVirtioCC->pfnDevCapWrite(pDevIns, uOffset, pv, cb);
#else
        Log6(("%-23s: RING0 => RING3 (demote)\n", __FUNCTION__));
        rcStrict = VINF_IOM_R3_MMIO_WRITE;
#endif
    }
    else if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, uOffset, pVirtio->LocCommonCfgCap))
        rcStrict = virtioCommonCfgAccessed(pDevIns, pVirtio, pVirtioCC, true /* fWrite */, uOffset, cb, (void *)pv);
    else if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, uOffset, pVirtio->LocIsrCap) && cb == sizeof(uint8_t))
    {
        pVirtio->uISR = *(uint8_t *)pv;
        Log6Func(("Setting uISR = 0x%02x (virtq interrupt: %d, dev confg interrupt: %d)\n",
                  pVirtio->uISR & 0xff,
                  pVirtio->uISR & VIRTIO_ISR_VIRTQ_INTERRUPT,
                  RT_BOOL(pVirtio->uISR & VIRTIO_ISR_DEVICE_CONFIG)));
        rcStrict = VINF_SUCCESS;
    }
    else if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, uOffset, pVirtio->LocNotifyCap) && cb == sizeof(uint16_t))
    {
        /* This *should* be guest driver dropping index of a new descriptor in avail ring */
        virtioCoreVirtqNotified(pDevIns, pVirtio, uOffset / VIRTIO_NOTIFY_OFFSET_MULTIPLIER, *(uint16_t *)pv);
        rcStrict = VINF_SUCCESS;
    }
    else
    {
        ASSERT_GUEST_MSG_FAILED(("Bad write access to mapped capabilities region: off=%RGp pv=%#p{%.*Rhxs} cb=%u\n", off, pv, cb, pv, cb));
        rcStrict = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                     "virtioMmioRead: Bad MMIO access to capabilities, offset=%RTiop cb=%08x\n", off, cb);
    }

    STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Memory mapped I/O Handler for Virtio over MMIO read operations.}
 *
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioMmioTransportRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    RT_NOREF(pvUser);
    STAM_PROFILE_ADV_START(&pVirtio->CTX_SUFF(StatRead), a);

    if (off >= VIRTIO_MMIO_SIZE)
    {
        VBOXSTRICTRC rcStrict = virtioDeviceCfgRead(pDevIns, pVirtio, pVirtioCC, (uint32_t)off - VIRTIO_MMIO_SIZE, pv, cb);
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatRead), a);
        return rcStrict;
    }

    /* All accesses below need to be aligned on a 32-bit boundary and must be 32-bit in size. */
    ASSERT_GUEST_MSG_RETURN(!(off & 0x3) && cb == sizeof(uint32_t),
                            ("Bad read access: off=%RGp pv=%#p{%.*Rhxs} cb=%u\n", off, pv, cb, pv, cb),
                            VINF_IOM_MMIO_UNUSED_FF);

    int rc = VINF_SUCCESS;
    uint32_t *pu32 = (uint32_t *)pv;
    switch (off)
    {
        case VIRTIO_MMIO_REG_MAGIC_OFF:
            *pu32 = RT_H2LE_U32(VIRTIO_MMIO_REG_MAGIC_VALUE);
            break;
        case VIRTIO_MMIO_REG_VERSION_OFF:
            *pu32 = RT_H2LE_U32(VIRTIO_MMIO_REG_VERSION_VALUE);
            break;
        case VIRTIO_MMIO_REG_DEVICEID_OFF:
            *pu32 = pVirtio->uDeviceType;
            break;
        case VIRTIO_MMIO_REG_VENDORID_OFF:
            *pu32 = RT_H2LE_U32(DEVICE_PCI_VENDOR_ID_VIRTIO);
            break;
        case VIRTIO_MMIO_REG_DEVICEFEAT_OFF:
        {
            switch (pVirtio->uDeviceFeaturesSelect)
            {
                case 0:
                    *pu32 = pVirtio->uDeviceFeatures & UINT32_C(0xffffffff);
                    break;
                case 1:
                    *pu32 = pVirtio->uDeviceFeatures >> 32;
                    break;
                default:
                    LogFunc(("Guest read uDeviceFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDeviceFeaturesSelect));
                    rc = VINF_IOM_MMIO_UNUSED_00;
            }
            break;
        }
        case VIRTIO_MMIO_REG_QUEUENUMMAX_OFF:
            *pu32 = VIRTQ_SIZE; /** @todo */
            break;
        case VIRTIO_MMIO_REG_QUEUERDY_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            *pu32 = pVirtQueue->uEnable;
            break;
        }
        case VIRTIO_MMIO_REG_INTRSTATUS_OFF:
            *pu32 = pVirtio->uISR;
            break;
        case VIRTIO_MMIO_REG_DEVSTATUS_OFF:
            *pu32 = virtioDeviceStatusRead(pVirtio);
            break;
        case VIRTIO_MMIO_REG_CFGGEN_OFF:
            *pu32 = pVirtio->uConfigGeneration;
            break;
        default:
            ASSERT_GUEST_MSG_FAILED(("Bad read access to mapped capabilities region: off=%RGp cb=%u\n", off, cb));
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                        "virtioMmioTransportRead: Bad MMIO access to capabilities, offset=%RTiop cb=%08x\n", off, cb);
    }

    STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatRead), a);
    return rc;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Memory mapped I/O Handler for Virtio over MMIO write operations.}
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioMmioTransportWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    RT_NOREF(pvUser);
    STAM_PROFILE_ADV_START(&pVirtio->CTX_SUFF(StatWrite), a);

    if (off >= VIRTIO_MMIO_SIZE)
    {
#ifdef IN_RING3
        /*
         * Forward this MMIO write access for client to deal with.
         */
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
        return pVirtioCC->pfnDevCapWrite(pDevIns, (uint32_t)off - VIRTIO_MMIO_SIZE, pv, cb);
#else
        STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
        Log6(("%-23s: RING0 => RING3 (demote)\n", __FUNCTION__));
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }

    /* All accesses below need to be aligned on a 32-bit boundary and must be 32-bit in size. */
    ASSERT_GUEST_MSG_RETURN(!(off & 0x3) && cb == sizeof(uint32_t),
                            ("Bad write access: off=%RGp pv=%#p{%.*Rhxs} cb=%u\n", off, pv, cb, pv, cb),
                            VINF_SUCCESS);

    int rc = VINF_SUCCESS;
    uint32_t const u32Val = *(const uint32_t *)pv;
    switch (off)
    {
        case VIRTIO_MMIO_REG_DEVICEFEATSEL_OFF:
        {
            pVirtio->uDeviceFeaturesSelect = u32Val;
            break;
        }
        case VIRTIO_MMIO_REG_DRIVERFEAT_OFF:
        {
            switch (pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    pVirtio->uDriverFeatures = (pVirtio->uDriverFeatures & UINT64_C(0xffffffff00000000)) | u32Val;
                    pVirtio->fDriverFeaturesWritten |= DRIVER_FEATURES_0_WRITTEN;
                    LogFunc(("Set DRIVER_FEATURES_0_WRITTEN. pVirtio->fDriverFeaturesWritten=%d\n", pVirtio->fDriverFeaturesWritten));
                    if (     (pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_0_AND_1_WRITTEN) == DRIVER_FEATURES_0_AND_1_WRITTEN
                        && !(pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_COMPLETE_HANDLED))
#ifdef IN_RING0
                        return VINF_IOM_R3_MMIO_WRITE;
#endif
#ifdef IN_RING3
                        virtioR3DoFeaturesCompleteOnceOnly(pVirtio, pVirtioCC);
#endif
                    break;
                case 1:
                    pVirtio->uDriverFeatures = (pVirtio->uDriverFeatures & UINT64_C(0x00000000ffffffff)) | ((uint64_t)u32Val << 32);
                    pVirtio->fDriverFeaturesWritten |= DRIVER_FEATURES_1_WRITTEN;
                    LogFunc(("Set DRIVER_FEATURES_1_WRITTEN. pVirtio->fDriverFeaturesWritten=%d\n", pVirtio->fDriverFeaturesWritten));
                    if (     (pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_0_AND_1_WRITTEN) == DRIVER_FEATURES_0_AND_1_WRITTEN
                        && !(pVirtio->fDriverFeaturesWritten & DRIVER_FEATURES_COMPLETE_HANDLED))
#ifdef IN_RING0
                        return VINF_IOM_R3_MMIO_WRITE;
#endif
#ifdef IN_RING3
                        virtioR3DoFeaturesCompleteOnceOnly(pVirtio, pVirtioCC);
#endif
                    break;
                default:
                    LogFunc(("Guest wrote uDriverFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDriverFeaturesSelect));
                    return VINF_SUCCESS;
            }
            break;
        }
        case VIRTIO_MMIO_REG_DRIVERFEATSEL_OFF:
        {
            pVirtio->uDriverFeaturesSelect = u32Val;
            break;
        }
        case VIRTIO_MMIO_REG_QUEUESEL_OFF:
        {
            if (u32Val < RT_ELEMENTS(pVirtio->aVirtqueues))
                pVirtio->uVirtqSelect = (uint16_t)u32Val;
            else
                LogFunc(("... WARNING: Guest attempted to write invalid virtq selector (ignoring)\n"));
            break;
        }
        case VIRTIO_MMIO_REG_QUEUENUM_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->uQueueSize = (uint16_t)u32Val;
            break;
        }
        case VIRTIO_MMIO_REG_QUEUERDY_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->uEnable = (uint16_t)u32Val;
            break;
        }
        case VIRTIO_MMIO_REG_QUEUENOTIFY_OFF:
        {
            virtioCoreVirtqNotified(pDevIns, pVirtio, u32Val, (uint16_t)u32Val);
            break;
        }
        case VIRTIO_MMIO_REG_INTRACK_OFF:
        {
            pVirtio->uISR &= ~u32Val;
            if (!pVirtio->uISR)
                virtioLowerInterrupt(pDevIns,  0);
            break;
        }
        case VIRTIO_MMIO_REG_DEVSTATUS_OFF:
        {
            rc = virtioDeviceStatusWrite(pDevIns, pVirtio, pVirtioCC, (uint8_t)u32Val);
            break;
        }
        case VIRTIO_MMIO_REG_QUEUEALIGN_LEGACY_OFF:
        {
            /* Written by edk2 even though we don't offer legacy mode, ignore. */
            break;
        }
        case VIRTIO_MMIO_REG_QUEUEDESCLOW_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->GCPhysVirtqDesc = (pVirtQueue->GCPhysVirtqDesc & UINT64_C(0xffffffff00000000)) | u32Val;
            break;
        }
        case VIRTIO_MMIO_REG_QUEUEDESCHIGH_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->GCPhysVirtqDesc = (pVirtQueue->GCPhysVirtqDesc & UINT64_C(0x00000000ffffffff)) | ((uint64_t)u32Val << 32);
            break;
        }
        case VIRTIO_MMIO_REG_QUEUEDRVLOW_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->GCPhysVirtqAvail = (pVirtQueue->GCPhysVirtqAvail & UINT64_C(0xffffffff00000000)) | u32Val;
            break;
        }
        case VIRTIO_MMIO_REG_QUEUEDRVHIGH_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->GCPhysVirtqAvail = (pVirtQueue->GCPhysVirtqAvail & UINT64_C(0x00000000ffffffff)) | ((uint64_t)u32Val << 32);
            break;
        }
        case VIRTIO_MMIO_REG_QUEUEDEVLOW_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->GCPhysVirtqUsed = (pVirtQueue->GCPhysVirtqUsed & UINT64_C(0xffffffff00000000)) | u32Val;
            break;
        }
        case VIRTIO_MMIO_REG_QUEUEDEVHIGH_OFF:
        {
            Assert(pVirtio->uVirtqSelect < RT_ELEMENTS(pVirtio->aVirtqueues));
            PVIRTQUEUE pVirtQueue = &pVirtio->aVirtqueues[pVirtio->uVirtqSelect];
            pVirtQueue->GCPhysVirtqUsed = (pVirtQueue->GCPhysVirtqUsed & UINT64_C(0x00000000ffffffff)) | ((uint64_t)u32Val << 32);
            break;
        }
        default:
            ASSERT_GUEST_MSG_FAILED(("Bad write access to mapped capabilities region: off=%RGp pv=%#p{%.*Rhxs} cb=%u\n", off, pv, cb, pv, cb));
            rc = PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS,
                                  "virtioMmioTransportWrite: Bad MMIO access to capabilities, offset=%RTiop cb=%08x\n", off, cb);
    }

    STAM_PROFILE_ADV_STOP(&pVirtio->CTX_SUFF(StatWrite), a);
    return rc;
}


#ifdef IN_RING3

/**
 * @callback_method_impl{FNPCICONFIGREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioR3PciConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                        uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    RT_NOREF(pPciDev);

    if (uAddress == pVirtio->uPciCfgDataOff)
    {
     /* See comments in PCI Cfg capability initialization (in capabilities setup section of this code) */
        struct virtio_pci_cap *pPciCap = &pVirtioCC->pPciCfgCap->pciCap;
        uint32_t uLength = pPciCap->uLength;

        Log7Func((" pDevIns=%p pPciDev=%p uAddress=%#x%s cb=%u uLength=%d, bar=%d\n",
                     pDevIns, pPciDev, uAddress,  uAddress < 0x10 ? " " : "", cb, uLength, pPciCap->uBar));

        if (  (uLength != 1 && uLength != 2 && uLength != 4)
            || pPciCap->uBar != VIRTIO_REGION_PCI_CAP)
        {
            ASSERT_GUEST_MSG_FAILED(("Guest read virtio_pci_cfg_cap.pci_cfg_data using mismatching config. "
                                     "Ignoring\n"));
            *pu32Value = UINT32_MAX;
            return VINF_SUCCESS;
        }

        VBOXSTRICTRC rcStrict = virtioMmioRead(pDevIns, pVirtio, pPciCap->uOffset, pu32Value, cb);
        Log7Func((" Guest read virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%d, length=%d, result=0x%x -> %Rrc\n",
                   pPciCap->uBar, pPciCap->uOffset, uLength, *pu32Value, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }
    Log7Func((" pDevIns=%p pPciDev=%p uAddress=%#x%s cb=%u pu32Value=%p\n",
                 pDevIns, pPciDev, uAddress,  uAddress < 0x10 ? " " : "", cb, pu32Value));
    return VINF_PDM_PCI_DO_DEFAULT;
}

/**
 * @callback_method_impl{FNPCICONFIGWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioR3PciConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                         uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    RT_NOREF(pPciDev);

    Log7Func(("pDevIns=%p pPciDev=%p uAddress=%#x %scb=%u u32Value=%#x\n", pDevIns, pPciDev, uAddress, uAddress < 0xf ? " " : "", cb, u32Value));
    if (uAddress == pVirtio->uPciCfgDataOff)
    {
        /* See comments in PCI Cfg capability initialization (in capabilities setup section of this code) */
        struct virtio_pci_cap *pPciCap = &pVirtioCC->pPciCfgCap->pciCap;
        uint32_t uLength = pPciCap->uLength;

        if (   (uLength != 1 && uLength != 2 && uLength != 4)
            || cb != uLength
            ||  pPciCap->uBar != VIRTIO_REGION_PCI_CAP)
        {
            ASSERT_GUEST_MSG_FAILED(("Guest write virtio_pci_cfg_cap.pci_cfg_data using mismatching config. Ignoring\n"));
            return VINF_SUCCESS;
        }

        VBOXSTRICTRC rcStrict = virtioMmioWrite(pDevIns, pVirtio, pPciCap->uOffset, &u32Value, cb);
        Log2Func(("Guest wrote  virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%x, length=%x, value=%d -> %Rrc\n",
                   pPciCap->uBar, pPciCap->uOffset, uLength, u32Value, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }
    return VINF_PDM_PCI_DO_DEFAULT;
}


/*********************************************************************************************************************************
*   Saved state (SSM)                                                                                                            *
*********************************************************************************************************************************/


/**
 * Loads a saved device state (called from device-specific code on SSM final pass)
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pHlp        The ring-3 device helpers.
 * @param   pSSM        The saved state handle.
 * @returns VBox status code.
 */
DECLHIDDEN(int) virtioCoreR3LegacyDeviceLoadExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM,
                                                 uint32_t uVersion, uint32_t uVirtioLegacy_3_1_Beta)
{
    int rc;
    uint32_t uDriverFeaturesLegacy32bit;

    rc = pHlp->pfnSSMGetU32(  pSSM, &uDriverFeaturesLegacy32bit);
    AssertRCReturn(rc, rc);
    pVirtio->uDriverFeatures = (uint64_t)uDriverFeaturesLegacy32bit;

    rc = pHlp->pfnSSMGetU16(  pSSM, &pVirtio->uVirtqSelect);
    AssertRCReturn(rc, rc);

    rc = pHlp->pfnSSMGetU8(   pSSM, &pVirtio->fDeviceStatus);
    AssertRCReturn(rc, rc);

#ifdef LOG_ENABLED
    char szOut[80] = { 0 };
    virtioCoreFormatDeviceStatus(pVirtio->fDeviceStatus, szOut, sizeof(szOut));
    Log(("Loaded legacy device status = (%s)\n", szOut));
#endif

    rc = pHlp->pfnSSMGetU8(   pSSM, &pVirtio->uISR);
    AssertRCReturn(rc, rc);

    uint32_t cQueues = 3; /* This constant default value copied from earliest v0.9 code */
    if (uVersion > uVirtioLegacy_3_1_Beta)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &cQueues);
        AssertRCReturn(rc, rc);
    }

    AssertLogRelMsgReturn(cQueues <= VIRTQ_MAX_COUNT, ("%#x\n", cQueues), VERR_SSM_LOAD_CONFIG_MISMATCH);
    AssertLogRelMsgReturn(pVirtio->uVirtqSelect < cQueues || (cQueues == 0 && pVirtio->uVirtqSelect),
                          ("uVirtqSelect=%u cQueues=%u\n", pVirtio->uVirtqSelect, cQueues),
                          VERR_SSM_LOAD_CONFIG_MISMATCH);

    Log(("\nRestoring %d  legacy-only virtio-net device queues from saved state:\n", cQueues));
    for (unsigned uVirtq = 0; uVirtq < cQueues; uVirtq++)
    {
        PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[uVirtq];

        if (uVirtq == cQueues - 1)
            RTStrPrintf(pVirtq->szName, sizeof(pVirtq->szName), "legacy-ctrlq");
        else if (uVirtq % 2)
            RTStrPrintf(pVirtq->szName, sizeof(pVirtq->szName), "legacy-xmitq<%d>", uVirtq / 2);
        else
            RTStrPrintf(pVirtq->szName, sizeof(pVirtq->szName), "legacy-recvq<%d>", uVirtq / 2);

        rc = pHlp->pfnSSMGetU16(pSSM, &pVirtq->uQueueSize);
        AssertRCReturn(rc, rc);

        uint32_t uVirtqPfn;
        rc = pHlp->pfnSSMGetU32(pSSM, &uVirtqPfn);
        AssertRCReturn(rc, rc);

        rc = pHlp->pfnSSMGetU16(pSSM, &pVirtq->uAvailIdxShadow);
        AssertRCReturn(rc, rc);

        rc = pHlp->pfnSSMGetU16(pSSM, &pVirtq->uUsedIdxShadow);
        AssertRCReturn(rc, rc);

        if (uVirtqPfn)
        {
            pVirtq->GCPhysVirtqDesc  = (uint64_t)uVirtqPfn * VIRTIO_PAGE_SIZE;
            pVirtq->GCPhysVirtqAvail = pVirtq->GCPhysVirtqDesc + sizeof(VIRTQ_DESC_T) * pVirtq->uQueueSize;
            pVirtq->GCPhysVirtqUsed  =
                RT_ALIGN(pVirtq->GCPhysVirtqAvail + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[pVirtq->uQueueSize]), VIRTIO_PAGE_SIZE);
            pVirtq->uEnable = 1;
        }
        else
        {
            LogFunc(("WARNING: QUEUE \"%s\" PAGE NUMBER ZERO IN SAVED STATE\n", pVirtq->szName));
            pVirtq->uEnable = 0;
        }
        pVirtq->uNotifyOffset = 0;  /* unused in legacy mode */
        pVirtq->uMsixVector   = 0;  /* unused in legacy mode */
    }
    pVirtio->fGenUpdatePending = 0; /* unused in legacy mode */
    pVirtio->uConfigGeneration = 0; /* unused in legacy mode */
    pVirtio->uPciCfgDataOff    = 0; /* unused in legacy mode (port I/O used instead)   */

    return VINF_SUCCESS;
}

/**
 * Loads a saved device state (called from device-specific code on SSM final pass)
 *
 * Note: This loads state saved by a Modern (VirtIO 1.0+) device, of which this transitional device is one,
 *       and thus supports both legacy and modern guest virtio drivers.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pHlp        The ring-3 device helpers.
 * @param   pSSM        The saved state handle.
 * @returns VBox status code.
 */
DECLHIDDEN(int) virtioCoreR3ModernDeviceLoadExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM,
                                                 uint32_t uVersion, uint32_t uTestVersion, uint32_t cQueues)
{
    RT_NOREF2(cQueues, uVersion);
    LogFunc(("\n"));
    /*
     * Check the marker and (embedded) version number.
     */
    uint64_t uMarker = 0;
    int rc;

    rc = pHlp->pfnSSMGetU64(pSSM, &uMarker);
    AssertRCReturn(rc, rc);
    if (uMarker != VIRTIO_SAVEDSTATE_MARKER)
        return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                        N_("Expected marker value %#RX64 found %#RX64 instead"),
                                        VIRTIO_SAVEDSTATE_MARKER, uMarker);
    uint32_t uVersionSaved = 0;
    rc = pHlp->pfnSSMGetU32(pSSM, &uVersionSaved);
    AssertRCReturn(rc, rc);
    if (uVersionSaved != uTestVersion)
        return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                        N_("Unsupported virtio version: %u"), uVersionSaved);
    /*
     * Load the state.
     */
    rc = pHlp->pfnSSMGetU32(  pSSM, &pVirtio->fLegacyDriver);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetBool( pSSM, &pVirtio->fGenUpdatePending);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU8(   pSSM, &pVirtio->fDeviceStatus);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU8(   pSSM, &pVirtio->uConfigGeneration);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU8(   pSSM, &pVirtio->uPciCfgDataOff);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU8(   pSSM, &pVirtio->uISR);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU16(  pSSM, &pVirtio->uVirtqSelect);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU32(  pSSM, &pVirtio->uDeviceFeaturesSelect);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU32(  pSSM, &pVirtio->uDriverFeaturesSelect);
    AssertRCReturn(rc, rc);
    rc = pHlp->pfnSSMGetU64(  pSSM, &pVirtio->uDriverFeatures);
    AssertRCReturn(rc, rc);

    /** @todo Adapt this loop use cQueues argument instead of static queue count (safely with SSM versioning) */
    for (uint32_t i = 0; i < VIRTQ_MAX_COUNT; i++)
    {
        PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[i];
        rc = pHlp->pfnSSMGetGCPhys64( pSSM, &pVirtq->GCPhysVirtqDesc);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetGCPhys64( pSSM, &pVirtq->GCPhysVirtqAvail);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetGCPhys64( pSSM, &pVirtq->GCPhysVirtqUsed);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetU16(      pSSM, &pVirtq->uNotifyOffset);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetU16(      pSSM, &pVirtq->uMsixVector);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetU16(      pSSM, &pVirtq->uEnable);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetU16(      pSSM, &pVirtq->uQueueSize);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetU16(      pSSM, &pVirtq->uAvailIdxShadow);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetU16(      pSSM, &pVirtq->uUsedIdxShadow);
        AssertRCReturn(rc, rc);
        rc = pHlp->pfnSSMGetMem( pSSM, pVirtq->szName,  sizeof(pVirtq->szName));
        AssertRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
}

/**
 * Called from the FNSSMDEVSAVEEXEC function of the device.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pHlp        The ring-3 device helpers.
 * @param   pSSM        The saved state handle.
 * @returns VBox status code.
 */
DECLHIDDEN(int) virtioCoreR3SaveExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t cQueues)
{
    RT_NOREF(cQueues);
    /** @todo figure out a way to save cQueues (with SSM versioning) */

    LogFunc(("\n"));
    pHlp->pfnSSMPutU64(pSSM, VIRTIO_SAVEDSTATE_MARKER);
    pHlp->pfnSSMPutU32(pSSM, uVersion);

    pHlp->pfnSSMPutU32( pSSM, pVirtio->fLegacyDriver);
    pHlp->pfnSSMPutBool(pSSM, pVirtio->fGenUpdatePending);
    pHlp->pfnSSMPutU8(  pSSM, pVirtio->fDeviceStatus);
    pHlp->pfnSSMPutU8(  pSSM, pVirtio->uConfigGeneration);
    pHlp->pfnSSMPutU8(  pSSM, pVirtio->uPciCfgDataOff);
    pHlp->pfnSSMPutU8(  pSSM, pVirtio->uISR);
    pHlp->pfnSSMPutU16( pSSM, pVirtio->uVirtqSelect);
    pHlp->pfnSSMPutU32( pSSM, pVirtio->uDeviceFeaturesSelect);
    pHlp->pfnSSMPutU32( pSSM, pVirtio->uDriverFeaturesSelect);
    pHlp->pfnSSMPutU64( pSSM, pVirtio->uDriverFeatures);

    for (uint32_t i = 0; i < VIRTQ_MAX_COUNT; i++)
    {
        PVIRTQUEUE pVirtq = &pVirtio->aVirtqueues[i];

        pHlp->pfnSSMPutGCPhys64( pSSM, pVirtq->GCPhysVirtqDesc);
        pHlp->pfnSSMPutGCPhys64( pSSM, pVirtq->GCPhysVirtqAvail);
        pHlp->pfnSSMPutGCPhys64( pSSM, pVirtq->GCPhysVirtqUsed);
        pHlp->pfnSSMPutU16(      pSSM, pVirtq->uNotifyOffset);
        pHlp->pfnSSMPutU16(      pSSM, pVirtq->uMsixVector);
        pHlp->pfnSSMPutU16(      pSSM, pVirtq->uEnable);
        pHlp->pfnSSMPutU16(      pSSM, pVirtq->uQueueSize);
        pHlp->pfnSSMPutU16(      pSSM, pVirtq->uAvailIdxShadow);
        pHlp->pfnSSMPutU16(      pSSM, pVirtq->uUsedIdxShadow);
        int rc = pHlp->pfnSSMPutMem(pSSM, pVirtq->szName, 32);
        AssertRCReturn(rc, rc);
    }
    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Device Level                                                                                                                 *
*********************************************************************************************************************************/

/**
 * This must be called by the client to handle VM state changes after the client takes care of its device-specific
 * tasks for the state change (i.e. reset, suspend, power-off, resume)
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 */
DECLHIDDEN(void) virtioCoreR3VmStateChanged(PVIRTIOCORE pVirtio, VIRTIOVMSTATECHANGED enmState)
{
    LogFunc(("State changing to %s\n",
        virtioCoreGetStateChangeText(enmState)));

    switch(enmState)
    {
        case kvirtIoVmStateChangedReset:
            virtioCoreResetAll(pVirtio);
            break;
        case kvirtIoVmStateChangedSuspend:
            break;
        case kvirtIoVmStateChangedPowerOff:
            break;
        case kvirtIoVmStateChangedResume:
            for (int uVirtq = 0; uVirtq < VIRTQ_MAX_COUNT; uVirtq++)
            {
                if ((!pVirtio->fLegacyDriver && pVirtio->aVirtqueues[uVirtq].uEnable)
                    | pVirtio->aVirtqueues[uVirtq].GCPhysVirtqDesc)
                        virtioCoreNotifyGuestDriver(pVirtio->pDevInsR3, pVirtio, uVirtq);
            }
            break;
        default:
            LogRelFunc(("Bad enum value"));
            return;
    }
}

/**
 * This should be called from PDMDEVREGR3::pfnDestruct.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pVirtioCC   Pointer to the ring-3 virtio state.
 */
DECLHIDDEN(void) virtioCoreR3Term(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC)
{
#ifdef VIRTIO_REL_INFO_DUMP
    RTTraceBufRelease(pVirtio->hTraceBuf);
    pVirtio->hTraceBuf = NIL_RTTRACEBUF;
#endif /* VIRTIO_REL_INFO_DUMP */
    if (pVirtioCC->pbPrevDevSpecificCfg)
    {
        RTMemFree(pVirtioCC->pbPrevDevSpecificCfg);
        pVirtioCC->pbPrevDevSpecificCfg = NULL;
    }

    RT_NOREF(pDevIns, pVirtio);
}


/**
 * Setup the Virtio device as a PCI device.
 *
 * @returns VBox status code.
 * @param   pDevIns                 Device instance.
 * @param   pVirtio                 Pointer to the shared virtio state.  This
 *                                  must be the first member in the shared
 *                                  device instance data!
 * @param   pVirtioCC               Pointer to the ring-3 virtio state.  This
 *                                  must be the first member in the ring-3
 *                                  device instance data!
 * @param   pPciParams              Values to populate industry standard PCI Configuration Space data structure
 * @param   pcszInstance            Device instance name (format-specifier)
 * @param   cbDevSpecificCfg        Size of virtio_pci_device_cap device-specific struct
 */
static int virtioR3PciTransportInit(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, PVIRTIOPCIPARAMS pPciParams,
                                    const char *pcszInstance, uint16_t cbDevSpecificCfg)
{
    /* Set PCI config registers (assume 32-bit mode) */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,           DEVICE_PCI_VENDOR_ID_VIRTIO);
    PDMPciDevSetDeviceId(pPciDev,           pPciParams->uDeviceId);

    if (pPciParams->uDeviceId < DEVICE_PCI_DEVICE_ID_VIRTIO_BASE)
        /* Transitional devices MUST have a PCI Revision ID of 0. */
        PDMPciDevSetRevisionId(pPciDev,     DEVICE_PCI_REVISION_ID_VIRTIO_TRANS);
    else
        /* Non-transitional devices SHOULD have a PCI Revision ID of 1 or higher. */
        PDMPciDevSetRevisionId(pPciDev,     DEVICE_PCI_REVISION_ID_VIRTIO_V1);

    PDMPciDevSetSubSystemId(pPciDev,        pPciParams->uSubsystemId);
    PDMPciDevSetSubSystemVendorId(pPciDev,  DEVICE_PCI_VENDOR_ID_VIRTIO);
    PDMPciDevSetClassBase(pPciDev,          pPciParams->uClassBase);
    PDMPciDevSetClassSub(pPciDev,           pPciParams->uClassSub);
    PDMPciDevSetClassProg(pPciDev,          pPciParams->uClassProg);
    PDMPciDevSetInterruptLine(pPciDev,      pPciParams->uInterruptLine);
    PDMPciDevSetInterruptPin(pPciDev,       pPciParams->uInterruptPin);

    /* Register PCI device */
    int rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register PCI Device")); /* can we put params in this error? */

    rc = PDMDevHlpPCIInterceptConfigAccesses(pDevIns, pPciDev, virtioR3PciConfigRead, virtioR3PciConfigWrite);
    AssertRCReturn(rc, rc);

    /* Construct & map PCI vendor-specific capabilities for virtio host negotiation with guest driver */

#define CFG_ADDR_2_IDX(addr) ((uint8_t)(((uintptr_t)(addr) - (uintptr_t)&pPciDev->abConfig[0])))
#define SET_PCI_CAP_LOC(a_pPciDev, a_pCfg, a_LocCap, a_uMmioLengthAlign) \
        do { \
            (a_LocCap).offMmio = (a_pCfg)->uOffset; \
            (a_LocCap).cbMmio  = RT_ALIGN_T((a_pCfg)->uLength, a_uMmioLengthAlign, uint16_t); \
            (a_LocCap).offPci  = (uint16_t)(uintptr_t)((uint8_t *)(a_pCfg) - &(a_pPciDev)->abConfig[0]); \
            (a_LocCap).cbPci   = (a_pCfg)->uCapLen; \
        } while (0)

    PVIRTIO_PCI_CAP_T pCfg;
    uint32_t cbRegion = 0;

    /*
     * Common capability (VirtIO 1.0, section 4.1.4.3)
     */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[0x40];
    pCfg->uCfgType = VIRTIO_PCI_CAP_COMMON_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = RT_ALIGN_32(0, 4); /* Currently 0, but reminder to 32-bit align if changing this */
    pCfg->uLength  = sizeof(VIRTIO_PCI_COMMON_CFG_T);
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocCommonCfgCap, 2);
    pVirtioCC->pCommonCfgCap = pCfg;

    /*
     * Notify capability (VirtIO 1.0, section 4.1.4.4).
     *
     * The size of the spec-defined subregion described by this VirtIO capability is
     * based-on the choice of this implementation to make the notification area of each
     * queue equal to queue's ordinal position (e.g. queue selector value). The VirtIO
     * specification leaves it up to implementation to define queue notification area layout.
     */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_NOTIFY_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_NOTIFY_CAP_T);
    pCfg->uCapNext = CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = pVirtioCC->pCommonCfgCap->uOffset + pVirtioCC->pCommonCfgCap->uLength;
    pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 4);
    pCfg->uLength  = VIRTQ_MAX_COUNT * VIRTIO_NOTIFY_OFFSET_MULTIPLIER + 2;  /* will change in VirtIO 1.1 */
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocNotifyCap, 1);
    pVirtioCC->pNotifyCap = (PVIRTIO_PCI_NOTIFY_CAP_T)pCfg;
    pVirtioCC->pNotifyCap->uNotifyOffMultiplier = VIRTIO_NOTIFY_OFFSET_MULTIPLIER;

    /* ISR capability (VirtIO 1.0, section 4.1.4.5)
     *
     * VirtIO 1.0 spec says 8-bit, unaligned in MMIO space. The specification example/diagram
     * illustrates this capability as 32-bit field with upper bits 'reserved'. Those depictions
     * differ. The spec's wording, not the diagram, is seen to work in practice.
     */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_ISR_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = pVirtioCC->pNotifyCap->pciCap.uOffset + pVirtioCC->pNotifyCap->pciCap.uLength;
    pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 4);
    pCfg->uLength  = sizeof(uint8_t);
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocIsrCap, 4);
    pVirtioCC->pIsrCap = pCfg;

    /*  PCI Cfg capability (VirtIO 1.0, section 4.1.4.7)
     *
     *  This capability facilitates early-boot access to this device (BIOS).
     *  This region isn't page-MMIO mapped. PCI configuration accesses are intercepted,
     *  wherein uBar, uOffset and uLength are modulated by consumers to locate and read/write
     *  values in any part of any region. (NOTE: Linux driver doesn't utilize this feature.
     *  This capability only appears in lspci output on Linux if uLength is non-zero, 4-byte aligned,
     *  during initialization of linux virtio driver).
     */
    pVirtio->uPciCfgDataOff = pCfg->uCapNext + RT_OFFSETOF(VIRTIO_PCI_CFG_CAP_T, uPciCfgData);
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_PCI_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CFG_CAP_T);
    pCfg->uCapNext = (pVirtio->fMsiSupport || pVirtioCC->pbDevSpecificCfg) ? CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen : 0;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = 0;
    pCfg->uLength  = 4;
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocPciCfgCap, 1);
    pVirtioCC->pPciCfgCap = (PVIRTIO_PCI_CFG_CAP_T)pCfg;

    if (pVirtioCC->pbDevSpecificCfg)
    {
        /* Device-specific config capability (VirtIO 1.0, section 4.1.4.6).
         *
         * Client defines the device-specific config struct and passes size to virtioCoreR3Init()
         * to inform this.
         */
        pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
        pCfg->uCfgType = VIRTIO_PCI_CAP_DEVICE_CFG;
        pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
        pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
        pCfg->uCapNext = pVirtio->fMsiSupport ? CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen : 0;
        pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
        pCfg->uOffset  = pVirtioCC->pIsrCap->uOffset + pVirtioCC->pIsrCap->uLength;
        pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 4);
        pCfg->uLength  = cbDevSpecificCfg;
        cbRegion += pCfg->uLength;
        SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocDeviceCap, 4);
        pVirtioCC->pDeviceCap = pCfg;
    }
    else
        Assert(pVirtio->LocDeviceCap.cbMmio == 0 && pVirtio->LocDeviceCap.cbPci == 0);

    if (pVirtio->fMsiSupport)
    {
        PDMMSIREG aMsiReg;
        RT_ZERO(aMsiReg);
        aMsiReg.iMsixCapOffset  = pCfg->uCapNext;
        aMsiReg.iMsixNextOffset = 0;
        aMsiReg.iMsixBar        = VIRTIO_REGION_MSIX_CAP;
        aMsiReg.cMsixVectors    = VBOX_MSIX_MAX_ENTRIES;
        rc = PDMDevHlpPCIRegisterMsi(pDevIns, &aMsiReg); /* see MsixR3init() */
        if (RT_FAILURE(rc))
        {
            /* See PDMDevHlp.cpp:pdmR3DevHlp_PCIRegisterMsi */
            LogFunc(("Failed to configure MSI-X (%Rrc). Reverting to INTx\n", rc));
            pVirtio->fMsiSupport = false;
        }
        else
            Log2Func(("Using MSI-X for guest driver notification\n"));
    }
    else
        LogFunc(("MSI-X not available for VBox, using INTx notification\n"));

    /* Set offset to first capability and enable PCI dev capabilities */
    PDMPciDevSetCapabilityList(pPciDev, 0x40);
    PDMPciDevSetStatus(pPciDev, VBOX_PCI_STATUS_CAP_LIST);

    size_t cbSize = RTStrPrintf(pVirtioCC->szMmioName, sizeof(pVirtioCC->szMmioName), "%s (modern)", pcszInstance);
    if (cbSize <= 0)
        return PDMDEV_SET_ERROR(pDevIns, VERR_BUFFER_OVERFLOW, N_("virtio: out of memory allocating string")); /* can we put params in this error? */

    cbSize = RTStrPrintf(pVirtioCC->szPortIoName, sizeof(pVirtioCC->szPortIoName), "%s (legacy)", pcszInstance);
    if (cbSize <= 0)
        return PDMDEV_SET_ERROR(pDevIns, VERR_BUFFER_OVERFLOW, N_("virtio: out of memory allocating string")); /* can we put params in this error? */

    if (pVirtio->fOfferLegacy)
    {
        /* As a transitional device that supports legacy VirtIO drivers, this VirtIO device generic implementation presents
         * legacy driver interface in I/O space at BAR0. The following maps the common (e.g. device independent)
         * dev config area as well as device-specific dev config area (whose size is passed to init function of this VirtIO
         * generic device code) for access via Port I/O, since legacy drivers (e.g. pre VirtIO 1.0) don't use MMIO callbacks.
         * (See VirtIO 1.1, Section 4.1.4.8).
         */
        rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, VIRTIO_REGION_LEGACY_IO, sizeof(VIRTIO_LEGACY_PCI_COMMON_CFG_T) + cbDevSpecificCfg,
                                          virtioLegacyIOPortOut, virtioLegacyIOPortIn, NULL /*pvUser*/, pVirtioCC->szPortIoName,
                                          NULL /*paExtDescs*/, &pVirtio->hLegacyIoPorts);
        AssertLogRelRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register legacy config in I/O space at BAR0 */")));
    }

    /*  Note: The Linux driver at drivers/virtio/virtio_pci_modern.c tries to map at least a page for the
     * 'unknown' device-specific capability without querying the capability to determine size, so pad w/extra page.
     */
    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, VIRTIO_REGION_PCI_CAP, RT_ALIGN_32(cbRegion + VIRTIO_PAGE_SIZE, VIRTIO_PAGE_SIZE),
                                        PCI_ADDRESS_SPACE_MEM, virtioMmioWrite, virtioMmioRead, pVirtio,
                                        IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                        pVirtioCC->szMmioName,
                                        &pVirtio->hMmioPciCap);
    AssertLogRelRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register PCI Capabilities address space")));
    return VINF_SUCCESS;
}


/**
 * Initializes the VirtIO device using the VirtIO over MMIO transport mode.
 *
 * @returns VBox status code.
 * @param   pDevIns                 Device instance.
 * @param   pVirtio                 Pointer to the shared virtio state.  This
 *                                  must be the first member in the shared
 *                                  device instance data!
 * @param   pVirtioCC               Pointer to the ring-3 virtio state.  This
 *                                  must be the first member in the ring-3
 *                                  device instance data!
 * @param   pcszInstance            Device instance name (format-specifier)
 * @param   cbDevSpecificCfg        Size of virtio_pci_device_cap device-specific struct
 * @param   GCPhysMmioBase          The physical guest address of the start of the MMIO area.
 * @param   u16Irq                  The interrupt number to use for the virtio device.
 */
static int virtioR3MmioTransportInit(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, const char *pcszInstance,
                                     uint16_t cbDevSpecificCfg, RTGCPHYS GCPhysMmioBase, uint16_t u16Irq)
{
    pVirtio->uIrqMmio = u16Irq;

    size_t cbSize = RTStrPrintf(pVirtioCC->szMmioName, sizeof(pVirtioCC->szMmioName), "%s (modern)", pcszInstance);
    if (cbSize <= 0)
        return PDMDEV_SET_ERROR(pDevIns, VERR_BUFFER_OVERFLOW, N_("virtio: out of memory allocating string")); /* can we put params in this error? */

    /*
     * Register and map the MMIO region.
     */
    int rc = PDMDevHlpMmioCreateAndMap(pDevIns, GCPhysMmioBase, RT_ALIGN_32(cbDevSpecificCfg + VIRTIO_MMIO_SIZE, 512),
                                       virtioMmioTransportWrite, virtioMmioTransportRead,
                                       IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                       pVirtioCC->szMmioName, &pVirtio->hMmioPciCap);
    AssertLogRelRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register PCI Capabilities address space")));
    return VINF_SUCCESS;
}


/** API Function: See header file */
DECLHIDDEN(int) virtioCoreR3Init(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, PVIRTIOPCIPARAMS pPciParams,
                                 const char *pcszInstance, uint64_t fDevSpecificFeatures, uint32_t fOfferLegacy,
                                 void *pvDevSpecificCfg, uint16_t cbDevSpecificCfg)
{
    /*
     * Virtio state must be the first member of shared device instance data,
     * otherwise can't get our bearings in PCI config callbacks.
     */
    AssertLogRelReturn(pVirtio == PDMINS_2_DATA(pDevIns, PVIRTIOCORE), VERR_STATE_CHANGED);
    AssertLogRelReturn(pVirtioCC == PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC), VERR_STATE_CHANGED);

    pVirtio->pDevInsR3 = pDevIns;

    /*
     * Caller must initialize these.
     */
    AssertReturn(pVirtioCC->pfnStatusChanged, VERR_INVALID_POINTER);
    AssertReturn(pVirtioCC->pfnVirtqNotified, VERR_INVALID_POINTER);
    AssertReturn(VIRTQ_SIZE > 0 && VIRTQ_SIZE <= 32768,  VERR_OUT_OF_RANGE); /* VirtIO specification-defined limit */

    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    uint16_t u16Irq = 0;
    int rc = pHlp->pfnCFGMQueryU16Def(pDevIns->pCfg, "Irq", &u16Irq, 0);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"Irq\" value"));

    RTGCPHYS GCPhysMmioBase = 0;
    rc = pHlp->pfnCFGMQueryU64Def(pDevIns->pCfg, "MmioBase", &GCPhysMmioBase, NIL_RTGCPHYS);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed to get the \"MmioBase\" value"));

#if 0 /* Until pdmR3DvHlp_PCISetIrq() impl is fixed and Assert that limits vec to 0 is removed
       * VBox legacy MSI support has not been implemented yet
       */
# ifdef VBOX_WITH_MSI_DEVICES
    pVirtio->fMsiSupport = true;
# endif
#endif

    /*
     * Host features (presented as a buffet for guest to select from)
     * include both dev-specific features & reserved dev-independent features (bitmask).
     */
    pVirtio->uDeviceType     = pPciParams->uDeviceType;
    pVirtio->uDeviceFeatures = VIRTIO_F_VERSION_1
                             | VIRTIO_DEV_INDEPENDENT_FEATURES_OFFERED
                             | fDevSpecificFeatures;

    pVirtio->fLegacyDriver = pVirtio->fOfferLegacy = fOfferLegacy;

    RTStrCopy(pVirtio->szInstance, sizeof(pVirtio->szInstance), pcszInstance);
    pVirtioCC->cbDevSpecificCfg = cbDevSpecificCfg;
    pVirtioCC->pbDevSpecificCfg = (uint8_t *)pvDevSpecificCfg;
    pVirtioCC->pbPrevDevSpecificCfg = (uint8_t *)RTMemDup(pvDevSpecificCfg, cbDevSpecificCfg);
    AssertLogRelReturn(pVirtioCC->pbPrevDevSpecificCfg, VERR_NO_MEMORY);

    if (GCPhysMmioBase != NIL_RTGCPHYS)
        rc = virtioR3MmioTransportInit(pDevIns, pVirtio, pVirtioCC, pcszInstance, cbDevSpecificCfg,
                                       GCPhysMmioBase, u16Irq);
    else
        rc = virtioR3PciTransportInit(pDevIns, pVirtio, pVirtioCC, pPciParams, pcszInstance, cbDevSpecificCfg);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Statistics.
     */
# ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegisterF(pDevIns, &pVirtio->StatDescChainsAllocated,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Total number of allocated descriptor chains",   "DescChainsAllocated");
    PDMDevHlpSTAMRegisterF(pDevIns, &pVirtio->StatDescChainsFreed,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Total number of freed descriptor chains",       "DescChainsFreed");
    PDMDevHlpSTAMRegisterF(pDevIns, &pVirtio->StatDescChainsSegsIn,     STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Total number of inbound segments",              "DescChainsSegsIn");
    PDMDevHlpSTAMRegisterF(pDevIns, &pVirtio->StatDescChainsSegsOut,    STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Total number of outbound segments",             "DescChainsSegsOut");
    PDMDevHlpSTAMRegister(pDevIns, &pVirtio->StatReadR3,    STAMTYPE_PROFILE, "IO/ReadR3",          STAMUNIT_TICKS_PER_CALL, "Profiling IO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pVirtio->StatReadR0,    STAMTYPE_PROFILE, "IO/ReadR0",          STAMUNIT_TICKS_PER_CALL, "Profiling IO reads in R0");
    PDMDevHlpSTAMRegister(pDevIns, &pVirtio->StatReadRC,    STAMTYPE_PROFILE, "IO/ReadRC",          STAMUNIT_TICKS_PER_CALL, "Profiling IO reads in RC");
    PDMDevHlpSTAMRegister(pDevIns, &pVirtio->StatWriteR3,   STAMTYPE_PROFILE, "IO/WriteR3",         STAMUNIT_TICKS_PER_CALL, "Profiling IO writes in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pVirtio->StatWriteR0,   STAMTYPE_PROFILE, "IO/WriteR0",         STAMUNIT_TICKS_PER_CALL, "Profiling IO writes in R0");
    PDMDevHlpSTAMRegister(pDevIns, &pVirtio->StatWriteRC,   STAMTYPE_PROFILE, "IO/WriteRC",         STAMUNIT_TICKS_PER_CALL, "Profiling IO writes in RC");
# endif /* VBOX_WITH_STATISTICS */

#ifdef VIRTIO_REL_INFO_DUMP
    pVirtio->fRecovering = false;
    pVirtio->fTestRecovery = false;
    size_t cbBlock = VIRTIO_CORE_TRACE_BUF_SIZE;
    rc = RTTraceBufCarve(&pVirtio->hTraceBuf, 0 /*cEntries*/, 0 /*cbEntry*/, 0 /*fFlags*/, pVirtio->aTraceBuf, &cbBlock);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        LogRel(("virtioCore: Failed to initialize trace buffer (rc=%d)\n", rc));
#endif /* VIRTIO_REL_INFO_DUMP */
    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * Sets up the core ring-0/raw-mode virtio bits.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.  This must be the first
 *                      member in the shared device instance data!
 */
DECLHIDDEN(int) virtioCoreRZInit(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio)
{
    AssertLogRelReturn(pVirtio == PDMINS_2_DATA(pDevIns, PVIRTIOCORE), VERR_STATE_CHANGED);
    int rc;
#ifdef FUTURE_OPTIMIZATION
     rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);
#endif

    if (pVirtio->uIrqMmio != 0)
    {
        rc = PDMDevHlpMmioSetUpContext(pDevIns, pVirtio->hMmioPciCap, virtioMmioTransportWrite, virtioMmioTransportRead, pVirtio);
        AssertRCReturn(rc, rc);
    }
    else
    {
        rc = PDMDevHlpMmioSetUpContext(pDevIns, pVirtio->hMmioPciCap, virtioMmioWrite, virtioMmioRead, pVirtio);
        AssertRCReturn(rc, rc);

        if (pVirtio->fOfferLegacy)
        {
            rc = PDMDevHlpIoPortSetUpContext(pDevIns, pVirtio->hLegacyIoPorts, virtioLegacyIOPortOut, virtioLegacyIOPortIn, NULL /*pvUser*/);
            AssertRCReturn(rc, rc);
        }
    }
    return rc;
}

#endif /* !IN_RING3 */

