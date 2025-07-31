/* $Id: VBoxDTraceR0.cpp 110494 2025-07-31 10:11:09Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxDTraceR0.
 *
 * Contributed by: bird
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from http://www.virtualbox.org.
 *
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License Version 1.0 (CDDL) only, as it
 * comes in the "COPYING.CDDL" file of the VirtualBox distribution.
 *
 * SPDX-License-Identifier: CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/sup.h>
#include <VBox/log.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <sys/dtrace_impl.h>

#include <VBox/VBoxTpG.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
//#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
//# define HAVE_RTMEMALLOCEX_FEATURES
//#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Caller indicator. */
typedef enum VBOXDTCALLER
{
    kVBoxDtCaller_Invalid = 0,
    kVBoxDtCaller_Generic,
    kVBoxDtCaller_ProbeFireUser,
    kVBoxDtCaller_ProbeFireKernel
} VBOXDTCALLER;

/**
 * Stack data used for thread structure and such.
 *
 * This is planted in every external entry point and used to emulate solaris
 * curthread, CRED, curproc and similar.  It is also used to get at the
 * uncached probe arguments.
 */
typedef struct VBoxDtStackData
{
    /** Eyecatcher no. 1 (VBDT_STACK_DATA_MAGIC2). */
    uint32_t                u32Magic1;
    /** Eyecatcher no. 2 (VBDT_STACK_DATA_MAGIC2). */
    uint32_t                u32Magic2;
    /** The format of the caller specific data. */
    VBOXDTCALLER            enmCaller;
    /** Caller specific data.  */
    union
    {
        /** kVBoxDtCaller_ProbeFireKernel. */
        struct
        {
            /** The caller. */
            uintptr_t               uCaller;
            /** Pointer to the stack arguments of a probe function call. */
            uintptr_t              *pauStackArgs;
        } ProbeFireKernel;
        /** kVBoxDtCaller_ProbeFireUser. */
        struct
        {
            /** The user context.  */
            PCSUPDRVTRACERUSRCTX    pCtx;
            /** The argument displacement caused by 64-bit arguments passed directly to
             *  dtrace_probe. */
            int                     offArg;
        } ProbeFireUser;
    } u;
    /** Credentials allocated by VBoxDtGetCurrentCreds. */
    struct VBoxDtCred      *pCred;
    /** Thread structure currently being held by this thread. */
    struct VBoxDtThread    *pThread;
    /** Pointer to this structure.
     * This is the final bit of integrity checking. */
    struct VBoxDtStackData *pSelf;
} VBDTSTACKDATA;
/** Pointer to the on-stack thread specific data. */
typedef VBDTSTACKDATA *PVBDTSTACKDATA;

/** The first magic value. */
#define VBDT_STACK_DATA_MAGIC1      RT_MAKE_U32_FROM_U8('V', 'B', 'o', 'x')
/** The second magic value. */
#define VBDT_STACK_DATA_MAGIC2      RT_MAKE_U32_FROM_U8('D', 'T', 'r', 'c')

/** The alignment of the stack data.
 * The data doesn't require more than sizeof(uintptr_t) alignment, but the
 * greater alignment the quicker lookup. */
#define VBDT_STACK_DATA_ALIGN       32

/** Plants the stack data. */
#define VBDT_SETUP_STACK_DATA(a_enmCaller) \
    uint8_t abBlob[sizeof(VBDTSTACKDATA) + VBDT_STACK_DATA_ALIGN - 1]; \
    PVBDTSTACKDATA pStackData = (PVBDTSTACKDATA)(    (uintptr_t)&abBlob[VBDT_STACK_DATA_ALIGN - 1] \
                                                 &  ~(uintptr_t)(VBDT_STACK_DATA_ALIGN - 1)); \
    pStackData->u32Magic1   = VBDT_STACK_DATA_MAGIC1; \
    pStackData->u32Magic2   = VBDT_STACK_DATA_MAGIC2; \
    pStackData->enmCaller   = a_enmCaller; \
    pStackData->pCred       = NULL; \
    pStackData->pThread     = NULL; \
    pStackData->pSelf       = pStackData

/** Passifies the stack data and frees up resource held within it. */
#define VBDT_CLEAR_STACK_DATA() \
    do \
    { \
        pStackData->u32Magic1   = 0; \
        pStackData->u32Magic2   = 0; \
        pStackData->pSelf       = NULL; \
        if (pStackData->pCred) \
            crfree(pStackData->pCred); \
        if (pStackData->pThread) \
            VBoxDtReleaseThread(pStackData->pThread); \
    } while (0)


/** Simple SUPR0Printf-style logging.  */
#if 0 /*def DEBUG_bird*/
# define LOG_DTRACE(a) SUPR0Printf a
#else
# define LOG_DTRACE(a) do { } while (0)
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Per CPU information */
cpucore_t                       g_aVBoxDtCpuCores[RTCPUSET_MAX_CPUS];
/** Dummy mutex. */
struct VBoxDtMutex              g_DummyMtx;
/** Pointer to the tracer helpers provided by VBoxDrv. */
static PCSUPDRVTRACERHLP        g_pVBoxDTraceHlp;

dtrace_cacheid_t dtrace_predcache_id = DTRACE_CACHEIDNONE + 1;

#if 0
void           (*dtrace_cpu_init)(processorid_t);
void           (*dtrace_modload)(struct modctl *);
void           (*dtrace_modunload)(struct modctl *);
void           (*dtrace_helpers_cleanup)(void);
void           (*dtrace_helpers_fork)(proc_t *, proc_t *);
void           (*dtrace_cpustart_init)(void);
void           (*dtrace_cpustart_fini)(void);
void           (*dtrace_cpc_fire)(uint64_t);
void           (*dtrace_debugger_init)(void);
void           (*dtrace_debugger_fini)(void);
#endif


/**
 * Gets the stack data.
 *
 * @returns Pointer to the stack data.  Never NULL.
 */
static PVBDTSTACKDATA vboxDtGetStackData(void)
{
    int volatile    iDummy = 1; /* use this to get the stack address. */
    PVBDTSTACKDATA  pData = (PVBDTSTACKDATA)(  ((uintptr_t)&iDummy + VBDT_STACK_DATA_ALIGN - 1)
                                             & ~(uintptr_t)(VBDT_STACK_DATA_ALIGN - 1));
    for (;;)
    {
        if (   pData->u32Magic1 == VBDT_STACK_DATA_MAGIC1
            && pData->u32Magic2 == VBDT_STACK_DATA_MAGIC2
            && pData->pSelf     == pData)
            return pData;
        pData = (PVBDTSTACKDATA)((uintptr_t)pData + VBDT_STACK_DATA_ALIGN);
    }
}


void dtrace_toxic_ranges(void (*pfnAddOne)(uintptr_t uBase, uintptr_t cbRange))
{
    /** @todo ? */
    RT_NOREF_PV(pfnAddOne);
}



/**
 * Dummy callback used by dtrace_sync.
 */
static DECLCALLBACK(void) vboxDtSyncCallback(RTCPUID idCpu, void *pvUser1, void *pvUser2)
{
    NOREF(idCpu); NOREF(pvUser1); NOREF(pvUser2);
}


/**
 * Synchronzie across all CPUs (expensive).
 */
void    dtrace_sync(void)
{
    int rc = RTMpOnAll(vboxDtSyncCallback, NULL, NULL);
    AssertRC(rc);
}


/**
 * Fetch a 8-bit "word" from userland.
 *
 * @return  The byte value.
 * @param   pvUserAddr      The userland address.
 */
uint8_t  dtrace_fuword8( void *pvUserAddr)
{
    uint8_t u8;
    int rc = RTR0MemUserCopyFrom(&u8, (uintptr_t)pvUserAddr, sizeof(u8));
    if (RT_FAILURE(rc))
    {
        RTCPUID iCpu = VBDT_GET_CPUID();
        cpu_core[iCpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
        cpu_core[iCpu].cpuc_dtrace_illval = (uintptr_t)pvUserAddr;
        u8 = 0;
    }
    return u8;
}


/**
 * Fetch a 16-bit word from userland.
 *
 * @return  The word value.
 * @param   pvUserAddr      The userland address.
 */
uint16_t dtrace_fuword16(void *pvUserAddr)
{
    uint16_t u16;
    int rc = RTR0MemUserCopyFrom(&u16, (uintptr_t)pvUserAddr, sizeof(u16));
    if (RT_FAILURE(rc))
    {
        RTCPUID iCpu = VBDT_GET_CPUID();
        cpu_core[iCpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
        cpu_core[iCpu].cpuc_dtrace_illval = (uintptr_t)pvUserAddr;
        u16 = 0;
    }
    return u16;
}


/**
 * Fetch a 32-bit word from userland.
 *
 * @return  The dword value.
 * @param   pvUserAddr      The userland address.
 */
uint32_t dtrace_fuword32(void *pvUserAddr)
{
    uint32_t u32;
    int rc = RTR0MemUserCopyFrom(&u32, (uintptr_t)pvUserAddr, sizeof(u32));
    if (RT_FAILURE(rc))
    {
        RTCPUID iCpu = VBDT_GET_CPUID();
        cpu_core[iCpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
        cpu_core[iCpu].cpuc_dtrace_illval = (uintptr_t)pvUserAddr;
        u32 = 0;
    }
    return u32;
}


/**
 * Fetch a 64-bit word from userland.
 *
 * @return  The qword value.
 * @param   pvUserAddr      The userland address.
 */
uint64_t dtrace_fuword64(void *pvUserAddr)
{
    uint64_t u64;
    int rc = RTR0MemUserCopyFrom(&u64, (uintptr_t)pvUserAddr, sizeof(u64));
    if (RT_FAILURE(rc))
    {
        RTCPUID iCpu = VBDT_GET_CPUID();
        cpu_core[iCpu].cpuc_dtrace_flags |= CPU_DTRACE_BADADDR;
        cpu_core[iCpu].cpuc_dtrace_illval = (uintptr_t)pvUserAddr;
        u64 = 0;
    }
    return u64;
}


/** copyin implementation */
int  VBoxDtCopyIn(void const *pvUser, void *pvDst, size_t cb)
{
    int rc = RTR0MemUserCopyFrom(pvDst, (uintptr_t)pvUser, cb);
    return RT_SUCCESS(rc) ? 0 : -1;
}


/** copyout implementation */
int  VBoxDtCopyOut(void const *pvSrc, void *pvUser, size_t cb)
{
    int rc = RTR0MemUserCopyTo((uintptr_t)pvUser, pvSrc, cb);
    return RT_SUCCESS(rc) ? 0 : -1;
}


/**
 * Copy data from userland into the kernel.
 *
 * @param   uUserAddr           The userland address.
 * @param   uKrnlAddr           The kernel buffer address.
 * @param   cb                  The number of bytes to copy.
 * @param   pfFlags             Pointer to the relevant cpuc_dtrace_flags.
 */
void dtrace_copyin(    uintptr_t uUserAddr, uintptr_t uKrnlAddr, size_t cb, volatile uint16_t *pfFlags)
{
    int rc = RTR0MemUserCopyFrom((void *)uKrnlAddr, uUserAddr, cb);
    if (RT_FAILURE(rc))
    {
        *pfFlags |= CPU_DTRACE_BADADDR;
        cpu_core[VBDT_GET_CPUID()].cpuc_dtrace_illval = uUserAddr;
    }
}


/**
 * Copy data from the kernel into userland.
 *
 * @param   uKrnlAddr           The kernel buffer address.
 * @param   uUserAddr           The userland address.
 * @param   cb                  The number of bytes to copy.
 * @param   pfFlags             Pointer to the relevant cpuc_dtrace_flags.
 */
void dtrace_copyout(   uintptr_t uKrnlAddr, uintptr_t uUserAddr, size_t cb, volatile uint16_t *pfFlags)
{
    int rc = RTR0MemUserCopyTo(uUserAddr, (void const *)uKrnlAddr, cb);
    if (RT_FAILURE(rc))
    {
        *pfFlags |= CPU_DTRACE_BADADDR;
        cpu_core[VBDT_GET_CPUID()].cpuc_dtrace_illval = uUserAddr;
    }
}


/**
 * Copy a string from userland into the kernel.
 *
 * @param   uUserAddr           The userland address.
 * @param   uKrnlAddr           The kernel buffer address.
 * @param   cbMax               The maximum number of bytes to copy. May stop
 *                              earlier if zero byte is encountered.
 * @param   pfFlags             Pointer to the relevant cpuc_dtrace_flags.
 */
void dtrace_copyinstr( uintptr_t uUserAddr, uintptr_t uKrnlAddr, size_t cbMax, volatile uint16_t *pfFlags)
{
    if (!cbMax)
        return;

    char   *pszDst = (char *)uKrnlAddr;
    int     rc = RTR0MemUserCopyFrom(pszDst, uUserAddr, cbMax);
    if (RT_FAILURE(rc))
    {
        /* Byte by byte - lazy bird! */
        size_t off = 0;
        while (off < cbMax)
        {
            rc = RTR0MemUserCopyFrom(&pszDst[off], uUserAddr + off, 1);
            if (RT_FAILURE(rc))
            {
                *pfFlags |= CPU_DTRACE_BADADDR;
                cpu_core[VBDT_GET_CPUID()].cpuc_dtrace_illval = uUserAddr;
                pszDst[off] = '\0';
                return;
            }
            if (!pszDst[off])
                return;
            off++;
        }
    }

    pszDst[cbMax - 1] = '\0';
}


/**
 * Copy a string from the kernel and into user land.
 *
 * @param   uKrnlAddr           The kernel string address.
 * @param   uUserAddr           The userland address.
 * @param   cbMax               The maximum number of bytes to copy.  Will stop
 *                              earlier if zero byte is encountered.
 * @param   pfFlags             Pointer to the relevant cpuc_dtrace_flags.
 */
void dtrace_copyoutstr(uintptr_t uKrnlAddr, uintptr_t uUserAddr, size_t cbMax, volatile uint16_t *pfFlags)
{
    const char *pszSrc   = (const char *)uKrnlAddr;
    size_t      cbActual = RTStrNLen(pszSrc, cbMax);
    cbActual += cbActual < cbMax;
    dtrace_copyout(uKrnlAddr,uUserAddr, cbActual, pfFlags);
}


/**
 * Get the caller @a cCallFrames call frames up the stack.
 *
 * @returns The caller's return address or ~(uintptr_t)0.
 * @param   cCallFrames         The number of frames.
 */
uintptr_t dtrace_caller(int cCallFrames)
{
    PVBDTSTACKDATA pData = vboxDtGetStackData();
    if (pData->enmCaller == kVBoxDtCaller_ProbeFireKernel)
        return pData->u.ProbeFireKernel.uCaller;
    RT_NOREF_PV(cCallFrames);
    return ~(uintptr_t)0;
}


/**
 * Get argument number @a iArg @a cCallFrames call frames up the stack.
 *
 * @returns The caller's return address or ~(uintptr_t)0.
 * @param   iArg                The argument to get.
 * @param   cCallFrames         The number of frames.
 */
uint64_t dtrace_getarg(int iArg, int cCallFrames)
{
    PVBDTSTACKDATA pData = vboxDtGetStackData();
    AssertReturn(iArg >= 5, UINT64_MAX);

    if (pData->enmCaller == kVBoxDtCaller_ProbeFireKernel)
        return pData->u.ProbeFireKernel.pauStackArgs[iArg - 5];
    RT_NOREF_PV(cCallFrames);
    return UINT64_MAX;
}


/**
 * Produce a traceback of the kernel stack.
 *
 * @param   paPcStack           Where to return the program counters.
 * @param   cMaxFrames          The maximum number of PCs to return.
 * @param   cSkipFrames         The number of artificial callstack frames to
 *                              skip at the top.
 * @param   pIntr               Not sure what this is...
 */
void dtrace_getpcstack(pc_t *paPcStack, int cMaxFrames, int cSkipFrames, uint32_t *pIntr)
{
    int iFrame = 0;
    while (iFrame < cMaxFrames)
    {
        paPcStack[iFrame] = NULL;
        iFrame++;
    }
    RT_NOREF_PV(pIntr);
    RT_NOREF_PV(cSkipFrames);
}


/**
 * Get the number of call frames on the stack.
 *
 * @returns The stack depth.
 * @param   cSkipFrames         The number of artificial callstack frames to
 *                              skip at the top.
 */
int dtrace_getstackdepth(int cSkipFrames)
{
    RT_NOREF_PV(cSkipFrames);
    return 1;
}


/**
 * Produce a traceback of the userland stack.
 *
 * @param   paPcStack           Where to return the program counters.
 * @param   paFpStack           Where to return the frame pointers.
 * @param   cMaxFrames          The maximum number of frames to return.
 */
void dtrace_getufpstack(uint64_t *paPcStack, uint64_t *paFpStack, int cMaxFrames)
{
    int iFrame = 0;
    while (iFrame < cMaxFrames)
    {
        paPcStack[iFrame] = 0;
        paFpStack[iFrame] = 0;
        iFrame++;
    }
}


/**
 * Produce a traceback of the userland stack.
 *
 * @param   paPcStack           Where to return the program counters.
 * @param   cMaxFrames          The maximum number of frames to return.
 */
void dtrace_getupcstack(uint64_t *paPcStack, int cMaxFrames)
{
    int iFrame = 0;
    while (iFrame < cMaxFrames)
    {
        paPcStack[iFrame] = 0;
        iFrame++;
    }
}


/**
 * Computes the depth of the userland stack.
 */
int dtrace_getustackdepth(void)
{
    return 0;
}


/**
 * Get the current IPL/IRQL.
 *
 * @returns Current level.
 */
int dtrace_getipl(void)
{
#ifdef RT_ARCH_AMD64
    /* CR8 is normally the same as IRQL / IPL on AMD64. */
    return ASMGetCR8();
#else
    /* Just fake it on x86. */
    return !ASMIntAreEnabled();
#endif
}


/**
 * Get current monotonic timestamp.
 *
 * @returns Timestamp, nano seconds.
 */
hrtime_t dtrace_gethrtime(void)
{
    return RTTimeNanoTS();
}


/**
 * Get current walltime.
 *
 * @returns Timestamp, nano seconds.
 */
hrtime_t dtrace_gethrestime(void)
{
    /** @todo try get better resolution here somehow ... */
    RTTIMESPEC Now;
    return RTTimeSpecGetNano(RTTimeNow(&Now));
}


/**
 * DTrace panic routine.
 *
 * @param   pszFormat           Panic message.
 * @param   va                  Arguments to the panic message.
 */
void dtrace_vpanic(const char *pszFormat, va_list va)
{
    RTAssertMsg1(NULL, __LINE__, __FILE__, __FUNCTION__);
    RTAssertMsg2WeakV(pszFormat, va);
    RTR0AssertPanicSystem();
    for (;;)
    {
        ASMBreakpoint();
        volatile char *pchCrash = (volatile char *)~(uintptr_t)0;
        *pchCrash = '\0';
    }
}


/**
 * DTrace panic routine.
 *
 * @param   pszFormat           Panic message.
 * @param   ...                 Arguments to the panic message.
 */
void VBoxDtPanic(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    dtrace_vpanic(pszFormat, va);
    /*va_end(va); - unreachable */
}


/**
 * DTrace kernel message routine.
 *
 * @param   pszFormat           Kernel message.
 * @param   ...                 Arguments to the panic message.
 */
void VBoxDtCmnErr(int iLevel, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    SUPR0Printf("%N", pszFormat, va);
    va_end(va);
    RT_NOREF_PV(iLevel);
}


/** uprintf implementation */
void VBoxDtUPrintf(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    VBoxDtUPrintfV(pszFormat, va);
    va_end(va);
}


/** vuprintf implementation */
void VBoxDtUPrintfV(const char *pszFormat, va_list va)
{
    SUPR0Printf("%N", pszFormat, va);
}


/* CRED implementation. */
cred_t *VBoxDtGetCurrentCreds(void)
{
    PVBDTSTACKDATA pData = vboxDtGetStackData();
    if (!pData->pCred)
    {
        struct VBoxDtCred *pCred;
#ifdef HAVE_RTMEMALLOCEX_FEATURES
        int rc = RTMemAllocEx(sizeof(*pCred), 0, RTMEMALLOCEX_FLAGS_ANY_CTX, (void **)&pCred);
#else
        int rc = RTMemAllocEx(sizeof(*pCred), 0, 0, (void **)&pCred);
#endif
        AssertFatalRC(rc);
        pCred->cr_refs  = 1;
        /** @todo get the right creds on unix systems. */
        pCred->cr_uid   = 0;
        pCred->cr_ruid  = 0;
        pCred->cr_suid  = 0;
        pCred->cr_gid   = 0;
        pCred->cr_rgid  = 0;
        pCred->cr_sgid  = 0;
        pCred->cr_zone  = 0;
        pData->pCred = pCred;
    }

    return pData->pCred;
}


/* crhold implementation */
void VBoxDtCredHold(struct VBoxDtCred *pCred)
{
    int32_t cRefs = ASMAtomicIncS32(&pCred->cr_refs);
    Assert(cRefs > 1); NOREF(cRefs);
}


/* crfree implementation */
void VBoxDtCredFree(struct VBoxDtCred *pCred)
{
    int32_t cRefs = ASMAtomicDecS32(&pCred->cr_refs);
    Assert(cRefs >= 0);
    if (!cRefs)
        RTMemFreeEx(pCred, sizeof(*pCred));
}

/** Spinlock protecting the thread structures. */
static RTSPINLOCK           g_hThreadSpinlock = NIL_RTSPINLOCK;
/** List of threads by usage age. */
static RTLISTANCHOR         g_ThreadAgeList;
/** Hash table for looking up thread structures.  */
static struct VBoxDtThread *g_apThreadsHash[16384];
/** Fake kthread_t structures.
 * The size of this array is making horrible ASSUMPTIONS about the number of
 * thread in the system that will be subjected to DTracing. */
static struct VBoxDtThread  g_aThreads[8192];


static int vboxDtInitThreadDb(void)
{
    int rc = RTSpinlockCreate(&g_hThreadSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxDtThreadDb");
    if (RT_FAILURE(rc))
        return rc;

    RTListInit(&g_ThreadAgeList);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aThreads); i++)
    {
        g_aThreads[i].hNative = NIL_RTNATIVETHREAD;
        g_aThreads[i].uPid    = NIL_RTPROCESS;
        RTListPrepend(&g_ThreadAgeList, &g_aThreads[i].AgeEntry);
    }

    return VINF_SUCCESS;
}


static void vboxDtTermThreadDb(void)
{
    RTSpinlockDestroy(g_hThreadSpinlock);
    g_hThreadSpinlock = NIL_RTSPINLOCK;
    RTListInit(&g_ThreadAgeList);
}


/* curthread implementation, providing a fake kthread_t. */
struct VBoxDtThread *VBoxDtGetCurrentThread(void)
{
    /*
     * Once we've retrieved a thread, we hold on to it until the thread exits
     * the VBoxDTrace module.
     */
    PVBDTSTACKDATA  pData       = vboxDtGetStackData();
    if (pData->pThread)
    {
        AssertPtr(pData->pThread);
        Assert(pData->pThread->hNative   == RTThreadNativeSelf());
        Assert(pData->pThread->uPid      == RTProcSelf());
        Assert(RTListIsEmpty(&pData->pThread->AgeEntry));
        return pData->pThread;
    }

    /*
     * Lookup the thread in the hash table.
     */
    RTNATIVETHREAD  hNativeSelf = RTThreadNativeSelf();
    RTPROCESS       uPid        = RTProcSelf();
    uintptr_t       iHash       = (hNativeSelf * 2654435761U) % RT_ELEMENTS(g_apThreadsHash);

    RTSpinlockAcquire(g_hThreadSpinlock);

    struct VBoxDtThread *pThread = g_apThreadsHash[iHash];
    while (pThread)
    {
        if (pThread->hNative == hNativeSelf)
        {
            if (pThread->uPid != uPid)
            {
                /* Re-initialize the reused thread. */
                pThread->uPid           = uPid;
                pThread->t_dtrace_vtime = 0;
                pThread->t_dtrace_start = 0;
                pThread->t_dtrace_stop  = 0;
                pThread->t_dtrace_scrpc = 0;
                pThread->t_dtrace_astpc = 0;
                pThread->t_predcache    = 0;
            }

            /* Hold the thread in the on-stack data, making sure it does not
               get reused till the thread leaves VBoxDTrace. */
            RTListNodeRemove(&pThread->AgeEntry);
            pData->pThread = pThread;

            RTSpinlockRelease(g_hThreadSpinlock);
            return pThread;
        }

        pThread = pThread->pNext;
    }

    /*
     * Unknown thread.  Allocate a new entry, recycling unused or old ones.
     */
    pThread = RTListGetLast(&g_ThreadAgeList, struct VBoxDtThread, AgeEntry);
    AssertFatal(pThread);
    RTListNodeRemove(&pThread->AgeEntry);
    if (pThread->hNative != NIL_RTNATIVETHREAD)
    {
        uintptr_t   iHash2 = (pThread->hNative * 2654435761U) % RT_ELEMENTS(g_apThreadsHash);
        if (g_apThreadsHash[iHash2] == pThread)
            g_apThreadsHash[iHash2] = pThread->pNext;
        else
        {
            for (struct VBoxDtThread *pPrev = g_apThreadsHash[iHash2]; ; pPrev = pPrev->pNext)
            {
                AssertPtr(pPrev);
                if (pPrev->pNext == pThread)
                {
                    pPrev->pNext = pThread->pNext;
                    break;
                }
            }
        }
    }

    /*
     * Initialize the data.
     */
    pThread->t_dtrace_vtime = 0;
    pThread->t_dtrace_start = 0;
    pThread->t_dtrace_stop  = 0;
    pThread->t_dtrace_scrpc = 0;
    pThread->t_dtrace_astpc = 0;
    pThread->t_predcache    = 0;
    pThread->hNative        = hNativeSelf;
    pThread->uPid           = uPid;

    /*
     * Add it to the hash as well as the on-stack data.
     */
    pThread->pNext = g_apThreadsHash[iHash];
    g_apThreadsHash[iHash] = pThread->pNext;

    pData->pThread = pThread;

    RTSpinlockRelease(g_hThreadSpinlock);
    return pThread;
}


/**
 * Called by the stack data destructor.
 *
 * @param   pThread         The thread to release.
 *
 */
static void VBoxDtReleaseThread(struct VBoxDtThread *pThread)
{
    RTSpinlockAcquire(g_hThreadSpinlock);

    RTListAppend(&g_ThreadAgeList, &pThread->AgeEntry);

    RTSpinlockRelease(g_hThreadSpinlock);
}




/*
 *
 * Virtual Memory / Resource Allocator.
 * Virtual Memory / Resource Allocator.
 * Virtual Memory / Resource Allocator.
 *
 */


/** The number of bits per chunk.
 * @remarks The 32 bytes are for heap headers and such like.  */
#define VBOXDTVMEMCHUNK_BITS    ( ((_64K - 32 - sizeof(uint32_t) * 2) / sizeof(uint32_t)) * 32)

/**
 * Resource allocator chunk.
 */
typedef struct  VBoxDtVMemChunk
{
    /** The ordinal (unbased) of the first item. */
    uint32_t            iFirst;
    /** The current number of free items in this chunk. */
    uint32_t            cCurFree;
    /** The allocation bitmap. */
    uint32_t            bm[VBOXDTVMEMCHUNK_BITS / 32];
} VBOXDTVMEMCHUNK;
/** Pointer to a resource allocator chunk. */
typedef VBOXDTVMEMCHUNK *PVBOXDTVMEMCHUNK;



/**
 * Resource allocator instance.
 */
typedef struct VBoxDtVMem
{
    /** Spinlock protecting the data (interrupt safe). */
    RTSPINLOCK          hSpinlock;
    /** Magic value. */
    uint32_t            u32Magic;
    /** The current number of free items in the chunks. */
    uint32_t            cCurFree;
    /** The current number of chunks that we have allocated. */
    uint32_t            cCurChunks;
    /** The configured resource base. */
    uint32_t            uBase;
    /** The configured max number of items. */
    uint32_t            cMaxItems;
    /** The size of the apChunks array. */
    uint32_t            cMaxChunks;
    /** Array of chunk pointers.
     * (The size is determined at creation.) */
    PVBOXDTVMEMCHUNK    apChunks[1];
} VBOXDTVMEM;
/** Pointer to a resource allocator instance. */
typedef VBOXDTVMEM *PVBOXDTVMEM;

/** Magic value for the VBOXDTVMEM structure. */
#define VBOXDTVMEM_MAGIC        RT_MAKE_U32_FROM_U8('V', 'M',  'e',  'm')


/* vmem_create implementation */
struct VBoxDtVMem *VBoxDtVMemCreate(const char *pszName, void *pvBase, size_t cb, size_t cbUnit,
                                    PFNRT pfnAlloc, PFNRT pfnFree, struct VBoxDtVMem *pSrc,
                                    size_t cbQCacheMax, uint32_t fFlags)
{
    /*
     * Assert preconditions of this implementation.
     */
    AssertMsgReturn((uintptr_t)pvBase <= UINT32_MAX, ("%p\n", pvBase), NULL);
    AssertMsgReturn(cb <= UINT32_MAX, ("%zu\n", cb), NULL);
    AssertMsgReturn((uintptr_t)pvBase + cb - 1 <= UINT32_MAX, ("%p %zu\n", pvBase, cb), NULL);
    AssertMsgReturn(cbUnit == 1, ("%zu\n", cbUnit), NULL);
    AssertReturn(!pfnAlloc, NULL);
    AssertReturn(!pfnFree, NULL);
    AssertReturn(!pSrc, NULL);
    AssertReturn(!cbQCacheMax, NULL);
    AssertReturn(fFlags & VM_SLEEP, NULL);
    AssertReturn(fFlags & VMC_IDENTIFIER, NULL);
    RT_NOREF_PV(pszName);

    /*
     * Allocate the instance.
     */
    uint32_t cChunks = (uint32_t)cb / VBOXDTVMEMCHUNK_BITS;
    if (cb % VBOXDTVMEMCHUNK_BITS)
        cChunks++;
    PVBOXDTVMEM pThis = (PVBOXDTVMEM)RTMemAllocZ(RT_UOFFSETOF_DYN(VBOXDTVMEM, apChunks[cChunks]));
    if (!pThis)
        return NULL;
    int rc = RTSpinlockCreate(&pThis->hSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxDtVMem");
    if (RT_FAILURE(rc))
    {
        RTMemFree(pThis);
        return NULL;
    }
    pThis->u32Magic     = VBOXDTVMEM_MAGIC;
    pThis->cCurFree     = 0;
    pThis->cCurChunks   = 0;
    pThis->uBase        = (uint32_t)(uintptr_t)pvBase;
    pThis->cMaxItems    = (uint32_t)cb;
    pThis->cMaxChunks   = cChunks;

    return pThis;
}


/* vmem_destroy implementation */
void  VBoxDtVMemDestroy(struct VBoxDtVMem *pThis)
{
    if (!pThis)
        return;
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == VBOXDTVMEM_MAGIC);

    /*
     * Invalidate the instance.
     */
    RTSpinlockAcquire(pThis->hSpinlock); /* paranoia */
    pThis->u32Magic = 0;
    RTSpinlockRelease(pThis->hSpinlock);
    RTSpinlockDestroy(pThis->hSpinlock);

    /*
     * Free the chunks, then the instance.
     */
    uint32_t iChunk = pThis->cCurChunks;
    while (iChunk-- > 0)
    {
        RTMemFree(pThis->apChunks[iChunk]);
        pThis->apChunks[iChunk] = NULL;
    }
    RTMemFree(pThis);
}


/* vmem_alloc implementation */
void *VBoxDtVMemAlloc(struct VBoxDtVMem *pThis, size_t cbMem, uint32_t fFlags)
{
    /*
     * Validate input.
     */
    AssertReturn(fFlags & VM_BESTFIT, NULL);
    AssertReturn(fFlags & VM_SLEEP, NULL);
    AssertReturn(cbMem == 1, NULL);
    AssertPtrReturn(pThis, NULL);
    AssertReturn(pThis->u32Magic == VBOXDTVMEM_MAGIC, NULL);

    /*
     * Allocation loop.
     */
    RTSpinlockAcquire(pThis->hSpinlock);
    for (;;)
    {
        PVBOXDTVMEMCHUNK pChunk;
        uint32_t const   cChunks = pThis->cCurChunks;

        if (RT_LIKELY(pThis->cCurFree > 0))
        {
            for (uint32_t iChunk = 0; iChunk < cChunks; iChunk++)
            {
                pChunk = pThis->apChunks[iChunk];
                if (pChunk->cCurFree > 0)
                {
                    int iBit = ASMBitFirstClear(pChunk->bm, VBOXDTVMEMCHUNK_BITS);
                    AssertMsgReturnStmt(iBit >= 0 && (unsigned)iBit < VBOXDTVMEMCHUNK_BITS, ("%d\n", iBit),
                                        RTSpinlockRelease(pThis->hSpinlock),
                                        NULL);

                    ASMBitSet(pChunk->bm, iBit);
                    pChunk->cCurFree--;
                    pThis->cCurFree--;

                    uint32_t iRet = (uint32_t)iBit + pChunk->iFirst + pThis->uBase;
                    RTSpinlockRelease(pThis->hSpinlock);
                    return (void *)(uintptr_t)iRet;
                }
            }
            AssertFailedBreak();
        }

        /* Out of resources? */
        if (cChunks >= pThis->cMaxChunks)
            break;

        /*
         * Allocate another chunk.
         */
        uint32_t const  iFirstBit = cChunks > 0 ? pThis->apChunks[cChunks - 1]->iFirst + VBOXDTVMEMCHUNK_BITS : 0;
        uint32_t const  cFreeBits = cChunks + 1 == pThis->cMaxChunks
                                  ? pThis->cMaxItems - (iFirstBit - pThis->uBase)
                                  : VBOXDTVMEMCHUNK_BITS;
        Assert(cFreeBits <= VBOXDTVMEMCHUNK_BITS);

        RTSpinlockRelease(pThis->hSpinlock);

        pChunk = (PVBOXDTVMEMCHUNK)RTMemAllocZ(sizeof(*pChunk));
        if (!pChunk)
            return NULL;

        pChunk->iFirst   = iFirstBit;
        pChunk->cCurFree = cFreeBits;
        if (cFreeBits != VBOXDTVMEMCHUNK_BITS)
        {
            /* lazy bird. */
            uint32_t iBit = cFreeBits;
            while (iBit < VBOXDTVMEMCHUNK_BITS)
            {
                ASMBitSet(pChunk->bm, iBit);
                iBit++;
            }
        }

        RTSpinlockAcquire(pThis->hSpinlock);

        /*
         * Insert the new chunk.  If someone raced us here, we'll drop it to
         * avoid wasting resources.
         */
        if (pThis->cCurChunks == cChunks)
        {
            pThis->apChunks[cChunks] = pChunk;
            pThis->cCurFree   += pChunk->cCurFree;
            pThis->cCurChunks += 1;
        }
        else
        {
            RTSpinlockRelease(pThis->hSpinlock);
            RTMemFree(pChunk);
            RTSpinlockAcquire(pThis->hSpinlock);
        }
    }
    RTSpinlockRelease(pThis->hSpinlock);

    return NULL;
}

/* vmem_free implementation */
void VBoxDtVMemFree(struct VBoxDtVMem *pThis, void *pvMem, size_t cbMem)
{
    /*
     * Validate input.
     */
    AssertReturnVoid(cbMem == 1);
    AssertPtrReturnVoid(pThis);
    AssertReturnVoid(pThis->u32Magic == VBOXDTVMEM_MAGIC);

    AssertReturnVoid((uintptr_t)pvMem < UINT32_MAX);
    uint32_t uMem = (uint32_t)(uintptr_t)pvMem;
    AssertReturnVoid(uMem >= pThis->uBase);
    uMem -= pThis->uBase;
    AssertReturnVoid(uMem < pThis->cMaxItems);


    /*
     * Free it.
     */
    RTSpinlockAcquire(pThis->hSpinlock);
    uint32_t const iChunk = uMem / VBOXDTVMEMCHUNK_BITS;
    if (iChunk < pThis->cCurChunks)
    {
        PVBOXDTVMEMCHUNK pChunk = pThis->apChunks[iChunk];
        uint32_t iBit = uMem - pChunk->iFirst;
        AssertReturnVoidStmt(iBit < VBOXDTVMEMCHUNK_BITS, RTSpinlockRelease(pThis->hSpinlock));
        AssertReturnVoidStmt(ASMBitTestAndClear(pChunk->bm, iBit), RTSpinlockRelease(pThis->hSpinlock));

        pChunk->cCurFree++;
        pThis->cCurFree++;
    }

    RTSpinlockRelease(pThis->hSpinlock);
}


/*
 *
 * Memory Allocators.
 * Memory Allocators.
 * Memory Allocators.
 *
 */


/* kmem_alloc implementation */
void *VBoxDtKMemAlloc(size_t cbMem, uint32_t fFlags)
{
    void    *pvMem;
#ifdef HAVE_RTMEMALLOCEX_FEATURES
    uint32_t fMemAllocFlags = fFlags & KM_NOSLEEP ? RTMEMALLOCEX_FLAGS_ANY_CTX : 0;
#else
    uint32_t fMemAllocFlags = 0;
    RT_NOREF_PV(fFlags);
#endif
    int rc = RTMemAllocEx(cbMem, 0, fMemAllocFlags, &pvMem);
    AssertRCReturn(rc, NULL);
    AssertPtr(pvMem);
    return pvMem;
}


/* kmem_zalloc implementation */
void *VBoxDtKMemAllocZ(size_t cbMem, uint32_t fFlags)
{
    void    *pvMem;
#ifdef HAVE_RTMEMALLOCEX_FEATURES
    uint32_t fMemAllocFlags = (fFlags & KM_NOSLEEP ? RTMEMALLOCEX_FLAGS_ANY_CTX : 0) | RTMEMALLOCEX_FLAGS_ZEROED;
#else
    uint32_t fMemAllocFlags = RTMEMALLOCEX_FLAGS_ZEROED;
    RT_NOREF_PV(fFlags);
#endif
    int rc = RTMemAllocEx(cbMem, 0, fMemAllocFlags, &pvMem);
    AssertRCReturn(rc, NULL);
    AssertPtr(pvMem);
    return pvMem;
}


/* kmem_free implementation */
void  VBoxDtKMemFree(void *pvMem, size_t cbMem)
{
    RTMemFreeEx(pvMem, cbMem);
}


/**
 * Memory cache mockup structure.
 * No slab allocator here!
 */
struct VBoxDtMemCache
{
    uint32_t u32Magic;
    size_t cbBuf;
    size_t cbAlign;
};


/* Limited kmem_cache_create implementation. */
struct VBoxDtMemCache *VBoxDtKMemCacheCreate(const char *pszName, size_t cbBuf, size_t cbAlign,
                                             PFNRT pfnCtor, PFNRT pfnDtor, PFNRT pfnReclaim,
                                             void *pvUser, void *pvVM, uint32_t fFlags)
{
    /*
     * Check the input.
     */
    AssertReturn(cbBuf > 0 && cbBuf < _1G, NULL);
    AssertReturn(RT_IS_POWER_OF_TWO(cbAlign), NULL);
    AssertReturn(!pfnCtor, NULL);
    AssertReturn(!pfnDtor, NULL);
    AssertReturn(!pfnReclaim, NULL);
    AssertReturn(!pvUser, NULL);
    AssertReturn(!pvVM, NULL);
    AssertReturn(!fFlags, NULL);
    RT_NOREF_PV(pszName);

    /*
     * Create a parameter container. Don't bother with anything fancy here yet,
     * just get something working.
     */
    struct VBoxDtMemCache *pThis = (struct VBoxDtMemCache *)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return NULL;

    pThis->cbAlign = cbAlign;
    pThis->cbBuf   = cbBuf;
    return pThis;
}


/* Limited kmem_cache_destroy implementation. */
void  VBoxDtKMemCacheDestroy(struct VBoxDtMemCache *pThis)
{
    RTMemFree(pThis);
}


/* kmem_cache_alloc implementation. */
void *VBoxDtKMemCacheAlloc(struct VBoxDtMemCache *pThis, uint32_t fFlags)
{
    void    *pvMem;
#ifdef HAVE_RTMEMALLOCEX_FEATURES
    uint32_t fMemAllocFlags = (fFlags & KM_NOSLEEP ? RTMEMALLOCEX_FLAGS_ANY_CTX : 0) | RTMEMALLOCEX_FLAGS_ZEROED;
#else
    uint32_t fMemAllocFlags = RTMEMALLOCEX_FLAGS_ZEROED;
    RT_NOREF_PV(fFlags);
#endif
    int rc = RTMemAllocEx(pThis->cbBuf, /*pThis->cbAlign*/0, fMemAllocFlags, &pvMem);
    AssertRCReturn(rc, NULL);
    AssertPtr(pvMem);
    return pvMem;
}


/* kmem_cache_free implementation. */
void  VBoxDtKMemCacheFree(struct VBoxDtMemCache *pThis, void *pvMem)
{
    RTMemFreeEx(pvMem, pThis->cbBuf);
}


/*
 *
 * Mutex Semaphore Wrappers.
 *
 */


/** Initializes a mutex. */
int VBoxDtMutexInit(struct VBoxDtMutex *pMtx)
{
    AssertReturn(pMtx != &g_DummyMtx, -1);
    AssertPtr(pMtx);

    pMtx->hOwner = NIL_RTNATIVETHREAD;
    pMtx->hMtx   = NIL_RTSEMMUTEX;
    int rc = RTSemMutexCreate(&pMtx->hMtx);
    if (RT_SUCCESS(rc))
        return 0;
    return -1;
}


/** Deletes a mutex. */
void VBoxDtMutexDelete(struct VBoxDtMutex *pMtx)
{
    AssertReturnVoid(pMtx != &g_DummyMtx);
    AssertPtr(pMtx);
    if (pMtx->hMtx == NIL_RTSEMMUTEX)
        return;

    Assert(pMtx->hOwner == NIL_RTNATIVETHREAD);
    int rc = RTSemMutexDestroy(pMtx->hMtx); AssertRC(rc);
    pMtx->hMtx = NIL_RTSEMMUTEX;
}


/* mutex_enter implementation */
void VBoxDtMutexEnter(struct VBoxDtMutex *pMtx)
{
    AssertPtr(pMtx);
    if (pMtx == &g_DummyMtx)
        return;

    RTNATIVETHREAD hSelf = RTThreadNativeSelf();

    int rc = RTSemMutexRequest(pMtx->hMtx, RT_INDEFINITE_WAIT);
    AssertFatalRC(rc);

    Assert(pMtx->hOwner == NIL_RTNATIVETHREAD);
    pMtx->hOwner = hSelf;
}


/* mutex_exit implementation */
void VBoxDtMutexExit(struct VBoxDtMutex *pMtx)
{
    AssertPtr(pMtx);
    if (pMtx == &g_DummyMtx)
        return;

    Assert(pMtx->hOwner == RTThreadNativeSelf());

    pMtx->hOwner = NIL_RTNATIVETHREAD;
    int rc = RTSemMutexRelease(pMtx->hMtx);
    AssertFatalRC(rc);
}


/* MUTEX_HELD implementation */
bool VBoxDtMutexIsOwner(struct VBoxDtMutex *pMtx)
{
    AssertPtrReturn(pMtx, false);
    if (pMtx == &g_DummyMtx)
        return true;
    return pMtx->hOwner == RTThreadNativeSelf();
}



/*
 *
 * Helpers for handling VTG structures.
 * Helpers for handling VTG structures.
 * Helpers for handling VTG structures.
 *
 */



/**
 * Converts an attribute from VTG description speak to DTrace.
 *
 * @param   pDtAttr             The DTrace attribute (dst).
 * @param   pVtgAttr            The VTG attribute descriptor (src).
 */
static void vboxDtVtgConvAttr(dtrace_attribute_t *pDtAttr, PCVTGDESCATTR pVtgAttr)
{
    pDtAttr->dtat_name  = pVtgAttr->u8Code - 1;
    pDtAttr->dtat_data  = pVtgAttr->u8Data - 1;
    pDtAttr->dtat_class = pVtgAttr->u8DataDep - 1;
}

/**
 * Gets a string from the string table.
 *
 * @returns Pointer to the string.
 * @param   pVtgHdr             The VTG object header.
 * @param   offStrTab           The string table offset.
 */
static const char *vboxDtVtgGetString(PVTGOBJHDR pVtgHdr, uint32_t offStrTab)
{
    Assert(offStrTab < pVtgHdr->cbStrTab);
    return (const char *)pVtgHdr + pVtgHdr->offStrTab + offStrTab;
}



/*
 *
 * DTrace Provider Interface.
 * DTrace Provider Interface.
 * DTrace Provider Interface.
 *
 */


/**
 * @callback_method_impl{dtrace_pops_t,dtps_provide}
 */
static void     vboxDtPOps_Provide(void *pvProv, const dtrace_probedesc_t *pDtProbeDesc)
{
    PSUPDRVVDTPROVIDERCORE  pProv = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturnVoid(pProv);
    LOG_DTRACE(("%s: %p / %p pDtProbeDesc=%p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, pDtProbeDesc));

    if (pDtProbeDesc)
        return;  /* We don't generate probes, so never mind these requests. */

    if (pProv->TracerData.DTrace.fZombie)
        return;

    dtrace_provider_id_t const idProvider = pProv->TracerData.DTrace.idProvider;
    AssertPtrReturnVoid(idProvider);

    AssertPtrReturnVoid(pProv->pHdr);
    AssertReturnVoid(pProv->pHdr->offProbeLocs != 0);
    uint32_t const  cProbeLocs    = pProv->pHdr->cbProbeLocs / sizeof(VTGPROBELOC);

    /* Need a buffer for extracting the function names and mangling them in
       case of collision. */
    size_t const cbFnNmBuf = _4K + _1K;
    char *pszFnNmBuf = (char *)RTMemAlloc(cbFnNmBuf);
    if (!pszFnNmBuf)
         return;

    /*
     * Itereate the probe location list and register all probes related to
     * this provider.
     */
    uint16_t const idxProv = (uint16_t)((PVTGDESCPROVIDER)((uintptr_t)pProv->pHdr + pProv->pHdr->offProviders) - pProv->pDesc);
    for (uint32_t idxProbeLoc = 0; idxProbeLoc < cProbeLocs; idxProbeLoc++)
    {
        /* Skip probe location belonging to other providers or once that
           we've already reported. */
        PCVTGPROBELOC pProbeLocRO = &pProv->paProbeLocsRO[idxProbeLoc];
        PVTGDESCPROBE pProbeDesc  = pProbeLocRO->pProbe;
        if (pProbeDesc->idxProvider != idxProv)
            continue;

        uint32_t *pidProbe;
        if (!pProv->fUmod)
            pidProbe = (uint32_t *)&pProbeLocRO->idProbe;
        else
            pidProbe = &pProv->paR0ProbeLocs[idxProbeLoc].idProbe;
        if (*pidProbe != 0)
            continue;

         /* The function name may need to be stripped since we're using C++
            compilers for most of the code.  ASSUMES nobody are brave/stupid
            enough to use function pointer returns without typedef'ing
            properly them (e.g. signal). */
         const char *pszPrbName = vboxDtVtgGetString(pProv->pHdr, pProbeDesc->offName);
         const char *pszFunc    = pProbeLocRO->pszFunction;
         const char *psz        = strchr(pProbeLocRO->pszFunction, '(');
         size_t      cch;
         if (psz)
         {
             /* skip blanks preceding the parameter parenthesis. */
             while (   (uintptr_t)psz > (uintptr_t)pProbeLocRO->pszFunction
                    && RT_C_IS_BLANK(psz[-1]))
                 psz--;

             /* Find the start of the function name. */
             pszFunc = psz - 1;
             while ((uintptr_t)pszFunc > (uintptr_t)pProbeLocRO->pszFunction)
             {
                 char ch = pszFunc[-1];
                 if (!RT_C_IS_ALNUM(ch) && ch != '_' && ch != ':')
                     break;
                 pszFunc--;
             }
             cch = psz - pszFunc;
         }
         else
             cch = strlen(pszFunc);
         RTStrCopyEx(pszFnNmBuf, cbFnNmBuf, pszFunc, cch);

         /* Look up the probe, if we have one in the same function, mangle
            the function name a little to avoid having to deal with having
            multiple location entries with the same probe ID. (lazy bird) */
         Assert(!*pidProbe);
         if (dtrace_probe_lookup(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName) != DTRACE_IDNONE)
         {
             RTStrPrintf(pszFnNmBuf+cch, cbFnNmBuf - cch, "-%u", pProbeLocRO->uLine);
             if (dtrace_probe_lookup(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName) != DTRACE_IDNONE)
             {
                 unsigned iOrd = 2;
                 while (iOrd < 128)
                 {
                     RTStrPrintf(pszFnNmBuf+cch, cbFnNmBuf - cch, "-%u-%u", pProbeLocRO->uLine, iOrd);
                     if (dtrace_probe_lookup(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName) == DTRACE_IDNONE)
                         break;
                     iOrd++;
                 }
                 if (iOrd >= 128)
                 {
                     LogRel(("VBoxDrv: More than 128 duplicate probe location instances at line %u in function %s [%s], probe %s\n",
                             pProbeLocRO->uLine, pProbeLocRO->pszFunction, pszFnNmBuf, pszPrbName));
                     continue;
                 }
             }
         }

         /* Create the probe. */
         AssertCompile(sizeof(*pidProbe) == sizeof(dtrace_id_t));
         *pidProbe = dtrace_probe_create(idProvider, pProv->pszModName, pszFnNmBuf, pszPrbName,
                                         1 /*aframes*/, (void *)(uintptr_t)idxProbeLoc);
         pProv->TracerData.DTrace.cProvidedProbes++;
     }

     RTMemFree(pszFnNmBuf);
     LOG_DTRACE(("%s: returns\n", __FUNCTION__));
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_enable}
 */
static int      vboxDtPOps_Enable(void *pvProv, dtrace_id_t idProbe, void *pvProbe)
{
    PSUPDRVVDTPROVIDERCORE  pProv   = (PSUPDRVVDTPROVIDERCORE)pvProv;
    LOG_DTRACE(("%s: %p / %p - %#x / %p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe));
    AssertPtrReturn(pProv->TracerData.DTrace.idProvider, EINVAL);
    RT_NOREF_PV(idProbe);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t        idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PVTGPROBELOC32  pProbeLocEn = (PVTGPROBELOC32)(  (uintptr_t)pProv->pvProbeLocsEn + idxProbeLoc * pProv->cbProbeLocsEn);
        PCVTGPROBELOC   pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        PCVTGDESCPROBE  pProbeDesc  = pProbeLocRO->pProbe;
        uint32_t const  idxProbe    = pProbeDesc->idxEnabled;

        if (!pProv->fUmod)
        {
            if (!pProbeLocEn->fEnabled)
            {
                pProbeLocEn->fEnabled = 1;
                ASMAtomicIncU32(&pProv->pacProbeEnabled[idxProbe]);
                ASMAtomicIncU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }
        }
        else
        {
            /* Update kernel mode structure */
            if (!pProv->paR0ProbeLocs[idxProbeLoc].fEnabled)
            {
                pProv->paR0ProbeLocs[idxProbeLoc].fEnabled = 1;
                ASMAtomicIncU32(&pProv->paR0Probes[idxProbe].cEnabled);
                ASMAtomicIncU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }

            /* Update user mode structure. */
            pProbeLocEn->fEnabled = 1;
            pProv->pacProbeEnabled[idxProbe] = pProv->paR0Probes[idxProbe].cEnabled;
        }
    }

    return 0;
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_disable}
 */
static void     vboxDtPOps_Disable(void *pvProv, dtrace_id_t idProbe, void *pvProbe)
{
    PSUPDRVVDTPROVIDERCORE  pProv  = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturnVoid(pProv);
    LOG_DTRACE(("%s: %p / %p - %#x / %p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe));
    AssertPtrReturnVoid(pProv->TracerData.DTrace.idProvider);
    RT_NOREF_PV(idProbe);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t        idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PVTGPROBELOC32  pProbeLocEn = (PVTGPROBELOC32)(  (uintptr_t)pProv->pvProbeLocsEn + idxProbeLoc * pProv->cbProbeLocsEn);
        PCVTGPROBELOC   pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        PCVTGDESCPROBE  pProbeDesc  = pProbeLocRO->pProbe;
        uint32_t const  idxProbe    = pProbeDesc->idxEnabled;

        if (!pProv->fUmod)
        {
            if (pProbeLocEn->fEnabled)
            {
                pProbeLocEn->fEnabled = 0;
                ASMAtomicDecU32(&pProv->pacProbeEnabled[idxProbe]);
                ASMAtomicDecU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }
        }
        else
        {
            /* Update kernel mode structure */
            if (pProv->paR0ProbeLocs[idxProbeLoc].fEnabled)
            {
                pProv->paR0ProbeLocs[idxProbeLoc].fEnabled = 0;
                ASMAtomicDecU32(&pProv->paR0Probes[idxProbe].cEnabled);
                ASMAtomicDecU32(&pProv->pDesc->cProbesEnabled);
                ASMAtomicIncU32(&pProv->pDesc->uSettingsSerialNo);
            }

            /* Update user mode structure. */
            pProbeLocEn->fEnabled = 0;
            pProv->pacProbeEnabled[idxProbe] = pProv->paR0Probes[idxProbe].cEnabled;
        }
    }
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_getargdesc}
 */
static void     vboxDtPOps_GetArgDesc(void *pvProv, dtrace_id_t idProbe, void *pvProbe,
                                      dtrace_argdesc_t *pArgDesc)
{
    PSUPDRVVDTPROVIDERCORE  pProv  = (PSUPDRVVDTPROVIDERCORE)pvProv;
    unsigned                uArg   = pArgDesc->dtargd_ndx;
    RT_NOREF_PV(idProbe);

    pArgDesc->dtargd_ndx = DTRACE_ARGNONE;
    AssertPtrReturnVoid(pProv);
    LOG_DTRACE(("%s: %p / %p - %#x / %p uArg=%d\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe, uArg));
    AssertPtrReturnVoid(pProv->TracerData.DTrace.idProvider);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t         idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PCVTGPROBELOC    pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        PCVTGDESCPROBE   pProbeDesc  = pProbeLocRO->pProbe;
        PCVTGDESCARGLIST pArgList    = (PCVTGDESCARGLIST)(  (uintptr_t)pProv->pHdr
                                                          + pProv->pHdr->offArgLists
                                                          + pProbeDesc->offArgList);
        AssertReturnVoid(pProbeDesc->offArgList < pProv->pHdr->cbArgLists);

        if (uArg < pArgList->cArgs)
        {
            const char *pszType = vboxDtVtgGetString(pProv->pHdr, pArgList->aArgs[uArg].offType);
            size_t      cchType = strlen(pszType);
            if (cchType < sizeof(pArgDesc->dtargd_native))
            {
                memcpy(pArgDesc->dtargd_native, pszType, cchType + 1);
                /** @todo mapping? */
                pArgDesc->dtargd_ndx = uArg;
                LOG_DTRACE(("%s: returns dtargd_native = %s\n", __FUNCTION__, pArgDesc->dtargd_native));
                return;
            }
        }
    }
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_getargval}
 */
static uint64_t vboxDtPOps_GetArgVal(void *pvProv, dtrace_id_t idProbe, void *pvProbe,
                                     int iArg, int cFrames)
{
    PSUPDRVVDTPROVIDERCORE  pProv = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturn(pProv, UINT64_MAX);
    LOG_DTRACE(("%s: %p / %p - %#x / %p iArg=%d cFrames=%u\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe, iArg, cFrames));
    AssertReturn(iArg >= 5, UINT64_MAX);
    RT_NOREF_PV(idProbe); RT_NOREF_PV(cFrames);

    if (pProv->TracerData.DTrace.fZombie)
        return UINT64_MAX;

    uint32_t                idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
    PCVTGPROBELOC           pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
    PCVTGDESCPROBE          pProbeDesc  = pProbeLocRO->pProbe;
    PCVTGDESCARGLIST        pArgList    = (PCVTGDESCARGLIST)(  (uintptr_t)pProv->pHdr
                                                             + pProv->pHdr->offArgLists
                                                             + pProbeDesc->offArgList);
    AssertReturn(pProbeDesc->offArgList < pProv->pHdr->cbArgLists, UINT64_MAX);

    PVBDTSTACKDATA          pData = vboxDtGetStackData();

    /*
     * Get the stack data. This is a wee bit complicated on 32-bit systems
     * since we want to support 64-bit integer arguments.
     */
    uint64_t u64Ret;
    if (iArg >= 20)
        u64Ret = UINT64_MAX;
    else if (pData->enmCaller == kVBoxDtCaller_ProbeFireKernel)
    {
#if ARCH_BITS == 64
        u64Ret = pData->u.ProbeFireKernel.pauStackArgs[iArg - 5];
#else
        if (   !pArgList->fHaveLargeArgs
            || iArg >= pArgList->cArgs)
            u64Ret = pData->u.ProbeFireKernel.pauStackArgs[iArg - 5];
        else
        {
            /* Similar to what we did for mac in when calling dtrace_probe(). */
            uint32_t offArg = 0;
            for (int i = 5; i < iArg; i++)
                if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType))
                    offArg++;
            u64Ret = pData->u.ProbeFireKernel.pauStackArgs[iArg - 5 + offArg];
            if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType))
                u64Ret |= (uint64_t)pData->u.ProbeFireKernel.pauStackArgs[iArg - 5 + offArg + 1] << 32;
        }
#endif
    }
    else if (pData->enmCaller == kVBoxDtCaller_ProbeFireUser)
    {
        int                     offArg    = pData->u.ProbeFireUser.offArg;
        PCSUPDRVTRACERUSRCTX    pCtx      = pData->u.ProbeFireUser.pCtx;
        AssertPtrReturn(pCtx, UINT64_MAX);

        if (pCtx->cBits == 32)
        {
            if (   !pArgList->fHaveLargeArgs
                || iArg >= pArgList->cArgs)
            {
                if (iArg + offArg < (int)RT_ELEMENTS(pCtx->u.X86.aArgs))
                    u64Ret = pCtx->u.X86.aArgs[iArg + offArg];
                else
                    u64Ret = UINT64_MAX;
            }
            else
            {
                for (int i = 5; i < iArg; i++)
                    if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType))
                        offArg++;
                if (offArg + iArg < (int)RT_ELEMENTS(pCtx->u.X86.aArgs))
                {
                    u64Ret = pCtx->u.X86.aArgs[iArg + offArg];
                    if (   VTG_TYPE_IS_LARGE(pArgList->aArgs[iArg].fType)
                        && offArg + iArg + 1 < (int)RT_ELEMENTS(pCtx->u.X86.aArgs))
                        u64Ret |= (uint64_t)pCtx->u.X86.aArgs[iArg + offArg + 1] << 32;
                }
                else
                    u64Ret = UINT64_MAX;
            }
        }
        else
        {
            if (iArg + offArg < (int)RT_ELEMENTS(pCtx->u.Amd64.aArgs))
                u64Ret = pCtx->u.Amd64.aArgs[iArg + offArg];
            else
                u64Ret = UINT64_MAX;
        }
    }
    else
        AssertFailedReturn(UINT64_MAX);

    LOG_DTRACE(("%s: returns %#llx\n", __FUNCTION__, u64Ret));
    return u64Ret;
}


/**
 * @callback_method_impl{dtrace_pops_t,dtps_destroy}
 */
static void    vboxDtPOps_Destroy(void *pvProv, dtrace_id_t idProbe, void *pvProbe)
{
    PSUPDRVVDTPROVIDERCORE  pProv  = (PSUPDRVVDTPROVIDERCORE)pvProv;
    AssertPtrReturnVoid(pProv);
    LOG_DTRACE(("%s: %p / %p - %#x / %p\n", __FUNCTION__, pProv, pProv->TracerData.DTrace.idProvider, idProbe, pvProbe));
    AssertReturnVoid(pProv->TracerData.DTrace.cProvidedProbes > 0);
    AssertPtrReturnVoid(pProv->TracerData.DTrace.idProvider);

    if (!pProv->TracerData.DTrace.fZombie)
    {
        uint32_t        idxProbeLoc = (uint32_t)(uintptr_t)pvProbe;
        PCVTGPROBELOC   pProbeLocRO = (PVTGPROBELOC)&pProv->paProbeLocsRO[idxProbeLoc];
        uint32_t       *pidProbe;
        if (!pProv->fUmod)
        {
            pidProbe = (uint32_t *)&pProbeLocRO->idProbe;
            Assert(!pProbeLocRO->fEnabled);
            Assert(*pidProbe == idProbe);
        }
        else
        {
            pidProbe = &pProv->paR0ProbeLocs[idxProbeLoc].idProbe;
            Assert(!pProv->paR0ProbeLocs[idxProbeLoc].fEnabled);
            Assert(*pidProbe == idProbe); NOREF(idProbe);
        }
        *pidProbe = 0;
    }
    pProv->TracerData.DTrace.cProvidedProbes--;
}



/**
 * DTrace provider method table.
 */
static const dtrace_pops_t g_vboxDtVtgProvOps =
{
    /* .dtps_provide         = */ vboxDtPOps_Provide,
    /* .dtps_provide_module  = */ NULL,
    /* .dtps_enable          = */ vboxDtPOps_Enable,
    /* .dtps_disable         = */ vboxDtPOps_Disable,
    /* .dtps_suspend         = */ NULL,
    /* .dtps_resume          = */ NULL,
    /* .dtps_getargdesc      = */ vboxDtPOps_GetArgDesc,
    /* .dtps_getargval       = */ vboxDtPOps_GetArgVal,
    /* .dtps_usermode        = */ NULL,
    /* .dtps_destroy         = */ vboxDtPOps_Destroy
};




/*
 *
 * Support Driver Tracer Interface.
 * Support Driver Tracer Interface.
 * Support Driver Tracer Interface.
 *
 */



/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProbeFireKernel}
 */
static DECLCALLBACK(void) vboxDtTOps_ProbeFireKernel(struct VTGPROBELOC *pVtgProbeLoc, uintptr_t uArg0, uintptr_t uArg1, uintptr_t uArg2,
                                                     uintptr_t uArg3, uintptr_t uArg4)
{
    AssertPtrReturnVoid(pVtgProbeLoc);
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pVtgProbeLoc, pVtgProbeLoc->idProbe));
    AssertPtrReturnVoid(pVtgProbeLoc->pProbe);
    AssertPtrReturnVoid(pVtgProbeLoc->pszFunction);

    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_ProbeFireKernel);

    pStackData->u.ProbeFireKernel.pauStackArgs  = &uArg4 + 1;

#if defined(RT_OS_DARWIN) && ARCH_BITS == 32
    /*
     * Convert arguments from uintptr_t to uint64_t.
     */
    PVTGDESCPROBE   pProbe   = pVtgProbeLoc->pProbe;
    AssertPtrReturnVoid(pProbe);
    PVTGOBJHDR      pVtgHdr  = (PVTGOBJHDR)((uintptr_t)pProbe + pProbe->offObjHdr);
    AssertPtrReturnVoid(pVtgHdr);
    PVTGDESCARGLIST pArgList = (PVTGDESCARGLIST)((uintptr_t)pVtgHdr + pVtgHdr->offArgLists + pProbe->offArgList);
    AssertPtrReturnVoid(pArgList);
    if (!pArgList->fHaveLargeArgs)
        dtrace_probe(pVtgProbeLoc->idProbe, uArg0, uArg1, uArg2, uArg3, uArg4);
    else
    {
        uintptr_t *auSrcArgs = &uArg0;
        uint32_t   iSrcArg   = 0;
        uint32_t   iDstArg   = 0;
        uint64_t   au64DstArgs[5];

        while (   iDstArg < RT_ELEMENTS(au64DstArgs)
               && iSrcArg < pArgList->cArgs)
        {
            au64DstArgs[iDstArg] = auSrcArgs[iSrcArg];
            if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iDstArg].fType))
                au64DstArgs[iDstArg] |= (uint64_t)auSrcArgs[++iSrcArg] << 32;
            iSrcArg++;
            iDstArg++;
        }
        while (iDstArg < RT_ELEMENTS(au64DstArgs))
            au64DstArgs[iDstArg++] = auSrcArgs[iSrcArg++];

        pStackData->u.ProbeFireKernel.pauStackArgs = &auSrcArgs[iSrcArg];
        dtrace_probe(pVtgProbeLoc->idProbe, au64DstArgs[0], au64DstArgs[1], au64DstArgs[2], au64DstArgs[3], au64DstArgs[4]);
    }
#else
    dtrace_probe(pVtgProbeLoc->idProbe, uArg0, uArg1, uArg2, uArg3, uArg4);
#endif

    VBDT_CLEAR_STACK_DATA();
    LOG_DTRACE(("%s: returns\n", __FUNCTION__));
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProbeFireUser}
 */
static DECLCALLBACK(void) vboxDtTOps_ProbeFireUser(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, PCSUPDRVTRACERUSRCTX pCtx,
                                                   PCVTGOBJHDR pVtgHdr, PCVTGPROBELOC pProbeLocRO)
{
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pCtx, pCtx->idProbe));
    AssertPtrReturnVoid(pProbeLocRO);
    AssertPtrReturnVoid(pVtgHdr);
    RT_NOREF_PV(pThis);
    RT_NOREF_PV(pSession);
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_ProbeFireUser);

    if (pCtx->cBits == 32)
    {
        pStackData->u.ProbeFireUser.pCtx   = pCtx;
        pStackData->u.ProbeFireUser.offArg = 0;

#if ARCH_BITS == 64 || defined(RT_OS_DARWIN)
        /*
         * Combine two 32-bit arguments into one 64-bit argument where needed.
         */
        PVTGDESCPROBE   pProbeDesc = pProbeLocRO->pProbe;
        AssertPtrReturnVoid(pProbeDesc);
        PVTGDESCARGLIST pArgList   = (PVTGDESCARGLIST)((uintptr_t)pVtgHdr + pVtgHdr->offArgLists + pProbeDesc->offArgList);
        AssertPtrReturnVoid(pArgList);

        if (!pArgList->fHaveLargeArgs)
            dtrace_probe(pCtx->idProbe,
                         pCtx->u.X86.aArgs[0],
                         pCtx->u.X86.aArgs[1],
                         pCtx->u.X86.aArgs[2],
                         pCtx->u.X86.aArgs[3],
                         pCtx->u.X86.aArgs[4]);
        else
        {
            uint32_t const *auSrcArgs = &pCtx->u.X86.aArgs[0];
            uint32_t        iSrcArg   = 0;
            uint32_t        iDstArg   = 0;
            uint64_t        au64DstArgs[5];

            while (   iDstArg < RT_ELEMENTS(au64DstArgs)
                   && iSrcArg < pArgList->cArgs)
            {
                au64DstArgs[iDstArg] = auSrcArgs[iSrcArg];
                if (VTG_TYPE_IS_LARGE(pArgList->aArgs[iDstArg].fType))
                    au64DstArgs[iDstArg] |= (uint64_t)auSrcArgs[++iSrcArg] << 32;
                iSrcArg++;
                iDstArg++;
            }
            while (iDstArg < RT_ELEMENTS(au64DstArgs))
                au64DstArgs[iDstArg++] = auSrcArgs[iSrcArg++];

            pStackData->u.ProbeFireUser.offArg = iSrcArg - RT_ELEMENTS(au64DstArgs);
            dtrace_probe(pCtx->idProbe, au64DstArgs[0], au64DstArgs[1], au64DstArgs[2], au64DstArgs[3], au64DstArgs[4]);
        }
#else
        dtrace_probe(pCtx->idProbe,
                     pCtx->u.X86.aArgs[0],
                     pCtx->u.X86.aArgs[1],
                     pCtx->u.X86.aArgs[2],
                     pCtx->u.X86.aArgs[3],
                     pCtx->u.X86.aArgs[4]);
#endif
    }
    else if (pCtx->cBits == 64)
    {
        pStackData->u.ProbeFireUser.pCtx   = pCtx;
        pStackData->u.ProbeFireUser.offArg = 0;
        dtrace_probe(pCtx->idProbe,
                     pCtx->u.Amd64.aArgs[0],
                     pCtx->u.Amd64.aArgs[1],
                     pCtx->u.Amd64.aArgs[2],
                     pCtx->u.Amd64.aArgs[3],
                     pCtx->u.Amd64.aArgs[4]);
    }
    else
        AssertFailed();

    VBDT_CLEAR_STACK_DATA();
    LOG_DTRACE(("%s: returns\n", __FUNCTION__));
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnTracerOpen}
 */
static DECLCALLBACK(int) vboxDtTOps_TracerOpen(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uint32_t uCookie,
                                               uintptr_t uArg, uintptr_t *puSessionData)
{
    if (uCookie != RT_MAKE_U32_FROM_U8('V', 'B', 'D', 'T'))
        return VERR_INVALID_MAGIC;
    if (uArg)
        return VERR_INVALID_PARAMETER;
    RT_NOREF_PV(pThis); RT_NOREF_PV(pSession);
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_Generic);

    int rc = dtrace_open((dtrace_state_t **)puSessionData, VBoxDtGetCurrentCreds());

    VBDT_CLEAR_STACK_DATA();
    return RTErrConvertFromErrno(rc);
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnTracerClose}
 */
static DECLCALLBACK(int) vboxDtTOps_TracerIoCtl(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uintptr_t uSessionData,
                                                uintptr_t uCmd, uintptr_t uArg, int32_t *piRetVal)
{
    AssertPtrReturn(uSessionData, VERR_INVALID_POINTER);
    RT_NOREF_PV(pThis); RT_NOREF_PV(pSession);
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_Generic);

    int rc = dtrace_ioctl((dtrace_state_t *)uSessionData, (intptr_t)uCmd, (intptr_t)uArg, piRetVal);

    VBDT_CLEAR_STACK_DATA();
    return RTErrConvertFromErrno(rc);
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnTracerClose}
 */
static DECLCALLBACK(void) vboxDtTOps_TracerClose(PCSUPDRVTRACERREG pThis, PSUPDRVSESSION pSession, uintptr_t uSessionData)
{
    AssertPtrReturnVoid(uSessionData);
    RT_NOREF_PV(pThis); RT_NOREF_PV(pSession);
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_Generic);

    dtrace_close((dtrace_state_t *)uSessionData);

    VBDT_CLEAR_STACK_DATA();
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProviderRegister}
 */
static DECLCALLBACK(int) vboxDtTOps_ProviderRegister(PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore)
{
    LOG_DTRACE(("%s: %p %s/%s\n", __FUNCTION__, pThis, pCore->pszModName, pCore->pszName));
    AssertReturn(pCore->TracerData.DTrace.idProvider == 0, VERR_INTERNAL_ERROR_3);
    RT_NOREF_PV(pThis);
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_Generic);

    PVTGDESCPROVIDER    pDesc = pCore->pDesc;
    dtrace_pattr_t      DtAttrs;
    vboxDtVtgConvAttr(&DtAttrs.dtpa_provider, &pDesc->AttrSelf);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_mod,      &pDesc->AttrModules);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_func,     &pDesc->AttrFunctions);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_name,     &pDesc->AttrNames);
    vboxDtVtgConvAttr(&DtAttrs.dtpa_args,     &pDesc->AttrArguments);

    /* Note! DTrace may call us back before dtrace_register returns, so we
             have to point it to pCore->TracerData.DTrace.idProvider. */
    AssertCompile(sizeof(dtrace_provider_id_t) == sizeof(pCore->TracerData.DTrace.idProvider));
    int rc = dtrace_register(pCore->pszName,
                             &DtAttrs,
                             DTRACE_PRIV_KERNEL,
                             NULL /* cred */,
                             &g_vboxDtVtgProvOps,
                             pCore,
                             &pCore->TracerData.DTrace.idProvider);
    if (!rc)
    {
        LOG_DTRACE(("%s: idProvider=%p\n", __FUNCTION__, pCore->TracerData.DTrace.idProvider));
        AssertPtr(pCore->TracerData.DTrace.idProvider);
        rc = VINF_SUCCESS;
    }
    else
    {
        pCore->TracerData.DTrace.idProvider = 0;
        rc = RTErrConvertFromErrno(rc);
    }

    VBDT_CLEAR_STACK_DATA();
    LOG_DTRACE(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProviderDeregister}
 */
static DECLCALLBACK(int) vboxDtTOps_ProviderDeregister(PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore)
{
    uintptr_t idProvider = pCore->TracerData.DTrace.idProvider;
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pThis, idProvider));
    AssertPtrReturn(idProvider, VERR_INTERNAL_ERROR_3);
    RT_NOREF_PV(pThis);
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_Generic);

    dtrace_invalidate(idProvider);
    int rc = dtrace_unregister(idProvider);
    if (!rc)
    {
        pCore->TracerData.DTrace.idProvider = 0;
        rc = VINF_SUCCESS;
    }
    else
    {
        AssertMsg(rc == EBUSY, ("%d\n", rc));
        pCore->TracerData.DTrace.fZombie = true;
        rc = VERR_TRY_AGAIN;
    }

    VBDT_CLEAR_STACK_DATA();
    LOG_DTRACE(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}


/**
 * interface_method_impl{SUPDRVTRACERREG,pfnProviderDeregisterZombie}
 */
static DECLCALLBACK(int) vboxDtTOps_ProviderDeregisterZombie(PCSUPDRVTRACERREG pThis, PSUPDRVVDTPROVIDERCORE pCore)
{
    uintptr_t idProvider = pCore->TracerData.DTrace.idProvider;
    LOG_DTRACE(("%s: %p / %p\n", __FUNCTION__, pThis, idProvider));
    AssertPtrReturn(idProvider, VERR_INTERNAL_ERROR_3);
    Assert(pCore->TracerData.DTrace.fZombie);
    RT_NOREF_PV(pThis);
    VBDT_SETUP_STACK_DATA(kVBoxDtCaller_Generic);

    int rc = dtrace_unregister(idProvider);
    if (!rc)
    {
        pCore->TracerData.DTrace.idProvider = 0;
        rc = VINF_SUCCESS;
    }
    else
    {
        AssertMsg(rc == EBUSY, ("%d\n", rc));
        rc = VERR_TRY_AGAIN;
    }

    VBDT_CLEAR_STACK_DATA();
    LOG_DTRACE(("%s: returns %Rrc\n", __FUNCTION__, rc));
    return rc;
}



/**
 * The tracer registration record of the VBox DTrace implementation
 */
static SUPDRVTRACERREG g_VBoxDTraceReg =
{
    SUPDRVTRACERREG_MAGIC,
    SUPDRVTRACERREG_VERSION,
    vboxDtTOps_ProbeFireKernel,
    vboxDtTOps_ProbeFireUser,
    vboxDtTOps_TracerOpen,
    vboxDtTOps_TracerIoCtl,
    vboxDtTOps_TracerClose,
    vboxDtTOps_ProviderRegister,
    vboxDtTOps_ProviderDeregister,
    vboxDtTOps_ProviderDeregisterZombie,
    SUPDRVTRACERREG_MAGIC
};



/**
 * Module termination code.
 *
 * @param   hMod            Opque module handle.
 */
DECLEXPORT(void) ModuleTerm(void *hMod)
{
    SUPR0TracerDeregisterImpl(hMod, NULL);
    dtrace_detach();
    vboxDtTermThreadDb();
}


/**
 * Module initialization code.
 *
 * @param   hMod            Opque module handle.
 */
DECLEXPORT(int)  ModuleInit(void *hMod)
{
    int rc = vboxDtInitThreadDb();
    if (RT_SUCCESS(rc))
    {
        rc = dtrace_attach();
        if (rc == DDI_SUCCESS)
        {
            rc = SUPR0TracerRegisterImpl(hMod, NULL, &g_VBoxDTraceReg, &g_pVBoxDTraceHlp);
            if (RT_SUCCESS(rc))
                return rc;

            dtrace_detach();
        }
        else
        {
            SUPR0Printf("dtrace_attach -> %d\n", rc);
            rc = VERR_INTERNAL_ERROR_5;
        }
        vboxDtTermThreadDb();
    }
    else
        SUPR0Printf("vboxDtInitThreadDb -> %d\n", rc);

    return rc;
}

