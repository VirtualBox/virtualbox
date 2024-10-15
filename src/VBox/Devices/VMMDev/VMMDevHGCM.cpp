/* $Id: VMMDevHGCM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMMDev - HGCM - Host-Guest Communication Manager Device.
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
#define LOG_GROUP LOG_GROUP_DEV_VMM
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/string.h>

#include <VBox/AssertGuest.h>
#include <VBox/err.h>
#include <VBox/hgcmsvc.h>
#include <VBox/log.h>

#include "VMMDevHGCM.h"

#ifdef DEBUG
# define VBOX_STRICT_GUEST
#endif

#ifdef VBOX_WITH_DTRACE
# include "dtrace/VBoxDD.h"
#else
# define VBOXDD_HGCMCALL_ENTER(a,b,c,d)             do { } while (0)
# define VBOXDD_HGCMCALL_COMPLETED_REQ(a,b)         do { } while (0)
# define VBOXDD_HGCMCALL_COMPLETED_EMT(a,b)         do { } while (0)
# define VBOXDD_HGCMCALL_COMPLETED_DONE(a,b,c,d)    do { } while (0)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum VBOXHGCMCMDTYPE
{
    VBOXHGCMCMDTYPE_LOADSTATE = 0,
    VBOXHGCMCMDTYPE_CONNECT,
    VBOXHGCMCMDTYPE_DISCONNECT,
    VBOXHGCMCMDTYPE_CALL,
    VBOXHGCMCMDTYPE_SizeHack = 0x7fffffff
} VBOXHGCMCMDTYPE;

/**
 * Information about a 32 or 64 bit parameter.
 */
typedef struct VBOXHGCMPARMVAL
{
    /** Actual value. Both 32 and 64 bit is saved here. */
    uint64_t u64Value;

    /** Offset from the start of the request where the value is stored. */
    uint32_t offValue;

    /** Size of the value: 4 for 32 bit and 8 for 64 bit. */
    uint32_t cbValue;

} VBOXHGCMPARMVAL;

/**
 * Information about a pointer parameter.
 */
typedef struct VBOXHGCMPARMPTR
{
    /** Size of the buffer described by the pointer parameter. */
    uint32_t cbData;

/** @todo save 8 bytes here by putting offFirstPage, cPages, and f32Direction
 *        into a bitfields like in VBOXHGCMPARMPAGES. */
    /** Offset in the first physical page of the region. */
    uint32_t offFirstPage;

    /** How many pages. */
    uint32_t cPages;

    /** How the buffer should be copied VBOX_HGCM_F_PARM_*. */
    uint32_t fu32Direction;

    /** Pointer to array of the GC physical addresses for these pages.
     * It is assumed that the physical address of the locked resident guest page
     * does not change.  */
    RTGCPHYS *paPages;

    /** For single page requests. */
    RTGCPHYS  GCPhysSinglePage;

} VBOXHGCMPARMPTR;


/**
 * Pages w/o bounce buffering.
 */
typedef struct VBOXHGCMPARMPAGES
{
    /** The buffer size. */
    uint32_t        cbData;
    /** Start of buffer offset into the first page. */
    uint32_t        offFirstPage : 12;
    /** VBOX_HGCM_F_PARM_XXX flags. */
    uint32_t        fFlags : 3;
    /** Set if we've locked all the pages. */
    uint32_t        fLocked : 1;
    /** Number of pages. */
    uint32_t        cPages : 16;
    /**< Array of page locks followed by array of page pointers, the first page
     * pointer is adjusted by offFirstPage. */
    PPGMPAGEMAPLOCK paPgLocks;
} VBOXHGCMPARMPAGES;

/**
 * Information about a guest HGCM parameter.
 */
typedef struct VBOXHGCMGUESTPARM
{
    /** The parameter type. */
    HGCMFunctionParameterType enmType;

    union
    {
        VBOXHGCMPARMVAL       val;
        VBOXHGCMPARMPTR       ptr;
        VBOXHGCMPARMPAGES     Pages;
    } u;

} VBOXHGCMGUESTPARM;

typedef struct VBOXHGCMCMD
{
    /** Active commands, list is protected by critsectHGCMCmdList. */
    RTLISTNODE          node;

    /** The type of the command (VBOXHGCMCMDTYPE). */
    uint8_t             enmCmdType;

    /** Whether the command was cancelled by the guest. */
    bool                fCancelled;

    /** Set if allocated from the memory cache, clear if heap. */
    bool                fMemCache;

    /** Whether the command was restored from saved state. */
    bool                fRestored : 1;
    /** Whether this command has a no-bounce page list and needs to be restored
     *  from guest memory the old fashioned way. */
    bool                fRestoreFromGuestMem : 1;

    /** Copy of VMMDevRequestHeader::fRequestor.
     * @note Only valid if VBOXGSTINFO2_F_REQUESTOR_INFO is set in
     *       VMMDevState.guestInfo2.fFeatures. */
    uint32_t            fRequestor;

    /** GC physical address of the guest request. */
    RTGCPHYS            GCPhys;

    /** Request packet size. */
    uint32_t            cbRequest;

    /** The type of the guest request. */
    VMMDevRequestType   enmRequestType;

    /** Pointer to the locked request, NULL if not locked. */
    void               *pvReqLocked;
    /** The PGM lock for GCPhys if pvReqLocked is not NULL. */
    PGMPAGEMAPLOCK      ReqMapLock;

    /** The accounting index (into VMMDEVR3::aHgcmAcc). */
    uint8_t             idxHeapAcc;
    uint8_t             abPadding[3];
    /** The heap cost of this command. */
    uint32_t            cbHeapCost;

    /** The STAM_GET_TS() value when the request arrived. */
    uint64_t            tsArrival;
    /** The STAM_GET_TS() value when the hgcmR3Completed() is called. */
    uint64_t            tsComplete;

    union
    {
        struct
        {
            uint32_t            u32ClientID;
            HGCMServiceLocation *pLoc;  /**< Allocated after this structure. */
        } connect;

        struct
        {
            uint32_t            u32ClientID;
        } disconnect;

        struct
        {
            /* Number of elements in paGuestParms and paHostParms arrays. */
            uint32_t            cParms;

            uint32_t            u32ClientID;

            uint32_t            u32Function;

            /** Pointer to information about guest parameters in case of a Call request.
             * Follows this structure in the same memory block.
             */
            VBOXHGCMGUESTPARM  *paGuestParms;

            /** Pointer to converted host parameters in case of a Call request.
             * Follows this structure in the same memory block.
             */
            VBOXHGCMSVCPARM    *paHostParms;

            /* VBOXHGCMGUESTPARM[] */
            /* VBOXHGCMSVCPARM[] */
        } call;
    } u;
} VBOXHGCMCMD;


/**
 * Version for the memory cache.
 */
typedef struct VBOXHGCMCMDCACHED
{
    VBOXHGCMCMD         Core;           /**< 120 */
    VBOXHGCMGUESTPARM   aGuestParms[6]; /**< 40 * 6 = 240 */
    VBOXHGCMSVCPARM     aHostParms[6];  /**< 24 * 6 = 144 */
} VBOXHGCMCMDCACHED;                    /**< 120+240+144 = 504 */
AssertCompile(sizeof(VBOXHGCMCMD) <= 120);
AssertCompile(sizeof(VBOXHGCMGUESTPARM) <= 40);
AssertCompile(sizeof(VBOXHGCMSVCPARM) <= 24);
AssertCompile(sizeof(VBOXHGCMCMDCACHED) <= 512);
AssertCompile(sizeof(VBOXHGCMCMDCACHED) > sizeof(VBOXHGCMCMD) + sizeof(HGCMServiceLocation));


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(void *) vmmdevR3HgcmCallMemAllocZ(PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd, size_t cbRequested);



DECLINLINE(int) vmmdevR3HgcmCmdListLock(PVMMDEVCC pThisCC)
{
    int rc = RTCritSectEnter(&pThisCC->critsectHGCMCmdList);
    AssertRC(rc);
    return rc;
}

DECLINLINE(void) vmmdevR3HgcmCmdListUnlock(PVMMDEVCC pThisCC)
{
    int rc = RTCritSectLeave(&pThisCC->critsectHGCMCmdList);
    AssertRC(rc);
}

/** Allocate and initialize VBOXHGCMCMD structure for HGCM request.
 *
 * @returns Pointer to the command on success, NULL otherwise.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   enmCmdType      Type of the command.
 * @param   GCPhys          The guest physical address of the HGCM request.
 * @param   cbRequest       The size of the HGCM request.
 * @param   cParms          Number of HGCM parameters for VBOXHGCMCMDTYPE_CALL command.
 * @param   fRequestor      The VMMDevRequestHeader::fRequestor value.
 */
static PVBOXHGCMCMD vmmdevR3HgcmCmdAlloc(PVMMDEVCC pThisCC, VBOXHGCMCMDTYPE enmCmdType, RTGCPHYS GCPhys,
                                         uint32_t cbRequest, uint32_t cParms, uint32_t fRequestor)
{
    /*
     * Pick the heap accounting category.
     *
     * Initial idea was to just use what VMMDEV_REQUESTOR_USR_MASK yields directly,
     * but there are so many unused categories then (DRV, RESERVED1, GUEST).  Better
     * to have fewer and more heap available in each.
     */
    uintptr_t idxHeapAcc;
    if (fRequestor != VMMDEV_REQUESTOR_LEGACY)
        switch (fRequestor & VMMDEV_REQUESTOR_USR_MASK)
        {
            case VMMDEV_REQUESTOR_USR_NOT_GIVEN:
            case VMMDEV_REQUESTOR_USR_DRV:
            case VMMDEV_REQUESTOR_USR_DRV_OTHER:
                idxHeapAcc = VMMDEV_HGCM_CATEGORY_KERNEL;
                break;
            case VMMDEV_REQUESTOR_USR_ROOT:
            case VMMDEV_REQUESTOR_USR_SYSTEM:
                idxHeapAcc = VMMDEV_HGCM_CATEGORY_ROOT;
                break;
            default:
                AssertFailed(); RT_FALL_THRU();
            case VMMDEV_REQUESTOR_USR_RESERVED1:
            case VMMDEV_REQUESTOR_USR_USER:
            case VMMDEV_REQUESTOR_USR_GUEST:
                idxHeapAcc = VMMDEV_HGCM_CATEGORY_USER;
                break;
        }
    else
        idxHeapAcc = VMMDEV_HGCM_CATEGORY_KERNEL;

#if 1
    /*
     * Try use the cache.
     */
    VBOXHGCMCMDCACHED *pCmdCached;
    AssertCompile(sizeof(*pCmdCached) >= sizeof(VBOXHGCMCMD) + sizeof(HGCMServiceLocation));
    if (cParms <= RT_ELEMENTS(pCmdCached->aGuestParms))
    {
        if (sizeof(*pCmdCached) <= pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget)
        {
            int rc = RTMemCacheAllocEx(pThisCC->hHgcmCmdCache, (void **)&pCmdCached);
            if (RT_SUCCESS(rc))
            {
                RT_ZERO(*pCmdCached);
                pCmdCached->Core.fMemCache  = true;
                pCmdCached->Core.GCPhys     = GCPhys;
                pCmdCached->Core.cbRequest  = cbRequest;
                pCmdCached->Core.enmCmdType = enmCmdType;
                pCmdCached->Core.fRequestor = fRequestor;
                pCmdCached->Core.idxHeapAcc = (uint8_t)idxHeapAcc;
                pCmdCached->Core.cbHeapCost = sizeof(*pCmdCached);
                Log5Func(("aHgcmAcc[%zu] %#RX64 -= %#zx (%p)\n",
                          idxHeapAcc, pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget, sizeof(*pCmdCached), &pCmdCached->Core));
                pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget -= sizeof(*pCmdCached);

                if (enmCmdType == VBOXHGCMCMDTYPE_CALL)
                {
                    pCmdCached->Core.u.call.cParms       = cParms;
                    pCmdCached->Core.u.call.paGuestParms = pCmdCached->aGuestParms;
                    pCmdCached->Core.u.call.paHostParms  = pCmdCached->aHostParms;
                }
                else if (enmCmdType == VBOXHGCMCMDTYPE_CONNECT)
                    pCmdCached->Core.u.connect.pLoc = (HGCMServiceLocation *)(&pCmdCached->Core + 1);

                Assert(!pCmdCached->Core.pvReqLocked);

                Log3Func(("returns %p (enmCmdType=%d GCPhys=%RGp)\n", &pCmdCached->Core, enmCmdType, GCPhys));
                return &pCmdCached->Core;
            }
        }
        else
            LogFunc(("Heap budget overrun: sizeof(*pCmdCached)=%#zx aHgcmAcc[%zu].cbHeapBudget=%#RX64 - enmCmdType=%d\n",
                     sizeof(*pCmdCached), idxHeapAcc, pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget, enmCmdType));
        STAM_REL_COUNTER_INC(&pThisCC->aHgcmAcc[idxHeapAcc].StatBudgetOverruns);
        return NULL;
    }
    STAM_REL_COUNTER_INC(&pThisCC->StatHgcmLargeCmdAllocs);

#else
    RT_NOREF(pThisCC);
#endif

    /* Size of required memory buffer. */
    const uint32_t cbCmd = sizeof(VBOXHGCMCMD) + cParms * (sizeof(VBOXHGCMGUESTPARM) + sizeof(VBOXHGCMSVCPARM))
                         + (enmCmdType == VBOXHGCMCMDTYPE_CONNECT ? sizeof(HGCMServiceLocation) : 0);
    if (cbCmd <= pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget)
    {
        PVBOXHGCMCMD pCmd = (PVBOXHGCMCMD)RTMemAllocZ(cbCmd);
        if (pCmd)
        {
            pCmd->enmCmdType = enmCmdType;
            pCmd->GCPhys     = GCPhys;
            pCmd->cbRequest  = cbRequest;
            pCmd->fRequestor = fRequestor;
            pCmd->idxHeapAcc = (uint8_t)idxHeapAcc;
            pCmd->cbHeapCost = cbCmd;
            Log5Func(("aHgcmAcc[%zu] %#RX64 -= %#x (%p)\n", idxHeapAcc, pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget, cbCmd, pCmd));
            pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget -= cbCmd;

            if (enmCmdType == VBOXHGCMCMDTYPE_CALL)
            {
                pCmd->u.call.cParms = cParms;
                if (cParms)
                {
                    pCmd->u.call.paGuestParms = (VBOXHGCMGUESTPARM *)((uint8_t *)pCmd
                                                                      + sizeof(struct VBOXHGCMCMD));
                    pCmd->u.call.paHostParms = (VBOXHGCMSVCPARM *)((uint8_t *)pCmd->u.call.paGuestParms
                                                                   + cParms * sizeof(VBOXHGCMGUESTPARM));
                }
            }
            else if (enmCmdType == VBOXHGCMCMDTYPE_CONNECT)
                pCmd->u.connect.pLoc = (HGCMServiceLocation *)(pCmd + 1);
        }
        Log3Func(("returns %p (enmCmdType=%d GCPhys=%RGp cbCmd=%#x)\n", pCmd, enmCmdType, GCPhys, cbCmd));
        return pCmd;
    }
    STAM_REL_COUNTER_INC(&pThisCC->aHgcmAcc[idxHeapAcc].StatBudgetOverruns);
    LogFunc(("Heap budget overrun: cbCmd=%#x aHgcmAcc[%zu].cbHeapBudget=%#RX64 - enmCmdType=%d\n",
             cbCmd, idxHeapAcc, pThisCC->aHgcmAcc[idxHeapAcc].cbHeapBudget, enmCmdType));
    return NULL;
}

/** Deallocate VBOXHGCMCMD memory.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pCmd            Command to deallocate.
 */
static void vmmdevR3HgcmCmdFree(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd)
{
    if (pCmd)
    {
        Assert(   pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL
               || pCmd->enmCmdType == VBOXHGCMCMDTYPE_CONNECT
               || pCmd->enmCmdType == VBOXHGCMCMDTYPE_DISCONNECT
               || pCmd->enmCmdType == VBOXHGCMCMDTYPE_LOADSTATE);
        if (pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL)
        {
            uint32_t i;
            for (i = 0; i < pCmd->u.call.cParms; ++i)
            {
                VBOXHGCMSVCPARM   * const pHostParm  = &pCmd->u.call.paHostParms[i];
                VBOXHGCMGUESTPARM * const pGuestParm = &pCmd->u.call.paGuestParms[i];

                if (   pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_In
                    || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_Out
                    || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr
                    || pGuestParm->enmType == VMMDevHGCMParmType_PageList
                    || pGuestParm->enmType == VMMDevHGCMParmType_ContiguousPageList)
                {
                    Assert(pHostParm->type == VBOX_HGCM_SVC_PARM_PTR);
                    if (pGuestParm->u.ptr.paPages != &pGuestParm->u.ptr.GCPhysSinglePage)
                        RTMemFree(pGuestParm->u.ptr.paPages);
                    RTMemFreeZ(pHostParm->u.pointer.addr, pGuestParm->u.ptr.cbData);
                }
                else if (pGuestParm->enmType == VMMDevHGCMParmType_Embedded)
                {
                    Assert(pHostParm->type == VBOX_HGCM_SVC_PARM_PTR);
                    RTMemFreeZ(pHostParm->u.pointer.addr, pGuestParm->u.ptr.cbData);
                }
                else if (pGuestParm->enmType == VMMDevHGCMParmType_NoBouncePageList)
                {
                    Assert(pHostParm->type == VBOX_HGCM_SVC_PARM_PAGES);
                    if (pGuestParm->u.Pages.paPgLocks)
                    {
                        if (pGuestParm->u.Pages.fLocked)
                            PDMDevHlpPhysBulkReleasePageMappingLocks(pDevIns, pGuestParm->u.Pages.cPages,
                                                                     pGuestParm->u.Pages.paPgLocks);
                        RTMemFree(pGuestParm->u.Pages.paPgLocks);
                        pGuestParm->u.Pages.paPgLocks = NULL;
                    }
                }
                else
                    Assert(pHostParm->type != VBOX_HGCM_SVC_PARM_PTR && pHostParm->type != VBOX_HGCM_SVC_PARM_PAGES);
            }
        }

        if (pCmd->pvReqLocked)
        {
            PDMDevHlpPhysReleasePageMappingLock(pDevIns, &pCmd->ReqMapLock);
            pCmd->pvReqLocked = NULL;
        }

        pCmd->enmCmdType = UINT8_MAX; /* poison */

        /* Update heap budget.  Need the critsect to do this safely. */
        Assert(pCmd->cbHeapCost != 0);
        uintptr_t idx = pCmd->idxHeapAcc;
        AssertStmt(idx < RT_ELEMENTS(pThisCC->aHgcmAcc), idx %= RT_ELEMENTS(pThisCC->aHgcmAcc));

        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

        Log5Func(("aHgcmAcc[%zu] %#RX64 += %#x (%p)\n", idx, pThisCC->aHgcmAcc[idx].cbHeapBudget, pCmd->cbHeapCost, pCmd));
        pThisCC->aHgcmAcc[idx].cbHeapBudget += pCmd->cbHeapCost;
        AssertMsg(pThisCC->aHgcmAcc[idx].cbHeapBudget <= pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig,
                  ("idx=%d (%d) fRequestor=%#x pCmd=%p: %#RX64 vs %#RX64 -> %#RX64\n", idx, pCmd->idxHeapAcc, pCmd->fRequestor, pCmd,
                   pThisCC->aHgcmAcc[idx].cbHeapBudget,  pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig,
                   pThisCC->aHgcmAcc[idx].cbHeapBudget - pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig));
        pCmd->cbHeapCost = 0;

#if 1
        if (pCmd->fMemCache)
        {
            RTMemCacheFree(pThisCC->hHgcmCmdCache, pCmd);
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect); /* releasing it after just to be on the safe side. */
        }
        else
#endif
        {
            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
            RTMemFree(pCmd);
        }
    }
}

/** Add VBOXHGCMCMD to the list of pending commands.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pCmd            Command to add.
 */
static int vmmdevR3HgcmAddCommand(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd)
{
    int rc = vmmdevR3HgcmCmdListLock(pThisCC);
    AssertRCReturn(rc, rc);

    LogFlowFunc(("%p type %d\n", pCmd, pCmd->enmCmdType));

    RTListPrepend(&pThisCC->listHGCMCmd, &pCmd->node);

    /* stats */
    uintptr_t idx = pCmd->idxHeapAcc;
    AssertStmt(idx < RT_ELEMENTS(pThisCC->aHgcmAcc), idx %= RT_ELEMENTS(pThisCC->aHgcmAcc));
    STAM_REL_PROFILE_ADD_PERIOD(&pThisCC->aHgcmAcc[idx].StateMsgHeapUsage, pCmd->cbHeapCost);

    /* Automatically enable HGCM events, if there are HGCM commands. */
    if (   pCmd->enmCmdType == VBOXHGCMCMDTYPE_CONNECT
        || pCmd->enmCmdType == VBOXHGCMCMDTYPE_DISCONNECT
        || pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL)
    {
        LogFunc(("u32HGCMEnabled = %d\n", pThisCC->u32HGCMEnabled));
        if (ASMAtomicCmpXchgU32(&pThisCC->u32HGCMEnabled, 1, 0))
             VMMDevCtlSetGuestFilterMask(pDevIns, pThis, pThisCC, VMMDEV_EVENT_HGCM, 0);
    }

    vmmdevR3HgcmCmdListUnlock(pThisCC);
    return rc;
}

/** Remove VBOXHGCMCMD from the list of pending commands.
 *
 * @returns VBox status code.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pCmd            Command to remove.
 */
static int vmmdevR3HgcmRemoveCommand(PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd)
{
    int rc = vmmdevR3HgcmCmdListLock(pThisCC);
    AssertRCReturn(rc, rc);

    LogFlowFunc(("%p\n", pCmd));

    RTListNodeRemove(&pCmd->node);

    vmmdevR3HgcmCmdListUnlock(pThisCC);
    return rc;
}

/**
 * Find a HGCM command by its physical address.
 *
 * The caller is responsible for taking the command list lock before calling
 * this function.
 *
 * @returns Pointer to the command on success, NULL otherwise.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   GCPhys          The physical address of the command we're looking for.
 */
DECLINLINE(PVBOXHGCMCMD) vmmdevR3HgcmFindCommandLocked(PVMMDEVCC pThisCC, RTGCPHYS GCPhys)
{
    PVBOXHGCMCMD pCmd;
    RTListForEach(&pThisCC->listHGCMCmd, pCmd, VBOXHGCMCMD, node)
    {
        if (pCmd->GCPhys == GCPhys)
            return pCmd;
    }
    return NULL;
}

/** Copy VMMDevHGCMConnect request data from the guest to VBOXHGCMCMD command.
 *
 * @param   pHGCMConnect    The source guest request (cached in host memory).
 * @param   pCmd            Destination command.
 */
static void vmmdevR3HgcmConnectFetch(const VMMDevHGCMConnect *pHGCMConnect, PVBOXHGCMCMD pCmd)
{
    pCmd->enmRequestType        = pHGCMConnect->header.header.requestType;
    pCmd->u.connect.u32ClientID = pHGCMConnect->u32ClientID;
    *pCmd->u.connect.pLoc       = pHGCMConnect->loc;
}

/** Handle VMMDevHGCMConnect request.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pHGCMConnect    The guest request (cached in host memory).
 * @param   GCPhys          The physical address of the request.
 */
int vmmdevR3HgcmConnect(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                        const VMMDevHGCMConnect *pHGCMConnect, RTGCPHYS GCPhys)
{
    int rc;
    PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, VBOXHGCMCMDTYPE_CONNECT, GCPhys, pHGCMConnect->header.header.size, 0,
                                             pHGCMConnect->header.header.fRequestor);
    if (pCmd)
    {
        vmmdevR3HgcmConnectFetch(pHGCMConnect, pCmd);

        /* Only allow the guest to use existing services! */
        ASSERT_GUEST(pHGCMConnect->loc.type == VMMDevHGCMLoc_LocalHost_Existing);
        pCmd->u.connect.pLoc->type = VMMDevHGCMLoc_LocalHost_Existing;

        vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);
        rc = pThisCC->pHGCMDrv->pfnConnect(pThisCC->pHGCMDrv, pCmd, pCmd->u.connect.pLoc, &pCmd->u.connect.u32ClientID);
        if (RT_FAILURE(rc))
            vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/** Copy VMMDevHGCMDisconnect request data from the guest to VBOXHGCMCMD command.
 *
 * @param   pHGCMDisconnect The source guest request (cached in host memory).
 * @param   pCmd            Destination command.
 */
static void vmmdevR3HgcmDisconnectFetch(const VMMDevHGCMDisconnect *pHGCMDisconnect, PVBOXHGCMCMD pCmd)
{
    pCmd->enmRequestType = pHGCMDisconnect->header.header.requestType;
    pCmd->u.disconnect.u32ClientID = pHGCMDisconnect->u32ClientID;
}

/** Handle VMMDevHGCMDisconnect request.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pHGCMDisconnect The guest request (cached in host memory).
 * @param   GCPhys          The physical address of the request.
 */
int vmmdevR3HgcmDisconnect(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                           const VMMDevHGCMDisconnect *pHGCMDisconnect, RTGCPHYS GCPhys)
{
    int rc;
    PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, VBOXHGCMCMDTYPE_DISCONNECT, GCPhys, pHGCMDisconnect->header.header.size, 0,
                                             pHGCMDisconnect->header.header.fRequestor);
    if (pCmd)
    {
        vmmdevR3HgcmDisconnectFetch(pHGCMDisconnect, pCmd);

        vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);
        rc = pThisCC->pHGCMDrv->pfnDisconnect(pThisCC->pHGCMDrv, pCmd, pCmd->u.disconnect.u32ClientID);
        if (RT_FAILURE(rc))
            vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/** Translate LinAddr parameter type to the direction of data transfer.
 *
 * @returns VBOX_HGCM_F_PARM_DIRECTION_* flags.
 * @param   enmType         Type of the LinAddr parameter.
 */
static uint32_t vmmdevR3HgcmParmTypeToDirection(HGCMFunctionParameterType enmType)
{
    if (enmType == VMMDevHGCMParmType_LinAddr_In)  return VBOX_HGCM_F_PARM_DIRECTION_TO_HOST;
    if (enmType == VMMDevHGCMParmType_LinAddr_Out) return VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST;
    return VBOX_HGCM_F_PARM_DIRECTION_BOTH;
}

/** Check if list of pages in a HGCM pointer parameter corresponds to a contiguous buffer.
 *
 * @returns true if pages are contiguous, false otherwise.
 * @param   pPtr            Information about a pointer HGCM parameter.
 */
DECLINLINE(bool) vmmdevR3HgcmGuestBufferIsContiguous(const VBOXHGCMPARMPTR *pPtr)
{
    if (pPtr->cPages == 1)
        return true;
    RTGCPHYS64 Phys = pPtr->paPages[0] + VMMDEV_PAGE_SIZE;
    if (Phys != pPtr->paPages[1])
        return false;
    if (pPtr->cPages > 2)
    {
        uint32_t iPage = 2;
        do
        {
            Phys += VMMDEV_PAGE_SIZE;
            if (Phys != pPtr->paPages[iPage])
                return false;
            ++iPage;
        } while (iPage < pPtr->cPages);
    }
    return true;
}

/** Copy data from guest memory to the host buffer.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance for PDMDevHlp.
 * @param   pvDst           The destination host buffer.
 * @param   cbDst           Size of the destination host buffer.
 * @param   pPtr            Description of the source HGCM pointer parameter.
 */
static int vmmdevR3HgcmGuestBufferRead(PPDMDEVINSR3 pDevIns, void *pvDst, uint32_t cbDst, const VBOXHGCMPARMPTR *pPtr)
{
    /*
     * Try detect contiguous buffers.
     */
    /** @todo We need a flag for indicating this. */
    if (vmmdevR3HgcmGuestBufferIsContiguous(pPtr))
        return PDMDevHlpPhysRead(pDevIns, pPtr->paPages[0] | pPtr->offFirstPage, pvDst, cbDst);

    /*
     * Page by page fallback.
     */
    uint8_t *pu8Dst = (uint8_t *)pvDst;
    uint32_t offPage = pPtr->offFirstPage;
    uint32_t cbRemaining = cbDst;

    for (uint32_t iPage = 0; iPage < pPtr->cPages && cbRemaining > 0; ++iPage)
    {
        uint32_t cbToRead = VMMDEV_PAGE_SIZE - offPage;
        if (cbToRead > cbRemaining)
            cbToRead = cbRemaining;

        /* Skip invalid pages. */
        const RTGCPHYS GCPhys = pPtr->paPages[iPage];
        if (GCPhys != NIL_RTGCPHYS)
        {
            int rc = PDMDevHlpPhysRead(pDevIns, GCPhys + offPage, pu8Dst, cbToRead);
            AssertMsgReturn(RT_SUCCESS(rc), ("rc=%Rrc GCPhys=%RGp offPage=%#x cbToRead=%#x\n", rc, GCPhys, offPage, cbToRead), rc);
        }

        offPage = 0; /* A next page is read from 0 offset. */
        cbRemaining -= cbToRead;
        pu8Dst += cbToRead;
    }

    return VINF_SUCCESS;
}

/** Copy data from the host buffer to guest memory.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance for PDMDevHlp.
 * @param   pPtr            Description of the destination HGCM pointer parameter.
 * @param   pvSrc           The source host buffer.
 * @param   cbSrc           Size of the source host buffer.
 */
static int vmmdevR3HgcmGuestBufferWrite(PPDMDEVINSR3 pDevIns, const VBOXHGCMPARMPTR *pPtr, const void *pvSrc,  uint32_t cbSrc)
{
    int rc = VINF_SUCCESS;

    uint8_t *pu8Src = (uint8_t *)pvSrc;
    uint32_t offPage = pPtr->offFirstPage;
    uint32_t cbRemaining = RT_MIN(cbSrc, pPtr->cbData);

    uint32_t iPage;
    for (iPage = 0; iPage < pPtr->cPages && cbRemaining > 0; ++iPage)
    {
        uint32_t cbToWrite = VMMDEV_PAGE_SIZE - offPage;
        if (cbToWrite > cbRemaining)
            cbToWrite = cbRemaining;

        /* Skip invalid pages. */
        const RTGCPHYS GCPhys = pPtr->paPages[iPage];
        if (GCPhys != NIL_RTGCPHYS)
        {
            rc = PDMDevHlpPhysWrite(pDevIns, GCPhys + offPage, pu8Src, cbToWrite);
            AssertRCBreak(rc);
        }

        offPage = 0; /* A next page is written at 0 offset. */
        cbRemaining -= cbToWrite;
        pu8Src += cbToWrite;
    }

    return rc;
}

/** Initializes pCmd->paHostParms from already initialized pCmd->paGuestParms.
 * Allocates memory for pointer parameters and copies data from the guest.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pCmd            Command structure where host parameters needs initialization.
 * @param   pbReq           The request buffer.
 */
static int vmmdevR3HgcmInitHostParameters(PPDMDEVINS pDevIns, PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd, uint8_t const *pbReq)
{
    AssertReturn(pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL, VERR_INTERNAL_ERROR);

    for (uint32_t i = 0; i < pCmd->u.call.cParms; ++i)
    {
        VBOXHGCMGUESTPARM * const pGuestParm = &pCmd->u.call.paGuestParms[i];
        VBOXHGCMSVCPARM   * const pHostParm  = &pCmd->u.call.paHostParms[i];

        switch (pGuestParm->enmType)
        {
            case VMMDevHGCMParmType_32bit:
            {
                pHostParm->type = VBOX_HGCM_SVC_PARM_32BIT;
                pHostParm->u.uint32 = (uint32_t)pGuestParm->u.val.u64Value;

                break;
            }

            case VMMDevHGCMParmType_64bit:
            {
                pHostParm->type = VBOX_HGCM_SVC_PARM_64BIT;
                pHostParm->u.uint64 = pGuestParm->u.val.u64Value;

                break;
            }

            case VMMDevHGCMParmType_PageList:
            case VMMDevHGCMParmType_LinAddr_In:
            case VMMDevHGCMParmType_LinAddr_Out:
            case VMMDevHGCMParmType_LinAddr:
            case VMMDevHGCMParmType_Embedded:
            case VMMDevHGCMParmType_ContiguousPageList:
            {
                const uint32_t cbData = pGuestParm->u.ptr.cbData;

                pHostParm->type = VBOX_HGCM_SVC_PARM_PTR;
                pHostParm->u.pointer.size = cbData;

                if (cbData)
                {
                    /* Zero memory, the buffer content is potentially copied to the guest. */
                    void *pv = vmmdevR3HgcmCallMemAllocZ(pThisCC, pCmd, cbData);
                    AssertReturn(pv, VERR_NO_MEMORY);
                    pHostParm->u.pointer.addr = pv;

                    if (pGuestParm->u.ptr.fu32Direction & VBOX_HGCM_F_PARM_DIRECTION_TO_HOST)
                    {
                        if (pGuestParm->enmType != VMMDevHGCMParmType_Embedded)
                        {
                            if (pGuestParm->enmType != VMMDevHGCMParmType_ContiguousPageList)
                            {
                                int rc = vmmdevR3HgcmGuestBufferRead(pDevIns, pv, cbData, &pGuestParm->u.ptr);
                                ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);
                                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
                            }
                            else
                            {
                                int rc = PDMDevHlpPhysRead(pDevIns,
                                                           pGuestParm->u.ptr.paPages[0] | pGuestParm->u.ptr.offFirstPage,
                                                           pv, cbData);
                                ASSERT_GUEST_RETURN(RT_SUCCESS(rc), rc);
                                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
                            }
                        }
                        else
                        {
                            memcpy(pv, &pbReq[pGuestParm->u.ptr.offFirstPage], cbData);
                            RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
                        }
                    }
                }
                else
                {
                    pHostParm->u.pointer.addr = NULL;
                }

                break;
            }

            case VMMDevHGCMParmType_NoBouncePageList:
            {
                pHostParm->type = VBOX_HGCM_SVC_PARM_PAGES;
                pHostParm->u.Pages.cb        = pGuestParm->u.Pages.cbData;
                pHostParm->u.Pages.cPages    = pGuestParm->u.Pages.cPages;
                pHostParm->u.Pages.papvPages = (void **)&pGuestParm->u.Pages.paPgLocks[pGuestParm->u.Pages.cPages];

                break;
            }

            default:
                ASSERT_GUEST_FAILED_RETURN(VERR_INVALID_PARAMETER);
        }
    }

    return VINF_SUCCESS;
}


/** Allocate and initialize VBOXHGCMCMD structure for a HGCMCall request.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pHGCMCall       The HGCMCall request (cached in host memory).
 * @param   cbHGCMCall      Size of the request.
 * @param   GCPhys          Guest physical address of the request.
 * @param   enmRequestType  The request type. Distinguishes 64 and 32 bit calls.
 * @param   ppCmd           Where to store pointer to allocated command.
 * @param   pcbHGCMParmStruct Where to store size of used HGCM parameter structure.
 */
static int vmmdevR3HgcmCallAlloc(PVMMDEVCC pThisCC, const VMMDevHGCMCall *pHGCMCall, uint32_t cbHGCMCall, RTGCPHYS GCPhys,
                                 VMMDevRequestType enmRequestType, PVBOXHGCMCMD *ppCmd, uint32_t *pcbHGCMParmStruct)
{
#ifdef VBOX_WITH_64_BITS_GUESTS
    const uint32_t cbHGCMParmStruct = enmRequestType == VMMDevReq_HGCMCall64 ? sizeof(HGCMFunctionParameter64)
                                                                             : sizeof(HGCMFunctionParameter32);
#else
    const uint32_t cbHGCMParmStruct = sizeof(HGCMFunctionParameter);
#endif

    const uint32_t cParms = pHGCMCall->cParms;

    /* Whether there is enough space for parameters and sane upper limit. */
    ASSERT_GUEST_STMT_RETURN(   cParms <= (cbHGCMCall - sizeof(VMMDevHGCMCall)) / cbHGCMParmStruct
                             && cParms <= VMMDEV_MAX_HGCM_PARMS,
                             LogRelMax(50, ("VMMDev: request packet with invalid number of HGCM parameters: %d vs %d. Refusing operation.\n",
                                           (cbHGCMCall - sizeof(VMMDevHGCMCall)) / cbHGCMParmStruct, cParms)),
                             VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, VBOXHGCMCMDTYPE_CALL, GCPhys, cbHGCMCall, cParms,
                                             pHGCMCall->header.header.fRequestor);
    if (pCmd == NULL)
        return VERR_NO_MEMORY;

    /* Request type has been validated in vmmdevReqDispatcher. */
    pCmd->enmRequestType     = enmRequestType;
    pCmd->u.call.u32ClientID = pHGCMCall->u32ClientID;
    pCmd->u.call.u32Function = pHGCMCall->u32Function;

    *ppCmd = pCmd;
    *pcbHGCMParmStruct = cbHGCMParmStruct;
    return VINF_SUCCESS;
}

/**
 * Heap budget wrapper around RTMemAlloc and RTMemAllocZ.
 */
static void *vmmdevR3HgcmCallMemAllocEx(PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd, size_t cbRequested, bool fZero)
{
    uintptr_t idx = pCmd->idxHeapAcc;
    AssertStmt(idx < RT_ELEMENTS(pThisCC->aHgcmAcc), idx %= RT_ELEMENTS(pThisCC->aHgcmAcc));

    /* Check against max heap costs for this request. */
    Assert(pCmd->cbHeapCost <= VMMDEV_MAX_HGCM_DATA_SIZE);
    if (cbRequested <= VMMDEV_MAX_HGCM_DATA_SIZE - pCmd->cbHeapCost)
    {
        /* Check heap budget (we're under lock). */
        if (cbRequested <= pThisCC->aHgcmAcc[idx].cbHeapBudget)
        {
            /* Do the actual allocation. */
            void *pv = fZero ? RTMemAllocZ(cbRequested) : RTMemAlloc(cbRequested);
            if (pv)
            {
                /* Update the request cost and heap budget. */
                Log5Func(("aHgcmAcc[%zu] %#RX64 += %#x (%p)\n", idx, pThisCC->aHgcmAcc[idx].cbHeapBudget, cbRequested, pCmd));
                pThisCC->aHgcmAcc[idx].cbHeapBudget -=           cbRequested;
                pCmd->cbHeapCost                    += (uint32_t)cbRequested;
                return pv;
            }
            LogFunc(("Heap alloc failed: cbRequested=%#zx - enmCmdType=%d\n", cbRequested, pCmd->enmCmdType));
        }
        else
            LogFunc(("Heap budget overrun: cbRequested=%#zx cbHeapCost=%#x aHgcmAcc[%u].cbHeapBudget=%#RX64 - enmCmdType=%d\n",
                     cbRequested, pCmd->cbHeapCost, pCmd->idxHeapAcc, pThisCC->aHgcmAcc[idx].cbHeapBudget, pCmd->enmCmdType));
    }
    else
        LogFunc(("Request too big: cbRequested=%#zx cbHeapCost=%#x - enmCmdType=%d\n",
                 cbRequested, pCmd->cbHeapCost, pCmd->enmCmdType));
    STAM_REL_COUNTER_INC(&pThisCC->aHgcmAcc[idx].StatBudgetOverruns);
    return NULL;
}

/**
 * Heap budget wrapper around RTMemAlloc.
 */
DECLINLINE(void *) vmmdevR3HgcmCallMemAlloc(PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd, size_t cbRequested)
{
    return vmmdevR3HgcmCallMemAllocEx(pThisCC, pCmd, cbRequested, false /*fZero*/);
}

/**
 * Heap budget wrapper around RTMemAllocZ.
 */
DECLINLINE(void *) vmmdevR3HgcmCallMemAllocZ(PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd, size_t cbRequested)
{
    return vmmdevR3HgcmCallMemAllocEx(pThisCC, pCmd, cbRequested, true /*fZero*/);
}

/** Copy VMMDevHGCMCall request data from the guest to VBOXHGCMCMD command.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pCmd            The destination command.
 * @param   pHGCMCall       The HGCMCall request (cached in host memory).
 * @param   cbHGCMCall      Size of the request.
 * @param   enmRequestType  The request type. Distinguishes 64 and 32 bit calls.
 * @param   cbHGCMParmStruct Size of used HGCM parameter structure.
 */
static int vmmdevR3HgcmCallFetchGuestParms(PPDMDEVINS pDevIns, PVMMDEVCC pThisCC, PVBOXHGCMCMD pCmd,
                                           const VMMDevHGCMCall *pHGCMCall, uint32_t cbHGCMCall,
                                           VMMDevRequestType enmRequestType, uint32_t cbHGCMParmStruct)
{
    /*
     * Go over all guest parameters and initialize relevant VBOXHGCMCMD fields.
     * VBOXHGCMCMD must contain all information about the request,
     * the request will be not read from the guest memory again.
     */
#ifdef VBOX_WITH_64_BITS_GUESTS
    const bool f64Bits = (enmRequestType == VMMDevReq_HGCMCall64);
#endif

    const uint32_t cParms = pCmd->u.call.cParms;

    /* Offsets in the request buffer to HGCM parameters and additional data. */
    const uint32_t offHGCMParms = sizeof(VMMDevHGCMCall);
    const uint32_t offExtra = offHGCMParms + cParms * cbHGCMParmStruct;

    /* Pointer to the next HGCM parameter of the request. */
    const uint8_t *pu8HGCMParm = (uint8_t *)pHGCMCall + offHGCMParms;

    for (uint32_t i = 0; i < cParms; ++i, pu8HGCMParm += cbHGCMParmStruct)
    {
        VBOXHGCMGUESTPARM * const pGuestParm = &pCmd->u.call.paGuestParms[i];

#ifdef VBOX_WITH_64_BITS_GUESTS
        AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, type, HGCMFunctionParameter32, type);
        pGuestParm->enmType = ((HGCMFunctionParameter64 *)pu8HGCMParm)->type;
#else
        pGuestParm->enmType = ((HGCMFunctionParameter   *)pu8HGCMParm)->type;
#endif

        switch (pGuestParm->enmType)
        {
            case VMMDevHGCMParmType_32bit:
            {
#ifdef VBOX_WITH_64_BITS_GUESTS
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.value32,   HGCMFunctionParameter32, u.value32);
                uint32_t *pu32 = &((HGCMFunctionParameter64 *)pu8HGCMParm)->u.value32;
#else
                uint32_t *pu32 = &((HGCMFunctionParameter   *)pu8HGCMParm)->u.value32;
#endif
                LogFunc(("uint32 guest parameter %RI32\n", *pu32));

                pGuestParm->u.val.u64Value = *pu32;
                pGuestParm->u.val.offValue = (uint32_t)((uintptr_t)pu32 - (uintptr_t)pHGCMCall);
                pGuestParm->u.val.cbValue = sizeof(uint32_t);

                break;
            }

            case VMMDevHGCMParmType_64bit:
            {
#ifdef VBOX_WITH_64_BITS_GUESTS
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.value64,   HGCMFunctionParameter32, u.value64);
                uint64_t *pu64 = (uint64_t *)(uintptr_t)&((HGCMFunctionParameter64 *)pu8HGCMParm)->u.value64; /* MSC detect misalignment, thus casts. */
#else
                uint64_t *pu64 = &((HGCMFunctionParameter   *)pu8HGCMParm)->u.value64;
#endif
                LogFunc(("uint64 guest parameter %RI64\n", *pu64));

                pGuestParm->u.val.u64Value = *pu64;
                pGuestParm->u.val.offValue = (uint32_t)((uintptr_t)pu64 - (uintptr_t)pHGCMCall);
                pGuestParm->u.val.cbValue = sizeof(uint64_t);

                break;
            }

            case VMMDevHGCMParmType_LinAddr_In:  /* In (read) */
            case VMMDevHGCMParmType_LinAddr_Out: /* Out (write) */
            case VMMDevHGCMParmType_LinAddr:     /* In & Out */
            {
#ifdef VBOX_WITH_64_BITS_GUESTS
                uint32_t cbData = f64Bits ? ((HGCMFunctionParameter64 *)pu8HGCMParm)->u.Pointer.size
                                          : ((HGCMFunctionParameter32 *)pu8HGCMParm)->u.Pointer.size;
                RTGCPTR GCPtr = f64Bits ? ((HGCMFunctionParameter64 *)pu8HGCMParm)->u.Pointer.u.linearAddr
                                        : ((HGCMFunctionParameter32 *)pu8HGCMParm)->u.Pointer.u.linearAddr;
#else
                uint32_t cbData = ((HGCMFunctionParameter *)pu8HGCMParm)->u.Pointer.size;
                RTGCPTR GCPtr = ((HGCMFunctionParameter *)pu8HGCMParm)->u.Pointer.u.linearAddr;
#endif
                LogFunc(("LinAddr guest parameter %RGv, cb %u\n", GCPtr, cbData));

                ASSERT_GUEST_RETURN(cbData <= VMMDEV_MAX_HGCM_DATA_SIZE, VERR_INVALID_PARAMETER);

                const uint32_t offFirstPage = cbData > 0 ? GCPtr & VMMDEV_PAGE_OFFSET_MASK : 0;
                const uint32_t cPages       = cbData > 0 ? (offFirstPage + cbData + VMMDEV_PAGE_SIZE - 1) / VMMDEV_PAGE_SIZE : 0;

                pGuestParm->u.ptr.cbData        = cbData;
                pGuestParm->u.ptr.offFirstPage  = offFirstPage;
                pGuestParm->u.ptr.cPages        = cPages;
                pGuestParm->u.ptr.fu32Direction = vmmdevR3HgcmParmTypeToDirection(pGuestParm->enmType);

                if (cbData > 0)
                {
                    if (cPages == 1)
                        pGuestParm->u.ptr.paPages = &pGuestParm->u.ptr.GCPhysSinglePage;
                    else
                    {
                        /* (Max 262144 bytes with current limits.) */
                        pGuestParm->u.ptr.paPages = (RTGCPHYS *)vmmdevR3HgcmCallMemAlloc(pThisCC, pCmd,
                                                                                         cPages * sizeof(RTGCPHYS));
                        AssertReturn(pGuestParm->u.ptr.paPages, VERR_NO_MEMORY);
                    }

                    /* Gonvert the guest linear pointers of pages to physical addresses. */
                    GCPtr &= ~(RTGCPTR)VMMDEV_PAGE_OFFSET_MASK;
                    for (uint32_t iPage = 0; iPage < cPages; ++iPage)
                    {
                        /* The guest might specify invalid GCPtr, just skip such addresses.
                         * Also if the guest parameters are fetched when restoring an old saved state,
                         * then GCPtr may become invalid and do not have a corresponding GCPhys.
                         * The command restoration routine will take care of this.
                         */
                        RTGCPHYS GCPhys;
                        int rc2 = PDMDevHlpPhysGCPtr2GCPhys(pDevIns, GCPtr, &GCPhys);
                        if (RT_FAILURE(rc2))
                            GCPhys = NIL_RTGCPHYS;
                        LogFunc(("Page %d: %RGv -> %RGp. %Rrc\n", iPage, GCPtr, GCPhys, rc2));

                        pGuestParm->u.ptr.paPages[iPage] = GCPhys;
                        GCPtr += VMMDEV_PAGE_SIZE;
                    }
                }

                break;
            }

            case VMMDevHGCMParmType_PageList:
            case VMMDevHGCMParmType_ContiguousPageList:
            case VMMDevHGCMParmType_NoBouncePageList:
            {
#ifdef VBOX_WITH_64_BITS_GUESTS
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.PageList.size,   HGCMFunctionParameter32, u.PageList.size);
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.PageList.offset, HGCMFunctionParameter32, u.PageList.offset);
                uint32_t cbData          = ((HGCMFunctionParameter64 *)pu8HGCMParm)->u.PageList.size;
                uint32_t offPageListInfo = ((HGCMFunctionParameter64 *)pu8HGCMParm)->u.PageList.offset;
#else
                uint32_t cbData          = ((HGCMFunctionParameter   *)pu8HGCMParm)->u.PageList.size;
                uint32_t offPageListInfo = ((HGCMFunctionParameter   *)pu8HGCMParm)->u.PageList.offset;
#endif
                LogFunc(("PageList guest parameter cb %u, offset %u\n", cbData, offPageListInfo));

                ASSERT_GUEST_RETURN(cbData <= VMMDEV_MAX_HGCM_DATA_SIZE, VERR_INVALID_PARAMETER);

/** @todo respect zero byte page lists...    */
                /* Check that the page list info is within the request. */
                ASSERT_GUEST_RETURN(   offPageListInfo >= offExtra
                                    && cbHGCMCall >= sizeof(HGCMPageListInfo)
                                    && offPageListInfo <= cbHGCMCall - sizeof(HGCMPageListInfo),
                                    VERR_INVALID_PARAMETER);
                RT_UNTRUSTED_VALIDATED_FENCE();

                /* The HGCMPageListInfo structure is within the request. */
                const HGCMPageListInfo *pPageListInfo = (HGCMPageListInfo *)((uint8_t *)pHGCMCall + offPageListInfo);

                /* Enough space for page pointers? */
                const uint32_t cMaxPages = 1 + (cbHGCMCall - offPageListInfo - sizeof(HGCMPageListInfo)) / sizeof(RTGCPHYS);
                ASSERT_GUEST_RETURN(   pPageListInfo->cPages > 0
                                    && pPageListInfo->cPages <= cMaxPages,
                                    VERR_INVALID_PARAMETER);

                /* Flags. */
                ASSERT_GUEST_MSG_RETURN(VBOX_HGCM_F_PARM_ARE_VALID(pPageListInfo->flags),
                                        ("%#x\n", pPageListInfo->flags), VERR_INVALID_FLAGS);
                /* First page offset. */
                ASSERT_GUEST_MSG_RETURN(pPageListInfo->offFirstPage < VMMDEV_PAGE_SIZE,
                                        ("%#x\n", pPageListInfo->offFirstPage), VERR_INVALID_PARAMETER);

                /* Contiguous page lists only ever have a single page and
                   no-bounce page list requires cPages to match the size exactly.
                   Plain page list does not impose any restrictions on cPages currently. */
                ASSERT_GUEST_MSG_RETURN(      pPageListInfo->cPages
                                           == (pGuestParm->enmType == VMMDevHGCMParmType_ContiguousPageList ? 1
                                               :    RT_ALIGN_32(pPageListInfo->offFirstPage + cbData, VMMDEV_PAGE_SIZE)
                                                 >> VMMDEV_PAGE_SHIFT)
                                        || pGuestParm->enmType == VMMDevHGCMParmType_PageList,
                                        ("offFirstPage=%#x cbData=%#x cPages=%#x enmType=%d\n",
                                         pPageListInfo->offFirstPage, cbData, pPageListInfo->cPages, pGuestParm->enmType),
                                        VERR_INVALID_PARAMETER);

                RT_UNTRUSTED_VALIDATED_FENCE();

                /*
                 * Deal with no-bounce buffers first, as
                 * VMMDevHGCMParmType_PageList is the fallback.
                 */
                if (pGuestParm->enmType == VMMDevHGCMParmType_NoBouncePageList)
                {
                    /* Validate page offsets */
                    ASSERT_GUEST_MSG_RETURN(   !(pPageListInfo->aPages[0] & VMMDEV_PAGE_OFFSET_MASK)
                                            || (pPageListInfo->aPages[0] & VMMDEV_PAGE_OFFSET_MASK) == pPageListInfo->offFirstPage,
                                            ("%#RX64 offFirstPage=%#x\n", pPageListInfo->aPages[0], pPageListInfo->offFirstPage),
                                            VERR_INVALID_POINTER);
                    uint32_t const cPages = pPageListInfo->cPages;
                    for (uint32_t iPage = 1; iPage < cPages; iPage++)
                        ASSERT_GUEST_MSG_RETURN(!(pPageListInfo->aPages[iPage] & VMMDEV_PAGE_OFFSET_MASK),
                                                ("[%#zx]=%#RX64\n", iPage, pPageListInfo->aPages[iPage]), VERR_INVALID_POINTER);
                    RT_UNTRUSTED_VALIDATED_FENCE();

                    pGuestParm->u.Pages.cbData       = cbData;
                    pGuestParm->u.Pages.offFirstPage = pPageListInfo->offFirstPage;
                    pGuestParm->u.Pages.fFlags       = pPageListInfo->flags;
                    pGuestParm->u.Pages.cPages       = (uint16_t)cPages;
                    pGuestParm->u.Pages.fLocked      = false;
                    pGuestParm->u.Pages.paPgLocks    = (PPGMPAGEMAPLOCK)vmmdevR3HgcmCallMemAllocZ(pThisCC, pCmd,
                                                                                                  (  sizeof(PGMPAGEMAPLOCK)
                                                                                                   + sizeof(void *)) * cPages);
                    AssertReturn(pGuestParm->u.Pages.paPgLocks, VERR_NO_MEMORY);

                    /* Make sure the page offsets are sensible. */
                    int rc = VINF_SUCCESS;
                    void **papvPages = (void **)&pGuestParm->u.Pages.paPgLocks[cPages];
                    if (pPageListInfo->flags & VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST)
                        rc = PDMDevHlpPhysBulkGCPhys2CCPtr(pDevIns, cPages, pPageListInfo->aPages, 0 /*fFlags*/,
                                                           papvPages, pGuestParm->u.Pages.paPgLocks);
                    else
                        rc = PDMDevHlpPhysBulkGCPhys2CCPtrReadOnly(pDevIns, cPages, pPageListInfo->aPages, 0 /*fFlags*/,
                                                                   (void const **)papvPages, pGuestParm->u.Pages.paPgLocks);
                    if (RT_SUCCESS(rc))
                    {
                        papvPages[0] = (void *)((uintptr_t)papvPages[0] | pPageListInfo->offFirstPage);
                        pGuestParm->u.Pages.fLocked = true;
                        break;
                    }

                    /* Locking failed, bail out.  In case of MMIO we fall back on regular page list handling. */
                    RTMemFree(pGuestParm->u.Pages.paPgLocks);
                    pGuestParm->u.Pages.paPgLocks = NULL;
                    STAM_REL_COUNTER_INC(&pThisCC->StatHgcmFailedPageListLocking);
                    ASSERT_GUEST_MSG_RETURN(rc == VERR_PGM_PHYS_PAGE_RESERVED, ("cPages=%u %Rrc\n", cPages, rc), rc);
                    pGuestParm->enmType = VMMDevHGCMParmType_PageList;
                }

                /*
                 * Regular page list or contiguous page list.
                 */
                pGuestParm->u.ptr.cbData        = cbData;
                pGuestParm->u.ptr.offFirstPage  = pPageListInfo->offFirstPage;
                pGuestParm->u.ptr.cPages        = pPageListInfo->cPages;
                pGuestParm->u.ptr.fu32Direction = pPageListInfo->flags;
                if (pPageListInfo->cPages == 1)
                {
                    pGuestParm->u.ptr.paPages   = &pGuestParm->u.ptr.GCPhysSinglePage;
                    pGuestParm->u.ptr.GCPhysSinglePage = pPageListInfo->aPages[0];
                }
                else
                {
                    pGuestParm->u.ptr.paPages   = (RTGCPHYS *)vmmdevR3HgcmCallMemAlloc(pThisCC, pCmd,
                                                                                       pPageListInfo->cPages * sizeof(RTGCPHYS));
                    AssertReturn(pGuestParm->u.ptr.paPages, VERR_NO_MEMORY);

                    for (uint32_t iPage = 0; iPage < pGuestParm->u.ptr.cPages; ++iPage)
                        pGuestParm->u.ptr.paPages[iPage] = pPageListInfo->aPages[iPage];
                }
                break;
            }

            case VMMDevHGCMParmType_Embedded:
            {
#ifdef VBOX_WITH_64_BITS_GUESTS
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.Embedded.cbData, HGCMFunctionParameter32, u.Embedded.cbData);
                uint32_t const cbData    = ((HGCMFunctionParameter64 *)pu8HGCMParm)->u.Embedded.cbData;
                uint32_t const offData   = ((HGCMFunctionParameter64 *)pu8HGCMParm)->u.Embedded.offData;
                uint32_t const fFlags    = ((HGCMFunctionParameter64 *)pu8HGCMParm)->u.Embedded.fFlags;
#else
                uint32_t const cbData    = ((HGCMFunctionParameter   *)pu8HGCMParm)->u.Embedded.cbData;
                uint32_t const offData   = ((HGCMFunctionParameter   *)pu8HGCMParm)->u.Embedded.offData;
                uint32_t const fFlags    = ((HGCMFunctionParameter   *)pu8HGCMParm)->u.Embedded.fFlags;
#endif
                LogFunc(("Embedded guest parameter cb %u, offset %u, flags %#x\n", cbData, offData, fFlags));

                ASSERT_GUEST_RETURN(cbData <= VMMDEV_MAX_HGCM_DATA_SIZE, VERR_INVALID_PARAMETER);

                /* Check flags and buffer range. */
                ASSERT_GUEST_MSG_RETURN(VBOX_HGCM_F_PARM_ARE_VALID(fFlags), ("%#x\n", fFlags), VERR_INVALID_FLAGS);
                ASSERT_GUEST_MSG_RETURN(   offData >= offExtra
                                        && offData <= cbHGCMCall
                                        && cbData  <= cbHGCMCall - offData,
                                        ("offData=%#x cbData=%#x cbHGCMCall=%#x offExtra=%#x\n", offData, cbData, cbHGCMCall, offExtra),
                                        VERR_INVALID_PARAMETER);
                RT_UNTRUSTED_VALIDATED_FENCE();

                /* We use part of the ptr member. */
                pGuestParm->u.ptr.fu32Direction     = fFlags;
                pGuestParm->u.ptr.cbData            = cbData;
                pGuestParm->u.ptr.offFirstPage      = offData;
                pGuestParm->u.ptr.GCPhysSinglePage  = pCmd->GCPhys + offData;
                pGuestParm->u.ptr.cPages            = 1;
                pGuestParm->u.ptr.paPages           = &pGuestParm->u.ptr.GCPhysSinglePage;
                break;
            }

            default:
                ASSERT_GUEST_FAILED_RETURN(VERR_INVALID_PARAMETER);
        }
    }

    return VINF_SUCCESS;
}

/**
 * Handles VMMDevHGCMCall request.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pHGCMCall       The request to handle (cached in host memory).
 * @param   cbHGCMCall      Size of the entire request (including HGCM parameters).
 * @param   GCPhys          The guest physical address of the request.
 * @param   enmRequestType  The request type. Distinguishes 64 and 32 bit calls.
 * @param   tsArrival       The STAM_GET_TS() value when the request arrived.
 * @param   ppLock          Pointer to the lock info pointer (latter can be
 *                          NULL).  Set to NULL if HGCM takes lock ownership.
 */
int vmmdevR3HgcmCall(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, const VMMDevHGCMCall *pHGCMCall, uint32_t cbHGCMCall,
                     RTGCPHYS GCPhys, VMMDevRequestType enmRequestType, uint64_t tsArrival, PVMMDEVREQLOCK *ppLock)
{
    LogFunc(("client id = %d, function = %d, cParms = %d, enmRequestType = %d, fRequestor = %#x\n", pHGCMCall->u32ClientID,
             pHGCMCall->u32Function, pHGCMCall->cParms, enmRequestType, pHGCMCall->header.header.fRequestor));

    /*
     * Validation.
     */
    ASSERT_GUEST_RETURN(cbHGCMCall >= sizeof(VMMDevHGCMCall), VERR_INVALID_PARAMETER);
#ifdef VBOX_WITH_64_BITS_GUESTS
    ASSERT_GUEST_RETURN(   enmRequestType == VMMDevReq_HGCMCall32
                        || enmRequestType == VMMDevReq_HGCMCall64, VERR_INVALID_PARAMETER);
#else
    ASSERT_GUEST_RETURN(enmRequestType == VMMDevReq_HGCMCall32, VERR_INVALID_PARAMETER);
#endif
    RT_UNTRUSTED_VALIDATED_FENCE();

    /*
     * Create a command structure.
     */
    PVBOXHGCMCMD pCmd;
    uint32_t cbHGCMParmStruct;
    int rc = vmmdevR3HgcmCallAlloc(pThisCC, pHGCMCall, cbHGCMCall, GCPhys, enmRequestType, &pCmd, &cbHGCMParmStruct);
    if (RT_SUCCESS(rc))
    {
        pCmd->tsArrival = tsArrival;
        PVMMDEVREQLOCK pLock = *ppLock;
        if (pLock)
        {
            pCmd->ReqMapLock  = pLock->Lock;
            pCmd->pvReqLocked = pLock->pvReq;
            *ppLock = NULL;
        }

        rc = vmmdevR3HgcmCallFetchGuestParms(pDevIns, pThisCC, pCmd, pHGCMCall, cbHGCMCall, enmRequestType, cbHGCMParmStruct);
        if (RT_SUCCESS(rc))
        {
            /* Copy guest data to host parameters, so HGCM services can use the data. */
            rc = vmmdevR3HgcmInitHostParameters(pDevIns, pThisCC, pCmd, (uint8_t const *)pHGCMCall);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Pass the function call to HGCM connector for actual processing
                 */
                vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);

#if 0 /* DONT ENABLE - for performance hacking. */
                if (    pCmd->u.call.u32Function == 9
                    &&  pCmd->u.call.cParms      == 5)
                {
                    vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);

                    if (pCmd->pvReqLocked)
                    {
                        VMMDevHGCMRequestHeader volatile *pHeader = (VMMDevHGCMRequestHeader volatile *)pCmd->pvReqLocked;
                        pHeader->header.rc = VINF_SUCCESS;
                        pHeader->result    = VINF_SUCCESS;
                        pHeader->fu32Flags |= VBOX_HGCM_REQ_DONE;
                    }
                    else
                    {
                        VMMDevHGCMRequestHeader *pHeader = (VMMDevHGCMRequestHeader *)pHGCMCall;
                        pHeader->header.rc = VINF_SUCCESS;
                        pHeader->result    = VINF_SUCCESS;
                        pHeader->fu32Flags |= VBOX_HGCM_REQ_DONE;
                        PDMDevHlpPhysWrite(pDevIns, GCPhys, pHeader,  sizeof(*pHeader));
                    }
                    vmmdevR3HgcmCmdFree(pDevIns, pThisCC, pCmd);
                    return VINF_HGCM_ASYNC_EXECUTE; /* ignored, but avoids assertions. */
                }
#endif

                rc = pThisCC->pHGCMDrv->pfnCall(pThisCC->pHGCMDrv, pCmd,
                                                pCmd->u.call.u32ClientID, pCmd->u.call.u32Function,
                                                pCmd->u.call.cParms, pCmd->u.call.paHostParms, tsArrival);

                if (rc == VINF_HGCM_ASYNC_EXECUTE)
                {
                    /*
                     * Done.  Just update statistics and return.
                     */
#ifndef VBOX_WITHOUT_RELEASE_STATISTICS
                    uint64_t tsNow;
                    STAM_GET_TS(tsNow);
                    STAM_REL_PROFILE_ADD_PERIOD(&pThisCC->StatHgcmCmdArrival, tsNow - tsArrival);
#endif
                    return rc;
                }

                /*
                 * Failed, bail out.
                 */
                LogFunc(("pfnCall rc = %Rrc\n", rc));
                vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);
            }
        }
        vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);
    }
    return rc;
}

/**
 * VMMDevReq_HGCMCancel worker.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pHGCMCancel     The request to handle (cached in host memory).
 * @param   GCPhys          The address of the request.
 *
 * @thread EMT
 */
int vmmdevR3HgcmCancel(PVMMDEVCC pThisCC, const VMMDevHGCMCancel *pHGCMCancel, RTGCPHYS GCPhys)
{
    NOREF(pHGCMCancel);
    int rc = vmmdevR3HgcmCancel2(pThisCC, GCPhys);
    return rc == VERR_NOT_FOUND ? VERR_INVALID_PARAMETER : rc;
}

/**
 * VMMDevReq_HGCMCancel2 worker.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_FOUND if the request was not found.
 * @retval  VERR_INVALID_PARAMETER if the request address is invalid.
 *
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   GCPhys          The address of the request that should be cancelled.
 *
 * @thread EMT
 */
int vmmdevR3HgcmCancel2(PVMMDEVCC pThisCC, RTGCPHYS GCPhys)
{
    if (    GCPhys == 0
        ||  GCPhys == NIL_RTGCPHYS
        ||  GCPhys == NIL_RTGCPHYS32)
    {
        Log(("vmmdevR3HgcmCancel2: GCPhys=%#x\n", GCPhys));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Locate the command and cancel it while under the protection of
     * the lock. hgcmCompletedWorker makes assumptions about this.
     */
    int rc = vmmdevR3HgcmCmdListLock(pThisCC);
    AssertRCReturn(rc, rc);

    PVBOXHGCMCMD pCmd = vmmdevR3HgcmFindCommandLocked(pThisCC, GCPhys);
    if (pCmd)
    {
        pCmd->fCancelled = true;

        Log(("vmmdevR3HgcmCancel2: Cancelled pCmd=%p / GCPhys=%#x\n", pCmd, GCPhys));
        if (pThisCC->pHGCMDrv)
            pThisCC->pHGCMDrv->pfnCancelled(pThisCC->pHGCMDrv, pCmd,
                                            pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL ? pCmd->u.call.u32ClientID
                                            : pCmd->enmCmdType == VBOXHGCMCMDTYPE_CONNECT ? pCmd->u.connect.u32ClientID
                                            : pCmd->enmCmdType == VBOXHGCMCMDTYPE_DISCONNECT ? pCmd->u.disconnect.u32ClientID
                                            : 0);
    }
    else
        rc = VERR_NOT_FOUND;

    vmmdevR3HgcmCmdListUnlock(pThisCC);
    return rc;
}

/** Write HGCM call parameters and buffers back to the guest request and memory.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pCmd            Completed call command.
 * @param   pHGCMCall       The guestrequest which needs updating (cached in the host memory).
 * @param   pbReq           The request copy or locked memory for handling
 *                          embedded buffers.
 */
static int vmmdevR3HgcmCompleteCallRequest(PPDMDEVINS pDevIns, PVBOXHGCMCMD pCmd, VMMDevHGCMCall *pHGCMCall, uint8_t *pbReq)
{
    AssertReturn(pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL, VERR_INTERNAL_ERROR);

    /*
     * Go over parameter descriptions saved in pCmd.
     */
#ifdef VBOX_WITH_64_BITS_GUESTS
    HGCMFunctionParameter64 *pReqParm         = (HGCMFunctionParameter64 *)(pbReq + sizeof(VMMDevHGCMCall));
    size_t const             cbHGCMParmStruct = pCmd->enmRequestType == VMMDevReq_HGCMCall64
                                              ? sizeof(HGCMFunctionParameter64) : sizeof(HGCMFunctionParameter32);
#else
    HGCMFunctionParameter   *pReqParm         = (HGCMFunctionParameter   *)(pbReq + sizeof(VMMDevHGCMCall));
    size_t const             cbHGCMParmStruct = sizeof(HGCMFunctionParameter);
#endif
    for (uint32_t i = 0;
         i < pCmd->u.call.cParms;
#ifdef VBOX_WITH_64_BITS_GUESTS
         ++i, pReqParm = (HGCMFunctionParameter64 *)((uint8_t *)pReqParm + cbHGCMParmStruct)
#else
         ++i, pReqParm = (HGCMFunctionParameter   *)((uint8_t *)pReqParm + cbHGCMParmStruct)
#endif
        )
    {
        VBOXHGCMGUESTPARM * const pGuestParm = &pCmd->u.call.paGuestParms[i];
        VBOXHGCMSVCPARM   * const pHostParm  = &pCmd->u.call.paHostParms[i];

        const HGCMFunctionParameterType enmType = pGuestParm->enmType;
        switch (enmType)
        {
            case VMMDevHGCMParmType_32bit:
            case VMMDevHGCMParmType_64bit:
            {
                const VBOXHGCMPARMVAL * const pVal = &pGuestParm->u.val;
                const void *pvSrc = enmType == VMMDevHGCMParmType_32bit ? (void *)&pHostParm->u.uint32
                                                                        : (void *)&pHostParm->u.uint64;
/** @todo optimize memcpy away here. */
                memcpy((uint8_t *)pHGCMCall + pVal->offValue, pvSrc, pVal->cbValue);
                break;
            }

            case VMMDevHGCMParmType_LinAddr_In:
            case VMMDevHGCMParmType_LinAddr_Out:
            case VMMDevHGCMParmType_LinAddr:
            case VMMDevHGCMParmType_PageList:
            {
/** @todo Update the return buffer size? */
                const VBOXHGCMPARMPTR * const pPtr = &pGuestParm->u.ptr;
                if (   pPtr->cbData > 0
                    && (pPtr->fu32Direction & VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST))
                {
                    const void *pvSrc = pHostParm->u.pointer.addr;
                    uint32_t cbSrc = pHostParm->u.pointer.size;
                    int rc = vmmdevR3HgcmGuestBufferWrite(pDevIns, pPtr, pvSrc, cbSrc);
                    if (RT_FAILURE(rc))
                        break;
                }
                break;
            }

            case VMMDevHGCMParmType_Embedded:
            {
                const VBOXHGCMPARMPTR * const pPtr = &pGuestParm->u.ptr;

                /* Update size. */
#ifdef VBOX_WITH_64_BITS_GUESTS
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.Embedded.cbData, HGCMFunctionParameter32, u.Embedded.cbData);
#endif
                pReqParm->u.Embedded.cbData = pHostParm->u.pointer.size;

                /* Copy out data. */
                if (   pPtr->cbData > 0
                    && (pPtr->fu32Direction & VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST))
                {
                    const void *pvSrc    = pHostParm->u.pointer.addr;
                    uint32_t    cbSrc    = pHostParm->u.pointer.size;
                    uint32_t    cbToCopy = RT_MIN(cbSrc, pPtr->cbData);
                    memcpy(pbReq + pPtr->offFirstPage, pvSrc, cbToCopy);
                }
                break;
            }

            case VMMDevHGCMParmType_ContiguousPageList:
            {
                const VBOXHGCMPARMPTR * const pPtr = &pGuestParm->u.ptr;

                /* Update size. */
#ifdef VBOX_WITH_64_BITS_GUESTS
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.PageList.size, HGCMFunctionParameter32, u.PageList.size);
#endif
                pReqParm->u.PageList.size = pHostParm->u.pointer.size;

                /* Copy out data. */
                if (   pPtr->cbData > 0
                    && (pPtr->fu32Direction & VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST))
                {
                    const void *pvSrc    = pHostParm->u.pointer.addr;
                    uint32_t    cbSrc    = pHostParm->u.pointer.size;
                    uint32_t    cbToCopy = RT_MIN(cbSrc, pPtr->cbData);
                    int rc = PDMDevHlpPhysWrite(pDevIns, pGuestParm->u.ptr.paPages[0] | pGuestParm->u.ptr.offFirstPage,
                                                pvSrc, cbToCopy);
                    if (RT_FAILURE(rc))
                        break;
                }
                break;
            }

            case VMMDevHGCMParmType_NoBouncePageList:
            {
                /* Update size. */
#ifdef VBOX_WITH_64_BITS_GUESTS
                AssertCompileMembersSameSizeAndOffset(HGCMFunctionParameter64, u.PageList.size, HGCMFunctionParameter32, u.PageList.size);
#endif
                pReqParm->u.PageList.size = pHostParm->u.Pages.cb;

                /* unlock early. */
                if (pGuestParm->u.Pages.fLocked)
                {
                    PDMDevHlpPhysBulkReleasePageMappingLocks(pDevIns, pGuestParm->u.Pages.cPages,
                                                             pGuestParm->u.Pages.paPgLocks);
                    pGuestParm->u.Pages.fLocked = false;
                }
                break;
            }

            default:
                break;
        }
    }

    return VINF_SUCCESS;
}

/** Update HGCM request in the guest memory and mark it as completed.
 *
 * @returns VINF_SUCCESS or VERR_CANCELLED.
 * @param   pInterface      Pointer to this PDM interface.
 * @param   result          HGCM completion status code (VBox status code).
 * @param   pCmd            Completed command, which contains updated host parameters.
 *
 * @thread EMT
 */
static int hgcmCompletedWorker(PPDMIHGCMPORT pInterface, int32_t result, PVBOXHGCMCMD pCmd)
{
    PVMMDEVCC  pThisCC    = RT_FROM_MEMBER(pInterface, VMMDEVCC, IHGCMPort);
    PPDMDEVINS pDevIns    = pThisCC->pDevIns;
    PVMMDEV    pThis      = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
#ifdef VBOX_WITH_DTRACE
    uint32_t   idFunction = 0;
    uint32_t   idClient   = 0;
#endif

    if (result == VINF_HGCM_SAVE_STATE)
    {
        /* If the completion routine was called while the HGCM service saves its state,
         * then currently nothing to be done here.  The pCmd stays in the list and will
         * be saved later when the VMMDev state will be saved and re-submitted on load.
         *
         * It it assumed that VMMDev saves state after the HGCM services (VMMDev driver
         * attached by constructor before it registers its SSM state), and, therefore,
         * VBOXHGCMCMD structures are not removed by vmmdevR3HgcmSaveState from the list,
         * while HGCM uses them.
         */
        LogFlowFunc(("VINF_HGCM_SAVE_STATE for command %p\n", pCmd));
        return VINF_SUCCESS;
    }

    VBOXDD_HGCMCALL_COMPLETED_EMT(pCmd, result);

    int rc = VINF_SUCCESS;

    /*
     * The cancellation protocol requires us to remove the command here
     * and then check the flag. Cancelled commands must not be written
     * back to guest memory.
     */
    vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);

    if (RT_LIKELY(!pCmd->fCancelled))
    {
        if (!pCmd->pvReqLocked)
        {
            /*
             * Request is not locked:
             */
            VMMDevHGCMRequestHeader *pHeader = (VMMDevHGCMRequestHeader *)RTMemAlloc(pCmd->cbRequest);
            if (pHeader)
            {
                /*
                 * Read the request from the guest memory for updating.
                 * The request data is not be used for anything but checking the request type.
                 */
                PDMDevHlpPhysRead(pDevIns, pCmd->GCPhys, pHeader, pCmd->cbRequest);
                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

                /* Verify the request type. This is the only field which is used from the guest memory. */
                const VMMDevRequestType enmRequestType = pHeader->header.requestType;
                if (   enmRequestType == pCmd->enmRequestType
                    || enmRequestType == VMMDevReq_HGCMCancel)
                {
                    RT_UNTRUSTED_VALIDATED_FENCE();

                    /*
                     * Update parameters and data buffers.
                     */
                    switch (enmRequestType)
                    {
#ifdef VBOX_WITH_64_BITS_GUESTS
                        case VMMDevReq_HGCMCall64:
#endif
                        case VMMDevReq_HGCMCall32:
                        {
                            VMMDevHGCMCall *pHGCMCall = (VMMDevHGCMCall *)pHeader;
                            rc = vmmdevR3HgcmCompleteCallRequest(pDevIns, pCmd, pHGCMCall, (uint8_t *)pHeader);
#ifdef VBOX_WITH_DTRACE
                            idFunction = pCmd->u.call.u32Function;
                            idClient   = pCmd->u.call.u32ClientID;
#endif
                            break;
                        }

                        case VMMDevReq_HGCMConnect:
                        {
                            /* save the client id in the guest request packet */
                            VMMDevHGCMConnect *pHGCMConnect = (VMMDevHGCMConnect *)pHeader;
                            pHGCMConnect->u32ClientID = pCmd->u.connect.u32ClientID;
                            break;
                        }

                        default:
                            /* make compiler happy */
                            break;
                    }
                }
                else
                {
                    /* Guest has changed the command type. */
                    LogRelMax(50, ("VMMDEV: Invalid HGCM command: pCmd->enmCmdType = 0x%08X, pHeader->header.requestType = 0x%08X\n",
                                   pCmd->enmCmdType, pHeader->header.requestType));

                    ASSERT_GUEST_FAILED_STMT(rc = VERR_INVALID_PARAMETER);
                }

                /* Setup return code for the guest. */
                if (RT_SUCCESS(rc))
                    pHeader->result = result;
                else
                    pHeader->result = rc;

                /* First write back the request. */
                PDMDevHlpPhysWrite(pDevIns, pCmd->GCPhys, pHeader, pCmd->cbRequest);

                /* Mark request as processed. */
                pHeader->fu32Flags |= VBOX_HGCM_REQ_DONE;

                /* Second write the flags to mark the request as processed. */
                PDMDevHlpPhysWrite(pDevIns, pCmd->GCPhys + RT_UOFFSETOF(VMMDevHGCMRequestHeader, fu32Flags),
                                   &pHeader->fu32Flags, sizeof(pHeader->fu32Flags));

                /* Now, when the command was removed from the internal list, notify the guest. */
                VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_HGCM);

                RTMemFreeZ(pHeader, pCmd->cbRequest);
            }
            else
            {
                LogRelMax(10, ("VMMDev: Failed to allocate %u bytes for HGCM request completion!!!\n", pCmd->cbRequest));
            }
        }
        /*
         * Request was locked:
         */
        else
        {
            VMMDevHGCMRequestHeader volatile *pHeader = (VMMDevHGCMRequestHeader volatile *)pCmd->pvReqLocked;

            /* Verify the request type. This is the only field which is used from the guest memory. */
            const VMMDevRequestType enmRequestType = pHeader->header.requestType;
            if (   enmRequestType == pCmd->enmRequestType
                || enmRequestType == VMMDevReq_HGCMCancel)
            {
                RT_UNTRUSTED_VALIDATED_FENCE();

                /*
                 * Update parameters and data buffers.
                 */
                switch (enmRequestType)
                {
#ifdef VBOX_WITH_64_BITS_GUESTS
                    case VMMDevReq_HGCMCall64:
#endif
                    case VMMDevReq_HGCMCall32:
                    {
                        VMMDevHGCMCall *pHGCMCall = (VMMDevHGCMCall *)pHeader;
                        rc = vmmdevR3HgcmCompleteCallRequest(pDevIns, pCmd, pHGCMCall, (uint8_t *)pHeader);
#ifdef VBOX_WITH_DTRACE
                        idFunction = pCmd->u.call.u32Function;
                        idClient   = pCmd->u.call.u32ClientID;
#endif
                        break;
                    }

                    case VMMDevReq_HGCMConnect:
                    {
                        /* save the client id in the guest request packet */
                        VMMDevHGCMConnect *pHGCMConnect = (VMMDevHGCMConnect *)pHeader;
                        pHGCMConnect->u32ClientID = pCmd->u.connect.u32ClientID;
                        break;
                    }

                    default:
                        /* make compiler happy */
                        break;
                }
            }
            else
            {
                /* Guest has changed the command type. */
                LogRelMax(50, ("VMMDEV: Invalid HGCM command: pCmd->enmCmdType = 0x%08X, pHeader->header.requestType = 0x%08X\n",
                               pCmd->enmCmdType, pHeader->header.requestType));

                ASSERT_GUEST_FAILED_STMT(rc = VERR_INVALID_PARAMETER);
            }

            /* Setup return code for the guest. */
            if (RT_SUCCESS(rc))
                pHeader->result = result;
            else
                pHeader->result = rc;

            /* Mark request as processed. */
            ASMAtomicOrU32(&pHeader->fu32Flags, VBOX_HGCM_REQ_DONE);

            /* Now, when the command was removed from the internal list, notify the guest. */
            VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_HGCM);
        }

        /* Set the status to success for now, though we might consider passing
           along the vmmdevR3HgcmCompleteCallRequest errors... */
        rc = VINF_SUCCESS;
    }
    else
    {
        LogFlowFunc(("Cancelled command %p\n", pCmd));
        rc = VERR_CANCELLED;
    }

#ifndef VBOX_WITHOUT_RELEASE_STATISTICS
    /* Save for final stats. */
    uint64_t const tsArrival = pCmd->tsArrival;
    uint64_t const tsComplete = pCmd->tsComplete;
#endif

    /* Deallocate the command memory. Enter the critsect for proper  */
    VBOXDD_HGCMCALL_COMPLETED_DONE(pCmd, idFunction, idClient, result);
    vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);

#ifndef VBOX_WITHOUT_RELEASE_STATISTICS
    /* Update stats. */
    uint64_t tsNow;
    STAM_GET_TS(tsNow);
    STAM_REL_PROFILE_ADD_PERIOD(&pThisCC->StatHgcmCmdCompletion, tsNow - tsComplete);
    if (tsArrival != 0)
        STAM_REL_PROFILE_ADD_PERIOD(&pThisCC->StatHgcmCmdTotal,  tsNow - tsArrival);
#endif

    return rc;
}

/**
 * HGCM callback for request completion. Forwards to hgcmCompletedWorker.
 *
 * @returns VINF_SUCCESS or VERR_CANCELLED.
 * @param   pInterface      Pointer to this PDM interface.
 * @param   result          HGCM completion status code (VBox status code).
 * @param   pCmd            Completed command, which contains updated host parameters.
 */
DECLCALLBACK(int) hgcmR3Completed(PPDMIHGCMPORT pInterface, int32_t result, PVBOXHGCMCMD pCmd)
{
#if 0 /* This seems to be significantly slower.  Half of MsgTotal time seems to be spend here. */
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IHGCMPort);
    STAM_GET_TS(pCmd->tsComplete);

    VBOXDD_HGCMCALL_COMPLETED_REQ(pCmd, result);

/** @todo no longer necessary to forward to EMT, but it might be more
 *        efficient...? */
    /* Not safe to execute asynchronously; forward to EMT */
    int rc = VMR3ReqCallVoidNoWait(PDMDevHlpGetVM(pDevIns), VMCPUID_ANY,
                                   (PFNRT)hgcmCompletedWorker, 3, pInterface, result, pCmd);
    AssertRC(rc);
    return VINF_SUCCESS; /* cannot tell if canceled or not... */
#else
    STAM_GET_TS(pCmd->tsComplete);
    VBOXDD_HGCMCALL_COMPLETED_REQ(pCmd, result);
    return hgcmCompletedWorker(pInterface, result, pCmd);
#endif
}

/**
 * @interface_method_impl{PDMIHGCMPORT,pfnIsCmdRestored}
 */
DECLCALLBACK(bool) hgcmR3IsCmdRestored(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd)
{
    RT_NOREF(pInterface);
    return pCmd && pCmd->fRestored;
}

/**
 * @interface_method_impl{PDMIHGCMPORT,pfnIsCmdCancelled}
 */
DECLCALLBACK(bool) hgcmR3IsCmdCancelled(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd)
{
    RT_NOREF(pInterface);
    return pCmd && pCmd->fCancelled;
}

/**
 * @interface_method_impl{PDMIHGCMPORT,pfnGetRequestor}
 */
DECLCALLBACK(uint32_t) hgcmR3GetRequestor(PPDMIHGCMPORT pInterface, PVBOXHGCMCMD pCmd)
{
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IHGCMPort);
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVMMDEV);
    AssertPtrReturn(pCmd, VMMDEV_REQUESTOR_LOWEST);
    if (pThis->guestInfo2.fFeatures & VBOXGSTINFO2_F_REQUESTOR_INFO)
        return pCmd->fRequestor;
   return VMMDEV_REQUESTOR_LEGACY;
}

/**
 * @interface_method_impl{PDMIHGCMPORT,pfnGetVMMDevSessionId}
 */
DECLCALLBACK(uint64_t) hgcmR3GetVMMDevSessionId(PPDMIHGCMPORT pInterface)
{
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IHGCMPort);
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVMMDEV);
    return pThis->idSession;
}

/** Save information about pending HGCM requests from pThisCC->listHGCMCmd.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pSSM            SSM handle for SSM functions.
 *
 * @thread EMT
 */
int vmmdevR3HgcmSaveState(PVMMDEVCC pThisCC, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pThisCC->pDevIns->pHlpR3;

    LogFlowFunc(("\n"));

    /* Compute how many commands are pending. */
    uint32_t cCmds = 0;
    PVBOXHGCMCMD pCmd;
    RTListForEach(&pThisCC->listHGCMCmd, pCmd, VBOXHGCMCMD, node)
    {
        LogFlowFunc(("pCmd %p\n", pCmd));
        ++cCmds;
    }
    LogFlowFunc(("cCmds = %d\n", cCmds));

    /* Save number of commands. */
    int rc = pHlp->pfnSSMPutU32(pSSM, cCmds);
    AssertRCReturn(rc, rc);

    if (cCmds > 0)
    {
        RTListForEach(&pThisCC->listHGCMCmd, pCmd, VBOXHGCMCMD, node)
        {
            LogFlowFunc(("Saving %RGp, size %d\n", pCmd->GCPhys, pCmd->cbRequest));

            /** @todo Don't save cancelled requests! It serves no purpose.  See restore and
             *        @bugref{4032#c4} for details. */
            pHlp->pfnSSMPutU32     (pSSM, (uint32_t)pCmd->enmCmdType);
            pHlp->pfnSSMPutBool    (pSSM, pCmd->fCancelled);
            pHlp->pfnSSMPutGCPhys  (pSSM, pCmd->GCPhys);
            pHlp->pfnSSMPutU32     (pSSM, pCmd->cbRequest);
            pHlp->pfnSSMPutU32     (pSSM, (uint32_t)pCmd->enmRequestType);
            const uint32_t cParms = pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL ? pCmd->u.call.cParms : 0;
            rc = pHlp->pfnSSMPutU32(pSSM, cParms);
            AssertRCReturn(rc, rc);

            if (pCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL)
            {
                pHlp->pfnSSMPutU32     (pSSM, pCmd->u.call.u32ClientID);
                rc = pHlp->pfnSSMPutU32(pSSM, pCmd->u.call.u32Function);
                AssertRCReturn(rc, rc);

                /* Guest parameters. */
                uint32_t i;
                for (i = 0; i < pCmd->u.call.cParms; ++i)
                {
                    VBOXHGCMGUESTPARM * const pGuestParm = &pCmd->u.call.paGuestParms[i];

                    rc = pHlp->pfnSSMPutU32(pSSM, (uint32_t)pGuestParm->enmType);
                    AssertRCReturn(rc, rc);

                    if (   pGuestParm->enmType == VMMDevHGCMParmType_32bit
                        || pGuestParm->enmType == VMMDevHGCMParmType_64bit)
                    {
                        const VBOXHGCMPARMVAL * const pVal = &pGuestParm->u.val;
                        pHlp->pfnSSMPutU64     (pSSM, pVal->u64Value);
                        pHlp->pfnSSMPutU32     (pSSM, pVal->offValue);
                        rc = pHlp->pfnSSMPutU32(pSSM, pVal->cbValue);
                    }
                    else if (   pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_In
                             || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_Out
                             || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr
                             || pGuestParm->enmType == VMMDevHGCMParmType_PageList
                             || pGuestParm->enmType == VMMDevHGCMParmType_Embedded
                             || pGuestParm->enmType == VMMDevHGCMParmType_ContiguousPageList)
                    {
                        const VBOXHGCMPARMPTR * const pPtr = &pGuestParm->u.ptr;
                        pHlp->pfnSSMPutU32     (pSSM, pPtr->cbData);
                        pHlp->pfnSSMPutU32     (pSSM, pPtr->offFirstPage);
                        pHlp->pfnSSMPutU32     (pSSM, pPtr->cPages);
                        rc = pHlp->pfnSSMPutU32(pSSM, pPtr->fu32Direction);

                        uint32_t iPage;
                        for (iPage = 0; RT_SUCCESS(rc) && iPage < pPtr->cPages; ++iPage)
                            rc = pHlp->pfnSSMPutGCPhys(pSSM, pPtr->paPages[iPage]);
                    }
                    else if (pGuestParm->enmType == VMMDevHGCMParmType_NoBouncePageList)
                    {
                        /* We don't have the page addresses here, so it will need to be
                           restored from guest memory.  This isn't an issue as it is only
                           use with services which won't survive a save/restore anyway. */
                    }
                    else
                    {
                        AssertFailedStmt(rc = VERR_INTERNAL_ERROR);
                    }
                    AssertRCReturn(rc, rc);
                }
            }
            else if (pCmd->enmCmdType == VBOXHGCMCMDTYPE_CONNECT)
            {
                pHlp->pfnSSMPutU32(pSSM, pCmd->u.connect.u32ClientID);
                pHlp->pfnSSMPutMem(pSSM, pCmd->u.connect.pLoc, sizeof(*pCmd->u.connect.pLoc));
            }
            else if (pCmd->enmCmdType == VBOXHGCMCMDTYPE_DISCONNECT)
            {
                pHlp->pfnSSMPutU32(pSSM, pCmd->u.disconnect.u32ClientID);
            }
            else
            {
                AssertFailedReturn(VERR_INTERNAL_ERROR);
            }

            /* A reserved field, will allow to extend saved data for a command. */
            rc = pHlp->pfnSSMPutU32(pSSM, 0);
            AssertRCReturn(rc, rc);
        }
    }

    /* A reserved field, will allow to extend saved data for VMMDevHGCM. */
    rc = pHlp->pfnSSMPutU32(pSSM, 0);
    AssertRCReturn(rc, rc);

    return rc;
}

/** Load information about pending HGCM requests.
 *
 * Allocate VBOXHGCMCMD commands and add them to pThisCC->listHGCMCmd
 * temporarily. vmmdevR3HgcmLoadStateDone will process the temporary list.  This
 * includes loading the correct fRequestor fields.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pSSM            SSM handle for SSM functions.
 * @param   uVersion        Saved state version.
 *
 * @thread EMT
 */
int vmmdevR3HgcmLoadState(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, PSSMHANDLE pSSM, uint32_t uVersion)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    LogFlowFunc(("\n"));

    pThisCC->uSavedStateVersion = uVersion; /* For vmmdevR3HgcmLoadStateDone */

    /* Read how many commands were pending. */
    uint32_t cCmds = 0;
    int rc = pHlp->pfnSSMGetU32(pSSM, &cCmds);
    AssertRCReturn(rc, rc);

    LogFlowFunc(("cCmds = %d\n", cCmds));

    if (uVersion >= VMMDEV_SAVED_STATE_VERSION_HGCM_PARAMS)
    {
        /* Saved information about all HGCM parameters. */
        uint32_t u32;

        uint32_t iCmd;
        for (iCmd = 0; iCmd < cCmds; ++iCmd)
        {
            /* Command fields. */
            VBOXHGCMCMDTYPE   enmCmdType;
            bool              fCancelled;
            RTGCPHYS          GCPhys;
            uint32_t          cbRequest;
            VMMDevRequestType enmRequestType;
            uint32_t          cParms;

            pHlp->pfnSSMGetU32     (pSSM, &u32);
            enmCmdType = (VBOXHGCMCMDTYPE)u32;
            pHlp->pfnSSMGetBool    (pSSM, &fCancelled);
            pHlp->pfnSSMGetGCPhys  (pSSM, &GCPhys);
            pHlp->pfnSSMGetU32     (pSSM, &cbRequest);
            pHlp->pfnSSMGetU32     (pSSM, &u32);
            enmRequestType = (VMMDevRequestType)u32;
            rc = pHlp->pfnSSMGetU32(pSSM, &cParms);
            AssertRCReturn(rc, rc);

            PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, enmCmdType, GCPhys, cbRequest, cParms, 0 /*fRequestor*/);
            AssertReturn(pCmd, VERR_NO_MEMORY);

            pCmd->fCancelled     = fCancelled;
            pCmd->GCPhys         = GCPhys;
            pCmd->cbRequest      = cbRequest;
            pCmd->enmRequestType = enmRequestType;

            if (enmCmdType == VBOXHGCMCMDTYPE_CALL)
            {
                pHlp->pfnSSMGetU32     (pSSM, &pCmd->u.call.u32ClientID);
                rc = pHlp->pfnSSMGetU32(pSSM, &pCmd->u.call.u32Function);
                AssertRCReturn(rc, rc);

                /* Guest parameters. */
                uint32_t i;
                for (i = 0; i < cParms; ++i)
                {
                    VBOXHGCMGUESTPARM * const pGuestParm = &pCmd->u.call.paGuestParms[i];

                    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
                    AssertRCReturn(rc, rc);
                    pGuestParm->enmType = (HGCMFunctionParameterType)u32;

                    if (   pGuestParm->enmType == VMMDevHGCMParmType_32bit
                        || pGuestParm->enmType == VMMDevHGCMParmType_64bit)
                    {
                        VBOXHGCMPARMVAL * const pVal = &pGuestParm->u.val;
                        pHlp->pfnSSMGetU64     (pSSM, &pVal->u64Value);
                        pHlp->pfnSSMGetU32     (pSSM, &pVal->offValue);
                        rc = pHlp->pfnSSMGetU32(pSSM, &pVal->cbValue);
                    }
                    else if (   pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_In
                             || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_Out
                             || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr
                             || pGuestParm->enmType == VMMDevHGCMParmType_PageList
                             || pGuestParm->enmType == VMMDevHGCMParmType_Embedded
                             || pGuestParm->enmType == VMMDevHGCMParmType_ContiguousPageList)
                    {
                        VBOXHGCMPARMPTR * const pPtr = &pGuestParm->u.ptr;
                        pHlp->pfnSSMGetU32     (pSSM, &pPtr->cbData);
                        pHlp->pfnSSMGetU32     (pSSM, &pPtr->offFirstPage);
                        pHlp->pfnSSMGetU32     (pSSM, &pPtr->cPages);
                        rc = pHlp->pfnSSMGetU32(pSSM, &pPtr->fu32Direction);
                        if (RT_SUCCESS(rc))
                        {
                            if (pPtr->cPages == 1)
                                pPtr->paPages = &pPtr->GCPhysSinglePage;
                            else
                            {
                                AssertReturn(   pGuestParm->enmType != VMMDevHGCMParmType_Embedded
                                             && pGuestParm->enmType != VMMDevHGCMParmType_ContiguousPageList, VERR_INTERNAL_ERROR_3);
                                pPtr->paPages = (RTGCPHYS *)vmmdevR3HgcmCallMemAlloc(pThisCC, pCmd,
                                                                                     pPtr->cPages * sizeof(RTGCPHYS));
                                AssertStmt(pPtr->paPages, rc = VERR_NO_MEMORY);
                            }

                            if (RT_SUCCESS(rc))
                            {
                                uint32_t iPage;
                                for (iPage = 0; iPage < pPtr->cPages; ++iPage)
                                    rc = pHlp->pfnSSMGetGCPhys(pSSM, &pPtr->paPages[iPage]);
                            }
                        }
                    }
                    else if (pGuestParm->enmType == VMMDevHGCMParmType_NoBouncePageList)
                    {
                        /* This request type can only be stored from guest memory for now. */
                        pCmd->fRestoreFromGuestMem = true;
                    }
                    else
                    {
                        AssertFailedStmt(rc = VERR_INTERNAL_ERROR);
                    }
                    AssertRCReturn(rc, rc);
                }
            }
            else if (enmCmdType == VBOXHGCMCMDTYPE_CONNECT)
            {
                pHlp->pfnSSMGetU32(pSSM, &pCmd->u.connect.u32ClientID);
                rc = pHlp->pfnSSMGetMem(pSSM, pCmd->u.connect.pLoc, sizeof(*pCmd->u.connect.pLoc));
                AssertRCReturn(rc, rc);
            }
            else if (enmCmdType == VBOXHGCMCMDTYPE_DISCONNECT)
            {
                rc = pHlp->pfnSSMGetU32(pSSM, &pCmd->u.disconnect.u32ClientID);
                AssertRCReturn(rc, rc);
            }
            else
            {
                AssertFailedReturn(VERR_INTERNAL_ERROR);
            }

            /* A reserved field, will allow to extend saved data for a command. */
            rc = pHlp->pfnSSMGetU32(pSSM, &u32);
            AssertRCReturn(rc, rc);

            /*
             * Do not restore cancelled calls.  Why do we save them to start with?
             *
             * The guest memory no longer contains a valid request!  So, it is not
             * possible to restore it.  The memory is often reused for a new request
             * by now and we will end up trying to complete that more than once if
             * we restore a cancelled call.  In some cases VERR_HGCM_INVALID_CLIENT_ID
             * is returned, though it might just be silent memory corruption.
             */
            /* See current version above. */
            if (!fCancelled)
                vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);
            else
            {
                Log(("vmmdevR3HgcmLoadState: Skipping cancelled request: enmCmdType=%d GCPhys=%#RX32 LB %#x\n",
                     enmCmdType, GCPhys, cbRequest));
                vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);
            }
        }

        /* A reserved field, will allow to extend saved data for VMMDevHGCM. */
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
    }
    else if (uVersion >= 9)
    {
        /* Version 9+: Load information about commands. Pre-rewrite. */
        uint32_t u32;

        uint32_t iCmd;
        for (iCmd = 0; iCmd < cCmds; ++iCmd)
        {
            VBOXHGCMCMDTYPE   enmCmdType;
            bool              fCancelled;
            RTGCPHYS          GCPhys;
            uint32_t          cbRequest;
            uint32_t          cLinAddrs;

            pHlp->pfnSSMGetGCPhys  (pSSM, &GCPhys);
            rc = pHlp->pfnSSMGetU32(pSSM, &cbRequest);
            AssertRCReturn(rc, rc);

            LogFlowFunc(("Restoring %RGp size %x bytes\n", GCPhys, cbRequest));

            /* For uVersion <= 12, this was the size of entire command.
             * Now the command is reconstructed in vmmdevR3HgcmLoadStateDone.
             */
            if (uVersion <= 12)
                pHlp->pfnSSMSkip(pSSM, sizeof (uint32_t));

            pHlp->pfnSSMGetU32     (pSSM, &u32);
            enmCmdType = (VBOXHGCMCMDTYPE)u32;
            pHlp->pfnSSMGetBool    (pSSM, &fCancelled);
            /* How many linear pointers. Always 0 if not a call command. */
            rc = pHlp->pfnSSMGetU32(pSSM, &cLinAddrs);
            AssertRCReturn(rc, rc);

            PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, enmCmdType, GCPhys, cbRequest, cLinAddrs, 0 /*fRequestor*/);
            AssertReturn(pCmd, VERR_NO_MEMORY);

            pCmd->fCancelled = fCancelled;
            pCmd->GCPhys     = GCPhys;
            pCmd->cbRequest  = cbRequest;

            if (cLinAddrs > 0)
            {
                /* Skip number of pages for all LinAddrs in this command. */
                pHlp->pfnSSMSkip(pSSM, sizeof(uint32_t));

                uint32_t i;
                for (i = 0; i < cLinAddrs; ++i)
                {
                    VBOXHGCMPARMPTR * const pPtr = &pCmd->u.call.paGuestParms[i].u.ptr;

                    /* Index of the parameter. Use cbData field to store the index. */
                    pHlp->pfnSSMGetU32     (pSSM, &pPtr->cbData);
                    pHlp->pfnSSMGetU32     (pSSM, &pPtr->offFirstPage);
                    rc = pHlp->pfnSSMGetU32(pSSM, &pPtr->cPages);
                    AssertRCReturn(rc, rc);

                    pPtr->paPages = (RTGCPHYS *)vmmdevR3HgcmCallMemAlloc(pThisCC, pCmd, pPtr->cPages * sizeof(RTGCPHYS));
                    AssertReturn(pPtr->paPages, VERR_NO_MEMORY);

                    uint32_t iPage;
                    for (iPage = 0; iPage < pPtr->cPages; ++iPage)
                        rc = pHlp->pfnSSMGetGCPhys(pSSM, &pPtr->paPages[iPage]);
                }
            }

            /* A reserved field, will allow to extend saved data for a command. */
            rc = pHlp->pfnSSMGetU32(pSSM, &u32);
            AssertRCReturn(rc, rc);

            /* See current version above. */
            if (!fCancelled)
                vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);
            else
            {
                Log(("vmmdevR3HgcmLoadState: Skipping cancelled request: enmCmdType=%d GCPhys=%#RX32 LB %#x\n",
                     enmCmdType, GCPhys, cbRequest));
                vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);
            }
        }

        /* A reserved field, will allow to extend saved data for VMMDevHGCM. */
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
    }
    else
    {
        /* Ancient. Only the guest physical address is saved. */
        uint32_t iCmd;
        for (iCmd = 0; iCmd < cCmds; ++iCmd)
        {
            RTGCPHYS GCPhys;
            uint32_t cbRequest;

            pHlp->pfnSSMGetGCPhys(pSSM, &GCPhys);
            rc = pHlp->pfnSSMGetU32(pSSM, &cbRequest);
            AssertRCReturn(rc, rc);

            LogFlowFunc(("Restoring %RGp size %x bytes\n", GCPhys, cbRequest));

            PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, VBOXHGCMCMDTYPE_LOADSTATE, GCPhys, cbRequest, 0, 0 /*fRequestor*/);
            AssertReturn(pCmd, VERR_NO_MEMORY);

            vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);
        }
    }

    return rc;
}

/** Restore HGCM connect command loaded from old saved state.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   uSavedStateVersion   The saved state version the command has been loaded from.
 * @param   pLoadedCmd      Command loaded from saved state, it is imcomplete and needs restoration.
 * @param   pReq            The guest request (cached in host memory).
 * @param   cbReq           Size of the guest request.
 * @param   enmRequestType  Type of the HGCM request.
 * @param   ppRestoredCmd   Where to store pointer to newly allocated restored command.
 */
static int vmmdevR3HgcmRestoreConnect(PVMMDEVCC pThisCC, uint32_t uSavedStateVersion, const VBOXHGCMCMD *pLoadedCmd,
                                      VMMDevHGCMConnect *pReq, uint32_t cbReq, VMMDevRequestType enmRequestType,
                                      VBOXHGCMCMD **ppRestoredCmd)
{
    /* Verify the request.  */
    ASSERT_GUEST_RETURN(cbReq >= sizeof(*pReq), VERR_MISMATCH);
    if (uSavedStateVersion >= 9)
        ASSERT_GUEST_RETURN(pLoadedCmd->enmCmdType == VBOXHGCMCMDTYPE_CONNECT, VERR_MISMATCH);

    PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, VBOXHGCMCMDTYPE_CONNECT, pLoadedCmd->GCPhys, cbReq, 0,
                                             pReq->header.header.fRequestor);
    AssertReturn(pCmd, VERR_NO_MEMORY);

    Assert(pLoadedCmd->fCancelled == false);
    pCmd->fCancelled = false;
    pCmd->fRestored      = true;
    pCmd->enmRequestType = enmRequestType;

    vmmdevR3HgcmConnectFetch(pReq, pCmd);

    *ppRestoredCmd = pCmd;
    return VINF_SUCCESS;
}

/** Restore HGCM disconnect command loaded from old saved state.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   uSavedStateVersion   The saved state version the command has been loaded from.
 * @param   pLoadedCmd      Command loaded from saved state, it is imcomplete and needs restoration.
 * @param   pReq            The guest request (cached in host memory).
 * @param   cbReq           Size of the guest request.
 * @param   enmRequestType  Type of the HGCM request.
 * @param   ppRestoredCmd   Where to store pointer to newly allocated restored command.
 */
static int vmmdevR3HgcmRestoreDisconnect(PVMMDEVCC pThisCC, uint32_t uSavedStateVersion, const VBOXHGCMCMD *pLoadedCmd,
                                         VMMDevHGCMDisconnect *pReq, uint32_t cbReq, VMMDevRequestType enmRequestType,
                                         VBOXHGCMCMD **ppRestoredCmd)
{
    /* Verify the request.  */
    ASSERT_GUEST_RETURN(cbReq >= sizeof(*pReq), VERR_MISMATCH);
    if (uSavedStateVersion >= 9)
        ASSERT_GUEST_RETURN(pLoadedCmd->enmCmdType == VBOXHGCMCMDTYPE_DISCONNECT, VERR_MISMATCH);

    PVBOXHGCMCMD pCmd = vmmdevR3HgcmCmdAlloc(pThisCC, VBOXHGCMCMDTYPE_DISCONNECT, pLoadedCmd->GCPhys, cbReq, 0,
                                             pReq->header.header.fRequestor);
    AssertReturn(pCmd, VERR_NO_MEMORY);

    Assert(pLoadedCmd->fCancelled == false);
    pCmd->fCancelled = false;
    pCmd->fRestored      = true;
    pCmd->enmRequestType = enmRequestType;

    vmmdevR3HgcmDisconnectFetch(pReq, pCmd);

    *ppRestoredCmd = pCmd;
    return VINF_SUCCESS;
}

/** Restore HGCM call command loaded from old saved state.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   uSavedStateVersion   The saved state version the command has been loaded from.
 * @param   pLoadedCmd      Command loaded from saved state, it is imcomplete and needs restoration.
 * @param   pReq            The guest request (cached in host memory).
 * @param   cbReq           Size of the guest request.
 * @param   enmRequestType  Type of the HGCM request.
 * @param   ppRestoredCmd   Where to store pointer to newly allocated restored command.
 */
static int vmmdevR3HgcmRestoreCall(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, uint32_t uSavedStateVersion,
                                   const VBOXHGCMCMD *pLoadedCmd, VMMDevHGCMCall *pReq, uint32_t cbReq,
                                   VMMDevRequestType enmRequestType, VBOXHGCMCMD **ppRestoredCmd)
{
    /* Verify the request.  */
    ASSERT_GUEST_RETURN(cbReq >= sizeof(*pReq), VERR_MISMATCH);
    if (uSavedStateVersion >= 9)
    {
        ASSERT_GUEST_RETURN(pLoadedCmd->enmCmdType == VBOXHGCMCMDTYPE_CALL, VERR_MISMATCH);
        Assert(pLoadedCmd->fCancelled == false);
    }

    PVBOXHGCMCMD pCmd;
    uint32_t cbHGCMParmStruct;
    int rc = vmmdevR3HgcmCallAlloc(pThisCC, pReq, cbReq, pLoadedCmd->GCPhys, enmRequestType, &pCmd, &cbHGCMParmStruct);
    if (RT_FAILURE(rc))
        return rc;

    /* pLoadedCmd is fake, it does not contain actual call parameters. Only pagelists for LinAddr. */
    pCmd->fCancelled = false;
    pCmd->fRestored      = true;
    pCmd->enmRequestType = enmRequestType;

    rc = vmmdevR3HgcmCallFetchGuestParms(pDevIns, pThisCC, pCmd, pReq, cbReq, enmRequestType, cbHGCMParmStruct);
    if (RT_SUCCESS(rc))
    {
        /* Update LinAddr parameters from pLoadedCmd.
         * pLoadedCmd->u.call.cParms is actually the number of LinAddrs, see vmmdevR3HgcmLoadState.
         */
        uint32_t iLinAddr;
        for (iLinAddr = 0; iLinAddr < pLoadedCmd->u.call.cParms; ++iLinAddr)
        {
            VBOXHGCMGUESTPARM * const pLoadedParm = &pLoadedCmd->u.call.paGuestParms[iLinAddr];
            /* pLoadedParm->cbData is actually index of the LinAddr parameter, see vmmdevR3HgcmLoadState. */
            const uint32_t iParm = pLoadedParm->u.ptr.cbData;
            ASSERT_GUEST_STMT_BREAK(iParm < pCmd->u.call.cParms, rc = VERR_MISMATCH);

            VBOXHGCMGUESTPARM * const pGuestParm = &pCmd->u.call.paGuestParms[iParm];
            ASSERT_GUEST_STMT_BREAK(   pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_In
                                    || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr_Out
                                    || pGuestParm->enmType == VMMDevHGCMParmType_LinAddr,
                                    rc = VERR_MISMATCH);
            ASSERT_GUEST_STMT_BREAK(   pLoadedParm->u.ptr.offFirstPage == pGuestParm->u.ptr.offFirstPage
                                    && pLoadedParm->u.ptr.cPages       == pGuestParm->u.ptr.cPages,
                                    rc = VERR_MISMATCH);
            memcpy(pGuestParm->u.ptr.paPages, pLoadedParm->u.ptr.paPages, pGuestParm->u.ptr.cPages * sizeof(RTGCPHYS));
        }
    }

    if (RT_SUCCESS(rc))
        *ppRestoredCmd = pCmd;
    else
        vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);

    return rc;
}

/** Allocate and initialize a HGCM command using the given request (pReqHdr)
 * and command loaded from saved state (pCmd).
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   uSavedStateVersion   Saved state version.
 * @param   pLoadedCmd      HGCM command which needs restoration.
 * @param   pReqHdr         The request (cached in host memory).
 * @param   cbReq           Size of the entire request (including HGCM parameters).
 * @param   ppRestoredCmd   Where to store pointer to restored command.
 */
static int vmmdevR3HgcmRestoreCommand(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, uint32_t uSavedStateVersion,
                                      const VBOXHGCMCMD *pLoadedCmd, const VMMDevHGCMRequestHeader *pReqHdr, uint32_t cbReq,
                                      VBOXHGCMCMD **ppRestoredCmd)
{
    int rc;

    /* Verify the request.  */
    ASSERT_GUEST_RETURN(cbReq >= sizeof(VMMDevHGCMRequestHeader), VERR_MISMATCH);
    ASSERT_GUEST_RETURN(cbReq == pReqHdr->header.size, VERR_MISMATCH);

    const VMMDevRequestType enmRequestType = pReqHdr->header.requestType;
    switch (enmRequestType)
    {
        case VMMDevReq_HGCMConnect:
        {
            VMMDevHGCMConnect *pReq = (VMMDevHGCMConnect *)pReqHdr;
            rc = vmmdevR3HgcmRestoreConnect(pThisCC, uSavedStateVersion, pLoadedCmd, pReq, cbReq, enmRequestType, ppRestoredCmd);
            break;
        }

        case VMMDevReq_HGCMDisconnect:
        {
            VMMDevHGCMDisconnect *pReq = (VMMDevHGCMDisconnect *)pReqHdr;
            rc = vmmdevR3HgcmRestoreDisconnect(pThisCC, uSavedStateVersion, pLoadedCmd, pReq, cbReq, enmRequestType, ppRestoredCmd);
            break;
        }

#ifdef VBOX_WITH_64_BITS_GUESTS
        case VMMDevReq_HGCMCall64:
#endif
        case VMMDevReq_HGCMCall32:
        {
            VMMDevHGCMCall *pReq = (VMMDevHGCMCall *)pReqHdr;
            rc = vmmdevR3HgcmRestoreCall(pDevIns, pThis, pThisCC, uSavedStateVersion, pLoadedCmd,
                                         pReq, cbReq, enmRequestType, ppRestoredCmd);
            break;
        }

        default:
            ASSERT_GUEST_FAILED_RETURN(VERR_MISMATCH);
    }

    return rc;
}

/** Resubmit pending HGCM commands which were loaded form saved state.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 *
 * @thread EMT
 */
int vmmdevR3HgcmLoadStateDone(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC)
{
    /*
     * Resubmit pending HGCM commands to services.
     *
     * pThisCC->pHGCMCmdList contains commands loaded by vmmdevR3HgcmLoadState.
     *
     * Legacy saved states (pre VMMDEV_SAVED_STATE_VERSION_HGCM_PARAMS)
     * do not have enough information about the command parameters,
     * therefore it is necessary to reload at least some data from the
     * guest memory to construct commands.
     *
     * There are two types of legacy saved states which contain:
     * 1) the guest physical address and size of request;
     * 2) additionally page lists for LinAddr parameters.
     *
     * Legacy commands have enmCmdType = VBOXHGCMCMDTYPE_LOADSTATE?
     */

    int rcFunc = VINF_SUCCESS; /* This status code will make the function fail. I.e. VM will not start. */

    /* Get local copy of the list of loaded commands. */
    RTLISTANCHOR listLoadedCommands;
    RTListMove(&listLoadedCommands, &pThisCC->listHGCMCmd);

    /* Resubmit commands. */
    PVBOXHGCMCMD pCmd, pNext;
    RTListForEachSafe(&listLoadedCommands, pCmd, pNext, VBOXHGCMCMD, node)
    {
        int rcCmd = VINF_SUCCESS; /* This status code will make the HGCM command fail for the guest. */

        RTListNodeRemove(&pCmd->node);

        /*
         * Re-read the request from the guest memory.
         * It will be used to:
         *   * reconstruct commands if legacy saved state has been restored;
         *   * report an error to the guest if resubmit failed.
         */
        VMMDevHGCMRequestHeader *pReqHdr = (VMMDevHGCMRequestHeader *)RTMemAlloc(pCmd->cbRequest);
        AssertBreakStmt(pReqHdr, vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd); rcFunc = VERR_NO_MEMORY);

        PDMDevHlpPhysRead(pDevIns, pCmd->GCPhys, pReqHdr, pCmd->cbRequest);
        RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

        if (pThisCC->pHGCMDrv)
        {
            /*
             * Reconstruct legacy commands.
             */
            if (RT_LIKELY(   pThisCC->uSavedStateVersion >= VMMDEV_SAVED_STATE_VERSION_HGCM_PARAMS
                          && !pCmd->fRestoreFromGuestMem))
            { /* likely */ }
            else
            {
                PVBOXHGCMCMD pRestoredCmd = NULL;
                rcCmd = vmmdevR3HgcmRestoreCommand(pDevIns, pThis, pThisCC, pThisCC->uSavedStateVersion, pCmd,
                                                   pReqHdr, pCmd->cbRequest, &pRestoredCmd);
                if (RT_SUCCESS(rcCmd))
                {
                    Assert(pCmd != pRestoredCmd); /* vmmdevR3HgcmRestoreCommand must allocate restored command. */
                    vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);
                    pCmd = pRestoredCmd;
                }
            }

            /* Resubmit commands. */
            if (RT_SUCCESS(rcCmd))
            {
                switch (pCmd->enmCmdType)
                {
                    case VBOXHGCMCMDTYPE_CONNECT:
                    {
                        vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);
                        rcCmd = pThisCC->pHGCMDrv->pfnConnect(pThisCC->pHGCMDrv, pCmd, pCmd->u.connect.pLoc,
                                                              &pCmd->u.connect.u32ClientID);
                        if (RT_FAILURE(rcCmd))
                            vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);
                        break;
                    }

                    case VBOXHGCMCMDTYPE_DISCONNECT:
                    {
                        vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);
                        rcCmd = pThisCC->pHGCMDrv->pfnDisconnect(pThisCC->pHGCMDrv, pCmd, pCmd->u.disconnect.u32ClientID);
                        if (RT_FAILURE(rcCmd))
                            vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);
                        break;
                    }

                    case VBOXHGCMCMDTYPE_CALL:
                    {
                        rcCmd = vmmdevR3HgcmInitHostParameters(pDevIns, pThisCC, pCmd, (uint8_t const *)pReqHdr);
                        if (RT_SUCCESS(rcCmd))
                        {
                            vmmdevR3HgcmAddCommand(pDevIns, pThis, pThisCC, pCmd);

                            /* Pass the function call to HGCM connector for actual processing */
                            uint64_t tsNow;
                            STAM_GET_TS(tsNow);
                            rcCmd = pThisCC->pHGCMDrv->pfnCall(pThisCC->pHGCMDrv, pCmd,
                                                               pCmd->u.call.u32ClientID, pCmd->u.call.u32Function,
                                                               pCmd->u.call.cParms, pCmd->u.call.paHostParms, tsNow);
                            if (RT_FAILURE(rcCmd))
                            {
                                LogFunc(("pfnCall rc = %Rrc\n", rcCmd));
                                vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);
                            }
                        }
                        break;
                    }

                    default:
                        AssertFailedStmt(rcCmd = VERR_INTERNAL_ERROR);
                }
            }
        }
        else
            AssertFailedStmt(rcCmd = VERR_INTERNAL_ERROR);

        if (RT_SUCCESS(rcCmd))
        { /* likely */ }
        else
        {
            /* Return the error to the guest. Guest may try to repeat the call. */
            pReqHdr->result = rcCmd;
            pReqHdr->header.rc = rcCmd;
            pReqHdr->fu32Flags |= VBOX_HGCM_REQ_DONE;

            /* Write back only the header. */
            PDMDevHlpPhysWrite(pDevIns, pCmd->GCPhys, pReqHdr, sizeof(*pReqHdr));

            VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_HGCM);

            /* Deallocate the command memory. */
            vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);
        }

        RTMemFree(pReqHdr);
    }

    if (RT_FAILURE(rcFunc))
    {
        RTListForEachSafe(&listLoadedCommands, pCmd, pNext, VBOXHGCMCMD, node)
        {
            RTListNodeRemove(&pCmd->node);
            vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);
        }
    }

    return rcFunc;
}


/**
 * Counterpart to vmmdevR3HgcmInit().
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 */
void vmmdevR3HgcmDestroy(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC)
{
    LogFlowFunc(("\n"));

    if (RTCritSectIsInitialized(&pThisCC->critsectHGCMCmdList))
    {
        PVBOXHGCMCMD pCmd, pNext;
        RTListForEachSafe(&pThisCC->listHGCMCmd, pCmd, pNext, VBOXHGCMCMD, node)
        {
            vmmdevR3HgcmRemoveCommand(pThisCC, pCmd);
            vmmdevR3HgcmCmdFree(pDevIns, pThis, pThisCC, pCmd);
        }

        RTCritSectDelete(&pThisCC->critsectHGCMCmdList);
    }

    AssertCompile(NIL_RTMEMCACHE == (RTMEMCACHE)0);
    if (pThisCC->hHgcmCmdCache != NIL_RTMEMCACHE)
    {
        RTMemCacheDestroy(pThisCC->hHgcmCmdCache);
        pThisCC->hHgcmCmdCache = NIL_RTMEMCACHE;
    }
}


/**
 * Initializes the HGCM specific state.
 *
 * Keeps VBOXHGCMCMDCACHED and friends local.
 *
 * @returns VBox status code.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 */
int vmmdevR3HgcmInit(PVMMDEVCC pThisCC)
{
    LogFlowFunc(("\n"));

    RTListInit(&pThisCC->listHGCMCmd);

    int rc = RTCritSectInit(&pThisCC->critsectHGCMCmdList);
    AssertLogRelRCReturn(rc, rc);

    rc = RTMemCacheCreate(&pThisCC->hHgcmCmdCache, sizeof(VBOXHGCMCMDCACHED), 64, _1M, NULL, NULL, NULL, 0);
    AssertLogRelRCReturn(rc, rc);

    pThisCC->u32HGCMEnabled = 0;

    return VINF_SUCCESS;
}

