/* $Id: PGMAllPhys.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM - Page Manager and Monitor, Physical Memory Addressing.
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
#define LOG_GROUP LOG_GROUP_PGM_PHYS
#define VBOX_WITHOUT_PAGING_BIT_FIELDS /* 64-bit bitfields are just asking for trouble. See @bugref{9841} and others. */
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/nem.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include "PGMInline.h"
#include <VBox/param.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <VBox/log.h>
#ifdef IN_RING3
# include <iprt/thread.h>
#elif defined(IN_RING0)
# include <iprt/mem.h>
# include <iprt/memobj.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Enable the physical TLB. */
#define PGM_WITH_PHYS_TLB

/** @def PGM_HANDLER_PHYS_IS_VALID_STATUS
 * Checks if valid physical access handler return code (normal handler, not PF).
 *
 * Checks if the given strict status code is one of the expected ones for a
 * physical access handler in the current context.
 *
 * @returns true or false.
 * @param   a_rcStrict      The status code.
 * @param   a_fWrite        Whether it is a write or read being serviced.
 *
 * @remarks We wish to keep the list of statuses here as short as possible.
 *          When changing, please make sure to update the PGMPhysRead,
 *          PGMPhysWrite, PGMPhysReadGCPtr and PGMPhysWriteGCPtr docs too.
 */
#ifdef IN_RING3
# define PGM_HANDLER_PHYS_IS_VALID_STATUS(a_rcStrict, a_fWrite) \
    (   (a_rcStrict) == VINF_SUCCESS \
     || (a_rcStrict) == VINF_PGM_HANDLER_DO_DEFAULT)
#elif defined(IN_RING0)
#define PGM_HANDLER_PHYS_IS_VALID_STATUS(a_rcStrict, a_fWrite) \
    (   (a_rcStrict) == VINF_SUCCESS \
     || (a_rcStrict) == VINF_PGM_HANDLER_DO_DEFAULT \
     \
     || (a_rcStrict) == ((a_fWrite) ? VINF_IOM_R3_MMIO_WRITE : VINF_IOM_R3_MMIO_READ) \
     || (a_rcStrict) == VINF_IOM_R3_MMIO_READ_WRITE \
     || ((a_rcStrict) == VINF_IOM_R3_MMIO_COMMIT_WRITE && (a_fWrite)) \
     \
     || (a_rcStrict) == VINF_EM_RAW_EMULATE_INSTR  \
     || (a_rcStrict) == VINF_EM_DBG_STOP \
     || (a_rcStrict) == VINF_EM_DBG_EVENT \
     || (a_rcStrict) == VINF_EM_DBG_BREAKPOINT \
     || (a_rcStrict) == VINF_EM_OFF \
     || (a_rcStrict) == VINF_EM_SUSPEND \
     || (a_rcStrict) == VINF_EM_RESET \
    )
#else
# error "Context?"
#endif

/** @def PGM_HANDLER_VIRT_IS_VALID_STATUS
 * Checks if valid virtual access handler return code (normal handler, not PF).
 *
 * Checks if the given strict status code is one of the expected ones for a
 * virtual access handler in the current context.
 *
 * @returns true or false.
 * @param   a_rcStrict      The status code.
 * @param   a_fWrite        Whether it is a write or read being serviced.
 *
 * @remarks We wish to keep the list of statuses here as short as possible.
 *          When changing, please make sure to update the PGMPhysRead,
 *          PGMPhysWrite, PGMPhysReadGCPtr and PGMPhysWriteGCPtr docs too.
 */
#ifdef IN_RING3
# define PGM_HANDLER_VIRT_IS_VALID_STATUS(a_rcStrict, a_fWrite) \
    (   (a_rcStrict) == VINF_SUCCESS \
     || (a_rcStrict) == VINF_PGM_HANDLER_DO_DEFAULT)
#elif defined(IN_RING0)
# define PGM_HANDLER_VIRT_IS_VALID_STATUS(a_rcStrict, a_fWrite) \
    (false /* no virtual handlers in ring-0! */ )
#else
# error "Context?"
#endif



/**
 * Calculate the actual table size.
 *
 * The memory is layed out like this:
 *      - PGMPHYSHANDLERTREE (8 bytes)
 *      - Allocation bitmap (8-byte size align)
 *      - Slab of PGMPHYSHANDLER. Start is 64 byte aligned.
 */
uint32_t pgmHandlerPhysicalCalcTableSizes(uint32_t *pcEntries, uint32_t *pcbTreeAndBitmap)
{
    /*
     * A minimum of 64 entries and a maximum of ~64K.
     */
    uint32_t cEntries = *pcEntries;
    if (cEntries <= 64)
        cEntries = 64;
    else if (cEntries >= _64K)
        cEntries = _64K;
    else
        cEntries = RT_ALIGN_32(cEntries, 16);

    /*
     * Do the initial calculation.
     */
    uint32_t cbBitmap        = RT_ALIGN_32(cEntries, 64) / 8;
    uint32_t cbTreeAndBitmap = RT_ALIGN_32(sizeof(PGMPHYSHANDLERTREE) + cbBitmap, 64);
    uint32_t cbTable         = cEntries * sizeof(PGMPHYSHANDLER);
    uint32_t cbTotal         = cbTreeAndBitmap + cbTable;

    /*
     * Align the total and try use up extra space from that.
     */
    uint32_t cbTotalAligned = RT_ALIGN_32(cbTotal, RT_MAX(HOST_PAGE_SIZE, _16K));
    uint32_t cAvail         = cbTotalAligned - cbTotal;
    cAvail /= sizeof(PGMPHYSHANDLER);
    if (cAvail >= 1)
        for (;;)
        {
            cbBitmap        = RT_ALIGN_32(cEntries, 64) / 8;
            cbTreeAndBitmap = RT_ALIGN_32(sizeof(PGMPHYSHANDLERTREE) + cbBitmap, 64);
            cbTable         = cEntries * sizeof(PGMPHYSHANDLER);
            cbTotal         = cbTreeAndBitmap + cbTable;
            if (cbTotal <= cbTotalAligned)
                break;
            cEntries--;
            Assert(cEntries >= 16);
        }

    /*
     * Return the result.
     */
    *pcbTreeAndBitmap = cbTreeAndBitmap;
    *pcEntries        = cEntries;
    return cbTotalAligned;
}



/*********************************************************************************************************************************
*   Access Handlers for ROM and MMIO2                                                                                            *
*********************************************************************************************************************************/

#ifndef IN_RING3

/**
 * @callback_method_impl{FNPGMRZPHYSPFHANDLER,
 *      \#PF access handler callback for guest ROM range write access.}
 *
 * @remarks The @a uUser argument is the PGMROMRANGE::GCPhys value.
 */
DECLCALLBACK(VBOXSTRICTRC) pgmPhysRomWritePfHandler(PVMCC pVM, PVMCPUCC pVCpu, RTGCUINT uErrorCode, PCPUMCTX pCtx,
                                                    RTGCPTR pvFault, RTGCPHYS GCPhysFault, uint64_t uUser)

{
    AssertReturn(uUser < RT_ELEMENTS(pVM->pgmr0.s.apRomRanges), VINF_EM_RAW_EMULATE_INSTR);
    PPGMROMRANGE const  pRom  = pVM->pgmr0.s.apRomRanges[uUser];
    AssertReturn(pRom, VINF_EM_RAW_EMULATE_INSTR);

    uint32_t const      iPage = (GCPhysFault - pRom->GCPhys) >> GUEST_PAGE_SHIFT;
    AssertReturn(iPage < (pRom->cb >> GUEST_PAGE_SHIFT), VERR_INTERNAL_ERROR_3);
#ifdef IN_RING0
    AssertReturn(iPage < pVM->pgmr0.s.acRomRangePages[uUser], VERR_INTERNAL_ERROR_2);
#endif

    RT_NOREF(uErrorCode, pvFault);
    Assert(uErrorCode & X86_TRAP_PF_RW); /* This shall not be used for read access! */

    int rc;
    switch (pRom->aPages[iPage].enmProt)
    {
        case PGMROMPROT_READ_ROM_WRITE_IGNORE:
        case PGMROMPROT_READ_RAM_WRITE_IGNORE:
        {
            /*
             * If it's a simple instruction which doesn't change the cpu state
             * we will simply skip it. Otherwise we'll have to defer it to REM.
             */
            uint32_t  cbOp;
            PDISSTATE pDis = &pVCpu->pgm.s.Dis;
            rc = EMInterpretDisasCurrent(pVCpu, pDis, &cbOp);
            if (     RT_SUCCESS(rc)
                &&   pDis->uCpuMode == DISCPUMODE_32BIT  /** @todo why does this matter? */
                &&  !(pDis->x86.fPrefix & (DISPREFIX_REPNE | DISPREFIX_REP | DISPREFIX_SEG)))
            {
                switch (pDis->x86.bOpCode)
                {
                    /** @todo Find other instructions we can safely skip, possibly
                     * adding this kind of detection to DIS or EM. */
                    case OP_MOV:
                        pCtx->rip += cbOp;
                        STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZGuestROMWriteHandled);
                        return VINF_SUCCESS;
                }
            }
            break;
        }

        case PGMROMPROT_READ_RAM_WRITE_RAM:
            pRom->aPages[iPage].LiveSave.fWrittenTo = true;
            rc = PGMHandlerPhysicalPageTempOff(pVM, pRom->GCPhys, GCPhysFault & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK);
            AssertRC(rc);
            break; /** @todo Must edit the shadow PT and restart the instruction, not use the interpreter! */

        case PGMROMPROT_READ_ROM_WRITE_RAM:
            /* Handle it in ring-3 because it's *way* easier there. */
            pRom->aPages[iPage].LiveSave.fWrittenTo = true;
            break;

        default:
            AssertMsgFailedReturn(("enmProt=%d iPage=%d GCPhysFault=%RGp\n",
                                   pRom->aPages[iPage].enmProt, iPage, GCPhysFault),
                                  VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }

    STAM_COUNTER_INC(&pVCpu->pgm.s.Stats.StatRZGuestROMWriteUnhandled);
    return VINF_EM_RAW_EMULATE_INSTR;
}

#endif /* !IN_RING3 */


/**
 * @callback_method_impl{FNPGMPHYSHANDLER,
 *      Access handler callback for ROM write accesses.}
 *
 * @remarks The @a uUser argument is the PGMROMRANGE::GCPhys value.
 */
DECLCALLBACK(VBOXSTRICTRC)
pgmPhysRomWriteHandler(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf,
                       PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin, uint64_t uUser)
{
    AssertReturn(uUser < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRomRanges), VERR_INTERNAL_ERROR_3);
    PPGMROMRANGE const  pRom  = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRomRanges[uUser];
    AssertReturn(pRom, VERR_INTERNAL_ERROR_3);

    uint32_t const      iPage    = (GCPhys - pRom->GCPhys) >> GUEST_PAGE_SHIFT;
    AssertReturn(iPage < (pRom->cb >> GUEST_PAGE_SHIFT), VERR_INTERNAL_ERROR_2);
#ifdef IN_RING0
    AssertReturn(iPage < pVM->pgmr0.s.acRomRangePages[uUser], VERR_INTERNAL_ERROR_2);
#endif
    PPGMROMPAGE const   pRomPage = &pRom->aPages[iPage];

    Log5(("pgmPhysRomWriteHandler: %d %c %#08RGp %#04zx\n", pRomPage->enmProt, enmAccessType == PGMACCESSTYPE_READ ? 'R' : 'W', GCPhys, cbBuf));
    RT_NOREF(pVCpu, pvPhys, enmOrigin);

    if (enmAccessType == PGMACCESSTYPE_READ)
    {
        switch (pRomPage->enmProt)
        {
            /*
             * Take the default action.
             */
            case PGMROMPROT_READ_ROM_WRITE_IGNORE:
            case PGMROMPROT_READ_RAM_WRITE_IGNORE:
            case PGMROMPROT_READ_ROM_WRITE_RAM:
            case PGMROMPROT_READ_RAM_WRITE_RAM:
                return VINF_PGM_HANDLER_DO_DEFAULT;

            default:
                AssertMsgFailedReturn(("enmProt=%d iPage=%d GCPhys=%RGp\n",
                                       pRom->aPages[iPage].enmProt, iPage, GCPhys),
                                      VERR_IPE_NOT_REACHED_DEFAULT_CASE);
        }
    }
    else
    {
        Assert(enmAccessType == PGMACCESSTYPE_WRITE);
        switch (pRomPage->enmProt)
        {
            /*
             * Ignore writes.
             */
            case PGMROMPROT_READ_ROM_WRITE_IGNORE:
            case PGMROMPROT_READ_RAM_WRITE_IGNORE:
                return VINF_SUCCESS;

            /*
             * Write to the RAM page.
             */
            case PGMROMPROT_READ_ROM_WRITE_RAM:
            case PGMROMPROT_READ_RAM_WRITE_RAM: /* yes this will get here too, it's *way* simpler that way. */
            {
                /* This should be impossible now, pvPhys doesn't work cross page anylonger. */
                Assert(((GCPhys - pRom->GCPhys + cbBuf - 1) >> GUEST_PAGE_SHIFT) == iPage);

                /*
                 * Take the lock, do lazy allocation, map the page and copy the data.
                 *
                 * Note that we have to bypass the mapping TLB since it works on
                 * guest physical addresses and entering the shadow page would
                 * kind of screw things up...
                 */
                PGM_LOCK_VOID(pVM);

                PPGMPAGE pShadowPage = &pRomPage->Shadow;
                if (!PGMROMPROT_IS_ROM(pRomPage->enmProt))
                {
                    pShadowPage = pgmPhysGetPage(pVM, GCPhys);
                    AssertLogRelMsgReturnStmt(pShadowPage, ("%RGp\n", GCPhys), PGM_UNLOCK(pVM), VERR_PGM_PHYS_PAGE_GET_IPE);
                }

                void *pvDstPage;
                int rc;
#if defined(VBOX_WITH_PGM_NEM_MODE) && defined(IN_RING3)
                if (PGM_IS_IN_NEM_MODE(pVM) && PGMROMPROT_IS_ROM(pRomPage->enmProt))
                {
                    pvDstPage = &pRom->pbR3Alternate[GCPhys - pRom->GCPhys];
                    rc = VINF_SUCCESS;
                }
                else
#endif
                {
                    rc = pgmPhysPageMakeWritableAndMap(pVM, pShadowPage, GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK, &pvDstPage);
                    if (RT_SUCCESS(rc))
                        pvDstPage = (uint8_t *)pvDstPage + (GCPhys & GUEST_PAGE_OFFSET_MASK);
                }
                if (RT_SUCCESS(rc))
                {
                    memcpy((uint8_t *)pvDstPage + (GCPhys & GUEST_PAGE_OFFSET_MASK), pvBuf, cbBuf);
                    pRomPage->LiveSave.fWrittenTo = true;

                    AssertMsg(    rc == VINF_SUCCESS
                              || (  rc == VINF_PGM_SYNC_CR3
                                  && VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3 | VMCPU_FF_PGM_SYNC_CR3_NON_GLOBAL))
                              , ("%Rrc\n", rc));
                    rc = VINF_SUCCESS;
                }

                PGM_UNLOCK(pVM);
                return rc;
            }

            default:
                AssertMsgFailedReturn(("enmProt=%d iPage=%d GCPhys=%RGp\n",
                                       pRom->aPages[iPage].enmProt, iPage, GCPhys),
                                      VERR_IPE_NOT_REACHED_DEFAULT_CASE);
        }
    }
}


/**
 * Common worker for pgmPhysMmio2WriteHandler and pgmPhysMmio2WritePfHandler.
 */
static VBOXSTRICTRC pgmPhysMmio2WriteHandlerCommon(PVMCC pVM, PVMCPUCC pVCpu, uint64_t hMmio2, RTGCPHYS GCPhys, RTGCPTR GCPtr)
{
    /*
     * Get the MMIO2 range.
     */
    AssertReturn(hMmio2 < RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges), VERR_INTERNAL_ERROR_3);
    AssertReturn(hMmio2 != 0, VERR_INTERNAL_ERROR_3);
    PPGMREGMMIO2RANGE const pMmio2 = &pVM->pgm.s.aMmio2Ranges[hMmio2 - 1];
    Assert(pMmio2->idMmio2 == hMmio2);
    AssertReturn((pMmio2->fFlags & PGMREGMMIO2RANGE_F_TRACK_DIRTY_PAGES) == PGMREGMMIO2RANGE_F_TRACK_DIRTY_PAGES,
                 VERR_INTERNAL_ERROR_4);

    /*
     * Get the page and make sure it's an MMIO2 page.
     */
    PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhys);
    AssertReturn(pPage, VINF_EM_RAW_EMULATE_INSTR);
    AssertReturn(PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2, VINF_EM_RAW_EMULATE_INSTR);

    /*
     * Set the dirty flag so we can avoid scanning all the pages when it isn't dirty.
     * (The PGM_PAGE_HNDL_PHYS_STATE_DISABLED handler state indicates that a single
     * page is dirty, saving the need for additional storage (bitmap).)
     */
    pMmio2->fFlags |= PGMREGMMIO2RANGE_F_IS_DIRTY;

    /*
     * Disable the handler for this page.
     */
    int rc = PGMHandlerPhysicalPageTempOff(pVM, pMmio2->GCPhys, GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK);
    AssertRC(rc);
#ifndef IN_RING3
    if (RT_SUCCESS(rc) && GCPtr != ~(RTGCPTR)0)
    {
        rc = PGMShwMakePageWritable(pVCpu, GCPtr, PGM_MK_PG_IS_MMIO2 | PGM_MK_PG_IS_WRITE_FAULT);
        AssertMsgReturn(rc == VINF_SUCCESS || rc == VERR_PAGE_TABLE_NOT_PRESENT,
                        ("PGMShwModifyPage -> GCPtr=%RGv rc=%d\n", GCPtr, rc), rc);
    }
#else
    RT_NOREF(pVCpu, GCPtr);
#endif
    return VINF_SUCCESS;
}


#ifndef IN_RING3
/**
 * @callback_method_impl{FNPGMRZPHYSPFHANDLER,
 *      \#PF access handler callback for guest MMIO2 dirty page tracing.}
 *
 * @remarks The @a uUser is the MMIO2 index.
 */
DECLCALLBACK(VBOXSTRICTRC) pgmPhysMmio2WritePfHandler(PVMCC pVM, PVMCPUCC pVCpu, RTGCUINT uErrorCode, PCPUMCTX pCtx,
                                                      RTGCPTR pvFault, RTGCPHYS GCPhysFault, uint64_t uUser)
{
    RT_NOREF(pVCpu, uErrorCode, pCtx);
    VBOXSTRICTRC rcStrict = PGM_LOCK(pVM); /* We should already have it, but just make sure we do. */
    if (RT_SUCCESS(rcStrict))
    {
        rcStrict = pgmPhysMmio2WriteHandlerCommon(pVM, pVCpu, uUser, GCPhysFault, pvFault);
        PGM_UNLOCK(pVM);
    }
    return rcStrict;
}
#endif /* !IN_RING3 */


/**
 * @callback_method_impl{FNPGMPHYSHANDLER,
 *      Access handler callback for MMIO2 dirty page tracing.}
 *
 * @remarks The @a uUser is the MMIO2 index.
 */
DECLCALLBACK(VBOXSTRICTRC)
pgmPhysMmio2WriteHandler(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf,
                         PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin, uint64_t uUser)
{
    VBOXSTRICTRC rcStrict = PGM_LOCK(pVM); /* We should already have it, but just make sure we do. */
    if (RT_SUCCESS(rcStrict))
    {
        rcStrict = pgmPhysMmio2WriteHandlerCommon(pVM, pVCpu, uUser, GCPhys, ~(RTGCPTR)0);
        PGM_UNLOCK(pVM);
        if (rcStrict == VINF_SUCCESS)
            rcStrict = VINF_PGM_HANDLER_DO_DEFAULT;
    }
    RT_NOREF(pvPhys, pvBuf, cbBuf, enmAccessType, enmOrigin);
    return rcStrict;
}



/*********************************************************************************************************************************
*   RAM Ranges                                                                                                                   *
*********************************************************************************************************************************/

#ifdef VBOX_STRICT
/**
 * Asserts that the RAM range structures are sane.
 */
DECLHIDDEN(bool) pgmPhysAssertRamRangesLocked(PVMCC pVM, bool fInUpdate, bool fRamRelaxed)
{
    bool fRet = true;

    /*
     * Check the generation ID.  This is stable since we own the PGM lock.
     */
    AssertStmt((pVM->pgm.s.RamRangeUnion.idGeneration & 1U) == (unsigned)fInUpdate, fRet = false);

    /*
     * Check the entry count and max ID.
     */
    uint32_t const idRamRangeMax  = pVM->pgm.s.idRamRangeMax;
    /* Since this is set to the highest ID, it cannot be the same as the table size. */
    AssertStmt(idRamRangeMax  < RT_ELEMENTS(pVM->pgm.s.apRamRanges), fRet = false);

    /* Because ID=0 is reserved, it's one less than the table size and at most the
       same as the max ID. */
    uint32_t const cLookupEntries = pVM->pgm.s.RamRangeUnion.cLookupEntries;
    AssertStmt(cLookupEntries < RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup), fRet = false);
    AssertStmt(cLookupEntries <= idRamRangeMax, fRet = false);

    /*
     * Check the pointer table(s).
     */
    /* The first entry shall be empty. */
    AssertStmt(pVM->pgm.s.apRamRanges[0] == NULL, fRet = false);
# ifdef IN_RING0
    AssertStmt(pVM->pgmr0.s.apRamRanges[0] == NULL, fRet = false);
    AssertStmt(pVM->pgmr0.s.acRamRangePages[0] == 0, fRet = false);
# endif

    uint32_t cMappedRanges = 0;
    for (uint32_t idRamRange = 1; idRamRange <= idRamRangeMax; idRamRange++)
    {
# ifdef IN_RING0
        PPGMRAMRANGE const pRamRange = pVM->pgmr0.s.apRamRanges[idRamRange];
        AssertContinueStmt(pRamRange, fRet = false);
        AssertStmt(pVM->pgm.s.apRamRanges[idRamRange] != NIL_RTR3PTR, fRet = false);
        AssertStmt(   (pRamRange->cb >> GUEST_PAGE_SHIFT) == pVM->pgmr0.s.acRamRangePages[idRamRange]
                   || (   (pRamRange->cb >> GUEST_PAGE_SHIFT) < pVM->pgmr0.s.acRamRangePages[idRamRange]
                       && !(pRamRange->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO_EX)),
                   fRet = false);
# else
        PPGMRAMRANGE const pRamRange = pVM->pgm.s.apRamRanges[idRamRange];
        AssertContinueStmt(pRamRange, fRet = false);
# endif
        AssertStmt(pRamRange->idRange == idRamRange, fRet = false);
        if (pRamRange->GCPhys != NIL_RTGCPHYS)
        {
            cMappedRanges++;
            AssertStmt((pRamRange->GCPhys & GUEST_PAGE_OFFSET_MASK) == 0, fRet = false);
            AssertStmt((pRamRange->GCPhysLast & GUEST_PAGE_OFFSET_MASK) == GUEST_PAGE_OFFSET_MASK, fRet = false);
            AssertStmt(pRamRange->GCPhysLast > pRamRange->GCPhys, fRet = false);
            AssertStmt(pRamRange->GCPhysLast - pRamRange->GCPhys + 1U == pRamRange->cb, fRet = false);
        }
        else
        {
            AssertStmt(pRamRange->GCPhysLast == NIL_RTGCPHYS, fRet = false);
            AssertStmt(PGM_RAM_RANGE_IS_AD_HOC(pRamRange) || fRamRelaxed, fRet = false);
        }
    }

    /*
     * Check that the lookup table is sorted and contains the right information.
     */
    AssertMsgStmt(cMappedRanges == cLookupEntries,
                  ("cMappedRanges=%#x cLookupEntries=%#x\n", cMappedRanges, cLookupEntries),
                  fRet = false);
    RTGCPHYS GCPhysPrev = ~(RTGCPHYS)0;
    for (uint32_t idxLookup = 0; idxLookup < cLookupEntries; idxLookup++)
    {
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        AssertContinueStmt(idRamRange > 0 && idRamRange <= idRamRangeMax, fRet = false);
        PPGMRAMRANGE const pRamRange = pVM->CTX_EXPR(pgm,pgmr0,pgmrc).s.apRamRanges[idRamRange];
        AssertContinueStmt(pRamRange, fRet = false);

        AssertStmt(pRamRange->idRange == idRamRange, fRet = false);
        AssertStmt(pRamRange->GCPhys == PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]),
                   fRet = false);
        AssertStmt(pRamRange->GCPhysLast == pVM->pgm.s.aRamRangeLookup[idxLookup].GCPhysLast, fRet = false);

        AssertStmt(pRamRange->GCPhys >= GCPhysPrev + 1U, fRet = false);
        GCPhysPrev = pRamRange->GCPhysLast;
    }

    return fRet;
}
#endif /* VBOX_STRICT */


/**
 * Invalidates the RAM range TLBs.
 *
 * @param   pVM                 The cross context VM structure.
 */
void pgmPhysInvalidRamRangeTlbs(PVMCC pVM)
{
    PGM_LOCK_VOID(pVM);

    /* This is technically only required when freeing the PCNet MMIO2 range
       during ancient saved state loading.  The code freeing the RAM range
       will make sure this function is called in both rings. */
    RT_ZERO(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb);
    VMCC_FOR_EACH_VMCPU_STMT(pVM, RT_ZERO(pVCpu->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb));

    PGM_UNLOCK(pVM);
}


/**
 * Tests if a value of type RTGCPHYS is negative if the type had been signed
 * instead of unsigned.
 *
 * @returns @c true if negative, @c false if positive or zero.
 * @param   a_GCPhys        The value to test.
 * @todo    Move me to iprt/types.h.
 */
#define RTGCPHYS_IS_NEGATIVE(a_GCPhys)  ((a_GCPhys) & ((RTGCPHYS)1 << (sizeof(RTGCPHYS)*8 - 1)))


/**
 * Slow worker for pgmPhysGetRange.
 *
 * @copydoc pgmPhysGetRange
 * @note    Caller owns the PGM lock.
 */
DECLHIDDEN(PPGMRAMRANGE) pgmPhysGetRangeSlow(PVMCC pVM, RTGCPHYS GCPhys)
{
    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,RamRangeTlbMisses));

    uint32_t idxEnd   = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries, RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    uint32_t idxStart = 0;
    for (;;)
    {
        uint32_t       idxLookup        = idxStart + (idxEnd - idxStart) / 2;
        RTGCPHYS const GCPhysEntryFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        RTGCPHYS const cbEntryMinus1    = pVM->pgm.s.aRamRangeLookup[idxLookup].GCPhysLast - GCPhysEntryFirst;
        RTGCPHYS const off              = GCPhys - GCPhysEntryFirst;
        if (off <= cbEntryMinus1)
        {
            uint32_t const     idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
            AssertReturn(idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges), NULL);
            PPGMRAMRANGE const pRamRange  = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
            Assert(pRamRange);
            pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb[PGM_RAMRANGE_TLB_IDX(GCPhys)] = pRamRange;
            return pRamRange;
        }
        if (RTGCPHYS_IS_NEGATIVE(off))
        {
            if (idxStart < idxLookup)
                idxEnd = idxLookup;
            else
                break;
        }
        else
        {
            idxLookup += 1;
            if (idxLookup < idxEnd)
                idxStart = idxLookup;
            else
                break;
        }
    }
    return NULL;
}


/**
 * Slow worker for pgmPhysGetRangeAtOrAbove.
 *
 * @copydoc pgmPhysGetRangeAtOrAbove
 */
DECLHIDDEN(PPGMRAMRANGE) pgmPhysGetRangeAtOrAboveSlow(PVMCC pVM, RTGCPHYS GCPhys)
{
    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,RamRangeTlbMisses));

    uint32_t idRamRangeLastLeft = UINT32_MAX;
    uint32_t idxEnd             = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries, RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    uint32_t idxStart           = 0;
    for (;;)
    {
        uint32_t       idxLookup        = idxStart + (idxEnd - idxStart) / 2;
        RTGCPHYS const GCPhysEntryFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        RTGCPHYS const cbEntryMinus1    = pVM->pgm.s.aRamRangeLookup[idxLookup].GCPhysLast - GCPhysEntryFirst;
        RTGCPHYS const off              = GCPhys - GCPhysEntryFirst;
        if (off <= cbEntryMinus1)
        {
            uint32_t const     idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
            AssertReturn(idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges), NULL);
            PPGMRAMRANGE const pRamRange  = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
            Assert(pRamRange);
            pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb[PGM_RAMRANGE_TLB_IDX(GCPhys)] = pRamRange;
            return pRamRange;
        }
        if (RTGCPHYS_IS_NEGATIVE(off))
        {
            idRamRangeLastLeft = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
            if (idxStart < idxLookup)
                idxEnd = idxLookup;
            else
                break;
        }
        else
        {
            idxLookup += 1;
            if (idxLookup < idxEnd)
                idxStart = idxLookup;
            else
                break;
        }
    }
    if (idRamRangeLastLeft != UINT32_MAX)
    {
        AssertReturn(idRamRangeLastLeft < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges), NULL);
        PPGMRAMRANGE const pRamRange = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRangeLastLeft];
        Assert(pRamRange);
        return pRamRange;
    }
    return NULL;
}


/**
 * Slow worker for pgmPhysGetPage.
 *
 * @copydoc pgmPhysGetPage
 */
DECLHIDDEN(PPGMPAGE) pgmPhysGetPageSlow(PVMCC pVM, RTGCPHYS GCPhys)
{
    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,RamRangeTlbMisses));

    uint32_t idxEnd   = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries, RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    uint32_t idxStart = 0;
    for (;;)
    {
        uint32_t       idxLookup        = idxStart + (idxEnd - idxStart) / 2;
        RTGCPHYS const GCPhysEntryFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        RTGCPHYS const cbEntryMinus1    = pVM->pgm.s.aRamRangeLookup[idxLookup].GCPhysLast - GCPhysEntryFirst;
        RTGCPHYS const off              = GCPhys - GCPhysEntryFirst;
        if (off <= cbEntryMinus1)
        {
            uint32_t const     idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
            AssertReturn(idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges), NULL);
            PPGMRAMRANGE const pRamRange  = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
            AssertReturn(pRamRange, NULL);
            pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb[PGM_RAMRANGE_TLB_IDX(GCPhys)] = pRamRange;

            /* Get the page. */
            Assert(off < pRamRange->cb);
            RTGCPHYS const idxPage = off >> GUEST_PAGE_SHIFT;
#ifdef IN_RING0
            AssertReturn(idxPage < pVM->pgmr0.s.acRamRangePages[idRamRange], NULL);
#endif
            return &pRamRange->aPages[idxPage];
        }
        if (RTGCPHYS_IS_NEGATIVE(off))
        {
            if (idxStart < idxLookup)
                idxEnd = idxLookup;
            else
                break;
        }
        else
        {
            idxLookup += 1;
            if (idxLookup < idxEnd)
                idxStart = idxLookup;
            else
                break;
        }
    }
    return NULL;
}


/**
 * Slow worker for pgmPhysGetPageEx.
 *
 * @copydoc pgmPhysGetPageEx
 */
DECLHIDDEN(int) pgmPhysGetPageExSlow(PVMCC pVM, RTGCPHYS GCPhys, PPPGMPAGE ppPage)
{
    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,RamRangeTlbMisses));

    uint32_t idxEnd   = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries, RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    uint32_t idxStart = 0;
    for (;;)
    {
        uint32_t       idxLookup        = idxStart + (idxEnd - idxStart) / 2;
        RTGCPHYS const GCPhysEntryFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        RTGCPHYS const cbEntryMinus1    = pVM->pgm.s.aRamRangeLookup[idxLookup].GCPhysLast - GCPhysEntryFirst;
        RTGCPHYS const off              = GCPhys - GCPhysEntryFirst;
        if (off <= cbEntryMinus1)
        {
            uint32_t const     idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
            AssertReturn(idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges), VERR_PGM_PHYS_RAM_LOOKUP_IPE);
            PPGMRAMRANGE const pRamRange  = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
            AssertReturn(pRamRange, VERR_PGM_PHYS_RAM_LOOKUP_IPE);
            pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb[PGM_RAMRANGE_TLB_IDX(GCPhys)] = pRamRange;

            /* Get the page. */
            Assert(off < pRamRange->cb);
            RTGCPHYS const idxPage = off >> GUEST_PAGE_SHIFT;
#ifdef IN_RING0
            AssertReturn(idxPage < pVM->pgmr0.s.acRamRangePages[idRamRange], VERR_PGM_PHYS_RAM_LOOKUP_IPE);
#endif
            *ppPage = &pRamRange->aPages[idxPage];
            return VINF_SUCCESS;
        }
        if (RTGCPHYS_IS_NEGATIVE(off))
        {
            if (idxStart < idxLookup)
                idxEnd = idxLookup;
            else
                break;
        }
        else
        {
            idxLookup += 1;
            if (idxLookup < idxEnd)
                idxStart = idxLookup;
            else
                break;
        }
    }

    *ppPage = NULL;
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Slow worker for pgmPhysGetPageAndRangeEx.
 *
 * @copydoc pgmPhysGetPageAndRangeEx
 */
DECLHIDDEN(int) pgmPhysGetPageAndRangeExSlow(PVMCC pVM, RTGCPHYS GCPhys, PPPGMPAGE ppPage, PPGMRAMRANGE *ppRam)
{
    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,RamRangeTlbMisses));

    uint32_t idxEnd   = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries, RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    uint32_t idxStart = 0;
    for (;;)
    {
        uint32_t       idxLookup        = idxStart + (idxEnd - idxStart) / 2;
        RTGCPHYS const GCPhysEntryFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        RTGCPHYS const cbEntryMinus1    = pVM->pgm.s.aRamRangeLookup[idxLookup].GCPhysLast - GCPhysEntryFirst;
        RTGCPHYS const off              = GCPhys - GCPhysEntryFirst;
        if (off <= cbEntryMinus1)
        {
            uint32_t const     idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
            AssertReturn(idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges), VERR_PGM_PHYS_RAM_LOOKUP_IPE);
            PPGMRAMRANGE const pRamRange  = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
            AssertReturn(pRamRange, VERR_PGM_PHYS_RAM_LOOKUP_IPE);
            pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb[PGM_RAMRANGE_TLB_IDX(GCPhys)] = pRamRange;

            /* Get the page. */
            Assert(off < pRamRange->cb);
            RTGCPHYS const idxPage = off >> GUEST_PAGE_SHIFT;
#ifdef IN_RING0
            AssertReturn(idxPage < pVM->pgmr0.s.acRamRangePages[idRamRange], VERR_PGM_PHYS_RAM_LOOKUP_IPE);
#endif
            *ppRam  = pRamRange;
            *ppPage = &pRamRange->aPages[idxPage];
            return VINF_SUCCESS;
        }
        if (RTGCPHYS_IS_NEGATIVE(off))
        {
            if (idxStart < idxLookup)
                idxEnd = idxLookup;
            else
                break;
        }
        else
        {
            idxLookup += 1;
            if (idxLookup < idxEnd)
                idxStart = idxLookup;
            else
                break;
        }
    }

    *ppRam  = NULL;
    *ppPage = NULL;
    return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
}


/**
 * Slow worker for pgmPhysGetPageAndRangeExLockless.
 *
 * @copydoc pgmPhysGetPageAndRangeExLockless
 */
DECLHIDDEN(int) pgmPhysGetPageAndRangeExSlowLockless(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys,
                                                     PGMPAGE volatile **ppPage, PGMRAMRANGE volatile **ppRam)
{
    STAM_REL_COUNTER_INC(&pVCpu->pgm.s.CTX_MID_Z(Stat,RamRangeTlbMisses));

    PGM::PGMRAMRANGEGENANDLOOKUPCOUNT RamRangeUnion;
    RamRangeUnion.u64Combined = ASMAtomicUoReadU64(&pVM->pgm.s.RamRangeUnion.u64Combined);

    uint32_t idxEnd   = RT_MIN(RamRangeUnion.cLookupEntries, RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    uint32_t idxStart = 0;
    for (;;)
    {
        /* Read the entry as atomically as possible: */
        uint32_t                idxLookup = idxStart + (idxEnd - idxStart) / 2;
        PGMRAMRANGELOOKUPENTRY  Entry;
#if (RTASM_HAVE_READ_U128+0) & 1
        Entry.u128Normal = ASMAtomicUoReadU128U(&pVM->pgm.s.aRamRangeLookup[idxLookup].u128Volatile);
#else
        Entry.u128Normal.s.Lo = pVM->pgm.s.aRamRangeLookup[idxLookup].u128Volatile.s.Lo;
        Entry.u128Normal.s.Hi = pVM->pgm.s.aRamRangeLookup[idxLookup].u128Volatile.s.Hi;
        ASMCompilerBarrier(); /*paranoia^2*/
        if (RT_LIKELY(Entry.u128Normal.s.Lo == pVM->pgm.s.aRamRangeLookup[idxLookup].u128Volatile.s.Lo))
        { /* likely */ }
        else
            break;
#endif

        /* Check how GCPhys relates to the entry: */
        RTGCPHYS const GCPhysEntryFirst = PGMRAMRANGELOOKUPENTRY_GET_FIRST(Entry);
        RTGCPHYS const cbEntryMinus1    = Entry.GCPhysLast - GCPhysEntryFirst;
        RTGCPHYS const off              = GCPhys - GCPhysEntryFirst;
        if (off <= cbEntryMinus1)
        {
            /* We seem to have a match. If, however, anything doesn't match up
               bail and redo owning the lock. No asserting here as we may be
               racing removal/insertion. */
            if (!RTGCPHYS_IS_NEGATIVE(off))
            {
                uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(Entry);
                if (idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges))
                {
                    PPGMRAMRANGE const pRamRange = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
                    if (pRamRange)
                    {
                        if (   pRamRange->GCPhys == GCPhysEntryFirst
                            && pRamRange->cb     == cbEntryMinus1 + 1U)
                        {
                            RTGCPHYS const idxPage = off >> GUEST_PAGE_SHIFT;
#ifdef IN_RING0
                            if (idxPage < pVM->pgmr0.s.acRamRangePages[idRamRange])
#endif
                            {
                                pVCpu->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb[PGM_RAMRANGE_TLB_IDX(GCPhys)] = pRamRange;
                                *ppRam  = pRamRange;
                                *ppPage = &pRamRange->aPages[idxPage];
                                return VINF_SUCCESS;
                            }
                        }
                    }
                }
            }
            break;
        }
        if (RTGCPHYS_IS_NEGATIVE(off))
        {
            if (idxStart < idxLookup)
                idxEnd = idxLookup;
            else
                break;
        }
        else
        {
            idxLookup += 1;
            if (idxLookup < idxEnd)
                idxStart = idxLookup;
            else
                break;
        }
    }

    /*
     * If we get down here, we do the lookup again but while owning the PGM lock.
     */
    *ppRam  = NULL;
    *ppPage = NULL;
    STAM_REL_COUNTER_INC(&pVCpu->pgm.s.CTX_MID_Z(Stat,RamRangeTlbLocking));

    PGM_LOCK_VOID(pVM);
    int rc = pgmPhysGetPageAndRangeEx(pVM, GCPhys, (PPGMPAGE *)ppPage, (PPGMRAMRANGE *)ppRam);
    PGM_UNLOCK(pVM);

    PGMRAMRANGE volatile * const pRam = *ppRam;
    if (pRam)
        pVCpu->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRangesTlb[PGM_RAMRANGE_TLB_IDX(GCPhys)] = (PPGMRAMRANGE)pRam;
    return rc;
}


/**
 * Common worker for pgmR3PhysAllocateRamRange, PGMR0PhysAllocateRamRangeReq,
 * and pgmPhysMmio2RegisterWorker2.
 */
DECLHIDDEN(int) pgmPhysRamRangeAllocCommon(PVMCC pVM, uint32_t cPages, uint32_t fFlags, uint32_t *pidNewRange)
{

    /*
     * Allocate the RAM range structure and map it into ring-3.
     */
    size_t const cbRamRange = RT_ALIGN_Z(RT_UOFFSETOF_DYN(PGMRAMRANGE, aPages[cPages]), HOST_PAGE_SIZE);
#ifdef IN_RING0
    RTR0MEMOBJ   hMemObj    = NIL_RTR0MEMOBJ;
    int rc = RTR0MemObjAllocPage(&hMemObj, cbRamRange, false /*fExecutable*/);
#else
    PPGMRAMRANGE pRamRange;
    int rc = SUPR3PageAlloc(cbRamRange >> HOST_PAGE_SHIFT, 0 /*fFlags*/, (void **)&pRamRange);
#endif
    if (RT_SUCCESS(rc))
    {
        /* Zero the memory and do basic range init before mapping it into userland. */
#ifdef IN_RING0
        PPGMRAMRANGE const pRamRange = (PPGMRAMRANGE)RTR0MemObjAddress(hMemObj);
        if (!RTR0MemObjWasZeroInitialized(hMemObj))
#endif
            RT_BZERO(pRamRange, cbRamRange);

        pRamRange->GCPhys       = NIL_RTGCPHYS;
        pRamRange->cb           = (RTGCPHYS)cPages << GUEST_PAGE_SHIFT;
        pRamRange->GCPhysLast   = NIL_RTGCPHYS;
        pRamRange->fFlags       = fFlags;
        pRamRange->idRange      = UINT32_MAX / 2;

#ifdef IN_RING0
        /* Map it into userland. */
        RTR0MEMOBJ hMapObj  = NIL_RTR0MEMOBJ;
        rc = RTR0MemObjMapUser(&hMapObj, hMemObj, (RTR3PTR)-1, 0 /*uAlignment*/,
                               RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
        if (RT_SUCCESS(rc))
#endif
        {
            /*
             * Grab the lock (unlikely to fail or block as caller typically owns it already).
             */
            rc = PGM_LOCK(pVM);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Allocate a range ID.
                 */
                uint32_t idRamRange = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.idRamRangeMax + 1;
                if (idRamRange != 0 && idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges))
                {
#ifdef IN_RING0
                    if (pVM->pgmr0.s.apRamRanges[idRamRange] == NULL)
#endif
                    {
                        if (pVM->pgm.s.apRamRanges[idRamRange] == NIL_RTR3PTR)
                        {
                            /*
                             * Commit it.
                             */
#ifdef IN_RING0
                            pVM->pgmr0.s.apRamRanges[idRamRange]       = pRamRange;
                            pVM->pgmr0.s.acRamRangePages[idRamRange]   = cPages;
                            pVM->pgmr0.s.ahRamRangeMemObjs[idRamRange] = hMemObj;
                            pVM->pgmr0.s.ahRamRangeMapObjs[idRamRange] = hMapObj;
                            pVM->pgmr0.s.idRamRangeMax                 = idRamRange;
#endif

                            pVM->pgm.s.idRamRangeMax                   = idRamRange;
#ifdef IN_RING0
                            pVM->pgm.s.apRamRanges[idRamRange]         = RTR0MemObjAddressR3(hMapObj);
#else
                            pVM->pgm.s.apRamRanges[idRamRange]         = pRamRange;
#endif

                            pRamRange->idRange                          = idRamRange;
                            *pidNewRange                                = idRamRange;

                            PGM_UNLOCK(pVM);
                            return VINF_SUCCESS;
                        }
                    }

                    /*
                     * Bail out.
                     */
                    rc = VERR_INTERNAL_ERROR_5;
                }
                else
                    rc = VERR_PGM_TOO_MANY_RAM_RANGES;
                PGM_UNLOCK(pVM);
            }
#ifdef IN_RING0
            RTR0MemObjFree(hMapObj, false /*fFreeMappings*/);
#endif
        }
#ifdef IN_RING0
        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
#else
        SUPR3PageFree(pRamRange, cbRamRange >> HOST_PAGE_SHIFT);
#endif
    }
    *pidNewRange = UINT32_MAX;
    return rc;
}


#ifdef IN_RING0
/**
 * This is called during VM initialization to allocate a RAM range.
 *
 * The range is not entered into the lookup table, that is something the caller
 * has to do.  The PGMPAGE entries are zero'ed, but otherwise uninitialized.
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 * @param   pReq    Where to get the parameters and return the range ID.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PGMR0PhysAllocateRamRangeReq(PGVM pGVM, PPGMPHYSALLOCATERAMRANGEREQ pReq)
{
    /*
     * Validate input (ASSUME pReq is a copy and can't be modified by ring-3
     * while we're here).
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x < %#zx\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    AssertReturn(pReq->cbGuestPage == GUEST_PAGE_SIZE, VERR_INCOMPATIBLE_CONFIG);

    AssertReturn(pReq->cGuestPages > 0, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cGuestPages <= PGM_MAX_PAGES_PER_RAM_RANGE, VERR_OUT_OF_RANGE);

    AssertMsgReturn(!(pReq->fFlags & ~(uint32_t)PGM_RAM_RANGE_FLAGS_VALID_MASK), ("fFlags=%#RX32\n", pReq->fFlags),
                    VERR_INVALID_FLAGS);

    /** @todo better VM state guard, enmVMState is ring-3 writable. */
    VMSTATE const enmState = pGVM->enmVMState;
    AssertMsgReturn(enmState == VMSTATE_CREATING, ("enmState=%d\n", enmState), VERR_VM_INVALID_VM_STATE);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /*
     * Call common worker.
     */
    return pgmPhysRamRangeAllocCommon(pGVM, pReq->cGuestPages, pReq->fFlags, &pReq->idNewRange);
}
#endif /* IN_RING0 */


/**
 * Frees a RAM range.
 *
 * This is not a typical occurence.  Currently only used for a special MMIO2
 * saved state compatibility scenario involving PCNet and state saved before
 * VBox v4.3.6.
 */
static int pgmPhysRamRangeFree(PVMCC pVM, PPGMRAMRANGE pRamRange)
{
    /*
     * Some basic input validation.
     */
    AssertPtrReturn(pRamRange, VERR_INVALID_PARAMETER);
    uint32_t const idRamRange = ASMAtomicReadU32(&pRamRange->idRange);
    ASMCompilerBarrier();
    AssertReturn(idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges), VERR_INVALID_PARAMETER);
    AssertReturn(pRamRange == pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange], VERR_INVALID_PARAMETER);
    AssertReturn(pRamRange->GCPhys == NIL_RTGCPHYS, VERR_RESOURCE_BUSY);

    /*
     * Kill the range pointers and associated data.
     */
    pVM->pgm.s.apRamRanges[idRamRange]   = NIL_RTR3PTR;
#ifdef IN_RING0
    pVM->pgmr0.s.apRamRanges[idRamRange] = NULL;
#endif

    /*
     * Zap the pages and other RAM ranges properties to ensure there aren't any
     * stale references to anything hanging around should the freeing go awry.
     */
#ifdef IN_RING0
    uint32_t const cPages = pVM->pgmr0.s.acRamRangePages[idRamRange];
    pVM->pgmr0.s.acRamRangePages[idRamRange] = 0;
#else
    uint32_t const cPages = pRamRange->cb >> GUEST_PAGE_SHIFT;
#endif
    RT_BZERO(pRamRange->aPages, cPages * sizeof(pRamRange->aPages[0]));

    pRamRange->fFlags    = UINT32_MAX;
    pRamRange->cb        = NIL_RTGCPHYS;
    pRamRange->pbR3      = NIL_RTR3PTR;
    pRamRange->pszDesc   = NIL_RTR3PTR;
    pRamRange->paLSPages = NIL_RTR3PTR;
    pRamRange->idRange   = UINT32_MAX / 8;

    /*
     * Free the RAM range itself.
     */
#ifdef IN_RING0
    Assert(pVM->pgmr0.s.ahRamRangeMapObjs[idRamRange] != NIL_RTR0MEMOBJ);
    int rc = RTR0MemObjFree(pVM->pgmr0.s.ahRamRangeMapObjs[idRamRange], true /*fFreeMappings*/);
    if (RT_SUCCESS(rc))
    {
        pVM->pgmr0.s.ahRamRangeMapObjs[idRamRange] = NIL_RTR0MEMOBJ;
        rc = RTR0MemObjFree(pVM->pgmr0.s.ahRamRangeMemObjs[idRamRange], true /*fFreeMappings*/);
        if (RT_SUCCESS(rc))
            pVM->pgmr0.s.ahRamRangeMemObjs[idRamRange] = NIL_RTR0MEMOBJ;
    }
#else
    size_t const cbRamRange = RT_ALIGN_Z(RT_UOFFSETOF_DYN(PGMRAMRANGE, aPages[cPages]), HOST_PAGE_SIZE);
    int rc = SUPR3PageFree(pRamRange, cbRamRange >> HOST_PAGE_SHIFT);
#endif

    /*
     * Decrease the max ID if removal was successful and this was the final
     * RAM range entry.
     */
    if (   RT_SUCCESS(rc)
        && idRamRange == pVM->CTX_EXPR(pgm, pgmr0, pgm).s.idRamRangeMax)
    {
        pVM->pgm.s.idRamRangeMax   = idRamRange - 1;
#ifdef IN_RING0
        pVM->pgmr0.s.idRamRangeMax = idRamRange - 1;
#endif
    }

    /*
     * Make sure the RAM range TLB does not contain any stale pointers to this range.
     */
    pgmPhysInvalidRamRangeTlbs(pVM);
    return rc;
}



/*********************************************************************************************************************************
*   MMIO2                                                                                                                        *
*********************************************************************************************************************************/

/**
 * Calculates the number of chunks
 *
 * @returns Number of registration chunk needed.
 * @param   cb              The size of the MMIO/MMIO2 range.
 * @param   pcPagesPerChunk Where to return the number of guest pages tracked by
 *                          each chunk.  Optional.
 */
DECLHIDDEN(uint16_t) pgmPhysMmio2CalcChunkCount(RTGCPHYS cb, uint32_t *pcPagesPerChunk)
{
    /*
     * This is the same calculation as PGMR3PhysRegisterRam does, except we'll be
     * needing a few bytes extra the PGMREGMMIO2RANGE structure.
     *
     * Note! In additions, we've got a 24 bit sub-page range for MMIO2 ranges, leaving
     *       us with an absolute maximum of 16777215 pages per chunk (close to 64 GB).
     */
    AssertCompile(PGM_MAX_PAGES_PER_RAM_RANGE < _16M);
    uint32_t const cPagesPerChunk = PGM_MAX_PAGES_PER_RAM_RANGE;

    if (pcPagesPerChunk)
        *pcPagesPerChunk = cPagesPerChunk;

    /* Calc the number of chunks we need. */
    RTGCPHYS const cGuestPages = cb >> GUEST_PAGE_SHIFT;
    uint16_t cChunks = (uint16_t)((cGuestPages + cPagesPerChunk - 1) / cPagesPerChunk);
#ifdef IN_RING3
    AssertRelease((RTGCPHYS)cChunks * cPagesPerChunk >= cGuestPages);
#else
    AssertReturn((RTGCPHYS)cChunks * cPagesPerChunk >= cGuestPages, 0);
#endif
    return cChunks;
}


/**
 * Worker for PGMR3PhysMmio2Register and PGMR0PhysMmio2RegisterReq.
 *
 * (The caller already know which MMIO2 region ID will be assigned and how many
 * chunks will be used, so no output parameters required.)
 */
DECLHIDDEN(int) pgmPhysMmio2RegisterWorker(PVMCC pVM, uint32_t const cGuestPages, uint8_t const idMmio2,
                                           const uint8_t cChunks, PPDMDEVINSR3 const pDevIns, uint8_t
                                           const iSubDev, uint8_t const iRegion, uint32_t const fFlags)
{
    /*
     * Get the number of pages per chunk.
     */
    uint32_t cGuestPagesPerChunk;
    AssertReturn(pgmPhysMmio2CalcChunkCount((RTGCPHYS)cGuestPages << GUEST_PAGE_SHIFT, &cGuestPagesPerChunk) == cChunks,
                 VERR_PGM_PHYS_MMIO_EX_IPE);
    Assert(idMmio2 != 0);

    /*
     * The first thing we need to do is the allocate the memory that will be
     * backing the whole range.
     */
    RTGCPHYS const          cbMmio2Backing   = (RTGCPHYS)cGuestPages << GUEST_PAGE_SHIFT;
    uint32_t const          cHostPages       = (cbMmio2Backing + HOST_PAGE_SIZE - 1U) >> HOST_PAGE_SHIFT;
    size_t const            cbMmio2Aligned   = cHostPages << HOST_PAGE_SHIFT;
    R3PTRTYPE(uint8_t *)    pbMmio2BackingR3 = NIL_RTR3PTR;
#ifdef IN_RING0
    RTR0MEMOBJ              hMemObj          = NIL_RTR0MEMOBJ;
# ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    int rc = RTR0MemObjAllocPage(&hMemObj, cbMmio2Aligned, false /*fExecutable*/);
# else
    int rc = RTR0MemObjAllocPhysNC(&hMemObj, cbMmio2Aligned, NIL_RTHCPHYS);
# endif
#else  /* !IN_RING0 */
    AssertReturn(PGM_IS_IN_NEM_MODE(pVM), VERR_INTERNAL_ERROR_4);
    int rc = SUPR3PageAlloc(cHostPages, pVM->pgm.s.fUseLargePages ? SUP_PAGE_ALLOC_F_LARGE_PAGES : 0, (void **)&pbMmio2BackingR3);
#endif /* !IN_RING0 */
    if (RT_SUCCESS(rc))
    {
        /*
         * Make sure it's is initialized to zeros before it's mapped to userland.
         */
#ifdef IN_RING0
# ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
        uint8_t *pbMmio2BackingR0 = (uint8_t *)RTR0MemObjAddress(hMemObj);
        AssertPtr(pbMmio2BackingR0);
# endif
        rc = RTR0MemObjZeroInitialize(hMemObj, false /*fForce*/);
        AssertRCReturnStmt(rc, RTR0MemObjFree(hMemObj, true /*fFreeMappings*/), rc);
#else
        RT_BZERO(pbMmio2BackingR3, cbMmio2Aligned);
#endif

#ifdef IN_RING0
        /*
         * Map it into ring-3.
         */
        RTR0MEMOBJ hMapObj = NIL_RTR0MEMOBJ;
        rc = RTR0MemObjMapUser(&hMapObj, hMemObj, (RTR3PTR)-1, 0, RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
        if (RT_SUCCESS(rc))
        {
            pbMmio2BackingR3 = RTR0MemObjAddressR3(hMapObj);
#endif

            /*
             * Create the MMIO2 registration records and associated RAM ranges.
             * The RAM range allocation may fail here.
             */
            RTGCPHYS offMmio2Backing = 0;
            uint32_t cGuestPagesLeft = cGuestPages;
            for (uint32_t iChunk = 0, idx = idMmio2 - 1; iChunk < cChunks; iChunk++, idx++)
            {
                uint32_t const cPagesTrackedByChunk = RT_MIN(cGuestPagesLeft, cGuestPagesPerChunk);

                /*
                 * Allocate the RAM range for this chunk.
                 */
                uint32_t idRamRange = UINT32_MAX;
                rc = pgmPhysRamRangeAllocCommon(pVM, cPagesTrackedByChunk, PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO_EX, &idRamRange);
                if (RT_FAILURE(rc))
                {
                    /* We only zap the pointers to the backing storage.
                       PGMR3Term and friends will clean up the RAM ranges and stuff. */
                    while (iChunk-- > 0)
                    {
                        idx--;
#ifdef IN_RING0
                        pVM->pgmr0.s.acMmio2RangePages[idx] = 0;
# ifndef VBOX_WITH_LINEAR_HOST_PHYS_MEM
                        pVM->pgmr0.s.apbMmio2Backing[idx]   = NULL;
# endif
#endif

                        PPGMREGMMIO2RANGE const pMmio2 = &pVM->pgm.s.aMmio2Ranges[idx];
                        pMmio2->pbR3 = NIL_RTR3PTR;

                        PPGMRAMRANGE const      pRamRange = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apMmio2RamRanges[idx];
                        pRamRange->pbR3 = NIL_RTR3PTR;
                        RT_BZERO(&pRamRange->aPages[0], sizeof(pRamRange->aPages) * cGuestPagesPerChunk);
                    }
                    break;
                }

                pVM->pgm.s.apMmio2RamRanges[idx]    = pVM->pgm.s.apRamRanges[idRamRange];
#ifdef IN_RING0
                pVM->pgmr0.s.apMmio2RamRanges[idx]  = pVM->pgmr0.s.apRamRanges[idRamRange];
                pVM->pgmr0.s.acMmio2RangePages[idx] = cPagesTrackedByChunk;
#endif

                /* Initialize the RAM range. */
                PPGMRAMRANGE const pRamRange       = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
                pRamRange->pbR3 = pbMmio2BackingR3 + offMmio2Backing;
                uint32_t iDstPage = cPagesTrackedByChunk;
#ifdef IN_RING0
                AssertRelease(HOST_PAGE_SHIFT == GUEST_PAGE_SHIFT);
                while (iDstPage-- > 0)
                {
                    RTHCPHYS HCPhys = RTR0MemObjGetPagePhysAddr(hMemObj, iDstPage + (offMmio2Backing >> HOST_PAGE_SHIFT));
                    Assert(HCPhys != NIL_RTHCPHYS);
                    PGM_PAGE_INIT(&pRamRange->aPages[iDstPage], HCPhys, PGM_MMIO2_PAGEID_MAKE(idMmio2, iDstPage),
                                  PGMPAGETYPE_MMIO2, PGM_PAGE_STATE_ALLOCATED);
                }
#else
                Assert(PGM_IS_IN_NEM_MODE(pVM));
                while (iDstPage-- > 0)
                    PGM_PAGE_INIT(&pRamRange->aPages[iDstPage], UINT64_C(0x0000ffffffff0000),
                                  PGM_MMIO2_PAGEID_MAKE(idMmio2, iDstPage),
                                  PGMPAGETYPE_MMIO2, PGM_PAGE_STATE_ALLOCATED);
#endif

                /*
                 * Initialize the MMIO2 registration structure.
                 */
                PPGMREGMMIO2RANGE const pMmio2 = &pVM->pgm.s.aMmio2Ranges[idx];
                pMmio2->pDevInsR3       = pDevIns;
                pMmio2->pbR3            = pbMmio2BackingR3 + offMmio2Backing;
                pMmio2->fFlags          = 0;
                if (iChunk == 0)
                    pMmio2->fFlags     |= PGMREGMMIO2RANGE_F_FIRST_CHUNK;
                if (iChunk + 1 == cChunks)
                    pMmio2->fFlags     |= PGMREGMMIO2RANGE_F_LAST_CHUNK;
                if (fFlags & PGMPHYS_MMIO2_FLAGS_TRACK_DIRTY_PAGES)
                    pMmio2->fFlags     |= PGMREGMMIO2RANGE_F_TRACK_DIRTY_PAGES;

                pMmio2->iSubDev         = iSubDev;
                pMmio2->iRegion         = iRegion;
                pMmio2->idSavedState    = UINT8_MAX;
                pMmio2->idMmio2         = idMmio2 + iChunk;
                pMmio2->idRamRange      = idRamRange;
                Assert(pMmio2->idRamRange == idRamRange);
                pMmio2->GCPhys          = NIL_RTGCPHYS;
                pMmio2->cbReal          = (RTGCPHYS)cPagesTrackedByChunk << GUEST_PAGE_SHIFT;
                pMmio2->pPhysHandlerR3  = NIL_RTR3PTR; /* Pre-alloc is done by ring-3 caller. */
                pMmio2->paLSPages       = NIL_RTR3PTR;

#if defined(IN_RING0) && !defined(VBOX_WITH_LINEAR_HOST_PHYS_MEM)
                pVM->pgmr0.s.apbMmio2Backing[idx] = &pbMmio2BackingR0[offMmio2Backing];
#endif

                /* Advance */
                cGuestPagesLeft -= cPagesTrackedByChunk;
                offMmio2Backing += (RTGCPHYS)cPagesTrackedByChunk << GUEST_PAGE_SHIFT;
            } /* chunk alloc loop */
            Assert(cGuestPagesLeft == 0 || RT_FAILURE_NP(rc));
            if (RT_SUCCESS(rc))
            {
                /*
                 * Account for pages and ring-0 memory objects.
                 */
                pVM->pgm.s.cAllPages     += cGuestPages;
                pVM->pgm.s.cPrivatePages += cGuestPages;
#ifdef IN_RING0
                pVM->pgmr0.s.ahMmio2MemObjs[idMmio2 - 1] = hMemObj;
                pVM->pgmr0.s.ahMmio2MapObjs[idMmio2 - 1] = hMapObj;
#endif
                pVM->pgm.s.cMmio2Ranges = idMmio2 + cChunks - 1U;

                /*
                 * Done!.
                 */
                return VINF_SUCCESS;
            }

            /*
             * Bail.
             */
#ifdef IN_RING0
            RTR0MemObjFree(hMapObj, true /*fFreeMappings*/);
        }
        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
#else
        SUPR3PageFree(pbMmio2BackingR3, cHostPages);
#endif
    }
    else
        LogRel(("pgmPhysMmio2RegisterWorker: Failed to allocate %RGp bytes of MMIO2 backing memory: %Rrc\n", cbMmio2Aligned, rc));
    return rc;
}


#ifdef IN_RING0
/**
 * This is called during VM initialization to create an MMIO2 range.
 *
 * This does everything except setting the PGMRAMRANGE::pszDesc to a non-zero
 * value and preallocating the access handler for dirty bitmap tracking.
 *
 * The caller already knows which MMIO2 ID will be assigned to the registration
 * and how many chunks it requires, so there are no output fields in the request
 * structure.
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 * @param   pReq    Where to get the parameters.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PGMR0PhysMmio2RegisterReq(PGVM pGVM, PPGMPHYSMMIO2REGISTERREQ pReq)
{
    /*
     * Validate input (ASSUME pReq is a copy and can't be modified by ring-3
     * while we're here).
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x < %#zx\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    /** @todo better VM state guard, enmVMState is ring-3 writable. */
    VMSTATE const enmState = pGVM->enmVMState;
    AssertMsgReturn(   enmState == VMSTATE_CREATING
                    || enmState == VMSTATE_LOADING /* pre 4.3.6 state loading needs to ignore a MMIO2 region in PCNet. */
                    , ("enmState=%d\n", enmState), VERR_VM_INVALID_VM_STATE);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    AssertReturn(pReq->cbGuestPage == GUEST_PAGE_SIZE, VERR_INCOMPATIBLE_CONFIG);
    AssertReturn(GUEST_PAGE_SIZE == HOST_PAGE_SIZE, VERR_INCOMPATIBLE_CONFIG);

    AssertReturn(pReq->cGuestPages > 0, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cGuestPages <= PGM_MAX_PAGES_PER_MMIO2_REGION, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cGuestPages <= (MM_MMIO_64_MAX >> GUEST_PAGE_SHIFT), VERR_OUT_OF_RANGE);

    AssertMsgReturn(!(pReq->fFlags & ~PGMPHYS_MMIO2_FLAGS_VALID_MASK), ("fFlags=%#x\n", pReq->fFlags), VERR_INVALID_FLAGS);

    AssertMsgReturn(   pReq->cChunks >  0
                    && pReq->cChunks <  PGM_MAX_MMIO2_RANGES
                    && pReq->cChunks == pgmPhysMmio2CalcChunkCount((RTGCPHYS)pReq->cGuestPages << GUEST_PAGE_SHIFT, NULL),
                    ("cChunks=%#x cGuestPages=%#x\n", pReq->cChunks, pReq->cGuestPages),
                    VERR_INVALID_PARAMETER);

    AssertMsgReturn(   pReq->idMmio2 != 0
                    && pReq->idMmio2 <= PGM_MAX_MMIO2_RANGES
                    && (unsigned)pReq->idMmio2 + pReq->cChunks - 1U <= PGM_MAX_MMIO2_RANGES,
                    ("idMmio2=%#x cChunks=%#x\n", pReq->idMmio2, pReq->cChunks),
                    VERR_INVALID_PARAMETER);

    for (uint32_t iChunk = 0, idx = pReq->idMmio2 - 1; iChunk < pReq->cChunks; iChunk++, idx++)
    {
        AssertReturn(pGVM->pgmr0.s.ahMmio2MapObjs[idx] == NIL_RTR0MEMOBJ, VERR_INVALID_STATE);
        AssertReturn(pGVM->pgmr0.s.ahMmio2MemObjs[idx] == NIL_RTR0MEMOBJ, VERR_INVALID_STATE);
        AssertReturn(pGVM->pgmr0.s.apMmio2RamRanges[idx] == NULL, VERR_INVALID_STATE);
    }

    /*
     * Make sure we're owning the PGM lock (caller should be), recheck idMmio2
     * and call the worker function we share with ring-3.
     */
    int rc = PGM_LOCK(pGVM);
    AssertRCReturn(rc, rc);

    AssertReturnStmt(pGVM->pgm.s.cMmio2Ranges + 1U == pReq->idMmio2,
                     PGM_UNLOCK(pGVM), VERR_INVALID_PARAMETER);
    AssertReturnStmt(pGVM->pgmr0.s.idRamRangeMax + 1U + pReq->cChunks <= RT_ELEMENTS(pGVM->pgmr0.s.apRamRanges),
                     PGM_UNLOCK(pGVM), VERR_PGM_TOO_MANY_RAM_RANGES);

    rc = pgmPhysMmio2RegisterWorker(pGVM, pReq->cGuestPages, pReq->idMmio2, pReq->cChunks,
                                    pReq->pDevIns, pReq->iSubDev, pReq->iRegion, pReq->fFlags);

    PGM_UNLOCK(pGVM);
    return rc;
}
#endif /* IN_RING0 */



/**
 * Worker for PGMR3PhysMmio2Deregister & PGMR0PhysMmio2DeregisterReq.
 */
DECLHIDDEN(int) pgmPhysMmio2DeregisterWorker(PVMCC pVM, uint8_t idMmio2, uint8_t cChunks, PPDMDEVINSR3 pDevIns)
{
    /*
     * The caller shall have made sure all this is true, but we check again
     * since we're paranoid.
     */
    AssertReturn(idMmio2 > 0 && idMmio2 <= RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges), VERR_INTERNAL_ERROR_2);
    AssertReturn(cChunks >= 1, VERR_INTERNAL_ERROR_2);
    uint8_t const idxFirst = idMmio2 - 1U;
    AssertReturn(idxFirst + cChunks <= pVM->pgm.s.cMmio2Ranges, VERR_INTERNAL_ERROR_2);
    uint32_t      cGuestPages = 0; /* (For accounting and calulating backing memory size) */
    for (uint32_t iChunk = 0, idx = idxFirst; iChunk < cChunks; iChunk++, idx++)
    {
        AssertReturn(pVM->pgm.s.aMmio2Ranges[idx].pDevInsR3 == pDevIns, VERR_NOT_OWNER);
        AssertReturn(!(pVM->pgm.s.aMmio2Ranges[idx].fFlags & PGMREGMMIO2RANGE_F_MAPPED), VERR_RESOURCE_BUSY);
        AssertReturn(pVM->pgm.s.aMmio2Ranges[idx].GCPhys == NIL_RTGCPHYS, VERR_INVALID_STATE);
        if (iChunk == 0)
            AssertReturn(pVM->pgm.s.aMmio2Ranges[idx].fFlags & PGMREGMMIO2RANGE_F_FIRST_CHUNK, VERR_INVALID_PARAMETER);
        else
            AssertReturn(!(pVM->pgm.s.aMmio2Ranges[idx].fFlags & PGMREGMMIO2RANGE_F_FIRST_CHUNK), VERR_INVALID_PARAMETER);
        if (iChunk + 1 == cChunks)
            AssertReturn(pVM->pgm.s.aMmio2Ranges[idx].fFlags & PGMREGMMIO2RANGE_F_LAST_CHUNK, VERR_INVALID_PARAMETER);
        else
            AssertReturn(!(pVM->pgm.s.aMmio2Ranges[idx].fFlags & PGMREGMMIO2RANGE_F_LAST_CHUNK), VERR_INVALID_PARAMETER);
        AssertReturn(pVM->pgm.s.aMmio2Ranges[idx].pPhysHandlerR3 == NIL_RTR3PTR, VERR_INVALID_STATE); /* caller shall free this */

#ifdef IN_RING0
        cGuestPages += pVM->pgmr0.s.acMmio2RangePages[idx];
#else
        cGuestPages += pVM->pgm.s.aMmio2Ranges[idx].cbReal >> GUEST_PAGE_SHIFT;
#endif

        PPGMRAMRANGE const pRamRange = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apMmio2RamRanges[idx];
        AssertPtrReturn(pRamRange, VERR_INVALID_STATE);
        AssertReturn(pRamRange->fFlags & PGM_RAM_RANGE_FLAGS_AD_HOC_MMIO_EX, VERR_INVALID_STATE);
        AssertReturn(pRamRange->GCPhys     == NIL_RTGCPHYS, VERR_INVALID_STATE);
        AssertReturn(pRamRange->GCPhysLast == NIL_RTGCPHYS, VERR_INVALID_STATE);
    }

    /*
     * Remove everything except the backing memory first.  We work the ranges
     * in reverse so that we can reduce the max RAM range ID when possible.
     */
#ifdef IN_RING3
    uint8_t * const pbMmio2Backing = pVM->pgm.s.aMmio2Ranges[idxFirst].pbR3;
    RTGCPHYS const  cbMmio2Backing = RT_ALIGN_T((RTGCPHYS)cGuestPages << GUEST_PAGE_SHIFT, HOST_PAGE_SIZE, RTGCPHYS);
#endif

    int      rc     = VINF_SUCCESS;
    uint32_t iChunk = cChunks;
    while (iChunk-- > 0)
    {
        uint32_t const     idx       = idxFirst + iChunk;
        PPGMRAMRANGE const pRamRange = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apMmio2RamRanges[idx];

        /* Zap the MMIO2 region data. */
        pVM->pgm.s.apMmio2RamRanges[idx]    = NIL_RTR3PTR;
#ifdef IN_RING0
        pVM->pgmr0.s.apMmio2RamRanges[idx]  = NULL;
        pVM->pgmr0.s.acMmio2RangePages[idx] = 0;
#endif
        pVM->pgm.s.aMmio2Ranges[idx].pDevInsR3      = NIL_RTR3PTR;
        pVM->pgm.s.aMmio2Ranges[idx].pbR3           = NIL_RTR3PTR;
        pVM->pgm.s.aMmio2Ranges[idx].fFlags         = 0;
        pVM->pgm.s.aMmio2Ranges[idx].iSubDev        = UINT8_MAX;
        pVM->pgm.s.aMmio2Ranges[idx].iRegion        = UINT8_MAX;
        pVM->pgm.s.aMmio2Ranges[idx].idSavedState   = UINT8_MAX;
        pVM->pgm.s.aMmio2Ranges[idx].idMmio2        = UINT8_MAX;
        pVM->pgm.s.aMmio2Ranges[idx].idRamRange     = UINT16_MAX;
        pVM->pgm.s.aMmio2Ranges[idx].GCPhys         = NIL_RTGCPHYS;
        pVM->pgm.s.aMmio2Ranges[idx].cbReal         = 0;
        pVM->pgm.s.aMmio2Ranges[idx].pPhysHandlerR3 = NIL_RTR3PTR;
        pVM->pgm.s.aMmio2Ranges[idx].paLSPages      = NIL_RTR3PTR;

        /* Free the RAM range. */
        int rc2 = pgmPhysRamRangeFree(pVM, pRamRange);
        AssertLogRelMsgStmt(RT_SUCCESS(rc2), ("rc=%Rrc idx=%u chunk=%u/%u\n", rc, idx, iChunk + 1, cChunks),
                            rc = RT_SUCCESS(rc) ? rc2 : rc);
    }

    /*
     * Final removal frees up the backing memory.
     */
#ifdef IN_RING3
    int const rcBacking = SUPR3PageFree(pbMmio2Backing, cbMmio2Backing >> HOST_PAGE_SHIFT);
    AssertLogRelMsgStmt(RT_SUCCESS(rcBacking), ("rc=%Rrc %p LB %#zx\n", rcBacking, pbMmio2Backing, cbMmio2Backing),
                        rc = RT_SUCCESS(rc) ? rcBacking : rc);
#else
    int rcBacking = RTR0MemObjFree(pVM->pgmr0.s.ahMmio2MapObjs[idxFirst], true /*fFreeMappings*/);
    AssertLogRelMsgStmt(RT_SUCCESS(rcBacking),
                        ("rc=%Rrc ahMmio2MapObjs[%u]=%p\n", rcBacking, pVM->pgmr0.s.ahMmio2MapObjs[idxFirst], idxFirst),
                        rc = RT_SUCCESS(rc) ? rcBacking : rc);
    if (RT_SUCCESS(rcBacking))
    {
        pVM->pgmr0.s.ahMmio2MapObjs[idxFirst] = NIL_RTR0MEMOBJ;

        rcBacking = RTR0MemObjFree(pVM->pgmr0.s.ahMmio2MemObjs[idxFirst], true /*fFreeMappings*/);
        AssertLogRelMsgStmt(RT_SUCCESS(rcBacking),
                            ("rc=%Rrc ahMmio2MemObjs[%u]=%p\n", rcBacking, pVM->pgmr0.s.ahMmio2MemObjs[idxFirst], idxFirst),
                            rc = RT_SUCCESS(rc) ? rcBacking : rc);
        if (RT_SUCCESS(rcBacking))
            pVM->pgmr0.s.ahMmio2MemObjs[idxFirst] = NIL_RTR0MEMOBJ;
    }
#endif

    /*
     * Decrease the MMIO2 count if these were the last ones.
     */
    if (idxFirst + cChunks == pVM->pgm.s.cMmio2Ranges)
        pVM->pgm.s.cMmio2Ranges = idxFirst;

    /*
     * Update page count stats.
     */
    pVM->pgm.s.cAllPages     -= cGuestPages;
    pVM->pgm.s.cPrivatePages -= cGuestPages;

    return rc;
}


#ifdef IN_RING0
/**
 * This is called during VM state loading to deregister an obsolete MMIO2 range.
 *
 * This does everything except TLB flushing and releasing the access handler.
 * The ranges must be unmapped and wihtout preallocated access handlers.
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 * @param   pReq    Where to get the parameters.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PGMR0PhysMmio2DeregisterReq(PGVM pGVM, PPGMPHYSMMIO2DEREGISTERREQ pReq)
{
    /*
     * Validate input (ASSUME pReq is a copy and can't be modified by ring-3
     * while we're here).
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x < %#zx\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    /** @todo better VM state guard, enmVMState is ring-3 writable. */
    /* Only LOADING, as this is special purpose for removing an unwanted PCNet MMIO2 region. */
    VMSTATE const enmState = pGVM->enmVMState;
    AssertMsgReturn(enmState == VMSTATE_LOADING, ("enmState=%d\n", enmState), VERR_VM_INVALID_VM_STATE);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    AssertMsgReturn(   pReq->cChunks > 0
                    && pReq->cChunks < PGM_MAX_MMIO2_RANGES,
                    ("idMmio2=%#x cChunks=%#x\n", pReq->idMmio2, pReq->cChunks),
                    VERR_INVALID_PARAMETER);

    AssertMsgReturn(   pReq->idMmio2 != 0
                    && pReq->idMmio2 <= PGM_MAX_MMIO2_RANGES
                    && (unsigned)pReq->idMmio2 + pReq->cChunks - 1U <= PGM_MAX_MMIO2_RANGES,
                    ("idMmio2=%#x cChunks=%#x\n", pReq->idMmio2, pReq->cChunks),
                    VERR_INVALID_PARAMETER);

    /*
     * Validate that the requested range is for exactly one MMIO2 registration.
     *
     * This is safe to do w/o the lock because registration and deregistration
     * is restricted to EMT0, and we're on EMT0 so can't race ourselves.
     */

    /* Check that the first entry is valid and has a memory object for the backing memory. */
    uint32_t idx = pReq->idMmio2 - 1;
    AssertReturn(pGVM->pgmr0.s.apMmio2RamRanges[idx]     != NULL, VERR_INVALID_STATE);
    AssertReturn(pGVM->pgmr0.s.ahMmio2MemObjs[idx]       != NIL_RTR0MEMOBJ, VERR_INVALID_STATE);

    /* Any additional regions must also have RAM ranges, but shall not have any backing memory. */
    idx++;
    for (uint32_t iChunk = 1; iChunk < pReq->cChunks; iChunk++, idx++)
    {
        AssertReturn(pGVM->pgmr0.s.apMmio2RamRanges[idx]     != NULL, VERR_INVALID_STATE);
        AssertReturn(pGVM->pgmr0.s.ahMmio2MemObjs[idx]       == NIL_RTR0MEMOBJ, VERR_INVALID_STATE);
    }

    /* Check that the next entry is for a different region. */
    AssertReturn(   idx >= RT_ELEMENTS(pGVM->pgmr0.s.apMmio2RamRanges)
                 || pGVM->pgmr0.s.apMmio2RamRanges[idx] == NULL
                 || pGVM->pgmr0.s.ahMmio2MemObjs[idx]   != NIL_RTR0MEMOBJ,
                 VERR_INVALID_PARAMETER);

    /*
     * Make sure we're owning the PGM lock (caller should be) and call the
     * common worker code.
     */
    int rc = PGM_LOCK(pGVM);
    AssertRCReturn(rc, rc);

    rc = pgmPhysMmio2DeregisterWorker(pGVM, pReq->idMmio2, pReq->cChunks, pReq->pDevIns);

    PGM_UNLOCK(pGVM);
    return rc;
}
#endif /* IN_RING0 */




/*********************************************************************************************************************************
*   ROM                                                                                                                          *
*********************************************************************************************************************************/


/**
 * Common worker for pgmR3PhysRomRegisterLocked and
 * PGMR0PhysRomAllocateRangeReq.
 */
DECLHIDDEN(int) pgmPhysRomRangeAllocCommon(PVMCC pVM, uint32_t cPages, uint8_t idRomRange, uint32_t fFlags)
{
    /*
     * Allocate the ROM range structure and map it into ring-3.
     */
    size_t const cbRomRange = RT_ALIGN_Z(RT_UOFFSETOF_DYN(PGMROMRANGE, aPages[cPages]), HOST_PAGE_SIZE);
#ifdef IN_RING0
    RTR0MEMOBJ   hMemObj    = NIL_RTR0MEMOBJ;
    int rc = RTR0MemObjAllocPage(&hMemObj, cbRomRange, false /*fExecutable*/);
#else
    PPGMROMRANGE pRomRange;
    int rc = SUPR3PageAlloc(cbRomRange >> HOST_PAGE_SHIFT, 0 /*fFlags*/, (void **)&pRomRange);
#endif
    if (RT_SUCCESS(rc))
    {
        /* Zero the memory and do basic range init before mapping it into userland. */
#ifdef IN_RING0
        PPGMROMRANGE const pRomRange = (PPGMROMRANGE)RTR0MemObjAddress(hMemObj);
        if (!RTR0MemObjWasZeroInitialized(hMemObj))
#endif
            RT_BZERO(pRomRange, cbRomRange);

        pRomRange->GCPhys       = NIL_RTGCPHYS;
        pRomRange->GCPhysLast   = NIL_RTGCPHYS;
        pRomRange->cb           = (RTGCPHYS)cPages << GUEST_PAGE_SHIFT;
        pRomRange->fFlags       = fFlags;
        pRomRange->idSavedState = UINT8_MAX;
        pRomRange->idRamRange   = UINT16_MAX;
        pRomRange->cbOriginal   = 0;
        pRomRange->pvOriginal   = NIL_RTR3PTR;
        pRomRange->pszDesc      = NIL_RTR3PTR;

#ifdef IN_RING0
        /* Map it into userland. */
        RTR0MEMOBJ hMapObj  = NIL_RTR0MEMOBJ;
        rc = RTR0MemObjMapUser(&hMapObj, hMemObj, (RTR3PTR)-1, 0 /*uAlignment*/,
                               RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
        if (RT_SUCCESS(rc))
#endif
        {
            /*
             * Grab the lock (unlikely to fail or block as caller typically owns it already).
             */
            rc = PGM_LOCK(pVM);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Check that idRomRange is still free.
                 */
                if (idRomRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRomRanges))
                {
#ifdef IN_RING0
                    if (pVM->pgmr0.s.apRomRanges[idRomRange] == NULL)
#endif
                    {
                        if (   pVM->pgm.s.apRomRanges[idRomRange] == NIL_RTR3PTR
                            && pVM->pgm.s.cRomRanges              == idRomRange)
                        {
                            /*
                             * Commit it.
                             */
#ifdef IN_RING0
                            pVM->pgmr0.s.apRomRanges[idRomRange]       = pRomRange;
                            pVM->pgmr0.s.acRomRangePages[idRomRange]   = cPages;
                            pVM->pgmr0.s.ahRomRangeMemObjs[idRomRange] = hMemObj;
                            pVM->pgmr0.s.ahRomRangeMapObjs[idRomRange] = hMapObj;
#endif

                            pVM->pgm.s.cRomRanges                      = idRomRange + 1;
#ifdef IN_RING0
                            pVM->pgm.s.apRomRanges[idRomRange]         = RTR0MemObjAddressR3(hMapObj);
#else
                            pVM->pgm.s.apRomRanges[idRomRange]         = pRomRange;
#endif

                            PGM_UNLOCK(pVM);
                            return VINF_SUCCESS;
                        }
                    }

                    /*
                     * Bail out.
                     */
                    rc = VERR_INTERNAL_ERROR_5;
                }
                else
                    rc = VERR_PGM_TOO_MANY_ROM_RANGES;
                PGM_UNLOCK(pVM);
            }
#ifdef IN_RING0
            RTR0MemObjFree(hMapObj, false /*fFreeMappings*/);
#endif
        }
#ifdef IN_RING0
        RTR0MemObjFree(hMemObj, true /*fFreeMappings*/);
#else
        SUPR3PageFree(pRomRange, cbRomRange >> HOST_PAGE_SHIFT);
#endif
    }
    return rc;
}


#ifdef IN_RING0
/**
 * This is called during VM initialization to allocate a ROM range.
 *
 * The page array is zeroed, the rest is initialized as best we can based on the
 * information in @a pReq.
 *
 * @returns VBox status code.
 * @param   pGVM    Pointer to the global VM structure.
 * @param   pReq    Where to get the parameters and return the range ID.
 * @thread  EMT(0)
 */
VMMR0_INT_DECL(int) PGMR0PhysRomAllocateRangeReq(PGVM pGVM, PPGMPHYSROMALLOCATERANGEREQ pReq)
{
    /*
     * Validate input (ASSUME pReq is a copy and can't be modified by ring-3
     * while we're here).
     */
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);
    AssertMsgReturn(pReq->Hdr.cbReq == sizeof(*pReq), ("%#x < %#zx\n", pReq->Hdr.cbReq, sizeof(*pReq)), VERR_INVALID_PARAMETER);

    AssertReturn(pReq->cbGuestPage == GUEST_PAGE_SIZE, VERR_INCOMPATIBLE_CONFIG);

    AssertReturn(pReq->cGuestPages > 0, VERR_OUT_OF_RANGE);
    AssertReturn(pReq->cGuestPages <= PGM_MAX_PAGES_PER_ROM_RANGE, VERR_OUT_OF_RANGE);

    AssertMsgReturn(!(pReq->fFlags & ~(uint32_t)PGMPHYS_ROM_FLAGS_VALID_MASK), ("fFlags=%#RX32\n", pReq->fFlags),
                    VERR_INVALID_FLAGS);

    AssertReturn(pReq->idRomRange < RT_ELEMENTS(pGVM->pgmr0.s.apRomRanges), VERR_OUT_OF_RANGE);
    AssertReturn(pReq->idRomRange == pGVM->pgm.s.cRomRanges, VERR_OUT_OF_RANGE);

    /** @todo better VM state guard, enmVMState is ring-3 writable. */
    VMSTATE const enmState = pGVM->enmVMState;
    AssertMsgReturn(enmState == VMSTATE_CREATING, ("enmState=%d\n", enmState), VERR_VM_INVALID_VM_STATE);
    VM_ASSERT_EMT0_RETURN(pGVM, VERR_VM_THREAD_NOT_EMT);

    /*
     * Call common worker.
     */
    return pgmPhysRomRangeAllocCommon(pGVM, pReq->cGuestPages, pReq->idRomRange, pReq->fFlags);
}
#endif /* IN_RING0 */


/*********************************************************************************************************************************
*   Other stuff
*********************************************************************************************************************************/



/**
 * Checks if Address Gate 20 is enabled or not.
 *
 * @returns true if enabled.
 * @returns false if disabled.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMDECL(bool) PGMPhysIsA20Enabled(PVMCPU pVCpu)
{
    /* Must check that pVCpu isn't NULL here because PDM device helper are a little lazy. */
    LogFlow(("PGMPhysIsA20Enabled %d\n", pVCpu && pVCpu->pgm.s.fA20Enabled));
    return pVCpu && pVCpu->pgm.s.fA20Enabled;
}


/**
 * Validates a GC physical address.
 *
 * @returns true if valid.
 * @returns false if invalid.
 * @param   pVM     The cross context VM structure.
 * @param   GCPhys  The physical address to validate.
 */
VMMDECL(bool) PGMPhysIsGCPhysValid(PVMCC pVM, RTGCPHYS GCPhys)
{
    PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhys);
    return pPage != NULL;
}


/**
 * Checks if a GC physical address is a normal page,
 * i.e. not ROM, MMIO or reserved.
 *
 * @returns true if normal.
 * @returns false if invalid, ROM, MMIO or reserved page.
 * @param   pVM     The cross context VM structure.
 * @param   GCPhys  The physical address to check.
 */
VMMDECL(bool) PGMPhysIsGCPhysNormal(PVMCC pVM, RTGCPHYS GCPhys)
{
    PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhys);
    return pPage
        && PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM;
}


/**
 * Converts a GC physical address to a HC physical address.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_PGM_PHYS_PAGE_RESERVED it it's a valid GC physical
 *          page but has no physical backing.
 * @returns VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid
 *          GC physical address.
 *
 * @param   pVM     The cross context VM structure.
 * @param   GCPhys  The GC physical address to convert.
 * @param   pHCPhys Where to store the HC physical address on success.
 */
VMM_INT_DECL(int) PGMPhysGCPhys2HCPhys(PVMCC pVM, RTGCPHYS GCPhys, PRTHCPHYS pHCPhys)
{
    PGM_LOCK_VOID(pVM);
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhys, &pPage);
    if (RT_SUCCESS(rc))
        *pHCPhys = PGM_PAGE_GET_HCPHYS(pPage) | (GCPhys & GUEST_PAGE_OFFSET_MASK);
    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Invalidates all page mapping TLBs.
 *
 * @param   pVM             The cross context VM structure.
 * @param   fInRendezvous   Set if we're in a rendezvous.
 */
void pgmPhysInvalidatePageMapTLB(PVMCC pVM, bool fInRendezvous)
{
    PGM_LOCK_VOID(pVM);
    STAM_COUNTER_INC(&pVM->pgm.s.Stats.StatPageMapTlbFlushes);

    /* Clear the R3 & R0 TLBs completely. */
    for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.PhysTlbR0.aEntries); i++)
    {
        pVM->pgm.s.PhysTlbR0.aEntries[i].GCPhys = NIL_RTGCPHYS;
        pVM->pgm.s.PhysTlbR0.aEntries[i].pPage = 0;
        pVM->pgm.s.PhysTlbR0.aEntries[i].pv = 0;
    }

    for (unsigned i = 0; i < RT_ELEMENTS(pVM->pgm.s.PhysTlbR3.aEntries); i++)
    {
        pVM->pgm.s.PhysTlbR3.aEntries[i].GCPhys = NIL_RTGCPHYS;
        pVM->pgm.s.PhysTlbR3.aEntries[i].pPage = 0;
        pVM->pgm.s.PhysTlbR3.aEntries[i].pMap = 0;
        pVM->pgm.s.PhysTlbR3.aEntries[i].pv = 0;
    }

    /* For the per VCPU lockless TLBs, we only invalid the GCPhys members so that
       anyone concurrently using the entry can safely continue to do so while any
       subsequent attempts to use it will fail. (Emulating a scenario where we
       lost the PGM lock race and the concurrent TLB user wont it.) */
    VMCC_FOR_EACH_VMCPU(pVM)
    {
        if (!fInRendezvous && pVCpu != VMMGetCpu(pVM))
            for (unsigned idx = 0; idx < RT_ELEMENTS(pVCpu->pgm.s.PhysTlb.aEntries); idx++)
                ASMAtomicWriteU64(&pVCpu->pgm.s.PhysTlb.aEntries[idx].GCPhys, NIL_RTGCPHYS);
        else
            for (unsigned idx = 0; idx < RT_ELEMENTS(pVCpu->pgm.s.PhysTlb.aEntries); idx++)
                pVCpu->pgm.s.PhysTlb.aEntries[idx].GCPhys = NIL_RTGCPHYS;
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);

    IEMTlbInvalidateAllPhysicalAllCpus(pVM, NIL_VMCPUID, IEMTLBPHYSFLUSHREASON_MISC);
    PGM_UNLOCK(pVM);
}


/**
 * Invalidates a page mapping TLB entry
 *
 * @param   pVM     The cross context VM structure.
 * @param   GCPhys  GCPhys entry to flush
 *
 * @note    Caller is responsible for calling IEMTlbInvalidateAllPhysicalAllCpus
 *          when needed.
 */
void pgmPhysInvalidatePageMapTLBEntry(PVMCC pVM, RTGCPHYS GCPhys)
{
    PGM_LOCK_ASSERT_OWNER(pVM);

    STAM_COUNTER_INC(&pVM->pgm.s.Stats.StatPageMapTlbFlushEntry);

    unsigned const idx = PGM_PAGER3MAPTLB_IDX(GCPhys);

    pVM->pgm.s.PhysTlbR0.aEntries[idx].GCPhys = NIL_RTGCPHYS;
    pVM->pgm.s.PhysTlbR0.aEntries[idx].pPage = 0;
    pVM->pgm.s.PhysTlbR0.aEntries[idx].pv = 0;

    pVM->pgm.s.PhysTlbR3.aEntries[idx].GCPhys = NIL_RTGCPHYS;
    pVM->pgm.s.PhysTlbR3.aEntries[idx].pPage = 0;
    pVM->pgm.s.PhysTlbR3.aEntries[idx].pMap = 0;
    pVM->pgm.s.PhysTlbR3.aEntries[idx].pv = 0;

    /* For the per VCPU lockless TLBs, we only invalid the GCPhys member so that
       anyone concurrently using the entry can safely continue to do so while any
       subsequent attempts to use it will fail. (Emulating a scenario where we
       lost the PGM lock race and the concurrent TLB user wont it.) */
    VMCC_FOR_EACH_VMCPU(pVM)
    {
        ASMAtomicWriteU64(&pVCpu->pgm.s.PhysTlb.aEntries[idx].GCPhys, NIL_RTGCPHYS);
    }
    VMCC_FOR_EACH_VMCPU_END(pVM);
}


/**
 * Makes sure that there is at least one handy page ready for use.
 *
 * This will also take the appropriate actions when reaching water-marks.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_EM_NO_MEMORY if we're really out of memory.
 *
 * @param   pVM     The cross context VM structure.
 *
 * @remarks Must be called from within the PGM critical section. It may
 *          nip back to ring-3/0 in some cases.
 */
static int pgmPhysEnsureHandyPage(PVMCC pVM)
{
    AssertMsg(pVM->pgm.s.cHandyPages <= RT_ELEMENTS(pVM->pgm.s.aHandyPages), ("%d\n", pVM->pgm.s.cHandyPages));

    /*
     * Do we need to do anything special?
     */
#ifdef IN_RING3
    if (pVM->pgm.s.cHandyPages <= RT_MAX(PGM_HANDY_PAGES_SET_FF, PGM_HANDY_PAGES_R3_ALLOC))
#else
    if (pVM->pgm.s.cHandyPages <= RT_MAX(PGM_HANDY_PAGES_SET_FF, PGM_HANDY_PAGES_RZ_TO_R3))
#endif
    {
        /*
         * Allocate pages only if we're out of them, or in ring-3, almost out.
         */
#ifdef IN_RING3
        if (pVM->pgm.s.cHandyPages <= PGM_HANDY_PAGES_R3_ALLOC)
#else
        if (pVM->pgm.s.cHandyPages <= PGM_HANDY_PAGES_RZ_ALLOC)
#endif
        {
            Log(("PGM: cHandyPages=%u out of %u -> allocate more; VM_FF_PGM_NO_MEMORY=%RTbool\n",
                 pVM->pgm.s.cHandyPages, RT_ELEMENTS(pVM->pgm.s.aHandyPages), VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY) ));
#ifdef IN_RING3
            int rc = PGMR3PhysAllocateHandyPages(pVM);
#else
            int rc = pgmR0PhysAllocateHandyPages(pVM, VMMGetCpuId(pVM), false /*fRing3*/);
#endif
            if (RT_UNLIKELY(rc != VINF_SUCCESS))
            {
                if (RT_FAILURE(rc))
                    return rc;
                AssertMsgReturn(rc == VINF_EM_NO_MEMORY, ("%Rrc\n", rc), VERR_IPE_UNEXPECTED_INFO_STATUS);
                if (!pVM->pgm.s.cHandyPages)
                {
                    LogRel(("PGM: no more handy pages!\n"));
                    return VERR_EM_NO_MEMORY;
                }
                Assert(VM_FF_IS_SET(pVM, VM_FF_PGM_NEED_HANDY_PAGES));
                Assert(VM_FF_IS_SET(pVM, VM_FF_PGM_NO_MEMORY));
#ifndef IN_RING3
                VMCPU_FF_SET(VMMGetCpu(pVM), VMCPU_FF_TO_R3); /* paranoia */
#endif
            }
            AssertMsgReturn(    pVM->pgm.s.cHandyPages > 0
                            &&  pVM->pgm.s.cHandyPages <= RT_ELEMENTS(pVM->pgm.s.aHandyPages),
                            ("%u\n", pVM->pgm.s.cHandyPages),
                            VERR_PGM_HANDY_PAGE_IPE);
        }
        else
        {
            if (pVM->pgm.s.cHandyPages <= PGM_HANDY_PAGES_SET_FF)
                VM_FF_SET(pVM, VM_FF_PGM_NEED_HANDY_PAGES);
#ifndef IN_RING3
            if (pVM->pgm.s.cHandyPages <= PGM_HANDY_PAGES_RZ_TO_R3)
            {
                Log(("PGM: VM_FF_TO_R3 - cHandyPages=%u out of %u\n", pVM->pgm.s.cHandyPages, RT_ELEMENTS(pVM->pgm.s.aHandyPages)));
                VMCPU_FF_SET(VMMGetCpu(pVM), VMCPU_FF_TO_R3);
            }
#endif
        }
    }

    return VINF_SUCCESS;
}


/**
 * Replace a zero or shared page with new page that we can write to.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success, pPage is modified.
 * @retval  VINF_PGM_SYNC_CR3 on success and a page pool flush is pending.
 * @retval  VERR_EM_NO_MEMORY if we're totally out of memory.
 *
 * @todo    Propagate VERR_EM_NO_MEMORY up the call tree.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The physical page tracking structure. This will
 *                      be modified on success.
 * @param   GCPhys      The address of the page.
 *
 * @remarks Must be called from within the PGM critical section. It may
 *          nip back to ring-3/0 in some cases.
 *
 * @remarks This function shouldn't really fail, however if it does
 *          it probably means we've screwed up the size of handy pages and/or
 *          the low-water mark. Or, that some device I/O is causing a lot of
 *          pages to be allocated while while the host is in a low-memory
 *          condition. This latter should be handled elsewhere and in a more
 *          controlled manner, it's on the @bugref{3170} todo list...
 */
int pgmPhysAllocPage(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    LogFlow(("pgmPhysAllocPage: %R[pgmpage] %RGp\n", pPage, GCPhys));

    /*
     * Prereqs.
     */
    PGM_LOCK_ASSERT_OWNER(pVM);
    AssertMsg(PGM_PAGE_IS_ZERO(pPage) || PGM_PAGE_IS_SHARED(pPage), ("%R[pgmpage] %RGp\n", pPage, GCPhys));
    Assert(!PGM_PAGE_IS_MMIO_OR_ALIAS(pPage));

# ifdef PGM_WITH_LARGE_PAGES
    /*
     * Try allocate a large page if applicable.
     */
    if (   PGMIsUsingLargePages(pVM)
        && PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM
        && !VM_IS_NEM_ENABLED(pVM)) /** @todo NEM: Implement large pages support. */
    {
        RTGCPHYS GCPhysBase = GCPhys & X86_PDE2M_PAE_PG_MASK;
        PPGMPAGE pBasePage;

        int rc = pgmPhysGetPageEx(pVM, GCPhysBase, &pBasePage);
        AssertRCReturn(rc, rc);     /* paranoia; can't happen. */
        if (PGM_PAGE_GET_PDE_TYPE(pBasePage) == PGM_PAGE_PDE_TYPE_DONTCARE)
        {
            rc = pgmPhysAllocLargePage(pVM, GCPhys);
            if (rc == VINF_SUCCESS)
                return rc;
        }
        /* Mark the base as type page table, so we don't check over and over again. */
        PGM_PAGE_SET_PDE_TYPE(pVM, pBasePage, PGM_PAGE_PDE_TYPE_PT);

        /* fall back to 4KB pages. */
    }
# endif

    /*
     * Flush any shadow page table mappings of the page.
     * When VBOX_WITH_NEW_LAZY_PAGE_ALLOC isn't defined, there shouldn't be any.
     */
    bool fFlushTLBs = false;
    int rc = pgmPoolTrackUpdateGCPhys(pVM, GCPhys, pPage, true /*fFlushTLBs*/, &fFlushTLBs);
    AssertMsgReturn(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3, ("%Rrc\n", rc), RT_FAILURE(rc) ? rc : VERR_IPE_UNEXPECTED_STATUS);

    /*
     * Ensure that we've got a page handy, take it and use it.
     */
    int rc2 = pgmPhysEnsureHandyPage(pVM);
    if (RT_FAILURE(rc2))
    {
        if (fFlushTLBs)
            PGM_INVL_ALL_VCPU_TLBS(pVM);
        Assert(rc2 == VERR_EM_NO_MEMORY);
        return rc2;
    }
    /* re-assert preconditions since pgmPhysEnsureHandyPage may do a context switch. */
    PGM_LOCK_ASSERT_OWNER(pVM);
    AssertMsg(PGM_PAGE_IS_ZERO(pPage) || PGM_PAGE_IS_SHARED(pPage), ("%R[pgmpage] %RGp\n", pPage, GCPhys));
    Assert(!PGM_PAGE_IS_MMIO_OR_ALIAS(pPage));

    uint32_t iHandyPage = --pVM->pgm.s.cHandyPages;
    AssertMsg(iHandyPage < RT_ELEMENTS(pVM->pgm.s.aHandyPages), ("%d\n", iHandyPage));
    Assert(pVM->pgm.s.aHandyPages[iHandyPage].HCPhysGCPhys != NIL_GMMPAGEDESC_PHYS);
    Assert(!(pVM->pgm.s.aHandyPages[iHandyPage].HCPhysGCPhys & ~X86_PTE_PAE_PG_MASK));
    Assert(pVM->pgm.s.aHandyPages[iHandyPage].idPage != NIL_GMM_PAGEID);
    Assert(pVM->pgm.s.aHandyPages[iHandyPage].idSharedPage == NIL_GMM_PAGEID);

    /*
     * There are one or two action to be taken the next time we allocate handy pages:
     *      - Tell the GMM (global memory manager) what the page is being used for.
     *        (Speeds up replacement operations - sharing and defragmenting.)
     *      - If the current backing is shared, it must be freed.
     */
    const RTHCPHYS HCPhys = pVM->pgm.s.aHandyPages[iHandyPage].HCPhysGCPhys;
    pVM->pgm.s.aHandyPages[iHandyPage].HCPhysGCPhys = GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;

    void const *pvSharedPage = NULL;
    if (!PGM_PAGE_IS_SHARED(pPage))
    {
        Log2(("PGM: Replaced zero page %RGp with %#x / %RHp\n", GCPhys, pVM->pgm.s.aHandyPages[iHandyPage].idPage, HCPhys));
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.StatRZPageReplaceZero);
        pVM->pgm.s.cZeroPages--;
    }
    else
    {
        /* Mark this shared page for freeing/dereferencing. */
        pVM->pgm.s.aHandyPages[iHandyPage].idSharedPage = PGM_PAGE_GET_PAGEID(pPage);
        Assert(PGM_PAGE_GET_PAGEID(pPage) != NIL_GMM_PAGEID);

        Log(("PGM: Replaced shared page %#x at %RGp with %#x / %RHp\n", PGM_PAGE_GET_PAGEID(pPage),
             GCPhys, pVM->pgm.s.aHandyPages[iHandyPage].idPage, HCPhys));
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PageReplaceShared));
        pVM->pgm.s.cSharedPages--;

        /* Grab the address of the page so we can make a copy later on. (safe) */
        rc = pgmPhysPageMapReadOnly(pVM, pPage, GCPhys, &pvSharedPage);
        AssertRC(rc);
    }

    /*
     * Do the PGMPAGE modifications.
     */
    pVM->pgm.s.cPrivatePages++;
    PGM_PAGE_SET_HCPHYS(pVM, pPage, HCPhys);
    PGM_PAGE_SET_PAGEID(pVM, pPage, pVM->pgm.s.aHandyPages[iHandyPage].idPage);
    PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ALLOCATED);
    PGM_PAGE_SET_PDE_TYPE(pVM, pPage, PGM_PAGE_PDE_TYPE_PT);
    pgmPhysInvalidatePageMapTLBEntry(pVM, GCPhys);
    IEMTlbInvalidateAllPhysicalAllCpus(pVM, NIL_VMCPUID,
                                       !pvSharedPage
                                       ? IEMTLBPHYSFLUSHREASON_ALLOCATED : IEMTLBPHYSFLUSHREASON_ALLOCATED_FROM_SHARED);

    /* Copy the shared page contents to the replacement page. */
    if (!pvSharedPage)
    { /* likely */ }
    else
    {
        /* Get the virtual address of the new page. */
        PGMPAGEMAPLOCK  PgMpLck;
        void           *pvNewPage;
        rc = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, GCPhys, &pvNewPage, &PgMpLck); AssertRC(rc);
        if (RT_SUCCESS(rc))
        {
            memcpy(pvNewPage, pvSharedPage, GUEST_PAGE_SIZE); /** @todo todo write ASMMemCopyPage */
            pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
        }
    }

    if (    fFlushTLBs
        &&  rc != VINF_PGM_GCPHYS_ALIASED)
        PGM_INVL_ALL_VCPU_TLBS(pVM);

    /*
     * Notify NEM about the mapping change for this page.
     *
     * Note! Shadow ROM pages are complicated as they can definitely be
     *       allocated while not visible, so play safe.
     */
    if (VM_IS_NEM_ENABLED(pVM))
    {
        PGMPAGETYPE enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
        if (   enmType != PGMPAGETYPE_ROM_SHADOW
            || pgmPhysGetPage(pVM, GCPhys) == pPage)
        {
            uint8_t u2State = PGM_PAGE_GET_NEM_STATE(pPage);
            rc2 = NEMHCNotifyPhysPageAllocated(pVM, GCPhys & ~(RTGCPHYS)X86_PAGE_OFFSET_MASK, HCPhys,
                                               pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
            if (RT_SUCCESS(rc))
                PGM_PAGE_SET_NEM_STATE(pPage, u2State);
            else
                rc = rc2;
        }
    }

    return rc;
}

#ifdef PGM_WITH_LARGE_PAGES

/**
 * Replace a 2 MB range of zero pages with new pages that we can write to.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success, pPage is modified.
 * @retval  VINF_PGM_SYNC_CR3 on success and a page pool flush is pending.
 * @retval  VERR_EM_NO_MEMORY if we're totally out of memory.
 *
 * @todo    Propagate VERR_EM_NO_MEMORY up the call tree.
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The address of the page.
 *
 * @remarks Must be called from within the PGM critical section.  It may block
 *          on GMM and host mutexes/locks, leaving HM context.
 */
int pgmPhysAllocLargePage(PVMCC pVM, RTGCPHYS GCPhys)
{
    RTGCPHYS GCPhysBase = GCPhys & X86_PDE2M_PAE_PG_MASK;
    LogFlow(("pgmPhysAllocLargePage: %RGp base %RGp\n", GCPhys, GCPhysBase));
    Assert(!VM_IS_NEM_ENABLED(pVM)); /** @todo NEM: Large page support. */

    /*
     * Check Prereqs.
     */
    PGM_LOCK_ASSERT_OWNER(pVM);
    Assert(PGMIsUsingLargePages(pVM));

    /*
     * All the pages must be unallocated RAM pages, i.e. mapping the ZERO page.
     */
    PPGMPAGE pFirstPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhysBase, &pFirstPage);
    if (   RT_SUCCESS(rc)
        && PGM_PAGE_GET_TYPE(pFirstPage)  == PGMPAGETYPE_RAM
        && PGM_PAGE_GET_STATE(pFirstPage) == PGM_PAGE_STATE_ZERO)
    {
        /*
         * Further they should have PDE type set to PGM_PAGE_PDE_TYPE_DONTCARE,
         * since they are unallocated.
         */
        unsigned uPDEType = PGM_PAGE_GET_PDE_TYPE(pFirstPage);
        Assert(uPDEType != PGM_PAGE_PDE_TYPE_PDE);
        if (uPDEType == PGM_PAGE_PDE_TYPE_DONTCARE)
        {
            /*
             * Now, make sure all the other pages in the 2 MB is in the same state.
             */
            GCPhys = GCPhysBase;
            unsigned cLeft = _2M / GUEST_PAGE_SIZE;
            while (cLeft-- > 0)
            {
                PPGMPAGE pSubPage = pgmPhysGetPage(pVM, GCPhys);
                if (   pSubPage
                    && PGM_PAGE_GET_TYPE(pSubPage)  == PGMPAGETYPE_RAM      /* Anything other than ram implies monitoring. */
                    && PGM_PAGE_GET_STATE(pSubPage) == PGM_PAGE_STATE_ZERO) /* Allocated, monitored or shared means we can't use a large page here */
                {
                    Assert(PGM_PAGE_GET_PDE_TYPE(pSubPage) == PGM_PAGE_PDE_TYPE_DONTCARE);
                    GCPhys += GUEST_PAGE_SIZE;
                }
                else
                {
                    LogFlow(("pgmPhysAllocLargePage: Found page %RGp with wrong attributes (type=%d; state=%d); cancel check.\n",
                             GCPhys, pSubPage ? PGM_PAGE_GET_TYPE(pSubPage) : -1, pSubPage ? PGM_PAGE_GET_STATE(pSubPage) : -1));

                    /* Failed. Mark as requiring a PT so we don't check the whole thing again in the future. */
                    STAM_REL_COUNTER_INC(&pVM->pgm.s.StatLargePageRefused);
                    PGM_PAGE_SET_PDE_TYPE(pVM, pFirstPage, PGM_PAGE_PDE_TYPE_PT);
                    return VERR_PGM_INVALID_LARGE_PAGE_RANGE;
                }
            }

            /*
             * Do the allocation.
             */
# ifdef IN_RING3
            rc = VMMR3CallR0(pVM, VMMR0_DO_PGM_ALLOCATE_LARGE_PAGE, GCPhysBase, NULL);
# elif defined(IN_RING0)
            rc = pgmR0PhysAllocateLargePage(pVM, VMMGetCpuId(pVM), GCPhysBase);
# else
#  error "Port me"
# endif
            if (RT_SUCCESS(rc))
            {
                Assert(PGM_PAGE_GET_STATE(pFirstPage) == PGM_PAGE_STATE_ALLOCATED);
                pVM->pgm.s.cLargePages++;
                return VINF_SUCCESS;
            }

            /* If we fail once, it most likely means the host's memory is too
               fragmented; don't bother trying again. */
            LogFlow(("pgmPhysAllocLargePage failed with %Rrc\n", rc));
            return rc;
        }
    }
    return VERR_PGM_INVALID_LARGE_PAGE_RANGE;
}


/**
 * Recheck the entire 2 MB range to see if we can use it again as a large page.
 *
 * @returns The following VBox status codes.
 * @retval  VINF_SUCCESS on success, the large page can be used again
 * @retval  VERR_PGM_INVALID_LARGE_PAGE_RANGE if it can't be reused
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The address of the page.
 * @param   pLargePage  Page structure of the base page
 */
int pgmPhysRecheckLargePage(PVMCC pVM, RTGCPHYS GCPhys, PPGMPAGE pLargePage)
{
    STAM_REL_COUNTER_INC(&pVM->pgm.s.StatLargePageRecheck);

    Assert(!VM_IS_NEM_ENABLED(pVM)); /** @todo NEM: Large page support. */

    AssertCompile(X86_PDE2M_PAE_PG_MASK == EPT_PDE2M_PG_MASK);  /* Paranoia: Caller uses this for guest EPT tables as well. */
    GCPhys &= X86_PDE2M_PAE_PG_MASK;

    /* Check the base page. */
    Assert(PGM_PAGE_GET_PDE_TYPE(pLargePage) == PGM_PAGE_PDE_TYPE_PDE_DISABLED);
    if (    PGM_PAGE_GET_STATE(pLargePage) != PGM_PAGE_STATE_ALLOCATED
        ||  PGM_PAGE_GET_TYPE(pLargePage) != PGMPAGETYPE_RAM
        ||  PGM_PAGE_GET_HNDL_PHYS_STATE(pLargePage) != PGM_PAGE_HNDL_PHYS_STATE_NONE)
    {
        LogFlow(("pgmPhysRecheckLargePage: checks failed for base page %x %x %x\n", PGM_PAGE_GET_STATE(pLargePage), PGM_PAGE_GET_TYPE(pLargePage), PGM_PAGE_GET_HNDL_PHYS_STATE(pLargePage)));
        return VERR_PGM_INVALID_LARGE_PAGE_RANGE;
    }

    STAM_PROFILE_START(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,IsValidLargePage), a);
    /* Check all remaining pages in the 2 MB range. */
    unsigned i;
    GCPhys += GUEST_PAGE_SIZE;
    for (i = 1; i < _2M / GUEST_PAGE_SIZE; i++)
    {
        PPGMPAGE pPage;
        int rc = pgmPhysGetPageEx(pVM, GCPhys, &pPage);
        AssertRCBreak(rc);

        if (    PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED
            ||  PGM_PAGE_GET_PDE_TYPE(pPage) != PGM_PAGE_PDE_TYPE_PDE
            ||  PGM_PAGE_GET_TYPE(pPage) != PGMPAGETYPE_RAM
            ||  PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) != PGM_PAGE_HNDL_PHYS_STATE_NONE)
        {
            LogFlow(("pgmPhysRecheckLargePage: checks failed for page %d; %x %x %x\n", i, PGM_PAGE_GET_STATE(pPage), PGM_PAGE_GET_TYPE(pPage), PGM_PAGE_GET_HNDL_PHYS_STATE(pPage)));
            break;
        }

        GCPhys += GUEST_PAGE_SIZE;
    }
    STAM_PROFILE_STOP(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,IsValidLargePage), a);

    if (i == _2M / GUEST_PAGE_SIZE)
    {
        PGM_PAGE_SET_PDE_TYPE(pVM, pLargePage, PGM_PAGE_PDE_TYPE_PDE);
        pVM->pgm.s.cLargePagesDisabled--;
        Log(("pgmPhysRecheckLargePage: page %RGp can be reused!\n", GCPhys - _2M));
        return VINF_SUCCESS;
    }

    return VERR_PGM_INVALID_LARGE_PAGE_RANGE;
}

#endif /* PGM_WITH_LARGE_PAGES */


/**
 * Deal with a write monitored page.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The guest physical address of the page.
 *                      PGMPhysReleasePageMappingLock() passes NIL_RTGCPHYS in a
 *                      very unlikely situation where it is okay that we let NEM
 *                      fix the page access in a lazy fasion.
 *
 * @remarks Called from within the PGM critical section.
 */
void pgmPhysPageMakeWriteMonitoredWritable(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    Assert(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_WRITE_MONITORED);
    PGM_PAGE_SET_WRITTEN_TO(pVM, pPage);
    PGM_PAGE_SET_STATE(pVM, pPage, PGM_PAGE_STATE_ALLOCATED);
    if (PGM_PAGE_IS_CODE_PAGE(pPage))
    {
        PGM_PAGE_CLEAR_CODE_PAGE(pVM, pPage);
        IEMTlbInvalidateAllPhysicalAllCpus(pVM, NIL_VMCPUID, IEMTLBPHYSFLUSHREASON_MADE_WRITABLE);
    }

    Assert(pVM->pgm.s.cMonitoredPages > 0);
    pVM->pgm.s.cMonitoredPages--;
    pVM->pgm.s.cWrittenToPages++;

#ifdef VBOX_WITH_NATIVE_NEM
    /*
     * Notify NEM about the protection change so we won't spin forever.
     *
     * Note! NEM need to be handle to lazily correct page protection as we cannot
     *       really get it 100% right here it seems.  The page pool does this too.
     */
    if (VM_IS_NEM_ENABLED(pVM) && GCPhys != NIL_RTGCPHYS)
    {
        uint8_t      u2State = PGM_PAGE_GET_NEM_STATE(pPage);
        PGMPAGETYPE  enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
        PPGMRAMRANGE pRam    = pgmPhysGetRange(pVM, GCPhys);
        NEMHCNotifyPhysPageProtChanged(pVM, GCPhys, PGM_PAGE_GET_HCPHYS(pPage),
                                       pRam ? PGM_RAMRANGE_CALC_PAGE_R3PTR(pRam, GCPhys) : NULL,
                                       pgmPhysPageCalcNemProtection(pPage, enmType), enmType, &u2State);
        PGM_PAGE_SET_NEM_STATE(pPage, u2State);
    }
#else
    RT_NOREF(GCPhys);
#endif
}


/**
 * Deal with pages that are not writable, i.e. not in the ALLOCATED state.
 *
 * @returns VBox strict status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_PGM_SYNC_CR3 on success and a page pool flush is pending.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The address of the page.
 *
 * @remarks Called from within the PGM critical section.
 */
int pgmPhysPageMakeWritable(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    PGM_LOCK_ASSERT_OWNER(pVM);
    switch (PGM_PAGE_GET_STATE(pPage))
    {
        case PGM_PAGE_STATE_WRITE_MONITORED:
            pgmPhysPageMakeWriteMonitoredWritable(pVM, pPage, GCPhys);
            RT_FALL_THRU();
        default: /* to shut up GCC */
        case PGM_PAGE_STATE_ALLOCATED:
            return VINF_SUCCESS;

        /*
         * Zero pages can be dummy pages for MMIO or reserved memory,
         * so we need to check the flags before joining cause with
         * shared page replacement.
         */
        case PGM_PAGE_STATE_ZERO:
            if (PGM_PAGE_IS_MMIO(pPage))
                return VERR_PGM_PHYS_PAGE_RESERVED;
            RT_FALL_THRU();
        case PGM_PAGE_STATE_SHARED:
            return pgmPhysAllocPage(pVM, pPage, GCPhys);

        /* Not allowed to write to ballooned pages. */
        case PGM_PAGE_STATE_BALLOONED:
            return VERR_PGM_PHYS_PAGE_BALLOONED;
    }
}


/**
 * Internal usage: Map the page specified by its GMM ID.
 *
 * This is similar to pgmPhysPageMap
 *
 * @returns VBox status code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   idPage      The Page ID.
 * @param   HCPhys      The physical address (for SUPR0HCPhysToVirt).
 * @param   ppv         Where to store the mapping address.
 *
 * @remarks Called from within the PGM critical section.  The mapping is only
 *          valid while you are inside this section.
 */
int pgmPhysPageMapByPageID(PVMCC pVM, uint32_t idPage, RTHCPHYS HCPhys, void **ppv)
{
    /*
     * Validation.
     */
    PGM_LOCK_ASSERT_OWNER(pVM);
    AssertReturn(HCPhys && !(HCPhys & GUEST_PAGE_OFFSET_MASK), VERR_INVALID_PARAMETER);
    const uint32_t idChunk = idPage >> GMM_CHUNKID_SHIFT;
    AssertReturn(idChunk != NIL_GMM_CHUNKID, VERR_INVALID_PARAMETER);

#ifdef IN_RING0
# ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
    return SUPR0HCPhysToVirt(HCPhys & ~(RTHCPHYS)GUEST_PAGE_OFFSET_MASK, ppv);
# else
    return GMMR0PageIdToVirt(pVM, idPage, ppv);
# endif

#else
    /*
     * Find/make Chunk TLB entry for the mapping chunk.
     */
    PPGMCHUNKR3MAP pMap;
    PPGMCHUNKR3MAPTLBE pTlbe = &pVM->pgm.s.ChunkR3Map.Tlb.aEntries[PGM_CHUNKR3MAPTLB_IDX(idChunk)];
    if (pTlbe->idChunk == idChunk)
    {
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,ChunkR3MapTlbHits));
        pMap = pTlbe->pChunk;
    }
    else
    {
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,ChunkR3MapTlbMisses));

        /*
         * Find the chunk, map it if necessary.
         */
        pMap = (PPGMCHUNKR3MAP)RTAvlU32Get(&pVM->pgm.s.ChunkR3Map.pTree, idChunk);
        if (pMap)
            pMap->iLastUsed = pVM->pgm.s.ChunkR3Map.iNow;
        else
        {
            int rc = pgmR3PhysChunkMap(pVM, idChunk, &pMap);
            if (RT_FAILURE(rc))
                return rc;
        }

        /*
         * Enter it into the Chunk TLB.
         */
        pTlbe->idChunk = idChunk;
        pTlbe->pChunk = pMap;
    }

    *ppv = (uint8_t *)pMap->pv + ((idPage & GMM_PAGEID_IDX_MASK) << GUEST_PAGE_SHIFT);
    return VINF_SUCCESS;
#endif
}


/**
 * Maps a page into the current virtual address space so it can be accessed.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The address of the page.
 * @param   ppMap       Where to store the address of the mapping tracking structure.
 * @param   ppv         Where to store the mapping address of the page. The page
 *                      offset is masked off!
 *
 * @remarks Called from within the PGM critical section.
 */
static int pgmPhysPageMapCommon(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, PPPGMPAGEMAP ppMap, void **ppv)
{
    PGM_LOCK_ASSERT_OWNER(pVM);
    NOREF(GCPhys);

    /*
     * Special cases: MMIO2 and specially aliased MMIO pages.
     */
    if (   PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2
        || PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_MMIO2_ALIAS_MMIO)
    {
        *ppMap = NULL;

        /* Decode the page id to a page in a MMIO2 ram range. */
        uint8_t const  idMmio2 = PGM_MMIO2_PAGEID_GET_MMIO2_ID(PGM_PAGE_GET_PAGEID(pPage));
        uint32_t const iPage   = PGM_MMIO2_PAGEID_GET_IDX(PGM_PAGE_GET_PAGEID(pPage));
        AssertLogRelMsgReturn((uint8_t)(idMmio2 - 1U) < RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges),
                              ("idMmio2=%u size=%u type=%u GCPHys=%#RGp Id=%u State=%u", idMmio2,
                               RT_ELEMENTS(pVM->pgm.s.aMmio2Ranges), PGM_PAGE_GET_TYPE(pPage), GCPhys,
                               pPage->s.idPage, pPage->s.uStateY),
                              VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE);
        PPGMREGMMIO2RANGE const pMmio2Range = &pVM->pgm.s.aMmio2Ranges[idMmio2 - 1];
        AssertLogRelReturn(pMmio2Range, VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE);
        AssertLogRelReturn(pMmio2Range->idMmio2 == idMmio2, VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE);
#ifndef IN_RING0
        uint32_t const          idRamRange  = pMmio2Range->idRamRange;
        AssertLogRelReturn(idRamRange < RT_ELEMENTS(pVM->pgm.s.apRamRanges), VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE);
        PPGMRAMRANGE const      pRamRange   = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
        AssertLogRelReturn(pRamRange, VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE);
        AssertLogRelReturn(iPage < (pRamRange->cb >> GUEST_PAGE_SHIFT), VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE);
        *ppv = pMmio2Range->pbR3 + ((uintptr_t)iPage << GUEST_PAGE_SHIFT);
        return VINF_SUCCESS;

#else /* IN_RING0 */
        AssertLogRelReturn(iPage < pVM->pgmr0.s.acMmio2RangePages[idMmio2 - 1], VERR_PGM_PHYS_PAGE_MAP_MMIO2_IPE);
# ifdef VBOX_WITH_LINEAR_HOST_PHYS_MEM
        return SUPR0HCPhysToVirt(PGM_PAGE_GET_HCPHYS(pPage), ppv);
# else
        AssertPtr(pVM->pgmr0.s.apbMmio2Backing[idMmio2 - 1]);
        *ppv = pVM->pgmr0.s.apbMmio2Backing[idMmio2 - 1] + ((uintptr_t)iPage << GUEST_PAGE_SHIFT);
        return VINF_SUCCESS;
# endif
#endif
    }

#ifdef VBOX_WITH_PGM_NEM_MODE
    if (pVM->pgm.s.fNemMode)
    {
# ifdef IN_RING3
        /*
         * Find the corresponding RAM range and use that to locate the mapping address.
         */
        /** @todo Use the page ID for some kind of indexing as we do with MMIO2 above. */
        PPGMRAMRANGE const pRam = pgmPhysGetRange(pVM, GCPhys);
        AssertLogRelMsgReturn(pRam, ("%RTGp\n", GCPhys), VERR_INTERNAL_ERROR_3);
        size_t const idxPage = (GCPhys - pRam->GCPhys) >> GUEST_PAGE_SHIFT;
        Assert(pPage == &pRam->aPages[idxPage]);
        *ppMap = NULL;
        *ppv   = (uint8_t *)pRam->pbR3 + (idxPage << GUEST_PAGE_SHIFT);
        return VINF_SUCCESS;
# else
        AssertFailedReturn(VERR_INTERNAL_ERROR_2);
# endif
    }
#endif /* VBOX_WITH_PGM_NEM_MODE */

    const uint32_t idChunk = PGM_PAGE_GET_CHUNKID(pPage);
    if (idChunk == NIL_GMM_CHUNKID)
    {
        AssertMsgReturn(PGM_PAGE_GET_PAGEID(pPage) == NIL_GMM_PAGEID, ("pPage=%R[pgmpage]\n", pPage),
                        VERR_PGM_PHYS_PAGE_MAP_IPE_1);
        if (!PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(pPage))
        {
            AssertMsgReturn(PGM_PAGE_IS_ZERO(pPage), ("pPage=%R[pgmpage]\n", pPage),
                            VERR_PGM_PHYS_PAGE_MAP_IPE_3);
            AssertMsgReturn(PGM_PAGE_GET_HCPHYS(pPage)== pVM->pgm.s.HCPhysZeroPg, ("pPage=%R[pgmpage]\n", pPage),
                            VERR_PGM_PHYS_PAGE_MAP_IPE_4);
            *ppv = pVM->pgm.s.abZeroPg;
        }
        else
            *ppv = pVM->pgm.s.abZeroPg;
        *ppMap = NULL;
        return VINF_SUCCESS;
    }

# if defined(IN_RING0) && defined(VBOX_WITH_LINEAR_HOST_PHYS_MEM)
    /*
     * Just use the physical address.
     */
    *ppMap = NULL;
    return SUPR0HCPhysToVirt(PGM_PAGE_GET_HCPHYS(pPage), ppv);

# elif defined(IN_RING0)
    /*
     * Go by page ID thru GMMR0.
     */
    *ppMap = NULL;
    return GMMR0PageIdToVirt(pVM, PGM_PAGE_GET_PAGEID(pPage), ppv);

# else
    /*
     * Find/make Chunk TLB entry for the mapping chunk.
     */
    PPGMCHUNKR3MAP pMap;
    PPGMCHUNKR3MAPTLBE pTlbe = &pVM->pgm.s.ChunkR3Map.Tlb.aEntries[PGM_CHUNKR3MAPTLB_IDX(idChunk)];
    if (pTlbe->idChunk == idChunk)
    {
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,ChunkR3MapTlbHits));
        pMap = pTlbe->pChunk;
        AssertPtr(pMap->pv);
    }
    else
    {
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,ChunkR3MapTlbMisses));

        /*
         * Find the chunk, map it if necessary.
         */
        pMap = (PPGMCHUNKR3MAP)RTAvlU32Get(&pVM->pgm.s.ChunkR3Map.pTree, idChunk);
        if (pMap)
        {
            AssertPtr(pMap->pv);
            pMap->iLastUsed = pVM->pgm.s.ChunkR3Map.iNow;
        }
        else
        {
            int rc = pgmR3PhysChunkMap(pVM, idChunk, &pMap);
            if (RT_FAILURE(rc))
                return rc;
            AssertPtr(pMap->pv);
        }

        /*
         * Enter it into the Chunk TLB.
         */
        pTlbe->idChunk = idChunk;
        pTlbe->pChunk = pMap;
    }

    *ppv = (uint8_t *)pMap->pv + (PGM_PAGE_GET_PAGE_IN_CHUNK(pPage) << GUEST_PAGE_SHIFT);
    *ppMap = pMap;
    return VINF_SUCCESS;
# endif /* !IN_RING0 */
}


/**
 * Combination of pgmPhysPageMakeWritable and pgmPhysPageMapWritable.
 *
 * This is typically used is paths where we cannot use the TLB methods (like ROM
 * pages) or where there is no point in using them since we won't get many hits.
 *
 * @returns VBox strict status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VINF_PGM_SYNC_CR3 on success and a page pool flush is pending.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The address of the page.
 * @param   ppv         Where to store the mapping address of the page. The page
 *                      offset is masked off!
 *
 * @remarks Called from within the PGM critical section.  The mapping is only
 *          valid while you are inside section.
 */
int pgmPhysPageMakeWritableAndMap(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv)
{
    int rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
    if (RT_SUCCESS(rc))
    {
        AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 /* returned */, ("%Rrc\n", rc));
        PPGMPAGEMAP pMapIgnore;
        int rc2 = pgmPhysPageMapCommon(pVM, pPage, GCPhys, &pMapIgnore, ppv);
        if (RT_FAILURE(rc2)) /* preserve rc */
            rc = rc2;
    }
    return rc;
}


/**
 * Maps a page into the current virtual address space so it can be accessed for
 * both writing and reading.
 *
 * This is typically used is paths where we cannot use the TLB methods (like ROM
 * pages) or where there is no point in using them since we won't get many hits.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The physical page tracking structure. Must be in the
 *                      allocated state.
 * @param   GCPhys      The address of the page.
 * @param   ppv         Where to store the mapping address of the page. The page
 *                      offset is masked off!
 *
 * @remarks Called from within the PGM critical section.  The mapping is only
 *          valid while you are inside section.
 */
int pgmPhysPageMap(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv)
{
    Assert(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED);
    PPGMPAGEMAP pMapIgnore;
    return pgmPhysPageMapCommon(pVM, pPage, GCPhys, &pMapIgnore, ppv);
}


/**
 * Maps a page into the current virtual address space so it can be accessed for
 * reading.
 *
 * This is typically used is paths where we cannot use the TLB methods (like ROM
 * pages) or where there is no point in using them since we won't get many hits.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The physical page tracking structure.
 * @param   GCPhys      The address of the page.
 * @param   ppv         Where to store the mapping address of the page. The page
 *                      offset is masked off!
 *
 * @remarks Called from within the PGM critical section.  The mapping is only
 *          valid while you are inside this section.
 */
int pgmPhysPageMapReadOnly(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void const **ppv)
{
    PPGMPAGEMAP pMapIgnore;
    return pgmPhysPageMapCommon(pVM, pPage, GCPhys, &pMapIgnore, (void **)ppv);
}


/**
 * Load a guest page into the ring-3 physical TLB.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The guest physical address in question.
 */
int pgmPhysPageLoadIntoTlb(PVMCC pVM, RTGCPHYS GCPhys)
{
    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Find the ram range and page and hand it over to the with-page function.
     * 99.8% of requests are expected to be in the first range.
     */
    PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhys);
    if (!pPage)
    {
        STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PageMapTlbMisses));
        return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
    }

    return pgmPhysPageLoadIntoTlbWithPage(pVM, pPage, GCPhys);
}


/**
 * Load a guest page into the ring-3 physical TLB.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       Pointer to the PGMPAGE structure corresponding to
 *                      GCPhys.
 * @param   GCPhys      The guest physical address in question.
 */
int pgmPhysPageLoadIntoTlbWithPage(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    PGM_LOCK_ASSERT_OWNER(pVM);
    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PageMapTlbMisses));

    /*
     * Map the page.
     * Make a special case for the zero page as it is kind of special.
     */
    PPGMPAGEMAPTLBE pTlbe = &pVM->pgm.s.CTX_SUFF(PhysTlb).aEntries[PGM_PAGEMAPTLB_IDX(GCPhys)];
    if (    !PGM_PAGE_IS_ZERO(pPage)
        &&  !PGM_PAGE_IS_BALLOONED(pPage))
    {
        void *pv;
        PPGMPAGEMAP pMap;
        int rc = pgmPhysPageMapCommon(pVM, pPage, GCPhys, &pMap, &pv);
        if (RT_FAILURE(rc))
            return rc;
# ifndef IN_RING0
        pTlbe->pMap = pMap;
# endif
        pTlbe->pv = pv;
        Assert(!((uintptr_t)pTlbe->pv & GUEST_PAGE_OFFSET_MASK));
    }
    else
    {
        AssertMsg(PGM_PAGE_GET_HCPHYS(pPage) == pVM->pgm.s.HCPhysZeroPg, ("%RGp/%R[pgmpage]\n", GCPhys, pPage));
# ifndef IN_RING0
        pTlbe->pMap = NULL;
# endif
        pTlbe->pv = pVM->pgm.s.abZeroPg;
    }
# ifdef PGM_WITH_PHYS_TLB
    if (    PGM_PAGE_GET_TYPE(pPage) < PGMPAGETYPE_ROM_SHADOW
        ||  PGM_PAGE_GET_TYPE(pPage) > PGMPAGETYPE_ROM)
        pTlbe->GCPhys = GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    else
        pTlbe->GCPhys = NIL_RTGCPHYS; /* ROM: Problematic because of the two pages. :-/ */
# else
    pTlbe->GCPhys = NIL_RTGCPHYS;
# endif
    pTlbe->pPage = pPage;
    return VINF_SUCCESS;
}


#ifdef IN_RING3 /** @todo Need ensure a ring-0 version gets invalidated safely */
/**
 * Load a guest page into the lockless ring-3 physical TLB for the calling EMT.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pPage       Pointer to the PGMPAGE structure corresponding to
 *                      GCPhys.
 * @param   GCPhys      The guest physical address in question.
 */
DECLHIDDEN(int) pgmPhysPageLoadIntoLocklessTlbWithPage(PVMCPUCC pVCpu, PPGMPAGE pPage, RTGCPHYS GCPhys)
{
    STAM_REL_COUNTER_INC(&pVCpu->pgm.s.CTX_MID_Z(Stat,PageMapTlbMisses));
    PPGMPAGEMAPTLBE const pLocklessTlbe = &pVCpu->pgm.s.PhysTlb.aEntries[PGM_PAGEMAPTLB_IDX(GCPhys)];
    PVMCC const           pVM           = pVCpu->CTX_SUFF(pVM);

    PGM_LOCK_VOID(pVM);

    PPGMPAGEMAPTLBE pSharedTlbe;
    int rc = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pSharedTlbe);
    if (RT_SUCCESS(rc))
        *pLocklessTlbe = *pSharedTlbe;

    PGM_UNLOCK(pVM);
    return rc;
}
#endif /* IN_RING3 */


/**
 * Internal version of PGMPhysGCPhys2CCPtr that expects the caller to
 * own the PGM lock and therefore not need to lock the mapped page.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The guest physical address of the page that should be mapped.
 * @param   pPage       Pointer to the PGMPAGE structure for the page.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 *
 * @internal
 * @deprecated Use pgmPhysGCPhys2CCPtrInternalEx.
 */
int pgmPhysGCPhys2CCPtrInternalDepr(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv)
{
    int rc;
    AssertReturn(pPage, VERR_PGM_PHYS_NULL_PAGE_PARAM);
    PGM_LOCK_ASSERT_OWNER(pVM);
    pVM->pgm.s.cDeprecatedPageLocks++;

    /*
     * Make sure the page is writable.
     */
    if (RT_UNLIKELY(PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED))
    {
        rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 /* not returned */, ("%Rrc\n", rc));
    }
    Assert(PGM_PAGE_GET_HCPHYS(pPage) != 0);

    /*
     * Get the mapping address.
     */
    PPGMPAGEMAPTLBE pTlbe;
    rc = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pTlbe);
    if (RT_FAILURE(rc))
        return rc;
    *ppv = (void *)((uintptr_t)pTlbe->pv | (uintptr_t)(GCPhys & GUEST_PAGE_OFFSET_MASK));
    return VINF_SUCCESS;
}


/**
 * Locks a page mapping for writing.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pPage               The page.
 * @param   pTlbe               The mapping TLB entry for the page.
 * @param   pLock               The lock structure (output).
 */
DECLINLINE(void) pgmPhysPageMapLockForWriting(PVM pVM, PPGMPAGE pPage, PPGMPAGEMAPTLBE pTlbe, PPGMPAGEMAPLOCK pLock)
{
# ifndef IN_RING0
    PPGMPAGEMAP pMap = pTlbe->pMap;
    if (pMap)
        pMap->cRefs++;
# else
    RT_NOREF(pTlbe);
# endif

    unsigned cLocks = PGM_PAGE_GET_WRITE_LOCKS(pPage);
    if (RT_LIKELY(cLocks < PGM_PAGE_MAX_LOCKS - 1))
    {
        if (cLocks == 0)
            pVM->pgm.s.cWriteLockedPages++;
        PGM_PAGE_INC_WRITE_LOCKS(pPage);
    }
    else if (cLocks != PGM_PAGE_MAX_LOCKS)
    {
        PGM_PAGE_INC_WRITE_LOCKS(pPage);
        AssertMsgFailed(("%R[pgmpage] is entering permanent write locked state!\n", pPage));
# ifndef IN_RING0
        if (pMap)
            pMap->cRefs++; /* Extra ref to prevent it from going away. */
# endif
    }

    pLock->uPageAndType = (uintptr_t)pPage | PGMPAGEMAPLOCK_TYPE_WRITE;
# ifndef IN_RING0
    pLock->pvMap = pMap;
# else
    pLock->pvMap = NULL;
# endif
}

/**
 * Locks a page mapping for reading.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pPage               The page.
 * @param   pTlbe               The mapping TLB entry for the page.
 * @param   pLock               The lock structure (output).
 */
DECLINLINE(void) pgmPhysPageMapLockForReading(PVM pVM, PPGMPAGE pPage, PPGMPAGEMAPTLBE pTlbe, PPGMPAGEMAPLOCK pLock)
{
# ifndef IN_RING0
    PPGMPAGEMAP pMap = pTlbe->pMap;
    if (pMap)
        pMap->cRefs++;
# else
    RT_NOREF(pTlbe);
# endif

    unsigned cLocks = PGM_PAGE_GET_READ_LOCKS(pPage);
    if (RT_LIKELY(cLocks < PGM_PAGE_MAX_LOCKS - 1))
    {
        if (cLocks == 0)
            pVM->pgm.s.cReadLockedPages++;
        PGM_PAGE_INC_READ_LOCKS(pPage);
    }
    else if (cLocks != PGM_PAGE_MAX_LOCKS)
    {
        PGM_PAGE_INC_READ_LOCKS(pPage);
        AssertMsgFailed(("%R[pgmpage] is entering permanent read locked state!\n", pPage));
# ifndef IN_RING0
        if (pMap)
            pMap->cRefs++; /* Extra ref to prevent it from going away. */
# endif
    }

    pLock->uPageAndType = (uintptr_t)pPage | PGMPAGEMAPLOCK_TYPE_READ;
# ifndef IN_RING0
    pLock->pvMap = pMap;
# else
    pLock->pvMap = NULL;
# endif
}


/**
 * Internal version of PGMPhysGCPhys2CCPtr that expects the caller to
 * own the PGM lock and have access to the page structure.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The guest physical address of the page that should be mapped.
 * @param   pPage       Pointer to the PGMPAGE structure for the page.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 * @param   pLock       Where to store the lock information that
 *                      pgmPhysReleaseInternalPageMappingLock needs.
 *
 * @internal
 */
int pgmPhysGCPhys2CCPtrInternal(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    int rc;
    AssertReturn(pPage, VERR_PGM_PHYS_NULL_PAGE_PARAM);
    PGM_LOCK_ASSERT_OWNER(pVM);

    /*
     * Make sure the page is writable.
     */
    if (RT_UNLIKELY(PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED))
    {
        rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 /* not returned */, ("%Rrc\n", rc));
    }
    Assert(PGM_PAGE_GET_HCPHYS(pPage) != 0);

    /*
     * Do the job.
     */
    PPGMPAGEMAPTLBE pTlbe;
    rc = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pTlbe);
    if (RT_FAILURE(rc))
        return rc;
    pgmPhysPageMapLockForWriting(pVM, pPage, pTlbe, pLock);
    *ppv = (void *)((uintptr_t)pTlbe->pv | (uintptr_t)(GCPhys & GUEST_PAGE_OFFSET_MASK));
    return VINF_SUCCESS;
}


/**
 * Internal version of PGMPhysGCPhys2CCPtrReadOnly that expects the caller to
 * own the PGM lock and have access to the page structure.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The guest physical address of the page that should be mapped.
 * @param   pPage       Pointer to the PGMPAGE structure for the page.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 * @param   pLock       Where to store the lock information that
 *                      pgmPhysReleaseInternalPageMappingLock needs.
 *
 * @internal
 */
int pgmPhysGCPhys2CCPtrInternalReadOnly(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, const void **ppv, PPGMPAGEMAPLOCK pLock)
{
    AssertReturn(pPage, VERR_PGM_PHYS_NULL_PAGE_PARAM);
    PGM_LOCK_ASSERT_OWNER(pVM);
    Assert(PGM_PAGE_GET_HCPHYS(pPage) != 0);

    /*
     * Do the job.
     */
    PPGMPAGEMAPTLBE pTlbe;
    int rc = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pTlbe);
    if (RT_FAILURE(rc))
        return rc;
    pgmPhysPageMapLockForReading(pVM, pPage, pTlbe, pLock);
    *ppv = (void *)((uintptr_t)pTlbe->pv | (uintptr_t)(GCPhys & GUEST_PAGE_OFFSET_MASK));
    return VINF_SUCCESS;
}


/**
 * Requests the mapping of a guest page into the current context.
 *
 * This API should only be used for very short term, as it will consume scarse
 * resources (R0 and GC) in the mapping cache. When you're done with the page,
 * call PGMPhysReleasePageMappingLock() ASAP to release it.
 *
 * This API will assume your intention is to write to the page, and will
 * therefore replace shared and zero pages. If you do not intend to modify
 * the page, use the PGMPhysGCPhys2CCPtrReadOnly() API.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The guest physical address of the page that should be
 *                      mapped.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 * @param   pLock       Where to store the lock information that
 *                      PGMPhysReleasePageMappingLock needs.
 *
 * @remarks The caller is responsible for dealing with access handlers.
 * @todo    Add an informational return code for pages with access handlers?
 *
 * @remark  Avoid calling this API from within critical sections (other than
 *          the PGM one) because of the deadlock risk. External threads may
 *          need to delegate jobs to the EMTs.
 * @remarks Only one page is mapped!  Make no assumption about what's after or
 *          before the returned page!
 * @thread  Any thread.
 */
VMM_INT_DECL(int) PGMPhysGCPhys2CCPtr(PVMCC pVM, RTGCPHYS GCPhys, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Query the Physical TLB entry for the page (may fail).
     */
    PPGMPAGEMAPTLBE pTlbe;
    rc = pgmPhysPageQueryTlbe(pVM, GCPhys, &pTlbe);
    if (RT_SUCCESS(rc))
    {
        /*
         * If the page is shared, the zero page, or being write monitored
         * it must be converted to a page that's writable if possible.
         */
        PPGMPAGE pPage = pTlbe->pPage;
        if (RT_UNLIKELY(PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_ALLOCATED))
        {
            rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
            if (RT_SUCCESS(rc))
            {
                AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 /* not returned */, ("%Rrc\n", rc));
                rc = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pTlbe);
            }
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Now, just perform the locking and calculate the return address.
             */
            pgmPhysPageMapLockForWriting(pVM, pPage, pTlbe, pLock);
            *ppv = (void *)((uintptr_t)pTlbe->pv | (uintptr_t)(GCPhys & GUEST_PAGE_OFFSET_MASK));
        }
    }

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Requests the mapping of a guest page into the current context.
 *
 * This API should only be used for very short term, as it will consume scarse
 * resources (R0 and GC) in the mapping cache.  When you're done with the page,
 * call PGMPhysReleasePageMappingLock() ASAP to release it.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The guest physical address of the page that should be
 *                      mapped.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 * @param   pLock       Where to store the lock information that
 *                      PGMPhysReleasePageMappingLock needs.
 *
 * @remarks The caller is responsible for dealing with access handlers.
 * @todo    Add an informational return code for pages with access handlers?
 *
 * @remarks Avoid calling this API from within critical sections (other than
 *          the PGM one) because of the deadlock risk.
 * @remarks Only one page is mapped!  Make no assumption about what's after or
 *          before the returned page!
 * @thread  Any thread.
 */
VMM_INT_DECL(int) PGMPhysGCPhys2CCPtrReadOnly(PVMCC pVM, RTGCPHYS GCPhys, void const **ppv, PPGMPAGEMAPLOCK pLock)
{
    int rc = PGM_LOCK(pVM);
    AssertRCReturn(rc, rc);

    /*
     * Query the Physical TLB entry for the page (may fail).
     */
    PPGMPAGEMAPTLBE pTlbe;
    rc = pgmPhysPageQueryTlbe(pVM, GCPhys, &pTlbe);
    if (RT_SUCCESS(rc))
    {
        /* MMIO pages doesn't have any readable backing. */
        PPGMPAGE pPage = pTlbe->pPage;
        if (RT_UNLIKELY(PGM_PAGE_IS_MMIO_OR_SPECIAL_ALIAS(pPage)))
            rc = VERR_PGM_PHYS_PAGE_RESERVED;
        else
        {
            /*
             * Now, just perform the locking and calculate the return address.
             */
            pgmPhysPageMapLockForReading(pVM, pPage, pTlbe, pLock);
            *ppv = (void *)((uintptr_t)pTlbe->pv | (uintptr_t)(GCPhys & GUEST_PAGE_OFFSET_MASK));
        }
    }

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Requests the mapping of a guest page given by virtual address into the current context.
 *
 * This API should only be used for very short term, as it will consume
 * scarse resources (R0 and GC) in the mapping cache. When you're done
 * with the page, call PGMPhysReleasePageMappingLock() ASAP to release it.
 *
 * This API will assume your intention is to write to the page, and will
 * therefore replace shared and zero pages. If you do not intend to modify
 * the page, use the PGMPhysGCPtr2CCPtrReadOnly() API.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT if the page directory for the virtual address isn't present.
 * @retval  VERR_PAGE_NOT_PRESENT if the page at the virtual address isn't present.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The guest physical address of the page that should be
 *                      mapped.
 * @param   ppv         Where to store the address corresponding to GCPhys.
 * @param   pLock       Where to store the lock information that PGMPhysReleasePageMappingLock needs.
 *
 * @remark  Avoid calling this API from within critical sections (other than
 *          the PGM one) because of the deadlock risk.
 * @thread  EMT
 */
VMM_INT_DECL(int) PGMPhysGCPtr2CCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtr, void **ppv, PPGMPAGEMAPLOCK pLock)
{
    VM_ASSERT_EMT(pVCpu->CTX_SUFF(pVM));
    RTGCPHYS GCPhys;
    int rc = PGMPhysGCPtr2GCPhys(pVCpu, GCPtr, &GCPhys);
    if (RT_SUCCESS(rc))
        rc = PGMPhysGCPhys2CCPtr(pVCpu->CTX_SUFF(pVM), GCPhys, ppv, pLock);
    return rc;
}


/**
 * Requests the mapping of a guest page given by virtual address into the current context.
 *
 * This API should only be used for very short term, as it will consume
 * scarse resources (R0 and GC) in the mapping cache. When you're done
 * with the page, call PGMPhysReleasePageMappingLock() ASAP to release it.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT if the page directory for the virtual address isn't present.
 * @retval  VERR_PAGE_NOT_PRESENT if the page at the virtual address isn't present.
 * @retval  VERR_PGM_PHYS_PAGE_RESERVED it it's a valid page but has no physical backing.
 * @retval  VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid physical address.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The guest physical address of the page that should be
 *                      mapped.
 * @param   ppv         Where to store the address corresponding to GCPtr.
 * @param   pLock       Where to store the lock information that PGMPhysReleasePageMappingLock needs.
 *
 * @remark  Avoid calling this API from within critical sections (other than
 *          the PGM one) because of the deadlock risk.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(int) PGMPhysGCPtr2CCPtrReadOnly(PVMCPUCC pVCpu, RTGCPTR GCPtr, void const **ppv, PPGMPAGEMAPLOCK pLock)
{
    VM_ASSERT_EMT(pVCpu->CTX_SUFF(pVM));
    RTGCPHYS GCPhys;
    int rc = PGMPhysGCPtr2GCPhys(pVCpu, GCPtr, &GCPhys);
    if (RT_SUCCESS(rc))
        rc = PGMPhysGCPhys2CCPtrReadOnly(pVCpu->CTX_SUFF(pVM), GCPhys, ppv, pLock);
    return rc;
}


/**
 * Release the mapping of a guest page.
 *
 * This is the counter part of PGMPhysGCPhys2CCPtr, PGMPhysGCPhys2CCPtrReadOnly
 * PGMPhysGCPtr2CCPtr and PGMPhysGCPtr2CCPtrReadOnly.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pLock       The lock structure initialized by the mapping function.
 */
VMMDECL(void) PGMPhysReleasePageMappingLock(PVMCC pVM, PPGMPAGEMAPLOCK pLock)
{
# ifndef IN_RING0
    PPGMPAGEMAP pMap       = (PPGMPAGEMAP)pLock->pvMap;
# endif
    PPGMPAGE    pPage      = (PPGMPAGE)(pLock->uPageAndType & ~PGMPAGEMAPLOCK_TYPE_MASK);
    bool        fWriteLock = (pLock->uPageAndType & PGMPAGEMAPLOCK_TYPE_MASK) == PGMPAGEMAPLOCK_TYPE_WRITE;

    pLock->uPageAndType = 0;
    pLock->pvMap = NULL;

    PGM_LOCK_VOID(pVM);
    if (fWriteLock)
    {
        unsigned cLocks = PGM_PAGE_GET_WRITE_LOCKS(pPage);
        Assert(cLocks > 0);
        if (RT_LIKELY(cLocks > 0 && cLocks < PGM_PAGE_MAX_LOCKS))
        {
            if (cLocks == 1)
            {
                Assert(pVM->pgm.s.cWriteLockedPages > 0);
                pVM->pgm.s.cWriteLockedPages--;
            }
            PGM_PAGE_DEC_WRITE_LOCKS(pPage);
        }

        if (PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_WRITE_MONITORED)
        { /* probably extremely likely */ }
        else
            pgmPhysPageMakeWriteMonitoredWritable(pVM, pPage, NIL_RTGCPHYS);
    }
    else
    {
        unsigned cLocks = PGM_PAGE_GET_READ_LOCKS(pPage);
        Assert(cLocks > 0);
        if (RT_LIKELY(cLocks > 0 && cLocks < PGM_PAGE_MAX_LOCKS))
        {
            if (cLocks == 1)
            {
                Assert(pVM->pgm.s.cReadLockedPages > 0);
                pVM->pgm.s.cReadLockedPages--;
            }
            PGM_PAGE_DEC_READ_LOCKS(pPage);
        }
    }

# ifndef IN_RING0
    if (pMap)
    {
        Assert(pMap->cRefs >= 1);
        pMap->cRefs--;
    }
# endif
    PGM_UNLOCK(pVM);
}


#ifdef IN_RING3
/**
 * Release the mapping of multiple guest pages.
 *
 * This is the counter part to PGMR3PhysBulkGCPhys2CCPtrExternal() and
 * PGMR3PhysBulkGCPhys2CCPtrReadOnlyExternal().
 *
 * @param   pVM         The cross context VM structure.
 * @param   cPages      Number of pages to unlock.
 * @param   paLocks     Array of locks lock structure initialized by the mapping
 *                      function.
 */
VMMDECL(void) PGMPhysBulkReleasePageMappingLocks(PVMCC pVM, uint32_t cPages, PPGMPAGEMAPLOCK paLocks)
{
    Assert(cPages > 0);
    bool const fWriteLock = (paLocks[0].uPageAndType & PGMPAGEMAPLOCK_TYPE_MASK) == PGMPAGEMAPLOCK_TYPE_WRITE;
#ifdef VBOX_STRICT
    for (uint32_t i = 1; i < cPages; i++)
    {
        Assert(fWriteLock == ((paLocks[i].uPageAndType & PGMPAGEMAPLOCK_TYPE_MASK) == PGMPAGEMAPLOCK_TYPE_WRITE));
        AssertPtr(paLocks[i].uPageAndType);
    }
#endif

    PGM_LOCK_VOID(pVM);
    if (fWriteLock)
    {
        /*
         * Write locks:
         */
        for (uint32_t i = 0; i < cPages; i++)
        {
            PPGMPAGE pPage  = (PPGMPAGE)(paLocks[i].uPageAndType & ~PGMPAGEMAPLOCK_TYPE_MASK);
            unsigned cLocks = PGM_PAGE_GET_WRITE_LOCKS(pPage);
            Assert(cLocks > 0);
            if (RT_LIKELY(cLocks > 0 && cLocks < PGM_PAGE_MAX_LOCKS))
            {
                if (cLocks == 1)
                {
                    Assert(pVM->pgm.s.cWriteLockedPages > 0);
                    pVM->pgm.s.cWriteLockedPages--;
                }
                PGM_PAGE_DEC_WRITE_LOCKS(pPage);
            }

            if (PGM_PAGE_GET_STATE(pPage) != PGM_PAGE_STATE_WRITE_MONITORED)
            { /* probably extremely likely */ }
            else
                pgmPhysPageMakeWriteMonitoredWritable(pVM, pPage, NIL_RTGCPHYS);

            PPGMPAGEMAP pMap = (PPGMPAGEMAP)paLocks[i].pvMap;
            if (pMap)
            {
                Assert(pMap->cRefs >= 1);
                pMap->cRefs--;
            }

            /* Yield the lock: */
            if ((i & 1023) == 1023 && i + 1 < cPages)
            {
                PGM_UNLOCK(pVM);
                PGM_LOCK_VOID(pVM);
            }
        }
    }
    else
    {
        /*
         * Read locks:
         */
        for (uint32_t i = 0; i < cPages; i++)
        {
            PPGMPAGE pPage  = (PPGMPAGE)(paLocks[i].uPageAndType & ~PGMPAGEMAPLOCK_TYPE_MASK);
            unsigned cLocks = PGM_PAGE_GET_READ_LOCKS(pPage);
            Assert(cLocks > 0);
            if (RT_LIKELY(cLocks > 0 && cLocks < PGM_PAGE_MAX_LOCKS))
            {
                if (cLocks == 1)
                {
                    Assert(pVM->pgm.s.cReadLockedPages > 0);
                    pVM->pgm.s.cReadLockedPages--;
                }
                PGM_PAGE_DEC_READ_LOCKS(pPage);
            }

            PPGMPAGEMAP pMap = (PPGMPAGEMAP)paLocks[i].pvMap;
            if (pMap)
            {
                Assert(pMap->cRefs >= 1);
                pMap->cRefs--;
            }

            /* Yield the lock: */
            if ((i & 1023) == 1023 && i + 1 < cPages)
            {
                PGM_UNLOCK(pVM);
                PGM_LOCK_VOID(pVM);
            }
        }
    }
    PGM_UNLOCK(pVM);

    RT_BZERO(paLocks, sizeof(paLocks[0]) * cPages);
}
#endif /* IN_RING3 */


/**
 * Release the internal mapping of a guest page.
 *
 * This is the counter part of pgmPhysGCPhys2CCPtrInternalEx and
 * pgmPhysGCPhys2CCPtrInternalReadOnly.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pLock       The lock structure initialized by the mapping function.
 *
 * @remarks Caller must hold the PGM lock.
 */
void pgmPhysReleaseInternalPageMappingLock(PVMCC pVM, PPGMPAGEMAPLOCK pLock)
{
    PGM_LOCK_ASSERT_OWNER(pVM);
    PGMPhysReleasePageMappingLock(pVM, pLock); /* lazy for now */
}


/**
 * Converts a GC physical address to a HC ring-3 pointer.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VERR_PGM_PHYS_PAGE_RESERVED it it's a valid GC physical
 *          page but has no physical backing.
 * @returns VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS if it's not a valid
 *          GC physical address.
 * @returns VERR_PGM_GCPHYS_RANGE_CROSSES_BOUNDARY if the range crosses
 *          a dynamic ram chunk boundary
 *
 * @param   pVM         The cross context VM structure.
 * @param   GCPhys      The GC physical address to convert.
 * @param   pR3Ptr      Where to store the R3 pointer on success.
 *
 * @deprecated  Avoid when possible!
 */
int pgmPhysGCPhys2R3Ptr(PVMCC pVM, RTGCPHYS GCPhys, PRTR3PTR pR3Ptr)
{
/** @todo this is kind of hacky and needs some more work. */
#ifndef DEBUG_sandervl
    VM_ASSERT_EMT(pVM); /* no longer safe for use outside the EMT thread! */
#endif

    Log(("pgmPhysGCPhys2R3Ptr(,%RGp,): dont use this API!\n", GCPhys)); /** @todo eliminate this API! */
    PGM_LOCK_VOID(pVM);

    PPGMRAMRANGE pRam;
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageAndRangeEx(pVM, GCPhys, &pPage, &pRam);
    if (RT_SUCCESS(rc))
        rc = pgmPhysGCPhys2CCPtrInternalDepr(pVM, pPage, GCPhys, (void **)pR3Ptr);

    PGM_UNLOCK(pVM);
    Assert(rc <= VINF_SUCCESS);
    return rc;
}


/**
 * Special lockless guest physical to current context pointer convertor.
 *
 * This is mainly for the page table walking and such.
 */
int pgmPhysGCPhys2CCPtrLockless(PVMCPUCC pVCpu, RTGCPHYS GCPhys, void **ppv)
{
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Get the RAM range and page structure.
     */
    PVMCC const           pVM = pVCpu->CTX_SUFF(pVM);
    PGMRAMRANGE volatile *pRam;
    PGMPAGE volatile     *pPage;
    int rc = pgmPhysGetPageAndRangeExLockless(pVM, pVCpu, GCPhys, &pPage, &pRam);
    if (RT_SUCCESS(rc))
    {
        /*
         * Now, make sure it's writable (typically it is).
         */
        if (RT_LIKELY(PGM_PAGE_GET_STATE(pPage) == PGM_PAGE_STATE_ALLOCATED))
        { /* likely, typically */ }
        else
        {
            PGM_LOCK_VOID(pVM);
            rc = pgmPhysPageMakeWritable(pVM, (PPGMPAGE)pPage, GCPhys);
            if (RT_SUCCESS(rc))
                rc = pgmPhysGetPageAndRangeExLockless(pVM, pVCpu, GCPhys, &pPage, &pRam);
            PGM_UNLOCK(pVM);
            if (RT_FAILURE(rc))
                return rc;
            AssertMsg(rc == VINF_SUCCESS || rc == VINF_PGM_SYNC_CR3 /* not returned */, ("%Rrc\n", rc));
        }
        Assert(PGM_PAGE_GET_HCPHYS(pPage) != 0);

        /*
         * Get the mapping address.
         */
        uint8_t *pb;
#ifdef IN_RING3
        if (PGM_IS_IN_NEM_MODE(pVM))
            pb = &pRam->pbR3[(RTGCPHYS)(uintptr_t)(pPage - &pRam->aPages[0]) << GUEST_PAGE_SHIFT];
        else
#endif
        {
#ifdef IN_RING3
            PPGMPAGEMAPTLBE pTlbe;
            rc = pgmPhysPageQueryLocklessTlbeWithPage(pVCpu, (PPGMPAGE)pPage, GCPhys, &pTlbe);
            AssertLogRelRCReturn(rc, rc);
            pb = (uint8_t *)pTlbe->pv;
            RT_NOREF(pVM);
#else /** @todo a safe lockless page TLB in ring-0 needs the to ensure it gets the right invalidations. later. */
            PGM_LOCK(pVM);
            PPGMPAGEMAPTLBE pTlbe;
            rc = pgmPhysPageQueryTlbeWithPage(pVM, (PPGMPAGE)pPage, GCPhys, &pTlbe);
            AssertLogRelRCReturnStmt(rc, PGM_UNLOCK(pVM), rc);
            pb = (uint8_t *)pTlbe->pv;
            PGM_UNLOCK(pVM);
            RT_NOREF(pVCpu);
#endif
        }
        *ppv = (void *)((uintptr_t)pb | (uintptr_t)(GCPhys & GUEST_PAGE_OFFSET_MASK));
        return VINF_SUCCESS;
    }
    Assert(rc <= VINF_SUCCESS);
    return rc;
}


/**
 * Converts a guest pointer to a GC physical address.
 *
 * This uses the current CR3/CR0/CR4 of the guest.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The guest pointer to convert.
 * @param   pGCPhys     Where to store the GC physical address.
 * @thread  EMT(pVCpu)
 */
VMMDECL(int) PGMPhysGCPtr2GCPhys(PVMCPUCC pVCpu, RTGCPTR GCPtr, PRTGCPHYS pGCPhys)
{
    VM_ASSERT_EMT(pVCpu->CTX_SUFF(pVM));
    PGMPTWALK Walk;
    int rc = PGMGstGetPage(pVCpu, (RTGCUINTPTR)GCPtr, &Walk);
    if (pGCPhys && RT_SUCCESS(rc))
        *pGCPhys = Walk.GCPhys | ((RTGCUINTPTR)GCPtr & GUEST_PAGE_OFFSET_MASK);
    return rc;
}


/**
 * Converts a guest pointer to a HC physical address.
 *
 * This uses the current CR3/CR0/CR4 of the guest.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtr       The guest pointer to convert.
 * @param   pHCPhys     Where to store the HC physical address.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(int) PGMPhysGCPtr2HCPhys(PVMCPUCC pVCpu, RTGCPTR GCPtr, PRTHCPHYS pHCPhys)
{
    VM_ASSERT_EMT(pVCpu->CTX_SUFF(pVM));
    PVMCC     pVM = pVCpu->CTX_SUFF(pVM);
    PGMPTWALK Walk;
    int rc = PGMGstGetPage(pVCpu, (RTGCUINTPTR)GCPtr, &Walk);
    if (RT_SUCCESS(rc))
        rc = PGMPhysGCPhys2HCPhys(pVM, Walk.GCPhys | ((RTGCUINTPTR)GCPtr & GUEST_PAGE_OFFSET_MASK), pHCPhys);
    return rc;
}



#undef LOG_GROUP
#define LOG_GROUP LOG_GROUP_PGM_PHYS_ACCESS


#if defined(IN_RING3) && defined(SOME_UNUSED_FUNCTION)
/**
 * Cache PGMPhys memory access
 *
 * @param   pVM             The cross context VM structure.
 * @param   pCache          Cache structure pointer
 * @param   GCPhys          GC physical address
 * @param   pbR3            HC pointer corresponding to physical page
 *
 * @thread  EMT.
 */
static void pgmPhysCacheAdd(PVM pVM, PGMPHYSCACHE *pCache, RTGCPHYS GCPhys, uint8_t *pbR3)
{
    uint32_t iCacheIndex;

    Assert(VM_IS_EMT(pVM));

    GCPhys &= ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    pbR3   = (uint8_t *)((uintptr_t)pbR3 & ~(uintptr_t)GUEST_PAGE_OFFSET_MASK);

    iCacheIndex = ((GCPhys >> GUEST_PAGE_SHIFT) & PGM_MAX_PHYSCACHE_ENTRIES_MASK);

    ASMBitSet(&pCache->aEntries, iCacheIndex);

    pCache->Entry[iCacheIndex].GCPhys = GCPhys;
    pCache->Entry[iCacheIndex].pbR3   = pbR3;
}
#endif /* IN_RING3 */


/**
 * Deals with reading from a page with one or more ALL access handlers.
 *
 * @returns Strict VBox status code in ring-0 and raw-mode, ignorable in ring-3.
 *          See PGM_HANDLER_PHYS_IS_VALID_STATUS and
 *          PGM_HANDLER_VIRT_IS_VALID_STATUS for details.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The page descriptor.
 * @param   GCPhys      The physical address to start reading at.
 * @param   pvBuf       Where to put the bits we read.
 * @param   cb          How much to read - less or equal to a page.
 * @param   enmOrigin   The origin of this call.
 */
static VBOXSTRICTRC pgmPhysReadHandler(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void *pvBuf, size_t cb,
                                       PGMACCESSORIGIN enmOrigin)
{
    /*
     * The most frequent access here is MMIO and shadowed ROM.
     * The current code ASSUMES all these access handlers covers full pages!
     */

    /*
     * Whatever we do we need the source page, map it first.
     */
    PGMPAGEMAPLOCK PgMpLck;
    const void    *pvSrc = NULL;
    int rc = pgmPhysGCPhys2CCPtrInternalReadOnly(pVM, pPage, GCPhys, &pvSrc, &PgMpLck);
/** @todo Check how this can work for MMIO pages? */
    if (RT_FAILURE(rc))
    {
        AssertLogRelMsgFailed(("pgmPhysGCPhys2CCPtrInternalReadOnly failed on %RGp / %R[pgmpage] -> %Rrc\n",
                               GCPhys, pPage, rc));
        memset(pvBuf, 0xff, cb);
        return VINF_SUCCESS;
    }

    VBOXSTRICTRC rcStrict = VINF_PGM_HANDLER_DO_DEFAULT;

    /*
     * Deal with any physical handlers.
     */
    PVMCPUCC pVCpu = VMMGetCpu(pVM);
    if (   PGM_PAGE_GET_HNDL_PHYS_STATE(pPage) == PGM_PAGE_HNDL_PHYS_STATE_ALL
        || PGM_PAGE_IS_MMIO_OR_SPECIAL_ALIAS(pPage))
    {
        PPGMPHYSHANDLER pCur;
        rc = pgmHandlerPhysicalLookup(pVM, GCPhys, &pCur);
        if (RT_SUCCESS(rc))
        {
            Assert(pCur && GCPhys >= pCur->Key && GCPhys <= pCur->KeyLast);
            Assert((pCur->Key     & GUEST_PAGE_OFFSET_MASK) == 0);
            Assert((pCur->KeyLast & GUEST_PAGE_OFFSET_MASK) == GUEST_PAGE_OFFSET_MASK);
#ifndef IN_RING3
            if (enmOrigin != PGMACCESSORIGIN_IEM)
            {
                /* Cannot reliably handle informational status codes in this context */
                pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
                return VERR_PGM_PHYS_WR_HIT_HANDLER;
            }
#endif
            PCPGMPHYSHANDLERTYPEINT const pCurType   = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
            PFNPGMPHYSHANDLER const       pfnHandler = pCurType->pfnHandler; Assert(pfnHandler);
            uint64_t const                uUser      = !pCurType->fRing0DevInsIdx ? pCur->uUser
                                                     : (uintptr_t)PDMDeviceRing0IdxToInstance(pVM, pCur->uUser);

            Log5(("pgmPhysReadHandler: GCPhys=%RGp cb=%#x pPage=%R[pgmpage] phys %s\n", GCPhys, cb, pPage, R3STRING(pCur->pszDesc) ));
            STAM_PROFILE_START(&pCur->Stat, h);
            PGM_LOCK_ASSERT_OWNER(pVM);

            /* Release the PGM lock as MMIO handlers take the IOM lock. (deadlock prevention) */
            PGM_UNLOCK(pVM);
            /* If the access origins with a device, make sure the buffer is initialized
               as a guard against leaking heap, stack and other info via badly written
               MMIO handling. @bugref{10651} */
            if (enmOrigin == PGMACCESSORIGIN_DEVICE)
                memset(pvBuf, 0xff, cb);
            rcStrict = pfnHandler(pVM, pVCpu, GCPhys, (void *)pvSrc, pvBuf, cb, PGMACCESSTYPE_READ, enmOrigin, uUser);
            PGM_LOCK_VOID(pVM);

            STAM_PROFILE_STOP(&pCur->Stat, h); /* no locking needed, entry is unlikely reused before we get here. */
            pCur = NULL; /* might not be valid anymore. */
            AssertLogRelMsg(PGM_HANDLER_PHYS_IS_VALID_STATUS(rcStrict, false),
                            ("rcStrict=%Rrc GCPhys=%RGp\n", VBOXSTRICTRC_VAL(rcStrict), GCPhys));
            if (   rcStrict != VINF_PGM_HANDLER_DO_DEFAULT
                && !PGM_PHYS_RW_IS_SUCCESS(rcStrict))
            {
                pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
                return rcStrict;
            }
        }
        else if (rc == VERR_NOT_FOUND)
            AssertLogRelMsgFailed(("rc=%Rrc GCPhys=%RGp cb=%#x\n", rc, GCPhys, cb));
        else
            AssertLogRelMsgFailedReturn(("rc=%Rrc GCPhys=%RGp cb=%#x\n", rc, GCPhys, cb), rc);
    }

    /*
     * Take the default action.
     */
    if (rcStrict == VINF_PGM_HANDLER_DO_DEFAULT)
    {
        memcpy(pvBuf, pvSrc, cb);
        rcStrict = VINF_SUCCESS;
    }
    pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
    return rcStrict;
}


/**
 * Read physical memory.
 *
 * This API respects access handlers and MMIO. Use PGMPhysSimpleReadGCPhys() if you
 * want to ignore those.
 *
 * @returns Strict VBox status code in raw-mode and ring-0, normal VBox status
 *          code in ring-3.  Use PGM_PHYS_RW_IS_SUCCESS to check.
 * @retval  VINF_SUCCESS in all context - read completed.
 *
 * @retval  VINF_EM_OFF in RC and R0 - read completed.
 * @retval  VINF_EM_SUSPEND in RC and R0 - read completed.
 * @retval  VINF_EM_RESET in RC and R0 - read completed.
 * @retval  VINF_EM_HALT in RC and R0 - read completed.
 * @retval  VINF_SELM_SYNC_GDT in RC only - read completed.
 *
 * @retval  VINF_EM_DBG_STOP in RC and R0 - read completed.
 * @retval  VINF_EM_DBG_BREAKPOINT in RC and R0 - read completed.
 * @retval  VINF_EM_RAW_EMULATE_INSTR in RC and R0 only.
 *
 * @retval  VINF_IOM_R3_MMIO_READ in RC and R0.
 * @retval  VINF_IOM_R3_MMIO_READ_WRITE in RC and R0.
 *
 * @retval  VINF_PATM_CHECK_PATCH_PAGE in RC only.
 *
 * @retval  VERR_PGM_PHYS_WR_HIT_HANDLER in RC and R0 for access origins that
 *          haven't been cleared for strict status codes yet.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Physical address start reading from.
 * @param   pvBuf           Where to put the read bits.
 * @param   cbRead          How many bytes to read.
 * @param   enmOrigin       The origin of this call.
 */
VMMDECL(VBOXSTRICTRC) PGMPhysRead(PVMCC pVM, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead, PGMACCESSORIGIN enmOrigin)
{
    AssertMsgReturn(cbRead > 0, ("don't even think about reading zero bytes!\n"), VINF_SUCCESS);
    LogFlow(("PGMPhysRead: %RGp %d\n", GCPhys, cbRead));

    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysRead));
    STAM_COUNTER_ADD(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysReadBytes), cbRead);

    PGM_LOCK_VOID(pVM);

    /*
     * Copy loop on ram ranges.
     */
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    for (;;)
    {
        PPGMRAMRANGE const pRam = pgmPhysGetRangeAtOrAbove(pVM, GCPhys);

        /* Inside range or not? */
        if (pRam && GCPhys >= pRam->GCPhys)
        {
            /*
             * Must work our way thru this page by page.
             */
            RTGCPHYS off = GCPhys - pRam->GCPhys;
            while (off < pRam->cb)
            {
                unsigned iPage = off >> GUEST_PAGE_SHIFT;
                PPGMPAGE pPage = &pRam->aPages[iPage];
                size_t   cb    = GUEST_PAGE_SIZE - (off & GUEST_PAGE_OFFSET_MASK);
                if (cb > cbRead)
                    cb = cbRead;

                /*
                 * Normal page? Get the pointer to it.
                 */
                if (   !PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage)
                    && !PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(pPage))
                {
                    /*
                     * Get the pointer to the page.
                     */
                    PGMPAGEMAPLOCK PgMpLck;
                    const void    *pvSrc;
                    int rc = pgmPhysGCPhys2CCPtrInternalReadOnly(pVM, pPage, pRam->GCPhys + off, &pvSrc, &PgMpLck);
                    if (RT_SUCCESS(rc))
                    {
                        memcpy(pvBuf, pvSrc, cb);
                        pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
                    }
                    else
                    {
                        AssertLogRelMsgFailed(("pgmPhysGCPhys2CCPtrInternalReadOnly failed on %RGp / %R[pgmpage] -> %Rrc\n",
                                               pRam->GCPhys + off, pPage, rc));
                        memset(pvBuf, 0xff, cb);
                    }
                }
                /*
                 * Have ALL/MMIO access handlers.
                 */
                else
                {
                    VBOXSTRICTRC rcStrict2 = pgmPhysReadHandler(pVM, pPage, pRam->GCPhys + off, pvBuf, cb, enmOrigin);
                    if (PGM_PHYS_RW_IS_SUCCESS(rcStrict2))
                        PGM_PHYS_RW_DO_UPDATE_STRICT_RC(rcStrict, rcStrict2);
                    else
                    {
                        /* Set the remaining buffer to a known value. */
                        memset(pvBuf, 0xff, cbRead);
                        PGM_UNLOCK(pVM);
                        return rcStrict2;
                    }
                }

                /* next page */
                if (cb >= cbRead)
                {
                    PGM_UNLOCK(pVM);
                    return rcStrict;
                }
                cbRead -= cb;
                off    += cb;
                pvBuf   = (char *)pvBuf + cb;
            } /* walk pages in ram range. */

            GCPhys = pRam->GCPhysLast + 1;
        }
        else
        {
            LogFlow(("PGMPhysRead: Unassigned %RGp size=%u\n", GCPhys, cbRead));

            /*
             * Unassigned address space.
             */
            size_t cb = pRam ? pRam->GCPhys - GCPhys : ~(size_t)0;
            if (cb >= cbRead)
            {
                memset(pvBuf, 0xff, cbRead);
                break;
            }
            memset(pvBuf, 0xff, cb);

            cbRead -= cb;
            pvBuf   = (char *)pvBuf + cb;
            GCPhys += cb;
        }

    } /* Ram range walk */

    PGM_UNLOCK(pVM);
    return rcStrict;
}


/**
 * Deals with writing to a page with one or more WRITE or ALL access handlers.
 *
 * @returns Strict VBox status code in ring-0 and raw-mode, ignorable in ring-3.
 *          See PGM_HANDLER_PHYS_IS_VALID_STATUS and
 *          PGM_HANDLER_VIRT_IS_VALID_STATUS for details.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pPage       The page descriptor.
 * @param   GCPhys      The physical address to start writing at.
 * @param   pvBuf       What to write.
 * @param   cbWrite     How much to write - less or equal to a page.
 * @param   enmOrigin   The origin of this call.
 */
static VBOXSTRICTRC pgmPhysWriteHandler(PVMCC pVM, PPGMPAGE pPage, RTGCPHYS GCPhys, void const *pvBuf, size_t cbWrite,
                                        PGMACCESSORIGIN enmOrigin)
{
    PGMPAGEMAPLOCK  PgMpLck;
    void           *pvDst = NULL;
    VBOXSTRICTRC    rcStrict;

    /*
     * Give priority to physical handlers (like #PF does).
     *
     * Hope for a lonely physical handler first that covers the whole write
     * area.  This should be a pretty frequent case with MMIO and the heavy
     * usage of full page handlers in the page pool.
     */
    PVMCPUCC pVCpu = VMMGetCpu(pVM);
    PPGMPHYSHANDLER pCur;
    rcStrict = pgmHandlerPhysicalLookup(pVM, GCPhys, &pCur);
    if (RT_SUCCESS(rcStrict))
    {
        Assert(GCPhys >= pCur->Key && GCPhys <= pCur->KeyLast);
#ifndef IN_RING3
        if (enmOrigin != PGMACCESSORIGIN_IEM)
            /* Cannot reliably handle informational status codes in this context */
            return VERR_PGM_PHYS_WR_HIT_HANDLER;
#endif
        size_t cbRange = pCur->KeyLast - GCPhys + 1;
        if (cbRange > cbWrite)
            cbRange = cbWrite;

        Assert(PGMPHYSHANDLER_GET_TYPE(pVM, pCur)->pfnHandler);
        Log5(("pgmPhysWriteHandler: GCPhys=%RGp cbRange=%#x pPage=%R[pgmpage] phys %s\n",
              GCPhys, cbRange, pPage, R3STRING(pCur->pszDesc) ));
        if (!PGM_PAGE_IS_MMIO_OR_SPECIAL_ALIAS(pPage))
            rcStrict = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, GCPhys, &pvDst, &PgMpLck);
        else
            rcStrict = VINF_SUCCESS;
        if (RT_SUCCESS(rcStrict))
        {
            PCPGMPHYSHANDLERTYPEINT const pCurType   = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pCur);
            PFNPGMPHYSHANDLER const       pfnHandler = pCurType->pfnHandler;
            uint64_t const                uUser      = !pCurType->fRing0DevInsIdx ? pCur->uUser
                                                     : (uintptr_t)PDMDeviceRing0IdxToInstance(pVM, pCur->uUser);
            STAM_PROFILE_START(&pCur->Stat, h);

            /* Most handlers will want to release the PGM lock for deadlock prevention
               (esp. MMIO), though some PGM internal ones like the page pool and MMIO2
               dirty page trackers will want to keep it for performance reasons. */
            PGM_LOCK_ASSERT_OWNER(pVM);
            if (pCurType->fKeepPgmLock)
                rcStrict = pfnHandler(pVM, pVCpu, GCPhys, pvDst, (void *)pvBuf, cbRange, PGMACCESSTYPE_WRITE, enmOrigin, uUser);
            else
            {
                PGM_UNLOCK(pVM);
                rcStrict = pfnHandler(pVM, pVCpu, GCPhys, pvDst, (void *)pvBuf, cbRange, PGMACCESSTYPE_WRITE, enmOrigin, uUser);
                PGM_LOCK_VOID(pVM);
            }

            STAM_PROFILE_STOP(&pCur->Stat, h); /* no locking needed, entry is unlikely reused before we get here. */
            pCur = NULL; /* might not be valid anymore. */
            if (rcStrict == VINF_PGM_HANDLER_DO_DEFAULT)
            {
                if (pvDst)
                    memcpy(pvDst, pvBuf, cbRange);
                rcStrict = VINF_SUCCESS;
            }
            else
                AssertLogRelMsg(PGM_HANDLER_PHYS_IS_VALID_STATUS(rcStrict, true),
                                ("rcStrict=%Rrc GCPhys=%RGp pPage=%R[pgmpage] %s\n",
                                 VBOXSTRICTRC_VAL(rcStrict), GCPhys, pPage, pCur ? R3STRING(pCur->pszDesc) : ""));
        }
        else
            AssertLogRelMsgFailedReturn(("pgmPhysGCPhys2CCPtrInternal failed on %RGp / %R[pgmpage] -> %Rrc\n",
                                         GCPhys, pPage, VBOXSTRICTRC_VAL(rcStrict)), rcStrict);
        if (RT_LIKELY(cbRange == cbWrite) || !PGM_PHYS_RW_IS_SUCCESS(rcStrict))
        {
            if (pvDst)
                pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
            return rcStrict;
        }

        /* more fun to be had below */
        cbWrite -= cbRange;
        GCPhys  += cbRange;
        pvBuf    = (uint8_t *)pvBuf + cbRange;
        pvDst    = (uint8_t *)pvDst + cbRange;
    }
    else if (rcStrict == VERR_NOT_FOUND) /* The handler is somewhere else in the page, deal with it below. */
        rcStrict = VINF_SUCCESS;
    else
        AssertMsgFailedReturn(("rcStrict=%Rrc GCPhys=%RGp\n", VBOXSTRICTRC_VAL(rcStrict), GCPhys), rcStrict);
    Assert(!PGM_PAGE_IS_MMIO_OR_ALIAS(pPage)); /* MMIO handlers are all GUEST_PAGE_SIZEed! */

    /*
     * Deal with all the odd ends (used to be deal with virt+phys).
     */
    Assert(rcStrict != VINF_PGM_HANDLER_DO_DEFAULT);

    /* We need a writable destination page. */
    if (!pvDst)
    {
        int rc2 = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, GCPhys, &pvDst, &PgMpLck);
        AssertLogRelMsgReturn(RT_SUCCESS(rc2),
                              ("pgmPhysGCPhys2CCPtrInternal failed on %RGp / %R[pgmpage] -> %Rrc\n", GCPhys, pPage, rc2),
                              rc2);
    }

    /** @todo clean up this code some more now there are no virtual handlers any
     *        more. */
    /* The loop state (big + ugly). */
    PPGMPHYSHANDLER pPhys       = NULL;
    uint32_t        offPhys     = GUEST_PAGE_SIZE;
    uint32_t        offPhysLast = GUEST_PAGE_SIZE;
    bool            fMorePhys   = PGM_PAGE_HAS_ACTIVE_PHYSICAL_HANDLERS(pPage);

    /* The loop. */
    for (;;)
    {
        if (fMorePhys && !pPhys)
        {
            rcStrict = pgmHandlerPhysicalLookup(pVM, GCPhys, &pPhys);
            if (RT_SUCCESS_NP(rcStrict))
            {
                offPhys = 0;
                offPhysLast = pPhys->KeyLast - GCPhys; /* ASSUMES < 4GB handlers... */
            }
            else
            {
                AssertMsgReturn(rcStrict == VERR_NOT_FOUND, ("%Rrc GCPhys=%RGp\n", VBOXSTRICTRC_VAL(rcStrict), GCPhys), rcStrict);

                rcStrict = pVM->VMCC_CTX(pgm).s.pPhysHandlerTree->lookupMatchingOrAbove(&pVM->VMCC_CTX(pgm).s.PhysHandlerAllocator,
                                                                                        GCPhys, &pPhys);
                AssertMsgReturn(RT_SUCCESS(rcStrict) || rcStrict == VERR_NOT_FOUND,
                                ("%Rrc GCPhys=%RGp\n", VBOXSTRICTRC_VAL(rcStrict), GCPhys), rcStrict);

                if (   RT_SUCCESS(rcStrict)
                    && pPhys->Key <= GCPhys + (cbWrite - 1))
                {
                    offPhys     = pPhys->Key     - GCPhys;
                    offPhysLast = pPhys->KeyLast - GCPhys; /* ASSUMES < 4GB handlers... */
                    Assert(pPhys->KeyLast - pPhys->Key < _4G);
                }
                else
                {
                    pPhys     = NULL;
                    fMorePhys = false;
                    offPhys   = offPhysLast = GUEST_PAGE_SIZE;
                }
            }
        }

        /*
         * Handle access to space without handlers (that's easy).
         */
        VBOXSTRICTRC rcStrict2 = VINF_PGM_HANDLER_DO_DEFAULT;
        uint32_t cbRange = (uint32_t)cbWrite;
        Assert(cbRange == cbWrite);

        /*
         * Physical handler.
         */
        if (!offPhys)
        {
#ifndef IN_RING3
            if (enmOrigin != PGMACCESSORIGIN_IEM)
                /* Cannot reliably handle informational status codes in this context */
                return VERR_PGM_PHYS_WR_HIT_HANDLER;
#endif
            if (cbRange > offPhysLast + 1)
                cbRange = offPhysLast + 1;

            PCPGMPHYSHANDLERTYPEINT const pCurType   = PGMPHYSHANDLER_GET_TYPE_NO_NULL(pVM, pPhys);
            PFNPGMPHYSHANDLER const       pfnHandler = pCurType->pfnHandler;
            uint64_t const                uUser      = !pCurType->fRing0DevInsIdx ? pPhys->uUser
                                                     : (uintptr_t)PDMDeviceRing0IdxToInstance(pVM, pPhys->uUser);

            Log5(("pgmPhysWriteHandler: GCPhys=%RGp cbRange=%#x pPage=%R[pgmpage] phys %s\n", GCPhys, cbRange, pPage, R3STRING(pPhys->pszDesc) ));
            STAM_PROFILE_START(&pPhys->Stat, h);

            /* Most handlers will want to release the PGM lock for deadlock prevention
               (esp. MMIO), though some PGM internal ones like the page pool and MMIO2
               dirty page trackers will want to keep it for performance reasons. */
            PGM_LOCK_ASSERT_OWNER(pVM);
            if (pCurType->fKeepPgmLock)
                rcStrict2 = pfnHandler(pVM, pVCpu, GCPhys, pvDst, (void *)pvBuf, cbRange, PGMACCESSTYPE_WRITE, enmOrigin, uUser);
            else
            {
                PGM_UNLOCK(pVM);
                rcStrict2 = pfnHandler(pVM, pVCpu, GCPhys, pvDst, (void *)pvBuf, cbRange, PGMACCESSTYPE_WRITE, enmOrigin, uUser);
                PGM_LOCK_VOID(pVM);
            }

            STAM_PROFILE_STOP(&pPhys->Stat, h); /* no locking needed, entry is unlikely reused before we get here. */
            pPhys = NULL; /* might not be valid anymore. */
            AssertLogRelMsg(PGM_HANDLER_PHYS_IS_VALID_STATUS(rcStrict2, true),
                            ("rcStrict2=%Rrc (rcStrict=%Rrc) GCPhys=%RGp pPage=%R[pgmpage] %s\n", VBOXSTRICTRC_VAL(rcStrict2),
                             VBOXSTRICTRC_VAL(rcStrict), GCPhys, pPage,  pPhys ? R3STRING(pPhys->pszDesc) : ""));
        }

        /*
         * Execute the default action and merge the status codes.
         */
        if (rcStrict2 == VINF_PGM_HANDLER_DO_DEFAULT)
        {
            memcpy(pvDst, pvBuf, cbRange);
            rcStrict2 = VINF_SUCCESS;
        }
        else if (!PGM_PHYS_RW_IS_SUCCESS(rcStrict2))
        {
            pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
            return rcStrict2;
        }
        else
            PGM_PHYS_RW_DO_UPDATE_STRICT_RC(rcStrict, rcStrict2);

        /*
         * Advance if we've got more stuff to do.
         */
        if (cbRange >= cbWrite)
        {
            pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
            return rcStrict;
        }


        cbWrite         -= cbRange;
        GCPhys          += cbRange;
        pvBuf            = (uint8_t *)pvBuf + cbRange;
        pvDst            = (uint8_t *)pvDst + cbRange;

        offPhys         -= cbRange;
        offPhysLast     -= cbRange;
    }
}


/**
 * Write to physical memory.
 *
 * This API respects access handlers and MMIO. Use PGMPhysSimpleWriteGCPhys() if you
 * want to ignore those.
 *
 * @returns Strict VBox status code in raw-mode and ring-0, normal VBox status
 *          code in ring-3.  Use PGM_PHYS_RW_IS_SUCCESS to check.
 * @retval  VINF_SUCCESS in all context - write completed.
 *
 * @retval  VINF_EM_OFF in RC and R0 - write completed.
 * @retval  VINF_EM_SUSPEND in RC and R0 - write completed.
 * @retval  VINF_EM_RESET in RC and R0 - write completed.
 * @retval  VINF_EM_HALT in RC and R0 - write completed.
 * @retval  VINF_SELM_SYNC_GDT in RC only - write completed.
 *
 * @retval  VINF_EM_DBG_STOP in RC and R0 - write completed.
 * @retval  VINF_EM_DBG_BREAKPOINT in RC and R0 - write completed.
 * @retval  VINF_EM_RAW_EMULATE_INSTR in RC and R0 only.
 *
 * @retval  VINF_IOM_R3_MMIO_WRITE in RC and R0.
 * @retval  VINF_IOM_R3_MMIO_READ_WRITE in RC and R0.
 * @retval  VINF_IOM_R3_MMIO_COMMIT_WRITE in RC and R0.
 *
 * @retval  VINF_EM_RAW_EMULATE_INSTR_GDT_FAULT in RC only - write completed.
 * @retval  VINF_EM_RAW_EMULATE_INSTR_LDT_FAULT in RC only.
 * @retval  VINF_EM_RAW_EMULATE_INSTR_TSS_FAULT in RC only.
 * @retval  VINF_EM_RAW_EMULATE_INSTR_IDT_FAULT in RC only.
 * @retval  VINF_CSAM_PENDING_ACTION in RC only.
 * @retval  VINF_PATM_CHECK_PATCH_PAGE in RC only.
 *
 * @retval  VERR_PGM_PHYS_WR_HIT_HANDLER in RC and R0 for access origins that
 *          haven't been cleared for strict status codes yet.
 *
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Physical address to write to.
 * @param   pvBuf           What to write.
 * @param   cbWrite         How many bytes to write.
 * @param   enmOrigin       Who is calling.
 */
VMMDECL(VBOXSTRICTRC) PGMPhysWrite(PVMCC pVM, RTGCPHYS GCPhys, const void *pvBuf, size_t cbWrite, PGMACCESSORIGIN enmOrigin)
{
    AssertMsg(!pVM->pgm.s.fNoMorePhysWrites, ("Calling PGMPhysWrite after pgmR3Save()! enmOrigin=%d\n", enmOrigin));
    AssertMsgReturn(cbWrite > 0, ("don't even think about writing zero bytes!\n"), VINF_SUCCESS);
    LogFlow(("PGMPhysWrite: %RGp %d\n", GCPhys, cbWrite));

    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysWrite));
    STAM_COUNTER_ADD(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysWriteBytes), cbWrite);

    PGM_LOCK_VOID(pVM);

    /*
     * Copy loop on ram ranges.
     */
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    for (;;)
    {
        PPGMRAMRANGE const pRam = pgmPhysGetRangeAtOrAbove(pVM, GCPhys);

        /* Inside range or not? */
        if (pRam && GCPhys >= pRam->GCPhys)
        {
            /*
             * Must work our way thru this page by page.
             */
            RTGCPTR off = GCPhys - pRam->GCPhys;
            while (off < pRam->cb)
            {
                RTGCPTR     iPage = off >> GUEST_PAGE_SHIFT;
                PPGMPAGE    pPage = &pRam->aPages[iPage];
                size_t      cb    = GUEST_PAGE_SIZE - (off & GUEST_PAGE_OFFSET_MASK);
                if (cb > cbWrite)
                    cb = cbWrite;

                /*
                 * Normal page? Get the pointer to it.
                 */
                if (   !PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage)
                    && !PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(pPage))
                {
                    PGMPAGEMAPLOCK PgMpLck;
                    void          *pvDst;
                    int rc = pgmPhysGCPhys2CCPtrInternal(pVM, pPage, pRam->GCPhys + off, &pvDst, &PgMpLck);
                    if (RT_SUCCESS(rc))
                    {
                        Assert(!PGM_PAGE_IS_BALLOONED(pPage));
                        memcpy(pvDst, pvBuf, cb);
                        pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
                    }
                    /* Ignore writes to ballooned pages. */
                    else if (!PGM_PAGE_IS_BALLOONED(pPage))
                        AssertLogRelMsgFailed(("pgmPhysGCPhys2CCPtrInternal failed on %RGp / %R[pgmpage] -> %Rrc\n",
                                                pRam->GCPhys + off, pPage, rc));
                }
                /*
                 * Active WRITE or ALL access handlers.
                 */
                else
                {
                    VBOXSTRICTRC rcStrict2 = pgmPhysWriteHandler(pVM, pPage, pRam->GCPhys + off, pvBuf, cb, enmOrigin);
                    if (PGM_PHYS_RW_IS_SUCCESS(rcStrict2))
                        PGM_PHYS_RW_DO_UPDATE_STRICT_RC(rcStrict, rcStrict2);
                    else
                    {
                        PGM_UNLOCK(pVM);
                        return rcStrict2;
                    }
                }

                /* next page */
                if (cb >= cbWrite)
                {
                    PGM_UNLOCK(pVM);
                    return rcStrict;
                }

                cbWrite -= cb;
                off     += cb;
                pvBuf    = (const char *)pvBuf + cb;
            } /* walk pages in ram range */

            GCPhys = pRam->GCPhysLast + 1;
        }
        else
        {
            /*
             * Unassigned address space, skip it.
             */
            if (!pRam)
                break;
            size_t cb = pRam->GCPhys - GCPhys;
            if (cb >= cbWrite)
                break;
            cbWrite -=                       cb;
            pvBuf    = (const char *)pvBuf + cb;
            GCPhys  +=                       cb;
        }

    } /* Ram range walk */

    PGM_UNLOCK(pVM);
    return rcStrict;
}


/**
 * Read from guest physical memory by GC physical address, bypassing
 * MMIO and access handlers.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvDst       The destination address.
 * @param   GCPhysSrc   The source address (GC physical address).
 * @param   cb          The number of bytes to read.
 */
VMMDECL(int) PGMPhysSimpleReadGCPhys(PVMCC pVM, void *pvDst, RTGCPHYS GCPhysSrc, size_t cb)
{
    /*
     * Treat the first page as a special case.
     */
    if (!cb)
        return VINF_SUCCESS;

    /* map the 1st page */
    void const *pvSrc;
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPhys2CCPtrReadOnly(pVM, GCPhysSrc, &pvSrc, &Lock);
    if (RT_FAILURE(rc))
        return rc;

    /* optimize for the case where access is completely within the first page. */
    size_t cbPage = GUEST_PAGE_SIZE - (GCPhysSrc & GUEST_PAGE_OFFSET_MASK);
    if (RT_LIKELY(cb <= cbPage))
    {
        memcpy(pvDst, pvSrc, cb);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        return VINF_SUCCESS;
    }

    /* copy to the end of the page. */
    memcpy(pvDst, pvSrc, cbPage);
    PGMPhysReleasePageMappingLock(pVM, &Lock);
    GCPhysSrc += cbPage;
    pvDst = (uint8_t *)pvDst + cbPage;
    cb -= cbPage;

    /*
     * Page by page.
     */
    for (;;)
    {
        /* map the page */
        rc = PGMPhysGCPhys2CCPtrReadOnly(pVM, GCPhysSrc, &pvSrc, &Lock);
        if (RT_FAILURE(rc))
            return rc;

        /* last page? */
        if (cb <= GUEST_PAGE_SIZE)
        {
            memcpy(pvDst, pvSrc, cb);
            PGMPhysReleasePageMappingLock(pVM, &Lock);
            return VINF_SUCCESS;
        }

        /* copy the entire page and advance */
        memcpy(pvDst, pvSrc, GUEST_PAGE_SIZE);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        GCPhysSrc +=                    GUEST_PAGE_SIZE;
        pvDst      = (uint8_t *)pvDst + GUEST_PAGE_SIZE;
        cb        -=                    GUEST_PAGE_SIZE;
    }
    /* won't ever get here. */
}


/**
 * Write to guest physical memory referenced by GC pointer.
 * Write memory to GC physical address in guest physical memory.
 *
 * This will bypass MMIO and access handlers.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   GCPhysDst   The GC physical address of the destination.
 * @param   pvSrc       The source buffer.
 * @param   cb          The number of bytes to write.
 */
VMMDECL(int) PGMPhysSimpleWriteGCPhys(PVMCC pVM, RTGCPHYS GCPhysDst, const void *pvSrc, size_t cb)
{
    LogFlow(("PGMPhysSimpleWriteGCPhys: %RGp %zu\n", GCPhysDst, cb));

    /*
     * Treat the first page as a special case.
     */
    if (!cb)
        return VINF_SUCCESS;

    /* map the 1st page */
    void *pvDst;
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPhys2CCPtr(pVM, GCPhysDst, &pvDst, &Lock);
    if (RT_FAILURE(rc))
        return rc;

    /* optimize for the case where access is completely within the first page. */
    size_t cbPage = GUEST_PAGE_SIZE - (GCPhysDst & GUEST_PAGE_OFFSET_MASK);
    if (RT_LIKELY(cb <= cbPage))
    {
        memcpy(pvDst, pvSrc, cb);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        return VINF_SUCCESS;
    }

    /* copy to the end of the page. */
    memcpy(pvDst, pvSrc, cbPage);
    PGMPhysReleasePageMappingLock(pVM, &Lock);
    GCPhysDst += cbPage;
    pvSrc = (const uint8_t *)pvSrc + cbPage;
    cb -= cbPage;

    /*
     * Page by page.
     */
    for (;;)
    {
        /* map the page */
        rc = PGMPhysGCPhys2CCPtr(pVM, GCPhysDst, &pvDst, &Lock);
        if (RT_FAILURE(rc))
            return rc;

        /* last page? */
        if (cb <= GUEST_PAGE_SIZE)
        {
            memcpy(pvDst, pvSrc, cb);
            PGMPhysReleasePageMappingLock(pVM, &Lock);
            return VINF_SUCCESS;
        }

        /* copy the entire page and advance */
        memcpy(pvDst, pvSrc, GUEST_PAGE_SIZE);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        GCPhysDst +=                          GUEST_PAGE_SIZE;
        pvSrc      = (const uint8_t *)pvSrc + GUEST_PAGE_SIZE;
        cb        -=                          GUEST_PAGE_SIZE;
    }
    /* won't ever get here. */
}


/**
 * Read from guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * bypass access handlers and not set any accessed bits.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pvDst       The destination address.
 * @param   GCPtrSrc    The source address (GC pointer).
 * @param   cb          The number of bytes to read.
 */
VMMDECL(int) PGMPhysSimpleReadGCPtr(PVMCPUCC pVCpu, void *pvDst, RTGCPTR GCPtrSrc, size_t cb)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
/** @todo fix the macro / state handling: VMCPU_ASSERT_EMT_OR_GURU(pVCpu); */

    /*
     * Treat the first page as a special case.
     */
    if (!cb)
        return VINF_SUCCESS;

    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysSimpleRead));
    STAM_COUNTER_ADD(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysSimpleReadBytes), cb);

    /* Take the PGM lock here, because many called functions take the lock for a very short period. That's counter-productive
     * when many VCPUs are fighting for the lock.
     */
    PGM_LOCK_VOID(pVM);

    /* map the 1st page */
    void const *pvSrc;
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPtr2CCPtrReadOnly(pVCpu, GCPtrSrc, &pvSrc, &Lock);
    if (RT_FAILURE(rc))
    {
        PGM_UNLOCK(pVM);
        return rc;
    }

    /* optimize for the case where access is completely within the first page. */
    size_t cbPage = GUEST_PAGE_SIZE - ((RTGCUINTPTR)GCPtrSrc & GUEST_PAGE_OFFSET_MASK);
    if (RT_LIKELY(cb <= cbPage))
    {
        memcpy(pvDst, pvSrc, cb);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        PGM_UNLOCK(pVM);
        return VINF_SUCCESS;
    }

    /* copy to the end of the page. */
    memcpy(pvDst, pvSrc, cbPage);
    PGMPhysReleasePageMappingLock(pVM, &Lock);
    GCPtrSrc = (RTGCPTR)((RTGCUINTPTR)GCPtrSrc + cbPage);
    pvDst = (uint8_t *)pvDst + cbPage;
    cb -= cbPage;

    /*
     * Page by page.
     */
    for (;;)
    {
        /* map the page */
        rc = PGMPhysGCPtr2CCPtrReadOnly(pVCpu, GCPtrSrc, &pvSrc, &Lock);
        if (RT_FAILURE(rc))
        {
            PGM_UNLOCK(pVM);
            return rc;
        }

        /* last page? */
        if (cb <= GUEST_PAGE_SIZE)
        {
            memcpy(pvDst, pvSrc, cb);
            PGMPhysReleasePageMappingLock(pVM, &Lock);
            PGM_UNLOCK(pVM);
            return VINF_SUCCESS;
        }

        /* copy the entire page and advance */
        memcpy(pvDst, pvSrc, GUEST_PAGE_SIZE);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        GCPtrSrc = (RTGCPTR)((RTGCUINTPTR)GCPtrSrc + GUEST_PAGE_SIZE);
        pvDst    = (uint8_t *)pvDst                + GUEST_PAGE_SIZE;
        cb      -=                                   GUEST_PAGE_SIZE;
    }
    /* won't ever get here. */
}


/**
 * Write to guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * bypass access handlers and not set dirty or accessed bits.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtrDst    The destination address (GC pointer).
 * @param   pvSrc       The source address.
 * @param   cb          The number of bytes to write.
 */
VMMDECL(int) PGMPhysSimpleWriteGCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Treat the first page as a special case.
     */
    if (!cb)
        return VINF_SUCCESS;

    STAM_COUNTER_INC(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysSimpleWrite));
    STAM_COUNTER_ADD(&pVM->pgm.s.Stats.CTX_MID_Z(Stat,PhysSimpleWriteBytes), cb);

    /* map the 1st page */
    void *pvDst;
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPtr2CCPtr(pVCpu, GCPtrDst, &pvDst, &Lock);
    if (RT_FAILURE(rc))
        return rc;

    /* optimize for the case where access is completely within the first page. */
    size_t cbPage = GUEST_PAGE_SIZE - ((RTGCUINTPTR)GCPtrDst & GUEST_PAGE_OFFSET_MASK);
    if (RT_LIKELY(cb <= cbPage))
    {
        memcpy(pvDst, pvSrc, cb);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        return VINF_SUCCESS;
    }

    /* copy to the end of the page. */
    memcpy(pvDst, pvSrc, cbPage);
    PGMPhysReleasePageMappingLock(pVM, &Lock);
    GCPtrDst = (RTGCPTR)((RTGCUINTPTR)GCPtrDst + cbPage);
    pvSrc = (const uint8_t *)pvSrc + cbPage;
    cb -= cbPage;

    /*
     * Page by page.
     */
    for (;;)
    {
        /* map the page */
        rc = PGMPhysGCPtr2CCPtr(pVCpu, GCPtrDst, &pvDst, &Lock);
        if (RT_FAILURE(rc))
            return rc;

        /* last page? */
        if (cb <= GUEST_PAGE_SIZE)
        {
            memcpy(pvDst, pvSrc, cb);
            PGMPhysReleasePageMappingLock(pVM, &Lock);
            return VINF_SUCCESS;
        }

        /* copy the entire page and advance */
        memcpy(pvDst, pvSrc, GUEST_PAGE_SIZE);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        GCPtrDst = (RTGCPTR)((RTGCUINTPTR)GCPtrDst + GUEST_PAGE_SIZE);
        pvSrc    = (const uint8_t *)pvSrc          + GUEST_PAGE_SIZE;
        cb      -=                                   GUEST_PAGE_SIZE;
    }
    /* won't ever get here. */
}


/**
 * Write to guest physical memory referenced by GC pointer and update the PTE.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * bypass access handlers but will set any dirty and accessed bits in the PTE.
 *
 * If you don't want to set the dirty bit, use PGMPhysSimpleWriteGCPtr().
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtrDst    The destination address (GC pointer).
 * @param   pvSrc       The source address.
 * @param   cb          The number of bytes to write.
 */
VMMDECL(int) PGMPhysSimpleDirtyWriteGCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb)
{
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Treat the first page as a special case.
     * Btw. this is the same code as in PGMPhyssimpleWriteGCPtr excep for the PGMGstModifyPage.
     */
    if (!cb)
        return VINF_SUCCESS;

    /* map the 1st page */
    void *pvDst;
    PGMPAGEMAPLOCK Lock;
    int rc = PGMPhysGCPtr2CCPtr(pVCpu, GCPtrDst, &pvDst, &Lock);
    if (RT_FAILURE(rc))
        return rc;

    /* optimize for the case where access is completely within the first page. */
    size_t cbPage = GUEST_PAGE_SIZE - ((RTGCUINTPTR)GCPtrDst & GUEST_PAGE_OFFSET_MASK);
    if (RT_LIKELY(cb <= cbPage))
    {
        memcpy(pvDst, pvSrc, cb);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        rc = PGMGstModifyPage(pVCpu, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D)); AssertRC(rc);
        return VINF_SUCCESS;
    }

    /* copy to the end of the page. */
    memcpy(pvDst, pvSrc, cbPage);
    PGMPhysReleasePageMappingLock(pVM, &Lock);
    rc = PGMGstModifyPage(pVCpu, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D)); AssertRC(rc);
    GCPtrDst = (RTGCPTR)((RTGCUINTPTR)GCPtrDst + cbPage);
    pvSrc = (const uint8_t *)pvSrc + cbPage;
    cb -= cbPage;

    /*
     * Page by page.
     */
    for (;;)
    {
        /* map the page */
        rc = PGMPhysGCPtr2CCPtr(pVCpu, GCPtrDst, &pvDst, &Lock);
        if (RT_FAILURE(rc))
            return rc;

        /* last page? */
        if (cb <= GUEST_PAGE_SIZE)
        {
            memcpy(pvDst, pvSrc, cb);
            PGMPhysReleasePageMappingLock(pVM, &Lock);
            rc = PGMGstModifyPage(pVCpu, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D)); AssertRC(rc);
            return VINF_SUCCESS;
        }

        /* copy the entire page and advance */
        memcpy(pvDst, pvSrc, GUEST_PAGE_SIZE);
        PGMPhysReleasePageMappingLock(pVM, &Lock);
        rc = PGMGstModifyPage(pVCpu, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D)); AssertRC(rc);
        GCPtrDst = (RTGCPTR)((RTGCUINTPTR)GCPtrDst + GUEST_PAGE_SIZE);
        pvSrc    = (const uint8_t *)pvSrc          + GUEST_PAGE_SIZE;
        cb      -=                                   GUEST_PAGE_SIZE;
    }
    /* won't ever get here. */
}


/**
 * Read from guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * respect access handlers and set accessed bits.
 *
 * @returns Strict VBox status, see PGMPhysRead for details.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT if there is no page mapped at the
 *          specified virtual address.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pvDst       The destination address.
 * @param   GCPtrSrc    The source address (GC pointer).
 * @param   cb          The number of bytes to read.
 * @param   enmOrigin   Who is calling.
 * @thread  EMT(pVCpu)
 */
VMMDECL(VBOXSTRICTRC) PGMPhysReadGCPtr(PVMCPUCC pVCpu, void *pvDst, RTGCPTR GCPtrSrc, size_t cb, PGMACCESSORIGIN enmOrigin)
{
    int         rc;
    PVMCC       pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Anything to do?
     */
    if (!cb)
        return VINF_SUCCESS;

    LogFlow(("PGMPhysReadGCPtr: %RGv %zu\n", GCPtrSrc, cb));

    /*
     * Optimize reads within a single page.
     */
    if (((RTGCUINTPTR)GCPtrSrc & GUEST_PAGE_OFFSET_MASK) + cb <= GUEST_PAGE_SIZE)
    {
        /* Convert virtual to physical address + flags */
        PGMPTWALK Walk;
        rc = PGMGstGetPage(pVCpu, (RTGCUINTPTR)GCPtrSrc, &Walk);
        AssertMsgRCReturn(rc, ("GetPage failed with %Rrc for %RGv\n", rc, GCPtrSrc), rc);
        RTGCPHYS const GCPhys = Walk.GCPhys | ((RTGCUINTPTR)GCPtrSrc & GUEST_PAGE_OFFSET_MASK);

        /* mark the guest page as accessed. */
        if (!(Walk.fEffective & X86_PTE_A))
        {
            rc = PGMGstModifyPage(pVCpu, GCPtrSrc, 1, X86_PTE_A, ~(uint64_t)(X86_PTE_A));
            AssertRC(rc);
        }

        return PGMPhysRead(pVM, GCPhys, pvDst, cb, enmOrigin);
    }

    /*
     * Page by page.
     */
    for (;;)
    {
        /* Convert virtual to physical address + flags */
        PGMPTWALK Walk;
        rc = PGMGstGetPage(pVCpu, (RTGCUINTPTR)GCPtrSrc, &Walk);
        AssertMsgRCReturn(rc, ("GetPage failed with %Rrc for %RGv\n", rc, GCPtrSrc), rc);
        RTGCPHYS const GCPhys = Walk.GCPhys | ((RTGCUINTPTR)GCPtrSrc & GUEST_PAGE_OFFSET_MASK);

        /* mark the guest page as accessed. */
        if (!(Walk.fEffective & X86_PTE_A))
        {
            rc = PGMGstModifyPage(pVCpu, GCPtrSrc, 1, X86_PTE_A, ~(uint64_t)(X86_PTE_A));
            AssertRC(rc);
        }

        /* copy */
        size_t cbRead = GUEST_PAGE_SIZE - ((RTGCUINTPTR)GCPtrSrc & GUEST_PAGE_OFFSET_MASK);
        if (cbRead < cb)
        {
            VBOXSTRICTRC rcStrict = PGMPhysRead(pVM, GCPhys, pvDst, cbRead, enmOrigin);
            if (RT_LIKELY(rcStrict == VINF_SUCCESS))
            { /* likely */ }
            else
                return rcStrict;
        }
        else    /* Last page (cbRead is GUEST_PAGE_SIZE, we only need cb!) */
            return PGMPhysRead(pVM, GCPhys, pvDst, cb, enmOrigin);

        /* next */
        Assert(cb > cbRead);
        cb         -= cbRead;
        pvDst       = (uint8_t *)pvDst + cbRead;
        GCPtrSrc   += cbRead;
    }
}


/**
 * Write to guest physical memory referenced by GC pointer.
 *
 * This function uses the current CR3/CR0/CR4 of the guest and will
 * respect access handlers and set dirty and accessed bits.
 *
 * @returns Strict VBox status, see PGMPhysWrite for details.
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT if there is no page mapped at the
 *          specified virtual address.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPtrDst    The destination address (GC pointer).
 * @param   pvSrc       The source address.
 * @param   cb          The number of bytes to write.
 * @param   enmOrigin       Who is calling.
 */
VMMDECL(VBOXSTRICTRC) PGMPhysWriteGCPtr(PVMCPUCC pVCpu, RTGCPTR GCPtrDst, const void *pvSrc, size_t cb, PGMACCESSORIGIN enmOrigin)
{
    int         rc;
    PVMCC       pVM = pVCpu->CTX_SUFF(pVM);
    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Anything to do?
     */
    if (!cb)
        return VINF_SUCCESS;

    LogFlow(("PGMPhysWriteGCPtr: %RGv %zu\n", GCPtrDst, cb));

    /*
     * Optimize writes within a single page.
     */
    if (((RTGCUINTPTR)GCPtrDst & GUEST_PAGE_OFFSET_MASK) + cb <= GUEST_PAGE_SIZE)
    {
        /* Convert virtual to physical address + flags */
        PGMPTWALK Walk;
        rc = PGMGstGetPage(pVCpu, (RTGCUINTPTR)GCPtrDst, &Walk);
        AssertMsgRCReturn(rc, ("GetPage failed with %Rrc for %RGv\n", rc, GCPtrDst), rc);
        RTGCPHYS const GCPhys = Walk.GCPhys | ((RTGCUINTPTR)GCPtrDst & GUEST_PAGE_OFFSET_MASK);

        /* Mention when we ignore X86_PTE_RW... */
        if (!(Walk.fEffective & X86_PTE_RW))
            Log(("PGMPhysWriteGCPtr: Writing to RO page %RGv %#x\n", GCPtrDst, cb));

        /* Mark the guest page as accessed and dirty if necessary. */
        if ((Walk.fEffective & (X86_PTE_A | X86_PTE_D)) != (X86_PTE_A | X86_PTE_D))
        {
            rc = PGMGstModifyPage(pVCpu, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D));
            AssertRC(rc);
        }

        return PGMPhysWrite(pVM, GCPhys, pvSrc, cb, enmOrigin);
    }

    /*
     * Page by page.
     */
    for (;;)
    {
        /* Convert virtual to physical address + flags */
        PGMPTWALK Walk;
        rc = PGMGstGetPage(pVCpu, (RTGCUINTPTR)GCPtrDst, &Walk);
        AssertMsgRCReturn(rc, ("GetPage failed with %Rrc for %RGv\n", rc, GCPtrDst), rc);
        RTGCPHYS const GCPhys = Walk.GCPhys | ((RTGCUINTPTR)GCPtrDst & GUEST_PAGE_OFFSET_MASK);

        /* Mention when we ignore X86_PTE_RW... */
        if (!(Walk.fEffective & X86_PTE_RW))
            Log(("PGMPhysWriteGCPtr: Writing to RO page %RGv %#x\n", GCPtrDst, cb));

        /* Mark the guest page as accessed and dirty if necessary. */
        if ((Walk.fEffective & (X86_PTE_A | X86_PTE_D)) != (X86_PTE_A | X86_PTE_D))
        {
            rc = PGMGstModifyPage(pVCpu, GCPtrDst, 1, X86_PTE_A | X86_PTE_D, ~(uint64_t)(X86_PTE_A | X86_PTE_D));
            AssertRC(rc);
        }

        /* copy */
        size_t cbWrite = GUEST_PAGE_SIZE - ((RTGCUINTPTR)GCPtrDst & GUEST_PAGE_OFFSET_MASK);
        if (cbWrite < cb)
        {
            VBOXSTRICTRC rcStrict = PGMPhysWrite(pVM, GCPhys, pvSrc, cbWrite, enmOrigin);
            if (RT_LIKELY(rcStrict == VINF_SUCCESS))
            { /* likely */ }
            else
                return rcStrict;
        }
        else    /* Last page (cbWrite is GUEST_PAGE_SIZE, we only need cb!) */
            return PGMPhysWrite(pVM, GCPhys, pvSrc, cb, enmOrigin);

        /* next */
        Assert(cb > cbWrite);
        cb         -= cbWrite;
        pvSrc       = (uint8_t *)pvSrc + cbWrite;
        GCPtrDst   += cbWrite;
    }
}


/**
 * Return the page type of the specified physical address.
 *
 * @returns The page type.
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          Guest physical address
 */
VMM_INT_DECL(PGMPAGETYPE) PGMPhysGetPageType(PVMCC pVM, RTGCPHYS GCPhys)
{
    PGM_LOCK_VOID(pVM);
    PPGMPAGE pPage = pgmPhysGetPage(pVM, GCPhys);
    PGMPAGETYPE enmPgType = pPage ? (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage) : PGMPAGETYPE_INVALID;
    PGM_UNLOCK(pVM);

    return enmPgType;
}


/** Helper for PGMPhysIemGCPhys2PtrNoLock. */
DECL_FORCE_INLINE(int)
pgmPhyIemGCphys2PtrNoLockReturnNoNothing(uint64_t uTlbPhysRev, R3R0PTRTYPE(uint8_t *) *ppb, uint64_t *pfTlb,
                                         RTGCPHYS GCPhys, PCPGMPAGE pPageCopy)
{
    *pfTlb |= uTlbPhysRev
           |  PGMIEMGCPHYS2PTR_F_NO_WRITE | PGMIEMGCPHYS2PTR_F_NO_READ | PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3;
    *ppb    = NULL;
    Log6(("PGMPhysIemGCPhys2PtrNoLock: GCPhys=%RGp *ppb=NULL *pfTlb=%#RX64 PageCopy=%R[pgmpage] NO\n", GCPhys,
          uTlbPhysRev |  PGMIEMGCPHYS2PTR_F_NO_WRITE | PGMIEMGCPHYS2PTR_F_NO_READ | PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3, pPageCopy));
    RT_NOREF(GCPhys, pPageCopy);
    return VINF_SUCCESS;
}


/** Helper for PGMPhysIemGCPhys2PtrNoLock. */
DECL_FORCE_INLINE(int)
pgmPhyIemGCphys2PtrNoLockReturnReadOnly(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uTlbPhysRev, RTGCPHYS GCPhys, PCPGMPAGE pPageCopy,
                                        PPGMRAMRANGE pRam, PPGMPAGE pPage, R3R0PTRTYPE(uint8_t *) *ppb, uint64_t *pfTlb)
{
    if (!PGM_PAGE_IS_CODE_PAGE(pPageCopy))
        *pfTlb |= uTlbPhysRev | PGMIEMGCPHYS2PTR_F_NO_WRITE;
    else
        *pfTlb |= uTlbPhysRev | PGMIEMGCPHYS2PTR_F_NO_WRITE | PGMIEMGCPHYS2PTR_F_CODE_PAGE;

#ifdef IN_RING3
    if (PGM_IS_IN_NEM_MODE(pVM))
        *ppb = &pRam->pbR3[(RTGCPHYS)(uintptr_t)(pPage - &pRam->aPages[0]) << GUEST_PAGE_SHIFT];
    else
#endif
    {
#ifdef IN_RING3
        PPGMPAGEMAPTLBE pTlbe;
        int rc = pgmPhysPageQueryLocklessTlbeWithPage(pVCpu, pPage, GCPhys, &pTlbe);
        AssertLogRelRCReturn(rc, rc);
        *ppb = (uint8_t *)pTlbe->pv;
        RT_NOREF(pVM);
#else /** @todo a safe lockless page TLB in ring-0 needs the to ensure it gets the right invalidations. later. */
        PGM_LOCK(pVM);
        PPGMPAGEMAPTLBE pTlbe;
        int rc = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pTlbe);
        AssertLogRelRCReturnStmt(rc, PGM_UNLOCK(pVM), rc);
        *ppb = (uint8_t *)pTlbe->pv;
        PGM_UNLOCK(pVM);
        RT_NOREF(pVCpu);
#endif
    }
    Log6(("PGMPhysIemGCPhys2PtrNoLock: GCPhys=%RGp *ppb=%p *pfTlb=%#RX64 PageCopy=%R[pgmpage] RO\n", GCPhys, *ppb, *pfTlb, pPageCopy));
    RT_NOREF(pRam);
    return VINF_SUCCESS;
}


/** Helper for PGMPhysIemGCPhys2PtrNoLock. */
DECL_FORCE_INLINE(int)
pgmPhyIemGCphys2PtrNoLockReturnReadWrite(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uTlbPhysRev, RTGCPHYS GCPhys, PCPGMPAGE pPageCopy,
                                         PPGMRAMRANGE pRam, PPGMPAGE pPage, R3R0PTRTYPE(uint8_t *) *ppb, uint64_t *pfTlb)
{
    Assert(!PGM_PAGE_IS_CODE_PAGE(pPageCopy));
    RT_NOREF(pPageCopy);
    *pfTlb |= uTlbPhysRev;

#ifdef IN_RING3
    if (PGM_IS_IN_NEM_MODE(pVM))
        *ppb = &pRam->pbR3[(RTGCPHYS)(uintptr_t)(pPage - &pRam->aPages[0]) << GUEST_PAGE_SHIFT];
    else
#endif
    {
#ifdef IN_RING3
        PPGMPAGEMAPTLBE pTlbe;
        int rc = pgmPhysPageQueryLocklessTlbeWithPage(pVCpu, pPage, GCPhys, &pTlbe);
        AssertLogRelRCReturn(rc, rc);
        *ppb = (uint8_t *)pTlbe->pv;
        RT_NOREF(pVM);
#else /** @todo a safe lockless page TLB in ring-0 needs the to ensure it gets the right invalidations. later. */
        PGM_LOCK(pVM);
        PPGMPAGEMAPTLBE pTlbe;
        int rc = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pTlbe);
        AssertLogRelRCReturnStmt(rc, PGM_UNLOCK(pVM), rc);
        *ppb = (uint8_t *)pTlbe->pv;
        PGM_UNLOCK(pVM);
        RT_NOREF(pVCpu);
#endif
    }
    Log6(("PGMPhysIemGCPhys2PtrNoLock: GCPhys=%RGp *ppb=%p *pfTlb=%#RX64 PageCopy=%R[pgmpage] RW\n", GCPhys, *ppb, *pfTlb, pPageCopy));
    RT_NOREF(pRam);
    return VINF_SUCCESS;
}


/**
 * Converts a GC physical address to a HC ring-3 pointer, with some
 * additional checks.
 *
 * @returns VBox status code (no informational statuses).
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   GCPhys          The GC physical address to convert.  This API mask
 *                          the A20 line when necessary.
 * @param   puTlbPhysRev    Where to read the physical TLB revision.  Needs to
 *                          be done while holding the PGM lock.
 * @param   ppb             Where to store the pointer corresponding to GCPhys
 *                          on success.
 * @param   pfTlb           The TLB flags and revision.  We only add stuff.
 *
 * @remarks This is more or a less a copy of PGMR3PhysTlbGCPhys2Ptr and
 *          PGMPhysIemGCPhys2Ptr.
 *
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(int) PGMPhysIemGCPhys2PtrNoLock(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, uint64_t const volatile *puTlbPhysRev,
                                             R3R0PTRTYPE(uint8_t *) *ppb, uint64_t *pfTlb)
{
    PGM_A20_APPLY_TO_VAR(pVCpu, GCPhys);
    Assert(!(GCPhys & X86_PAGE_OFFSET_MASK));

    PGMRAMRANGE volatile *pRam;
    PGMPAGE volatile     *pPage;
    int rc = pgmPhysGetPageAndRangeExLockless(pVM, pVCpu, GCPhys, &pPage, &pRam);
    if (RT_SUCCESS(rc))
    {
        /*
         * Wrt to update races, we will try to pretend we beat the update we're
         * racing.  We do this by sampling the physical TLB revision first, so
         * that the TLB entry / whatever purpose the caller has with the info
         * will become invalid immediately if it's updated.
         *
         * This means the caller will (probably) make use of the returned info
         * only once and then requery it the next time it is use, getting the
         * updated info. This would then be just as if the first query got the
         * PGM lock before the updater.
         */
        /** @todo make PGMPAGE updates more atomic, possibly flagging complex
         * updates by adding a u1UpdateInProgress field (or revision).
         * This would be especially important when updating the page ID...  */
        uint64_t uTlbPhysRev = *puTlbPhysRev;
        PGMPAGE  PageCopy    = { { pPage->au64[0], pPage->au64[1] } };
        if (   uTlbPhysRev      == *puTlbPhysRev
            && PageCopy.au64[0] == pPage->au64[0]
            && PageCopy.au64[1] == pPage->au64[1])
            ASMCompilerBarrier(); /* likely */
        else
        {
            PGM_LOCK_VOID(pVM);
            uTlbPhysRev = *puTlbPhysRev;
            PageCopy.au64[0] = pPage->au64[0];
            PageCopy.au64[1] = pPage->au64[1];
            PGM_UNLOCK(pVM);
        }

        /*
         * Try optimize for the regular case first: Writable RAM.
         */
        switch (PGM_PAGE_GET_HNDL_PHYS_STATE(&PageCopy))
        {
            case PGM_PAGE_HNDL_PHYS_STATE_DISABLED:
                if (!PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(&PageCopy))
                { /* likely */ }
                else
                    return pgmPhyIemGCphys2PtrNoLockReturnNoNothing(uTlbPhysRev, ppb, pfTlb, GCPhys, &PageCopy);
                RT_FALL_THRU();
            case PGM_PAGE_HNDL_PHYS_STATE_NONE:
                Assert(!PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(&PageCopy));
                switch (PGM_PAGE_GET_STATE_NA(&PageCopy))
                {
                    case PGM_PAGE_STATE_ALLOCATED:
                        return pgmPhyIemGCphys2PtrNoLockReturnReadWrite(pVM, pVCpu, uTlbPhysRev, GCPhys, &PageCopy,
                                                                       (PPGMRAMRANGE)pRam, (PPGMPAGE)pPage, ppb, pfTlb);

                    case PGM_PAGE_STATE_ZERO:
                    case PGM_PAGE_STATE_WRITE_MONITORED:
                    case PGM_PAGE_STATE_SHARED:
                        return pgmPhyIemGCphys2PtrNoLockReturnReadOnly(pVM, pVCpu, uTlbPhysRev, GCPhys, &PageCopy,
                                                                       (PPGMRAMRANGE)pRam, (PPGMPAGE)pPage, ppb, pfTlb);

                    default: AssertFailed(); RT_FALL_THROUGH();
                    case PGM_PAGE_STATE_BALLOONED:
                        return pgmPhyIemGCphys2PtrNoLockReturnNoNothing(uTlbPhysRev, ppb, pfTlb, GCPhys, &PageCopy);
                }
                break;

            case PGM_PAGE_HNDL_PHYS_STATE_WRITE:
                Assert(!PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(&PageCopy));
                switch (PGM_PAGE_GET_STATE_NA(&PageCopy))
                {
                    case PGM_PAGE_STATE_ALLOCATED:
                        Assert(!PGM_PAGE_IS_CODE_PAGE(&PageCopy));
                        RT_FALL_THRU();
                    case PGM_PAGE_STATE_ZERO:
                    case PGM_PAGE_STATE_WRITE_MONITORED:
                    case PGM_PAGE_STATE_SHARED:
                        return pgmPhyIemGCphys2PtrNoLockReturnReadOnly(pVM, pVCpu, uTlbPhysRev, GCPhys, &PageCopy,
                                                                       (PPGMRAMRANGE)pRam, (PPGMPAGE)pPage, ppb, pfTlb);

                    default: AssertFailed(); RT_FALL_THROUGH();
                    case PGM_PAGE_STATE_BALLOONED:
                        return pgmPhyIemGCphys2PtrNoLockReturnNoNothing(uTlbPhysRev, ppb, pfTlb, GCPhys, &PageCopy);
                }
                break;

            case PGM_PAGE_HNDL_PHYS_STATE_ALL:
                Assert(!PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(&PageCopy));
                return pgmPhyIemGCphys2PtrNoLockReturnNoNothing(uTlbPhysRev, ppb, pfTlb, GCPhys, &PageCopy);
        }
    }
    else
    {
        *pfTlb |= *puTlbPhysRev | PGMIEMGCPHYS2PTR_F_NO_WRITE | PGMIEMGCPHYS2PTR_F_NO_READ
               |  PGMIEMGCPHYS2PTR_F_NO_MAPPINGR3 | PGMIEMGCPHYS2PTR_F_UNASSIGNED;
        *ppb    = NULL;
        Log6(("PGMPhysIemGCPhys2PtrNoLock: GCPhys=%RGp *ppb=%p *pfTlb=%#RX64 (rc=%Rrc)\n", GCPhys, *ppb, *pfTlb, rc));
    }

    return VINF_SUCCESS;
}


/**
 * Converts a GC physical address to a HC ring-3 pointer, with some
 * additional checks.
 *
 * @returns VBox status code (no informational statuses).
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_TLB_CATCH_WRITE and *ppv set if the page has a write
 *          access handler of some kind.
 * @retval  VERR_PGM_PHYS_TLB_CATCH_ALL if the page has a handler catching all
 *          accesses or is odd in any way.
 * @retval  VERR_PGM_PHYS_TLB_UNASSIGNED if the page doesn't exist.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   GCPhys          The GC physical address to convert.  This API mask
 *                          the A20 line when necessary.
 * @param   fWritable       Whether write access is required.
 * @param   fByPassHandlers Whether to bypass access handlers.
 * @param   ppv             Where to store the pointer corresponding to GCPhys
 *                          on success.
 * @param   pLock
 *
 * @remarks This is more or a less a copy of PGMR3PhysTlbGCPhys2Ptr.
 * @thread  EMT(pVCpu).
 */
VMM_INT_DECL(int) PGMPhysIemGCPhys2Ptr(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, bool fWritable, bool fByPassHandlers,
                                       void **ppv, PPGMPAGEMAPLOCK pLock)
{
    PGM_A20_APPLY_TO_VAR(pVCpu, GCPhys);

    PGM_LOCK_VOID(pVM);

    PPGMRAMRANGE pRam;
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageAndRangeEx(pVM, GCPhys, &pPage, &pRam);
    if (RT_SUCCESS(rc))
    {
        if (PGM_PAGE_IS_BALLOONED(pPage))
            rc = VERR_PGM_PHYS_TLB_CATCH_WRITE;
        else if (PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(pPage))
            rc = VERR_PGM_PHYS_TLB_CATCH_ALL;
        else if (   !PGM_PAGE_HAS_ANY_HANDLERS(pPage)
                 || (fByPassHandlers && !PGM_PAGE_IS_MMIO(pPage)) )
            rc = VINF_SUCCESS;
        else
        {
            if (PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage)) /* catches MMIO */
            {
                Assert(!fByPassHandlers || PGM_PAGE_IS_MMIO(pPage));
                rc = VERR_PGM_PHYS_TLB_CATCH_ALL;
            }
            else if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) && fWritable)
            {
                Assert(!fByPassHandlers);
                rc = VERR_PGM_PHYS_TLB_CATCH_WRITE;
            }
        }
        if (RT_SUCCESS(rc))
        {
            int rc2;

            /* Make sure what we return is writable. */
            if (fWritable)
                switch (PGM_PAGE_GET_STATE(pPage))
                {
                    case PGM_PAGE_STATE_ALLOCATED:
                        break;
                    case PGM_PAGE_STATE_BALLOONED:
                        AssertFailed();
                        break;
                    case PGM_PAGE_STATE_ZERO:
                    case PGM_PAGE_STATE_SHARED:
                    case PGM_PAGE_STATE_WRITE_MONITORED:
                        rc2 = pgmPhysPageMakeWritable(pVM, pPage, GCPhys & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK);
                        AssertLogRelRCReturn(rc2, rc2);
                        break;
                }

            /* Get a ring-3 mapping of the address. */
            PPGMPAGEMAPTLBE pTlbe;
            rc2 = pgmPhysPageQueryTlbeWithPage(pVM, pPage, GCPhys, &pTlbe);
            AssertLogRelRCReturn(rc2, rc2);

            /* Lock it and calculate the address. */
            if (fWritable)
                pgmPhysPageMapLockForWriting(pVM, pPage, pTlbe, pLock);
            else
                pgmPhysPageMapLockForReading(pVM, pPage, pTlbe, pLock);
            *ppv = (void *)((uintptr_t)pTlbe->pv | (uintptr_t)(GCPhys & GUEST_PAGE_OFFSET_MASK));

            Log6(("PGMPhysIemGCPhys2Ptr: GCPhys=%RGp rc=%Rrc pPage=%R[pgmpage] *ppv=%p\n", GCPhys, rc, pPage, *ppv));
        }
        else
            Log6(("PGMPhysIemGCPhys2Ptr: GCPhys=%RGp rc=%Rrc pPage=%R[pgmpage]\n", GCPhys, rc, pPage));

        /* else: handler catching all access, no pointer returned. */
    }
    else
        rc = VERR_PGM_PHYS_TLB_UNASSIGNED;

    PGM_UNLOCK(pVM);
    return rc;
}


/**
 * Checks if the give GCPhys page requires special handling for the given access
 * because it's MMIO or otherwise monitored.
 *
 * @returns VBox status code (no informational statuses).
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_PGM_PHYS_TLB_CATCH_WRITE and *ppv set if the page has a write
 *          access handler of some kind.
 * @retval  VERR_PGM_PHYS_TLB_CATCH_ALL if the page has a handler catching all
 *          accesses or is odd in any way.
 * @retval  VERR_PGM_PHYS_TLB_UNASSIGNED if the page doesn't exist.
 *
 * @param   pVM             The cross context VM structure.
 * @param   GCPhys          The GC physical address to convert.  Since this is
 *                          only used for filling the REM TLB, the A20 mask must
 *                          be applied before calling this API.
 * @param   fWritable       Whether write access is required.
 * @param   fByPassHandlers Whether to bypass access handlers.
 *
 * @remarks This is a watered down version PGMPhysIemGCPhys2Ptr and really just
 *          a stop gap thing that should be removed once there is a better TLB
 *          for virtual address accesses.
 */
VMM_INT_DECL(int) PGMPhysIemQueryAccess(PVMCC pVM, RTGCPHYS GCPhys, bool fWritable, bool fByPassHandlers)
{
    PGM_LOCK_VOID(pVM);
    PGM_A20_ASSERT_MASKED(VMMGetCpu(pVM), GCPhys);

    PPGMRAMRANGE pRam;
    PPGMPAGE pPage;
    int rc = pgmPhysGetPageAndRangeEx(pVM, GCPhys, &pPage, &pRam);
    if (RT_SUCCESS(rc))
    {
        if (PGM_PAGE_IS_BALLOONED(pPage))
            rc = VERR_PGM_PHYS_TLB_CATCH_WRITE;
        else if (PGM_PAGE_IS_SPECIAL_ALIAS_MMIO(pPage))
            rc = VERR_PGM_PHYS_TLB_CATCH_ALL;
        else if (   !PGM_PAGE_HAS_ANY_HANDLERS(pPage)
                 || (fByPassHandlers && !PGM_PAGE_IS_MMIO(pPage)) )
            rc = VINF_SUCCESS;
        else
        {
            if (PGM_PAGE_HAS_ACTIVE_ALL_HANDLERS(pPage)) /* catches MMIO */
            {
                Assert(!fByPassHandlers || PGM_PAGE_IS_MMIO(pPage));
                rc = VERR_PGM_PHYS_TLB_CATCH_ALL;
            }
            else if (PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) && fWritable)
            {
                Assert(!fByPassHandlers);
                rc = VERR_PGM_PHYS_TLB_CATCH_WRITE;
            }
        }
    }

    PGM_UNLOCK(pVM);
    return rc;
}

#ifdef VBOX_WITH_NATIVE_NEM

/**
 * Interface used by NEM to check what to do on a memory access exit.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per virtual CPU structure.
 *                          Optional.
 * @param   GCPhys          The guest physical address.
 * @param   fMakeWritable   Whether to try make the page writable or not.  If it
 *                          cannot be made writable, NEM_PAGE_PROT_WRITE won't
 *                          be returned and the return code will be unaffected
 * @param   pInfo           Where to return the page information.  This is
 *                          initialized even on failure.
 * @param   pfnChecker      Page in-sync checker callback.  Optional.
 * @param   pvUser          User argument to pass to pfnChecker.
 */
VMM_INT_DECL(int) PGMPhysNemPageInfoChecker(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, bool fMakeWritable,
                                            PPGMPHYSNEMPAGEINFO pInfo, PFNPGMPHYSNEMCHECKPAGE pfnChecker, void *pvUser)
{
    PGM_LOCK_VOID(pVM);

    PPGMPAGE pPage;
    int rc = pgmPhysGetPageEx(pVM, GCPhys, &pPage);
    if (RT_SUCCESS(rc))
    {
        /* Try make it writable if requested. */
        pInfo->u2OldNemState = PGM_PAGE_GET_NEM_STATE(pPage);
        if (fMakeWritable)
            switch (PGM_PAGE_GET_STATE(pPage))
            {
                case PGM_PAGE_STATE_SHARED:
                case PGM_PAGE_STATE_WRITE_MONITORED:
                case PGM_PAGE_STATE_ZERO:
                    rc = pgmPhysPageMakeWritable(pVM, pPage, GCPhys);
                    if (rc == VERR_PGM_PHYS_PAGE_RESERVED)
                        rc = VINF_SUCCESS;
                    break;
            }

        /* Fill in the info. */
        pInfo->HCPhys       = PGM_PAGE_GET_HCPHYS(pPage);
        pInfo->u2NemState   = PGM_PAGE_GET_NEM_STATE(pPage);
        pInfo->fHasHandlers = PGM_PAGE_HAS_ACTIVE_HANDLERS(pPage) ? 1 : 0;
        PGMPAGETYPE const enmType = (PGMPAGETYPE)PGM_PAGE_GET_TYPE(pPage);
        pInfo->enmType      = enmType;
        pInfo->fNemProt     = pgmPhysPageCalcNemProtection(pPage, enmType);
        switch (PGM_PAGE_GET_STATE(pPage))
        {
            case PGM_PAGE_STATE_ALLOCATED:
                pInfo->fZeroPage = 0;
                break;

            case PGM_PAGE_STATE_ZERO:
                pInfo->fZeroPage = 1;
                break;

            case PGM_PAGE_STATE_WRITE_MONITORED:
                pInfo->fZeroPage = 0;
                break;

            case PGM_PAGE_STATE_SHARED:
                pInfo->fZeroPage = 0;
                break;

            case PGM_PAGE_STATE_BALLOONED:
                pInfo->fZeroPage = 1;
                break;

            default:
                pInfo->fZeroPage = 1;
                AssertFailedStmt(rc = VERR_PGM_PHYS_PAGE_GET_IPE);
        }

        /* Call the checker and update NEM state. */
        if (pfnChecker)
        {
            rc = pfnChecker(pVM, pVCpu, GCPhys, pInfo, pvUser);
            PGM_PAGE_SET_NEM_STATE(pPage, pInfo->u2NemState);
        }

        /* Done. */
        PGM_UNLOCK(pVM);
    }
    else
    {
        PGM_UNLOCK(pVM);

        pInfo->HCPhys       = NIL_RTHCPHYS;
        pInfo->fNemProt     = NEM_PAGE_PROT_NONE;
        pInfo->u2NemState   = 0;
        pInfo->fHasHandlers = 0;
        pInfo->fZeroPage    = 0;
        pInfo->enmType      = PGMPAGETYPE_INVALID;
    }

    return rc;
}


/**
 * NEM helper that performs @a pfnCallback on pages with NEM state @a uMinState
 * or higher.
 *
 * @returns VBox status code from callback.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context per CPU structure.  This is
 *                          optional as its only for passing to callback.
 * @param   uMinState       The minimum NEM state value to call on.
 * @param   pfnCallback     The callback function.
 * @param   pvUser          User argument for the callback.
 */
VMM_INT_DECL(int) PGMPhysNemEnumPagesByState(PVMCC pVM, PVMCPUCC pVCpu, uint8_t uMinState,
                                             PFNPGMPHYSNEMENUMCALLBACK pfnCallback, void *pvUser)
{
    /*
     * Just brute force this problem.
     */
    PGM_LOCK_VOID(pVM);
    int            rc             = VINF_SUCCESS;
    uint32_t const cLookupEntries = RT_MIN(pVM->pgm.s.RamRangeUnion.cLookupEntries, RT_ELEMENTS(pVM->pgm.s.aRamRangeLookup));
    for (uint32_t idxLookup = 0; idxLookup < cLookupEntries && RT_SUCCESS(rc); idxLookup++)
    {
        uint32_t const idRamRange = PGMRAMRANGELOOKUPENTRY_GET_ID(pVM->pgm.s.aRamRangeLookup[idxLookup]);
        AssertContinue(idRamRange < RT_ELEMENTS(pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges));
        PPGMRAMRANGE const pRam = pVM->CTX_EXPR(pgm, pgmr0, pgm).s.apRamRanges[idRamRange];
        AssertContinue(pRam);
        Assert(pRam->GCPhys == PGMRAMRANGELOOKUPENTRY_GET_FIRST(pVM->pgm.s.aRamRangeLookup[idxLookup]));

#ifdef IN_RING0
        uint32_t const cPages = RT_MIN(pRam->cb >> X86_PAGE_SHIFT, pVM->pgmr0.s.acRamRangePages[idRamRange]);
#else
        uint32_t const cPages = pRam->cb >> X86_PAGE_SHIFT;
#endif
        for (uint32_t iPage = 0; iPage < cPages; iPage++)
        {
            uint8_t u2State = PGM_PAGE_GET_NEM_STATE(&pRam->aPages[iPage]);
            if (u2State < uMinState)
            { /* likely */ }
            else
            {
                rc = pfnCallback(pVM, pVCpu, pRam->GCPhys + ((RTGCPHYS)iPage << X86_PAGE_SHIFT), &u2State, pvUser);
                if (RT_SUCCESS(rc))
                    PGM_PAGE_SET_NEM_STATE(&pRam->aPages[iPage], u2State);
                else
                    break;
            }
        }
    }
    PGM_UNLOCK(pVM);

    return rc;
}


/**
 * Helper for setting the NEM state for a range of pages.
 *
 * @param   paPages     Array of pages to modify.
 * @param   cPages      How many pages to modify.
 * @param   u2State     The new state value.
 */
DECLHIDDEN(void) pgmPhysSetNemStateForPages(PPGMPAGE paPages, RTGCPHYS cPages, uint8_t u2State)
{
    PPGMPAGE pPage = paPages;
    while (cPages-- > 0)
    {
        PGM_PAGE_SET_NEM_STATE(pPage, u2State);
        pPage++;
    }
}

#endif /* VBOX_WITH_NATIVE_NEM */

