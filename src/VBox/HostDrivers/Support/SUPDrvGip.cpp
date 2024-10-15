/* $Id: SUPDrvGip.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - Common code for GIP.
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
#define LOG_GROUP LOG_GROUP_SUP_DRV
#define SUPDRV_AGNOSTIC
#include "SUPDrvInternal.h"
#ifndef PAGE_SHIFT
# include <iprt/param.h>
#endif
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/asm-math.h>
#include <iprt/cpuset.h>
#include <iprt/handletable.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/power.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#include <iprt/net.h>
#include <iprt/crc.h>
#include <iprt/string.h>
#include <iprt/timer.h>
#if defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
# include <iprt/rand.h>
# include <iprt/path.h>
#endif
#include <iprt/uint128.h>
#include <iprt/x86.h>

#include <VBox/param.h>
#include <VBox/log.h>
#include <VBox/err.h>

#if defined(RT_OS_SOLARIS) || defined(RT_OS_DARWIN)
# include "dtrace/SUPDrv.h"
#else
/* ... */
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The frequency by which we recalculate the u32UpdateHz and
 * u32UpdateIntervalNS GIP members. The value must be a power of 2.
 *
 * Warning: Bumping this too high might overflow u32UpdateIntervalNS.
 */
#define GIP_UPDATEHZ_RECALC_FREQ            0x800

/** A reserved TSC value used for synchronization as well as measurement of
 *  TSC deltas. */
#define GIP_TSC_DELTA_RSVD                  UINT64_MAX
/** The number of TSC delta measurement loops in total (includes primer and
 *  read-time loops). */
#define GIP_TSC_DELTA_LOOPS                 96
/** The number of cache primer loops. */
#define GIP_TSC_DELTA_PRIMER_LOOPS          4
/** The number of loops until we keep computing the minumum read time. */
#define GIP_TSC_DELTA_READ_TIME_LOOPS       24

/** The TSC frequency refinement period in seconds.
 * The timer fires after 200ms, then every second, this value just says when
 * to stop it after that. */
#define GIP_TSC_REFINE_PERIOD_IN_SECS       12
/** The TSC-delta threshold for the SUPGIPUSETSCDELTA_PRACTICALLY_ZERO rating */
#define GIP_TSC_DELTA_THRESHOLD_PRACTICALLY_ZERO    32
/** The TSC-delta threshold for the SUPGIPUSETSCDELTA_ROUGHLY_ZERO rating */
#define GIP_TSC_DELTA_THRESHOLD_ROUGHLY_ZERO        448
/** The TSC delta value for the initial GIP master - 0 in regular builds.
 * To test the delta code this can be set to a non-zero value.  */
#if 0
# define GIP_TSC_DELTA_INITIAL_MASTER_VALUE INT64_C(170139095182512) /* 0x00009abd9854acb0 */
#else
# define GIP_TSC_DELTA_INITIAL_MASTER_VALUE INT64_C(0)
#endif

AssertCompile(GIP_TSC_DELTA_PRIMER_LOOPS < GIP_TSC_DELTA_READ_TIME_LOOPS);
AssertCompile(GIP_TSC_DELTA_PRIMER_LOOPS + GIP_TSC_DELTA_READ_TIME_LOOPS < GIP_TSC_DELTA_LOOPS);

/** @def VBOX_SVN_REV
 * The makefile should define this if it can. */
#ifndef VBOX_SVN_REV
# define VBOX_SVN_REV 0
#endif

#if 0 /* Don't start the GIP timers. Useful when debugging the IPRT timer code. */
# define DO_NOT_START_GIP
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(void)   supdrvGipSyncAndInvariantTimer(PRTTIMER pTimer, void *pvUser, uint64_t iTick);
static DECLCALLBACK(void)   supdrvGipAsyncTimer(PRTTIMER pTimer, void *pvUser, uint64_t iTick);
static int                  supdrvGipSetFlags(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uint32_t fOrMask, uint32_t fAndMask);
static void                 supdrvGipInitCpu(PSUPGLOBALINFOPAGE pGip, PSUPGIPCPU pCpu, uint64_t u64NanoTS, uint64_t uCpuHz);
static void                 supdrvTscResetSamples(PSUPDRVDEVEXT pDevExt, bool fClearDeltas);
#ifdef SUPDRV_USE_TSC_DELTA_THREAD
static int                  supdrvTscDeltaThreadInit(PSUPDRVDEVEXT pDevExt);
static void                 supdrvTscDeltaTerm(PSUPDRVDEVEXT pDevExt);
static void                 supdrvTscDeltaThreadStartMeasurement(PSUPDRVDEVEXT pDevExt, bool fForceAll);
#else
static int                  supdrvTscMeasureInitialDeltas(PSUPDRVDEVEXT pDevExt);
static int                  supdrvTscMeasureDeltaOne(PSUPDRVDEVEXT pDevExt, uint32_t idxWorker);
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
DECLEXPORT(PSUPGLOBALINFOPAGE) g_pSUPGlobalInfoPage = NULL;
SUPR0_EXPORT_SYMBOL(g_pSUPGlobalInfoPage);



/*
 *
 * Misc Common GIP Code
 * Misc Common GIP Code
 * Misc Common GIP Code
 *
 *
 */


/**
 * Finds the GIP CPU index corresponding to @a idCpu.
 *
 * @returns GIP CPU array index, UINT32_MAX if not found.
 * @param   pGip                The GIP.
 * @param   idCpu               The CPU ID.
 */
static uint32_t supdrvGipFindCpuIndexForCpuId(PSUPGLOBALINFOPAGE pGip, RTCPUID idCpu)
{
    uint32_t i;
    for (i = 0; i < pGip->cCpus; i++)
        if (pGip->aCPUs[i].idCpu == idCpu)
            return i;
    return UINT32_MAX;
}


/**
 * Gets the APIC ID using the best available method.
 *
 * @returns APIC ID.
 * @param   pGip                The GIP, for SUPGIPGETCPU_XXX.
 */
DECLINLINE(uint32_t) supdrvGipGetApicId(PSUPGLOBALINFOPAGE pGip)
{
    if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_0B)
        return ASMGetApicIdExt0B();
    if (pGip->fGetGipCpu & SUPGIPGETCPU_APIC_ID_EXT_8000001E)
        return ASMGetApicIdExt8000001E();
    return ASMGetApicId();
}


/**
 * Gets the APIC ID using the best available method, slow version.
 */
static uint32_t supdrvGipGetApicIdSlow(void)
{
    uint32_t const idApic = ASMGetApicId();

    /* The Intel CPU topology leaf: */
    uint32_t uOther = ASMCpuId_EAX(0);
    if (uOther >= UINT32_C(0xb) && RTX86IsValidStdRange(uOther))
    {
        uint32_t uEax = 0;
        uint32_t uEbx = 0;
        uint32_t uEcx = 0;
        uint32_t uEdx = 0;
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
        ASMCpuId_Idx_ECX(0xb, 0, &uEax, &uEbx, &uEcx, &uEdx);
#else
        ASMCpuIdExSlow(0xb, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
#endif
        if ((uEcx >> 8) != 0) /* level type != invalid */
        {
            if ((uEdx & 0xff) == idApic)
                return uEdx;
            AssertMsgFailed(("ASMGetApicIdExt0B=>%#x idApic=%#x\n", uEdx, idApic));
        }
    }

    /* The AMD leaf: */
    uOther = ASMCpuId_EAX(UINT32_C(0x80000000));
    if (uOther >= UINT32_C(0x8000001e) && RTX86IsValidExtRange(uOther))
    {
        uOther = ASMGetApicIdExt8000001E();
        if ((uOther & 0xff) == idApic)
            return uOther;
        AssertMsgFailed(("ASMGetApicIdExt8000001E=>%#x idApic=%#x\n", uOther, idApic));
    }
    return idApic;
}


/*
 *
 * GIP Mapping and Unmapping Related Code.
 * GIP Mapping and Unmapping Related Code.
 * GIP Mapping and Unmapping Related Code.
 *
 *
 */


/**
 * (Re-)initializes the per-cpu structure prior to starting or resuming the GIP
 * updating.
 *
 * @param   pGipCpu          The per CPU structure for this CPU.
 * @param   u64NanoTS        The current time.
 */
static void supdrvGipReInitCpu(PSUPGIPCPU pGipCpu, uint64_t u64NanoTS)
{
    /*
     * Here we don't really care about applying the TSC delta. The re-initialization of this
     * value is not relevant especially while (re)starting the GIP as the first few ones will
     * be ignored anyway, see supdrvGipDoUpdateCpu().
     */
    pGipCpu->u64TSC    = ASMReadTSC() - pGipCpu->u32UpdateIntervalTSC;
    pGipCpu->u64NanoTS = u64NanoTS;
}


/**
 * Set the current TSC and NanoTS value for the CPU.
 *
 * @param   idCpu            The CPU ID. Unused - we have to use the APIC ID.
 * @param   pvUser1          Pointer to the ring-0 GIP mapping.
 * @param   pvUser2          Pointer to the variable holding the current time.
 */
static DECLCALLBACK(void) supdrvGipReInitCpuCallback(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PSUPGLOBALINFOPAGE  pGip   = (PSUPGLOBALINFOPAGE)pvUser1;
    uint32_t const      idApic = supdrvGipGetApicId(pGip);
    if (idApic < RT_ELEMENTS(pGip->aiCpuFromApicId))
    {
        unsigned const  iCpu   = pGip->aiCpuFromApicId[idApic];

        if (RT_LIKELY(iCpu < pGip->cCpus && pGip->aCPUs[iCpu].idCpu == idCpu))
            supdrvGipReInitCpu(&pGip->aCPUs[iCpu], *(uint64_t *)pvUser2);
        else
            LogRelMax(64, ("supdrvGipReInitCpuCallback: iCpu=%#x out of bounds (%#zx, idApic=%#x)\n",
                           iCpu, RT_ELEMENTS(pGip->aiCpuFromApicId), idApic));
    }
    else
        LogRelMax(64, ("supdrvGipReInitCpuCallback: idApic=%#x out of bounds (%#zx)\n",
                       idApic, RT_ELEMENTS(pGip->aiCpuFromApicId)));

    NOREF(pvUser2);
}


/**
 * State structure for supdrvGipDetectGetGipCpuCallback.
 */
typedef struct SUPDRVGIPDETECTGETCPU
{
    /** Bitmap of APIC IDs that has been seen (initialized to zero).
     *  Used to detect duplicate APIC IDs (paranoia). */
    uint8_t volatile    bmApicId[4096 / 8];
    /** Mask of supported GIP CPU getter methods (SUPGIPGETCPU_XXX) (all bits set
     *  initially). The callback clears the methods not detected. */
    uint32_t volatile   fSupported;
    /** The first callback detecting any kind of range issues (initialized to
     * NIL_RTCPUID). */
    RTCPUID volatile    idCpuProblem;
} SUPDRVGIPDETECTGETCPU;
/** Pointer to state structure for supdrvGipDetectGetGipCpuCallback. */
typedef SUPDRVGIPDETECTGETCPU *PSUPDRVGIPDETECTGETCPU;


/**
 * Checks for alternative ways of getting the CPU ID.
 *
 * This also checks the APIC ID, CPU ID and CPU set index values against the
 * GIP tables.
 *
 * @param   idCpu            The CPU ID. Unused - we have to use the APIC ID.
 * @param   pvUser1          Pointer to the state structure.
 * @param   pvUser2          Pointer to the GIP.
 */
static DECLCALLBACK(void) supdrvGipDetectGetGipCpuCallback(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PSUPDRVGIPDETECTGETCPU  pState = (PSUPDRVGIPDETECTGETCPU)pvUser1;
    PSUPGLOBALINFOPAGE      pGip   = (PSUPGLOBALINFOPAGE)pvUser2;
    uint32_t                fSupported = 0;
    uint32_t                idApic;
    uint32_t                uEax, uEbx, uEcx, uEdx;
    int                     iCpuSet;
    NOREF(pGip);

    AssertMsg(idCpu == RTMpCpuId(), ("idCpu=%#x RTMpCpuId()=%#x\n", idCpu, RTMpCpuId())); /* paranoia^3 */

    /*
     * Check that the CPU ID and CPU set index are interchangable.
     */
    iCpuSet = RTMpCpuIdToSetIndex(idCpu);
    if ((RTCPUID)iCpuSet == idCpu)
    {
        AssertCompile(RT_IS_POWER_OF_TWO(RTCPUSET_MAX_CPUS));
        if (   iCpuSet >= 0
            && iCpuSet < RTCPUSET_MAX_CPUS
            && RT_IS_POWER_OF_TWO(RTCPUSET_MAX_CPUS))
        {
            PSUPGIPCPU pGipCpu = SUPGetGipCpuBySetIndex(pGip, iCpuSet);

            /*
             * Check whether the IDTR.LIMIT contains a CPU number.
             */
#ifdef RT_ARCH_X86
            uint16_t const  cbIdt = sizeof(X86DESC64SYSTEM) * 256;
#else
            uint16_t const  cbIdt = sizeof(X86DESCGATE)     * 256;
#endif
            RTIDTR          Idtr;
            ASMGetIDTR(&Idtr);
            if (Idtr.cbIdt >= cbIdt)
            {
                uint32_t uTmp = Idtr.cbIdt - cbIdt;
                uTmp &= RTCPUSET_MAX_CPUS - 1;
                if (uTmp == idCpu)
                {
                    RTIDTR Idtr2;
                    ASMGetIDTR(&Idtr2);
                    if (Idtr2.cbIdt == Idtr.cbIdt)
                        fSupported |= SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS;
                }
            }

            /*
             * Check whether RDTSCP is an option.
             */
            if (ASMHasCpuId())
            {
                if (   RTX86IsValidExtRange(ASMCpuId_EAX(UINT32_C(0x80000000)))
                    && (ASMCpuId_EDX(UINT32_C(0x80000001)) & X86_CPUID_EXT_FEATURE_EDX_RDTSCP) )
                {
                    uint32_t uAux;
                    ASMReadTscWithAux(&uAux);
                    if ((uAux & (RTCPUSET_MAX_CPUS - 1)) == idCpu)
                    {
                        ASMNopPause();
                        ASMReadTscWithAux(&uAux);
                        if ((uAux & (RTCPUSET_MAX_CPUS - 1)) == idCpu)
                            fSupported |= SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS;
                    }

                    if (pGipCpu)
                    {
                        uint32_t const  uGroupedAux = (uint8_t)pGipCpu->iCpuGroupMember | ((uint32_t)pGipCpu->iCpuGroup << 8);
                        if (   (uAux & UINT16_MAX) == uGroupedAux
                            && pGipCpu->iCpuGroupMember <= UINT8_MAX)
                        {
                            ASMNopPause();
                            ASMReadTscWithAux(&uAux);
                            if ((uAux & UINT16_MAX) == uGroupedAux)
                                fSupported |= SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL;
                        }
                    }
                }
            }
        }
    }

    /*
     * Check for extended APIC ID methods.
     */
    idApic = UINT32_MAX;
    uEax = ASMCpuId_EAX(0);
    if (uEax >= UINT32_C(0xb) && RTX86IsValidStdRange(uEax))
    {
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
        ASMCpuId_Idx_ECX(0xb, 0, &uEax, &uEbx, &uEcx, &uEdx);
#else
        ASMCpuIdExSlow(0xb, 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
#endif
        if ((uEcx >> 8) != 0) /* level type != invalid */
        {
            if (RT_LIKELY(   uEdx < RT_ELEMENTS(pGip->aiCpuFromApicId)
                          && !ASMBitTest(pState->bmApicId, uEdx)))
            {
                if (uEdx == ASMGetApicIdExt0B())
                {
                    idApic = uEdx;
                    fSupported |= SUPGIPGETCPU_APIC_ID_EXT_0B;
                }
                else
                    AssertMsgFailed(("%#x vs %#x\n", uEdx, ASMGetApicIdExt0B()));
            }
        }
    }

    uEax = ASMCpuId_EAX(UINT32_C(0x80000000));
    if (uEax >= UINT32_C(0x8000001e) && RTX86IsValidExtRange(uEax))
    {
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
        ASMCpuId_Idx_ECX(UINT32_C(0x8000001e), 0, &uEax, &uEbx, &uEcx, &uEdx);
#else
        ASMCpuIdExSlow(UINT32_C(0x8000001e), 0, 0, 0, &uEax, &uEbx, &uEcx, &uEdx);
#endif
        if (uEax || uEbx || uEcx || uEdx)
        {
            if (RT_LIKELY(   uEax < RT_ELEMENTS(pGip->aiCpuFromApicId)
                          && (   idApic == UINT32_MAX
                              || idApic == uEax)
                          && !ASMBitTest(pState->bmApicId, uEax)))
            {
                if (uEax == ASMGetApicIdExt8000001E())
                {
                    idApic = uEax;
                    fSupported |= SUPGIPGETCPU_APIC_ID_EXT_8000001E;
                }
                else
                    AssertMsgFailed(("%#x vs %#x\n", uEax, ASMGetApicIdExt8000001E()));
            }
        }
    }

    /*
     * Check that the APIC ID is unique.
     */
    uEax = ASMGetApicId();
    if (RT_LIKELY(   uEax < RT_ELEMENTS(pGip->aiCpuFromApicId)
                  && (   idApic == UINT32_MAX
                      || idApic == uEax)
                  && !ASMAtomicBitTestAndSet(pState->bmApicId, uEax)))
    {
        idApic = uEax;
        fSupported |= SUPGIPGETCPU_APIC_ID;
    }
    else if (   idApic == UINT32_MAX
             || idApic >= RT_ELEMENTS(pGip->aiCpuFromApicId) /* parnaoia */
             || ASMAtomicBitTestAndSet(pState->bmApicId, idApic))
    {
        AssertCompile(sizeof(pState->bmApicId) * 8 == RT_ELEMENTS(pGip->aiCpuFromApicId));
        ASMAtomicCmpXchgU32(&pState->idCpuProblem, idCpu, NIL_RTCPUID);
        LogRel(("supdrvGipDetectGetGipCpuCallback: idCpu=%#x iCpuSet=%d idApic=%#x/%#x - duplicate APIC ID.\n",
                idCpu, iCpuSet, uEax, idApic));
    }

    /*
     * Check that the iCpuSet is within the expected range.
     */
    if (RT_UNLIKELY(   iCpuSet < 0
                    || (unsigned)iCpuSet >= RTCPUSET_MAX_CPUS
                    || (unsigned)iCpuSet >= RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)))
    {
        ASMAtomicCmpXchgU32(&pState->idCpuProblem, idCpu, NIL_RTCPUID);
        LogRel(("supdrvGipDetectGetGipCpuCallback: idCpu=%#x iCpuSet=%d idApic=%#x - CPU set index is out of range.\n",
                idCpu, iCpuSet, idApic));
    }
    else
    {
        RTCPUID idCpu2 = RTMpCpuIdFromSetIndex(iCpuSet);
        if (RT_UNLIKELY(idCpu2 != idCpu))
        {
            ASMAtomicCmpXchgU32(&pState->idCpuProblem, idCpu, NIL_RTCPUID);
            LogRel(("supdrvGipDetectGetGipCpuCallback: idCpu=%#x iCpuSet=%d idApic=%#x - CPU id/index roundtrip problem: %#x\n",
                    idCpu, iCpuSet, idApic, idCpu2));
        }
    }

    /*
     * Update the supported feature mask before we return.
     */
    ASMAtomicAndU32(&pState->fSupported, fSupported);

    NOREF(pvUser2);
}


/**
 * Increase the timer freqency on hosts where this is possible (NT).
 *
 * The idea is that more interrupts is better for us... Also, it's better than
 * we increase the timer frequence, because we might end up getting inaccurate
 * callbacks if someone else does it.
 *
 * @param   pDevExt   Sets u32SystemTimerGranularityGrant if increased.
 */
static void supdrvGipRequestHigherTimerFrequencyFromSystem(PSUPDRVDEVEXT pDevExt)
{
    if (pDevExt->u32SystemTimerGranularityGrant == 0)
    {
        uint32_t u32SystemResolution;
        if (   RT_SUCCESS_NP(RTTimerRequestSystemGranularity(  976563 /* 1024 HZ */, &u32SystemResolution))
            || RT_SUCCESS_NP(RTTimerRequestSystemGranularity( 1000000 /* 1000 HZ */, &u32SystemResolution))
            || RT_SUCCESS_NP(RTTimerRequestSystemGranularity( 1953125 /*  512 HZ */, &u32SystemResolution))
            || RT_SUCCESS_NP(RTTimerRequestSystemGranularity( 2000000 /*  500 HZ */, &u32SystemResolution))
           )
        {
#if 0 /* def VBOX_STRICT - this is somehow triggers bogus assertions on windows 10 */
            uint32_t u32After = RTTimerGetSystemGranularity();
            AssertMsg(u32After <= u32SystemResolution, ("u32After=%u u32SystemResolution=%u\n", u32After, u32SystemResolution));
#endif
            pDevExt->u32SystemTimerGranularityGrant = u32SystemResolution;
        }
    }
}


/**
 * Undoes supdrvGipRequestHigherTimerFrequencyFromSystem.
 *
 * @param   pDevExt     Clears u32SystemTimerGranularityGrant.
 */
static void supdrvGipReleaseHigherTimerFrequencyFromSystem(PSUPDRVDEVEXT pDevExt)
{
    if (pDevExt->u32SystemTimerGranularityGrant)
    {
        int rc2 = RTTimerReleaseSystemGranularity(pDevExt->u32SystemTimerGranularityGrant);
        AssertRC(rc2);
        pDevExt->u32SystemTimerGranularityGrant = 0;
    }
}


/**
 * Maps the GIP into userspace and/or get the physical address of the GIP.
 *
 * @returns IPRT status code.
 * @param   pSession        Session to which the GIP mapping should belong.
 * @param   ppGipR3         Where to store the address of the ring-3 mapping. (optional)
 * @param   pHCPhysGip      Where to store the physical address. (optional)
 *
 * @remark  There is no reference counting on the mapping, so one call to this function
 *          count globally as one reference. One call to SUPR0GipUnmap() is will unmap GIP
 *          and remove the session as a GIP user.
 */
SUPR0DECL(int) SUPR0GipMap(PSUPDRVSESSION pSession, PRTR3PTR ppGipR3, PRTHCPHYS pHCPhysGip)
{
    int             rc;
    PSUPDRVDEVEXT   pDevExt = pSession->pDevExt;
    RTR3PTR         pGipR3  = NIL_RTR3PTR;
    RTHCPHYS        HCPhys  = NIL_RTHCPHYS;
    LogFlow(("SUPR0GipMap: pSession=%p ppGipR3=%p pHCPhysGip=%p\n", pSession, ppGipR3, pHCPhysGip));

    /*
     * Validate
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(ppGipR3, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pHCPhysGip, VERR_INVALID_POINTER);

#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSemMutexRequest(pDevExt->mtxGip, RT_INDEFINITE_WAIT);
#else
    RTSemFastMutexRequest(pDevExt->mtxGip);
#endif
    if (pDevExt->pGip)
    {
        /*
         * Map it?
         */
        rc = VINF_SUCCESS;
        if (ppGipR3)
        {
            if (pSession->GipMapObjR3 == NIL_RTR0MEMOBJ)
                rc = RTR0MemObjMapUser(&pSession->GipMapObjR3, pDevExt->GipMemObj, (RTR3PTR)-1, 0,
                                       RTMEM_PROT_READ, NIL_RTR0PROCESS);
            if (RT_SUCCESS(rc))
                pGipR3 = RTR0MemObjAddressR3(pSession->GipMapObjR3);
        }

        /*
         * Get physical address.
         */
        if (pHCPhysGip && RT_SUCCESS(rc))
            HCPhys = pDevExt->HCPhysGip;

        /*
         * Reference globally.
         */
        if (!pSession->fGipReferenced && RT_SUCCESS(rc))
        {
            pSession->fGipReferenced = 1;
            pDevExt->cGipUsers++;
            if (pDevExt->cGipUsers == 1)
            {
                PSUPGLOBALINFOPAGE pGipR0 = pDevExt->pGip;
                uint64_t u64NanoTS;

                /*
                 * GIP starts/resumes updating again.  On windows we bump the
                 * host timer frequency to make sure we don't get stuck in guest
                 * mode and to get better timer (and possibly clock) accuracy.
                 */
                LogFlow(("SUPR0GipMap: Resumes GIP updating\n"));

                supdrvGipRequestHigherTimerFrequencyFromSystem(pDevExt);

                /*
                 * document me
                 */
                if (pGipR0->aCPUs[0].u32TransactionId != 2 /* not the first time */)
                {
                    unsigned i;
                    for (i = 0; i < pGipR0->cCpus; i++)
                        ASMAtomicUoWriteU32(&pGipR0->aCPUs[i].u32TransactionId,
                                            (pGipR0->aCPUs[i].u32TransactionId + GIP_UPDATEHZ_RECALC_FREQ * 2)
                                            & ~(GIP_UPDATEHZ_RECALC_FREQ * 2 - 1));
                    ASMAtomicWriteU64(&pGipR0->u64NanoTSLastUpdateHz, 0);
                }

                /*
                 * document me
                 */
                u64NanoTS = RTTimeSystemNanoTS() - pGipR0->u32UpdateIntervalNS;
                if (   pGipR0->u32Mode == SUPGIPMODE_INVARIANT_TSC
                    || pGipR0->u32Mode == SUPGIPMODE_SYNC_TSC
                    || RTMpGetOnlineCount() == 1)
                    supdrvGipReInitCpu(&pGipR0->aCPUs[0], u64NanoTS);
                else
                    RTMpOnAll(supdrvGipReInitCpuCallback, pGipR0, &u64NanoTS);

                /*
                 * Detect alternative ways to figure the CPU ID in ring-3 and
                 * raw-mode context.  Check the sanity of the APIC IDs, CPU IDs,
                 * and CPU set indexes while we're at it.
                 */
                if (RT_SUCCESS(rc))
                {
                    PSUPDRVGIPDETECTGETCPU pDetectState = (PSUPDRVGIPDETECTGETCPU)RTMemTmpAllocZ(sizeof(*pDetectState));
                    if (pDetectState)
                    {
                        pDetectState->fSupported   = UINT32_MAX;
                        pDetectState->idCpuProblem = NIL_RTCPUID;
                        rc = RTMpOnAll(supdrvGipDetectGetGipCpuCallback, pDetectState, pGipR0);
                        if (pDetectState->idCpuProblem == NIL_RTCPUID)
                        {
                            if (   pDetectState->fSupported != UINT32_MAX
                                && pDetectState->fSupported != 0)
                            {
                                if (pGipR0->fGetGipCpu != pDetectState->fSupported)
                                {
                                    pGipR0->fGetGipCpu = pDetectState->fSupported;
                                    LogRel(("SUPR0GipMap: fGetGipCpu=%#x\n", pDetectState->fSupported));
                                }
                            }
                            else
                            {
                                LogRel(("SUPR0GipMap: No supported ways of getting the APIC ID or CPU number in ring-3! (%#x)\n",
                                        pDetectState->fSupported));
                                rc = VERR_UNSUPPORTED_CPU;
                            }
                        }
                        else
                        {
                            LogRel(("SUPR0GipMap: APIC ID, CPU ID or CPU set index problem detected on CPU #%u (%#x)!\n",
                                    pDetectState->idCpuProblem, pDetectState->idCpuProblem));
                            rc = VERR_INVALID_CPU_ID;
                        }
                        RTMemTmpFree(pDetectState);
                    }
                    else
                        rc = VERR_NO_TMP_MEMORY;
                }

                /*
                 * Start the GIP timer if all is well..
                 */
                if (RT_SUCCESS(rc))
                {
#ifndef DO_NOT_START_GIP
                    rc = RTTimerStart(pDevExt->pGipTimer, 0 /* fire ASAP */); AssertRC(rc);
#endif
                    rc = VINF_SUCCESS;
                }

                /*
                 * Bail out on error.
                 */
                if (RT_FAILURE(rc))
                {
                    LogRel(("SUPR0GipMap: failed rc=%Rrc\n", rc));
                    pDevExt->cGipUsers = 0;
                    pSession->fGipReferenced = 0;
                    if (pSession->GipMapObjR3 != NIL_RTR0MEMOBJ)
                    {
                        int rc2 = RTR0MemObjFree(pSession->GipMapObjR3, false); AssertRC(rc2);
                        if (RT_SUCCESS(rc2))
                            pSession->GipMapObjR3 = NIL_RTR0MEMOBJ;
                    }
                    HCPhys = NIL_RTHCPHYS;
                    pGipR3 = NIL_RTR3PTR;
                }
            }
        }
    }
    else
    {
        rc = VERR_GENERAL_FAILURE;
        Log(("SUPR0GipMap: GIP is not available!\n"));
    }
#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSemMutexRelease(pDevExt->mtxGip);
#else
    RTSemFastMutexRelease(pDevExt->mtxGip);
#endif

    /*
     * Write returns.
     */
    if (pHCPhysGip)
        *pHCPhysGip = HCPhys;
    if (ppGipR3)
        *ppGipR3 = pGipR3;

#ifdef DEBUG_DARWIN_GIP
    OSDBGPRINT(("SUPR0GipMap: returns %d *pHCPhysGip=%lx pGipR3=%p\n", rc, (unsigned long)HCPhys, (void *)pGipR3));
#else
    LogFlow((   "SUPR0GipMap: returns %d *pHCPhysGip=%lx pGipR3=%p\n", rc, (unsigned long)HCPhys, (void *)pGipR3));
#endif
    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPR0GipMap);


/**
 * Unmaps any user mapping of the GIP and terminates all GIP access
 * from this session.
 *
 * @returns IPRT status code.
 * @param   pSession        Session to which the GIP mapping should belong.
 */
SUPR0DECL(int) SUPR0GipUnmap(PSUPDRVSESSION pSession)
{
    int                     rc = VINF_SUCCESS;
    PSUPDRVDEVEXT           pDevExt = pSession->pDevExt;
#ifdef DEBUG_DARWIN_GIP
    OSDBGPRINT(("SUPR0GipUnmap: pSession=%p pGip=%p GipMapObjR3=%p\n",
                pSession,
                pSession->GipMapObjR3 != NIL_RTR0MEMOBJ ? RTR0MemObjAddress(pSession->GipMapObjR3) : NULL,
                pSession->GipMapObjR3));
#else
    LogFlow(("SUPR0GipUnmap: pSession=%p\n", pSession));
#endif
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);

#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSemMutexRequest(pDevExt->mtxGip, RT_INDEFINITE_WAIT);
#else
    RTSemFastMutexRequest(pDevExt->mtxGip);
#endif

    /*
     * GIP test-mode session?
     */
    if (   pSession->fGipTestMode
        && pDevExt->pGip)
    {
        supdrvGipSetFlags(pDevExt, pSession, 0, ~SUPGIP_FLAGS_TESTING_ENABLE);
        Assert(!pSession->fGipTestMode);
    }

    /*
     * Unmap anything?
     */
    if (pSession->GipMapObjR3 != NIL_RTR0MEMOBJ)
    {
        rc = RTR0MemObjFree(pSession->GipMapObjR3, false);
        AssertRC(rc);
        if (RT_SUCCESS(rc))
            pSession->GipMapObjR3 = NIL_RTR0MEMOBJ;
    }

    /*
     * Dereference global GIP.
     */
    if (pSession->fGipReferenced && !rc)
    {
        pSession->fGipReferenced = 0;
        if (    pDevExt->cGipUsers > 0
            &&  !--pDevExt->cGipUsers)
        {
            LogFlow(("SUPR0GipUnmap: Suspends GIP updating\n"));
#ifndef DO_NOT_START_GIP
            rc = RTTimerStop(pDevExt->pGipTimer); AssertRC(rc); rc = VINF_SUCCESS;
#endif
            supdrvGipReleaseHigherTimerFrequencyFromSystem(pDevExt);
        }
    }

#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSemMutexRelease(pDevExt->mtxGip);
#else
    RTSemFastMutexRelease(pDevExt->mtxGip);
#endif

    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPR0GipUnmap);


/**
 * Gets the GIP pointer.
 *
 * @returns Pointer to the GIP or NULL.
 */
SUPDECL(PSUPGLOBALINFOPAGE) SUPGetGIP(void)
{
    return g_pSUPGlobalInfoPage;
}





/*
 *
 *
 * GIP Initialization, Termination and CPU Offline / Online Related Code.
 * GIP Initialization, Termination and CPU Offline / Online Related Code.
 * GIP Initialization, Termination and CPU Offline / Online Related Code.
 *
 *
 */

/**
 * Used by supdrvGipInitRefineInvariantTscFreqTimer and supdrvGipInitMeasureTscFreq
 * to update the TSC frequency related GIP variables.
 *
 * @param   pGip                The GIP.
 * @param   nsElapsed           The number of nanoseconds elapsed.
 * @param   cElapsedTscTicks    The corresponding number of TSC ticks.
 * @param   iTick               The tick number for debugging.
 */
static void supdrvGipInitSetCpuFreq(PSUPGLOBALINFOPAGE pGip, uint64_t nsElapsed, uint64_t cElapsedTscTicks, uint32_t iTick)
{
    /*
     * Calculate the frequency.
     */
    uint64_t uCpuHz;
    if (   cElapsedTscTicks < UINT64_MAX / RT_NS_1SEC
        && nsElapsed < UINT32_MAX)
        uCpuHz = ASMMultU64ByU32DivByU32(cElapsedTscTicks, RT_NS_1SEC, (uint32_t)nsElapsed);
    else
    {
        RTUINT128U CpuHz, Tmp, Divisor;
        CpuHz.s.Lo = CpuHz.s.Hi = 0;
        RTUInt128MulU64ByU64(&Tmp, cElapsedTscTicks, RT_NS_1SEC_64);
        RTUInt128Div(&CpuHz, &Tmp, RTUInt128AssignU64(&Divisor, nsElapsed));
        uCpuHz = CpuHz.s.Lo;
    }

    /*
     * Update the GIP.
     */
    ASMAtomicWriteU64(&pGip->u64CpuHz, uCpuHz);
    if (pGip->u32Mode != SUPGIPMODE_ASYNC_TSC)
    {
        ASMAtomicWriteU64(&pGip->aCPUs[0].u64CpuHz, uCpuHz);

        /* For inspecting the frequency calcs using tstGIP-2, debugger or similar. */
        if (iTick + 1 < pGip->cCpus)
            ASMAtomicWriteU64(&pGip->aCPUs[iTick + 1].u64CpuHz, uCpuHz);
    }
}


/**
 * Timer callback function for TSC frequency refinement in invariant GIP mode.
 *
 * This is started during driver init and fires once
 * GIP_TSC_REFINE_PERIOD_IN_SECS seconds later.
 *
 * @param   pTimer      The timer.
 * @param   pvUser      Opaque pointer to the device instance data.
 * @param   iTick       The timer tick.
 */
static DECLCALLBACK(void) supdrvGipInitRefineInvariantTscFreqTimer(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PSUPDRVDEVEXT       pDevExt = (PSUPDRVDEVEXT)pvUser;
    PSUPGLOBALINFOPAGE  pGip = pDevExt->pGip;
    RTCPUID             idCpu;
    uint64_t            cNsElapsed;
    uint64_t            cTscTicksElapsed;
    uint64_t            nsNow;
    uint64_t            uTsc;
    RTCCUINTREG         fEFlags;

    /* Paranoia. */
    AssertReturnVoid(pGip);
    AssertReturnVoid(pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC);

    /*
     * If we got a power event, stop the refinement process.
     */
    if (pDevExt->fInvTscRefinePowerEvent)
    {
        int rc = RTTimerStop(pTimer); AssertRC(rc);
        return;
    }

    /*
     * Read the TSC and time, noting which CPU we are on.
     *
     * Don't bother spinning until RTTimeSystemNanoTS changes, since on
     * systems where it matters we're in a context where we cannot waste that
     * much time (DPC watchdog, called from clock interrupt).
     */
    fEFlags = ASMIntDisableFlags();
    uTsc    = ASMReadTSC();
    nsNow   = RTTimeSystemNanoTS();
    idCpu   = RTMpCpuId();
    ASMSetFlags(fEFlags);

    cNsElapsed          = nsNow - pDevExt->nsStartInvarTscRefine;
    cTscTicksElapsed    = uTsc  - pDevExt->uTscStartInvarTscRefine;

    /*
     * If the above measurement was taken on a different CPU than the one we
     * started the process on, cTscTicksElapsed will need to be adjusted with
     * the TSC deltas of both the CPUs.
     *
     * We ASSUME that the delta calculation process takes less time than the
     * TSC frequency refinement timer.  If it doesn't, we'll complain and
     * drop the frequency refinement.
     *
     * Note! We cannot entirely trust enmUseTscDelta here because it's
     *       downgraded after each delta calculation.
     */
    if (   idCpu != pDevExt->idCpuInvarTscRefine
        && pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED)
    {
        uint32_t iStartCpuSet   = RTMpCpuIdToSetIndex(pDevExt->idCpuInvarTscRefine);
        uint32_t iStopCpuSet    = RTMpCpuIdToSetIndex(idCpu);
        uint16_t iStartGipCpu   = iStartCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                                ? pGip->aiCpuFromCpuSetIdx[iStartCpuSet] : UINT16_MAX;
        uint16_t iStopGipCpu    = iStopCpuSet  < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                                ? pGip->aiCpuFromCpuSetIdx[iStopCpuSet]  : UINT16_MAX;
        int64_t  iStartTscDelta = iStartGipCpu < pGip->cCpus ? pGip->aCPUs[iStartGipCpu].i64TSCDelta : INT64_MAX;
        int64_t  iStopTscDelta  = iStopGipCpu  < pGip->cCpus ? pGip->aCPUs[iStopGipCpu].i64TSCDelta  : INT64_MAX;
        if (RT_LIKELY(iStartTscDelta != INT64_MAX && iStopTscDelta != INT64_MAX))
        {
            if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_PRACTICALLY_ZERO)
            {
                /* cTscTicksElapsed = (uTsc - iStopTscDelta) - (pDevExt->uTscStartInvarTscRefine - iStartTscDelta); */
                cTscTicksElapsed += iStartTscDelta - iStopTscDelta;
            }
        }
        /*
         * Allow 5 times the refinement period to elapse before we give up on the TSC delta
         * calculations.
         */
        else if (cNsElapsed > GIP_TSC_REFINE_PERIOD_IN_SECS * 5 * RT_NS_1SEC_64)
        {
            SUPR0Printf("vboxdrv: Failed to refine invariant TSC frequency because deltas are unavailable after %u (%u) seconds\n",
                        (uint32_t)(cNsElapsed / RT_NS_1SEC), GIP_TSC_REFINE_PERIOD_IN_SECS);
            SUPR0Printf("vboxdrv: start: %u, %u, %#llx  stop: %u, %u, %#llx\n",
                        iStartCpuSet, iStartGipCpu, iStartTscDelta, iStopCpuSet, iStopGipCpu, iStopTscDelta);
            int rc = RTTimerStop(pTimer); AssertRC(rc);
            return;
        }
    }

    /*
     * Calculate and update the CPU frequency variables in GIP.
     *
     * If there is a GIP user already and we've already refined the frequency
     * a couple of times, don't update it as we want a stable frequency value
     * for all VMs.
     */
    if (   pDevExt->cGipUsers == 0
        || cNsElapsed < RT_NS_1SEC * 2)
    {
        supdrvGipInitSetCpuFreq(pGip, cNsElapsed, cTscTicksElapsed, (uint32_t)iTick);

        /*
         * Stop the timer once we've reached the defined refinement period.
         */
        if (cNsElapsed > GIP_TSC_REFINE_PERIOD_IN_SECS * RT_NS_1SEC_64)
        {
            int rc = RTTimerStop(pTimer);
            AssertRC(rc);
        }
    }
    else
    {
        int rc = RTTimerStop(pTimer);
        AssertRC(rc);
    }
}


/**
 * @callback_method_impl{FNRTPOWERNOTIFICATION}
 */
static DECLCALLBACK(void) supdrvGipPowerNotificationCallback(RTPOWEREVENT enmEvent, void *pvUser)
{
    PSUPDRVDEVEXT      pDevExt = (PSUPDRVDEVEXT)pvUser;
    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;

    /*
     * If the TSC frequency refinement timer is running, we need to cancel it so it
     * doesn't screw up the frequency after a long suspend.
     *
     * Recalculate all TSC-deltas on host resume as it may have changed, seen
     * on Windows 7 running on the Dell Optiplex Intel Core i5-3570.
     */
    if (enmEvent == RTPOWEREVENT_RESUME)
    {
        ASMAtomicWriteBool(&pDevExt->fInvTscRefinePowerEvent, true);
        if (   RT_LIKELY(pGip)
            && pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED
            && !supdrvOSAreCpusOfflinedOnSuspend())
        {
#ifdef SUPDRV_USE_TSC_DELTA_THREAD
            supdrvTscDeltaThreadStartMeasurement(pDevExt, true /* fForceAll */);
#else
            RTCpuSetCopy(&pDevExt->TscDeltaCpuSet, &pGip->OnlineCpuSet);
            supdrvTscMeasureInitialDeltas(pDevExt);
#endif
        }
    }
    else if (enmEvent == RTPOWEREVENT_SUSPEND)
        ASMAtomicWriteBool(&pDevExt->fInvTscRefinePowerEvent, true);
}


/**
 * Start the TSC-frequency refinment timer for the invariant TSC GIP mode.
 *
 * We cannot use this in the synchronous and asynchronous tsc GIP modes because
 * the CPU may change the TSC frequence between now and when the timer fires
 * (supdrvInitAsyncRefineTscTimer).
 *
 * @param   pDevExt         Pointer to the device instance data.
 */
static void supdrvGipInitStartTimerForRefiningInvariantTscFreq(PSUPDRVDEVEXT pDevExt)
{
    uint64_t    u64NanoTS;
    RTCCUINTREG fEFlags;
    int         rc;

    /*
     * Register a power management callback.
     */
    pDevExt->fInvTscRefinePowerEvent = false;
    rc = RTPowerNotificationRegister(supdrvGipPowerNotificationCallback, pDevExt);
    AssertRC(rc); /* ignore */

    /*
     * Record the TSC and NanoTS as the starting anchor point for refinement
     * of the TSC.  We try get as close to a clock tick as possible on systems
     * which does not provide high resolution time.
     */
    u64NanoTS = RTTimeSystemNanoTS();
    while (RTTimeSystemNanoTS() == u64NanoTS)
        ASMNopPause();

    fEFlags = ASMIntDisableFlags();
    pDevExt->uTscStartInvarTscRefine = ASMReadTSC();
    pDevExt->nsStartInvarTscRefine   = RTTimeSystemNanoTS();
    pDevExt->idCpuInvarTscRefine     = RTMpCpuId();
    ASMSetFlags(fEFlags);

    /*
     * Create a timer that runs on the same CPU so we won't have a depencency
     * on the TSC-delta and can run in parallel to it. On systems that does not
     * implement CPU specific timers we'll apply deltas in the timer callback,
     * just like we do for CPUs going offline.
     *
     * The longer the refinement interval the better the accuracy, at least in
     * theory.  If it's too long though, ring-3 may already be starting its
     * first VMs before we're done.  On most systems we will be loading the
     * support driver during boot and VMs won't be started for a while yet,
     * it is really only a problem during development (especially with
     * on-demand driver starting on windows).
     *
     * To avoid wasting time doing a long supdrvGipInitMeasureTscFreq() call
     * to calculate the frequency during driver loading, the timer is set
     * to fire after 200 ms the first time. It will then reschedule itself
     * to fire every second until GIP_TSC_REFINE_PERIOD_IN_SECS has been
     * reached or it notices that there is a user land client with GIP
     * mapped (we want a stable frequency for all VMs).
     */
    rc = RTTimerCreateEx(&pDevExt->pInvarTscRefineTimer, RT_NS_1SEC,
                         RTTIMER_FLAGS_CPU(RTMpCpuIdToSetIndex(pDevExt->idCpuInvarTscRefine)),
                         supdrvGipInitRefineInvariantTscFreqTimer, pDevExt);
    if (RT_SUCCESS(rc))
    {
        rc = RTTimerStart(pDevExt->pInvarTscRefineTimer, 2*RT_NS_100MS);
        if (RT_SUCCESS(rc))
            return;
        RTTimerDestroy(pDevExt->pInvarTscRefineTimer);
    }

    if (rc == VERR_CPU_OFFLINE || rc == VERR_NOT_SUPPORTED)
    {
        rc = RTTimerCreateEx(&pDevExt->pInvarTscRefineTimer, RT_NS_1SEC, RTTIMER_FLAGS_CPU_ANY,
                             supdrvGipInitRefineInvariantTscFreqTimer, pDevExt);
        if (RT_SUCCESS(rc))
        {
            rc = RTTimerStart(pDevExt->pInvarTscRefineTimer, 2*RT_NS_100MS);
            if (RT_SUCCESS(rc))
                return;
            RTTimerDestroy(pDevExt->pInvarTscRefineTimer);
        }
    }

    pDevExt->pInvarTscRefineTimer = NULL;
    OSDBGPRINT(("vboxdrv: Failed to create or start TSC frequency refinement timer: rc=%Rrc\n", rc));
}


/**
 * @callback_method_impl{PFNRTMPWORKER,
 *      RTMpOnSpecific callback for reading TSC and time on the CPU we started
 *      the measurements on.}
 */
static DECLCALLBACK(void) supdrvGipInitReadTscAndNanoTsOnCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    RTCCUINTREG fEFlags   = ASMIntDisableFlags();
    uint64_t   *puTscStop = (uint64_t *)pvUser1;
    uint64_t   *pnsStop   = (uint64_t *)pvUser2;
    RT_NOREF1(idCpu);

    *puTscStop = ASMReadTSC();
    *pnsStop   = RTTimeSystemNanoTS();

    ASMSetFlags(fEFlags);
}


/**
 * Measures the TSC frequency of the system.
 *
 * The TSC frequency can vary on systems which are not reported as invariant.
 * On such systems the object of this function is to find out what the nominal,
 * maximum TSC frequency under 'normal' CPU operation.
 *
 * @returns VBox status code.
 * @param   pGip            Pointer to the GIP.
 * @param   fRough          Set if we're doing the rough calculation that the
 *                          TSC measuring code needs, where accuracy isn't all
 *                          that important (too high is better than too low).
 *                          When clear we try for best accuracy that we can
 *                          achieve in reasonably short time.
 */
static int supdrvGipInitMeasureTscFreq(PSUPGLOBALINFOPAGE pGip, bool fRough)
{
    uint32_t nsTimerIncr = RTTimerGetSystemGranularity();
    int      cTriesLeft = fRough ? 4 : 2;
    while (cTriesLeft-- > 0)
    {
        RTCCUINTREG fEFlags;
        uint64_t    nsStart;
        uint64_t    nsStop;
        uint64_t    uTscStart;
        uint64_t    uTscStop;
        RTCPUID     idCpuStart;
        RTCPUID     idCpuStop;

        /*
         * Synchronize with the host OS clock tick on systems without high
         * resolution time API (older Windows version for example).
         */
        nsStart = RTTimeSystemNanoTS();
        while (RTTimeSystemNanoTS() == nsStart)
            ASMNopPause();

        /*
         * Read the TSC and current time, noting which CPU we're on.
         */
        fEFlags = ASMIntDisableFlags();
        uTscStart   = ASMReadTSC();
        nsStart     = RTTimeSystemNanoTS();
        idCpuStart  = RTMpCpuId();
        ASMSetFlags(fEFlags);

        /*
         * Delay for a while.
         */
        if (pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC)
        {
            /*
             * Sleep-wait since the TSC frequency is constant, it eases host load.
             * Shorter interval produces more variance in the frequency (esp. Windows).
             */
            uint64_t msElapsed = 0;
            uint64_t msDelay =   ( ((fRough ? 16 : 200) * RT_NS_1MS + nsTimerIncr - 1) / nsTimerIncr * nsTimerIncr - RT_NS_100US )
                               / RT_NS_1MS;
            do
            {
                RTThreadSleep((RTMSINTERVAL)(msDelay - msElapsed));
                nsStop    = RTTimeSystemNanoTS();
                msElapsed = (nsStop - nsStart) / RT_NS_1MS;
            } while (msElapsed < msDelay);

            while (RTTimeSystemNanoTS() == nsStop)
                ASMNopPause();
        }
        else
        {
            /*
             * Busy-wait keeping the frequency up.
             */
            do
            {
                ASMNopPause();
                nsStop = RTTimeSystemNanoTS();
            } while (nsStop - nsStart < RT_NS_100MS);
        }

        /*
         * Read the TSC and time again.
         */
        fEFlags = ASMIntDisableFlags();
        uTscStop    = ASMReadTSC();
        nsStop      = RTTimeSystemNanoTS();
        idCpuStop   = RTMpCpuId();
        ASMSetFlags(fEFlags);

        /*
         * If the CPU changes, things get a bit complicated and what we
         * can get away with depends on the GIP mode / TSC reliability.
         */
        if (idCpuStop != idCpuStart)
        {
            bool fDoXCall = false;

            /*
             * Synchronous TSC mode: we're probably fine as it's unlikely
             * that we were rescheduled because of TSC throttling or power
             * management reasons, so just go ahead.
             */
            if (pGip->u32Mode == SUPGIPMODE_SYNC_TSC)
            {
                /* Probably ok, maybe we should retry once?. */
                Assert(pGip->enmUseTscDelta == SUPGIPUSETSCDELTA_NOT_APPLICABLE);
            }
            /*
             * If we're just doing the rough measurement, do the cross call and
             * get on with things (we don't have deltas!).
             */
            else if (fRough)
                fDoXCall = true;
            /*
             * Invariant TSC mode: It doesn't matter if we have delta available
             * for both CPUs.  That is not something we can assume at this point.
             *
             * Note! We cannot necessarily trust enmUseTscDelta here because it's
             *       downgraded after each delta calculation and the delta
             *       calculations may not be complete yet.
             */
            else if (pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC)
            {
/** @todo This section of code is never reached atm, consider dropping it later on... */
                if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED)
                {
                    uint32_t iStartCpuSet   = RTMpCpuIdToSetIndex(idCpuStart);
                    uint32_t iStopCpuSet    = RTMpCpuIdToSetIndex(idCpuStop);
                    uint16_t iStartGipCpu   = iStartCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                                            ? pGip->aiCpuFromCpuSetIdx[iStartCpuSet] : UINT16_MAX;
                    uint16_t iStopGipCpu    = iStopCpuSet  < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                                            ? pGip->aiCpuFromCpuSetIdx[iStopCpuSet]  : UINT16_MAX;
                    int64_t  iStartTscDelta = iStartGipCpu < pGip->cCpus ? pGip->aCPUs[iStartGipCpu].i64TSCDelta : INT64_MAX;
                    int64_t  iStopTscDelta  = iStopGipCpu  < pGip->cCpus ? pGip->aCPUs[iStopGipCpu].i64TSCDelta  : INT64_MAX;
                    if (RT_LIKELY(iStartTscDelta != INT64_MAX && iStopTscDelta != INT64_MAX))
                    {
                        if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_PRACTICALLY_ZERO)
                        {
                            uTscStart -= iStartTscDelta;
                            uTscStop  -= iStopTscDelta;
                        }
                    }
                    /*
                     * Invalid CPU indexes are not caused by online/offline races, so
                     * we have to trigger driver load failure if that happens as GIP
                     * and IPRT assumptions are busted on this system.
                     */
                    else if (iStopGipCpu >= pGip->cCpus || iStartGipCpu >= pGip->cCpus)
                    {
                        SUPR0Printf("vboxdrv: Unexpected CPU index in supdrvGipInitMeasureTscFreq.\n");
                        SUPR0Printf("vboxdrv: start: %u, %u, %#llx  stop: %u, %u, %#llx\n",
                                    iStartCpuSet, iStartGipCpu, iStartTscDelta, iStopCpuSet, iStopGipCpu, iStopTscDelta);
                        return VERR_INVALID_CPU_INDEX;
                    }
                    /*
                     * No valid deltas.  We retry, if we're on our last retry
                     * we do the cross call instead just to get a result.  The
                     * frequency will be refined in a few seconds anyway.
                     */
                    else if (cTriesLeft > 0)
                        continue;
                    else
                        fDoXCall = true;
                }
            }
            /*
             * Asynchronous TSC mode: This is bad, as the reason we usually
             * use this mode is to deal with variable TSC frequencies and
             * deltas.  So, we need to get the TSC from the same CPU as
             * started it, we also need to keep that CPU busy.  So, retry
             * and fall back to the cross call on the last attempt.
             */
            else
            {
                Assert(pGip->u32Mode == SUPGIPMODE_ASYNC_TSC);
                if (cTriesLeft > 0)
                    continue;
                fDoXCall = true;
            }

            if (fDoXCall)
            {
                /*
                 * Try read the TSC and timestamp on the start CPU.
                 */
                int rc = RTMpOnSpecific(idCpuStart, supdrvGipInitReadTscAndNanoTsOnCpu, &uTscStop, &nsStop);
                if (RT_FAILURE(rc) && (!fRough || cTriesLeft > 0))
                    continue;
            }
        }

        /*
         * Calculate the TSC frequency and update it (shared with the refinement timer).
         */
        supdrvGipInitSetCpuFreq(pGip, nsStop - nsStart, uTscStop - uTscStart, 0);
        return VINF_SUCCESS;
    }

    Assert(!fRough);
    return VERR_SUPDRV_TSC_FREQ_MEASUREMENT_FAILED;
}


/**
 * Finds our (@a idCpu) entry, or allocates a new one if not found.
 *
 * @returns Index of the CPU in the cache set.
 * @param   pGip                The GIP.
 * @param   idCpu               The CPU ID.
 */
static uint32_t supdrvGipFindOrAllocCpuIndexForCpuId(PSUPGLOBALINFOPAGE pGip, RTCPUID idCpu)
{
    uint32_t i, cTries;

    /*
     * ASSUMES that CPU IDs are constant.
     */
    for (i = 0; i < pGip->cCpus; i++)
        if (pGip->aCPUs[i].idCpu == idCpu)
            return i;

    cTries = 0;
    do
    {
        for (i = 0; i < pGip->cCpus; i++)
        {
            bool fRc;
            ASMAtomicCmpXchgSize(&pGip->aCPUs[i].idCpu, idCpu, NIL_RTCPUID, fRc);
            if (fRc)
                return i;
        }
    } while (cTries++ < 32);
    AssertReleaseFailed();
    return i - 1;
}


/**
 * The calling CPU should be accounted as online, update GIP accordingly.
 *
 * This is used by supdrvGipCreate() as well as supdrvGipMpEvent().
 *
 * @param   pDevExt             The device extension.
 * @param   idCpu               The CPU ID.
 */
static void supdrvGipMpEventOnlineOrInitOnCpu(PSUPDRVDEVEXT pDevExt, RTCPUID idCpu)
{
    PSUPGLOBALINFOPAGE  pGip      = pDevExt->pGip;
    int                 iCpuSet   = 0;
    uint32_t            idApic;
    uint32_t            i         = 0;
    uint64_t            u64NanoTS = 0;

    AssertPtrReturnVoid(pGip);
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    AssertRelease(idCpu == RTMpCpuId());
    Assert(pGip->cPossibleCpus == RTMpGetCount());

    /*
     * Do this behind a spinlock with interrupts disabled as this can fire
     * on all CPUs simultaneously, see @bugref{6110}.
     */
    RTSpinlockAcquire(pDevExt->hGipSpinlock);

    /*
     * Update the globals.
     */
    ASMAtomicWriteU16(&pGip->cPresentCpus,  RTMpGetPresentCount());
    ASMAtomicWriteU16(&pGip->cOnlineCpus,   RTMpGetOnlineCount());
    iCpuSet = RTMpCpuIdToSetIndex(idCpu);
    if (iCpuSet >= 0)
    {
        Assert(RTCpuSetIsMemberByIndex(&pGip->PossibleCpuSet, iCpuSet));
        RTCpuSetAddByIndex(&pGip->OnlineCpuSet, iCpuSet);
        RTCpuSetAddByIndex(&pGip->PresentCpuSet, iCpuSet);
    }

    /*
     * Update the entry.
     */
    u64NanoTS = RTTimeSystemNanoTS() - pGip->u32UpdateIntervalNS;
    i = supdrvGipFindOrAllocCpuIndexForCpuId(pGip, idCpu);

    supdrvGipInitCpu(pGip, &pGip->aCPUs[i], u64NanoTS, pGip->u64CpuHz);

    idApic = supdrvGipGetApicIdSlow();
    ASMAtomicWriteU16(&pGip->aCPUs[i].idApic,  idApic);
    ASMAtomicWriteS16(&pGip->aCPUs[i].iCpuSet, (int16_t)iCpuSet);
    ASMAtomicWriteSize(&pGip->aCPUs[i].idCpu,  idCpu);

    pGip->aCPUs[i].iCpuGroup = 0;
    pGip->aCPUs[i].iCpuGroupMember = iCpuSet;
#ifdef RT_OS_WINDOWS
    supdrvOSGipInitGroupBitsForCpu(pDevExt, pGip, &pGip->aCPUs[i]);
#endif

    /*
     * Update the APIC ID and CPU set index mappings.
     */
    if (idApic < RT_ELEMENTS(pGip->aiCpuFromApicId))
        ASMAtomicWriteU16(&pGip->aiCpuFromApicId[idApic],     i);
    else
        LogRelMax(64, ("supdrvGipMpEventOnlineOrInitOnCpu: idApic=%#x is out of bounds (%#zx, i=%u, iCpuSet=%d)\n",
                       idApic, RT_ELEMENTS(pGip->aiCpuFromApicId), i, iCpuSet));
    if ((unsigned)iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx))
        ASMAtomicWriteU16(&pGip->aiCpuFromCpuSetIdx[iCpuSet], i);
    else
        LogRelMax(64, ("supdrvGipMpEventOnlineOrInitOnCpu: iCpuSet=%d is out of bounds (%#zx, i=%u, idApic=%d)\n",
                       iCpuSet, RT_ELEMENTS(pGip->aiCpuFromApicId), i, idApic));

    /* Add this CPU to this set of CPUs we need to calculate the TSC-delta for. */
    RTCpuSetAddByIndex(&pDevExt->TscDeltaCpuSet, RTMpCpuIdToSetIndex(idCpu));

    /* Update the Mp online/offline counter. */
    ASMAtomicIncU32(&pDevExt->cMpOnOffEvents);

    /* Commit it. */
    ASMAtomicWriteSize(&pGip->aCPUs[i].enmState, SUPGIPCPUSTATE_ONLINE);

    RTSpinlockRelease(pDevExt->hGipSpinlock);
}


/**
 * RTMpOnSpecific callback wrapper for supdrvGipMpEventOnlineOrInitOnCpu().
 *
 * @param   idCpu     The CPU ID we are running on.
 * @param   pvUser1    Opaque pointer to the device instance data.
 * @param   pvUser2    Not used.
 */
static DECLCALLBACK(void) supdrvGipMpEventOnlineCallback(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    PSUPDRVDEVEXT pDevExt = (PSUPDRVDEVEXT)pvUser1;
    NOREF(pvUser2);
    supdrvGipMpEventOnlineOrInitOnCpu(pDevExt, idCpu);
}


/**
 * The CPU should be accounted as offline, update the GIP accordingly.
 *
 * This is used by supdrvGipMpEvent.
 *
 * @param   pDevExt             The device extension.
 * @param   idCpu               The CPU ID.
 */
static void supdrvGipMpEventOffline(PSUPDRVDEVEXT pDevExt, RTCPUID idCpu)
{
    PSUPGLOBALINFOPAGE  pGip = pDevExt->pGip;
    int                 iCpuSet;
    unsigned            i;

    AssertPtrReturnVoid(pGip);
    RTSpinlockAcquire(pDevExt->hGipSpinlock);

    iCpuSet = RTMpCpuIdToSetIndex(idCpu);
    AssertReturnVoid(iCpuSet >= 0);

    i = pGip->aiCpuFromCpuSetIdx[iCpuSet];
    AssertReturnVoid(i < pGip->cCpus);
    AssertReturnVoid(pGip->aCPUs[i].idCpu == idCpu);

    Assert(RTCpuSetIsMemberByIndex(&pGip->PossibleCpuSet, iCpuSet));
    RTCpuSetDelByIndex(&pGip->OnlineCpuSet, iCpuSet);

    /* Update the Mp online/offline counter. */
    ASMAtomicIncU32(&pDevExt->cMpOnOffEvents);

    if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED)
    {
        /* Reset the TSC delta, we will recalculate it lazily. */
        ASMAtomicWriteS64(&pGip->aCPUs[i].i64TSCDelta, INT64_MAX);
        /* Remove this CPU from the set of CPUs that we have obtained the TSC deltas. */
        RTCpuSetDelByIndex(&pDevExt->TscDeltaObtainedCpuSet, iCpuSet);
    }

    /* Commit it. */
    ASMAtomicWriteSize(&pGip->aCPUs[i].enmState, SUPGIPCPUSTATE_OFFLINE);

    RTSpinlockRelease(pDevExt->hGipSpinlock);
}


/**
 * Multiprocessor event notification callback.
 *
 * This is used to make sure that the GIP master gets passed on to
 * another CPU.  It also updates the associated CPU data.
 *
 * @param   enmEvent    The event.
 * @param   idCpu       The cpu it applies to.
 * @param   pvUser      Pointer to the device extension.
 */
static DECLCALLBACK(void) supdrvGipMpEvent(RTMPEVENT enmEvent, RTCPUID idCpu, void *pvUser)
{
    PSUPDRVDEVEXT       pDevExt = (PSUPDRVDEVEXT)pvUser;
    PSUPGLOBALINFOPAGE  pGip    = pDevExt->pGip;

    if (pGip)
    {
        RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
        switch (enmEvent)
        {
            case RTMPEVENT_ONLINE:
            {
                RTThreadPreemptDisable(&PreemptState);
                if (idCpu == RTMpCpuId())
                {
                    supdrvGipMpEventOnlineOrInitOnCpu(pDevExt, idCpu);
                    RTThreadPreemptRestore(&PreemptState);
                }
                else
                {
                    RTThreadPreemptRestore(&PreemptState);
                    RTMpOnSpecific(idCpu, supdrvGipMpEventOnlineCallback, pDevExt, NULL /* pvUser2 */);
                }

                /*
                 * Recompute TSC-delta for the newly online'd CPU.
                 */
                if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED)
                {
#ifdef SUPDRV_USE_TSC_DELTA_THREAD
                    supdrvTscDeltaThreadStartMeasurement(pDevExt, false /* fForceAll */);
#else
                    uint32_t iCpu = supdrvGipFindOrAllocCpuIndexForCpuId(pGip, idCpu);
                    supdrvTscMeasureDeltaOne(pDevExt, iCpu);
#endif
                }
                break;
            }

            case RTMPEVENT_OFFLINE:
                supdrvGipMpEventOffline(pDevExt, idCpu);
                break;
        }
    }

    /*
     * Make sure there is a master GIP.
     */
    if (enmEvent == RTMPEVENT_OFFLINE)
    {
        RTCPUID idGipMaster = ASMAtomicReadU32(&pDevExt->idGipMaster);
        if (idGipMaster == idCpu)
        {
            /*
             * The GIP master is going offline, find a new one.
             */
            bool        fIgnored;
            unsigned    i;
            RTCPUID     idNewGipMaster = NIL_RTCPUID;
            RTCPUSET    OnlineCpus;
            RTMpGetOnlineSet(&OnlineCpus);

            for (i = 0; i < RTCPUSET_MAX_CPUS; i++)
                if (RTCpuSetIsMemberByIndex(&OnlineCpus, i))
                {
                    RTCPUID idCurCpu = RTMpCpuIdFromSetIndex(i);
                    if (idCurCpu != idGipMaster)
                    {
                        idNewGipMaster = idCurCpu;
                        break;
                    }
                }

            Log(("supdrvGipMpEvent: Gip master %#lx -> %#lx\n", (long)idGipMaster, (long)idNewGipMaster));
            ASMAtomicCmpXchgSize(&pDevExt->idGipMaster, idNewGipMaster, idGipMaster, fIgnored);
            NOREF(fIgnored);
        }
    }
}


/**
 * On CPU initialization callback for RTMpOnAll.
 *
 * @param   idCpu               The CPU ID.
 * @param   pvUser1             The device extension.
 * @param   pvUser2             The GIP.
 */
static DECLCALLBACK(void) supdrvGipInitOnCpu(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    /* This is good enough, even though it will update some of the globals a
       bit to much. */
    supdrvGipMpEventOnlineOrInitOnCpu((PSUPDRVDEVEXT)pvUser1, idCpu);
    NOREF(pvUser2);
}


/**
 * Callback used by supdrvDetermineAsyncTSC to read the TSC on a CPU.
 *
 * @param   idCpu       Ignored.
 * @param   pvUser1     Where to put the TSC.
 * @param   pvUser2     Ignored.
 */
static DECLCALLBACK(void) supdrvGipInitDetermineAsyncTscWorker(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    Assert(RTMpCpuIdToSetIndex(idCpu) == (intptr_t)pvUser2);
    ASMAtomicWriteU64((uint64_t volatile *)pvUser1, ASMReadTSC());
    RT_NOREF2(idCpu, pvUser2);
}


/**
 * Determine if Async GIP mode is required because of TSC drift.
 *
 * When using the default/normal timer code it is essential that the time stamp counter
 * (TSC) runs never backwards, that is, a read operation to the counter should return
 * a bigger value than any previous read operation. This is guaranteed by the latest
 * AMD CPUs and by newer Intel CPUs which never enter the C2 state (P4). In any other
 * case we have to choose the asynchronous timer mode.
 *
 * @param   poffMin     Pointer to the determined difference between different
 *                      cores (optional, can be NULL).
 * @return  false if the time stamp counters appear to be synchronized, true otherwise.
 */
static bool supdrvGipInitDetermineAsyncTsc(uint64_t *poffMin)
{
    /*
     * Just iterate all the cpus 8 times and make sure that the TSC is
     * ever increasing. We don't bother taking TSC rollover into account.
     */
    int         iEndCpu = RTMpGetArraySize();
    int         iCpu;
    int         cLoops = 8;
    bool        fAsync = false;
    int         rc = VINF_SUCCESS;
    uint64_t    offMax = 0;
    uint64_t    offMin = ~(uint64_t)0;
    uint64_t    PrevTsc = ASMReadTSC();

    while (cLoops-- > 0)
    {
        for (iCpu = 0; iCpu < iEndCpu; iCpu++)
        {
            uint64_t CurTsc;
            rc = RTMpOnSpecific(RTMpCpuIdFromSetIndex(iCpu), supdrvGipInitDetermineAsyncTscWorker,
                                &CurTsc, (void *)(uintptr_t)iCpu);
            if (RT_SUCCESS(rc))
            {
                if (CurTsc <= PrevTsc)
                {
                    fAsync = true;
                    offMin = offMax = PrevTsc - CurTsc;
                    Log(("supdrvGipInitDetermineAsyncTsc: iCpu=%d cLoops=%d CurTsc=%llx PrevTsc=%llx\n",
                         iCpu, cLoops, CurTsc, PrevTsc));
                    break;
                }

                /* Gather statistics (except the first time). */
                if (iCpu != 0 || cLoops != 7)
                {
                    uint64_t off = CurTsc - PrevTsc;
                    if (off < offMin)
                        offMin = off;
                    if (off > offMax)
                        offMax = off;
                    Log2(("%d/%d: off=%llx\n", cLoops, iCpu, off));
                }

                /* Next */
                PrevTsc = CurTsc;
            }
            else if (rc == VERR_NOT_SUPPORTED)
                break;
            else
                AssertMsg(rc == VERR_CPU_NOT_FOUND || rc == VERR_CPU_OFFLINE, ("%d\n", rc));
        }

        /* broke out of the loop. */
        if (iCpu < iEndCpu)
            break;
    }

    if (poffMin)
        *poffMin = offMin; /* Almost RTMpOnSpecific profiling. */
    Log(("supdrvGipInitDetermineAsyncTsc: returns %d; iEndCpu=%d rc=%d offMin=%llx offMax=%llx\n",
         fAsync, iEndCpu, rc, offMin, offMax));
#if !defined(RT_OS_SOLARIS) && !defined(RT_OS_OS2) && !defined(RT_OS_WINDOWS)
    OSDBGPRINT(("vboxdrv: fAsync=%d offMin=%#lx offMax=%#lx\n", fAsync, (long)offMin, (long)offMax));
#endif
    return fAsync;
}


/**
 * supdrvGipInit() worker that determines the GIP TSC mode.
 *
 * @returns The most suitable TSC mode.
 * @param   pDevExt     Pointer to the device instance data.
 */
static SUPGIPMODE supdrvGipInitDetermineTscMode(PSUPDRVDEVEXT pDevExt)
{
    uint64_t u64DiffCoresIgnored;
    uint32_t uEAX, uEBX, uECX, uEDX;

    /*
     * Establish whether the CPU advertises TSC as invariant, we need that in
     * a couple of places below.
     */
    bool fInvariantTsc = false;
    if (ASMHasCpuId())
    {
        uEAX = ASMCpuId_EAX(0x80000000);
        if (RTX86IsValidExtRange(uEAX) && uEAX >= 0x80000007)
        {
            uEDX = ASMCpuId_EDX(0x80000007);
            if (uEDX & X86_CPUID_AMD_ADVPOWER_EDX_TSCINVAR)
                fInvariantTsc = true;
        }
    }

    /*
     * On single CPU systems, we don't need to consider ASYNC mode.
     */
    if (RTMpGetCount() <= 1)
        return fInvariantTsc ? SUPGIPMODE_INVARIANT_TSC : SUPGIPMODE_SYNC_TSC;

    /*
     * Allow the user and/or OS specific bits to force async mode.
     */
    if (supdrvOSGetForcedAsyncTscMode(pDevExt))
        return SUPGIPMODE_ASYNC_TSC;

    /*
     * Use invariant mode if the CPU says TSC is invariant.
     */
    if (fInvariantTsc)
        return SUPGIPMODE_INVARIANT_TSC;

    /*
     * TSC is not invariant and we're on SMP, this presents two problems:
     *
     *      (1) There might be a skew between the CPU, so that cpu0
     *          returns a TSC that is slightly different from cpu1.
     *          This screw may be due to (2), bad TSC initialization
     *          or slightly different TSC rates.
     *
     *      (2) Power management (and other things) may cause the TSC
     *          to run at a non-constant speed, and cause the speed
     *          to be different on the cpus. This will result in (1).
     *
     * If any of the above is detected, we will have to use ASYNC mode.
     */
    /* (1). Try check for current differences between the cpus. */
    if (supdrvGipInitDetermineAsyncTsc(&u64DiffCoresIgnored))
        return SUPGIPMODE_ASYNC_TSC;

    /* (2) If it's an AMD CPU with power management, we won't trust its TSC. */
    ASMCpuId(0, &uEAX, &uEBX, &uECX, &uEDX);
    if (   RTX86IsValidStdRange(uEAX)
        && (RTX86IsAmdCpu(uEBX, uECX, uEDX) || RTX86IsHygonCpu(uEBX, uECX, uEDX)) )
    {
        /* Check for APM support. */
        uEAX = ASMCpuId_EAX(0x80000000);
        if (RTX86IsValidExtRange(uEAX) && uEAX >= 0x80000007)
        {
            uEDX = ASMCpuId_EDX(0x80000007);
            if (uEDX & 0x3e)  /* STC|TM|THERMTRIP|VID|FID. Ignore TS. */
                return SUPGIPMODE_ASYNC_TSC;
        }
    }

    return SUPGIPMODE_SYNC_TSC;
}


/**
 * Initializes per-CPU GIP information.
 *
 * @param   pGip        Pointer to the GIP.
 * @param   pCpu        Pointer to which GIP CPU to initialize.
 * @param   u64NanoTS   The current nanosecond timestamp.
 * @param   uCpuHz      The CPU frequency to set, 0 if the caller doesn't know.
 */
static void supdrvGipInitCpu(PSUPGLOBALINFOPAGE pGip, PSUPGIPCPU pCpu, uint64_t u64NanoTS, uint64_t uCpuHz)
{
    pCpu->u32TransactionId   = 2;
    pCpu->u64NanoTS          = u64NanoTS;
    pCpu->u64TSC             = ASMReadTSC();
    pCpu->u64TSCSample       = GIP_TSC_DELTA_RSVD;
    pCpu->i64TSCDelta        = pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED ? INT64_MAX : 0;

    ASMAtomicWriteSize(&pCpu->enmState,             SUPGIPCPUSTATE_INVALID);
    ASMAtomicWriteU32(&pCpu->idCpu,                 NIL_RTCPUID);
    ASMAtomicWriteS16(&pCpu->iCpuSet,               -1);
    ASMAtomicWriteU16(&pCpu->iCpuGroup,             0);
    ASMAtomicWriteU16(&pCpu->iCpuGroupMember,       UINT16_MAX);
    ASMAtomicWriteU16(&pCpu->idApic,                UINT16_MAX);
    ASMAtomicWriteU32(&pCpu->iReservedForNumaNode,  0);

    /*
     * The first time we're called, we don't have a CPU frequency handy,
     * so pretend it's a 4 GHz CPU.  On CPUs that are online, we'll get
     * called again and at that point we have a more plausible CPU frequency
     * value handy.  The frequency history will also be adjusted again on
     * the 2nd timer callout (maybe we can skip that now?).
     */
    if (!uCpuHz)
    {
        pCpu->u64CpuHz             = _4G - 1;
        pCpu->u32UpdateIntervalTSC = (uint32_t)((_4G - 1) / pGip->u32UpdateHz);
    }
    else
    {
        pCpu->u64CpuHz             = uCpuHz;
        pCpu->u32UpdateIntervalTSC = (uint32_t)(uCpuHz / pGip->u32UpdateHz);
    }
    pCpu->au32TSCHistory[0]
        = pCpu->au32TSCHistory[1]
        = pCpu->au32TSCHistory[2]
        = pCpu->au32TSCHistory[3]
        = pCpu->au32TSCHistory[4]
        = pCpu->au32TSCHistory[5]
        = pCpu->au32TSCHistory[6]
        = pCpu->au32TSCHistory[7]
        = pCpu->u32UpdateIntervalTSC;
}


/**
 * Initializes the GIP data.
 *
 * @returns VBox status code.
 * @param   pDevExt             Pointer to the device instance data.
 * @param   pGip                Pointer to the read-write kernel mapping of the GIP.
 * @param   HCPhys              The physical address of the GIP.
 * @param   u64NanoTS           The current nanosecond timestamp.
 * @param   uUpdateHz           The update frequency.
 * @param   uUpdateIntervalNS   The update interval in nanoseconds.
 * @param   cCpus               The CPU count.
 * @param   cbGipCpuGroups      The supdrvOSGipGetGroupTableSize return value we
 *                              used when allocating the GIP structure.
 */
static int supdrvGipInit(PSUPDRVDEVEXT pDevExt, PSUPGLOBALINFOPAGE pGip, RTHCPHYS HCPhys,
                         uint64_t u64NanoTS, unsigned uUpdateHz, unsigned uUpdateIntervalNS,
                         unsigned cCpus, size_t cbGipCpuGroups)
{
    size_t const cbGip = RT_ALIGN_Z(RT_UOFFSETOF_DYN(SUPGLOBALINFOPAGE, aCPUs[cCpus]) + cbGipCpuGroups, PAGE_SIZE);
    unsigned i;
#ifdef DEBUG_DARWIN_GIP
    OSDBGPRINT(("supdrvGipInit: pGip=%p HCPhys=%lx u64NanoTS=%llu uUpdateHz=%d cCpus=%u\n", pGip, (long)HCPhys, u64NanoTS, uUpdateHz, cCpus));
#else
    LogFlow(("supdrvGipInit: pGip=%p HCPhys=%lx u64NanoTS=%llu uUpdateHz=%d cCpus=%u\n", pGip, (long)HCPhys, u64NanoTS, uUpdateHz, cCpus));
#endif

    /*
     * Initialize the structure.
     */
    memset(pGip, 0, cbGip);

    pGip->u32Magic                = SUPGLOBALINFOPAGE_MAGIC;
    pGip->u32Version              = SUPGLOBALINFOPAGE_VERSION;
    pGip->u32Mode                 = supdrvGipInitDetermineTscMode(pDevExt);
    if (   pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC
        /*|| pGip->u32Mode == SUPGIPMODE_SYNC_TSC */)
        pGip->enmUseTscDelta      = supdrvOSAreTscDeltasInSync() /* Allow OS override (windows). */
                                  ? SUPGIPUSETSCDELTA_ZERO_CLAIMED : SUPGIPUSETSCDELTA_PRACTICALLY_ZERO /* downgrade later */;
    else
        pGip->enmUseTscDelta      = SUPGIPUSETSCDELTA_NOT_APPLICABLE;
    pGip->cCpus                   = (uint16_t)cCpus;
    pGip->cPages                  = (uint16_t)(cbGip / PAGE_SIZE);
    pGip->u32UpdateHz             = uUpdateHz;
    pGip->u32UpdateIntervalNS     = uUpdateIntervalNS;
    pGip->fGetGipCpu              = SUPGIPGETCPU_APIC_ID;
    RTCpuSetEmpty(&pGip->OnlineCpuSet);
    RTCpuSetEmpty(&pGip->PresentCpuSet);
    RTMpGetSet(&pGip->PossibleCpuSet);
    pGip->cOnlineCpus             = RTMpGetOnlineCount();
    pGip->cPresentCpus            = RTMpGetPresentCount();
    pGip->cPossibleCpus           = RTMpGetCount();
    pGip->cPossibleCpuGroups      = 1;
    pGip->idCpuMax                = RTMpGetMaxCpuId();
    for (i = 0; i < RT_ELEMENTS(pGip->aiCpuFromApicId); i++)
        pGip->aiCpuFromApicId[i]    = UINT16_MAX;
    for (i = 0; i < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx); i++)
        pGip->aiCpuFromCpuSetIdx[i] = UINT16_MAX;
    for (i = 0; i < RT_ELEMENTS(pGip->aoffCpuGroup); i++)
        pGip->aoffCpuGroup[i]       = UINT32_MAX;
    for (i = 0; i < cCpus; i++)
        supdrvGipInitCpu(pGip, &pGip->aCPUs[i], u64NanoTS, 0 /*uCpuHz*/);
#ifdef RT_OS_WINDOWS
    int rc = supdrvOSInitGipGroupTable(pDevExt, pGip, cbGipCpuGroups);
    AssertRCReturn(rc, rc);
#endif

    /*
     * Link it to the device extension.
     */
    pDevExt->pGip      = pGip;
    pDevExt->HCPhysGip = HCPhys;
    pDevExt->cGipUsers = 0;

    return VINF_SUCCESS;
}


/**
 * Creates the GIP.
 *
 * @returns VBox status code.
 * @param   pDevExt     Instance data. GIP stuff may be updated.
 */
int VBOXCALL supdrvGipCreate(PSUPDRVDEVEXT pDevExt)
{
    PSUPGLOBALINFOPAGE  pGip;
    size_t              cbGip;
    size_t              cbGipCpuGroups;
    RTHCPHYS            HCPhysGip;
    uint32_t            u32SystemResolution;
    uint32_t            u32Interval;
    uint32_t            u32MinInterval;
    uint32_t            uMod;
    unsigned            cCpus;
    int                 rc;

    LogFlow(("supdrvGipCreate:\n"));

    /*
     * Assert order.
     */
    Assert(pDevExt->u32SystemTimerGranularityGrant == 0);
    Assert(pDevExt->GipMemObj == NIL_RTR0MEMOBJ);
    Assert(!pDevExt->pGipTimer);
#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    Assert(pDevExt->mtxGip != NIL_RTSEMMUTEX);
    Assert(pDevExt->mtxTscDelta != NIL_RTSEMMUTEX);
#else
    Assert(pDevExt->mtxGip != NIL_RTSEMFASTMUTEX);
    Assert(pDevExt->mtxTscDelta != NIL_RTSEMFASTMUTEX);
#endif

    /*
     * Check the CPU count.
     */
    cCpus = RTMpGetArraySize();
    if (cCpus > RT_MIN(RTCPUSET_MAX_CPUS, RT_ELEMENTS(pGip->aiCpuFromApicId)))
    {
        SUPR0Printf("VBoxDrv: Too many CPUs (%u) for the GIP (max %u)\n", cCpus, RT_MIN(RTCPUSET_MAX_CPUS, RT_ELEMENTS(pGip->aiCpuFromApicId)));
        return VERR_TOO_MANY_CPUS;
    }

    /*
     * Allocate a contiguous set of pages with a default kernel mapping.
     */
#ifdef RT_OS_WINDOWS
    cbGipCpuGroups = supdrvOSGipGetGroupTableSize(pDevExt);
#else
    cbGipCpuGroups = 0;
#endif
    cbGip = RT_UOFFSETOF_DYN(SUPGLOBALINFOPAGE, aCPUs[cCpus]) + cbGipCpuGroups;
    rc = RTR0MemObjAllocCont(&pDevExt->GipMemObj, cbGip, NIL_RTHCPHYS /*PhysHighest*/, false /*fExecutable*/);
    if (RT_FAILURE(rc))
    {
        OSDBGPRINT(("supdrvGipCreate: failed to allocate the GIP page. rc=%d\n", rc));
        return rc;
    }
    pGip = (PSUPGLOBALINFOPAGE)RTR0MemObjAddress(pDevExt->GipMemObj); AssertPtr(pGip);
    HCPhysGip = RTR0MemObjGetPagePhysAddr(pDevExt->GipMemObj, 0); Assert(HCPhysGip != NIL_RTHCPHYS);

    /*
     * Find a reasonable update interval and initialize the structure.
     */
    supdrvGipRequestHigherTimerFrequencyFromSystem(pDevExt);
    /** @todo figure out why using a 100Ms interval upsets timekeeping in VMs.
     *        See @bugref{6710}. */
    u32MinInterval      = RT_NS_10MS;
    u32SystemResolution = RTTimerGetSystemGranularity();
    u32Interval         = u32MinInterval;
    uMod                = u32MinInterval % u32SystemResolution;
    if (uMod)
        u32Interval += u32SystemResolution - uMod;

    rc = supdrvGipInit(pDevExt, pGip, HCPhysGip, RTTimeSystemNanoTS(), RT_NS_1SEC / u32Interval /*=Hz*/, u32Interval,
                       cCpus, cbGipCpuGroups);

    /*
     * Important sanity check...  (Sets rc)
     */
    if (RT_UNLIKELY(   pGip->enmUseTscDelta == SUPGIPUSETSCDELTA_ZERO_CLAIMED
                    && pGip->u32Mode == SUPGIPMODE_ASYNC_TSC
                    && !supdrvOSGetForcedAsyncTscMode(pDevExt)))
    {
        OSDBGPRINT(("supdrvGipCreate: Host-OS/user claims the TSC-deltas are zero but we detected async. TSC! Bad.\n"));
        rc = VERR_INTERNAL_ERROR_2;
    }

    /* It doesn't make sense to do TSC-delta detection on systems we detect as async. */
    AssertStmt(   pGip->u32Mode != SUPGIPMODE_ASYNC_TSC
               || pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ZERO_CLAIMED,
               rc = VERR_INTERNAL_ERROR_3);

    /*
     * Do the TSC frequency measurements.
     *
     * If we're in invariant TSC mode, just to a quick preliminary measurement
     * that the TSC-delta measurement code can use to yield cross calls.
     *
     * If we're in any of the other two modes, neither which require MP init,
     * notifications or deltas for the job, do the full measurement now so
     * that supdrvGipInitOnCpu() can populate the TSC interval and history
     * array with more reasonable values.
     */
    if (RT_SUCCESS(rc))
    {
        if (pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC)
        {
            rc = supdrvGipInitMeasureTscFreq(pGip, true /*fRough*/); /* cannot fail */
            supdrvGipInitStartTimerForRefiningInvariantTscFreq(pDevExt);
        }
        else
            rc = supdrvGipInitMeasureTscFreq(pGip, false /*fRough*/);
        if (RT_SUCCESS(rc))
        {
            /*
             * Start TSC-delta measurement thread before we start getting MP
             * events that will try kick it into action (includes the
             * RTMpOnAll/supdrvGipInitOnCpu call below).
             */
            RTCpuSetEmpty(&pDevExt->TscDeltaCpuSet);
            RTCpuSetEmpty(&pDevExt->TscDeltaObtainedCpuSet);
#ifdef SUPDRV_USE_TSC_DELTA_THREAD
            if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED)
                rc = supdrvTscDeltaThreadInit(pDevExt);
#endif
            if (RT_SUCCESS(rc))
            {
                rc = RTMpNotificationRegister(supdrvGipMpEvent, pDevExt);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Do GIP initialization on all online CPUs.  Wake up the
                     * TSC-delta thread afterwards.
                     */
                    rc = RTMpOnAll(supdrvGipInitOnCpu, pDevExt, pGip);
                    if (RT_SUCCESS(rc))
                    {
#ifdef SUPDRV_USE_TSC_DELTA_THREAD
                        supdrvTscDeltaThreadStartMeasurement(pDevExt, true /* fForceAll */);
#else
                        uint16_t iCpu;
                        if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED)
                        {
                            /*
                             * Measure the TSC deltas now that we have MP notifications.
                             */
                            int cTries = 5;
                            do
                            {
                                rc = supdrvTscMeasureInitialDeltas(pDevExt);
                                if (   rc != VERR_TRY_AGAIN
                                    && rc != VERR_CPU_OFFLINE)
                                    break;
                            } while (--cTries > 0);
                            for (iCpu = 0; iCpu < pGip->cCpus; iCpu++)
                                Log(("supdrvTscDeltaInit: cpu[%u] delta %lld\n", iCpu, pGip->aCPUs[iCpu].i64TSCDelta));
                        }
                        else
                        {
                            for (iCpu = 0; iCpu < pGip->cCpus; iCpu++)
                                AssertMsg(!pGip->aCPUs[iCpu].i64TSCDelta, ("iCpu=%u %lld mode=%d\n", iCpu, pGip->aCPUs[iCpu].i64TSCDelta, pGip->u32Mode));
                        }
                        if (RT_SUCCESS(rc))
#endif
                        {
                            /*
                             * Create the timer.
                             * If CPU_ALL isn't supported we'll have to fall back to synchronous mode.
                             */
                            if (pGip->u32Mode == SUPGIPMODE_ASYNC_TSC)
                            {
                                rc = RTTimerCreateEx(&pDevExt->pGipTimer, u32Interval, RTTIMER_FLAGS_CPU_ALL,
                                                     supdrvGipAsyncTimer, pDevExt);
                                if (rc == VERR_NOT_SUPPORTED)
                                {
                                    OSDBGPRINT(("supdrvGipCreate: omni timer not supported, falling back to synchronous mode\n"));
                                    pGip->u32Mode = SUPGIPMODE_SYNC_TSC;
                                }
                            }
                            if (pGip->u32Mode != SUPGIPMODE_ASYNC_TSC)
                                rc = RTTimerCreateEx(&pDevExt->pGipTimer, u32Interval, 0 /* fFlags */,
                                                     supdrvGipSyncAndInvariantTimer, pDevExt);
                            if (RT_SUCCESS(rc))
                            {
                                /*
                                 * We're good.
                                 */
                                Log(("supdrvGipCreate: %u ns interval.\n", u32Interval));
                                supdrvGipReleaseHigherTimerFrequencyFromSystem(pDevExt);

                                g_pSUPGlobalInfoPage = pGip;
                                return VINF_SUCCESS;
                            }

                            OSDBGPRINT(("supdrvGipCreate: failed create GIP timer at %u ns interval. rc=%Rrc\n", u32Interval, rc));
                            Assert(!pDevExt->pGipTimer);
                        }
                    }
                    else
                        OSDBGPRINT(("supdrvGipCreate: RTMpOnAll failed. rc=%Rrc\n", rc));
                }
                else
                    OSDBGPRINT(("supdrvGipCreate: failed to register MP event notfication. rc=%Rrc\n", rc));
            }
            else
                OSDBGPRINT(("supdrvGipCreate: supdrvTscDeltaInit failed. rc=%Rrc\n", rc));
        }
        else
            OSDBGPRINT(("supdrvGipCreate: supdrvTscMeasureInitialDeltas failed. rc=%Rrc\n", rc));
    }

    /* Releases timer frequency increase too. */
    supdrvGipDestroy(pDevExt);
    return rc;
}


/**
 * Invalidates the GIP data upon termination.
 *
 * @param   pGip        Pointer to the read-write kernel mapping of the GIP.
 */
static void supdrvGipTerm(PSUPGLOBALINFOPAGE pGip)
{
    unsigned i;
    pGip->u32Magic = 0;
    for (i = 0; i < pGip->cCpus; i++)
    {
        pGip->aCPUs[i].u64NanoTS = 0;
        pGip->aCPUs[i].u64TSC = 0;
        pGip->aCPUs[i].iTSCHistoryHead = 0;
        pGip->aCPUs[i].u64TSCSample = 0;
        pGip->aCPUs[i].i64TSCDelta = INT64_MAX;
    }
}


/**
 * Terminates the GIP.
 *
 * @param   pDevExt     Instance data. GIP stuff may be updated.
 */
void VBOXCALL supdrvGipDestroy(PSUPDRVDEVEXT pDevExt)
{
    int rc;
#ifdef DEBUG_DARWIN_GIP
    OSDBGPRINT(("supdrvGipDestroy: pDevExt=%p pGip=%p pGipTimer=%p GipMemObj=%p\n", pDevExt,
                pDevExt->GipMemObj != NIL_RTR0MEMOBJ ? RTR0MemObjAddress(pDevExt->GipMemObj) : NULL,
                pDevExt->pGipTimer, pDevExt->GipMemObj));
#endif

    /*
     * Stop receiving MP notifications before tearing anything else down.
     */
    RTMpNotificationDeregister(supdrvGipMpEvent, pDevExt);

#ifdef SUPDRV_USE_TSC_DELTA_THREAD
    /*
     * Terminate the TSC-delta measurement thread and resources.
     */
    supdrvTscDeltaTerm(pDevExt);
#endif

    /*
     * Destroy the TSC-refinement timer.
     */
    if (pDevExt->pInvarTscRefineTimer)
    {
        RTTimerDestroy(pDevExt->pInvarTscRefineTimer);
        pDevExt->pInvarTscRefineTimer = NULL;
    }

    /*
     * Invalid the GIP data.
     */
    if (pDevExt->pGip)
    {
        supdrvGipTerm(pDevExt->pGip);
        pDevExt->pGip = NULL;
    }
    g_pSUPGlobalInfoPage = NULL;

    /*
     * Destroy the timer and free the GIP memory object.
     */
    if (pDevExt->pGipTimer)
    {
        rc = RTTimerDestroy(pDevExt->pGipTimer); AssertRC(rc);
        pDevExt->pGipTimer = NULL;
    }

    if (pDevExt->GipMemObj != NIL_RTR0MEMOBJ)
    {
        rc = RTR0MemObjFree(pDevExt->GipMemObj, true /* free mappings */); AssertRC(rc);
        pDevExt->GipMemObj = NIL_RTR0MEMOBJ;
    }

    /*
     * Finally, make sure we've release the system timer resolution request
     * if one actually succeeded and is still pending.
     */
    supdrvGipReleaseHigherTimerFrequencyFromSystem(pDevExt);
}




/*
 *
 *
 * GIP Update Timer Related Code
 * GIP Update Timer Related Code
 * GIP Update Timer Related Code
 *
 *
 */


/**
 * Worker routine for supdrvGipUpdate() and supdrvGipUpdatePerCpu() that
 * updates all the per cpu data except the transaction id.
 *
 * @param   pDevExt         The device extension.
 * @param   pGipCpu         Pointer to the per cpu data.
 * @param   u64NanoTS       The current time stamp.
 * @param   u64TSC          The current TSC.
 * @param   iTick           The current timer tick.
 *
 * @remarks Can be called with interrupts disabled!
 */
static void supdrvGipDoUpdateCpu(PSUPDRVDEVEXT pDevExt, PSUPGIPCPU pGipCpu, uint64_t u64NanoTS, uint64_t u64TSC, uint64_t iTick)
{
    uint64_t            u64TSCDelta;
    bool                fUpdateCpuHz;
    PSUPGLOBALINFOPAGE  pGip = pDevExt->pGip;
    AssertPtrReturnVoid(pGip);

    /* Delta between this and the previous update. */
    ASMAtomicUoWriteU32(&pGipCpu->u32PrevUpdateIntervalNS, (uint32_t)(u64NanoTS - pGipCpu->u64NanoTS));

    /*
     * Update the NanoTS.
     */
    ASMAtomicWriteU64(&pGipCpu->u64NanoTS, u64NanoTS);

    /*
     * Calc TSC delta.
     */
    u64TSCDelta = u64TSC - pGipCpu->u64TSC;
    ASMAtomicWriteU64(&pGipCpu->u64TSC, u64TSC);

    /*
     * Determine if we need to update the CPU (TSC) frequency calculation.
     *
     * We don't need to keep recalculating the frequency when it's invariant,
     * unless the special tstGIP-2 testing mode is enabled.
     */
    fUpdateCpuHz = pGip->u32Mode != SUPGIPMODE_INVARIANT_TSC;
    if (!(pGip->fFlags & SUPGIP_FLAGS_TESTING))
    { /* likely*/ }
    else
    {
        uint32_t fGipFlags = pGip->fFlags;
        if (fGipFlags & (SUPGIP_FLAGS_TESTING_ENABLE | SUPGIP_FLAGS_TESTING_START))
        {
            if (fGipFlags & SUPGIP_FLAGS_TESTING_START)
            {
                /* Cache the TSC frequency before forcing updates due to test mode. */
                if (!fUpdateCpuHz)
                    pDevExt->uGipTestModeInvariantCpuHz = pGip->aCPUs[0].u64CpuHz;
                ASMAtomicAndU32(&pGip->fFlags, ~SUPGIP_FLAGS_TESTING_START);
            }
            fUpdateCpuHz = true;
        }
        else if (fGipFlags & SUPGIP_FLAGS_TESTING_STOP)
        {
            /* Restore the cached TSC frequency if any. */
            if (!fUpdateCpuHz)
            {
                Assert(pDevExt->uGipTestModeInvariantCpuHz);
                ASMAtomicWriteU64(&pGip->aCPUs[0].u64CpuHz, pDevExt->uGipTestModeInvariantCpuHz);
            }
            ASMAtomicAndU32(&pGip->fFlags, ~(SUPGIP_FLAGS_TESTING_STOP | SUPGIP_FLAGS_TESTING));
        }
    }

    /*
     * Calculate the CPU (TSC) frequency if necessary.
     */
    if (fUpdateCpuHz)
    {
        uint64_t    u64CpuHz;
        uint32_t    u32UpdateIntervalTSC;
        uint32_t    u32UpdateIntervalTSCSlack;
        uint32_t    u32TransactionId;
        unsigned    iTSCHistoryHead;

        if (u64TSCDelta >> 32)
        {
            u64TSCDelta = pGipCpu->u32UpdateIntervalTSC;
            pGipCpu->cErrors++;
        }

        /*
         * On the 2nd and 3rd callout, reset the history with the current TSC
         * interval since the values entered by supdrvGipInit are totally off.
         * The interval on the 1st callout completely unreliable, the 2nd is a bit
         * better, while the 3rd should be most reliable.
         */
        /** @todo Could we drop this now that we initializes the history
         *        with nominal TSC frequency values? */
        u32TransactionId = pGipCpu->u32TransactionId;
        if (RT_UNLIKELY(   (   u32TransactionId == 5
                            || u32TransactionId == 7)
                        && (   iTick == 2
                            || iTick == 3) ))
        {
            unsigned i;
            for (i = 0; i < RT_ELEMENTS(pGipCpu->au32TSCHistory); i++)
                ASMAtomicUoWriteU32(&pGipCpu->au32TSCHistory[i], (uint32_t)u64TSCDelta);
        }

        /*
         * Validate the NanoTS deltas between timer fires with an arbitrary threshold of 0.5%.
         * Wait until we have at least one full history since the above history reset. The
         * assumption is that the majority of the previous history values will be tolerable.
         * See @bugref{6710#c67}.
         */
        /** @todo Could we drop the fudging there now that we initializes the history
         *        with nominal TSC frequency values?  */
        if (   u32TransactionId > 23 /* 7 + (8 * 2) */
            && pGip->u32Mode != SUPGIPMODE_ASYNC_TSC)
        {
            uint32_t uNanoTsThreshold = pGip->u32UpdateIntervalNS / 200;
            if (   pGipCpu->u32PrevUpdateIntervalNS > pGip->u32UpdateIntervalNS + uNanoTsThreshold
                || pGipCpu->u32PrevUpdateIntervalNS < pGip->u32UpdateIntervalNS - uNanoTsThreshold)
            {
                uint32_t u32;
                u32  = pGipCpu->au32TSCHistory[0];
                u32 += pGipCpu->au32TSCHistory[1];
                u32 += pGipCpu->au32TSCHistory[2];
                u32 += pGipCpu->au32TSCHistory[3];
                u32 >>= 2;
                u64TSCDelta  = pGipCpu->au32TSCHistory[4];
                u64TSCDelta += pGipCpu->au32TSCHistory[5];
                u64TSCDelta += pGipCpu->au32TSCHistory[6];
                u64TSCDelta += pGipCpu->au32TSCHistory[7];
                u64TSCDelta >>= 2;
                u64TSCDelta += u32;
                u64TSCDelta >>= 1;
            }
        }

        /*
         * TSC History.
         */
        Assert(RT_ELEMENTS(pGipCpu->au32TSCHistory) == 8);
        iTSCHistoryHead = (pGipCpu->iTSCHistoryHead + 1) & 7;
        ASMAtomicWriteU32(&pGipCpu->iTSCHistoryHead, iTSCHistoryHead);
        ASMAtomicWriteU32(&pGipCpu->au32TSCHistory[iTSCHistoryHead], (uint32_t)u64TSCDelta);

        /*
         * UpdateIntervalTSC = average of last 8,2,1 intervals depending on update HZ.
         *
         * On Windows, we have an occasional (but recurring) sour value that messed up
         * the history but taking only 1 interval reduces the precision overall.
         */
        if (   pGip->u32Mode == SUPGIPMODE_INVARIANT_TSC
            || pGip->u32UpdateHz >= 1000)
        {
            uint32_t u32;
            u32  = pGipCpu->au32TSCHistory[0];
            u32 += pGipCpu->au32TSCHistory[1];
            u32 += pGipCpu->au32TSCHistory[2];
            u32 += pGipCpu->au32TSCHistory[3];
            u32 >>= 2;
            u32UpdateIntervalTSC  = pGipCpu->au32TSCHistory[4];
            u32UpdateIntervalTSC += pGipCpu->au32TSCHistory[5];
            u32UpdateIntervalTSC += pGipCpu->au32TSCHistory[6];
            u32UpdateIntervalTSC += pGipCpu->au32TSCHistory[7];
            u32UpdateIntervalTSC >>= 2;
            u32UpdateIntervalTSC += u32;
            u32UpdateIntervalTSC >>= 1;

            /* Value chosen for a 2GHz Athlon64 running linux 2.6.10/11. */
            u32UpdateIntervalTSCSlack = u32UpdateIntervalTSC >> 14;
        }
        else if (pGip->u32UpdateHz >= 90)
        {
            u32UpdateIntervalTSC  = (uint32_t)u64TSCDelta;
            u32UpdateIntervalTSC += pGipCpu->au32TSCHistory[(iTSCHistoryHead - 1) & 7];
            u32UpdateIntervalTSC >>= 1;

            /* value chosen on a 2GHz thinkpad running windows */
            u32UpdateIntervalTSCSlack = u32UpdateIntervalTSC >> 7;
        }
        else
        {
            u32UpdateIntervalTSC  = (uint32_t)u64TSCDelta;

            /* This value hasn't be checked yet.. waiting for OS/2 and 33Hz timers.. :-) */
            u32UpdateIntervalTSCSlack = u32UpdateIntervalTSC >> 6;
        }
        ASMAtomicWriteU32(&pGipCpu->u32UpdateIntervalTSC, u32UpdateIntervalTSC + u32UpdateIntervalTSCSlack);

        /*
         * CpuHz.
         */
        u64CpuHz = ASMMult2xU32RetU64(u32UpdateIntervalTSC, RT_NS_1SEC);
        u64CpuHz /= pGip->u32UpdateIntervalNS;
        ASMAtomicWriteU64(&pGipCpu->u64CpuHz, u64CpuHz);
    }
}


/**
 * Updates the GIP.
 *
 * @param   pDevExt         The device extension.
 * @param   u64NanoTS       The current nanosecond timestamp.
 * @param   u64TSC          The current TSC timestamp.
 * @param   idCpu           The CPU ID.
 * @param   iTick           The current timer tick.
 *
 * @remarks Can be called with interrupts disabled!
 */
static void supdrvGipUpdate(PSUPDRVDEVEXT pDevExt, uint64_t u64NanoTS, uint64_t u64TSC, RTCPUID idCpu, uint64_t iTick)
{
    /*
     * Determine the relevant CPU data.
     */
    PSUPGIPCPU pGipCpu;
    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;
    AssertPtrReturnVoid(pGip);

    if (pGip->u32Mode != SUPGIPMODE_ASYNC_TSC)
        pGipCpu = &pGip->aCPUs[0];
    else
    {
        unsigned iCpu;
        uint32_t idApic = supdrvGipGetApicId(pGip);
        if (RT_LIKELY(idApic < RT_ELEMENTS(pGip->aiCpuFromApicId)))
        { /* likely */ }
        else
            return;
        iCpu = pGip->aiCpuFromApicId[idApic];
        if (RT_LIKELY(iCpu < pGip->cCpus))
        { /* likely */ }
        else
            return;
        pGipCpu = &pGip->aCPUs[iCpu];
        if (RT_LIKELY(pGipCpu->idCpu == idCpu))
        { /* likely */ }
        else
            return;
    }

    /*
     * Start update transaction.
     */
    if (!(ASMAtomicIncU32(&pGipCpu->u32TransactionId) & 1))
    {
        /* this can happen on win32 if we're taking to long and there are more CPUs around. shouldn't happen though. */
        AssertMsgFailed(("Invalid transaction id, %#x, not odd!\n", pGipCpu->u32TransactionId));
        ASMAtomicIncU32(&pGipCpu->u32TransactionId);
        pGipCpu->cErrors++;
        return;
    }

    /*
     * Recalc the update frequency every 0x800th time.
     */
    if (   pGip->u32Mode != SUPGIPMODE_INVARIANT_TSC   /* cuz we're not recalculating the frequency on invariant hosts. */
        && !(pGipCpu->u32TransactionId & (GIP_UPDATEHZ_RECALC_FREQ * 2 - 2)))
    {
        if (pGip->u64NanoTSLastUpdateHz)
        {
#ifdef RT_ARCH_AMD64 /** @todo fix 64-bit div here to work on x86 linux. */
            uint64_t u64Delta = u64NanoTS - pGip->u64NanoTSLastUpdateHz;
            uint32_t u32UpdateHz = (uint32_t)((RT_NS_1SEC_64 * GIP_UPDATEHZ_RECALC_FREQ) / u64Delta);
            if (u32UpdateHz <= 2000 && u32UpdateHz >= 30)
            {
                /** @todo r=ramshankar: Changing u32UpdateHz might screw up TSC frequency
                 *        calculation on non-invariant hosts if it changes the history decision
                 *        taken in supdrvGipDoUpdateCpu(). */
                uint64_t u64Interval = u64Delta / GIP_UPDATEHZ_RECALC_FREQ;
                ASMAtomicWriteU32(&pGip->u32UpdateHz, u32UpdateHz);
                ASMAtomicWriteU32(&pGip->u32UpdateIntervalNS, (uint32_t)u64Interval);
            }
#endif
        }
        ASMAtomicWriteU64(&pGip->u64NanoTSLastUpdateHz, u64NanoTS | 1);
    }

    /*
     * Update the data.
     */
    supdrvGipDoUpdateCpu(pDevExt, pGipCpu, u64NanoTS, u64TSC, iTick);

    /*
     * Complete transaction.
     */
    ASMAtomicIncU32(&pGipCpu->u32TransactionId);
}


/**
 * Updates the per cpu GIP data for the calling cpu.
 *
 * @param   pDevExt         The device extension.
 * @param   u64NanoTS       The current nanosecond timestamp.
 * @param   u64TSC          The current TSC timesaver.
 * @param   idCpu           The CPU ID.
 * @param   idApic          The APIC id for the CPU index.
 * @param   iTick           The current timer tick.
 *
 * @remarks Can be called with interrupts disabled!
 */
static void supdrvGipUpdatePerCpu(PSUPDRVDEVEXT pDevExt, uint64_t u64NanoTS, uint64_t u64TSC,
                                  RTCPUID idCpu, uint8_t idApic, uint64_t iTick)
{
    uint32_t iCpu;
    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;

    /*
     * Avoid a potential race when a CPU online notification doesn't fire on
     * the onlined CPU but the tick creeps in before the event notification is
     * run.
     */
    if (RT_LIKELY(iTick != 1))
    { /* likely*/ }
    else
    {
        iCpu = supdrvGipFindOrAllocCpuIndexForCpuId(pGip, idCpu);
        if (pGip->aCPUs[iCpu].enmState == SUPGIPCPUSTATE_OFFLINE)
            supdrvGipMpEventOnlineOrInitOnCpu(pDevExt, idCpu);
    }

    iCpu = pGip->aiCpuFromApicId[idApic];
    if (RT_LIKELY(iCpu < pGip->cCpus))
    {
        PSUPGIPCPU pGipCpu = &pGip->aCPUs[iCpu];
        if (pGipCpu->idCpu == idCpu)
        {
            /*
             * Start update transaction.
             */
            if (!(ASMAtomicIncU32(&pGipCpu->u32TransactionId) & 1))
            {
                AssertMsgFailed(("Invalid transaction id, %#x, not odd!\n", pGipCpu->u32TransactionId));
                ASMAtomicIncU32(&pGipCpu->u32TransactionId);
                pGipCpu->cErrors++;
                return;
            }

            /*
             * Update the data.
             */
            supdrvGipDoUpdateCpu(pDevExt, pGipCpu, u64NanoTS, u64TSC, iTick);

            /*
             * Complete transaction.
             */
            ASMAtomicIncU32(&pGipCpu->u32TransactionId);
        }
    }
}


/**
 * Timer callback function for the sync and invariant GIP modes.
 *
 * @param   pTimer      The timer.
 * @param   pvUser      Opaque pointer to the device extension.
 * @param   iTick       The timer tick.
 */
static DECLCALLBACK(void) supdrvGipSyncAndInvariantTimer(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PSUPDRVDEVEXT      pDevExt   = (PSUPDRVDEVEXT)pvUser;
    PSUPGLOBALINFOPAGE pGip      = pDevExt->pGip;
    RTCCUINTREG        fEFlags   = ASMIntDisableFlags(); /* No interruptions please (real problem on S10). */
    uint64_t           u64TSC    = ASMReadTSC();
    uint64_t           u64NanoTS = RTTimeSystemNanoTS();
    RT_NOREF1(pTimer);

    if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_PRACTICALLY_ZERO)
    {
        /*
         * The calculations in supdrvGipUpdate() is somewhat timing sensitive,
         * missing timer ticks is not an option for GIP because the GIP users
         * will end up incrementing the time in 1ns per time getter call until
         * there is a complete timer update.   So, if the delta has yet to be
         * calculated, we just pretend it is zero for now (the GIP users
         * probably won't have it for a wee while either and will do the same).
         *
         * We could maybe on some platforms try cross calling a CPU with a
         * working delta here, but it's not worth the hassle since the
         * likelihood of this happening is really low.  On Windows, Linux, and
         * Solaris timers fire on the CPU they were registered/started on.
         * Darwin timers doesn't necessarily (they are high priority threads).
         */
        uint32_t iCpuSet = RTMpCpuIdToSetIndex(RTMpCpuId());
        uint16_t iGipCpu = RT_LIKELY(iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx))
                         ? pGip->aiCpuFromCpuSetIdx[iCpuSet] : UINT16_MAX;
        Assert(!ASMIntAreEnabled());
        if (RT_LIKELY(iGipCpu < pGip->cCpus))
        {
            int64_t iTscDelta = pGip->aCPUs[iGipCpu].i64TSCDelta;
            if (iTscDelta != INT64_MAX)
                u64TSC -= iTscDelta;
        }
    }

    supdrvGipUpdate(pDevExt, u64NanoTS, u64TSC, NIL_RTCPUID, iTick);

    ASMSetFlags(fEFlags);
}


/**
 * Timer callback function for async GIP mode.
 * @param   pTimer      The timer.
 * @param   pvUser      Opaque pointer to the device extension.
 * @param   iTick       The timer tick.
 */
static DECLCALLBACK(void) supdrvGipAsyncTimer(PRTTIMER pTimer, void *pvUser, uint64_t iTick)
{
    PSUPDRVDEVEXT   pDevExt   = (PSUPDRVDEVEXT)pvUser;
    RTCCUINTREG     fEFlags   = ASMIntDisableFlags(); /* No interruptions please (real problem on S10). */
    RTCPUID         idCpu     = RTMpCpuId();
    uint64_t        u64TSC    = ASMReadTSC();
    uint64_t        NanoTS    = RTTimeSystemNanoTS();
    RT_NOREF1(pTimer);

    /** @todo reset the transaction number and whatnot when iTick == 1. */
    if (pDevExt->idGipMaster == idCpu)
        supdrvGipUpdate(pDevExt, NanoTS, u64TSC, idCpu, iTick);
    else
        supdrvGipUpdatePerCpu(pDevExt, NanoTS, u64TSC, idCpu, supdrvGipGetApicId(pDevExt->pGip), iTick);

    ASMSetFlags(fEFlags);
}




/*
 *
 *
 * TSC Delta Measurements And Related Code
 * TSC Delta Measurements And Related Code
 * TSC Delta Measurements And Related Code
 *
 *
 */


/*
 * Select TSC delta measurement algorithm.
 */
#if 0
# define GIP_TSC_DELTA_METHOD_1
#else
# define GIP_TSC_DELTA_METHOD_2
#endif

/** For padding variables to keep them away from other cache lines.  Better too
 * large than too small!
 * @remarks Current AMD64 and x86 CPUs seems to use 64 bytes.  There are claims
 *          that NetBurst had 128 byte cache lines while the 486 thru Pentium
 *          III had 32 bytes cache lines. */
#define GIP_TSC_DELTA_CACHE_LINE_SIZE           128


/**
 * TSC delta measurement algorithm \#2 result entry.
 */
typedef struct SUPDRVTSCDELTAMETHOD2ENTRY
{
    uint32_t    iSeqMine;
    uint32_t    iSeqOther;
    uint64_t    uTsc;
} SUPDRVTSCDELTAMETHOD2ENTRY;

/**
 * TSC delta measurement algorithm \#2 Data.
 */
typedef struct SUPDRVTSCDELTAMETHOD2
{
    /** Padding to make sure the iCurSeqNo is in its own cache line. */
    uint64_t                    au64CacheLinePaddingBefore[GIP_TSC_DELTA_CACHE_LINE_SIZE / sizeof(uint64_t)];
    /** The current sequence number of this worker. */
    uint32_t volatile           iCurSeqNo;
    /** Padding to make sure the iCurSeqNo is in its own cache line. */
    uint32_t                    au64CacheLinePaddingAfter[GIP_TSC_DELTA_CACHE_LINE_SIZE / sizeof(uint32_t) - 1];
    /** Result table. */
    SUPDRVTSCDELTAMETHOD2ENTRY  aResults[64];
} SUPDRVTSCDELTAMETHOD2;
/** Pointer to the data for TSC delta measurement algorithm \#2 .*/
typedef SUPDRVTSCDELTAMETHOD2 *PSUPDRVTSCDELTAMETHOD2;


/**
 * The TSC delta synchronization struct, version 2.
 *
 * The synchronization variable is completely isolated in its own cache line
 * (provided our max cache line size estimate is correct).
 */
typedef struct SUPTSCDELTASYNC2
{
    /** Padding to make sure the uVar1 is in its own cache line. */
    uint64_t                    au64CacheLinePaddingBefore[GIP_TSC_DELTA_CACHE_LINE_SIZE / sizeof(uint64_t)];

    /** The synchronization variable, holds values GIP_TSC_DELTA_SYNC_*. */
    volatile uint32_t           uSyncVar;
    /** Sequence synchronizing variable used for post 'GO' synchronization. */
    volatile uint32_t           uSyncSeq;

    /** Padding to make sure the uVar1 is in its own cache line. */
    uint64_t                    au64CacheLinePaddingAfter[GIP_TSC_DELTA_CACHE_LINE_SIZE / sizeof(uint64_t) - 2];

    /** Start RDTSC value.  Put here mainly to save stack space. */
    uint64_t                    uTscStart;
    /** Copy of SUPDRVGIPTSCDELTARGS::cMaxTscTicks. */
    uint64_t                    cMaxTscTicks;
} SUPTSCDELTASYNC2;
AssertCompileSize(SUPTSCDELTASYNC2, GIP_TSC_DELTA_CACHE_LINE_SIZE * 2 + sizeof(uint64_t));
typedef SUPTSCDELTASYNC2 *PSUPTSCDELTASYNC2;

/** Prestart wait. */
#define GIP_TSC_DELTA_SYNC2_PRESTART_WAIT    UINT32_C(0x0ffe)
/** Prestart aborted. */
#define GIP_TSC_DELTA_SYNC2_PRESTART_ABORT   UINT32_C(0x0fff)
/** Ready (on your mark). */
#define GIP_TSC_DELTA_SYNC2_READY            UINT32_C(0x1000)
/** Steady (get set). */
#define GIP_TSC_DELTA_SYNC2_STEADY           UINT32_C(0x1001)
/** Go! */
#define GIP_TSC_DELTA_SYNC2_GO               UINT32_C(0x1002)
/** Used by the verification test. */
#define GIP_TSC_DELTA_SYNC2_GO_GO            UINT32_C(0x1003)

/** We reached the time limit. */
#define GIP_TSC_DELTA_SYNC2_TIMEOUT          UINT32_C(0x1ffe)
/** The other party won't touch the sync struct ever again. */
#define GIP_TSC_DELTA_SYNC2_FINAL            UINT32_C(0x1fff)


/**
 * Argument package/state passed by supdrvTscMeasureDeltaOne() to the RTMpOn
 * callback worker.
 * @todo add
 */
typedef struct SUPDRVGIPTSCDELTARGS
{
    /** The device extension.   */
    PSUPDRVDEVEXT               pDevExt;
    /** Pointer to the GIP CPU array entry for the worker. */
    PSUPGIPCPU                  pWorker;
    /** Pointer to the GIP CPU array entry for the master. */
    PSUPGIPCPU                  pMaster;
    /** The maximum number of ticks to spend in supdrvTscMeasureDeltaCallback.
     * (This is what we need a rough TSC frequency for.)  */
    uint64_t                    cMaxTscTicks;
    /** Used to abort synchronization setup. */
    bool volatile               fAbortSetup;

    /** Padding to make sure the master variables live in its own cache lines. */
    uint64_t                    au64CacheLinePaddingBefore[GIP_TSC_DELTA_CACHE_LINE_SIZE / sizeof(uint64_t)];

    /** @name Master
     * @{ */
    /** The time the master spent in the MP worker.  */
    uint64_t                    cElapsedMasterTscTicks;
    /** The iTry value when stopped at. */
    uint32_t                    iTry;
    /** Set if the run timed out.   */
    bool volatile               fTimedOut;
    /** Pointer to the master's synchronization struct (on stack). */
    PSUPTSCDELTASYNC2 volatile  pSyncMaster;
    /** Master data union. */
    union
    {
        /** Data (master) for delta verification. */
        struct
        {
            /** Verification test TSC values for the master. */
            uint64_t volatile       auTscs[32];
        } Verify;
        /** Data (master) for measurement method \#2. */
        struct
        {
            /** Data and sequence number. */
            SUPDRVTSCDELTAMETHOD2   Data;
            /** The lag setting for the next run. */
            bool                    fLag;
            /** Number of hits. */
            uint32_t                cHits;
        } M2;
    } uMaster;
    /** The verifier verdict, VINF_SUCCESS if ok, VERR_OUT_OF_RANGE if not,
     * VERR_TRY_AGAIN on timeout. */
    int32_t                     rcVerify;
#ifdef TSCDELTA_VERIFY_WITH_STATS
    /** The maximum difference between TSC read during delta verification. */
    int64_t                     cMaxVerifyTscTicks;
    /** The minimum difference between two TSC reads during verification. */
    int64_t                     cMinVerifyTscTicks;
    /** The bad TSC diff, worker relative to master (= worker - master).
     * Negative value means the worker is behind the master.  */
    int64_t                     iVerifyBadTscDiff;
#endif
    /** @} */

    /** Padding to make sure the worker variables live is in its own cache line. */
    uint64_t                    au64CacheLinePaddingBetween[GIP_TSC_DELTA_CACHE_LINE_SIZE / sizeof(uint64_t)];

    /** @name Proletarian
     * @{ */
    /** Pointer to the worker's synchronization struct (on stack). */
    PSUPTSCDELTASYNC2 volatile  pSyncWorker;
    /** The time the worker spent in the MP worker.  */
    uint64_t                    cElapsedWorkerTscTicks;
    /** Worker data union. */
    union
    {
        /** Data (worker) for delta verification. */
        struct
        {
            /** Verification test TSC values for the worker. */
            uint64_t volatile       auTscs[32];
        } Verify;
        /** Data (worker) for measurement method \#2. */
        struct
        {
            /** Data and sequence number. */
            SUPDRVTSCDELTAMETHOD2   Data;
            /** The lag setting for the next run (set by master). */
            bool                    fLag;
        } M2;
    } uWorker;
    /** @} */

    /** Padding to make sure the above is in its own cache line. */
    uint64_t                    au64CacheLinePaddingAfter[GIP_TSC_DELTA_CACHE_LINE_SIZE / sizeof(uint64_t)];
} SUPDRVGIPTSCDELTARGS;
typedef SUPDRVGIPTSCDELTARGS *PSUPDRVGIPTSCDELTARGS;


/** @name Macros that implements the basic synchronization steps common to
 *        the algorithms.
 *
 * Must be used from loop as the timeouts are implemented via 'break' statements
 * at the moment.
 *
 * @{
 */
#if defined(DEBUG_bird) /* || defined(VBOX_STRICT) */
# define TSCDELTA_DBG_VARS()            uint32_t iDbgCounter
# define TSCDELTA_DBG_START_LOOP()      do { iDbgCounter = 0; } while (0)
# define TSCDELTA_DBG_CHECK_LOOP() \
    do { iDbgCounter++; if ((iDbgCounter & UINT32_C(0x01ffffff)) == 0) RT_BREAKPOINT(); } while (0)
#else
# define TSCDELTA_DBG_VARS()            ((void)0)
# define TSCDELTA_DBG_START_LOOP()      ((void)0)
# define TSCDELTA_DBG_CHECK_LOOP()      ((void)0)
#endif
#if 0
# define TSCDELTA_DBG_SYNC_MSG(a_Args)  SUPR0Printf a_Args
#else
# define TSCDELTA_DBG_SYNC_MSG(a_Args)  ((void)0)
#endif
#if 0
# define TSCDELTA_DBG_SYNC_MSG2(a_Args) SUPR0Printf a_Args
#else
# define TSCDELTA_DBG_SYNC_MSG2(a_Args) ((void)0)
#endif
#if 0
# define TSCDELTA_DBG_SYNC_MSG9(a_Args) SUPR0Printf a_Args
#else
# define TSCDELTA_DBG_SYNC_MSG9(a_Args) ((void)0)
#endif


static bool supdrvTscDeltaSync2_Before(PSUPTSCDELTASYNC2 pMySync, PSUPTSCDELTASYNC2 pOtherSync,
                                       bool fIsMaster, PRTCCUINTREG pfEFlags, PSUPDRVGIPTSCDELTARGS pArgs)
{
    uint32_t        iMySeq  = fIsMaster ? 0 : 256;
    uint32_t const  iMaxSeq = iMySeq + 16;  /* For the last loop, darn linux/freebsd C-ishness. */
    uint32_t        u32Tmp;
    uint32_t        iSync2Loops = 0;
    RTCCUINTREG     fEFlags;
    TSCDELTA_DBG_VARS();

    *pfEFlags = X86_EFL_IF | X86_EFL_1; /* should shut up most nagging compilers. */

    /*
     * The master tells the worker to get on it's mark.
     */
    if (fIsMaster)
    {
        if (RT_LIKELY(ASMAtomicCmpXchgU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_STEADY, GIP_TSC_DELTA_SYNC2_READY)))
        { /* likely*/ }
        else
        {
            TSCDELTA_DBG_SYNC_MSG(("sync/before/%s: #1 uSyncVar=%#x\n", fIsMaster ? "master" : "worker", pOtherSync->uSyncVar));
            return false;
        }
    }

    /*
     * Wait for the on your mark signal (ack in the master case). We process timeouts here.
     */
    ASMAtomicWriteU32(&(pMySync)->uSyncSeq, 0);
    for (;;)
    {
        fEFlags = ASMIntDisableFlags();
        u32Tmp = ASMAtomicReadU32(&pMySync->uSyncVar);
        if (u32Tmp == GIP_TSC_DELTA_SYNC2_STEADY)
            break;
        ASMSetFlags(fEFlags);
        ASMNopPause();

        /* Abort? */
        if (u32Tmp != GIP_TSC_DELTA_SYNC2_READY)
        {
            TSCDELTA_DBG_SYNC_MSG(("sync/before/%s: #2 u32Tmp=%#x\n", fIsMaster ? "master" : "worker", u32Tmp));
            return false;
        }

        /* Check for timeouts every so often (not every loop in case RDTSC is
           trapping or something).  Must check the first time around. */
#if 0 /* For debugging the timeout paths. */
        static uint32_t volatile xxx;
#endif
        if (   (   (iSync2Loops & 0x3ff) == 0
                && ASMReadTSC() - pMySync->uTscStart > pMySync->cMaxTscTicks)
#if 0 /* This is crazy, I know, but enable this code and the results are markedly better when enabled on the 1.4GHz AMD (debug). */
            || (!fIsMaster && (++xxx & 0xf) == 0)
#endif
           )
        {
            /* Try switch our own state into timeout mode so the master cannot tell us to 'GO',
               ignore the timeout if we've got the go ahead already (simpler). */
            if (ASMAtomicCmpXchgU32(&pMySync->uSyncVar, GIP_TSC_DELTA_SYNC2_TIMEOUT, GIP_TSC_DELTA_SYNC2_READY))
            {
                TSCDELTA_DBG_SYNC_MSG(("sync/before/%s: timeout\n", fIsMaster ? "master" : "worker"));
                ASMAtomicCmpXchgU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_TIMEOUT, GIP_TSC_DELTA_SYNC2_STEADY);
                ASMAtomicWriteBool(&pArgs->fTimedOut, true);
                return false;
            }
        }
        iSync2Loops++;
    }

    /*
     * Interrupts are now disabled and will remain disabled until we do
     * TSCDELTA_MASTER_SYNC_AFTER / TSCDELTA_OTHER_SYNC_AFTER.
     */
    *pfEFlags = fEFlags;

    /*
     * The worker tells the master that it is on its mark and that the master
     * need to get into position as well.
     */
    if (!fIsMaster)
    {
        if (RT_LIKELY(ASMAtomicCmpXchgU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_STEADY, GIP_TSC_DELTA_SYNC2_READY)))
        { /* likely */ }
        else
        {
            ASMSetFlags(fEFlags);
            TSCDELTA_DBG_SYNC_MSG(("sync/before/%s: #3 uSyncVar=%#x\n", fIsMaster ? "master" : "worker", pOtherSync->uSyncVar));
            return false;
        }
    }

    /*
     * The master sends the 'go' to the worker and wait for ACK.
     */
    if (fIsMaster)
    {
        if (RT_LIKELY(ASMAtomicCmpXchgU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_GO, GIP_TSC_DELTA_SYNC2_STEADY)))
        { /* likely */ }
        else
        {
            ASMSetFlags(fEFlags);
            TSCDELTA_DBG_SYNC_MSG(("sync/before/%s: #4 uSyncVar=%#x\n", fIsMaster ? "master" : "worker", pOtherSync->uSyncVar));
            return false;
        }
    }

    /*
     * Wait for the 'go' signal (ack in the master case).
     */
    TSCDELTA_DBG_START_LOOP();
    for (;;)
    {
        u32Tmp = ASMAtomicReadU32(&pMySync->uSyncVar);
        if (u32Tmp == GIP_TSC_DELTA_SYNC2_GO)
            break;
        if (RT_LIKELY(u32Tmp == GIP_TSC_DELTA_SYNC2_STEADY))
        { /* likely */ }
        else
        {
            ASMSetFlags(fEFlags);
            TSCDELTA_DBG_SYNC_MSG(("sync/before/%s: #5 u32Tmp=%#x\n", fIsMaster ? "master" : "worker", u32Tmp));
            return false;
        }

        TSCDELTA_DBG_CHECK_LOOP();
        ASMNopPause();
    }

    /*
     * The worker acks the 'go' (shouldn't fail).
     */
    if (!fIsMaster)
    {
        if (RT_LIKELY(ASMAtomicCmpXchgU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_GO, GIP_TSC_DELTA_SYNC2_STEADY)))
        { /* likely */ }
        else
        {
            ASMSetFlags(fEFlags);
            TSCDELTA_DBG_SYNC_MSG(("sync/before/%s: #6 uSyncVar=%#x\n", fIsMaster ? "master" : "worker", pOtherSync->uSyncVar));
            return false;
        }
    }

    /*
     * Try enter mostly lockstep execution with it.
     */
    for (;;)
    {
        uint32_t iOtherSeq1, iOtherSeq2;
        ASMCompilerBarrier();
        ASMSerializeInstruction();

        ASMAtomicWriteU32(&pMySync->uSyncSeq, iMySeq);
        ASMNopPause();
        iOtherSeq1 = ASMAtomicXchgU32(&pOtherSync->uSyncSeq, iMySeq);
        ASMNopPause();
        iOtherSeq2 = ASMAtomicReadU32(&pMySync->uSyncSeq);

        ASMCompilerBarrier();
        if (iOtherSeq1 == iOtherSeq2)
            return true;

        /* Did the other guy give up? Should we give up? */
        if (   iOtherSeq1 == UINT32_MAX
            || iOtherSeq2 == UINT32_MAX)
            return true;
        if (++iMySeq >= iMaxSeq)
        {
            ASMAtomicWriteU32(&pMySync->uSyncSeq, UINT32_MAX);
            return true;
        }
        ASMNopPause();
    }
}

#define TSCDELTA_MASTER_SYNC_BEFORE(a_pMySync, a_pOtherSync, a_pfEFlags, a_pArgs) \
    if (RT_LIKELY(supdrvTscDeltaSync2_Before(a_pMySync, a_pOtherSync, true /*fIsMaster*/, a_pfEFlags, a_pArgs))) \
    { /*likely*/ } \
    else if (true) \
    { \
        TSCDELTA_DBG_SYNC_MSG9(("sync/before/master: #89\n")); \
        break; \
    } else do {} while (0)
#define TSCDELTA_OTHER_SYNC_BEFORE(a_pMySync, a_pOtherSync, a_pfEFlags, a_pArgs) \
    if (RT_LIKELY(supdrvTscDeltaSync2_Before(a_pMySync, a_pOtherSync, false /*fIsMaster*/, a_pfEFlags, a_pArgs))) \
    { /*likely*/ } \
    else if (true) \
    { \
        TSCDELTA_DBG_SYNC_MSG9(("sync/before/other: #89\n")); \
        break; \
    } else do {} while (0)


static bool supdrvTscDeltaSync2_After(PSUPTSCDELTASYNC2 pMySync, PSUPTSCDELTASYNC2 pOtherSync,
                                      bool fIsMaster, RTCCUINTREG fEFlags)
{
    TSCDELTA_DBG_VARS();
    RT_NOREF1(pOtherSync);

    /*
     * Wait for the 'ready' signal.  In the master's case, this means the
     * worker has completed its data collection, while in the worker's case it
     * means the master is done processing the data and it's time for the next
     * loop iteration (or whatever).
     */
    ASMSetFlags(fEFlags);
    TSCDELTA_DBG_START_LOOP();
    for (;;)
    {
        uint32_t u32Tmp = ASMAtomicReadU32(&pMySync->uSyncVar);
        if (   u32Tmp == GIP_TSC_DELTA_SYNC2_READY
            || (u32Tmp == GIP_TSC_DELTA_SYNC2_STEADY && !fIsMaster) /* kicked twice => race */ )
            return true;
        ASMNopPause();
        if (RT_LIKELY(u32Tmp == GIP_TSC_DELTA_SYNC2_GO))
        { /* likely */}
        else
        {
            TSCDELTA_DBG_SYNC_MSG(("sync/after/other: #1 u32Tmp=%#x\n", u32Tmp));
            return false; /* shouldn't ever happen! */
        }
        TSCDELTA_DBG_CHECK_LOOP();
        ASMNopPause();
    }
}

#define TSCDELTA_MASTER_SYNC_AFTER(a_pMySync, a_pOtherSync, a_fEFlags) \
    if (RT_LIKELY(supdrvTscDeltaSync2_After(a_pMySync, a_pOtherSync, true /*fIsMaster*/, a_fEFlags))) \
    { /* likely */ } \
    else if (true) \
    { \
        TSCDELTA_DBG_SYNC_MSG9(("sync/after/master: #97\n")); \
        break; \
    } else do {} while (0)

#define TSCDELTA_MASTER_KICK_OTHER_OUT_OF_AFTER(a_pMySync, a_pOtherSync) \
    /* \
     * Tell the worker that we're done processing the data and ready for the next round. \
     */ \
    if (RT_LIKELY(ASMAtomicCmpXchgU32(&(a_pOtherSync)->uSyncVar, GIP_TSC_DELTA_SYNC2_READY, GIP_TSC_DELTA_SYNC2_GO))) \
    { /* likely */ } \
    else if (true)\
    { \
        TSCDELTA_DBG_SYNC_MSG(("sync/after/master: #99 uSyncVar=%#x\n", (a_pOtherSync)->uSyncVar)); \
        break; \
    } else do {} while (0)

#define TSCDELTA_OTHER_SYNC_AFTER(a_pMySync, a_pOtherSync, a_fEFlags) \
    if (true) { \
        /* \
         * Tell the master that we're done collecting data and wait for the next round to start. \
         */ \
        if (RT_LIKELY(ASMAtomicCmpXchgU32(&(a_pOtherSync)->uSyncVar, GIP_TSC_DELTA_SYNC2_READY, GIP_TSC_DELTA_SYNC2_GO))) \
        { /* likely */ } \
        else \
        { \
            ASMSetFlags(a_fEFlags); \
            TSCDELTA_DBG_SYNC_MSG(("sync/after/other: #0 uSyncVar=%#x\n", (a_pOtherSync)->uSyncVar)); \
            break; \
        } \
        if (RT_LIKELY(supdrvTscDeltaSync2_After(a_pMySync, a_pOtherSync, false /*fIsMaster*/, a_fEFlags))) \
        { /* likely */ } \
        else \
        { \
            TSCDELTA_DBG_SYNC_MSG9(("sync/after/other: #98\n")); \
            break;  \
        } \
    } else do {} while (0)
/** @} */


#ifdef GIP_TSC_DELTA_METHOD_1
/**
 * TSC delta measurement algorithm \#1 (GIP_TSC_DELTA_METHOD_1).
 *
 *
 * We ignore the first few runs of the loop in order to prime the
 * cache. Also, we need to be careful about using 'pause' instruction
 * in critical busy-wait loops in this code - it can cause undesired
 * behaviour with hyperthreading.
 *
 * We try to minimize the measurement error by computing the minimum
 * read time of the compare statement in the worker by taking TSC
 * measurements across it.
 *
 * It must be noted that the computed minimum read time is mostly to
 * eliminate huge deltas when the worker is too early and doesn't by
 * itself help produce more accurate deltas. We allow two times the
 * computed minimum as an arbitrary acceptable threshold. Therefore,
 * it is still possible to get negative deltas where there are none
 * when the worker is earlier. As long as these occasional negative
 * deltas are lower than the time it takes to exit guest-context and
 * the OS to reschedule EMT on a different CPU, we won't expose a TSC
 * that jumped backwards. It is due to the existence of the negative
 * deltas that we don't recompute the delta with the master and
 * worker interchanged to eliminate the remaining measurement error.
 *
 *
 * @param   pArgs               The argument/state data.
 * @param   pMySync             My synchronization structure.
 * @param   pOtherSync          My partner's synchronization structure.
 * @param   fIsMaster           Set if master, clear if worker.
 * @param   iTry                The attempt number.
 */
static void supdrvTscDeltaMethod1Loop(PSUPDRVGIPTSCDELTARGS pArgs, PSUPTSCDELTASYNC2 pMySync, PSUPTSCDELTASYNC2 pOtherSync,
                                      bool fIsMaster, uint32_t iTry)
{
    PSUPGIPCPU  pGipCpuWorker   = pArgs->pWorker;
    PSUPGIPCPU  pGipCpuMaster   = pArgs->pMaster;
    uint64_t    uMinCmpReadTime = UINT64_MAX;
    unsigned    iLoop;
    NOREF(iTry);

    for (iLoop = 0; iLoop < GIP_TSC_DELTA_LOOPS; iLoop++)
    {
        RTCCUINTREG fEFlags;
        if (fIsMaster)
        {
            /*
             * The master.
             */
            AssertMsg(pGipCpuMaster->u64TSCSample == GIP_TSC_DELTA_RSVD,
                      ("%#llx idMaster=%#x idWorker=%#x (idGipMaster=%#x)\n",
                       pGipCpuMaster->u64TSCSample, pGipCpuMaster->idCpu, pGipCpuWorker->idCpu, pArgs->pDevExt->idGipMaster));
            TSCDELTA_MASTER_SYNC_BEFORE(pMySync, pOtherSync, &fEFlags, pArgs);

            do
            {
                ASMSerializeInstruction();
                ASMAtomicWriteU64(&pGipCpuMaster->u64TSCSample, ASMReadTSC());
            } while (pGipCpuMaster->u64TSCSample == GIP_TSC_DELTA_RSVD);

            TSCDELTA_MASTER_SYNC_AFTER(pMySync, pOtherSync, fEFlags);

            /* Process the data. */
            if (iLoop > GIP_TSC_DELTA_PRIMER_LOOPS + GIP_TSC_DELTA_READ_TIME_LOOPS)
            {
                if (pGipCpuWorker->u64TSCSample != GIP_TSC_DELTA_RSVD)
                {
                    int64_t iDelta = pGipCpuWorker->u64TSCSample
                                   - (pGipCpuMaster->u64TSCSample - pGipCpuMaster->i64TSCDelta);
                    if (  iDelta >= GIP_TSC_DELTA_INITIAL_MASTER_VALUE
                        ? iDelta < pGipCpuWorker->i64TSCDelta
                        : iDelta > pGipCpuWorker->i64TSCDelta || pGipCpuWorker->i64TSCDelta == INT64_MAX)
                        pGipCpuWorker->i64TSCDelta = iDelta;
                }
            }

            /* Reset our TSC sample and tell the worker to move on. */
            ASMAtomicWriteU64(&pGipCpuMaster->u64TSCSample, GIP_TSC_DELTA_RSVD);
            TSCDELTA_MASTER_KICK_OTHER_OUT_OF_AFTER(pMySync, pOtherSync);
        }
        else
        {
            /*
             * The worker.
             */
            uint64_t uTscWorker;
            uint64_t uTscWorkerFlushed;
            uint64_t uCmpReadTime;

            ASMAtomicReadU64(&pGipCpuMaster->u64TSCSample);     /* Warm the cache line. */
            TSCDELTA_OTHER_SYNC_BEFORE(pMySync, pOtherSync, &fEFlags, pArgs);

            /*
             * Keep reading the TSC until we notice that the master has read his. Reading
             * the TSC -after- the master has updated the memory is way too late. We thus
             * compensate by trying to measure how long it took for the worker to notice
             * the memory flushed from the master.
             */
            do
            {
                ASMSerializeInstruction();
                uTscWorker = ASMReadTSC();
            } while (pGipCpuMaster->u64TSCSample == GIP_TSC_DELTA_RSVD);
            ASMSerializeInstruction();
            uTscWorkerFlushed = ASMReadTSC();

            uCmpReadTime = uTscWorkerFlushed - uTscWorker;
            if (iLoop > GIP_TSC_DELTA_PRIMER_LOOPS + GIP_TSC_DELTA_READ_TIME_LOOPS)
            {
                /* This is totally arbitrary a.k.a I don't like it but I have no better ideas for now. */
                if (uCmpReadTime < (uMinCmpReadTime << 1))
                {
                    ASMAtomicWriteU64(&pGipCpuWorker->u64TSCSample, uTscWorker);
                    if (uCmpReadTime < uMinCmpReadTime)
                        uMinCmpReadTime = uCmpReadTime;
                }
                else
                    ASMAtomicWriteU64(&pGipCpuWorker->u64TSCSample, GIP_TSC_DELTA_RSVD);
            }
            else if (iLoop > GIP_TSC_DELTA_PRIMER_LOOPS)
            {
                if (uCmpReadTime < uMinCmpReadTime)
                    uMinCmpReadTime = uCmpReadTime;
            }

            TSCDELTA_OTHER_SYNC_AFTER(pMySync, pOtherSync, fEFlags);
        }
    }

    TSCDELTA_DBG_SYNC_MSG9(("sync/method1loop/%s: #92 iLoop=%u MyState=%#x\n", fIsMaster ? "master" : "worker", iLoop,
                           pMySync->uSyncVar));

    /*
     * We must reset the worker TSC sample value in case it gets picked as a
     * GIP master later on (it's trashed above, naturally).
     */
    if (!fIsMaster)
        ASMAtomicWriteU64(&pGipCpuWorker->u64TSCSample, GIP_TSC_DELTA_RSVD);
}
#endif /* GIP_TSC_DELTA_METHOD_1 */


#ifdef GIP_TSC_DELTA_METHOD_2
/*
 * TSC delta measurement algorithm \#2 configuration and code - Experimental!!
 */

# define GIP_TSC_DELTA_M2_LOOPS             (7 + GIP_TSC_DELTA_M2_PRIMER_LOOPS)
# define GIP_TSC_DELTA_M2_PRIMER_LOOPS      0


static void supdrvTscDeltaMethod2ProcessDataOnMaster(PSUPDRVGIPTSCDELTARGS pArgs)
{
    int64_t     iMasterTscDelta  = pArgs->pMaster->i64TSCDelta;
    int64_t     iBestDelta       = pArgs->pWorker->i64TSCDelta;
    uint32_t    idxResult;
    uint32_t    cHits            = 0;

    /*
     * Look for matching entries in the master and worker tables.
     */
    for (idxResult = 0; idxResult < RT_ELEMENTS(pArgs->uMaster.M2.Data.aResults); idxResult++)
    {
        uint32_t idxOther = pArgs->uMaster.M2.Data.aResults[idxResult].iSeqOther;
        if (idxOther & 1)
        {
            idxOther >>= 1;
            if (idxOther < RT_ELEMENTS(pArgs->uWorker.M2.Data.aResults))
            {
                if (pArgs->uWorker.M2.Data.aResults[idxOther].iSeqOther == pArgs->uMaster.M2.Data.aResults[idxResult].iSeqMine)
                {
                    int64_t iDelta;
                    iDelta = pArgs->uWorker.M2.Data.aResults[idxOther].uTsc
                           - (pArgs->uMaster.M2.Data.aResults[idxResult].uTsc - iMasterTscDelta);
                    if (  iDelta >= GIP_TSC_DELTA_INITIAL_MASTER_VALUE
                        ? iDelta < iBestDelta
                        : iDelta > iBestDelta || iBestDelta == INT64_MAX)
                        iBestDelta = iDelta;
                    cHits++;
                }
            }
        }
    }

    /*
     * Save the results.
     */
    if (cHits > 2)
        pArgs->pWorker->i64TSCDelta = iBestDelta;
    pArgs->uMaster.M2.cHits += cHits;
}


/**
 * The core function of the 2nd TSC delta measurement algorithm.
 *
 * The idea here is that we have the two CPUs execute the exact same code
 * collecting a largish set of TSC samples.  The code has one data dependency on
 * the other CPU which intention it is to synchronize the execution as well as
 * help cross references the two sets of TSC samples (the sequence numbers).
 *
 * The @a fLag parameter is used to modify the execution a tiny bit on one or
 * both of the CPUs.  When @a fLag differs between the CPUs, it is thought that
 * it will help with making the CPUs enter lock step execution occasionally.
 *
 */
static void supdrvTscDeltaMethod2CollectData(PSUPDRVTSCDELTAMETHOD2 pMyData, uint32_t volatile *piOtherSeqNo, bool fLag)
{
    SUPDRVTSCDELTAMETHOD2ENTRY *pEntry = &pMyData->aResults[0];
    uint32_t                    cLeft  = RT_ELEMENTS(pMyData->aResults);

    ASMAtomicWriteU32(&pMyData->iCurSeqNo, 0);
    ASMSerializeInstruction();
    while (cLeft-- > 0)
    {
        uint64_t uTsc;
        uint32_t iSeqMine  = ASMAtomicIncU32(&pMyData->iCurSeqNo);
        uint32_t iSeqOther = ASMAtomicReadU32(piOtherSeqNo);
        ASMCompilerBarrier();
        ASMSerializeInstruction(); /* Way better result than with ASMMemoryFenceSSE2() in this position! */
        uTsc = ASMReadTSC();
        ASMAtomicIncU32(&pMyData->iCurSeqNo);
        ASMCompilerBarrier();
        ASMSerializeInstruction();
        pEntry->iSeqMine  = iSeqMine;
        pEntry->iSeqOther = iSeqOther;
        pEntry->uTsc      = uTsc;
        pEntry++;
        ASMSerializeInstruction();
        if (fLag)
            ASMNopPause();
    }
}


/**
 * TSC delta measurement algorithm \#2 (GIP_TSC_DELTA_METHOD_2).
 *
 * See supdrvTscDeltaMethod2CollectData for algorithm details.
 *
 * @param   pArgs               The argument/state data.
 * @param   pMySync             My synchronization structure.
 * @param   pOtherSync          My partner's synchronization structure.
 * @param   fIsMaster           Set if master, clear if worker.
 * @param   iTry                The attempt number.
 */
static void supdrvTscDeltaMethod2Loop(PSUPDRVGIPTSCDELTARGS pArgs, PSUPTSCDELTASYNC2 pMySync, PSUPTSCDELTASYNC2 pOtherSync,
                                      bool fIsMaster, uint32_t iTry)
{
    unsigned iLoop;
    RT_NOREF1(iTry);

    for (iLoop = 0; iLoop < GIP_TSC_DELTA_M2_LOOPS; iLoop++)
    {
        RTCCUINTREG fEFlags;
        if (fIsMaster)
        {
            /*
             * Adjust the loop lag fudge.
             */
# if GIP_TSC_DELTA_M2_PRIMER_LOOPS > 0
            if (iLoop < GIP_TSC_DELTA_M2_PRIMER_LOOPS)
            {
                /* Lag during the priming to be nice to everyone.. */
                pArgs->uMaster.M2.fLag = true;
                pArgs->uWorker.M2.fLag = true;
            }
            else
# endif
            if (iLoop < (GIP_TSC_DELTA_M2_LOOPS - GIP_TSC_DELTA_M2_PRIMER_LOOPS) / 4)
            {
                /* 25 % of the body without lagging. */
                pArgs->uMaster.M2.fLag = false;
                pArgs->uWorker.M2.fLag = false;
            }
            else if (iLoop < (GIP_TSC_DELTA_M2_LOOPS - GIP_TSC_DELTA_M2_PRIMER_LOOPS) / 4 * 2)
            {
                /* 25 % of the body with both lagging. */
                pArgs->uMaster.M2.fLag = true;
                pArgs->uWorker.M2.fLag = true;
            }
            else
            {
                /* 50% of the body with alternating lag. */
                pArgs->uMaster.M2.fLag = (iLoop & 1) == 0;
                pArgs->uWorker.M2.fLag= (iLoop & 1) == 1;
            }

            /*
             * Sync up with the worker and collect data.
             */
            TSCDELTA_MASTER_SYNC_BEFORE(pMySync, pOtherSync, &fEFlags, pArgs);
            supdrvTscDeltaMethod2CollectData(&pArgs->uMaster.M2.Data, &pArgs->uWorker.M2.Data.iCurSeqNo, pArgs->uMaster.M2.fLag);
            TSCDELTA_MASTER_SYNC_AFTER(pMySync, pOtherSync, fEFlags);

            /*
             * Process the data.
             */
# if GIP_TSC_DELTA_M2_PRIMER_LOOPS > 0
            if (iLoop >= GIP_TSC_DELTA_M2_PRIMER_LOOPS)
# endif
                supdrvTscDeltaMethod2ProcessDataOnMaster(pArgs);

            TSCDELTA_MASTER_KICK_OTHER_OUT_OF_AFTER(pMySync, pOtherSync);
        }
        else
        {
            /*
             * The worker.
             */
            TSCDELTA_OTHER_SYNC_BEFORE(pMySync, pOtherSync, &fEFlags, pArgs);
            supdrvTscDeltaMethod2CollectData(&pArgs->uWorker.M2.Data, &pArgs->uMaster.M2.Data.iCurSeqNo, pArgs->uWorker.M2.fLag);
            TSCDELTA_OTHER_SYNC_AFTER(pMySync, pOtherSync, fEFlags);
        }
    }
}

#endif /* GIP_TSC_DELTA_METHOD_2 */



static int supdrvTscDeltaVerify(PSUPDRVGIPTSCDELTARGS pArgs, PSUPTSCDELTASYNC2 pMySync,
                                PSUPTSCDELTASYNC2 pOtherSync, bool fIsMaster, int64_t iWorkerTscDelta)
{
    /*PSUPGIPCPU pGipCpuWorker = pArgs->pWorker; - unused */
    PSUPGIPCPU pGipCpuMaster = pArgs->pMaster;
    uint32_t   i;
    TSCDELTA_DBG_VARS();

    for (;;)
    {
        RTCCUINTREG fEFlags;
        AssertCompile((RT_ELEMENTS(pArgs->uMaster.Verify.auTscs) & 1) == 0);
        AssertCompile(RT_ELEMENTS(pArgs->uMaster.Verify.auTscs) == RT_ELEMENTS(pArgs->uWorker.Verify.auTscs));

        if (fIsMaster)
        {
            uint64_t uTscWorker;
            TSCDELTA_MASTER_SYNC_BEFORE(pMySync, pOtherSync, &fEFlags, pArgs);

            /*
             * Collect TSC, master goes first.
             */
            for (i = 0; i < RT_ELEMENTS(pArgs->uMaster.Verify.auTscs); i += 2)
            {
                /* Read, kick & wait #1. */
                uint64_t uTsc = ASMReadTSC();
                ASMAtomicWriteU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_GO_GO);
                ASMSerializeInstruction();
                pArgs->uMaster.Verify.auTscs[i] = uTsc;
                TSCDELTA_DBG_START_LOOP();
                while (ASMAtomicReadU32(&pMySync->uSyncVar) == GIP_TSC_DELTA_SYNC2_GO)
                {
                    TSCDELTA_DBG_CHECK_LOOP();
                    ASMNopPause();
                }

                /* Read, kick & wait #2. */
                uTsc = ASMReadTSC();
                ASMAtomicWriteU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_GO);
                ASMSerializeInstruction();
                pArgs->uMaster.Verify.auTscs[i + 1] = uTsc;
                TSCDELTA_DBG_START_LOOP();
                while (ASMAtomicReadU32(&pMySync->uSyncVar) == GIP_TSC_DELTA_SYNC2_GO_GO)
                {
                    TSCDELTA_DBG_CHECK_LOOP();
                    ASMNopPause();
                }
            }

            TSCDELTA_MASTER_SYNC_AFTER(pMySync, pOtherSync, fEFlags);

            /*
             * Process the data.
             */
#ifdef TSCDELTA_VERIFY_WITH_STATS
            pArgs->cMaxVerifyTscTicks = INT64_MIN;
            pArgs->cMinVerifyTscTicks = INT64_MAX;
            pArgs->iVerifyBadTscDiff  = 0;
#endif
            ASMAtomicWriteS32(&pArgs->rcVerify, VINF_SUCCESS);
            uTscWorker = 0;
            for (i = 0; i < RT_ELEMENTS(pArgs->uMaster.Verify.auTscs); i++)
            {
                /* Master vs previous worker entry. */
                uint64_t uTscMaster = pArgs->uMaster.Verify.auTscs[i] - pGipCpuMaster->i64TSCDelta;
                int64_t  iDiff;
                if (i > 0)
                {
                    iDiff = uTscMaster - uTscWorker;
#ifdef TSCDELTA_VERIFY_WITH_STATS
                    if (iDiff > pArgs->cMaxVerifyTscTicks)
                        pArgs->cMaxVerifyTscTicks = iDiff;
                    if (iDiff < pArgs->cMinVerifyTscTicks)
                        pArgs->cMinVerifyTscTicks = iDiff;
#endif
                    if (iDiff < 0)
                    {
#ifdef TSCDELTA_VERIFY_WITH_STATS
                        pArgs->iVerifyBadTscDiff = -iDiff;
#endif
                        ASMAtomicWriteS32(&pArgs->rcVerify, VERR_OUT_OF_RANGE);
                        break;
                    }
                }

                /* Worker vs master. */
                uTscWorker = pArgs->uWorker.Verify.auTscs[i] - iWorkerTscDelta;
                iDiff = uTscWorker - uTscMaster;
#ifdef TSCDELTA_VERIFY_WITH_STATS
                if (iDiff > pArgs->cMaxVerifyTscTicks)
                    pArgs->cMaxVerifyTscTicks = iDiff;
                if (iDiff < pArgs->cMinVerifyTscTicks)
                    pArgs->cMinVerifyTscTicks = iDiff;
#endif
                if (iDiff < 0)
                {
#ifdef TSCDELTA_VERIFY_WITH_STATS
                    pArgs->iVerifyBadTscDiff = iDiff;
#endif
                    ASMAtomicWriteS32(&pArgs->rcVerify, VERR_OUT_OF_RANGE);
                    break;
                }
            }

            /* Done. */
            TSCDELTA_MASTER_KICK_OTHER_OUT_OF_AFTER(pMySync, pOtherSync);
        }
        else
        {
            /*
             * The worker, master leads.
             */
            TSCDELTA_OTHER_SYNC_BEFORE(pMySync, pOtherSync, &fEFlags, pArgs);

            for (i = 0; i < RT_ELEMENTS(pArgs->uWorker.Verify.auTscs); i += 2)
            {
                uint64_t uTsc;

                /* Wait, Read and Kick #1. */
                TSCDELTA_DBG_START_LOOP();
                while (ASMAtomicReadU32(&pMySync->uSyncVar) == GIP_TSC_DELTA_SYNC2_GO)
                {
                    TSCDELTA_DBG_CHECK_LOOP();
                    ASMNopPause();
                }
                uTsc = ASMReadTSC();
                ASMAtomicWriteU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_GO_GO);
                ASMSerializeInstruction();
                pArgs->uWorker.Verify.auTscs[i] = uTsc;

                /* Wait, Read and Kick #2. */
                TSCDELTA_DBG_START_LOOP();
                while (ASMAtomicReadU32(&pMySync->uSyncVar) == GIP_TSC_DELTA_SYNC2_GO_GO)
                {
                    TSCDELTA_DBG_CHECK_LOOP();
                    ASMNopPause();
                }
                uTsc = ASMReadTSC();
                ASMAtomicWriteU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_GO);
                ASMSerializeInstruction();
                pArgs->uWorker.Verify.auTscs[i + 1] = uTsc;
            }

            TSCDELTA_OTHER_SYNC_AFTER(pMySync, pOtherSync, fEFlags);
        }
        return pArgs->rcVerify;
    }

    /*
     * Timed out, please retry.
     */
    ASMAtomicWriteS32(&pArgs->rcVerify, VERR_TRY_AGAIN);
    return VERR_TIMEOUT;
}



/**
 * Handles the special abort procedure during synchronization setup in
 * supdrvTscMeasureDeltaCallbackUnwrapped().
 *
 * @returns 0 (dummy, ignored)
 * @param   pArgs               Pointer to argument/state data.
 * @param   pMySync             Pointer to my sync structure.
 * @param   fIsMaster           Set if we're the master, clear if worker.
 * @param   fTimeout            Set if it's a timeout.
 */
DECL_NO_INLINE(static, int)
supdrvTscMeasureDeltaCallbackAbortSyncSetup(PSUPDRVGIPTSCDELTARGS pArgs, PSUPTSCDELTASYNC2 pMySync, bool fIsMaster, bool fTimeout)
{
    PSUPTSCDELTASYNC2 volatile *ppMySync    = fIsMaster ? &pArgs->pSyncMaster : &pArgs->pSyncWorker;
    PSUPTSCDELTASYNC2 volatile *ppOtherSync = fIsMaster ? &pArgs->pSyncWorker : &pArgs->pSyncMaster;
    TSCDELTA_DBG_VARS();
    RT_NOREF1(pMySync);

    /*
     * Clear our sync pointer and make sure the abort flag is set.
     */
    ASMAtomicWriteNullPtr(ppMySync);
    ASMAtomicWriteBool(&pArgs->fAbortSetup, true);
    if (fTimeout)
        ASMAtomicWriteBool(&pArgs->fTimedOut, true);

    /*
     * Make sure the other party is out of there and won't be touching our
     * sync state again (would cause stack corruption).
     */
    TSCDELTA_DBG_START_LOOP();
    while (ASMAtomicReadPtrT(ppOtherSync, PSUPTSCDELTASYNC2) != NULL)
    {
        ASMNopPause();
        ASMNopPause();
        ASMNopPause();
        TSCDELTA_DBG_CHECK_LOOP();
    }

    return 0;
}


/**
 * This is used by supdrvTscMeasureInitialDeltas() to read the TSC on two CPUs
 * and compute the delta between them.
 *
 * To reduce code size a good when timeout handling was added, a dummy return
 * value had to be added (saves 1-3 lines per timeout case), thus this
 * 'Unwrapped' function and the dummy 0 return value.
 *
 * @returns 0 (dummy, ignored)
 * @param   idCpu       The CPU we are current scheduled on.
 * @param   pArgs       Pointer to a parameter package.
 *
 * @remarks Measuring TSC deltas between the CPUs is tricky because we need to
 *          read the TSC at exactly the same time on both the master and the
 *          worker CPUs. Due to DMA, bus arbitration, cache locality,
 *          contention, SMI, pipelining etc. there is no guaranteed way of
 *          doing this on x86 CPUs.
 */
static int supdrvTscMeasureDeltaCallbackUnwrapped(RTCPUID idCpu, PSUPDRVGIPTSCDELTARGS pArgs)
{
    PSUPDRVDEVEXT               pDevExt          = pArgs->pDevExt;
    PSUPGIPCPU                  pGipCpuWorker    = pArgs->pWorker;
    PSUPGIPCPU                  pGipCpuMaster    = pArgs->pMaster;
    bool const                  fIsMaster        = idCpu == pGipCpuMaster->idCpu;
    uint32_t                    iTry;
    PSUPTSCDELTASYNC2 volatile *ppMySync         = fIsMaster ? &pArgs->pSyncMaster : &pArgs->pSyncWorker;
    PSUPTSCDELTASYNC2 volatile *ppOtherSync      = fIsMaster ? &pArgs->pSyncWorker : &pArgs->pSyncMaster;
    SUPTSCDELTASYNC2            MySync;
    PSUPTSCDELTASYNC2           pOtherSync;
    int                         rc;
    TSCDELTA_DBG_VARS();

    /* A bit of paranoia first. */
    if (!pGipCpuMaster || !pGipCpuWorker)
        return 0;

    /*
     * If the CPU isn't part of the measurement, return immediately.
     */
    if (   !fIsMaster
        && idCpu != pGipCpuWorker->idCpu)
        return 0;

    /*
     * Set up my synchronization stuff and wait for the other party to show up.
     *
     * We don't wait forever since the other party may be off fishing (offline,
     * spinning with ints disables, whatever), we must play nice to the rest of
     * the system as this context generally isn't one in which we will get
     * preempted and we may hold up a number of lower priority interrupts.
     */
    ASMAtomicWriteU32(&MySync.uSyncVar, GIP_TSC_DELTA_SYNC2_PRESTART_WAIT);
    ASMAtomicWritePtr(ppMySync, &MySync);
    MySync.uTscStart = ASMReadTSC();
    MySync.cMaxTscTicks = pArgs->cMaxTscTicks;

    /* Look for the partner, might not be here yet... Special abort considerations. */
    iTry = 0;
    TSCDELTA_DBG_START_LOOP();
    while ((pOtherSync = ASMAtomicReadPtrT(ppOtherSync, PSUPTSCDELTASYNC2)) == NULL)
    {
        ASMNopPause();
        if (   ASMAtomicReadBool(&pArgs->fAbortSetup)
            || !RTMpIsCpuOnline(fIsMaster ? pGipCpuWorker->idCpu : pGipCpuMaster->idCpu) )
            return supdrvTscMeasureDeltaCallbackAbortSyncSetup(pArgs, &MySync, fIsMaster, false /*fTimeout*/);
        if (   (iTry++ & 0xff) == 0
            && ASMReadTSC() - MySync.uTscStart > pArgs->cMaxTscTicks)
            return supdrvTscMeasureDeltaCallbackAbortSyncSetup(pArgs, &MySync, fIsMaster, true /*fTimeout*/);
        TSCDELTA_DBG_CHECK_LOOP();
        ASMNopPause();
    }

    /* I found my partner, waiting to be found... Special abort considerations. */
    if (fIsMaster)
        if (!ASMAtomicCmpXchgU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_READY, GIP_TSC_DELTA_SYNC2_PRESTART_WAIT)) /* parnaoia */
            return supdrvTscMeasureDeltaCallbackAbortSyncSetup(pArgs, &MySync, fIsMaster, false /*fTimeout*/);

    iTry = 0;
    TSCDELTA_DBG_START_LOOP();
    while (ASMAtomicReadU32(&MySync.uSyncVar) == GIP_TSC_DELTA_SYNC2_PRESTART_WAIT)
    {
        ASMNopPause();
        if (ASMAtomicReadBool(&pArgs->fAbortSetup))
            return supdrvTscMeasureDeltaCallbackAbortSyncSetup(pArgs, &MySync, fIsMaster, false /*fTimeout*/);
        if (   (iTry++ & 0xff) == 0
            && ASMReadTSC() - MySync.uTscStart > pArgs->cMaxTscTicks)
        {
            if (   fIsMaster
                && !ASMAtomicCmpXchgU32(&MySync.uSyncVar, GIP_TSC_DELTA_SYNC2_PRESTART_ABORT, GIP_TSC_DELTA_SYNC2_PRESTART_WAIT))
                break; /* race #1: slave has moved on, handle timeout in loop instead. */
            return supdrvTscMeasureDeltaCallbackAbortSyncSetup(pArgs, &MySync, fIsMaster, true /*fTimeout*/);
        }
        TSCDELTA_DBG_CHECK_LOOP();
    }

    if (!fIsMaster)
        if (!ASMAtomicCmpXchgU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_READY, GIP_TSC_DELTA_SYNC2_PRESTART_WAIT)) /* race #1 */
            return supdrvTscMeasureDeltaCallbackAbortSyncSetup(pArgs, &MySync, fIsMaster, false /*fTimeout*/);

/** @todo Add a resumable state to pArgs so we don't waste time if we time
 *        out or something.  Timeouts are legit, any of the two CPUs may get
 *        interrupted. */

    /*
     * Start by seeing if we have a zero delta between the two CPUs.
     * This should normally be the case.
     */
    rc = supdrvTscDeltaVerify(pArgs, &MySync, pOtherSync, fIsMaster, GIP_TSC_DELTA_INITIAL_MASTER_VALUE);
    if (RT_SUCCESS(rc))
    {
        if (fIsMaster)
        {
            ASMAtomicWriteS64(&pGipCpuWorker->i64TSCDelta, GIP_TSC_DELTA_INITIAL_MASTER_VALUE);
            RTCpuSetDelByIndex(&pDevExt->TscDeltaCpuSet, pGipCpuWorker->iCpuSet);
            RTCpuSetAddByIndex(&pDevExt->TscDeltaObtainedCpuSet, pGipCpuWorker->iCpuSet);
        }
    }
    /*
     * If the verification didn't time out, do regular delta measurements.
     * We retry this until we get a reasonable value.
     */
    else if (rc != VERR_TIMEOUT)
    {
        Assert(pGipCpuWorker->i64TSCDelta == INT64_MAX);
        for (iTry = 0; iTry < 12; iTry++)
        {
            /*
             * Check the state before we start.
             */
            uint32_t u32Tmp = ASMAtomicReadU32(&MySync.uSyncVar);
            if (   u32Tmp != GIP_TSC_DELTA_SYNC2_READY
                && (fIsMaster || u32Tmp != GIP_TSC_DELTA_SYNC2_STEADY) /* worker may be late prepping for the next round */ )
            {
                TSCDELTA_DBG_SYNC_MSG(("sync/loop/%s: #0 iTry=%u MyState=%#x\n", fIsMaster ? "master" : "worker", iTry, u32Tmp));
                break;
            }

            /*
             * Do the measurements.
             */
#ifdef GIP_TSC_DELTA_METHOD_1
            supdrvTscDeltaMethod1Loop(pArgs, &MySync, pOtherSync, fIsMaster, iTry);
#elif defined(GIP_TSC_DELTA_METHOD_2)
            supdrvTscDeltaMethod2Loop(pArgs, &MySync, pOtherSync, fIsMaster, iTry);
#else
# error "huh??"
#endif

            /*
             * Check the state.
             */
            u32Tmp = ASMAtomicReadU32(&MySync.uSyncVar);
            if (   u32Tmp != GIP_TSC_DELTA_SYNC2_READY
                && (fIsMaster || u32Tmp != GIP_TSC_DELTA_SYNC2_STEADY) /* worker may be late prepping for the next round */ )
            {
                if (fIsMaster)
                    TSCDELTA_DBG_SYNC_MSG(("sync/loop/master: #1 iTry=%u MyState=%#x\n", iTry, u32Tmp));
                else
                    TSCDELTA_DBG_SYNC_MSG2(("sync/loop/worker: #1 iTry=%u MyState=%#x\n", iTry, u32Tmp));
                break;
            }

            /*
             * Success? If so, stop trying. Master decides.
             */
            if (fIsMaster)
            {
                if (pGipCpuWorker->i64TSCDelta != INT64_MAX)
                {
                    RTCpuSetDelByIndex(&pDevExt->TscDeltaCpuSet, pGipCpuWorker->iCpuSet);
                    RTCpuSetAddByIndex(&pDevExt->TscDeltaObtainedCpuSet, pGipCpuWorker->iCpuSet);
                    TSCDELTA_DBG_SYNC_MSG2(("sync/loop/master: #9 iTry=%u MyState=%#x\n", iTry, MySync.uSyncVar));
                    break;
                }
            }
        }
        if (fIsMaster)
            pArgs->iTry = iTry;
    }

    /*
     * End the synchronization dance.  We tell the other that we're done,
     * then wait for the same kind of reply.
     */
    ASMAtomicWriteU32(&pOtherSync->uSyncVar, GIP_TSC_DELTA_SYNC2_FINAL);
    ASMAtomicWriteNullPtr(ppMySync);
    iTry = 0;
    TSCDELTA_DBG_START_LOOP();
    while (ASMAtomicReadU32(&MySync.uSyncVar) != GIP_TSC_DELTA_SYNC2_FINAL)
    {
        iTry++;
        if (   iTry == 0
            && !RTMpIsCpuOnline(fIsMaster ? pGipCpuWorker->idCpu : pGipCpuMaster->idCpu))
            break; /* this really shouldn't happen. */
        TSCDELTA_DBG_CHECK_LOOP();
        ASMNopPause();
    }

    /*
     * Collect some runtime stats.
     */
    if (fIsMaster)
        pArgs->cElapsedMasterTscTicks = ASMReadTSC() - MySync.uTscStart;
    else
        pArgs->cElapsedWorkerTscTicks = ASMReadTSC() - MySync.uTscStart;
    return 0;
}

/**
 * Callback used by supdrvTscMeasureInitialDeltas() to read the TSC on two CPUs
 * and compute the delta between them.
 *
 * @param   idCpu       The CPU we are current scheduled on.
 * @param   pvUser1     Pointer to a parameter package (SUPDRVGIPTSCDELTARGS).
 * @param   pvUser2     Unused.
 */
static DECLCALLBACK(void) supdrvTscMeasureDeltaCallback(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    supdrvTscMeasureDeltaCallbackUnwrapped(idCpu, (PSUPDRVGIPTSCDELTARGS)pvUser1);
    RT_NOREF1(pvUser2);
}


/**
 * Measures the TSC delta between the master GIP CPU and one specified worker
 * CPU.
 *
 * @returns VBox status code.
 * @retval  VERR_SUPDRV_TSC_DELTA_MEASUREMENT_FAILED on pure measurement
 *          failure.
 * @param   pDevExt         Pointer to the device instance data.
 * @param   idxWorker       The index of the worker CPU from the GIP's array of
 *                          CPUs.
 *
 * @remarks This must be called with preemption enabled!
 */
static int supdrvTscMeasureDeltaOne(PSUPDRVDEVEXT pDevExt, uint32_t idxWorker)
{
    int                 rc;
    int                 rc2;
    PSUPGLOBALINFOPAGE  pGip          = pDevExt->pGip;
    RTCPUID             idMaster      = pDevExt->idGipMaster;
    PSUPGIPCPU          pGipCpuWorker = &pGip->aCPUs[idxWorker];
    PSUPGIPCPU          pGipCpuMaster;
    uint32_t            iGipCpuMaster;
    uint32_t            u32Tmp;

    /* Validate input a bit. */
    AssertReturn(pGip, VERR_INVALID_PARAMETER);
    Assert(pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED);
    Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    /*
     * Don't attempt measuring the delta for the GIP master.
     */
    if (pGipCpuWorker->idCpu == idMaster)
    {
        if (pGipCpuWorker->i64TSCDelta == INT64_MAX) /* This shouldn't happen, but just in case. */
            ASMAtomicWriteS64(&pGipCpuWorker->i64TSCDelta, GIP_TSC_DELTA_INITIAL_MASTER_VALUE);
        return VINF_SUCCESS;
    }

    /*
     * One measurement at a time, at least for now.  We might be using
     * broadcast IPIs so, so be nice to the rest of the system.
     */
#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    rc = RTSemMutexRequest(pDevExt->mtxTscDelta, RT_INDEFINITE_WAIT);
#else
    rc = RTSemFastMutexRequest(pDevExt->mtxTscDelta);
#endif
    if (RT_FAILURE(rc))
        return rc;

    /*
     * If the CPU has hyper-threading and the APIC IDs of the master and worker are adjacent,
     * try pick a different master.  (This fudge only works with multi core systems.)
     * ASSUMES related threads have adjacent APIC IDs.  ASSUMES two threads per core.
     *
     * We skip this on AMDs for now as their HTT is different from Intel's and
     * it doesn't seem to have any favorable effect on the results.
     *
     * If the master is offline, we need a new master too, so share the code.
     */
    iGipCpuMaster = supdrvGipFindCpuIndexForCpuId(pGip, idMaster);
    AssertReturn(iGipCpuMaster < pGip->cCpus, VERR_INVALID_CPU_ID);
    pGipCpuMaster = &pGip->aCPUs[iGipCpuMaster];
    if (   (   (pGipCpuMaster->idApic & ~1) == (pGipCpuWorker->idApic & ~1)
            && pGip->cOnlineCpus > 2
            && ASMHasCpuId()
            && RTX86IsValidStdRange(ASMCpuId_EAX(0))
            && (ASMCpuId_EDX(1) & X86_CPUID_FEATURE_EDX_HTT)
            && (   !ASMIsAmdCpu()
                || RTX86GetCpuFamily(u32Tmp = ASMCpuId_EAX(1)) > 0x15
                || (   RTX86GetCpuFamily(u32Tmp)   == 0x15           /* Piledriver+, not bulldozer (FX-4150 didn't like it). */
                    && RTX86GetCpuModelAMD(u32Tmp) >= 0x02) ) )
        || !RTMpIsCpuOnline(idMaster) )
    {
        uint32_t i;
        for (i = 0; i < pGip->cCpus; i++)
            if (   i != iGipCpuMaster
                && i != idxWorker
                && pGip->aCPUs[i].enmState == SUPGIPCPUSTATE_ONLINE
                && pGip->aCPUs[i].i64TSCDelta != INT64_MAX
                && pGip->aCPUs[i].idCpu  != NIL_RTCPUID
                && pGip->aCPUs[i].idCpu  != idMaster              /* paranoia starts here... */
                && pGip->aCPUs[i].idCpu  != pGipCpuWorker->idCpu
                && pGip->aCPUs[i].idApic != pGipCpuWorker->idApic
                && pGip->aCPUs[i].idApic != pGipCpuMaster->idApic
                && RTMpIsCpuOnline(pGip->aCPUs[i].idCpu))
            {
                iGipCpuMaster = i;
                pGipCpuMaster = &pGip->aCPUs[i];
                idMaster = pGipCpuMaster->idCpu;
                break;
            }
    }

    if (RTCpuSetIsMemberByIndex(&pGip->OnlineCpuSet, pGipCpuWorker->iCpuSet))
    {
        /*
         * Initialize data package for the RTMpOnPair callback.
         */
        PSUPDRVGIPTSCDELTARGS pArgs = (PSUPDRVGIPTSCDELTARGS)RTMemAllocZ(sizeof(*pArgs));
        if (pArgs)
        {
            pArgs->pWorker      = pGipCpuWorker;
            pArgs->pMaster      = pGipCpuMaster;
            pArgs->pDevExt      = pDevExt;
            pArgs->pSyncMaster  = NULL;
            pArgs->pSyncWorker  = NULL;
            pArgs->cMaxTscTicks = ASMAtomicReadU64(&pGip->u64CpuHz) / 512; /* 1953 us */

            /*
             * Do the RTMpOnPair call.  We reset i64TSCDelta first so we
             * and supdrvTscMeasureDeltaCallback can use it as a success check.
             */
            /** @todo Store the i64TSCDelta result in pArgs first?   Perhaps deals with
             *        that when doing the restart loop reorg.  */
            ASMAtomicWriteS64(&pGipCpuWorker->i64TSCDelta, INT64_MAX);
            rc = RTMpOnPair(pGipCpuMaster->idCpu, pGipCpuWorker->idCpu, RTMPON_F_CONCURRENT_EXEC,
                            supdrvTscMeasureDeltaCallback, pArgs, NULL);
            if (RT_SUCCESS(rc))
            {
#if 0
                SUPR0Printf("mponpair ticks: %9llu %9llu  max: %9llu  iTry: %u%s\n", pArgs->cElapsedMasterTscTicks,
                            pArgs->cElapsedWorkerTscTicks, pArgs->cMaxTscTicks, pArgs->iTry,
                            pArgs->fTimedOut ? " timed out" :"");
#endif
#if 0
                SUPR0Printf("rcVerify=%d iVerifyBadTscDiff=%lld cMinVerifyTscTicks=%lld cMaxVerifyTscTicks=%lld\n",
                            pArgs->rcVerify, pArgs->iVerifyBadTscDiff, pArgs->cMinVerifyTscTicks, pArgs->cMaxVerifyTscTicks);
#endif
                if (RT_LIKELY(pGipCpuWorker->i64TSCDelta != INT64_MAX))
                {
                    /*
                     * Work the TSC delta applicability rating.  It starts
                     * optimistic in supdrvGipInit, we downgrade it here.
                     */
                    SUPGIPUSETSCDELTA enmRating;
                    if (   pGipCpuWorker->i64TSCDelta >  GIP_TSC_DELTA_THRESHOLD_ROUGHLY_ZERO
                        || pGipCpuWorker->i64TSCDelta < -GIP_TSC_DELTA_THRESHOLD_ROUGHLY_ZERO)
                        enmRating = SUPGIPUSETSCDELTA_NOT_ZERO;
                    else if (   pGipCpuWorker->i64TSCDelta >  GIP_TSC_DELTA_THRESHOLD_PRACTICALLY_ZERO
                             || pGipCpuWorker->i64TSCDelta < -GIP_TSC_DELTA_THRESHOLD_PRACTICALLY_ZERO)
                        enmRating = SUPGIPUSETSCDELTA_ROUGHLY_ZERO;
                    else
                        enmRating = SUPGIPUSETSCDELTA_PRACTICALLY_ZERO;
                    if (pGip->enmUseTscDelta < enmRating)
                    {
                        AssertCompile(sizeof(pGip->enmUseTscDelta) == sizeof(uint32_t));
                        ASMAtomicWriteU32((uint32_t volatile *)&pGip->enmUseTscDelta, enmRating);
                    }
                }
                else
                    rc = VERR_SUPDRV_TSC_DELTA_MEASUREMENT_FAILED;
            }
            /** @todo return try-again if we get an offline CPU error.   */

            RTMemFree(pArgs);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_CPU_OFFLINE;

    /*
     * We're done now.
     */
#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    rc2 = RTSemMutexRelease(pDevExt->mtxTscDelta); AssertRC(rc2);
#else
    rc2 = RTSemFastMutexRelease(pDevExt->mtxTscDelta); AssertRC(rc2);
#endif
    return rc;
}


/**
 * Resets the TSC-delta related TSC samples and optionally the deltas
 * themselves.
 *
 * @param   pDevExt             Pointer to the device instance data.
 * @param   fResetTscDeltas     Whether the TSC-deltas are also to be reset.
 *
 * @remarks This might be called while holding a spinlock!
 */
static void supdrvTscResetSamples(PSUPDRVDEVEXT pDevExt, bool fResetTscDeltas)
{
    unsigned iCpu;
    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;
    for (iCpu = 0; iCpu < pGip->cCpus; iCpu++)
    {
        PSUPGIPCPU pGipCpu = &pGip->aCPUs[iCpu];
        ASMAtomicWriteU64(&pGipCpu->u64TSCSample, GIP_TSC_DELTA_RSVD);
        if (fResetTscDeltas)
        {
            RTCpuSetDelByIndex(&pDevExt->TscDeltaObtainedCpuSet, pGipCpu->iCpuSet);
            ASMAtomicWriteS64(&pGipCpu->i64TSCDelta, INT64_MAX);
        }
    }
}


/**
 * Picks an online CPU as the master TSC for TSC-delta computations.
 *
 * @returns VBox status code.
 * @param   pDevExt         Pointer to the device instance data.
 * @param   pidxMaster      Where to store the CPU array index of the chosen
 *                          master. Optional, can be NULL.
 */
static int supdrvTscPickMaster(PSUPDRVDEVEXT pDevExt, uint32_t *pidxMaster)
{
    /*
     * Pick the first CPU online as the master TSC and make it the new GIP master based
     * on the APIC ID.
     *
     * Technically we can simply use "idGipMaster" but doing this gives us master as CPU 0
     * in most cases making it nicer/easier for comparisons. It is safe to update the GIP
     * master as this point since the sync/async timer isn't created yet.
     */
    unsigned iCpu;
    uint32_t idxMaster = UINT32_MAX;
    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;
    for (iCpu = 0; iCpu < RT_ELEMENTS(pGip->aiCpuFromApicId); iCpu++)
    {
        uint16_t idxCpu = pGip->aiCpuFromApicId[iCpu];
        if (idxCpu != UINT16_MAX)
        {
            PSUPGIPCPU pGipCpu = &pGip->aCPUs[idxCpu];
            if (RTCpuSetIsMemberByIndex(&pGip->OnlineCpuSet, pGipCpu->iCpuSet))
            {
                idxMaster = idxCpu;
                pGipCpu->i64TSCDelta = GIP_TSC_DELTA_INITIAL_MASTER_VALUE;
                ASMAtomicWriteSize(&pDevExt->idGipMaster, pGipCpu->idCpu);
                if (pidxMaster)
                    *pidxMaster = idxMaster;
                return VINF_SUCCESS;
            }
        }
    }
    return VERR_CPU_OFFLINE;
}


/**
 * Performs the initial measurements of the TSC deltas between CPUs.
 *
 * This is called by supdrvGipCreate(), supdrvGipPowerNotificationCallback() or
 * triggered by it if threaded.
 *
 * @returns VBox status code.
 * @param   pDevExt     Pointer to the device instance data.
 *
 * @remarks Must be called only after supdrvGipInitOnCpu() as this function uses
 *          idCpu, GIP's online CPU set which are populated in
 *          supdrvGipInitOnCpu().
 */
static int supdrvTscMeasureInitialDeltas(PSUPDRVDEVEXT pDevExt)
{
    PSUPGIPCPU pGipCpuMaster;
    unsigned   iCpu;
    unsigned   iOddEven;
    PSUPGLOBALINFOPAGE pGip   = pDevExt->pGip;
    uint32_t   idxMaster      = UINT32_MAX;
    uint32_t   cMpOnOffEvents = ASMAtomicReadU32(&pDevExt->cMpOnOffEvents);

    Assert(pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED);
    supdrvTscResetSamples(pDevExt, true /* fClearDeltas */);
    int rc = supdrvTscPickMaster(pDevExt, &idxMaster);
    if (RT_FAILURE(rc))
    {
        SUPR0Printf("Failed to pick a CPU master for TSC-delta measurements rc=%Rrc\n", rc);
        return rc;
    }
    AssertReturn(idxMaster < pGip->cCpus, VERR_INVALID_CPU_INDEX);
    pGipCpuMaster = &pGip->aCPUs[idxMaster];
    Assert(pDevExt->idGipMaster == pGipCpuMaster->idCpu);

    /*
     * If there is only a single CPU online we have nothing to do.
     */
    if (pGip->cOnlineCpus <= 1)
    {
        AssertReturn(pGip->cOnlineCpus > 0, VERR_INTERNAL_ERROR_5);
        return VINF_SUCCESS;
    }

    /*
     * Loop thru the GIP CPU array and get deltas for each CPU (except the
     * master).   We do the CPUs with the even numbered APIC IDs first so that
     * we've got alternative master CPUs to pick from on hyper-threaded systems.
     */
    for (iOddEven = 0; iOddEven < 2; iOddEven++)
    {
        for (iCpu = 0; iCpu < pGip->cCpus; iCpu++)
        {
            PSUPGIPCPU pGipCpuWorker = &pGip->aCPUs[iCpu];
            if (   iCpu != idxMaster
                && (iOddEven > 0 || (pGipCpuWorker->idApic & 1) == 0)
                && RTCpuSetIsMemberByIndex(&pDevExt->TscDeltaCpuSet, pGipCpuWorker->iCpuSet))
            {
                rc = supdrvTscMeasureDeltaOne(pDevExt, iCpu);
                if (RT_FAILURE(rc))
                {
                    SUPR0Printf("supdrvTscMeasureDeltaOne failed. rc=%d CPU[%u].idCpu=%u Master[%u].idCpu=%u\n", rc, iCpu,
                                pGipCpuWorker->idCpu, idxMaster, pDevExt->idGipMaster, pGipCpuMaster->idCpu);
                    break;
                }

                if (ASMAtomicReadU32(&pDevExt->cMpOnOffEvents) != cMpOnOffEvents)
                {
                    SUPR0Printf("One or more CPUs transitioned between online & offline states. I'm confused, retry...\n");
                    rc = VERR_TRY_AGAIN;
                    break;
                }
            }
        }
    }

    return rc;
}


#ifdef SUPDRV_USE_TSC_DELTA_THREAD

/**
 * Switches the TSC-delta measurement thread into the butchered state.
 *
 * @returns VBox status code.
 * @param pDevExt           Pointer to the device instance data.
 * @param fSpinlockHeld     Whether the TSC-delta spinlock is held or not.
 * @param pszFailed         An error message to log.
 * @param rcFailed          The error code to exit the thread with.
 */
static int supdrvTscDeltaThreadButchered(PSUPDRVDEVEXT pDevExt, bool fSpinlockHeld, const char *pszFailed, int rcFailed)
{
    if (!fSpinlockHeld)
        RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);

    pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_Butchered;
    RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
    OSDBGPRINT(("supdrvTscDeltaThreadButchered: %s. rc=%Rrc\n", pszFailed, rcFailed));
    return rcFailed;
}


/**
 * The TSC-delta measurement thread.
 *
 * @returns VBox status code.
 * @param hThread   The thread handle.
 * @param pvUser    Opaque pointer to the device instance data.
 */
static DECLCALLBACK(int) supdrvTscDeltaThread(RTTHREAD hThread, void *pvUser)
{
    PSUPDRVDEVEXT     pDevExt = (PSUPDRVDEVEXT)pvUser;
    int               rc = VERR_INTERNAL_ERROR_2;
    for (;;)
    {
        /*
         * Switch on the current state.
         */
        SUPDRVTSCDELTATHREADSTATE enmState;
        RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
        enmState = pDevExt->enmTscDeltaThreadState;
        switch (enmState)
        {
            case kTscDeltaThreadState_Creating:
            {
                pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_Listening;
                rc = RTSemEventSignal(pDevExt->hTscDeltaEvent);
                if (RT_FAILURE(rc))
                    return supdrvTscDeltaThreadButchered(pDevExt, true /* fSpinlockHeld */, "RTSemEventSignal", rc);
                RT_FALL_THRU();
            }

            case kTscDeltaThreadState_Listening:
            {
                RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);

                /*
                 * Linux counts uninterruptible sleeps as load, hence we shall do a
                 * regular, interruptible sleep here and ignore wake ups due to signals.
                 * See task_contributes_to_load() in include/linux/sched.h in the Linux sources.
                 */
                rc = RTThreadUserWaitNoResume(hThread, pDevExt->cMsTscDeltaTimeout);
                if (   RT_FAILURE(rc)
                    && rc != VERR_TIMEOUT
                    && rc != VERR_INTERRUPTED)
                    return supdrvTscDeltaThreadButchered(pDevExt, false /* fSpinlockHeld */, "RTThreadUserWait", rc);
                RTThreadUserReset(hThread);
                break;
            }

            case kTscDeltaThreadState_WaitAndMeasure:
            {
                pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_Measuring;
                rc = RTSemEventSignal(pDevExt->hTscDeltaEvent); /* (Safe on windows as long as spinlock isn't IRQ safe.) */
                if (RT_FAILURE(rc))
                    return supdrvTscDeltaThreadButchered(pDevExt, true /* fSpinlockHeld */, "RTSemEventSignal", rc);
                RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
                RTThreadSleep(1);
                RT_FALL_THRU();
            }

            case kTscDeltaThreadState_Measuring:
            {
                if (pDevExt->fTscThreadRecomputeAllDeltas)
                {
                    int cTries = 8;
                    int cMsWaitPerTry = 10;
                    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;
                    Assert(pGip);
                    do
                    {
                        RTCpuSetCopy(&pDevExt->TscDeltaCpuSet, &pGip->OnlineCpuSet);
                        rc = supdrvTscMeasureInitialDeltas(pDevExt);
                        if (   RT_SUCCESS(rc)
                            || (   RT_FAILURE(rc)
                                && rc != VERR_TRY_AGAIN
                                && rc != VERR_CPU_OFFLINE))
                        {
                            break;
                        }
                        RTThreadSleep(cMsWaitPerTry);
                    } while (cTries-- > 0);
                    pDevExt->fTscThreadRecomputeAllDeltas = false;
                }
                else
                {
                    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;
                    unsigned iCpu;

                    /* Measure TSC-deltas only for the CPUs that are in the set. */
                    rc = VINF_SUCCESS;
                    for (iCpu = 0; iCpu < pGip->cCpus; iCpu++)
                    {
                        PSUPGIPCPU pGipCpuWorker = &pGip->aCPUs[iCpu];
                        if (RTCpuSetIsMemberByIndex(&pDevExt->TscDeltaCpuSet, pGipCpuWorker->iCpuSet))
                        {
                            if (pGipCpuWorker->i64TSCDelta == INT64_MAX)
                            {
                                int rc2 = supdrvTscMeasureDeltaOne(pDevExt, iCpu);
                                if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                                    rc = rc2;
                            }
                            else
                            {
                                /*
                                 * The thread/someone must've called SUPR0TscDeltaMeasureBySetIndex(),
                                 * mark the delta as fine to get the timer thread off our back.
                                 */
                                RTCpuSetDelByIndex(&pDevExt->TscDeltaCpuSet, pGipCpuWorker->iCpuSet);
                                RTCpuSetAddByIndex(&pDevExt->TscDeltaObtainedCpuSet, pGipCpuWorker->iCpuSet);
                            }
                        }
                    }
                }
                RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
                if (pDevExt->enmTscDeltaThreadState == kTscDeltaThreadState_Measuring)
                    pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_Listening;
                RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
                Assert(rc != VERR_NOT_AVAILABLE);  /* VERR_NOT_AVAILABLE is used as init value, see supdrvTscDeltaThreadInit(). */
                ASMAtomicWriteS32(&pDevExt->rcTscDelta, rc);
                break;
            }

            case kTscDeltaThreadState_Terminating:
                pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_Destroyed;
                RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
                return VINF_SUCCESS;

            case kTscDeltaThreadState_Butchered:
            default:
                return supdrvTscDeltaThreadButchered(pDevExt, true /* fSpinlockHeld */, "Invalid state", VERR_INVALID_STATE);
        }
    }
    /* not reached */
}


/**
 * Waits for the TSC-delta measurement thread to respond to a state change.
 *
 * @returns VINF_SUCCESS on success, VERR_TIMEOUT if it doesn't respond in time,
 *          other error code on internal error.
 *
 * @param   pDevExt         The device instance data.
 * @param   enmCurState     The current state.
 * @param   enmNewState     The new state we're waiting for it to enter.
 */
static int supdrvTscDeltaThreadWait(PSUPDRVDEVEXT pDevExt, SUPDRVTSCDELTATHREADSTATE enmCurState,
                                    SUPDRVTSCDELTATHREADSTATE enmNewState)
{
    SUPDRVTSCDELTATHREADSTATE enmActualState;
    int rc;

    /*
     * Wait a short while for the expected state transition.
     */
    RTSemEventWait(pDevExt->hTscDeltaEvent, RT_MS_1SEC);
    RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
    enmActualState = pDevExt->enmTscDeltaThreadState;
    if (enmActualState == enmNewState)
    {
        RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
        rc = VINF_SUCCESS;
    }
    else if (enmActualState == enmCurState)
    {
        /*
         * Wait longer if the state has not yet transitioned to the one we want.
         */
        RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
        rc = RTSemEventWait(pDevExt->hTscDeltaEvent, 50 * RT_MS_1SEC);
        if (   RT_SUCCESS(rc)
            || rc == VERR_TIMEOUT)
        {
            /*
             * Check the state whether we've succeeded.
             */
            RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
            enmActualState = pDevExt->enmTscDeltaThreadState;
            RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
            if (enmActualState == enmNewState)
                rc = VINF_SUCCESS;
            else if (enmActualState == enmCurState)
            {
                rc = VERR_TIMEOUT;
                OSDBGPRINT(("supdrvTscDeltaThreadWait: timed out state transition. enmActualState=%d enmNewState=%d\n",
                            enmActualState, enmNewState));
            }
            else
            {
                rc = VERR_INTERNAL_ERROR;
                OSDBGPRINT(("supdrvTscDeltaThreadWait: invalid state transition from %d to %d, expected %d\n", enmCurState,
                            enmActualState, enmNewState));
            }
        }
        else
            OSDBGPRINT(("supdrvTscDeltaThreadWait: RTSemEventWait failed. rc=%Rrc\n", rc));
    }
    else
    {
        RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
        OSDBGPRINT(("supdrvTscDeltaThreadWait: invalid state %d when transitioning from %d to %d\n",
                    enmActualState, enmCurState, enmNewState));
        rc = VERR_INTERNAL_ERROR;
    }

    return rc;
}


/**
 * Signals the TSC-delta thread to start measuring TSC-deltas.
 *
 * @param   pDevExt     Pointer to the device instance data.
 * @param   fForceAll   Force re-calculating TSC-deltas on all CPUs.
 */
static void supdrvTscDeltaThreadStartMeasurement(PSUPDRVDEVEXT pDevExt, bool fForceAll)
{
    if (pDevExt->hTscDeltaThread != NIL_RTTHREAD)
    {
        RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
        if (   pDevExt->enmTscDeltaThreadState == kTscDeltaThreadState_Listening
            || pDevExt->enmTscDeltaThreadState == kTscDeltaThreadState_Measuring)
        {
            pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_WaitAndMeasure;
            if (fForceAll)
                pDevExt->fTscThreadRecomputeAllDeltas = true;
        }
        else if (   pDevExt->enmTscDeltaThreadState == kTscDeltaThreadState_WaitAndMeasure
                 && fForceAll)
            pDevExt->fTscThreadRecomputeAllDeltas = true;
        RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
        RTThreadUserSignal(pDevExt->hTscDeltaThread);
    }
}


/**
 * Terminates the actual thread running supdrvTscDeltaThread().
 *
 * This is an internal worker function for supdrvTscDeltaThreadInit() and
 * supdrvTscDeltaTerm().
 *
 * @param   pDevExt   Pointer to the device instance data.
 */
static void supdrvTscDeltaThreadTerminate(PSUPDRVDEVEXT pDevExt)
{
    int rc;
    RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
    pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_Terminating;
    RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
    RTThreadUserSignal(pDevExt->hTscDeltaThread);
    rc = RTThreadWait(pDevExt->hTscDeltaThread, 50 * RT_MS_1SEC, NULL /* prc */);
    if (RT_FAILURE(rc))
    {
        /* Signal a few more times before giving up. */
        int cTriesLeft = 5;
        while (--cTriesLeft > 0)
        {
            RTThreadUserSignal(pDevExt->hTscDeltaThread);
            rc = RTThreadWait(pDevExt->hTscDeltaThread, 2 * RT_MS_1SEC, NULL /* prc */);
            if (rc != VERR_TIMEOUT)
                break;
        }
    }
}


/**
 * Initializes and spawns the TSC-delta measurement thread.
 *
 * A thread is required for servicing re-measurement requests from events like
 * CPUs coming online, suspend/resume etc. as it cannot be done synchronously
 * under all contexts on all OSs.
 *
 * @returns VBox status code.
 * @param   pDevExt           Pointer to the device instance data.
 *
 * @remarks Must only be called -after- initializing GIP and setting up MP
 *          notifications!
 */
static int supdrvTscDeltaThreadInit(PSUPDRVDEVEXT pDevExt)
{
    int rc;
    Assert(pDevExt->pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED);
    rc = RTSpinlockCreate(&pDevExt->hTscDeltaSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_UNSAFE, "VBoxTscSpnLck");
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventCreate(&pDevExt->hTscDeltaEvent);
        if (RT_SUCCESS(rc))
        {
            pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_Creating;
            pDevExt->cMsTscDeltaTimeout = 60000;
            rc = RTThreadCreate(&pDevExt->hTscDeltaThread, supdrvTscDeltaThread, pDevExt, 0 /* cbStack */,
                                RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "VBoxTscThread");
            if (RT_SUCCESS(rc))
            {
                rc = supdrvTscDeltaThreadWait(pDevExt, kTscDeltaThreadState_Creating, kTscDeltaThreadState_Listening);
                if (RT_SUCCESS(rc))
                {
                    ASMAtomicWriteS32(&pDevExt->rcTscDelta, VERR_NOT_AVAILABLE);
                    return rc;
                }

                OSDBGPRINT(("supdrvTscDeltaInit: supdrvTscDeltaThreadWait failed. rc=%Rrc\n", rc));
                supdrvTscDeltaThreadTerminate(pDevExt);
            }
            else
                OSDBGPRINT(("supdrvTscDeltaInit: RTThreadCreate failed. rc=%Rrc\n", rc));
            RTSemEventDestroy(pDevExt->hTscDeltaEvent);
            pDevExt->hTscDeltaEvent = NIL_RTSEMEVENT;
        }
        else
            OSDBGPRINT(("supdrvTscDeltaInit: RTSemEventCreate failed. rc=%Rrc\n", rc));
        RTSpinlockDestroy(pDevExt->hTscDeltaSpinlock);
        pDevExt->hTscDeltaSpinlock = NIL_RTSPINLOCK;
    }
    else
        OSDBGPRINT(("supdrvTscDeltaInit: RTSpinlockCreate failed. rc=%Rrc\n", rc));

    return rc;
}


/**
 * Terminates the TSC-delta measurement thread and cleanup.
 *
 * @param   pDevExt         Pointer to the device instance data.
 */
static void supdrvTscDeltaTerm(PSUPDRVDEVEXT pDevExt)
{
    if (   pDevExt->hTscDeltaSpinlock != NIL_RTSPINLOCK
        && pDevExt->hTscDeltaEvent != NIL_RTSEMEVENT)
    {
        supdrvTscDeltaThreadTerminate(pDevExt);
    }

    if (pDevExt->hTscDeltaSpinlock != NIL_RTSPINLOCK)
    {
        RTSpinlockDestroy(pDevExt->hTscDeltaSpinlock);
        pDevExt->hTscDeltaSpinlock = NIL_RTSPINLOCK;
    }

    if (pDevExt->hTscDeltaEvent != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pDevExt->hTscDeltaEvent);
        pDevExt->hTscDeltaEvent = NIL_RTSEMEVENT;
    }

    ASMAtomicWriteS32(&pDevExt->rcTscDelta, VERR_NOT_AVAILABLE);
}

#endif /* SUPDRV_USE_TSC_DELTA_THREAD */

/**
 * Measure the TSC delta for the CPU given by its CPU set index.
 *
 * @returns VBox status code.
 * @retval  VERR_INTERRUPTED if interrupted while waiting.
 * @retval  VERR_SUPDRV_TSC_DELTA_MEASUREMENT_FAILED if we were unable to get a
 *          measurement.
 * @retval  VERR_CPU_OFFLINE if the specified CPU is offline.
 *
 * @param   pSession        The caller's session.  GIP must've been mapped.
 * @param   iCpuSet         The CPU set index of the CPU to measure.
 * @param   fFlags          Flags, SUP_TSCDELTA_MEASURE_F_XXX.
 * @param   cMsWaitRetry    Number of milliseconds to wait between each retry.
 * @param   cMsWaitThread   Number of milliseconds to wait for the thread to get
 *                          ready.
 * @param   cTries          Number of times to try, pass 0 for the default.
 */
SUPR0DECL(int) SUPR0TscDeltaMeasureBySetIndex(PSUPDRVSESSION pSession, uint32_t iCpuSet, uint32_t fFlags,
                                              RTMSINTERVAL cMsWaitRetry, RTMSINTERVAL cMsWaitThread, uint32_t cTries)
{
    PSUPDRVDEVEXT       pDevExt;
    PSUPGLOBALINFOPAGE  pGip;
    uint16_t            iGipCpu;
    int                 rc;
#ifdef SUPDRV_USE_TSC_DELTA_THREAD
    uint64_t            msTsStartWait;
    uint32_t            iWaitLoop;
#endif

    /*
     * Validate and adjust the input.
     */
    AssertReturn(SUP_IS_SESSION_VALID(pSession), VERR_INVALID_PARAMETER);
    if (!pSession->fGipReferenced)
        return VERR_WRONG_ORDER;

    pDevExt = pSession->pDevExt;
    AssertReturn(SUP_IS_DEVEXT_VALID(pDevExt), VERR_INVALID_PARAMETER);

    pGip = pDevExt->pGip;
    AssertPtrReturn(pGip, VERR_INTERNAL_ERROR_2);

    AssertReturn(iCpuSet < RTCPUSET_MAX_CPUS, VERR_INVALID_CPU_INDEX);
    AssertReturn(iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx), VERR_INVALID_CPU_INDEX);
    iGipCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet];
    AssertReturn(iGipCpu < pGip->cCpus, VERR_INVALID_CPU_INDEX);

    if (fFlags & ~SUP_TSCDELTA_MEASURE_F_VALID_MASK)
        return VERR_INVALID_FLAGS;

    /*
     * The request is a noop if the TSC delta isn't being used.
     */
    if (pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ZERO_CLAIMED)
        return VINF_SUCCESS;

    if (cTries == 0)
        cTries = 12;
    else if (cTries > 256)
        cTries = 256;

    if (cMsWaitRetry == 0)
        cMsWaitRetry = 2;
    else if (cMsWaitRetry > 1000)
        cMsWaitRetry = 1000;

#ifdef SUPDRV_USE_TSC_DELTA_THREAD
    /*
     * Has the TSC already been measured and we're not forced to redo it?
     */
    if (   pGip->aCPUs[iGipCpu].i64TSCDelta != INT64_MAX
        && !(fFlags & SUP_TSCDELTA_MEASURE_F_FORCE))
        return VINF_SUCCESS;

    /*
     * Asynchronous request? Forward it to the thread, no waiting.
     */
    if (fFlags & SUP_TSCDELTA_MEASURE_F_ASYNC)
    {
        /** @todo Async. doesn't implement options like retries, waiting. We'll need
         *        to pass those options to the thread somehow and implement it in the
         *        thread. Check if anyone uses/needs fAsync before implementing this. */
        RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
        RTCpuSetAddByIndex(&pDevExt->TscDeltaCpuSet, iCpuSet);
        if (   pDevExt->enmTscDeltaThreadState == kTscDeltaThreadState_Listening
            || pDevExt->enmTscDeltaThreadState == kTscDeltaThreadState_Measuring)
        {
            pDevExt->enmTscDeltaThreadState = kTscDeltaThreadState_WaitAndMeasure;
            rc = VINF_SUCCESS;
        }
        else if (pDevExt->enmTscDeltaThreadState != kTscDeltaThreadState_WaitAndMeasure)
            rc = VERR_THREAD_IS_DEAD;
        RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);
        RTThreadUserSignal(pDevExt->hTscDeltaThread);
        return VINF_SUCCESS;
    }

    /*
     * If a TSC-delta measurement request is already being serviced by the thread,
     * wait 'cTries' times if a retry-timeout is provided, otherwise bail as busy.
     */
    msTsStartWait = RTTimeSystemMilliTS();
    for (iWaitLoop = 0;; iWaitLoop++)
    {
        uint64_t cMsElapsed;
        SUPDRVTSCDELTATHREADSTATE enmState;
        RTSpinlockAcquire(pDevExt->hTscDeltaSpinlock);
        enmState = pDevExt->enmTscDeltaThreadState;
        RTSpinlockRelease(pDevExt->hTscDeltaSpinlock);

        if (enmState == kTscDeltaThreadState_Measuring)
        { /* Must wait, the thread is busy. */ }
        else if (enmState == kTscDeltaThreadState_WaitAndMeasure)
        { /* Must wait, this state only says what will happen next. */ }
        else if (enmState == kTscDeltaThreadState_Terminating)
        { /* Must wait, this state only says what should happen next. */ }
        else
            break; /* All other states, the thread is either idly listening or dead. */

        /* Wait or fail. */
        if (cMsWaitThread == 0)
            return VERR_SUPDRV_TSC_DELTA_MEASUREMENT_BUSY;
        cMsElapsed = RTTimeSystemMilliTS() - msTsStartWait;
        if (cMsElapsed >= cMsWaitThread)
            return VERR_SUPDRV_TSC_DELTA_MEASUREMENT_BUSY;

        rc = RTThreadSleep(RT_MIN((RTMSINTERVAL)(cMsWaitThread - cMsElapsed), RT_MIN(iWaitLoop + 1, 10)));
        if (rc == VERR_INTERRUPTED)
            return rc;
    }
#endif /* SUPDRV_USE_TSC_DELTA_THREAD */

    /*
     * Try measure the TSC delta the given number of times.
     */
    for (;;)
    {
        /* Unless we're forced to measure the delta, check whether it's done already. */
        if (   !(fFlags & SUP_TSCDELTA_MEASURE_F_FORCE)
            && pGip->aCPUs[iGipCpu].i64TSCDelta != INT64_MAX)
        {
            rc = VINF_SUCCESS;
            break;
        }

        /* Measure it. */
        rc = supdrvTscMeasureDeltaOne(pDevExt, iGipCpu);
        if (rc != VERR_SUPDRV_TSC_DELTA_MEASUREMENT_FAILED)
        {
            Assert(pGip->aCPUs[iGipCpu].i64TSCDelta != INT64_MAX || RT_FAILURE_NP(rc));
            break;
        }

        /* Retry? */
        if (cTries <= 1)
            break;
        cTries--;

        /* Always delay between retries (be nice to the rest of the system
           and avoid the BSOD hounds). */
        rc = RTThreadSleep(cMsWaitRetry);
        if (rc == VERR_INTERRUPTED)
            break;
    }

    return rc;
}
SUPR0_EXPORT_SYMBOL(SUPR0TscDeltaMeasureBySetIndex);


/**
 * Service a TSC-delta measurement request.
 *
 * @returns VBox status code.
 * @param   pDevExt         Pointer to the device instance data.
 * @param   pSession        The support driver session.
 * @param   pReq            Pointer to the TSC-delta measurement request.
 */
int VBOXCALL supdrvIOCtl_TscDeltaMeasure(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPTSCDELTAMEASURE pReq)
{
    uint32_t        cTries;
    uint32_t        iCpuSet;
    uint32_t        fFlags;
    RTMSINTERVAL    cMsWaitRetry;
    RT_NOREF1(pDevExt);

    /*
     * Validate and adjust/resolve the input so they can be passed onto SUPR0TscDeltaMeasureBySetIndex.
     */
    AssertPtr(pDevExt); AssertPtr(pSession); AssertPtr(pReq); /* paranoia^2 */

    if (pReq->u.In.idCpu == NIL_RTCPUID)
        return VERR_INVALID_CPU_ID;
    iCpuSet = RTMpCpuIdToSetIndex(pReq->u.In.idCpu);
    if (iCpuSet >= RTCPUSET_MAX_CPUS)
        return VERR_INVALID_CPU_ID;

    cTries = pReq->u.In.cRetries == 0 ? 0 : (uint32_t)pReq->u.In.cRetries + 1;

    cMsWaitRetry = RT_MAX(pReq->u.In.cMsWaitRetry, 5);

    fFlags = 0;
    if (pReq->u.In.fAsync)
        fFlags |= SUP_TSCDELTA_MEASURE_F_ASYNC;
    if (pReq->u.In.fForce)
        fFlags |= SUP_TSCDELTA_MEASURE_F_FORCE;

    return SUPR0TscDeltaMeasureBySetIndex(pSession, iCpuSet, fFlags, cMsWaitRetry,
                                          cTries == 0 ? 5 * RT_MS_1SEC : cMsWaitRetry * cTries /*cMsWaitThread*/,
                                          cTries);
}


/**
 * Reads TSC with delta applied.
 *
 * Will try to resolve delta value INT64_MAX before applying it.  This is the
 * main purpose of this function, to handle the case where the delta needs to be
 * determined.
 *
 * @returns VBox status code.
 * @param   pDevExt         Pointer to the device instance data.
 * @param   pSession        The support driver session.
 * @param   pReq            Pointer to the TSC-read request.
 */
int VBOXCALL supdrvIOCtl_TscRead(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPTSCREAD pReq)
{
    PSUPGLOBALINFOPAGE pGip;
    int rc;

    /*
     * Validate.  We require the client to have mapped GIP (no asserting on
     * ring-3 preconditions).
     */
    AssertPtr(pDevExt); AssertPtr(pReq); AssertPtr(pSession); /* paranoia^2 */
    if (pSession->GipMapObjR3 == NIL_RTR0MEMOBJ)
        return VERR_WRONG_ORDER;
    pGip = pDevExt->pGip;
    AssertReturn(pGip, VERR_INTERNAL_ERROR_2);

    /*
     * We're usually here because we need to apply delta, but we shouldn't be
     * upset if the GIP is some different mode.
     */
    if (pGip->enmUseTscDelta > SUPGIPUSETSCDELTA_ZERO_CLAIMED)
    {
        uint32_t cTries = 0;
        for (;;)
        {
            /*
             * Start by gathering the data, using CLI for disabling preemption
             * while we do that.
             */
            RTCCUINTREG fEFlags = ASMIntDisableFlags();
            int         iCpuSet = RTMpCpuIdToSetIndex(RTMpCpuId());
            int         iGipCpu = 0; /* gcc maybe used uninitialized */
            if (RT_LIKELY(   (unsigned)iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                          && (iGipCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet]) < pGip->cCpus ))
            {
                int64_t i64Delta   = pGip->aCPUs[iGipCpu].i64TSCDelta;
                pReq->u.Out.idApic = pGip->aCPUs[iGipCpu].idApic;
                pReq->u.Out.u64AdjustedTsc = ASMReadTSC();
                ASMSetFlags(fEFlags);

                /*
                 * If we're lucky we've got a delta, but no predictions here
                 * as this I/O control is normally only used when the TSC delta
                 * is set to INT64_MAX.
                 */
                if (i64Delta != INT64_MAX)
                {
                    pReq->u.Out.u64AdjustedTsc -= i64Delta;
                    rc = VINF_SUCCESS;
                    break;
                }

                /* Give up after a few times. */
                if (cTries >= 4)
                {
                    rc = VWRN_SUPDRV_TSC_DELTA_MEASUREMENT_FAILED;
                    break;
                }

                /* Need to measure the delta an try again. */
                rc = supdrvTscMeasureDeltaOne(pDevExt, iGipCpu);
                Assert(pGip->aCPUs[iGipCpu].i64TSCDelta != INT64_MAX || RT_FAILURE_NP(rc));
                /** @todo should probably delay on failure... dpc watchdogs */
            }
            else
            {
                /* This really shouldn't happen. */
                AssertMsgFailed(("idCpu=%#x iCpuSet=%#x (%d)\n", RTMpCpuId(), iCpuSet, iCpuSet));
                pReq->u.Out.idApic = supdrvGipGetApicIdSlow();
                pReq->u.Out.u64AdjustedTsc = ASMReadTSC();
                ASMSetFlags(fEFlags);
                rc = VERR_INTERNAL_ERROR_5; /** @todo change to warning. */
                break;
            }
        }
    }
    else
    {
        /*
         * No delta to apply. Easy. Deal with preemption the lazy way.
         */
        RTCCUINTREG fEFlags = ASMIntDisableFlags();
        int         iCpuSet = RTMpCpuIdToSetIndex(RTMpCpuId());
        int         iGipCpu = 0; /* gcc may be used uninitialized */
        if (RT_LIKELY(   (unsigned)iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                      && (iGipCpu = pGip->aiCpuFromCpuSetIdx[iCpuSet]) < pGip->cCpus ))
            pReq->u.Out.idApic = pGip->aCPUs[iGipCpu].idApic;
        else
            pReq->u.Out.idApic = supdrvGipGetApicIdSlow();
        pReq->u.Out.u64AdjustedTsc = ASMReadTSC();
        ASMSetFlags(fEFlags);
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Worker for supdrvIOCtl_GipSetFlags.
 *
 * @returns VBox status code.
 * @retval  VERR_WRONG_ORDER if an enable-once-per-session flag is set again for
 *          a session.
 *
 * @param   pDevExt         Pointer to the device instance data.
 * @param   pSession        The support driver session.
 * @param   fOrMask         The OR mask of the GIP flags, see SUPGIP_FLAGS_XXX.
 * @param   fAndMask        The AND mask of the GIP flags, see SUPGIP_FLAGS_XXX.
 *
 * @remarks Caller must own the GIP mutex.
 *
 * @remarks This function doesn't validate any of the flags.
 */
static int supdrvGipSetFlags(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uint32_t fOrMask, uint32_t fAndMask)
{
    uint32_t           cRefs;
    PSUPGLOBALINFOPAGE pGip = pDevExt->pGip;
    AssertMsg((fOrMask & fAndMask) == fOrMask, ("%#x & %#x\n", fOrMask, fAndMask)); /* ASSUMED by code below */

    /*
     * Compute GIP test-mode flags.
     */
    if (fOrMask & SUPGIP_FLAGS_TESTING_ENABLE)
    {
        if (!pSession->fGipTestMode)
        {
            Assert(pDevExt->cGipTestModeRefs < _64K);
            pSession->fGipTestMode = true;
            cRefs = ++pDevExt->cGipTestModeRefs;
            if (cRefs == 1)
            {
                fOrMask  |= SUPGIP_FLAGS_TESTING | SUPGIP_FLAGS_TESTING_START;
                fAndMask &= ~SUPGIP_FLAGS_TESTING_STOP;
            }
        }
        else
        {
            LogRelMax(10, ("supdrvGipSetFlags: SUPGIP_FLAGS_TESTING_ENABLE already set for this session\n"));
            return VERR_WRONG_ORDER;
        }
    }
    else if (   !(fAndMask & SUPGIP_FLAGS_TESTING_ENABLE)
             && pSession->fGipTestMode)
    {
        Assert(pDevExt->cGipTestModeRefs > 0);
        Assert(pDevExt->cGipTestModeRefs < _64K);
        pSession->fGipTestMode = false;
        cRefs = --pDevExt->cGipTestModeRefs;
        if (!cRefs)
            fOrMask |= SUPGIP_FLAGS_TESTING_STOP;
        else
            fAndMask |= SUPGIP_FLAGS_TESTING_ENABLE;
    }

    /*
     * Commit the flags.  This should be done as atomically as possible
     * since the flag consumers won't be holding the GIP mutex.
     */
    ASMAtomicOrU32(&pGip->fFlags, fOrMask);
    ASMAtomicAndU32(&pGip->fFlags, fAndMask);

    return VINF_SUCCESS;
}


/**
 * Sets GIP test mode parameters.
 *
 * @returns VBox status code.
 * @param   pDevExt         Pointer to the device instance data.
 * @param   pSession        The support driver session.
 * @param   fOrMask         The OR mask of the GIP flags, see SUPGIP_FLAGS_XXX.
 * @param   fAndMask        The AND mask of the GIP flags, see SUPGIP_FLAGS_XXX.
 */
int VBOXCALL supdrvIOCtl_GipSetFlags(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uint32_t fOrMask, uint32_t fAndMask)
{
    PSUPGLOBALINFOPAGE pGip;
    int                rc;

    /*
     * Validate.  We require the client to have mapped GIP (no asserting on
     * ring-3 preconditions).
     */
    AssertPtr(pDevExt); AssertPtr(pSession); /* paranoia^2 */
    if (pSession->GipMapObjR3 == NIL_RTR0MEMOBJ)
        return VERR_WRONG_ORDER;
    pGip = pDevExt->pGip;
    AssertReturn(pGip, VERR_INTERNAL_ERROR_3);

    if (fOrMask & ~SUPGIP_FLAGS_VALID_MASK)
        return VERR_INVALID_PARAMETER;
    if ((fAndMask & ~SUPGIP_FLAGS_VALID_MASK) != ~SUPGIP_FLAGS_VALID_MASK)
        return VERR_INVALID_PARAMETER;

    /*
     * Don't confuse supdrvGipSetFlags or anyone else by both setting
     * and clearing the same flags.  AND takes precedence.
     */
    fOrMask &= fAndMask;

    /*
     * Take the loader lock to avoid having to think about races between two
     * clients changing the flags at the same time (state is not simple).
     */
#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSemMutexRequest(pDevExt->mtxGip, RT_INDEFINITE_WAIT);
#else
    RTSemFastMutexRequest(pDevExt->mtxGip);
#endif

    rc = supdrvGipSetFlags(pDevExt, pSession, fOrMask, fAndMask);

#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSemMutexRelease(pDevExt->mtxGip);
#else
    RTSemFastMutexRelease(pDevExt->mtxGip);
#endif
    return rc;
}

