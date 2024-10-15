/* $Id: timesupref.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Time using SUPLib, the C Code Template.
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


/**
 * The C reference implementation of the assembly routines.
 *
 * Calculate NanoTS using the information in the global information page (GIP)
 * which the support library (SUPLib) exports.
 *
 * This function guarantees that the returned timestamp is later (in time) than
 * any previous calls in the same thread.
 *
 * @remark  The way the ever increasing time guarantee is currently implemented means
 *          that if you call this function at a frequency higher than 1GHz you're in for
 *          trouble. We currently assume that no idiot will do that for real life purposes.
 *
 * @returns Nanosecond timestamp.
 * @param   pData       Pointer to the data structure.
 * @param   pExtra      Where to return extra time info. Optional.
 */
RTDECL(uint64_t) rtTimeNanoTSInternalRef(PRTTIMENANOTSDATA pData, PRTITMENANOTSEXTRA pExtra)
{
#if TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA && defined(IN_RING3)
    PSUPGIPCPU pGipCpuAttemptedTscRecalibration = NULL;
#endif
    AssertCompile(RT_IS_POWER_OF_TWO(RTCPUSET_MAX_CPUS));

    for (;;)
    {
#ifndef IN_RING3 /* This simplifies and improves everything. */
        RTCCUINTREG const  uFlags = ASMIntDisableFlags();
#endif

        /*
         * Check that the GIP is sane and that the premises for this worker function
         * hasn't changed (CPU onlined with bad delta or missing features).
         */
        PSUPGLOBALINFOPAGE pGip = g_pSUPGlobalInfoPage;
        if (   RT_LIKELY(pGip)
            && RT_LIKELY(pGip->u32Magic == SUPGLOBALINFOPAGE_MAGIC)
#if TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA
            && RT_LIKELY(pGip->enmUseTscDelta >= SUPGIPUSETSCDELTA_PRACTICALLY_ZERO)
#else
            && RT_LIKELY(pGip->enmUseTscDelta <= SUPGIPUSETSCDELTA_ROUGHLY_ZERO)
#endif
#if defined(IN_RING3) && TMPL_GET_CPU_METHOD != 0
            && RT_LIKELY(pGip->fGetGipCpu & TMPL_GET_CPU_METHOD)
#endif
           )
        {
            /*
             * Resolve pGipCpu if needed.  If the instruction is serializing, we
             * read the transaction id first if possible.
             */
#if TMPL_MODE == TMPL_MODE_ASYNC || TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA
# if   defined(IN_RING0)
            uint32_t const  iCpuSet  = RTMpCurSetIndex();
            uint16_t const  iGipCpu  = iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                                     ? pGip->aiCpuFromCpuSetIdx[iCpuSet] : UINT16_MAX;
# elif defined(IN_RC)
            uint32_t const  iCpuSet  = VMMGetCpu(&g_VM)->iHostCpuSet;
            uint16_t const  iGipCpu  = iCpuSet < RT_ELEMENTS(pGip->aiCpuFromCpuSetIdx)
                                     ? pGip->aiCpuFromCpuSetIdx[iCpuSet] : UINT16_MAX;
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_APIC_ID
#  if TMPL_MODE != TMPL_MODE_ASYNC
            uint32_t const  u32TransactionId = pGip->aCPUs[0].u32TransactionId;
#  endif
            uint8_t  const  idApic   = ASMGetApicId();
            uint16_t const  iGipCpu  = pGip->aiCpuFromApicId[idApic];
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_APIC_ID_EXT_0B
#  if TMPL_MODE != TMPL_MODE_ASYNC
            uint32_t const  u32TransactionId = pGip->aCPUs[0].u32TransactionId;
#  endif
            uint32_t const  idApic   = ASMGetApicIdExt0B();
            uint16_t const  iGipCpu  = pGip->aiCpuFromApicId[idApic];
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_APIC_ID_EXT_8000001E
#  if TMPL_MODE != TMPL_MODE_ASYNC
            uint32_t const  u32TransactionId = pGip->aCPUs[0].u32TransactionId;
#  endif
            uint32_t const  idApic   = ASMGetApicIdExt8000001E();
            uint16_t const  iGipCpu  = pGip->aiCpuFromApicId[idApic];
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS \
    || TMPL_GET_CPU_METHOD == SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL
#  if TMPL_MODE != TMPL_MODE_ASYNC
            uint32_t const  u32TransactionId = pGip->aCPUs[0].u32TransactionId;
#  endif
            uint32_t        uAux;
            ASMReadTscWithAux(&uAux);
#  if TMPL_GET_CPU_METHOD == SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS
            uint16_t const  iCpuSet  = uAux & (RTCPUSET_MAX_CPUS - 1);
#  else
            uint16_t        iCpuSet = 0;
            uint16_t        offGipCpuGroup = pGip->aoffCpuGroup[(uAux >> 8) & UINT8_MAX];
            if (offGipCpuGroup < pGip->cPages * PAGE_SIZE)
            {
                PSUPGIPCPUGROUP pGipCpuGroup = (PSUPGIPCPUGROUP)((uintptr_t)pGip + offGipCpuGroup);
                if (   (uAux & UINT8_MAX) < pGipCpuGroup->cMaxMembers
                    && pGipCpuGroup->aiCpuSetIdxs[uAux & UINT8_MAX] != -1)
                    iCpuSet = pGipCpuGroup->aiCpuSetIdxs[uAux & UINT8_MAX];
            }
#  endif
            uint16_t const  iGipCpu  = pGip->aiCpuFromCpuSetIdx[iCpuSet];
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
            uint16_t const  cbLim    = ASMGetIdtrLimit();
            uint16_t const  iCpuSet  = (cbLim - 256 * (ARCH_BITS == 64 ? 16 : 8)) & (RTCPUSET_MAX_CPUS - 1);
            uint16_t const  iGipCpu  = pGip->aiCpuFromCpuSetIdx[iCpuSet];
# else
#  error "What?"
# endif
            if (RT_LIKELY(iGipCpu < pGip->cCpus))
            {
                PSUPGIPCPU pGipCpu = &pGip->aCPUs[iGipCpu];
#else
            {
#endif
                /*
                 * Get the transaction ID if necessary and we haven't already
                 * read it before a serializing instruction above.  We can skip
                 * this for ASYNC_TSC mode in ring-0 and raw-mode context since
                 * we disable interrupts.
                 */
#if TMPL_MODE == TMPL_MODE_ASYNC && defined(IN_RING3)
                uint32_t const u32TransactionId = pGipCpu->u32TransactionId;
                ASMCompilerBarrier();
                TMPL_READ_FENCE();
#elif TMPL_MODE != TMPL_MODE_ASYNC \
   && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID \
   && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID_EXT_0B \
   && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID_EXT_8000001E \
   && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS \
   && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL
                uint32_t const u32TransactionId = pGip->aCPUs[0].u32TransactionId;
                ASMCompilerBarrier();
                TMPL_READ_FENCE();
#endif

                /*
                 * Gather all the data we need.  The mess at the end is to make
                 * sure all loads are done before we recheck the transaction ID
                 * without triggering serializing twice.
                 */
                uint32_t u32NanoTSFactor0       = pGip->u32UpdateIntervalNS;
#if TMPL_MODE == TMPL_MODE_ASYNC
                uint32_t u32UpdateIntervalTSC   = pGipCpu->u32UpdateIntervalTSC;
                uint64_t u64NanoTS              = pGipCpu->u64NanoTS;
                uint64_t u64TSC                 = pGipCpu->u64TSC;
#else
                uint32_t u32UpdateIntervalTSC   = pGip->aCPUs[0].u32UpdateIntervalTSC;
                uint64_t u64NanoTS              = pGip->aCPUs[0].u64NanoTS;
                uint64_t u64TSC                 = pGip->aCPUs[0].u64TSC;
# if TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA
                int64_t i64TscDelta             = pGipCpu->i64TSCDelta;
# endif
#endif
                uint64_t u64PrevNanoTS          = ASMAtomicUoReadU64(pData->pu64Prev);
#if TMPL_GET_CPU_METHOD == SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS \
 || TMPL_GET_CPU_METHOD == SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL
                ASMCompilerBarrier();
                uint32_t uAux2;
                uint64_t u64Delta               = ASMReadTscWithAux(&uAux2); /* serializing */
#else
                uint64_t u64Delta               = ASMReadTSC();
                ASMCompilerBarrier();
# if TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID /* getting APIC will serialize  */ \
  && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID_EXT_0B \
  && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID_EXT_8000001E \
  && (defined(IN_RING3) || TMPL_MODE != TMPL_MODE_ASYNC)
                TMPL_READ_FENCE(); /* Expensive (~30 ticks).  Would like convincing argumentation that let us remove it. */
# endif
#endif

                /*
                 * Check that we didn't change CPU.
                 */
#if defined(IN_RING3) && ( TMPL_MODE == TMPL_MODE_ASYNC || TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA )
# if   TMPL_GET_CPU_METHOD == SUPGIPGETCPU_APIC_ID
                if (RT_LIKELY(ASMGetApicId() == idApic))
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_APIC_ID_EXT_0B
                if (RT_LIKELY(ASMGetApicIdExt0B() == idApic))
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_APIC_ID_EXT_8000001E
                if (RT_LIKELY(ASMGetApicIdExt8000001E() == idApic))
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_RDTSCP_MASK_MAX_SET_CPUS \
    || TMPL_GET_CPU_METHOD == SUPGIPGETCPU_RDTSCP_GROUP_IN_CH_NUMBER_IN_CL
                if (RT_LIKELY(uAux2 == uAux))
# elif TMPL_GET_CPU_METHOD == SUPGIPGETCPU_IDTR_LIMIT_MASK_MAX_SET_CPUS
                if (RT_LIKELY(ASMGetIdtrLimit() == cbLim))
# endif
#endif
                {
                    /*
                     * Check the transaction ID (see above for R0/RC + ASYNC).
                     */
#if defined(IN_RING3) || TMPL_MODE != TMPL_MODE_ASYNC
# if TMPL_MODE == TMPL_MODE_ASYNC
                    if (RT_LIKELY(pGipCpu->u32TransactionId       == u32TransactionId && !(u32TransactionId & 1) ))
# else
                    if (RT_LIKELY(pGip->aCPUs[0].u32TransactionId == u32TransactionId && !(u32TransactionId & 1) ))
# endif
#endif
                    {

                        /*
                         * Apply the TSC delta.  If the delta is invalid and the
                         * execution allows it, try trigger delta recalibration.
                         */
#if TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA && defined(IN_RING3)
                        if (RT_LIKELY(   i64TscDelta != INT64_MAX
                                      || pGipCpu == pGipCpuAttemptedTscRecalibration))
#endif
                        {
#if TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA
# ifndef IN_RING3
                            if (RT_LIKELY(i64TscDelta != INT64_MAX))
# endif
                                u64Delta -= i64TscDelta;
#endif

                            /*
                             * Bingo! We've got a consistent set of data.
                             */
#ifndef IN_RING3
                            ASMSetFlags(uFlags);
#endif

                            if (pExtra)
                                pExtra->uTSCValue = u64Delta;

                            /*
                             * Calc NanoTS delta.
                             */
                            u64Delta -= u64TSC;
                            if (RT_LIKELY(u64Delta <= u32UpdateIntervalTSC))
                            { /* MSVC branch hint, probably pointless. */ }
                            else
                            {
                                /*
                                 * We've expired the interval, cap it. If we're here for the 2nd
                                 * time without any GIP update in-between, the checks against
                                 * *pu64Prev below will force 1ns stepping.
                                 */
                                ASMAtomicIncU32(&pData->cExpired);
                                u64Delta = u32UpdateIntervalTSC;
                            }
#if !defined(_MSC_VER) || !defined(RT_ARCH_X86) /* GCC makes very pretty code from these two inline calls, while MSC cannot. */
                            u64Delta = ASMMult2xU32RetU64((uint32_t)u64Delta, u32NanoTSFactor0);
                            u64Delta = ASMDivU64ByU32RetU32(u64Delta, u32UpdateIntervalTSC);
#else
                            __asm
                            {
                                mov     eax, dword ptr [u64Delta]
                                mul     dword ptr [u32NanoTSFactor0]
                                div     dword ptr [u32UpdateIntervalTSC]
                                mov     dword ptr [u64Delta], eax
                                xor     edx, edx
                                mov     dword ptr [u64Delta + 4], edx
                            }
#endif

                            /*
                             * Calculate the time and compare it with the previously returned value.
                             */
                            u64NanoTS += u64Delta;
                            uint64_t u64DeltaPrev = u64NanoTS - u64PrevNanoTS;
                            if (RT_LIKELY(   u64DeltaPrev > 0
                                          && u64DeltaPrev < UINT64_C(86000000000000) /* 24h */))
                            { /* Frequent - less than 24h since last call. */ }
                            else if (RT_LIKELY(   (int64_t)u64DeltaPrev <= 0
                                               && (int64_t)u64DeltaPrev + u32NanoTSFactor0 * 2 >= 0))
                            {
                                /* Occasional - u64NanoTS is in the recent 'past' relative the previous call. */
                                ASMAtomicIncU32(&pData->c1nsSteps);
                                u64NanoTS = u64PrevNanoTS + 1;
                            }
                            else if (!u64PrevNanoTS)
                                /* We're resuming (see TMVirtualResume). */;
                            else
                            {
                                /* Something has gone bust, if negative offset it's real bad. */
                                ASMAtomicIncU32(&pData->cBadPrev);
                                pData->pfnBad(pData, u64NanoTS, u64DeltaPrev, u64PrevNanoTS);
                            }

                            /*
                             * Attempt updating the previous value, provided we're still ahead of it.
                             *
                             * There is no point in recalculating u64NanoTS because we got preempted or if
                             * we raced somebody while the GIP was updated, since these are events
                             * that might occur at any point in the return path as well.
                             */
                            if (RT_LIKELY(ASMAtomicCmpXchgU64(pData->pu64Prev, u64NanoTS, u64PrevNanoTS)))
                                return u64NanoTS;

                            ASMAtomicIncU32(&pData->cUpdateRaces);
                            for (int cTries = 25; cTries > 0; cTries--)
                            {
                                u64PrevNanoTS = ASMAtomicReadU64(pData->pu64Prev);
                                if (u64PrevNanoTS >= u64NanoTS)
                                    break;
                                if (ASMAtomicCmpXchgU64(pData->pu64Prev, u64NanoTS, u64PrevNanoTS))
                                    break;
                                ASMNopPause();
                            }
                            return u64NanoTS;
                        }

#if TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA && defined(IN_RING3)
                        /*
                         * Call into the support driver to try make it recalculate the delta. We
                         * remember which GIP CPU structure we're probably working on so we won't
                         * end up in a loop if the driver for some reason cannot get the job done.
                         */
                        else /* else is unecessary, but helps checking the preprocessor spaghetti. */
                        {
                            pGipCpuAttemptedTscRecalibration = pGipCpu;
                            uint64_t u64TscTmp;
                            uint16_t idApicUpdate;
                            int rc = SUPR3ReadTsc(&u64TscTmp, &idApicUpdate);
                            if (RT_SUCCESS(rc) && idApicUpdate < RT_ELEMENTS(pGip->aiCpuFromApicId))
                            {
                                uint32_t iUpdateGipCpu = pGip->aiCpuFromApicId[idApicUpdate];
                                if (iUpdateGipCpu < pGip->cCpus)
                                    pGipCpuAttemptedTscRecalibration = &pGip->aCPUs[iUpdateGipCpu];
                            }
                        }
#endif
                    }
                }

                /*
                 * No joy must try again.
                 */
#ifdef _MSC_VER
# pragma warning(disable: 4702)
#endif
#ifndef IN_RING3
                ASMSetFlags(uFlags);
#endif
                ASMNopPause();
                continue;
            }

#if TMPL_MODE == TMPL_MODE_ASYNC || TMPL_MODE == TMPL_MODE_SYNC_INVAR_WITH_DELTA
            /*
             * We've got a bad CPU or APIC index of some kind.
             */
            else /* else is unecessary, but helps checking the preprocessor spaghetti. */
            {
# ifndef IN_RING3
                ASMSetFlags(uFlags);
# endif
# if defined(IN_RING0) \
  || defined(IN_RC) \
  || (   TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID \
      && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID_EXT_0B /*?*/ \
      && TMPL_GET_CPU_METHOD != SUPGIPGETCPU_APIC_ID_EXT_8000001E /*?*/)
                return pData->pfnBadCpuIndex(pData, pExtra, UINT16_MAX-1, iCpuSet, iGipCpu);
# else
                return pData->pfnBadCpuIndex(pData, pExtra, idApic, UINT16_MAX-1, iGipCpu);
# endif
            }
#endif
        }

        /*
         * Something changed in the GIP config or it was unmapped, figure out
         * the right worker function to use now.
         */
#ifndef IN_RING3
        ASMSetFlags(uFlags);
#endif
        return pData->pfnRediscover(pData, pExtra);
    }
}

