/* $Id: nt3fakes-r0drv-nt.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - NT 3.x fakes for NT 4.0 KPIs.
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
#define _IMAGE_NT_HEADERS           RT_CONCAT(_IMAGE_NT_HEADERS,ARCH_BITS)
#include "the-nt-kernel.h"
#include <iprt/mem.h>

#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/x86.h>
#include <iprt/formats/mz.h>
#include <iprt/formats/pecoff.h>
#include "internal-r0drv-nt.h"

typedef uint32_t DWORD;
#include <VerRsrc.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLASM(void) rtNt3InitSymbolsAssembly(void); /* in nt3fakesA-r0drv-nt.asm */


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static uint32_t         g_uNt3MajorVer  = 3;
static uint32_t         g_uNt3MinorVer  = 51;
static uint32_t         g_uNt3BuildNo   = 1057;
static bool             g_fNt3Checked   = false;
static bool             g_fNt3Smp       = false; /**< Not reliable. */
static bool volatile    g_fNt3VersionInitialized = false;

static uint8_t         *g_pbNt3OsKrnl   = (uint8_t *)UINT32_C(0x80100000);
static uint32_t         g_cbNt3OsKrnl   = 0x300000;
static uint8_t         *g_pbNt3Hal      = (uint8_t *)UINT32_C(0x80400000);
static uint32_t         g_cbNt3Hal      = _512K;
static bool volatile    g_fNt3ModuleInfoInitialized = false;


RT_C_DECLS_BEGIN
/** @name KPIs we provide fallback implementations for.
 *
 * The assembly init routine will point the __imp_xxx variable to the NT
 * implementation if available, using the fallback if not.
 * @{  */
decltype(PsGetVersion)                     *g_pfnrtPsGetVersion;
decltype(ZwQuerySystemInformation)         *g_pfnrtZwQuerySystemInformation;
decltype(KeSetTimerEx)                     *g_pfnrtKeSetTimerEx;
decltype(IoAttachDeviceToDeviceStack)      *g_pfnrtIoAttachDeviceToDeviceStack;
decltype(PsGetCurrentProcessId)            *g_pfnrtPsGetCurrentProcessId;
decltype(ZwYieldExecution)                 *g_pfnrtZwYieldExecution;
decltype(ExAcquireFastMutex)               *g_pfnrtExAcquireFastMutex;
decltype(ExReleaseFastMutex)               *g_pfnrtExReleaseFastMutex;
/** @} */

/** @name Fastcall optimizations not present in NT 3.1.
 *
 * We try resolve both the stdcall and fastcall variants and patch it up in
 * assembly. The last four routines are in the hal.
 *
 * @{  */
decltype(IofCompleteRequest)               *g_pfnrtIofCompleteRequest;
decltype(ObfDereferenceObject)             *g_pfnrtObfDereferenceObject;
decltype(IofCallDriver)                    *g_pfnrtIofCallDriver;
decltype(KfAcquireSpinLock)                *g_pfnrtKfAcquireSpinLock;
decltype(KfReleaseSpinLock)                *g_pfnrtKfReleaseSpinLock;
decltype(KefAcquireSpinLockAtDpcLevel)     *g_pfnrtKefAcquireSpinLockAtDpcLevel;
decltype(KefReleaseSpinLockFromDpcLevel)   *g_pfnrtKefReleaseSpinLockFromDpcLevel;
decltype(KfLowerIrql)                      *g_pfnrtKfLowerIrql;
decltype(KfRaiseIrql)                      *g_pfnrtKfRaiseIrql;

VOID                            (__stdcall *g_pfnrtIoCompleteRequest)(PIRP, CCHAR);
LONG_PTR                        (__stdcall *g_pfnrtObDereferenceObject)(PVOID);
NTSTATUS                        (__stdcall *g_pfnrtIoCallDriver)(PDEVICE_OBJECT, PIRP);
KIRQL                           (__stdcall *g_pfnrtKeAcquireSpinLock)(PKSPIN_LOCK);
VOID                            (__stdcall *g_pfnrtKeReleaseSpinLock)(PKSPIN_LOCK, KIRQL);
KIRQL                           (__stdcall *g_pfnrtKeAcquireSpinLockAtDpcLevel)(PKSPIN_LOCK);
VOID                            (__stdcall *g_pfnrtKeReleaseSpinLockFromDpcLevel)(PKSPIN_LOCK);
VOID                            (__stdcall *g_pfnrtKeLowerIrql)(KIRQL);
KIRQL                           (__stdcall *g_pfnrtKeRaiseIrql)(KIRQL);
/** @} */

/** @name DATA exports and associated stuff
 * @{ */
/** Import address table entry for KeTickCount (defined in asm). */
extern KSYSTEM_TIME                        *_imp__KeTickCount;
/** @} */

RT_C_DECLS_END


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static void rtR0Nt3InitModuleInfo(void);


/**
 * Converts a string to a number, stopping at the first non-digit.
 *
 * @returns The value
 * @param   ppwcValue       Pointer to the string pointer variable.  Updated.
 * @param   pcwcValue       Pointer to the string length variable.  Updated.
 */
static uint32_t rtR0Nt3StringToNum(PCRTUTF16 *ppwcValue, size_t *pcwcValue)
{
    uint32_t  uValue   = 0;
    PCRTUTF16 pwcValue = *ppwcValue;
    size_t    cwcValue = *pcwcValue;

    while (cwcValue > 0)
    {
        RTUTF16 uc = *pwcValue;
        unsigned uDigit = (unsigned)uc - (unsigned)'0';
        if (uDigit < (unsigned)10)
        {
            uValue *= 10;
            uValue += uDigit;
        }
        else
            break;
        pwcValue++;
        cwcValue--;
    }

    *ppwcValue = pwcValue;
    *pcwcValue = cwcValue;
    return uValue;
}


/**
 * Implements RTL_QUERY_REGISTRY_ROUTINE for processing
 * 'HKLM/Software/Microsoft/Window NT/CurrentVersion/CurrentVersion'
 */
static NTSTATUS NTAPI rtR0Nt3VerEnumCallback_CurrentVersion(PWSTR pwszValueName, ULONG uValueType,
                                                            PVOID pvValue, ULONG cbValue, PVOID pvUser, PVOID pvEntryCtx)
{
    RT_NOREF(pwszValueName, pvEntryCtx);
    if (   uValueType == REG_SZ
        || uValueType == REG_EXPAND_SZ)
    {
        PCRTUTF16 pwcValue = (PCRTUTF16)pvValue;
        size_t    cwcValue  = cbValue / sizeof(*pwcValue);
        uint32_t uMajor = rtR0Nt3StringToNum(&pwcValue, &cwcValue);
        uint32_t uMinor = 0;
        if (cwcValue > 1)
        {
            pwcValue++;
            cwcValue--;
            uMinor = rtR0Nt3StringToNum(&pwcValue, &cwcValue);
        }

        if (uMajor >= 3)
        {
            g_uNt3MajorVer = uMajor;
            g_uNt3MinorVer = uMinor;
            RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentVersion found: uMajor=%u uMinor=%u\n", uMajor, uMinor);
            *(uint32_t *)pvUser |= RT_BIT_32(0);
            return STATUS_SUCCESS;
        }

        RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentVersion: '%.*ls'\n", cbValue / sizeof(RTUTF16), pvValue);
    }
    else
        RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentVersion: uValueType=%u %.*Rhxs\n", uValueType, cbValue, pvValue);
    return STATUS_SUCCESS;
}


/**
 * Implements RTL_QUERY_REGISTRY_ROUTINE for processing
 * 'HKLM/Software/Microsoft/Window NT/CurrentVersion/CurrentBuildNumber'
 */
static NTSTATUS NTAPI rtR0Nt3VerEnumCallback_CurrentBuildNumber(PWSTR pwszValueName, ULONG uValueType,
                                                                PVOID pvValue, ULONG cbValue, PVOID pvUser, PVOID pvEntryCtx)
{
    RT_NOREF(pwszValueName, pvEntryCtx);
    if (   uValueType == REG_SZ
        || uValueType == REG_EXPAND_SZ)
    {
        PCRTUTF16 pwcValue = (PCRTUTF16)pvValue;
        size_t    cwcValue  = cbValue / sizeof(*pwcValue);
        uint32_t uBuildNo = rtR0Nt3StringToNum(&pwcValue, &cwcValue);

        if (uBuildNo >= 100 && uBuildNo < _1M)
        {
            g_uNt3BuildNo = uBuildNo;
            RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentBuildNumber found: uBuildNo=%u\n", uBuildNo);
            *(uint32_t *)pvUser |= RT_BIT_32(1);
            return STATUS_SUCCESS;
        }

        RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentBuildNumber: '%.*ls'\n", cbValue / sizeof(RTUTF16), pvValue);
    }
    else
        RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentBuildNumber: uValueType=%u %.*Rhxs\n", uValueType, cbValue, pvValue);
    return STATUS_SUCCESS;
}


/**
 * Implements RTL_QUERY_REGISTRY_ROUTINE for processing
 * 'HKLM/Software/Microsoft/Window NT/CurrentVersion/CurrentType'
 */
static NTSTATUS NTAPI rtR0Nt3VerEnumCallback_CurrentType(PWSTR pwszValueName, ULONG uValueType,
                                                         PVOID pvValue, ULONG cbValue, PVOID pvUser, PVOID pvEntryCtx)
{
    RT_NOREF(pwszValueName, pvEntryCtx);
    if (   uValueType == REG_SZ
        || uValueType == REG_EXPAND_SZ)
    {
        PCRTUTF16 pwcValue = (PCRTUTF16)pvValue;
        size_t    cwcValue = cbValue / sizeof(*pwcValue);

        int fSmp = -1;
        if (cwcValue >= 12 && RTUtf16NICmpAscii(pwcValue, "Uniprocessor", 12) == 0)
        {
            cwcValue -= 12;
            pwcValue += 12;
            fSmp = 0;
        }
        else if (cwcValue >= 14 && RTUtf16NICmpAscii(pwcValue, "Multiprocessor", 14) == 0)
        {
            cwcValue -= 14;
            pwcValue += 14;
            fSmp = 1;
        }
        if (fSmp != -1)
        {
            while (cwcValue > 0 && RT_C_IS_SPACE(*pwcValue))
                cwcValue--, pwcValue++;

            int fChecked = -1;
            if (cwcValue >= 4 && RTUtf16NICmpAscii(pwcValue, "Free", 4) == 0)
                fChecked = 0;
            else if (cwcValue >= 7 && RTUtf16NICmpAscii(pwcValue, "Checked", 7) == 0)
                fChecked = 1;
            if (fChecked != -1)
            {
                g_fNt3Smp     = fSmp     != 0;
                g_fNt3Checked = fChecked != 0;
                RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentType found: fSmp=%d fChecked=%d\n", fSmp, fChecked);
                *(uint32_t *)pvUser |= RT_BIT_32(2);
                return STATUS_SUCCESS;
            }
        }

        RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentType: '%.*ls'\n", cbValue / sizeof(RTUTF16), pvValue);
    }
    else
        RTLogBackdoorPrintf("rtR0Nt3VerEnumCallback_CurrentType: uValueType=%u %.*Rhxs\n", uValueType, cbValue, pvValue);
    return STATUS_SUCCESS;
}


/**
 * Figure out the NT 3 version from the registry.
 *
 * @note this will be called before the rtR0Nt3InitSymbols is called.
 */
static void rtR0Nt3InitVersion(void)
{
    /*
     * No PsGetVersion, so try the registry.  Unfortunately not necessarily
     * initialized when we're loaded.
     */
    RTL_QUERY_REGISTRY_TABLE aQuery[4];
    RT_ZERO(aQuery);
    aQuery[0].QueryRoutine = rtR0Nt3VerEnumCallback_CurrentVersion;
    aQuery[0].Flags        = 0;
    aQuery[0].Name         = L"CurrentVersion";
    aQuery[0].EntryContext = NULL;
    aQuery[0].DefaultType  = REG_NONE;

    aQuery[1].QueryRoutine = rtR0Nt3VerEnumCallback_CurrentBuildNumber;
    aQuery[1].Flags        = 0;
    aQuery[1].Name         = L"CurrentBuildNumber";
    aQuery[1].EntryContext = NULL;
    aQuery[1].DefaultType  = REG_NONE;

    aQuery[2].QueryRoutine = rtR0Nt3VerEnumCallback_CurrentType;
    aQuery[2].Flags        = 0;
    aQuery[2].Name         = L"CurrentType";
    aQuery[2].EntryContext = NULL;
    aQuery[2].DefaultType  = REG_NONE;

    uint32_t fFound = 0;
    //NTSTATUS rcNt = RtlQueryRegistryValues(RTL_REGISTRY_WINDOWS_NT, NULL, &aQuery[0], &fFound, NULL /*Environment*/);
    NTSTATUS rcNt = RtlQueryRegistryValues(RTL_REGISTRY_ABSOLUTE,
                                           L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion",
                                           &aQuery[0], &fFound, NULL /*Environment*/);
    if (!NT_SUCCESS(rcNt))
        RTLogBackdoorPrintf("rtR0Nt3InitVersion: RtlQueryRegistryValues failed: %#x\n", rcNt);
    else
        RTLogBackdoorPrintf("rtR0Nt3InitVersion: Didn't get all values: fFound=%#x\n", fFound);

    /*
     * We really need the version number.  Build, type and SMP is off less importance.
     * Derive it from the NT kernel PE header.
     */
    if (!(fFound & RT_BIT_32(0)))
    {
        if (!g_fNt3ModuleInfoInitialized)
            rtR0Nt3InitModuleInfo();

        PIMAGE_DOS_HEADER   pMzHdr  = (PIMAGE_DOS_HEADER)g_pbNt3OsKrnl;
        PIMAGE_NT_HEADERS32 pNtHdrs = (PIMAGE_NT_HEADERS32)&g_pbNt3OsKrnl[pMzHdr->e_lfanew];
        if (pNtHdrs->OptionalHeader.MajorOperatingSystemVersion == 1)
        {
            /* NT 3.1 and NT 3.50 both set OS version to 1.0 in the optional header. */
            g_uNt3MajorVer = 3;
            if (   pNtHdrs->OptionalHeader.MajorLinkerVersion == 2
                && pNtHdrs->OptionalHeader.MinorLinkerVersion < 50)
                g_uNt3MinorVer = 10;
            else
                g_uNt3MinorVer = 50;
        }
        else
        {
            g_uNt3MajorVer = pNtHdrs->OptionalHeader.MajorOperatingSystemVersion;
            g_uNt3MinorVer = pNtHdrs->OptionalHeader.MinorOperatingSystemVersion;
        }
        RTLogBackdoorPrintf("rtR0Nt3InitVersion: guessed %u.%u from PE header\n", g_uNt3MajorVer, g_uNt3MinorVer);

        /* Check out the resource section, looking for VS_FIXEDFILEINFO. */
        __try /* (pointless) */
        {
            PIMAGE_SECTION_HEADER paShdrs = (PIMAGE_SECTION_HEADER)(pNtHdrs + 1);
            uint32_t const        cShdrs  = pNtHdrs->FileHeader.NumberOfSections;
            uint32_t              iShdr   = 0;
            while (iShdr < cShdrs && memcmp(paShdrs[iShdr].Name, ".rsrc", 6) != 0)
                iShdr++;
            if (iShdr < cShdrs)
            {
                if (   paShdrs[iShdr].VirtualAddress > 0
                    && paShdrs[iShdr].VirtualAddress < pNtHdrs->OptionalHeader.SizeOfImage)
                {
                    uint32_t const  cbRsrc   = RT_MIN(paShdrs[iShdr].Misc.VirtualSize
                                                      ? paShdrs[iShdr].Misc.VirtualSize : paShdrs[iShdr].SizeOfRawData,
                                                      pNtHdrs->OptionalHeader.SizeOfImage - paShdrs[iShdr].VirtualAddress);
                    uint8_t const  *pbRsrc   = &g_pbNt3OsKrnl[paShdrs[iShdr].VirtualAddress];
                    uint32_t const *puDwords = (uint32_t const *)pbRsrc;
                    uint32_t        cDWords  = (cbRsrc - sizeof(VS_FIXEDFILEINFO) + sizeof(uint32_t)) / sizeof(uint32_t);
                    while (cDWords-- > 0)
                    {
                        if (   puDwords[0] == VS_FFI_SIGNATURE
                            && puDwords[1] == VS_FFI_STRUCVERSION)
                        {
                            VS_FIXEDFILEINFO const *pVerInfo = (VS_FIXEDFILEINFO const *)puDwords;
                            g_uNt3MajorVer = pVerInfo->dwProductVersionMS >> 16;
                            g_uNt3MinorVer = pVerInfo->dwProductVersionMS >> 16;
                            g_uNt3BuildNo  = pVerInfo->dwProductVersionLS >> 16;
                            RTLogBackdoorPrintf("rtR0Nt3InitVersion: Found version info %u.%u build %u\n",
                                                g_uNt3MajorVer, g_uNt3MinorVer, g_uNt3BuildNo);
                            break;
                        }
                        puDwords++;
                    }
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            RTLogBackdoorPrintf("rtR0Nt3InitVersion: Exception scanning .rsrc section for version info!\n");
        }
    }

    /*
     * If we've got PsGetVersion, use it to override the above finding!
     * (We may end up here for reasons other than the PsGetVersion fallback.)
     */
    if (g_pfnrtPsGetVersion)
    {
        WCHAR          wszCsd[64];
        UNICODE_STRING UniStr;
        UniStr.Buffer        = wszCsd;
        UniStr.MaximumLength = sizeof(wszCsd) - sizeof(WCHAR);
        UniStr.Length        = 0;
        RT_ZERO(wszCsd);
        ULONG   uMajor   = 3;
        ULONG   uMinor   = 51;
        ULONG   uBuildNo = 1057;
        BOOLEAN fChecked = g_pfnrtPsGetVersion(&uMajor, &uMinor, &uBuildNo, &UniStr);

        g_uNt3MajorVer           = uMajor;
        g_uNt3MinorVer           = uMinor;
        g_uNt3BuildNo            = uBuildNo;
        g_fNt3Checked            = fChecked != FALSE;
    }

    g_fNt3VersionInitialized = true;
}


extern "C" DECLEXPORT(BOOLEAN) __stdcall
Nt3Fb_PsGetVersion(ULONG *puMajor, ULONG *puMinor, ULONG *puBuildNo, UNICODE_STRING *pCsdStr)
{
    if (!g_fNt3VersionInitialized)
        rtR0Nt3InitVersion();
    if (puMajor)
        *puMajor = g_uNt3MajorVer;
    if (puMinor)
        *puMinor = g_uNt3MinorVer;
    if (puBuildNo)
        *puBuildNo = g_uNt3BuildNo;
    if (pCsdStr)
    {
        pCsdStr->Buffer[0] = '\0';
        pCsdStr->Length = 0;
    }
    return g_fNt3Checked;
}


/**
 * Worker for rtR0Nt3InitModuleInfo.
 */
static bool rtR0Nt3InitModuleInfoOne(const char *pszImage, uint8_t const *pbCode, uint8_t **ppbModule, uint32_t *pcbModule)
{
    uintptr_t const uImageAlign = _4K; /* XP may put the kernel at */

    /* Align pbCode. */
    pbCode = (uint8_t const *)((uintptr_t)pbCode & ~(uintptr_t)(uImageAlign - 1));

    /* Scan backwards till we find a PE signature. */
    for (uint32_t cbChecked = 0; cbChecked < _64M; cbChecked += uImageAlign, pbCode -= uImageAlign)
    {
        if (!MmIsAddressValid((void *)pbCode))
            continue;

        uint32_t uZero     = 0;
        uint32_t offNewHdr = 0;
        __try /* pointless */
        {
            uZero     = *(uint32_t const *)pbCode;
            offNewHdr = *(uint32_t const *)&pbCode[RT_UOFFSETOF(IMAGE_DOS_HEADER, e_lfanew)];
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            RTLogBackdoorPrintf("rtR0Nt3InitModuleInfo: Exception at %p scanning for DOS header...\n", pbCode);
            continue;
        }
        if (   (uint16_t)uZero == IMAGE_DOS_SIGNATURE
            && offNewHdr < _2K
            && offNewHdr >= sizeof(IMAGE_DOS_HEADER))
        {
            RT_CONCAT(IMAGE_NT_HEADERS,ARCH_BITS) NtHdrs;
            __try /* pointless */
            {
                NtHdrs = *(decltype(NtHdrs) const *)&pbCode[offNewHdr];
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                RTLogBackdoorPrintf("rtR0Nt3InitModuleInfo: Exception at %p reading NT headers...\n", pbCode);
                continue;
            }
            if (   NtHdrs.Signature == IMAGE_NT_SIGNATURE
                && NtHdrs.FileHeader.SizeOfOptionalHeader == sizeof(NtHdrs.OptionalHeader)
                && NtHdrs.FileHeader.NumberOfSections > 2
                && NtHdrs.FileHeader.NumberOfSections < _4K
                && NtHdrs.OptionalHeader.Magic == RT_CONCAT3(IMAGE_NT_OPTIONAL_HDR,ARCH_BITS,_MAGIC))
            {
                *ppbModule = (uint8_t *)pbCode;
                *pcbModule = NtHdrs.OptionalHeader.SizeOfImage;
                RTLogBackdoorPrintf("rtR0Nt3InitModuleInfo: Found %s at %#p LB %#x\n",
                                    pszImage, pbCode, NtHdrs.OptionalHeader.SizeOfImage);
                return true;
            }
        }
    }
    RTLogBackdoorPrintf("rtR0Nt3InitModuleInfo: Warning! Unable to locate %s...\n");
    return false;
}


/**
 * Initializes the module information (NTOSKRNL + HAL) using exported symbols.
 * This only works as long as noone is intercepting the symbols.
 */
static void rtR0Nt3InitModuleInfo(void)
{
    rtR0Nt3InitModuleInfoOne("ntoskrnl.exe", (uint8_t const *)(uintptr_t)IoGetCurrentProcess, &g_pbNt3OsKrnl, &g_cbNt3OsKrnl);
    rtR0Nt3InitModuleInfoOne("hal.dll",      (uint8_t const *)(uintptr_t)HalGetBusData,       &g_pbNt3Hal,    &g_cbNt3Hal);
    g_fNt3ModuleInfoInitialized = true;
}


extern "C" DECLEXPORT(NTSTATUS) __stdcall
Nt3Fb_ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS enmClass, PVOID pvBuf, ULONG cbBuf, PULONG pcbActual)
{
    switch (enmClass)
    {
        case SystemModuleInformation:
        {
            PRTL_PROCESS_MODULES pInfo    = (PRTL_PROCESS_MODULES)pvBuf;
            ULONG                cbNeeded = RT_UOFFSETOF(RTL_PROCESS_MODULES, Modules[2]);
            if (pcbActual)
                *pcbActual = cbNeeded;
            if (cbBuf < cbNeeded)
                return STATUS_INFO_LENGTH_MISMATCH;

            if (!g_fNt3ModuleInfoInitialized)
                rtR0Nt3InitModuleInfo();

            pInfo->NumberOfModules = 2;

            /* ntoskrnl.exe */
            pInfo->Modules[0].Section           = NULL;
            pInfo->Modules[0].MappedBase        = g_pbNt3OsKrnl;
            pInfo->Modules[0].ImageBase         = g_pbNt3OsKrnl;
            pInfo->Modules[0].ImageSize         = g_cbNt3OsKrnl;
            pInfo->Modules[0].Flags             = 0;
            pInfo->Modules[0].LoadOrderIndex    = 0;
            pInfo->Modules[0].InitOrderIndex    = 0;
            pInfo->Modules[0].LoadCount         = 1024;
            pInfo->Modules[0].OffsetToFileName  = sizeof("\\SystemRoot\\System32\\") - 1;
            memcpy(pInfo->Modules[0].FullPathName, RT_STR_TUPLE("\\SystemRoot\\System32\\ntoskrnl.exe"));

            /* hal.dll */
            pInfo->Modules[1].Section           = NULL;
            pInfo->Modules[1].MappedBase        = g_pbNt3Hal;
            pInfo->Modules[1].ImageBase         = g_pbNt3Hal;
            pInfo->Modules[1].ImageSize         = g_cbNt3Hal;
            pInfo->Modules[1].Flags             = 0;
            pInfo->Modules[1].LoadOrderIndex    = 1;
            pInfo->Modules[1].InitOrderIndex    = 0;
            pInfo->Modules[1].LoadCount         = 1024;
            pInfo->Modules[1].OffsetToFileName  = sizeof("\\SystemRoot\\System32\\") - 1;
            memcpy(pInfo->Modules[1].FullPathName, RT_STR_TUPLE("\\SystemRoot\\System32\\hal.dll"));

            return STATUS_SUCCESS;
        }

        default:
            return STATUS_INVALID_INFO_CLASS;
    }
}

/**
 * Calculates the length indicated by an ModR/M sequence.
 *
 * @returns Length, including RM byte.
 * @param   bRm         The RM byte.
 */
static uint32_t rtR0Nt3CalcModRmLength(uint8_t bRm)
{
    uint32_t cbRm = 1;

    if (   (bRm & X86_MODRM_MOD_MASK) == (3 << X86_MODRM_MOD_SHIFT)
        || (bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5)
        cbRm += 4; /* disp32 */
    else if ((bRm & X86_MODRM_MOD_MASK) == (1 << X86_MODRM_MOD_SHIFT))
        cbRm += 1; /* disp8 */
    else if ((bRm & X86_MODRM_MOD_MASK) == (2 << X86_MODRM_MOD_SHIFT))
        cbRm += 2; /* disp16 */

    if ((bRm & X86_MODRM_RM_MASK) == 4 && (bRm & X86_MODRM_MOD_MASK) != (3 << X86_MODRM_MOD_SHIFT))
        cbRm += 1; /* SIB */

    return cbRm;
}


/**
 * Init symbols.
 *
 * This is called after both ZwQuerySystemInformation and PsGetVersion are used
 * for the first time.
 *
 * @returns IPRT status code
 * @param   hKrnlInfo           Kernel symbol digger handle.
 */
DECLHIDDEN(int) rtR0Nt3InitSymbols(RTDBGKRNLINFO hKrnlInfo)
{
    /*
     * Resolve symbols.  (We set C variables (g_pfnrtXxx) here, not the __imp__Xxx ones.)
     */
#define GET_SYSTEM_ROUTINE(a_fnName) do { \
            RT_CONCAT(g_pfnrt, a_fnName) = (decltype(RT_CONCAT(g_pfnrt, a_fnName)))RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, #a_fnName); \
        } while (0)

    GET_SYSTEM_ROUTINE(PsGetVersion);
    GET_SYSTEM_ROUTINE(ZwQuerySystemInformation);
    GET_SYSTEM_ROUTINE(KeSetTimerEx);
    GET_SYSTEM_ROUTINE(IoAttachDeviceToDeviceStack);
    GET_SYSTEM_ROUTINE(PsGetCurrentProcessId);
    GET_SYSTEM_ROUTINE(ZwYieldExecution);
    GET_SYSTEM_ROUTINE(ExAcquireFastMutex);
    GET_SYSTEM_ROUTINE(ExReleaseFastMutex);

#define GET_FAST_CALL_SYSTEM_ROUTINE(a_fnFastcall, a_fnStdcall) do { \
            GET_SYSTEM_ROUTINE(a_fnFastcall); \
            GET_SYSTEM_ROUTINE(a_fnStdcall); \
            AssertLogRelReturn(RT_CONCAT(g_pfnrt,a_fnFastcall) || RT_CONCAT(g_pfnrt,a_fnStdcall), VERR_INTERNAL_ERROR_3); \
        } while (0)
    GET_FAST_CALL_SYSTEM_ROUTINE(IofCompleteRequest,                IoCompleteRequest);
    GET_FAST_CALL_SYSTEM_ROUTINE(ObfDereferenceObject,              ObDereferenceObject);
    GET_FAST_CALL_SYSTEM_ROUTINE(IofCallDriver,                     IoCallDriver);
    GET_FAST_CALL_SYSTEM_ROUTINE(KfAcquireSpinLock,                 KeAcquireSpinLock);
    GET_FAST_CALL_SYSTEM_ROUTINE(KfReleaseSpinLock,                 KeReleaseSpinLock);
    GET_FAST_CALL_SYSTEM_ROUTINE(KfLowerIrql,                       KeLowerIrql);
    GET_FAST_CALL_SYSTEM_ROUTINE(KfRaiseIrql,                       KeRaiseIrql);
    GET_FAST_CALL_SYSTEM_ROUTINE(KefAcquireSpinLockAtDpcLevel,      KeAcquireSpinLockAtDpcLevel);
    GET_FAST_CALL_SYSTEM_ROUTINE(KefReleaseSpinLockFromDpcLevel,    KeReleaseSpinLockFromDpcLevel);

    /*
     * We need to call assembly to update the __imp__Xxx entries, since C
     * doesn't allow '@' in symbols.
     */
    rtNt3InitSymbolsAssembly();

    /*
     * Tick count data.  We disassemble KeQueryTickCount until we find the
     * first absolute address referenced in it.
     *      %80105b70 8b 44 24 04             mov eax, dword [esp+004h]
     *      %80105b74 c7 40 04 00 00 00 00    mov dword [eax+004h], 000000000h
     *      %80105b7b 8b 0d 88 70 19 80       mov ecx, dword [080197088h]
     *      %80105b81 89 08                   mov dword [eax], ecx
     *      %80105b83 c2 04 00                retn 00004h
     */
    _imp__KeTickCount = (decltype(_imp__KeTickCount))RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "KeTickCount");
    if (!_imp__KeTickCount)
    {
        if (!g_fNt3VersionInitialized)
            rtR0Nt3InitVersion();
        Assert(g_uNt3MajorVer == 3 && g_uNt3MinorVer < 50);

        uint8_t const *pbCode = (uint8_t const *)RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "KeQueryTickCount");
        AssertLogRelReturn(pbCode, VERR_INTERNAL_ERROR_2);

        for (uint32_t off = 0; off < 128 && _imp__KeTickCount == NULL;)
        {
            uint8_t const b1 = pbCode[off++];
            switch (b1)
            {
                case 0x8b: /* mov reg, r/m     ; We're looking for absolute address in r/m. */
                    if ((pbCode[off] & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5 /*disp32*/)
                        _imp__KeTickCount = *(KSYSTEM_TIME **)&pbCode[off + 1];
                    RT_FALL_THRU();
                case 0x89: /* mov r/m, reg */
                    off += rtR0Nt3CalcModRmLength(pbCode[off]);
                    break;

                case 0xc7:
                    if ((pbCode[off] & X86_MODRM_REG_MASK) == 0) /* mov r/m, imm32 */
                        off += rtR0Nt3CalcModRmLength(pbCode[off]) + 4;
                    else
                    {
                        RTLogBackdoorPrintf("rtR0Nt3InitSymbols: Failed to find KeTickCount! Encountered unknown opcode at %#x! %.*Rhxs\n",
                                            off - 1, RT_MAX(off + 16, RT_MIN(PAGE_SIZE - ((uintptr_t)pbCode & PAGE_OFFSET_MASK), 128)), pbCode);
                        return VERR_INTERNAL_ERROR_3;
                    }
                    break;

                case 0xc2: /* ret iw */
                    RTLogBackdoorPrintf("rtR0Nt3InitSymbols: Failed to find KeTickCount! Encountered RET! %.*Rhxs\n",
                                        off + 2, pbCode);
                    return VERR_INTERNAL_ERROR_3;

                default:
                    RTLogBackdoorPrintf("rtR0Nt3InitSymbols: Failed to find KeTickCount! Encountered unknown opcode at %#x! %.*Rhxs\n",
                                        off - 1, RT_MAX(off + 16, RT_MIN(PAGE_SIZE - ((uintptr_t)pbCode & PAGE_OFFSET_MASK), 128)), pbCode);
                    return VERR_INTERNAL_ERROR_3;

                /* Just in case: */

                case 0xa1: /* mov eax, [m32] */
                    _imp__KeTickCount = *(KSYSTEM_TIME **)&pbCode[off];
                    off += 4;
                    break;

                case 50: case 51: case 52: case 53: case 54: case 55: case 56: case 57: /* push reg */
                    break;
            }
        }
        if (!_imp__KeTickCount)
        {
            RTLogBackdoorPrintf("rtR0Nt3InitSymbols: Failed to find KeTickCount after 128 bytes! %.*Rhxs\n", 128, pbCode);
            return VERR_INTERNAL_ERROR_3;
        }
    }

    return VINF_SUCCESS;
}


extern "C" DECLEXPORT(VOID)
Nt3Fb_KeInitializeTimerEx(PKTIMER pTimer, TIMER_TYPE enmType)
{
    KeInitializeTimer(pTimer);
    NOREF(enmType);
    /** @todo Default is NotificationTimer, for SyncrhonizationTimer we need to
     *        do more work.  timer-r0drv-nt.cpp is using the latter. :/  */
}


extern "C" DECLEXPORT(BOOLEAN) __stdcall
Nt3Fb_KeSetTimerEx(PKTIMER pTimer, LARGE_INTEGER DueTime, LONG cMsPeriod, PKDPC pDpc)
{
    AssertReturn(cMsPeriod == 0, FALSE);
    return KeSetTimer(pTimer, DueTime, pDpc);
}


extern "C" DECLEXPORT(PDEVICE_OBJECT)
Nt3Fb_IoAttachDeviceToDeviceStack(PDEVICE_OBJECT pSourceDevice, PDEVICE_OBJECT pTargetDevice)
{
    NOREF(pSourceDevice); NOREF(pTargetDevice);
    return NULL;
}


extern "C" DECLEXPORT(HANDLE)
Nt3Fb_PsGetCurrentProcessId(void)
{
    if (!g_fNt3VersionInitialized)
        rtR0Nt3InitVersion();

    uint8_t const *pbProcess = (uint8_t const *)IoGetCurrentProcess();
    if (   g_uNt3MajorVer > 3
        || g_uNt3MinorVer >= 50)
        return *(HANDLE const *)&pbProcess[0x94];
    return *(HANDLE const *)&pbProcess[0xb0];
}


extern "C" DECLEXPORT(NTSTATUS)
Nt3Fb_ZwYieldExecution(VOID)
{
    LARGE_INTEGER Interval;
    Interval.QuadPart = 0;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    return STATUS_SUCCESS;
}


/**
 * This is a simple implementation of the fast mutex api introduced in 3.50.
 */
extern "C" DECLEXPORT(VOID) FASTCALL
Nt3Fb_ExAcquireFastMutex(PFAST_MUTEX pFastMtx)
{
    PETHREAD pSelf = PsGetCurrentThread();
    KIRQL    OldIrql;
    KeRaiseIrql(APC_LEVEL, &OldIrql);

    /* The Count member is initialized to 1.  So if we decrement it to zero, we're
       the first locker and owns the mutex.  Otherwise we must wait for our turn. */
    int32_t  cLockers = ASMAtomicDecS32((int32_t volatile *)&pFastMtx->Count);
    if (cLockers != 0)
    {
        ASMAtomicIncU32((uint32_t volatile *)&pFastMtx->Contention);
        KeWaitForSingleObject(&pFastMtx->Event, Executive, KernelMode, FALSE /*fAlertable*/, NULL /*pTimeout*/);
    }

    pFastMtx->Owner   = (PKTHREAD)pSelf;
    pFastMtx->OldIrql = OldIrql;
}


/**
 * This is a simple implementation of the fast mutex api introduced in 3.50.
 */
extern "C" DECLEXPORT(VOID) FASTCALL
Nt3Fb_ExReleaseFastMutex(PFAST_MUTEX pFastMtx)
{
    AssertMsg(pFastMtx->Owner == (PKTHREAD)PsGetCurrentThread(), ("Owner=%p, expected %p\n", pFastMtx->Owner, PsGetCurrentThread()));

    KIRQL    OldIrql  = pFastMtx->OldIrql;
    pFastMtx->Owner   = NULL;
    int32_t  cLockers = ASMAtomicIncS32((int32_t volatile *)&pFastMtx->Count);
    if (cLockers <= 0)
        KeSetEvent(&pFastMtx->Event, EVENT_INCREMENT, FALSE /*fWait*/);
    if (OldIrql != APC_LEVEL)
        KeLowerIrql(OldIrql);
}

