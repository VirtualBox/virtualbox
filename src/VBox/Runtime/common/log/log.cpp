/* $Id: log.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Runtime VBox - Logger.
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
#include <iprt/log.h>
#include "internal/iprt.h"

#include <iprt/alloc.h>
#include <iprt/crc.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/mp.h>
#ifdef IN_RING3
# ifdef RT_OS_WINDOWS
#  include <iprt/dir.h>
# endif
# include <iprt/env.h>
# include <iprt/file.h>
# include <iprt/lockvalidator.h>
# include <iprt/path.h>
#endif
#include <iprt/time.h>
#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/param.h>

#include <iprt/stdarg.h>
#include <iprt/string.h>
#include <iprt/ctype.h>
#ifdef IN_RING3
# include <iprt/alloca.h>
# ifndef IPRT_NO_CRT
#  include <stdio.h>
# endif
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def RTLOG_RINGBUF_DEFAULT_SIZE
 * The default ring buffer size. */
/** @def RTLOG_RINGBUF_MAX_SIZE
 * The max ring buffer size. */
/** @def RTLOG_RINGBUF_MIN_SIZE
 * The min ring buffer size. */
#ifdef IN_RING0
# define RTLOG_RINGBUF_DEFAULT_SIZE     _64K
# define RTLOG_RINGBUF_MAX_SIZE         _4M
# define RTLOG_RINGBUF_MIN_SIZE         _1K
#elif defined(IN_RING3) || defined(DOXYGEN_RUNNING)
# define RTLOG_RINGBUF_DEFAULT_SIZE     _512K
# define RTLOG_RINGBUF_MAX_SIZE         _1G
# define RTLOG_RINGBUF_MIN_SIZE         _4K
#endif
/** The start of ring buffer eye catcher (16 bytes). */
#define RTLOG_RINGBUF_EYE_CATCHER           "START RING BUF\0"
AssertCompile(sizeof(RTLOG_RINGBUF_EYE_CATCHER) == 16);
/** The end of ring buffer eye catcher (16 bytes).  This also ensures that the ring buffer
 * forms are properly terminated C string (leading zero chars).  */
#define RTLOG_RINGBUF_EYE_CATCHER_END    "\0\0\0END RING BUF"
AssertCompile(sizeof(RTLOG_RINGBUF_EYE_CATCHER_END) == 16);

/** The default buffer size. */
#ifdef IN_RING0
# define RTLOG_BUFFER_DEFAULT_SIZE      _16K
#else
# define RTLOG_BUFFER_DEFAULT_SIZE      _128K
#endif
/** Buffer alignment used RTLogCreateExV.   */
#define RTLOG_BUFFER_ALIGN              64


/** Resolved a_pLoggerInt to the default logger if NULL, returning @a a_rcRet if
 * no default logger could be created. */
#define RTLOG_RESOLVE_DEFAULT_RET(a_pLoggerInt, a_rcRet) do {\
        if (a_pLoggerInt) { /*maybe*/ } \
        else \
        { \
            a_pLoggerInt = (PRTLOGGERINTERNAL)rtLogDefaultInstanceCommon(); \
            if (a_pLoggerInt) { /*maybe*/ } \
            else \
                return (a_rcRet); \
        } \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Internal logger data.
 *
 * @remarks Don't make casual changes to this structure.
 */
typedef struct RTLOGGERINTERNAL
{
    /** The public logger core. */
    RTLOGGER                Core;

    /** The structure revision (RTLOGGERINTERNAL_REV). */
    uint32_t                uRevision;
    /** The size of the internal logger structure. */
    uint32_t                cbSelf;

    /** Logger instance flags - RTLOGFLAGS. */
    uint64_t                fFlags;
    /** Destination flags - RTLOGDEST. */
    uint32_t                fDestFlags;

    /** Number of buffer descriptors. */
    uint8_t                 cBufDescs;
    /** Index of the current buffer descriptor. */
    uint8_t                 idxBufDesc;
    /** Pointer to buffer the descriptors. */
    PRTLOGBUFFERDESC        paBufDescs;
    /** Pointer to the current buffer the descriptor. */
    PRTLOGBUFFERDESC        pBufDesc;

    /** Spinning mutex semaphore.  Can be NIL. */
    RTSEMSPINMUTEX          hSpinMtx;
    /** Pointer to the flush function. */
    PFNRTLOGFLUSH           pfnFlush;

    /** Custom prefix callback. */
    PFNRTLOGPREFIX          pfnPrefix;
    /** Prefix callback argument. */
    void                   *pvPrefixUserArg;
    /** This is set if a prefix is pending. */
    bool                    fPendingPrefix;
    /** Alignment padding. */
    bool                    afPadding1[2];
    /** Set if fully created.  Used to avoid confusing in a few functions used to
     * parse logger settings from environment variables. */
    bool                    fCreated;

    /** The max number of groups that there is room for in afGroups and papszGroups.
     * Used by RTLogCopyGroupAndFlags(). */
    uint32_t                cMaxGroups;
    /** Pointer to the group name array.
     * (The data is readonly and provided by the user.) */
    const char * const     *papszGroups;

    /** The number of log entries per group.  NULL if
     * RTLOGFLAGS_RESTRICT_GROUPS is not specified. */
    uint32_t               *pacEntriesPerGroup;
    /** The max number of entries per group. */
    uint32_t                cMaxEntriesPerGroup;

    /** @name Ring buffer logging
     * The ring buffer records the last cbRingBuf - 1 of log output.  The
     * other configured log destinations are not touched until someone calls
     * RTLogFlush(), when the ring buffer content is written to them all.
     *
     * The aim here is a fast logging destination, that avoids wasting storage
     * space saving disk space when dealing with huge log volumes where the
     * interesting bits usually are found near the end of the log.  This is
     * typically the case for scenarios that crashes or hits assertions.
     *
     * RTLogFlush() is called implicitly when hitting an assertion.  While on a
     * crash the most debuggers are able to make calls these days, it's usually
     * possible to view the ring buffer memory.
     *
     * @{ */
    /** Ring buffer size (including both eye catchers). */
    uint32_t                cbRingBuf;
    /** Number of bytes passing thru the ring buffer since last RTLogFlush call.
     * (This is used to avoid writing out the same bytes twice.) */
    uint64_t volatile       cbRingBufUnflushed;
    /** Ring buffer pointer (points at RTLOG_RINGBUF_EYE_CATCHER). */
    char                   *pszRingBuf;
    /** Current ring buffer position (where to write the next char). */
    char * volatile         pchRingBufCur;
    /** @} */

    /** Program time base for ring-0 (copy of g_u64ProgramStartNanoTS). */
    uint64_t                nsR0ProgramStart;
    /** Thread name for use in ring-0 with RTLOGFLAGS_PREFIX_THREAD. */
    char                    szR0ThreadName[16];

#ifdef IN_RING3
    /** @name File logging bits for the logger.
     * @{ */
    /** Pointer to the function called when starting logging, and when
     * ending or starting a new log file as part of history rotation.
     * This can be NULL. */
    PFNRTLOGPHASE           pfnPhase;
    /** Pointer to the output interface used. */
    PCRTLOGOUTPUTIF         pOutputIf;
    /** Opaque user data passed to the callbacks in the output interface. */
    void                   *pvOutputIfUser;
    /** Opaque directory context.
     * This is kept open while we have an open log file. */
    void                   *pvDirCtx;
# if ARCH_BITS == 32
    void                   *pvPadding32;
# endif

    /** Handle to log file (if open) - only used by the default output interface to avoid additional layers of indirection. */
    RTFILE                  hFile;
    /** Log file history settings: maximum amount of data to put in a file. */
    uint64_t                cbHistoryFileMax;
    /** Log file history settings: current amount of data in a file. */
    uint64_t                cbHistoryFileWritten;
    /** Log file history settings: maximum time to use a file (in seconds). */
    uint32_t                cSecsHistoryTimeSlot;
    /** Log file history settings: in what time slot was the file created. */
    uint32_t                uHistoryTimeSlotStart;
    /** Log file history settings: number of older files to keep.
     * 0 means no history. */
    uint32_t                cHistory;
    /** Pointer to filename. */
    char                    szFilename[RTPATH_MAX];
    /** Flag whether the log file was opened successfully. */
    bool                    fLogOpened;
    /** @} */
#endif /* IN_RING3 */

    /** Number of groups in the afGroups and papszGroups members. */
    uint32_t                cGroups;
    /** Group flags array - RTLOGGRPFLAGS.
     * This member have variable length and may extend way beyond
     * the declared size of 1 entry. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    uint32_t                afGroups[RT_FLEXIBLE_ARRAY];
} RTLOGGERINTERNAL;

/** The revision of the internal logger structure. */
# define RTLOGGERINTERNAL_REV    UINT32_C(14)

AssertCompileMemberAlignment(RTLOGGERINTERNAL, cbRingBufUnflushed, sizeof(uint64_t));
#ifdef IN_RING3
AssertCompileMemberAlignment(RTLOGGERINTERNAL, hFile, sizeof(void *));
AssertCompileMemberAlignment(RTLOGGERINTERNAL, cbHistoryFileMax, sizeof(uint64_t));
#endif

/** Pointer to internal logger bits. */
typedef struct RTLOGGERINTERNAL *PRTLOGGERINTERNAL;


/**
 * Arguments passed to the output function.
 */
typedef struct RTLOGOUTPUTPREFIXEDARGS
{
    /** The logger instance. */
    PRTLOGGERINTERNAL       pLoggerInt;
    /** The flags. (used for prefixing.) */
    unsigned                fFlags;
    /** The group. (used for prefixing.) */
    unsigned                iGroup;
    /** Used by RTLogBulkNestedWrite.   */
    const char             *pszInfix;
} RTLOGOUTPUTPREFIXEDARGS, *PRTLOGOUTPUTPREFIXEDARGS;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static unsigned rtlogGroupFlags(const char *psz);
#ifdef IN_RING3
static int  rtR3LogOpenFileDestination(PRTLOGGERINTERNAL pLoggerInt, PRTERRINFO pErrInfo);
#endif
static void rtLogRingBufFlush(PRTLOGGERINTERNAL pLoggerInt);
static void rtlogFlush(PRTLOGGERINTERNAL pLoggerInt, bool fNeedSpace);
#ifdef IN_RING3
static FNRTLOGPHASEMSG rtlogPhaseMsgLocked;
static FNRTLOGPHASEMSG rtlogPhaseMsgNormal;
#endif
static DECLCALLBACK(size_t) rtLogOutputPrefixed(void *pv, const char *pachChars, size_t cbChars);
static void rtlogLoggerExFLocked(PRTLOGGERINTERNAL pLoggerInt, unsigned fFlags, unsigned iGroup, const char *pszFormat, ...);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Default logger instance. */
static PRTLOGGER                    g_pLogger;
/** Default release logger instance. */
static PRTLOGGER                    g_pRelLogger;
#ifdef IN_RING3
/** The RTThreadGetWriteLockCount() change caused by the logger mutex semaphore. */
static uint32_t volatile            g_cLoggerLockCount;
#endif

#ifdef IN_RING0
/** Number of per-thread loggers. */
static int32_t volatile             g_cPerThreadLoggers;
/** Per-thread loggers.
 * This is just a quick TLS hack suitable for debug logging only.
 * If we run out of entries, just unload and reload the driver. */
static struct RTLOGGERPERTHREAD
{
    /** The thread. */
    RTNATIVETHREAD volatile NativeThread;
    /** The (process / session) key. */
    uintptr_t volatile      uKey;
    /** The logger instance.*/
    PRTLOGGER volatile      pLogger;
} g_aPerThreadLoggers[8] =
{
    { NIL_RTNATIVETHREAD, 0, 0},
    { NIL_RTNATIVETHREAD, 0, 0},
    { NIL_RTNATIVETHREAD, 0, 0},
    { NIL_RTNATIVETHREAD, 0, 0},
    { NIL_RTNATIVETHREAD, 0, 0},
    { NIL_RTNATIVETHREAD, 0, 0},
    { NIL_RTNATIVETHREAD, 0, 0},
    { NIL_RTNATIVETHREAD, 0, 0}
};
#endif /* IN_RING0 */

/**
 * Logger flags instructions.
 */
static struct
{
    const char *pszInstr;               /**< The name  */
    size_t      cchInstr;               /**< The size of the name. */
    uint64_t    fFlag;                  /**< The flag value. */
    bool        fInverted;              /**< Inverse meaning? */
    uint32_t    fFixedDest;             /**< RTLOGDEST_FIXED_XXX flags blocking this. */
} const g_aLogFlags[] =
{
    { "disabled",     sizeof("disabled"    ) - 1,   RTLOGFLAGS_DISABLED,            false, 0 },
    { "enabled",      sizeof("enabled"     ) - 1,   RTLOGFLAGS_DISABLED,            true,  0 },
    { "buffered",     sizeof("buffered"    ) - 1,   RTLOGFLAGS_BUFFERED,            false, 0 },
    { "unbuffered",   sizeof("unbuffered"  ) - 1,   RTLOGFLAGS_BUFFERED,            true,  0 },
    { "usecrlf",      sizeof("usecrlf"     ) - 1,   RTLOGFLAGS_USECRLF,             false, 0 },
    { "uself",        sizeof("uself"       ) - 1,   RTLOGFLAGS_USECRLF,             true,  0 },
    { "append",       sizeof("append"      ) - 1,   RTLOGFLAGS_APPEND,              false, RTLOGDEST_FIXED_FILE },
    { "overwrite",    sizeof("overwrite"   ) - 1,   RTLOGFLAGS_APPEND,              true,  RTLOGDEST_FIXED_FILE },
    { "rel",          sizeof("rel"         ) - 1,   RTLOGFLAGS_REL_TS,              false, 0 },
    { "abs",          sizeof("abs"         ) - 1,   RTLOGFLAGS_REL_TS,              true,  0 },
    { "dec",          sizeof("dec"         ) - 1,   RTLOGFLAGS_DECIMAL_TS,          false, 0 },
    { "hex",          sizeof("hex"         ) - 1,   RTLOGFLAGS_DECIMAL_TS,          true,  0 },
    { "writethru",    sizeof("writethru"   ) - 1,   RTLOGFLAGS_WRITE_THROUGH,       false, 0 },
    { "writethrough", sizeof("writethrough") - 1,   RTLOGFLAGS_WRITE_THROUGH,       false, 0 },
    { "flush",        sizeof("flush"       ) - 1,   RTLOGFLAGS_FLUSH,               false, 0 },
    { "lockcnts",     sizeof("lockcnts"    ) - 1,   RTLOGFLAGS_PREFIX_LOCK_COUNTS,  false, 0 },
    { "cpuid",        sizeof("cpuid"       ) - 1,   RTLOGFLAGS_PREFIX_CPUID,        false, 0 },
    { "pid",          sizeof("pid"         ) - 1,   RTLOGFLAGS_PREFIX_PID,          false, 0 },
    { "flagno",       sizeof("flagno"      ) - 1,   RTLOGFLAGS_PREFIX_FLAG_NO,      false, 0 },
    { "flag",         sizeof("flag"        ) - 1,   RTLOGFLAGS_PREFIX_FLAG,         false, 0 },
    { "groupno",      sizeof("groupno"     ) - 1,   RTLOGFLAGS_PREFIX_GROUP_NO,     false, 0 },
    { "group",        sizeof("group"       ) - 1,   RTLOGFLAGS_PREFIX_GROUP,        false, 0 },
    { "tid",          sizeof("tid"         ) - 1,   RTLOGFLAGS_PREFIX_TID,          false, 0 },
    { "thread",       sizeof("thread"      ) - 1,   RTLOGFLAGS_PREFIX_THREAD,       false, 0 },
    { "custom",       sizeof("custom"      ) - 1,   RTLOGFLAGS_PREFIX_CUSTOM,       false, 0 },
    { "timeprog",     sizeof("timeprog"    ) - 1,   RTLOGFLAGS_PREFIX_TIME_PROG,    false, 0 },
    { "time",         sizeof("time"        ) - 1,   RTLOGFLAGS_PREFIX_TIME,         false, 0 },
    { "msprog",       sizeof("msprog"      ) - 1,   RTLOGFLAGS_PREFIX_MS_PROG,      false, 0 },
    { "tsc",          sizeof("tsc"         ) - 1,   RTLOGFLAGS_PREFIX_TSC,          false, 0 }, /* before ts! */
    { "ts",           sizeof("ts"          ) - 1,   RTLOGFLAGS_PREFIX_TS,           false, 0 },
    /* We intentionally omit RTLOGFLAGS_RESTRICT_GROUPS. */
};

/**
 * Logger destination instructions.
 */
static struct
{
    const char *pszInstr;               /**< The name. */
    size_t      cchInstr;               /**< The size of the name. */
    uint32_t    fFlag;                  /**< The corresponding destination flag. */
} const g_aLogDst[] =
{
    { RT_STR_TUPLE("file"),         RTLOGDEST_FILE },       /* Must be 1st! */
    { RT_STR_TUPLE("dir"),          RTLOGDEST_FILE },       /* Must be 2nd! */
    { RT_STR_TUPLE("history"),      0 },                    /* Must be 3rd! */
    { RT_STR_TUPLE("histsize"),     0 },                    /* Must be 4th! */
    { RT_STR_TUPLE("histtime"),     0 },                    /* Must be 5th! */
    { RT_STR_TUPLE("ringbuf"),      RTLOGDEST_RINGBUF },    /* Must be 6th! */
    { RT_STR_TUPLE("stdout"),       RTLOGDEST_STDOUT },
    { RT_STR_TUPLE("stderr"),       RTLOGDEST_STDERR },
    { RT_STR_TUPLE("debugger"),     RTLOGDEST_DEBUGGER },
    { RT_STR_TUPLE("com"),          RTLOGDEST_COM },
    { RT_STR_TUPLE("nodeny"),       RTLOGDEST_F_NO_DENY },
    { RT_STR_TUPLE("vmmrel"),       RTLOGDEST_VMM_REL },    /* before vmm */
    { RT_STR_TUPLE("vmm"),          RTLOGDEST_VMM },
    { RT_STR_TUPLE("user"),         RTLOGDEST_USER },
    /* The RTLOGDEST_FIXED_XXX flags are omitted on purpose. */
};

#ifdef IN_RING3
/** Log rotation backoff table - millisecond sleep intervals.
 * Important on Windows host, especially for VBoxSVC release logging.  Only a
 * medium term solution, until a proper fix for log file handling is available.
 * 10 seconds total.
 */
static const uint32_t g_acMsLogBackoff[] =
{ 10, 10, 10, 20, 50, 100, 200, 200, 200, 200, 500, 500, 500, 500, 1000, 1000, 1000, 1000, 1000, 1000, 1000 };
#endif


/**
 * Locks the logger instance.
 *
 * @returns See RTSemSpinMutexRequest().
 * @param   pLoggerInt  The logger instance.
 */
DECLINLINE(int) rtlogLock(PRTLOGGERINTERNAL pLoggerInt)
{
    AssertMsgReturn(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC, ("%#x != %#x\n", pLoggerInt->Core.u32Magic, RTLOGGER_MAGIC),
                    VERR_INVALID_MAGIC);
    AssertMsgReturn(pLoggerInt->uRevision == RTLOGGERINTERNAL_REV, ("%#x != %#x\n", pLoggerInt->uRevision, RTLOGGERINTERNAL_REV),
                    VERR_LOG_REVISION_MISMATCH);
    AssertMsgReturn(pLoggerInt->cbSelf == sizeof(*pLoggerInt), ("%#x != %#x\n", pLoggerInt->cbSelf, sizeof(*pLoggerInt)),
                    VERR_LOG_REVISION_MISMATCH);
    if (pLoggerInt->hSpinMtx != NIL_RTSEMSPINMUTEX)
    {
        int rc = RTSemSpinMutexRequest(pLoggerInt->hSpinMtx);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Unlocks the logger instance.
 * @param   pLoggerInt  The logger instance.
 */
DECLINLINE(void) rtlogUnlock(PRTLOGGERINTERNAL pLoggerInt)
{
    if (pLoggerInt->hSpinMtx != NIL_RTSEMSPINMUTEX)
        RTSemSpinMutexRelease(pLoggerInt->hSpinMtx);
    return;
}


/*********************************************************************************************************************************
*   Logger Instance Management.                                                                                                  *
*********************************************************************************************************************************/

/**
 * Common worker for RTLogDefaultInstance and RTLogDefaultInstanceEx.
 */
DECL_NO_INLINE(static, PRTLOGGER) rtLogDefaultInstanceCreateNew(void)
{
    PRTLOGGER pRet = NULL;

    /*
     * It's soo easy to end up in a infinite recursion here when enabling 'all'
     * the logging groups. So, only allow one thread to instantiate the default
     * logger, muting other attempts at logging while it's being created.
     */
    static volatile bool s_fCreating = false;
    if (ASMAtomicCmpXchgBool(&s_fCreating, true, false))
    {
        pRet = RTLogDefaultInit();
        if (pRet)
        {
            bool fRc = ASMAtomicCmpXchgPtr(&g_pLogger, pRet, NULL);
            if (!fRc)
            {
                RTLogDestroy(pRet);
                pRet = g_pLogger;
            }
        }
        ASMAtomicWriteBool(&s_fCreating, true);
    }
    return pRet;
}


/**
 * Common worker for RTLogDefaultInstance and RTLogDefaultInstanceEx.
 */
DECL_FORCE_INLINE(PRTLOGGER) rtLogDefaultInstanceCommon(void)
{
    PRTLOGGER pRet;

#ifdef IN_RING0
    /*
     * Check per thread loggers first.
     */
    if (g_cPerThreadLoggers)
    {
        const RTNATIVETHREAD Self = RTThreadNativeSelf();
        int32_t i = RT_ELEMENTS(g_aPerThreadLoggers);
        while (i-- > 0)
            if (g_aPerThreadLoggers[i].NativeThread == Self)
                return g_aPerThreadLoggers[i].pLogger;
    }
#endif /* IN_RING0 */

    /*
     * If no per thread logger, use the default one.
     */
    pRet = g_pLogger;
    if (RT_LIKELY(pRet))
    { /* likely */ }
    else
        pRet = rtLogDefaultInstanceCreateNew();
    return pRet;
}


RTDECL(PRTLOGGER)   RTLogDefaultInstance(void)
{
    return rtLogDefaultInstanceCommon();
}
RT_EXPORT_SYMBOL(RTLogDefaultInstance);


/**
 * Worker for RTLogDefaultInstanceEx, RTLogGetDefaultInstanceEx,
 * RTLogRelGetDefaultInstanceEx and RTLogCheckGroupFlags.
 */
DECL_FORCE_INLINE(PRTLOGGERINTERNAL) rtLogCheckGroupFlagsWorker(PRTLOGGERINTERNAL pLoggerInt, uint32_t fFlagsAndGroup)
{
    if (pLoggerInt->fFlags & RTLOGFLAGS_DISABLED)
        pLoggerInt = NULL;
    else
    {
        uint32_t const fFlags = RT_LO_U16(fFlagsAndGroup);
        uint16_t const iGroup = RT_HI_U16(fFlagsAndGroup);
        if (   iGroup != UINT16_MAX
            && (   (pLoggerInt->afGroups[iGroup < pLoggerInt->cGroups ? iGroup : 0] & (fFlags | RTLOGGRPFLAGS_ENABLED))
                != (fFlags | RTLOGGRPFLAGS_ENABLED)))
            pLoggerInt = NULL;
    }
    return pLoggerInt;
}


RTDECL(PRTLOGGER)   RTLogDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)rtLogDefaultInstanceCommon();
    if (pLoggerInt)
        pLoggerInt = rtLogCheckGroupFlagsWorker(pLoggerInt, fFlagsAndGroup);
    AssertCompileMemberOffset(RTLOGGERINTERNAL, Core, 0);
    return (PRTLOGGER)pLoggerInt;
}
RT_EXPORT_SYMBOL(RTLogDefaultInstanceEx);


/**
 * Common worker for RTLogGetDefaultInstance and RTLogGetDefaultInstanceEx.
 */
DECL_FORCE_INLINE(PRTLOGGER) rtLogGetDefaultInstanceCommon(void)
{
#ifdef IN_RING0
    /*
     * Check per thread loggers first.
     */
    if (g_cPerThreadLoggers)
    {
        const RTNATIVETHREAD Self = RTThreadNativeSelf();
        int32_t i = RT_ELEMENTS(g_aPerThreadLoggers);
        while (i-- > 0)
            if (g_aPerThreadLoggers[i].NativeThread == Self)
                return g_aPerThreadLoggers[i].pLogger;
    }
#endif /* IN_RING0 */

    return g_pLogger;
}


RTDECL(PRTLOGGER) RTLogGetDefaultInstance(void)
{
    return rtLogGetDefaultInstanceCommon();
}
RT_EXPORT_SYMBOL(RTLogGetDefaultInstance);


RTDECL(PRTLOGGER) RTLogGetDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)rtLogGetDefaultInstanceCommon();
    if (pLoggerInt)
        pLoggerInt = rtLogCheckGroupFlagsWorker(pLoggerInt, fFlagsAndGroup);
    AssertCompileMemberOffset(RTLOGGERINTERNAL, Core, 0);
    return (PRTLOGGER)pLoggerInt;
}
RT_EXPORT_SYMBOL(RTLogGetDefaultInstanceEx);


RTDECL(PRTLOGGER) RTLogSetDefaultInstance(PRTLOGGER pLogger)
{
#if defined(IN_RING3) && (defined(IN_RT_STATIC) || defined(IPRT_NO_CRT))
    /* Set the pointers for emulating "weak symbols" the first time we're
       called with something useful: */
    if (pLogger != NULL && g_pfnRTLogGetDefaultInstanceEx == NULL)
    {
        g_pfnRTLogGetDefaultInstance   = RTLogGetDefaultInstance;
        g_pfnRTLogGetDefaultInstanceEx = RTLogGetDefaultInstanceEx;
    }
#endif
    return ASMAtomicXchgPtrT(&g_pLogger, pLogger, PRTLOGGER);
}
RT_EXPORT_SYMBOL(RTLogSetDefaultInstance);


#ifdef IN_RING0
/**
 * Changes the default logger instance for the current thread.
 *
 * @returns IPRT status code.
 * @param   pLogger     The logger instance. Pass NULL for deregistration.
 * @param   uKey        Associated key for cleanup purposes. If pLogger is NULL,
 *                      all instances with this key will be deregistered. So in
 *                      order to only deregister the instance associated with the
 *                      current thread use 0.
 */
RTR0DECL(int) RTLogSetDefaultInstanceThread(PRTLOGGER pLogger, uintptr_t uKey)
{
    int             rc;
    RTNATIVETHREAD  Self = RTThreadNativeSelf();
    if (pLogger)
    {
        int32_t i;
        unsigned j;

        AssertReturn(pLogger->u32Magic == RTLOGGER_MAGIC, VERR_INVALID_MAGIC);

        /*
         * Iterate the table to see if there is already an entry for this thread.
         */
        i = RT_ELEMENTS(g_aPerThreadLoggers);
        while (i-- > 0)
            if (g_aPerThreadLoggers[i].NativeThread == Self)
            {
                ASMAtomicWritePtr((void * volatile *)&g_aPerThreadLoggers[i].uKey, (void *)uKey);
                g_aPerThreadLoggers[i].pLogger = pLogger;
                return VINF_SUCCESS;
            }

        /*
         * Allocate a new table entry.
         */
        i = ASMAtomicIncS32(&g_cPerThreadLoggers);
        if (i > (int32_t)RT_ELEMENTS(g_aPerThreadLoggers))
        {
            ASMAtomicDecS32(&g_cPerThreadLoggers);
            return VERR_BUFFER_OVERFLOW; /* horrible error code! */
        }

        for (j = 0; j < 10; j++)
        {
            i = RT_ELEMENTS(g_aPerThreadLoggers);
            while (i-- > 0)
            {
                AssertCompile(sizeof(RTNATIVETHREAD) == sizeof(void*));
                if (    g_aPerThreadLoggers[i].NativeThread == NIL_RTNATIVETHREAD
                    &&  ASMAtomicCmpXchgPtr((void * volatile *)&g_aPerThreadLoggers[i].NativeThread, (void *)Self, (void *)NIL_RTNATIVETHREAD))
                {
                    ASMAtomicWritePtr((void * volatile *)&g_aPerThreadLoggers[i].uKey, (void *)uKey);
                    ASMAtomicWritePtr(&g_aPerThreadLoggers[i].pLogger, pLogger);
                    return VINF_SUCCESS;
                }
            }
        }

        ASMAtomicDecS32(&g_cPerThreadLoggers);
        rc = VERR_INTERNAL_ERROR;
    }
    else
    {
        /*
         * Search the array for the current thread.
         */
        int32_t i = RT_ELEMENTS(g_aPerThreadLoggers);
        while (i-- > 0)
            if (    g_aPerThreadLoggers[i].NativeThread == Self
                ||  g_aPerThreadLoggers[i].uKey == uKey)
            {
                ASMAtomicWriteNullPtr((void * volatile *)&g_aPerThreadLoggers[i].uKey);
                ASMAtomicWriteNullPtr(&g_aPerThreadLoggers[i].pLogger);
                ASMAtomicWriteHandle(&g_aPerThreadLoggers[i].NativeThread, NIL_RTNATIVETHREAD);
                ASMAtomicDecS32(&g_cPerThreadLoggers);
            }

        rc = VINF_SUCCESS;
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLogSetDefaultInstanceThread);
#endif /* IN_RING0 */


RTDECL(PRTLOGGER)   RTLogRelGetDefaultInstance(void)
{
    return g_pRelLogger;
}
RT_EXPORT_SYMBOL(RTLogRelGetDefaultInstance);


RTDECL(PRTLOGGER)   RTLogRelGetDefaultInstanceEx(uint32_t fFlagsAndGroup)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)g_pRelLogger;
    if (pLoggerInt)
        pLoggerInt = rtLogCheckGroupFlagsWorker(pLoggerInt, fFlagsAndGroup);
    return (PRTLOGGER)pLoggerInt;
}
RT_EXPORT_SYMBOL(RTLogRelGetDefaultInstanceEx);


RTDECL(PRTLOGGER) RTLogRelSetDefaultInstance(PRTLOGGER pLogger)
{
#if defined(IN_RING3) && (defined(IN_RT_STATIC) || defined(IPRT_NO_CRT))
    /* Set the pointers for emulating "weak symbols" the first time we're
       called with something useful: */
    if (pLogger != NULL && g_pfnRTLogRelGetDefaultInstanceEx == NULL)
    {
        g_pfnRTLogRelGetDefaultInstance   = RTLogRelGetDefaultInstance;
        g_pfnRTLogRelGetDefaultInstanceEx = RTLogRelGetDefaultInstanceEx;
    }
#endif
    return ASMAtomicXchgPtrT(&g_pRelLogger, pLogger, PRTLOGGER);
}
RT_EXPORT_SYMBOL(RTLogRelSetDefaultInstance);


RTDECL(PRTLOGGER)   RTLogCheckGroupFlags(PRTLOGGER pLogger, uint32_t fFlagsAndGroup)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    if (pLoggerInt)
        pLoggerInt = rtLogCheckGroupFlagsWorker(pLoggerInt, fFlagsAndGroup);
    return (PRTLOGGER)pLoggerInt;
}
RT_EXPORT_SYMBOL(RTLogCheckGroupFlags);


/*********************************************************************************************************************************
*   Default file I/O interface                                                                                                   *
*********************************************************************************************************************************/

#ifdef IN_RING3
# ifdef RT_OS_WINDOWS
#  define RTLOG_DIR_CTX_IS_PARENT_DIR
# endif

/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnDirCtxOpen}
 */
static DECLCALLBACK(int) rtLogOutputIfDefDirCtxOpen(PCRTLOGOUTPUTIF pIf, void *pvUser, const char *pszFilename, void **ppvDirCtx)
{
    RT_NOREF(pIf, pvUser);
# ifdef RTLOG_DIR_CTX_IS_PARENT_DIR
    /* Open the parent directory to make sure it is a real directory and not
       object directory placed there by someone with evil intent. @bugref{10632} */
    RTDIR hDir = NIL_RTDIR;
    int rc = RTDirOpenFiltered(&hDir, pszFilename, RTDIRFILTER_WINNT, 0);
    if (RT_FAILURE(rc))
        return rc;
    AssertCompile(sizeof(*ppvDirCtx) == sizeof(hDir));
    *ppvDirCtx = hDir;
# else
    RT_NOREF(pIf, pvUser, pszFilename);
    *ppvDirCtx = (void *)(intptr_t)42;
# endif
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnDirCtxClose}
 */
static DECLCALLBACK(int) rtLogOutputIfDefDirCtxClose(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx)
{
    RT_NOREF(pIf, pvUser);
# ifdef RTLOG_DIR_CTX_IS_PARENT_DIR
    RTDIR hDir = (RTDIR)pvDirCtx;
    int rc = RTDirClose(hDir);
    AssertRC(rc);
    return rc;
# else
    Assert((intptr_t)pvDirCtx == 42);
    RT_NOREF(pvDirCtx);
    return VINF_SUCCESS;
# endif
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnDelete}
 */
static DECLCALLBACK(int) rtLogOutputIfDefDelete(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx, const char *pszFilename)
{
    RT_NOREF(pIf, pvUser, pvDirCtx);
# ifdef RTLOG_DIR_CTX_IS_PARENT_DIR
    /** @todo use RTDirRelPathUnlink when it adds any improvements. */
# else
    Assert((intptr_t)pvDirCtx == 42);
# endif

    return RTFileDelete(pszFilename);
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnRename}
 */
static DECLCALLBACK(int) rtLogOutputIfDefRename(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx,
                                                const char *pszFilenameOld, const char *pszFilenameNew, uint32_t fFlags)
{
    RT_NOREF(pIf, pvUser, pvDirCtx);
# ifdef RTLOG_DIR_CTX_IS_PARENT_DIR
    /** @todo use RTDirRelPathRename when it adds any improvements. */
# else
    Assert((intptr_t)pvDirCtx == 42);
# endif
    return RTFileRename(pszFilenameOld, pszFilenameNew, fFlags);
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnOpen}
 */
static DECLCALLBACK(int) rtLogOutputIfDefOpen(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx,
                                              const char *pszFilename, uint32_t fFlags)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pvUser;
    RT_NOREF(pIf, pvDirCtx);
# ifdef RTLOG_DIR_CTX_IS_PARENT_DIR
    /** @todo use RTDirRelFileOpen */
# else
    Assert((intptr_t)pvDirCtx == 42);
# endif

    return RTFileOpen(&pLoggerInt->hFile, pszFilename, fFlags);
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnClose}
 */
static DECLCALLBACK(int) rtLogOutputIfDefClose(PCRTLOGOUTPUTIF pIf, void *pvUser)
{
    RT_NOREF(pIf);
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pvUser;

    int rc = VINF_SUCCESS;
    if (pLoggerInt->hFile != NIL_RTFILE)
        rc = RTFileClose(pLoggerInt->hFile);

    pLoggerInt->hFile = NIL_RTFILE;
    return rc;
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnQuerySize}
 */
static DECLCALLBACK(int) rtLogOutputIfDefQuerySize(PCRTLOGOUTPUTIF pIf, void *pvUser, uint64_t *pcbSize)
{
    RT_NOREF(pIf);
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pvUser;

    if (pLoggerInt->hFile != NIL_RTFILE)
        return RTFileQuerySize(pLoggerInt->hFile, pcbSize);

    *pcbSize = 0;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnWrite}
 */
static DECLCALLBACK(int) rtLogOutputIfDefWrite(PCRTLOGOUTPUTIF pIf, void *pvUser, const void *pvBuf,
                                               size_t cbWrite, size_t *pcbWritten)
{
    RT_NOREF(pIf);
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pvUser;

    if (pLoggerInt->hFile != NIL_RTFILE)
        return RTFileWrite(pLoggerInt->hFile, pvBuf, cbWrite, pcbWritten);

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{RTLOGOUTPUTIF,pfnFlush}
 */
static DECLCALLBACK(int) rtLogOutputIfDefFlush(PCRTLOGOUTPUTIF pIf, void *pvUser)
{
    RT_NOREF(pIf);
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pvUser;

    if (pLoggerInt->hFile != NIL_RTFILE)
        return RTFileFlush(pLoggerInt->hFile);

    return VINF_SUCCESS;
}


/**
 * The default file output interface.
 */
static const RTLOGOUTPUTIF g_LogOutputIfDef =
{
    rtLogOutputIfDefDirCtxOpen,
    rtLogOutputIfDefDirCtxClose,
    rtLogOutputIfDefDelete,
    rtLogOutputIfDefRename,
    rtLogOutputIfDefOpen,
    rtLogOutputIfDefClose,
    rtLogOutputIfDefQuerySize,
    rtLogOutputIfDefWrite,
    rtLogOutputIfDefFlush
};

#endif /* IN_RING3 */


/*********************************************************************************************************************************
*   Ring Buffer                                                                                                                  *
*********************************************************************************************************************************/

/**
 * Adjusts the ring buffer.
 *
 * @returns IPRT status code.
 * @param   pLoggerInt  The logger instance.
 * @param   cbNewSize   The new ring buffer size (0 == default).
 * @param   fForce      Whether to do this even if the logger instance hasn't
 *                      really been fully created yet (i.e. during RTLogCreate).
 */
static int rtLogRingBufAdjust(PRTLOGGERINTERNAL pLoggerInt, uint32_t cbNewSize, bool fForce)
{
    /*
     * If this is early logger init, don't do anything.
     */
    if (!pLoggerInt->fCreated && !fForce)
        return VINF_SUCCESS;

    /*
     * Lock the logger and make the necessary changes.
     */
    int rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        if (cbNewSize == 0)
            cbNewSize = RTLOG_RINGBUF_DEFAULT_SIZE;
        if (   pLoggerInt->cbRingBuf != cbNewSize
            || !pLoggerInt->pchRingBufCur)
        {
            uintptr_t offOld = pLoggerInt->pchRingBufCur - pLoggerInt->pszRingBuf;
            if (offOld < sizeof(RTLOG_RINGBUF_EYE_CATCHER))
                offOld = sizeof(RTLOG_RINGBUF_EYE_CATCHER);
            else if (offOld >= cbNewSize)
            {
                memmove(pLoggerInt->pszRingBuf, &pLoggerInt->pszRingBuf[offOld - cbNewSize], cbNewSize);
                offOld = sizeof(RTLOG_RINGBUF_EYE_CATCHER);
            }

            void *pvNew = RTMemRealloc(pLoggerInt->pchRingBufCur, cbNewSize);
            if (pvNew)
            {
                pLoggerInt->pszRingBuf    = (char *)pvNew;
                pLoggerInt->pchRingBufCur = (char *)pvNew + offOld;
                pLoggerInt->cbRingBuf     = cbNewSize;
                memcpy(pvNew, RTLOG_RINGBUF_EYE_CATCHER, sizeof(RTLOG_RINGBUF_EYE_CATCHER));
                memcpy((char *)pvNew + cbNewSize - sizeof(RTLOG_RINGBUF_EYE_CATCHER_END),
                       RTLOG_RINGBUF_EYE_CATCHER_END, sizeof(RTLOG_RINGBUF_EYE_CATCHER_END));
                rc = VINF_SUCCESS;
            }
            else
                rc = VERR_NO_MEMORY;
        }
        rtlogUnlock(pLoggerInt);
    }

    return rc;
}


/**
 * Writes text to the ring buffer.
 *
 * @param   pInt                The internal logger data structure.
 * @param   pachText            The text to write.
 * @param   cchText             The number of chars (bytes) to write.
 */
static void rtLogRingBufWrite(PRTLOGGERINTERNAL pInt, const char *pachText, size_t cchText)
{
    /*
     * Get the ring buffer data, adjusting it to only describe the writable
     * part of the buffer.
     */
    char * const pchStart = &pInt->pszRingBuf[sizeof(RTLOG_RINGBUF_EYE_CATCHER)];
    size_t const cchBuf   = pInt->cbRingBuf - sizeof(RTLOG_RINGBUF_EYE_CATCHER) - sizeof(RTLOG_RINGBUF_EYE_CATCHER_END);
    char        *pchCur   = pInt->pchRingBufCur;
    size_t       cchLeft  = pchCur - pchStart;
    if (RT_LIKELY(cchLeft < cchBuf))
        cchLeft = cchBuf - cchLeft;
    else
    {
        /* May happen in ring-0 where a thread or two went ahead without getting the lock. */
        pchCur = pchStart;
        cchLeft = cchBuf;
    }
    Assert(cchBuf < pInt->cbRingBuf);

    if (cchText < cchLeft)
    {
        /*
         * The text fits in the remaining space.
         */
        memcpy(pchCur, pachText, cchText);
        pchCur[cchText] = '\0';
        pInt->pchRingBufCur = &pchCur[cchText];
        pInt->cbRingBufUnflushed += cchText;
    }
    else
    {
        /*
         * The text wraps around.  Taking the simple but inefficient approach
         * to input texts that are longer than the ring buffer since that
         * is unlikely to the be a frequent case.
         */
        /* Fill to the end of the buffer. */
        memcpy(pchCur, pachText, cchLeft);
        pachText += cchLeft;
        cchText  -= cchLeft;
        pInt->cbRingBufUnflushed += cchLeft;
        pInt->pchRingBufCur       = pchStart;

        /* Ring buffer overflows (the plainly inefficient bit). */
        while (cchText >= cchBuf)
        {
            memcpy(pchStart, pachText, cchBuf);
            pachText += cchBuf;
            cchText  -= cchBuf;
            pInt->cbRingBufUnflushed += cchBuf;
        }

        /* The final bit, if any. */
        if (cchText > 0)
        {
            memcpy(pchStart, pachText, cchText);
            pInt->cbRingBufUnflushed += cchText;
        }
        pchStart[cchText] = '\0';
        pInt->pchRingBufCur = &pchStart[cchText];
    }
}


/**
 * Flushes the ring buffer to all the other log destinations.
 *
 * @param   pLoggerInt  The logger instance which ring buffer should be flushed.
 */
static void rtLogRingBufFlush(PRTLOGGERINTERNAL pLoggerInt)
{
    const char  *pszPreamble;
    size_t       cchPreamble;
    const char  *pszFirst;
    size_t       cchFirst;
    const char  *pszSecond;
    size_t       cchSecond;

    /*
     * Get the ring buffer data, adjusting it to only describe the writable
     * part of the buffer.
     */
    uint64_t     cchUnflushed = pLoggerInt->cbRingBufUnflushed;
    char * const pszBuf   = &pLoggerInt->pszRingBuf[sizeof(RTLOG_RINGBUF_EYE_CATCHER)];
    size_t const cchBuf   = pLoggerInt->cbRingBuf - sizeof(RTLOG_RINGBUF_EYE_CATCHER) - sizeof(RTLOG_RINGBUF_EYE_CATCHER_END);
    size_t       offCur   = pLoggerInt->pchRingBufCur - pszBuf;
    size_t       cchAfter;
    if (RT_LIKELY(offCur < cchBuf))
        cchAfter = cchBuf - offCur;
    else /* May happen in ring-0 where a thread or two went ahead without getting the lock. */
    {
        offCur   = 0;
        cchAfter = cchBuf;
    }

    pLoggerInt->cbRingBufUnflushed = 0;

    /*
     * Figure out whether there are one or two segments that needs writing,
     * making the last segment is terminated.  (The first is always
     * terminated because of the eye-catcher at the end of the buffer.)
     */
    if (cchUnflushed == 0)
        return;
    pszBuf[offCur] = '\0';
    if (cchUnflushed >= cchBuf)
    {
        pszFirst    = &pszBuf[offCur + 1];
        cchFirst    = cchAfter ? cchAfter - 1 : 0;
        pszSecond   = pszBuf;
        cchSecond   = offCur;
        pszPreamble =        "\n*FLUSH RING BUF*\n";
        cchPreamble = sizeof("\n*FLUSH RING BUF*\n") - 1;
    }
    else if ((size_t)cchUnflushed <= offCur)
    {
        cchFirst    = (size_t)cchUnflushed;
        pszFirst    = &pszBuf[offCur - cchFirst];
        pszSecond   = "";
        cchSecond   = 0;
        pszPreamble = "";
        cchPreamble = 0;
    }
    else
    {
        cchFirst    = (size_t)cchUnflushed - offCur;
        pszFirst    = &pszBuf[cchBuf - cchFirst];
        pszSecond   = pszBuf;
        cchSecond   = offCur;
        pszPreamble = "";
        cchPreamble = 0;
    }

    /*
     * Write the ring buffer to all other destiations.
     */
    if (pLoggerInt->fDestFlags & RTLOGDEST_USER)
    {
        if (cchPreamble)
            RTLogWriteUser(pszPreamble, cchPreamble);
        if (cchFirst)
            RTLogWriteUser(pszFirst, cchFirst);
        if (cchSecond)
            RTLogWriteUser(pszSecond, cchSecond);
    }

# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
    if (pLoggerInt->fDestFlags & RTLOGDEST_VMM)
    {
        if (cchPreamble)
            RTLogWriteVmm(pszPreamble, cchPreamble, false /*fReleaseLog*/);
        if (cchFirst)
            RTLogWriteVmm(pszFirst, cchFirst, false /*fReleaseLog*/);
        if (cchSecond)
            RTLogWriteVmm(pszSecond, cchSecond, false /*fReleaseLog*/);
    }

    if (pLoggerInt->fDestFlags & RTLOGDEST_VMM_REL)
    {
        if (cchPreamble)
            RTLogWriteVmm(pszPreamble, cchPreamble, true /*fReleaseLog*/);
        if (cchFirst)
            RTLogWriteVmm(pszFirst, cchFirst, true /*fReleaseLog*/);
        if (cchSecond)
            RTLogWriteVmm(pszSecond, cchSecond, true /*fReleaseLog*/);
    }
# endif

    if (pLoggerInt->fDestFlags & RTLOGDEST_DEBUGGER)
    {
        if (cchPreamble)
            RTLogWriteDebugger(pszPreamble, cchPreamble);
        if (cchFirst)
            RTLogWriteDebugger(pszFirst, cchFirst);
        if (cchSecond)
            RTLogWriteDebugger(pszSecond, cchSecond);
    }

# ifdef IN_RING3
    if (pLoggerInt->fDestFlags & RTLOGDEST_FILE)
    {
        if (pLoggerInt->fLogOpened)
        {
            if (cchPreamble)
                pLoggerInt->pOutputIf->pfnWrite(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser,
                                                pszPreamble, cchPreamble, NULL /*pcbWritten*/);
            if (cchFirst)
                pLoggerInt->pOutputIf->pfnWrite(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser,
                                                pszFirst, cchFirst, NULL /*pcbWritten*/);
            if (cchSecond)
                pLoggerInt->pOutputIf->pfnWrite(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser,
                                                pszSecond, cchSecond, NULL /*pcbWritten*/);
            if (pLoggerInt->fFlags & RTLOGFLAGS_FLUSH)
                pLoggerInt->pOutputIf->pfnFlush(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser);
        }
        if (pLoggerInt->cHistory)
            pLoggerInt->cbHistoryFileWritten += cchFirst + cchSecond;
    }
# endif

    if (pLoggerInt->fDestFlags & RTLOGDEST_STDOUT)
    {
        if (cchPreamble)
            RTLogWriteStdOut(pszPreamble, cchPreamble);
        if (cchFirst)
            RTLogWriteStdOut(pszFirst, cchFirst);
        if (cchSecond)
            RTLogWriteStdOut(pszSecond, cchSecond);
    }

    if (pLoggerInt->fDestFlags & RTLOGDEST_STDERR)
    {
        if (cchPreamble)
            RTLogWriteStdErr(pszPreamble, cchPreamble);
        if (cchFirst)
            RTLogWriteStdErr(pszFirst, cchFirst);
        if (cchSecond)
            RTLogWriteStdErr(pszSecond, cchSecond);
    }

# if defined(IN_RING0) && !defined(LOG_NO_COM)
    if (pLoggerInt->fDestFlags & RTLOGDEST_COM)
    {
        if (cchPreamble)
            RTLogWriteCom(pszPreamble, cchPreamble);
        if (cchFirst)
            RTLogWriteCom(pszFirst, cchFirst);
        if (cchSecond)
            RTLogWriteCom(pszSecond, cchSecond);
    }
# endif
}


/*********************************************************************************************************************************
*   Create, Destroy, Setup                                                                                                       *
*********************************************************************************************************************************/

RTDECL(int) RTLogCreateExV(PRTLOGGER *ppLogger, const char *pszEnvVarBase, uint64_t fFlags, const char *pszGroupSettings,
                           uint32_t cGroups, const char * const *papszGroups, uint32_t cMaxEntriesPerGroup,
                           uint32_t cBufDescs, PRTLOGBUFFERDESC paBufDescs, uint32_t fDestFlags,
                           PFNRTLOGPHASE pfnPhase, uint32_t cHistory, uint64_t cbHistoryFileMax, uint32_t cSecsHistoryTimeSlot,
                           PCRTLOGOUTPUTIF pOutputIf, void *pvOutputIfUser,
                           PRTERRINFO pErrInfo, const char *pszFilenameFmt, va_list args)
{
    int                 rc;
    size_t              cbLogger;
    size_t              offBuffers;
    PRTLOGGERINTERNAL   pLoggerInt;
    uint32_t            i;

    /*
     * Validate input.
     */
    AssertPtrReturn(ppLogger, VERR_INVALID_POINTER);
    *ppLogger = NULL;
    if (cGroups)
    {
        AssertPtrReturn(papszGroups, VERR_INVALID_POINTER);
        AssertReturn(cGroups < _8K, VERR_OUT_OF_RANGE);
    }
    AssertMsgReturn(cHistory < _1M, ("%#x", cHistory), VERR_OUT_OF_RANGE);
    AssertReturn(cBufDescs <= 128, VERR_OUT_OF_RANGE);

    /*
     * Calculate the logger size.
     */
    AssertCompileSize(RTLOGGER, 32);
    cbLogger = RT_UOFFSETOF_DYN(RTLOGGERINTERNAL, afGroups[cGroups]);
    if (fFlags & RTLOGFLAGS_RESTRICT_GROUPS)
        cbLogger += cGroups * sizeof(uint32_t);
    if (cBufDescs == 0)
    {
        /* Allocate one buffer descriptor and a default sized buffer. */
        cbLogger   = RT_ALIGN_Z(cbLogger, RTLOG_BUFFER_ALIGN);
        offBuffers = cbLogger;
        cbLogger  += RT_ALIGN_Z(sizeof(paBufDescs[0]), RTLOG_BUFFER_ALIGN) + RTLOG_BUFFER_DEFAULT_SIZE;
    }
    else
    {
        /* Caller-supplied buffer descriptors.  If pchBuf is NULL, we have to allocate the buffers. */
        AssertPtrReturn(paBufDescs, VERR_INVALID_POINTER);
        if (paBufDescs[0].pchBuf != NULL)
            offBuffers = 0;
        else
        {
            cbLogger = RT_ALIGN_Z(cbLogger, RTLOG_BUFFER_ALIGN);
            offBuffers = cbLogger;
        }

        for (i = 0; i < cBufDescs; i++)
        {
            AssertReturn(paBufDescs[i].u32Magic == RTLOGBUFFERDESC_MAGIC, VERR_INVALID_MAGIC);
            AssertReturn(paBufDescs[i].uReserved == 0, VERR_INVALID_PARAMETER);
            AssertMsgReturn(paBufDescs[i].cbBuf >= _1K && paBufDescs[i].cbBuf <= _64M,
                            ("paBufDesc[%u].cbBuf=%#x\n", i, paBufDescs[i].cbBuf), VERR_OUT_OF_RANGE);
            AssertReturn(paBufDescs[i].offBuf == 0, VERR_INVALID_PARAMETER);
            if (offBuffers != 0)
            {
                cbLogger += RT_ALIGN_Z(paBufDescs[i].cbBuf, RTLOG_BUFFER_ALIGN);
                AssertReturn(paBufDescs[i].pchBuf == NULL, VERR_INVALID_PARAMETER);
                AssertReturn(paBufDescs[i].pAux == NULL, VERR_INVALID_PARAMETER);
            }
            else
            {
                AssertPtrReturn(paBufDescs[i].pchBuf, VERR_INVALID_POINTER);
                AssertPtrNullReturn(paBufDescs[i].pAux, VERR_INVALID_POINTER);
            }
        }
    }

    /*
     * Allocate a logger instance.
     */
    pLoggerInt = (PRTLOGGERINTERNAL)RTMemAllocZVarTag(cbLogger, "may-leak:log-instance");
    if (pLoggerInt)
    {
# if defined(RT_ARCH_X86) && !defined(LOG_USE_C99)
        uint8_t *pu8Code;
# endif
        pLoggerInt->Core.u32Magic               = RTLOGGER_MAGIC;
        pLoggerInt->cGroups                     = cGroups;
        pLoggerInt->fFlags                      = fFlags;
        pLoggerInt->fDestFlags                  = fDestFlags;
        pLoggerInt->uRevision                   = RTLOGGERINTERNAL_REV;
        pLoggerInt->cbSelf                      = sizeof(RTLOGGERINTERNAL);
        pLoggerInt->hSpinMtx                    = NIL_RTSEMSPINMUTEX;
        pLoggerInt->pfnFlush                    = NULL;
        pLoggerInt->pfnPrefix                   = NULL;
        pLoggerInt->pvPrefixUserArg             = NULL;
        pLoggerInt->fPendingPrefix              = true;
        pLoggerInt->fCreated                    = false;
        pLoggerInt->nsR0ProgramStart            = 0;
        RT_ZERO(pLoggerInt->szR0ThreadName);
        pLoggerInt->cMaxGroups                  = cGroups;
        pLoggerInt->papszGroups                 = papszGroups;
        if (fFlags & RTLOGFLAGS_RESTRICT_GROUPS)
            pLoggerInt->pacEntriesPerGroup      = &pLoggerInt->afGroups[cGroups];
        else
            pLoggerInt->pacEntriesPerGroup      = NULL;
        pLoggerInt->cMaxEntriesPerGroup         = cMaxEntriesPerGroup ? cMaxEntriesPerGroup : UINT32_MAX;
# ifdef IN_RING3
        pLoggerInt->pfnPhase                    = pfnPhase;
        pLoggerInt->hFile                       = NIL_RTFILE;
        pLoggerInt->fLogOpened                  = false;
        pLoggerInt->cHistory                    = cHistory;
        if (cbHistoryFileMax == 0)
            pLoggerInt->cbHistoryFileMax        = UINT64_MAX;
        else
            pLoggerInt->cbHistoryFileMax        = cbHistoryFileMax;
        if (cSecsHistoryTimeSlot == 0)
            pLoggerInt->cSecsHistoryTimeSlot    = UINT32_MAX;
        else
            pLoggerInt->cSecsHistoryTimeSlot    = cSecsHistoryTimeSlot;

        if (pOutputIf)
        {
            pLoggerInt->pOutputIf               = pOutputIf;
            pLoggerInt->pvOutputIfUser          = pvOutputIfUser;
        }
        else
        {
            /* Use the default interface for output logging. */
            pLoggerInt->pOutputIf               = &g_LogOutputIfDef;
            pLoggerInt->pvOutputIfUser          = pLoggerInt;
        }

# else   /* !IN_RING3 */
        RT_NOREF_PV(pfnPhase); RT_NOREF_PV(cHistory); RT_NOREF_PV(cbHistoryFileMax); RT_NOREF_PV(cSecsHistoryTimeSlot);
        RT_NOREF_PV(pOutputIf); RT_NOREF_PV(pvOutputIfUser);
# endif  /* !IN_RING3 */
        if (pszGroupSettings)
            RTLogGroupSettings(&pLoggerInt->Core, pszGroupSettings);

        /*
         * Buffer descriptors.
         */
        if (!offBuffers)
        {
            /* Caller-supplied descriptors: */
            pLoggerInt->cBufDescs  = cBufDescs;
            pLoggerInt->paBufDescs = paBufDescs;
        }
        else if (cBufDescs)
        {
            /* Caller-supplied descriptors, but we allocate the actual buffers: */
            pLoggerInt->cBufDescs  = cBufDescs;
            pLoggerInt->paBufDescs = paBufDescs;
            for (i = 0; i < cBufDescs; i++)
            {
                paBufDescs[i].pchBuf = (char *)pLoggerInt + offBuffers;
                offBuffers = RT_ALIGN_Z(offBuffers + paBufDescs[i].cbBuf, RTLOG_BUFFER_ALIGN);
            }
            Assert(offBuffers == cbLogger);
        }
        else
        {
            /* One descriptor with a default sized buffer. */
            pLoggerInt->cBufDescs  = cBufDescs  = 1;
            pLoggerInt->paBufDescs = paBufDescs = (PRTLOGBUFFERDESC)((char *)(char *)pLoggerInt + offBuffers);
            offBuffers = RT_ALIGN_Z(offBuffers + sizeof(paBufDescs[0]) * cBufDescs, RTLOG_BUFFER_ALIGN);
            for (i = 0; i < cBufDescs; i++)
            {
                paBufDescs[i].u32Magic  = RTLOGBUFFERDESC_MAGIC;
                paBufDescs[i].uReserved = 0;
                paBufDescs[i].cbBuf     = RTLOG_BUFFER_DEFAULT_SIZE;
                paBufDescs[i].offBuf    = 0;
                paBufDescs[i].pAux      = NULL;
                paBufDescs[i].pchBuf    = (char *)pLoggerInt + offBuffers;
                offBuffers = RT_ALIGN_Z(offBuffers + RTLOG_BUFFER_DEFAULT_SIZE, RTLOG_BUFFER_ALIGN);
            }
            Assert(offBuffers == cbLogger);
        }
        pLoggerInt->pBufDesc   = paBufDescs;
        pLoggerInt->idxBufDesc = 0;

# if defined(RT_ARCH_X86) && !defined(LOG_USE_C99) && 0 /* retired */
        /*
         * Emit wrapper code.
         */
        pu8Code = (uint8_t *)RTMemExecAlloc(64);
        if (pu8Code)
        {
            pLoggerInt->Core.pfnLogger = *(PFNRTLOGGER *)&pu8Code;
            *pu8Code++ = 0x68;          /* push imm32 */
            *(void **)pu8Code = &pLoggerInt->Core;
            pu8Code += sizeof(void *);
            *pu8Code++ = 0xe8;          /* call rel32 */
            *(uint32_t *)pu8Code = (uintptr_t)RTLogLogger - ((uintptr_t)pu8Code + sizeof(uint32_t));
            pu8Code += sizeof(uint32_t);
            *pu8Code++ = 0x8d;          /* lea esp, [esp + 4] */
            *pu8Code++ = 0x64;
            *pu8Code++ = 0x24;
            *pu8Code++ = 0x04;
            *pu8Code++ = 0xc3;          /* ret near */
            AssertMsg((uintptr_t)pu8Code - (uintptr_t)pLoggerInt->Core.pfnLogger <= 64,
                      ("Wrapper assembly is too big! %d bytes\n", (uintptr_t)pu8Code - (uintptr_t)pLoggerInt->Core.pfnLogger));
            rc = VINF_SUCCESS;
        }
        else
        {
            rc = VERR_NO_MEMORY;
#  ifdef RT_OS_LINUX
            /* Most probably SELinux causing trouble since the larger RTMemAlloc succeeded. */
            RTErrInfoSet(pErrInfo, rc, N_("mmap(PROT_WRITE | PROT_EXEC) failed -- SELinux?"));
#  endif
        }
        if (RT_SUCCESS(rc))
# endif /* X86 wrapper code */
        {
# ifdef IN_RING3 /* files and env.vars. are only accessible when in R3 at the present time. */
            /*
             * Format the filename.
             */
            if (pszFilenameFmt)
            {
                /** @todo validate the length, fail on overflow. */
                RTStrPrintfV(pLoggerInt->szFilename, sizeof(pLoggerInt->szFilename), pszFilenameFmt, args);
                if (pLoggerInt->szFilename[0])
                    pLoggerInt->fDestFlags |= RTLOGDEST_FILE;
            }

            /*
             * Parse the environment variables.
             */
            if (pszEnvVarBase)
            {
                /* make temp copy of environment variable base. */
                size_t  cchEnvVarBase = strlen(pszEnvVarBase);
                char   *pszEnvVar = (char *)alloca(cchEnvVarBase + 16);
                memcpy(pszEnvVar, pszEnvVarBase, cchEnvVarBase);

                /*
                 * Destination.
                 */
                strcpy(pszEnvVar + cchEnvVarBase, "_DEST");
                const char *pszValue = RTEnvGet(pszEnvVar);
                if (pszValue)
                    RTLogDestinations(&pLoggerInt->Core, pszValue);

                /*
                 * The flags.
                 */
                strcpy(pszEnvVar + cchEnvVarBase, "_FLAGS");
                pszValue = RTEnvGet(pszEnvVar);
                if (pszValue)
                    RTLogFlags(&pLoggerInt->Core, pszValue);

                /*
                 * The group settings.
                 */
                pszEnvVar[cchEnvVarBase] = '\0';
                pszValue = RTEnvGet(pszEnvVar);
                if (pszValue)
                    RTLogGroupSettings(&pLoggerInt->Core, pszValue);

                /*
                 * Group limit.
                 */
                strcpy(pszEnvVar + cchEnvVarBase, "_MAX_PER_GROUP");
                pszValue = RTEnvGet(pszEnvVar);
                if (pszValue)
                {
                    uint32_t cMax;
                    rc = RTStrToUInt32Full(pszValue, 0, &cMax);
                    if (RT_SUCCESS(rc))
                        pLoggerInt->cMaxEntriesPerGroup = cMax ? cMax : UINT32_MAX;
                    else
                        AssertMsgFailed(("Invalid group limit! %s=%s\n", pszEnvVar, pszValue));
                }

            }
# else  /* !IN_RING3 */
            RT_NOREF_PV(pszEnvVarBase); RT_NOREF_PV(pszFilenameFmt); RT_NOREF_PV(args);
# endif /* !IN_RING3 */

            /*
             * Open the destination(s).
             */
            rc = VINF_SUCCESS;
            if ((pLoggerInt->fDestFlags & (RTLOGDEST_F_DELAY_FILE | RTLOGDEST_FILE)) == RTLOGDEST_F_DELAY_FILE)
                pLoggerInt->fDestFlags &= ~RTLOGDEST_F_DELAY_FILE;
# ifdef IN_RING3
            if ((pLoggerInt->fDestFlags & (RTLOGDEST_FILE | RTLOGDEST_F_DELAY_FILE)) == RTLOGDEST_FILE)
                rc = rtR3LogOpenFileDestination(pLoggerInt, pErrInfo);
# endif

            if ((pLoggerInt->fDestFlags & RTLOGDEST_RINGBUF) && RT_SUCCESS(rc))
                rc = rtLogRingBufAdjust(pLoggerInt, pLoggerInt->cbRingBuf, true /*fForce*/);

            /*
             * Create mutex and check how much it counts when entering the lock
             * so that we can report the values for RTLOGFLAGS_PREFIX_LOCK_COUNTS.
             */
            if (RT_SUCCESS(rc))
            {
                if (!(fFlags & RTLOG_F_NO_LOCKING))
                    rc = RTSemSpinMutexCreate(&pLoggerInt->hSpinMtx, RTSEMSPINMUTEX_FLAGS_IRQ_SAFE);
                if (RT_SUCCESS(rc))
                {
# ifdef IN_RING3 /** @todo do counters in ring-0 too? */
                    RTTHREAD Thread = RTThreadSelf();
                    if (Thread != NIL_RTTHREAD)
                    {
                        int32_t c = RTLockValidatorWriteLockGetCount(Thread);
                        RTSemSpinMutexRequest(pLoggerInt->hSpinMtx);
                        c = RTLockValidatorWriteLockGetCount(Thread) - c;
                        RTSemSpinMutexRelease(pLoggerInt->hSpinMtx);
                        ASMAtomicWriteU32(&g_cLoggerLockCount, c);
                    }

                    /* Use the callback to generate some initial log contents. */
                    AssertPtrNull(pLoggerInt->pfnPhase);
                    if (pLoggerInt->pfnPhase)
                        pLoggerInt->pfnPhase(&pLoggerInt->Core, RTLOGPHASE_BEGIN, rtlogPhaseMsgNormal);
# endif
                    pLoggerInt->fCreated = true;
                    *ppLogger = &pLoggerInt->Core;

# if defined(IN_RING3) && (defined(IN_RT_STATIC) || defined(IPRT_NO_CRT))
                    /* Make sure the weak symbol emulation bits are ready before returning. */
                    if (!g_pfnRTLogLoggerExV)
                        g_pfnRTLogLoggerExV = RTLogLoggerExV;
# endif
                    return VINF_SUCCESS;
                }

                RTErrInfoSet(pErrInfo, rc, N_("failed to create semaphore"));
            }
# ifdef IN_RING3
            if (pLoggerInt->fLogOpened)
            {
                pLoggerInt->pOutputIf->pfnClose(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser);
                pLoggerInt->pOutputIf->pfnDirCtxClose(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx);
                pLoggerInt->pvDirCtx = NULL;
            }
#endif
# if defined(RT_ARCH_X86) && !defined(LOG_USE_C99) && 0 /* retired */
            if (pLoggerInt->Core.pfnLogger)
            {
                RTMemExecFree(*(void **)&pLoggerInt->Core.pfnLogger, 64);
                pLoggerInt->Core.pfnLogger = NULL;
            }
# endif
        }
        RTMemFree(pLoggerInt);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}
RT_EXPORT_SYMBOL(RTLogCreateExV);


RTDECL(int) RTLogCreate(PRTLOGGER *ppLogger, uint64_t fFlags, const char *pszGroupSettings,
                        const char *pszEnvVarBase, unsigned cGroups, const char * const * papszGroups,
                        uint32_t fDestFlags, const char *pszFilenameFmt, ...)
{
    va_list va;
    int     rc;

    va_start(va, pszFilenameFmt);
    rc = RTLogCreateExV(ppLogger, pszEnvVarBase, fFlags, pszGroupSettings, cGroups, papszGroups,
                        UINT32_MAX /*cMaxEntriesPerGroup*/,
                        0 /*cBufDescs*/, NULL /*paBufDescs*/, fDestFlags,
                        NULL /*pfnPhase*/, 0 /*cHistory*/, 0 /*cbHistoryFileMax*/, 0 /*cSecsHistoryTimeSlot*/,
                        NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                        NULL /*pErrInfo*/, pszFilenameFmt, va);
    va_end(va);
    return rc;
}
RT_EXPORT_SYMBOL(RTLogCreate);


RTDECL(int) RTLogDestroy(PRTLOGGER pLogger)
{
    int                 rc;
    uint32_t            iGroup;
    RTSEMSPINMUTEX      hSpinMtx;
    PRTLOGGERINTERNAL   pLoggerInt = (PRTLOGGERINTERNAL)pLogger;

    /*
     * Validate input.
     */
    if (!pLoggerInt)
        return VINF_SUCCESS;
    AssertPtrReturn(pLoggerInt, VERR_INVALID_POINTER);
    AssertReturn(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC, VERR_INVALID_MAGIC);

    /*
     * Acquire logger instance sem and disable all logging. (paranoia)
     */
    rc = rtlogLock(pLoggerInt);
    AssertMsgRCReturn(rc, ("%Rrc\n", rc), rc);

    pLoggerInt->fFlags |= RTLOGFLAGS_DISABLED;
    iGroup = pLoggerInt->cGroups;
    while (iGroup-- > 0)
        pLoggerInt->afGroups[iGroup] = 0;

    /*
     * Flush it.
     */
    rtlogFlush(pLoggerInt, false /*fNeedSpace*/);

# ifdef IN_RING3
    /*
     * Add end of logging message.
     */
    if (   (pLoggerInt->fDestFlags & RTLOGDEST_FILE)
        && pLoggerInt->fLogOpened)
        pLoggerInt->pfnPhase(&pLoggerInt->Core, RTLOGPHASE_END, rtlogPhaseMsgLocked);

    /*
     * Close output stuffs.
     */
    if (pLoggerInt->fLogOpened)
    {
        int rc2 = pLoggerInt->pOutputIf->pfnClose(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
        pLoggerInt->fLogOpened = false;

        pLoggerInt->pOutputIf->pfnDirCtxClose(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx);
        pLoggerInt->pvDirCtx = NULL;
    }
# endif

    /*
     * Free the mutex, the wrapper and the instance memory.
     */
    hSpinMtx = pLoggerInt->hSpinMtx;
    pLoggerInt->hSpinMtx = NIL_RTSEMSPINMUTEX;
    if (hSpinMtx != NIL_RTSEMSPINMUTEX)
    {
        int rc2;
        RTSemSpinMutexRelease(hSpinMtx);
        rc2 = RTSemSpinMutexDestroy(hSpinMtx);
        AssertRC(rc2);
        if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
            rc = rc2;
    }

# if defined(RT_ARCH_X86) && !defined(LOG_USE_C99) && 0 /* retired */
    if (pLoggerInt->Core.pfnLogger)
    {
        RTMemExecFree(*(void **)&pLoggerInt->Core.pfnLogger, 64);
        pLoggerInt->Core.pfnLogger = NULL;
    }
# endif
    RTMemFree(pLoggerInt);

    return rc;
}
RT_EXPORT_SYMBOL(RTLogDestroy);


RTDECL(int) RTLogSetCustomPrefixCallback(PRTLOGGER pLogger, PFNRTLOGPREFIX pfnCallback, void *pvUser)
{
    int               rc;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Do the work.
     */
    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        pLoggerInt->pvPrefixUserArg = pvUser;
        pLoggerInt->pfnPrefix       = pfnCallback;
        rtlogUnlock(pLoggerInt);
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTLogSetCustomPrefixCallback);


RTDECL(int) RTLogSetFlushCallback(PRTLOGGER pLogger, PFNRTLOGFLUSH pfnFlush)
{
    int               rc;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Do the work.
     */
    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        if (pLoggerInt->pfnFlush && pLoggerInt->pfnFlush != pfnFlush)
            rc = VWRN_ALREADY_EXISTS;
        pLoggerInt->pfnFlush = pfnFlush;
        rtlogUnlock(pLoggerInt);
    }

    return rc;
}
RT_EXPORT_SYMBOL(RTLogSetFlushCallback);


/**
 * Matches a group name with a pattern mask in an case insensitive manner (ASCII).
 *
 * @returns true if matching and *ppachMask set to the end of the pattern.
 * @returns false if no match.
 * @param   pszGrp      The group name.
 * @param   ppachMask   Pointer to the pointer to the mask. Only wildcard supported is '*'.
 * @param   cchMask     The length of the mask, including modifiers. The modifiers is why
 *                      we update *ppachMask on match.
 */
static bool rtlogIsGroupMatching(const char *pszGrp, const char **ppachMask, size_t cchMask)
{
    const char *pachMask;

    if (!pszGrp || !*pszGrp)
        return false;
    pachMask = *ppachMask;
    for (;;)
    {
        if (RT_C_TO_LOWER(*pszGrp) != RT_C_TO_LOWER(*pachMask))
        {
            const char *pszTmp;

            /*
             * Check for wildcard and do a minimal match if found.
             */
            if (*pachMask != '*')
                return false;

            /* eat '*'s. */
            do  pachMask++;
            while (--cchMask && *pachMask == '*');

            /* is there more to match? */
            if (    !cchMask
                ||  *pachMask == '.'
                ||  *pachMask == '=')
                break; /* we're good */

            /* do extremely minimal matching (fixme) */
            pszTmp = strchr(pszGrp, RT_C_TO_LOWER(*pachMask));
            if (!pszTmp)
                pszTmp = strchr(pszGrp, RT_C_TO_UPPER(*pachMask));
            if (!pszTmp)
                return false;
            pszGrp = pszTmp;
            continue;
        }

        /* done? */
        if (!*++pszGrp)
        {
            /* trailing wildcard is ok. */
            do
            {
                pachMask++;
                cchMask--;
            } while (cchMask && *pachMask == '*');
            if (    !cchMask
                ||  *pachMask == '.'
                ||  *pachMask == '=')
                break; /* we're good */
            return false;
        }

        if (!--cchMask)
            return false;
        pachMask++;
    }

    /* match */
    *ppachMask = pachMask;
    return true;
}


RTDECL(int) RTLogGroupSettings(PRTLOGGER pLogger, const char *pszValue)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);

    /*
     * Iterate the string.
     */
    while (*pszValue)
    {
        /*
         * Skip prefixes (blanks, ;, + and -).
         */
        bool    fEnabled = true;
        char    ch;
        const char *pszStart;
        unsigned i;
        size_t cch;

        while ((ch = *pszValue) == '+' || ch == '-' || ch == ' ' || ch == '\t' || ch == '\n' || ch == ';')
        {
            if (ch == '+' || ch == '-' || ch == ';')
                fEnabled = ch != '-';
            pszValue++;
        }
        if (!*pszValue)
            break;

        /*
         * Find end.
         */
        pszStart = pszValue;
        while ((ch = *pszValue) != '\0' && ch != '+' && ch != '-' && ch != ' ' && ch != '\t')
            pszValue++;

        /*
         * Find the group (ascii case insensitive search).
         * Special group 'all'.
         */
        cch = pszValue - pszStart;
        if (    cch >= 3
            &&  (pszStart[0] == 'a' || pszStart[0] == 'A')
            &&  (pszStart[1] == 'l' || pszStart[1] == 'L')
            &&  (pszStart[2] == 'l' || pszStart[2] == 'L')
            &&  (cch == 3 || pszStart[3] == '.' || pszStart[3] == '='))
        {
            /*
             * All.
             */
            unsigned fFlags = cch == 3
                            ? RTLOGGRPFLAGS_ENABLED | RTLOGGRPFLAGS_LEVEL_1
                            : rtlogGroupFlags(&pszStart[3]);
            for (i = 0; i < pLoggerInt->cGroups; i++)
            {
                if (fEnabled)
                    pLoggerInt->afGroups[i] |= fFlags;
                else
                    pLoggerInt->afGroups[i] &= ~fFlags;
            }
        }
        else
        {
            /*
             * Specific group(s).
             */
            for (i = 0; i < pLoggerInt->cGroups; i++)
            {
                const char *psz2 = (const char*)pszStart;
                if (rtlogIsGroupMatching(pLoggerInt->papszGroups[i], &psz2, cch))
                {
                    unsigned fFlags = RTLOGGRPFLAGS_ENABLED | RTLOGGRPFLAGS_LEVEL_1;
                    if (*psz2 == '.' || *psz2 == '=')
                        fFlags = rtlogGroupFlags(psz2);
                    if (fEnabled)
                        pLoggerInt->afGroups[i] |= fFlags;
                    else
                        pLoggerInt->afGroups[i] &= ~fFlags;
                }
            } /* for each group */
        }

    } /* parse specification */

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTLogGroupSettings);


/**
 * Interprets the group flags suffix.
 *
 * @returns Flags specified. (0 is possible!)
 * @param   psz     Start of Suffix. (Either dot or equal sign.)
 */
static unsigned rtlogGroupFlags(const char *psz)
{
    unsigned fFlags = 0;

    /*
     * Literal flags.
     */
    while (*psz == '.')
    {
        static struct
        {
            const char *pszFlag;        /* lowercase!! */
            unsigned    fFlag;
        } aFlags[] =
        {
            { "eo",         RTLOGGRPFLAGS_ENABLED },
            { "enabledonly",RTLOGGRPFLAGS_ENABLED },
            { "e",          RTLOGGRPFLAGS_ENABLED | RTLOGGRPFLAGS_LEVEL_1 | RTLOGGRPFLAGS_WARN },
            { "enabled",    RTLOGGRPFLAGS_ENABLED | RTLOGGRPFLAGS_LEVEL_1 | RTLOGGRPFLAGS_WARN },
            { "l1",         RTLOGGRPFLAGS_LEVEL_1 },
            { "level1",     RTLOGGRPFLAGS_LEVEL_1 },
            { "l",          RTLOGGRPFLAGS_LEVEL_2 },
            { "l2",         RTLOGGRPFLAGS_LEVEL_2 },
            { "level2",     RTLOGGRPFLAGS_LEVEL_2 },
            { "l3",         RTLOGGRPFLAGS_LEVEL_3 },
            { "level3",     RTLOGGRPFLAGS_LEVEL_3 },
            { "l4",         RTLOGGRPFLAGS_LEVEL_4 },
            { "level4",     RTLOGGRPFLAGS_LEVEL_4 },
            { "l5",         RTLOGGRPFLAGS_LEVEL_5 },
            { "level5",     RTLOGGRPFLAGS_LEVEL_5 },
            { "l6",         RTLOGGRPFLAGS_LEVEL_6 },
            { "level6",     RTLOGGRPFLAGS_LEVEL_6 },
            { "l7",         RTLOGGRPFLAGS_LEVEL_7 },
            { "level7",     RTLOGGRPFLAGS_LEVEL_7 },
            { "l8",         RTLOGGRPFLAGS_LEVEL_8 },
            { "level8",     RTLOGGRPFLAGS_LEVEL_8 },
            { "l9",         RTLOGGRPFLAGS_LEVEL_9 },
            { "level9",     RTLOGGRPFLAGS_LEVEL_9 },
            { "l10",        RTLOGGRPFLAGS_LEVEL_10 },
            { "level10",    RTLOGGRPFLAGS_LEVEL_10 },
            { "l11",        RTLOGGRPFLAGS_LEVEL_11 },
            { "level11",    RTLOGGRPFLAGS_LEVEL_11 },
            { "l12",        RTLOGGRPFLAGS_LEVEL_12 },
            { "level12",    RTLOGGRPFLAGS_LEVEL_12 },
            { "f",          RTLOGGRPFLAGS_FLOW },
            { "flow",       RTLOGGRPFLAGS_FLOW },
            { "w",          RTLOGGRPFLAGS_WARN },
            { "warn",       RTLOGGRPFLAGS_WARN },
            { "warning",    RTLOGGRPFLAGS_WARN },
            { "restrict",   RTLOGGRPFLAGS_RESTRICT },

        };
        unsigned    i;
        bool        fFound = false;
        psz++;
        for (i = 0; i < RT_ELEMENTS(aFlags) && !fFound; i++)
        {
            const char *psz1 = aFlags[i].pszFlag;
            const char *psz2 = psz;
            while (*psz1 == RT_C_TO_LOWER(*psz2))
            {
                psz1++;
                psz2++;
                if (!*psz1)
                {
                    if (    (*psz2 >= 'a' && *psz2 <= 'z')
                        ||  (*psz2 >= 'A' && *psz2 <= 'Z')
                        ||  (*psz2 >= '0' && *psz2 <= '9') )
                        break;
                    fFlags |= aFlags[i].fFlag;
                    fFound = true;
                    psz = psz2;
                    break;
                }
            } /* strincmp */
        } /* for each flags */
        AssertMsg(fFound, ("%.15s...", psz));
    }

    /*
     * Flag value.
     */
    if (*psz == '=')
    {
        psz++;
        if (*psz == '~')
            fFlags = ~RTStrToInt32(psz + 1);
        else
            fFlags = RTStrToInt32(psz);
    }

    return fFlags;
}


/**
 * Helper for RTLogGetGroupSettings.
 */
static int rtLogGetGroupSettingsAddOne(const char *pszName, uint32_t fGroup, char **ppszBuf, size_t *pcchBuf, bool *pfNotFirst)
{
#define APPEND_PSZ(psz,cch) do { memcpy(*ppszBuf, (psz), (cch)); *ppszBuf += (cch); *pcchBuf -= (cch); } while (0)
#define APPEND_SZ(sz)       APPEND_PSZ(sz, sizeof(sz) - 1)
#define APPEND_CH(ch)       do { **ppszBuf = (ch); *ppszBuf += 1; *pcchBuf -= 1; } while (0)

    /*
     * Add the name.
     */
    size_t cchName = strlen(pszName);
    if (cchName + 1 + *pfNotFirst > *pcchBuf)
        return VERR_BUFFER_OVERFLOW;
    if (*pfNotFirst)
        APPEND_CH(' ');
    else
        *pfNotFirst = true;
    APPEND_PSZ(pszName, cchName);

    /*
     * Only generate mnemonics for the simple+common bits.
     */
    if (fGroup == (RTLOGGRPFLAGS_ENABLED | RTLOGGRPFLAGS_LEVEL_1))
        /* nothing */;
    else if (    fGroup == (RTLOGGRPFLAGS_ENABLED | RTLOGGRPFLAGS_LEVEL_1 | RTLOGGRPFLAGS_LEVEL_2 |  RTLOGGRPFLAGS_FLOW)
             &&  *pcchBuf >= sizeof(".e.l.f"))
        APPEND_SZ(".e.l.f");
    else if (    fGroup == (RTLOGGRPFLAGS_ENABLED | RTLOGGRPFLAGS_LEVEL_1 | RTLOGGRPFLAGS_FLOW)
             &&  *pcchBuf >= sizeof(".e.f"))
        APPEND_SZ(".e.f");
    else if (*pcchBuf >= 1 + 10 + 1)
    {
        size_t cch;
        APPEND_CH('=');
        cch = RTStrFormatNumber(*ppszBuf, fGroup, 16, 0, 0, RTSTR_F_SPECIAL | RTSTR_F_32BIT);
        *ppszBuf += cch;
        *pcchBuf -= cch;
    }
    else
        return VERR_BUFFER_OVERFLOW;

#undef APPEND_PSZ
#undef APPEND_SZ
#undef APPEND_CH
    return VINF_SUCCESS;
}


RTDECL(int) RTLogQueryGroupSettings(PRTLOGGER pLogger, char *pszBuf, size_t cchBuf)
{
    bool              fNotFirst  = false;
    int               rc         = VINF_SUCCESS;
    uint32_t          cGroups;
    uint32_t          fGroup;
    uint32_t          i;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);
    Assert(cchBuf);

    /*
     * Check if all are the same.
     */
    cGroups = pLoggerInt->cGroups;
    fGroup  = pLoggerInt->afGroups[0];
    for (i = 1; i < cGroups; i++)
        if (pLoggerInt->afGroups[i] != fGroup)
            break;
    if (i >= cGroups)
        rc = rtLogGetGroupSettingsAddOne("all", fGroup, &pszBuf, &cchBuf, &fNotFirst);
    else
    {

        /*
         * Iterate all the groups and print all that are enabled.
         */
        for (i = 0; i < cGroups; i++)
        {
            fGroup = pLoggerInt->afGroups[i];
            if (fGroup)
            {
                const char *pszName = pLoggerInt->papszGroups[i];
                if (pszName)
                {
                    rc = rtLogGetGroupSettingsAddOne(pszName, fGroup, &pszBuf, &cchBuf, &fNotFirst);
                    if (rc)
                        break;
                }
            }
        }
    }

    *pszBuf = '\0';
    return rc;
}
RT_EXPORT_SYMBOL(RTLogQueryGroupSettings);


RTDECL(int) RTLogFlags(PRTLOGGER pLogger, const char *pszValue)
{
    int               rc         = VINF_SUCCESS;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);

    /*
     * Iterate the string.
     */
    while (*pszValue)
    {
        /* check no prefix. */
        bool fNo = false;
        char ch;
        unsigned i;

        /* skip blanks. */
        while (RT_C_IS_SPACE(*pszValue))
            pszValue++;
        if (!*pszValue)
            return rc;

        while ((ch = *pszValue) != '\0')
        {
            if (ch == 'n' && pszValue[1] == 'o')
            {
                pszValue += 2;
                fNo = !fNo;
            }
            else if (ch == '+')
            {
                pszValue++;
                fNo = true;
            }
            else if (ch == '-' || ch == '!' || ch == '~')
            {
                pszValue++;
                fNo = !fNo;
            }
            else
                break;
        }

        /* instruction. */
        for (i = 0; i < RT_ELEMENTS(g_aLogFlags); i++)
        {
            if (!strncmp(pszValue, g_aLogFlags[i].pszInstr, g_aLogFlags[i].cchInstr))
            {
                if (!(g_aLogFlags[i].fFixedDest & pLoggerInt->fDestFlags))
                {
                    if (fNo == g_aLogFlags[i].fInverted)
                        pLoggerInt->fFlags |= g_aLogFlags[i].fFlag;
                    else
                        pLoggerInt->fFlags &= ~g_aLogFlags[i].fFlag;
                }
                pszValue += g_aLogFlags[i].cchInstr;
                break;
            }
        }

        /* unknown instruction? */
        if (i >= RT_ELEMENTS(g_aLogFlags))
        {
            AssertMsgFailed(("Invalid flags! unknown instruction %.20s\n", pszValue));
            pszValue++;
        }

        /* skip blanks and delimiters. */
        while (RT_C_IS_SPACE(*pszValue) || *pszValue == ';')
            pszValue++;
    } /* while more environment variable value left */

    return rc;
}
RT_EXPORT_SYMBOL(RTLogFlags);


RTDECL(bool) RTLogSetBuffering(PRTLOGGER pLogger, bool fBuffered)
{
    int               rc;
    bool              fOld       = false;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, false);

    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        fOld  = !!(pLoggerInt->fFlags & RTLOGFLAGS_BUFFERED);
        if (fBuffered)
            pLoggerInt->fFlags |= RTLOGFLAGS_BUFFERED;
        else
            pLoggerInt->fFlags &= ~RTLOGFLAGS_BUFFERED;
        rtlogUnlock(pLoggerInt);
    }

    return fOld;
}
RT_EXPORT_SYMBOL(RTLogSetBuffering);


RTDECL(uint32_t) RTLogSetGroupLimit(PRTLOGGER pLogger, uint32_t cMaxEntriesPerGroup)
{
    int               rc;
    uint32_t          cOld       = UINT32_MAX;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, UINT32_MAX);

    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        cOld = pLoggerInt->cMaxEntriesPerGroup;
        pLoggerInt->cMaxEntriesPerGroup = cMaxEntriesPerGroup;
        rtlogUnlock(pLoggerInt);
    }

    return cOld;
}
RT_EXPORT_SYMBOL(RTLogSetGroupLimit);


#ifdef IN_RING0

RTR0DECL(int) RTLogSetR0ThreadNameV(PRTLOGGER pLogger, const char *pszNameFmt, va_list va)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    int               rc;
    if (pLoggerInt)
    {
        rc = rtlogLock(pLoggerInt);
        if (RT_SUCCESS(rc))
        {
            ssize_t cch = RTStrPrintf2V(pLoggerInt->szR0ThreadName, sizeof(pLoggerInt->szR0ThreadName), pszNameFmt, va);
            rtlogUnlock(pLoggerInt);
            rc = cch > 0 ? VINF_SUCCESS : VERR_BUFFER_OVERFLOW;
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
}
RT_EXPORT_SYMBOL(RTLogSetR0ThreadNameV);


RTR0DECL(int) RTLogSetR0ProgramStart(PRTLOGGER pLogger, uint64_t nsStart)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    int               rc;
    if (pLoggerInt)
    {
        rc = rtlogLock(pLoggerInt);
        if (RT_SUCCESS(rc))
        {
            pLoggerInt->nsR0ProgramStart = nsStart;
            rtlogUnlock(pLoggerInt);
        }
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
}
RT_EXPORT_SYMBOL(RTLogSetR0ProgramStart);

#endif /* IN_RING0 */

RTDECL(uint64_t) RTLogGetFlags(PRTLOGGER pLogger)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, UINT64_MAX);
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);
    return pLoggerInt->fFlags;
}
RT_EXPORT_SYMBOL(RTLogGetFlags);


RTDECL(int) RTLogChangeFlags(PRTLOGGER pLogger, uint64_t fSet, uint64_t fClear)
{
    int               rc;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    AssertReturn(!(fSet & ~RTLOG_F_VALID_MASK), VERR_INVALID_FLAGS);
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Make the changes.
     */
    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        pLoggerInt->fFlags &= ~fClear;
        pLoggerInt->fFlags |= fSet;
        rtlogUnlock(pLoggerInt);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLogChangeFlags);


RTDECL(int) RTLogQueryFlags(PRTLOGGER pLogger, char *pszBuf, size_t cchBuf)
{
    bool              fNotFirst  = false;
    int               rc         = VINF_SUCCESS;
    uint32_t          fFlags;
    unsigned          i;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;

    Assert(cchBuf);
    *pszBuf = '\0';
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);

    /*
     * Add the flags in the list.
     */
    fFlags = pLoggerInt->fFlags;
    for (i = 0; i < RT_ELEMENTS(g_aLogFlags); i++)
        if (    !g_aLogFlags[i].fInverted
            ?   (g_aLogFlags[i].fFlag & fFlags)
            :   !(g_aLogFlags[i].fFlag & fFlags))
        {
            size_t cchInstr = g_aLogFlags[i].cchInstr;
            if (cchInstr + fNotFirst + 1 > cchBuf)
            {
                rc = VERR_BUFFER_OVERFLOW;
                break;
            }
            if (fNotFirst)
            {
                *pszBuf++ = ' ';
                cchBuf--;
            }
            memcpy(pszBuf, g_aLogFlags[i].pszInstr, cchInstr);
            pszBuf += cchInstr;
            cchBuf -= cchInstr;
            fNotFirst = true;
        }
    *pszBuf = '\0';
    return rc;
}
RT_EXPORT_SYMBOL(RTLogQueryFlags);


/**
 * Finds the end of a destination value.
 *
 * The value ends when we counter a ';' or a free standing word (space on both
 * from the g_aLogDst table.  (If this is problematic for someone, we could
 * always do quoting and escaping.)
 *
 * @returns Value length in chars.
 * @param   pszValue            The first char after '=' or ':'.
 */
static size_t rtLogDestFindValueLength(const char *pszValue)
{
    size_t off = 0;
    char   ch;
    while ((ch = pszValue[off]) != '\0' && ch != ';')
    {
        if (!RT_C_IS_SPACE(ch))
            off++;
        else
        {
            unsigned i;
            size_t   cchThusFar = off;
            do
                off++;
            while ((ch = pszValue[off]) != '\0' && RT_C_IS_SPACE(ch));
            if (ch == ';')
                return cchThusFar;

            if (ch == 'n' && pszValue[off + 1] == 'o')
                off += 2;
            for (i = 0; i < RT_ELEMENTS(g_aLogDst); i++)
                if (!strncmp(&pszValue[off], g_aLogDst[i].pszInstr, g_aLogDst[i].cchInstr))
                {
                    ch = pszValue[off + g_aLogDst[i].cchInstr];
                    if (ch == '\0' || RT_C_IS_SPACE(ch) || ch == '=' || ch == ':' || ch == ';')
                        return cchThusFar;
                }
        }
    }
    return off;
}


RTDECL(int) RTLogDestinations(PRTLOGGER pLogger, char const *pszValue)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);
    /** @todo locking?   */

    /*
     * Do the parsing.
     */
    while (*pszValue)
    {
        bool fNo;
        unsigned i;

        /* skip blanks. */
        while (RT_C_IS_SPACE(*pszValue))
            pszValue++;
        if (!*pszValue)
            break;

        /* check no prefix. */
        fNo = false;
        if (   pszValue[0] == 'n'
            && pszValue[1] == 'o'
            && (   pszValue[2] != 'd'
                || pszValue[3] != 'e'
                || pszValue[4] != 'n'
                || pszValue[5] != 'y'))
        {
            fNo = true;
            pszValue += 2;
        }

        /* instruction. */
        for (i = 0; i < RT_ELEMENTS(g_aLogDst); i++)
        {
            if (!strncmp(pszValue, g_aLogDst[i].pszInstr, g_aLogDst[i].cchInstr))
            {
                if (!fNo)
                    pLoggerInt->fDestFlags |= g_aLogDst[i].fFlag;
                else
                    pLoggerInt->fDestFlags &= ~g_aLogDst[i].fFlag;
                pszValue += g_aLogDst[i].cchInstr;

                /* check for value. */
                while (RT_C_IS_SPACE(*pszValue))
                    pszValue++;
                if (*pszValue == '=' || *pszValue == ':')
                {
                    pszValue++;
                    size_t cch = rtLogDestFindValueLength(pszValue);
                    const char *pszEnd = pszValue + cch;

# ifdef IN_RING3
                    char szTmp[sizeof(pLoggerInt->szFilename)];
# else
                    char szTmp[32];
# endif
                    if (0)
                    { /* nothing */ }
# ifdef IN_RING3

                    /* log file name */
                    else if (i == 0 /* file */ && !fNo)
                    {
                        if (!(pLoggerInt->fDestFlags & RTLOGDEST_FIXED_FILE))
                        {
                            AssertReturn(cch < sizeof(pLoggerInt->szFilename), VERR_OUT_OF_RANGE);
                            memcpy(pLoggerInt->szFilename, pszValue, cch);
                            pLoggerInt->szFilename[cch] = '\0';
                            /** @todo reopen log file if pLoggerInt->fCreated is true ... */
                        }
                    }
                    /* log directory */
                    else if (i == 1 /* dir */ && !fNo)
                    {
                        if (!(pLoggerInt->fDestFlags & RTLOGDEST_FIXED_DIR))
                        {
                            const char *pszFile = RTPathFilename(pLoggerInt->szFilename);
                            size_t      cchFile = pszFile ? strlen(pszFile) : 0;
                            AssertReturn(cchFile + cch + 1 < sizeof(pLoggerInt->szFilename), VERR_OUT_OF_RANGE);
                            memcpy(szTmp, cchFile ? pszFile : "", cchFile + 1);

                            memcpy(pLoggerInt->szFilename, pszValue, cch);
                            pLoggerInt->szFilename[cch] = '\0';
                            RTPathStripTrailingSlash(pLoggerInt->szFilename);

                            cch = strlen(pLoggerInt->szFilename);
                            pLoggerInt->szFilename[cch++] = '/';
                            memcpy(&pLoggerInt->szFilename[cch], szTmp, cchFile);
                            pLoggerInt->szFilename[cch + cchFile] = '\0';
                            /** @todo reopen log file if pLoggerInt->fCreated is true ... */
                        }
                    }
                    else if (i == 2 /* history */)
                    {
                        if (!fNo)
                        {
                            uint32_t cHistory = 0;
                            int rc = RTStrCopyEx(szTmp, sizeof(szTmp), pszValue, cch);
                            if (RT_SUCCESS(rc))
                                rc = RTStrToUInt32Full(szTmp, 0, &cHistory);
                            AssertMsgReturn(RT_SUCCESS(rc) && cHistory < _1M, ("Invalid history value %s (%Rrc)!\n", szTmp, rc), rc);
                            pLoggerInt->cHistory = cHistory;
                        }
                        else
                            pLoggerInt->cHistory = 0;
                    }
                    else if (i == 3 /* histsize */)
                    {
                        if (!fNo)
                        {
                            int rc = RTStrCopyEx(szTmp, sizeof(szTmp), pszValue, cch);
                            if (RT_SUCCESS(rc))
                                rc = RTStrToUInt64Full(szTmp, 0, &pLoggerInt->cbHistoryFileMax);
                            AssertMsgRCReturn(rc, ("Invalid history file size value %s (%Rrc)!\n", szTmp, rc), rc);
                            if (pLoggerInt->cbHistoryFileMax == 0)
                                pLoggerInt->cbHistoryFileMax = UINT64_MAX;
                        }
                        else
                            pLoggerInt->cbHistoryFileMax = UINT64_MAX;
                    }
                    else if (i == 4 /* histtime */)
                    {
                        if (!fNo)
                        {
                            int rc = RTStrCopyEx(szTmp, sizeof(szTmp), pszValue, cch);
                            if (RT_SUCCESS(rc))
                                rc = RTStrToUInt32Full(szTmp, 0, &pLoggerInt->cSecsHistoryTimeSlot);
                            AssertMsgRCReturn(rc, ("Invalid history time slot value %s (%Rrc)!\n", szTmp, rc), rc);
                            if (pLoggerInt->cSecsHistoryTimeSlot == 0)
                                pLoggerInt->cSecsHistoryTimeSlot = UINT32_MAX;
                        }
                        else
                            pLoggerInt->cSecsHistoryTimeSlot = UINT32_MAX;
                    }
# endif /* IN_RING3 */
                    else if (i == 5 /* ringbuf */ && !fNo)
                    {
                        int rc = RTStrCopyEx(szTmp, sizeof(szTmp), pszValue, cch);
                        uint32_t cbRingBuf = 0;
                        if (RT_SUCCESS(rc))
                            rc = RTStrToUInt32Full(szTmp, 0, &cbRingBuf);
                        AssertMsgRCReturn(rc, ("Invalid ring buffer size value '%s' (%Rrc)!\n", szTmp, rc), rc);

                        if (cbRingBuf == 0)
                            cbRingBuf = RTLOG_RINGBUF_DEFAULT_SIZE;
                        else if (cbRingBuf < RTLOG_RINGBUF_MIN_SIZE)
                            cbRingBuf = RTLOG_RINGBUF_MIN_SIZE;
                        else if (cbRingBuf > RTLOG_RINGBUF_MAX_SIZE)
                            cbRingBuf = RTLOG_RINGBUF_MAX_SIZE;
                        else
                            cbRingBuf = RT_ALIGN_32(cbRingBuf, 64);
                        rc = rtLogRingBufAdjust(pLoggerInt, cbRingBuf, false /*fForce*/);
                        if (RT_FAILURE(rc))
                            return rc;
                    }
                    else
                        AssertMsgFailedReturn(("Invalid destination value! %s%s doesn't take a value!\n",
                                               fNo ? "no" : "", g_aLogDst[i].pszInstr),
                                              VERR_INVALID_PARAMETER);

                    pszValue = pszEnd + (*pszEnd != '\0');
                }
                else if (i == 5 /* ringbuf */ && !fNo && !pLoggerInt->pszRingBuf)
                {
                    int rc = rtLogRingBufAdjust(pLoggerInt, pLoggerInt->cbRingBuf, false /*fForce*/);
                    if (RT_FAILURE(rc))
                        return rc;
                }
                break;
            }
        }

        /* assert known instruction */
        AssertMsgReturn(i < RT_ELEMENTS(g_aLogDst),
                        ("Invalid destination value! unknown instruction %.20s\n", pszValue),
                        VERR_INVALID_PARAMETER);

        /* skip blanks and delimiters. */
        while (RT_C_IS_SPACE(*pszValue) || *pszValue == ';')
            pszValue++;
    } /* while more environment variable value left */

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTLogDestinations);


RTDECL(int) RTLogClearFileDelayFlag(PRTLOGGER pLogger, PRTERRINFO pErrInfo)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Do the work.
     */
    int rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        if (pLoggerInt->fDestFlags & RTLOGDEST_F_DELAY_FILE)
        {
            pLoggerInt->fDestFlags &= ~RTLOGDEST_F_DELAY_FILE;
# ifdef IN_RING3
            if (   pLoggerInt->fDestFlags & RTLOGDEST_FILE
                && !pLoggerInt->fLogOpened)
            {
                rc = rtR3LogOpenFileDestination(pLoggerInt, pErrInfo);
                if (RT_SUCCESS(rc))
                    rtlogFlush(pLoggerInt, false /*fNeedSpace*/);
            }
# endif
            RT_NOREF(pErrInfo); /** @todo fix create API to use RTErrInfo */
        }
        rtlogUnlock(pLoggerInt);
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTLogClearFileDelayFlag);


RTDECL(int) RTLogChangeDestinations(PRTLOGGER pLogger, uint32_t fSet, uint32_t fClear)
{
    int               rc;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    AssertCompile((RTLOG_DST_VALID_MASK & RTLOG_DST_CHANGE_MASK) == RTLOG_DST_CHANGE_MASK);
    AssertReturn(!(fSet & ~RTLOG_DST_CHANGE_MASK), VERR_INVALID_FLAGS);
    AssertReturn(!(fClear & ~RTLOG_DST_CHANGE_MASK), VERR_INVALID_FLAGS);
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Make the changes.
     */
    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        pLoggerInt->fDestFlags &= ~fClear;
        pLoggerInt->fDestFlags |= fSet;
        rtlogUnlock(pLoggerInt);
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTLogChangeDestinations);


RTDECL(uint32_t) RTLogGetDestinations(PRTLOGGER pLogger)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    if (!pLoggerInt)
    {
        pLoggerInt = (PRTLOGGERINTERNAL)RTLogDefaultInstance();
        if (!pLoggerInt)
            return UINT32_MAX;
    }
    return pLoggerInt->fDestFlags;
}
RT_EXPORT_SYMBOL(RTLogGetDestinations);


RTDECL(int) RTLogQueryDestinations(PRTLOGGER pLogger, char *pszBuf, size_t cchBuf)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    bool              fNotFirst  = false;
    int               rc         = VINF_SUCCESS;
    uint32_t          fDestFlags;
    unsigned          i;

    AssertReturn(cchBuf, VERR_INVALID_PARAMETER);
    *pszBuf = '\0';
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);

    /*
     * Add the flags in the list.
     */
    fDestFlags = pLoggerInt->fDestFlags;
    for (i = 6; i < RT_ELEMENTS(g_aLogDst); i++)
        if (g_aLogDst[i].fFlag & fDestFlags)
        {
            if (fNotFirst)
            {
                rc = RTStrCopyP(&pszBuf, &cchBuf, " ");
                if (RT_FAILURE(rc))
                    return rc;
            }
            rc = RTStrCopyP(&pszBuf, &cchBuf, g_aLogDst[i].pszInstr);
            if (RT_FAILURE(rc))
                return rc;
            fNotFirst = true;
        }

    char szNum[32];

# ifdef IN_RING3
    /*
     * Add the filename.
     */
    if (fDestFlags & RTLOGDEST_FILE)
    {
        rc = RTStrCopyP(&pszBuf, &cchBuf, fNotFirst ? " file=" : "file=");
        if (RT_FAILURE(rc))
            return rc;
        rc = RTStrCopyP(&pszBuf, &cchBuf, pLoggerInt->szFilename);
        if (RT_FAILURE(rc))
            return rc;
        fNotFirst = true;

        if (pLoggerInt->cHistory)
        {
            RTStrPrintf(szNum, sizeof(szNum), fNotFirst ? " history=%u" : "history=%u", pLoggerInt->cHistory);
            rc = RTStrCopyP(&pszBuf, &cchBuf, szNum);
            if (RT_FAILURE(rc))
                return rc;
            fNotFirst = true;
        }
        if (pLoggerInt->cbHistoryFileMax != UINT64_MAX)
        {
            RTStrPrintf(szNum, sizeof(szNum), fNotFirst ? " histsize=%llu" : "histsize=%llu", pLoggerInt->cbHistoryFileMax);
            rc = RTStrCopyP(&pszBuf, &cchBuf, szNum);
            if (RT_FAILURE(rc))
                return rc;
            fNotFirst = true;
        }
        if (pLoggerInt->cSecsHistoryTimeSlot != UINT32_MAX)
        {
            RTStrPrintf(szNum, sizeof(szNum), fNotFirst ? " histtime=%llu" : "histtime=%llu", pLoggerInt->cSecsHistoryTimeSlot);
            rc = RTStrCopyP(&pszBuf, &cchBuf, szNum);
            if (RT_FAILURE(rc))
                return rc;
            fNotFirst = true;
        }
    }
# endif /* IN_RING3 */

    /*
     * Add the ring buffer.
     */
    if (fDestFlags & RTLOGDEST_RINGBUF)
    {
        if (pLoggerInt->cbRingBuf == RTLOG_RINGBUF_DEFAULT_SIZE)
            rc = RTStrCopyP(&pszBuf, &cchBuf, fNotFirst ? " ringbuf" : "ringbuf");
        else
        {
            RTStrPrintf(szNum, sizeof(szNum), fNotFirst ? " ringbuf=%#x" : "ringbuf=%#x", pLoggerInt->cbRingBuf);
            rc = RTStrCopyP(&pszBuf, &cchBuf, szNum);
        }
        if (RT_FAILURE(rc))
            return rc;
        fNotFirst = true;
    }

    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTLogQueryDestinations);


/**
 * Helper for calculating the CRC32 of all the group names.
 */
static uint32_t rtLogCalcGroupNameCrc32(PRTLOGGERINTERNAL pLoggerInt)
{
    const char * const * const  papszGroups = pLoggerInt->papszGroups;
    uint32_t                    iGroup      = pLoggerInt->cGroups;
    uint32_t                    uCrc32      = RTCrc32Start();
    while (iGroup-- > 0)
    {
        const char *pszGroup = papszGroups[iGroup];
        uCrc32 = RTCrc32Process(uCrc32, pszGroup, strlen(pszGroup) + 1);
    }
    return RTCrc32Finish(uCrc32);
}

#ifdef IN_RING3

/**
 * Opens/creates the log file.
 *
 * @param   pLoggerInt      The logger instance to update. NULL is not allowed!
 * @param   pErrInfo        Where to return extended error information.
 *                          Optional.
 */
static int rtlogFileOpen(PRTLOGGERINTERNAL pLoggerInt, PRTERRINFO pErrInfo)
{
    uint32_t fOpen = RTFILE_O_WRITE | RTFILE_O_DENY_NONE;
    if (pLoggerInt->fFlags & RTLOGFLAGS_APPEND)
        fOpen |= RTFILE_O_OPEN_CREATE | RTFILE_O_APPEND;
    else
    {
        pLoggerInt->pOutputIf->pfnDelete(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                         pLoggerInt->szFilename);
        fOpen |= RTFILE_O_CREATE;
    }
    if (pLoggerInt->fFlags & RTLOGFLAGS_WRITE_THROUGH)
        fOpen |= RTFILE_O_WRITE_THROUGH;
    if (pLoggerInt->fDestFlags & RTLOGDEST_F_NO_DENY)
        fOpen = (fOpen & ~RTFILE_O_DENY_NONE) | RTFILE_O_DENY_NOT_DELETE;

    unsigned cBackoff = 0;
    int rc = pLoggerInt->pOutputIf->pfnOpen(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                            pLoggerInt->szFilename, fOpen);
    while (   (   rc == VERR_SHARING_VIOLATION
               || (rc == VERR_ALREADY_EXISTS && !(pLoggerInt->fFlags & RTLOGFLAGS_APPEND)))
           && cBackoff < RT_ELEMENTS(g_acMsLogBackoff))
    {
        RTThreadSleep(g_acMsLogBackoff[cBackoff++]);
        if (!(pLoggerInt->fFlags & RTLOGFLAGS_APPEND))
            pLoggerInt->pOutputIf->pfnDelete(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                             pLoggerInt->szFilename);
        rc = pLoggerInt->pOutputIf->pfnOpen(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                            pLoggerInt->szFilename, fOpen);
    }
    if (RT_SUCCESS(rc))
    {
        pLoggerInt->fLogOpened = true;

        rc = pLoggerInt->pOutputIf->pfnQuerySize(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser,
                                                 &pLoggerInt->cbHistoryFileWritten);
        if (RT_FAILURE(rc))
        {
            /* Don't complain if this fails, assume the file is empty. */
            pLoggerInt->cbHistoryFileWritten = 0;
            rc = VINF_SUCCESS;
        }
    }
    else
    {
        pLoggerInt->fLogOpened = false;
        RTErrInfoSetF(pErrInfo, rc, N_("could not open file '%s' (fOpen=%#x)"), pLoggerInt->szFilename, fOpen);
    }
    return rc;
}


/**
 * Closes, rotates and opens the log files if necessary.
 *
 * Used by the rtlogFlush() function as well as RTLogCreateExV() by way of
 * rtR3LogOpenFileDestination().
 *
 * @param   pLoggerInt  The logger instance to update. NULL is not allowed!
 * @param   uTimeSlot   Current time slot (for tikme based rotation).
 * @param   fFirst      Flag whether this is the beginning of logging, i.e.
 *                      called from RTLogCreateExV.  Prevents pfnPhase from
 *                      being called.
 * @param   pErrInfo    Where to return extended error information. Optional.
 */
static void rtlogRotate(PRTLOGGERINTERNAL pLoggerInt, uint32_t uTimeSlot, bool fFirst, PRTERRINFO pErrInfo)
{
    /* Suppress rotating empty log files simply because the time elapsed. */
    if (RT_UNLIKELY(!pLoggerInt->cbHistoryFileWritten))
        pLoggerInt->uHistoryTimeSlotStart = uTimeSlot;

    /* Check rotation condition: file still small enough and not too old? */
    if (RT_LIKELY(   pLoggerInt->cbHistoryFileWritten < pLoggerInt->cbHistoryFileMax
                  && uTimeSlot == pLoggerInt->uHistoryTimeSlotStart))
        return;

    /*
     * Save "disabled" log flag and make sure logging is disabled.
     * The logging in the functions called during log file history
     * rotation would cause severe trouble otherwise.
     */
    uint32_t const fSavedFlags = pLoggerInt->fFlags;
    pLoggerInt->fFlags |= RTLOGFLAGS_DISABLED;

    /*
     * Disable log rotation temporarily, otherwise with extreme settings and
     * chatty phase logging we could run into endless rotation.
     */
    uint32_t const cSavedHistory = pLoggerInt->cHistory;
    pLoggerInt->cHistory = 0;

    /*
     * Close the old log file.
     */
    if (pLoggerInt->fLogOpened)
    {
        /* Use the callback to generate some final log contents, but only if
         * this is a rotation with a fully set up logger. Leave the other case
         * to the RTLogCreateExV function. */
        if (pLoggerInt->pfnPhase && !fFirst)
        {
            uint32_t fODestFlags = pLoggerInt->fDestFlags;
            pLoggerInt->fDestFlags &= RTLOGDEST_FILE;
            pLoggerInt->pfnPhase(&pLoggerInt->Core, RTLOGPHASE_PREROTATE, rtlogPhaseMsgLocked);
            pLoggerInt->fDestFlags = fODestFlags;
        }

        pLoggerInt->pOutputIf->pfnClose(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser);
    }
    /*
     * If the log file was closed and we're being called from rtR3LogOpenFileDestination
     * we must open a log directory context before going on.
     */
    else if (!fFirst)
    {
        int rc = pLoggerInt->pOutputIf->pfnDirCtxOpen(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser,
                                                      pLoggerInt->szFilename, &pLoggerInt->pvDirCtx);
        if (RT_FAILURE(rc))
            return;
    }

    if (cSavedHistory)
    {
        /*
         * Rotate the log files.
         */
        for (uint32_t i = cSavedHistory - 1; i + 1 > 0; i--)
        {
            char szOldName[sizeof(pLoggerInt->szFilename) + 32];
            if (i > 0)
                RTStrPrintf(szOldName, sizeof(szOldName), "%s.%u", pLoggerInt->szFilename, i);
            else
                RTStrCopy(szOldName, sizeof(szOldName), pLoggerInt->szFilename);

            char szNewName[sizeof(pLoggerInt->szFilename) + 32];
            RTStrPrintf(szNewName, sizeof(szNewName), "%s.%u", pLoggerInt->szFilename, i + 1);

            unsigned cBackoff = 0;
            int rc = pLoggerInt->pOutputIf->pfnRename(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                                      szOldName, szNewName, RTFILEMOVE_FLAGS_REPLACE);
            while (   rc == VERR_SHARING_VIOLATION
                   && cBackoff < RT_ELEMENTS(g_acMsLogBackoff))
            {
                RTThreadSleep(g_acMsLogBackoff[cBackoff++]);
                rc = pLoggerInt->pOutputIf->pfnRename(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                                      szOldName, szNewName, RTFILEMOVE_FLAGS_REPLACE);
            }

            if (rc == VERR_FILE_NOT_FOUND)
                pLoggerInt->pOutputIf->pfnDelete(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                                 szNewName);
        }

        /*
         * Delete excess log files.
         */
        for (uint32_t i = cSavedHistory + 1; ; i++)
        {
            char szExcessName[sizeof(pLoggerInt->szFilename) + 32];
            RTStrPrintf(szExcessName, sizeof(szExcessName), "%s.%u", pLoggerInt->szFilename, i);
            int rc = pLoggerInt->pOutputIf->pfnDelete(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx,
                                                      szExcessName);
            if (RT_FAILURE(rc))
                break;
        }
    }

    /*
     * Update logger state and create new log file.
     */
    pLoggerInt->cbHistoryFileWritten = 0;
    pLoggerInt->uHistoryTimeSlotStart = uTimeSlot;
    rtlogFileOpen(pLoggerInt, pErrInfo);

    /*
     * Use the callback to generate some initial log contents, but only if this
     * is a rotation with a fully set up logger.  Leave the other case to the
     * RTLogCreateExV function.
     */
    if (pLoggerInt->pfnPhase && !fFirst)
    {
        uint32_t const fSavedDestFlags = pLoggerInt->fDestFlags;
        pLoggerInt->fDestFlags &= RTLOGDEST_FILE;
        pLoggerInt->pfnPhase(&pLoggerInt->Core, RTLOGPHASE_POSTROTATE, rtlogPhaseMsgLocked);
        pLoggerInt->fDestFlags = fSavedDestFlags;
    }

    /*
     * Close the context if we didn't get a log file, unless we're being
     * called by rtR3LogOpenFileDestination.
     */
    if (!pLoggerInt->fLogOpened && !fFirst)
    {
        pLoggerInt->pOutputIf->pfnDirCtxClose(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx);
        pLoggerInt->pvDirCtx = NULL;
    }

    /*
     * Restore saved values.
     */
    pLoggerInt->cHistory = cSavedHistory;
    pLoggerInt->fFlags   = fSavedFlags;
}


/**
 * Worker for RTLogCreateExV and RTLogClearFileDelayFlag.
 *
 * This will later be used to reopen the file by RTLogDestinations.
 *
 * @returns IPRT status code.
 * @param   pLoggerInt          The logger.
 * @param   pErrInfo            Where to return extended error information.
 *                              Optional.
 */
static int rtR3LogOpenFileDestination(PRTLOGGERINTERNAL pLoggerInt, PRTERRINFO pErrInfo)
{
    /*
     * Open a log directory context so the output backend can better secure
     * the log file opening, renaming and deleting. (See @bugref{10632}.)
     */
    Assert(!pLoggerInt->fLogOpened);
    int rc = pLoggerInt->pOutputIf->pfnDirCtxOpen(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser,
                                                  pLoggerInt->szFilename, &pLoggerInt->pvDirCtx);
    if (RT_SUCCESS(rc))
    {
        if (pLoggerInt->fFlags & RTLOGFLAGS_APPEND)
        {
            rc = rtlogFileOpen(pLoggerInt, pErrInfo);

            /* Rotate in case of appending to a too big log file,
               otherwise this simply doesn't do anything. */
            rtlogRotate(pLoggerInt, 0, true /* fFirst */, pErrInfo);
        }
        else
        {
            /* Force rotation if it is configured. */
            pLoggerInt->cbHistoryFileWritten = UINT64_MAX;
            rtlogRotate(pLoggerInt, 0, true /* fFirst */, pErrInfo);

            /* If the file is not open then rotation is not set up. */
            if (!pLoggerInt->fLogOpened)
            {
                pLoggerInt->cbHistoryFileWritten = 0;
                rc = rtlogFileOpen(pLoggerInt, pErrInfo);
            }
            else
                rc = VINF_SUCCESS;
        }

        /*
         * Close the directory context if we failed to get open a log file.
         * This gives the user a chance to make changes so it may succeed later.
         */
        if (!pLoggerInt->fLogOpened)
        {
            pLoggerInt->pOutputIf->pfnDirCtxClose(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser, pLoggerInt->pvDirCtx);
            pLoggerInt->pvDirCtx = NULL;
        }
    }
    return rc;
}

#endif /* IN_RING3 */


/*********************************************************************************************************************************
*   Bulk Reconfig & Logging for ring-0 EMT loggers.                                                                              *
*********************************************************************************************************************************/

RTDECL(int) RTLogBulkUpdate(PRTLOGGER pLogger, uint64_t fFlags, uint32_t uGroupCrc32, uint32_t cGroups, uint32_t const *pafGroups)
{
    int               rc;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Do the updating.
     */
    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        pLoggerInt->fFlags = fFlags;
        if (   uGroupCrc32 == rtLogCalcGroupNameCrc32(pLoggerInt)
            && pLoggerInt->cGroups == cGroups)
        {
            RT_BCOPY_UNFORTIFIED(pLoggerInt->afGroups, pafGroups, sizeof(pLoggerInt->afGroups[0]) * cGroups);
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_MISMATCH;

        rtlogUnlock(pLoggerInt);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLogBulkUpdate);


RTDECL(int) RTLogQueryBulk(PRTLOGGER pLogger, uint64_t *pfFlags, uint32_t *puGroupCrc32, uint32_t *pcGroups, uint32_t *pafGroups)
{
    PRTLOGGERINTERNAL pLoggerInt   = (PRTLOGGERINTERNAL)pLogger;
    uint32_t const    cGroupsAlloc = *pcGroups;

    *pfFlags      = 0;
    *puGroupCrc32 = 0;
    *pcGroups     = 0;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);
    AssertReturn(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC, VERR_INVALID_MAGIC);

    /*
     * Get the data.
     */
    *pfFlags  = pLoggerInt->fFlags;
    *pcGroups = pLoggerInt->cGroups;
    if (cGroupsAlloc >= pLoggerInt->cGroups)
    {
        memcpy(pafGroups, pLoggerInt->afGroups, sizeof(pLoggerInt->afGroups[0]) * pLoggerInt->cGroups);
        *puGroupCrc32 = rtLogCalcGroupNameCrc32(pLoggerInt);
        return VINF_SUCCESS;
    }
    return VERR_BUFFER_OVERFLOW;
}
RT_EXPORT_SYMBOL(RTLogQueryBulk);


RTDECL(int) RTLogBulkWrite(PRTLOGGER pLogger, const char *pszBefore, const char *pch, size_t cch, const char *pszAfter)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Lock and validate it.
     */
    int rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        if (cch > 0)
        {
            /*
             * Heading/marker.
             */
            if (pszBefore)
                rtlogLoggerExFLocked(pLoggerInt, RTLOGGRPFLAGS_LEVEL_1, UINT32_MAX, "%s", pszBefore);

            /*
             * Do the copying.
             */
            do
            {
                PRTLOGBUFFERDESC const  pBufDesc = pLoggerInt->pBufDesc;
                char * const            pchBuf   = pBufDesc->pchBuf;
                uint32_t const          cbBuf    = pBufDesc->cbBuf;
                uint32_t                offBuf   = pBufDesc->offBuf;
                if (cch + 1 < cbBuf - offBuf)
                {
                    memcpy(&pchBuf[offBuf], pch, cch);
                    offBuf += (uint32_t)cch;
                    pchBuf[offBuf] = '\0';
                    pBufDesc->offBuf = offBuf;
                    if (pBufDesc->pAux)
                        pBufDesc->pAux->offBuf = offBuf;
                    if (!(pLoggerInt->fDestFlags & RTLOGFLAGS_BUFFERED))
                        rtlogFlush(pLoggerInt, false /*fNeedSpace*/);
                    break;
                }

                /* Not enough space. */
                if (offBuf + 1 < cbBuf)
                {
                    uint32_t cbToCopy = cbBuf - offBuf - 1;
                    memcpy(&pchBuf[offBuf], pch, cbToCopy);
                    offBuf += cbToCopy;
                    pchBuf[offBuf] = '\0';
                    pBufDesc->offBuf = offBuf;
                    if (pBufDesc->pAux)
                        pBufDesc->pAux->offBuf = offBuf;
                    pch += cbToCopy;
                    cch -= cbToCopy;
                }

                rtlogFlush(pLoggerInt, false /*fNeedSpace*/);
            } while (cch > 0);

            /*
             * Footer/marker.
             */
            if (pszAfter)
                rtlogLoggerExFLocked(pLoggerInt, RTLOGGRPFLAGS_LEVEL_1, UINT32_MAX, "%s", pszAfter);
        }

        rtlogUnlock(pLoggerInt);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLogBulkWrite);


RTDECL(int) RTLogBulkNestedWrite(PRTLOGGER pLogger, const char *pch, size_t cch, const char *pszInfix)
{
    if (cch > 0)
    {
        PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
        RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

        /*
         * Lock and validate it.
         */
        int rc = rtlogLock(pLoggerInt);
        if (RT_SUCCESS(rc))
        {
            /*
             * If we've got an auxilary descriptor, check if the buffer was flushed.
             */
            PRTLOGBUFFERDESC    pBufDesc = pLoggerInt->pBufDesc;
            PRTLOGBUFFERAUXDESC pAuxDesc = pBufDesc->pAux;
            if (!pAuxDesc || !pAuxDesc->fFlushedIndicator)
            { /* likely, except maybe for ring-0 */ }
            else
            {
                pAuxDesc->fFlushedIndicator = false;
                pBufDesc->offBuf            = 0;
            }

            /*
             * Write the stuff.
             */
            RTLOGOUTPUTPREFIXEDARGS Args;
            Args.pLoggerInt = pLoggerInt;
            Args.fFlags     = 0;
            Args.iGroup     = ~0U;
            Args.pszInfix   = pszInfix;
            rtLogOutputPrefixed(&Args, pch, cch);
            rtLogOutputPrefixed(&Args, pch, 0); /* termination call */

            /*
             * Maybe flush the buffer and update the auxiliary descriptor if there is one.
             */
            pBufDesc = pLoggerInt->pBufDesc;  /* (the descriptor may have changed) */
            if (    !(pLoggerInt->fFlags & RTLOGFLAGS_BUFFERED)
                &&  pBufDesc->offBuf)
                rtlogFlush(pLoggerInt, false /*fNeedSpace*/);
            else
            {
                pAuxDesc = pBufDesc->pAux;
                if (pAuxDesc)
                    pAuxDesc->offBuf = pBufDesc->offBuf;
            }

            rtlogUnlock(pLoggerInt);
        }
        return rc;
    }
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTLogBulkNestedWrite);


/*********************************************************************************************************************************
*   Flushing                                                                                                                     *
*********************************************************************************************************************************/

RTDECL(int) RTLogFlush(PRTLOGGER pLogger)
{
    if (!pLogger)
    {
        pLogger = rtLogGetDefaultInstanceCommon(); /* Get it if it exists, do _not_ create one if it doesn't. */
        if (!pLogger)
            return VINF_LOG_NO_LOGGER;
    }
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    Assert(pLoggerInt->Core.u32Magic == RTLOGGER_MAGIC);
    AssertPtr(pLoggerInt->pBufDesc);
    Assert(pLoggerInt->pBufDesc->u32Magic == RTLOGBUFFERDESC_MAGIC);

    /*
     * Acquire logger instance sem.
     */
    int rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        /*
         * Any thing to flush?
         */
        if (   pLoggerInt->pBufDesc->offBuf > 0
            || (pLoggerInt->fDestFlags & RTLOGDEST_RINGBUF))
        {
            /*
             * Call worker.
             */
            rtlogFlush(pLoggerInt, false /*fNeedSpace*/);

            /*
             * Since this is an explicit flush call, the ring buffer content should
             * be flushed to the other destinations if active.
             */
            if (   (pLoggerInt->fDestFlags & RTLOGDEST_RINGBUF)
                && pLoggerInt->pszRingBuf /* paranoia */)
                rtLogRingBufFlush(pLoggerInt);
        }

        rtlogUnlock(pLoggerInt);
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTLogFlush);


/**
 * Writes the buffer to the given log device without checking for buffered
 * data or anything.
 *
 * Used by the RTLogFlush() function.
 *
 * @param   pLoggerInt  The logger instance to write to. NULL is not allowed!
 * @param   fNeedSpace  Set if the caller assumes space will be made available.
 */
static void rtlogFlush(PRTLOGGERINTERNAL pLoggerInt, bool fNeedSpace)
{
    PRTLOGBUFFERDESC    pBufDesc   = pLoggerInt->pBufDesc;
    uint32_t            cchToFlush = pBufDesc->offBuf;
    char *              pchToFlush = pBufDesc->pchBuf;
    uint32_t const      cbBuf      = pBufDesc->cbBuf;
    Assert(pBufDesc->u32Magic == RTLOGBUFFERDESC_MAGIC);

    NOREF(fNeedSpace);
    if (cchToFlush == 0)
        return; /* nothing to flush. */

    AssertPtrReturnVoid(pchToFlush);
    AssertReturnVoid(cbBuf > 0);
    AssertMsgStmt(cchToFlush < cbBuf, ("%#x vs %#x\n", cchToFlush, cbBuf), cchToFlush = cbBuf - 1);

    /*
     * If the ring buffer is active, the other destinations are only written
     * to when the ring buffer is flushed by RTLogFlush().
     */
    if (   (pLoggerInt->fDestFlags & RTLOGDEST_RINGBUF)
        && pLoggerInt->pszRingBuf /* paranoia */)
    {
        rtLogRingBufWrite(pLoggerInt, pchToFlush, cchToFlush);

        /* empty the buffer. */
        pBufDesc->offBuf = 0;
        *pchToFlush      = '\0';
    }
    /*
     * In file delay mode, we ignore flush requests except when we're full
     * and the caller really needs some scratch space to get work done.
     */
    else
#ifdef IN_RING3
         if (!(pLoggerInt->fDestFlags & RTLOGDEST_F_DELAY_FILE))
#endif
    {
        /* Make sure the string is terminated.  On Windows, RTLogWriteDebugger
           will get upset if it isn't. */
        pchToFlush[cchToFlush] = '\0';

        if (pLoggerInt->fDestFlags & RTLOGDEST_USER)
            RTLogWriteUser(pchToFlush, cchToFlush);

#if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        if (pLoggerInt->fDestFlags & RTLOGDEST_VMM)
            RTLogWriteVmm(pchToFlush, cchToFlush, false /*fReleaseLog*/);

        if (pLoggerInt->fDestFlags & RTLOGDEST_VMM_REL)
            RTLogWriteVmm(pchToFlush, cchToFlush, true /*fReleaseLog*/);
#endif

        if (pLoggerInt->fDestFlags & RTLOGDEST_DEBUGGER)
            RTLogWriteDebugger(pchToFlush, cchToFlush);

#ifdef IN_RING3
        if ((pLoggerInt->fDestFlags & (RTLOGDEST_FILE | RTLOGDEST_RINGBUF)) == RTLOGDEST_FILE)
        {
            if (pLoggerInt->fLogOpened)
            {
                pLoggerInt->pOutputIf->pfnWrite(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser,
                                                pchToFlush, cchToFlush, NULL /*pcbWritten*/);
                if (pLoggerInt->fFlags & RTLOGFLAGS_FLUSH)
                    pLoggerInt->pOutputIf->pfnFlush(pLoggerInt->pOutputIf, pLoggerInt->pvOutputIfUser);
            }
            if (pLoggerInt->cHistory)
                pLoggerInt->cbHistoryFileWritten += cchToFlush;
        }
#endif

        if (pLoggerInt->fDestFlags & RTLOGDEST_STDOUT)
            RTLogWriteStdOut(pchToFlush, cchToFlush);

        if (pLoggerInt->fDestFlags & RTLOGDEST_STDERR)
            RTLogWriteStdErr(pchToFlush, cchToFlush);

#if (defined(IN_RING0) || defined(IN_RC)) && !defined(LOG_NO_COM)
        if (pLoggerInt->fDestFlags & RTLOGDEST_COM)
            RTLogWriteCom(pchToFlush, cchToFlush);
#endif

        if (pLoggerInt->pfnFlush)
        {
            /*
             * We have a custom flush callback.  Before calling it we must make
             * sure the aux descriptor is up to date.  When we get back, we may
             * need to switch to the next buffer if the current is being flushed
             * asynchronously.  This of course requires there to be more than one
             * buffer.  (The custom flush callback is responsible for making sure
             * the next buffer isn't being flushed before returning.)
             */
            if (pBufDesc->pAux)
                pBufDesc->pAux->offBuf = cchToFlush;
            if (!pLoggerInt->pfnFlush(&pLoggerInt->Core, pBufDesc))
            {
                /* advance to the next buffer */
                Assert(pLoggerInt->cBufDescs > 1);
                size_t idxBufDesc = pBufDesc - pLoggerInt->paBufDescs;
                Assert(idxBufDesc < pLoggerInt->cBufDescs);
                idxBufDesc = (idxBufDesc + 1) % pLoggerInt->cBufDescs;
                pLoggerInt->idxBufDesc = (uint8_t)idxBufDesc;
                pLoggerInt->pBufDesc   = pBufDesc = &pLoggerInt->paBufDescs[idxBufDesc];
                pchToFlush = pBufDesc->pchBuf;
            }
        }

        /* Empty the buffer. */
        pBufDesc->offBuf = 0;
        if (pBufDesc->pAux)
            pBufDesc->pAux->offBuf = 0;
        *pchToFlush      = '\0';

#ifdef IN_RING3
        /*
         * Rotate the log file if configured.  Must be done after everything is
         * flushed, since this will also use logging/flushing to write the header
         * and footer messages.
         */
        if (   pLoggerInt->cHistory > 0
            && (pLoggerInt->fDestFlags & RTLOGDEST_FILE))
            rtlogRotate(pLoggerInt, RTTimeProgramSecTS() / pLoggerInt->cSecsHistoryTimeSlot, false /*fFirst*/, NULL /*pErrInfo*/);
#endif
    }
#ifdef IN_RING3
    else
    {
        /*
         * Delay file open but the caller really need some space.  So, give him half a
         * buffer and insert a message indicating that we've dropped output.
         */
        uint32_t offHalf = cbBuf / 2;
        if (cchToFlush > offHalf)
        {
            static const char s_szDropMsgLf[]   = "\n[DROP DROP DROP]\n";
            static const char s_szDropMsgCrLf[] = "\r\n[DROP DROP DROP]\r\n";
            if (!(pLoggerInt->fFlags & RTLOGFLAGS_USECRLF))
            {
                memcpy(&pchToFlush[offHalf], RT_STR_TUPLE(s_szDropMsgLf));
                offHalf += sizeof(s_szDropMsgLf) - 1;
            }
            else
            {
                memcpy(&pchToFlush[offHalf], RT_STR_TUPLE(s_szDropMsgCrLf));
                offHalf += sizeof(s_szDropMsgCrLf) - 1;
            }
            pBufDesc->offBuf = offHalf;
        }
    }
#endif
}


/*********************************************************************************************************************************
*   Logger Core                                                                                                                  *
*********************************************************************************************************************************/

#ifdef IN_RING0

/**
 * For rtR0LogLoggerExFallbackOutput and rtR0LogLoggerExFallbackFlush.
 */
typedef struct RTR0LOGLOGGERFALLBACK
{
    /** The current scratch buffer offset. */
    uint32_t            offScratch;
    /** The destination flags. */
    uint32_t            fDestFlags;
    /** For ring buffer output. */
    PRTLOGGERINTERNAL   pInt;
    /** The scratch buffer. */
    char                achScratch[80];
} RTR0LOGLOGGERFALLBACK;
/** Pointer to RTR0LOGLOGGERFALLBACK which is used by
 * rtR0LogLoggerExFallbackOutput. */
typedef RTR0LOGLOGGERFALLBACK *PRTR0LOGLOGGERFALLBACK;


/**
 * Flushes the fallback buffer.
 *
 * @param   pThis       The scratch buffer.
 */
static void rtR0LogLoggerExFallbackFlush(PRTR0LOGLOGGERFALLBACK pThis)
{
    if (!pThis->offScratch)
        return;

    if (   (pThis->fDestFlags & RTLOGDEST_RINGBUF)
        && pThis->pInt
        && pThis->pInt->pszRingBuf /* paranoia */)
        rtLogRingBufWrite(pThis->pInt, pThis->achScratch, pThis->offScratch);
    else
    {
        if (pThis->fDestFlags & RTLOGDEST_USER)
            RTLogWriteUser(pThis->achScratch, pThis->offScratch);

# if defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)
        if (pThis->fDestFlags & RTLOGDEST_VMM)
            RTLogWriteVmm(pThis->achScratch, pThis->offScratch, false /*fReleaseLog*/);

        if (pThis->fDestFlags & RTLOGDEST_VMM_REL)
            RTLogWriteVmm(pThis->achScratch, pThis->offScratch, true /*fReleaseLog*/);
# endif

        if (pThis->fDestFlags & RTLOGDEST_DEBUGGER)
            RTLogWriteDebugger(pThis->achScratch, pThis->offScratch);

        if (pThis->fDestFlags & RTLOGDEST_STDOUT)
            RTLogWriteStdOut(pThis->achScratch, pThis->offScratch);

        if (pThis->fDestFlags & RTLOGDEST_STDERR)
            RTLogWriteStdErr(pThis->achScratch, pThis->offScratch);

# ifndef LOG_NO_COM
        if (pThis->fDestFlags & RTLOGDEST_COM)
            RTLogWriteCom(pThis->achScratch, pThis->offScratch);
# endif
    }

    /* empty the buffer. */
    pThis->offScratch = 0;
}


/**
 * Callback for RTLogFormatV used by rtR0LogLoggerExFallback.
 * See PFNLOGOUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtR0LogLoggerExFallbackOutput(void *pv, const char *pachChars, size_t cbChars)
{
    PRTR0LOGLOGGERFALLBACK pThis = (PRTR0LOGLOGGERFALLBACK)pv;
    if (cbChars)
    {
        size_t cbRet = 0;
        for (;;)
        {
            /* how much */
            uint32_t cb = sizeof(pThis->achScratch) - pThis->offScratch - 1; /* minus 1 - for the string terminator. */
            if (cb > cbChars)
                cb = (uint32_t)cbChars;

            /* copy */
            memcpy(&pThis->achScratch[pThis->offScratch], pachChars, cb);

            /* advance */
            pThis->offScratch += cb;
            cbRet += cb;
            cbChars -= cb;

            /* done? */
            if (cbChars <= 0)
                return cbRet;

            pachChars += cb;

            /* flush */
            pThis->achScratch[pThis->offScratch] = '\0';
            rtR0LogLoggerExFallbackFlush(pThis);
        }

        /* won't ever get here! */
    }
    else
    {
        /*
         * Termination call, flush the log.
         */
        pThis->achScratch[pThis->offScratch] = '\0';
        rtR0LogLoggerExFallbackFlush(pThis);
        return 0;
    }
}


/**
 * Ring-0 fallback for cases where we're unable to grab the lock.
 *
 * This will happen when we're at a too high IRQL on Windows for instance and
 * needs to be dealt with or we'll drop a lot of log output. This fallback will
 * only output to some of the log destinations as a few of them may be doing
 * dangerous things. We won't be doing any prefixing here either, at least not
 * for the present, because it's too much hassle.
 *
 * @param   fDestFlags  The destination flags.
 * @param   fFlags      The logger flags.
 * @param   pInt        The internal logger data, for ring buffer output.
 * @param   pszFormat   The format string.
 * @param   va          The format arguments.
 */
static void rtR0LogLoggerExFallback(uint32_t fDestFlags, uint32_t fFlags, PRTLOGGERINTERNAL pInt,
                                    const char *pszFormat, va_list va)
{
    RTR0LOGLOGGERFALLBACK This;
    This.fDestFlags = fDestFlags;
    This.pInt = pInt;

    /* fallback indicator. */
    This.offScratch = 2;
    This.achScratch[0] = '[';
    This.achScratch[1] = 'F';

    /* selected prefixes */
    if (fFlags & RTLOGFLAGS_PREFIX_PID)
    {
        RTPROCESS Process = RTProcSelf();
        This.achScratch[This.offScratch++] = ' ';
        This.offScratch += RTStrFormatNumber(&This.achScratch[This.offScratch], Process, 16, sizeof(RTPROCESS) * 2, 0, RTSTR_F_ZEROPAD);
    }
    if (fFlags & RTLOGFLAGS_PREFIX_TID)
    {
        RTNATIVETHREAD Thread = RTThreadNativeSelf();
        This.achScratch[This.offScratch++] = ' ';
        This.offScratch += RTStrFormatNumber(&This.achScratch[This.offScratch], Thread, 16, sizeof(RTNATIVETHREAD) * 2, 0, RTSTR_F_ZEROPAD);
    }

    This.achScratch[This.offScratch++] = ']';
    This.achScratch[This.offScratch++] = ' ';

    RTLogFormatV(rtR0LogLoggerExFallbackOutput, &This, pszFormat, va);
}

#endif /* IN_RING0 */


/**
 * Callback for RTLogFormatV which writes to the com port.
 * See PFNLOGOUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogOutput(void *pv, const char *pachChars, size_t cbChars)
{
    PRTLOGGERINTERNAL pLoggerInt  = (PRTLOGGERINTERNAL)pv;
    if (cbChars)
    {
        size_t cbRet = 0;
        for (;;)
        {
            PRTLOGBUFFERDESC const pBufDesc = pLoggerInt->pBufDesc;
            if (pBufDesc->offBuf < pBufDesc->cbBuf)
            {
                /* how much */
                char    *pchBuf = pBufDesc->pchBuf;
                uint32_t offBuf = pBufDesc->offBuf;
                size_t   cb     = pBufDesc->cbBuf - offBuf - 1;
                if (cb > cbChars)
                    cb = cbChars;

                switch (cb)
                {
                    default:
                        memcpy(&pchBuf[offBuf], pachChars, cb);
                        pBufDesc->offBuf   = offBuf + (uint32_t)cb;
                        cbRet             += cb;
                        cbChars           -= cb;
                        if (cbChars <= 0)
                            return cbRet;
                        pachChars += cb;
                        break;

                    case 1:
                        pchBuf[offBuf]     = pachChars[0];
                        pBufDesc->offBuf   = offBuf + 1;
                        if (cbChars == 1)
                            return cbRet + 1;
                        cbChars   -= 1;
                        pachChars += 1;
                        break;

                    case 2:
                        pchBuf[offBuf]     = pachChars[0];
                        pchBuf[offBuf + 1] = pachChars[1];
                        pBufDesc->offBuf   = offBuf + 2;
                        if (cbChars == 2)
                            return cbRet + 2;
                        cbChars   -= 2;
                        pachChars += 2;
                        break;

                    case 3:
                        pchBuf[offBuf]     = pachChars[0];
                        pchBuf[offBuf + 1] = pachChars[1];
                        pchBuf[offBuf + 2] = pachChars[2];
                        pBufDesc->offBuf   = offBuf + 3;
                        if (cbChars == 3)
                            return cbRet + 3;
                        cbChars   -= 3;
                        pachChars += 3;
                        break;
                }

            }
#if defined(RT_STRICT) && defined(IN_RING3)
            else
            {
# ifndef IPRT_NO_CRT
                fprintf(stderr, "pBufDesc->offBuf >= pBufDesc->cbBuf (%#x >= %#x)\n", pBufDesc->offBuf, pBufDesc->cbBuf);
# else
                RTLogWriteStdErr(RT_STR_TUPLE("pBufDesc->offBuf >= pBufDesc->cbBuf\n"));
# endif
                AssertBreakpoint(); AssertBreakpoint();
            }
#endif

            /* flush */
            rtlogFlush(pLoggerInt, true /*fNeedSpace*/);
        }

        /* won't ever get here! */
    }
    else
    {
        /*
         * Termination call.
         * There's always space for a terminator, and it's not counted.
         */
        PRTLOGBUFFERDESC const pBufDesc = pLoggerInt->pBufDesc;
        pBufDesc->pchBuf[RT_MIN(pBufDesc->offBuf, pBufDesc->cbBuf - 1)] = '\0';
        return 0;
    }
}


/**
 * stpncpy implementation for use in rtLogOutputPrefixed w/ padding.
 *
 * @returns Pointer to the destination buffer byte following the copied string.
 * @param   pszDst              The destination buffer.
 * @param   pszSrc              The source string.
 * @param   cchSrcMax           The maximum number of characters to copy from
 *                              the string.
 * @param   cchMinWidth         The minimum field with, padd with spaces to
 *                              reach this.
 */
DECLINLINE(char *) rtLogStPNCpyPad(char *pszDst, const char *pszSrc, size_t cchSrcMax, size_t cchMinWidth)
{
    size_t cchSrc = 0;
    if (pszSrc)
    {
        cchSrc = strlen(pszSrc);
        if (cchSrc > cchSrcMax)
            cchSrc = cchSrcMax;

        memcpy(pszDst, pszSrc, cchSrc);
        pszDst += cchSrc;
    }
    do
        *pszDst++ = ' ';
    while (cchSrc++ < cchMinWidth);

    return pszDst;
}


/**
 * stpncpy implementation for use in rtLogOutputPrefixed w/ padding.
 *
 * @returns Pointer to the destination buffer byte following the copied string.
 * @param   pszDst              The destination buffer.
 * @param   pszSrc              The source string.
 * @param   cchSrc              The number of characters to copy from the
 *                              source.  Equal or less than string length.
 * @param   cchMinWidth         The minimum field with, padd with spaces to
 *                              reach this.
 */
DECLINLINE(char *) rtLogStPNCpyPad2(char *pszDst, const char *pszSrc, size_t cchSrc, size_t cchMinWidth)
{
    Assert(pszSrc);
    Assert(strlen(pszSrc) >= cchSrc);

    memcpy(pszDst, pszSrc, cchSrc);
    pszDst += cchSrc;
    do
        *pszDst++ = ' ';
    while (cchSrc++ < cchMinWidth);

    return pszDst;
}



/**
 * Callback for RTLogFormatV which writes to the logger instance.
 * This version supports prefixes.
 *
 * See PFNLOGOUTPUT() for details.
 */
static DECLCALLBACK(size_t) rtLogOutputPrefixed(void *pv, const char *pachChars, size_t cbChars)
{
    PRTLOGOUTPUTPREFIXEDARGS    pArgs      = (PRTLOGOUTPUTPREFIXEDARGS)pv;
    PRTLOGGERINTERNAL           pLoggerInt = pArgs->pLoggerInt;
    if (cbChars)
    {
        uint64_t const fFlags = pLoggerInt->fFlags;
        size_t         cbRet  = 0;
        for (;;)
        {
            PRTLOGBUFFERDESC const  pBufDesc = pLoggerInt->pBufDesc;
            char * const            pchBuf   = pBufDesc->pchBuf;
            uint32_t const          cbBuf    = pBufDesc->cbBuf;
            uint32_t                offBuf   = pBufDesc->offBuf;
            size_t                  cb       = cbBuf - offBuf - 1;
            const char             *pszNewLine;
            char                   *psz;

#if defined(RT_STRICT) && defined(IN_RING3)
            /* sanity */
            if (offBuf < cbBuf)
            { /* likely */ }
            else
            {
# ifndef IPRT_NO_CRT
                fprintf(stderr, "offBuf >= cbBuf (%#x >= %#x)\n", offBuf, cbBuf);
# else
                RTLogWriteStdErr(RT_STR_TUPLE("offBuf >= cbBuf\n"));
# endif
                AssertBreakpoint(); AssertBreakpoint();
            }
#endif

            /*
             * Pending prefix?
             */
            if (pLoggerInt->fPendingPrefix)
            {
                /*
                 * Flush the buffer if there isn't enough room for the maximum prefix config.
                 * Max is 265, add a couple of extra bytes.  See CCH_PREFIX check way below.
                 */
                if (cb >= 265 + 16)
                    pLoggerInt->fPendingPrefix = false;
                else
                {
                    rtlogFlush(pLoggerInt, true /*fNeedSpace*/);
                    continue;
                }

                /*
                 * Write the prefixes.
                 * psz is pointing to the current position.
                 */
                psz = &pchBuf[offBuf];
                if (fFlags & RTLOGFLAGS_PREFIX_TS)
                {
                    uint64_t     u64       = RTTimeNanoTS();
                    int          iBase     = 16;
                    unsigned int fStrFlags = RTSTR_F_ZEROPAD;
                    if (fFlags & RTLOGFLAGS_DECIMAL_TS)
                    {
                        iBase     = 10;
                        fStrFlags = 0;
                    }
                    if (fFlags & RTLOGFLAGS_REL_TS)
                    {
                        static volatile uint64_t s_u64LastTs;
                        uint64_t        u64DiffTs = u64 - s_u64LastTs;
                        s_u64LastTs = u64;
                        /* We could have been preempted just before reading of s_u64LastTs by
                         * another thread which wrote s_u64LastTs. In that case the difference
                         * is negative which we simply ignore. */
                        u64         = (int64_t)u64DiffTs < 0 ? 0 : u64DiffTs;
                    }
                    /* 1E15 nanoseconds = 11 days */
                    psz += RTStrFormatNumber(psz, u64, iBase, 16, 0, fStrFlags);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_01   0 + 17

                if (fFlags & RTLOGFLAGS_PREFIX_TSC)
                {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
                    uint64_t     u64       = ASMReadTSC();
#else
                    uint64_t     u64       = RTTimeNanoTS();
#endif
                    int          iBase     = 16;
                    unsigned int fStrFlags = RTSTR_F_ZEROPAD;
                    if (fFlags & RTLOGFLAGS_DECIMAL_TS)
                    {
                        iBase    = 10;
                        fStrFlags = 0;
                    }
                    if (fFlags & RTLOGFLAGS_REL_TS)
                    {
                        static volatile uint64_t s_u64LastTsc;
                        int64_t        i64DiffTsc = u64 - s_u64LastTsc;
                        s_u64LastTsc = u64;
                        /* We could have been preempted just before reading of s_u64LastTsc by
                         * another thread which wrote s_u64LastTsc. In that case the difference
                         * is negative which we simply ignore. */
                        u64          = i64DiffTsc < 0 ? 0 : i64DiffTsc;
                    }
                    /* 1E15 ticks at 4GHz = 69 hours */
                    psz += RTStrFormatNumber(psz, u64, iBase, 16, 0, fStrFlags);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_02   CCH_PREFIX_01 + 17

                if (fFlags & RTLOGFLAGS_PREFIX_MS_PROG)
                {
#ifndef IN_RING0
                    uint64_t u64 = RTTimeProgramMilliTS();
#else
                    uint64_t u64 = (RTTimeNanoTS() - pLoggerInt->nsR0ProgramStart) / RT_NS_1MS;
#endif
                    /* 1E8 milliseconds = 27 hours */
                    psz += RTStrFormatNumber(psz, u64, 10, 9, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_03   CCH_PREFIX_02 + 21

                if (fFlags & RTLOGFLAGS_PREFIX_TIME)
                {
#if defined(IN_RING3) || defined(IN_RING0)
                    RTTIMESPEC TimeSpec;
                    RTTIME Time;
                    RTTimeExplode(&Time, RTTimeNow(&TimeSpec));
                    psz += RTStrFormatNumber(psz, Time.u8Hour, 10, 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ':';
                    psz += RTStrFormatNumber(psz, Time.u8Minute, 10, 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ':';
                    psz += RTStrFormatNumber(psz, Time.u8Second, 10, 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = '.';
                    psz += RTStrFormatNumber(psz, Time.u32Nanosecond / 1000, 10, 6, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ' ';
#else
                    memset(psz, ' ', 16);
                    psz += 16;
#endif
                }
#define CCH_PREFIX_04   CCH_PREFIX_03 + (3+1+3+1+3+1+7+1)

                if (fFlags & RTLOGFLAGS_PREFIX_TIME_PROG)
                {

#ifndef IN_RING0
                    uint64_t u64 = RTTimeProgramMicroTS();
#else
                    uint64_t u64 = (RTTimeNanoTS() - pLoggerInt->nsR0ProgramStart) / RT_NS_1US;

#endif
                    psz += RTStrFormatNumber(psz, (uint32_t)(u64 / RT_US_1HOUR), 10, 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ':';
                    uint32_t u32 = (uint32_t)(u64 % RT_US_1HOUR);
                    psz += RTStrFormatNumber(psz, u32 / RT_US_1MIN, 10, 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ':';
                    u32 %= RT_US_1MIN;

                    psz += RTStrFormatNumber(psz, u32 / RT_US_1SEC, 10, 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = '.';
                    psz += RTStrFormatNumber(psz, u32 % RT_US_1SEC, 10, 6, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_05   CCH_PREFIX_04 + (9+1+2+1+2+1+6+1)

# if 0
                if (fFlags & RTLOGFLAGS_PREFIX_DATETIME)
                {
                    char szDate[32];
                    RTTIMESPEC Time;
                    RTTimeSpecToString(RTTimeNow(&Time), szDate, sizeof(szDate));
                    size_t cch = strlen(szDate);
                    memcpy(psz, szDate, cch);
                    psz += cch;
                    *psz++ = ' ';
                }
#  define CCH_PREFIX_06   CCH_PREFIX_05 + 32
# else
#  define CCH_PREFIX_06   CCH_PREFIX_05 + 0
# endif

                if (fFlags & RTLOGFLAGS_PREFIX_PID)
                {
                    RTPROCESS Process = RTProcSelf();
                    psz += RTStrFormatNumber(psz, Process, 16, sizeof(RTPROCESS) * 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_07   CCH_PREFIX_06 + 9

                if (fFlags & RTLOGFLAGS_PREFIX_TID)
                {
                    RTNATIVETHREAD Thread = RTThreadNativeSelf();
                    psz += RTStrFormatNumber(psz, Thread, 16, sizeof(RTNATIVETHREAD) * 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_08   CCH_PREFIX_07 + 17

                if (fFlags & RTLOGFLAGS_PREFIX_THREAD)
                {
#ifdef IN_RING3
                    const char *pszName = RTThreadSelfName();
#elif defined IN_RC
                    const char *pszName = "EMT-RC";
#else
                    const char *pszName = pLoggerInt->szR0ThreadName[0] ? pLoggerInt->szR0ThreadName : "R0";
#endif
                    psz = rtLogStPNCpyPad(psz, pszName, 16, 8);
                }
#define CCH_PREFIX_09   CCH_PREFIX_08 + 17

                if (fFlags & RTLOGFLAGS_PREFIX_CPUID)
                {
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
                    const uint8_t idCpu = ASMGetApicId();
#else
                    const RTCPUID idCpu = RTMpCpuId();
#endif
                    psz += RTStrFormatNumber(psz, idCpu, 16, sizeof(idCpu) * 2, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_10   CCH_PREFIX_09 + 17

                if (    (fFlags & RTLOGFLAGS_PREFIX_CUSTOM)
                    &&  pLoggerInt->pfnPrefix)
                {
                    psz += pLoggerInt->pfnPrefix(&pLoggerInt->Core, psz, 31, pLoggerInt->pvPrefixUserArg);
                    *psz++ = ' ';                                                               /* +32 */
                }
#define CCH_PREFIX_11   CCH_PREFIX_10 + 32

                if (fFlags & RTLOGFLAGS_PREFIX_LOCK_COUNTS)
                {
#ifdef IN_RING3 /** @todo implement these counters in ring-0 too? */
                    RTTHREAD Thread = RTThreadSelf();
                    if (Thread != NIL_RTTHREAD)
                    {
                        uint32_t cReadLocks  = RTLockValidatorReadLockGetCount(Thread);
                        uint32_t cWriteLocks = RTLockValidatorWriteLockGetCount(Thread) - g_cLoggerLockCount;
                        cReadLocks  = RT_MIN(0xfff, cReadLocks);
                        cWriteLocks = RT_MIN(0xfff, cWriteLocks);
                        psz += RTStrFormatNumber(psz, cReadLocks,  16, 1, 0, RTSTR_F_ZEROPAD);
                        *psz++ = '/';
                        psz += RTStrFormatNumber(psz, cWriteLocks, 16, 1, 0, RTSTR_F_ZEROPAD);
                    }
                    else
#endif
                    {
                        *psz++ = '?';
                        *psz++ = '/';
                        *psz++ = '?';
                    }
                    *psz++ = ' ';
                }
#define CCH_PREFIX_12   CCH_PREFIX_11 + 8

                if (fFlags & RTLOGFLAGS_PREFIX_FLAG_NO)
                {
                    psz += RTStrFormatNumber(psz, pArgs->fFlags, 16, 8, 0, RTSTR_F_ZEROPAD);
                    *psz++ = ' ';
                }
#define CCH_PREFIX_13   CCH_PREFIX_12 + 9

                if (fFlags & RTLOGFLAGS_PREFIX_FLAG)
                {
#ifdef IN_RING3
                    const char *pszGroup = pArgs->iGroup != ~0U ? pLoggerInt->papszGroups[pArgs->iGroup] : NULL;
#else
                    const char *pszGroup = NULL;
#endif
                    psz = rtLogStPNCpyPad(psz, pszGroup, 16, 8);
                }
#define CCH_PREFIX_14   CCH_PREFIX_13 + 17

                if (fFlags & RTLOGFLAGS_PREFIX_GROUP_NO)
                {
                    if (pArgs->iGroup != ~0U)
                    {
                        psz += RTStrFormatNumber(psz, pArgs->iGroup, 16, 3, 0, RTSTR_F_ZEROPAD);
                        *psz++ = ' ';
                    }
                    else
                    {
                        memcpy(psz, "-1  ", sizeof("-1  ") - 1);
                        psz += sizeof("-1  ") - 1;
                    }                                                                           /* +9 */
                }
#define CCH_PREFIX_15   CCH_PREFIX_14 + 9

                if (fFlags & RTLOGFLAGS_PREFIX_GROUP)
                {
                    const unsigned fGrp = pLoggerInt->afGroups[pArgs->iGroup != ~0U ? pArgs->iGroup : 0];
                    const char *pszGroup;
                    size_t cchGroup;
                    switch (pArgs->fFlags & fGrp)
                    {
                        case 0:                         pszGroup = "--------";  cchGroup = sizeof("--------") - 1; break;
                        case RTLOGGRPFLAGS_ENABLED:     pszGroup = "enabled" ;  cchGroup = sizeof("enabled" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_1:     pszGroup = "level 1" ;  cchGroup = sizeof("level 1" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_2:     pszGroup = "level 2" ;  cchGroup = sizeof("level 2" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_3:     pszGroup = "level 3" ;  cchGroup = sizeof("level 3" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_4:     pszGroup = "level 4" ;  cchGroup = sizeof("level 4" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_5:     pszGroup = "level 5" ;  cchGroup = sizeof("level 5" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_6:     pszGroup = "level 6" ;  cchGroup = sizeof("level 6" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_7:     pszGroup = "level 7" ;  cchGroup = sizeof("level 7" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_8:     pszGroup = "level 8" ;  cchGroup = sizeof("level 8" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_9:     pszGroup = "level 9" ;  cchGroup = sizeof("level 9" ) - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_10:    pszGroup = "level 10";  cchGroup = sizeof("level 10") - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_11:    pszGroup = "level 11";  cchGroup = sizeof("level 11") - 1; break;
                        case RTLOGGRPFLAGS_LEVEL_12:    pszGroup = "level 12";  cchGroup = sizeof("level 12") - 1; break;
                        case RTLOGGRPFLAGS_FLOW:        pszGroup = "flow"    ;  cchGroup = sizeof("flow"    ) - 1; break;
                        case RTLOGGRPFLAGS_WARN:        pszGroup = "warn"    ;  cchGroup = sizeof("warn"    ) - 1; break;
                        default:                        pszGroup = "????????";  cchGroup = sizeof("????????") - 1; break;
                    }
                    psz = rtLogStPNCpyPad2(psz, pszGroup, RT_MIN(cchGroup, 16), 8);
                }
#define CCH_PREFIX_16   CCH_PREFIX_15 + 17

                if (pArgs->pszInfix)
                {
                    size_t cchInfix = strlen(pArgs->pszInfix);
                    psz = rtLogStPNCpyPad2(psz, pArgs->pszInfix, RT_MIN(cchInfix, 8), 1);
                }
#define CCH_PREFIX_17   CCH_PREFIX_16 + 9


#define CCH_PREFIX      ( CCH_PREFIX_17 )
                { AssertCompile(CCH_PREFIX < 265); }

                /*
                 * Done, figure what we've used and advance the buffer and free size.
                 */
                AssertMsg(psz - &pchBuf[offBuf] <= 223,
                          ("%#zx (%zd) - fFlags=%#x\n", psz - &pchBuf[offBuf], psz - &pchBuf[offBuf], fFlags));
                pBufDesc->offBuf = offBuf = (uint32_t)(psz - pchBuf);
                cb = cbBuf - offBuf - 1;
            }
            else if (cb <= 2) /* 2 - Make sure we can write a \r\n and not loop forever. */
            {
                rtlogFlush(pLoggerInt, true /*fNeedSpace*/);
                continue;
            }

            /*
             * Done with the prefixing. Copy message text past the next newline.
             */

            /* how much */
            if (cb > cbChars)
                cb = cbChars;

            /* have newline? */
            pszNewLine = (const char *)memchr(pachChars, '\n', cb);
            if (pszNewLine)
            {
                cb = pszNewLine - pachChars;
                if (!(fFlags & RTLOGFLAGS_USECRLF))
                {
                    cb += 1;
                    memcpy(&pchBuf[offBuf], pachChars, cb);
                    pLoggerInt->fPendingPrefix = true;
                }
                else if (cb + 2U < cbBuf - offBuf)
                {
                    memcpy(&pchBuf[offBuf], pachChars, cb);
                    pchBuf[offBuf + cb++] = '\r';
                    pchBuf[offBuf + cb++] = '\n';
                    cbChars++;      /* Discount the extra '\r'. */
                    pachChars--;    /* Ditto. */
                    cbRet--;        /* Ditto. */
                    pLoggerInt->fPendingPrefix = true;
                }
                else
                {
                    /* Insufficient buffer space, leave the '\n' for the next iteration. */
                    memcpy(&pchBuf[offBuf], pachChars, cb);
                }
            }
            else
                memcpy(&pchBuf[offBuf], pachChars, cb);

            /* advance */
            pBufDesc->offBuf = offBuf += (uint32_t)cb;
            cbRet   += cb;
            cbChars -= cb;

            /* done? */
            if (cbChars <= 0)
                return cbRet;
            pachChars += cb;
        }

        /* won't ever get here! */
    }
    else
    {
        /*
         * Termination call.
         * There's always space for a terminator, and it's not counted.
         */
        PRTLOGBUFFERDESC const pBufDesc = pLoggerInt->pBufDesc;
        pBufDesc->pchBuf[RT_MIN(pBufDesc->offBuf, pBufDesc->cbBuf - 1)] = '\0';
        return 0;
    }
}


/**
 * Write to a logger instance (worker function).
 *
 * This function will check whether the instance, group and flags makes up a
 * logging kind which is currently enabled before writing anything to the log.
 *
 * @param   pLoggerInt  Pointer to logger instance. Must be non-NULL.
 * @param   fFlags      The logging flags.
 * @param   iGroup      The group.
 *                      The value ~0U is reserved for compatibility with RTLogLogger[V] and is
 *                      only for internal usage!
 * @param   pszFormat   Format string.
 * @param   args        Format arguments.
 */
static void rtlogLoggerExVLocked(PRTLOGGERINTERNAL pLoggerInt, unsigned fFlags, unsigned iGroup,
                                 const char *pszFormat, va_list args)
{
    /*
     * If we've got an auxilary descriptor, check if the buffer was flushed.
     */
    PRTLOGBUFFERDESC    pBufDesc = pLoggerInt->pBufDesc;
    PRTLOGBUFFERAUXDESC pAuxDesc = pBufDesc->pAux;
    if (!pAuxDesc || !pAuxDesc->fFlushedIndicator)
    { /* likely, except maybe for ring-0 */ }
    else
    {
        pAuxDesc->fFlushedIndicator = false;
        pBufDesc->offBuf            = 0;
    }

    /*
     * Format the message.
     */
    if (pLoggerInt->fFlags & (RTLOGFLAGS_PREFIX_MASK | RTLOGFLAGS_USECRLF))
    {
        RTLOGOUTPUTPREFIXEDARGS OutputArgs;
        OutputArgs.pLoggerInt = pLoggerInt;
        OutputArgs.iGroup     = iGroup;
        OutputArgs.fFlags     = fFlags;
        OutputArgs.pszInfix   = NULL;
        RTLogFormatV(rtLogOutputPrefixed, &OutputArgs, pszFormat, args);
    }
    else
        RTLogFormatV(rtLogOutput, pLoggerInt, pszFormat, args);

    /*
     * Maybe flush the buffer and update the auxiliary descriptor if there is one.
     */
    pBufDesc = pLoggerInt->pBufDesc;  /* (the descriptor may have changed) */
    if (    !(pLoggerInt->fFlags & RTLOGFLAGS_BUFFERED)
        &&  pBufDesc->offBuf)
        rtlogFlush(pLoggerInt, false /*fNeedSpace*/);
    else
    {
        pAuxDesc = pBufDesc->pAux;
        if (pAuxDesc)
            pAuxDesc->offBuf = pBufDesc->offBuf;
    }
}


/**
 * For calling rtlogLoggerExVLocked.
 *
 * @param   pLoggerInt  The logger.
 * @param   fFlags      The logging flags.
 * @param   iGroup      The group.
 *                      The value ~0U is reserved for compatibility with RTLogLogger[V] and is
 *                      only for internal usage!
 * @param   pszFormat   Format string.
 * @param   ...         Format arguments.
 */
static void rtlogLoggerExFLocked(PRTLOGGERINTERNAL pLoggerInt, unsigned fFlags, unsigned iGroup, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    rtlogLoggerExVLocked(pLoggerInt, fFlags, iGroup, pszFormat, va);
    va_end(va);
}


RTDECL(int) RTLogLoggerExV(PRTLOGGER pLogger, unsigned fFlags, unsigned iGroup, const char *pszFormat, va_list args)
{
    int               rc;
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    RTLOG_RESOLVE_DEFAULT_RET(pLoggerInt, VINF_LOG_NO_LOGGER);

    /*
     * Validate and correct iGroup.
     */
    if (iGroup != ~0U && iGroup >= pLoggerInt->cGroups)
        iGroup = 0;

    /*
     * If no output, then just skip it.
     */
    if (    (pLoggerInt->fFlags & RTLOGFLAGS_DISABLED)
        || !pLoggerInt->fDestFlags
        || !pszFormat || !*pszFormat)
        return VINF_LOG_DISABLED;
    if (    iGroup != ~0U
        &&  (pLoggerInt->afGroups[iGroup] & (fFlags | RTLOGGRPFLAGS_ENABLED)) != (fFlags | RTLOGGRPFLAGS_ENABLED))
        return VINF_LOG_DISABLED;

    /*
     * Acquire logger instance sem.
     */
    rc = rtlogLock(pLoggerInt);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check group restrictions and call worker.
         */
        if (RT_LIKELY(   !(pLoggerInt->fFlags & RTLOGFLAGS_RESTRICT_GROUPS)
                      || iGroup >= pLoggerInt->cGroups
                      || !(pLoggerInt->afGroups[iGroup] & RTLOGGRPFLAGS_RESTRICT)
                      || ++pLoggerInt->pacEntriesPerGroup[iGroup] < pLoggerInt->cMaxEntriesPerGroup ))
            rtlogLoggerExVLocked(pLoggerInt, fFlags, iGroup, pszFormat, args);
        else
        {
            uint32_t cEntries = pLoggerInt->pacEntriesPerGroup[iGroup];
            if (cEntries > pLoggerInt->cMaxEntriesPerGroup)
                pLoggerInt->pacEntriesPerGroup[iGroup] = cEntries - 1;
            else
            {
                rtlogLoggerExVLocked(pLoggerInt, fFlags, iGroup, pszFormat, args);
                if (   pLoggerInt->papszGroups
                    && pLoggerInt->papszGroups[iGroup])
                    rtlogLoggerExFLocked(pLoggerInt, fFlags, iGroup, "%u messages from group %s (#%u), muting it.\n",
                                         cEntries, pLoggerInt->papszGroups[iGroup], iGroup);
                else
                    rtlogLoggerExFLocked(pLoggerInt, fFlags, iGroup, "%u messages from group #%u, muting it.\n", cEntries, iGroup);
            }
        }

        /*
         * Release the semaphore.
         */
        rtlogUnlock(pLoggerInt);
        return VINF_SUCCESS;
    }

#ifdef IN_RING0
    if (pLoggerInt->fDestFlags & ~RTLOGDEST_FILE)
    {
        rtR0LogLoggerExFallback(pLoggerInt->fDestFlags, pLoggerInt->fFlags, pLoggerInt, pszFormat, args);
        return VINF_SUCCESS;
    }
#endif
    return rc;
}
RT_EXPORT_SYMBOL(RTLogLoggerExV);


RTDECL(void) RTLogLoggerV(PRTLOGGER pLogger, const char *pszFormat, va_list args)
{
    RTLogLoggerExV(pLogger, 0, ~0U, pszFormat, args);
}
RT_EXPORT_SYMBOL(RTLogLoggerV);


RTDECL(void) RTLogPrintfV(const char *pszFormat, va_list va)
{
    RTLogLoggerV(NULL, pszFormat, va);
}
RT_EXPORT_SYMBOL(RTLogPrintfV);


RTDECL(void) RTLogDumpPrintfV(void *pvUser, const char *pszFormat, va_list va)
{
    RTLogLoggerV((PRTLOGGER)pvUser, pszFormat, va);
}
RT_EXPORT_SYMBOL(RTLogDumpPrintfV);


RTDECL(void) RTLogAssert(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    RTLogAssertV(pszFormat,va);
    va_end(va);
}


RTDECL(void) RTLogAssertV(const char *pszFormat, va_list va)
{
    /*
     * To the release log if we got one.
     */
    PRTLOGGER pLogger = RTLogRelGetDefaultInstance();
    if (pLogger)
    {
        va_list vaCopy;
        va_copy(vaCopy, va);
        RTLogLoggerExV(pLogger, 0 /*fFlags*/, ~0U /*uGroup*/, pszFormat, vaCopy);
        va_end(vaCopy);
#ifndef IN_RC
        RTLogFlush(pLogger);
#endif
    }

    /*
     * To the debug log if we got one, however when LOG_ENABLE (debug builds and
     * such) we'll allow it to be created here.
     */
#ifdef LOG_ENABLED
    pLogger = RTLogDefaultInstance();
#else
    pLogger = RTLogGetDefaultInstance();
#endif
    if (pLogger)
    {
        RTLogLoggerExV(pLogger, 0 /*fFlags*/, ~0U /*uGroup*/, pszFormat, va);
# ifndef IN_RC /* flushing is done automatically in RC */
        RTLogFlush(pLogger);
#endif
    }
}


#if defined(IN_RING3) && (defined(IN_RT_STATIC) || defined(IPRT_NO_CRT))
/**
 * "Weak symbol" emulation to prevent dragging in log.cpp and all its friends
 * just because some code is using Assert() in a statically linked binary.
 *
 * The pointers are in log-assert-pfn.cpp, so users only drag in that file and
 * they remain NULL unless this file is also linked into the binary.
 */
class RTLogAssertWeakSymbolEmulator
{
public:
    RTLogAssertWeakSymbolEmulator(void)
    {
        g_pfnRTLogAssert  = RTLogAssert;
        g_pfnRTLogAssertV = RTLogAssertV;
    }
};
static RTLogAssertWeakSymbolEmulator rtLogInitWeakSymbolPointers;
#endif


#ifdef IN_RING3

/**
 * @callback_method_impl{FNRTLOGPHASEMSG,
 * Log phase callback function - assumes the lock is already held.}
 */
static DECLCALLBACK(void) rtlogPhaseMsgLocked(PRTLOGGER pLogger, const char *pszFormat, ...)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    AssertPtrReturnVoid(pLoggerInt);
    Assert(pLoggerInt->hSpinMtx != NIL_RTSEMSPINMUTEX);

    va_list args;
    va_start(args, pszFormat);
    rtlogLoggerExVLocked(pLoggerInt, 0, ~0U, pszFormat, args);
    va_end(args);
}


/**
 * @callback_method_impl{FNRTLOGPHASEMSG,
 * Log phase callback function - assumes the lock is not held.}
 */
static DECLCALLBACK(void) rtlogPhaseMsgNormal(PRTLOGGER pLogger, const char *pszFormat, ...)
{
    PRTLOGGERINTERNAL pLoggerInt = (PRTLOGGERINTERNAL)pLogger;
    AssertPtrReturnVoid(pLoggerInt);
    Assert(pLoggerInt->hSpinMtx != NIL_RTSEMSPINMUTEX);

    va_list args;
    va_start(args, pszFormat);
    RTLogLoggerExV(&pLoggerInt->Core, 0, ~0U, pszFormat, args);
    va_end(args);
}

#endif /* IN_RING3 */

