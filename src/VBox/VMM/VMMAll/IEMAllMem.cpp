/* $Id: IEMAllMem.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Interpreted Execution Manager - Common Memory Routines.
 */

/*
 * Copyright (C) 2011-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP   LOG_GROUP_IEM_MEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/dbgf.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"
#ifdef VBOX_VMM_TARGET_X86
# include "target-x86/IEMAllTlbInline-x86.h"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(IEM_LOG_MEMORY_WRITES)
/** What IEM just wrote. */
uint8_t g_abIemWrote[256];
/** How much IEM just wrote. */
size_t g_cbIemWrote;
#endif


/** @name   Memory access.
 *
 * @{
 */

#ifndef IN_RING3
/**
 * Postpone a bounce buffer writeback to ring-3 after failure.
 *
 * We make the data accessible to ring-3 by importing the state into one of its
 * memory mapping bounce buffers.
 *
 * @note There can only be one buffer in play here, so we will trash the whole
 *       ring-3 buffer state when called, boldly ASSUMING that we won't be
 *       called a 2nd time w/o returning to ring-3.  The ASSUMPTION is that this
 *       is only used for INS and REP INS emulation.
 */
static VBOXSTRICTRC iemMemBounceBufferPostponeToRing3(PVMCPUCC pVCpu, unsigned iMemMap, VBOXSTRICTRC rcStrict,
                                                      uint32_t fPendingWriteAccess)
{
    Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_IEM)); /* Can only be called once! */

    /*
     * Copy the buffering info into ring-3 entry #0.
     */

    /* The mapping info. */
    pVCpu->iem.s.Core.aMemMappings[0].pv             = NULL;
    pVCpu->iem.s.Core.aMemMappings[0].fAccess        = ICORE(pVCpu).aMemMappings[iMemMap].fAccess | fPendingWriteAccess;

    /* The bounce buffer info. */
    pVCpu->iem.s.Core.aMemBbMappings[0].GCPhysFirst  = ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst;
    pVCpu->iem.s.Core.aMemBbMappings[0].GCPhysSecond = ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond;
    pVCpu->iem.s.Core.aMemBbMappings[0].cbFirst      = ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst;
    pVCpu->iem.s.Core.aMemBbMappings[0].cbSecond     = ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond;
    pVCpu->iem.s.Core.aMemBbMappings[0].fUnassigned  = ICORE(pVCpu).aMemBbMappings[iMemMap].fUnassigned;

    /* The data. */
    size_t cbToCopy = ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst + ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond;
    cbToCopy = RT_MIN(cbToCopy, sizeof(pVCpu->iem.s.Core.aBounceBuffers[0].ab));
    memcpy(pVCpu->iem.s.Core.aBounceBuffers[0].ab, ICORE(pVCpu).aBounceBuffers[iMemMap].ab, cbToCopy);

    /* Reset the other ring-3 mappings. */
    pVCpu->iem.s.Core.aMemMappings[1].fAccess = IEM_ACCESS_INVALID;
# if IEM_MAX_MEM_MAPPINGS > 2
    pVCpu->iem.s.Core.aMemMappings[2].fAccess = IEM_ACCESS_INVALID;
# endif

    /*
     * Tell ring-3 there is one pending buffer and set the FF that will force a return.
     */
    pVCpu->iem.s.Core.cActiveMappings = 1;
    VMCPU_FF_SET(pVCpu, VMCPU_FF_IEM);

    /*
     * Free the ring-0 buffer entry, pretending we've successfully committed it.
     */
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    Assert(ICORE(pVCpu).cActiveMappings != 0);
    ICORE(pVCpu).cActiveMappings--;

    pVCpu->iem.s.cPendingCommit += 1;

    return iemSetPassUpStatus(pVCpu, rcStrict);
}
#endif

/**
 * Commits a bounce buffer that needs writing back and unmaps it.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling thread.
 * @param   iMemMap         The index of the buffer to commit.
 * @param   fPostponeFail   Whether we can postpone writer failures to ring-3.
 *                          Always false in ring-3, obviously.
 */
static VBOXSTRICTRC iemMemBounceBufferCommitAndUnmap(PVMCPUCC pVCpu, unsigned iMemMap, bool fPostponeFail)
{
    Assert(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_BOUNCE_BUFFERED);
    Assert(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_TYPE_WRITE);
#ifdef IN_RING3
    Assert(!fPostponeFail);
    RT_NOREF_PV(fPostponeFail);
#endif

    /*
     * Do the writing.
     */
    PVMCC pVM = pVCpu->CTX_SUFF(pVM);
    if (!ICORE(pVCpu).aMemBbMappings[iMemMap].fUnassigned)
    {
        uint16_t const  cbFirst  = ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst;
        uint16_t const  cbSecond = ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond;
        uint8_t const  *pbBuf    = &ICORE(pVCpu).aBounceBuffers[iMemMap].ab[0];
        if (!(ICORE(pVCpu).fExec & IEM_F_BYPASS_HANDLERS))
        {
            /*
             * Carefully and efficiently dealing with access handler return
             * codes make this a little bloated.
             */
            VBOXSTRICTRC rcStrict = PGMPhysWrite(pVM,
                                                 ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst,
                                                 pbBuf,
                                                 cbFirst,
                                                 PGMACCESSORIGIN_IEM);
            if (rcStrict == VINF_SUCCESS)
            {
                if (cbSecond)
                {
                    rcStrict = PGMPhysWrite(pVM,
                                            ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond,
                                            pbBuf + cbFirst,
                                            cbSecond,
                                            PGMACCESSORIGIN_IEM);
                    if (rcStrict == VINF_SUCCESS)
                    { /* nothing */ }
                    else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x GCPhysSecond=%RGp/%#x %Rrc\n",
                              ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst,
                              ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, VBOXSTRICTRC_VAL(rcStrict) ));
                        rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                    }
#ifndef IN_RING3
                    else if (fPostponeFail)
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x GCPhysSecond=%RGp/%#x %Rrc (postponed)\n",
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst,
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, VBOXSTRICTRC_VAL(rcStrict) ));
                        return iemMemBounceBufferPostponeToRing3(pVCpu, iMemMap, rcStrict, IEM_ACCESS_PENDING_R3_WRITE_2ND);
                    }
#endif
                    else
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x GCPhysSecond=%RGp/%#x %Rrc (!!)\n",
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst,
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, VBOXSTRICTRC_VAL(rcStrict) ));
                        return rcStrict;
                    }
                }
            }
            else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
            {
                if (!cbSecond)
                {
                    LogEx(LOG_GROUP_IEM,
                          ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x %Rrc\n",
                           ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst, VBOXSTRICTRC_VAL(rcStrict) ));
                    rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                }
                else
                {
                    VBOXSTRICTRC rcStrict2 = PGMPhysWrite(pVM,
                                                          ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond,
                                                          pbBuf + cbFirst,
                                                          cbSecond,
                                                          PGMACCESSORIGIN_IEM);
                    if (rcStrict2 == VINF_SUCCESS)
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x %Rrc GCPhysSecond=%RGp/%#x\n",
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst, VBOXSTRICTRC_VAL(rcStrict),
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond));
                        rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                    }
                    else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict2))
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x %Rrc GCPhysSecond=%RGp/%#x %Rrc\n",
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst, VBOXSTRICTRC_VAL(rcStrict),
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, VBOXSTRICTRC_VAL(rcStrict2) ));
                        PGM_PHYS_RW_DO_UPDATE_STRICT_RC(rcStrict, rcStrict2);
                        rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                    }
#ifndef IN_RING3
                    else if (fPostponeFail)
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x GCPhysSecond=%RGp/%#x %Rrc (postponed)\n",
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst,
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, VBOXSTRICTRC_VAL(rcStrict) ));
                        return iemMemBounceBufferPostponeToRing3(pVCpu, iMemMap, rcStrict, IEM_ACCESS_PENDING_R3_WRITE_2ND);
                    }
#endif
                    else
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x %Rrc GCPhysSecond=%RGp/%#x %Rrc (!!)\n",
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst, VBOXSTRICTRC_VAL(rcStrict),
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, VBOXSTRICTRC_VAL(rcStrict2) ));
                        return rcStrict2;
                    }
                }
            }
#ifndef IN_RING3
            else if (fPostponeFail)
            {
                LogEx(LOG_GROUP_IEM,
                      ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x GCPhysSecond=%RGp/%#x %Rrc (postponed)\n",
                       ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst,
                       ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, VBOXSTRICTRC_VAL(rcStrict) ));
                return iemMemBounceBufferPostponeToRing3(pVCpu, iMemMap, rcStrict,
                                                            IEM_ACCESS_PENDING_R3_WRITE_1ST
                                                         | (!cbSecond ? IEM_ACCESS_PENDING_R3_WRITE_2ND : 0));
            }
#endif
            else
            {
                LogEx(LOG_GROUP_IEM,
                      ("iemMemBounceBufferCommitAndUnmap: PGMPhysWrite GCPhysFirst=%RGp/%#x %Rrc [GCPhysSecond=%RGp/%#x] (!!)\n",
                       ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst, VBOXSTRICTRC_VAL(rcStrict),
                       ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond));
                return rcStrict;
            }
        }
        else
        {
            /*
             * No access handlers, much simpler.
             */
            int rc = PGMPhysSimpleWriteGCPhys(pVM, ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, pbBuf, cbFirst);
            if (RT_SUCCESS(rc))
            {
                if (cbSecond)
                {
                    rc = PGMPhysSimpleWriteGCPhys(pVM, ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, pbBuf + cbFirst, cbSecond);
                    if (RT_SUCCESS(rc))
                    { /* likely */ }
                    else
                    {
                        LogEx(LOG_GROUP_IEM,
                              ("iemMemBounceBufferCommitAndUnmap: PGMPhysSimpleWriteGCPhys GCPhysFirst=%RGp/%#x GCPhysSecond=%RGp/%#x %Rrc (!!)\n",
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst,
                               ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond, rc));
                        return rc;
                    }
                }
            }
            else
            {
                LogEx(LOG_GROUP_IEM,
                      ("iemMemBounceBufferCommitAndUnmap: PGMPhysSimpleWriteGCPhys GCPhysFirst=%RGp/%#x %Rrc [GCPhysSecond=%RGp/%#x] (!!)\n",
                       ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst, rc,
                       ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond));
                return rc;
            }
        }
    }

#if defined(IEM_LOG_MEMORY_WRITES)
    Log5(("IEM Wrote %RGp: %.*Rhxs\n", ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst,
          RT_MAX(RT_MIN(ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst, 64), 1), &ICORE(pVCpu).aBounceBuffers[iMemMap].ab[0]));
    if (ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond)
        Log5(("IEM Wrote %RGp: %.*Rhxs [2nd page]\n", ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond,
              RT_MIN(ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond, 64),
              &ICORE(pVCpu).aBounceBuffers[iMemMap].ab[ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst]));

    size_t cbWrote = ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst + ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond;
    g_cbIemWrote = cbWrote;
    memcpy(g_abIemWrote, &ICORE(pVCpu).aBounceBuffers[iMemMap].ab[0], RT_MIN(cbWrote, sizeof(g_abIemWrote)));
#endif

    /*
     * Free the mapping entry.
     */
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    Assert(ICORE(pVCpu).cActiveMappings != 0);
    ICORE(pVCpu).cActiveMappings--;
    return VINF_SUCCESS;
}


/**
 * iemMemMap worker that deals with a request crossing pages.
 */
VBOXSTRICTRC iemMemBounceBufferMapCrossPage(PVMCPUCC pVCpu, int iMemMap, void **ppvMem, uint8_t *pbUnmapInfo,
                                            size_t cbMem, RTGCPTR GCPtrFirst, uint32_t fAccess) RT_NOEXCEPT
{
    STAM_COUNTER_INC(&pVCpu->iem.s.StatMemBounceBufferCrossPage);
    Assert(cbMem <= GUEST_PAGE_SIZE);

    /*
     * Do the address translations.
     */
    uint32_t const cbFirstPage  = GUEST_PAGE_SIZE - (uint32_t)(GCPtrFirst & GUEST_PAGE_OFFSET_MASK);
    RTGCPHYS GCPhysFirst;
    VBOXSTRICTRC rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, GCPtrFirst, cbFirstPage, fAccess, &GCPhysFirst);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    Assert((GCPhysFirst & GUEST_PAGE_OFFSET_MASK) == (GCPtrFirst & GUEST_PAGE_OFFSET_MASK));

    uint32_t const cbSecondPage = (uint32_t)cbMem - cbFirstPage;
    RTGCPHYS GCPhysSecond;
    rcStrict = iemMemPageTranslateAndCheckAccess(pVCpu, (GCPtrFirst + (cbMem - 1)) & ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK,
                                                 cbSecondPage, fAccess, &GCPhysSecond);
    if (rcStrict != VINF_SUCCESS)
        return rcStrict;
    Assert((GCPhysSecond & GUEST_PAGE_OFFSET_MASK) == 0);
    GCPhysSecond &= ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK; /** @todo why? */

    PVMCC pVM = pVCpu->CTX_SUFF(pVM);

    /*
     * Check for data breakpoints.
     */
    if (RT_LIKELY(!(ICORE(pVCpu).fExec & IEM_F_PENDING_BRK_DATA)))
    { /* likely */ }
    else
    {
        uint32_t fDataBps = iemMemCheckDataBreakpoint(pVM, pVCpu, GCPtrFirst, cbFirstPage, fAccess);
        fDataBps         |= iemMemCheckDataBreakpoint(pVM, pVCpu, (GCPtrFirst + (cbMem - 1)) & ~(RTGCPTR)GUEST_PAGE_OFFSET_MASK,
                                                      cbSecondPage, fAccess);
#ifdef VBOX_VMM_TARGET_X86
        pVCpu->cpum.GstCtx.eflags.uBoth |= fDataBps & (CPUMCTX_DBG_HIT_DRX_MASK | CPUMCTX_DBG_DBGF_MASK);
        if (fDataBps > 1)
            LogEx(LOG_GROUP_IEM, ("iemMemBounceBufferMapCrossPage: Data breakpoint: fDataBps=%#x for %RGv LB %zx; fAccess=%#x cs:rip=%04x:%08RX64\n",
                                  fDataBps, GCPtrFirst, cbMem, fAccess, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip));
#elif defined(VBOX_VMM_TARGET_ARMV8)
        AssertFailed(); RT_NOREF(fDataBps); /** @todo ARMv8/IEM: implement data breakpoints. */
#else
# error "port me"
#endif
    }

    /*
     * Read in the current memory content if it's a read, execute or partial
     * write access.
     */
    uint8_t * const pbBuf = &ICORE(pVCpu).aBounceBuffers[iMemMap].ab[0];

    if (fAccess & (IEM_ACCESS_TYPE_READ | IEM_ACCESS_TYPE_EXEC | IEM_ACCESS_PARTIAL_WRITE))
    {
        if (!(ICORE(pVCpu).fExec & IEM_F_BYPASS_HANDLERS))
        {
            /*
             * Must carefully deal with access handler status codes here,
             * makes the code a bit bloated.
             */
            rcStrict = PGMPhysRead(pVM, GCPhysFirst, pbBuf, cbFirstPage, PGMACCESSORIGIN_IEM);
            if (rcStrict == VINF_SUCCESS)
            {
                rcStrict = PGMPhysRead(pVM, GCPhysSecond, pbBuf + cbFirstPage, cbSecondPage, PGMACCESSORIGIN_IEM);
                if (rcStrict == VINF_SUCCESS)
                { /*likely */ }
                else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
                    rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                else
                {
                    LogEx(LOG_GROUP_IEM, ("iemMemBounceBufferMapPhys: PGMPhysRead GCPhysSecond=%RGp rcStrict2=%Rrc (!!)\n",
                                          GCPhysSecond, VBOXSTRICTRC_VAL(rcStrict) ));
                    return rcStrict;
                }
            }
            else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
            {
                VBOXSTRICTRC rcStrict2 = PGMPhysRead(pVM, GCPhysSecond, pbBuf + cbFirstPage, cbSecondPage, PGMACCESSORIGIN_IEM);
                if (PGM_PHYS_RW_IS_SUCCESS(rcStrict2))
                {
                    PGM_PHYS_RW_DO_UPDATE_STRICT_RC(rcStrict, rcStrict2);
                    rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                }
                else
                {
                    LogEx(LOG_GROUP_IEM,
                          ("iemMemBounceBufferMapPhys: PGMPhysRead GCPhysSecond=%RGp rcStrict2=%Rrc (rcStrict=%Rrc) (!!)\n",
                           GCPhysSecond, VBOXSTRICTRC_VAL(rcStrict2), VBOXSTRICTRC_VAL(rcStrict2) ));
                    return rcStrict2;
                }
            }
            else
            {
                LogEx(LOG_GROUP_IEM, ("iemMemBounceBufferMapPhys: PGMPhysRead GCPhysFirst=%RGp rcStrict=%Rrc (!!)\n",
                                      GCPhysFirst, VBOXSTRICTRC_VAL(rcStrict) ));
                return rcStrict;
            }
        }
        else
        {
            /*
             * No informational status codes here, much more straight forward.
             */
            int rc = PGMPhysSimpleReadGCPhys(pVM, pbBuf, GCPhysFirst, cbFirstPage);
            if (RT_SUCCESS(rc))
            {
                Assert(rc == VINF_SUCCESS);
                rc = PGMPhysSimpleReadGCPhys(pVM, pbBuf + cbFirstPage, GCPhysSecond, cbSecondPage);
                if (RT_SUCCESS(rc))
                    Assert(rc == VINF_SUCCESS);
                else
                {
                    LogEx(LOG_GROUP_IEM,
                          ("iemMemBounceBufferMapPhys: PGMPhysSimpleReadGCPhys GCPhysSecond=%RGp rc=%Rrc (!!)\n", GCPhysSecond, rc));
                    return rc;
                }
            }
            else
            {
                LogEx(LOG_GROUP_IEM,
                      ("iemMemBounceBufferMapPhys: PGMPhysSimpleReadGCPhys GCPhysFirst=%RGp rc=%Rrc (!!)\n", GCPhysFirst, rc));
                return rc;
            }
        }
    }
#ifdef VBOX_STRICT
    else
        memset(pbBuf, 0xcc, cbMem);
    if (cbMem < sizeof(ICORE(pVCpu).aBounceBuffers[iMemMap].ab))
        memset(pbBuf + cbMem, 0xaa, sizeof(ICORE(pVCpu).aBounceBuffers[iMemMap].ab) - cbMem);
#endif
#if !defined(IN_RING0) || !defined(__GNUC__) /** @todo fix the macro... -Winvalid-offsetof issue. */
    AssertCompileMemberAlignment(VMCPUCC, IEM_CORE_MEMBER.aBounceBuffers, 64);
#endif

    /*
     * Commit the bounce buffer entry.
     */
    ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst    = GCPhysFirst;
    ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond   = GCPhysSecond;
    ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst        = (uint16_t)cbFirstPage;
    ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond       = (uint16_t)cbSecondPage;
    ICORE(pVCpu).aMemBbMappings[iMemMap].fUnassigned    = false;
    ICORE(pVCpu).aMemMappings[iMemMap].pv               = pbBuf;
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess          = fAccess | IEM_ACCESS_BOUNCE_BUFFERED;
    ICORE(pVCpu).iNextMapping = iMemMap + 1;
    ICORE(pVCpu).cActiveMappings++;

    *ppvMem = pbBuf;
    *pbUnmapInfo = iMemMap | 0x08 | ((fAccess & IEM_ACCESS_TYPE_MASK) << 4);
    return VINF_SUCCESS;
}


/**
 * iemMemMap woker that deals with iemMemPageMap failures.
 */
VBOXSTRICTRC iemMemBounceBufferMapPhys(PVMCPUCC pVCpu, unsigned iMemMap, void **ppvMem, uint8_t *pbUnmapInfo, size_t cbMem,
                                       RTGCPHYS GCPhysFirst, uint32_t fAccess, VBOXSTRICTRC rcMap) RT_NOEXCEPT
{
    STAM_COUNTER_INC(&pVCpu->iem.s.StatMemBounceBufferMapPhys);

    /*
     * Filter out conditions we can handle and the ones which shouldn't happen.
     */
    if (   rcMap != VERR_PGM_PHYS_TLB_CATCH_WRITE
        && rcMap != VERR_PGM_PHYS_TLB_CATCH_ALL
        && rcMap != VERR_PGM_PHYS_TLB_UNASSIGNED)
    {
        AssertReturn(RT_FAILURE_NP(rcMap), VERR_IEM_IPE_8);
        return rcMap;
    }
    ICORE(pVCpu).cPotentialExits++;

    /*
     * Read in the current memory content if it's a read, execute or partial
     * write access.
     */
    uint8_t *pbBuf = &ICORE(pVCpu).aBounceBuffers[iMemMap].ab[0];
    if (fAccess & (IEM_ACCESS_TYPE_READ | IEM_ACCESS_TYPE_EXEC | IEM_ACCESS_PARTIAL_WRITE))
    {
        if (rcMap == VERR_PGM_PHYS_TLB_UNASSIGNED)
            memset(pbBuf, 0xff, cbMem);
        else
        {
            int rc;
            if (!(ICORE(pVCpu).fExec & IEM_F_BYPASS_HANDLERS))
            {
                VBOXSTRICTRC rcStrict = PGMPhysRead(pVCpu->CTX_SUFF(pVM), GCPhysFirst, pbBuf, cbMem, PGMACCESSORIGIN_IEM);
                if (rcStrict == VINF_SUCCESS)
                { /* nothing */ }
                else if (PGM_PHYS_RW_IS_SUCCESS(rcStrict))
                    rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                else
                {
                    LogEx(LOG_GROUP_IEM, ("iemMemBounceBufferMapPhys: PGMPhysRead GCPhysFirst=%RGp rcStrict=%Rrc (!!)\n",
                                          GCPhysFirst, VBOXSTRICTRC_VAL(rcStrict) ));
                    return rcStrict;
                }
            }
            else
            {
                rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), pbBuf, GCPhysFirst, cbMem);
                if (RT_SUCCESS(rc))
                { /* likely */ }
                else
                {
                    LogEx(LOG_GROUP_IEM, ("iemMemBounceBufferMapPhys: PGMPhysSimpleReadGCPhys GCPhysFirst=%RGp rcStrict=%Rrc (!!)\n",
                                          GCPhysFirst, rc));
                    return rc;
                }
            }
        }
    }
#ifdef VBOX_STRICT
    else
        memset(pbBuf, 0xcc, cbMem);
#endif
#ifdef VBOX_STRICT
    if (cbMem < sizeof(ICORE(pVCpu).aBounceBuffers[iMemMap].ab))
        memset(pbBuf + cbMem, 0xaa, sizeof(ICORE(pVCpu).aBounceBuffers[iMemMap].ab) - cbMem);
#endif

    /*
     * Commit the bounce buffer entry.
     */
    ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst    = GCPhysFirst;
    ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond   = NIL_RTGCPHYS;
    ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst        = (uint16_t)cbMem;
    ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond       = 0;
    ICORE(pVCpu).aMemBbMappings[iMemMap].fUnassigned    = rcMap == VERR_PGM_PHYS_TLB_UNASSIGNED;
    ICORE(pVCpu).aMemMappings[iMemMap].pv               = pbBuf;
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess          = fAccess | IEM_ACCESS_BOUNCE_BUFFERED;
    ICORE(pVCpu).iNextMapping = iMemMap + 1;
    ICORE(pVCpu).cActiveMappings++;

    *ppvMem = pbBuf;
    *pbUnmapInfo = iMemMap | 0x08 | ((fAccess & IEM_ACCESS_TYPE_MASK) << 4);
    return VINF_SUCCESS;
}



/**
 * Commits the guest memory if bounce buffered and unmaps it.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   bUnmapInfo          Unmap info set by iemMemMap.
 */
VBOXSTRICTRC iemMemCommitAndUnmap(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT
{
    uintptr_t const iMemMap = bUnmapInfo & 0x7;
    AssertMsgReturn(   (bUnmapInfo & 0x08)
                    && iMemMap < RT_ELEMENTS(ICORE(pVCpu).aMemMappings)
                    && (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & (IEM_ACCESS_TYPE_MASK | 0xf)) == ((unsigned)bUnmapInfo >> 4),
                    ("%#x fAccess=%#x\n", bUnmapInfo, ICORE(pVCpu).aMemMappings[iMemMap].fAccess),
                    VERR_NOT_FOUND);

    /* If it's bounce buffered, we may need to write back the buffer. */
    if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_BOUNCE_BUFFERED)
    {
        if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_TYPE_WRITE)
            return iemMemBounceBufferCommitAndUnmap(pVCpu, iMemMap, false /*fPostponeFail*/);
    }
    /* Otherwise unlock it. */
    else if (!(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_NOT_LOCKED))
        PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), &ICORE(pVCpu).aMemMappingLocks[iMemMap].Lock);

    /* Free the entry. */
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    Assert(ICORE(pVCpu).cActiveMappings != 0);
    ICORE(pVCpu).cActiveMappings--;
    return VINF_SUCCESS;
}


/**
 * Rolls back the guest memory (conceptually only) and unmaps it.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   bUnmapInfo          Unmap info set by iemMemMap.
 */
void iemMemRollbackAndUnmap(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT
{
    uintptr_t const iMemMap = bUnmapInfo & 0x7;
    AssertMsgReturnVoid(   (bUnmapInfo & 0x08)
                        && iMemMap < RT_ELEMENTS(ICORE(pVCpu).aMemMappings)
                        &&    (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & (IEM_ACCESS_TYPE_MASK | 0xf))
                           == ((unsigned)bUnmapInfo >> 4),
                        ("%#x fAccess=%#x\n", bUnmapInfo, ICORE(pVCpu).aMemMappings[iMemMap].fAccess));

    /* Unlock it if necessary. */
    if (!(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_NOT_LOCKED))
        PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), &ICORE(pVCpu).aMemMappingLocks[iMemMap].Lock);

    /* Free the entry. */
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    Assert(ICORE(pVCpu).cActiveMappings != 0);
    ICORE(pVCpu).cActiveMappings--;
}


/**
 * Commits the guest memory if bounce buffered and unmaps it, longjmp on error.
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   bUnmapInfo          Unmap info set by iemMemMap.
 */
void iemMemCommitAndUnmapJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
    uintptr_t const iMemMap = bUnmapInfo & 0x7;
    AssertMsgReturnVoid(   (bUnmapInfo & 0x08)
                        && iMemMap < RT_ELEMENTS(ICORE(pVCpu).aMemMappings)
                        &&    (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & (IEM_ACCESS_TYPE_MASK | 0xf))
                           == ((unsigned)bUnmapInfo >> 4),
                        ("%#x fAccess=%#x\n", bUnmapInfo, ICORE(pVCpu).aMemMappings[iMemMap].fAccess));

    /* If it's bounce buffered, we may need to write back the buffer. */
    if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_BOUNCE_BUFFERED)
    {
        if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_TYPE_WRITE)
        {
            VBOXSTRICTRC rcStrict = iemMemBounceBufferCommitAndUnmap(pVCpu, iMemMap, false /*fPostponeFail*/);
            if (rcStrict == VINF_SUCCESS)
                return;
            IEM_DO_LONGJMP(pVCpu, VBOXSTRICTRC_VAL(rcStrict));
        }
    }
    /* Otherwise unlock it. */
    else if (!(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_NOT_LOCKED))
        PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), &ICORE(pVCpu).aMemMappingLocks[iMemMap].Lock);

    /* Free the entry. */
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    Assert(ICORE(pVCpu).cActiveMappings != 0);
    ICORE(pVCpu).cActiveMappings--;
}


/** Fallback for iemMemCommitAndUnmapRwJmp.  */
void iemMemCommitAndUnmapRwSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(((bUnmapInfo >> 4) & IEM_ACCESS_TYPE_MASK) == (IEM_ACCESS_TYPE_READ | IEM_ACCESS_TYPE_WRITE));
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
}


/** Fallback for iemMemCommitAndUnmapAtJmp.  */
void iemMemCommitAndUnmapAtSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(((bUnmapInfo >> 4) & IEM_ACCESS_TYPE_MASK) == (IEM_ACCESS_TYPE_READ | IEM_ACCESS_TYPE_WRITE));
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
}


/** Fallback for iemMemCommitAndUnmapWoJmp.  */
void iemMemCommitAndUnmapWoSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(((bUnmapInfo >> 4) & IEM_ACCESS_TYPE_MASK) == IEM_ACCESS_TYPE_WRITE);
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
}


/** Fallback for iemMemCommitAndUnmapRoJmp.  */
void iemMemCommitAndUnmapRoSafeJmp(PVMCPUCC pVCpu, uint8_t bUnmapInfo) IEM_NOEXCEPT_MAY_LONGJMP
{
    Assert(((bUnmapInfo >> 4) & IEM_ACCESS_TYPE_MASK) == IEM_ACCESS_TYPE_READ);
    iemMemCommitAndUnmapJmp(pVCpu, bUnmapInfo);
}


/** Fallback for iemMemRollbackAndUnmapWo.  */
void iemMemRollbackAndUnmapWoSafe(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT
{
    Assert(((bUnmapInfo >> 4) & IEM_ACCESS_TYPE_MASK) == IEM_ACCESS_TYPE_WRITE);
    iemMemRollbackAndUnmap(pVCpu, bUnmapInfo);
}


#ifndef IN_RING3
/**
 * Commits the guest memory if bounce buffered and unmaps it, if any bounce
 * buffer part shows trouble it will be postponed to ring-3 (sets FF and stuff).
 *
 * Allows the instruction to be completed and retired, while the IEM user will
 * return to ring-3 immediately afterwards and do the postponed writes there.
 *
 * @returns VBox status code (no strict statuses).  Caller must check
 *          VMCPU_FF_IEM before repeating string instructions and similar stuff.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   pvMem               The mapping.
 * @param   fAccess             The kind of access.
 *
 * @note    Can only be used on 1 - one - memory bounce buffer.  The code up in
 *          iemMemBounceBufferPostponeToRing3() is not ready for more.
 */
VBOXSTRICTRC iemMemCommitAndUnmapPostponeTroubleToR3(PVMCPUCC pVCpu, uint8_t bUnmapInfo) RT_NOEXCEPT
{
    uintptr_t const iMemMap = bUnmapInfo & 0x7;
    AssertMsgReturn(   (bUnmapInfo & 0x08)
                    && iMemMap < RT_ELEMENTS(ICORE(pVCpu).aMemMappings)
                    &&    (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & (IEM_ACCESS_TYPE_MASK | 0xf))
                       == ((unsigned)bUnmapInfo >> 4),
                    ("%#x fAccess=%#x\n", bUnmapInfo, ICORE(pVCpu).aMemMappings[iMemMap].fAccess),
                    VERR_NOT_FOUND);

    /* If it's bounce buffered, we may need to write back the buffer. */
    if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_BOUNCE_BUFFERED)
    {
        if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_TYPE_WRITE)
            return iemMemBounceBufferCommitAndUnmap(pVCpu, iMemMap, true /*fPostponeFail*/);
    }
    /* Otherwise unlock it. */
    else if (!(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_NOT_LOCKED))
        PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), &ICORE(pVCpu).aMemMappingLocks[iMemMap].Lock);

    /* Free the entry. */
    ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    Assert(ICORE(pVCpu).cActiveMappings != 0);
    ICORE(pVCpu).cActiveMappings--;
    return VINF_SUCCESS;
}
#endif


/**
 * Rollbacks mappings, releasing page locks and such.
 *
 * The caller shall only call this after checking cActiveMappings.
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling thread.
 */
void iemMemRollback(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Assert(ICORE(pVCpu).cActiveMappings > 0);

    uint32_t iMemMap = RT_ELEMENTS(ICORE(pVCpu).aMemMappings);
    while (iMemMap-- > 0)
    {
        uint32_t const fAccess = ICORE(pVCpu).aMemMappings[iMemMap].fAccess;
        if (fAccess != IEM_ACCESS_INVALID)
        {
            AssertMsg(!(fAccess & ~IEM_ACCESS_VALID_MASK) && fAccess != 0, ("%#x\n", fAccess));
            ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
            if (!(fAccess & (IEM_ACCESS_BOUNCE_BUFFERED | IEM_ACCESS_NOT_LOCKED)))
                PGMPhysReleasePageMappingLock(pVCpu->CTX_SUFF(pVM), &ICORE(pVCpu).aMemMappingLocks[iMemMap].Lock);
            AssertMsg(ICORE(pVCpu).cActiveMappings > 0,
                      ("iMemMap=%u fAccess=%#x pv=%p GCPhysFirst=%RGp GCPhysSecond=%RGp\n",
                       iMemMap, fAccess, ICORE(pVCpu).aMemMappings[iMemMap].pv,
                       ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond));
            ICORE(pVCpu).cActiveMappings--;
        }
    }
}

#undef  LOG_GROUP
#define LOG_GROUP LOG_GROUP_IEM

/** @} */


#ifdef IN_RING3

/**
 * Handles the unlikely and probably fatal merge cases.
 *
 * @returns Merged status code.
 * @param   rcStrict        Current EM status code.
 * @param   rcStrictCommit  The IOM I/O or MMIO write commit status to merge
 *                          with @a rcStrict.
 * @param   iMemMap         The memory mapping index. For error reporting only.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          thread, for error reporting only.
 */
DECL_NO_INLINE(static, VBOXSTRICTRC) iemR3MergeStatusSlow(VBOXSTRICTRC rcStrict, VBOXSTRICTRC rcStrictCommit,
                                                          unsigned iMemMap, PVMCPUCC pVCpu)
{
    if (RT_FAILURE_NP(rcStrict))
        return rcStrict;

    if (RT_FAILURE_NP(rcStrictCommit))
        return rcStrictCommit;

    if (rcStrict == rcStrictCommit)
        return rcStrictCommit;

    AssertLogRelMsgFailed(("rcStrictCommit=%Rrc rcStrict=%Rrc iMemMap=%u fAccess=%#x FirstPg=%RGp LB %u SecondPg=%RGp LB %u\n",
                           VBOXSTRICTRC_VAL(rcStrictCommit), VBOXSTRICTRC_VAL(rcStrict), iMemMap,
                           ICORE(pVCpu).aMemMappings[iMemMap].fAccess,
                           ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst,
                           ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond));
    return VERR_IOM_FF_STATUS_IPE;
}


/**
 * Helper for IOMR3ProcessForceFlag.
 *
 * @returns Merged status code.
 * @param   rcStrict        Current EM status code.
 * @param   rcStrictCommit  The IOM I/O or MMIO write commit status to merge
 *                          with @a rcStrict.
 * @param   iMemMap         The memory mapping index. For error reporting only.
 * @param   pVCpu           The cross context virtual CPU structure of the calling
 *                          thread, for error reporting only.
 */
DECLINLINE(VBOXSTRICTRC) iemR3MergeStatus(VBOXSTRICTRC rcStrict, VBOXSTRICTRC rcStrictCommit, unsigned iMemMap, PVMCPUCC pVCpu)
{
    /* Simple. */
    if (RT_LIKELY(rcStrict == VINF_SUCCESS || rcStrict == VINF_EM_RAW_TO_R3))
        return rcStrictCommit;

    if (RT_LIKELY(rcStrictCommit == VINF_SUCCESS))
        return rcStrict;

    /* EM scheduling status codes. */
    if (RT_LIKELY(   rcStrict >= VINF_EM_FIRST
                  && rcStrict <= VINF_EM_LAST))
    {
        if (RT_LIKELY(   rcStrictCommit >= VINF_EM_FIRST
                      && rcStrictCommit <= VINF_EM_LAST))
            return rcStrict < rcStrictCommit ? rcStrict : rcStrictCommit;
    }

    /* Unlikely */
    return iemR3MergeStatusSlow(rcStrict, rcStrictCommit, iMemMap, pVCpu);
}


/**
 * Called by force-flag handling code when VMCPU_FF_IEM is set.
 *
 * @returns Merge between @a rcStrict and what the commit operation returned.
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   rcStrict    The status code returned by ring-0 or raw-mode.
 */
VMMR3_INT_DECL(VBOXSTRICTRC) IEMR3ProcessForceFlag(PVM pVM, PVMCPUCC pVCpu, VBOXSTRICTRC rcStrict)
{
    /* There shall be 1 in entry #0. */
    AssertMsg(   ICORE(pVCpu).aMemMappings[0].fAccess & (IEM_ACCESS_PENDING_R3_WRITE_1ST | IEM_ACCESS_PENDING_R3_WRITE_2ND)
              && ICORE(pVCpu).cActiveMappings == 1,
              ("%u: %#x (%#x %#x)\n", ICORE(pVCpu).cActiveMappings,
               ICORE(pVCpu).aMemMappings[0].fAccess, ICORE(pVCpu).aMemMappings[1].fAccess, ICORE(pVCpu).aMemMappings[2].fAccess));

    /*
     * Reset the pending commit.
     */
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_IEM);

    /*
     * Commit the pending bounce buffers (there can only be one, #0).
     */
    unsigned const iMemMap = 0;
    if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & (IEM_ACCESS_PENDING_R3_WRITE_1ST | IEM_ACCESS_PENDING_R3_WRITE_2ND))
    {
        Assert(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_TYPE_WRITE);
        Assert(ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_BOUNCE_BUFFERED);
        Assert(!ICORE(pVCpu).aMemBbMappings[iMemMap].fUnassigned);

        uint16_t const  cbFirst  = ICORE(pVCpu).aMemBbMappings[iMemMap].cbFirst;
        uint16_t const  cbSecond = ICORE(pVCpu).aMemBbMappings[iMemMap].cbSecond;
        uint8_t const  *pbBuf    = &ICORE(pVCpu).aBounceBuffers[iMemMap].ab[0];

        if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_PENDING_R3_WRITE_1ST)
        {
            VBOXSTRICTRC rcStrictCommit1 = PGMPhysWrite(pVM,
                                                        ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst,
                                                        pbBuf,
                                                        cbFirst,
                                                        PGMACCESSORIGIN_IEM);
            rcStrict = iemR3MergeStatus(rcStrict, rcStrictCommit1, iMemMap, pVCpu);
            Log(("IEMR3ProcessForceFlag: iMemMap=%u GCPhysFirst=%RGp LB %#x %Rrc => %Rrc\n",
                 iMemMap, ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysFirst, cbFirst,
                 VBOXSTRICTRC_VAL(rcStrictCommit1), VBOXSTRICTRC_VAL(rcStrict)));
        }

        if (ICORE(pVCpu).aMemMappings[iMemMap].fAccess & IEM_ACCESS_PENDING_R3_WRITE_2ND)
        {
            VBOXSTRICTRC rcStrictCommit2 = PGMPhysWrite(pVM,
                                                        ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond,
                                                        pbBuf + cbFirst,
                                                        cbSecond,
                                                        PGMACCESSORIGIN_IEM);
            rcStrict = iemR3MergeStatus(rcStrict, rcStrictCommit2, iMemMap, pVCpu);
            Log(("IEMR3ProcessForceFlag: iMemMap=%u GCPhysSecond=%RGp LB %#x %Rrc => %Rrc\n",
                 iMemMap, ICORE(pVCpu).aMemBbMappings[iMemMap].GCPhysSecond, cbSecond,
                 VBOXSTRICTRC_VAL(rcStrictCommit2), VBOXSTRICTRC_VAL(rcStrict)));
        }
        ICORE(pVCpu).aMemMappings[iMemMap].fAccess = IEM_ACCESS_INVALID;
    }

    ICORE(pVCpu).cActiveMappings = 0;
    return rcStrict;
}

#endif /* IN_RING3 */

