/* $Id: lockvalidator.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Lock Validator.
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
#include <iprt/lockvalidator.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/once.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include "internal/lockvalidator.h"
#include "internal/magics.h"
#include "internal/strhash.h"
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Macro that asserts that a pointer is aligned correctly.
 * Only used when fighting bugs. */
#if 1
# define RTLOCKVAL_ASSERT_PTR_ALIGN(p) \
    AssertMsg(!((uintptr_t)(p) & (sizeof(uintptr_t) - 1)), ("%p\n", (p)));
#else
# define RTLOCKVAL_ASSERT_PTR_ALIGN(p)   do { } while (0)
#endif

/** Hashes the class handle (pointer) into an apPriorLocksHash index. */
#define RTLOCKVALCLASS_HASH(hClass) \
    (   ((uintptr_t)(hClass) >> 6 ) \
      % (  RT_SIZEOFMEMB(RTLOCKVALCLASSINT, apPriorLocksHash) \
         / sizeof(PRTLOCKVALCLASSREF)) )

/** The max value for RTLOCKVALCLASSINT::cRefs. */
#define RTLOCKVALCLASS_MAX_REFS                 UINT32_C(0xffff0000)
/** The max value for RTLOCKVALCLASSREF::cLookups. */
#define RTLOCKVALCLASSREF_MAX_LOOKUPS           UINT32_C(0xfffe0000)
/** The absolute max value for RTLOCKVALCLASSREF::cLookups at which it will
 *  be set back to RTLOCKVALCLASSREF_MAX_LOOKUPS. */
#define RTLOCKVALCLASSREF_MAX_LOOKUPS_FIX       UINT32_C(0xffff0000)


/** @def RTLOCKVAL_WITH_RECURSION_RECORDS
 *  Enable recursion records.  */
#if defined(IN_RING3) || defined(DOXYGEN_RUNNING)
# define RTLOCKVAL_WITH_RECURSION_RECORDS  1
#endif

/** @def RTLOCKVAL_WITH_VERBOSE_DUMPS
 * Enables some extra verbosity in the lock dumping.  */
#if defined(DOXYGEN_RUNNING)
# define RTLOCKVAL_WITH_VERBOSE_DUMPS
#endif

/** @def RTLOCKVAL_WITH_CLASS_HASH_STATS
 * Enables collection prior class hash lookup statistics, dumping them when
 * complaining about the class. */
#if defined(DEBUG) || defined(DOXYGEN_RUNNING)
# define RTLOCKVAL_WITH_CLASS_HASH_STATS
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Deadlock detection stack entry.
 */
typedef struct RTLOCKVALDDENTRY
{
    /** The current record. */
    PRTLOCKVALRECUNION      pRec;
    /** The current entry number if pRec is a shared one. */
    uint32_t                iEntry;
    /** The thread state of the thread we followed to get to pFirstSibling.
     * This is only used for validating a deadlock stack.  */
    RTTHREADSTATE           enmState;
    /** The thread we followed to get to pFirstSibling.
     * This is only used for validating a deadlock stack. */
    PRTTHREADINT            pThread;
    /** What pThread is waiting on, i.e. where we entered the circular list of
     * siblings.  This is used for validating a deadlock stack as well as
     * terminating the sibling walk. */
    PRTLOCKVALRECUNION      pFirstSibling;
} RTLOCKVALDDENTRY;


/**
 * Deadlock detection stack.
 */
typedef struct RTLOCKVALDDSTACK
{
    /** The number stack entries. */
    uint32_t                c;
    /** The stack entries. */
    RTLOCKVALDDENTRY        a[32];
} RTLOCKVALDDSTACK;
/** Pointer to a deadlock detection stack. */
typedef RTLOCKVALDDSTACK *PRTLOCKVALDDSTACK;


/**
 * Reference to another class.
 */
typedef struct RTLOCKVALCLASSREF
{
    /** The class. */
    RTLOCKVALCLASS          hClass;
    /** The number of lookups of this class. */
    uint32_t volatile       cLookups;
    /** Indicates whether the entry was added automatically during order checking
     *  (true) or manually via the API (false). */
    bool                    fAutodidacticism;
    /** Reserved / explicit alignment padding. */
    bool                    afReserved[3];
} RTLOCKVALCLASSREF;
/** Pointer to a class reference. */
typedef RTLOCKVALCLASSREF *PRTLOCKVALCLASSREF;


/** Pointer to a chunk of class references. */
typedef struct RTLOCKVALCLASSREFCHUNK *PRTLOCKVALCLASSREFCHUNK;
/**
 * Chunk of class references.
 */
typedef struct RTLOCKVALCLASSREFCHUNK
{
    /** Array of refs. */
#if 0 /** @todo for testing allocation of new chunks. */
    RTLOCKVALCLASSREF       aRefs[ARCH_BITS == 32 ? 10 : 8];
#else
    RTLOCKVALCLASSREF       aRefs[2];
#endif
    /** Pointer to the next chunk. */
    PRTLOCKVALCLASSREFCHUNK volatile pNext;
} RTLOCKVALCLASSREFCHUNK;


/**
 * Lock class.
 */
typedef struct RTLOCKVALCLASSINT
{
    /** AVL node core. */
    AVLLU32NODECORE         Core;
    /** Magic value (RTLOCKVALCLASS_MAGIC). */
    uint32_t volatile       u32Magic;
    /** Reference counter.  See RTLOCKVALCLASS_MAX_REFS. */
    uint32_t volatile       cRefs;
    /** Whether the class is allowed to teach it self new locking order rules. */
    bool                    fAutodidact;
    /** Whether to allow recursion. */
    bool                    fRecursionOk;
    /** Strict release order. */
    bool                    fStrictReleaseOrder;
    /** Whether this class is in the tree. */
    bool                    fInTree;
    /** Donate a reference to the next retainer. This is a hack to make
     *  RTLockValidatorClassCreateUnique work. */
    bool volatile           fDonateRefToNextRetainer;
    /** Reserved future use / explicit alignment. */
    bool                    afReserved[3];
    /** The minimum wait interval for which we do deadlock detection
     *  (milliseconds). */
    RTMSINTERVAL            cMsMinDeadlock;
    /** The minimum wait interval for which we do order checks (milliseconds). */
    RTMSINTERVAL            cMsMinOrder;
    /** More padding. */
    uint32_t                au32Reserved[ARCH_BITS == 32 ? 5 : 2];
    /** Classes that may be taken prior to this one.
     * This is a linked list where each node contains a chunk of locks so that we
     * reduce the number of allocations as well as localize the data. */
    RTLOCKVALCLASSREFCHUNK  PriorLocks;
    /** Hash table containing frequently encountered prior locks. */
    PRTLOCKVALCLASSREF      apPriorLocksHash[17];
    /** Class name. (Allocated after the end of the block as usual.) */
    char const             *pszName;
    /** Where this class was created.
     *  This is mainly used for finding automatically created lock classes.
     *  @remarks The strings are stored after this structure so we won't crash
     *           if the class lives longer than the module (dll/so/dylib) that
     *           spawned it. */
    RTLOCKVALSRCPOS         CreatePos;
#ifdef RTLOCKVAL_WITH_CLASS_HASH_STATS
    /** Hash hits. */
    uint32_t volatile       cHashHits;
    /** Hash misses. */
    uint32_t volatile       cHashMisses;
#endif
} RTLOCKVALCLASSINT;
AssertCompileSize(AVLLU32NODECORE, ARCH_BITS == 32 ? 20 : 32);
AssertCompileMemberOffset(RTLOCKVALCLASSINT, PriorLocks, 64);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Serializing object destruction and deadlock detection.
 *
 * This makes sure that none of the memory examined by the deadlock detection
 * code will become invalid (reused for other purposes or made not present)
 * while the detection is in progress.
 *
 * NS: RTLOCKVALREC*, RTTHREADINT and RTLOCKVALDRECSHRD::papOwners destruction.
 * EW: Deadlock detection and some related activities.
 */
static RTSEMXROADS      g_hLockValidatorXRoads   = NIL_RTSEMXROADS;
/** Serializing class tree insert and lookups. */
static RTSEMRW          g_hLockValClassTreeRWLock= NIL_RTSEMRW;
/** Class tree. */
static PAVLLU32NODECORE g_LockValClassTree       = NULL;
/** Critical section serializing the teaching new rules to the classes. */
static RTCRITSECT       g_LockValClassTeachCS;

/** Whether the lock validator is enabled or disabled.
 * Only applies to new locks.  */
static bool volatile    g_fLockValidatorEnabled  = true;
/** Set if the lock validator is quiet. */
#ifdef RT_STRICT
static bool volatile    g_fLockValidatorQuiet    = false;
#else
static bool volatile    g_fLockValidatorQuiet    = true;
#endif
/** Set if the lock validator may panic. */
#ifdef RT_STRICT
static bool volatile    g_fLockValidatorMayPanic = true;
#else
static bool volatile    g_fLockValidatorMayPanic = false;
#endif
/** Whether to return an error status on wrong locking order. */
static bool volatile    g_fLockValSoftWrongOrder = false;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void     rtLockValidatorClassDestroy(RTLOCKVALCLASSINT *pClass);
static uint32_t rtLockValidatorStackDepth(PRTTHREADINT pThread);


/**
 * Lazy initialization of the lock validator globals.
 */
static void rtLockValidatorLazyInit(void)
{
    static uint32_t volatile s_fInitializing = false;
    if (ASMAtomicCmpXchgU32(&s_fInitializing, true, false))
    {
        /*
         * The locks.
         */
        if (!RTCritSectIsInitialized(&g_LockValClassTeachCS))
            RTCritSectInitEx(&g_LockValClassTeachCS, RTCRITSECT_FLAGS_NO_LOCK_VAL, NIL_RTLOCKVALCLASS,
                             RTLOCKVAL_SUB_CLASS_ANY, "RTLockVal-Teach");

        if (g_hLockValClassTreeRWLock == NIL_RTSEMRW)
        {
            RTSEMRW hSemRW;
            int rc = RTSemRWCreateEx(&hSemRW, RTSEMRW_FLAGS_NO_LOCK_VAL, NIL_RTLOCKVALCLASS, RTLOCKVAL_SUB_CLASS_ANY, "RTLockVal-Tree");
            if (RT_SUCCESS(rc))
                ASMAtomicWriteHandle(&g_hLockValClassTreeRWLock, hSemRW);
        }

        if (g_hLockValidatorXRoads == NIL_RTSEMXROADS)
        {
            RTSEMXROADS hXRoads;
            int rc = RTSemXRoadsCreate(&hXRoads);
            if (RT_SUCCESS(rc))
                ASMAtomicWriteHandle(&g_hLockValidatorXRoads, hXRoads);
        }

#ifdef IN_RING3
        /*
         * Check the environment for our config variables.
         */
        if (RTEnvExist("IPRT_LOCK_VALIDATOR_ENABLED"))
            ASMAtomicWriteBool(&g_fLockValidatorEnabled, true);
        if (RTEnvExist("IPRT_LOCK_VALIDATOR_DISABLED"))
            ASMAtomicWriteBool(&g_fLockValidatorEnabled, false);

        if (RTEnvExist("IPRT_LOCK_VALIDATOR_MAY_PANIC"))
            ASMAtomicWriteBool(&g_fLockValidatorMayPanic, true);
        if (RTEnvExist("IPRT_LOCK_VALIDATOR_MAY_NOT_PANIC"))
            ASMAtomicWriteBool(&g_fLockValidatorMayPanic, false);

        if (RTEnvExist("IPRT_LOCK_VALIDATOR_NOT_QUIET"))
            ASMAtomicWriteBool(&g_fLockValidatorQuiet, false);
        if (RTEnvExist("IPRT_LOCK_VALIDATOR_QUIET"))
            ASMAtomicWriteBool(&g_fLockValidatorQuiet, true);

        if (RTEnvExist("IPRT_LOCK_VALIDATOR_STRICT_ORDER"))
            ASMAtomicWriteBool(&g_fLockValSoftWrongOrder, false);
        if (RTEnvExist("IPRT_LOCK_VALIDATOR_SOFT_ORDER"))
            ASMAtomicWriteBool(&g_fLockValSoftWrongOrder, true);
#endif

        /*
         * Register cleanup
         */
        /** @todo register some cleanup callback if we care. */

        ASMAtomicWriteU32(&s_fInitializing, false);
    }
}



/** Wrapper around ASMAtomicReadPtr. */
DECL_FORCE_INLINE(PRTLOCKVALRECUNION) rtLockValidatorReadRecUnionPtr(PRTLOCKVALRECUNION volatile *ppRec)
{
    PRTLOCKVALRECUNION p = ASMAtomicReadPtrT(ppRec, PRTLOCKVALRECUNION);
    RTLOCKVAL_ASSERT_PTR_ALIGN(p);
    return p;
}


/** Wrapper around ASMAtomicWritePtr. */
DECL_FORCE_INLINE(void) rtLockValidatorWriteRecUnionPtr(PRTLOCKVALRECUNION volatile *ppRec, PRTLOCKVALRECUNION pRecNew)
{
    RTLOCKVAL_ASSERT_PTR_ALIGN(pRecNew);
    ASMAtomicWritePtr(ppRec, pRecNew);
}


/** Wrapper around ASMAtomicReadPtr. */
DECL_FORCE_INLINE(PRTTHREADINT) rtLockValidatorReadThreadHandle(RTTHREAD volatile *phThread)
{
    PRTTHREADINT p = ASMAtomicReadPtrT(phThread, PRTTHREADINT);
    RTLOCKVAL_ASSERT_PTR_ALIGN(p);
    return p;
}


/** Wrapper around ASMAtomicUoReadPtr. */
DECL_FORCE_INLINE(PRTLOCKVALRECSHRDOWN) rtLockValidatorUoReadSharedOwner(PRTLOCKVALRECSHRDOWN volatile *ppOwner)
{
    PRTLOCKVALRECSHRDOWN p = ASMAtomicUoReadPtrT(ppOwner, PRTLOCKVALRECSHRDOWN);
    RTLOCKVAL_ASSERT_PTR_ALIGN(p);
    return p;
}


/**
 * Reads a volatile thread handle field and returns the thread name.
 *
 * @returns Thread name (read only).
 * @param   phThread            The thread handle field.
 */
static const char *rtLockValidatorNameThreadHandle(RTTHREAD volatile *phThread)
{
    PRTTHREADINT pThread = rtLockValidatorReadThreadHandle(phThread);
    if (!pThread)
        return "<NIL>";
    if (!RT_VALID_PTR(pThread))
        return "<INVALID>";
    if (pThread->u32Magic != RTTHREADINT_MAGIC)
        return "<BAD-THREAD-MAGIC>";
    return pThread->szName;
}


/**
 * Launch a simple assertion like complaint w/ panic.
 *
 * @param   SRC_POS             The source position where call is being made from.
 * @param   pszWhat             What we're complaining about.
 * @param   ...                 Format arguments.
 */
static void rtLockValComplain(RT_SRC_POS_DECL, const char *pszWhat, ...)
{
    if (!ASMAtomicUoReadBool(&g_fLockValidatorQuiet))
    {
        RTAssertMsg1Weak("RTLockValidator", iLine, pszFile, pszFunction);
        va_list va;
        va_start(va, pszWhat);
        RTAssertMsg2WeakV(pszWhat, va);
        va_end(va);
    }
    if (!ASMAtomicUoReadBool(&g_fLockValidatorQuiet))
        RTAssertPanic();
}


/**
 * Describes the class.
 *
 * @param   pszPrefix           Message prefix.
 * @param   pClass              The class to complain about.
 * @param   uSubClass           My sub-class.
 * @param   fVerbose            Verbose description including relations to other
 *                              classes.
 */
static void rtLockValComplainAboutClass(const char *pszPrefix, RTLOCKVALCLASSINT *pClass, uint32_t uSubClass, bool fVerbose)
{
    if (ASMAtomicUoReadBool(&g_fLockValidatorQuiet))
        return;

    /* Stringify the sub-class. */
    const char *pszSubClass;
    char        szSubClass[32];
    if (uSubClass < RTLOCKVAL_SUB_CLASS_USER)
        switch (uSubClass)
        {
            case RTLOCKVAL_SUB_CLASS_NONE: pszSubClass = "none"; break;
            case RTLOCKVAL_SUB_CLASS_ANY:  pszSubClass = "any"; break;
            default:
                RTStrPrintf(szSubClass, sizeof(szSubClass),  "invl-%u", uSubClass);
                pszSubClass = szSubClass;
                break;
        }
    else
    {
        RTStrPrintf(szSubClass, sizeof(szSubClass),  "%u", uSubClass);
        pszSubClass = szSubClass;
    }

    /* Validate the class pointer. */
    if (!RT_VALID_PTR(pClass))
    {
        RTAssertMsg2AddWeak("%sbad class=%p sub-class=%s\n", pszPrefix, pClass, pszSubClass);
        return;
    }
    if (pClass->u32Magic != RTLOCKVALCLASS_MAGIC)
    {
        RTAssertMsg2AddWeak("%sbad class=%p magic=%#x sub-class=%s\n", pszPrefix, pClass, pClass->u32Magic, pszSubClass);
        return;
    }

    /* OK, dump the class info. */
    RTAssertMsg2AddWeak("%sclass=%p %s created={%Rbn(%u) %Rfn %p} sub-class=%s\n", pszPrefix,
                        pClass,
                        pClass->pszName,
                        pClass->CreatePos.pszFile,
                        pClass->CreatePos.uLine,
                        pClass->CreatePos.pszFunction,
                        pClass->CreatePos.uId,
                        pszSubClass);
    if (fVerbose)
    {
        uint32_t i        = 0;
        uint32_t cPrinted = 0;
        for (PRTLOCKVALCLASSREFCHUNK pChunk = &pClass->PriorLocks; pChunk; pChunk = pChunk->pNext)
            for (unsigned j = 0; j < RT_ELEMENTS(pChunk->aRefs); j++, i++)
            {
                RTLOCKVALCLASSINT *pCurClass = pChunk->aRefs[j].hClass;
                if (pCurClass != NIL_RTLOCKVALCLASS)
                {
                    RTAssertMsg2AddWeak("%s%s #%02u: %s, %s, %u lookup%s\n", pszPrefix,
                                        cPrinted == 0
                                        ? "Prior:"
                                        : "      ",
                                        i,
                                        pCurClass->pszName,
                                        pChunk->aRefs[j].fAutodidacticism
                                        ? "autodidactic"
                                        : "manually    ",
                                        pChunk->aRefs[j].cLookups,
                                        pChunk->aRefs[j].cLookups != 1 ? "s" : "");
                    cPrinted++;
                }
            }
        if (!cPrinted)
            RTAssertMsg2AddWeak("%sPrior: none\n", pszPrefix);
#ifdef RTLOCKVAL_WITH_CLASS_HASH_STATS
        RTAssertMsg2AddWeak("%sHash Stats: %u hits, %u misses\n", pszPrefix, pClass->cHashHits, pClass->cHashMisses);
#endif
    }
    else
    {
        uint32_t cPrinted = 0;
        for (PRTLOCKVALCLASSREFCHUNK pChunk = &pClass->PriorLocks; pChunk; pChunk = pChunk->pNext)
            for (unsigned j = 0; j < RT_ELEMENTS(pChunk->aRefs); j++)
            {
                RTLOCKVALCLASSINT *pCurClass = pChunk->aRefs[j].hClass;
                if (pCurClass != NIL_RTLOCKVALCLASS)
                {
                    if ((cPrinted % 10) == 0)
                        RTAssertMsg2AddWeak("%sPrior classes: %s%s", pszPrefix, pCurClass->pszName,
                                            pChunk->aRefs[j].fAutodidacticism ? "*" : "");
                    else if ((cPrinted % 10) != 9)
                        RTAssertMsg2AddWeak(", %s%s", pCurClass->pszName,
                                            pChunk->aRefs[j].fAutodidacticism ? "*" : "");
                    else
                        RTAssertMsg2AddWeak(", %s%s\n", pCurClass->pszName,
                                            pChunk->aRefs[j].fAutodidacticism ? "*" : "");
                    cPrinted++;
                }
            }
        if (!cPrinted)
            RTAssertMsg2AddWeak("%sPrior classes: none\n", pszPrefix);
        else if ((cPrinted % 10) != 0)
            RTAssertMsg2AddWeak("\n");
    }
}


/**
 * Helper for getting the class name.
 * @returns Class name string.
 * @param   pClass              The class.
 */
static const char *rtLockValComplainGetClassName(RTLOCKVALCLASSINT *pClass)
{
    if (!pClass)
        return "<nil-class>";
    if (!RT_VALID_PTR(pClass))
        return "<bad-class-ptr>";
    if (pClass->u32Magic != RTLOCKVALCLASS_MAGIC)
        return "<bad-class-magic>";
    if (!pClass->pszName)
        return "<no-class-name>";
    return pClass->pszName;
}

/**
 * Formats the sub-class.
 *
 * @returns Stringified sub-class.
 * @param  uSubClass            The name.
 * @param  pszBuf               Buffer that is big enough.
 */
static const char *rtLockValComplainGetSubClassName(uint32_t uSubClass, char *pszBuf)
{
    if (uSubClass < RTLOCKVAL_SUB_CLASS_USER)
        switch (uSubClass)
        {
            case RTLOCKVAL_SUB_CLASS_NONE: return "none";
            case RTLOCKVAL_SUB_CLASS_ANY:  return "any";
            default:
                RTStrPrintf(pszBuf, 32, "invl-%u", uSubClass);
                break;
        }
    else
        RTStrPrintf(pszBuf, 32, "%x", uSubClass);
    return pszBuf;
}


/**
 * Helper for rtLockValComplainAboutLock.
 */
DECL_FORCE_INLINE(void) rtLockValComplainAboutLockHlp(const char *pszPrefix, PRTLOCKVALRECUNION pRec, const char *pszSuffix,
                                                      uint32_t u32Magic, PCRTLOCKVALSRCPOS pSrcPos, uint32_t cRecursion,
                                                      const char *pszFrameType)
{
    char szBuf[32];
    switch (u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
#ifdef RTLOCKVAL_WITH_VERBOSE_DUMPS
            RTAssertMsg2AddWeak("%s%p %s xrec=%p own=%s r=%u cls=%s/%s pos={%Rbn(%u) %Rfn %p} [x%s]%s", pszPrefix,
                                pRec->Excl.hLock, pRec->Excl.szName, pRec,
                                rtLockValidatorNameThreadHandle(&pRec->Excl.hThread), cRecursion,
                                rtLockValComplainGetClassName(pRec->Excl.hClass),
                                rtLockValComplainGetSubClassName(pRec->Excl.uSubClass, szBuf),
                                pSrcPos->pszFile, pSrcPos->uLine, pSrcPos->pszFunction, pSrcPos->uId,
                                pszFrameType, pszSuffix);
#else
            RTAssertMsg2AddWeak("%s%p %s own=%s r=%u cls=%s/%s pos={%Rbn(%u) %Rfn %p} [x%s]%s", pszPrefix,
                                pRec->Excl.hLock, pRec->Excl.szName,
                                rtLockValidatorNameThreadHandle(&pRec->Excl.hThread), cRecursion,
                                rtLockValComplainGetClassName(pRec->Excl.hClass),
                                rtLockValComplainGetSubClassName(pRec->Excl.uSubClass, szBuf),
                                pSrcPos->pszFile, pSrcPos->uLine, pSrcPos->pszFunction, pSrcPos->uId,
                                pszFrameType, pszSuffix);
#endif
            break;

        case RTLOCKVALRECSHRD_MAGIC:
            RTAssertMsg2AddWeak("%ss %p %s srec=%p cls=%s/%s [s%s]%s", pszPrefix,
                                pRec->Shared.hLock, pRec->Shared.szName, pRec,
                                rtLockValComplainGetClassName(pRec->Shared.hClass),
                                rtLockValComplainGetSubClassName(pRec->Shared.uSubClass, szBuf),
                                pszFrameType, pszSuffix);
            break;

        case RTLOCKVALRECSHRDOWN_MAGIC:
        {
            PRTLOCKVALRECSHRD pShared = pRec->ShrdOwner.pSharedRec;
            if (    RT_VALID_PTR(pShared)
                &&  pShared->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC)
#ifdef RTLOCKVAL_WITH_VERBOSE_DUMPS
                RTAssertMsg2AddWeak("%s%p %s srec=%p trec=%p own=%s r=%u cls=%s/%s pos={%Rbn(%u) %Rfn %p} [o%s]%s", pszPrefix,
                                    pShared->hLock, pShared->szName, pShared,
                                    pRec, rtLockValidatorNameThreadHandle(&pRec->ShrdOwner.hThread), cRecursion,
                                    rtLockValComplainGetClassName(pShared->hClass),
                                    rtLockValComplainGetSubClassName(pShared->uSubClass, szBuf),
                                    pSrcPos->pszFile, pSrcPos->uLine, pSrcPos->pszFunction, pSrcPos->uId,
                                    pszSuffix, pszSuffix);
#else
                RTAssertMsg2AddWeak("%s%p %s own=%s r=%u cls=%s/%s pos={%Rbn(%u) %Rfn %p} [o%s]%s", pszPrefix,
                                    pShared->hLock, pShared->szName,
                                    rtLockValidatorNameThreadHandle(&pRec->ShrdOwner.hThread), cRecursion,
                                    rtLockValComplainGetClassName(pShared->hClass),
                                    rtLockValComplainGetSubClassName(pShared->uSubClass, szBuf),
                                    pSrcPos->pszFile, pSrcPos->uLine, pSrcPos->pszFunction, pSrcPos->uId,
                                    pszFrameType, pszSuffix);
#endif
            else
                RTAssertMsg2AddWeak("%sbad srec=%p trec=%p own=%s r=%u pos={%Rbn(%u) %Rfn %p} [x%s]%s", pszPrefix,
                                    pShared,
                                    pRec, rtLockValidatorNameThreadHandle(&pRec->ShrdOwner.hThread), cRecursion,
                                    pSrcPos->pszFile, pSrcPos->uLine, pSrcPos->pszFunction, pSrcPos->uId,
                                    pszFrameType, pszSuffix);
            break;
        }

        default:
            AssertMsgFailed(("%#x\n", u32Magic));
    }
}


/**
 * Describes the lock.
 *
 * @param   pszPrefix           Message prefix.
 * @param   pRec                The lock record we're working on.
 * @param   pszSuffix           Message suffix.
 */
static void rtLockValComplainAboutLock(const char *pszPrefix, PRTLOCKVALRECUNION pRec, const char *pszSuffix)
{
#ifdef RTLOCKVAL_WITH_RECURSION_RECORDS
# define FIX_REC(r)     1
#else
# define FIX_REC(r)     (r)
#endif
    if (    RT_VALID_PTR(pRec)
        &&  !ASMAtomicUoReadBool(&g_fLockValidatorQuiet))
    {
        switch (pRec->Core.u32Magic)
        {
            case RTLOCKVALRECEXCL_MAGIC:
                rtLockValComplainAboutLockHlp(pszPrefix, pRec, pszSuffix, RTLOCKVALRECEXCL_MAGIC,
                                              &pRec->Excl.SrcPos, FIX_REC(pRec->Excl.cRecursion), "");
                break;

            case RTLOCKVALRECSHRD_MAGIC:
                rtLockValComplainAboutLockHlp(pszPrefix, pRec, pszSuffix, RTLOCKVALRECSHRD_MAGIC, NULL, 0, "");
                break;

            case RTLOCKVALRECSHRDOWN_MAGIC:
                rtLockValComplainAboutLockHlp(pszPrefix, pRec, pszSuffix, RTLOCKVALRECSHRDOWN_MAGIC,
                                              &pRec->ShrdOwner.SrcPos, FIX_REC(pRec->ShrdOwner.cRecursion), "");
                break;

            case RTLOCKVALRECNEST_MAGIC:
            {
                PRTLOCKVALRECUNION  pRealRec = pRec->Nest.pRec;
                uint32_t            u32Magic;
                if (   RT_VALID_PTR(pRealRec)
                    && (   (u32Magic = pRealRec->Core.u32Magic) == RTLOCKVALRECEXCL_MAGIC
                        || u32Magic == RTLOCKVALRECSHRD_MAGIC
                        || u32Magic == RTLOCKVALRECSHRDOWN_MAGIC)
                    )
                    rtLockValComplainAboutLockHlp(pszPrefix, pRealRec, pszSuffix, u32Magic,
                                                  &pRec->Nest.SrcPos, pRec->Nest.cRecursion, "/r");
                else
                    RTAssertMsg2AddWeak("%sbad rrec=%p nrec=%p r=%u pos={%Rbn(%u) %Rfn %p}%s", pszPrefix,
                                        pRealRec, pRec, pRec->Nest.cRecursion,
                                        pRec->Nest.SrcPos.pszFile, pRec->Nest.SrcPos.uLine, pRec->Nest.SrcPos.pszFunction, pRec->Nest.SrcPos.uId,
                                        pszSuffix);
                break;
            }

            default:
                RTAssertMsg2AddWeak("%spRec=%p u32Magic=%#x (bad)%s", pszPrefix, pRec, pRec->Core.u32Magic, pszSuffix);
                break;
        }
    }
#undef FIX_REC
}


/**
 * Dump the lock stack.
 *
 * @param   pThread             The thread which lock stack we're gonna dump.
 * @param   cchIndent           The indentation in chars.
 * @param   cMinFrames          The minimum number of frames to consider
 *                              dumping.
 * @param   pHighightRec        Record that should be marked specially in the
 *                              dump.
 */
static void rtLockValComplainAboutLockStack(PRTTHREADINT pThread, unsigned cchIndent, uint32_t cMinFrames,
                                            PRTLOCKVALRECUNION pHighightRec)
{
    if (    RT_VALID_PTR(pThread)
        &&  !ASMAtomicUoReadBool(&g_fLockValidatorQuiet)
        &&  pThread->u32Magic == RTTHREADINT_MAGIC
       )
    {
        uint32_t cEntries = rtLockValidatorStackDepth(pThread);
        if (cEntries >= cMinFrames)
        {
            RTAssertMsg2AddWeak("%*s---- start of lock stack for %p %s - %u entr%s ----\n", cchIndent, "",
                                pThread, pThread->szName, cEntries, cEntries == 1 ? "y" : "ies");
            PRTLOCKVALRECUNION pCur = rtLockValidatorReadRecUnionPtr(&pThread->LockValidator.pStackTop);
            for (uint32_t i = 0; RT_VALID_PTR(pCur); i++)
            {
                char szPrefix[80];
                RTStrPrintf(szPrefix, sizeof(szPrefix), "%*s#%02u: ", cchIndent, "", i);
                rtLockValComplainAboutLock(szPrefix, pCur, pHighightRec != pCur ? "\n" : " (*)\n");
                switch (pCur->Core.u32Magic)
                {
                    case RTLOCKVALRECEXCL_MAGIC:    pCur = rtLockValidatorReadRecUnionPtr(&pCur->Excl.pDown);      break;
                    case RTLOCKVALRECSHRDOWN_MAGIC: pCur = rtLockValidatorReadRecUnionPtr(&pCur->ShrdOwner.pDown); break;
                    case RTLOCKVALRECNEST_MAGIC:    pCur = rtLockValidatorReadRecUnionPtr(&pCur->Nest.pDown);      break;
                    default:
                        RTAssertMsg2AddWeak("%*s<bad stack frame>\n", cchIndent, "");
                        pCur = NULL;
                        break;
                }
            }
            RTAssertMsg2AddWeak("%*s---- end of lock stack ----\n", cchIndent, "");
        }
    }
}


/**
 * Launch the initial complaint.
 *
 * @param   pszWhat             What we're complaining about.
 * @param   pSrcPos             Where we are complaining from, as it were.
 * @param   pThreadSelf         The calling thread.
 * @param   pRec                The main lock involved. Can be NULL.
 * @param   fDumpStack          Whether to dump the lock stack (true) or not
 *                              (false).
 */
static void rtLockValComplainFirst(const char *pszWhat, PCRTLOCKVALSRCPOS pSrcPos, PRTTHREADINT pThreadSelf,
                                   PRTLOCKVALRECUNION pRec, bool fDumpStack)
{
    if (!ASMAtomicUoReadBool(&g_fLockValidatorQuiet))
    {
        ASMCompilerBarrier(); /* paranoia */
        RTAssertMsg1Weak("RTLockValidator", pSrcPos ? pSrcPos->uLine : 0, pSrcPos ? pSrcPos->pszFile : NULL, pSrcPos ? pSrcPos->pszFunction : NULL);
        if (pSrcPos && pSrcPos->uId)
            RTAssertMsg2Weak("%s  [uId=%p  thrd=%s]\n", pszWhat, pSrcPos->uId, RT_VALID_PTR(pThreadSelf) ? pThreadSelf->szName : "<NIL>");
        else
            RTAssertMsg2Weak("%s  [thrd=%s]\n", pszWhat, RT_VALID_PTR(pThreadSelf) ? pThreadSelf->szName : "<NIL>");
        rtLockValComplainAboutLock("Lock: ", pRec, "\n");
        if (fDumpStack)
            rtLockValComplainAboutLockStack(pThreadSelf, 0, 1, pRec);
    }
}


/**
 * Continue bitching.
 *
 * @param   pszFormat           Format string.
 * @param   ...                 Format arguments.
 */
static void rtLockValComplainMore(const char *pszFormat, ...)
{
    if (!ASMAtomicUoReadBool(&g_fLockValidatorQuiet))
    {
        va_list va;
        va_start(va, pszFormat);
        RTAssertMsg2AddWeakV(pszFormat, va);
        va_end(va);
    }
}


/**
 * Raise a panic if enabled.
 */
static void rtLockValComplainPanic(void)
{
    if (ASMAtomicUoReadBool(&g_fLockValidatorMayPanic))
        RTAssertPanic();
}


/**
 * Copy a source position record.
 *
 * @param   pDst                The destination.
 * @param   pSrc                The source.  Can be NULL.
 */
DECL_FORCE_INLINE(void) rtLockValidatorSrcPosCopy(PRTLOCKVALSRCPOS pDst, PCRTLOCKVALSRCPOS pSrc)
{
    if (pSrc)
    {
        ASMAtomicUoWriteU32(&pDst->uLine,        pSrc->uLine);
        ASMAtomicUoWritePtr(&pDst->pszFile,      pSrc->pszFile);
        ASMAtomicUoWritePtr(&pDst->pszFunction,  pSrc->pszFunction);
        ASMAtomicUoWritePtr((void * volatile *)&pDst->uId, (void *)pSrc->uId);
    }
    else
    {
        ASMAtomicUoWriteU32(&pDst->uLine,        0);
        ASMAtomicUoWriteNullPtr(&pDst->pszFile);
        ASMAtomicUoWriteNullPtr(&pDst->pszFunction);
        ASMAtomicUoWritePtr(&pDst->uId, (RTHCUINTPTR)0);
    }
}


/**
 * Init a source position record.
 *
 * @param   pSrcPos             The source position record.
 */
DECL_FORCE_INLINE(void) rtLockValidatorSrcPosInit(PRTLOCKVALSRCPOS pSrcPos)
{
    pSrcPos->pszFile        = NULL;
    pSrcPos->pszFunction    = NULL;
    pSrcPos->uId            = 0;
    pSrcPos->uLine          = 0;
#if HC_ARCH_BITS == 64
    pSrcPos->u32Padding     = 0;
#endif
}


/**
 * Hashes the specified source position.
 *
 * @returns Hash.
 * @param   pSrcPos             The source position record.
 */
static uint32_t rtLockValidatorSrcPosHash(PCRTLOCKVALSRCPOS pSrcPos)
{
    uint32_t uHash;
    if (   (   pSrcPos->pszFile
            || pSrcPos->pszFunction)
        && pSrcPos->uLine != 0)
    {
        uHash = 0;
        if (pSrcPos->pszFile)
            uHash = sdbmInc(pSrcPos->pszFile, uHash);
        if (pSrcPos->pszFunction)
            uHash = sdbmInc(pSrcPos->pszFunction, uHash);
        uHash += pSrcPos->uLine;
    }
    else
    {
        Assert(pSrcPos->uId);
        uHash = (uint32_t)pSrcPos->uId;
    }

    return uHash;
}


/**
 * Compares two source positions.
 *
 * @returns 0 if equal, < 0 if pSrcPos1 is smaller than pSrcPos2, > 0 if
 *          otherwise.
 * @param   pSrcPos1            The first source position.
 * @param   pSrcPos2            The second source position.
 */
static int rtLockValidatorSrcPosCompare(PCRTLOCKVALSRCPOS pSrcPos1, PCRTLOCKVALSRCPOS pSrcPos2)
{
    if (pSrcPos1->uLine != pSrcPos2->uLine)
        return pSrcPos1->uLine < pSrcPos2->uLine ? -1 : 1;

    int iDiff = RTStrCmp(pSrcPos1->pszFile, pSrcPos2->pszFile);
    if (iDiff != 0)
        return iDiff;

    iDiff = RTStrCmp(pSrcPos1->pszFunction, pSrcPos2->pszFunction);
    if (iDiff != 0)
        return iDiff;

    if (pSrcPos1->uId != pSrcPos2->uId)
        return pSrcPos1->uId < pSrcPos2->uId ? -1 : 1;
    return 0;
}



/**
 * Serializes destruction of RTLOCKVALREC* and RTTHREADINT structures.
 */
DECLHIDDEN(void) rtLockValidatorSerializeDestructEnter(void)
{
    RTSEMXROADS hXRoads = g_hLockValidatorXRoads;
    if (hXRoads != NIL_RTSEMXROADS)
        RTSemXRoadsNSEnter(hXRoads);
}


/**
 * Call after rtLockValidatorSerializeDestructEnter.
 */
DECLHIDDEN(void) rtLockValidatorSerializeDestructLeave(void)
{
    RTSEMXROADS hXRoads = g_hLockValidatorXRoads;
    if (hXRoads != NIL_RTSEMXROADS)
        RTSemXRoadsNSLeave(hXRoads);
}


/**
 * Serializes deadlock detection against destruction of the objects being
 * inspected.
 */
DECLINLINE(void) rtLockValidatorSerializeDetectionEnter(void)
{
    RTSEMXROADS hXRoads = g_hLockValidatorXRoads;
    if (hXRoads != NIL_RTSEMXROADS)
        RTSemXRoadsEWEnter(hXRoads);
}


/**
 * Call after rtLockValidatorSerializeDetectionEnter.
 */
DECLINLINE(void) rtLockValidatorSerializeDetectionLeave(void)
{
    RTSEMXROADS hXRoads = g_hLockValidatorXRoads;
    if (hXRoads != NIL_RTSEMXROADS)
        RTSemXRoadsEWLeave(hXRoads);
}


/**
 * Initializes the per thread lock validator data.
 *
 * @param   pPerThread      The data.
 */
DECLHIDDEN(void) rtLockValidatorInitPerThread(RTLOCKVALPERTHREAD *pPerThread)
{
    pPerThread->bmFreeShrdOwners = UINT32_MAX;

    /* ASSUMES the rest has already been zeroed. */
    Assert(pPerThread->pRec == NULL);
    Assert(pPerThread->cWriteLocks == 0);
    Assert(pPerThread->cReadLocks == 0);
    Assert(pPerThread->fInValidator == false);
    Assert(pPerThread->pStackTop == NULL);
}


/**
 * Delete the per thread lock validator data.
 *
 * @param   pPerThread      The data.
 */
DECLHIDDEN(void) rtLockValidatorDeletePerThread(RTLOCKVALPERTHREAD *pPerThread)
{
    /*
     * Check that the thread doesn't own any locks at this time.
     */
    if (pPerThread->pStackTop)
    {
        rtLockValComplainFirst("Thread terminating owning locks!", NULL,
                               RT_FROM_MEMBER(pPerThread, RTTHREADINT, LockValidator),
                               pPerThread->pStackTop, true);
        rtLockValComplainPanic();
    }

    /*
     * Free the recursion records.
     */
    PRTLOCKVALRECNEST pCur = pPerThread->pFreeNestRecs;
    pPerThread->pFreeNestRecs = NULL;
    while (pCur)
    {
        PRTLOCKVALRECNEST pNext = pCur->pNextFree;
        RTMemFree(pCur);
        pCur = pNext;
    }
}

RTDECL(int) RTLockValidatorClassCreateEx(PRTLOCKVALCLASS phClass, PCRTLOCKVALSRCPOS pSrcPos,
                                         bool fAutodidact, bool fRecursionOk, bool fStrictReleaseOrder,
                                         RTMSINTERVAL cMsMinDeadlock, RTMSINTERVAL cMsMinOrder,
                                         const char *pszNameFmt, ...)
{
    va_list va;
    va_start(va, pszNameFmt);
    int rc = RTLockValidatorClassCreateExV(phClass, pSrcPos, fAutodidact, fRecursionOk, fStrictReleaseOrder,
                                           cMsMinDeadlock, cMsMinOrder, pszNameFmt, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTLockValidatorClassCreateExV(PRTLOCKVALCLASS phClass, PCRTLOCKVALSRCPOS pSrcPos,
                                          bool fAutodidact, bool fRecursionOk, bool fStrictReleaseOrder,
                                          RTMSINTERVAL cMsMinDeadlock, RTMSINTERVAL cMsMinOrder,
                                          const char *pszNameFmt, va_list va)
{
    Assert(cMsMinDeadlock >= 1);
    Assert(cMsMinOrder    >= 1);
    AssertPtr(pSrcPos);

    /*
     * Format the name and calc its length.
     */
    size_t cbName;
    char   szName[32];
    if (pszNameFmt && *pszNameFmt)
        cbName = RTStrPrintfV(szName, sizeof(szName), pszNameFmt, va) + 1;
    else
    {
        static uint32_t volatile s_cAnonymous = 0;
        uint32_t i = ASMAtomicIncU32(&s_cAnonymous);
        cbName = RTStrPrintf(szName, sizeof(szName), "anon-%u", i - 1) + 1;
    }

    /*
     * Figure out the file and function name lengths and allocate memory for
     * it all.
     */
    size_t const       cbFile     = pSrcPos->pszFile     ? strlen(pSrcPos->pszFile)     + 1 : 0;
    size_t const       cbFunction = pSrcPos->pszFunction ? strlen(pSrcPos->pszFunction) + 1 : 0;
    RTLOCKVALCLASSINT *pThis      = (RTLOCKVALCLASSINT *)RTMemAllocVarTag(sizeof(*pThis) + cbFile + cbFunction + cbName,
                                                                          "may-leak:RTLockValidatorClassCreateExV");
    if (!pThis)
        return VERR_NO_MEMORY;
    RTMEM_MAY_LEAK(pThis);

    /*
     * Initialize the class data.
     */
    pThis->Core.Key             = rtLockValidatorSrcPosHash(pSrcPos);
    pThis->Core.uchHeight       = 0;
    pThis->Core.pLeft           = NULL;
    pThis->Core.pRight          = NULL;
    pThis->Core.pList           = NULL;
    pThis->u32Magic             = RTLOCKVALCLASS_MAGIC;
    pThis->cRefs                = 1;
    pThis->fAutodidact          = fAutodidact;
    pThis->fRecursionOk         = fRecursionOk;
    pThis->fStrictReleaseOrder  = fStrictReleaseOrder;
    pThis->fInTree              = false;
    pThis->fDonateRefToNextRetainer = false;
    pThis->afReserved[0]        = false;
    pThis->afReserved[1]        = false;
    pThis->afReserved[2]        = false;
    pThis->cMsMinDeadlock       = cMsMinDeadlock;
    pThis->cMsMinOrder          = cMsMinOrder;
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->au32Reserved); i++)
        pThis->au32Reserved[i]  = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->PriorLocks.aRefs); i++)
    {
        pThis->PriorLocks.aRefs[i].hClass           = NIL_RTLOCKVALCLASS;
        pThis->PriorLocks.aRefs[i].cLookups         = 0;
        pThis->PriorLocks.aRefs[i].fAutodidacticism = false;
        pThis->PriorLocks.aRefs[i].afReserved[0]    = false;
        pThis->PriorLocks.aRefs[i].afReserved[1]    = false;
        pThis->PriorLocks.aRefs[i].afReserved[2]    = false;
    }
    pThis->PriorLocks.pNext     = NULL;
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->apPriorLocksHash); i++)
        pThis->apPriorLocksHash[i] = NULL;
    char *pszDst = (char *)(pThis + 1);
    pThis->pszName              = (char *)memcpy(pszDst, szName, cbName);
    pszDst += cbName;
    rtLockValidatorSrcPosCopy(&pThis->CreatePos, pSrcPos);
    pThis->CreatePos.pszFile    = pSrcPos->pszFile     ? (char *)memcpy(pszDst, pSrcPos->pszFile,     cbFile)     : NULL;
    pszDst += cbFile;
    pThis->CreatePos.pszFunction= pSrcPos->pszFunction ? (char *)memcpy(pszDst, pSrcPos->pszFunction, cbFunction) : NULL;
    Assert(rtLockValidatorSrcPosHash(&pThis->CreatePos) == pThis->Core.Key);
#ifdef RTLOCKVAL_WITH_CLASS_HASH_STATS
    pThis->cHashHits            = 0;
    pThis->cHashMisses          = 0;
#endif

    *phClass = pThis;
    return VINF_SUCCESS;
}


RTDECL(int) RTLockValidatorClassCreate(PRTLOCKVALCLASS phClass, bool fAutodidact, RT_SRC_POS_DECL, const char *pszNameFmt, ...)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_POS_NO_ID();
    va_list va;
    va_start(va, pszNameFmt);
    int rc = RTLockValidatorClassCreateExV(phClass, &SrcPos,
                                           fAutodidact, true /*fRecursionOk*/, false /*fStrictReleaseOrder*/,
                                           1 /*cMsMinDeadlock*/, 1 /*cMsMinOrder*/,
                                           pszNameFmt, va);
    va_end(va);
    return rc;
}


/**
 * Creates a new lock validator class with a reference that is consumed by the
 * first call to RTLockValidatorClassRetain.
 *
 * This is tailored for use in the parameter list of a semaphore constructor.
 *
 * @returns Class handle with a reference that is automatically consumed by the
 *          first retainer.  NIL_RTLOCKVALCLASS if we run into trouble.
 *
 * @param   SRC_POS             The source position where call is being made from.
 *                              Use RT_SRC_POS when possible.  Optional.
 * @param   pszNameFmt          Class name format string, optional (NULL).  Max
 *                              length is 32 bytes.
 * @param   ...                 Format string arguments.
 */
RTDECL(RTLOCKVALCLASS) RTLockValidatorClassCreateUnique(RT_SRC_POS_DECL, const char *pszNameFmt, ...)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_POS_NO_ID();
    RTLOCKVALCLASSINT *pClass;
    va_list va;
    va_start(va, pszNameFmt);
    int rc = RTLockValidatorClassCreateExV(&pClass, &SrcPos,
                                           true /*fAutodidact*/, true /*fRecursionOk*/, false /*fStrictReleaseOrder*/,
                                           1 /*cMsMinDeadlock*/, 1 /*cMsMinOrder*/,
                                           pszNameFmt, va);
    va_end(va);
    if (RT_FAILURE(rc))
        return NIL_RTLOCKVALCLASS;
    ASMAtomicWriteBool(&pClass->fDonateRefToNextRetainer, true); /* see rtLockValidatorClassRetain */
    return pClass;
}


/**
 * Internal class retainer.
 * @returns The new reference count.
 * @param   pClass              The class.
 */
DECL_FORCE_INLINE(uint32_t) rtLockValidatorClassRetain(RTLOCKVALCLASSINT *pClass)
{
    uint32_t cRefs = ASMAtomicIncU32(&pClass->cRefs);
    if (cRefs > RTLOCKVALCLASS_MAX_REFS)
        ASMAtomicWriteU32(&pClass->cRefs, RTLOCKVALCLASS_MAX_REFS);
    else if (   cRefs == 2
             && ASMAtomicXchgBool(&pClass->fDonateRefToNextRetainer, false))
        cRefs = ASMAtomicDecU32(&pClass->cRefs);
    return cRefs;
}


/**
 * Validates and retains a lock validator class.
 *
 * @returns @a hClass on success, NIL_RTLOCKVALCLASS on failure.
 * @param   hClass              The class handle.  NIL_RTLOCKVALCLASS is ok.
 */
DECL_FORCE_INLINE(RTLOCKVALCLASS) rtLockValidatorClassValidateAndRetain(RTLOCKVALCLASS hClass)
{
    if (hClass == NIL_RTLOCKVALCLASS)
        return hClass;
    AssertPtrReturn(hClass, NIL_RTLOCKVALCLASS);
    AssertReturn(hClass->u32Magic == RTLOCKVALCLASS_MAGIC, NIL_RTLOCKVALCLASS);
    rtLockValidatorClassRetain(hClass);
    return hClass;
}


/**
 * Internal class releaser.
 * @returns The new reference count.
 * @param   pClass              The class.
 */
DECLINLINE(uint32_t) rtLockValidatorClassRelease(RTLOCKVALCLASSINT *pClass)
{
    uint32_t cRefs = ASMAtomicDecU32(&pClass->cRefs);
    if (cRefs + 1 == RTLOCKVALCLASS_MAX_REFS)
        ASMAtomicWriteU32(&pClass->cRefs, RTLOCKVALCLASS_MAX_REFS);
    else if (!cRefs)
        rtLockValidatorClassDestroy(pClass);
    return cRefs;
}


/**
 * Destroys a class once there are not more references to it.
 *
 * @param   pClass              The class.
 */
static void rtLockValidatorClassDestroy(RTLOCKVALCLASSINT *pClass)
{
    AssertReturnVoid(!pClass->fInTree);
    ASMAtomicWriteU32(&pClass->u32Magic, RTLOCKVALCLASS_MAGIC_DEAD);

    PRTLOCKVALCLASSREFCHUNK pChunk = &pClass->PriorLocks;
    while (pChunk)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(pChunk->aRefs); i++)
        {
            RTLOCKVALCLASSINT *pClass2 = pChunk->aRefs[i].hClass;
            if (pClass2 != NIL_RTLOCKVALCLASS)
            {
                pChunk->aRefs[i].hClass = NIL_RTLOCKVALCLASS;
                rtLockValidatorClassRelease(pClass2);
            }
        }

        PRTLOCKVALCLASSREFCHUNK pNext = pChunk->pNext;
        pChunk->pNext = NULL;
        if (pChunk != &pClass->PriorLocks)
            RTMemFree(pChunk);
        pChunk = pNext;
    }

    RTMemFree(pClass);
}


RTDECL(RTLOCKVALCLASS) RTLockValidatorClassFindForSrcPos(PRTLOCKVALSRCPOS pSrcPos)
{
    if (g_hLockValClassTreeRWLock == NIL_RTSEMRW)
        rtLockValidatorLazyInit();
    int rcLock = RTSemRWRequestRead(g_hLockValClassTreeRWLock, RT_INDEFINITE_WAIT);

    uint32_t uSrcPosHash = rtLockValidatorSrcPosHash(pSrcPos);
    RTLOCKVALCLASSINT *pClass = (RTLOCKVALCLASSINT *)RTAvllU32Get(&g_LockValClassTree, uSrcPosHash);
    while (pClass)
    {
        if (rtLockValidatorSrcPosCompare(&pClass->CreatePos, pSrcPos) == 0)
            break;
        pClass = (RTLOCKVALCLASSINT *)pClass->Core.pList;
    }

    if (RT_SUCCESS(rcLock))
        RTSemRWReleaseRead(g_hLockValClassTreeRWLock);
    return pClass;
}


RTDECL(RTLOCKVALCLASS) RTLockValidatorClassForSrcPos(RT_SRC_POS_DECL, const char *pszNameFmt, ...)
{
    RTLOCKVALSRCPOS SrcPos = RTLOCKVALSRCPOS_INIT_POS_NO_ID();
    RTLOCKVALCLASS  hClass = RTLockValidatorClassFindForSrcPos(&SrcPos);
    if (hClass == NIL_RTLOCKVALCLASS)
    {
        /*
         * Create a new class and insert it into the tree.
         */
        va_list va;
        va_start(va, pszNameFmt);
        int rc = RTLockValidatorClassCreateExV(&hClass, &SrcPos,
                                               true /*fAutodidact*/, true /*fRecursionOk*/, false /*fStrictReleaseOrder*/,
                                               1 /*cMsMinDeadlock*/, 1 /*cMsMinOrder*/,
                                               pszNameFmt, va);
        va_end(va);
        if (RT_SUCCESS(rc))
        {
            if (g_hLockValClassTreeRWLock == NIL_RTSEMRW)
                rtLockValidatorLazyInit();
            int rcLock = RTSemRWRequestWrite(g_hLockValClassTreeRWLock, RT_INDEFINITE_WAIT);

            Assert(!hClass->fInTree);
            hClass->fInTree = RTAvllU32Insert(&g_LockValClassTree, &hClass->Core);
            Assert(hClass->fInTree);

            if (RT_SUCCESS(rcLock))
                RTSemRWReleaseWrite(g_hLockValClassTreeRWLock);
            return hClass;
        }
    }
    return hClass;
}


RTDECL(uint32_t) RTLockValidatorClassRetain(RTLOCKVALCLASS hClass)
{
    RTLOCKVALCLASSINT *pClass = hClass;
    AssertPtrReturn(pClass, UINT32_MAX);
    AssertReturn(pClass->u32Magic == RTLOCKVALCLASS_MAGIC, UINT32_MAX);
    return rtLockValidatorClassRetain(pClass);
}


RTDECL(uint32_t) RTLockValidatorClassRelease(RTLOCKVALCLASS hClass)
{
    RTLOCKVALCLASSINT *pClass = hClass;
    if (pClass == NIL_RTLOCKVALCLASS)
        return 0;
    AssertPtrReturn(pClass, UINT32_MAX);
    AssertReturn(pClass->u32Magic == RTLOCKVALCLASS_MAGIC, UINT32_MAX);
    return rtLockValidatorClassRelease(pClass);
}


/**
 * Worker for rtLockValidatorClassIsPriorClass that does a linear search thru
 * all the chunks for @a pPriorClass.
 *
 * @returns true / false.
 * @param   pClass              The class to search.
 * @param   pPriorClass         The class to search for.
 */
static bool rtLockValidatorClassIsPriorClassByLinearSearch(RTLOCKVALCLASSINT *pClass, RTLOCKVALCLASSINT *pPriorClass)
{
    for (PRTLOCKVALCLASSREFCHUNK pChunk = &pClass->PriorLocks; pChunk; pChunk = pChunk->pNext)
        for (uint32_t i = 0; i < RT_ELEMENTS(pChunk->aRefs); i++)
        {
            if (pChunk->aRefs[i].hClass == pPriorClass)
            {
                uint32_t cLookups = ASMAtomicIncU32(&pChunk->aRefs[i].cLookups);
                if (RT_UNLIKELY(cLookups >= RTLOCKVALCLASSREF_MAX_LOOKUPS_FIX))
                {
                    ASMAtomicWriteU32(&pChunk->aRefs[i].cLookups, RTLOCKVALCLASSREF_MAX_LOOKUPS);
                    cLookups = RTLOCKVALCLASSREF_MAX_LOOKUPS;
                }

                /* update the hash table entry. */
                PRTLOCKVALCLASSREF *ppHashEntry = &pClass->apPriorLocksHash[RTLOCKVALCLASS_HASH(pPriorClass)];
                if (    !(*ppHashEntry)
                    ||  (*ppHashEntry)->cLookups + 128 < cLookups)
                    ASMAtomicWritePtr(ppHashEntry, &pChunk->aRefs[i]);

#ifdef RTLOCKVAL_WITH_CLASS_HASH_STATS
                ASMAtomicIncU32(&pClass->cHashMisses);
#endif
                return true;
            }
        }

    return false;
}


/**
 * Checks if @a pPriorClass is a known prior class.
 *
 * @returns true / false.
 * @param   pClass              The class to search.
 * @param   pPriorClass         The class to search for.
 */
DECL_FORCE_INLINE(bool) rtLockValidatorClassIsPriorClass(RTLOCKVALCLASSINT *pClass, RTLOCKVALCLASSINT *pPriorClass)
{
    /*
     * Hash lookup here.
     */
    PRTLOCKVALCLASSREF pRef = pClass->apPriorLocksHash[RTLOCKVALCLASS_HASH(pPriorClass)];
    if (    pRef
        &&  pRef->hClass == pPriorClass)
    {
        uint32_t cLookups = ASMAtomicIncU32(&pRef->cLookups);
        if (RT_UNLIKELY(cLookups >= RTLOCKVALCLASSREF_MAX_LOOKUPS_FIX))
            ASMAtomicWriteU32(&pRef->cLookups, RTLOCKVALCLASSREF_MAX_LOOKUPS);
#ifdef RTLOCKVAL_WITH_CLASS_HASH_STATS
        ASMAtomicIncU32(&pClass->cHashHits);
#endif
        return true;
    }

    return rtLockValidatorClassIsPriorClassByLinearSearch(pClass, pPriorClass);
}


/**
 * Adds a class to the prior list.
 *
 * @returns VINF_SUCCESS, VERR_NO_MEMORY or VERR_SEM_LV_WRONG_ORDER.
 * @param   pClass              The class to work on.
 * @param   pPriorClass         The class to add.
 * @param   fAutodidacticism    Whether we're teaching ourselves (true) or
 *                              somebody is teaching us via the API (false).
 * @param   pSrcPos             Where this rule was added (optional).
 */
static int rtLockValidatorClassAddPriorClass(RTLOCKVALCLASSINT *pClass, RTLOCKVALCLASSINT *pPriorClass,
                                             bool fAutodidacticism, PCRTLOCKVALSRCPOS pSrcPos)
{
    NOREF(pSrcPos);
    if (!RTCritSectIsInitialized(&g_LockValClassTeachCS))
        rtLockValidatorLazyInit();
    int rcLock = RTCritSectEnter(&g_LockValClassTeachCS);

    /*
     * Check that there are no conflict (no assert since we might race each other).
     */
    int rc = VERR_SEM_LV_INTERNAL_ERROR;
    if (!rtLockValidatorClassIsPriorClass(pPriorClass, pClass))
    {
        if (!rtLockValidatorClassIsPriorClass(pClass, pPriorClass))
        {
            /*
             * Scan the table for a free entry, allocating a new chunk if necessary.
             */
            for (PRTLOCKVALCLASSREFCHUNK pChunk = &pClass->PriorLocks; ; pChunk = pChunk->pNext)
            {
                bool fDone = false;
                for (uint32_t i = 0; i < RT_ELEMENTS(pChunk->aRefs); i++)
                {
                    ASMAtomicCmpXchgHandle(&pChunk->aRefs[i].hClass, pPriorClass, NIL_RTLOCKVALCLASS, fDone);
                    if (fDone)
                    {
                        pChunk->aRefs[i].fAutodidacticism = fAutodidacticism;
                        rtLockValidatorClassRetain(pPriorClass);
                        rc = VINF_SUCCESS;
                        break;
                    }
                }
                if (fDone)
                    break;

                /* If no more chunks, allocate a new one and insert the class before linking it. */
                if (!pChunk->pNext)
                {
                    PRTLOCKVALCLASSREFCHUNK pNew = (PRTLOCKVALCLASSREFCHUNK)RTMemAlloc(sizeof(*pNew));
                    if (!pNew)
                    {
                        rc = VERR_NO_MEMORY;
                        break;
                    }
                    RTMEM_MAY_LEAK(pNew);
                    pNew->pNext = NULL;
                    for (uint32_t i = 0; i < RT_ELEMENTS(pNew->aRefs); i++)
                    {
                        pNew->aRefs[i].hClass           = NIL_RTLOCKVALCLASS;
                        pNew->aRefs[i].cLookups         = 0;
                        pNew->aRefs[i].fAutodidacticism = false;
                        pNew->aRefs[i].afReserved[0]    = false;
                        pNew->aRefs[i].afReserved[1]    = false;
                        pNew->aRefs[i].afReserved[2]    = false;
                    }

                    pNew->aRefs[0].hClass           = pPriorClass;
                    pNew->aRefs[0].fAutodidacticism = fAutodidacticism;

                    ASMAtomicWritePtr(&pChunk->pNext, pNew);
                    rtLockValidatorClassRetain(pPriorClass);
                    rc = VINF_SUCCESS;
                    break;
                }
            } /* chunk loop */
        }
        else
            rc = VINF_SUCCESS;
    }
    else
        rc = !g_fLockValSoftWrongOrder ? VERR_SEM_LV_WRONG_ORDER : VINF_SUCCESS;

    if (RT_SUCCESS(rcLock))
        RTCritSectLeave(&g_LockValClassTeachCS);
    return rc;
}


RTDECL(int) RTLockValidatorClassAddPriorClass(RTLOCKVALCLASS hClass, RTLOCKVALCLASS hPriorClass)
{
    RTLOCKVALCLASSINT *pClass = hClass;
    AssertPtrReturn(pClass, VERR_INVALID_HANDLE);
    AssertReturn(pClass->u32Magic == RTLOCKVALCLASS_MAGIC, VERR_INVALID_HANDLE);

    RTLOCKVALCLASSINT *pPriorClass = hPriorClass;
    AssertPtrReturn(pPriorClass, VERR_INVALID_HANDLE);
    AssertReturn(pPriorClass->u32Magic == RTLOCKVALCLASS_MAGIC, VERR_INVALID_HANDLE);

    return rtLockValidatorClassAddPriorClass(pClass, pPriorClass, false /*fAutodidacticism*/, NULL);
}


RTDECL(int) RTLockValidatorClassEnforceStrictReleaseOrder(RTLOCKVALCLASS hClass, bool fEnabled)
{
    RTLOCKVALCLASSINT *pClass = hClass;
    AssertPtrReturn(pClass, VERR_INVALID_HANDLE);
    AssertReturn(pClass->u32Magic == RTLOCKVALCLASS_MAGIC, VERR_INVALID_HANDLE);

    ASMAtomicWriteBool(&pClass->fStrictReleaseOrder, fEnabled);
    return VINF_SUCCESS;
}


/**
 * Unlinks all siblings.
 *
 * This is used during record deletion and assumes no races.
 *
 * @param   pCore               One of the siblings.
 */
static void rtLockValidatorUnlinkAllSiblings(PRTLOCKVALRECCORE pCore)
{
    /* ASSUMES sibling destruction doesn't involve any races and that all
       related records are to be disposed off now.  */
    PRTLOCKVALRECUNION pSibling = (PRTLOCKVALRECUNION)pCore;
    while (pSibling)
    {
        PRTLOCKVALRECUNION volatile *ppCoreNext;
        switch (pSibling->Core.u32Magic)
        {
            case RTLOCKVALRECEXCL_MAGIC:
            case RTLOCKVALRECEXCL_MAGIC_DEAD:
                ppCoreNext = &pSibling->Excl.pSibling;
                break;

            case RTLOCKVALRECSHRD_MAGIC:
            case RTLOCKVALRECSHRD_MAGIC_DEAD:
                ppCoreNext = &pSibling->Shared.pSibling;
                break;

            default:
                AssertFailed();
                ppCoreNext = NULL;
                break;
        }
        if (RT_UNLIKELY(ppCoreNext))
            break;
        pSibling = ASMAtomicXchgPtrT(ppCoreNext, NULL, PRTLOCKVALRECUNION);
    }
}


RTDECL(int) RTLockValidatorRecMakeSiblings(PRTLOCKVALRECCORE pRec1, PRTLOCKVALRECCORE pRec2)
{
    /*
     * Validate input.
     */
    PRTLOCKVALRECUNION p1 = (PRTLOCKVALRECUNION)pRec1;
    PRTLOCKVALRECUNION p2 = (PRTLOCKVALRECUNION)pRec2;

    AssertPtrReturn(p1, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(   p1->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC
                 || p1->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC
                 , VERR_SEM_LV_INVALID_PARAMETER);

    AssertPtrReturn(p2, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(   p2->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC
                 || p2->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC
                 , VERR_SEM_LV_INVALID_PARAMETER);

    /*
     * Link them (circular list).
     */
    if (    p1->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC
        &&  p2->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC)
    {
        p1->Excl.pSibling   = p2;
        p2->Shared.pSibling = p1;
    }
    else if (   p1->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC
             && p2->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC)
    {
        p1->Shared.pSibling = p2;
        p2->Excl.pSibling   = p1;
    }
    else
        AssertFailedReturn(VERR_SEM_LV_INVALID_PARAMETER); /* unsupported mix */

    return VINF_SUCCESS;
}


#if 0 /* unused */
/**
 * Gets the lock name for the given record.
 *
 * @returns Read-only lock name.
 * @param   pRec                The lock record.
 */
DECL_FORCE_INLINE(const char *) rtLockValidatorRecName(PRTLOCKVALRECUNION pRec)
{
    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
            return pRec->Excl.szName;
        case RTLOCKVALRECSHRD_MAGIC:
            return pRec->Shared.szName;
        case RTLOCKVALRECSHRDOWN_MAGIC:
            return pRec->ShrdOwner.pSharedRec ? pRec->ShrdOwner.pSharedRec->szName : "orphaned";
        case RTLOCKVALRECNEST_MAGIC:
            pRec = rtLockValidatorReadRecUnionPtr(&pRec->Nest.pRec);
            if (RT_VALID_PTR(pRec))
            {
                switch (pRec->Core.u32Magic)
                {
                    case RTLOCKVALRECEXCL_MAGIC:
                        return pRec->Excl.szName;
                    case RTLOCKVALRECSHRD_MAGIC:
                        return pRec->Shared.szName;
                    case RTLOCKVALRECSHRDOWN_MAGIC:
                        return pRec->ShrdOwner.pSharedRec ? pRec->ShrdOwner.pSharedRec->szName : "orphaned";
                    default:
                        return "unknown-nested";
                }
            }
            return "orphaned-nested";
        default:
            return "unknown";
    }
}
#endif /* unused */


#if 0 /* unused */
/**
 * Gets the class for this locking record.
 *
 * @returns Pointer to the class or NIL_RTLOCKVALCLASS.
 * @param   pRec                The lock validator record.
 */
DECLINLINE(RTLOCKVALCLASSINT *) rtLockValidatorRecGetClass(PRTLOCKVALRECUNION pRec)
{
    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
            return pRec->Excl.hClass;

        case RTLOCKVALRECSHRD_MAGIC:
            return pRec->Shared.hClass;

        case RTLOCKVALRECSHRDOWN_MAGIC:
        {
            PRTLOCKVALRECSHRD pSharedRec = pRec->ShrdOwner.pSharedRec;
            if (RT_LIKELY(   RT_VALID_PTR(pSharedRec)
                          && pSharedRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC))
                return pSharedRec->hClass;
            return NIL_RTLOCKVALCLASS;
        }

        case RTLOCKVALRECNEST_MAGIC:
        {
            PRTLOCKVALRECUNION pRealRec = pRec->Nest.pRec;
            if (RT_VALID_PTR(pRealRec))
            {
                switch (pRealRec->Core.u32Magic)
                {
                    case RTLOCKVALRECEXCL_MAGIC:
                        return pRealRec->Excl.hClass;

                    case RTLOCKVALRECSHRDOWN_MAGIC:
                    {
                        PRTLOCKVALRECSHRD pSharedRec = pRealRec->ShrdOwner.pSharedRec;
                        if (RT_LIKELY(   RT_VALID_PTR(pSharedRec)
                                      && pSharedRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC))
                            return pSharedRec->hClass;
                        break;
                    }

                    default:
                        AssertMsgFailed(("%p %p %#x\n", pRec, pRealRec, pRealRec->Core.u32Magic));
                        break;
                }
            }
            return NIL_RTLOCKVALCLASS;
        }

        default:
            AssertMsgFailed(("%#x\n", pRec->Core.u32Magic));
            return NIL_RTLOCKVALCLASS;
    }
}
#endif /* unused */

/**
 * Gets the class for this locking record and the pointer to the one below it in
 * the stack.
 *
 * @returns Pointer to the class or NIL_RTLOCKVALCLASS.
 * @param   pRec                The lock validator record.
 * @param   puSubClass          Where to return the sub-class.
 * @param   ppDown              Where to return the pointer to the record below.
 */
DECL_FORCE_INLINE(RTLOCKVALCLASSINT *)
rtLockValidatorRecGetClassesAndDown(PRTLOCKVALRECUNION pRec, uint32_t *puSubClass, PRTLOCKVALRECUNION *ppDown)
{
    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
            *ppDown = pRec->Excl.pDown;
            *puSubClass = pRec->Excl.uSubClass;
            return pRec->Excl.hClass;

        case RTLOCKVALRECSHRD_MAGIC:
            *ppDown = NULL;
            *puSubClass = pRec->Shared.uSubClass;
            return pRec->Shared.hClass;

        case RTLOCKVALRECSHRDOWN_MAGIC:
        {
            *ppDown = pRec->ShrdOwner.pDown;

            PRTLOCKVALRECSHRD pSharedRec = pRec->ShrdOwner.pSharedRec;
            if (RT_LIKELY(   RT_VALID_PTR(pSharedRec)
                          && pSharedRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC))
            {
                *puSubClass = pSharedRec->uSubClass;
                return pSharedRec->hClass;
            }
            *puSubClass = RTLOCKVAL_SUB_CLASS_NONE;
            return NIL_RTLOCKVALCLASS;
        }

        case RTLOCKVALRECNEST_MAGIC:
        {
            *ppDown = pRec->Nest.pDown;

            PRTLOCKVALRECUNION pRealRec = pRec->Nest.pRec;
            if (RT_VALID_PTR(pRealRec))
            {
                switch (pRealRec->Core.u32Magic)
                {
                    case RTLOCKVALRECEXCL_MAGIC:
                        *puSubClass = pRealRec->Excl.uSubClass;
                        return pRealRec->Excl.hClass;

                    case RTLOCKVALRECSHRDOWN_MAGIC:
                    {
                        PRTLOCKVALRECSHRD pSharedRec = pRealRec->ShrdOwner.pSharedRec;
                        if (RT_LIKELY(   RT_VALID_PTR(pSharedRec)
                                      && pSharedRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC))
                        {
                            *puSubClass = pSharedRec->uSubClass;
                            return pSharedRec->hClass;
                        }
                        break;
                    }

                    default:
                        AssertMsgFailed(("%p %p %#x\n", pRec, pRealRec, pRealRec->Core.u32Magic));
                        break;
                }
            }
            *puSubClass = RTLOCKVAL_SUB_CLASS_NONE;
            return NIL_RTLOCKVALCLASS;
        }

        default:
            AssertMsgFailed(("%#x\n", pRec->Core.u32Magic));
            *ppDown = NULL;
            *puSubClass = RTLOCKVAL_SUB_CLASS_NONE;
            return NIL_RTLOCKVALCLASS;
    }
}


/**
 * Gets the sub-class for a lock record.
 *
 * @returns the sub-class.
 * @param   pRec                The lock validator record.
 */
DECLINLINE(uint32_t) rtLockValidatorRecGetSubClass(PRTLOCKVALRECUNION pRec)
{
    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
            return pRec->Excl.uSubClass;

        case RTLOCKVALRECSHRD_MAGIC:
            return pRec->Shared.uSubClass;

        case RTLOCKVALRECSHRDOWN_MAGIC:
        {
            PRTLOCKVALRECSHRD pSharedRec = pRec->ShrdOwner.pSharedRec;
            if (RT_LIKELY(   RT_VALID_PTR(pSharedRec)
                          && pSharedRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC))
                return pSharedRec->uSubClass;
            return RTLOCKVAL_SUB_CLASS_NONE;
        }

        case RTLOCKVALRECNEST_MAGIC:
        {
            PRTLOCKVALRECUNION pRealRec = pRec->Nest.pRec;
            if (RT_VALID_PTR(pRealRec))
            {
                switch (pRealRec->Core.u32Magic)
                {
                    case RTLOCKVALRECEXCL_MAGIC:
                        return pRec->Excl.uSubClass;

                    case RTLOCKVALRECSHRDOWN_MAGIC:
                    {
                        PRTLOCKVALRECSHRD pSharedRec = pRealRec->ShrdOwner.pSharedRec;
                        if (RT_LIKELY(   RT_VALID_PTR(pSharedRec)
                                      && pSharedRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC))
                            return pSharedRec->uSubClass;
                        break;
                    }

                    default:
                        AssertMsgFailed(("%p %p %#x\n", pRec, pRealRec, pRealRec->Core.u32Magic));
                        break;
                }
            }
            return RTLOCKVAL_SUB_CLASS_NONE;
        }

        default:
            AssertMsgFailed(("%#x\n", pRec->Core.u32Magic));
            return RTLOCKVAL_SUB_CLASS_NONE;
    }
}




/**
 * Calculates the depth of a lock stack.
 *
 * @returns Number of stack frames.
 * @param   pThread         The thread.
 */
static uint32_t rtLockValidatorStackDepth(PRTTHREADINT pThread)
{
    uint32_t            cEntries = 0;
    PRTLOCKVALRECUNION  pCur = rtLockValidatorReadRecUnionPtr(&pThread->LockValidator.pStackTop);
    while (RT_VALID_PTR(pCur))
    {
        switch (pCur->Core.u32Magic)
        {
            case RTLOCKVALRECEXCL_MAGIC:
                pCur = rtLockValidatorReadRecUnionPtr(&pCur->Excl.pDown);
                break;

            case RTLOCKVALRECSHRDOWN_MAGIC:
                pCur = rtLockValidatorReadRecUnionPtr(&pCur->ShrdOwner.pDown);
                break;

            case RTLOCKVALRECNEST_MAGIC:
                pCur = rtLockValidatorReadRecUnionPtr(&pCur->Nest.pDown);
                break;

            default:
                AssertMsgFailedReturn(("%#x\n", pCur->Core.u32Magic), cEntries);
        }
        cEntries++;
    }
    return cEntries;
}


#ifdef RT_STRICT
/**
 * Checks if the stack contains @a pRec.
 *
 * @returns true / false.
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The lock record.
 */
static bool rtLockValidatorStackContainsRec(PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION pRec)
{
    PRTLOCKVALRECUNION pCur = pThreadSelf->LockValidator.pStackTop;
    while (pCur)
    {
        AssertPtrReturn(pCur, false);
        if (pCur == pRec)
            return true;
        switch (pCur->Core.u32Magic)
        {
            case RTLOCKVALRECEXCL_MAGIC:
                Assert(pCur->Excl.cRecursion >= 1);
                pCur = pCur->Excl.pDown;
                break;

            case RTLOCKVALRECSHRDOWN_MAGIC:
                Assert(pCur->ShrdOwner.cRecursion >= 1);
                pCur = pCur->ShrdOwner.pDown;
                break;

            case RTLOCKVALRECNEST_MAGIC:
                Assert(pCur->Nest.cRecursion > 1);
                pCur = pCur->Nest.pDown;
                break;

            default:
                AssertMsgFailedReturn(("%#x\n", pCur->Core.u32Magic), false);
        }
    }
    return false;
}
#endif /* RT_STRICT */


/**
 * Pushes a lock record onto the stack.
 *
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The lock record.
 */
static void rtLockValidatorStackPush(PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION pRec)
{
    Assert(pThreadSelf == RTThreadSelf());
    Assert(!rtLockValidatorStackContainsRec(pThreadSelf, pRec));

    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
            Assert(pRec->Excl.cRecursion == 1);
            Assert(pRec->Excl.pDown == NULL);
            rtLockValidatorWriteRecUnionPtr(&pRec->Excl.pDown, pThreadSelf->LockValidator.pStackTop);
            break;

        case RTLOCKVALRECSHRDOWN_MAGIC:
            Assert(pRec->ShrdOwner.cRecursion == 1);
            Assert(pRec->ShrdOwner.pDown == NULL);
            rtLockValidatorWriteRecUnionPtr(&pRec->ShrdOwner.pDown, pThreadSelf->LockValidator.pStackTop);
            break;

        default:
            AssertMsgFailedReturnVoid(("%#x\n",  pRec->Core.u32Magic));
    }
    rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pStackTop, pRec);
}


/**
 * Pops a lock record off the stack.
 *
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The lock.
 */
static void rtLockValidatorStackPop(PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION pRec)
{
    Assert(pThreadSelf == RTThreadSelf());

    PRTLOCKVALRECUNION pDown;
    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
            Assert(pRec->Excl.cRecursion == 0);
            pDown = pRec->Excl.pDown;
            rtLockValidatorWriteRecUnionPtr(&pRec->Excl.pDown, NULL); /* lazy bird */
            break;

        case RTLOCKVALRECSHRDOWN_MAGIC:
            Assert(pRec->ShrdOwner.cRecursion == 0);
            pDown = pRec->ShrdOwner.pDown;
            rtLockValidatorWriteRecUnionPtr(&pRec->ShrdOwner.pDown, NULL);
            break;

        default:
            AssertMsgFailedReturnVoid(("%#x\n",  pRec->Core.u32Magic));
    }
    if (pThreadSelf->LockValidator.pStackTop == pRec)
        rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pStackTop, pDown);
    else
    {
        /* Find the pointer to our record and unlink ourselves. */
        PRTLOCKVALRECUNION pCur = pThreadSelf->LockValidator.pStackTop;
        while (pCur)
        {
            PRTLOCKVALRECUNION volatile *ppDown;
            switch (pCur->Core.u32Magic)
            {
                case RTLOCKVALRECEXCL_MAGIC:
                    Assert(pCur->Excl.cRecursion >= 1);
                    ppDown = &pCur->Excl.pDown;
                    break;

                case RTLOCKVALRECSHRDOWN_MAGIC:
                    Assert(pCur->ShrdOwner.cRecursion >= 1);
                    ppDown = &pCur->ShrdOwner.pDown;
                    break;

                case RTLOCKVALRECNEST_MAGIC:
                    Assert(pCur->Nest.cRecursion >= 1);
                    ppDown = &pCur->Nest.pDown;
                    break;

                default:
                    AssertMsgFailedReturnVoid(("%#x\n", pCur->Core.u32Magic));
            }
            pCur = *ppDown;
            if (pCur == pRec)
            {
                rtLockValidatorWriteRecUnionPtr(ppDown, pDown);
                return;
            }
        }
        AssertMsgFailed(("%p %p\n", pRec, pThreadSelf));
    }
}


/**
 * Creates and pushes lock recursion record onto the stack.
 *
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The lock record.
 * @param   pSrcPos             Where the recursion occurred.
 */
static void rtLockValidatorStackPushRecursion(PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION pRec, PCRTLOCKVALSRCPOS pSrcPos)
{
    Assert(pThreadSelf == RTThreadSelf());
    Assert(rtLockValidatorStackContainsRec(pThreadSelf, pRec));

#ifdef RTLOCKVAL_WITH_RECURSION_RECORDS
    /*
     * Allocate a new recursion record
     */
    PRTLOCKVALRECNEST pRecursionRec = pThreadSelf->LockValidator.pFreeNestRecs;
    if (pRecursionRec)
        pThreadSelf->LockValidator.pFreeNestRecs = pRecursionRec->pNextFree;
    else
    {
        pRecursionRec = (PRTLOCKVALRECNEST)RTMemAlloc(sizeof(*pRecursionRec));
        if (!pRecursionRec)
            return;
    }

    /*
     * Initialize it.
     */
    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:
            pRecursionRec->cRecursion = pRec->Excl.cRecursion;
            break;

        case RTLOCKVALRECSHRDOWN_MAGIC:
            pRecursionRec->cRecursion = pRec->ShrdOwner.cRecursion;
            break;

        default:
            AssertMsgFailed(("%#x\n",  pRec->Core.u32Magic));
            rtLockValidatorSerializeDestructEnter();
            rtLockValidatorSerializeDestructLeave();
            RTMemFree(pRecursionRec);
            return;
    }
    Assert(pRecursionRec->cRecursion > 1);
    pRecursionRec->pRec          = pRec;
    pRecursionRec->pDown         = NULL;
    pRecursionRec->pNextFree     = NULL;
    rtLockValidatorSrcPosCopy(&pRecursionRec->SrcPos, pSrcPos);
    pRecursionRec->Core.u32Magic = RTLOCKVALRECNEST_MAGIC;

    /*
     * Link it.
     */
    pRecursionRec->pDown = pThreadSelf->LockValidator.pStackTop;
    rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pStackTop, (PRTLOCKVALRECUNION)pRecursionRec);
#endif /* RTLOCKVAL_WITH_RECURSION_RECORDS */
}


/**
 * Pops a lock recursion record off the stack.
 *
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The lock record.
 */
static void rtLockValidatorStackPopRecursion(PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION pRec)
{
    Assert(pThreadSelf == RTThreadSelf());
    Assert(rtLockValidatorStackContainsRec(pThreadSelf, pRec));

    uint32_t cRecursion;
    switch (pRec->Core.u32Magic)
    {
        case RTLOCKVALRECEXCL_MAGIC:    cRecursion = pRec->Excl.cRecursion; break;
        case RTLOCKVALRECSHRDOWN_MAGIC: cRecursion = pRec->ShrdOwner.cRecursion; break;
        default:                        AssertMsgFailedReturnVoid(("%#x\n",  pRec->Core.u32Magic));
    }
    Assert(cRecursion >= 1);

#ifdef RTLOCKVAL_WITH_RECURSION_RECORDS
    /*
     * Pop the recursion record.
     */
    PRTLOCKVALRECUNION pNest = pThreadSelf->LockValidator.pStackTop;
    if (   pNest != NULL
        && pNest->Core.u32Magic == RTLOCKVALRECNEST_MAGIC
        && pNest->Nest.pRec == pRec
       )
    {
        Assert(pNest->Nest.cRecursion == cRecursion + 1);
        rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pStackTop, pNest->Nest.pDown);
    }
    else
    {
        /* Find the record above ours. */
        PRTLOCKVALRECUNION volatile *ppDown = NULL;
        for (;;)
        {
            AssertMsgReturnVoid(pNest, ("%p %p\n", pRec, pThreadSelf));
            switch (pNest->Core.u32Magic)
            {
                case RTLOCKVALRECEXCL_MAGIC:
                    ppDown = &pNest->Excl.pDown;
                    pNest = *ppDown;
                    continue;
                case RTLOCKVALRECSHRDOWN_MAGIC:
                    ppDown = &pNest->ShrdOwner.pDown;
                    pNest = *ppDown;
                    continue;
                case RTLOCKVALRECNEST_MAGIC:
                    if (pNest->Nest.pRec == pRec)
                        break;
                    ppDown = &pNest->Nest.pDown;
                    pNest = *ppDown;
                    continue;
                default:
                    AssertMsgFailedReturnVoid(("%#x\n", pNest->Core.u32Magic));
            }
            break; /* ugly */
        }
        Assert(pNest->Nest.cRecursion == cRecursion + 1);
        rtLockValidatorWriteRecUnionPtr(ppDown, pNest->Nest.pDown);
    }

    /*
     * Invalidate and free the record.
     */
    ASMAtomicWriteU32(&pNest->Core.u32Magic, RTLOCKVALRECNEST_MAGIC);
    rtLockValidatorWriteRecUnionPtr(&pNest->Nest.pDown, NULL);
    rtLockValidatorWriteRecUnionPtr(&pNest->Nest.pRec, NULL);
    pNest->Nest.cRecursion = 0;
    pNest->Nest.pNextFree  = pThreadSelf->LockValidator.pFreeNestRecs;
    pThreadSelf->LockValidator.pFreeNestRecs = &pNest->Nest;
#endif /* RTLOCKVAL_WITH_RECURSION_RECORDS */
}


/**
 * Helper for rtLockValidatorStackCheckLockingOrder that does the bitching and
 * returns VERR_SEM_LV_WRONG_ORDER.
 */
static int rtLockValidatorStackWrongOrder(const char *pszWhat, PCRTLOCKVALSRCPOS pSrcPos, PRTTHREADINT pThreadSelf,
                                          PRTLOCKVALRECUNION pRec1, PRTLOCKVALRECUNION pRec2,
                                          RTLOCKVALCLASSINT *pClass1, RTLOCKVALCLASSINT *pClass2)


{
    rtLockValComplainFirst(pszWhat, pSrcPos, pThreadSelf, pRec1, false);
    rtLockValComplainAboutLock("Other lock:   ", pRec2, "\n");
    rtLockValComplainAboutClass("My class:    ", pClass1, rtLockValidatorRecGetSubClass(pRec1), true /*fVerbose*/);
    rtLockValComplainAboutClass("Other class: ", pClass2, rtLockValidatorRecGetSubClass(pRec2), true /*fVerbose*/);
    rtLockValComplainAboutLockStack(pThreadSelf, 0, 0, pRec2);
    rtLockValComplainPanic();
    return !g_fLockValSoftWrongOrder ? VERR_SEM_LV_WRONG_ORDER : VINF_SUCCESS;
}


/**
 * Checks if the sub-class order is ok or not.
 *
 * Used to deal with two locks from the same class.
 *
 * @returns true if ok, false if not.
 * @param   uSubClass1          The sub-class of the lock that is being
 *                              considered.
 * @param   uSubClass2          The sub-class of the lock that is already being
 *                              held.
 */
DECL_FORCE_INLINE(bool) rtLockValidatorIsSubClassOrderOk(uint32_t uSubClass1, uint32_t uSubClass2)
{
    if (uSubClass1 > uSubClass2)
    {
        /* NONE kills ANY. */
        if (uSubClass2 == RTLOCKVAL_SUB_CLASS_NONE)
            return false;
        return true;
    }

    /* ANY counters all USER values. (uSubClass1 == NONE only if they are equal) */
    AssertCompile(RTLOCKVAL_SUB_CLASS_ANY > RTLOCKVAL_SUB_CLASS_NONE);
    if (uSubClass1 == RTLOCKVAL_SUB_CLASS_ANY)
        return true;
    return false;
}


/**
 * Checks if the class and sub-class lock order is ok.
 *
 * @returns true if ok, false if not.
 * @param   pClass1             The class of the lock that is being considered.
 * @param   uSubClass1          The sub-class that goes with @a pClass1.
 * @param   pClass2             The class of the lock that is already being
 *                              held.
 * @param   uSubClass2          The sub-class that goes with @a pClass2.
 */
DECL_FORCE_INLINE(bool) rtLockValidatorIsClassOrderOk(RTLOCKVALCLASSINT *pClass1, uint32_t uSubClass1,
                                                      RTLOCKVALCLASSINT *pClass2, uint32_t uSubClass2)
{
    if (pClass1 == pClass2)
        return rtLockValidatorIsSubClassOrderOk(uSubClass1, uSubClass2);
    return rtLockValidatorClassIsPriorClass(pClass1, pClass2);
}


/**
 * Checks the locking order, part two.
 *
 * @returns VINF_SUCCESS, VERR_SEM_LV_WRONG_ORDER or VERR_SEM_LV_INTERNAL_ERROR.
 * @param   pClass              The lock class.
 * @param   uSubClass           The lock sub-class.
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The lock record.
 * @param   pSrcPos             The source position of the locking operation.
 * @param   pFirstBadClass      The first bad class.
 * @param   pFirstBadRec        The first bad lock record.
 * @param   pFirstBadDown       The next record on the lock stack.
 */
static int rtLockValidatorStackCheckLockingOrder2(RTLOCKVALCLASSINT * const pClass, uint32_t const uSubClass,
                                                  PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION const pRec,
                                                  PCRTLOCKVALSRCPOS const   pSrcPos,
                                                  RTLOCKVALCLASSINT * const pFirstBadClass,
                                                  PRTLOCKVALRECUNION const  pFirstBadRec,
                                                  PRTLOCKVALRECUNION const  pFirstBadDown)
{
    /*
     * Something went wrong, pCur is pointing to where.
     */
    if (   pClass == pFirstBadClass
        || rtLockValidatorClassIsPriorClass(pFirstBadClass, pClass))
        return rtLockValidatorStackWrongOrder("Wrong locking order!", pSrcPos, pThreadSelf,
                                              pRec, pFirstBadRec, pClass, pFirstBadClass);
    if (!pClass->fAutodidact)
        return rtLockValidatorStackWrongOrder("Wrong locking order! (unknown)", pSrcPos, pThreadSelf,
                                              pRec, pFirstBadRec, pClass, pFirstBadClass);

    /*
     * This class is an autodidact, so we have to check out the rest of the stack
     * for direct violations.
     */
    uint32_t           cNewRules = 1;
    PRTLOCKVALRECUNION pCur      = pFirstBadDown;
    while (pCur)
    {
        AssertPtrReturn(pCur, VERR_SEM_LV_INTERNAL_ERROR);

        if (pCur->Core.u32Magic == RTLOCKVALRECNEST_MAGIC)
            pCur = pCur->Nest.pDown;
        else
        {
            PRTLOCKVALRECUNION  pDown;
            uint32_t            uPriorSubClass;
            RTLOCKVALCLASSINT  *pPriorClass = rtLockValidatorRecGetClassesAndDown(pCur, &uPriorSubClass, &pDown);
            if (pPriorClass != NIL_RTLOCKVALCLASS)
            {
                AssertPtrReturn(pPriorClass, VERR_SEM_LV_INTERNAL_ERROR);
                AssertReturn(pPriorClass->u32Magic == RTLOCKVALCLASS_MAGIC, VERR_SEM_LV_INTERNAL_ERROR);
                if (!rtLockValidatorIsClassOrderOk(pClass, uSubClass, pPriorClass, uPriorSubClass))
                {
                    if (   pClass == pPriorClass
                        || rtLockValidatorClassIsPriorClass(pPriorClass, pClass))
                        return rtLockValidatorStackWrongOrder("Wrong locking order! (more than one)", pSrcPos, pThreadSelf,
                                                              pRec, pCur, pClass, pPriorClass);
                    cNewRules++;
                }
            }
            pCur = pDown;
        }
    }

    if (cNewRules == 1)
    {
        /*
         * Special case the simple operation, hoping that it will be a
         * frequent case.
         */
        int rc = rtLockValidatorClassAddPriorClass(pClass, pFirstBadClass, true /*fAutodidacticism*/, pSrcPos);
        if (rc == VERR_SEM_LV_WRONG_ORDER)
            return rtLockValidatorStackWrongOrder("Wrong locking order! (race)", pSrcPos, pThreadSelf,
                                                  pRec, pFirstBadRec, pClass, pFirstBadClass);
        Assert(RT_SUCCESS(rc) || rc == VERR_NO_MEMORY);
    }
    else
    {
        /*
         * We may be adding more than one rule, so we have to take the lock
         * before starting to add the rules.  This means we have to check
         * the state after taking it since we might be racing someone adding
         * a conflicting rule.
         */
        if (!RTCritSectIsInitialized(&g_LockValClassTeachCS))
            rtLockValidatorLazyInit();
        int rcLock = RTCritSectEnter(&g_LockValClassTeachCS);

        /* Check */
        pCur = pFirstBadRec;
        while (pCur)
        {
            if (pCur->Core.u32Magic == RTLOCKVALRECNEST_MAGIC)
                pCur = pCur->Nest.pDown;
            else
            {
                uint32_t            uPriorSubClass;
                PRTLOCKVALRECUNION  pDown;
                RTLOCKVALCLASSINT  *pPriorClass = rtLockValidatorRecGetClassesAndDown(pCur, &uPriorSubClass, &pDown);
                if (pPriorClass != NIL_RTLOCKVALCLASS)
                {
                    if (!rtLockValidatorIsClassOrderOk(pClass, uSubClass, pPriorClass, uPriorSubClass))
                    {
                        if (   pClass == pPriorClass
                            || rtLockValidatorClassIsPriorClass(pPriorClass, pClass))
                        {
                            if (RT_SUCCESS(rcLock))
                                RTCritSectLeave(&g_LockValClassTeachCS);
                            return rtLockValidatorStackWrongOrder("Wrong locking order! (2nd)", pSrcPos, pThreadSelf,
                                                                  pRec, pCur, pClass, pPriorClass);
                        }
                    }
                }
                pCur = pDown;
            }
        }

        /* Iterate the stack yet again, adding new rules this time. */
        pCur = pFirstBadRec;
        while (pCur)
        {
            if (pCur->Core.u32Magic == RTLOCKVALRECNEST_MAGIC)
                pCur = pCur->Nest.pDown;
            else
            {
                uint32_t            uPriorSubClass;
                PRTLOCKVALRECUNION  pDown;
                RTLOCKVALCLASSINT  *pPriorClass = rtLockValidatorRecGetClassesAndDown(pCur, &uPriorSubClass, &pDown);
                if (pPriorClass != NIL_RTLOCKVALCLASS)
                {
                    if (!rtLockValidatorIsClassOrderOk(pClass, uSubClass, pPriorClass, uPriorSubClass))
                    {
                        Assert(   pClass != pPriorClass
                               && !rtLockValidatorClassIsPriorClass(pPriorClass, pClass));
                        int rc = rtLockValidatorClassAddPriorClass(pClass, pPriorClass, true /*fAutodidacticism*/, pSrcPos);
                        if (RT_FAILURE(rc))
                        {
                            Assert(rc == VERR_NO_MEMORY);
                            break;
                        }
                        Assert(rtLockValidatorClassIsPriorClass(pClass, pPriorClass));
                    }
                }
                pCur = pDown;
            }
        }

        if (RT_SUCCESS(rcLock))
            RTCritSectLeave(&g_LockValClassTeachCS);
    }

    return VINF_SUCCESS;
}



/**
 * Checks the locking order.
 *
 * @returns VINF_SUCCESS, VERR_SEM_LV_WRONG_ORDER or VERR_SEM_LV_INTERNAL_ERROR.
 * @param   pClass              The lock class.
 * @param   uSubClass           The lock sub-class.
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The lock record.
 * @param   pSrcPos             The source position of the locking operation.
 */
static int rtLockValidatorStackCheckLockingOrder(RTLOCKVALCLASSINT * const pClass, uint32_t const uSubClass,
                                                 PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION const pRec,
                                                 PCRTLOCKVALSRCPOS pSrcPos)
{
    /*
     * Some internal paranoia first.
     */
    AssertPtr(pClass);
    Assert(pClass->u32Magic == RTLOCKVALCLASS_MAGIC);
    AssertPtr(pThreadSelf);
    Assert(pThreadSelf->u32Magic == RTTHREADINT_MAGIC);
    AssertPtr(pRec);
    AssertPtrNull(pSrcPos);

    /*
     * Walk the stack, delegate problems to a worker routine.
     */
    PRTLOCKVALRECUNION pCur = pThreadSelf->LockValidator.pStackTop;
    if (!pCur)
        return VINF_SUCCESS;

    for (;;)
    {
        AssertPtrReturn(pCur, VERR_SEM_LV_INTERNAL_ERROR);

        if (pCur->Core.u32Magic == RTLOCKVALRECNEST_MAGIC)
            pCur = pCur->Nest.pDown;
        else
        {
            uint32_t            uPriorSubClass;
            PRTLOCKVALRECUNION  pDown;
            RTLOCKVALCLASSINT  *pPriorClass = rtLockValidatorRecGetClassesAndDown(pCur, &uPriorSubClass, &pDown);
            if (pPriorClass != NIL_RTLOCKVALCLASS)
            {
                AssertPtrReturn(pPriorClass, VERR_SEM_LV_INTERNAL_ERROR);
                AssertReturn(pPriorClass->u32Magic == RTLOCKVALCLASS_MAGIC, VERR_SEM_LV_INTERNAL_ERROR);
                if (RT_UNLIKELY(!rtLockValidatorIsClassOrderOk(pClass, uSubClass, pPriorClass, uPriorSubClass)))
                    return rtLockValidatorStackCheckLockingOrder2(pClass, uSubClass, pThreadSelf, pRec, pSrcPos,
                                                                  pPriorClass, pCur, pDown);
            }
            pCur = pDown;
        }
        if (!pCur)
            return VINF_SUCCESS;
    }
}


/**
 * Check that the lock record is the topmost one on the stack, complain and fail
 * if it isn't.
 *
 * @returns VINF_SUCCESS, VERR_SEM_LV_WRONG_RELEASE_ORDER or
 *          VERR_SEM_LV_INVALID_PARAMETER.
 * @param   pThreadSelf         The current thread.
 * @param   pRec                The record.
 */
static int rtLockValidatorStackCheckReleaseOrder(PRTTHREADINT pThreadSelf, PRTLOCKVALRECUNION pRec)
{
    AssertReturn(pThreadSelf != NIL_RTTHREAD, VERR_SEM_LV_INVALID_PARAMETER);
    Assert(pThreadSelf == RTThreadSelf());

    PRTLOCKVALRECUNION pTop = pThreadSelf->LockValidator.pStackTop;
    if (RT_LIKELY(   pTop == pRec
                  || (   pTop
                      && pTop->Core.u32Magic == RTLOCKVALRECNEST_MAGIC
                      && pTop->Nest.pRec == pRec) ))
        return VINF_SUCCESS;

#ifdef RTLOCKVAL_WITH_RECURSION_RECORDS
    /* Look for a recursion record so the right frame is dumped and marked. */
    while (pTop)
    {
        if (pTop->Core.u32Magic == RTLOCKVALRECNEST_MAGIC)
        {
            if (pTop->Nest.pRec == pRec)
            {
                pRec = pTop;
                break;
            }
            pTop = pTop->Nest.pDown;
        }
        else if (pTop->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC)
            pTop = pTop->Excl.pDown;
        else if (pTop->Core.u32Magic == RTLOCKVALRECSHRDOWN_MAGIC)
            pTop = pTop->ShrdOwner.pDown;
        else
            break;
    }
#endif

    rtLockValComplainFirst("Wrong release order!", NULL, pThreadSelf, pRec, true);
    rtLockValComplainPanic();
    return !g_fLockValSoftWrongOrder ? VERR_SEM_LV_WRONG_RELEASE_ORDER : VINF_SUCCESS;
}


/**
 * Checks if all owners are blocked - shared record operated in signaller mode.
 *
 * @returns true / false accordingly.
 * @param   pRec                The record.
 * @param   pThreadSelf         The current thread.
 */
DECL_FORCE_INLINE(bool) rtLockValidatorDdAreAllThreadsBlocked(PRTLOCKVALRECSHRD pRec, PRTTHREADINT pThreadSelf)
{
    PRTLOCKVALRECSHRDOWN volatile  *papOwners  = pRec->papOwners;
    uint32_t                        cAllocated = pRec->cAllocated;
    uint32_t                        cEntries   = ASMAtomicUoReadU32(&pRec->cEntries);
    if (cEntries == 0)
        return false;

    for (uint32_t i = 0; i < cAllocated; i++)
    {
        PRTLOCKVALRECSHRDOWN pEntry = rtLockValidatorUoReadSharedOwner(&papOwners[i]);
        if (   pEntry
            && pEntry->Core.u32Magic == RTLOCKVALRECSHRDOWN_MAGIC)
        {
            PRTTHREADINT pCurThread = rtLockValidatorReadThreadHandle(&pEntry->hThread);
            if (!pCurThread)
                return false;
            if (pCurThread->u32Magic != RTTHREADINT_MAGIC)
                return false;
            if (   !RTTHREAD_IS_SLEEPING(rtThreadGetState(pCurThread))
                && pCurThread != pThreadSelf)
                return false;
            if (--cEntries == 0)
                break;
        }
        else
            Assert(!pEntry || pEntry->Core.u32Magic == RTLOCKVALRECSHRDOWN_MAGIC_DEAD);
    }

    return true;
}


/**
 * Verifies the deadlock stack before calling it a deadlock.
 *
 * @retval  VERR_SEM_LV_DEADLOCK if it's a deadlock.
 * @retval  VERR_SEM_LV_ILLEGAL_UPGRADE if it's a deadlock on the same lock.
 * @retval  VERR_TRY_AGAIN if something changed.
 *
 * @param   pStack              The deadlock detection stack.
 * @param   pThreadSelf         The current thread.
 */
static int rtLockValidatorDdVerifyDeadlock(PRTLOCKVALDDSTACK pStack, PRTTHREADINT pThreadSelf)
{
    uint32_t const c = pStack->c;
    for (uint32_t iPass = 0; iPass < 3; iPass++)
    {
        for (uint32_t i = 1; i < c; i++)
        {
            PRTTHREADINT pThread = pStack->a[i].pThread;
            if (pThread->u32Magic != RTTHREADINT_MAGIC)
                return VERR_TRY_AGAIN;
            if (rtThreadGetState(pThread) != pStack->a[i].enmState)
                return VERR_TRY_AGAIN;
            if (rtLockValidatorReadRecUnionPtr(&pThread->LockValidator.pRec) != pStack->a[i].pFirstSibling)
                return VERR_TRY_AGAIN;
            /* ASSUMES the signaller records won't have siblings! */
            PRTLOCKVALRECUNION pRec = pStack->a[i].pRec;
            if (   pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC
                && pRec->Shared.fSignaller
                && !rtLockValidatorDdAreAllThreadsBlocked(&pRec->Shared, pThreadSelf))
                return VERR_TRY_AGAIN;
        }
        RTThreadYield();
    }

    if (c == 1)
        return VERR_SEM_LV_ILLEGAL_UPGRADE;
    return VERR_SEM_LV_DEADLOCK;
}


/**
 * Checks for stack cycles caused by another deadlock before returning.
 *
 * @retval  VINF_SUCCESS if the stack is simply too small.
 * @retval  VERR_SEM_LV_EXISTING_DEADLOCK if a cycle was detected.
 *
 * @param   pStack              The deadlock detection stack.
 */
static int rtLockValidatorDdHandleStackOverflow(PRTLOCKVALDDSTACK pStack)
{
    for (size_t i = 0; i < RT_ELEMENTS(pStack->a) - 1; i++)
    {
        PRTTHREADINT pThread = pStack->a[i].pThread;
        for (size_t j = i + 1; j < RT_ELEMENTS(pStack->a); j++)
            if (pStack->a[j].pThread == pThread)
                return VERR_SEM_LV_EXISTING_DEADLOCK;
    }
    static bool volatile s_fComplained = false;
    if (!s_fComplained)
    {
        s_fComplained = true;
        rtLockValComplain(RT_SRC_POS, "lock validator stack is too small! (%zu entries)\n", RT_ELEMENTS(pStack->a));
    }
    return VINF_SUCCESS;
}


/**
 * Worker for rtLockValidatorDeadlockDetection that does the actual deadlock
 * detection.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_SEM_LV_DEADLOCK
 * @retval  VERR_SEM_LV_EXISTING_DEADLOCK
 * @retval  VERR_SEM_LV_ILLEGAL_UPGRADE
 * @retval  VERR_TRY_AGAIN
 *
 * @param   pStack          The stack to use.
 * @param   pOriginalRec    The original record.
 * @param   pThreadSelf     The calling thread.
 */
static int rtLockValidatorDdDoDetection(PRTLOCKVALDDSTACK pStack, PRTLOCKVALRECUNION const pOriginalRec,
                                        PRTTHREADINT const pThreadSelf)
{
    pStack->c = 0;

    /* We could use a single RTLOCKVALDDENTRY variable here, but the
       compiler may make a better job of it when using individual variables. */
    PRTLOCKVALRECUNION  pRec            = pOriginalRec;
    PRTLOCKVALRECUNION  pFirstSibling   = pOriginalRec;
    uint32_t            iEntry          = UINT32_MAX;
    PRTTHREADINT        pThread         = NIL_RTTHREAD;
    RTTHREADSTATE       enmState        = RTTHREADSTATE_RUNNING;
    for (uint32_t iLoop = 0; ; iLoop++)
    {
        /*
         * Process the current record.
         */
        RTLOCKVAL_ASSERT_PTR_ALIGN(pRec);

        /* Find the next relevant owner thread and record. */
        PRTLOCKVALRECUNION  pNextRec     = NULL;
        RTTHREADSTATE       enmNextState = RTTHREADSTATE_RUNNING;
        PRTTHREADINT        pNextThread  = NIL_RTTHREAD;
        switch (pRec->Core.u32Magic)
        {
            case RTLOCKVALRECEXCL_MAGIC:
                Assert(iEntry == UINT32_MAX);
                for (;;)
                {
                    pNextThread = rtLockValidatorReadThreadHandle(&pRec->Excl.hThread);
                    if (   !pNextThread
                        || pNextThread->u32Magic != RTTHREADINT_MAGIC)
                        break;
                    enmNextState = rtThreadGetState(pNextThread);
                    if (    !RTTHREAD_IS_SLEEPING(enmNextState)
                        &&  pNextThread != pThreadSelf)
                        break;
                    pNextRec = rtLockValidatorReadRecUnionPtr(&pNextThread->LockValidator.pRec);
                    if (RT_LIKELY(   !pNextRec
                                  || enmNextState == rtThreadGetState(pNextThread)))
                        break;
                    pNextRec = NULL;
                }
                if (!pNextRec)
                {
                    pRec = pRec->Excl.pSibling;
                    if (    pRec
                        &&  pRec != pFirstSibling)
                        continue;
                    pNextThread = NIL_RTTHREAD;
                }
                break;

            case RTLOCKVALRECSHRD_MAGIC:
                if (!pRec->Shared.fSignaller)
                {
                    /* Skip to the next sibling if same side.  ASSUMES reader priority. */
                    /** @todo The read side of a read-write lock is problematic if
                     * the implementation prioritizes writers over readers because
                     * that means we should could deadlock against current readers
                     * if a writer showed up.  If the RW sem implementation is
                     * wrapping some native API, it's not so easy to detect when we
                     * should do this and when we shouldn't.  Checking when we
                     * shouldn't is subject to wakeup scheduling and cannot easily
                     * be made reliable.
                     *
                     * At the moment we circumvent all this mess by declaring that
                     * readers has priority.  This is TRUE on linux, but probably
                     * isn't on Solaris and FreeBSD. */
                    if (   pRec == pFirstSibling
                        && pRec->Shared.pSibling != NULL
                        && pRec->Shared.pSibling != pFirstSibling)
                    {
                        pRec = pRec->Shared.pSibling;
                        Assert(iEntry == UINT32_MAX);
                        continue;
                    }
                }

                /* Scan the owner table for blocked owners. */
                if (    ASMAtomicUoReadU32(&pRec->Shared.cEntries) > 0
                    &&  (   !pRec->Shared.fSignaller
                         || iEntry != UINT32_MAX
                         || rtLockValidatorDdAreAllThreadsBlocked(&pRec->Shared, pThreadSelf)
                        )
                    )
                {
                    uint32_t                        cAllocated = pRec->Shared.cAllocated;
                    PRTLOCKVALRECSHRDOWN volatile  *papOwners  = pRec->Shared.papOwners;
                    while (++iEntry < cAllocated)
                    {
                        PRTLOCKVALRECSHRDOWN pEntry = rtLockValidatorUoReadSharedOwner(&papOwners[iEntry]);
                        if (pEntry)
                        {
                            for (;;)
                            {
                                if (pEntry->Core.u32Magic != RTLOCKVALRECSHRDOWN_MAGIC)
                                    break;
                                pNextThread = rtLockValidatorReadThreadHandle(&pEntry->hThread);
                                if (   !pNextThread
                                    || pNextThread->u32Magic != RTTHREADINT_MAGIC)
                                    break;
                                enmNextState = rtThreadGetState(pNextThread);
                                if (    !RTTHREAD_IS_SLEEPING(enmNextState)
                                    &&  pNextThread != pThreadSelf)
                                    break;
                                pNextRec = rtLockValidatorReadRecUnionPtr(&pNextThread->LockValidator.pRec);
                                if (RT_LIKELY(   !pNextRec
                                              || enmNextState == rtThreadGetState(pNextThread)))
                                    break;
                                pNextRec = NULL;
                            }
                            if (pNextRec)
                                break;
                        }
                        else
                            Assert(!pEntry || pEntry->Core.u32Magic == RTLOCKVALRECSHRDOWN_MAGIC_DEAD);
                    }
                    if (pNextRec)
                        break;
                    pNextThread = NIL_RTTHREAD;
                }

                /* Advance to the next sibling, if any. */
                pRec = pRec->Shared.pSibling;
                if (   pRec != NULL
                    && pRec != pFirstSibling)
                {
                    iEntry = UINT32_MAX;
                    continue;
                }
                break;

            case RTLOCKVALRECEXCL_MAGIC_DEAD:
            case RTLOCKVALRECSHRD_MAGIC_DEAD:
                break;

            case RTLOCKVALRECSHRDOWN_MAGIC:
            case RTLOCKVALRECSHRDOWN_MAGIC_DEAD:
            default:
                AssertMsgFailed(("%p: %#x\n", pRec, pRec->Core.u32Magic));
                break;
        }

        if (pNextRec)
        {
            /*
             * Recurse and check for deadlock.
             */
            uint32_t i = pStack->c;
            if (RT_UNLIKELY(i >= RT_ELEMENTS(pStack->a)))
                return rtLockValidatorDdHandleStackOverflow(pStack);

            pStack->c++;
            pStack->a[i].pRec           = pRec;
            pStack->a[i].iEntry         = iEntry;
            pStack->a[i].enmState       = enmState;
            pStack->a[i].pThread        = pThread;
            pStack->a[i].pFirstSibling  = pFirstSibling;

            if (RT_UNLIKELY(   pNextThread == pThreadSelf
                            && (   i != 0
                                || pRec->Core.u32Magic != RTLOCKVALRECSHRD_MAGIC
                                || !pRec->Shared.fSignaller) /* ASSUMES signaller records have no siblings. */
                            )
                )
                return rtLockValidatorDdVerifyDeadlock(pStack, pThreadSelf);

            pRec            = pNextRec;
            pFirstSibling   = pNextRec;
            iEntry          = UINT32_MAX;
            enmState        = enmNextState;
            pThread         = pNextThread;
        }
        else
        {
            /*
             * No deadlock here, unwind the stack and deal with any unfinished
             * business there.
             */
            uint32_t i = pStack->c;
            for (;;)
            {
                /* pop */
                if (i == 0)
                    return VINF_SUCCESS;
                i--;
                pRec    = pStack->a[i].pRec;
                iEntry  = pStack->a[i].iEntry;

                /* Examine it. */
                uint32_t u32Magic = pRec->Core.u32Magic;
                if (u32Magic == RTLOCKVALRECEXCL_MAGIC)
                    pRec = pRec->Excl.pSibling;
                else if (u32Magic == RTLOCKVALRECSHRD_MAGIC)
                {
                    if (iEntry + 1 < pRec->Shared.cAllocated)
                        break;  /* continue processing this record. */
                    pRec = pRec->Shared.pSibling;
                }
                else
                {
                    Assert(   u32Magic == RTLOCKVALRECEXCL_MAGIC_DEAD
                           || u32Magic == RTLOCKVALRECSHRD_MAGIC_DEAD);
                    continue;
                }

                /* Any next record to advance to? */
                if (   !pRec
                    || pRec == pStack->a[i].pFirstSibling)
                    continue;
                iEntry = UINT32_MAX;
                break;
            }

            /* Restore the rest of the state and update the stack. */
            pFirstSibling   = pStack->a[i].pFirstSibling;
            enmState        = pStack->a[i].enmState;
            pThread         = pStack->a[i].pThread;
            pStack->c       = i;
        }

        Assert(iLoop != 1000000);
    }
}


/**
 * Check for the simple no-deadlock case.
 *
 * @returns true if no deadlock, false if further investigation is required.
 *
 * @param   pOriginalRec    The original record.
 */
DECLINLINE(int) rtLockValidatorIsSimpleNoDeadlockCase(PRTLOCKVALRECUNION pOriginalRec)
{
    if (    pOriginalRec->Excl.Core.u32Magic == RTLOCKVALRECEXCL_MAGIC
        &&  !pOriginalRec->Excl.pSibling)
    {
        PRTTHREADINT pThread = rtLockValidatorReadThreadHandle(&pOriginalRec->Excl.hThread);
        if (   !pThread
            || pThread->u32Magic != RTTHREADINT_MAGIC)
            return true;
        RTTHREADSTATE enmState = rtThreadGetState(pThread);
        if (!RTTHREAD_IS_SLEEPING(enmState))
            return true;
    }
    return false;
}


/**
 * Worker for rtLockValidatorDeadlockDetection that bitches about a deadlock.
 *
 * @param   pStack          The chain of locks causing the deadlock.
 * @param   pRec            The record relating to the current thread's lock
 *                          operation.
 * @param   pThreadSelf     This thread.
 * @param   pSrcPos         Where we are going to deadlock.
 * @param   rc              The return code.
 */
static void rcLockValidatorDoDeadlockComplaining(PRTLOCKVALDDSTACK pStack, PRTLOCKVALRECUNION pRec,
                                                 PRTTHREADINT pThreadSelf, PCRTLOCKVALSRCPOS pSrcPos, int rc)
{
    if (!ASMAtomicUoReadBool(&g_fLockValidatorQuiet))
    {
        const char *pszWhat;
        switch (rc)
        {
            case VERR_SEM_LV_DEADLOCK:          pszWhat = "Detected deadlock!"; break;
            case VERR_SEM_LV_EXISTING_DEADLOCK: pszWhat = "Found existing deadlock!"; break;
            case VERR_SEM_LV_ILLEGAL_UPGRADE:   pszWhat = "Illegal lock upgrade!"; break;
            default:            AssertFailed(); pszWhat = "!unexpected rc!"; break;
        }
        rtLockValComplainFirst(pszWhat, pSrcPos, pThreadSelf, pStack->a[0].pRec != pRec ? pRec : NULL, true);
        rtLockValComplainMore("---- start of deadlock chain - %u entries ----\n", pStack->c);
        for (uint32_t i = 0; i < pStack->c; i++)
        {
            char szPrefix[24];
            RTStrPrintf(szPrefix, sizeof(szPrefix), "#%02u: ", i);
            PRTLOCKVALRECUNION pShrdOwner = NULL;
            if (pStack->a[i].pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC)
                pShrdOwner = (PRTLOCKVALRECUNION)pStack->a[i].pRec->Shared.papOwners[pStack->a[i].iEntry];
            if (RT_VALID_PTR(pShrdOwner) && pShrdOwner->Core.u32Magic == RTLOCKVALRECSHRDOWN_MAGIC)
            {
                rtLockValComplainAboutLock(szPrefix, pShrdOwner, "\n");
                rtLockValComplainAboutLockStack(pShrdOwner->ShrdOwner.hThread, 5, 2, pShrdOwner);
            }
            else
            {
                rtLockValComplainAboutLock(szPrefix, pStack->a[i].pRec, "\n");
                if (pStack->a[i].pRec->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC)
                    rtLockValComplainAboutLockStack(pStack->a[i].pRec->Excl.hThread, 5, 2, pStack->a[i].pRec);
            }
        }
        rtLockValComplainMore("---- end of deadlock chain ----\n");
    }

    rtLockValComplainPanic();
}


/**
 * Perform deadlock detection.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_SEM_LV_DEADLOCK
 * @retval  VERR_SEM_LV_EXISTING_DEADLOCK
 * @retval  VERR_SEM_LV_ILLEGAL_UPGRADE
 *
 * @param   pRec            The record relating to the current thread's lock
 *                          operation.
 * @param   pThreadSelf     The current thread.
 * @param   pSrcPos         The position of the current lock operation.
 */
static int rtLockValidatorDeadlockDetection(PRTLOCKVALRECUNION pRec, PRTTHREADINT pThreadSelf, PCRTLOCKVALSRCPOS pSrcPos)
{
    RTLOCKVALDDSTACK Stack;
    rtLockValidatorSerializeDetectionEnter();
    int rc = rtLockValidatorDdDoDetection(&Stack, pRec, pThreadSelf);
    rtLockValidatorSerializeDetectionLeave();
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    if (rc == VERR_TRY_AGAIN)
    {
        for (uint32_t iLoop = 0; ; iLoop++)
        {
            rtLockValidatorSerializeDetectionEnter();
            rc = rtLockValidatorDdDoDetection(&Stack, pRec, pThreadSelf);
            rtLockValidatorSerializeDetectionLeave();
            if (RT_SUCCESS_NP(rc))
                return VINF_SUCCESS;
            if (rc != VERR_TRY_AGAIN)
                break;
            RTThreadYield();
            if (iLoop >= 3)
                return VINF_SUCCESS;
        }
    }

    rcLockValidatorDoDeadlockComplaining(&Stack, pRec, pThreadSelf, pSrcPos, rc);
    return rc;
}


RTDECL(void) RTLockValidatorRecExclInitV(PRTLOCKVALRECEXCL pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                         void *hLock, bool fEnabled, const char *pszNameFmt, va_list va)
{
    RTLOCKVAL_ASSERT_PTR_ALIGN(pRec);
    RTLOCKVAL_ASSERT_PTR_ALIGN(hLock);
    Assert(   uSubClass >= RTLOCKVAL_SUB_CLASS_USER
           || uSubClass == RTLOCKVAL_SUB_CLASS_NONE
           || uSubClass == RTLOCKVAL_SUB_CLASS_ANY);

    pRec->Core.u32Magic = RTLOCKVALRECEXCL_MAGIC;
    pRec->fEnabled      = fEnabled && RTLockValidatorIsEnabled();
    pRec->afReserved[0] = 0;
    pRec->afReserved[1] = 0;
    pRec->afReserved[2] = 0;
    rtLockValidatorSrcPosInit(&pRec->SrcPos);
    pRec->hThread       = NIL_RTTHREAD;
    pRec->pDown         = NULL;
    pRec->hClass        = rtLockValidatorClassValidateAndRetain(hClass);
    pRec->uSubClass     = uSubClass;
    pRec->cRecursion    = 0;
    pRec->hLock         = hLock;
    pRec->pSibling      = NULL;
    if (pszNameFmt)
        RTStrPrintfV(pRec->szName, sizeof(pRec->szName), pszNameFmt, va);
    else
    {
        static uint32_t volatile s_cAnonymous = 0;
        uint32_t i = ASMAtomicIncU32(&s_cAnonymous) - 1;
        RTStrPrintf(pRec->szName, sizeof(pRec->szName), "anon-excl-%u", i);
    }

    /* Lazy initialization. */
    if (RT_UNLIKELY(g_hLockValidatorXRoads == NIL_RTSEMXROADS))
        rtLockValidatorLazyInit();
}


RTDECL(void) RTLockValidatorRecExclInit(PRTLOCKVALRECEXCL pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                        void *hLock, bool fEnabled, const char *pszNameFmt, ...)
{
    va_list va;
    va_start(va, pszNameFmt);
    RTLockValidatorRecExclInitV(pRec, hClass, uSubClass, hLock, fEnabled, pszNameFmt, va);
    va_end(va);
}


RTDECL(int)  RTLockValidatorRecExclCreateV(PRTLOCKVALRECEXCL *ppRec, RTLOCKVALCLASS hClass,
                                           uint32_t uSubClass, void *pvLock, bool fEnabled,
                                           const char *pszNameFmt, va_list va)
{
    PRTLOCKVALRECEXCL pRec;
    *ppRec = pRec = (PRTLOCKVALRECEXCL)RTMemAlloc(sizeof(*pRec));
    if (!pRec)
        return VERR_NO_MEMORY;
    RTLockValidatorRecExclInitV(pRec, hClass, uSubClass, pvLock, fEnabled, pszNameFmt, va);
    return VINF_SUCCESS;
}


RTDECL(int)  RTLockValidatorRecExclCreate(PRTLOCKVALRECEXCL *ppRec, RTLOCKVALCLASS hClass,
                                          uint32_t uSubClass, void *pvLock, bool fEnabled,
                                          const char *pszNameFmt, ...)
{
    va_list va;
    va_start(va, pszNameFmt);
    int rc = RTLockValidatorRecExclCreateV(ppRec, hClass, uSubClass, pvLock, fEnabled, pszNameFmt, va);
    va_end(va);
    return rc;
}


RTDECL(void) RTLockValidatorRecExclDelete(PRTLOCKVALRECEXCL pRec)
{
    Assert(pRec->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC);

    rtLockValidatorSerializeDestructEnter();

    /** @todo Check that it's not on our stack first.  Need to make it
     *        configurable whether deleting a owned lock is acceptable? */

    ASMAtomicWriteU32(&pRec->Core.u32Magic, RTLOCKVALRECEXCL_MAGIC_DEAD);
    ASMAtomicWriteHandle(&pRec->hThread, NIL_RTTHREAD);
    RTLOCKVALCLASS hClass;
    ASMAtomicXchgHandle(&pRec->hClass, NIL_RTLOCKVALCLASS, &hClass);
    if (pRec->pSibling)
        rtLockValidatorUnlinkAllSiblings(&pRec->Core);
    rtLockValidatorSerializeDestructLeave();
    if (hClass != NIL_RTLOCKVALCLASS)
        RTLockValidatorClassRelease(hClass);
}


RTDECL(void) RTLockValidatorRecExclDestroy(PRTLOCKVALRECEXCL *ppRec)
{
    PRTLOCKVALRECEXCL pRec = *ppRec;
    *ppRec = NULL;
    if (pRec)
    {
        RTLockValidatorRecExclDelete(pRec);
        RTMemFree(pRec);
    }
}


RTDECL(uint32_t) RTLockValidatorRecExclSetSubClass(PRTLOCKVALRECEXCL pRec, uint32_t uSubClass)
{
    AssertPtrReturn(pRec, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pRec->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(   uSubClass >= RTLOCKVAL_SUB_CLASS_USER
                 || uSubClass == RTLOCKVAL_SUB_CLASS_NONE
                 || uSubClass == RTLOCKVAL_SUB_CLASS_ANY,
                 RTLOCKVAL_SUB_CLASS_INVALID);
    return ASMAtomicXchgU32(&pRec->uSubClass, uSubClass);
}


RTDECL(void) RTLockValidatorRecExclSetOwner(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                            PCRTLOCKVALSRCPOS pSrcPos, bool fFirstRecursion)
{
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    if (!pRecU)
        return;
    AssertReturnVoid(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC);
    if (!pRecU->Excl.fEnabled)
        return;
    if (hThreadSelf == NIL_RTTHREAD)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        AssertReturnVoid(hThreadSelf != NIL_RTTHREAD);
    }
    AssertReturnVoid(hThreadSelf->u32Magic == RTTHREADINT_MAGIC);
    Assert(hThreadSelf == RTThreadSelf());

    ASMAtomicIncS32(&hThreadSelf->LockValidator.cWriteLocks);

    if (pRecU->Excl.hThread == hThreadSelf)
    {
        Assert(!fFirstRecursion); RT_NOREF_PV(fFirstRecursion);
        pRecU->Excl.cRecursion++;
        rtLockValidatorStackPushRecursion(hThreadSelf, pRecU, pSrcPos);
    }
    else
    {
        Assert(pRecU->Excl.hThread == NIL_RTTHREAD);

        rtLockValidatorSrcPosCopy(&pRecU->Excl.SrcPos, pSrcPos);
        ASMAtomicUoWriteU32(&pRecU->Excl.cRecursion, 1);
        ASMAtomicWriteHandle(&pRecU->Excl.hThread, hThreadSelf);

        rtLockValidatorStackPush(hThreadSelf, pRecU);
    }
}


/**
 * Internal worker for RTLockValidatorRecExclReleaseOwner and
 * RTLockValidatorRecExclReleaseOwnerUnchecked.
 */
static void  rtLockValidatorRecExclReleaseOwnerUnchecked(PRTLOCKVALRECUNION pRec, bool fFinalRecursion)
{
    RTTHREADINT *pThread = pRec->Excl.hThread;
    AssertReturnVoid(pThread != NIL_RTTHREAD);
    Assert(pThread == RTThreadSelf());

    ASMAtomicDecS32(&pThread->LockValidator.cWriteLocks);
    uint32_t c = ASMAtomicDecU32(&pRec->Excl.cRecursion);
    if (c == 0)
    {
        rtLockValidatorStackPop(pThread, pRec);
        ASMAtomicWriteHandle(&pRec->Excl.hThread, NIL_RTTHREAD);
    }
    else
    {
        Assert(c < UINT32_C(0xffff0000));
        Assert(!fFinalRecursion); RT_NOREF_PV(fFinalRecursion);
        rtLockValidatorStackPopRecursion(pThread, pRec);
    }
}

RTDECL(int)  RTLockValidatorRecExclReleaseOwner(PRTLOCKVALRECEXCL pRec, bool fFinalRecursion)
{
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    if (!pRecU)
        return VINF_SUCCESS;
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRecU->Excl.fEnabled)
        return VINF_SUCCESS;

    /*
     * Check the release order.
     */
    if (   pRecU->Excl.hClass != NIL_RTLOCKVALCLASS
        && pRecU->Excl.hClass->fStrictReleaseOrder
        && pRecU->Excl.hClass->cMsMinOrder != RT_INDEFINITE_WAIT
       )
    {
        int rc = rtLockValidatorStackCheckReleaseOrder(pRecU->Excl.hThread, pRecU);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Join paths with RTLockValidatorRecExclReleaseOwnerUnchecked.
     */
    rtLockValidatorRecExclReleaseOwnerUnchecked(pRecU, fFinalRecursion);
    return VINF_SUCCESS;
}


RTDECL(void) RTLockValidatorRecExclReleaseOwnerUnchecked(PRTLOCKVALRECEXCL pRec)
{
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    AssertReturnVoid(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC);
    if (pRecU->Excl.fEnabled)
        rtLockValidatorRecExclReleaseOwnerUnchecked(pRecU, false);
}


RTDECL(int) RTLockValidatorRecExclRecursion(PRTLOCKVALRECEXCL pRec, PCRTLOCKVALSRCPOS pSrcPos)
{
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    if (!pRecU)
        return VINF_SUCCESS;
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRecU->Excl.fEnabled)
        return VINF_SUCCESS;
    AssertReturn(pRecU->Excl.hThread != NIL_RTTHREAD, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(pRecU->Excl.cRecursion > 0, VERR_SEM_LV_INVALID_PARAMETER);

    if (   pRecU->Excl.hClass != NIL_RTLOCKVALCLASS
        && !pRecU->Excl.hClass->fRecursionOk)
    {
        rtLockValComplainFirst("Recursion not allowed by the class!",
                               pSrcPos, pRecU->Excl.hThread, (PRTLOCKVALRECUNION)pRec, true);
        rtLockValComplainPanic();
        return VERR_SEM_LV_NESTED;
    }

    Assert(pRecU->Excl.cRecursion < _1M);
    pRecU->Excl.cRecursion++;
    rtLockValidatorStackPushRecursion(pRecU->Excl.hThread, pRecU, pSrcPos);
    return VINF_SUCCESS;
}


RTDECL(int) RTLockValidatorRecExclUnwind(PRTLOCKVALRECEXCL pRec)
{
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRecU->Excl.fEnabled)
        return VINF_SUCCESS;
    AssertReturn(pRecU->Excl.hThread != NIL_RTTHREAD, VERR_SEM_LV_INVALID_PARAMETER);
    Assert(pRecU->Excl.hThread == RTThreadSelf());
    AssertReturn(pRecU->Excl.cRecursion > 1, VERR_SEM_LV_INVALID_PARAMETER);

    /*
     * Check the release order.
     */
    if (   pRecU->Excl.hClass != NIL_RTLOCKVALCLASS
        && pRecU->Excl.hClass->fStrictReleaseOrder
        && pRecU->Excl.hClass->cMsMinOrder != RT_INDEFINITE_WAIT
       )
    {
        int rc = rtLockValidatorStackCheckReleaseOrder(pRecU->Excl.hThread, pRecU);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Perform the unwind.
     */
    pRecU->Excl.cRecursion--;
    rtLockValidatorStackPopRecursion(pRecU->Excl.hThread, pRecU);
    return VINF_SUCCESS;
}


RTDECL(int) RTLockValidatorRecExclRecursionMixed(PRTLOCKVALRECEXCL pRec, PRTLOCKVALRECCORE pRecMixed, PCRTLOCKVALSRCPOS pSrcPos)
{
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    PRTLOCKVALRECUNION pRecMixedU = (PRTLOCKVALRECUNION)pRecMixed;
    AssertReturn(   pRecMixedU->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC
                 || pRecMixedU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC
                 , VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRecU->Excl.fEnabled)
        return VINF_SUCCESS;
    Assert(pRecU->Excl.hThread == RTThreadSelf());
    AssertReturn(pRecU->Excl.hThread != NIL_RTTHREAD, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(pRecU->Excl.cRecursion > 0, VERR_SEM_LV_INVALID_PARAMETER);

    if (   pRecU->Excl.hClass != NIL_RTLOCKVALCLASS
        && !pRecU->Excl.hClass->fRecursionOk)
    {
        rtLockValComplainFirst("Mixed recursion not allowed by the class!",
                               pSrcPos, pRecU->Excl.hThread, (PRTLOCKVALRECUNION)pRec, true);
        rtLockValComplainPanic();
        return VERR_SEM_LV_NESTED;
    }

    Assert(pRecU->Excl.cRecursion < _1M);
    pRecU->Excl.cRecursion++;
    rtLockValidatorStackPushRecursion(pRecU->Excl.hThread, pRecU, pSrcPos);

    return VINF_SUCCESS;
}


RTDECL(int) RTLockValidatorRecExclUnwindMixed(PRTLOCKVALRECEXCL pRec, PRTLOCKVALRECCORE pRecMixed)
{
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    PRTLOCKVALRECUNION pRecMixedU = (PRTLOCKVALRECUNION)pRecMixed;
    AssertReturn(   pRecMixedU->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC
                 || pRecMixedU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC
                 , VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRecU->Excl.fEnabled)
        return VINF_SUCCESS;
    Assert(pRecU->Excl.hThread == RTThreadSelf());
    AssertReturn(pRecU->Excl.hThread != NIL_RTTHREAD, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(pRecU->Excl.cRecursion > 1, VERR_SEM_LV_INVALID_PARAMETER);

    /*
     * Check the release order.
     */
    if (   pRecU->Excl.hClass != NIL_RTLOCKVALCLASS
        && pRecU->Excl.hClass->fStrictReleaseOrder
        && pRecU->Excl.hClass->cMsMinOrder != RT_INDEFINITE_WAIT
       )
    {
        int rc = rtLockValidatorStackCheckReleaseOrder(pRecU->Excl.hThread, pRecU);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Perform the unwind.
     */
    pRecU->Excl.cRecursion--;
    rtLockValidatorStackPopRecursion(pRecU->Excl.hThread, pRecU);
    return VINF_SUCCESS;
}


RTDECL(int) RTLockValidatorRecExclCheckOrder(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                             PCRTLOCKVALSRCPOS pSrcPos, RTMSINTERVAL cMillies)
{
    /*
     * Validate and adjust input.  Quit early if order validation is disabled.
     */
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    if (!pRecU)
        return VINF_SUCCESS;
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (   !pRecU->Excl.fEnabled
        || pRecU->Excl.hClass == NIL_RTLOCKVALCLASS
        || pRecU->Excl.hClass->cMsMinOrder == RT_INDEFINITE_WAIT
        || pRecU->Excl.hClass->cMsMinOrder > cMillies)
        return VINF_SUCCESS;

    if (hThreadSelf == NIL_RTTHREAD)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        AssertReturn(hThreadSelf != NIL_RTTHREAD, VERR_SEM_LV_INTERNAL_ERROR);
    }
    AssertReturn(hThreadSelf->u32Magic == RTTHREADINT_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    Assert(hThreadSelf == RTThreadSelf());

    /*
     * Detect recursion as it isn't subject to order restrictions.
     */
    if (pRec->hThread == hThreadSelf)
        return VINF_SUCCESS;

    return rtLockValidatorStackCheckLockingOrder(pRecU->Excl.hClass, pRecU->Excl.uSubClass, hThreadSelf, pRecU, pSrcPos);
}


RTDECL(int) RTLockValidatorRecExclCheckBlocking(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                                PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                RTTHREADSTATE enmSleepState, bool fReallySleeping)
{
    /*
     * Fend off wild life.
     */
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    if (!pRecU)
        return VINF_SUCCESS;
    AssertPtrReturn(pRecU, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECEXCL_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRec->fEnabled)
        return VINF_SUCCESS;

    PRTTHREADINT pThreadSelf = hThreadSelf;
    AssertPtrReturn(pThreadSelf, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(pThreadSelf->u32Magic == RTTHREADINT_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    Assert(pThreadSelf == RTThreadSelf());

    AssertReturn(RTTHREAD_IS_SLEEPING(enmSleepState), VERR_SEM_LV_INVALID_PARAMETER);

    RTTHREADSTATE enmThreadState = rtThreadGetState(pThreadSelf);
    if (RT_UNLIKELY(enmThreadState != RTTHREADSTATE_RUNNING))
    {
        AssertReturn(   enmThreadState == RTTHREADSTATE_TERMINATED   /* rtThreadRemove uses locks too */
                     || enmThreadState == RTTHREADSTATE_INITIALIZING /* rtThreadInsert uses locks too */
                     , VERR_SEM_LV_INVALID_PARAMETER);
        enmSleepState = enmThreadState;
    }

    /*
     * Record the location.
     */
    rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pRec, pRecU);
    rtLockValidatorSrcPosCopy(&pThreadSelf->LockValidator.SrcPos, pSrcPos);
    ASMAtomicWriteBool(&pThreadSelf->LockValidator.fInValidator, true);
    pThreadSelf->LockValidator.enmRecState = enmSleepState;
    rtThreadSetState(pThreadSelf, enmSleepState);

    /*
     * Don't do deadlock detection if we're recursing.
     *
     * On some hosts we don't do recursion accounting our selves and there
     * isn't any other place to check for this.
     */
    int rc = VINF_SUCCESS;
    if (rtLockValidatorReadThreadHandle(&pRecU->Excl.hThread) == pThreadSelf)
    {
        if (   !fRecursiveOk
            || (   pRecU->Excl.hClass != NIL_RTLOCKVALCLASS
                && !pRecU->Excl.hClass->fRecursionOk))
        {
            rtLockValComplainFirst("Recursion not allowed!", pSrcPos, pThreadSelf, pRecU, true);
            rtLockValComplainPanic();
            rc = VERR_SEM_LV_NESTED;
        }
    }
    /*
     * Perform deadlock detection.
     */
    else if (   pRecU->Excl.hClass != NIL_RTLOCKVALCLASS
             && (   pRecU->Excl.hClass->cMsMinDeadlock > cMillies
                 || pRecU->Excl.hClass->cMsMinDeadlock == RT_INDEFINITE_WAIT))
        rc = VINF_SUCCESS;
    else if (!rtLockValidatorIsSimpleNoDeadlockCase(pRecU))
        rc = rtLockValidatorDeadlockDetection(pRecU, pThreadSelf, pSrcPos);

    if (RT_SUCCESS(rc))
        ASMAtomicWriteBool(&pThreadSelf->fReallySleeping, fReallySleeping);
    else
    {
        rtThreadSetState(pThreadSelf, enmThreadState);
        rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pRec, NULL);
    }
    ASMAtomicWriteBool(&pThreadSelf->LockValidator.fInValidator, false);
    return rc;
}
RT_EXPORT_SYMBOL(RTLockValidatorRecExclCheckBlocking);


RTDECL(int) RTLockValidatorRecExclCheckOrderAndBlocking(PRTLOCKVALRECEXCL pRec, RTTHREAD hThreadSelf,
                                                        PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                        RTTHREADSTATE enmSleepState, bool fReallySleeping)
{
    int rc = RTLockValidatorRecExclCheckOrder(pRec, hThreadSelf, pSrcPos, cMillies);
    if (RT_SUCCESS(rc))
        rc = RTLockValidatorRecExclCheckBlocking(pRec, hThreadSelf, pSrcPos, fRecursiveOk, cMillies,
                                                 enmSleepState, fReallySleeping);
    return rc;
}
RT_EXPORT_SYMBOL(RTLockValidatorRecExclCheckOrderAndBlocking);


RTDECL(void) RTLockValidatorRecSharedInitV(PRTLOCKVALRECSHRD pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                           void *hLock, bool fSignaller, bool fEnabled, const char *pszNameFmt, va_list va)
{
    RTLOCKVAL_ASSERT_PTR_ALIGN(pRec);
    RTLOCKVAL_ASSERT_PTR_ALIGN(hLock);
    Assert(   uSubClass >= RTLOCKVAL_SUB_CLASS_USER
           || uSubClass == RTLOCKVAL_SUB_CLASS_NONE
           || uSubClass == RTLOCKVAL_SUB_CLASS_ANY);

    pRec->Core.u32Magic = RTLOCKVALRECSHRD_MAGIC;
    pRec->uSubClass     = uSubClass;
    pRec->hClass        = rtLockValidatorClassValidateAndRetain(hClass);
    pRec->hLock         = hLock;
    pRec->fEnabled      = fEnabled && RTLockValidatorIsEnabled();
    pRec->fSignaller    = fSignaller;
    pRec->pSibling      = NULL;

    /* the table */
    pRec->cEntries      = 0;
    pRec->iLastEntry    = 0;
    pRec->cAllocated    = 0;
    pRec->fReallocating = false;
    pRec->fPadding      = false;
    pRec->papOwners     = NULL;

    /* the name */
    if (pszNameFmt)
        RTStrPrintfV(pRec->szName, sizeof(pRec->szName), pszNameFmt, va);
    else
    {
        static uint32_t volatile s_cAnonymous = 0;
        uint32_t i = ASMAtomicIncU32(&s_cAnonymous) - 1;
        RTStrPrintf(pRec->szName, sizeof(pRec->szName), "anon-shrd-%u", i);
    }
}


RTDECL(void) RTLockValidatorRecSharedInit(PRTLOCKVALRECSHRD pRec, RTLOCKVALCLASS hClass, uint32_t uSubClass,
                                          void *hLock, bool fSignaller, bool fEnabled, const char *pszNameFmt, ...)
{
    va_list va;
    va_start(va, pszNameFmt);
    RTLockValidatorRecSharedInitV(pRec, hClass, uSubClass, hLock, fSignaller, fEnabled, pszNameFmt, va);
    va_end(va);
}


RTDECL(int)  RTLockValidatorRecSharedCreateV(PRTLOCKVALRECSHRD *ppRec, RTLOCKVALCLASS hClass,
                                             uint32_t uSubClass, void *pvLock, bool fSignaller, bool fEnabled,
                                             const char *pszNameFmt, va_list va)
{
    PRTLOCKVALRECSHRD pRec;
    *ppRec = pRec = (PRTLOCKVALRECSHRD)RTMemAlloc(sizeof(*pRec));
    if (!pRec)
        return VERR_NO_MEMORY;
    RTLockValidatorRecSharedInitV(pRec, hClass, uSubClass, pvLock, fSignaller, fEnabled, pszNameFmt, va);
    return VINF_SUCCESS;
}


RTDECL(int)  RTLockValidatorRecSharedCreate(PRTLOCKVALRECSHRD *ppRec, RTLOCKVALCLASS hClass,
                                            uint32_t uSubClass, void *pvLock, bool fSignaller, bool fEnabled,
                                            const char *pszNameFmt, ...)
{
    va_list va;
    va_start(va, pszNameFmt);
    int rc = RTLockValidatorRecSharedCreateV(ppRec, hClass, uSubClass, pvLock, fSignaller, fEnabled, pszNameFmt, va);
    va_end(va);
    return rc;
}


RTDECL(void) RTLockValidatorRecSharedDelete(PRTLOCKVALRECSHRD pRec)
{
    Assert(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC);

    /** @todo Check that it's not on our stack first.  Need to make it
     *        configurable whether deleting a owned lock is acceptable? */

    /*
     * Flip it into table realloc mode and take the destruction lock.
     */
    rtLockValidatorSerializeDestructEnter();
    while (!ASMAtomicCmpXchgBool(&pRec->fReallocating, true, false))
    {
        rtLockValidatorSerializeDestructLeave();

        rtLockValidatorSerializeDetectionEnter();
        rtLockValidatorSerializeDetectionLeave();

        rtLockValidatorSerializeDestructEnter();
    }

    ASMAtomicWriteU32(&pRec->Core.u32Magic, RTLOCKVALRECSHRD_MAGIC_DEAD);
    RTLOCKVALCLASS hClass;
    ASMAtomicXchgHandle(&pRec->hClass, NIL_RTLOCKVALCLASS, &hClass);
    if (pRec->papOwners)
    {
        PRTLOCKVALRECSHRDOWN volatile *papOwners = pRec->papOwners;
        ASMAtomicUoWriteNullPtr(&pRec->papOwners);
        ASMAtomicUoWriteU32(&pRec->cAllocated, 0);

        RTMemFree((void *)papOwners);
    }
    if (pRec->pSibling)
        rtLockValidatorUnlinkAllSiblings(&pRec->Core);
    ASMAtomicWriteBool(&pRec->fReallocating, false);

    rtLockValidatorSerializeDestructLeave();

    if (hClass != NIL_RTLOCKVALCLASS)
        RTLockValidatorClassRelease(hClass);
}


RTDECL(void) RTLockValidatorRecSharedDestroy(PRTLOCKVALRECSHRD *ppRec)
{
    PRTLOCKVALRECSHRD pRec = *ppRec;
    *ppRec = NULL;
    if (pRec)
    {
        RTLockValidatorRecSharedDelete(pRec);
        RTMemFree(pRec);
    }
}


RTDECL(uint32_t) RTLockValidatorRecSharedSetSubClass(PRTLOCKVALRECSHRD pRec, uint32_t uSubClass)
{
    AssertPtrReturn(pRec, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, RTLOCKVAL_SUB_CLASS_INVALID);
    AssertReturn(   uSubClass >= RTLOCKVAL_SUB_CLASS_USER
                 || uSubClass == RTLOCKVAL_SUB_CLASS_NONE
                 || uSubClass == RTLOCKVAL_SUB_CLASS_ANY,
                 RTLOCKVAL_SUB_CLASS_INVALID);
    return ASMAtomicXchgU32(&pRec->uSubClass, uSubClass);
}


/**
 * Locates an owner (thread) in a shared lock record.
 *
 * @returns Pointer to the owner entry on success, NULL on failure..
 * @param   pShared             The shared lock record.
 * @param   hThread             The thread (owner) to find.
 * @param   piEntry             Where to optionally return the table in index.
 *                              Optional.
 */
DECLINLINE(PRTLOCKVALRECUNION)
rtLockValidatorRecSharedFindOwner(PRTLOCKVALRECSHRD pShared, RTTHREAD hThread, uint32_t *piEntry)
{
    rtLockValidatorSerializeDetectionEnter();

    PRTLOCKVALRECSHRDOWN volatile *papOwners = pShared->papOwners;
    if (papOwners)
    {
        uint32_t const cMax = pShared->cAllocated;
        for (uint32_t iEntry = 0; iEntry < cMax; iEntry++)
        {
            PRTLOCKVALRECUNION pEntry = (PRTLOCKVALRECUNION)rtLockValidatorUoReadSharedOwner(&papOwners[iEntry]);
            if (pEntry && pEntry->ShrdOwner.hThread == hThread)
            {
                rtLockValidatorSerializeDetectionLeave();
                if (piEntry)
                    *piEntry = iEntry;
                return pEntry;
            }
        }
    }

    rtLockValidatorSerializeDetectionLeave();
    return NULL;
}


RTDECL(int) RTLockValidatorRecSharedCheckOrder(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf,
                                               PCRTLOCKVALSRCPOS pSrcPos, RTMSINTERVAL cMillies)
{
    /*
     * Validate and adjust input.  Quit early if order validation is disabled.
     */
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (   !pRecU->Shared.fEnabled
        || pRecU->Shared.hClass == NIL_RTLOCKVALCLASS
        || pRecU->Shared.hClass->cMsMinOrder == RT_INDEFINITE_WAIT
        || pRecU->Shared.hClass->cMsMinOrder > cMillies
        )
        return VINF_SUCCESS;

    if (hThreadSelf == NIL_RTTHREAD)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        AssertReturn(hThreadSelf != NIL_RTTHREAD, VERR_SEM_LV_INTERNAL_ERROR);
    }
    AssertReturn(hThreadSelf->u32Magic == RTTHREADINT_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    Assert(hThreadSelf == RTThreadSelf());

    /*
     * Detect recursion as it isn't subject to order restrictions.
     */
    PRTLOCKVALRECUNION pEntry = rtLockValidatorRecSharedFindOwner(&pRecU->Shared, hThreadSelf, NULL);
    if (pEntry)
        return VINF_SUCCESS;

    return rtLockValidatorStackCheckLockingOrder(pRecU->Shared.hClass, pRecU->Shared.uSubClass, hThreadSelf, pRecU, pSrcPos);
}


RTDECL(int) RTLockValidatorRecSharedCheckBlocking(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf,
                                                  PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                  RTTHREADSTATE enmSleepState, bool fReallySleeping)
{
    /*
     * Fend off wild life.
     */
    PRTLOCKVALRECUNION pRecU = (PRTLOCKVALRECUNION)pRec;
    AssertPtrReturn(pRecU, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(pRecU->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRecU->Shared.fEnabled)
        return VINF_SUCCESS;

    PRTTHREADINT pThreadSelf = hThreadSelf;
    AssertPtrReturn(pThreadSelf, VERR_SEM_LV_INVALID_PARAMETER);
    AssertReturn(pThreadSelf->u32Magic == RTTHREADINT_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    Assert(pThreadSelf == RTThreadSelf());

    AssertReturn(RTTHREAD_IS_SLEEPING(enmSleepState), VERR_SEM_LV_INVALID_PARAMETER);

    RTTHREADSTATE enmThreadState = rtThreadGetState(pThreadSelf);
    if (RT_UNLIKELY(enmThreadState != RTTHREADSTATE_RUNNING))
    {
        AssertReturn(   enmThreadState == RTTHREADSTATE_TERMINATED   /* rtThreadRemove uses locks too */
                     || enmThreadState == RTTHREADSTATE_INITIALIZING /* rtThreadInsert uses locks too */
                     , VERR_SEM_LV_INVALID_PARAMETER);
        enmSleepState = enmThreadState;
    }

    /*
     * Record the location.
     */
    rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pRec, pRecU);
    rtLockValidatorSrcPosCopy(&pThreadSelf->LockValidator.SrcPos, pSrcPos);
    ASMAtomicWriteBool(&pThreadSelf->LockValidator.fInValidator, true);
    pThreadSelf->LockValidator.enmRecState = enmSleepState;
    rtThreadSetState(pThreadSelf, enmSleepState);

    /*
     * Don't do deadlock detection if we're recursing.
     */
    int rc = VINF_SUCCESS;
    PRTLOCKVALRECUNION pEntry = !pRecU->Shared.fSignaller
                              ? rtLockValidatorRecSharedFindOwner(&pRecU->Shared, pThreadSelf, NULL)
                              : NULL;
    if (pEntry)
    {
        if (   !fRecursiveOk
            || (   pRec->hClass
                && !pRec->hClass->fRecursionOk)
            )
        {
            rtLockValComplainFirst("Recursion not allowed!", pSrcPos, pThreadSelf, pRecU, true);
            rtLockValComplainPanic();
            rc =  VERR_SEM_LV_NESTED;
        }
    }
    /*
     * Perform deadlock detection.
     */
    else if (   pRec->hClass
             && (   pRec->hClass->cMsMinDeadlock == RT_INDEFINITE_WAIT
                 || pRec->hClass->cMsMinDeadlock > cMillies))
        rc = VINF_SUCCESS;
    else if (!rtLockValidatorIsSimpleNoDeadlockCase(pRecU))
        rc = rtLockValidatorDeadlockDetection(pRecU, pThreadSelf, pSrcPos);

    if (RT_SUCCESS(rc))
        ASMAtomicWriteBool(&pThreadSelf->fReallySleeping, fReallySleeping);
    else
    {
        rtThreadSetState(pThreadSelf, enmThreadState);
        rtLockValidatorWriteRecUnionPtr(&pThreadSelf->LockValidator.pRec, NULL);
    }
    ASMAtomicWriteBool(&pThreadSelf->LockValidator.fInValidator, false);
    return rc;
}
RT_EXPORT_SYMBOL(RTLockValidatorRecSharedCheckBlocking);


RTDECL(int) RTLockValidatorRecSharedCheckOrderAndBlocking(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf,
                                                          PCRTLOCKVALSRCPOS pSrcPos, bool fRecursiveOk, RTMSINTERVAL cMillies,
                                                          RTTHREADSTATE enmSleepState, bool fReallySleeping)
{
    int rc = RTLockValidatorRecSharedCheckOrder(pRec, hThreadSelf, pSrcPos, cMillies);
    if (RT_SUCCESS(rc))
        rc = RTLockValidatorRecSharedCheckBlocking(pRec, hThreadSelf, pSrcPos, fRecursiveOk, cMillies,
                                                   enmSleepState, fReallySleeping);
    return rc;
}
RT_EXPORT_SYMBOL(RTLockValidatorRecSharedCheckOrderAndBlocking);


/**
 * Allocates and initializes an owner entry for the shared lock record.
 *
 * @returns The new owner entry.
 * @param   pRec                The shared lock record.
 * @param   pThreadSelf         The calling thread and owner.  Used for record
 *                              initialization and allocation.
 * @param   pSrcPos             The source position.
 */
DECLINLINE(PRTLOCKVALRECUNION)
rtLockValidatorRecSharedAllocOwner(PRTLOCKVALRECSHRD pRec, PRTTHREADINT pThreadSelf, PCRTLOCKVALSRCPOS pSrcPos)
{
    PRTLOCKVALRECUNION pEntry;

    /*
     * Check if the thread has any statically allocated records we can easily
     * make use of.
     */
    unsigned iEntry = ASMBitFirstSetU32(ASMAtomicUoReadU32(&pThreadSelf->LockValidator.bmFreeShrdOwners));
    if (   iEntry > 0
        && ASMAtomicBitTestAndClear(&pThreadSelf->LockValidator.bmFreeShrdOwners, iEntry - 1))
    {
        pEntry = (PRTLOCKVALRECUNION)&pThreadSelf->LockValidator.aShrdOwners[iEntry - 1];
        Assert(!pEntry->ShrdOwner.fReserved);
        pEntry->ShrdOwner.fStaticAlloc = true;
        rtThreadGet(pThreadSelf);
    }
    else
    {
        pEntry = (PRTLOCKVALRECUNION)RTMemAlloc(sizeof(RTLOCKVALRECSHRDOWN));
        if (RT_UNLIKELY(!pEntry))
            return NULL;
        pEntry->ShrdOwner.fStaticAlloc = false;
    }

    pEntry->Core.u32Magic        = RTLOCKVALRECSHRDOWN_MAGIC;
    pEntry->ShrdOwner.cRecursion = 1;
    pEntry->ShrdOwner.fReserved  = true;
    pEntry->ShrdOwner.hThread    = pThreadSelf;
    pEntry->ShrdOwner.pDown      = NULL;
    pEntry->ShrdOwner.pSharedRec = pRec;
#if HC_ARCH_BITS == 32
    pEntry->ShrdOwner.pvReserved = NULL;
#endif
    if (pSrcPos)
        pEntry->ShrdOwner.SrcPos = *pSrcPos;
    else
        rtLockValidatorSrcPosInit(&pEntry->ShrdOwner.SrcPos);
    return pEntry;
}


/**
 * Frees an owner entry allocated by rtLockValidatorRecSharedAllocOwner.
 *
 * @param   pEntry              The owner entry.
 */
DECLINLINE(void) rtLockValidatorRecSharedFreeOwner(PRTLOCKVALRECSHRDOWN pEntry)
{
    if (pEntry)
    {
        Assert(pEntry->Core.u32Magic == RTLOCKVALRECSHRDOWN_MAGIC);
        ASMAtomicWriteU32(&pEntry->Core.u32Magic, RTLOCKVALRECSHRDOWN_MAGIC_DEAD);

        PRTTHREADINT pThread;
        ASMAtomicXchgHandle(&pEntry->hThread, NIL_RTTHREAD, &pThread);

        Assert(pEntry->fReserved);
        pEntry->fReserved = false;

        if (pEntry->fStaticAlloc)
        {
            AssertPtrReturnVoid(pThread);
            AssertReturnVoid(pThread->u32Magic == RTTHREADINT_MAGIC);

            uintptr_t iEntry = pEntry - &pThread->LockValidator.aShrdOwners[0];
            AssertReleaseReturnVoid(iEntry < RT_ELEMENTS(pThread->LockValidator.aShrdOwners));

            Assert(!ASMBitTest(&pThread->LockValidator.bmFreeShrdOwners, (int32_t)iEntry));
            ASMAtomicBitSet(&pThread->LockValidator.bmFreeShrdOwners, (int32_t)iEntry);

            rtThreadRelease(pThread);
        }
        else
        {
            rtLockValidatorSerializeDestructEnter();
            rtLockValidatorSerializeDestructLeave();

            RTMemFree(pEntry);
        }
    }
}


/**
 * Make more room in the table.
 *
 * @retval  true on success
 * @retval  false if we're out of memory or running into a bad race condition
 *          (probably a bug somewhere).  No longer holding the lock.
 *
 * @param   pShared             The shared lock record.
 */
static bool rtLockValidatorRecSharedMakeRoom(PRTLOCKVALRECSHRD pShared)
{
    for (unsigned i = 0; i < 1000; i++)
    {
        /*
         * Switch to the other data access direction.
         */
        rtLockValidatorSerializeDetectionLeave();
        if (i >= 10)
        {
            Assert(i != 10 && i != 100);
            RTThreadSleep(i >= 100);
        }
        rtLockValidatorSerializeDestructEnter();

        /*
         * Try grab the privilege to reallocating the table.
         */
        if (    pShared->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC
            &&  ASMAtomicCmpXchgBool(&pShared->fReallocating, true, false))
        {
            uint32_t cAllocated = pShared->cAllocated;
            if (cAllocated < pShared->cEntries)
            {
                /*
                 * Ok, still not enough space.  Reallocate the table.
                 */
                uint32_t                cInc = RT_ALIGN_32(pShared->cEntries - cAllocated, 16);
                PRTLOCKVALRECSHRDOWN   *papOwners;
                papOwners = (PRTLOCKVALRECSHRDOWN *)RTMemRealloc((void *)pShared->papOwners,
                                                                 (cAllocated + cInc) * sizeof(void *));
                if (!papOwners)
                {
                    ASMAtomicWriteBool(&pShared->fReallocating, false);
                    rtLockValidatorSerializeDestructLeave();
                    /* RTMemRealloc will assert */
                    return false;
                }

                while (cInc-- > 0)
                {
                    papOwners[cAllocated] = NULL;
                    cAllocated++;
                }

                ASMAtomicWritePtr(&pShared->papOwners, papOwners);
                ASMAtomicWriteU32(&pShared->cAllocated, cAllocated);
            }
            ASMAtomicWriteBool(&pShared->fReallocating, false);
        }
        rtLockValidatorSerializeDestructLeave();

        rtLockValidatorSerializeDetectionEnter();
        if (RT_UNLIKELY(pShared->Core.u32Magic != RTLOCKVALRECSHRD_MAGIC))
            break;

        if (pShared->cAllocated >= pShared->cEntries)
            return true;
    }

    rtLockValidatorSerializeDetectionLeave();
    AssertFailed(); /* too many iterations or destroyed while racing. */
    return false;
}


/**
 * Adds an owner entry to a shared lock record.
 *
 * @returns true on success, false on serious race or we're if out of memory.
 * @param   pShared             The shared lock record.
 * @param   pEntry              The owner entry.
 */
DECLINLINE(bool) rtLockValidatorRecSharedAddOwner(PRTLOCKVALRECSHRD pShared, PRTLOCKVALRECSHRDOWN pEntry)
{
    rtLockValidatorSerializeDetectionEnter();
    if (RT_LIKELY(pShared->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC)) /* paranoia */
    {
        if (   ASMAtomicIncU32(&pShared->cEntries) > pShared->cAllocated /** @todo add fudge */
            && !rtLockValidatorRecSharedMakeRoom(pShared))
            return false; /* the worker leave the lock */

        PRTLOCKVALRECSHRDOWN volatile  *papOwners = pShared->papOwners;
        uint32_t const                  cMax      = pShared->cAllocated;
        for (unsigned i = 0; i < 100; i++)
        {
            for (uint32_t iEntry = 0; iEntry < cMax; iEntry++)
            {
                if (ASMAtomicCmpXchgPtr(&papOwners[iEntry], pEntry, NULL))
                {
                    rtLockValidatorSerializeDetectionLeave();
                    return true;
                }
            }
            Assert(i != 25);
        }
        AssertFailed();
    }
    rtLockValidatorSerializeDetectionLeave();
    return false;
}


/**
 * Remove an owner entry from a shared lock record and free it.
 *
 * @param   pShared             The shared lock record.
 * @param   pEntry              The owner entry to remove.
 * @param   iEntry              The last known index.
 */
DECLINLINE(void) rtLockValidatorRecSharedRemoveAndFreeOwner(PRTLOCKVALRECSHRD pShared, PRTLOCKVALRECSHRDOWN pEntry,
                                                            uint32_t iEntry)
{
    /*
     * Remove it from the table.
     */
    rtLockValidatorSerializeDetectionEnter();
    AssertReturnVoidStmt(pShared->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, rtLockValidatorSerializeDetectionLeave());
    if (RT_UNLIKELY(   iEntry >= pShared->cAllocated
                    || !ASMAtomicCmpXchgPtr(&pShared->papOwners[iEntry], NULL, pEntry)))
    {
        /* this shouldn't happen yet... */
        AssertFailed();
        PRTLOCKVALRECSHRDOWN volatile  *papOwners = pShared->papOwners;
        uint32_t const                  cMax      = pShared->cAllocated;
        for (iEntry = 0; iEntry < cMax; iEntry++)
            if (ASMAtomicCmpXchgPtr(&papOwners[iEntry], NULL, pEntry))
               break;
        AssertReturnVoidStmt(iEntry < cMax, rtLockValidatorSerializeDetectionLeave());
    }
    uint32_t cNow = ASMAtomicDecU32(&pShared->cEntries);
    Assert(!(cNow & RT_BIT_32(31))); NOREF(cNow);
    rtLockValidatorSerializeDetectionLeave();

    /*
     * Successfully removed, now free it.
     */
    rtLockValidatorRecSharedFreeOwner(pEntry);
}


RTDECL(void) RTLockValidatorRecSharedResetOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread, PCRTLOCKVALSRCPOS pSrcPos)
{
    AssertReturnVoid(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC);
    if (!pRec->fEnabled)
        return;
    AssertReturnVoid(hThread == NIL_RTTHREAD || hThread->u32Magic == RTTHREADINT_MAGIC);
    AssertReturnVoid(pRec->fSignaller);

    /*
     * Free all current owners.
     */
    rtLockValidatorSerializeDetectionEnter();
    while (ASMAtomicUoReadU32(&pRec->cEntries) > 0)
    {
        AssertReturnVoidStmt(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, rtLockValidatorSerializeDetectionLeave());
        uint32_t                        iEntry     = 0;
        uint32_t                        cEntries   = pRec->cAllocated;
        PRTLOCKVALRECSHRDOWN volatile  *papEntries = pRec->papOwners;
        while (iEntry < cEntries)
        {
            PRTLOCKVALRECSHRDOWN pEntry = ASMAtomicXchgPtrT(&papEntries[iEntry], NULL, PRTLOCKVALRECSHRDOWN);
            if (pEntry)
            {
                ASMAtomicDecU32(&pRec->cEntries);
                rtLockValidatorSerializeDetectionLeave();

                rtLockValidatorRecSharedFreeOwner(pEntry);

                rtLockValidatorSerializeDetectionEnter();
                if (ASMAtomicUoReadU32(&pRec->cEntries) == 0)
                    break;
                cEntries   = pRec->cAllocated;
                papEntries = pRec->papOwners;
            }
            iEntry++;
        }
    }
    rtLockValidatorSerializeDetectionLeave();

    if (hThread != NIL_RTTHREAD)
    {
        /*
         * Allocate a new owner entry and insert it into the table.
         */
        PRTLOCKVALRECUNION pEntry = rtLockValidatorRecSharedAllocOwner(pRec, hThread, pSrcPos);
        if (    pEntry
            &&  !rtLockValidatorRecSharedAddOwner(pRec, &pEntry->ShrdOwner))
            rtLockValidatorRecSharedFreeOwner(&pEntry->ShrdOwner);
    }
}
RT_EXPORT_SYMBOL(RTLockValidatorRecSharedResetOwner);


RTDECL(void) RTLockValidatorRecSharedAddOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread, PCRTLOCKVALSRCPOS pSrcPos)
{
    AssertReturnVoid(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC);
    if (!pRec->fEnabled)
        return;
    if (hThread == NIL_RTTHREAD)
    {
        hThread = RTThreadSelfAutoAdopt();
        AssertReturnVoid(hThread != NIL_RTTHREAD);
    }
    AssertReturnVoid(hThread->u32Magic == RTTHREADINT_MAGIC);

    /*
     * Recursive?
     *
     * Note! This code can be optimized to try avoid scanning the table on
     *       insert.  However, that's annoying work that makes the code big,
     *       so it can wait til later sometime.
     */
    PRTLOCKVALRECUNION pEntry = rtLockValidatorRecSharedFindOwner(pRec, hThread, NULL);
    if (pEntry)
    {
        Assert(!pRec->fSignaller);
        pEntry->ShrdOwner.cRecursion++;
        rtLockValidatorStackPushRecursion(hThread, pEntry, pSrcPos);
        return;
    }

    /*
     * Allocate a new owner entry and insert it into the table.
     */
    pEntry = rtLockValidatorRecSharedAllocOwner(pRec, hThread, pSrcPos);
    if (pEntry)
    {
        if (rtLockValidatorRecSharedAddOwner(pRec, &pEntry->ShrdOwner))
        {
            if (!pRec->fSignaller)
                rtLockValidatorStackPush(hThread, pEntry);
        }
        else
            rtLockValidatorRecSharedFreeOwner(&pEntry->ShrdOwner);
    }
}
RT_EXPORT_SYMBOL(RTLockValidatorRecSharedAddOwner);


RTDECL(void) RTLockValidatorRecSharedRemoveOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread)
{
    AssertReturnVoid(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC);
    if (!pRec->fEnabled)
        return;
    if (hThread == NIL_RTTHREAD)
    {
        hThread = RTThreadSelfAutoAdopt();
        AssertReturnVoid(hThread != NIL_RTTHREAD);
    }
    AssertReturnVoid(hThread->u32Magic == RTTHREADINT_MAGIC);

    /*
     * Find the entry hope it's a recursive one.
     */
    uint32_t iEntry = UINT32_MAX; /* shuts up gcc */
    PRTLOCKVALRECUNION pEntry = rtLockValidatorRecSharedFindOwner(pRec, hThread, &iEntry);
    AssertReturnVoid(pEntry);
    AssertReturnVoid(pEntry->ShrdOwner.cRecursion > 0);

    uint32_t c = --pEntry->ShrdOwner.cRecursion;
    if (c == 0)
    {
        if (!pRec->fSignaller)
            rtLockValidatorStackPop(hThread, (PRTLOCKVALRECUNION)pEntry);
        rtLockValidatorRecSharedRemoveAndFreeOwner(pRec, &pEntry->ShrdOwner, iEntry);
    }
    else
    {
        Assert(!pRec->fSignaller);
        rtLockValidatorStackPopRecursion(hThread, pEntry);
    }
}
RT_EXPORT_SYMBOL(RTLockValidatorRecSharedRemoveOwner);


RTDECL(bool) RTLockValidatorRecSharedIsOwner(PRTLOCKVALRECSHRD pRec, RTTHREAD hThread)
{
    /* Validate and resolve input. */
    AssertReturn(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, false);
    if (!pRec->fEnabled)
        return false;
    if (hThread == NIL_RTTHREAD)
    {
        hThread = RTThreadSelfAutoAdopt();
        AssertReturn(hThread != NIL_RTTHREAD, false);
    }
    AssertReturn(hThread->u32Magic == RTTHREADINT_MAGIC, false);

    /* Do the job. */
    PRTLOCKVALRECUNION pEntry = rtLockValidatorRecSharedFindOwner(pRec, hThread, NULL);
    return pEntry != NULL;
}
RT_EXPORT_SYMBOL(RTLockValidatorRecSharedIsOwner);


RTDECL(int) RTLockValidatorRecSharedCheckAndRelease(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf)
{
    AssertReturn(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRec->fEnabled)
        return VINF_SUCCESS;
    if (hThreadSelf == NIL_RTTHREAD)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        AssertReturn(hThreadSelf != NIL_RTTHREAD, VERR_SEM_LV_INTERNAL_ERROR);
    }
    Assert(hThreadSelf == RTThreadSelf());
    AssertReturn(hThreadSelf->u32Magic == RTTHREADINT_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);

    /*
     * Locate the entry for this thread in the table.
     */
    uint32_t            iEntry = 0;
    PRTLOCKVALRECUNION  pEntry = rtLockValidatorRecSharedFindOwner(pRec, hThreadSelf, &iEntry);
    if (RT_UNLIKELY(!pEntry))
    {
        rtLockValComplainFirst("Not owner (shared)!", NULL, hThreadSelf, (PRTLOCKVALRECUNION)pRec, true);
        rtLockValComplainPanic();
        return VERR_SEM_LV_NOT_OWNER;
    }

    /*
     * Check the release order.
     */
    if (   pRec->hClass != NIL_RTLOCKVALCLASS
        && pRec->hClass->fStrictReleaseOrder
        && pRec->hClass->cMsMinOrder != RT_INDEFINITE_WAIT
       )
    {
        int rc = rtLockValidatorStackCheckReleaseOrder(hThreadSelf, (PRTLOCKVALRECUNION)pEntry);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Release the ownership or unwind a level of recursion.
     */
    Assert(pEntry->ShrdOwner.cRecursion > 0);
    uint32_t c = --pEntry->ShrdOwner.cRecursion;
    if (c == 0)
    {
        rtLockValidatorStackPop(hThreadSelf, pEntry);
        rtLockValidatorRecSharedRemoveAndFreeOwner(pRec, &pEntry->ShrdOwner, iEntry);
    }
    else
        rtLockValidatorStackPopRecursion(hThreadSelf, pEntry);

    return VINF_SUCCESS;
}


RTDECL(int) RTLockValidatorRecSharedCheckSignaller(PRTLOCKVALRECSHRD pRec, RTTHREAD hThreadSelf)
{
    AssertReturn(pRec->Core.u32Magic == RTLOCKVALRECSHRD_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);
    if (!pRec->fEnabled)
        return VINF_SUCCESS;
    if (hThreadSelf == NIL_RTTHREAD)
    {
        hThreadSelf = RTThreadSelfAutoAdopt();
        AssertReturn(hThreadSelf != NIL_RTTHREAD, VERR_SEM_LV_INTERNAL_ERROR);
    }
    Assert(hThreadSelf == RTThreadSelf());
    AssertReturn(hThreadSelf->u32Magic == RTTHREADINT_MAGIC, VERR_SEM_LV_INVALID_PARAMETER);

    /*
     * Locate the entry for this thread in the table.
     */
    uint32_t            iEntry = 0;
    PRTLOCKVALRECUNION  pEntry = rtLockValidatorRecSharedFindOwner(pRec, hThreadSelf, &iEntry);
    if (RT_UNLIKELY(!pEntry))
    {
        rtLockValComplainFirst("Invalid signaller!", NULL, hThreadSelf, (PRTLOCKVALRECUNION)pRec, true);
        rtLockValComplainPanic();
        return VERR_SEM_LV_NOT_SIGNALLER;
    }
    return VINF_SUCCESS;
}


RTDECL(int32_t) RTLockValidatorWriteLockGetCount(RTTHREAD Thread)
{
    if (Thread == NIL_RTTHREAD)
        return 0;

    PRTTHREADINT pThread = rtThreadGet(Thread);
    if (!pThread)
        return VERR_INVALID_HANDLE;
    int32_t cWriteLocks = ASMAtomicReadS32(&pThread->LockValidator.cWriteLocks);
    rtThreadRelease(pThread);
    return cWriteLocks;
}
RT_EXPORT_SYMBOL(RTLockValidatorWriteLockGetCount);


RTDECL(void) RTLockValidatorWriteLockInc(RTTHREAD Thread)
{
    PRTTHREADINT pThread = rtThreadGet(Thread);
    AssertReturnVoid(pThread);
    ASMAtomicIncS32(&pThread->LockValidator.cWriteLocks);
    rtThreadRelease(pThread);
}
RT_EXPORT_SYMBOL(RTLockValidatorWriteLockInc);


RTDECL(void) RTLockValidatorWriteLockDec(RTTHREAD Thread)
{
    PRTTHREADINT pThread = rtThreadGet(Thread);
    AssertReturnVoid(pThread);
    ASMAtomicDecS32(&pThread->LockValidator.cWriteLocks);
    rtThreadRelease(pThread);
}
RT_EXPORT_SYMBOL(RTLockValidatorWriteLockDec);


RTDECL(int32_t) RTLockValidatorReadLockGetCount(RTTHREAD Thread)
{
    if (Thread == NIL_RTTHREAD)
        return 0;

    PRTTHREADINT pThread = rtThreadGet(Thread);
    if (!pThread)
        return VERR_INVALID_HANDLE;
    int32_t cReadLocks = ASMAtomicReadS32(&pThread->LockValidator.cReadLocks);
    rtThreadRelease(pThread);
    return cReadLocks;
}
RT_EXPORT_SYMBOL(RTLockValidatorReadLockGetCount);


RTDECL(void) RTLockValidatorReadLockInc(RTTHREAD Thread)
{
    PRTTHREADINT pThread = rtThreadGet(Thread);
    Assert(pThread);
    ASMAtomicIncS32(&pThread->LockValidator.cReadLocks);
    rtThreadRelease(pThread);
}
RT_EXPORT_SYMBOL(RTLockValidatorReadLockInc);


RTDECL(void) RTLockValidatorReadLockDec(RTTHREAD Thread)
{
    PRTTHREADINT pThread = rtThreadGet(Thread);
    Assert(pThread);
    ASMAtomicDecS32(&pThread->LockValidator.cReadLocks);
    rtThreadRelease(pThread);
}
RT_EXPORT_SYMBOL(RTLockValidatorReadLockDec);


RTDECL(void *) RTLockValidatorQueryBlocking(RTTHREAD hThread)
{
    void           *pvLock  = NULL;
    PRTTHREADINT    pThread = rtThreadGet(hThread);
    if (pThread)
    {
        RTTHREADSTATE enmState = rtThreadGetState(pThread);
        if (RTTHREAD_IS_SLEEPING(enmState))
        {
            rtLockValidatorSerializeDetectionEnter();

            enmState = rtThreadGetState(pThread);
            if (RTTHREAD_IS_SLEEPING(enmState))
            {
                PRTLOCKVALRECUNION pRec = rtLockValidatorReadRecUnionPtr(&pThread->LockValidator.pRec);
                if (pRec)
                {
                    switch (pRec->Core.u32Magic)
                    {
                        case RTLOCKVALRECEXCL_MAGIC:
                            pvLock = pRec->Excl.hLock;
                            break;

                        case RTLOCKVALRECSHRDOWN_MAGIC:
                            pRec = (PRTLOCKVALRECUNION)pRec->ShrdOwner.pSharedRec;
                            if (!pRec || pRec->Core.u32Magic != RTLOCKVALRECSHRD_MAGIC)
                                break;
                            RT_FALL_THRU();
                        case RTLOCKVALRECSHRD_MAGIC:
                            pvLock = pRec->Shared.hLock;
                            break;
                    }
                    if (RTThreadGetState(pThread) != enmState)
                        pvLock = NULL;
                }
            }

            rtLockValidatorSerializeDetectionLeave();
        }
        rtThreadRelease(pThread);
    }
    return pvLock;
}
RT_EXPORT_SYMBOL(RTLockValidatorQueryBlocking);


RTDECL(bool) RTLockValidatorIsBlockedThreadInValidator(RTTHREAD hThread)
{
    bool            fRet    = false;
    PRTTHREADINT    pThread = rtThreadGet(hThread);
    if (pThread)
    {
        fRet = ASMAtomicReadBool(&pThread->LockValidator.fInValidator);
        rtThreadRelease(pThread);
    }
    return fRet;
}
RT_EXPORT_SYMBOL(RTLockValidatorIsBlockedThreadInValidator);


RTDECL(bool) RTLockValidatorHoldsLocksInClass(RTTHREAD hCurrentThread, RTLOCKVALCLASS hClass)
{
    bool            fRet    = false;
    if (hCurrentThread == NIL_RTTHREAD)
        hCurrentThread = RTThreadSelf();
    else
        Assert(hCurrentThread == RTThreadSelf());
    PRTTHREADINT    pThread = rtThreadGet(hCurrentThread);
    if (pThread)
    {
        if (hClass != NIL_RTLOCKVALCLASS)
        {
            PRTLOCKVALRECUNION pCur = rtLockValidatorReadRecUnionPtr(&pThread->LockValidator.pStackTop);
            while (RT_VALID_PTR(pCur) && !fRet)
            {
                switch (pCur->Core.u32Magic)
                {
                    case RTLOCKVALRECEXCL_MAGIC:
                        fRet = pCur->Excl.hClass == hClass;
                        pCur = rtLockValidatorReadRecUnionPtr(&pCur->Excl.pDown);
                        break;
                    case RTLOCKVALRECSHRDOWN_MAGIC:
                        fRet = RT_VALID_PTR(pCur->ShrdOwner.pSharedRec)
                            && pCur->ShrdOwner.pSharedRec->hClass == hClass;
                        pCur = rtLockValidatorReadRecUnionPtr(&pCur->ShrdOwner.pDown);
                        break;
                    case RTLOCKVALRECNEST_MAGIC:
                        switch (pCur->Nest.pRec->Core.u32Magic)
                        {
                            case RTLOCKVALRECEXCL_MAGIC:
                                fRet = pCur->Nest.pRec->Excl.hClass == hClass;
                                break;
                            case RTLOCKVALRECSHRDOWN_MAGIC:
                                fRet = RT_VALID_PTR(pCur->ShrdOwner.pSharedRec)
                                    && pCur->Nest.pRec->ShrdOwner.pSharedRec->hClass == hClass;
                                break;
                        }
                        pCur = rtLockValidatorReadRecUnionPtr(&pCur->Nest.pDown);
                        break;
                    default:
                        pCur = NULL;
                        break;
                }
            }
        }

        rtThreadRelease(pThread);
    }
    return fRet;
}
RT_EXPORT_SYMBOL(RTLockValidatorHoldsLocksInClass);


RTDECL(bool) RTLockValidatorHoldsLocksInSubClass(RTTHREAD hCurrentThread, RTLOCKVALCLASS hClass, uint32_t uSubClass)
{
    bool            fRet    = false;
    if (hCurrentThread == NIL_RTTHREAD)
        hCurrentThread = RTThreadSelf();
    else
        Assert(hCurrentThread == RTThreadSelf());
    PRTTHREADINT    pThread = rtThreadGet(hCurrentThread);
    if (pThread)
    {
        if (hClass != NIL_RTLOCKVALCLASS)
        {
            PRTLOCKVALRECUNION pCur = rtLockValidatorReadRecUnionPtr(&pThread->LockValidator.pStackTop);
            while (RT_VALID_PTR(pCur) && !fRet)
            {
                switch (pCur->Core.u32Magic)
                {
                    case RTLOCKVALRECEXCL_MAGIC:
                        fRet = pCur->Excl.hClass == hClass
                            && pCur->Excl.uSubClass == uSubClass;
                        pCur = rtLockValidatorReadRecUnionPtr(&pCur->Excl.pDown);
                        break;
                    case RTLOCKVALRECSHRDOWN_MAGIC:
                        fRet = RT_VALID_PTR(pCur->ShrdOwner.pSharedRec)
                            && pCur->ShrdOwner.pSharedRec->hClass == hClass
                            && pCur->ShrdOwner.pSharedRec->uSubClass == uSubClass;
                        pCur = rtLockValidatorReadRecUnionPtr(&pCur->ShrdOwner.pDown);
                        break;
                    case RTLOCKVALRECNEST_MAGIC:
                        switch (pCur->Nest.pRec->Core.u32Magic)
                        {
                            case RTLOCKVALRECEXCL_MAGIC:
                                fRet = pCur->Nest.pRec->Excl.hClass == hClass
                                    && pCur->Nest.pRec->Excl.uSubClass == uSubClass;
                                break;
                            case RTLOCKVALRECSHRDOWN_MAGIC:
                                fRet = RT_VALID_PTR(pCur->ShrdOwner.pSharedRec)
                                    && pCur->Nest.pRec->ShrdOwner.pSharedRec->hClass == hClass
                                    && pCur->Nest.pRec->ShrdOwner.pSharedRec->uSubClass == uSubClass;
                                break;
                        }
                        pCur = rtLockValidatorReadRecUnionPtr(&pCur->Nest.pDown);
                        break;
                    default:
                        pCur = NULL;
                        break;
                }
            }
        }

        rtThreadRelease(pThread);
    }
    return fRet;
}
RT_EXPORT_SYMBOL(RTLockValidatorHoldsLocksInClass);


RTDECL(bool) RTLockValidatorSetEnabled(bool fEnabled)
{
    return ASMAtomicXchgBool(&g_fLockValidatorEnabled, fEnabled);
}
RT_EXPORT_SYMBOL(RTLockValidatorSetEnabled);


RTDECL(bool) RTLockValidatorIsEnabled(void)
{
    return ASMAtomicUoReadBool(&g_fLockValidatorEnabled);
}
RT_EXPORT_SYMBOL(RTLockValidatorIsEnabled);


RTDECL(bool) RTLockValidatorSetQuiet(bool fQuiet)
{
    return ASMAtomicXchgBool(&g_fLockValidatorQuiet, fQuiet);
}
RT_EXPORT_SYMBOL(RTLockValidatorSetQuiet);


RTDECL(bool) RTLockValidatorIsQuiet(void)
{
    return ASMAtomicUoReadBool(&g_fLockValidatorQuiet);
}
RT_EXPORT_SYMBOL(RTLockValidatorIsQuiet);


RTDECL(bool) RTLockValidatorSetMayPanic(bool fMayPanic)
{
    return ASMAtomicXchgBool(&g_fLockValidatorMayPanic, fMayPanic);
}
RT_EXPORT_SYMBOL(RTLockValidatorSetMayPanic);


RTDECL(bool) RTLockValidatorMayPanic(void)
{
    return ASMAtomicUoReadBool(&g_fLockValidatorMayPanic);
}
RT_EXPORT_SYMBOL(RTLockValidatorMayPanic);

