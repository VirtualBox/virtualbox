/* $Id: tracebuf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Tracebuffer common functions.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include "internal/iprt.h"
#include <iprt/trace.h>


#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/errcore.h>
#include <iprt/log.h>
#ifndef IN_RC
# include <iprt/mem.h>
#endif
#include <iprt/mp.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/time.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Alignment used to place the trace buffer members, this should be a multiple
 * of the cache line size if possible.  (We should dynamically determine it.) */
#define RTTRACEBUF_ALIGNMENT        64
AssertCompile(RTTRACEBUF_ALIGNMENT >= sizeof(uint64_t) * 2);

/** The maximum number of entries. */
#define RTTRACEBUF_MAX_ENTRIES      _64K
/** The minimum number of entries. */
#define RTTRACEBUF_MIN_ENTRIES      4
/** The default number of entries. */
#define RTTRACEBUF_DEF_ENTRIES      256

/** The maximum entry size. */
#define RTTRACEBUF_MAX_ENTRY_SIZE   _1M
/** The minimum entry size. */
#define RTTRACEBUF_MIN_ENTRY_SIZE   RTTRACEBUF_ALIGNMENT
/** The default entry size. */
#define RTTRACEBUF_DEF_ENTRY_SIZE   256
AssertCompile(!(RTTRACEBUF_DEF_ENTRY_SIZE & (RTTRACEBUF_DEF_ENTRY_SIZE - 1)));

/**
 * The volatile trace buffer members.
 */
typedef struct RTTRACEBUFVOLATILE
{
    /** Reference counter. */
    uint32_t volatile   cRefs;
    /** The next entry to make use of. */
    uint32_t volatile   iEntry;
} RTTRACEBUFVOLATILE;
/** Pointer to the volatile parts of a trace buffer. */
typedef RTTRACEBUFVOLATILE *PRTTRACEBUFVOLATILE;


/**
 * Trace buffer entry.
 */
typedef struct RTTRACEBUFENTRY
{
    /** The nano second entry time stamp. */
    uint64_t            NanoTS;
    /** The ID of the CPU the event was recorded.  */
    RTCPUID             idCpu;
    /** The message. */
    char                szMsg[RTTRACEBUF_ALIGNMENT - sizeof(uint64_t) - sizeof(RTCPUID)];
} RTTRACEBUFENTRY;
AssertCompile(sizeof(RTTRACEBUFENTRY) <= RTTRACEBUF_ALIGNMENT);
/** Pointer to a trace buffer entry. */
typedef RTTRACEBUFENTRY *PRTTRACEBUFENTRY;



/**
 * Trace buffer structure.
 *
 * @remarks     This structure must be context agnostic, i.e. no pointers or
 *              other types that may differ between contexts (R3/R0/RC).
 */
typedef struct RTTRACEBUFINT
{
    /** Magic value (RTTRACEBUF_MAGIC). */
    uint32_t            u32Magic;
    /** The entry size. */
    uint32_t            cbEntry;
    /** The number of entries. */
    uint32_t            cEntries;
    /** Flags (always zero for now).  */
    uint32_t            fFlags;
    /** The offset to the volatile members (RTTRACEBUFVOLATILE) (relative to
     *  the start of this structure). */
    uint32_t            offVolatile;
    /** The offset to the entries (relative to the start of this structure). */
    uint32_t            offEntries;
    /** Reserved entries. */
    uint32_t            au32Reserved[2];
} RTTRACEBUFINT;
/** Pointer to a const trace buffer. */
typedef RTTRACEBUFINT const *PCRTTRACEBUFINT;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Get the current CPU Id.
 */
#if defined(IN_RING0) \
 || defined(RT_OS_WINDOWS) \
 || (!defined(RT_ARCH_AMD64) && !defined(RT_ARCH_X86))
# define RTTRACEBUF_CUR_CPU()   RTMpCpuId()
#else
# define RTTRACEBUF_CUR_CPU()   ASMGetApicId() /** @todo this isn't good enough for big boxes with lots of CPUs/cores. */
#endif

/** Calculates the address of the volatile trace buffer members. */
#define RTTRACEBUF_TO_VOLATILE(a_pThis)     ((PRTTRACEBUFVOLATILE)((uint8_t *)(a_pThis) + (a_pThis)->offVolatile))

/** Calculates the address of a trace buffer entry. */
#define RTTRACEBUF_TO_ENTRY(a_pThis, a_iEntry) \
    ((PRTTRACEBUFENTRY)( (uint8_t *)(a_pThis) + (a_pThis)->offEntries + (a_iEntry) * (a_pThis)->cbEntry ))

/** Validates a trace buffer handle and returns rc if not valid. */
#define RTTRACEBUF_VALID_RETURN_RC(a_pThis, a_rc) \
    do { \
        AssertPtrReturn((a_pThis), (a_rc)); \
        AssertReturn((a_pThis)->u32Magic == RTTRACEBUF_MAGIC, (a_rc)); \
        AssertReturn((a_pThis)->offVolatile < RTTRACEBUF_ALIGNMENT * 2, (a_rc)); \
        AssertReturn(RTTRACEBUF_TO_VOLATILE(a_pThis)->cRefs > 0, (a_rc)); \
    } while (0)

/**
 * Resolves and validates a trace buffer handle and returns rc if not valid.
 *
 * @param   a_hTraceBuf     The trace buffer handle passed by the user.
 * @param   a_pThis         Where to store the trace buffer pointer.
 */
#define RTTRACEBUF_RESOLVE_VALIDATE_RETAIN_RETURN(a_hTraceBuf, a_pThis) \
    do { \
        uint32_t cRefs; \
        if ((a_hTraceBuf) == RTTRACEBUF_DEFAULT) \
        { \
            (a_pThis) = RTTraceGetDefaultBuf(); \
            if (!RT_VALID_PTR(a_pThis)) \
                return VERR_NOT_FOUND; \
        } \
        else \
        { \
            (a_pThis) = (a_hTraceBuf); \
            AssertPtrReturn((a_pThis), VERR_INVALID_HANDLE); \
        } \
        AssertReturn((a_pThis)->u32Magic == RTTRACEBUF_MAGIC, VERR_INVALID_HANDLE); \
        AssertReturn((a_pThis)->offVolatile < RTTRACEBUF_ALIGNMENT * 2, VERR_INVALID_HANDLE); \
        \
        cRefs = ASMAtomicIncU32(&RTTRACEBUF_TO_VOLATILE(a_pThis)->cRefs); \
        if (RT_UNLIKELY(cRefs < 1 || cRefs >= _1M)) \
        { \
            ASMAtomicDecU32(&RTTRACEBUF_TO_VOLATILE(a_pThis)->cRefs); \
            AssertFailedReturn(VERR_INVALID_HANDLE); \
        } \
    } while (0)


/**
 * Drops a trace buffer reference.
 *
 * @param   a_pThis     Pointer to the trace buffer.
 */
#define RTTRACEBUF_DROP_REFERENCE(a_pThis) \
    do { \
        uint32_t cRefs = ASMAtomicDecU32(&RTTRACEBUF_TO_VOLATILE(a_pThis)->cRefs); \
        if (!cRefs) \
            rtTraceBufDestroy((RTTRACEBUFINT *)a_pThis); \
    } while (0)


/**
 * The prologue code for a RTTraceAddSomething function.
 *
 * Resolves a trace buffer handle, grabs a reference to it and allocates the
 * next entry.  Return with an appropriate error status on failure.
 *
 * @param   a_hTraceBuf     The trace buffer handle passed by the user.
 *
 * @remarks This is kind of ugly, sorry.
 */
#define RTTRACEBUF_ADD_PROLOGUE(a_hTraceBuf) \
    int                 rc; \
    uint32_t            cRefs; \
    uint32_t            iEntry; \
    PCRTTRACEBUFINT     pThis; \
    PRTTRACEBUFVOLATILE pVolatile; \
    PRTTRACEBUFENTRY    pEntry; \
    char               *pszBuf; \
    size_t              cchBuf; \
    \
    /* Resolve and validate the handle. */ \
    if ((a_hTraceBuf) == RTTRACEBUF_DEFAULT) \
    { \
        pThis = RTTraceGetDefaultBuf(); \
        if (!RT_VALID_PTR(pThis)) \
            return VERR_NOT_FOUND; \
    } \
    else if ((a_hTraceBuf) != NIL_RTTRACEBUF) \
    { \
        pThis = (a_hTraceBuf); \
        AssertPtrReturn(pThis, VERR_INVALID_HANDLE); \
    } \
    else \
        return VERR_INVALID_HANDLE; \
    \
    AssertReturn(pThis->u32Magic == RTTRACEBUF_MAGIC, VERR_INVALID_HANDLE); \
    if (pThis->fFlags & RTTRACEBUF_FLAGS_DISABLED) \
        return VINF_SUCCESS; \
    AssertReturn(pThis->offVolatile < RTTRACEBUF_ALIGNMENT * 2, VERR_INVALID_HANDLE); \
    pVolatile = RTTRACEBUF_TO_VOLATILE(pThis); \
    \
    /* Grab a reference. */ \
    cRefs = ASMAtomicIncU32(&pVolatile->cRefs); \
    if (RT_UNLIKELY(cRefs < 1 || cRefs >= _1M)) \
    { \
        ASMAtomicDecU32(&pVolatile->cRefs); \
        AssertFailedReturn(VERR_INVALID_HANDLE); \
    } \
    \
    /* Grab the next entry and set the time stamp. */ \
    iEntry  = ASMAtomicIncU32(&pVolatile->iEntry) - 1; \
    iEntry %= pThis->cEntries; \
    pEntry  = RTTRACEBUF_TO_ENTRY(pThis, iEntry); \
    pEntry->NanoTS = RTTimeNanoTS(); \
    pEntry->idCpu  = RTTRACEBUF_CUR_CPU(); \
    pszBuf  = &pEntry->szMsg[0]; \
    *pszBuf = '\0'; \
    cchBuf  = pThis->cbEntry - RT_UOFFSETOF(RTTRACEBUFENTRY, szMsg) - 1; \
    rc      = VINF_SUCCESS


/**
 * Used by a RTTraceAddPosSomething to store the source position in the entry
 * prior to adding the actual trace message text.
 *
 * Both pszBuf and cchBuf will be adjusted such that pszBuf points and the zero
 * terminator after the source position part.
 */
#define RTTRACEBUF_ADD_STORE_SRC_POS() \
    do { \
        /* file(line): - no path */ \
        size_t cchPos = RTStrPrintf(pszBuf, cchBuf, "%s(%d): ", RTPathFilename(pszFile), iLine); \
        pszBuf += cchPos; \
        cchBuf -= cchPos; \
        NOREF(pszFunction); \
    } while (0)


/**
 * The epilogue code for a RTTraceAddSomething function.
 *
 * This will release the trace buffer reference.
 */
#define RTTRACEBUF_ADD_EPILOGUE() \
    cRefs = ASMAtomicDecU32(&pVolatile->cRefs); \
    if (!cRefs) \
        rtTraceBufDestroy((RTTRACEBUFINT *)pThis); \
    return rc


#ifndef IN_RC /* Drop this in RC context (too lazy to split the file). */

RTDECL(int) RTTraceBufCreate(PRTTRACEBUF phTraceBuf, uint32_t cEntries, uint32_t cbEntry, uint32_t fFlags)
{
    AssertPtrReturn(phTraceBuf, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~(RTTRACEBUF_FLAGS_MASK & ~ RTTRACEBUF_FLAGS_FREE_ME)), VERR_INVALID_PARAMETER);
    AssertMsgReturn(cbEntry  <= RTTRACEBUF_MAX_ENTRIES,    ("%#x\n", cbEntry),  VERR_OUT_OF_RANGE);
    AssertMsgReturn(cEntries <= RTTRACEBUF_MAX_ENTRY_SIZE, ("%#x\n", cEntries), VERR_OUT_OF_RANGE);

    /*
     * Apply default and alignment adjustments.
     */
    if (!cbEntry)
        cbEntry = RTTRACEBUF_DEF_ENTRY_SIZE;
    else
        cbEntry = RT_ALIGN_32(cbEntry, RTTRACEBUF_ALIGNMENT);

    if (!cEntries)
        cEntries = RTTRACEBUF_DEF_ENTRIES;
    else if (cEntries < RTTRACEBUF_MIN_ENTRIES)
        cEntries = RTTRACEBUF_MIN_ENTRIES;

    /*
     * Calculate the required buffer size, allocte it and hand it on to the
     * carver API.
     */
    size_t  cbBlock = cbEntry * cEntries
                    + RT_ALIGN_Z(sizeof(RTTRACEBUFINT),      RTTRACEBUF_ALIGNMENT)
                    + RT_ALIGN_Z(sizeof(RTTRACEBUFVOLATILE), RTTRACEBUF_ALIGNMENT);
    void   *pvBlock = RTMemAlloc(cbBlock);
    if (!((uintptr_t)pvBlock & (RTTRACEBUF_ALIGNMENT - 1)))
    {
        RTMemFree(pvBlock);
        cbBlock += RTTRACEBUF_ALIGNMENT - 1;
        pvBlock = RTMemAlloc(cbBlock);
    }
    int rc;
    if (pvBlock)
    {
        rc = RTTraceBufCarve(phTraceBuf, cEntries, cbEntry, fFlags, pvBlock, &cbBlock);
        if (RT_FAILURE(rc))
            RTMemFree(pvBlock);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


RTDECL(int) RTTraceBufCarve(PRTTRACEBUF phTraceBuf, uint32_t cEntries, uint32_t cbEntry, uint32_t fFlags,
                            void *pvBlock, size_t *pcbBlock)
{
    AssertPtrReturn(phTraceBuf, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~RTTRACEBUF_FLAGS_MASK), VERR_INVALID_PARAMETER);
    AssertMsgReturn(cbEntry  <= RTTRACEBUF_MAX_ENTRIES,    ("%#x\n", cbEntry),  VERR_OUT_OF_RANGE);
    AssertMsgReturn(cEntries <= RTTRACEBUF_MAX_ENTRY_SIZE, ("%#x\n", cEntries), VERR_OUT_OF_RANGE);
    AssertPtrReturn(pcbBlock, VERR_INVALID_POINTER);
    size_t const cbBlock = *pcbBlock;
    AssertReturn(RT_VALID_PTR(pvBlock) || !cbBlock, VERR_INVALID_POINTER);

    /*
     * Apply defaults, align sizes and check against available buffer space.
     * This code can be made a bit more clever, if someone feels like it.
     */
    size_t const cbHdr      = RT_ALIGN_Z(sizeof(RTTRACEBUFINT),      RTTRACEBUF_ALIGNMENT)
                            + RT_ALIGN_Z(sizeof(RTTRACEBUFVOLATILE), RTTRACEBUF_ALIGNMENT);
    size_t const cbEntryBuf = cbBlock > cbHdr ? cbBlock - cbHdr : 0;
    if (cbEntry)
        cbEntry = RT_ALIGN_32(cbEntry, RTTRACEBUF_ALIGNMENT);
    else
    {
        if (!cbEntryBuf)
        {
            cbEntry  = RTTRACEBUF_DEF_ENTRY_SIZE;
            cEntries = RTTRACEBUF_DEF_ENTRIES;
        }
        else if (cEntries)
        {
            size_t cbEntryZ = cbBlock / cEntries;
            cbEntryZ &= ~(RTTRACEBUF_ALIGNMENT - 1);
            if (cbEntryZ > RTTRACEBUF_MAX_ENTRIES)
                cbEntryZ = RTTRACEBUF_MAX_ENTRIES;
            cbEntry = (uint32_t)cbEntryZ;
        }
        else if (cbBlock >= RT_ALIGN_32(512, RTTRACEBUF_ALIGNMENT) * 256)
            cbEntry = RT_ALIGN_32(512, RTTRACEBUF_ALIGNMENT);
        else if (cbBlock >= RT_ALIGN_32(256, RTTRACEBUF_ALIGNMENT) * 64)
            cbEntry = RT_ALIGN_32(256, RTTRACEBUF_ALIGNMENT);
        else if (cbBlock >= RT_ALIGN_32(128, RTTRACEBUF_ALIGNMENT) * 32)
            cbEntry = RT_ALIGN_32(128, RTTRACEBUF_ALIGNMENT);
        else
            cbEntry = sizeof(RTTRACEBUFENTRY);
    }
    Assert(RT_ALIGN_32(cbEntry, RTTRACEBUF_ALIGNMENT) == cbEntry);

    if (!cEntries)
    {
        size_t cEntriesZ = cbEntryBuf / cbEntry;
        if (cEntriesZ > RTTRACEBUF_MAX_ENTRIES)
            cEntriesZ = RTTRACEBUF_MAX_ENTRIES;
        cEntries = (uint32_t)cEntriesZ;
    }
    if (cEntries < RTTRACEBUF_MIN_ENTRIES)
        cEntries = RTTRACEBUF_MIN_ENTRIES;

    uint32_t offVolatile = RTTRACEBUF_ALIGNMENT - ((uintptr_t)pvBlock & (RTTRACEBUF_ALIGNMENT - 1));
    if (offVolatile < sizeof(RTTRACEBUFINT))
        offVolatile += RTTRACEBUF_ALIGNMENT;
    size_t cbReqBlock = offVolatile
                      + RT_ALIGN_Z(sizeof(RTTRACEBUFVOLATILE), RTTRACEBUF_ALIGNMENT)
                      + cbEntry * cEntries;
    if (*pcbBlock < cbReqBlock)
    {
        *pcbBlock = cbReqBlock;
        return VERR_BUFFER_OVERFLOW;
    }

    /*
     * Do the carving.
     */
    memset(pvBlock, 0, cbBlock);

    RTTRACEBUFINT *pThis = (RTTRACEBUFINT *)pvBlock;
    pThis->u32Magic         = RTTRACEBUF_MAGIC;
    pThis->cbEntry          = cbEntry;
    pThis->cEntries         = cEntries;
    pThis->fFlags           = fFlags;
    pThis->offVolatile      = offVolatile;
    pThis->offEntries       = offVolatile + RT_ALIGN_Z(sizeof(RTTRACEBUFVOLATILE), RTTRACEBUF_ALIGNMENT);

    PRTTRACEBUFVOLATILE pVolatile = (PRTTRACEBUFVOLATILE)((uint8_t *)pThis + offVolatile);
    pVolatile->cRefs        = 1;
    pVolatile->iEntry       = 0;

    *pcbBlock   = cbBlock - cbReqBlock;
    *phTraceBuf = pThis;
    return VINF_SUCCESS;
}

#endif /* !IN_RC */


/**
 * Destructor.
 *
 * @param   pThis               The trace buffer to destroy.
 */
static void rtTraceBufDestroy(RTTRACEBUFINT *pThis)
{
    AssertReturnVoid(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTTRACEBUF_MAGIC_DEAD, RTTRACEBUF_MAGIC));
    if (pThis->fFlags & RTTRACEBUF_FLAGS_FREE_ME)
    {
#ifdef IN_RC
        AssertReleaseFailed();
#else
        RTMemFree(pThis);
#endif
    }
}


RTDECL(uint32_t) RTTraceBufRetain(RTTRACEBUF hTraceBuf)
{
    PCRTTRACEBUFINT pThis = hTraceBuf;
    RTTRACEBUF_VALID_RETURN_RC(pThis, UINT32_MAX);
    return ASMAtomicIncU32(&RTTRACEBUF_TO_VOLATILE(pThis)->cRefs);
}


RTDECL(uint32_t) RTTraceBufRelease(RTTRACEBUF hTraceBuf)
{
    if (hTraceBuf == NIL_RTTRACEBUF)
        return 0;

    PCRTTRACEBUFINT pThis = hTraceBuf;
    RTTRACEBUF_VALID_RETURN_RC(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&RTTRACEBUF_TO_VOLATILE(pThis)->cRefs);
    if (!cRefs)
        rtTraceBufDestroy((RTTRACEBUFINT *)pThis);
    return cRefs;
}


RTDECL(int) RTTraceBufAddMsg(RTTRACEBUF hTraceBuf, const char *pszMsg)
{
    RTTRACEBUF_ADD_PROLOGUE(hTraceBuf);
    RTStrCopy(pszBuf, cchBuf, pszMsg);
    RTTRACEBUF_ADD_EPILOGUE();
}


RTDECL(int) RTTraceBufAddMsgEx(  RTTRACEBUF hTraceBuf, const char *pszMsg, size_t cbMaxMsg)
{
    RTTRACEBUF_ADD_PROLOGUE(hTraceBuf);
    RTStrCopyEx(pszBuf, cchBuf, pszMsg, cbMaxMsg);
    RTTRACEBUF_ADD_EPILOGUE();
}


RTDECL(int) RTTraceBufAddMsgF(RTTRACEBUF hTraceBuf, const char *pszMsgFmt, ...)
{
    int         rc;
    va_list     va;
    va_start(va, pszMsgFmt);
    rc = RTTraceBufAddMsgV(hTraceBuf, pszMsgFmt, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTTraceBufAddMsgV(RTTRACEBUF hTraceBuf, const char *pszMsgFmt, va_list va)
{
    RTTRACEBUF_ADD_PROLOGUE(hTraceBuf);
    RTStrPrintfV(pszBuf, cchBuf, pszMsgFmt, va);
    RTTRACEBUF_ADD_EPILOGUE();
}


RTDECL(int) RTTraceBufAddPos(RTTRACEBUF hTraceBuf, RT_SRC_POS_DECL)
{
    RTTRACEBUF_ADD_PROLOGUE(hTraceBuf);
    RTTRACEBUF_ADD_STORE_SRC_POS();
    RTTRACEBUF_ADD_EPILOGUE();
}


RTDECL(int) RTTraceBufAddPosMsg(RTTRACEBUF hTraceBuf, RT_SRC_POS_DECL, const char *pszMsg)
{
    RTTRACEBUF_ADD_PROLOGUE(hTraceBuf);
    RTTRACEBUF_ADD_STORE_SRC_POS();
    RTStrCopy(pszBuf, cchBuf, pszMsg);
    RTTRACEBUF_ADD_EPILOGUE();
}


RTDECL(int) RTTraceBufAddPosMsgEx(RTTRACEBUF hTraceBuf, RT_SRC_POS_DECL, const char *pszMsg, size_t cbMaxMsg)
{
    RTTRACEBUF_ADD_PROLOGUE(hTraceBuf);
    RTTRACEBUF_ADD_STORE_SRC_POS();
    RTStrCopyEx(pszBuf, cchBuf, pszMsg, cbMaxMsg);
    RTTRACEBUF_ADD_EPILOGUE();
}


RTDECL(int) RTTraceBufAddPosMsgF(RTTRACEBUF hTraceBuf, RT_SRC_POS_DECL, const char *pszMsgFmt, ...)
{
    int         rc;
    va_list     va;
    va_start(va, pszMsgFmt);
    rc = RTTraceBufAddPosMsgV(hTraceBuf, RT_SRC_POS_ARGS, pszMsgFmt, va);
    va_end(va);
    return rc;
}


RTDECL(int) RTTraceBufAddPosMsgV(RTTRACEBUF hTraceBuf, RT_SRC_POS_DECL, const char *pszMsgFmt, va_list va)
{
    RTTRACEBUF_ADD_PROLOGUE(hTraceBuf);
    RTTRACEBUF_ADD_STORE_SRC_POS();
    RTStrPrintfV(pszBuf, cchBuf, pszMsgFmt, va);
    RTTRACEBUF_ADD_EPILOGUE();
}


RTDECL(int) RTTraceBufEnumEntries(RTTRACEBUF hTraceBuf, PFNRTTRACEBUFCALLBACK pfnCallback, void *pvUser)
{
    int                 rc = VINF_SUCCESS;
    uint32_t            iBase;
    uint32_t            cLeft;
    PCRTTRACEBUFINT     pThis;
    RTTRACEBUF_RESOLVE_VALIDATE_RETAIN_RETURN(hTraceBuf, pThis);

    iBase = ASMAtomicReadU32(&RTTRACEBUF_TO_VOLATILE(pThis)->iEntry);
    cLeft = pThis->cEntries;
    while (cLeft--)
    {
        PRTTRACEBUFENTRY pEntry;

        iBase %= pThis->cEntries;
        pEntry = RTTRACEBUF_TO_ENTRY(pThis, iBase);
        if (pEntry->NanoTS)
        {
            rc = pfnCallback((RTTRACEBUF)pThis, cLeft, pEntry->NanoTS, pEntry->idCpu, pEntry->szMsg, pvUser);
            if (rc != VINF_SUCCESS)
                break;
        }

        /* next */
        iBase += 1;
    }

    RTTRACEBUF_DROP_REFERENCE(pThis);
    return rc;
}


RTDECL(uint32_t) RTTraceBufGetEntrySize(RTTRACEBUF hTraceBuf)
{
    PCRTTRACEBUFINT pThis = hTraceBuf;
    RTTRACEBUF_VALID_RETURN_RC(pThis, 0);
    return pThis->cbEntry;
}


RTDECL(uint32_t) RTTraceBufGetEntryCount(RTTRACEBUF hTraceBuf)
{
    PCRTTRACEBUFINT pThis = hTraceBuf;
    RTTRACEBUF_VALID_RETURN_RC(pThis, 0);
    return pThis->cEntries;
}


RTDECL(bool) RTTraceBufDisable(RTTRACEBUF hTraceBuf)
{
    PCRTTRACEBUFINT pThis = hTraceBuf;
    RTTRACEBUF_VALID_RETURN_RC(pThis, false);
    return !ASMAtomicBitTestAndSet((void volatile *)&pThis->fFlags, RTTRACEBUF_FLAGS_DISABLED_BIT);
}


RTDECL(bool) RTTraceBufEnable(RTTRACEBUF hTraceBuf)
{
    PCRTTRACEBUFINT pThis = hTraceBuf;
    RTTRACEBUF_VALID_RETURN_RC(pThis, false);
    return !ASMAtomicBitTestAndClear((void volatile *)&pThis->fFlags, RTTRACEBUF_FLAGS_DISABLED_BIT);
}


/*
 *
 * Move the following to a separate file, consider using the enumerator.
 *
 */

RTDECL(int) RTTraceBufDumpToLog(RTTRACEBUF hTraceBuf)
{
    uint32_t            iBase;
    uint32_t            cLeft;
    PCRTTRACEBUFINT     pThis;
    RTTRACEBUF_RESOLVE_VALIDATE_RETAIN_RETURN(hTraceBuf, pThis);

    iBase = ASMAtomicReadU32(&RTTRACEBUF_TO_VOLATILE(pThis)->iEntry);
    cLeft = pThis->cEntries;
    while (cLeft--)
    {
        PRTTRACEBUFENTRY pEntry;

        iBase %= pThis->cEntries;
        pEntry = RTTRACEBUF_TO_ENTRY(pThis, iBase);
        if (pEntry->NanoTS)
            RTLogPrintf("%04u/%'llu/%02x: %s\n", cLeft, pEntry->NanoTS, pEntry->idCpu, pEntry->szMsg);

        /* next */
        iBase += 1;
    }

    RTTRACEBUF_DROP_REFERENCE(pThis);
    return VINF_SUCCESS;
}


RTDECL(int) RTTraceBufDumpToAssert(RTTRACEBUF hTraceBuf)
{
    uint32_t            iBase;
    uint32_t            cLeft;
    PCRTTRACEBUFINT     pThis;
    RTTRACEBUF_RESOLVE_VALIDATE_RETAIN_RETURN(hTraceBuf, pThis);

    iBase = ASMAtomicReadU32(&RTTRACEBUF_TO_VOLATILE(pThis)->iEntry);
    cLeft = pThis->cEntries;
    while (cLeft--)
    {
        PRTTRACEBUFENTRY pEntry;

        iBase %= pThis->cEntries;
        pEntry = RTTRACEBUF_TO_ENTRY(pThis, iBase);
        if (pEntry->NanoTS)
            RTAssertMsg2AddWeak("%u/%'llu/%02x: %s\n", cLeft, pEntry->NanoTS, pEntry->idCpu, pEntry->szMsg);

        /* next */
        iBase += 1;
    }

    RTTRACEBUF_DROP_REFERENCE(pThis);
    return VINF_SUCCESS;
}

