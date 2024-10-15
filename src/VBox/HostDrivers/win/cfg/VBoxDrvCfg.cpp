/* $Id: VBoxDrvCfg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDrvCfg.cpp - Windows Driver Manipulation API implementation.
 *
 * @note This is EXTREMELY BADLY documented code. Please help improve by
 *       adding comments whenever you've got a chance!
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
#include <VBox/VBoxDrvCfg-win.h>

#include <iprt/win/setupapi.h>
#include <iprt/win/shlobj.h>
#include <Newdev.h>

#include <iprt/alloca.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/utf16.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static PFNVBOXDRVCFGLOG g_pfnVBoxDrvCfgLog;
static void *g_pvVBoxDrvCfgLog;

static PFNVBOXDRVCFGPANIC g_pfnVBoxDrvCfgPanic;
static void *g_pvVBoxDrvCfgPanic;


VBOXDRVCFG_DECL(void) VBoxDrvCfgLoggerSet(PFNVBOXDRVCFGLOG pfnLog, void *pvLog)
{
    g_pfnVBoxDrvCfgLog = pfnLog;
    g_pvVBoxDrvCfgLog = pvLog;
}

VBOXDRVCFG_DECL(void) VBoxDrvCfgPanicSet(PFNVBOXDRVCFGPANIC pfnPanic, void *pvPanic)
{
    g_pfnVBoxDrvCfgPanic = pfnPanic;
    g_pvVBoxDrvCfgPanic = pvPanic;
}

static void vboxDrvCfgLogRel(const char *pszFormat, ...)
{
    PFNVBOXDRVCFGLOG pfnLog = g_pfnVBoxDrvCfgLog;
    void *pvLog = g_pvVBoxDrvCfgLog;
    if (pfnLog)
    {
        char szBuffer[4096];
        va_list va;
        va_start(va, pszFormat);
        RTStrPrintfV(szBuffer, RT_ELEMENTS(szBuffer), pszFormat, va);
        va_end(va);
        pfnLog(VBOXDRVCFG_LOG_SEVERITY_REL, szBuffer, pvLog);
    }
}

static void vboxDrvCfgLogRegular(const char *pszFormat, ...)
{
    PFNVBOXDRVCFGLOG pfnLog = g_pfnVBoxDrvCfgLog;
    void *pvLog = g_pvVBoxDrvCfgLog;
    if (pfnLog)
    {
        char szBuffer[4096];
        va_list va;
        va_start(va, pszFormat);
        RTStrPrintfV(szBuffer, RT_ELEMENTS(szBuffer), pszFormat, va);
        va_end(va);
        pfnLog(VBOXDRVCFG_LOG_SEVERITY_REGULAR, szBuffer, pvLog);
    }
}

static void vboxDrvCfgLogFlow(const char *pszFormat, ...)
{
    PFNVBOXDRVCFGLOG pfnLog = g_pfnVBoxDrvCfgLog;
    void *pvLog = g_pvVBoxDrvCfgLog;
    if (pfnLog)
    {
        char szBuffer[4096];
        va_list va;
        va_start(va, pszFormat);
        RTStrPrintfV(szBuffer, RT_ELEMENTS(szBuffer), pszFormat, va);
        va_end(va);
        pfnLog(VBOXDRVCFG_LOG_SEVERITY_FLOW, szBuffer, pvLog);
    }
}

static void vboxDrvCfgPanic(void)
{
    PFNVBOXDRVCFGPANIC pfnPanic = g_pfnVBoxDrvCfgPanic;
    void *pvPanic = g_pvVBoxDrvCfgPanic;
    if (pfnPanic)
        pfnPanic(pvPanic);
}

/* we do not use IPRT Logging because the lib is used in host installer and needs to
 * post its msgs to MSI logger */
#define NonStandardLogCrap(_m)     do { vboxDrvCfgLogRegular _m ; } while (0)
#define NonStandardLogFlowCrap(_m) do { vboxDrvCfgLogFlow _m ; } while (0)
#define NonStandardLogRelCrap(_m)  do { vboxDrvCfgLogRel _m ; } while (0)
#define NonStandardAssertFailed() vboxDrvCfgPanic()
#define NonStandardAssert(_m) do { \
        if (RT_UNLIKELY(!(_m))) {  vboxDrvCfgPanic(); } \
    } while (0)


/**
 * This is a simple string vector class.
 *
 * @note Is is _NOT_ a list as the name could lead you to believe, but a vector.
 */
class VBoxDrvCfgStringList
{
public:
    VBoxDrvCfgStringList(size_t a_cElements);
    ~VBoxDrvCfgStringList();

    HRESULT add(LPWSTR pStr);

    size_t size() { return m_cUsed; }

    LPWSTR get(size_t i) { return i < m_cUsed ? m_paStrings[i] : NULL; }
private:
    HRESULT grow(size_t a_cNew);

    /** Array of strings. */
    LPWSTR *m_paStrings;
    size_t  m_cAllocated;
    size_t  m_cUsed;
};

VBoxDrvCfgStringList::VBoxDrvCfgStringList(size_t a_cElements)
{
    m_paStrings  = (LPWSTR *)RTMemAllocZ(sizeof(m_paStrings[0]) * a_cElements);
    m_cAllocated = a_cElements;
    m_cUsed      = 0;
}

VBoxDrvCfgStringList::~VBoxDrvCfgStringList()
{
    if (!m_cAllocated)
        return;

    for (size_t i = 0; i < m_cUsed; ++i)
        RTMemFree(m_paStrings[i]);
    RTMemFree(m_paStrings);
    m_paStrings  = NULL;
    m_cAllocated = 0;
    m_cUsed      = 0;
}

HRESULT VBoxDrvCfgStringList::add(LPWSTR pStr)
{
    if (m_cUsed == m_cAllocated)
    {
        int hrc = grow(m_cAllocated + 16);
        if (SUCCEEDED(hrc))
            return hrc;
    }
    LPWSTR str = (LPWSTR)RTMemDup(pStr, (RTUtf16Len(pStr) + 1) * sizeof(m_paStrings[0][0]));
    if (!str)
        return E_OUTOFMEMORY;
    m_paStrings[m_cUsed] = str;
    ++m_cUsed;
    return S_OK;
}

HRESULT VBoxDrvCfgStringList::grow(size_t a_cNew)
{
    NonStandardAssert(a_cNew >= m_cUsed);
    if (a_cNew < m_cUsed)
        return E_FAIL;
    void *pvNew = RTMemReallocZ(m_paStrings, m_cUsed * sizeof(m_paStrings[0]), a_cNew * sizeof(m_paStrings[0]));
    if (!pvNew)
        return E_OUTOFMEMORY;
    m_paStrings  = (LPWSTR *)pvNew;
    m_cAllocated = a_cNew;
    return S_OK;
}

/*
 * inf file manipulation API
 */
typedef bool (*PFNVBOXNETCFG_ENUMERATION_CALLBACK_T)(LPCWSTR lpszFileName, PVOID pContext);

typedef struct INF_INFO_T
{
    LPCWSTR pwszClassName;
    LPCWSTR pwszPnPId;
} INF_INFO_T, *PINF_INFO_T;

typedef struct INFENUM_CONTEXT_T
{
    INF_INFO_T InfInfo;
    DWORD fFlags;
    HRESULT hrc;
} INFENUM_CONTEXT_T, *PINFENUM_CONTEXT_T;

static HRESULT vboxDrvCfgInfQueryContext(HINF hInf, LPCWSTR pwszSection, LPCWSTR pwszKey, PINFCONTEXT pCtx)
{
    if (!SetupFindFirstLineW(hInf, pwszSection, pwszKey, pCtx))
    {
        DWORD dwErr = GetLastError();
        NonStandardLogRelCrap((__FUNCTION__ ": SetupFindFirstLine failed WinEr (%Rwc) for Section(%ls), Key(%ls)\n",
                               dwErr, pwszSection, pwszKey));
        return HRESULT_FROM_WIN32(dwErr);
    }
    return S_OK;
}

static HRESULT vboxDrvCfgInfQueryKeyValue(PINFCONTEXT pCtx, DWORD iValue, LPWSTR *ppwszValue, PDWORD pcwcValue)
{
    *ppwszValue = NULL;
    if (pcwcValue)
        *pcwcValue = 0;

    DWORD cwcValue;
    if (!SetupGetStringFieldW(pCtx, iValue, NULL, 0, &cwcValue))
    {
        DWORD dwErr = GetLastError();
//        NonStandardAssert(dwErr == ERROR_INSUFFICIENT_BUFFER);
        if (dwErr != ERROR_INSUFFICIENT_BUFFER)
        {
            NonStandardLogFlowCrap((__FUNCTION__ ": SetupGetStringField failed WinEr (%Rwc) for iValue(%d)\n", dwErr, iValue));
            return HRESULT_FROM_WIN32(dwErr);
        }
    }

    LPWSTR pwszValue = (LPWSTR)RTMemAlloc(cwcValue * sizeof(pwszValue[0]));
    NonStandardAssert(pwszValue);
    if (!pwszValue)
    {
        NonStandardLogRelCrap((__FUNCTION__ ": SetCoTaskMemAlloc failed to alloc mem of size (%d), for iValue(%d)\n",
                               cwcValue * sizeof(pwszValue[0]), iValue));
        return E_FAIL;
    }

    if (!SetupGetStringFieldW(pCtx, iValue, pwszValue, cwcValue, &cwcValue))
    {
        DWORD dwErr = GetLastError();
        NonStandardLogRelCrap((__FUNCTION__ ": SetupGetStringField failed WinEr (%Rwc) for iValue(%d)\n", dwErr, iValue));
        NonStandardAssertFailed();
        RTMemFree(pwszValue);
        return HRESULT_FROM_WIN32(dwErr);
    }

    *ppwszValue = pwszValue;
    if (pcwcValue)
        *pcwcValue = cwcValue;
    return S_OK;
}

#if defined(RT_ARCH_AMD64)
# define VBOXDRVCFG_ARCHSTR "amd64"
#else
# define VBOXDRVCFG_ARCHSTR "x86"
#endif

static HRESULT vboxDrvCfgInfQueryModelsSectionName(HINF hInf, LPWSTR *ppwszValue, PDWORD pcwcValue)
{
    *ppwszValue = NULL;
    if (pcwcValue)
        *pcwcValue = 0;

    INFCONTEXT InfCtx;
    HRESULT hrc = vboxDrvCfgInfQueryContext(hInf, L"Manufacturer", NULL, &InfCtx);
    if (hrc != S_OK)
    {
        NonStandardLogCrap((__FUNCTION__ ": vboxDrvCfgInfQueryContext for Manufacturer failed, hrc=0x%x\n", hrc));
        return hrc;
    }

    LPWSTR pwszModels;
    DWORD  cwcModels;
    hrc = vboxDrvCfgInfQueryKeyValue(&InfCtx, 1, &pwszModels, &cwcModels);
    if (hrc != S_OK)
    {
        NonStandardLogRelCrap((__FUNCTION__ ": vboxDrvCfgRegQueryKeyValue 1 for Manufacturer failed, hrc=0x%x\n", hrc));
        return hrc;
    }

    LPWSTR pwszPlatform = NULL;
    DWORD  cwcPlatform  = 0;
    bool   fArch        = false;
    bool   fNt          = false;

    LPWSTR pwszPlatformCur;
    DWORD  cwcPlatformCur;
    for (DWORD i = 2; (hrc = vboxDrvCfgInfQueryKeyValue(&InfCtx, i, &pwszPlatformCur, &cwcPlatformCur)) == S_OK; ++i)
    {
        if (RTUtf16ICmpAscii(pwszPlatformCur, "NT" VBOXDRVCFG_ARCHSTR) == 0)
            fArch = true;
        else
        {
            if (fNt || RTUtf16ICmpAscii(pwszPlatformCur, "NT") != 0)
            {
                RTMemFree(pwszPlatformCur);
                pwszPlatformCur = NULL;
                continue;
            }
            fNt = true;
        }

        cwcPlatform = cwcPlatformCur;
        if (pwszPlatform)
            RTMemFree(pwszPlatform);
        pwszPlatform = pwszPlatformCur;
        pwszPlatformCur = NULL;
    }

    hrc = S_OK;

    LPWSTR pwszResult = NULL;
    DWORD  cwcResult = 0;
    if (pwszPlatform)
    {
        pwszResult = (LPWSTR)RTMemAlloc((cwcModels + cwcPlatform) * sizeof(pwszResult[0]));
        if (pwszResult)
        {
            memcpy(pwszResult, pwszModels, (cwcModels - 1) * sizeof(pwszResult[0]));
            pwszResult[cwcModels - 1] = L'.';
            memcpy(&pwszResult[cwcModels], pwszPlatform, cwcPlatform * sizeof(pwszResult[0]));
            cwcResult = cwcModels + cwcPlatform;
        }
        else
            hrc = E_OUTOFMEMORY;
    }
    else
    {
        pwszResult = pwszModels;
        cwcResult  = cwcModels;
        pwszModels = NULL;
    }

    if (pwszModels)
        RTMemFree(pwszModels);
    if (pwszPlatform)
        RTMemFree(pwszPlatform);

    if (hrc == S_OK)
    {
        *ppwszValue = pwszResult;
        if (pcwcValue)
            *pcwcValue = cwcResult;
    }

    return hrc;
}

static HRESULT vboxDrvCfgInfQueryFirstPnPId(HINF hInf, LPWSTR *ppwszPnPId)
{
    *ppwszPnPId = NULL;

    LPWSTR pwszModels;
    HRESULT hrc = vboxDrvCfgInfQueryModelsSectionName(hInf, &pwszModels, NULL);
    NonStandardLogRelCrap((__FUNCTION__ ": vboxDrvCfgInfQueryModelsSectionName returned pwszModels = (%ls)", pwszModels));
    if (hrc != S_OK)
    {
        NonStandardLogCrap((__FUNCTION__ ": vboxDrvCfgRegQueryKeyValue for Manufacturer failed, hrc=0x%x\n", hrc));
        return hrc;
    }

    LPWSTR     pwszPnPId = NULL;
    INFCONTEXT InfCtx;
    hrc = vboxDrvCfgInfQueryContext(hInf, pwszModels, NULL, &InfCtx);
    if (hrc == S_OK)
    {
        hrc = vboxDrvCfgInfQueryKeyValue(&InfCtx, 2, &pwszPnPId, NULL);
        if (hrc == S_OK)
        {
            NonStandardLogRelCrap((__FUNCTION__ ": vboxDrvCfgRegQueryKeyValue for models (%ls) returned pwszPnPId (%ls)\n", pwszModels, pwszPnPId));
            *ppwszPnPId = pwszPnPId;
        }
        else
            NonStandardLogRelCrap((__FUNCTION__ ": vboxDrvCfgRegQueryKeyValue for models (%ls) failed, hrc=0x%x\n", pwszModels, hrc));
    }
    else
        NonStandardLogRelCrap((__FUNCTION__ ": vboxDrvCfgInfQueryContext for models (%ls) failed, hrc=0x%x\n", pwszModels, hrc));

    RTMemFree(pwszModels);
    return hrc;
}

static bool vboxDrvCfgInfEnumerationCallback(LPCWSTR pwszFileName, PVOID pCtxt)
{
    PINFENUM_CONTEXT_T pContext = (PINFENUM_CONTEXT_T)pCtxt;
    NonStandardLogRelCrap((__FUNCTION__": pwszFileName (%ls)\n", pwszFileName));
    NonStandardLogRelCrap((__FUNCTION__ ": pContext->InfInfo.pwszClassName = (%ls)\n", pContext->InfInfo.pwszClassName));
    HINF hInf = SetupOpenInfFileW(pwszFileName, pContext->InfInfo.pwszClassName, INF_STYLE_WIN4, NULL /*__in PUINT ErrorLine */);
    if (hInf == INVALID_HANDLE_VALUE)
    {
        DWORD const dwErr = GetLastError();
//        NonStandardAssert(dwErr == ERROR_CLASS_MISMATCH);
        if (dwErr != ERROR_CLASS_MISMATCH)
            NonStandardLogCrap((__FUNCTION__ ": SetupOpenInfFileW err dwErr=%u\n", dwErr));
        else
            NonStandardLogCrap((__FUNCTION__ ": dwErr == ERROR_CLASS_MISMATCH\n"));
        return true;
    }

    LPWSTR pwszPnPId;
    HRESULT hrc = vboxDrvCfgInfQueryFirstPnPId(hInf, &pwszPnPId);
    NonStandardLogRelCrap((__FUNCTION__ ": vboxDrvCfgInfQueryFirstPnPId returned pwszPnPId = (%ls)\n", pwszPnPId));
    NonStandardLogRelCrap((__FUNCTION__ ": pContext->InfInfo.pwszPnPId = (%ls)\n", pContext->InfInfo.pwszPnPId));
    if (hrc == S_OK)
    {
        if (!RTUtf16ICmp(pContext->InfInfo.pwszPnPId, pwszPnPId))
        {
            /** @todo bird/2020-09-01: See the following during uninstallation with
             * windbg attached (see DllMain trick):
             *
             *   ModLoad: 00007ffa`73c20000 00007ffa`73c4f000   C:\WINDOWS\SYSTEM32\drvsetup.dll
             *   (1b238.1b254): Access violation - code c0000005 (first chance)
             *   First chance exceptions are reported before any exception handling.
             *   This exception may be expected and handled.
             *   KERNELBASE!WaitForMultipleObjectsEx+0x9e:
             *   00007ffa`8247cb6e 458b74fd00      mov     r14d,dword ptr [r13+rdi*8] ds:00000000`00000010=????????
             *   0:006> k
             *    # Child-SP          RetAddr           Call Site
             *   00 00000099`6e4fe7a0 00007ffa`73c2df46 KERNELBASE!WaitForMultipleObjectsEx+0x9e
             *   01 00000099`6e4fea90 00007ffa`73c32ec2 drvsetup!pSetupStringTableEnum+0x3e
             *   02 00000099`6e4feae0 00007ffa`73c2ae9d drvsetup!DrvUtilsUpdateInfoEnumDriverInfs+0x8e
             *   03 00000099`6e4feb20 00007ffa`73c2b1cc drvsetup!DrvSetupUninstallDriverInternal+0x211
             *   04 00000099`6e4febe0 00007ffa`83eb09d7 drvsetup!pDrvSetupUninstallDriver+0xfc
             *   05 00000099`6e4fec30 00007ffa`83eb06a0 SETUPAPI!pSetupUninstallOEMInf+0x26b
             *   06 00000099`6e4fef00 00007ffa`57a39fb7 SETUPAPI!SetupUninstallOEMInfW+0x170
             *   07 00000099`6e4ff190 00007ffa`57a3ae0c MSID039!vboxDrvCfgInfEnumerationCallback+0xf7 [E:\vbox\svn\trunk\src\VBox\HostDrivers\win\cfg\VBoxDrvCfg.cpp @ 445]
             *   08 00000099`6e4ff1c0 00007ffa`57a321e6 MSID039!VBoxDrvCfgInfUninstallAllSetupDi+0xfc [E:\vbox\svn\trunk\src\VBox\HostDrivers\win\cfg\VBoxDrvCfg.cpp @ 653]
             *   09 (Inline Function) --------`-------- MSID039!_removeHostOnlyInterfaces+0x6c [E:\vbox\svn\trunk\src\VBox\Installer\win\InstallHelper\VBoxInstallHelper.cpp @ 1523]
             *   0a 00000099`6e4ff240 00007ffa`610f59d3 MSID039!RemoveHostOnlyInterfaces+0x76 [E:\vbox\svn\trunk\src\VBox\Installer\win\InstallHelper\VBoxInstallHelper.cpp @ 1545]
             *   0b 00000099`6e4ff270 00007ffa`610d80ac msi!CallCustomDllEntrypoint+0x2b
             *   0c 00000099`6e4ff2e0 00007ffa`84567034 msi!CMsiCustomAction::CustomActionThread+0x34c
             *   0d 00000099`6e4ff8d0 00007ffa`849a2651 KERNEL32!BaseThreadInitThunk+0x14
             *   0e 00000099`6e4ff900 00000000`00000000 ntdll!RtlUserThreadStart+0x21
             *   0:006> r
             *   rax=000000996e114000 rbx=0000000000000002 rcx=0000000000000002
             *   rdx=0000000000000000 rsi=0000000000000000 rdi=0000000000000000
             *   rip=00007ffa8247cb6e rsp=000000996e4fe7a0 rbp=0000000000000004
             *    r8=0000000000000000  r9=00000000ffffffff r10=0000000000000000
             *   r11=0000000000000246 r12=00000000ffffffff r13=0000000000000010
             *   r14=00007ffa73c32e00 r15=0000000000000001
             *   iopl=0         nv up ei ng nz ac pe cy
             *   cs=0033  ss=002b  ds=002b  es=002b  fs=0053  gs=002b             efl=00010293
             *   KERNELBASE!WaitForMultipleObjectsEx+0x9e:
             *   00007ffa`8247cb6e 458b74fd00      mov     r14d,dword ptr [r13+rdi*8] ds:00000000`00000010=????????
             *
             * Happens with the filter driver too:
             *
             *   (1b238.1b7e0): Access violation - code c0000005 (first chance)
             *   First chance exceptions are reported before any exception handling.
             *   This exception may be expected and handled.
             *   KERNELBASE!WaitForMultipleObjectsEx+0x9e:
             *   00007ffa`8247cb6e 458b74fd00      mov     r14d,dword ptr [r13+rdi*8] ds:00000000`00000010=????????
             *   0:006> k
             *    # Child-SP          RetAddr           Call Site
             *   00 00000099`6e4fe8c0 00007ffa`6558df46 KERNELBASE!WaitForMultipleObjectsEx+0x9e
             *   01 00000099`6e4febb0 00007ffa`65592ec2 drvsetup!pSetupStringTableEnum+0x3e
             *   02 00000099`6e4fec00 00007ffa`6558ae9d drvsetup!DrvUtilsUpdateInfoEnumDriverInfs+0x8e
             *   03 00000099`6e4fec40 00007ffa`6558b1cc drvsetup!DrvSetupUninstallDriverInternal+0x211
             *   04 00000099`6e4fed00 00007ffa`83eb09d7 drvsetup!pDrvSetupUninstallDriver+0xfc
             *   05 00000099`6e4fed50 00007ffa`83eb06a0 SETUPAPI!pSetupUninstallOEMInf+0x26b
             *   06 00000099`6e4ff020 00007ffa`57a39fb7 SETUPAPI!SetupUninstallOEMInfW+0x170
             *   07 00000099`6e4ff2b0 00007ffa`57a3abaf MSI398C!vboxDrvCfgInfEnumerationCallback+0xf7 [E:\vbox\svn\trunk\src\VBox\HostDrivers\win\cfg\VBoxDrvCfg.cpp @ 445]
             *   08 (Inline Function) --------`-------- MSI398C!vboxDrvCfgEnumFiles+0x4f [E:\vbox\svn\trunk\src\VBox\HostDrivers\win\cfg\VBoxDrvCfg.cpp @ 670]
             *   09 00000099`6e4ff2e0 00007ffa`57a3792e MSI398C!VBoxDrvCfgInfUninstallAllF+0xdf [E:\vbox\svn\trunk\src\VBox\HostDrivers\win\cfg\VBoxDrvCfg.cpp @ 723]
             *   0a 00000099`6e4ff7b0 00007ffa`57a33411 MSI398C!vboxNetCfgWinNetLwfUninstall+0x9e [E:\vbox\svn\trunk\src\VBox\HostDrivers\VBoxNetFlt\win\cfg\VBoxNetCfg.cpp @ 2249]
             *   0b 00000099`6e4ff7e0 00007ffa`57a3263d MSI398C!_uninstallNetLwf+0x71 [E:\vbox\svn\trunk\src\VBox\Installer\win\InstallHelper\VBoxInstallHelper.cpp @ 1206]
             *   0c 00000099`6e4ff810 00007ffa`610f59d3 MSI398C!UninstallNetFlt+0xd [E:\vbox\svn\trunk\src\VBox\Installer\win\InstallHelper\VBoxInstallHelper.cpp @ 1124]
             *   0d 00000099`6e4ff840 00007ffa`610d80ac msi!CallCustomDllEntrypoint+0x2b
             *   0e 00000099`6e4ff8b0 00007ffa`84567034 msi!CMsiCustomAction::CustomActionThread+0x34c
             *   0f 00000099`6e4ffea0 00007ffa`849a2651 KERNEL32!BaseThreadInitThunk+0x14
             *   10 00000099`6e4ffed0 00000000`00000000 ntdll!RtlUserThreadStart+0x21
             *   0:006> r
             *   rax=000000996e114000 rbx=0000000000000002 rcx=0000000000000002
             *   rdx=0000000000000000 rsi=0000000000000000 rdi=0000000000000000
             *   rip=00007ffa8247cb6e rsp=000000996e4fe8c0 rbp=0000000000000004
             *    r8=0000000000000000  r9=00000000ffffffff r10=0000000000000000
             *   r11=0000000000000246 r12=00000000ffffffff r13=0000000000000010
             *   r14=00007ffa65592e00 r15=0000000000000000
             *   iopl=0         nv up ei ng nz ac pe cy
             *   cs=0033  ss=002b  ds=002b  es=002b  fs=0053  gs=002b             efl=00010293
             *   KERNELBASE!WaitForMultipleObjectsEx+0x9e:
             *   00007ffa`8247cb6e 458b74fd00      mov     r14d,dword ptr [r13+rdi*8] ds:00000000`00000010=????????
             *
             * BUGBUG
             */
#if 0
            if (!SetupUninstallOEMInfW(pwszFileName, pContext->fFlags, /* could be SUOI_FORCEDELETE */ NULL /* Reserved */))
#else /* Just in case the API doesn't catch it itself (seems it does on w10/19044).  */
            BOOL  fRc = TRUE;
            __try
            {
                fRc = SetupUninstallOEMInfW(pwszFileName, pContext->fFlags, /* could be SUOI_FORCEDELETE */ NULL /* Reserved */);
            }
            __except(hrc = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
            {
                NonStandardLogRelCrap((__FUNCTION__ ": SetupUninstallOEMInf raised an exception: %#x\n", hrc));
                hrc = E_ABORT;
            }
            if (!fRc)
#endif
            {
                DWORD const dwErr = GetLastError();
                NonStandardLogRelCrap((__FUNCTION__ ": SetupUninstallOEMInf failed for file (%ls), dwErr=%u\n", pwszFileName, dwErr));
                NonStandardAssertFailed();
                hrc = HRESULT_FROM_WIN32( dwErr );
            }
        }

        RTMemFree(pwszPnPId);
    }
    else
        NonStandardLogCrap((__FUNCTION__ ": vboxDrvCfgInfQueryFirstPnPId failed, hrc=0x%x\n", hrc));

    SetupCloseInfFile(hInf);
    return true;
}


#define VBOXDRVCFG_S_INFEXISTS (HRESULT_FROM_WIN32(ERROR_FILE_EXISTS))

static HRESULT vboxDrvCfgInfCopyEx(IN LPCWSTR pwszInfPath, IN DWORD fCopyStyle, OUT LPWSTR pwszDstName, IN DWORD cwcDstName,
                                   OUT PDWORD pcwcDstNameRet, OUT LPWSTR *pwszDstNameComponent)
{
    /* Extract the director from pwszInfPath */
    size_t cchPath = RTUtf16Len(pwszInfPath);
    while (cchPath > 0 && !RTPATH_IS_SEP(pwszInfPath[cchPath - 1]))
        cchPath--;

    WCHAR *pwszMediaLocation = (WCHAR *)alloca(((cchPath) + 1) * sizeof(pwszMediaLocation[0]));
    memcpy(pwszMediaLocation, pwszInfPath, cchPath * sizeof(pwszMediaLocation[0]));
    pwszMediaLocation[cchPath] = '\0';


    if (!SetupCopyOEMInfW(pwszInfPath, pwszMediaLocation, SPOST_PATH, fCopyStyle,
                          pwszDstName, cwcDstName, pcwcDstNameRet, pwszDstNameComponent))
    {
        DWORD const dwErr = GetLastError();
        HRESULT hrc = HRESULT_FROM_WIN32(dwErr);
        if (fCopyStyle != SP_COPY_REPLACEONLY || hrc != VBOXDRVCFG_S_INFEXISTS)
            NonStandardLogRelCrap((__FUNCTION__ ": SetupCopyOEMInf fail dwErr=%u\n", dwErr));
        return hrc;
    }

    return S_OK;
}

VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfInstall(IN LPCWSTR pwszInfPath)
{
    return vboxDrvCfgInfCopyEx(pwszInfPath, 0 /*fCopyStyle*/, NULL /*pwszDstName*/, 0 /*cwcDstName*/,
                               NULL /*pcwcDstNameRet*/, NULL /*pwszDstNameComponent*/);
}

VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfUninstall(IN LPCWSTR pwszInfPath, DWORD fFlags)
{
    WCHAR   wszDstInfName[MAX_PATH];
    DWORD   cwcDword = RT_ELEMENTS(wszDstInfName);
    HRESULT hrc = vboxDrvCfgInfCopyEx(pwszInfPath, SP_COPY_REPLACEONLY, wszDstInfName, cwcDword, &cwcDword, NULL);
    if (hrc == VBOXDRVCFG_S_INFEXISTS)
    {
        if (!SetupUninstallOEMInfW(wszDstInfName, fFlags, NULL /*Reserved*/))
        {
            DWORD dwErr = GetLastError();
            NonStandardLogRelCrap((__FUNCTION__ ": SetupUninstallOEMInf failed for file (%ls), oem(%ls), dwErr=%u\n",
                                   pwszInfPath, wszDstInfName, dwErr));
            NonStandardAssertFailed();
            return HRESULT_FROM_WIN32(dwErr);
        }
    }
    return S_OK;
}


static HRESULT vboxDrvCfgCollectInfsSetupDi(const GUID *pGuid, LPCWSTR pwszPnPId, VBoxDrvCfgStringList &a_rList)
{
    DWORD dwErrRet = ERROR_SUCCESS;
    HDEVINFO hDevInfo = SetupDiCreateDeviceInfoList(pGuid, /*ClassGuid*/ NULL /*hwndParent*/);
    if (hDevInfo != INVALID_HANDLE_VALUE)
    {
        /** @todo bird/2020-09-01: seeing this during uninstall when windbg is
         * attached to msiexec.exe (see trick in DllMain):
         *
         *    (1b238.1b254): Access violation - code c0000005 (first chance)
         *    First chance exceptions are reported before any exception handling.
         *    This exception may be expected and handled.
         *    SETUPAPI!SpSignVerifyInfFile+0x246:
         *    00007ffa`83e3ee3e 663907          cmp     word ptr [rdi],ax ds:00000000`00000000=????
         *    0:006> k
         *     # Child-SP          RetAddr           Call Site
         *    00 00000099`6e4f8340 00007ffa`83e1e765 SETUPAPI!SpSignVerifyInfFile+0x246
         *    01 00000099`6e4f8420 00007ffa`83e9ebfd SETUPAPI!DrvSearchCallback+0x1155
         *    02 00000099`6e4f9380 00007ffa`83e9eed3 SETUPAPI!InfCacheSearchDirectory+0x469
         *    03 00000099`6e4f98b0 00007ffa`83e9f454 SETUPAPI!InfCacheSearchDirectoryRecursive+0xcf
         *    04 00000099`6e4f9fe0 00007ffa`83e9da10 SETUPAPI!InfCacheSearchPath+0x1a0
         *    05 00000099`6e4fa2b0 00007ffa`83e262a2 SETUPAPI!EnumDrvInfsInDirPathList+0x560
         *    06 00000099`6e4fa3f0 00007ffa`57a39a21 SETUPAPI!SetupDiBuildDriverInfoList+0x1242
         *    07 00000099`6e4fab10 00007ffa`57a3ad6e MSID039!vboxDrvCfgCollectInfsSetupDi+0x71 [E:\vbox\svn\trunk\src\VBox\HostDrivers\win\cfg\VBoxDrvCfg.cpp @ 526]
         *    08 00000099`6e4ff1c0 00007ffa`57a321e6 MSID039!VBoxDrvCfgInfUninstallAllSetupDi+0x5e [E:\vbox\svn\trunk\src\VBox\HostDrivers\win\cfg\VBoxDrvCfg.cpp @ 633]
         *    09 (Inline Function) --------`-------- MSID039!_removeHostOnlyInterfaces+0x6c [E:\vbox\svn\trunk\src\VBox\Installer\win\InstallHelper\VBoxInstallHelper.cpp @ 1523]
         *    0a 00000099`6e4ff240 00007ffa`610f59d3 MSID039!RemoveHostOnlyInterfaces+0x76 [E:\vbox\svn\trunk\src\VBox\Installer\win\InstallHelper\VBoxInstallHelper.cpp @ 1545]
         *    0b 00000099`6e4ff270 00007ffa`610d80ac msi!CallCustomDllEntrypoint+0x2b
         *    0c 00000099`6e4ff2e0 00007ffa`84567034 msi!CMsiCustomAction::CustomActionThread+0x34c
         *    0d 00000099`6e4ff8d0 00007ffa`849a2651 KERNEL32!BaseThreadInitThunk+0x14
         *    0e 00000099`6e4ff900 00000000`00000000 ntdll!RtlUserThreadStart+0x21
         *    0:006> r
         *    rax=0000000000000000 rbx=0000000000000490 rcx=aa222a2675da0000
         *    rdx=0000000000000000 rsi=0000000000000000 rdi=0000000000000000
         *    rip=00007ffa83e3ee3e rsp=000000996e4f8340 rbp=000000996e4f9480
         *     r8=0000000000050004  r9=00007ffa83ef5418 r10=0000000000008000
         *    r11=000000996e4f76f0 r12=000000996e4f84c8 r13=0000000000000000
         *    r14=000000996e4f88d0 r15=0000000000000000
         *    iopl=0         nv up ei pl nz ac pe cy
         *    cs=0033  ss=002b  ds=002b  es=002b  fs=0053  gs=002b             efl=00010213
         *    SETUPAPI!SpSignVerifyInfFile+0x246:
         *    00007ffa`83e3ee3e 663907          cmp     word ptr [rdi],ax ds:00000000`00000000=????
          */
#if 0
        if (SetupDiBuildDriverInfoList(hDevInfo, NULL /*DeviceInfoData*/, SPDIT_CLASSDRIVER))
#else   /* Just in case the API doesn't catch it itself (seems it does on w10/19044).  */
        BOOL fRc = FALSE;
        DWORD uXcpt = 0;
        __try
        {
            fRc = SetupDiBuildDriverInfoList(hDevInfo, NULL /*DeviceInfoData*/, SPDIT_CLASSDRIVER);
        }
        __except(uXcpt = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
        {
            NonStandardLogRelCrap((__FUNCTION__ ": SetupDiBuildDriverInfoList raised an exception: %#x\n", uXcpt));
        }
        if (fRc)
#endif
        {
            SP_DRVINFO_DATA DrvInfo;
            DrvInfo.cbSize = sizeof(SP_DRVINFO_DATA);

            union
            {
                SP_DRVINFO_DETAIL_DATA_W s;
                uint8_t ab[16384];
            } DrvDetail;

            /* Ensure zero terminated buffer: */
            DrvDetail.ab[sizeof(DrvDetail) - 1] = '\0';
            DrvDetail.ab[sizeof(DrvDetail) - 2] = '\0';

            for (DWORD i = 0; dwErrRet == ERROR_SUCCESS; i++)
            {
                if (SetupDiEnumDriverInfo(hDevInfo, NULL /*DeviceInfoData*/, SPDIT_CLASSDRIVER /*DriverType*/,
                                          i /*MemberIndex*/, &DrvInfo /*DriverInfoData*/))
                {
                    DWORD dwReq = 0;
                    DrvDetail.s.cbSize = sizeof(SP_DRVINFO_DETAIL_DATA);
                    if (SetupDiGetDriverInfoDetail(hDevInfo, NULL /*DeviceInfoData*/, &DrvInfo,
                                                   &DrvDetail.s, sizeof(DrvDetail) - 2 /*our terminator*/, &dwReq))
                    {
                        for (WCHAR *pwszHwId = DrvDetail.s.HardwareID;
                             *pwszHwId != '\0' && (uintptr_t)pwszHwId < (uintptr_t)&DrvDetail.ab[sizeof(DrvDetail)];
                             pwszHwId += RTUtf16Len(pwszHwId) + 1)
                        {
                            if (RTUtf16ICmp(pwszHwId, pwszPnPId) == 0)
                            {
                                NonStandardAssert(DrvDetail.s.InfFileName[0]);
                                if (DrvDetail.s.InfFileName[0])
                                {
                                    HRESULT hrc = a_rList.add(DrvDetail.s.InfFileName);
                                    NonStandardLogRelCrap((__FUNCTION__": %ls added to list (%#x)", DrvDetail.s.InfFileName, hrc));
                                    if (hrc != S_OK)
                                    {
                                        dwErrRet = ERROR_OUTOFMEMORY;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        DWORD dwErr2 = GetLastError();
                        NonStandardLogRelCrap((__FUNCTION__": SetupDiGetDriverInfoDetail fail dwErr=%u, size(%d)", dwErr2, dwReq));
//                        NonStandardAssertFailed();
                    }
                }
                else
                {
                    DWORD dwErr2 = GetLastError();
                    if (dwErr2 == ERROR_NO_MORE_ITEMS)
                    {
                        NonStandardLogRelCrap((__FUNCTION__": dwErr == ERROR_NO_MORE_ITEMS -> search was finished "));
                        break;
                    }
                    NonStandardAssertFailed();
                }
            }

            SetupDiDestroyDriverInfoList(hDevInfo, NULL /*DeviceInfoData*/, SPDIT_CLASSDRIVER);
        }
        else
        {
            dwErrRet = GetLastError();
            NonStandardAssertFailed();
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    else
    {
        dwErrRet = GetLastError();
        NonStandardAssertFailed();
    }

    return HRESULT_FROM_WIN32(dwErrRet);
}

#if 0
VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInit()
{
    int rc = RTR3InitDll(0);
    if (rc != VINF_SUCCESS)
    {
        NonStandardLogRelCrap(("Could not init IPRT!, rc (%d)\n", rc));
        return E_FAIL;
    }

    return S_OK;
}

VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgTerm()
{
    return S_OK;
}
#endif

VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfUninstallAllSetupDi(IN const GUID *pGuidClass, IN LPCWSTR pwszClassName,
                                                          IN LPCWSTR pwszPnPId, IN DWORD fFlags)
{
    VBoxDrvCfgStringList list(128);
    HRESULT hrc = vboxDrvCfgCollectInfsSetupDi(pGuidClass, pwszPnPId, list);
    NonStandardLogRelCrap((__FUNCTION__": vboxDrvCfgCollectInfsSetupDi returned %d devices with PnPId %ls and class name %ls",
                           list.size(), pwszPnPId, pwszClassName));
    if (hrc == S_OK)
    {
        INFENUM_CONTEXT_T Context;
        Context.InfInfo.pwszClassName = pwszClassName;
        Context.InfInfo.pwszPnPId = pwszPnPId;
        Context.fFlags = fFlags;
        Context.hrc = S_OK;
        size_t const cItems = list.size();
        for (size_t i = 0; i < cItems; ++i)
        {
            LPCWSTR pwszInf = list.get(i);

            /* Find the start of the filename: */
            size_t offFilename = RTUtf16Len(pwszInf);
            while (offFilename > 0 && !RTPATH_IS_SEP(pwszInf[offFilename - 1]))
                offFilename--;

            vboxDrvCfgInfEnumerationCallback(&pwszInf[offFilename], &Context);
            NonStandardLogRelCrap((__FUNCTION__": inf = %ls\n", pwszInf));
        }
    }
    return hrc;
}

static HRESULT vboxDrvCfgEnumFiles(LPCWSTR pwszDirAndPattern, PFNVBOXNETCFG_ENUMERATION_CALLBACK_T pfnCallback, PVOID pContext)
{
    HRESULT hrc = S_OK;

    WIN32_FIND_DATAW Data;
    RT_ZERO(Data);
    HANDLE hEnum = FindFirstFileW(pwszDirAndPattern, &Data);
    if (hEnum != INVALID_HANDLE_VALUE)
    {
        for (;;)
        {
            if (!pfnCallback(Data.cFileName, pContext))
                break;

            /* next iteration */
            RT_ZERO(Data);
            BOOL fNext = FindNextFile(hEnum, &Data);
            if (!fNext)
            {
                DWORD dwErr = GetLastError();
                if (dwErr != ERROR_NO_MORE_FILES)
                {
                    NonStandardLogRelCrap((__FUNCTION__": FindNextFile fail dwErr=%u\n", dwErr));
                    NonStandardAssertFailed();
                    hrc = HRESULT_FROM_WIN32(dwErr);
                }
                break;
            }
        }

        FindClose(hEnum);
    }
    else
    {
        DWORD dwErr = GetLastError();
        if (dwErr != ERROR_NO_MORE_FILES)
        {
            NonStandardLogRelCrap((__FUNCTION__": FindFirstFile fail dwErr=%u\n", dwErr));
            NonStandardAssertFailed();
            hrc = HRESULT_FROM_WIN32(dwErr);
        }
    }

    return hrc;
}

VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgInfUninstallAllF(LPCWSTR pwszClassName, LPCWSTR pwszPnPId, DWORD fFlags)
{
    static WCHAR const s_wszFilter[] = L"\\inf\\oem*.inf";

    HRESULT hrc;
    WCHAR   wszInfDirPath[MAX_PATH];
    UINT    cwcInput = RT_ELEMENTS(wszInfDirPath) - RT_ELEMENTS(s_wszFilter);
    UINT    cwcWindows = GetSystemWindowsDirectoryW(wszInfDirPath, cwcInput);
    if (cwcWindows > 0 && cwcWindows < cwcInput)
    {
        RTUtf16Copy(&wszInfDirPath[cwcWindows], RT_ELEMENTS(wszInfDirPath) - cwcWindows, s_wszFilter);

        INFENUM_CONTEXT_T Context;
        Context.InfInfo.pwszClassName = pwszClassName;
        Context.InfInfo.pwszPnPId = pwszPnPId;
        Context.fFlags = fFlags;
        Context.hrc = S_OK;
        NonStandardLogRelCrap((__FUNCTION__": Calling vboxDrvCfgEnumFiles(wszInfDirPath, vboxDrvCfgInfEnumerationCallback, &Context)"));
        hrc = vboxDrvCfgEnumFiles(wszInfDirPath, vboxDrvCfgInfEnumerationCallback, &Context);
        NonStandardAssert(hrc == S_OK);
        if (hrc == S_OK)
            hrc = Context.hrc;
        else
            NonStandardLogRelCrap((__FUNCTION__": vboxDrvCfgEnumFiles failed, hrc=0x%x\n", hrc));
    }
    else
    {
        NonStandardLogRelCrap((__FUNCTION__": GetSystemWindowsDirectory failed, cwcWindows=%u lasterr=%u\n", cwcWindows, GetLastError()));
        NonStandardAssertFailed();
        hrc = E_FAIL;
    }

    return hrc;

}

/* time intervals in milliseconds */
/* max time to wait for the service to startup */
#define VBOXDRVCFG_SVC_WAITSTART_TIME 10000
/* sleep time before service status polls */
#define VBOXDRVCFG_SVC_WAITSTART_TIME_PERIOD 100
/* number of service start polls */
#define VBOXDRVCFG_SVC_WAITSTART_RETRIES (VBOXDRVCFG_SVC_WAITSTART_TIME/VBOXDRVCFG_SVC_WAITSTART_TIME_PERIOD)

VBOXDRVCFG_DECL(HRESULT) VBoxDrvCfgSvcStart(LPCWSTR pwszSvcName)
{
    SC_HANDLE hMgr = OpenSCManagerW(NULL, NULL, SERVICE_QUERY_STATUS | SERVICE_START);
    if (hMgr == NULL)
    {
        DWORD dwErr = GetLastError();
        NonStandardLogRelCrap((__FUNCTION__": OpenSCManager failed, dwErr=%u\n", dwErr));
        return HRESULT_FROM_WIN32(dwErr);
    }

    HRESULT hrc = S_OK;
    SC_HANDLE hSvc = OpenServiceW(hMgr, pwszSvcName, SERVICE_QUERY_STATUS | SERVICE_START);
    if (hSvc)
    {
        SERVICE_STATUS Status;
        BOOL fRc = QueryServiceStatus(hSvc, &Status);
        if (fRc)
        {
            if (Status.dwCurrentState != SERVICE_RUNNING && Status.dwCurrentState != SERVICE_START_PENDING)
            {
                NonStandardLogRelCrap(("Starting service (%ls)\n", pwszSvcName));

                fRc = StartService(hSvc, 0, NULL);
                if (!fRc)
                {
                    DWORD dwErr = GetLastError();
                    NonStandardLogRelCrap((__FUNCTION__": StartService failed dwErr=%u\n", dwErr));
                    hrc = HRESULT_FROM_WIN32(dwErr);
                }
            }

            if (fRc)
            {
                fRc = QueryServiceStatus(hSvc, &Status);
                if (fRc)
                {
                    if (Status.dwCurrentState == SERVICE_START_PENDING)
                        for (size_t i = 0; i < VBOXDRVCFG_SVC_WAITSTART_RETRIES; ++i)
                        {
                            Sleep(VBOXDRVCFG_SVC_WAITSTART_TIME_PERIOD);
                            fRc = QueryServiceStatus(hSvc, &Status);
                            if (!fRc)
                            {
                                DWORD dwErr = GetLastError();
                                NonStandardLogRelCrap((__FUNCTION__": QueryServiceStatus failed dwErr=%u\n", dwErr));
                                hrc = HRESULT_FROM_WIN32(dwErr);
                                break;
                            }
                            if (Status.dwCurrentState != SERVICE_START_PENDING)
                                break;
                        }

                    if (hrc != S_OK || Status.dwCurrentState != SERVICE_RUNNING)
                    {
                        NonStandardLogRelCrap((__FUNCTION__": Failed to start the service\n"));
                        hrc = E_FAIL;
                    }
                }
                else
                {
                    DWORD dwErr = GetLastError();
                    NonStandardLogRelCrap((__FUNCTION__": QueryServiceStatus failed dwErr=%u\n", dwErr));
                    hrc = HRESULT_FROM_WIN32(dwErr);
                }
            }
        }
        else
        {
            DWORD dwErr = GetLastError();
            NonStandardLogRelCrap((__FUNCTION__": QueryServiceStatus failed dwErr=%u\n", dwErr));
            hrc = HRESULT_FROM_WIN32(dwErr);
        }

        CloseServiceHandle(hSvc);
    }
    else
    {
        DWORD dwErr = GetLastError();
        NonStandardLogRelCrap((__FUNCTION__": OpenServiceW failed, dwErr=%u\n", dwErr));
        hrc = HRESULT_FROM_WIN32(dwErr);
    }

    CloseServiceHandle(hMgr);
    return hrc;
}


HRESULT VBoxDrvCfgDrvUpdate(LPCWSTR pszwHwId, LPCWSTR psxwInf, BOOL *pfRebootRequired)
{
    if (pfRebootRequired)
        *pfRebootRequired = FALSE;

    WCHAR wszInfFullPath[MAX_PATH];
    DWORD dwChars = GetFullPathNameW(psxwInf, MAX_PATH, wszInfFullPath, NULL /*lpFilePart*/);
    if (!dwChars || dwChars >= MAX_PATH)
    {
        NonStandardLogCrap(("GetFullPathNameW failed, dwErr=%u, dwChars=%ld\n", GetLastError(), dwChars));
        return E_INVALIDARG;
    }

    BOOL fRebootRequired = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(NULL /*hwndParent*/, pszwHwId, wszInfFullPath, INSTALLFLAG_FORCE, &fRebootRequired))
    {
        DWORD dwErr = GetLastError();
        NonStandardLogCrap(("UpdateDriverForPlugAndPlayDevicesW failed, dwErr=%u\n", dwErr));
        return HRESULT_FROM_WIN32(dwErr);
    }

    if (fRebootRequired)
        NonStandardLogCrap(("!!Driver Update: REBOOT REQUIRED!!\n"));

    if (pfRebootRequired)
        *pfRebootRequired = fRebootRequired;

    return S_OK;
}

