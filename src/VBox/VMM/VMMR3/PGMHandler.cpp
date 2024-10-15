/* $Id: PGMHandler.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM - Page Manager / Monitor, Access Handlers.
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
#define LOG_GROUP LOG_GROUP_PGM
#define VBOX_WITHOUT_PAGING_BIT_FIELDS /* 64-bit bitfields are just asking for trouble. See @bugref{9841} and others. */
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/iom.h>
#include <VBox/sup.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/ssm.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "PGMInline.h"
#include <VBox/dbg.h>

#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/thread.h>
#include <iprt/string.h>
#include <VBox/param.h>
#include <VBox/vmm/hm.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(int) pgmR3HandlerPhysicalOneClear(PPGMPHYSHANDLER pHandler, void *pvUser);
static DECLCALLBACK(int) pgmR3HandlerPhysicalOneSet(PPGMPHYSHANDLER pHandler, void *pvUser);
static DECLCALLBACK(int) pgmR3InfoHandlersPhysicalOne(PPGMPHYSHANDLER pHandler, void *pvUser);



/**
 * @callback_method_impl{FNPGMPHYSHANDLER,
 * Invalid callback entry triggering guru mediation}
 */
DECLCALLBACK(VBOXSTRICTRC) pgmR3HandlerPhysicalHandlerInvalid(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, void *pvPhys,
                                                              void *pvBuf, size_t cbBuf, PGMACCESSTYPE enmAccessType,
                                                              PGMACCESSORIGIN enmOrigin, uint64_t uUser)
{
    RT_NOREF(pVM, pVCpu, GCPhys, pvPhys, pvBuf, cbBuf, enmAccessType, enmOrigin, uUser);
    LogRel(("GCPhys=%RGp cbBuf=%#zx enmAccessType=%d uUser=%#RX64\n", GCPhys, cbBuf, enmAccessType, uUser));
    return VERR_PGM_HANDLER_IPE_1;
}


/**
 * Register a physical page access handler type.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   enmKind     The kind of access handler.
 * @param   fFlags      PGMPHYSHANDLER_F_XXX
 * @param   pfnHandler  Pointer to the ring-3 handler callback.
 * @param   pszDesc     The type description.
 * @param   phType      Where to return the type handle (cross context safe).
 */
VMMR3_INT_DECL(int) PGMR3HandlerPhysicalTypeRegister(PVM pVM, PGMPHYSHANDLERKIND enmKind, uint32_t fFlags,
                                                     PFNPGMPHYSHANDLER pfnHandler, const char *pszDesc,
                                                     PPGMPHYSHANDLERTYPE phType)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(phType, VERR_INVALID_POINTER);
    *phType = NIL_PGMPHYSHANDLERTYPE;

    AssertPtrReturn(pfnHandler, VERR_INVALID_POINTER);
    AssertPtrReturn(pszDesc, VERR_INVALID_POINTER);
    AssertReturn(   enmKind == PGMPHYSHANDLERKIND_WRITE
                 || enmKind == PGMPHYSHANDLERKIND_ALL
                 || enmKind == PGMPHYSHANDLERKIND_MMIO,
                 VERR_INVALID_PARAMETER);
    AssertMsgReturn(!(fFlags & ~PGMPHYSHANDLER_F_VALID_MASK), ("%#x\n", fFlags), VERR_INVALID_FLAGS);

    VM_ASSERT_EMT0_RETURN(pVM, VERR_VM_THREAD_NOT_EMT);
    VM_ASSERT_STATE_RETURN(pVM, VMSTATE_CREATING, VERR_VM_INVALID_VM_STATE);

    /*
     * Do the allocating.
     */
    uint32_t const idxType = pVM->pgm.s.cPhysHandlerTypes;
    AssertLogRelReturn(idxType < RT_ELEMENTS(pVM->pgm.s.aPhysHandlerTypes), VERR_OUT_OF_RESOURCES);
    PPGMPHYSHANDLERTYPEINTR3 const pType = &pVM->pgm.s.aPhysHandlerTypes[idxType];
    AssertReturn(pType->enmKind == PGMPHYSHANDLERKIND_INVALID, VERR_PGM_HANDLER_IPE_1);
    pVM->pgm.s.cPhysHandlerTypes = idxType + 1;

    pType->enmKind          = enmKind;
    pType->uState           = enmKind == PGMPHYSHANDLERKIND_WRITE
                            ? PGM_PAGE_HNDL_PHYS_STATE_WRITE : PGM_PAGE_HNDL_PHYS_STATE_ALL;
    pType->fKeepPgmLock     = RT_BOOL(fFlags & PGMPHYSHANDLER_F_KEEP_PGM_LOCK);
    pType->fRing0DevInsIdx  = RT_BOOL(fFlags & PGMPHYSHANDLER_F_R0_DEVINS_IDX);
    pType->fNotInHm         = RT_BOOL(fFlags & PGMPHYSHANDLER_F_NOT_IN_HM);
    pType->pfnHandler       = pfnHandler;
    pType->pszDesc          = pszDesc;

    *phType = pType->hType;
    LogFlow(("PGMR3HandlerPhysicalTypeRegisterEx: hType=%#RX64/%#x: enmKind=%d fFlags=%#x pfnHandler=%p pszDesc=%s\n",
             pType->hType, idxType, enmKind, fFlags, pfnHandler, pszDesc));
    return VINF_SUCCESS;
}


/**
 * Updates the physical page access handlers.
 *
 * @param   pVM     The cross context VM structure.
 * @remark  Only used when restoring a saved state.
 */
void pgmR3HandlerPhysicalUpdateAll(PVM pVM)
{
    LogFlow(("pgmHandlerPhysicalUpdateAll:\n"));

    /*
     * Clear and set.
     * (the right -> left on the setting pass is just bird speculating on cache hits)
     */
    PGM_LOCK_VOID(pVM);

    int rc = pVM->pgm.s.pPhysHandlerTree->doWithAllFromLeft(&pVM->pgm.s.PhysHandlerAllocator, pgmR3HandlerPhysicalOneClear, pVM);
    AssertRC(rc);
    rc = pVM->pgm.s.pPhysHandlerTree->doWithAllFromRight(&pVM->pgm.s.PhysHandlerAllocator, pgmR3HandlerPhysicalOneSet, pVM);
    AssertRC(rc);

    PGM_UNLOCK(pVM);
}


/**
 * Clears all the page level flags for one physical handler range.
 *
 * @returns 0
 * @param   pHandler    The physical access handler entry.
 * @param   pvUser      Pointer to the VM.
 */
static DECLCALLBACK(int) pgmR3HandlerPhysicalOneClear(PPGMPHYSHANDLER pHandler, void *pvUser)
{
    PPGMRAMRANGE    pRamHint = NULL;
    RTGCPHYS        GCPhys   = pHandler->Key;
    RTUINT          cPages   = pHandler->cPages;
    PVM             pVM      = (PVM)pvUser;
    for (;;)
    {
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageWithHintEx(pVM, GCPhys, &pPage, &pRamHint);
        if (RT_SUCCESS(rc))
        {
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, PGM_PAGE_HNDL_PHYS_STATE_NONE, false);

#ifdef VBOX_WITH_NATIVE_NEM
            /* Tell NEM about the protection change. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage),
                                               PGM_RAMRANGE_CALC_PAGE_R3PTR(pRamHint, GCPhys),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
#endif
        }
        else
            AssertRC(rc);

        if (--cPages == 0)
            return 0;
        GCPhys += GUEST_PAGE_SIZE;
    }
}


/**
 * Sets all the page level flags for one physical handler range.
 *
 * @returns 0
 * @param   pHandler    The physical access handler entry.
 * @param   pvUser      Pointer to the VM.
 */
static DECLCALLBACK(int) pgmR3HandlerPhysicalOneSet(PPGMPHYSHANDLER pHandler, void *pvUser)
{
    PVM                     pVM       = (PVM)pvUser;
    PCPGMPHYSHANDLERTYPEINT pType     = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pHandler);
    unsigned                uState    = pType->uState;
    PPGMRAMRANGE            pRamHint  = NULL;
    RTGCPHYS                GCPhys    = pHandler->Key;
    RTUINT                  cPages    = pHandler->cPages;
    for (;;)
    {
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageWithHintEx(pVM, GCPhys, &pPage, &pRamHint);
        if (RT_SUCCESS(rc))
        {
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, uState, pType->fNotInHm);

#ifdef VBOX_WITH_NATIVE_NEM
            /* Tell NEM about the protection change. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage),
                                               PGM_RAMRANGE_CALC_PAGE_R3PTR(pRamHint, GCPhys),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
#endif
        }
        else
            AssertRC(rc);

        if (--cPages == 0)
            return 0;
        GCPhys += GUEST_PAGE_SIZE;
    }
}


/**
 * Arguments for pgmR3InfoHandlersPhysicalOne and pgmR3InfoHandlersVirtualOne.
 */
typedef struct PGMHANDLERINFOARG
{
    /** The output helpers.*/
    PCDBGFINFOHLP   pHlp;
    /** Pointer to the cross context VM handle. */
    PVM             pVM;
    /** Set if statistics should be dumped. */
    bool            fStats;
} PGMHANDLERINFOARG, *PPGMHANDLERINFOARG;


/**
 * Info callback for 'pgmhandlers'.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pHlp        The output helpers.
 * @param   pszArgs     The arguments. phys or virt.
 */
DECLCALLBACK(void) pgmR3InfoHandlers(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /*
     * Parse options.
     */
    PGMHANDLERINFOARG Args = { pHlp, pVM, /* .fStats = */ true };
    if (pszArgs)
        Args.fStats = strstr(pszArgs, "nost") == NULL;

    /*
     * Dump the handlers.
     */
    pHlp->pfnPrintf(pHlp,
                    "Physical handlers: max %#x, %u allocator error%s, %u tree error%s\n"
                    "%*s %*s %*s uUser             Type     Description\n",
                    pVM->pgm.s.PhysHandlerAllocator.m_cNodes,
                    pVM->pgm.s.PhysHandlerAllocator.m_cErrors, pVM->pgm.s.PhysHandlerAllocator.m_cErrors != 0 ? "s" : "",
                    pVM->pgm.s.pPhysHandlerTree->m_cErrors, pVM->pgm.s.pPhysHandlerTree->m_cErrors != 0 ? "s" : "",
                    - (int)sizeof(RTGCPHYS) * 2,     "From",
                    - (int)sizeof(RTGCPHYS) * 2 - 3, "- To (incl)",
                    - (int)sizeof(RTHCPTR)  * 2 - 1, "Handler (R3)");
    pVM->pgm.s.pPhysHandlerTree->doWithAllFromLeft(&pVM->pgm.s.PhysHandlerAllocator, pgmR3InfoHandlersPhysicalOne, &Args);
}


/**
 * Displays one physical handler range.
 *
 * @returns 0
 * @param   pHandler    The physical access handler entry.
 * @param   pvUser      Pointer to command helper functions.
 */
static DECLCALLBACK(int) pgmR3InfoHandlersPhysicalOne(PPGMPHYSHANDLER pHandler, void *pvUser)
{
    PPGMHANDLERINFOARG      pArgs = (PPGMHANDLERINFOARG)pvUser;
    PCDBGFINFOHLP           pHlp  = pArgs->pHlp;
    PCPGMPHYSHANDLERTYPEINT pType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pArgs->pVM, pHandler);
    const char *pszType;
    switch (pType->enmKind)
    {
        case PGMPHYSHANDLERKIND_MMIO:   pszType = "MMIO   "; break;
        case PGMPHYSHANDLERKIND_WRITE:  pszType = "Write  "; break;
        case PGMPHYSHANDLERKIND_ALL:    pszType = "All    "; break;
        default:                        pszType = "???????"; break;
    }

    char   szFlags[80];
    size_t cchFlags = 0;
    if (pType->fKeepPgmLock)
        cchFlags = RTStrPrintf(szFlags, sizeof(szFlags), "(keep-pgm-lock");
    if (pType->fRing0DevInsIdx)
        cchFlags += RTStrPrintf(&szFlags[cchFlags], sizeof(szFlags) - cchFlags, cchFlags ? ", keep-pgm-lock" : "(keep-pgm-lock");
    if (pType->fRing0Enabled)
        cchFlags += RTStrPrintf(&szFlags[cchFlags], sizeof(szFlags) - cchFlags, cchFlags ? ", r0-enabled)" : "(r0-enabled)");
    else
        cchFlags += RTStrPrintf(&szFlags[cchFlags], sizeof(szFlags) - cchFlags, cchFlags ? ", r3-only)" : "(r3-only)");

    pHlp->pfnPrintf(pHlp,
                    "%RGp - %RGp  %p  %016RX64  %s  %s  %s\n",
                    pHandler->Key, pHandler->KeyLast, pType->pfnHandler, pHandler->uUser, pszType, pHandler->pszDesc, szFlags);
#ifdef VBOX_WITH_STATISTICS
    if (pArgs->fStats)
        pHlp->pfnPrintf(pHlp, "   cPeriods: %9RU64  cTicks: %11RU64  Min: %11RU64  Avg: %11RU64 Max: %11RU64\n",
                        pHandler->Stat.cPeriods, pHandler->Stat.cTicks, pHandler->Stat.cTicksMin,
                        pHandler->Stat.cPeriods ? pHandler->Stat.cTicks / pHandler->Stat.cPeriods : 0, pHandler->Stat.cTicksMax);
#endif
    return 0;
}

