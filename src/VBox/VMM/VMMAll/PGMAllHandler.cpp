/* $Id: PGMAllHandler.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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
#include <VBox/vmm/iem.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/dbgf.h>
#ifdef IN_RING0
# include <VBox/vmm/pdmdev.h>
#endif
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "PGMInline.h"

#include <VBox/log.h>
#include <iprt/assert.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/string.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/vmm/selm.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Dummy physical access handler type record. */
CTX_SUFF(PGMPHYSHANDLERTYPEINT) const g_pgmHandlerPhysicalDummyType =
{
    /* .hType = */              UINT64_C(0x93b7557e1937aaff),
    /* .enmKind = */            PGMPHYSHANDLERKIND_INVALID,
    /* .uState = */             PGM_PAGE_HNDL_PHYS_STATE_ALL,
    /* .fKeepPgmLock = */       true,
    /* .fRing0DevInsIdx = */    false,
#ifdef IN_RING0
    /* .fNotInHm = */           false,
    /* .pfnHandler = */         pgmR0HandlerPhysicalHandlerToRing3,
    /* .pfnPfHandler = */       pgmR0HandlerPhysicalPfHandlerToRing3,
#elif defined(IN_RING3)
    /* .fRing0Enabled = */      false,
    /* .fNotInHm = */           false,
    /* .pfnHandler = */         pgmR3HandlerPhysicalHandlerInvalid,
#else
# error "unsupported context"
#endif
    /* .pszDesc = */            "dummy"
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int  pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(PVMCC pVM, PPGMPHYSHANDLER pCur, PPGMRAMRANGE pRam,
                                                           void *pvBitmap, uint32_t offBitmap);
static void pgmHandlerPhysicalDeregisterNotifyNEM(PVMCC pVM, PPGMPHYSHANDLER pCur);
static void pgmHandlerPhysicalResetRamFlags(PVMCC pVM, PPGMPHYSHANDLER pCur);


#ifndef IN_RING3

/**
 * @callback_method_impl{FNPGMPHYSHANDLER,
 *      Dummy for forcing ring-3 handling of the access.}
 */
DECLCALLBACK(VBOXSTRICTRC)
pgmR0HandlerPhysicalHandlerToRing3(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf,
                                   PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin, uint64_t uUser)
{
    RT_NOREF(pVM, pVCpu, GCPhys, pvPhys, pvBuf, cbBuf, enmAccessType, enmOrigin, uUser);
    return VINF_EM_RAW_EMULATE_INSTR;
}


/**
 * @callback_method_impl{FNPGMRZPHYSPFHANDLER,
 *      Dummy for forcing ring-3 handling of the access.}
 */
DECLCALLBACK(VBOXSTRICTRC)
pgmR0HandlerPhysicalPfHandlerToRing3(PVMCC pVM, PVMCPUCC pVCpu, RTGCUINT uErrorCode, PCPUMCTX pCtx,
                                     RTGCPTR pvFault, RTGCPHYS GCPhysFault, uint64_t uUser)
{
    RT_NOREF(pVM, pVCpu, uErrorCode, pCtx, pvFault, GCPhysFault, uUser);
    return VINF_EM_RAW_EMULATE_INSTR;
}

#endif /* !IN_RING3 */


/**
 * Worker for pgmHandlerPhysicalExCreate.
 *
 * @returns A new physical handler on success or NULL on failure.
 * @param   pVM         The cross context VM structure.
 * @param   pType       The physical handler type.
 * @param   hType       The physical handler type registeration handle.
 * @param   uUser       User argument to the handlers (not pointer).
 * @param   pszDesc     Description of this handler.  If NULL, the type description
 *                      will be used instead.
 */
DECL_FORCE_INLINE(PPGMPHYSHANDLER) pgmHandlerPhysicalExCreateWorker(PVMCC pVM, PCPGMPHYSHANDLERTYPEINT pType,
                                                                    PGMPHYSHANDLERTYPE hType, uint64_t uUser,
                                                                    R3PTRTYPE(const char *) pszDesc)
{
    PGM_LOCK_ASSERT_OWNER(pVM);
    PPGMPHYSHANDLER pNew = pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator.allocateNode();
    if (pNew)
    {
        pNew->Key           = NIL_RTGCPHYS;
        pNew->KeyLast       = NIL_RTGCPHYS;
        pNew->cPages        = 0;
        pNew->cAliasedPages = 0;
        pNew->cTmpOffPages  = 0;
        pNew->uUser         = uUser;
        pNew->hType         = hType;
        pNew->pszDesc       = pszDesc != NIL_RTR3PTR ? pszDesc
#ifdef IN_RING3
                            : pType->pszDesc;
#else
                            : pVM->pgm.s.aPhysHandlerTypes[hType & PGMPHYSHANDLERTYPE_IDX_MASK].pszDesc;
        NOREF(pType);
#endif
    }
    return pNew;
}


/**
 * Creates a physical access handler, allocation part.
 *
 * @returns VBox status code.
 * @retval  VERR_OUT_OF_RESOURCES if no more handlers available.
 *
 * @param   pVM             The cross context VM structure.
 * @param   hType           The handler type registration handle.
 * @param   uUser           User argument to the handlers (not pointer).
 * @param   pszDesc         Description of this handler.  If NULL, the type
 *                          description will be used instead.
 * @param   ppPhysHandler   Where to return the access handler structure on
 *                          success.
 */
int pgmHandlerPhysicalExCreate(PVMCC pVM, PGMPHYSHANDLERTYPE hType, uint64_t uUser,
                               R3PTRTYPE(const char *) pszDesc, PPGMPHYSHANDLER *ppPhysHandler)
{
    /*
     * Validate input.
     */
    PCPGMPHYSHANDLERTYPEINT const pType = pgmHandlerPhysicalTypeHandleToPtr(pVM, hType);
    AssertReturn(pType, VERR_INVALID_HANDLE);
    AssertReturn(pType->enmKind > PGMPHYSHANDLERKIND_INVALID && pType->enmKind < PGMPHYSHANDLERKIND_END, VERR_INVALID_HANDLE);
    AssertPtr(ppPhysHandler);

    Log(("pgmHandlerPhysicalExCreate: uUser=%#RX64 hType=%#x (%d, %s) pszDesc=%RHv:%s\n",
         uUser, hType, pType->enmKind, pType->pszDesc, pszDesc, R3STRING(pszDesc)));

    /*
     * Allocate and initialize the new entry.
     */
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);
    *ppPhysHandler = pgmHandlerPhysicalExCreateWorker(pVM, pType, hType, uUser, pszDesc);
    PGM_UNLOCK(pVM);
    if (*ppPhysHandler)
        return VINF_SUCCESS;
    return VERR_OUT_OF_RESOURCES;
}


/**
 * Duplicates a physical access handler.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pPhysHandlerSrc The source handler to duplicate
 * @param   ppPhysHandler   Where to return the access handler structure on
 *                          success.
 */
int pgmHandlerPhysicalExDup(PVMCC pVM, PPGMPHYSHANDLER pPhysHandlerSrc, PPGMPHYSHANDLER *ppPhysHandler)
{
    return pgmHandlerPhysicalExCreate(pVM, pPhysHandlerSrc->hType, pPhysHandlerSrc->uUser,
                                      pPhysHandlerSrc->pszDesc, ppPhysHandler);
}


/**
 * Register a access handler for a physical range.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 * @retval  VINF_PGM_GCPHYS_ALIASED could be returned.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pPhysHandler    The physical handler.
 * @param   GCPhys          Start physical address.
 * @param   GCPhysLast      Last physical address. (inclusive)
 */
int pgmHandlerPhysicalExRegister(PVMCC pVM, PPGMPHYSHANDLER pPhysHandler, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast)
{
    /*
     * Validate input.
     */
    AssertReturn(pPhysHandler, VERR_INVALID_POINTER);
    PGMPHYSHANDLERTYPE const      hType = pPhysHandler->hType;
    PCPGMPHYSHANDLERTYPEINT const pType = pgmHandlerPhysicalTypeHandleToPtr(pVM, hType);
    AssertReturn(pType, VERR_INVALID_HANDLE);
    AssertReturn(pType->enmKind > PGMPHYSHANDLERKIND_INVALID && pType->enmKind < PGMPHYSHANDLERKIND_END, VERR_INVALID_HANDLE);

    AssertPtr(pPhysHandler);

    Log(("pgmHandlerPhysicalExRegister: GCPhys=%RGp GCPhysLast=%RGp hType=%#x (%d, %s) pszDesc=%RHv:%s\n", GCPhys, GCPhysLast,
         hType, pType->enmKind, pType->pszDesc, pPhysHandler->pszDesc, R3STRING(pPhysHandler->pszDesc)));
    AssertReturn(pPhysHandler->Key == NIL_RTGCPHYS, VERR_WRONG_ORDER);

    AssertMsgReturn(GCPhys < GCPhysLast, ("GCPhys >= GCPhysLast (%#x >= %#x)\n", GCPhys, GCPhysLast), VERR_INVALID_PARAMETER);
    Assert(GCPhysLast - GCPhys < _4G); /* ASSUMPTION in PGMAllPhys.cpp */

    switch (pType->enmKind)
    {
        case PGMPHYSHANDLERKIND_WRITE:
            if (!pType->fNotInHm)
                break;
            RT_FALL_THRU(); /* Simplification: fNotInHm can only be used with full pages */
        case PGMPHYSHANDLERKIND_MMIO:
        case PGMPHYSHANDLERKIND_ALL:
            /* Simplification for PGMPhysRead, PGMR0Trap0eHandlerNPMisconfig and others: Full pages. */
            AssertMsgReturn(!(GCPhys & GUEST_PAGE_OFFSET_MASK), ("%RGp\n", GCPhys), VERR_INVALID_PARAMETER);
            AssertMsgReturn((GCPhysLast & GUEST_PAGE_OFFSET_MASK) == GUEST_PAGE_OFFSET_MASK, ("%RGp\n", GCPhysLast), VERR_INVALID_PARAMETER);
            break;
        default:
            AssertMsgFailed(("Invalid input enmKind=%d!\n", pType->enmKind));
            return VERR_INVALID_PARAMETER;
    }

    /*
     * We require the range to be within registered ram.
     * There is no apparent need to support ranges which cover more than one ram range.
     */
    PPGMRAMRANGE pRam = pgmPhysGetRange(pVM, GCPhys);
    if (   !pRam
        || GCPhysLast > pRam->GCPhysLast)
    {
#ifdef IN_RING3
        DBGFR3Info(pVM->pUVM, "phys", NULL, NULL);
#endif
        AssertMsgFailed(("No RAM range for %RGp-%RGp\n", GCPhys, GCPhysLast));
        return VERR_PGM_HANDLER_PHYSICAL_NO_RAM_RANGE;
    }
    Assert(GCPhys >= pRam->GCPhys && GCPhys < pRam->GCPhysLast);
    Assert(GCPhysLast <= pRam->GCPhysLast && GCPhysLast >= pRam->GCPhys);

    /*
     * Try insert into list.
     */
    pPhysHandler->Key     = GCPhys;
    pPhysHandler->KeyLast = GCPhysLast;
    pPhysHandler->cPages  = (GCPhysLast - (GCPhys & X86_PTE_PAE_PG_MASK) + GUEST_PAGE_SIZE) >> GUEST_PAGE_SHIFT;

    int rc = PGM_LOCK(pVM);
    if (RT_SUCCESS(rc))
    {
        rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->insert(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, pPhysHandler);
        if (RT_SUCCESS(rc))
        {
            rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pPhysHandler, pRam, NULL /*pvBitmap*/, 0 /*offBitmap*/);
            if (rc == VINF_PGM_SYNC_CR3)
                rc = VINF_PGM_GCPHYS_ALIASED;

#if defined(IN_RING3) || defined(IN_RING0)
            NEMHCNotifyHandlerPhysicalRegister(pVM, pType->enmKind, GCPhys, GCPhysLast - GCPhys + 1);
#endif
            PGM_UNLOCK(pVM);

            if (rc != VINF_SUCCESS)
                Log(("PGMHandlerPhysicalRegisterEx: returns %Rrc (%RGp-%RGp)\n", rc, GCPhys, GCPhysLast));
            return rc;
        }
        PGM_UNLOCK(pVM);
    }

    pPhysHandler->Key     = NIL_RTGCPHYS;
    pPhysHandler->KeyLast = NIL_RTGCPHYS;

    AssertMsgReturn(rc == VERR_ALREADY_EXISTS, ("%Rrc GCPhys=%RGp GCPhysLast=%RGp\n", rc, GCPhys, GCPhysLast), rc);

#if defined(IN_RING3) && defined(VBOX_STRICT)
    DBGFR3Info(pVM->pUVM, "handlers", "phys nostats", NULL);
#endif
    AssertMsgFailed(("Conflict! GCPhys=%RGp GCPhysLast=%RGp pszDesc=%s/%s\n",
                     GCPhys, GCPhysLast, R3STRING(pPhysHandler->pszDesc), R3STRING(pType->pszDesc)));
    return VERR_PGM_HANDLER_PHYSICAL_CONFLICT;
}


/**
 * Worker for pgmHandlerPhysicalRegisterVmxApicAccessPage.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 * @retval  VINF_PGM_GCPHYS_ALIASED could be returned.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pPhysHandler    The physical handler.
 * @param   GCPhys          The address of the virtual VMX APIC-access page. Must be
 *                          page aligned.
 */
static int pgmHandlerPhysicalRegisterVmxApicAccessPage(PVMCC pVM, PPGMPHYSHANDLER pPhysHandler, RTGCPHYS GCPhys)
{
    PGM_LOCK_ASSERT_OWNER(pVM);
    LogFunc(("GCPhys=%RGp\n", GCPhys));

    /*
     * We require the range to be within registered ram.
     * There is no apparent need to support ranges which cover more than one ram range.
     */
    PPGMRAMRANGE pRam = pgmPhysGetRange(pVM, GCPhys);
    RTGCPHYS const GCPhysLast = GCPhys | X86_PAGE_4K_OFFSET_MASK;
    if (   !pRam
        || GCPhysLast > pRam->GCPhysLast)
    {
#ifdef IN_RING3
        DBGFR3Info(pVM->pUVM, "phys", NULL, NULL);
#endif
        AssertMsgFailed(("No RAM range for %RGp-%RGp\n", GCPhys, GCPhysLast));
        return VERR_PGM_HANDLER_PHYSICAL_NO_RAM_RANGE;
    }
    Assert(GCPhys >= pRam->GCPhys && GCPhys < pRam->GCPhysLast);
    Assert(GCPhysLast <= pRam->GCPhysLast && GCPhysLast >= pRam->GCPhys);

    /*
     * Try insert into list.
     */
    pPhysHandler->Key     = GCPhys;
    pPhysHandler->KeyLast = GCPhysLast;
    pPhysHandler->cPages  = (GCPhysLast - (GCPhys & X86_PTE_PAE_PG_MASK) + GUEST_PAGE_SIZE) >> GUEST_PAGE_SHIFT;

    int rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->insert(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, pPhysHandler);
    if (RT_SUCCESS(rc))
    {
        rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pPhysHandler, pRam, NULL /*pvBitmap*/, 0 /*offBitmap*/);
        if (rc == VINF_PGM_SYNC_CR3)
            rc = VINF_PGM_GCPHYS_ALIASED;

#if defined(IN_RING3) || defined(IN_RING0)
        NEMHCNotifyHandlerPhysicalRegister(pVM, PGMPHYSHANDLERKIND_ALL, GCPhys, GCPhysLast - GCPhys + 1);
#endif
        return rc;
    }

    pPhysHandler->Key     = NIL_RTGCPHYS;
    pPhysHandler->KeyLast = NIL_RTGCPHYS;

    AssertMsgReturn(rc == VERR_ALREADY_EXISTS, ("%Rrc GCPhys=%RGp GCPhysLast=%RGp\n", rc, GCPhys, GCPhysLast), rc);
#if defined(IN_RING3) && defined(VBOX_STRICT)
    DBGFR3Info(pVM->pUVM, "handlers", "phys nostats", NULL);
#endif
    AssertMsgFailed(("Conflict! GCPhys=%RGp GCPhysLast=%RGp\n", GCPhys, GCPhysLast));
    return VERR_PGM_HANDLER_PHYSICAL_CONFLICT;
}


/**
 * Register a access handler for a physical range.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 * @retval  VINF_PGM_GCPHYS_ALIASED when the shadow PTs could be updated because
 *          the guest page aliased or/and mapped by multiple PTs. A CR3 sync has been
 *          flagged together with a pool clearing.
 * @retval  VERR_PGM_HANDLER_PHYSICAL_CONFLICT if the range conflicts with an existing
 *          one. A debug assertion is raised.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Start physical address.
 * @param   GCPhysLast      Last physical address. (inclusive)
 * @param   hType           The handler type registration handle.
 * @param   uUser           User argument to the handler.
 * @param   pszDesc         Description of this handler.  If NULL, the type
 *                          description will be used instead.
 */
VMMDECL(int) PGMHandlerPhysicalRegister(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast, PGMPHYSHANDLERTYPE hType,
                                        uint64_t uUser, R3PTRTYPE(const char *) pszDesc)
{
#ifdef LOG_ENABLED
    PCPGMPHYSHANDLERTYPEINT pType = pgmHandlerPhysicalTypeHandleToPtr(pVM, hType);
    Log(("PGMHandlerPhysicalRegister: GCPhys=%RGp GCPhysLast=%RGp uUser=%#RX64 hType=%#x (%d, %s) pszDesc=%RHv:%s\n",
         GCPhys, GCPhysLast, uUser, hType, pType->enmKind, R3STRING(pType->pszDesc), pszDesc, R3STRING(pszDesc)));
#endif

    PPGMPHYSHANDLER pNew;
    int rc = pgmHandlerPhysicalExCreate(pVM, hType, uUser, pszDesc, &pNew);
    if (RT_SUCCESS(rc))
    {
        rc = pgmHandlerPhysicalExRegister(pVM, pNew, GCPhys, GCPhysLast);
        if (RT_SUCCESS(rc))
            return rc;
        pgmHandlerPhysicalExDestroy(pVM, pNew);
    }
    return rc;
}


/**
 * Register an access handler for a virtual VMX APIC-access page.
 *
 * This holds the PGM lock across the whole operation to resolve races between
 * VCPUs registering the same page simultaneously. It's also a slightly slimmer
 * version of the regular registeration function as it's specific to the VMX
 * APIC-access page.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when successfully installed.
 * @retval  VINF_PGM_GCPHYS_ALIASED when the shadow PTs could be updated because
 *          the guest page aliased or/and mapped by multiple PTs. A CR3 sync has been
 *          flagged together with a pool clearing.
 * @retval  VERR_PGM_HANDLER_PHYSICAL_CONFLICT if the range conflicts with an existing
 *          one. A debug assertion is raised.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          The address of the VMX virtual-APIC access page. Must be
 *                          page aligned.
 * @param   hType           The handler type registration handle.
 */
VMMDECL(int) PGMHandlerPhysicalRegisterVmxApicAccessPage(PVMCC pVM, RTGCPHYS GCPhys, PGMPHYSHANDLERTYPE hType)
{
    PCPGMPHYSHANDLERTYPEINT const pType = pgmHandlerPhysicalTypeHandleToPtr(pVM, hType);
    AssertReturn(pType, VERR_INVALID_HANDLE);
    AssertMsg(!(GCPhys & GUEST_PAGE_OFFSET_MASK), ("%RGp\n", GCPhys));

    /*
     * Find if the VMX APIC access page has already been registered at this address.
     */
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    PPGMPHYSHANDLER pHandler;
    rc = pgmHandlerPhysicalLookup(pVM, GCPhys, &pHandler);
    if (RT_SUCCESS(rc))
    {
        PCPGMPHYSHANDLERTYPEINT const pHandlerType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pHandler);
        Assert(GCPhys >= pHandler->Key && GCPhys <= pHandler->KeyLast);
        Assert(   pHandlerType->enmKind == PGMPHYSHANDLERKIND_WRITE
               || pHandlerType->enmKind == PGMPHYSHANDLERKIND_ALL
               || pHandlerType->enmKind == PGMPHYSHANDLERKIND_MMIO);

        /* Check it's the virtual VMX APIC-access page. */
        if (pHandlerType->fNotInHm)
        {
            Assert(pHandlerType->enmKind == PGMPHYSHANDLERKIND_ALL);
            rc = VINF_SUCCESS;
        }
        else
        {
            rc = VERR_PGM_HANDLER_PHYSICAL_CONFLICT;
            AssertMsgFailed(("Conflict! GCPhys=%RGp enmKind=%#x fNotInHm=%RTbool\n", GCPhys, pHandlerType->enmKind,
                             pHandlerType->fNotInHm));
        }

        PGM_UNLOCK(pVM);
        return rc;
    }

    /*
     * Validate the page handler parameters before registering the virtual VMX APIC-access page.
     */
    AssertReturn(pType->enmKind == PGMPHYSHANDLERKIND_ALL, VERR_INVALID_HANDLE);
    AssertReturn(pType->fNotInHm, VERR_PGM_HANDLER_IPE_1);

    /*
     * Create and register a physical handler for the virtual VMX APIC-access page.
     */
    pHandler = pgmHandlerPhysicalExCreateWorker(pVM, pType, hType, 0 /*uUser*/, NULL /*pszDesc*/);
    if (pHandler)
    {
        rc = pgmHandlerPhysicalRegisterVmxApicAccessPage(pVM, pHandler, GCPhys);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            pgmHandlerPhysicalExDestroy(pVM, pHandler);
    }
    else
        rc = VERR_OUT_OF_RESOURCES;

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Sets ram range flags and attempts updating shadow PTs.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when shadow PTs was successfully updated.
 * @retval  VINF_PGM_SYNC_CR3 when the shadow PTs could be updated because
 *          the guest page aliased or/and mapped by multiple PTs. FFs set.
 * @param   pVM         The cross context VM structure.
 * @param   pCur        The physical handler.
 * @param   pRam        The RAM range.
 * @param   pvBitmap    Dirty bitmap. Optional.
 * @param   offBitmap   Dirty bitmap offset.
 */
static int pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(PVMCC pVM, PPGMPHYSHANDLER pCur, PPGMRAMRANGE pRam,
                                                          void *pvBitmap, uint32_t offBitmap)
{
    /*
     * Iterate the guest ram pages updating the flags and flushing PT entries
     * mapping the page.
     */
    bool                    fFlushTLBs = false;
    int                     rc         = VINF_SUCCESS;
    PCPGMPHYSHANDLERTYPEINT pCurType   = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
    const unsigned          uState     = pCurType->uState;
    uint32_t                cPages     = pCur->cPages;
    uint32_t                i          = (pCur->Key - pRam->GCPhys) >> GUEST_PAGE_SHIFT;
    for (;;)
    {
        PPGMPAGE pPage = &pRam->aPages[i];
        AssertMsg(pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO || PGM_PAGE_IS_MMIO(pPage),
                  ("%RGp %R[pgmpage]\n", pRam->GCPhys + (i << GUEST_PAGE_SHIFT), pPage));

        /* Only do upgrades. */
        if (PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) < uState)
        {
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, uState, pCurType->fNotInHm);

            const RTGCPHYS GCPhysPage = pRam->GCPhys + (i << GUEST_PAGE_SHIFT);
            int rc2 = pgmPoolTrackUpdateGCPhys(pVM, GCPhysPage, pPage,
                                               false /* allow updates of PTEs (instead of flushing) */, &fFlushTLBs);
            if (rc2 != VINF_SUCCESS && rc == VINF_SUCCESS)
                rc = rc2;

#ifdef VBOX_WITH_NATIVE_NEM
            /* Tell NEM about the protection update. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhysPage, PGM_PAGE_GET_HCPHYS(pPage),
                                               PGM_RAMRANGE_CALC_PAGE_R3PTR(pRam, GCPhysPage),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
#endif
            if (pvBitmap)
                ASMBitSet(pvBitmap, offBitmap);
        }

        /* next */
        if (--cPages == 0)
            break;
        i++;
        offBitmap++;
    }

    if (fFlushTLBs)
    {
        PGM_INVL_ALL_VCPU_TLBS(pVM);
        Log(("pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs: flushing guest TLBs; rc=%d\n", rc));
    }
    else
        Log(("pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs: doesn't flush guest TLBs. rc=%Rrc; sync flags=%x VMCPU_FF_PGM_SYNC_CR3=%d\n", rc, VMMGetCpu(pVM)->pgm.s.fSyncFlags, VMCPU_FF_IS_SET(VMMGetCpu(pVM), VMCPU_FF_PGM_SYNC_CR3)));

    return rc;
}


/**
 * Deregister a physical page access handler.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pPhysHandler    The handler to deregister (but not free).
 */
int pgmHandlerPhysicalExDeregister(PVMCC pVM, PPGMPHYSHANDLER pPhysHandler)
{
    LogFlow(("pgmHandlerPhysicalExDeregister: Removing Range %RGp-%RGp %s\n",
             pPhysHandler->Key, pPhysHandler->KeyLast, R3STRING(pPhysHandler->pszDesc)));

    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    RTGCPHYS const GCPhys = pPhysHandler->Key;
    AssertReturnStmt(GCPhys != NIL_RTGCPHYS, PGM_UNLOCK(pVM), VERR_PGM_HANDLER_NOT_FOUND);

    /*
     * Remove the handler from the tree.
     */

    PPGMPHYSHANDLER pRemoved;
    rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->remove(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pRemoved);
    if (RT_SUCCESS(rc))
    {
        if (pRemoved == pPhysHandler)
        {
            /*
             * Clear the page bits, notify the REM about this change and clear
             * the cache.
             */
            pgmHandlerPhysicalResetRamFlags(pVM, pPhysHandler);
            if (VM_IS_NEM_ENABLED(pVM))
                pgmHandlerPhysicalDeregisterNotifyNEM(pVM, pPhysHandler);
            pVM->pgm.s.idxLastPhysHandler = 0;

            pPhysHandler->Key     = NIL_RTGCPHYS;
            pPhysHandler->KeyLast = NIL_RTGCPHYS;

            PGM_UNLOCK(pVM);

            return VINF_SUCCESS;
        }

        /*
         * Both of the failure conditions here are considered internal processing
         * errors because they can only be caused by race conditions or corruption.
         * If we ever need to handle concurrent deregistration, we have to move
         * the NIL_RTGCPHYS check inside the PGM lock.
         */
        pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->insert(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, pRemoved);
    }

    PGM_UNLOCK(pVM);

    if (RT_FAILURE(rc))
        AssertMsgFailed(("Didn't find range starting at %RGp in the tree! %Rrc=rc\n", GCPhys, rc));
    else
        AssertMsgFailed(("Found different handle at %RGp in the tree: got %p insteaded of %p\n",
                         GCPhys, pRemoved, pPhysHandler));
    return VERR_PGM_HANDLER_IPE_1;
}


/**
 * Destroys (frees) a physical handler.
 *
 * The caller must deregister it before destroying it!
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pHandler    The handler to free.  NULL if ignored.
 */
int pgmHandlerPhysicalExDestroy(PVMCC pVM, PPGMPHYSHANDLER pHandler)
{
    if (pHandler)
    {
        AssertPtr(pHandler);
        AssertReturn(pHandler->Key == NIL_RTGCPHYS, VERR_WRONG_ORDER);

        int rc = PGM_LOCK(pVM);
        if (RT_SUCCESS(rc))
        {
            rc = pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator.freeNode(pHandler);
            PGM_UNLOCK(pVM);
        }
        return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Deregister a physical page access handler.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      Start physical address.
 */
VMMDECL(int)  PGMHandlerPhysicalDeregister(PVMCC pVM, RTGCPHYS GCPhys)
{
    AssertReturn(pVM->VMCC_CTX(pgm).s.pPhysHandlerTree, VERR_PGM_HANDLER_IPE_1);

    /*
     * Find the handler.
     */
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    PPGMPHYSHANDLER pRemoved;
    rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->remove(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pRemoved);
    if (RT_SUCCESS(rc))
    {
        Assert(pRemoved->Key == GCPhys);
        LogFlow(("PGMHandlerPhysicalDeregister: Removing Range %RGp-%RGp %s\n",
                 pRemoved->Key, pRemoved->KeyLast, R3STRING(pRemoved->pszDesc)));

        /*
         * Clear the page bits, notify the REM about this change and clear
         * the cache.
         */
        pgmHandlerPhysicalResetRamFlags(pVM, pRemoved);
        if (VM_IS_NEM_ENABLED(pVM))
            pgmHandlerPhysicalDeregisterNotifyNEM(pVM, pRemoved);
        pVM->pgm.s.idxLastPhysHandler = 0;

        pRemoved->Key = NIL_RTGCPHYS;
        rc = pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator.freeNode(pRemoved);

        PGM_UNLOCK(pVM);
        return rc;
    }

    PGM_UNLOCK(pVM);

    if (rc == VERR_NOT_FOUND)
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }
    return rc;
}


/**
 * Shared code with modify.
 */
static void pgmHandlerPhysicalDeregisterNotifyNEM(PVMCC pVM, PPGMPHYSHANDLER pCur)
{
#ifdef VBOX_WITH_NATIVE_NEM
    PCPGMPHYSHANDLERTYPEINT pCurType    = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
    RTGCPHYS                GCPhysStart = pCur->Key;
    RTGCPHYS                GCPhysLast  = pCur->KeyLast;

    /*
     * Page align the range.
     *
     * Since we've reset (recalculated) the physical handler state of all pages
     * we can make use of the page states to figure out whether a page should be
     * included in the REM notification or not.
     */
    if (   (pCur->Key           & GUEST_PAGE_OFFSET_MASK)
        || ((pCur->KeyLast + 1) & GUEST_PAGE_OFFSET_MASK))
    {
        Assert(pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO);

        if (GCPhysStart & GUEST_PAGE_OFFSET_MASK)
        {
            PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhysStart);
            if (    pPage
                &&  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_NONE)
            {
                RTGCPHYS GCPhys = (GCPhysStart + (GUEST_PAGE_SIZE - 1)) & X86_PTE_PAE_PG_MASK;
                if (    GCPhys > GCPhysLast
                    ||  GCPhys < GCPhysStart)
                    return;
                GCPhysStart = GCPhys;
            }
            else
                GCPhysStart &= X86_PTE_PAE_PG_MASK;
            Assert(!pPage || PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO); /* these are page aligned atm! */
        }

        if (GCPhysLast & GUEST_PAGE_OFFSET_MASK)
        {
            PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhysLast);
            if (    pPage
                &&  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_NONE)
            {
                RTGCPHYS GCPhys = (GCPhysLast & X86_PTE_PAE_PG_MASK) - 1;
                if (    GCPhys < GCPhysStart
                    ||  GCPhys > GCPhysLast)
                    return;
                GCPhysLast = GCPhys;
            }
            else
                GCPhysLast |= GUEST_PAGE_OFFSET_MASK;
            Assert(!pPage || PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO); /* these are page aligned atm! */
        }
    }

    /*
     * Tell NEM.
     */
    PPGMRAMRANGE const pRam    = pgmPhysGetRange(pVM, GCPhysStart);
    RTGCPHYS const     cb      = GCPhysLast - GCPhysStart + 1;
    uint8_t            u2State = UINT8_MAX;
    NEMHCNotifyHandlerPhysicalDeregister(pVM, pCurType->enmKind, GCPhysStart, cb,
                                         pRam ? PGM_RAMRANGE_CALC_PAGE_R3PTR(pRam, GCPhysStart) : NULL, &u2State);
    if (u2State != UINT8_MAX && pRam)
        pgmPhysSetNemStateForPages(&pRam->aPages[(GCPhysStart - pRam->GCPhys) >> GUEST_PAGE_SHIFT],
                                   cb >> GUEST_PAGE_SHIFT, u2State);
#else
    RT_NOREF(pVM, pCur);
#endif
}


/**
 * pgmHandlerPhysicalResetRamFlags helper that checks for other handlers on
 * edge pages.
 */
DECLINLINE(void) pgmHandlerPhysicalRecalcPageState(PVMCC pVM, RTGCPHYS GCPhys, bool fAbove, PPGMRAMRANGE *ppRamHint)
{
    /*
     * Look for other handlers.
     */
    unsigned uState = PGM_PAGE_HNDL_PHYS_STATE_NONE;
    for (;;)
    {
        PPGMPHYSHANDLER pCur;
        int rc;
        if (fAbove)
            rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookupMatchingOrAbove(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator,
                                                                              GCPhys, &pCur);
        else
            rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookupMatchingOrBelow(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator,
                                                                              GCPhys, &pCur);
        if (rc == VERR_NOT_FOUND)
            break;
        AssertRCBreak(rc);
        if (((fAbove ? pCur->Key : pCur->KeyLast) >> GUEST_PAGE_SHIFT) != (GCPhys >> GUEST_PAGE_SHIFT))
            break;
        PCPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
        uState = RT_MAX(uState, pCurType->uState);

        /* next? */
        RTGCPHYS GCPhysNext = fAbove
                            ? pCur->KeyLast + 1
                            : pCur->Key     - 1;
        if ((GCPhysNext >> GUEST_PAGE_SHIFT) != (GCPhys >> GUEST_PAGE_SHIFT))
            break;
        GCPhys = GCPhysNext;
    }

    /*
     * Update if we found something that is a higher priority state than the current.
     * Note! The PGMPHYSHANDLER_F_NOT_IN_HM can be ignored here as it requires whole pages.
     */
    if (uState != PGM_PAGE_HNDL_PHYS_STATE_NONE)
    {
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageWithHintEx(pVM, GCPhys, &pPage, ppRamHint);
        if (   RT_SUCCESS(rc)
            && PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) < uState)
        {
            /* This should normally not be necessary. */
            PGM_PAGE_SET_HNDL_PHYS_STATE_ONLY(pPage, uState);
            bool fFlushTLBs;
            rc = pgmPoolTrackUpdateGCPhys(pVM, GCPhys, pPage, false /*fFlushPTEs*/, &fFlushTLBs);
            if (RT_SUCCESS(rc) && fFlushTLBs)
                PGM_INVL_ALL_VCPU_TLBS(pVM);
            else
                AssertRC(rc);

#ifdef VBOX_WITH_NATIVE_NEM
            /* Tell NEM about the protection update. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage),
                                               PGM_RAMRANGE_CALC_PAGE_R3PTR(*ppRamHint, GCPhys),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
#endif
        }
        else
            AssertRC(rc);
    }
}


/**
 * Resets an aliased page.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pPage           The page.
 * @param   GCPhysPage      The page address in case it comes in handy.
 * @param   pRam            The RAM range the page is associated with (for NEM
 *                          notifications).
 * @param   fDoAccounting   Whether to perform accounting.  (Only set during
 *                          reset where pgmR3PhysRamReset doesn't have the
 *                          handler structure handy.)
 * @param   fFlushIemTlbs   Whether to perform IEM TLB flushing or not.  This
 *                          can be cleared only if the caller does the flushing
 *                          after calling this function.
 */
void pgmHandlerPhysicalResetAliasedPage(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhysPage, PPGMRAMRANGE pRam,
                                        bool fDoAccounting, bool fFlushIemTlbs)
{
    Assert(   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO
           || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO);
    Assert(PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) == PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
#ifdef VBOX_WITH_NATIVE_NEM
    RTHCPHYS const HCPhysPrev = PGM_PAGE_GET_HCPHYS(pPage);
#endif

    /*
     * Flush any shadow page table references *first*.
     */
    bool fFlushTLBs = false;
    int rc = pgmPoolTrackUpdateGCPhys(pVM, GCPhysPage, pPage, true /*fFlushPTEs*/, &fFlushTLBs);
    AssertLogRelRCReturnVoid(rc);
#if defined(VBOX_VMM_TARGET_ARMV8)
    AssertReleaseFailed();
#else
    HMFlushTlbOnAllVCpus(pVM);
#endif

    /*
     * Make it an MMIO/Zero page.
     */
    PGM_PAGE_SET_HCPHYS(pVM, pPage, pVM->pgm.s.HCPhysZeroPg);
    PGM_PAGE_SET_TYPE(pVM, pPage, PGMPAGETYPE_MMIO);
    PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ZERO);
    PGM_PAGE_SET_PAGEID(pVM, pPage, NIL_GMM_PAGEID);
    PGM_PAGE_SET_HNDL_PHYS_STATE_ONLY(pPage, PGM_PAGE_HNDL_PHYS_STATE_ALL);

    /*
     * Flush its TLB entry.
     */
    pgmPhysInvalidatePageMapTLBEntry(pVM, GCPhysPage);
    if (fFlushIemTlbs)
        IEMTlbInvalidateAllPhysicalAllCpus(pVM, NIL_VMCPUID, IEMTLBPHYSFLUSHREASON_RESET_ALIAS);

    /*
     * Do accounting for pgmR3PhysRamReset.
     */
    if (fDoAccounting)
    {
        PPGMPHYSHANDLER pHandler;
        rc = pgmHandlerPhysicalLookup(pVM, GCPhysPage, &pHandler);
        if (RT_SUCCESS(rc))
        {
            Assert(pHandler->cAliasedPages > 0);
            pHandler->cAliasedPages--;
        }
        else
            AssertMsgFailed(("rc=%Rrc GCPhysPage=%RGp\n", rc, GCPhysPage));
    }

#ifdef VBOX_WITH_NATIVE_NEM
    /*
     * Tell NEM about the protection change.
     */
    if (VM_IS_NEM_ENABLED(pVM))
    {
        uint8_t u2State = PGM_PAGE_GET_NEM_STATE(pPage);
        NEMHCNotifyPhysPageChanged(pVM, GCPhysPage, HCPhysPrev, pVM->pgm.s.HCPhysZeroPg,
                                   PGM_RAMRANGE_CALC_PAGE_R3PTR(pRam, GCPhysPage),
                                   NEM_PAGE_PROT_NONE, PGMPAGETYPE_MMIO, &u2State);
        PGM_PAGE_SET_NEM_STATE(pPage, u2State);
    }
#else
    RT_NOREF(pRam);
#endif
}


/**
 * Resets ram range flags.
 *
 * @param   pVM     The cross context VM structure.
 * @param   pCur    The physical handler.
 *
 * @remark  We don't start messing with the shadow page tables, as we've
 *          already got code in Trap0e which deals with out of sync handler
 *          flags (originally conceived for global pages).
 */
static void pgmHandlerPhysicalResetRamFlags(PVMCC pVM, PPGMPHYSHANDLER pCur)
{
    /*
     * Iterate the guest ram pages updating the state.
     */
    RTUINT          cPages   = pCur->cPages;
    RTGCPHYS        GCPhys   = pCur->Key;
    PPGMRAMRANGE    pRamHint = NULL;
    for (;;)
    {
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageWithHintEx(pVM, GCPhys, &pPage, &pRamHint);
        if (RT_SUCCESS(rc))
        {
            /* Reset aliased MMIO pages to MMIO, since this aliasing is our business.
               (We don't flip MMIO to RAM though, that's PGMPhys.cpp's job.)  */
            bool fNemNotifiedAlready = false;
            if (   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO
                || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO)
            {
                Assert(pCur->cAliasedPages > 0);
                pgmHandlerPhysicalResetAliasedPage(pVM, pPage, GCPhys, pRamHint, false /*fDoAccounting*/, true /*fFlushIemTlbs*/);
                pCur->cAliasedPages--;
                fNemNotifiedAlready = true;
            }
#ifdef VBOX_STRICT
            PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
            AssertMsg(pCurType && (pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO || PGM_PAGE_IS_MMIO(pPage)),
                      ("%RGp %R[pgmpage]\n", GCPhys, pPage));
#endif
            PGM_PAGE_SET_HNDL_PHYS_STATE(pPage, PGM_PAGE_HNDL_PHYS_STATE_NONE, false);

#ifdef VBOX_WITH_NATIVE_NEM
            /* Tell NEM about the protection change. */
            if (VM_IS_NEM_ENABLED(pVM) && !fNemNotifiedAlready)
            {
                uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage),
                                               PGM_RAMRANGE_CALC_PAGE_R3PTR(pRamHint, GCPhys),
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
#endif
            RT_NOREF(fNemNotifiedAlready);
        }
        else
            AssertRC(rc);

        /* next */
        if (--cPages == 0)
            break;
        GCPhys += GUEST_PAGE_SIZE;
    }

    pCur->cAliasedPages = 0;
    pCur->cTmpOffPages  = 0;

    /*
     * Check for partial start and end pages.
     */
    if (pCur->Key & GUEST_PAGE_OFFSET_MASK)
        pgmHandlerPhysicalRecalcPageState(pVM, pCur->Key - 1, false /* fAbove */, &pRamHint);
    if ((pCur->KeyLast & GUEST_PAGE_OFFSET_MASK) != GUEST_PAGE_OFFSET_MASK)
        pgmHandlerPhysicalRecalcPageState(pVM, pCur->KeyLast + 1, true /* fAbove */, &pRamHint);
}


#if 0 /* unused */
/**
 * Modify a physical page access handler.
 *
 * Modification can only be done to the range it self, not the type or anything else.
 *
 * @returns VBox status code.
 *          For all return codes other than VERR_PGM_HANDLER_NOT_FOUND and VINF_SUCCESS the range is deregistered
 *          and a new registration must be performed!
 * @param   pVM             The cross context VM structure.
 * @param   GCPhysCurrent   Current location.
 * @param   GCPhys          New location.
 * @param   GCPhysLast      New last location.
 */
VMMDECL(int) PGMHandlerPhysicalModify(PVMCC pVM, RTGCPHYS GCPhysCurrent, RTGCPHYS GCPhys, RTGCPHYS GCPhysLast)
{
    /*
     * Remove it.
     */
    int rc;
    PGM_LOCK_VOID(pVM);
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysRemove(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhysCurrent);
    if (pCur)
    {
        /*
         * Clear the ram flags. (We're gonna move or free it!)
         */
        pgmHandlerPhysicalResetRamFlags(pVM, pCur);
        PCPGMPHYSHANDLERTYPEINT const pCurType     = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
        @todo pCurType validation
        bool const                   fRestoreAsRAM = pCurType->pfnHandlerR3 /** @todo this isn't entirely correct. */
                                                  && pCurType->enmKind != PGMPHYSHANDLERKIND_MMIO;

        /*
         * Validate the new range, modify and reinsert.
         */
        if (GCPhysLast >= GCPhys)
        {
            /*
             * We require the range to be within registered ram.
             * There is no apparent need to support ranges which cover more than one ram range.
             */
            PPGMRAMRANGE pRam = pgmPhysGetRange(pVM, GCPhys);
            if (   pRam
                && GCPhys <= pRam->GCPhysLast
                && GCPhysLast >= pRam->GCPhys)
            {
                pCur->Core.Key      = GCPhys;
                pCur->Core.KeyLast  = GCPhysLast;
                pCur->cPages        = (GCPhysLast - (GCPhys & X86_PTE_PAE_PG_MASK) + 1) >> GUEST_PAGE_SHIFT;

                if (RTAvlroGCPhysInsert(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, &pCur->Core))
                {
                    RTGCPHYS            const cb            = GCPhysLast - GCPhys + 1;
                    PGMPHYSHANDLERKIND  const enmKind       = pCurType->enmKind;

                    /*
                     * Set ram flags, flush shadow PT entries and finally tell REM about this.
                     */
                    rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pCur, pRam, NULL, 0);

                    /** @todo NEM: not sure we need this notification... */
                    NEMHCNotifyHandlerPhysicalModify(pVM, enmKind, GCPhysCurrent, GCPhys, cb, fRestoreAsRAM);

                    PGM_UNLOCK(pVM);

                    PGM_INVL_ALL_VCPU_TLBS(pVM);
                    Log(("PGMHandlerPhysicalModify: GCPhysCurrent=%RGp -> GCPhys=%RGp GCPhysLast=%RGp\n",
                         GCPhysCurrent, GCPhys, GCPhysLast));
                    return VINF_SUCCESS;
                }

                AssertMsgFailed(("Conflict! GCPhys=%RGp GCPhysLast=%RGp\n", GCPhys, GCPhysLast));
                rc = VERR_PGM_HANDLER_PHYSICAL_CONFLICT;
            }
            else
            {
                AssertMsgFailed(("No RAM range for %RGp-%RGp\n", GCPhys, GCPhysLast));
                rc = VERR_PGM_HANDLER_PHYSICAL_NO_RAM_RANGE;
            }
        }
        else
        {
            AssertMsgFailed(("Invalid range %RGp-%RGp\n", GCPhys, GCPhysLast));
            rc = VERR_INVALID_PARAMETER;
        }

        /*
         * Invalid new location, flush the cache and free it.
         * We've only gotta notify REM and free the memory.
         */
        if (VM_IS_NEM_ENABLED(pVM))
            pgmHandlerPhysicalDeregisterNotifyNEM(pVM, pCur);
        pVM->pgm.s.pLastPhysHandlerR0 = 0;
        pVM->pgm.s.pLastPhysHandlerR3 = 0;
        PGMHandlerPhysicalTypeRelease(pVM, pCur->hType);
        MMHyperFree(pVM, pCur);
    }
    else
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhysCurrent));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }

    PGM_UNLOCK(pVM);
    return rc;
}
#endif /* unused */


/**
 * Changes the user callback arguments associated with a physical access handler.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Start physical address of the handler.
 * @param   uUser           User argument to the handlers.
 */
VMMDECL(int) PGMHandlerPhysicalChangeUserArg(PVMCC pVM, RTGCPHYS GCPhys, uint64_t uUser)
{
    /*
     * Find the handler and make the change.
     */
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    PPGMPHYSHANDLER pCur;
    rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookup(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pCur);
    if (RT_SUCCESS(rc))
    {
        Assert(pCur->Key == GCPhys);
        pCur->uUser = uUser;
    }
    else if (rc == VERR_NOT_FOUND)
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }

    PGM_UNLOCK(pVM);
    return rc;
}

#if 0 /* unused */

/**
 * Splits a physical access handler in two.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Start physical address of the handler.
 * @param   GCPhysSplit     The split address.
 */
VMMDECL(int) PGMHandlerPhysicalSplit(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysSplit)
{
    AssertReturn(GCPhys < GCPhysSplit, VERR_INVALID_PARAMETER);

    /*
     * Do the allocation without owning the lock.
     */
    PPGMPHYSHANDLER pNew;
    int rc = MMHyperAlloc(pVM, sizeof(*pNew), 0, MM_TAG_PGM_HANDLERS, (void **)&pNew);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Get the handler.
     */
    PGM_LOCK_VOID(pVM);
    PPGMPHYSHANDLER pCur = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys);
    if (RT_LIKELY(pCur))
    {
        if (RT_LIKELY(GCPhysSplit <= pCur->Core.KeyLast))
        {
            /*
             * Create new handler node for the 2nd half.
             */
            *pNew = *pCur;
            pNew->Core.Key      = GCPhysSplit;
            pNew->cPages        = (pNew->Core.KeyLast - (pNew->Core.Key & X86_PTE_PAE_PG_MASK) + GUEST_PAGE_SIZE) >> GUEST_PAGE_SHIFT;

            pCur->Core.KeyLast  = GCPhysSplit - 1;
            pCur->cPages        = (pCur->Core.KeyLast - (pCur->Core.Key & X86_PTE_PAE_PG_MASK) + GUEST_PAGE_SIZE) >> GUEST_PAGE_SHIFT;

            if (RT_LIKELY(RTAvlroGCPhysInsert(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, &pNew->Core)))
            {
                LogFlow(("PGMHandlerPhysicalSplit: %RGp-%RGp and %RGp-%RGp\n",
                         pCur->Core.Key, pCur->Core.KeyLast, pNew->Core.Key, pNew->Core.KeyLast));
                PGM_UNLOCK(pVM);
                return VINF_SUCCESS;
            }
            AssertMsgFailed(("whu?\n"));
            rc = VERR_PGM_PHYS_HANDLER_IPE;
        }
        else
        {
            AssertMsgFailed(("outside range: %RGp-%RGp split %RGp\n", pCur->Core.Key, pCur->Core.KeyLast, GCPhysSplit));
            rc = VERR_INVALID_PARAMETER;
        }
    }
    else
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }
    PGM_UNLOCK(pVM);
    MMHyperFree(pVM, pNew);
    return rc;
}


/**
 * Joins up two adjacent physical access handlers which has the same callbacks.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys1         Start physical address of the first handler.
 * @param   GCPhys2         Start physical address of the second handler.
 */
VMMDECL(int) PGMHandlerPhysicalJoin(PVMCC pVM, RTGCPHYS GCPhys1, RTGCPHYS GCPhys2)
{
    /*
     * Get the handlers.
     */
    int rc;
    PGM_LOCK_VOID(pVM);
    PPGMPHYSHANDLER pCur1 = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys1);
    if (RT_LIKELY(pCur1))
    {
        PPGMPHYSHANDLER pCur2 = (PPGMPHYSHANDLER)RTAvlroGCPhysGet(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys2);
        if (RT_LIKELY(pCur2))
        {
            /*
             * Make sure that they are adjacent, and that they've got the same callbacks.
             */
            if (RT_LIKELY(pCur1->Core.KeyLast + 1 == pCur2->Core.Key))
            {
                if (RT_LIKELY(pCur1->hType == pCur2->hType))
                {
                    PPGMPHYSHANDLER pCur3 = (PPGMPHYSHANDLER)RTAvlroGCPhysRemove(&pVM->pgm.s.CTX_SUFF(pTrees)->PhysHandlers, GCPhys2);
                    if (RT_LIKELY(pCur3 == pCur2))
                    {
                        pCur1->Core.KeyLast  = pCur2->Core.KeyLast;
                        pCur1->cPages        = (pCur1->Core.KeyLast - (pCur1->Core.Key & X86_PTE_PAE_PG_MASK) + GUEST_PAGE_SIZE) >> GUEST_PAGE_SHIFT;
                        LogFlow(("PGMHandlerPhysicalJoin: %RGp-%RGp %RGp-%RGp\n",
                                 pCur1->Core.Key, pCur1->Core.KeyLast, pCur2->Core.Key, pCur2->Core.KeyLast));
                        pVM->pgm.s.pLastPhysHandlerR0 = 0;
                        pVM->pgm.s.pLastPhysHandlerR3 = 0;
                        PGMHandlerPhysicalTypeRelease(pVM, pCur2->hType);
                        MMHyperFree(pVM, pCur2);
                        PGM_UNLOCK(pVM);
                        return VINF_SUCCESS;
                    }

                    Assert(pCur3 == pCur2);
                    rc = VERR_PGM_PHYS_HANDLER_IPE;
                }
                else
                {
                    AssertMsgFailed(("mismatching handlers\n"));
                    rc = VERR_ACCESS_DENIED;
                }
            }
            else
            {
                AssertMsgFailed(("not adjacent: %RGp-%RGp %RGp-%RGp\n",
                                 pCur1->Core.Key, pCur1->Core.KeyLast, pCur2->Core.Key, pCur2->Core.KeyLast));
                rc = VERR_INVALID_PARAMETER;
            }
        }
        else
        {
            AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys2));
            rc = VERR_PGM_HANDLER_NOT_FOUND;
        }
    }
    else
    {
        AssertMsgFailed(("Didn't find range starting at %RGp\n", GCPhys1));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }
    PGM_UNLOCK(pVM);
    return rc;

}

#endif /* unused */

/**
 * Resets any modifications to individual pages in a physical page access
 * handler region.
 *
 * This is used in pair with PGMHandlerPhysicalPageTempOff(),
 * PGMHandlerPhysicalPageAliasMmio2() or PGMHandlerPhysicalPageAliasHC().
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The start address of the handler regions, i.e. what you
 *                      passed to PGMR3HandlerPhysicalRegister(),
 *                      PGMHandlerPhysicalRegisterEx() or
 *                      PGMHandlerPhysicalModify().
 */
VMMDECL(int) PGMHandlerPhysicalReset(PVMCC pVM, RTGCPHYS GCPhys)
{
    LogFlow(("PGMHandlerPhysicalReset GCPhys=%RGp\n", GCPhys));
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Find the handler.
     */
    PPGMPHYSHANDLER pCur;
    rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookup(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pCur);
    if (RT_SUCCESS(rc))
    {
        Assert(pCur->Key == GCPhys);

        /*
         * Validate kind.
         */
        PCPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
        switch (pCurType->enmKind)
        {
            case PGMPHYSHANDLERKIND_WRITE:
            case PGMPHYSHANDLERKIND_ALL:
            case PGMPHYSHANDLERKIND_MMIO: /* NOTE: Only use when clearing MMIO ranges with aliased MMIO2 pages! */
            {
                STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysHandlerReset)); /** @todo move out of switch */
                PPGMRAMRANGE pRam = pgmPhysGetRange(pVM, GCPhys);
                Assert(pRam);
                Assert(pRam->GCPhys     <= pCur->Key);
                Assert(pRam->GCPhysLast >= pCur->KeyLast);

                if (pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO)
                {
                    /*
                     * Reset all the PGMPAGETYPE_MMIO2_ALIAS_MMIO pages first and that's it.
                     * This could probably be optimized a bit wrt to flushing, but I'm too lazy
                     * to do that now...
                     */
                    if (pCur->cAliasedPages)
                    {
                        PPGMPAGE    pPage        = &pRam->aPages[(pCur->Key - pRam->GCPhys) >> GUEST_PAGE_SHIFT];
                        RTGCPHYS    GCPhysPage   = pCur->Key;
                        uint32_t    cLeft        = pCur->cPages;
                        bool        fFlushIemTlb = false;
                        while (cLeft-- > 0)
                        {
                            if (   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO
                                || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO)
                            {
                                fFlushIemTlb |= PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO;
                                Assert(pCur->cAliasedPages > 0);
                                pgmHandlerPhysicalResetAliasedPage(pVM, pPage, GCPhysPage, pRam,
                                                                   false /*fDoAccounting*/, false /*fFlushIemTlbs*/);
                                --pCur->cAliasedPages;
#ifndef VBOX_STRICT
                                if (pCur->cAliasedPages == 0)
                                    break;
#endif
                            }
                            Assert(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO);
                            GCPhysPage += GUEST_PAGE_SIZE;
                            pPage++;
                        }
                        Assert(pCur->cAliasedPages == 0);

                        /*
                         * Flush IEM TLBs in case they contain any references to aliased pages.
                         * This is only necessary for MMIO2 aliases.
                         */
                        if (fFlushIemTlb)
                            IEMTlbInvalidateAllPhysicalAllCpus(pVM, NIL_VMCPUID, IEMTLBPHYSFLUSHREASON_HANDLER_RESET);
                    }
                }
                else if (pCur->cTmpOffPages > 0)
                {
                    /*
                     * Set the flags and flush shadow PT entries.
                     */
                    rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pCur, pRam, NULL /*pvBitmap*/, 0 /*offBitmap*/);
                }

                pCur->cAliasedPages = 0;
                pCur->cTmpOffPages  = 0;

                rc = VINF_SUCCESS;
                break;
            }

            /*
             * Invalid.
             */
            default:
                AssertMsgFailed(("Invalid type %d/%#x! Corruption!\n",  pCurType->enmKind, pCur->hType));
                rc = VERR_PGM_PHYS_HANDLER_IPE;
                break;
        }
    }
    else if (rc == VERR_NOT_FOUND)
    {
        AssertMsgFailed(("Didn't find MMIO Range starting at %#x\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Special version of PGMHandlerPhysicalReset used by MMIO2 w/ dirty page
 * tracking.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The start address of the handler region.
 * @param   pvBitmap    Dirty bitmap. Caller has cleared this already, only
 *                      dirty bits will be set. Caller also made sure it's big
 *                      enough.
 * @param   offBitmap   Dirty bitmap offset.
 * @remarks Caller must own the PGM critical section.
 */
DECLHIDDEN(int) pgmHandlerPhysicalResetMmio2WithBitmap(PVMCC pVM, RTGCPHYS GCPhys, void *pvBitmap, uint32_t offBitmap)
{
    LogFlow(("pgmHandlerPhysicalResetMmio2WithBitmap GCPhys=%RGp\n", GCPhys));
    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Find the handler.
     */
    PPGMPHYSHANDLER pCur;
    int rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookup(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pCur);
    if (RT_SUCCESS(rc))
    {
        Assert(pCur->Key == GCPhys);

        /*
         * Validate kind.
         */
        PCPGMPHYSHANDLERTYPEINT pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
        if (   pCurType
            && pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE)
        {
            STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysHandlerReset));

#ifdef VBOX_STRICT
            PPGMRAMRANGE const pRamStrict = pgmPhysGetRange(pVM, GCPhys);
            Assert(pRamStrict && pRamStrict->GCPhys     <= pCur->Key);
            Assert(pRamStrict && pRamStrict->GCPhysLast >= pCur->KeyLast);
#endif

            /*
             * Set the flags and flush shadow PT entries.
             */
            if (pCur->cTmpOffPages > 0)
            {
                PPGMRAMRANGE const pRam = pgmPhysGetRange(pVM, GCPhys);
                if (pRam) /* paranoia */
                    rc = pgmHandlerPhysicalSetRamFlagsAndFlushShadowPTs(pVM, pCur, pRam, pvBitmap, offBitmap);
                else
                    AssertFailed();
                pCur->cTmpOffPages  = 0;
            }
            else
                rc = VINF_SUCCESS;
        }
        else
        {
            AssertFailed();
            rc = VERR_WRONG_TYPE;
        }
    }
    else if (rc == VERR_NOT_FOUND)
    {
        AssertMsgFailed(("Didn't find MMIO Range starting at %#x\n", GCPhys));
        rc = VERR_PGM_HANDLER_NOT_FOUND;
    }

    return rc;
}


/**
 * Temporarily turns off the access monitoring of a page within a monitored
 * physical write/all page access handler region.
 *
 * Use this when no further \#PFs are required for that page. Be aware that
 * a page directory sync might reset the flags, and turn on access monitoring
 * for the page.
 *
 * The caller must do required page table modifications.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The start address of the access handler. This
 *                              must be a fully page aligned range or we risk
 *                              messing up other handlers installed for the
 *                              start and end pages.
 * @param   GCPhysPage          The physical address of the page to turn off
 *                              access monitoring for.
 */
VMMDECL(int)  PGMHandlerPhysicalPageTempOff(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage)
{
    LogFlow(("PGMHandlerPhysicalPageTempOff GCPhysPage=%RGp\n", GCPhysPage));
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Validate the range.
     */
    PPGMPHYSHANDLER pCur;
    rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookup(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pCur);
    if (RT_SUCCESS(rc))
    {
        Assert(pCur->Key == GCPhys);
        if (RT_LIKELY(   GCPhysPage >= pCur->Key
                      && GCPhysPage <= pCur->KeyLast))
        {
            Assert(!(pCur->Key & GUEST_PAGE_OFFSET_MASK));
            Assert((pCur->KeyLast & GUEST_PAGE_OFFSET_MASK) == GUEST_PAGE_OFFSET_MASK);

            PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE(pVM, pCur);
            AssertReturnStmt(   pCurType
                             && (   pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE
                                 || pCurType->enmKind == PGMPHYSHANDLERKIND_ALL),
                             PGM_UNLOCK(pVM), VERR_ACCESS_DENIED);

            /*
             * Change the page status.
             */
            PPGMPAGE     pPage;
            PPGMRAMRANGE pRam;
            rc = pgmPhysGetPageAndRangeEx(pVM, GCPhysPage, &pPage, &pRam);
            AssertReturnStmt(RT_SUCCESS_NP(rc), PGM_UNLOCK(pVM), rc);
            if (PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_DISABLED)
            {
                PGM_PAGE_SET_HNDL_PHYS_STATE_ONLY(pPage, PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
                pCur->cTmpOffPages++;

#ifdef VBOX_WITH_NATIVE_NEM
                /* Tell NEM about the protection change (VGA is using this to track dirty pages). */
                if (VM_IS_NEM_ENABLED(pVM))
                {
                    uint8_t     u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                    PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
                    NEMHCNotifyPhysPageProtChanged(pVM, GCPhysPage, PGM_PAGE_GET_HCPHYS(pPage),
                                                   PGM_RAMRANGE_CALC_PAGE_R3PTR(pRam, GCPhysPage),
                                                   pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
                    PGM_PAGE_SET_NEM_STATE(pPage, u2State);
                }
#endif
            }
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }
        PGM_UNLOCK(pVM);
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n", GCPhysPage, pCur->Key, pCur->KeyLast));
        return VERR_INVALID_PARAMETER;
    }
    PGM_UNLOCK(pVM);

    if (rc == VERR_NOT_FOUND)
    {
        AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
        return VERR_PGM_HANDLER_NOT_FOUND;
    }
    return rc;
}


/**
 * Resolves an MMIO2 page.
 *
 * Caller as taken the PGM lock.
 *
 * @returns Pointer to the page if valid, NULL otherwise
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         The device owning it.
 * @param   hMmio2          The MMIO2 region.
 * @param   offMmio2Page    The offset into the region.
 */
static PPGMPAGE pgmPhysResolveMmio2PageLocked(PVMCC pVM, PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS offMmio2Page)
{
    /* Only works if the handle is in the handle table! */
    AssertReturn(hMmio2 != 0, NULL);
    uint32_t const cMmio2Ranges = RT_MIN(pVM->pgm.s.cMmio2Ranges, RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges));
    AssertReturn(hMmio2 <= cMmio2Ranges, NULL);
    AssertCompile(RT_ELEMENTS(pVM->pgm.s.apMmio2RamRanges)    == RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges));
#ifdef IN_RING0
    AssertCompile(RT_ELEMENTS(pVM->pgmr0.s.apMmio2RamRanges)  == RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges));
    AssertCompile(RT_ELEMENTS(pVM->pgmr0.s.acMmio2RangePages) == RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges));
#endif
    uint32_t const idxFirst = hMmio2 - 1U;

    /* Must check the first one for PGMREGMMIO2RANGE_F_FIRST_CHUNK. */
    AssertReturn(pVM->pgm.s.aMmio2Ranges[idxFirst].fFlags & PGMREGMMIO2RANGE_F_FIRST_CHUNK, NULL);
#ifdef IN_RING0
    AssertReturn(pVM->pgmr0.s.ahMmio2MapObjs[idxFirst] != NIL_RTR0MEMOBJ, NULL); /* Only the first chunk has a backing object. */
#endif

    /* Loop thru the sub-ranges till we find the one covering offMmio2. */
    for (uint32_t idx = idxFirst; idx < cMmio2Ranges; idx++)
    {
#ifdef IN_RING3
        AssertReturn(pVM->pgm.s.aMmio2Ranges[idx].pDevInsR3 == pDevIns, NULL);
#else
        AssertReturn(pVM->pgm.s.aMmio2Ranges[idx].pDevInsR3 == pDevIns->pDevInsForR3, NULL);
#endif

        /* Does it match the offset? */
        PPGMRAMRANGE const pRamRange = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apMmio2RamRanges[idx];
        AssertReturn(pRamRange, NULL);
#ifdef IN_RING3
        RTGCPHYS const     cbRange   = RT_MIN(pRamRange->cb, pVM->pgm.s.aMmio2Ranges[idx].cbReal);
#else
        RTGCPHYS const     cbRange   = RT_MIN(pRamRange->cb, (RTGCPHYS)pVM->pgmr0.s.acMmio2RangePages[idx] << GUEST_PAGE_SHIFT);
#endif
        if (offMmio2Page < cbRange)
            return &pRamRange->aPages[offMmio2Page >> GUEST_PAGE_SHIFT];

        /* Advance. */
        AssertReturn(!(pVM->pgm.s.aMmio2Ranges[idx].fFlags & PGMREGMMIO2RANGE_F_LAST_CHUNK), NULL);
        offMmio2Page -= cbRange;
    }
    AssertFailed();
    return NULL;
}


/**
 * Replaces an MMIO page with an MMIO2 page.
 *
 * This is a worker for IOMMMIOMapMMIO2Page that works in a similar way to
 * PGMHandlerPhysicalPageTempOff but for an MMIO page. Since an MMIO page has no
 * backing, the caller must provide a replacement page. For various reasons the
 * replacement page must be an MMIO2 page.
 *
 * The caller must do required page table modifications. You can get away
 * without making any modifications since it's an MMIO page, the cost is an extra
 * \#PF which will the resync the page.
 *
 * Call PGMHandlerPhysicalReset() to restore the MMIO page.
 *
 * The caller may still get handler callback even after this call and must be
 * able to deal correctly with such calls. The reason for these callbacks are
 * either that we're executing in the recompiler (which doesn't know about this
 * arrangement) or that we've been restored from saved state (where we won't
 * save the change).
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The start address of the access handler. This
 *                              must be a fully page aligned range or we risk
 *                              messing up other handlers installed for the
 *                              start and end pages.
 * @param   GCPhysPage          The physical address of the page to turn off
 *                              access monitoring for and replace with the MMIO2
 *                              page.
 * @param   pDevIns             The device instance owning @a hMmio2.
 * @param   hMmio2              Handle to the MMIO2 region containing the page
 *                              to remap in the the MMIO page at @a GCPhys.
 * @param   offMmio2PageRemap   The offset into @a hMmio2 of the MMIO2 page that
 *                              should serve as backing memory.
 *
 * @remark  May cause a page pool flush if used on a page that is already
 *          aliased.
 *
 * @note    This trick does only work reliably if the two pages are never ever
 *          mapped in the same page table. If they are the page pool code will
 *          be confused should either of them be flushed. See the special case
 *          of zero page aliasing mentioned in #3170.
 *
 */
VMMDECL(int)  PGMHandlerPhysicalPageAliasMmio2(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage,
                                               PPDMDEVINS pDevIns, PGMMMIO2HANDLE hMmio2, RTGCPHYS offMmio2PageRemap)
{
#ifdef VBOX_WITH_PGM_NEM_MODE
    AssertReturn(!VM_IS_NEM_ENABLED(pVM) || !pVM->pgm.s.fNemMode, VERR_PGM_NOT_SUPPORTED_FOR_NEM_MODE);
#endif
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Resolve the MMIO2 reference.
     */
    PPGMPAGE pPageRemap = pgmPhysResolveMmio2PageLocked(pVM, pDevIns, hMmio2, offMmio2PageRemap);
    if (RT_LIKELY(pPageRemap))
        AssertMsgReturnStmt(PGM_PAGE_GET_TYPE(pPageRemap) == PGMPAGETYPE_MMIO2,
                            ("hMmio2=%RU64 offMmio2PageRemap=%RGp %R[pgmpage]\n", hMmio2, offMmio2PageRemap, pPageRemap),
                            PGM_UNLOCK(pVM), VERR_PGM_PHYS_NOT_MMIO2);
    else
    {
        PGM_UNLOCK(pVM);
        return VERR_OUT_OF_RANGE;
    }

    /*
     * Lookup and validate the range.
     */
    PPGMPHYSHANDLER pCur;
    rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookup(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pCur);
    if (RT_SUCCESS(rc))
    {
        Assert(pCur->Key == GCPhys);
        if (RT_LIKELY(   GCPhysPage >= pCur->Key
                      && GCPhysPage <= pCur->KeyLast))
        {
            PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
            AssertReturnStmt(pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO, PGM_UNLOCK(pVM), VERR_ACCESS_DENIED);
            AssertReturnStmt(!(pCur->Key & GUEST_PAGE_OFFSET_MASK), PGM_UNLOCK(pVM), VERR_INVALID_PARAMETER);
            AssertReturnStmt((pCur->KeyLast & GUEST_PAGE_OFFSET_MASK) == GUEST_PAGE_OFFSET_MASK,
                             PGM_UNLOCK(pVM), VERR_INVALID_PARAMETER);

            /*
             * Validate the page.
             */
            PPGMPAGE     pPage;
            PPGMRAMRANGE pRam;
            rc = pgmPhysGetPageAndRangeEx(pVM, GCPhysPage, &pPage, &pRam);
            AssertReturnStmt(RT_SUCCESS_NP(rc), PGM_UNLOCK(pVM), rc);
            if (PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO)
            {
                AssertMsgReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO,
                                ("GCPhysPage=%RGp %R[pgmpage]\n", GCPhysPage, pPage),
                                VERR_PGM_PHYS_NOT_MMIO2);
                if (PGM_PAGE_GET_HCPHYS(pPage) == PGM_PAGE_GET_HCPHYS(pPageRemap))
                {
                    PGM_UNLOCK(pVM);
                    return VINF_PGM_HANDLER_ALREADY_ALIASED;
                }

                /*
                 * The page is already mapped as some other page, reset it
                 * to an MMIO/ZERO page before doing the new mapping.
                 */
                Log(("PGMHandlerPhysicalPageAliasMmio2: GCPhysPage=%RGp (%R[pgmpage]; %RHp -> %RHp\n",
                     GCPhysPage, pPage, PGM_PAGE_GET_HCPHYS(pPage), PGM_PAGE_GET_HCPHYS(pPageRemap)));
                pgmHandlerPhysicalResetAliasedPage(pVM, pPage, GCPhysPage, pRam,
                                                   false /*fDoAccounting*/, false /*fFlushIemTlbs*/);
                pCur->cAliasedPages--;

                /* Since this may be present in the TLB and now be wrong, invalid
                   the guest physical address part of the IEM TLBs.  Note, we do
                   this here as we will not invalid */
                IEMTlbInvalidateAllPhysicalAllCpus(pVM, NIL_VMCPUID, IEMTLBPHYSFLUSHREASON_MMIO2_ALIAS);
            }
            Assert(PGM_PAGE_IS_ZERO(pPage));

            /*
             * Do the actual remapping here.
             * This page now serves as an alias for the backing memory specified.
             */
            LogFlow(("PGMHandlerPhysicalPageAliasMmio2: %RGp (%R[pgmpage]) alias for %RU64/%RGp (%R[pgmpage])\n",
                     GCPhysPage, pPage, hMmio2, offMmio2PageRemap, pPageRemap ));
            PGM_PAGE_SET_HCPHYS(pVM, pPage, PGM_PAGE_GET_HCPHYS(pPageRemap));
            PGM_PAGE_SET_TYPE(pVM, pPage, PGMPAGETYPE_MMIO2_ALIAS_MMIO);
            PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ALLOCATED);
            PGM_PAGE_SET_PAGEID(pVM, pPage, PGM_PAGE_GET_PAGEID(pPageRemap));
            PGM_PAGE_SET_HNDL_PHYS_STATE_ONLY(pPage, PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
            pCur->cAliasedPages++;
            Assert(pCur->cAliasedPages <= pCur->cPages);

            /*
             * Flush its TLB entry.
             *
             * Not calling IEMTlbInvalidateAllPhysicalAllCpus here to conserve
             * all the other IEM TLB entires.  When this one is kicked out and
             * reloaded, it will be using the MMIO2 alias, but till then we'll
             * continue doing MMIO.
             */
            pgmPhysInvalidatePageMapTLBEntry(pVM, GCPhysPage);
            /** @todo Do some preformance checks of calling
             *        IEMTlbInvalidateAllPhysicalAllCpus when in IEM mode, to see if it
             *        actually makes sense or not.  Screen updates are typically massive
             *        and important when this kind of aliasing is used, so it may pay of... */

#ifdef VBOX_WITH_NATIVE_NEM
            /* Tell NEM about the backing and protection change. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                NEMHCNotifyPhysPageChanged(pVM, GCPhysPage, pVM->pgm.s.HCPhysZeroPg, PGM_PAGE_GET_HCPHYS(pPage),
                                           PGM_RAMRANGE_CALC_PAGE_R3PTR(pRam, GCPhysPage),
                                           pgmPhysPageCalcNemProtection(pPage, PGMPAGETYPE_MMIO2_ALIAS_MMIO),
                                           PGMPAGETYPE_MMIO2_ALIAS_MMIO, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
#endif
            LogFlow(("PGMHandlerPhysicalPageAliasMmio2: => %R[pgmpage]\n", pPage));
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }

        PGM_UNLOCK(pVM);
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n", GCPhysPage, pCur->Key, pCur->KeyLast));
        return VERR_INVALID_PARAMETER;
    }

    PGM_UNLOCK(pVM);
    if (rc == VERR_NOT_FOUND)
    {
        AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
        return VERR_PGM_HANDLER_NOT_FOUND;
    }
    return rc;
}


/**
 * Replaces an MMIO page with an arbitrary HC page in the shadow page tables.
 *
 * This differs from PGMHandlerPhysicalPageAliasMmio2 in that the page doesn't
 * need to be a known MMIO2 page and that only shadow paging may access the
 * page. The latter distinction is important because the only use for this
 * feature is for mapping the special APIC access page that VT-x uses to detect
 * APIC MMIO operations, the page is shared between all guest CPUs and actually
 * not written to. At least at the moment.
 *
 * The caller must do required page table modifications. You can get away
 * without making any modifications since it's an MMIO page, the cost is an extra
 * \#PF which will the resync the page.
 *
 * Call PGMHandlerPhysicalReset() to restore the MMIO page.
 *
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPhys              The start address of the access handler. This
 *                              must be a fully page aligned range or we risk
 *                              messing up other handlers installed for the
 *                              start and end pages.
 * @param   GCPhysPage          The physical address of the page to turn off
 *                              access monitoring for.
 * @param   HCPhysPageRemap     The physical address of the HC page that
 *                              serves as backing memory.
 *
 * @remark  May cause a page pool flush if used on a page that is already
 *          aliased.
 */
VMMDECL(int)  PGMHandlerPhysicalPageAliasHC(PVMCC pVM, RTGCPHYS GCPhys, RTGCPHYS GCPhysPage, RTHCPHYS HCPhysPageRemap)
{
///    Assert(!IOMIsLockOwner(pVM)); /* We mustn't own any other locks when calling this */
#ifdef VBOX_WITH_PGM_NEM_MODE
    AssertReturn(!VM_IS_NEM_ENABLED(pVM) || !pVM->pgm.s.fNemMode, VERR_PGM_NOT_SUPPORTED_FOR_NEM_MODE);
#endif
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Lookup and validate the range.
     */
    PPGMPHYSHANDLER pCur;
    rc = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookup(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, GCPhys, &pCur);
    if (RT_SUCCESS(rc))
    {
        Assert(pCur->Key == GCPhys);
        if (RT_LIKELY(    GCPhysPage >= pCur->Key
                      &&  GCPhysPage <= pCur->KeyLast))
        {
            PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
            AssertReturnStmt(pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO, PGM_UNLOCK(pVM), VERR_ACCESS_DENIED);
            AssertReturnStmt(!(pCur->Key & GUEST_PAGE_OFFSET_MASK), PGM_UNLOCK(pVM), VERR_INVALID_PARAMETER);
            AssertReturnStmt((pCur->KeyLast & GUEST_PAGE_OFFSET_MASK) == GUEST_PAGE_OFFSET_MASK,
                             PGM_UNLOCK(pVM), VERR_INVALID_PARAMETER);

            /*
             * Get and validate the pages.
             */
            PPGMPAGE     pPage = NULL;
#ifdef VBOX_WITH_NATIVE_NEM
            PPGMRAMRANGE pRam  = NULL;
            rc = pgmPhysGetPageAndRangeEx(pVM, GCPhysPage, &pPage, &pRam);
#else
            rc = pgmPhysGetPageEx(pVM, GCPhysPage, &pPage);
#endif
            AssertReturnStmt(RT_SUCCESS_NP(rc), PGM_UNLOCK(pVM), rc);
            if (PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_MMIO)
            {
                PGM_UNLOCK(pVM);
                AssertMsgReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_SPECIAL_ALIAS_MMIO,
                                ("GCPhysPage=%RGp %R[pgmpage]\n", GCPhysPage, pPage),
                                VERR_PGM_PHYS_NOT_MMIO2);
                return VINF_PGM_HANDLER_ALREADY_ALIASED;
            }
            Assert(PGM_PAGE_IS_ZERO(pPage));

            /*
             * Do the actual remapping here.
             * This page now serves as an alias for the backing memory
             * specified as far as shadow paging is concerned.
             */
            LogFlow(("PGMHandlerPhysicalPageAliasHC: %RGp (%R[pgmpage]) alias for %RHp\n",
                     GCPhysPage, pPage, HCPhysPageRemap));
            PGM_PAGE_SET_HCPHYS(pVM, pPage, HCPhysPageRemap);
            PGM_PAGE_SET_TYPE(pVM, pPage, PGMPAGETYPE_SPECIAL_ALIAS_MMIO);
            PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ALLOCATED);
            PGM_PAGE_SET_PAGEID(pVM, pPage, NIL_GMM_PAGEID);
            PGM_PAGE_SET_HNDL_PHYS_STATE_ONLY(pPage, PGM_PAGE_HNDL_PHYS_STATE_DISABLED);
            pCur->cAliasedPages++;
            Assert(pCur->cAliasedPages <= pCur->cPages);

            /*
             * Flush its TLB entry.
             *
             * Not calling IEMTlbInvalidateAllPhysicalAllCpus here as special
             * aliased MMIO pages are handled like MMIO by the IEM TLB.
             */
            pgmPhysInvalidatePageMapTLBEntry(pVM, GCPhysPage);

#ifdef VBOX_WITH_NATIVE_NEM
            /* Tell NEM about the backing and protection change. */
            if (VM_IS_NEM_ENABLED(pVM))
            {
                uint8_t u2State = PGM_PAGE_GET_NEM_STATE(pPage);
                NEMHCNotifyPhysPageChanged(pVM, GCPhysPage, pVM->pgm.s.HCPhysZeroPg, PGM_PAGE_GET_HCPHYS(pPage),
                                           PGM_RAMRANGE_CALC_PAGE_R3PTR(pRam, GCPhysPage),
                                           pgmPhysPageCalcNemProtection(pPage, PGMPAGETYPE_SPECIAL_ALIAS_MMIO),
                                           PGMPAGETYPE_SPECIAL_ALIAS_MMIO, &u2State);
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            }
#endif
            LogFlow(("PGMHandlerPhysicalPageAliasHC: => %R[pgmpage]\n", pPage));
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }
        PGM_UNLOCK(pVM);
        AssertMsgFailed(("The page %#x is outside the range %#x-%#x\n", GCPhysPage, pCur->Key, pCur->KeyLast));
        return VERR_INVALID_PARAMETER;
    }
    PGM_UNLOCK(pVM);

    if (rc == VERR_NOT_FOUND)
    {
        AssertMsgFailed(("Specified physical handler start address %#x is invalid.\n", GCPhys));
        return VERR_PGM_HANDLER_NOT_FOUND;
    }
    return rc;
}


/**
 * Checks if a physical range is handled
 *
 * @returns boolean
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      Start physical address earlier passed to PGMR3HandlerPhysicalRegister().
 * @remarks Caller must take the PGM lock...
 * @thread  EMT.
 */
VMMDECL(bool) PGMHandlerPhysicalIsRegistered(PVMCC pVM, RTGCPHYS GCPhys)
{
    /*
     * Find the handler.
     */
    PGM_LOCK_VOID(pVM);
    PPGMPHYSHANDLER pCur;
    int rc = pgmHandlerPhysicalLookup(pVM, GCPhys, &pCur);
    if (RT_SUCCESS(rc))
    {
#ifdef VBOX_STRICT
        Assert(GCPhys >= pCur->Key && GCPhys <= pCur->KeyLast);
        PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
        Assert(   pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE
               || pCurType->enmKind == PGMPHYSHANDLERKIND_ALL
               || pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO);
#endif
        PGM_UNLOCK(pVM);
        return true;
    }
    PGM_UNLOCK(pVM);
    return false;
}


/**
 * Checks if it's an disabled all access handler or write access handler at the
 * given address.
 *
 * @returns true if it's an all access handler, false if it's a write access
 *          handler.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The address of the page with a disabled handler.
 *
 * @remarks The caller, PGMR3PhysTlbGCPhys2Ptr, must hold the PGM lock.
 */
bool pgmHandlerPhysicalIsAll(PVMCC pVM, RTGCPHYS GCPhys)
{
    PGM_LOCK_VOID(pVM);
    PPGMPHYSHANDLER pCur;
    int rc = pgmHandlerPhysicalLookup(pVM, GCPhys, &pCur);
    AssertRCReturnStmt(rc, PGM_UNLOCK(pVM), true);

    /* Only whole pages can be disabled. */
    Assert(   pCur->Key     <= (GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK)
           && pCur->KeyLast >= (GCPhys | GUEST_PAGE_OFFSET_MASK));

    PCPGMPHYSHANDLERTYPEINT const pCurType = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
    Assert(   pCurType->enmKind == PGMPHYSHANDLERKIND_WRITE
           || pCurType->enmKind == PGMPHYSHANDLERKIND_ALL
           || pCurType->enmKind == PGMPHYSHANDLERKIND_MMIO); /* sanity */
    bool const fRet = pCurType->enmKind != PGMPHYSHANDLERKIND_WRITE;
    PGM_UNLOCK(pVM);
    return fRet;
}

#ifdef VBOX_STRICT

/**
 * State structure used by the PGMAssertHandlerAndFlagsInSync() function
 * and its AVL enumerators.
 */
typedef struct PGMAHAFIS
{
    /** The current physical address. */
    RTGCPHYS    GCPhys;
    /** Number of errors. */
    unsigned    cErrors;
    /** Pointer to the VM. */
    PVM         pVM;
} PGMAHAFIS, *PPGMAHAFIS;


/**
 * Asserts that the handlers+guest-page-tables == ramrange-flags and
 * that the physical addresses associated with virtual handlers are correct.
 *
 * @returns Number of mismatches.
 * @param   pVM     The cross context VM structure.
 */
VMMDECL(unsigned) PGMAssertHandlerAndFlagsInSync(PVMCC pVM)
{
    PGMAHAFIS   State;
    State.GCPhys  = 0;
    State.cErrors = 0;
    State.pVM     = pVM;

    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Check the RAM flags against the handlers.
     */
    PPGMPHYSHANDLERTREE const pPhysHandlerTree = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree;
    uint32_t const            cLookupEntries   = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries,
                                                        RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    for (uint32_t idxLookup = 0; idxLookup < cLookupEntries; idxLookup++)
    {
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        AssertContinue(idRamRange < RT_ELEMENTS(pVM->pgm.s.apRamRanges));
        PPGMRAMRANGE const pRam = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
        AssertContinue(pRam);
        const uint32_t cPages = pRam->cb >> GUEST_PAGE_SHIFT;
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            PGMPAGE const *pPage = &pRam->aPages[iPage];
            if (PGM_PAGE_HAS_ANY_HANDLERS(pPage))
            {
                State.GCPhys = pRam->GCPhys + (iPage << GUEST_PAGE_SHIFT);

                /*
                 * Physical first - calculate the state based on the handlers
                 *                  active on the page, then compare.
                 */
                if (PGM_PAGE_HAS_ANY_PHYSICAL_HANDLERS(pPage))
                {
                    /* the first */
                    PPGMPHYSHANDLER pPhys;
                    int rc = pPhysHandlerTree->lookup(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator, State.GCPhys, &pPhys);
                    if (rc == VERR_NOT_FOUND)
                    {
                        rc = pPhysHandlerTree->lookupMatchingOrAbove(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator,
                                                                     State.GCPhys, &pPhys);
                        if (RT_SUCCESS(rc))
                        {
                            Assert(pPhys->Key >= State.GCPhys);
                            if (pPhys->Key > (State.GCPhys + GUEST_PAGE_SIZE - 1))
                                pPhys = NULL;
                        }
                        else
                            AssertLogRelMsgReturn(rc == VERR_NOT_FOUND, ("rc=%Rrc GCPhys=%RGp\n", rc, State.GCPhys), 999);
                    }
                    else
                        AssertLogRelMsgReturn(RT_SUCCESS(rc), ("rc=%Rrc GCPhys=%RGp\n", rc, State.GCPhys), 999);

                    if (pPhys)
                    {
                        PCPGMPHYSHANDLERTYPEINT pPhysType = pgmHandlerPhysicalTypeHandleToPtr(pVM, pPhys->hType);
                        unsigned   uState   = pPhysType->uState;
                        bool const fNotInHm = pPhysType->fNotInHm; /* whole pages, so no need to accumulate sub-page configs. */

                        /* more? */
                        while (pPhys->KeyLast < (State.GCPhys | GUEST_PAGE_OFFSET_MASK))
                        {
                            PPGMPHYSHANDLER pPhys2;
                            rc = pPhysHandlerTree->lookupMatchingOrAbove(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator,
                                                                         pPhys->KeyLast + 1, &pPhys2);
                            if (rc == VERR_NOT_FOUND)
                                break;
                            AssertLogRelMsgReturn(RT_SUCCESS(rc), ("rc=%Rrc KeyLast+1=%RGp\n", rc, pPhys->KeyLast + 1), 999);
                            if (pPhys2->Key > (State.GCPhys | GUEST_PAGE_OFFSET_MASK))
                                break;
                            PCPGMPHYSHANDLERTYPEINT pPhysType2 = pgmHandlerPhysicalTypeHandleToPtr(pVM, pPhys2->hType);
                            uState = RT_MAX(uState, pPhysType2->uState);
                            pPhys = pPhys2;
                        }

                        /* compare.*/
                        if (    PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != uState
                            &&  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_DISABLED)
                        {
                            AssertMsgFailed(("ram range vs phys handler flags mismatch. GCPhys=%RGp state=%d expected=%d %s\n",
                                             State.GCPhys, PGM_PAGE_GET_HNDL_PHYS_STATE(pPage), uState, pPhysType->pszDesc));
                            State.cErrors++;
                        }
                        AssertMsgStmt(PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage) == fNotInHm,
                                      ("ram range vs phys handler flags mismatch. GCPhys=%RGp fNotInHm=%d, %d %s\n",
                                       State.GCPhys, PGM_PAGE_IS_HNDL_PHYS_NOT_IN_HM(pPage), fNotInHm, pPhysType->pszDesc),
                                      State.cErrors++);
                    }
                    else
                    {
                        AssertMsgFailed(("ram range vs phys handler mismatch. no handler for GCPhys=%RGp\n", State.GCPhys));
                        State.cErrors++;
                    }
                }
            }
        } /* foreach page in ram range. */
    } /* foreach ram range. */

    /*
     * Do the reverse check for physical handlers.
     */
    /** @todo */

    return State.cErrors;
}

#endif /* VBOX_STRICT */

