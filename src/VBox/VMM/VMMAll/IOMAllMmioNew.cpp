/* $Id: IOMAllMmioNew.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOM - Input / Output Monitor - Any Context, MMIO & String I/O.
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
#define LOG_GROUP LOG_GROUP_IOM_MMIO
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iom.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/trpm.h>
#include <VBox/vmm/iem.h>
#include "IOMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/hm.h>
#include "IOMInline.h"

#include <VBox/vmm/pdmdev.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <iprt/assert.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def IOM_MMIO_STATS_COMMA_DECL
 * Parameter list declaration for statistics entry pointer. */
/** @def IOM_MMIO_STATS_COMMA_ARG
 * Statistics entry pointer argument. */
#if defined(VBOX_WITH_STATISTICS) || defined(DOXYGEN_RUNNING)
# define IOM_MMIO_STATS_COMMA_DECL , PIOMMMIOSTATSENTRY pStats
# define IOM_MMIO_STATS_COMMA_ARG  , pStats
#else
# define IOM_MMIO_STATS_COMMA_DECL
# define IOM_MMIO_STATS_COMMA_ARG
#endif



#ifndef IN_RING3
/**
 * Defers a pending MMIO write to ring-3.
 *
 * @returns VINF_IOM_R3_MMIO_COMMIT_WRITE
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhys      The write address.
 * @param   pvBuf       The bytes being written.
 * @param   cbBuf       How many bytes.
 * @param   idxRegEntry The MMIO registration index (handle) if available,
 *                      otherwise UINT32_MAX.
 */
static VBOXSTRICTRC iomMmioRing3WritePending(PVMCPU pVCpu, RTGCPHYS GCPhys, void const *pvBuf, size_t cbBuf,
                                             uint32_t idxRegEntry)
{
    Log5(("iomMmioRing3WritePending: %RGp LB %#x (idx=%#x)\n", GCPhys, cbBuf, idxRegEntry));
    if (pVCpu->iom.s.PendingMmioWrite.cbValue == 0)
    {
        pVCpu->iom.s.PendingMmioWrite.GCPhys            = GCPhys;
        AssertReturn(cbBuf <= sizeof(pVCpu->iom.s.PendingMmioWrite.abValue), VERR_IOM_MMIO_IPE_2);
        pVCpu->iom.s.PendingMmioWrite.cbValue           = (uint32_t)cbBuf;
        pVCpu->iom.s.PendingMmioWrite.idxMmioRegionHint = idxRegEntry;
        memcpy(pVCpu->iom.s.PendingMmioWrite.abValue, pvBuf, cbBuf);
    }
    else
    {
        /*
         * Join with pending if adjecent.
         *
         * This may happen if the stack overflows into MMIO territory and RSP/ESP/SP
         * isn't aligned. IEM will bounce buffer the access and do one write for each
         * page.  We get here when the 2nd page part is written.
         */
        uint32_t const cbOldValue = pVCpu->iom.s.PendingMmioWrite.cbValue;
        AssertMsgReturn(GCPhys == pVCpu->iom.s.PendingMmioWrite.GCPhys + cbOldValue,
                        ("pending %RGp LB %#x; incoming %RGp LB %#x\n",
                         pVCpu->iom.s.PendingMmioWrite.GCPhys, cbOldValue, GCPhys, cbBuf),
                        VERR_IOM_MMIO_IPE_1);
        AssertReturn(cbBuf <= sizeof(pVCpu->iom.s.PendingMmioWrite.abValue) - cbOldValue, VERR_IOM_MMIO_IPE_2);
        pVCpu->iom.s.PendingMmioWrite.cbValue = cbOldValue + (uint32_t)cbBuf;
        memcpy(&pVCpu->iom.s.PendingMmioWrite.abValue[cbOldValue], pvBuf, cbBuf);
    }

    VMCPU_FF_SET(pVCpu, VMCPU_FF_IOM);
    return VINF_IOM_R3_MMIO_COMMIT_WRITE;
}
#endif


/**
 * Deals with complicated MMIO writes.
 *
 * Complicated means unaligned or non-dword/qword sized accesses depending on
 * the MMIO region's access mode flags.
 *
 * @returns Strict VBox status code. Any EM scheduling status code,
 *          VINF_IOM_R3_MMIO_WRITE, VINF_IOM_R3_MMIO_READ_WRITE or
 *          VINF_IOM_R3_MMIO_READ may be returned.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pRegEntry   The MMIO entry for the current context.
 * @param   GCPhys      The physical address to start writing.
 * @param   offRegion   MMIO region offset corresponding to @a GCPhys.
 * @param   pvValue     Where to store the value.
 * @param   cbValue     The size of the value to write.
 * @param   pStats      Pointer to the statistics (never NULL).
 */
static VBOXSTRICTRC iomMmioDoComplicatedWrite(PVM pVM, PVMCPU pVCpu, CTX_SUFF(PIOMMMIOENTRY) pRegEntry,
                                              RTGCPHYS GCPhys, RTGCPHYS offRegion,
                                              void const *pvValue, unsigned cbValue IOM_MMIO_STATS_COMMA_DECL)
{
    AssertReturn(   (pRegEntry->fFlags & IOMMMIO_FLAGS_WRITE_MODE) != IOMMMIO_FLAGS_WRITE_PASSTHRU
                 && (pRegEntry->fFlags & IOMMMIO_FLAGS_WRITE_MODE) <= IOMMMIO_FLAGS_WRITE_DWORD_QWORD_READ_MISSING,
                 VERR_IOM_MMIO_IPE_1);
    AssertReturn(cbValue != 0 && cbValue <= 16, VERR_IOM_MMIO_IPE_2);
    RTGCPHYS const GCPhysStart  = GCPhys; NOREF(GCPhysStart);
    bool const     fReadMissing = (pRegEntry->fFlags & IOMMMIO_FLAGS_WRITE_MODE) == IOMMMIO_FLAGS_WRITE_DWORD_READ_MISSING
                               || (pRegEntry->fFlags & IOMMMIO_FLAGS_WRITE_MODE) == IOMMMIO_FLAGS_WRITE_DWORD_QWORD_READ_MISSING;
    RT_NOREF_PV(pVCpu); /* ring-3 */

    /*
     * Do debug stop if requested.
     */
    VBOXSTRICTRC rc = VINF_SUCCESS; NOREF(pVM);
#ifdef VBOX_STRICT
    if (!(pRegEntry->fFlags & IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_WRITE))
    { /* likely */ }
    else
    {
# ifdef IN_RING3
        LogRel(("IOM: Complicated write %#x byte at %RGp to %s, initiating debugger intervention\n", cbValue, GCPhys,
                R3STRING(pRegEntry->pszDesc)));
        rc = DBGFR3EventSrc(pVM, DBGFEVENT_DEV_STOP, RT_SRC_POS,
                            "Complicated write %#x byte at %RGp to %s\n", cbValue, GCPhys, pRegEntry->pszDesc);
        if (rc == VERR_DBGF_NOT_ATTACHED)
            rc = VINF_SUCCESS;
# else
        return VINF_IOM_R3_MMIO_WRITE;
# endif
    }
#endif

    STAM_COUNTER_INC(&pStats->ComplicatedWrites);

    /*
     * Check if we should ignore the write.
     */
    if ((pRegEntry->fFlags & IOMMMIO_FLAGS_WRITE_MODE) == IOMMMIO_FLAGS_WRITE_ONLY_DWORD)
    {
        Assert(cbValue != 4 || (GCPhys & 3));
        return VINF_SUCCESS;
    }
    if ((pRegEntry->fFlags & IOMMMIO_FLAGS_WRITE_MODE) == IOMMMIO_FLAGS_WRITE_ONLY_DWORD_QWORD)
    {
        Assert((cbValue != 4 && cbValue != 8) || (GCPhys & (cbValue - 1)));
        return VINF_SUCCESS;
    }

    /*
     * Split and conquer.
     */
    for (;;)
    {
        unsigned const  offAccess  = GCPhys & 3;
        unsigned        cbThisPart = 4 - offAccess;
        if (cbThisPart > cbValue)
            cbThisPart = cbValue;

        /*
         * Get the missing bits (if any).
         */
        uint32_t u32MissingValue = 0;
        if (fReadMissing && cbThisPart != 4)
        {
            VBOXSTRICTRC rc2 = pRegEntry->pfnReadCallback(pRegEntry->pDevIns, pRegEntry->pvUser,
                                                          !(pRegEntry->fFlags & IOMMMIO_FLAGS_ABS)
                                                          ? offRegion & ~(RTGCPHYS)3 : (GCPhys & ~(RTGCPHYS)3),
                                                          &u32MissingValue, sizeof(u32MissingValue));
            switch (VBOXSTRICTRC_VAL(rc2))
            {
                case VINF_SUCCESS:
                    break;
                case VINF_IOM_MMIO_UNUSED_FF:
                    STAM_COUNTER_INC(&pStats->FFor00Reads);
                    u32MissingValue = UINT32_C(0xffffffff);
                    break;
                case VINF_IOM_MMIO_UNUSED_00:
                    STAM_COUNTER_INC(&pStats->FFor00Reads);
                    u32MissingValue = 0;
                    break;
#ifndef IN_RING3
                case VINF_IOM_R3_MMIO_READ:
                case VINF_IOM_R3_MMIO_READ_WRITE:
                case VINF_IOM_R3_MMIO_WRITE:
                    LogFlow(("iomMmioDoComplicatedWrite: GCPhys=%RGp GCPhysStart=%RGp cbValue=%u rc=%Rrc [read]\n", GCPhys, GCPhysStart, cbValue, VBOXSTRICTRC_VAL(rc2)));
                    rc2 = iomMmioRing3WritePending(pVCpu, GCPhys, pvValue, cbValue, pRegEntry->idxSelf);
                    if (rc == VINF_SUCCESS || rc2 < rc)
                        rc = rc2;
                    return rc;
#endif
                default:
                    if (RT_FAILURE(rc2))
                    {
                        Log(("iomMmioDoComplicatedWrite: GCPhys=%RGp GCPhysStart=%RGp cbValue=%u rc=%Rrc [read]\n", GCPhys, GCPhysStart, cbValue, VBOXSTRICTRC_VAL(rc2)));
                        return rc2;
                    }
                    AssertMsgReturn(rc2 >= VINF_EM_FIRST && rc2 <= VINF_EM_LAST, ("%Rrc\n", VBOXSTRICTRC_VAL(rc2)), VERR_IPE_UNEXPECTED_INFO_STATUS);
                    if (rc == VINF_SUCCESS || rc2 < rc)
                        rc = rc2;
                    break;
            }
        }

        /*
         * Merge missing and given bits.
         */
        uint32_t u32GivenMask;
        uint32_t u32GivenValue;
        switch (cbThisPart)
        {
            case 1:
                u32GivenValue = *(uint8_t  const *)pvValue;
                u32GivenMask  = UINT32_C(0x000000ff);
                break;
            case 2:
                u32GivenValue = *(uint16_t const *)pvValue;
                u32GivenMask  = UINT32_C(0x0000ffff);
                break;
            case 3:
                u32GivenValue = RT_MAKE_U32_FROM_U8(((uint8_t const *)pvValue)[0], ((uint8_t const *)pvValue)[1],
                                                    ((uint8_t const *)pvValue)[2], 0);
                u32GivenMask  = UINT32_C(0x00ffffff);
                break;
            case 4:
                u32GivenValue = *(uint32_t const *)pvValue;
                u32GivenMask  = UINT32_C(0xffffffff);
                break;
            default:
                AssertFailedReturn(VERR_IOM_MMIO_IPE_3);
        }
        if (offAccess)
        {
            u32GivenValue <<= offAccess * 8;
            u32GivenMask  <<= offAccess * 8;
        }

        uint32_t u32Value = (u32MissingValue & ~u32GivenMask)
                          | (u32GivenValue & u32GivenMask);

        /*
         * Do DWORD write to the device.
         */
        VBOXSTRICTRC rc2 = pRegEntry->pfnWriteCallback(pRegEntry->pDevIns, pRegEntry->pvUser,
                                                       !(pRegEntry->fFlags & IOMMMIO_FLAGS_ABS)
                                                       ? offRegion & ~(RTGCPHYS)3 : GCPhys & ~(RTGCPHYS)3,
                                                       &u32Value, sizeof(u32Value));
        switch (VBOXSTRICTRC_VAL(rc2))
        {
            case VINF_SUCCESS:
                break;
#ifndef IN_RING3
            case VINF_IOM_R3_MMIO_READ:
            case VINF_IOM_R3_MMIO_READ_WRITE:
            case VINF_IOM_R3_MMIO_WRITE:
                Log3(("iomMmioDoComplicatedWrite: deferring GCPhys=%RGp GCPhysStart=%RGp cbValue=%u rc=%Rrc [write]\n", GCPhys, GCPhysStart, cbValue, VBOXSTRICTRC_VAL(rc2)));
                AssertReturn(pVCpu->iom.s.PendingMmioWrite.cbValue == 0, VERR_IOM_MMIO_IPE_1);
                AssertReturn(cbValue + (GCPhys & 3) <= sizeof(pVCpu->iom.s.PendingMmioWrite.abValue), VERR_IOM_MMIO_IPE_2);
                pVCpu->iom.s.PendingMmioWrite.GCPhys  = GCPhys & ~(RTGCPHYS)3;
                pVCpu->iom.s.PendingMmioWrite.cbValue = cbValue + (GCPhys & 3);
                *(uint32_t *)pVCpu->iom.s.PendingMmioWrite.abValue = u32Value;
                if (cbValue > cbThisPart)
                    memcpy(&pVCpu->iom.s.PendingMmioWrite.abValue[4],
                           (uint8_t const *)pvValue + cbThisPart, cbValue - cbThisPart);
                VMCPU_FF_SET(pVCpu, VMCPU_FF_IOM);
                if (rc == VINF_SUCCESS)
                    rc = VINF_IOM_R3_MMIO_COMMIT_WRITE;
                return rc;
#endif
            default:
                if (RT_FAILURE(rc2))
                {
                    Log(("iomMmioDoComplicatedWrite: GCPhys=%RGp GCPhysStart=%RGp cbValue=%u rc=%Rrc [write]\n", GCPhys, GCPhysStart, cbValue, VBOXSTRICTRC_VAL(rc2)));
                    return rc2;
                }
                AssertMsgReturn(rc2 >= VINF_EM_FIRST && rc2 <= VINF_EM_LAST, ("%Rrc\n", VBOXSTRICTRC_VAL(rc2)), VERR_IPE_UNEXPECTED_INFO_STATUS);
                if (rc == VINF_SUCCESS || rc2 < rc)
                    rc = rc2;
                break;
        }

        /*
         * Advance.
         */
        cbValue   -= cbThisPart;
        if (!cbValue)
            break;
        GCPhys    += cbThisPart;
        offRegion += cbThisPart;
        pvValue    = (uint8_t const *)pvValue + cbThisPart;
    }

    return rc;
}




/**
 * Wrapper which does the write.
 */
DECLINLINE(VBOXSTRICTRC) iomMmioDoWrite(PVMCC pVM, PVMCPU pVCpu, CTX_SUFF(PIOMMMIOENTRY) pRegEntry,
                                        RTGCPHYS GCPhys, RTGCPHYS offRegion,
                                        const void *pvData, uint32_t cb IOM_MMIO_STATS_COMMA_DECL)
{
    VBOXSTRICTRC rcStrict;
    if (RT_LIKELY(pRegEntry->pfnWriteCallback))
    {
        if (   (cb == 4 && !(GCPhys & 3))
            || (pRegEntry->fFlags & IOMMMIO_FLAGS_WRITE_MODE) == IOMMMIO_FLAGS_WRITE_PASSTHRU
            || (cb == 8 && !(GCPhys & 7) && IOMMMIO_DOES_WRITE_MODE_ALLOW_QWORD(pRegEntry->fFlags)) )
            rcStrict = pRegEntry->pfnWriteCallback(pRegEntry->pDevIns, pRegEntry->pvUser,
                                                   !(pRegEntry->fFlags & IOMMMIO_FLAGS_ABS) ? offRegion : GCPhys, pvData, cb);
        else
            rcStrict = iomMmioDoComplicatedWrite(pVM, pVCpu, pRegEntry, GCPhys, offRegion, pvData, cb IOM_MMIO_STATS_COMMA_ARG);
    }
    else
        rcStrict = VINF_SUCCESS;
    return rcStrict;
}


#ifdef IN_RING3
/**
 * Helper for IOMR3ProcessForceFlag() that lives here to utilize iomMmioDoWrite et al.
 */
VBOXSTRICTRC iomR3MmioCommitWorker(PVM pVM, PVMCPU pVCpu, PIOMMMIOENTRYR3 pRegEntry, RTGCPHYS offRegion)
{
# ifdef VBOX_WITH_STATISTICS
    STAM_PROFILE_START(UnusedMacroArg, Prf);
    PIOMMMIOSTATSENTRY const pStats = iomMmioGetStats(pVM, pRegEntry);
# endif
    PPDMDEVINS const         pDevIns = pRegEntry->pDevIns;
    int rc = PDMCritSectEnter(pVM, pDevIns->CTX_SUFF(pCritSectRo), VERR_IGNORED);
    AssertRCReturn(rc, rc);

    VBOXSTRICTRC rcStrict = iomMmioDoWrite(pVM, pVCpu, pRegEntry, pVCpu->iom.s.PendingMmioWrite.GCPhys, offRegion,
                                           pVCpu->iom.s.PendingMmioWrite.abValue, pVCpu->iom.s.PendingMmioWrite.cbValue
                                           IOM_MMIO_STATS_COMMA_ARG);

    PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));
    STAM_PROFILE_STOP(&pStats->ProfWriteR3, Prf);
    return rcStrict;
}
#endif /* IN_RING3 */


/**
 * Deals with complicated MMIO reads.
 *
 * Complicated means unaligned or non-dword/qword sized accesses depending on
 * the MMIO region's access mode flags.
 *
 * @returns Strict VBox status code. Any EM scheduling status code,
 *          VINF_IOM_R3_MMIO_READ, VINF_IOM_R3_MMIO_READ_WRITE or
 *          VINF_IOM_R3_MMIO_WRITE may be returned.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pRegEntry   The MMIO entry for the current context.
 * @param   GCPhys      The physical address to start reading.
 * @param   offRegion   MMIO region offset corresponding to @a GCPhys.
 * @param   pvValue     Where to store the value.
 * @param   cbValue     The size of the value to read.
 * @param   pStats      Pointer to the statistics (never NULL).
 */
static VBOXSTRICTRC iomMMIODoComplicatedRead(PVM pVM, CTX_SUFF(PIOMMMIOENTRY) pRegEntry, RTGCPHYS GCPhys, RTGCPHYS offRegion,
                                             void *pvValue, unsigned cbValue IOM_MMIO_STATS_COMMA_DECL)
{
    AssertReturn(   (pRegEntry->fFlags & IOMMMIO_FLAGS_READ_MODE) == IOMMMIO_FLAGS_READ_DWORD
                 || (pRegEntry->fFlags & IOMMMIO_FLAGS_READ_MODE) == IOMMMIO_FLAGS_READ_DWORD_QWORD,
                 VERR_IOM_MMIO_IPE_1);
    AssertReturn(cbValue != 0 && cbValue <= 16, VERR_IOM_MMIO_IPE_2);
#ifdef LOG_ENABLED
    RTGCPHYS const GCPhysStart = GCPhys;
#endif

    /*
     * Do debug stop if requested.
     */
    VBOXSTRICTRC rc = VINF_SUCCESS; NOREF(pVM);
#ifdef VBOX_STRICT
    if (pRegEntry->fFlags & IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_READ)
    {
# ifdef IN_RING3
        rc = DBGFR3EventSrc(pVM, DBGFEVENT_DEV_STOP, RT_SRC_POS,
                            "Complicated read %#x byte at %RGp to %s\n", cbValue, GCPhys, R3STRING(pRegEntry->pszDesc));
        if (rc == VERR_DBGF_NOT_ATTACHED)
            rc = VINF_SUCCESS;
# else
        return VINF_IOM_R3_MMIO_READ;
# endif
    }
#endif

    STAM_COUNTER_INC(&pStats->ComplicatedReads);

    /*
     * Split and conquer.
     */
    for (;;)
    {
        /*
         * Do DWORD read from the device.
         */
        uint32_t u32Value = 0;
        VBOXSTRICTRC rcStrict2 = pRegEntry->pfnReadCallback(pRegEntry->pDevIns, pRegEntry->pvUser,
                                                            !(pRegEntry->fFlags & IOMMMIO_FLAGS_ABS)
                                                            ? offRegion & ~(RTGCPHYS)3 : GCPhys & ~(RTGCPHYS)3,
                                                            &u32Value, sizeof(u32Value));
        switch (VBOXSTRICTRC_VAL(rcStrict2))
        {
            case VINF_SUCCESS:
                break;
            case VINF_IOM_MMIO_UNUSED_FF:
                STAM_COUNTER_INC(&pStats->FFor00Reads);
                u32Value = UINT32_C(0xffffffff);
                break;
            case VINF_IOM_MMIO_UNUSED_00:
                STAM_COUNTER_INC(&pStats->FFor00Reads);
                u32Value = 0;
                break;
            case VINF_IOM_R3_MMIO_READ:
            case VINF_IOM_R3_MMIO_READ_WRITE:
            case VINF_IOM_R3_MMIO_WRITE:
                /** @todo What if we've split a transfer and already read
                 * something?  Since reads can have sideeffects we could be
                 * kind of screwed here... */
                LogFlow(("iomMMIODoComplicatedRead: GCPhys=%RGp GCPhysStart=%RGp cbValue=%u rcStrict2=%Rrc\n",
                         GCPhys, GCPhysStart, cbValue, VBOXSTRICTRC_VAL(rcStrict2)));
                return rcStrict2;
            default:
                if (RT_FAILURE(rcStrict2))
                {
                    Log(("iomMMIODoComplicatedRead: GCPhys=%RGp GCPhysStart=%RGp cbValue=%u rcStrict2=%Rrc\n",
                         GCPhys, GCPhysStart, cbValue, VBOXSTRICTRC_VAL(rcStrict2)));
                    return rcStrict2;
                }
                AssertMsgReturn(rcStrict2 >= VINF_EM_FIRST && rcStrict2 <= VINF_EM_LAST, ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict2)),
                                VERR_IPE_UNEXPECTED_INFO_STATUS);
                if (rc == VINF_SUCCESS || rcStrict2 < rc)
                    rc = rcStrict2;
                break;
        }
        u32Value >>= (GCPhys & 3) * 8;

        /*
         * Write what we've read.
         */
        unsigned cbThisPart = 4 - (GCPhys & 3);
        if (cbThisPart > cbValue)
            cbThisPart = cbValue;

        switch (cbThisPart)
        {
            case 1:
                *(uint8_t *)pvValue = (uint8_t)u32Value;
                break;
            case 2:
                *(uint16_t *)pvValue = (uint16_t)u32Value;
                break;
            case 3:
                ((uint8_t *)pvValue)[0] = RT_BYTE1(u32Value);
                ((uint8_t *)pvValue)[1] = RT_BYTE2(u32Value);
                ((uint8_t *)pvValue)[2] = RT_BYTE3(u32Value);
                break;
            case 4:
                *(uint32_t *)pvValue = u32Value;
                break;
        }

        /*
         * Advance.
         */
        cbValue   -= cbThisPart;
        if (!cbValue)
            break;
        GCPhys    += cbThisPart;
        offRegion += cbThisPart;
        pvValue    = (uint8_t *)pvValue + cbThisPart;
    }

    return rc;
}


/**
 * Implements VINF_IOM_MMIO_UNUSED_FF.
 *
 * @returns VINF_SUCCESS.
 * @param   pvValue     Where to store the zeros.
 * @param   cbValue     How many bytes to read.
 * @param   pStats      Pointer to the statistics (never NULL).
 */
static int iomMMIODoReadFFs(void *pvValue, size_t cbValue IOM_MMIO_STATS_COMMA_DECL)
{
    switch (cbValue)
    {
        case 1: *(uint8_t  *)pvValue = UINT8_C(0xff); break;
        case 2: *(uint16_t *)pvValue = UINT16_C(0xffff); break;
        case 4: *(uint32_t *)pvValue = UINT32_C(0xffffffff); break;
        case 8: *(uint64_t *)pvValue = UINT64_C(0xffffffffffffffff); break;
        default:
        {
            uint8_t *pb = (uint8_t *)pvValue;
            while (cbValue--)
                *pb++ = UINT8_C(0xff);
            break;
        }
    }
    STAM_COUNTER_INC(&pStats->FFor00Reads);
    return VINF_SUCCESS;
}


/**
 * Implements VINF_IOM_MMIO_UNUSED_00.
 *
 * @returns VINF_SUCCESS.
 * @param   pvValue     Where to store the zeros.
 * @param   cbValue     How many bytes to read.
 * @param   pStats      Pointer to the statistics (never NULL).
 */
static int iomMMIODoRead00s(void *pvValue, size_t cbValue IOM_MMIO_STATS_COMMA_DECL)
{
    switch (cbValue)
    {
        case 1: *(uint8_t  *)pvValue = UINT8_C(0x00); break;
        case 2: *(uint16_t *)pvValue = UINT16_C(0x0000); break;
        case 4: *(uint32_t *)pvValue = UINT32_C(0x00000000); break;
        case 8: *(uint64_t *)pvValue = UINT64_C(0x0000000000000000); break;
        default:
        {
            uint8_t *pb = (uint8_t *)pvValue;
            while (cbValue--)
                *pb++ = UINT8_C(0x00);
            break;
        }
    }
    STAM_COUNTER_INC(&pStats->FFor00Reads);
    return VINF_SUCCESS;
}


/**
 * Wrapper which does the read.
 */
DECLINLINE(VBOXSTRICTRC) iomMmioDoRead(PVMCC pVM, CTX_SUFF(PIOMMMIOENTRY) pRegEntry, RTGCPHYS GCPhys, RTGCPHYS offRegion,
                                       void *pvValue, uint32_t cbValue IOM_MMIO_STATS_COMMA_DECL)
{
    VBOXSTRICTRC rcStrict;
    if (RT_LIKELY(pRegEntry->pfnReadCallback))
    {
        if (   (   cbValue == 4
                && !(GCPhys & 3))
            || (pRegEntry->fFlags & IOMMMIO_FLAGS_READ_MODE) == IOMMMIO_FLAGS_READ_PASSTHRU
            || (    cbValue == 8
                && !(GCPhys & 7)
                && (pRegEntry->fFlags & IOMMMIO_FLAGS_READ_MODE) == IOMMMIO_FLAGS_READ_DWORD_QWORD ) )
            rcStrict = pRegEntry->pfnReadCallback(pRegEntry->pDevIns, pRegEntry->pvUser,
                                                  !(pRegEntry->fFlags & IOMMMIO_FLAGS_ABS) ? offRegion : GCPhys, pvValue, cbValue);
        else
            rcStrict = iomMMIODoComplicatedRead(pVM, pRegEntry, GCPhys, offRegion, pvValue, cbValue IOM_MMIO_STATS_COMMA_ARG);
    }
    else
        rcStrict = VINF_IOM_MMIO_UNUSED_FF;
    if (rcStrict != VINF_SUCCESS)
    {
        switch (VBOXSTRICTRC_VAL(rcStrict))
        {
            case VINF_IOM_MMIO_UNUSED_FF: rcStrict = iomMMIODoReadFFs(pvValue, cbValue IOM_MMIO_STATS_COMMA_ARG); break;
            case VINF_IOM_MMIO_UNUSED_00: rcStrict = iomMMIODoRead00s(pvValue, cbValue IOM_MMIO_STATS_COMMA_ARG); break;
        }
    }
    return rcStrict;
}

#ifndef IN_RING3

/**
 * Checks if we can handle an MMIO \#PF in R0/RC.
 */
DECLINLINE(bool) iomMmioCanHandlePfInRZ(PVMCC pVM, uint32_t uErrorCode, CTX_SUFF(PIOMMMIOENTRY) pRegEntry)
{
    if (pRegEntry->cbRegion > 0)
    {
        if (   pRegEntry->pfnWriteCallback
            && pRegEntry->pfnReadCallback)
            return true;

        PIOMMMIOENTRYR3 const pRegEntryR3 = &pVM->iomr0.s.paMmioRing3Regs[pRegEntry->idxSelf];
        if (  uErrorCode == UINT32_MAX
            ? pRegEntryR3->pfnWriteCallback || pRegEntryR3->pfnReadCallback
            : uErrorCode & X86_TRAP_PF_RW
              ? !pRegEntry->pfnWriteCallback && pRegEntryR3->pfnWriteCallback
              : !pRegEntry->pfnReadCallback  && pRegEntryR3->pfnReadCallback)
            return false;

        return true;
    }
    return false;
}


/**
 * Common worker for the \#PF handler and IOMMMIOPhysHandler (APIC+VT-x).
 *
 * @returns VBox status code (appropriate for GC return).
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uErrorCode  CPU Error code.  This is UINT32_MAX when we don't have
 *                      any error code (the EPT misconfig hack).
 * @param   GCPhysFault The GC physical address corresponding to pvFault.
 * @param   pRegEntry   The MMIO entry for the current context.
 */
DECLINLINE(VBOXSTRICTRC) iomMmioCommonPfHandlerNew(PVMCC pVM, PVMCPUCC pVCpu, uint32_t uErrorCode,
                                                   RTGCPHYS GCPhysFault, CTX_SUFF(PIOMMMIOENTRY) pRegEntry)
{
    Log(("iomMmioCommonPfHandler: GCPhysFault=%RGp uErr=%#x rip=%RGv\n", GCPhysFault, uErrorCode, CPUMGetGuestRIP(pVCpu) ));
    RT_NOREF(GCPhysFault, uErrorCode);

    VBOXSTRICTRC rcStrict;

#ifndef IN_RING3
    /*
     * Should we defer the request right away?  This isn't usually the case, so
     * do the simple test first and the try deal with uErrorCode being N/A.
     */
    PPDMDEVINS const pDevIns = pRegEntry->pDevIns;
    if (RT_LIKELY(   pDevIns
                  && iomMmioCanHandlePfInRZ(pVM, uErrorCode, pRegEntry)))
    {
        /*
         * Enter the device critsect prior to engaging IOM in case of lock contention.
         * Note! Perhaps not a good move?
         */
        rcStrict = PDMCritSectEnter(pVM, pDevIns->CTX_SUFF(pCritSectRo), VINF_IOM_R3_MMIO_READ_WRITE);
        if (rcStrict == VINF_SUCCESS)
        {
#endif /* !IN_RING3 */

            /*
             * Let IEM call us back via iomMmioHandler.
             */
            rcStrict = IEMExecOne(pVCpu);

#ifndef IN_RING3
            PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));
#endif
            if (RT_SUCCESS(rcStrict))
            { /* likely */ }
            else if (   rcStrict == VERR_IEM_ASPECT_NOT_IMPLEMENTED
                     || rcStrict == VERR_IEM_INSTR_NOT_IMPLEMENTED)
            {
                Log(("IOM: Hit unsupported IEM feature!\n"));
                rcStrict = VINF_EM_RAW_EMULATE_INSTR;
            }
#ifndef IN_RING3
            return rcStrict;
        }
        STAM_COUNTER_INC(&pVM->iom.s.StatMmioDevLockContentionR0);
    }
    else
        rcStrict = VINF_IOM_R3_MMIO_READ_WRITE;

# ifdef VBOX_WITH_STATISTICS
    if (rcStrict == VINF_IOM_R3_MMIO_READ_WRITE)
    {
        PIOMMMIOSTATSENTRY const pStats = iomMmioGetStats(pVM, pRegEntry);
        if (uErrorCode & X86_TRAP_PF_RW)
        {
            STAM_COUNTER_INC(&pStats->WriteRZToR3);
            STAM_COUNTER_INC(&pVM->iom.s.StatMmioWritesR0ToR3);
        }
        else
        {
            STAM_COUNTER_INC(&pStats->ReadRZToR3);
            STAM_COUNTER_INC(&pVM->iom.s.StatMmioReadsR0ToR3);
        }
    }
# endif
#else  /* IN_RING3 */
    RT_NOREF(pVM, pRegEntry);
#endif /* IN_RING3 */
    return rcStrict;
}


/**
 * @callback_method_impl{FNPGMRZPHYSPFHANDLER,
 *      \#PF access handler callback for MMIO pages.}
 *
 * @remarks The @a uUser argument is the MMIO handle.
 */
DECLCALLBACK(VBOXSTRICTRC) iomMmioPfHandlerNew(PVMCC pVM, PVMCPUCC pVCpu, RTGCUINT uErrorCode, PCPUMCTX pCtx,
                                               RTGCPTR pvFault, RTGCPHYS GCPhysFault, uint64_t uUser)
{
    STAM_PROFILE_START(&pVM->iom.s.StatMmioPfHandler, Prf);
    LogFlow(("iomMmioPfHandlerNew: GCPhys=%RGp uErr=%#x pvFault=%RGv rip=%RGv\n",
             GCPhysFault, (uint32_t)uErrorCode, pvFault, (RTGCPTR)pCtx->rip));
    RT_NOREF(pvFault, pCtx);

    /* Translate the MMIO handle to a registration entry for the current context. */
    AssertReturn(uUser < RT_MIN(pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc), VERR_IOM_INVALID_MMIO_HANDLE);
# ifdef IN_RING0
    AssertReturn(uUser < pVM->iomr0.s.cMmioAlloc, VERR_IOM_INVALID_MMIO_HANDLE);
    CTX_SUFF(PIOMMMIOENTRY) pRegEntry = &pVM->iomr0.s.paMmioRegs[uUser];
# else
    CTX_SUFF(PIOMMMIOENTRY) pRegEntry = &pVM->iom.s.paMmioRegs[uUser];
# endif

    VBOXSTRICTRC rcStrict = iomMmioCommonPfHandlerNew(pVM, pVCpu, (uint32_t)uErrorCode, GCPhysFault, pRegEntry);

    STAM_PROFILE_STOP(&pVM->iom.s.StatMmioPfHandler, Prf);
    return rcStrict;
}

#endif /* !IN_RING3 */

#ifdef IN_RING0
/**
 * Physical access handler for MMIO ranges.
 *
 * This is actually only used by VT-x for APIC page accesses.
 *
 * @returns VBox status code (appropriate for GC return).
 * @param   pVM         The cross context VM structure.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   uErrorCode  CPU Error code.
 * @param   GCPhysFault The GC physical address.
 */
VMM_INT_DECL(VBOXSTRICTRC) IOMR0MmioPhysHandler(PVMCC pVM, PVMCPUCC pVCpu, uint32_t uErrorCode, RTGCPHYS GCPhysFault)
{
    STAM_PROFILE_START(&pVM->iom.s.StatMmioPhysHandler, Prf);

    /*
     * We don't have a range here, so look it up before calling the common function.
     */
    VBOXSTRICTRC rcStrict = IOM_LOCK_SHARED(pVM);
    if (RT_SUCCESS(rcStrict))
    {
        RTGCPHYS offRegion;
        CTX_SUFF(PIOMMMIOENTRY) pRegEntry = iomMmioGetEntry(pVM, GCPhysFault, &offRegion, &pVCpu->iom.s.idxMmioLastPhysHandler);
        IOM_UNLOCK_SHARED(pVM);
        if (RT_LIKELY(pRegEntry))
            rcStrict = iomMmioCommonPfHandlerNew(pVM, pVCpu, (uint32_t)uErrorCode, GCPhysFault, pRegEntry);
        else
            rcStrict = VERR_IOM_MMIO_RANGE_NOT_FOUND;
    }
    else if (rcStrict == VERR_SEM_BUSY)
        rcStrict = VINF_IOM_R3_MMIO_READ_WRITE;

    STAM_PROFILE_STOP(&pVM->iom.s.StatMmioPhysHandler, Prf);
    return rcStrict;
}
#endif /* IN_RING0 */


/**
 * @callback_method_impl{FNPGMPHYSHANDLER, MMIO page accesses}
 *
 * @remarks The @a uUser argument is the MMIO handle.
 */
DECLCALLBACK(VBOXSTRICTRC) iomMmioHandlerNew(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhysFault, void *pvPhys, void *pvBuf,
                                             size_t cbBuf, PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin, uint64_t uUser)
{
    STAM_PROFILE_START(UnusedMacroArg, Prf);
    STAM_COUNTER_INC(&pVM->iom.s.CTX_SUFF(StatMmioHandler));
    Log4(("iomMmioHandlerNew: GCPhysFault=%RGp cbBuf=%#x enmAccessType=%d enmOrigin=%d uUser=%p\n", GCPhysFault, cbBuf, enmAccessType, enmOrigin, uUser));

    Assert(enmAccessType == PGMACCESSTYPE_READ || enmAccessType == PGMACCESSTYPE_WRITE);
    AssertMsg(cbBuf >= 1, ("%zu\n", cbBuf));
    NOREF(pvPhys); NOREF(enmOrigin);

#ifdef IN_RING3
    int const rcToRing3 = VERR_IOM_MMIO_IPE_3;
#else
    int const rcToRing3 = enmAccessType == PGMACCESSTYPE_READ ? VINF_IOM_R3_MMIO_READ : VINF_IOM_R3_MMIO_WRITE;
#endif

    /*
     * Translate uUser to an MMIO registration table entry.  We can do this
     * without any locking as the data is static after VM creation.
     */
    AssertReturn(uUser < RT_MIN(pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc), VERR_IOM_INVALID_MMIO_HANDLE);
#ifdef IN_RING0
    AssertReturn(uUser < pVM->iomr0.s.cMmioAlloc, VERR_IOM_INVALID_MMIO_HANDLE);
    CTX_SUFF(PIOMMMIOENTRY) const pRegEntry    = &pVM->iomr0.s.paMmioRegs[uUser];
    PIOMMMIOENTRYR3 const         pRegEntryR3  = &pVM->iomr0.s.paMmioRing3Regs[uUser];
#else
    CTX_SUFF(PIOMMMIOENTRY) const pRegEntry    = &pVM->iom.s.paMmioRegs[uUser];
#endif
#ifdef VBOX_WITH_STATISTICS
    PIOMMMIOSTATSENTRY const      pStats       = iomMmioGetStats(pVM, pRegEntry);  /* (Works even without ring-0 device setup.) */
#endif
    PPDMDEVINS const              pDevIns      = pRegEntry->pDevIns;

#ifdef VBOX_STRICT
    /*
     * Assert the right entry in strict builds.  This may yield a false positive
     * for SMP VMs if we're unlucky and the guest isn't well behaved.
     */
# ifdef IN_RING0
    Assert(pRegEntry && (GCPhysFault - pRegEntryR3->GCPhysMapping < pRegEntryR3->cbRegion || !pRegEntryR3->fMapped));
# else
    Assert(pRegEntry && (GCPhysFault - pRegEntry->GCPhysMapping   < pRegEntry->cbRegion   || !pRegEntry->fMapped));
# endif
#endif

#ifndef IN_RING3
    /*
     * If someone is doing FXSAVE, FXRSTOR, XSAVE, XRSTOR or other stuff dealing with
     * large amounts of data, just go to ring-3 where we don't need to deal with partial
     * successes.  No chance any of these will be problematic read-modify-write stuff.
     *
     * Also drop back if the ring-0 registration entry isn't actually used.
     */
    if (   RT_LIKELY(cbBuf <= sizeof(pVCpu->iom.s.PendingMmioWrite.abValue))
        && pRegEntry->cbRegion != 0
        && (  enmAccessType == PGMACCESSTYPE_READ
            ? pRegEntry->pfnReadCallback  != NULL || pVM->iomr0.s.paMmioRing3Regs[uUser].pfnReadCallback == NULL
            : pRegEntry->pfnWriteCallback != NULL || pVM->iomr0.s.paMmioRing3Regs[uUser].pfnWriteCallback == NULL)
        && pDevIns )
    { /* likely */ }
    else
    {
        Log4(("iomMmioHandlerNew: to ring-3: to-big=%RTbool zero-size=%RTbool no-callback=%RTbool pDevIns=%p hRegion=%#RX64\n",
              !(cbBuf <= sizeof(pVCpu->iom.s.PendingMmioWrite.abValue)), !(pRegEntry->cbRegion != 0),
              !(  enmAccessType == PGMACCESSTYPE_READ
                ? pRegEntry->pfnReadCallback  != NULL || pVM->iomr0.s.paMmioRing3Regs[uUser].pfnReadCallback == NULL
                : pRegEntry->pfnWriteCallback != NULL || pVM->iomr0.s.paMmioRing3Regs[uUser].pfnWriteCallback == NULL),
              pDevIns, uUser));
        STAM_COUNTER_INC(enmAccessType == PGMACCESSTYPE_READ ? &pStats->ReadRZToR3 : &pStats->WriteRZToR3);
        STAM_COUNTER_INC(enmAccessType == PGMACCESSTYPE_READ ? &pVM->iom.s.StatMmioReadsR0ToR3 : &pVM->iom.s.StatMmioWritesR0ToR3);
        return rcToRing3;
    }
#endif /* !IN_RING3 */

    /*
     * If we've got an offset that's outside the region, defer to ring-3 if we
     * can, or pretend there is nothing there.  This shouldn't happen, but can
     * if we're unlucky with an SMP VM and the guest isn't behaving very well.
     */
#ifdef IN_RING0
    RTGCPHYS const GCPhysMapping = pRegEntryR3->GCPhysMapping;
#else
    RTGCPHYS const GCPhysMapping = pRegEntry->GCPhysMapping;
#endif
    RTGCPHYS const offRegion     = GCPhysFault - GCPhysMapping;
    if (RT_LIKELY(offRegion < pRegEntry->cbRegion && GCPhysMapping != NIL_RTGCPHYS))
    { /* likely */ }
    else
    {
        STAM_REL_COUNTER_INC(&pVM->iom.s.StatMmioStaleMappings);
        LogRelMax(64, ("iomMmioHandlerNew: Stale access at %#RGp to range #%#x currently residing at %RGp LB %RGp\n",
                       GCPhysFault, pRegEntry->idxSelf, GCPhysMapping, pRegEntry->cbRegion));
#ifdef IN_RING3
        if (enmAccessType == PGMACCESSTYPE_READ)
            iomMMIODoReadFFs(pvBuf, cbBuf IOM_MMIO_STATS_COMMA_ARG);
        return VINF_SUCCESS;
#else
        STAM_COUNTER_INC(enmAccessType == PGMACCESSTYPE_READ ? &pStats->ReadRZToR3 : &pStats->WriteRZToR3);
        STAM_COUNTER_INC(enmAccessType == PGMACCESSTYPE_READ ? &pVM->iom.s.StatMmioReadsR0ToR3 : &pVM->iom.s.StatMmioWritesR0ToR3);
        return rcToRing3;
#endif
    }

    /*
     * Guard against device configurations causing recursive MMIO accesses
     * (see @bugref{10315}).
     */
    uint8_t const idxDepth = pVCpu->iom.s.cMmioRecursionDepth;
    if (RT_LIKELY(idxDepth < RT_ELEMENTS(pVCpu->iom.s.apMmioRecursionStack)))
    {
        pVCpu->iom.s.cMmioRecursionDepth  = idxDepth + 1;
        /** @todo Add iomr0 with a apMmioRecursionStack for ring-0. */
#ifdef IN_RING3
        pVCpu->iom.s.apMmioRecursionStack[idxDepth] = pDevIns;
#endif
    }
    else
    {
        STAM_REL_COUNTER_INC(&pVM->iom.s.StatMmioTooDeepRecursion);
#ifdef IN_RING3
        AssertCompile(RT_ELEMENTS(pVCpu->iom.s.apMmioRecursionStack) == 2);
        LogRelMax(64, ("iomMmioHandlerNew: Too deep recursion %RGp LB %#zx: %p (%s); %p (%s); %p (%s)\n",
                       GCPhysFault, cbBuf, pDevIns, pDevIns->pReg->szName,
                       pVCpu->iom.s.apMmioRecursionStack[1], pVCpu->iom.s.apMmioRecursionStack[1]->pReg->szName,
                       pVCpu->iom.s.apMmioRecursionStack[0], pVCpu->iom.s.apMmioRecursionStack[0]->pReg->szName));
#else
        LogRelMax(64, ("iomMmioHandlerNew: Too deep recursion %RGp LB %#zx!: %p (%s)\n",
                       GCPhysFault, cbBuf, pDevIns, pDevIns->pReg->szName));
#endif
        return VINF_PGM_HANDLER_DO_DEFAULT;
    }


    /*
     * Perform locking and the access.
     *
     * Writes requiring a return to ring-3 are buffered by IOM so IEM can
     * commit the instruction.
     *
     * Note! We may end up locking the device even when the relevant callback is
     *       NULL.  This is supposed to be an unlikely case, so not optimized yet.
     *
     * Note! All returns goes thru the one return statement at the end of the
     *       function in order to correctly maintaint the recursion counter.
     */
    VBOXSTRICTRC rcStrict = PDMCritSectEnter(pVM, pDevIns->CTX_SUFF(pCritSectRo), rcToRing3);
    if (rcStrict == VINF_SUCCESS)
    {
        if (enmAccessType == PGMACCESSTYPE_READ)
        {
            /*
             * Read.
             */
            rcStrict = iomMmioDoRead(pVM, pRegEntry, GCPhysFault, offRegion, pvBuf, (uint32_t)cbBuf IOM_MMIO_STATS_COMMA_ARG);

            PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));
#ifndef IN_RING3
            if (rcStrict == VINF_IOM_R3_MMIO_READ)
            {
                STAM_COUNTER_INC(&pStats->ReadRZToR3);
                STAM_COUNTER_INC(&pVM->iom.s.StatMmioReadsR0ToR3);
            }
            else
#endif
                STAM_COUNTER_INC(&pStats->Reads);
            STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfRead), Prf);
        }
        else
        {
            /*
             * Write.
             */
            rcStrict = iomMmioDoWrite(pVM, pVCpu, pRegEntry, GCPhysFault, offRegion, pvBuf, (uint32_t)cbBuf IOM_MMIO_STATS_COMMA_ARG);
            PDMCritSectLeave(pVM, pDevIns->CTX_SUFF(pCritSectRo));
#ifndef IN_RING3
            if (rcStrict == VINF_IOM_R3_MMIO_WRITE)
                rcStrict = iomMmioRing3WritePending(pVCpu, GCPhysFault, pvBuf, cbBuf, pRegEntry->idxSelf);
            if (rcStrict == VINF_IOM_R3_MMIO_WRITE)
            {
                STAM_COUNTER_INC(&pStats->WriteRZToR3);
                STAM_COUNTER_INC(&pVM->iom.s.StatMmioWritesR0ToR3);
            }
            else if (rcStrict == VINF_IOM_R3_MMIO_COMMIT_WRITE)
            {
                STAM_COUNTER_INC(&pStats->CommitRZToR3);
                STAM_COUNTER_INC(&pVM->iom.s.StatMmioCommitsR0ToR3);
            }
            else
#endif
                STAM_COUNTER_INC(&pStats->Writes);
            STAM_PROFILE_STOP(&pStats->CTX_SUFF_Z(ProfWrite), Prf);
        }

        /*
         * Check the return code.
         */
#ifdef IN_RING3
        AssertMsg(rcStrict == VINF_SUCCESS, ("%Rrc -  Access type %d - %RGp - %s\n",
                                             VBOXSTRICTRC_VAL(rcStrict), enmAccessType, GCPhysFault, pRegEntry->pszDesc));
#else
        AssertMsg(   rcStrict == VINF_SUCCESS
                  || rcStrict == rcToRing3
                  || (rcStrict == VINF_IOM_R3_MMIO_COMMIT_WRITE && enmAccessType == PGMACCESSTYPE_WRITE)
                  || rcStrict == VINF_EM_DBG_STOP
                  || rcStrict == VINF_EM_DBG_EVENT
                  || rcStrict == VINF_EM_DBG_BREAKPOINT
                  || rcStrict == VINF_EM_OFF
                  || rcStrict == VINF_EM_SUSPEND
                  || rcStrict == VINF_EM_RESET
                  //|| rcStrict == VINF_EM_HALT       /* ?? */
                  //|| rcStrict == VINF_EM_NO_MEMORY  /* ?? */
                  , ("%Rrc - Access type %d - %RGp - %s #%u\n",
                     VBOXSTRICTRC_VAL(rcStrict), enmAccessType, GCPhysFault, pDevIns->pReg->szName, pDevIns->iInstance));
#endif
    }
    /*
     * Deal with enter-critsect failures.
     */
#ifndef IN_RING3
    else if (rcStrict == VINF_IOM_R3_MMIO_WRITE)
    {
        Assert(enmAccessType == PGMACCESSTYPE_WRITE);
        rcStrict = iomMmioRing3WritePending(pVCpu, GCPhysFault, pvBuf, cbBuf, pRegEntry->idxSelf);
        if (rcStrict == VINF_IOM_R3_MMIO_COMMIT_WRITE)
        {
            STAM_COUNTER_INC(&pStats->CommitRZToR3);
            STAM_COUNTER_INC(&pVM->iom.s.StatMmioCommitsR0ToR3);
        }
        else
        {
            STAM_COUNTER_INC(&pStats->WriteRZToR3);
            STAM_COUNTER_INC(&pVM->iom.s.StatMmioWritesR0ToR3);
        }
        STAM_COUNTER_INC(&pVM->iom.s.StatMmioDevLockContentionR0);
    }
    else if (rcStrict == VINF_IOM_R3_MMIO_READ)
    {
        Assert(enmAccessType == PGMACCESSTYPE_READ);
        STAM_COUNTER_INC(&pStats->ReadRZToR3);
        STAM_COUNTER_INC(&pVM->iom.s.StatMmioDevLockContentionR0);
    }
#endif
    else
        AssertMsg(RT_FAILURE_NP(rcStrict), ("%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));

    pVCpu->iom.s.cMmioRecursionDepth = idxDepth;
    return rcStrict;
}


/**
 * Mapping an MMIO2 page in place of an MMIO page for direct access.
 *
 * This is a special optimization used by the VGA device.  Call
 * IOMMmioResetRegion() to undo the mapping.
 *
 * @returns VBox status code.  This API may return VINF_SUCCESS even if no
 *          remapping is made.
 * @retval  VERR_SEM_BUSY in ring-0 if we cannot get the IOM lock.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         The device instance @a hRegion and @a hMmio2 are
 *                          associated with.
 * @param   hRegion         The handle to the MMIO region.
 * @param   offRegion       The offset into @a hRegion of the page to be
 *                          remapped.
 * @param   hMmio2          The MMIO2 handle.
 * @param   offMmio2        Offset into @a hMmio2 of the page to be use for the
 *                          mapping.
 * @param   fPageFlags      Page flags to set. Must be (X86_PTE_RW | X86_PTE_P)
 *                          for the time being.
 */
VMMDECL(int) IOMMmioMapMmio2Page(PVMCC pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion, RTGCPHYS offRegion,
                                 uint64_t hMmio2, RTGCPHYS offMmio2, uint64_t fPageFlags)
{
    /* Currently only called from the VGA device during MMIO. */
    Log(("IOMMmioMapMmio2Page %#RX64/%RGp -> %#RX64/%RGp flags=%RX64\n", hRegion, offRegion, hMmio2, offMmio2, fPageFlags));
    AssertReturn(fPageFlags == (X86_PTE_RW | X86_PTE_P), VERR_INVALID_PARAMETER);
    AssertReturn(pDevIns, VERR_INVALID_POINTER);

#if defined(VBOX_VMM_TARGET_ARMV8)
    /** @todo NEM: MMIO page aliasing. */
    RT_NOREF(pVM, hRegion, offRegion, hMmio2, offMmio2);
    return VINF_SUCCESS;    /* ignore */ /** @todo return some indicator if we fail here */
#else
/** @todo Why is this restricted to protected mode???  Try it in all modes! */
    PVMCPUCC pVCpu = VMMGetCpu(pVM);

    /* This currently only works in real mode, protected mode without paging or with nested paging. */
    /** @todo NEM: MMIO page aliasing. */
    if (    !HMIsEnabled(pVM)       /* useless without VT-x/AMD-V */
        ||  (   CPUMIsGuestInPagedProtectedMode(pVCpu)
             && !HMIsNestedPagingActive(pVM)))
        return VINF_SUCCESS;    /* ignore */ /** @todo return some indicator if we fail here */

    /*
     * Translate the handle into an entry and check the region offset.
     */
    AssertReturn(hRegion < RT_MIN(pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc), VERR_IOM_INVALID_MMIO_HANDLE);
# ifdef IN_RING0
    AssertReturn(hRegion < pVM->iomr0.s.cMmioAlloc, VERR_IOM_INVALID_MMIO_HANDLE);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iomr0.s.paMmioRing3Regs[hRegion];
    AssertReturn(pRegEntry->cbRegion > 0, VERR_IOM_INVALID_MMIO_HANDLE);
    AssertReturn(offRegion < pVM->iomr0.s.paMmioRegs[hRegion].cbRegion, VERR_OUT_OF_RANGE);
    AssertReturn(   pVM->iomr0.s.paMmioRegs[hRegion].pDevIns == pDevIns
                 || (   pVM->iomr0.s.paMmioRegs[hRegion].pDevIns == NULL
                     && pRegEntry->pDevIns == pDevIns->pDevInsForR3), VERR_ACCESS_DENIED);
# else
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->cbRegion > 0, VERR_IOM_INVALID_MMIO_HANDLE);
    AssertReturn(pRegEntry->pDevIns == pDevIns, VERR_ACCESS_DENIED);
# endif
    AssertReturn(offRegion < pRegEntry->cbRegion, VERR_OUT_OF_RANGE);
    Assert((pRegEntry->cbRegion & GUEST_PAGE_OFFSET_MASK) == 0);

    /*
     * When getting and using the mapping address, we must sit on the IOM lock
     * to prevent remapping.  Shared suffices as we change nothing.
     */
    int rc = IOM_LOCK_SHARED(pVM);
    if (rc == VINF_SUCCESS)
    {
        RTGCPHYS const GCPhys = pRegEntry->fMapped ? pRegEntry->GCPhysMapping : NIL_RTGCPHYS;
        if (GCPhys != NIL_RTGCPHYS)
        {
            Assert(!(GCPhys & GUEST_PAGE_OFFSET_MASK));

            /*
             * Do the aliasing; page align the addresses since PGM is picky.
             */
            rc = PGMHandlerPhysicalPageAliasMmio2(pVM, GCPhys, GCPhys + (offRegion & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK),
                                                  pDevIns, hMmio2, offMmio2);
        }
        else
            AssertFailedStmt(rc = VERR_IOM_MMIO_REGION_NOT_MAPPED);

        IOM_UNLOCK_SHARED(pVM);
    }

/** @todo either ditch this or replace it with something that works in the
 *        nested case, since we really only care about nested paging! */
# if 0
    /*
     * Modify the shadow page table. Since it's an MMIO page it won't be present and we
     * can simply prefetch it.
     *
     * Note: This is a NOP in the EPT case; we'll just let it fault again to resync the page.
     */
#  if 0 /* The assertion is wrong for the PGM_SYNC_CLEAR_PGM_POOL and VINF_PGM_HANDLER_ALREADY_ALIASED cases. */
#   ifdef VBOX_STRICT
    uint64_t fFlags;
    RTHCPHYS HCPhys;
    rc = PGMShwGetPage(pVCpu, (RTGCPTR)GCPhys, &fFlags, &HCPhys);
    Assert(rc == VERR_PAGE_NOT_PRESENT || rc == VERR_PAGE_TABLE_NOT_PRESENT);
#   endif
#  endif
    rc = PGMPrefetchPage(pVCpu, (RTGCPTR)GCPhys);
    Assert(rc == VINF_SUCCESS || rc == VERR_PAGE_NOT_PRESENT || rc == VERR_PAGE_TABLE_NOT_PRESENT);
# endif
    return rc;
#endif
}


#ifdef IN_RING0 /* VT-x ring-0 only, move to IOMR0Mmio.cpp later.  */
/**
 * Mapping a HC page in place of an MMIO page for direct access.
 *
 * This is a special optimization used by the APIC in the VT-x case.  This VT-x
 * code uses PGMHandlerPhysicalReset rather than IOMMmioResetRegion() to undo
 * the effects here.
 *
 * @todo Make VT-x usage more consistent.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhys          The address of the MMIO page to be changed.
 * @param   HCPhys          The address of the host physical page.
 * @param   fPageFlags      Page flags to set. Must be (X86_PTE_RW | X86_PTE_P)
 *                          for the time being.
 */
VMMR0_INT_DECL(int) IOMR0MmioMapMmioHCPage(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhys, RTHCPHYS HCPhys, uint64_t fPageFlags)
{
    /* Currently only called from VT-x code during a page fault. */
    Log(("IOMR0MmioMapMmioHCPage %RGp -> %RGp flags=%RX64\n", GCPhys, HCPhys, fPageFlags));

    AssertReturn(fPageFlags == (X86_PTE_RW | X86_PTE_P), VERR_INVALID_PARAMETER);
    /** @todo NEM: MMIO page aliasing?? */
    Assert(HMIsEnabled(pVM));

# ifdef VBOX_STRICT
    /*
     * Check input address (it's HM calling, not the device, so no region handle).
     */
    int rcSem = IOM_LOCK_SHARED(pVM);
    if (rcSem == VINF_SUCCESS)
    {
        RTGCPHYS offIgn;
        uint16_t idxIgn = UINT16_MAX;
        PIOMMMIOENTRYR0 pRegEntry = iomMmioGetEntry(pVM, GCPhys, &offIgn, &idxIgn);
        IOM_UNLOCK_SHARED(pVM);
        Assert(pRegEntry);
        Assert(pRegEntry && !(pRegEntry->cbRegion & GUEST_PAGE_OFFSET_MASK));
    }
# endif

    /*
     * Do the aliasing; page align the addresses since PGM is picky.
     */
    GCPhys &= ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    HCPhys &= ~(RTHCPHYS)GUEST_PAGE_OFFSET_MASK;

    int rc = PGMHandlerPhysicalPageAliasHC(pVM, GCPhys, GCPhys, HCPhys);
    AssertRCReturn(rc, rc);

/** @todo either ditch this or replace it with something that works in the
 *        nested case, since we really only care about nested paging! */

    /*
     * Modify the shadow page table. Since it's an MMIO page it won't be present and we
     * can simply prefetch it.
     *
     * Note: This is a NOP in the EPT case; we'll just let it fault again to resync the page.
     */
    rc = PGMPrefetchPage(pVCpu, (RTGCPTR)GCPhys);
    Assert(rc == VINF_SUCCESS || rc == VERR_PAGE_NOT_PRESENT || rc == VERR_PAGE_TABLE_NOT_PRESENT);
    return VINF_SUCCESS;
}
#endif


/**
 * Reset a previously modified MMIO region; restore the access flags.
 *
 * This undoes the effects of IOMMmioMapMmio2Page() and is currently only
 * intended for some ancient VGA hack.  However, it would be great to extend it
 * beyond VT-x and/or nested-paging.
 *
 * @returns VBox status code.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         The device instance @a hRegion is associated with.
 * @param   hRegion         The handle to the MMIO region.
 */
VMMDECL(int) IOMMmioResetRegion(PVMCC pVM, PPDMDEVINS pDevIns, IOMMMIOHANDLE hRegion)
{
    Log(("IOMMMIOResetRegion %#RX64\n", hRegion));
    AssertReturn(pDevIns, VERR_INVALID_POINTER);

#if defined(VBOX_VMM_TARGET_ARMV8)
    /** @todo NEM: MMIO page aliasing. */
    RT_NOREF(pVM, hRegion);
    return VINF_SUCCESS;    /* ignore */ /** @todo return some indicator if we fail here */
#else
/** @todo Get rid of this this real/protected or nested paging restriction,
 *        it probably shouldn't be here and would be nasty when the CPU
 *        changes mode while we have the hack enabled... */
    PVMCPUCC pVCpu = VMMGetCpu(pVM);

    /* This currently only works in real mode, protected mode without paging or with nested paging. */
    /** @todo NEM: MMIO page aliasing. */
    if (   !HMIsEnabled(pVM)       /* useless without VT-x/AMD-V */
        || (   CPUMIsGuestInPagedProtectedMode(pVCpu)
            && !HMIsNestedPagingActive(pVM)))
        return VINF_SUCCESS;    /* ignore */

    /*
     * Translate the handle into an entry and mapping address for PGM.
     * We have to take the lock to safely access the mapping address here.
     */
    AssertReturn(hRegion < RT_MIN(pVM->iom.s.cMmioRegs, pVM->iom.s.cMmioAlloc), VERR_IOM_INVALID_MMIO_HANDLE);
# ifdef IN_RING0
    AssertReturn(hRegion < pVM->iomr0.s.cMmioAlloc, VERR_IOM_INVALID_MMIO_HANDLE);
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iomr0.s.paMmioRing3Regs[hRegion];
    AssertReturn(pRegEntry->cbRegion > 0, VERR_IOM_INVALID_MMIO_HANDLE);
    AssertReturn(   pVM->iomr0.s.paMmioRegs[hRegion].pDevIns == pDevIns
                 || (   pVM->iomr0.s.paMmioRegs[hRegion].pDevIns == NULL
                     && pRegEntry->pDevIns == pDevIns->pDevInsForR3), VERR_ACCESS_DENIED);
# else
    PIOMMMIOENTRYR3 const pRegEntry = &pVM->iom.s.paMmioRegs[hRegion];
    AssertReturn(pRegEntry->cbRegion > 0, VERR_IOM_INVALID_MMIO_HANDLE);
    AssertReturn(pRegEntry->pDevIns == pDevIns, VERR_ACCESS_DENIED);
# endif
    Assert((pRegEntry->cbRegion & GUEST_PAGE_OFFSET_MASK) == 0);

    int rcSem = IOM_LOCK_SHARED(pVM);
    RTGCPHYS GCPhys = pRegEntry->fMapped ? pRegEntry->GCPhysMapping : NIL_RTGCPHYS;
    if (rcSem == VINF_SUCCESS)
        IOM_UNLOCK_SHARED(pVM);

    Assert(!(GCPhys              & GUEST_PAGE_OFFSET_MASK));
    Assert(!(pRegEntry->cbRegion & GUEST_PAGE_OFFSET_MASK));

    /*
     * Call PGM to do the job work.
     *
     * After the call, all the pages should be non-present, unless there is
     * a page pool flush pending (unlikely).
     */
    int rc = PGMHandlerPhysicalReset(pVM, GCPhys);
    AssertRC(rc);

# ifdef VBOX_STRICT
    if (!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_PGM_SYNC_CR3))
    {
        RTGCPHYS cb = pRegEntry->cbRegion;
        while (cb)
        {
            uint64_t fFlags;
            RTHCPHYS HCPhys;
            rc = PGMShwGetPage(pVCpu, (RTGCPTR)GCPhys, &fFlags, &HCPhys);
            Assert(rc == VERR_PAGE_NOT_PRESENT || rc == VERR_PAGE_TABLE_NOT_PRESENT);
            cb     -= RT_MIN(GUEST_PAGE_SIZE, HOST_PAGE_SIZE);
            GCPhys += RT_MIN(GUEST_PAGE_SIZE, HOST_PAGE_SIZE);
        }
    }
# endif
    return rc;
#endif
}

