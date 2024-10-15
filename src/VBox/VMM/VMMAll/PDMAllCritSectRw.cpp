/* $Id: PDMAllCritSectRw.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Read/Write Critical Section, Generic.
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
#define LOG_GROUP LOG_GROUP_PDM_CRITSECTRW
#include "PDMInternal.h"
#include <VBox/vmm/pdmcritsectrw.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/vmcc.h>
#include <VBox/err.h>
#include <VBox/vmm/hm.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#ifdef IN_RING3
# include <iprt/lockvalidator.h>
#endif
#if defined(IN_RING3) || defined(IN_RING0)
# include <iprt/semaphore.h>
# include <iprt/thread.h>
#endif
#ifdef IN_RING0
# include <iprt/time.h>
#endif
#ifdef RT_ARCH_AMD64
# include <iprt/x86.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if 0 /* unused */
/** The number loops to spin for shared access in ring-3. */
#define PDMCRITSECTRW_SHRD_SPIN_COUNT_R3        20
/** The number loops to spin for shared access in ring-0. */
#define PDMCRITSECTRW_SHRD_SPIN_COUNT_R0        128
/** The number loops to spin for shared access in the raw-mode context. */
#define PDMCRITSECTRW_SHRD_SPIN_COUNT_RC        128

/** The number loops to spin for exclusive access in ring-3. */
#define PDMCRITSECTRW_EXCL_SPIN_COUNT_R3        20
/** The number loops to spin for exclusive access in ring-0. */
#define PDMCRITSECTRW_EXCL_SPIN_COUNT_R0        256
/** The number loops to spin for exclusive access in the raw-mode context. */
#define PDMCRITSECTRW_EXCL_SPIN_COUNT_RC        256
#endif

/** Max number of write or write/read recursions. */
#define PDM_CRITSECTRW_MAX_RECURSIONS           _1M

/** Skips some of the overly paranoid atomic reads and updates.
 * Makes some assumptions about cache coherence, though not brave enough not to
 * always end with an atomic update. */
#define PDMCRITSECTRW_WITH_LESS_ATOMIC_STUFF

/** For reading RTCRITSECTRWSTATE::s::u64State. */
#ifdef PDMCRITSECTRW_WITH_LESS_ATOMIC_STUFF
# define PDMCRITSECTRW_READ_STATE(a_pu64State)  ASMAtomicUoReadU64(a_pu64State)
#else
# define PDMCRITSECTRW_READ_STATE(a_pu64State)  ASMAtomicReadU64(a_pu64State)
#endif


/* Undefine the automatic VBOX_STRICT API mappings. */
#undef PDMCritSectRwEnterExcl
#undef PDMCritSectRwTryEnterExcl
#undef PDMCritSectRwEnterShared
#undef PDMCritSectRwTryEnterShared


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RTASM_HAVE_CMP_WRITE_U128) && defined(RT_ARCH_AMD64)
static int32_t g_fCmpWriteSupported = -1;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int pdmCritSectRwLeaveSharedWorker(PVMCC pVM, PPDMCRITSECTRW pThis, bool fNoVal);


#ifdef RTASM_HAVE_CMP_WRITE_U128

# ifdef RT_ARCH_AMD64
/**
 * Called once to initialize g_fCmpWriteSupported.
 */
DECL_NO_INLINE(static, bool) pdmCritSectRwIsCmpWriteU128SupportedSlow(void)
{
    bool const fCmpWriteSupported = RT_BOOL(ASMCpuId_ECX(1) & X86_CPUID_FEATURE_ECX_CX16);
    ASMAtomicWriteS32(&g_fCmpWriteSupported, fCmpWriteSupported);
    return fCmpWriteSupported;
}
# endif


/**
 * Indicates whether hardware actually supports 128-bit compare & write.
 */
DECL_FORCE_INLINE(bool) pdmCritSectRwIsCmpWriteU128Supported(void)
{
# ifdef RT_ARCH_AMD64
    int32_t const fCmpWriteSupported = g_fCmpWriteSupported;
    if (RT_LIKELY(fCmpWriteSupported >= 0))
        return fCmpWriteSupported != 0;
    return pdmCritSectRwIsCmpWriteU128SupportedSlow();
# else
    return true;
# endif
}

#endif /* RTASM_HAVE_CMP_WRITE_U128 */

/**
 * Gets the ring-3 native thread handle of the calling thread.
 *
 * @returns native thread handle (ring-3).
 * @param   pVM         The cross context VM structure.
 * @param   pThis       The read/write critical section.  This is only used in
 *                      R0 and RC.
 */
DECL_FORCE_INLINE(RTNATIVETHREAD) pdmCritSectRwGetNativeSelf(PVMCC pVM, PCPDMCRITSECTRW pThis)
{
#ifdef IN_RING3
    RT_NOREF(pVM, pThis);
    RTNATIVETHREAD  hNativeSelf = RTThreadNativeSelf();

#elif defined(IN_RING0)
    AssertMsgReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, ("%RX32\n", pThis->s.Core.u32Magic),
                    NIL_RTNATIVETHREAD);
    RTNATIVETHREAD  hNativeSelf = GVMMR0GetRing3ThreadForSelf(pVM);
    Assert(hNativeSelf != NIL_RTNATIVETHREAD);

#else
# error "invalid context"
#endif
    return hNativeSelf;
}


DECL_NO_INLINE(static, int) pdmCritSectRwCorrupted(PPDMCRITSECTRW pThis, const char *pszMsg)
{
    ASMAtomicWriteU32(&pThis->s.Core.u32Magic, PDMCRITSECTRW_MAGIC_CORRUPT);
    LogRel(("PDMCritSect: %s pCritSect=%p\n", pszMsg, pThis));
    return VERR_PDM_CRITSECTRW_IPE;
}



#ifdef IN_RING3
/**
 * Changes the lock validator sub-class of the read/write critical section.
 *
 * It is recommended to try make sure that nobody is using this critical section
 * while changing the value.
 *
 * @returns The old sub-class.  RTLOCKVAL_SUB_CLASS_INVALID is returns if the
 *          lock validator isn't compiled in or either of the parameters are
 *          invalid.
 * @param   pThis           Pointer to the read/write critical section.
 * @param   uSubClass       The new sub-class value.
 */
VMMDECL(uint32_t) PDMR3CritSectRwSetSubClass(PPDMCRITSECTRW pThis, uint32_t uSubClass)
{
    AssertPtrReturn(pThis, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);
# if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
    AssertReturn(!(pThis->s.Core.fFlags & RTCRITSECT_FLAGS_NOP), RTLOCKVAL_SUB_CLASS_INVALID);

    RTLockValidatorRecSharedSetSubClass(pThis->s.Core.pValidatorRead, uSubClass);
    return RTLockValidatorRecExclSetSubClass(pThis->s.Core.pValidatorWrite, uSubClass);
# else
    NOREF(uSubClass);
    return RTLOCKVAL_SUB_CLASS_INVALID;
# endif
}
#endif /* IN_RING3 */


/**
 * Worker for pdmCritSectRwEnterShared returning with read-ownership of the CS.
 */
DECL_FORCE_INLINE(int) pdmCritSectRwEnterSharedGotIt(PPDMCRITSECTRW pThis, PCRTLOCKVALSRCPOS pSrcPos,
                                                     bool fNoVal, RTTHREAD hThreadSelf)
{
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
    if (!fNoVal)
        RTLockValidatorRecSharedAddOwner(pThis->s.Core.pValidatorRead, hThreadSelf, pSrcPos);
#else
    RT_NOREF(pSrcPos, fNoVal, hThreadSelf);
#endif

    /* got it! */
    STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(Stat,EnterShared));
    Assert((PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State) & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT));
    return VINF_SUCCESS;
}

/**
 * Worker for pdmCritSectRwEnterShared and pdmCritSectRwEnterSharedBailOut
 * that decrement the wait count and maybe resets the semaphore.
 */
DECLINLINE(int) pdmCritSectRwEnterSharedGotItAfterWaiting(PVMCC pVM, PPDMCRITSECTRW pThis, uint64_t u64State,
                                                          PCRTLOCKVALSRCPOS pSrcPos, bool fNoVal, RTTHREAD hThreadSelf)
{
    for (;;)
    {
        uint64_t const u64OldState = u64State;
        uint64_t       cWait       = (u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT;
        AssertReturn(cWait > 0, pdmCritSectRwCorrupted(pThis, "Invalid waiting read count"));
        AssertReturn((u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT > 0,
                     pdmCritSectRwCorrupted(pThis, "Invalid read count"));
        cWait--;
        u64State &= ~RTCSRW_WAIT_CNT_RD_MASK;
        u64State |= cWait << RTCSRW_WAIT_CNT_RD_SHIFT;

        if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
        {
            if (cWait == 0)
            {
                if (ASMAtomicXchgBool(&pThis->s.Core.fNeedReset, false))
                {
                    int rc = SUPSemEventMultiReset(pVM->pSession, (SUPSEMEVENTMULTI)pThis->s.Core.hEvtRead);
                    AssertRCReturn(rc, rc);
                }
            }
            return pdmCritSectRwEnterSharedGotIt(pThis, pSrcPos, fNoVal, hThreadSelf);
        }

        ASMNopPause();
        AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);
        ASMNopPause();

        u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
    }
    /* not reached */
}


#if defined(IN_RING0) || (defined(IN_RING3) && defined(PDMCRITSECTRW_STRICT))
/**
 * Worker for pdmCritSectRwEnterSharedContended that decrements both read counts
 * and returns @a rc.
 *
 * @note May return VINF_SUCCESS if we race the exclusive leave function and
 *       come out on the bottom.
 *
 *       Ring-3 only calls in a case where it is _not_ acceptable to take the
 *       lock, so even if we get the lock we'll have to leave.  In the ring-0
 *       contexts, we can safely return VINF_SUCCESS in case of a race.
 */
DECL_NO_INLINE(static, int) pdmCritSectRwEnterSharedBailOut(PVMCC pVM, PPDMCRITSECTRW pThis, int rc,
                                                            PCRTLOCKVALSRCPOS pSrcPos, bool fNoVal, RTTHREAD hThreadSelf)
{
#ifdef IN_RING0
    uint64_t const tsStart    = RTTimeNanoTS();
    uint64_t       cNsElapsed = 0;
#endif
    for (;;)
    {
        uint64_t u64State    = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
        uint64_t u64OldState = u64State;

        uint64_t cWait       = (u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT;
        AssertReturn(cWait > 0, pdmCritSectRwCorrupted(pThis, "Invalid waiting read count on bailout"));
        cWait--;

        uint64_t c           = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
        AssertReturn(c > 0, pdmCritSectRwCorrupted(pThis, "Invalid read count on bailout"));

        if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT))
        {
            c--;
            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_WAIT_CNT_RD_MASK);
            u64State |= (c << RTCSRW_CNT_RD_SHIFT) | (cWait << RTCSRW_WAIT_CNT_RD_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                return rc;
        }
        else
        {
            /*
             * The direction changed, so we can actually get the lock now.
             *
             * This means that we _have_ to wait on the semaphore to be signalled
             * so we can properly reset it.  Otherwise the stuff gets out of wack,
             * because signalling and resetting will race one another.  An
             * exception would be if we're not the last reader waiting and don't
             * need to worry about the resetting.
             *
             * An option would be to do the resetting in PDMCritSectRwEnterExcl,
             * but that would still leave a racing PDMCritSectRwEnterShared
             * spinning hard for a little bit, which isn't great...
             */
            if (cWait == 0)
            {
# ifdef IN_RING0
                /* Do timeout processing first to avoid redoing the above. */
                uint32_t cMsWait;
                if (cNsElapsed <= RT_NS_10SEC)
                    cMsWait = 32;
                else
                {
                    u64State &= ~RTCSRW_WAIT_CNT_RD_MASK;
                    u64State |= cWait << RTCSRW_WAIT_CNT_RD_SHIFT;
                    if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                    {
                        LogFunc(("%p: giving up\n", pThis));
                        return rc;
                    }
                    cMsWait = 2;
                }

                int rcWait = SUPSemEventMultiWait(pVM->pSession, (SUPSEMEVENTMULTI)pThis->s.Core.hEvtRead, cMsWait);
                Log11Func(("%p: rc=%Rrc %'RU64 ns (hNativeWriter=%p u64State=%#RX64)\n", pThis, rcWait,
                           RTTimeNanoTS() - tsStart, pThis->s.Core.u.s.hNativeWriter, pThis->s.Core.u.s.u64State));
# else
                RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_READ, false);
                int rcWait = SUPSemEventMultiWaitNoResume(pVM->pSession, (SUPSEMEVENTMULTI)pThis->s.Core.hEvtRead, RT_MS_5SEC);
                RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
# endif
                if (rcWait == VINF_SUCCESS)
                {
# ifdef IN_RING0
                    return pdmCritSectRwEnterSharedGotItAfterWaiting(pVM, pThis, u64State, pSrcPos, fNoVal, hThreadSelf);
# else
                    /* ring-3: Cannot return VINF_SUCCESS. */
                    Assert(RT_FAILURE_NP(rc));
                    int rc2 = pdmCritSectRwEnterSharedGotItAfterWaiting(pVM, pThis, u64State, pSrcPos, fNoVal, hThreadSelf);
                    if (RT_SUCCESS(rc2))
                        rc2 = pdmCritSectRwLeaveSharedWorker(pVM, pThis, fNoVal);
                    return rc;
# endif
                }
                AssertMsgReturn(rcWait == VERR_TIMEOUT || rcWait == VERR_INTERRUPTED,
                                ("%p: rcWait=%Rrc rc=%Rrc", pThis, rcWait, rc),
                                RT_FAILURE_NP(rcWait) ? rcWait : -rcWait);
            }
            else
            {
                u64State &= ~RTCSRW_WAIT_CNT_RD_MASK;
                u64State |= cWait << RTCSRW_WAIT_CNT_RD_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                    return pdmCritSectRwEnterSharedGotIt(pThis, pSrcPos, fNoVal, hThreadSelf);
            }

# ifdef IN_RING0
            /* Calculate the elapsed time here to avoid redoing state work. */
            cNsElapsed = RTTimeNanoTS() - tsStart;
# endif
        }

        ASMNopPause();
        AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);
        ASMNopPause();
    }
}
#endif /* IN_RING0  || (IN_RING3 && PDMCRITSECTRW_STRICT) */


/**
 * Worker for pdmCritSectRwEnterShared that handles waiting for a contended CS.
 * Caller has already added us to the read and read-wait counters.
 */
static int pdmCritSectRwEnterSharedContended(PVMCC pVM, PVMCPUCC pVCpu, PPDMCRITSECTRW pThis,
                                             int rcBusy, PCRTLOCKVALSRCPOS pSrcPos, bool fNoVal, RTTHREAD hThreadSelf)
{
    PSUPDRVSESSION const    pSession          = pVM->pSession;
    SUPSEMEVENTMULTI const  hEventMulti       = (SUPSEMEVENTMULTI)pThis->s.Core.hEvtRead;
# ifdef IN_RING0
    uint64_t const          tsStart           = RTTimeNanoTS();
    uint64_t const          cNsMaxTotalDef    = RT_NS_5MIN;
    uint64_t                cNsMaxTotal       = cNsMaxTotalDef;
    uint32_t                cMsMaxOne         = RT_MS_5SEC;
    bool                    fNonInterruptible = false;
# endif

    for (uint32_t iLoop = 0; ; iLoop++)
    {
        /*
         * Wait for the direction to switch.
         */
        int rc;
# ifdef IN_RING3
#  if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
        rc = RTLockValidatorRecSharedCheckBlocking(pThis->s.Core.pValidatorRead, hThreadSelf, pSrcPos, true,
                                                   RT_INDEFINITE_WAIT, RTTHREADSTATE_RW_READ, false);
        if (RT_FAILURE(rc))
            return pdmCritSectRwEnterSharedBailOut(pVM, pThis, rc, pSrcPos, fNoVal, hThreadSelf);
#  else
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_READ, false);
#  endif
# endif

        for (;;)
        {
            /*
             * We always wait with a timeout so we can re-check the structure sanity
             * and not get stuck waiting on a corrupt or deleted section.
             */
# ifdef IN_RING3
            rc = SUPSemEventMultiWaitNoResume(pSession, hEventMulti, RT_MS_5SEC);
# else
            rc = !fNonInterruptible
               ? SUPSemEventMultiWaitNoResume(pSession, hEventMulti, cMsMaxOne)
               : SUPSemEventMultiWait(pSession, hEventMulti, cMsMaxOne);
            Log11Func(("%p: rc=%Rrc %'RU64 ns (cMsMaxOne=%RU64 hNativeWriter=%p u64State=%#RX64)\n", pThis, rc,
                       RTTimeNanoTS() - tsStart, cMsMaxOne, pThis->s.Core.u.s.hNativeWriter, pThis->s.Core.u.s.u64State));
# endif
            if (RT_LIKELY(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC))
            { /* likely */ }
            else
            {
# ifdef IN_RING3
                RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
# endif
                return VERR_SEM_DESTROYED;
            }
            if (RT_LIKELY(rc == VINF_SUCCESS))
                break;

            /*
             * Timeout and interrupted waits needs careful handling in ring-0
             * because we're cooperating with ring-3 on this critical section
             * and thus need to make absolutely sure we won't get stuck here.
             *
             * The r0 interrupted case means something is pending (termination,
             * signal, APC, debugger, whatever), so we must try our best to
             * return to the caller and to ring-3 so it can be dealt with.
             */
            if (rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED)
            {
# ifdef IN_RING0
                uint64_t const cNsElapsed = RTTimeNanoTS() - tsStart;
                int const      rcTerm     = RTThreadQueryTerminationStatus(NIL_RTTHREAD);
                AssertMsg(rcTerm == VINF_SUCCESS || rcTerm == VERR_NOT_SUPPORTED || rcTerm == VINF_THREAD_IS_TERMINATING,
                          ("rcTerm=%Rrc\n", rcTerm));
                if (rcTerm == VERR_NOT_SUPPORTED && cNsMaxTotal == cNsMaxTotalDef)
                    cNsMaxTotal = RT_NS_1MIN;

                if (rc == VERR_TIMEOUT)
                {
                    /* Try return get out of here with a non-VINF_SUCCESS status if
                       the thread is terminating or if the timeout has been exceeded. */
                    STAM_REL_COUNTER_INC(&pVM->pdm.s.StatCritSectRwSharedVerrTimeout);
                    if (   rcTerm == VINF_THREAD_IS_TERMINATING
                        || cNsElapsed > cNsMaxTotal)
                        return pdmCritSectRwEnterSharedBailOut(pVM, pThis, rcBusy != VINF_SUCCESS ? rcBusy : rc,
                                                               pSrcPos, fNoVal, hThreadSelf);
                }
                else
                {
                    /* For interrupt cases, we must return if we can.  If rcBusy is VINF_SUCCESS,
                       we will try non-interruptible sleep for a while to help resolve the issue
                       w/o guru'ing. */
                    STAM_REL_COUNTER_INC(&pVM->pdm.s.StatCritSectRwSharedVerrInterrupted);
                    if (   rcTerm != VINF_THREAD_IS_TERMINATING
                        && rcBusy == VINF_SUCCESS
                        && pVCpu != NULL
                        && cNsElapsed <= cNsMaxTotal)
                    {
                        if (!fNonInterruptible)
                        {
                            STAM_REL_COUNTER_INC(&pVM->pdm.s.StatCritSectRwSharedNonInterruptibleWaits);
                            fNonInterruptible   = true;
                            cMsMaxOne           = 32;
                            uint64_t cNsLeft = cNsMaxTotal - cNsElapsed;
                            if (cNsLeft > RT_NS_10SEC)
                                cNsMaxTotal = cNsElapsed + RT_NS_10SEC;
                        }
                    }
                    else
                        return pdmCritSectRwEnterSharedBailOut(pVM, pThis, rcBusy != VINF_SUCCESS ? rcBusy : rc,
                                                               pSrcPos, fNoVal, hThreadSelf);
                }
# else  /* IN_RING3 */
                RT_NOREF(pVM, pVCpu, rcBusy);
# endif /* IN_RING3 */
            }
            /*
             * Any other return code is fatal.
             */
            else
            {
# ifdef IN_RING3
                RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
# endif
                AssertMsgFailed(("rc=%Rrc\n", rc));
                return RT_FAILURE_NP(rc) ? rc : -rc;
            }
        }

# ifdef IN_RING3
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_READ);
# endif

        /*
         * Check the direction.
         */
        Assert(pThis->s.Core.fNeedReset);
        uint64_t u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
        if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
        {
            /*
             * Decrement the wait count and maybe reset the semaphore (if we're last).
             */
            return pdmCritSectRwEnterSharedGotItAfterWaiting(pVM, pThis, u64State, pSrcPos, fNoVal, hThreadSelf);
        }

        AssertMsg(iLoop < 1,
                  ("%p: %u u64State=%#RX64 hNativeWriter=%p\n", pThis, iLoop, u64State, pThis->s.Core.u.s.hNativeWriter));
        RTThreadYield();
    }

    /* not reached */
}


/**
 * Worker that enters a read/write critical section with shard access.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   rcBusy      The busy return code for ring-0 and ring-3.
 * @param   fTryOnly    Only try enter it, don't wait.
 * @param   pSrcPos     The source position. (Can be NULL.)
 * @param   fNoVal      No validation records.
 */
static int pdmCritSectRwEnterShared(PVMCC pVM, PPDMCRITSECTRW pThis, int rcBusy, bool fTryOnly,
                                    PCRTLOCKVALSRCPOS pSrcPos, bool fNoVal)
{
    /*
     * Validate input.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);

#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
    RTTHREAD hThreadSelf = RTThreadSelfAutoAdopt();
    if (!fTryOnly)
    {
        int            rc9;
        RTNATIVETHREAD hNativeWriter;
        ASMAtomicUoReadHandle(&pThis->s.Core.u.s.hNativeWriter, &hNativeWriter);
        if (hNativeWriter != NIL_RTTHREAD && hNativeWriter == pdmCritSectRwGetNativeSelf(pVM, pThis))
            rc9 = RTLockValidatorRecExclCheckOrder(pThis->s.Core.pValidatorWrite, hThreadSelf, pSrcPos, RT_INDEFINITE_WAIT);
        else
            rc9 = RTLockValidatorRecSharedCheckOrder(pThis->s.Core.pValidatorRead, hThreadSelf, pSrcPos, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#else
    RTTHREAD hThreadSelf = NIL_RTTHREAD;
#endif

    /*
     * Work the state.
     */
    uint64_t u64State    = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
    uint64_t u64OldState = u64State;
    for (;;)
    {
        if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
        {
            /* It flows in the right direction, try follow it before it changes. */
            uint64_t c = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
            c++;
            Assert(c < RTCSRW_CNT_MASK / 4);
            AssertReturn(c < RTCSRW_CNT_MASK, VERR_PDM_CRITSECTRW_TOO_MANY_READERS);
            u64State &= ~RTCSRW_CNT_RD_MASK;
            u64State |= c << RTCSRW_CNT_RD_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                return pdmCritSectRwEnterSharedGotIt(pThis, pSrcPos, fNoVal, hThreadSelf);
        }
        else if ((u64State & (RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK)) == 0)
        {
            /* Wrong direction, but we're alone here and can simply try switch the direction. */
            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK | RTCSRW_DIR_MASK);
            u64State |= (UINT64_C(1) << RTCSRW_CNT_RD_SHIFT) | (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
            {
                Assert(!pThis->s.Core.fNeedReset);
                return pdmCritSectRwEnterSharedGotIt(pThis, pSrcPos, fNoVal, hThreadSelf);
            }
        }
        else
        {
            /* Is the writer perhaps doing a read recursion? */
            RTNATIVETHREAD hNativeWriter;
            ASMAtomicUoReadHandle(&pThis->s.Core.u.s.hNativeWriter, &hNativeWriter);
            if (hNativeWriter != NIL_RTNATIVETHREAD)
            {
                RTNATIVETHREAD hNativeSelf = pdmCritSectRwGetNativeSelf(pVM, pThis);
                if (hNativeSelf == hNativeWriter)
                {
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
                    if (!fNoVal)
                    {
                        int rc9 = RTLockValidatorRecExclRecursionMixed(pThis->s.Core.pValidatorWrite, &pThis->s.Core.pValidatorRead->Core, pSrcPos);
                        if (RT_FAILURE(rc9))
                            return rc9;
                    }
#endif
                    uint32_t const cReads = ASMAtomicIncU32(&pThis->s.Core.cWriterReads);
                    Assert(cReads < _16K);
                    AssertReturnStmt(cReads < PDM_CRITSECTRW_MAX_RECURSIONS, ASMAtomicDecU32(&pThis->s.Core.cWriterReads),
                                     VERR_PDM_CRITSECTRW_TOO_MANY_RECURSIONS);
                    STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(Stat,EnterShared));
                    return VINF_SUCCESS; /* don't break! */
                }
            }

            /*
             * If we're only trying, return already.
             */
            if (fTryOnly)
            {
                STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(StatContention,EnterShared));
                return VERR_SEM_BUSY;
            }

#if defined(IN_RING3) || defined(IN_RING0)
            /*
             * Add ourselves to the queue and wait for the direction to change.
             */
            uint64_t c = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
            c++;
            Assert(c < RTCSRW_CNT_MASK / 2);
            AssertReturn(c < RTCSRW_CNT_MASK, VERR_PDM_CRITSECTRW_TOO_MANY_READERS);

            uint64_t cWait = (u64State & RTCSRW_WAIT_CNT_RD_MASK) >> RTCSRW_WAIT_CNT_RD_SHIFT;
            cWait++;
            Assert(cWait <= c);
            Assert(cWait < RTCSRW_CNT_MASK / 2);
            AssertReturn(cWait < RTCSRW_CNT_MASK, VERR_PDM_CRITSECTRW_TOO_MANY_READERS);

            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_WAIT_CNT_RD_MASK);
            u64State |= (c << RTCSRW_CNT_RD_SHIFT) | (cWait << RTCSRW_WAIT_CNT_RD_SHIFT);

            if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
            {
                /*
                 * In ring-3 it's straight forward, just optimize the RTThreadSelf() call.
                 */
# if defined(IN_RING3) && defined(PDMCRITSECTRW_STRICT)
                return pdmCritSectRwEnterSharedContended(pVM, NULL, pThis, rcBusy, pSrcPos, fNoVal, hThreadSelf);
# elif defined(IN_RING3)
                return pdmCritSectRwEnterSharedContended(pVM, NULL, pThis, rcBusy, pSrcPos, fNoVal, RTThreadSelf());
# else /* IN_RING0 */
                /*
                 * In ring-0 context we have to take the special VT-x/AMD-V HM context into
                 * account when waiting on contended locks.
                 */
                PVMCPUCC pVCpu = VMMGetCpu(pVM);
                if (pVCpu)
                {
                    VMMR0EMTBLOCKCTX Ctx;
                    int rc = VMMR0EmtPrepareToBlock(pVCpu, rcBusy, __FUNCTION__, pThis, &Ctx);
                    if (rc == VINF_SUCCESS)
                    {
                        Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));

                        rc = pdmCritSectRwEnterSharedContended(pVM, pVCpu, pThis, rcBusy, pSrcPos, fNoVal, hThreadSelf);

                        VMMR0EmtResumeAfterBlocking(pVCpu, &Ctx);
                    }
                    else
                    {
                        //STAM_REL_COUNTER_INC(&pThis->s.StatContentionRZLockBusy);
                        rc = pdmCritSectRwEnterSharedBailOut(pVM, pThis, rc, pSrcPos, fNoVal, hThreadSelf);
                    }
                    return rc;
                }

                /* Non-EMT. */
                Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));
                return pdmCritSectRwEnterSharedContended(pVM, NULL, pThis, rcBusy, pSrcPos, fNoVal, hThreadSelf);
# endif /* IN_RING0 */
            }

#else  /* !IN_RING3 && !IN_RING0 */
            /*
             * We cannot call SUPSemEventMultiWaitNoResume in this context. Go
             * back to ring-3 and do it there or return rcBusy.
             */
# error "Unused code."
            STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(StatContention,EnterShared));
            if (rcBusy == VINF_SUCCESS)
            {
                PVMCPUCC  pVCpu = VMMGetCpu(pVM); AssertPtr(pVCpu);
                /** @todo Should actually do this in via VMMR0.cpp instead of going all the way
                 *        back to ring-3. Goes for both kind of crit sects. */
                return VMMRZCallRing3(pVM, pVCpu, VMMCALLRING3_PDM_CRIT_SECT_RW_ENTER_SHARED, MMHyperCCToR3(pVM, pThis));
            }
            return rcBusy;
#endif /* !IN_RING3 && !IN_RING0 */
        }

        ASMNopPause();
        if (RT_LIKELY(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC))
        { /* likely */ }
        else
            return VERR_SEM_DESTROYED;
        ASMNopPause();

        u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
        u64OldState = u64State;
    }
    /* not reached */
}


/**
 * Enter a critical section with shared (read) access.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  rcBusy if in ring-0 or raw-mode context and it is busy.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   rcBusy      The status code to return when we're in RC or R0 and the
 *                      section is busy.   Pass VINF_SUCCESS to acquired the
 *                      critical section thru a ring-3 call if necessary.
 * @sa      PDMCritSectRwEnterSharedDebug, PDMCritSectRwTryEnterShared,
 *          PDMCritSectRwTryEnterSharedDebug, PDMCritSectRwLeaveShared,
 *          RTCritSectRwEnterShared.
 */
VMMDECL(int) PDMCritSectRwEnterShared(PVMCC pVM, PPDMCRITSECTRW pThis, int rcBusy)
{
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterShared(pVM, pThis, rcBusy, false /*fTryOnly*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return pdmCritSectRwEnterShared(pVM, pThis, rcBusy, false /*fTryOnly*/, &SrcPos, false /*fNoVal*/);
#endif
}


/**
 * Enter a critical section with shared (read) access.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  rcBusy if in ring-0 or raw-mode context and it is busy.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   rcBusy      The status code to return when we're in RC or R0 and the
 *                      section is busy.   Pass VINF_SUCCESS to acquired the
 *                      critical section thru a ring-3 call if necessary.
 * @param   uId         Where we're entering the section.
 * @param   SRC_POS     The source position.
 * @sa      PDMCritSectRwEnterShared, PDMCritSectRwTryEnterShared,
 *          PDMCritSectRwTryEnterSharedDebug, PDMCritSectRwLeaveShared,
 *          RTCritSectRwEnterSharedDebug.
 */
VMMDECL(int) PDMCritSectRwEnterSharedDebug(PVMCC pVM, PPDMCRITSECTRW pThis, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    NOREF(uId); NOREF(pszFile); NOREF(iLine); NOREF(pszFunction);
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterShared(pVM, pThis, rcBusy, false /*fTryOnly*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return pdmCritSectRwEnterShared(pVM, pThis, rcBusy, false /*fTryOnly*/, &SrcPos, false /*fNoVal*/);
#endif
}


/**
 * Try enter a critical section with shared (read) access.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_BUSY if the critsect was owned.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwTryEnterSharedDebug, PDMCritSectRwEnterShared,
 *          PDMCritSectRwEnterSharedDebug, PDMCritSectRwLeaveShared,
 *          RTCritSectRwTryEnterShared.
 */
VMMDECL(int) PDMCritSectRwTryEnterShared(PVMCC pVM, PPDMCRITSECTRW pThis)
{
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterShared(pVM, pThis, VERR_SEM_BUSY, true /*fTryOnly*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return pdmCritSectRwEnterShared(pVM, pThis, VERR_SEM_BUSY, true /*fTryOnly*/, &SrcPos, false /*fNoVal*/);
#endif
}


/**
 * Try enter a critical section with shared (read) access.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_BUSY if the critsect was owned.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   uId         Where we're entering the section.
 * @param   SRC_POS     The source position.
 * @sa      PDMCritSectRwTryEnterShared, PDMCritSectRwEnterShared,
 *          PDMCritSectRwEnterSharedDebug, PDMCritSectRwLeaveShared,
 *          RTCritSectRwTryEnterSharedDebug.
 */
VMMDECL(int) PDMCritSectRwTryEnterSharedDebug(PVMCC pVM, PPDMCRITSECTRW pThis, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    NOREF(uId); NOREF(pszFile); NOREF(iLine); NOREF(pszFunction);
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterShared(pVM, pThis, VERR_SEM_BUSY, true /*fTryOnly*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return pdmCritSectRwEnterShared(pVM, pThis, VERR_SEM_BUSY, true /*fTryOnly*/, &SrcPos, false /*fNoVal*/);
#endif
}


#ifdef IN_RING3
/**
 * Enters a PDM read/write critical section with shared (read) access.
 *
 * @returns VINF_SUCCESS if entered successfully.
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   fCallRing3  Whether this is a VMMRZCallRing3()request.
 */
VMMR3DECL(int) PDMR3CritSectRwEnterSharedEx(PVM pVM, PPDMCRITSECTRW pThis, bool fCallRing3)
{
    return pdmCritSectRwEnterShared(pVM, pThis, VERR_SEM_BUSY, false /*fTryAgain*/, NULL, fCallRing3);
}
#endif


/**
 * Leave a critical section held with shared access.
 *
 * @returns VBox status code.
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   fNoVal      No validation records (i.e. queued release).
 * @sa      PDMCritSectRwEnterShared, PDMCritSectRwTryEnterShared,
 *          PDMCritSectRwEnterSharedDebug, PDMCritSectRwTryEnterSharedDebug,
 *          PDMCritSectRwLeaveExcl, RTCritSectRwLeaveShared.
 */
static int pdmCritSectRwLeaveSharedWorker(PVMCC pVM, PPDMCRITSECTRW pThis, bool fNoVal)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);

#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    NOREF(fNoVal);
#endif

    /*
     * Check the direction and take action accordingly.
     */
#ifdef IN_RING0
    PVMCPUCC pVCpu       = NULL;
#endif
    uint64_t u64State    = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
    uint64_t u64OldState = u64State;
    if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
    {
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
        if (fNoVal)
            Assert(!RTLockValidatorRecSharedIsOwner(pThis->s.Core.pValidatorRead, NIL_RTTHREAD));
        else
        {
            int rc9 = RTLockValidatorRecSharedCheckAndRelease(pThis->s.Core.pValidatorRead, NIL_RTTHREAD);
            if (RT_FAILURE(rc9))
                return rc9;
        }
#endif
        for (;;)
        {
            uint64_t c = (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
            AssertReturn(c > 0, VERR_NOT_OWNER);
            c--;

            if (   c > 0
                || (u64State & RTCSRW_CNT_WR_MASK) == 0)
            {
                /* Don't change the direction. */
                u64State &= ~RTCSRW_CNT_RD_MASK;
                u64State |= c << RTCSRW_CNT_RD_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                    break;
            }
            else
            {
#if defined(IN_RING3) || defined(IN_RING0)
# ifdef IN_RING0
                Assert(RTSemEventIsSignalSafe() == RTSemEventMultiIsSignalSafe());
                if (!pVCpu)
                    pVCpu = VMMGetCpu(pVM);
                if (   pVCpu == NULL /* non-EMT access, if we implement it must be able to block */
                    || VMMRZCallRing3IsEnabled(pVCpu)
                    || RTSemEventIsSignalSafe()
                    || (   VMMR0ThreadCtxHookIsEnabled(pVCpu)       /* Doesn't matter if Signal() blocks if we have hooks, ... */
                        && RTThreadPreemptIsEnabled(NIL_RTTHREAD)   /* ... and preemption is still enabled, */
                        && ASMIntAreEnabled())                      /* ... and interrupts hasn't yet been disabled. Special pre-GC HM env. */
                   )
# endif
                {
                    /* Reverse the direction and signal the writer threads. */
                    u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_DIR_MASK);
                    u64State |= RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT;
                    if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                    {
                        int rc;
# ifdef IN_RING0
                        STAM_REL_COUNTER_INC(&pThis->s.StatContentionRZLeaveShared);
                        if (!RTSemEventIsSignalSafe() && pVCpu != NULL)
                        {
                            VMMR0EMTBLOCKCTX Ctx;
                            rc = VMMR0EmtPrepareToBlock(pVCpu, VINF_SUCCESS, __FUNCTION__, pThis, &Ctx);
                            VMM_ASSERT_RELEASE_MSG_RETURN(pVM, RT_SUCCESS(rc), ("rc=%Rrc\n", rc), rc);

                            rc = SUPSemEventSignal(pVM->pSession, (SUPSEMEVENT)pThis->s.Core.hEvtWrite);

                            VMMR0EmtResumeAfterBlocking(pVCpu, &Ctx);
                        }
                        else
# endif
                            rc = SUPSemEventSignal(pVM->pSession, (SUPSEMEVENT)pThis->s.Core.hEvtWrite);
                        AssertRC(rc);
                        return rc;
                    }
                }
#endif /* IN_RING3 || IN_RING0 */
#ifndef IN_RING3
# ifdef IN_RING0
                else
# endif
                {
                    /* Queue the exit request (ring-3). */
# ifndef IN_RING0
                    PVMCPUCC    pVCpu = VMMGetCpu(pVM); AssertPtr(pVCpu);
# endif
                    uint32_t    i     = pVCpu->pdm.s.cQueuedCritSectRwShrdLeaves++;
                    LogFlow(("PDMCritSectRwLeaveShared: [%d]=%p => R3 c=%d (%#llx)\n", i, pThis, c, u64State));
                    VMM_ASSERT_RELEASE_MSG_RETURN(pVM, i < RT_ELEMENTS(pVCpu->pdm.s.apQueuedCritSectRwShrdLeaves),
                                                  ("i=%u\n", i), VERR_PDM_CRITSECTRW_IPE);
                    pVCpu->pdm.s.apQueuedCritSectRwShrdLeaves[i] = pThis->s.pSelfR3;
                    VMM_ASSERT_RELEASE_MSG_RETURN(pVM,
                                                     RT_VALID_PTR(pVCpu->pdm.s.apQueuedCritSectRwShrdLeaves[i])
                                                  &&    ((uintptr_t)pVCpu->pdm.s.apQueuedCritSectRwShrdLeaves[i] & HOST_PAGE_OFFSET_MASK)
                                                     == ((uintptr_t)pThis & HOST_PAGE_OFFSET_MASK),
                                                  ("%p vs %p\n", pVCpu->pdm.s.apQueuedCritSectRwShrdLeaves[i], pThis),
                                                  pdmCritSectRwCorrupted(pThis, "Invalid self pointer"));
                    VMCPU_FF_SET(pVCpu, VMCPU_FF_PDM_CRITSECT);
                    VMCPU_FF_SET(pVCpu, VMCPU_FF_TO_R3);
                    STAM_REL_COUNTER_INC(&pVM->pdm.s.StatQueuedCritSectLeaves);
                    STAM_REL_COUNTER_INC(&pThis->s.StatContentionRZLeaveShared);
                    break;
                }
#endif
            }

            ASMNopPause();
            if (RT_LIKELY(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC))
            { }
            else
                return VERR_SEM_DESTROYED;
            ASMNopPause();

            u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
            u64OldState = u64State;
        }
    }
    else
    {
        /*
         * Write direction. Check that it's the owner calling and that it has reads to undo.
         */
        RTNATIVETHREAD hNativeSelf = pdmCritSectRwGetNativeSelf(pVM, pThis);
        AssertReturn(hNativeSelf != NIL_RTNATIVETHREAD, VERR_VM_THREAD_NOT_EMT);

        RTNATIVETHREAD hNativeWriter;
        ASMAtomicUoReadHandle(&pThis->s.Core.u.s.hNativeWriter, &hNativeWriter);
        AssertReturn(hNativeSelf == hNativeWriter, VERR_NOT_OWNER);
        AssertReturn(pThis->s.Core.cWriterReads > 0, VERR_NOT_OWNER);
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
        if (!fNoVal)
        {
            int rc = RTLockValidatorRecExclUnwindMixed(pThis->s.Core.pValidatorWrite, &pThis->s.Core.pValidatorRead->Core);
            if (RT_FAILURE(rc))
                return rc;
        }
#endif
        uint32_t cDepth = ASMAtomicDecU32(&pThis->s.Core.cWriterReads);
        AssertReturn(cDepth < PDM_CRITSECTRW_MAX_RECURSIONS, pdmCritSectRwCorrupted(pThis, "too many writer-read recursions"));
    }

    return VINF_SUCCESS;
}


/**
 * Leave a critical section held with shared access.
 *
 * @returns VBox status code.
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwEnterShared, PDMCritSectRwTryEnterShared,
 *          PDMCritSectRwEnterSharedDebug, PDMCritSectRwTryEnterSharedDebug,
 *          PDMCritSectRwLeaveExcl, RTCritSectRwLeaveShared.
 */
VMMDECL(int) PDMCritSectRwLeaveShared(PVMCC pVM, PPDMCRITSECTRW pThis)
{
    return pdmCritSectRwLeaveSharedWorker(pVM, pThis, false /*fNoVal*/);
}


#if defined(IN_RING3) || defined(IN_RING0)
/**
 * PDMCritSectBothFF interface.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 */
void pdmCritSectRwLeaveSharedQueued(PVMCC pVM, PPDMCRITSECTRW pThis)
{
    pdmCritSectRwLeaveSharedWorker(pVM, pThis, true /*fNoVal*/);
}
#endif


/**
 * Worker for pdmCritSectRwEnterExcl that bails out on wait failure.
 *
 * @returns @a rc unless corrupted.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   rc          The status to return.
 */
DECL_NO_INLINE(static, int) pdmCritSectRwEnterExclBailOut(PPDMCRITSECTRW pThis, int rc)
{
    /*
     * Decrement the counts and return the error.
     */
    for (;;)
    {
        uint64_t       u64State    = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
        uint64_t const u64OldState = u64State;
        uint64_t c                 = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT;
        AssertReturn(c > 0, pdmCritSectRwCorrupted(pThis, "Invalid write count on bailout"));
        c--;
        u64State &= ~RTCSRW_CNT_WR_MASK;
        u64State |= c << RTCSRW_CNT_WR_SHIFT;
        if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
            return rc;

        ASMNopPause();
        AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);
        ASMNopPause();
    }
}


/**
 * Worker for pdmCritSectRwEnterExcl that handles the red tape after we've
 * gotten exclusive ownership of the critical section.
 */
DECL_FORCE_INLINE(int) pdmCritSectRwEnterExclFirst(PPDMCRITSECTRW pThis, PCRTLOCKVALSRCPOS pSrcPos,
                                                   bool fNoVal, RTTHREAD hThreadSelf)
{
    RT_NOREF(hThreadSelf, fNoVal, pSrcPos);
    Assert((PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State) & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT));

#ifdef PDMCRITSECTRW_WITH_LESS_ATOMIC_STUFF
    pThis->s.Core.cWriteRecursions = 1;
#else
    ASMAtomicWriteU32(&pThis->s.Core.cWriteRecursions, 1);
#endif
    Assert(pThis->s.Core.cWriterReads == 0);

#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
    if (!fNoVal)
    {
        if (hThreadSelf == NIL_RTTHREAD)
            hThreadSelf = RTThreadSelfAutoAdopt();
        RTLockValidatorRecExclSetOwner(pThis->s.Core.pValidatorWrite, hThreadSelf, pSrcPos, true);
    }
#endif
    STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(Stat,EnterExcl));
    STAM_PROFILE_ADV_START(&pThis->s.StatWriteLocked, swl);
    return VINF_SUCCESS;
}


#if defined(IN_RING3) || defined(IN_RING0)
/**
 * Worker for pdmCritSectRwEnterExcl that handles waiting when the section is
 * contended.
 */
static int pdmR3R0CritSectRwEnterExclContended(PVMCC pVM, PVMCPUCC pVCpu, PPDMCRITSECTRW pThis, RTNATIVETHREAD hNativeSelf,
                                               PCRTLOCKVALSRCPOS pSrcPos, bool fNoVal, int rcBusy, RTTHREAD hThreadSelf)
{
    RT_NOREF(hThreadSelf, rcBusy, pSrcPos, fNoVal, pVCpu);

    PSUPDRVSESSION const    pSession          = pVM->pSession;
    SUPSEMEVENT const       hEvent            = (SUPSEMEVENT)pThis->s.Core.hEvtWrite;
# ifdef IN_RING0
    uint64_t const          tsStart           = RTTimeNanoTS();
    uint64_t const          cNsMaxTotalDef    = RT_NS_5MIN;
    uint64_t                cNsMaxTotal       = cNsMaxTotalDef;
    uint32_t                cMsMaxOne         = RT_MS_5SEC;
    bool                    fNonInterruptible = false;
# endif

    for (uint32_t iLoop = 0; ; iLoop++)
    {
        /*
         * Wait for our turn.
         */
        int rc;
# ifdef IN_RING3
#  ifdef PDMCRITSECTRW_STRICT
        rc = RTLockValidatorRecExclCheckBlocking(pThis->s.Core.pValidatorWrite, hThreadSelf, pSrcPos, true,
                                                 RT_INDEFINITE_WAIT, RTTHREADSTATE_RW_WRITE, false);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            return pdmCritSectRwEnterExclBailOut(pThis, rc);
#  else
        RTThreadBlocking(hThreadSelf, RTTHREADSTATE_RW_WRITE, false);
#  endif
# endif

        for (;;)
        {
            /*
             * We always wait with a timeout so we can re-check the structure sanity
             * and not get stuck waiting on a corrupt or deleted section.
             */
# ifdef IN_RING3
            rc = SUPSemEventWaitNoResume(pSession, hEvent, RT_MS_5SEC);
# else
            rc = !fNonInterruptible
               ? SUPSemEventWaitNoResume(pSession, hEvent, cMsMaxOne)
               : SUPSemEventWait(pSession, hEvent, cMsMaxOne);
            Log11Func(("%p: rc=%Rrc %'RU64 ns (cMsMaxOne=%RU64 hNativeWriter=%p)\n",
                       pThis, rc, RTTimeNanoTS() - tsStart, cMsMaxOne, pThis->s.Core.u.s.hNativeWriter));
# endif
            if (RT_LIKELY(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC))
            { /* likely */ }
            else
            {
# ifdef IN_RING3
                RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
# endif
                return VERR_SEM_DESTROYED;
            }
            if (RT_LIKELY(rc == VINF_SUCCESS))
                break;

            /*
             * Timeout and interrupted waits needs careful handling in ring-0
             * because we're cooperating with ring-3 on this critical section
             * and thus need to make absolutely sure we won't get stuck here.
             *
             * The r0 interrupted case means something is pending (termination,
             * signal, APC, debugger, whatever), so we must try our best to
             * return to the caller and to ring-3 so it can be dealt with.
             */
            if (rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED)
            {
# ifdef IN_RING0
                uint64_t const cNsElapsed = RTTimeNanoTS() - tsStart;
                int const      rcTerm     = RTThreadQueryTerminationStatus(NIL_RTTHREAD);
                AssertMsg(rcTerm == VINF_SUCCESS || rcTerm == VERR_NOT_SUPPORTED || rcTerm == VINF_THREAD_IS_TERMINATING,
                          ("rcTerm=%Rrc\n", rcTerm));
                if (rcTerm == VERR_NOT_SUPPORTED && cNsMaxTotal == cNsMaxTotalDef)
                    cNsMaxTotal = RT_NS_1MIN;

                if (rc == VERR_TIMEOUT)
                {
                    /* Try return get out of here with a non-VINF_SUCCESS status if
                       the thread is terminating or if the timeout has been exceeded. */
                    STAM_REL_COUNTER_INC(&pVM->pdm.s.StatCritSectRwExclVerrTimeout);
                    if (   rcTerm == VINF_THREAD_IS_TERMINATING
                        || cNsElapsed > cNsMaxTotal)
                        return pdmCritSectRwEnterExclBailOut(pThis, rcBusy != VINF_SUCCESS ? rcBusy : rc);
                }
                else
                {
                    /* For interrupt cases, we must return if we can.  If rcBusy is VINF_SUCCESS,
                       we will try non-interruptible sleep for a while to help resolve the issue
                       w/o guru'ing. */
                    STAM_REL_COUNTER_INC(&pVM->pdm.s.StatCritSectRwExclVerrInterrupted);
                    if (   rcTerm != VINF_THREAD_IS_TERMINATING
                        && rcBusy == VINF_SUCCESS
                        && pVCpu != NULL
                        && cNsElapsed <= cNsMaxTotal)
                    {
                        if (!fNonInterruptible)
                        {
                            STAM_REL_COUNTER_INC(&pVM->pdm.s.StatCritSectRwExclNonInterruptibleWaits);
                            fNonInterruptible   = true;
                            cMsMaxOne           = 32;
                            uint64_t cNsLeft = cNsMaxTotal - cNsElapsed;
                            if (cNsLeft > RT_NS_10SEC)
                                cNsMaxTotal = cNsElapsed + RT_NS_10SEC;
                        }
                    }
                    else
                        return pdmCritSectRwEnterExclBailOut(pThis, rcBusy != VINF_SUCCESS ? rcBusy : rc);
                }
# else  /* IN_RING3 */
                RT_NOREF(pVM, pVCpu, rcBusy);
# endif /* IN_RING3 */
            }
            /*
             * Any other return code is fatal.
             */
            else
            {
# ifdef IN_RING3
                RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
# endif
                AssertMsgFailed(("rc=%Rrc\n", rc));
                return RT_FAILURE_NP(rc) ? rc : -rc;
            }
        }

# ifdef IN_RING3
        RTThreadUnblocked(hThreadSelf, RTTHREADSTATE_RW_WRITE);
# endif

        /*
         * Try take exclusive write ownership.
         */
        uint64_t u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
        if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT))
        {
            bool fDone;
            ASMAtomicCmpXchgHandle(&pThis->s.Core.u.s.hNativeWriter, hNativeSelf, NIL_RTNATIVETHREAD, fDone);
            if (fDone)
                return pdmCritSectRwEnterExclFirst(pThis, pSrcPos, fNoVal, hThreadSelf);
        }
        AssertMsg(iLoop < 1000, ("%u\n", iLoop)); /* may loop a few times here... */
    }
}
#endif /* IN_RING3 || IN_RING0 */


/**
 * Worker that enters a read/write critical section with exclusive access.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   rcBusy      The busy return code for ring-0 and ring-3.
 * @param   fTryOnly    Only try enter it, don't wait.
 * @param   pSrcPos     The source position. (Can be NULL.)
 * @param   fNoVal      No validation records.
 */
static int pdmCritSectRwEnterExcl(PVMCC pVM, PPDMCRITSECTRW pThis, int rcBusy, bool fTryOnly,
                                  PCRTLOCKVALSRCPOS pSrcPos, bool fNoVal)
{
    /*
     * Validate input.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);

    RTTHREAD hThreadSelf = NIL_RTTHREAD;
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
    if (!fTryOnly)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        int rc9 = RTLockValidatorRecExclCheckOrder(pThis->s.Core.pValidatorWrite, hThreadSelf, pSrcPos, RT_INDEFINITE_WAIT);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif

    /*
     * Check if we're already the owner and just recursing.
     */
    RTNATIVETHREAD const hNativeSelf = pdmCritSectRwGetNativeSelf(pVM, pThis);
    AssertReturn(hNativeSelf != NIL_RTNATIVETHREAD, VERR_VM_THREAD_NOT_EMT);
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->s.Core.u.s.hNativeWriter, &hNativeWriter);
    if (hNativeSelf == hNativeWriter)
    {
        Assert((PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State) & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT));
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
        if (!fNoVal)
        {
            int rc9 = RTLockValidatorRecExclRecursion(pThis->s.Core.pValidatorWrite, pSrcPos);
            if (RT_FAILURE(rc9))
                return rc9;
        }
#endif
        STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(Stat,EnterExcl));
#ifdef PDMCRITSECTRW_WITH_LESS_ATOMIC_STUFF
        uint32_t const cDepth = ++pThis->s.Core.cWriteRecursions;
#else
        uint32_t const cDepth = ASMAtomicIncU32(&pThis->s.Core.cWriteRecursions);
#endif
        AssertReturnStmt(cDepth > 1 && cDepth <= PDM_CRITSECTRW_MAX_RECURSIONS,
                         ASMAtomicDecU32(&pThis->s.Core.cWriteRecursions),
                         VERR_PDM_CRITSECTRW_TOO_MANY_RECURSIONS);
        return VINF_SUCCESS;
    }

    /*
     * First we try grab an idle critical section using 128-bit atomics.
     */
    /** @todo This could be moved up before the recursion check. */
    uint64_t u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
#ifdef RTASM_HAVE_CMP_WRITE_U128
    if (   (u64State & ~RTCSRW_DIR_MASK) == 0
        && pdmCritSectRwIsCmpWriteU128Supported())
    {
        RTCRITSECTRWSTATE OldState;
        OldState.s.u64State      = u64State;
        OldState.s.hNativeWriter = NIL_RTNATIVETHREAD;
        AssertCompile(sizeof(OldState.s.hNativeWriter) == sizeof(OldState.u128.s.Lo));

        RTCRITSECTRWSTATE NewState;
        NewState.s.u64State      = (UINT64_C(1) << RTCSRW_CNT_WR_SHIFT) | (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT);
        NewState.s.hNativeWriter = hNativeSelf;

        if (ASMAtomicCmpWriteU128U(&pThis->s.Core.u.u128, NewState.u128, OldState.u128))
            return pdmCritSectRwEnterExclFirst(pThis, pSrcPos, fNoVal, hThreadSelf);

        u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
    }
#endif

    /*
     * Do it step by step.  Update the state to reflect our desire.
     */
    uint64_t u64OldState = u64State;

    for (;;)
    {
        if (   (u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT)
            || (u64State & (RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK)) != 0)
        {
            /* It flows in the right direction, try follow it before it changes. */
            uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT;
            AssertReturn(c < RTCSRW_CNT_MASK, VERR_PDM_CRITSECTRW_TOO_MANY_WRITERS);
            c++;
            Assert(c < RTCSRW_CNT_WR_MASK / 4);
            u64State &= ~RTCSRW_CNT_WR_MASK;
            u64State |= c << RTCSRW_CNT_WR_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                break;
        }
        else if ((u64State & (RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK)) == 0)
        {
            /* Wrong direction, but we're alone here and can simply try switch the direction. */
            u64State &= ~(RTCSRW_CNT_RD_MASK | RTCSRW_CNT_WR_MASK | RTCSRW_DIR_MASK);
            u64State |= (UINT64_C(1) << RTCSRW_CNT_WR_SHIFT) | (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                break;
        }
        else if (fTryOnly)
        {
            /* Wrong direction and we're not supposed to wait, just return. */
            STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(StatContention,EnterExcl));
            return VERR_SEM_BUSY;
        }
        else
        {
            /* Add ourselves to the write count and break out to do the wait. */
            uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT;
            AssertReturn(c < RTCSRW_CNT_MASK, VERR_PDM_CRITSECTRW_TOO_MANY_WRITERS);
            c++;
            Assert(c < RTCSRW_CNT_WR_MASK / 4);
            u64State &= ~RTCSRW_CNT_WR_MASK;
            u64State |= c << RTCSRW_CNT_WR_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                break;
        }

        ASMNopPause();

        if (pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC)
        { /* likely */ }
        else
            return VERR_SEM_DESTROYED;

        ASMNopPause();
        u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
        u64OldState = u64State;
    }

    /*
     * If we're in write mode now try grab the ownership. Play fair if there
     * are threads already waiting.
     */
    bool fDone = (u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT)
              && (  ((u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT) == 1
                  || fTryOnly);
    if (fDone)
    {
        ASMAtomicCmpXchgHandle(&pThis->s.Core.u.s.hNativeWriter, hNativeSelf, NIL_RTNATIVETHREAD, fDone);
        if (fDone)
            return pdmCritSectRwEnterExclFirst(pThis, pSrcPos, fNoVal, hThreadSelf);
    }

    /*
     * Okay, we have contention and will have to wait unless we're just trying.
     */
    if (fTryOnly)
    {
        STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(StatContention,EnterExcl)); /** @todo different statistics for this */
        return pdmCritSectRwEnterExclBailOut(pThis, VERR_SEM_BUSY);
    }

    STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(StatContention,EnterExcl));

    /*
     * Ring-3 is pretty straight forward.
     */
#if defined(IN_RING3) && defined(PDMCRITSECTRW_STRICT)
    return pdmR3R0CritSectRwEnterExclContended(pVM, NULL, pThis, hNativeSelf, pSrcPos, fNoVal, rcBusy, hThreadSelf);
#elif defined(IN_RING3)
    return pdmR3R0CritSectRwEnterExclContended(pVM, NULL, pThis, hNativeSelf, pSrcPos, fNoVal, rcBusy, RTThreadSelf());

#elif defined(IN_RING0)
    /*
     * In ring-0 context we have to take the special VT-x/AMD-V HM context into
     * account when waiting on contended locks.
     */
    PVMCPUCC pVCpu = VMMGetCpu(pVM);
    if (pVCpu)
    {
        VMMR0EMTBLOCKCTX Ctx;
        int rc = VMMR0EmtPrepareToBlock(pVCpu, rcBusy, __FUNCTION__, pThis, &Ctx);
        if (rc == VINF_SUCCESS)
        {
            Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));

            rc = pdmR3R0CritSectRwEnterExclContended(pVM, pVCpu, pThis, hNativeSelf, pSrcPos, fNoVal, rcBusy, NIL_RTTHREAD);

            VMMR0EmtResumeAfterBlocking(pVCpu, &Ctx);
        }
        else
        {
            //STAM_REL_COUNTER_INC(&pThis->s.StatContentionRZLockBusy);
            rc = pdmCritSectRwEnterExclBailOut(pThis, rc);
        }
        return rc;
    }

    /* Non-EMT. */
    Assert(RTThreadPreemptIsEnabled(NIL_RTTHREAD));
    return pdmR3R0CritSectRwEnterExclContended(pVM, NULL, pThis, hNativeSelf, pSrcPos, fNoVal, rcBusy, NIL_RTTHREAD);

#else
# error "Unused."
    /*
     * Raw-mode: Call host and take it there if rcBusy is VINF_SUCCESS.
     */
    rcBusy = pdmCritSectRwEnterExclBailOut(pThis, rcBusy);
    if (rcBusy == VINF_SUCCESS)
    {
        Assert(!fTryOnly);
        PVMCPUCC  pVCpu = VMMGetCpu(pVM); AssertPtr(pVCpu);
        /** @todo Should actually do this in via VMMR0.cpp instead of going all the way
         *        back to ring-3. Goes for both kind of crit sects. */
        return VMMRZCallRing3(pVM, pVCpu, VMMCALLRING3_PDM_CRIT_SECT_RW_ENTER_EXCL, MMHyperCCToR3(pVM, pThis));
    }
    return rcBusy;
#endif
}


/**
 * Try enter a critical section with exclusive (write) access.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  rcBusy if in ring-0 or raw-mode context and it is busy.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   rcBusy      The status code to return when we're in RC or R0 and the
 *                      section is busy.   Pass VINF_SUCCESS to acquired the
 *                      critical section thru a ring-3 call if necessary.
 * @sa      PDMCritSectRwEnterExclDebug, PDMCritSectRwTryEnterExcl,
 *          PDMCritSectRwTryEnterExclDebug,
 *          PDMCritSectEnterDebug, PDMCritSectEnter,
 * RTCritSectRwEnterExcl.
 */
VMMDECL(int) PDMCritSectRwEnterExcl(PVMCC pVM, PPDMCRITSECTRW pThis, int rcBusy)
{
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterExcl(pVM, pThis, rcBusy, false /*fTryAgain*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return pdmCritSectRwEnterExcl(pVM, pThis, rcBusy, false /*fTryAgain*/, &SrcPos, false /*fNoVal*/);
#endif
}


/**
 * Try enter a critical section with exclusive (write) access.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  rcBusy if in ring-0 or raw-mode context and it is busy.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   rcBusy      The status code to return when we're in RC or R0 and the
 *                      section is busy.   Pass VINF_SUCCESS to acquired the
 *                      critical section thru a ring-3 call if necessary.
 * @param   uId         Where we're entering the section.
 * @param   SRC_POS     The source position.
 * @sa      PDMCritSectRwEnterExcl, PDMCritSectRwTryEnterExcl,
 *          PDMCritSectRwTryEnterExclDebug,
 *          PDMCritSectEnterDebug, PDMCritSectEnter,
 *          RTCritSectRwEnterExclDebug.
 */
VMMDECL(int) PDMCritSectRwEnterExclDebug(PVMCC pVM, PPDMCRITSECTRW pThis, int rcBusy, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    NOREF(uId); NOREF(pszFile); NOREF(iLine); NOREF(pszFunction);
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterExcl(pVM, pThis, rcBusy, false /*fTryAgain*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return pdmCritSectRwEnterExcl(pVM, pThis, rcBusy, false /*fTryAgain*/, &SrcPos, false /*fNoVal*/);
#endif
}


/**
 * Try enter a critical section with exclusive (write) access.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_BUSY if the critsect was owned.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwEnterExcl, PDMCritSectRwTryEnterExclDebug,
 *          PDMCritSectRwEnterExclDebug,
 *          PDMCritSectTryEnter, PDMCritSectTryEnterDebug,
 *          RTCritSectRwTryEnterExcl.
 */
VMMDECL(int) PDMCritSectRwTryEnterExcl(PVMCC pVM, PPDMCRITSECTRW pThis)
{
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterExcl(pVM, pThis, VERR_SEM_BUSY, true /*fTryAgain*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_NORMAL_API();
    return pdmCritSectRwEnterExcl(pVM, pThis, VERR_SEM_BUSY, true /*fTryAgain*/, &SrcPos, false /*fNoVal*/);
#endif
}


/**
 * Try enter a critical section with exclusive (write) access.
 *
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_SEM_BUSY if the critsect was owned.
 * @retval  VERR_SEM_NESTED if nested enter on a no nesting section. (Asserted.)
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   uId         Where we're entering the section.
 * @param   SRC_POS     The source position.
 * @sa      PDMCritSectRwTryEnterExcl, PDMCritSectRwEnterExcl,
 *          PDMCritSectRwEnterExclDebug,
 *          PDMCritSectTryEnterDebug, PDMCritSectTryEnter,
 *          RTCritSectRwTryEnterExclDebug.
 */
VMMDECL(int) PDMCritSectRwTryEnterExclDebug(PVMCC pVM, PPDMCRITSECTRW pThis, RTHCUINTPTR uId, RT_SRC_POS_DECL)
{
    NOREF(uId); NOREF(pszFile); NOREF(iLine); NOREF(pszFunction);
#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    return pdmCritSectRwEnterExcl(pVM, pThis, VERR_SEM_BUSY, true /*fTryAgain*/, NULL,    false /*fNoVal*/);
#else
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_DEBUG_API();
    return pdmCritSectRwEnterExcl(pVM, pThis, VERR_SEM_BUSY, true /*fTryAgain*/, &SrcPos, false /*fNoVal*/);
#endif
}


#ifdef IN_RING3
/**
 * Enters a PDM read/write critical section with exclusive (write) access.
 *
 * @returns VINF_SUCCESS if entered successfully.
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   fCallRing3  Whether this is a VMMRZCallRing3()request.
 */
VMMR3DECL(int) PDMR3CritSectRwEnterExclEx(PVM pVM, PPDMCRITSECTRW pThis, bool fCallRing3)
{
    return pdmCritSectRwEnterExcl(pVM, pThis, VERR_SEM_BUSY, false /*fTryAgain*/, NULL, fCallRing3 /*fNoVal*/);
}
#endif /* IN_RING3 */


/**
 * Leave a critical section held exclusively.
 *
 * @returns VBox status code.
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   fNoVal      No validation records (i.e. queued release).
 * @sa      PDMCritSectRwLeaveShared, RTCritSectRwLeaveExcl.
 */
static int pdmCritSectRwLeaveExclWorker(PVMCC pVM, PPDMCRITSECTRW pThis, bool fNoVal)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, VERR_SEM_DESTROYED);

#if !defined(PDMCRITSECTRW_STRICT) || !defined(IN_RING3)
    NOREF(fNoVal);
#endif

    /*
     * Check ownership.
     */
    RTNATIVETHREAD hNativeSelf = pdmCritSectRwGetNativeSelf(pVM, pThis);
    AssertReturn(hNativeSelf != NIL_RTNATIVETHREAD, VERR_VM_THREAD_NOT_EMT);

    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->s.Core.u.s.hNativeWriter, &hNativeWriter);
    AssertReturn(hNativeSelf == hNativeWriter, VERR_NOT_OWNER);


    /*
     * Unwind one recursion. Not the last?
     */
    if (pThis->s.Core.cWriteRecursions != 1)
    {
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
        if (fNoVal)
            Assert(pThis->s.Core.pValidatorWrite->hThread == NIL_RTTHREAD);
        else
        {
            int rc9 = RTLockValidatorRecExclUnwind(pThis->s.Core.pValidatorWrite);
            if (RT_FAILURE(rc9))
                return rc9;
        }
#endif
#ifdef PDMCRITSECTRW_WITH_LESS_ATOMIC_STUFF
        uint32_t const cDepth = --pThis->s.Core.cWriteRecursions;
#else
        uint32_t const cDepth = ASMAtomicDecU32(&pThis->s.Core.cWriteRecursions);
#endif
        AssertReturn(cDepth != 0 && cDepth < UINT32_MAX, pdmCritSectRwCorrupted(pThis, "Invalid write recursion value on leave"));
        return VINF_SUCCESS;
    }


    /*
     * Final recursion.
     */
    AssertReturn(pThis->s.Core.cWriterReads == 0, VERR_WRONG_ORDER); /* (must release all read recursions before the final write.) */
#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
    if (fNoVal)
        Assert(pThis->s.Core.pValidatorWrite->hThread == NIL_RTTHREAD);
    else
    {
        int rc9 = RTLockValidatorRecExclReleaseOwner(pThis->s.Core.pValidatorWrite, true);
        if (RT_FAILURE(rc9))
            return rc9;
    }
#endif


#ifdef RTASM_HAVE_CMP_WRITE_U128
    /*
     * See if we can get out w/o any signalling as this is a common case.
     */
    if (pdmCritSectRwIsCmpWriteU128Supported())
    {
        RTCRITSECTRWSTATE OldState;
        OldState.s.u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
        if (OldState.s.u64State == ((UINT64_C(1) << RTCSRW_CNT_WR_SHIFT) | (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT)))
        {
            OldState.s.hNativeWriter = hNativeSelf;
            AssertCompile(sizeof(OldState.s.hNativeWriter) == sizeof(OldState.u128.s.Lo));

            RTCRITSECTRWSTATE NewState;
            NewState.s.u64State      = RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT;
            NewState.s.hNativeWriter = NIL_RTNATIVETHREAD;

# ifdef PDMCRITSECTRW_WITH_LESS_ATOMIC_STUFF
            pThis->s.Core.cWriteRecursions = 0;
# else
            ASMAtomicWriteU32(&pThis->s.Core.cWriteRecursions, 0);
# endif
            STAM_PROFILE_ADV_STOP(&pThis->s.StatWriteLocked, swl);

            if (ASMAtomicCmpWriteU128U(&pThis->s.Core.u.u128, NewState.u128, OldState.u128))
                return VINF_SUCCESS;

            /* bail out. */
            pThis->s.Core.cWriteRecursions = 1;
        }
    }
#endif /* RTASM_HAVE_CMP_WRITE_U128 */


#if defined(IN_RING3) || defined(IN_RING0)
    /*
     * Ring-3: Straight forward, just update the state and if necessary signal waiters.
     * Ring-0: Try leave for real, depends on host and context.
     */
# ifdef IN_RING0
    Assert(RTSemEventIsSignalSafe() == RTSemEventMultiIsSignalSafe());
    PVMCPUCC pVCpu = VMMGetCpu(pVM);
    if (   pVCpu == NULL /* non-EMT access, if we implement it must be able to block */
        || VMMRZCallRing3IsEnabled(pVCpu)
        || RTSemEventIsSignalSafe()
        || (   VMMR0ThreadCtxHookIsEnabled(pVCpu)       /* Doesn't matter if Signal() blocks if we have hooks, ... */
            && RTThreadPreemptIsEnabled(NIL_RTTHREAD)   /* ... and preemption is still enabled, */
            && ASMIntAreEnabled())                      /* ... and interrupts hasn't yet been disabled. Special pre-GC HM env. */
       )
# endif
    {
# ifdef PDMCRITSECTRW_WITH_LESS_ATOMIC_STUFF
        pThis->s.Core.cWriteRecursions = 0;
# else
        ASMAtomicWriteU32(&pThis->s.Core.cWriteRecursions, 0);
# endif
        STAM_PROFILE_ADV_STOP(&pThis->s.StatWriteLocked, swl);
        ASMAtomicWriteHandle(&pThis->s.Core.u.s.hNativeWriter, NIL_RTNATIVETHREAD);

        for (;;)
        {
            uint64_t u64State    = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
            uint64_t u64OldState = u64State;

            uint64_t c = (u64State & RTCSRW_CNT_WR_MASK) >> RTCSRW_CNT_WR_SHIFT;
            AssertReturn(c > 0, pdmCritSectRwCorrupted(pThis, "Invalid write count on leave"));
            c--;

            if (   c > 0
                || (u64State & RTCSRW_CNT_RD_MASK) == 0)
            {
                /*
                 * Don't change the direction, wake up the next writer if any.
                 */
                u64State &= ~RTCSRW_CNT_WR_MASK;
                u64State |= c << RTCSRW_CNT_WR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                {
                    STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(StatContention,LeaveExcl));
                    int rc;
                    if (c == 0)
                        rc = VINF_SUCCESS;
# ifdef IN_RING0
                    else if (!RTSemEventIsSignalSafe() && pVCpu != NULL)
                    {
                        VMMR0EMTBLOCKCTX Ctx;
                        rc = VMMR0EmtPrepareToBlock(pVCpu, VINF_SUCCESS, __FUNCTION__, pThis, &Ctx);
                        VMM_ASSERT_RELEASE_MSG_RETURN(pVM, RT_SUCCESS(rc), ("rc=%Rrc\n", rc), rc);

                        rc = SUPSemEventSignal(pVM->pSession, (SUPSEMEVENT)pThis->s.Core.hEvtWrite);

                        VMMR0EmtResumeAfterBlocking(pVCpu, &Ctx);
                    }
# endif
                    else
                        rc = SUPSemEventSignal(pVM->pSession, (SUPSEMEVENT)pThis->s.Core.hEvtWrite);
                    AssertRC(rc);
                    return rc;
                }
            }
            else
            {
                /*
                 * Reverse the direction and signal the reader threads.
                 */
                u64State &= ~(RTCSRW_CNT_WR_MASK | RTCSRW_DIR_MASK);
                u64State |= RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT;
                if (ASMAtomicCmpXchgU64(&pThis->s.Core.u.s.u64State, u64State, u64OldState))
                {
                    Assert(!pThis->s.Core.fNeedReset);
                    ASMAtomicWriteBool(&pThis->s.Core.fNeedReset, true);
                    STAM_REL_COUNTER_INC(&pThis->s.CTX_MID_Z(StatContention,LeaveExcl));

                    int rc;
# ifdef IN_RING0
                    if (!RTSemEventMultiIsSignalSafe() && pVCpu != NULL)
                    {
                        VMMR0EMTBLOCKCTX Ctx;
                        rc = VMMR0EmtPrepareToBlock(pVCpu, VINF_SUCCESS, __FUNCTION__, pThis, &Ctx);
                        VMM_ASSERT_RELEASE_MSG_RETURN(pVM, RT_SUCCESS(rc), ("rc=%Rrc\n", rc), rc);

                        rc = SUPSemEventMultiSignal(pVM->pSession, (SUPSEMEVENTMULTI)pThis->s.Core.hEvtRead);

                        VMMR0EmtResumeAfterBlocking(pVCpu, &Ctx);
                    }
                    else
# endif
                        rc = SUPSemEventMultiSignal(pVM->pSession, (SUPSEMEVENTMULTI)pThis->s.Core.hEvtRead);
                    AssertRC(rc);
                    return rc;
                }
            }

            ASMNopPause();
            if (pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC)
            { /*likely*/ }
            else
                return VERR_SEM_DESTROYED;
            ASMNopPause();
        }
        /* not reached! */
    }
#endif /* IN_RING3 || IN_RING0 */


#ifndef IN_RING3
    /*
     * Queue the requested exit for ring-3 execution.
     */
# ifndef IN_RING0
    PVMCPUCC    pVCpu = VMMGetCpu(pVM); AssertPtr(pVCpu);
# endif
    uint32_t    i     = pVCpu->pdm.s.cQueuedCritSectRwExclLeaves++;
    LogFlow(("PDMCritSectRwLeaveShared: [%d]=%p => R3\n", i, pThis));
    VMM_ASSERT_RELEASE_MSG_RETURN(pVM, i < RT_ELEMENTS(pVCpu->pdm.s.apQueuedCritSectRwExclLeaves),
                                  ("i=%u\n", i), VERR_PDM_CRITSECTRW_IPE);
    pVCpu->pdm.s.apQueuedCritSectRwExclLeaves[i] = pThis->s.pSelfR3;
    VMM_ASSERT_RELEASE_MSG_RETURN(pVM,
                                     RT_VALID_PTR(pVCpu->pdm.s.apQueuedCritSectRwExclLeaves[i])
                                  &&    ((uintptr_t)pVCpu->pdm.s.apQueuedCritSectRwExclLeaves[i] & HOST_PAGE_OFFSET_MASK)
                                     == ((uintptr_t)pThis & HOST_PAGE_OFFSET_MASK),
                                  ("%p vs %p\n", pVCpu->pdm.s.apQueuedCritSectRwExclLeaves[i], pThis),
                                  pdmCritSectRwCorrupted(pThis, "Invalid self pointer on queue (excl)"));
    VMCPU_FF_SET(pVCpu, VMCPU_FF_PDM_CRITSECT);
    VMCPU_FF_SET(pVCpu, VMCPU_FF_TO_R3);
    STAM_REL_COUNTER_INC(&pVM->pdm.s.StatQueuedCritSectLeaves);
    STAM_REL_COUNTER_INC(&pThis->s.StatContentionRZLeaveExcl);
    return VINF_SUCCESS;
#endif
}


/**
 * Leave a critical section held exclusively.
 *
 * @returns VBox status code.
 * @retval  VERR_SEM_DESTROYED if the critical section is delete before or
 *          during the operation.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwLeaveShared, RTCritSectRwLeaveExcl.
 */
VMMDECL(int) PDMCritSectRwLeaveExcl(PVMCC pVM, PPDMCRITSECTRW pThis)
{
    return pdmCritSectRwLeaveExclWorker(pVM, pThis, false /*fNoVal*/);
}


#if defined(IN_RING3) || defined(IN_RING0)
/**
 * PDMCritSectBothFF interface.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 */
void pdmCritSectRwLeaveExclQueued(PVMCC pVM, PPDMCRITSECTRW pThis)
{
    pdmCritSectRwLeaveExclWorker(pVM, pThis, true /*fNoVal*/);
}
#endif


/**
 * Checks the caller is the exclusive (write) owner of the critical section.
 *
 * @retval  true if owner.
 * @retval  false if not owner.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwIsReadOwner, PDMCritSectIsOwner,
 *          RTCritSectRwIsWriteOwner.
 */
VMMDECL(bool) PDMCritSectRwIsWriteOwner(PVMCC pVM, PPDMCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, false);

    /*
     * Check ownership.
     */
    RTNATIVETHREAD hNativeWriter;
    ASMAtomicUoReadHandle(&pThis->s.Core.u.s.hNativeWriter, &hNativeWriter);
    if (hNativeWriter == NIL_RTNATIVETHREAD)
        return false;
    return hNativeWriter == pdmCritSectRwGetNativeSelf(pVM, pThis);
}


/**
 * Checks if the caller is one of the read owners of the critical section.
 *
 * @note    !CAUTION!  This API doesn't work reliably if lock validation isn't
 *          enabled. Meaning, the answer is not trustworhty unless
 *          RT_LOCK_STRICT or PDMCRITSECTRW_STRICT was defined at build time.
 *          Also, make sure you do not use RTCRITSECTRW_FLAGS_NO_LOCK_VAL when
 *          creating the semaphore.  And finally, if you used a locking class,
 *          don't disable deadlock detection by setting cMsMinDeadlock to
 *          RT_INDEFINITE_WAIT.
 *
 *          In short, only use this for assertions.
 *
 * @returns @c true if reader, @c false if not.
 * @param   pVM         The cross context VM structure.
 * @param   pThis       Pointer to the read/write critical section.
 * @param   fWannaHear  What you'd like to hear when lock validation is not
 *                      available.  (For avoiding asserting all over the place.)
 * @sa      PDMCritSectRwIsWriteOwner, RTCritSectRwIsReadOwner.
 */
VMMDECL(bool) PDMCritSectRwIsReadOwner(PVMCC pVM, PPDMCRITSECTRW pThis, bool fWannaHear)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, false);

    /*
     * Inspect the state.
     */
    uint64_t u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
    if ((u64State & RTCSRW_DIR_MASK) == (RTCSRW_DIR_WRITE << RTCSRW_DIR_SHIFT))
    {
        /*
         * It's in write mode, so we can only be a reader if we're also the
         * current writer.
         */
        RTNATIVETHREAD hWriter;
        ASMAtomicUoReadHandle(&pThis->s.Core.u.s.hNativeWriter, &hWriter);
        if (hWriter == NIL_RTNATIVETHREAD)
            return false;
        return hWriter == pdmCritSectRwGetNativeSelf(pVM, pThis);
    }

    /*
     * Read mode.  If there are no current readers, then we cannot be a reader.
     */
    if (!(u64State & RTCSRW_CNT_RD_MASK))
        return false;

#if defined(PDMCRITSECTRW_STRICT) && defined(IN_RING3)
    /*
     * Ask the lock validator.
     * Note! It doesn't know everything, let's deal with that if it becomes an issue...
     */
    NOREF(fWannaHear);
    return RTLockValidatorRecSharedIsOwner(pThis->s.Core.pValidatorRead, NIL_RTTHREAD);
#else
    /*
     * Ok, we don't know, just tell the caller what he want to hear.
     */
    return fWannaHear;
#endif
}


/**
 * Gets the write recursion count.
 *
 * @returns The write recursion count (0 if bad critsect).
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwGetWriterReadRecursion, PDMCritSectRwGetReadCount,
 *          RTCritSectRwGetWriteRecursion.
 */
VMMDECL(uint32_t) PDMCritSectRwGetWriteRecursion(PPDMCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    return pThis->s.Core.cWriteRecursions;
}


/**
 * Gets the read recursion count of the current writer.
 *
 * @returns The read recursion count (0 if bad critsect).
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwGetWriteRecursion, PDMCritSectRwGetReadCount,
 *          RTCritSectRwGetWriterReadRecursion.
 */
VMMDECL(uint32_t) PDMCritSectRwGetWriterReadRecursion(PPDMCRITSECTRW pThis)
{
    /*
     * Validate handle.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    return pThis->s.Core.cWriterReads;
}


/**
 * Gets the current number of reads.
 *
 * This includes all read recursions, so it might be higher than the number of
 * read owners.  It does not include reads done by the current writer.
 *
 * @returns The read count (0 if bad critsect).
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectRwGetWriteRecursion, PDMCritSectRwGetWriterReadRecursion,
 *          RTCritSectRwGetReadCount.
 */
VMMDECL(uint32_t) PDMCritSectRwGetReadCount(PPDMCRITSECTRW pThis)
{
    /*
     * Validate input.
     */
    AssertPtr(pThis);
    AssertReturn(pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC, 0);

    /*
     * Return the requested data.
     */
    uint64_t u64State = PDMCRITSECTRW_READ_STATE(&pThis->s.Core.u.s.u64State);
    if ((u64State & RTCSRW_DIR_MASK) != (RTCSRW_DIR_READ << RTCSRW_DIR_SHIFT))
        return 0;
    return (u64State & RTCSRW_CNT_RD_MASK) >> RTCSRW_CNT_RD_SHIFT;
}


/**
 * Checks if the read/write critical section is initialized or not.
 *
 * @retval  true if initialized.
 * @retval  false if not initialized.
 * @param   pThis       Pointer to the read/write critical section.
 * @sa      PDMCritSectIsInitialized, RTCritSectRwIsInitialized.
 */
VMMDECL(bool) PDMCritSectRwIsInitialized(PCPDMCRITSECTRW pThis)
{
    AssertPtr(pThis);
    return pThis->s.Core.u32Magic == RTCRITSECTRW_MAGIC;
}

