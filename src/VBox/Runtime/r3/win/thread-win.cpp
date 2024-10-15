/* $Id: thread-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Threads, Windows.
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
#define LOG_GROUP RTLOGGROUP_THREAD
#include <iprt/nt/nt-and-windows.h>

#ifndef IPRT_NO_CRT
# include <errno.h>
# include <process.h>
#endif

#include <iprt/thread.h>
#include "internal/iprt.h"

#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include "internal/thread.h"
#include "internal-r3-win.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** SetThreadDescription */
typedef HRESULT (WINAPI *PFNSETTHREADDESCRIPTION)(HANDLE hThread, WCHAR *pwszName); /* Since W10 1607 */

/** CoInitializeEx */
typedef HRESULT (WINAPI *PFNCOINITIALIZEEX)(LPVOID, DWORD);
/** CoUninitialize */
typedef void (WINAPI *PFNCOUNINITIALIZE)(void);
/** OleUninitialize */
typedef void (WINAPI *PFNOLEUNINITIALIZE)(void);



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The TLS index allocated for storing the RTTHREADINT pointer. */
static DWORD                    g_dwSelfTLS = TLS_OUT_OF_INDEXES;
/** Pointer to SetThreadDescription (KERNEL32.DLL) if available. */
static PFNSETTHREADDESCRIPTION  g_pfnSetThreadDescription = NULL;

/** Pointer to CoInitializeEx (OLE32.DLL / combase.dll) if available. */
static PFNCOINITIALIZEEX volatile   g_pfnCoInitializeEx  = NULL;
/** Pointer to CoUninitialize (OLE32.DLL / combase.dll) if available. */
static PFNCOUNINITIALIZE volatile   g_pfnCoUninitialize  = NULL;
/** Pointer to OleUninitialize (OLE32.DLL / combase.dll) if available. */
static PFNOLEUNINITIALIZE volatile  g_pfnOleUninitialize = NULL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtThreadWinTellDebuggerThreadName(uint32_t idThread, const char *pszName);
DECLINLINE(void) rtThreadWinSetThreadName(PRTTHREADINT pThread, DWORD idThread);


DECLHIDDEN(int) rtThreadNativeInit(void)
{
    g_dwSelfTLS = TlsAlloc();
    if (g_dwSelfTLS == TLS_OUT_OF_INDEXES)
        return VERR_NO_TLS_FOR_SELF;

    g_pfnSetThreadDescription = (PFNSETTHREADDESCRIPTION)GetProcAddress(g_hModKernel32, "SetThreadDescription");
    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtThreadNativeReInitObtrusive(void)
{
    /* nothing to do here. */
}


DECLHIDDEN(void) rtThreadNativeDetach(void)
{
    /*
     * Deal with alien threads.
     */
    PRTTHREADINT pThread = (PRTTHREADINT)TlsGetValue(g_dwSelfTLS);
    if (    pThread
        &&  (pThread->fIntFlags & RTTHREADINT_FLAGS_ALIEN))
    {
        rtThreadTerminate(pThread, 0);
        TlsSetValue(g_dwSelfTLS, NULL);
    }
}


DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    if (pThread == (PRTTHREADINT)TlsGetValue(g_dwSelfTLS))
        TlsSetValue(g_dwSelfTLS, NULL);

    if ((HANDLE)pThread->hThread != INVALID_HANDLE_VALUE)
    {
        CloseHandle((HANDLE)pThread->hThread);
        pThread->hThread = (uintptr_t)INVALID_HANDLE_VALUE;
    }
}


DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    if (!TlsSetValue(g_dwSelfTLS, pThread))
        return VERR_FAILED_TO_SET_SELF_TLS;
    rtThreadWinSetThreadName(pThread, GetCurrentThreadId());
    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtThreadNativeInformDebugger(PRTTHREADINT pThread)
{
    rtThreadWinTellDebuggerThreadName((uint32_t)(uintptr_t)pThread->Core.Key, pThread->szName);
}


/**
 * Communicates the thread name to the debugger, if we're begin debugged that
 * is.
 *
 * See http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx for debugger
 * interface details.
 *
 * @param   idThread        The thread ID. UINT32_MAX for current thread.
 * @param   pszName         The name.
 */
static void rtThreadWinTellDebuggerThreadName(uint32_t idThread, const char *pszName)
{
    struct
    {
        uint32_t    uType;
        const char *pszName;
        uint32_t    idThread;
        uint32_t    fFlags;
    } Pkg = { 0x1000, pszName, idThread, 0 };
    __try
    {
        RaiseException(0x406d1388, 0, sizeof(Pkg)/sizeof(ULONG_PTR), (ULONG_PTR *)&Pkg);
    }
    __except(EXCEPTION_CONTINUE_EXECUTION)
    {

    }
}


/**
 * Sets the thread name as best as we can.
 */
DECLINLINE(void) rtThreadWinSetThreadName(PRTTHREADINT pThread, DWORD idThread)
{
    if (g_pfnIsDebuggerPresent && g_pfnIsDebuggerPresent())
        rtThreadWinTellDebuggerThreadName(idThread, &pThread->szName[0]);

    /* The SetThreadDescription API introduced in windows 10 1607 / server 2016
       allows setting the thread name while the debugger isn't attached.  Works
       with WinDbgX, VisualStudio 2017 v15.6+, and presumeably some recent windbg
       version. */
    if (g_pfnSetThreadDescription)
    {
        /* The name should be ASCII, so we just need to expand 'char' to 'WCHAR'. */
        WCHAR wszName[RTTHREAD_NAME_LEN];
        for (size_t i = 0; i < RTTHREAD_NAME_LEN; i++)
            wszName[i] = pThread->szName[i];

        HRESULT hrc = g_pfnSetThreadDescription(GetCurrentThread(), wszName);
        Assert(SUCCEEDED(hrc)); RT_NOREF(hrc);
    }
}


/**
 * Bitch about dangling COM and OLE references, dispose of them
 * afterwards so we don't end up deadlocked somewhere below
 * OLE32!DllMain.
 */
static void rtThreadNativeUninitComAndOle(void)
{
#if 1 /* experimental code */
    /*
     * Read the counters.
     */
    struct MySOleTlsData
    {
        void       *apvReserved0[2];    /**< x86=0x00  W7/64=0x00 */
        DWORD       adwReserved0[3];    /**< x86=0x08  W7/64=0x10 */
        void       *apvReserved1[1];    /**< x86=0x14  W7/64=0x20 */
        DWORD       cComInits;          /**< x86=0x18  W7/64=0x28 */
        DWORD       cOleInits;          /**< x86=0x1c  W7/64=0x2c */
        DWORD       dwReserved1;        /**< x86=0x20  W7/64=0x30 */
        void       *apvReserved2[4];    /**< x86=0x24  W7/64=0x38 */
        DWORD       adwReserved2[1];    /**< x86=0x34  W7/64=0x58 */
        void       *pvCurrentCtx;       /**< x86=0x38  W7/64=0x60 */
        IUnknown   *pCallState;         /**< x86=0x3c  W7/64=0x68 */
    }      *pOleTlsData = NULL;         /* outside the try/except for debugging */
    DWORD   cComInits   = 0;
    DWORD   cOleInits   = 0;
    __try
    {
        void *pvTeb = NtCurrentTeb();
# ifdef RT_ARCH_AMD64
        pOleTlsData = *(struct MySOleTlsData **)((uintptr_t)pvTeb + 0x1758); /*TEB.ReservedForOle*/
# elif RT_ARCH_X86
        pOleTlsData = *(struct MySOleTlsData **)((uintptr_t)pvTeb + 0x0f80); /*TEB.ReservedForOle*/
# else
#  error "Port me!"
# endif
        if (pOleTlsData)
        {
            cComInits = pOleTlsData->cComInits;
            cOleInits = pOleTlsData->cOleInits;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        AssertFailedReturnVoid();
    }

    /*
     * Assert sanity. If any of these breaks, the structure layout above is
     * probably not correct any longer.
     */
    AssertMsgReturnVoid(cComInits < 1000, ("%u (%#x)\n", cComInits, cComInits));
    AssertMsgReturnVoid(cOleInits < 1000, ("%u (%#x)\n", cOleInits, cOleInits));
    AssertMsgReturnVoid(cComInits >= cOleInits, ("cComInits=%#x cOleInits=%#x\n", cComInits, cOleInits));

    /*
     * Do the uninitializing.
     */
    if (cComInits)
    {
        AssertMsgFailed(("cComInits=%u (%#x) cOleInits=%u (%#x) - dangling COM/OLE inits!\n",
                         cComInits, cComInits, cOleInits, cOleInits));

        PFNOLEUNINITIALIZE  pfnOleUninitialize = g_pfnOleUninitialize;
        PFNCOUNINITIALIZE   pfnCoUninitialize  = g_pfnCoUninitialize;
        if (pfnCoUninitialize && pfnOleUninitialize)
        { /* likely */ }
        else
        {
            HMODULE hOle32 = GetModuleHandle("ole32.dll");
            AssertReturnVoid(hOle32 != NULL);

            pfnOleUninitialize = (PFNOLEUNINITIALIZE)GetProcAddress(hOle32, "OleUninitialize");
            AssertReturnVoid(pfnOleUninitialize);

            pfnCoUninitialize  = (PFNCOUNINITIALIZE)GetProcAddress(hOle32, "CoUninitialize");
            AssertReturnVoid(pfnCoUninitialize);
        }

        while (cOleInits-- > 0)
        {
            pfnOleUninitialize();
            cComInits--;
        }

        while (cComInits-- > 0)
            pfnCoUninitialize();
    }
#endif
}


/**
 * Implements the RTTHREADFLAGS_COM_MTA and RTTHREADFLAGS_COM_STA flags.
 *
 * @returns true if COM uninitialization should be done, false if not.
 * @param   fFlags      The thread flags.
 */
static bool rtThreadNativeWinCoInitialize(unsigned fFlags)
{
    /*
     * Resolve the ole32 init and uninit functions dynamically.
     */
    PFNCOINITIALIZEEX pfnCoInitializeEx = g_pfnCoInitializeEx;
    PFNCOUNINITIALIZE pfnCoUninitialize = g_pfnCoUninitialize;
    if (pfnCoInitializeEx && pfnCoUninitialize)
    { /* likely */ }
    else
    {
        RTLDRMOD hModOle32 = NIL_RTLDRMOD;
        int rc = RTLdrLoadSystem("ole32.dll", true /*fNoUnload*/, &hModOle32);
        AssertRCReturn(rc, false);

        PFNOLEUNINITIALIZE pfnOleUninitialize;
        pfnOleUninitialize = (PFNOLEUNINITIALIZE)RTLdrGetFunction(hModOle32, "OleUninitialize");
        pfnCoUninitialize  = (PFNCOUNINITIALIZE )RTLdrGetFunction(hModOle32, "CoUninitialize");
        pfnCoInitializeEx  = (PFNCOINITIALIZEEX )RTLdrGetFunction(hModOle32, "CoInitializeEx");

        RTLdrClose(hModOle32);
        AssertReturn(pfnCoInitializeEx && pfnCoUninitialize, false);

        if (pfnOleUninitialize && !g_pfnOleUninitialize)
            g_pfnOleUninitialize = pfnOleUninitialize;
        g_pfnCoInitializeEx = pfnCoInitializeEx;
        g_pfnCoUninitialize = pfnCoUninitialize;
    }

    /*
     * Do the initializating.
     */
    DWORD fComInit;
    if (fFlags & RTTHREADFLAGS_COM_MTA)
        fComInit = COINIT_MULTITHREADED     | COINIT_SPEED_OVER_MEMORY | COINIT_DISABLE_OLE1DDE;
    else
        fComInit = COINIT_APARTMENTTHREADED | COINIT_SPEED_OVER_MEMORY;
    HRESULT hrc = pfnCoInitializeEx(NULL, fComInit);
    AssertMsg(SUCCEEDED(hrc), ("%Rhrc fComInit=%#x\n", hrc, fComInit));
    return SUCCEEDED(hrc);
}


/**
 * Wrapper which unpacks the param stuff and calls thread function.
 */
#ifndef IPRT_NO_CRT
static unsigned __stdcall rtThreadNativeMain(void *pvArgs) RT_NOTHROW_DEF
#else
static DWORD __stdcall rtThreadNativeMain(void *pvArgs) RT_NOTHROW_DEF
#endif
{
    DWORD           dwThreadId = GetCurrentThreadId();
    PRTTHREADINT    pThread = (PRTTHREADINT)pvArgs;

    if (!TlsSetValue(g_dwSelfTLS, pThread))
        AssertReleaseMsgFailed(("failed to set self TLS. lasterr=%d thread '%s'\n", GetLastError(), pThread->szName));
    rtThreadWinSetThreadName(pThread, dwThreadId);

    bool fUninitCom = (pThread->fFlags & (RTTHREADFLAGS_COM_MTA | RTTHREADFLAGS_COM_STA)) != 0;
    if (fUninitCom)
        fUninitCom = rtThreadNativeWinCoInitialize(pThread->fFlags);

    int rc = rtThreadMain(pThread, dwThreadId, &pThread->szName[0]);

    TlsSetValue(g_dwSelfTLS, NULL); /* rtThreadMain already released the structure. */

    if (fUninitCom && g_pfnCoUninitialize)
        g_pfnCoUninitialize();

    rtThreadNativeUninitComAndOle();
#ifndef IPRT_NO_CRT
    _endthreadex(rc);
    return rc; /* not reached */
#else
    for (;;)
        ExitThread(rc);
#endif
}


DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThread, PRTNATIVETHREAD pNativeThread)
{
    AssertReturn(pThread->cbStack < ~(unsigned)0, VERR_INVALID_PARAMETER);

    /*
     * If a stack size is given, make sure it's not a multiple of 64KB so that we
     * get one or more pages for overflow protection.  (ASSUMES 64KB alloc align.)
     */
    unsigned cbStack = (unsigned)pThread->cbStack;
    if (cbStack > 0 && RT_ALIGN_T(cbStack, _64K, unsigned) == cbStack)
        cbStack += PAGE_SIZE;

    /*
     * Create the thread.
     */
    pThread->hThread = (uintptr_t)INVALID_HANDLE_VALUE;
#ifndef IPRT_NO_CRT
    unsigned    uThreadId = 0;
    uintptr_t   hThread   = _beginthreadex(NULL /*pSecAttrs*/, cbStack, rtThreadNativeMain, pThread, 0 /*fFlags*/, &uThreadId);
    if (hThread != 0 && hThread != ~0U)
    {
        pThread->hThread = hThread;
        *pNativeThread = uThreadId;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromErrno(errno);
#else
    DWORD  idThread = 0;
    HANDLE hThread = CreateThread(NULL /*pSecAttrs*/, cbStack, rtThreadNativeMain, pThread, 0 /*fFlags*/, &idThread);
    if (hThread != NULL)
    {
        pThread->hThread = (uintptr_t)hThread;
        *pNativeThread = idThread;
        return VINF_SUCCESS;
    }
    return RTErrConvertFromWin32(GetLastError());
#endif
}


DECLHIDDEN(bool) rtThreadNativeIsAliveKludge(PRTTHREADINT pThread)
{
    PPEB_COMMON pPeb = NtCurrentPeb();
    if (!pPeb || !pPeb->Ldr || !pPeb->Ldr->ShutdownInProgress)
        return true;
    DWORD rcWait = WaitForSingleObject((HANDLE)pThread->hThread, 0);
    return rcWait != WAIT_OBJECT_0;
}


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    PRTTHREADINT pThread = (PRTTHREADINT)TlsGetValue(g_dwSelfTLS);
    /** @todo import alien threads ? */
    return pThread;
}


#if 0 /* noone is using this ... */
/**
 * Returns the processor number the current thread was running on during this call
 *
 * @returns processor nr
 */
static int rtThreadGetCurrentProcessorNumber(void)
{
    static bool           fInitialized = false;
    static DWORD (WINAPI *pfnGetCurrentProcessorNumber)(void) = NULL;
    if (!fInitialized)
    {
        HMODULE hmodKernel32 = GetModuleHandle("kernel32.dll");
        if (hmodKernel32)
            pfnGetCurrentProcessorNumber = (DWORD (WINAPI*)(void))GetProcAddress(hmodKernel32, "GetCurrentProcessorNumber");
        fInitialized = true;
    }
    if (pfnGetCurrentProcessorNumber)
        return pfnGetCurrentProcessorNumber();
    return -1;
}
#endif


RTR3DECL(int) RTThreadSetAffinity(PCRTCPUSET pCpuSet)
{
    /* The affinity functionality was added in NT 3.50, so we resolve the APIs
       dynamically to be able to run on NT 3.1. */
    if (g_pfnSetThreadAffinityMask)
    {
        DWORD_PTR fNewMask = pCpuSet ? RTCpuSetToU64(pCpuSet) : ~(DWORD_PTR)0;
        DWORD_PTR dwRet = g_pfnSetThreadAffinityMask(GetCurrentThread(), fNewMask);
        if (dwRet)
            return VINF_SUCCESS;

        int iLastError = GetLastError();
        AssertMsgFailed(("SetThreadAffinityMask failed, LastError=%d\n", iLastError));
        return RTErrConvertFromWin32(iLastError);
    }
    return VERR_NOT_SUPPORTED;
}


RTR3DECL(int) RTThreadGetAffinity(PRTCPUSET pCpuSet)
{
    /* The affinity functionality was added in NT 3.50, so we resolve the APIs
       dynamically to be able to run on NT 3.1. */
    if (   g_pfnSetThreadAffinityMask
        && g_pfnGetProcessAffinityMask)
    {
        /*
         * Haven't found no query api, but the set api returns the old mask, so let's use that.
         */
        DWORD_PTR dwIgnored;
        DWORD_PTR dwProcAff = 0;
        if (g_pfnGetProcessAffinityMask(GetCurrentProcess(), &dwProcAff, &dwIgnored))
        {
            HANDLE hThread = GetCurrentThread();
            DWORD_PTR dwRet = g_pfnSetThreadAffinityMask(hThread, dwProcAff);
            if (dwRet)
            {
                DWORD_PTR dwSet = g_pfnSetThreadAffinityMask(hThread, dwRet);
                Assert(dwSet == dwProcAff); NOREF(dwRet);

                RTCpuSetFromU64(pCpuSet, (uint64_t)dwSet);
                return VINF_SUCCESS;
            }
        }

        int iLastError = GetLastError();
        AssertMsgFailed(("SetThreadAffinityMask or GetProcessAffinityMask failed, LastError=%d\n", iLastError));
        return RTErrConvertFromWin32(iLastError);
    }
    return VERR_NOT_SUPPORTED;
}


RTR3DECL(int) RTThreadGetExecutionTimeMilli(uint64_t *pcMsKernelTime, uint64_t *pcMsUserTime)
{
    uint64_t u64CreationTime, u64ExitTime, u64KernelTime, u64UserTime;

    if (GetThreadTimes(GetCurrentThread(), (LPFILETIME)&u64CreationTime, (LPFILETIME)&u64ExitTime, (LPFILETIME)&u64KernelTime, (LPFILETIME)&u64UserTime))
    {
        *pcMsKernelTime = u64KernelTime / 10000;    /* GetThreadTimes returns time in 100 ns units */
        *pcMsUserTime   = u64UserTime / 10000;    /* GetThreadTimes returns time in 100 ns units */
        return VINF_SUCCESS;
    }

    DWORD const dwErr = GetLastError();
    AssertMsgFailed(("GetThreadTimes failed, LastError=%d\n", dwErr));
    return RTErrConvertFromWin32(dwErr);
}


/**
 * Gets the native thread handle for a IPRT thread.
 *
 * @returns The thread handle. INVALID_HANDLE_VALUE on failure.
 * @param   hThread     The IPRT thread handle.
 *
 * @note    Windows only.
 * @note    Only valid after parent returns from the thread creation call.
 */
RTDECL(uintptr_t) RTThreadGetNativeHandle(RTTHREAD hThread)
{
    PRTTHREADINT pThread = rtThreadGet(hThread);
    if (pThread)
    {
        uintptr_t hHandle = pThread->hThread;
        rtThreadRelease(pThread);
        return hHandle;
    }
    return (uintptr_t)INVALID_HANDLE_VALUE;
}
RT_EXPORT_SYMBOL(RTThreadGetNativeHandle);


RTDECL(int) RTThreadPoke(RTTHREAD hThread)
{
    AssertReturn(hThread != RTThreadSelf(), VERR_INVALID_PARAMETER);
    if (g_pfnNtAlertThread)
    {
        PRTTHREADINT pThread = rtThreadGet(hThread);
        AssertReturn(pThread, VERR_INVALID_HANDLE);

        NTSTATUS rcNt = g_pfnNtAlertThread((HANDLE)pThread->hThread);

        rtThreadRelease(pThread);
        if (NT_SUCCESS(rcNt))
            return VINF_SUCCESS;
        return RTErrConvertFromNtStatus(rcNt);
    }
    return VERR_NOT_IMPLEMENTED;
}

