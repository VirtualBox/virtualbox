/* $Id: semxroads-generic.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTSemXRoads, generic implementation.
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
#define RTASSERT_QUIET
#include <iprt/semaphore.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/thread.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct RTSEMXROADSINTERNAL
{
    /** Magic value (RTSEMXROADS_MAGIC).  */
    uint32_t volatile   u32Magic;
    uint32_t            u32Padding; /**< alignment padding.*/
    /* The state variable.
     * All accesses are atomic and it bits are defined like this:
     *      Bits 0..14  - cNorthSouth.
     *      Bit 15      - Unused.
     *      Bits 16..31 - cEastWest.
     *      Bit 31      - fDirection; 0=NS, 1=EW.
     *      Bits 32..46 - cWaitingNS
     *      Bit 47      - Unused.
     *      Bits 48..62 - cWaitingEW
     *      Bit 63      - Unused.
     */
    uint64_t volatile   u64State;
    /** Per-direction data. */
    struct
    {
        /** What the north/south bound threads are blocking on when waiting for
         * east/west traffic to stop. */
        RTSEMEVENTMULTI     hEvt;
        /** Indicates whether the semaphore needs resetting. */
        bool volatile       fNeedReset;
    } aDirs[2];
} RTSEMXROADSINTERNAL;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define RTSEMXROADS_CNT_BITS            15
#define RTSEMXROADS_CNT_MASK            UINT64_C(0x00007fff)

#define RTSEMXROADS_CNT_NS_SHIFT        0
#define RTSEMXROADS_CNT_NS_MASK         (RTSEMXROADS_CNT_MASK << RTSEMXROADS_CNT_NS_SHIFT)
#define RTSEMXROADS_CNT_EW_SHIFT        16
#define RTSEMXROADS_CNT_EW_MASK         (RTSEMXROADS_CNT_MASK << RTSEMXROADS_CNT_EW_SHIFT)
#define RTSEMXROADS_DIR_SHIFT           31
#define RTSEMXROADS_DIR_MASK            RT_BIT_64(RTSEMXROADS_DIR_SHIFT)

#define RTSEMXROADS_WAIT_CNT_NS_SHIFT   32
#define RTSEMXROADS_WAIT_CNT_NS_MASK    (RTSEMXROADS_CNT_MASK << RTSEMXROADS_WAIT_CNT_NS_SHIFT)
#define RTSEMXROADS_WAIT_CNT_EW_SHIFT   48
#define RTSEMXROADS_WAIT_CNT_EW_MASK    (RTSEMXROADS_CNT_MASK << RTSEMXROADS_WAIT_CNT_EW_SHIFT)


#if 0 /* debugging aid */
static uint32_t volatile g_iHist = 0;
static struct
{
    void *tsc;
    RTTHREAD hThread;
    uint32_t line;
    bool fDir;
    void *u64State;
    void *u64OldState;
    bool fNeedResetNS;
    bool fNeedResetEW;
    const char *psz;
} g_aHist[256];

# define add_hist(ns, os, dir, what)  \
    do \
    { \
        uint32_t i = (ASMAtomicIncU32(&g_iHist) - 1) % RT_ELEMENTS(g_aHist);\
        g_aHist[i].line         = __LINE__; \
        g_aHist[i].u64OldState  = (void *)(os); \
        g_aHist[i].u64State     = (void *)(ns); \
        g_aHist[i].fDir         = (dir); \
        g_aHist[i].psz          = (what); \
        g_aHist[i].fNeedResetNS = pThis->aDirs[0].fNeedReset; \
        g_aHist[i].fNeedResetEW = pThis->aDirs[1].fNeedReset; \
        g_aHist[i].hThread      = RTThreadSelf(); \
        g_aHist[i].tsc          = (void *)ASMReadTSC(); \
    } while (0)

# undef DECL_FORCE_INLINE
# define DECL_FORCE_INLINE(type) static type
#else
# define add_hist(ns, os, dir, what)  do { } while (0)
#endif


RTDECL(int) RTSemXRoadsCreate(PRTSEMXROADS phXRoads)
{
    RTSEMXROADSINTERNAL *pThis = (RTSEMXROADSINTERNAL *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    int rc = RTSemEventMultiCreate(&pThis->aDirs[0].hEvt);
    if (RT_SUCCESS(rc))
    {
        rc = RTSemEventMultiCreate(&pThis->aDirs[1].hEvt);
        if (RT_SUCCESS(rc))
        {
            pThis->u32Magic            = RTSEMXROADS_MAGIC;
            pThis->u32Padding          = 0;
            pThis->u64State            = 0;
            pThis->aDirs[0].fNeedReset = false;
            pThis->aDirs[1].fNeedReset = false;
            *phXRoads = pThis;
            return VINF_SUCCESS;
        }
        RTSemEventMultiDestroy(pThis->aDirs[0].hEvt);
    }
    return rc;
}


RTDECL(int) RTSemXRoadsDestroy(RTSEMXROADS hXRoads)
{
    /*
     * Validate input.
     */
    RTSEMXROADSINTERNAL *pThis = hXRoads;
    if (pThis == NIL_RTSEMXROADS)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMXROADS_MAGIC, VERR_INVALID_HANDLE);
    Assert(!(ASMAtomicReadU64(&pThis->u64State) & (RTSEMXROADS_CNT_NS_MASK | RTSEMXROADS_CNT_EW_MASK)));

    /*
     * Invalidate the object and free up the resources.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSEMXROADS_MAGIC_DEAD, RTSEMXROADS_MAGIC), VERR_INVALID_HANDLE);

    RTSEMEVENTMULTI hEvt;
    ASMAtomicXchgHandle(&pThis->aDirs[0].hEvt, NIL_RTSEMEVENTMULTI, &hEvt);
    int rc = RTSemEventMultiDestroy(hEvt);
    AssertRC(rc);

    ASMAtomicXchgHandle(&pThis->aDirs[1].hEvt, NIL_RTSEMEVENTMULTI, &hEvt);
    rc = RTSemEventMultiDestroy(hEvt);
    AssertRC(rc);

    RTMemFree(pThis);
    return VINF_SUCCESS;
}


/**
 * Internal worker for RTSemXRoadsNSEnter and RTSemXRoadsEWEnter.
 *
 * @returns IPRT status code.
 * @param   pThis               The semaphore instance.
 * @param   fDir                The direction.
 * @param   uCountShift         The shift count for getting the count.
 * @param   fCountMask          The mask for getting the count.
 * @param   uWaitCountShift     The shift count for getting the wait count.
 * @param   fWaitCountMask      The mask for getting the wait count.
 */
DECL_FORCE_INLINE(int) rtSemXRoadsEnter(RTSEMXROADSINTERNAL *pThis, uint64_t fDir,
                                        uint64_t uCountShift, uint64_t fCountMask,
                                        uint64_t uWaitCountShift, uint64_t fWaitCountMask)
{
    uint64_t    u64OldState;
    uint64_t    u64State;

    u64State = ASMAtomicReadU64(&pThis->u64State);
    u64OldState = u64State;
    add_hist(u64State, u64OldState, fDir, "enter");

    for (;;)
    {
        if ((u64State & RTSEMXROADS_DIR_MASK) == (fDir << RTSEMXROADS_DIR_SHIFT))
        {
            /* It flows in the right direction, try follow it before it changes. */
            uint64_t c = (u64State & fCountMask) >> uCountShift;
            c++;
            Assert(c < 8*_1K);
            u64State &= ~fCountMask;
            u64State |= c << uCountShift;
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
                add_hist(u64State, u64OldState, fDir, "enter-simple");
                break;
            }
        }
        else if ((u64State & (RTSEMXROADS_CNT_NS_MASK | RTSEMXROADS_CNT_EW_MASK)) == 0)
        {
            /* Wrong direction, but we're alone here and can simply try switch the direction. */
            u64State &= ~(RTSEMXROADS_CNT_NS_MASK | RTSEMXROADS_CNT_EW_MASK | RTSEMXROADS_DIR_MASK);
            u64State |= (UINT64_C(1) << uCountShift) | (fDir << RTSEMXROADS_DIR_SHIFT);
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
                Assert(!pThis->aDirs[fDir].fNeedReset);
                add_hist(u64State, u64OldState, fDir, "enter-switch");
                break;
            }
        }
        else
        {
            /* Add ourselves to the queue and wait for the direction to change. */
            uint64_t c = (u64State & fCountMask) >> uCountShift;
            c++;
            Assert(c < RTSEMXROADS_CNT_MASK / 2);

            uint64_t cWait = (u64State & fWaitCountMask) >> uWaitCountShift;
            cWait++;
            Assert(cWait <= c);
            Assert(cWait < RTSEMXROADS_CNT_MASK / 2);

            u64State &= ~(fCountMask | fWaitCountMask);
            u64State |= (c << uCountShift) | (cWait << uWaitCountShift);

            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
                add_hist(u64State, u64OldState, fDir, "enter-wait");
                for (uint32_t iLoop = 0; ; iLoop++)
                {
                    int rc = RTSemEventMultiWait(pThis->aDirs[fDir].hEvt, RT_INDEFINITE_WAIT);
                    AssertRCReturn(rc, rc);

                    if (pThis->u32Magic != RTSEMXROADS_MAGIC)
                        return VERR_SEM_DESTROYED;

                    Assert(pThis->aDirs[fDir].fNeedReset);
                    u64State = ASMAtomicReadU64(&pThis->u64State);
                    add_hist(u64State, u64OldState, fDir, "enter-wakeup");
                    if ((u64State & RTSEMXROADS_DIR_MASK) == (fDir << RTSEMXROADS_DIR_SHIFT))
                        break;
                    AssertMsg(iLoop < 1, ("%u\n", iLoop));
                }

                /* Decrement the wait count and maybe reset the semaphore (if we're last). */
                for (;;)
                {
                    u64OldState = u64State;

                    cWait = (u64State & fWaitCountMask) >> uWaitCountShift;
                    Assert(cWait > 0);
                    cWait--;
                    u64State &= ~fWaitCountMask;
                    u64State |= cWait << uWaitCountShift;

                    if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
                    {
                        if (cWait == 0)
                        {
                            if (ASMAtomicXchgBool(&pThis->aDirs[fDir].fNeedReset, false))
                            {
                                add_hist(u64State, u64OldState, fDir, fDir ? "enter-reset-EW" : "enter-reset-NS");
                                int rc = RTSemEventMultiReset(pThis->aDirs[fDir].hEvt);
                                AssertRCReturn(rc, rc);
                            }
                            else
                                add_hist(u64State, u64OldState, fDir, "enter-dec-no-need");
                        }
                        break;
                    }
                    u64State = ASMAtomicReadU64(&pThis->u64State);
                }
                break;
            }

            add_hist(u64State, u64OldState, fDir, "enter-wait-failed");
        }

        if (pThis->u32Magic != RTSEMXROADS_MAGIC)
            return VERR_SEM_DESTROYED;

        ASMNopPause();
        u64State = ASMAtomicReadU64(&pThis->u64State);
        u64OldState = u64State;
    }

    /* got it! */
    Assert((ASMAtomicReadU64(&pThis->u64State) & RTSEMXROADS_DIR_MASK) == (fDir << RTSEMXROADS_DIR_SHIFT));
    return VINF_SUCCESS;
}


/**
 * Internal worker for RTSemXRoadsNSLeave and RTSemXRoadsEWLeave.
 *
 * @returns IPRT status code.
 * @param   pThis               The semaphore instance.
 * @param   fDir                The direction.
 * @param   uCountShift         The shift count for getting the count.
 * @param   fCountMask          The mask for getting the count.
 */
DECL_FORCE_INLINE(int) rtSemXRoadsLeave(RTSEMXROADSINTERNAL *pThis, uint64_t fDir, uint64_t uCountShift, uint64_t fCountMask)
{
    for (;;)
    {
        uint64_t u64OldState;
        uint64_t u64State;
        uint64_t c;

        u64State = ASMAtomicReadU64(&pThis->u64State);
        u64OldState = u64State;

        /* The direction cannot change until we've left or we'll crash. */
        Assert((u64State & RTSEMXROADS_DIR_MASK) == (fDir << RTSEMXROADS_DIR_SHIFT));

        c = (u64State & fCountMask) >> uCountShift;
        Assert(c > 0);
        c--;

        if (    c > 0
            ||  (u64State & ((RTSEMXROADS_CNT_NS_MASK | RTSEMXROADS_CNT_EW_MASK) & ~fCountMask)) == 0)
        {
            /* We're not the last one across or there aren't any one waiting in the other direction.  */
            u64State &= ~fCountMask;
            u64State |= c << uCountShift;
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
                add_hist(u64State, u64OldState, fDir, "leave-simple");
                return VINF_SUCCESS;
            }
        }
        else
        {
            /* Reverse the direction and signal the threads in the other direction. */
            u64State &= ~(fCountMask | RTSEMXROADS_DIR_MASK);
            u64State |= (uint64_t)!fDir << RTSEMXROADS_DIR_SHIFT;
            if (ASMAtomicCmpXchgU64(&pThis->u64State, u64State, u64OldState))
            {
                add_hist(u64State, u64OldState, fDir, fDir ? "leave-signal-NS" : "leave-signal-EW");
                Assert(!pThis->aDirs[!fDir].fNeedReset);
                ASMAtomicWriteBool(&pThis->aDirs[!fDir].fNeedReset, true);
                int rc = RTSemEventMultiSignal(pThis->aDirs[!fDir].hEvt);
                AssertRC(rc);
                return VINF_SUCCESS;
            }
        }

        ASMNopPause();
        if (pThis->u32Magic != RTSEMXROADS_MAGIC)
            return VERR_SEM_DESTROYED;
    }
}


RTDECL(int) RTSemXRoadsNSEnter(RTSEMXROADS hXRoads)
{
    /*
     * Validate input.
     */
    RTSEMXROADSINTERNAL *pThis = hXRoads;
    if (pThis == NIL_RTSEMXROADS)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMXROADS_MAGIC, VERR_INVALID_HANDLE);

    return rtSemXRoadsEnter(pThis, 0, RTSEMXROADS_CNT_NS_SHIFT, RTSEMXROADS_CNT_NS_MASK, RTSEMXROADS_WAIT_CNT_NS_SHIFT, RTSEMXROADS_WAIT_CNT_NS_MASK);
}


RTDECL(int) RTSemXRoadsNSLeave(RTSEMXROADS hXRoads)
{
    /*
     * Validate input.
     */
    RTSEMXROADSINTERNAL *pThis = hXRoads;
    if (pThis == NIL_RTSEMXROADS)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMXROADS_MAGIC, VERR_INVALID_HANDLE);

    return rtSemXRoadsLeave(pThis, 0, RTSEMXROADS_CNT_NS_SHIFT, RTSEMXROADS_CNT_NS_MASK);
}


RTDECL(int) RTSemXRoadsEWEnter(RTSEMXROADS hXRoads)
{
    /*
     * Validate input.
     */
    RTSEMXROADSINTERNAL *pThis = hXRoads;
    if (pThis == NIL_RTSEMXROADS)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMXROADS_MAGIC, VERR_INVALID_HANDLE);

    return rtSemXRoadsEnter(pThis, 1, RTSEMXROADS_CNT_EW_SHIFT, RTSEMXROADS_CNT_EW_MASK, RTSEMXROADS_WAIT_CNT_EW_SHIFT, RTSEMXROADS_WAIT_CNT_EW_MASK);
}


RTDECL(int) RTSemXRoadsEWLeave(RTSEMXROADS hXRoads)
{
    /*
     * Validate input.
     */
    RTSEMXROADSINTERNAL *pThis = hXRoads;
    if (pThis == NIL_RTSEMXROADS)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTSEMXROADS_MAGIC, VERR_INVALID_HANDLE);

    return rtSemXRoadsLeave(pThis, 1, RTSEMXROADS_CNT_EW_SHIFT, RTSEMXROADS_CNT_EW_MASK);
}

