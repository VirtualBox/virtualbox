/* $Id: SUPR3HardenedMainImports-win.cpp 107702 2025-01-10 10:45:42Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Support Library - Hardened Main, Windows Import Trickery.
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
#include <iprt/nt/nt-and-windows.h>

#include <VBox/sup.h>
#include <VBox/err.h>
#include <iprt/ctype.h>
#include <iprt/initterm.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "SUPLibInternal.h"
#include "SUPHardenedVerify-win.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SUPHARNT_COMMENT(a_Blah) /* nothing */

#define VBOX_HARDENED_STUB_WITHOUT_IMPORTS
#ifdef VBOX_HARDENED_STUB_WITHOUT_IMPORTS
# define SUPHNTIMP_ERROR(a_fReportErrors, a_id, a_szWhere, a_enmOp, a_rc, ...) \
    do { \
        const char * const pszWhereError = (a_szWhere); \
        if (a_fReportErrors) supR3HardenedFatalMsg(pszWhereError, a_enmOp, a_rc, __VA_ARGS__); \
        else { *(uintptr_t *)(uintptr_t)(a_id) += (uintptr_t)pszWhereError; __debugbreak(); } \
    } while (0)
#else
# define SUPHNTIMP_ERROR(a_fReportErrors, a_id, a_szWhere, a_enmOp, a_rc, ...) \
    supR3HardenedFatalMsg(a_szWhere, a_enmOp, a_rc, __VA_ARGS__)

#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Import function entry.
 */
typedef struct SUPHNTIMPFUNC
{
    /** The name of the function we're importing. */
    const char         *pszName;
    /** Where to store the function address (think __imp_ApiName). */
    PFNRT              *ppfnImport;
    /** Pointer to an early dummy function for imports that aren't available
     * during early process initialization. */
    PFNRT               pfnEarlyDummy;
    /** Indicates whether this is an optional import and failure to locate it
     * should set it to NULL instead of freaking out. */
    bool                fOptional;
} SUPHNTIMPFUNC;
/** Pointer to an import table entry.  */
typedef SUPHNTIMPFUNC const *PCSUPHNTIMPFUNC;

/**
 * Information for constructing a direct system call.
 */
typedef struct SUPHNTIMPSYSCALL
{
    /** Where to store the system call number.
     * NULL if this import doesn't stupport direct system call.  */
    uint32_t               *puApiNo;
    /** Assembly system call routine, type 1.  */
    PFNRT                   pfnType1;
    /** Assembly system call routine, type 2.  */
    PFNRT                   pfnType2;
#ifdef RT_ARCH_X86
    /** The parameter size in bytes for a standard call. */
    uint32_t                cbParams;
#endif
} SUPHNTIMPSYSCALL;
/** Pointer to a system call entry. */
typedef SUPHNTIMPSYSCALL const *PCSUPHNTIMPSYSCALL;

/**
 * Import DLL.
 *
 * This contains both static (like name & imports) and runtime information (like
 * load and export table locations).
 *
 * @sa RTDBGNTKRNLMODINFO
 */
typedef struct SUPHNTIMPDLL
{
    /** @name Static data.
     * @{  */
    const wchar_t          *pwszName;
    const char             *pszName;
    size_t                  cImports;
    PCSUPHNTIMPFUNC         paImports;
    /** Array running parallel to paImports if present. */
    PCSUPHNTIMPSYSCALL      paSyscalls;
    /** @} */


    /** The image base. */
    uint8_t const          *pbImageBase;
    /** The NT headers. */
    PIMAGE_NT_HEADERS       pNtHdrs;
    /** The NT header offset/RVA. */
    uint32_t                offNtHdrs;
    /** The end of the section headers. */
    uint32_t                offEndSectHdrs;
    /** The end of the image. */
    uint32_t                cbImage;
    /** Offset of the export directory. */
    uint32_t                offExportDir;
    /** Size of the export directory. */
    uint32_t                cbExportDir;

    /** Exported functions and data by ordinal (RVAs). */
    uint32_t const         *paoffExports;
    /** The number of exports. */
    uint32_t                cExports;
    /** The number of exported names. */
    uint32_t                cNamedExports;
    /** Pointer to the array of exported names (RVAs to strings). */
    uint32_t const         *paoffNamedExports;
    /** Array parallel to paoffNamedExports with the corresponding ordinals
     *  (indexes into paoffExports). */
    uint16_t const         *pau16NameOrdinals;

    /** Number of patched export table entries. */
    uint32_t                cPatchedExports;

} SUPHNTIMPDLL;
/** Pointer to an import DLL entry. */
typedef SUPHNTIMPDLL *PSUPHNTIMPDLL;



/*
 * Declare assembly symbols.
 */
#define SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86) \
    extern PFNRT    RT_CONCAT(g_pfn, a_Name);
#define SUPHARNT_IMPORT_STDCALL_EARLY_OPTIONAL(a_Name, a_cbParamsX86)  SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86)
#define SUPHARNT_IMPORT_SYSCALL(a_Name, a_cbParamsX86) \
    SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86) \
    extern uint32_t RT_CONCAT(g_uApiNo, a_Name); \
    extern FNRT     RT_CONCAT(a_Name, _SyscallType1); \
    extern FNRT     RT_CONCAT(a_Name, _SyscallType2);
#define SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86) \
    extern PFNRT    RT_CONCAT(g_pfn, a_Name); \
    extern FNRT     RT_CONCAT(a_Name, _Early);
#define SUPHARNT_IMPORT_STDCALL_OPTIONAL(a_Name, a_cbParamsX86) SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)

RT_C_DECLS_BEGIN
#include "import-template-ntdll.h"
#include "import-template-kernel32.h"
RT_C_DECLS_END

/*
 * Import functions.
 */
#undef SUPHARNT_IMPORT_SYSCALL
#undef SUPHARNT_IMPORT_STDCALL_EARLY
#undef SUPHARNT_IMPORT_STDCALL_EARLY_OPTIONAL
#undef SUPHARNT_IMPORT_STDCALL
#undef SUPHARNT_IMPORT_STDCALL_OPTIONAL
#define SUPHARNT_IMPORT_SYSCALL(a_Name, a_cbParamsX86) \
    { #a_Name, &RT_CONCAT(g_pfn, a_Name), NULL, false },
#define SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86) \
    { #a_Name, &RT_CONCAT(g_pfn, a_Name), NULL, false },
#define SUPHARNT_IMPORT_STDCALL_EARLY_OPTIONAL(a_Name, a_cbParamsX86) \
    { #a_Name, &RT_CONCAT(g_pfn, a_Name), NULL, true },
#define SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86) \
    { #a_Name, &RT_CONCAT(g_pfn, a_Name), RT_CONCAT(a_Name,_Early), false },
#define SUPHARNT_IMPORT_STDCALL_OPTIONAL(a_Name, a_cbParamsX86) \
    { #a_Name, &RT_CONCAT(g_pfn, a_Name), RT_CONCAT(a_Name,_Early), true },
static const SUPHNTIMPFUNC g_aSupNtImpNtDllFunctions[] =
{
#include "import-template-ntdll.h"
};

static const SUPHNTIMPFUNC g_aSupNtImpKernel32Functions[] =
{
#include "import-template-kernel32.h"
};



/*
 * Syscalls in ntdll.
 */
#undef SUPHARNT_IMPORT_SYSCALL
#undef SUPHARNT_IMPORT_STDCALL_EARLY
#undef SUPHARNT_IMPORT_STDCALL_EARLY_OPTIONAL
#undef SUPHARNT_IMPORT_STDCALL
#undef SUPHARNT_IMPORT_STDCALL_OPTIONAL
#ifdef RT_ARCH_AMD64
# define SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86) \
    { NULL, NULL },
# define SUPHARNT_IMPORT_SYSCALL(a_Name, a_cbParamsX86) \
    { &RT_CONCAT(g_uApiNo, a_Name), &RT_CONCAT(a_Name, _SyscallType1), &RT_CONCAT(a_Name, _SyscallType2) },
#elif defined(RT_ARCH_X86)
# define SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86) \
    { NULL, NULL, NULL, 0 },
# define SUPHARNT_IMPORT_SYSCALL(a_Name, a_cbParamsX86) \
    { &RT_CONCAT(g_uApiNo, a_Name), &RT_CONCAT(a_Name,_SyscallType1), &RT_CONCAT(a_Name, _SyscallType2), a_cbParamsX86 },
#endif
#define SUPHARNT_IMPORT_STDCALL_OPTIONAL(a_Name, a_cbParamsX86)       SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)
#define SUPHARNT_IMPORT_STDCALL_EARLY(a_Name, a_cbParamsX86)          SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)
#define SUPHARNT_IMPORT_STDCALL_EARLY_OPTIONAL(a_Name, a_cbParamsX86) SUPHARNT_IMPORT_STDCALL(a_Name, a_cbParamsX86)
static const SUPHNTIMPSYSCALL g_aSupNtImpNtDllSyscalls[] =
{
#include "import-template-ntdll.h"
};


/**
 * All the DLLs we import from.
 * @remarks Code ASSUMES that ntdll is the first entry.
 */
static SUPHNTIMPDLL g_aSupNtImpDlls[] =
{
    { L"ntdll.dll",      "ntdll.dll",      RT_ELEMENTS(g_aSupNtImpNtDllFunctions), g_aSupNtImpNtDllFunctions, g_aSupNtImpNtDllSyscalls },
    { L"kernelbase.dll", "kernelbase.dll", 0 /* optional module, forwarders only */, NULL, NULL },
    { L"kernel32.dll",   "kernel32.dll",   RT_ELEMENTS(g_aSupNtImpKernel32Functions), g_aSupNtImpKernel32Functions, NULL },
};


static void supR3HardenedFindOrLoadModule(PSUPHNTIMPDLL pDll)
{
#ifdef VBOX_HARDENED_STUB_WITHOUT_IMPORTS
    uint32_t const  cbName     = (uint32_t)RTUtf16Len(pDll->pwszName) * sizeof(WCHAR);
    PPEB_LDR_DATA   pLdrData   = NtCurrentPeb()->Ldr;
    LIST_ENTRY     *pList      = &pLdrData->InMemoryOrderModuleList;
    LIST_ENTRY     *pListEntry = pList->Flink;
    uint32_t        cLoops     = 0;
    while (pListEntry != pList && cLoops < 1024)
    {
        PLDR_DATA_TABLE_ENTRY pLdrEntry = RT_FROM_MEMBER(pListEntry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);

        if (   pLdrEntry->FullDllName.Length > cbName + sizeof(WCHAR)
            && (   pLdrEntry->FullDllName.Buffer[(pLdrEntry->FullDllName.Length - cbName) / sizeof(WCHAR) - 1] == '\\'
                || pLdrEntry->FullDllName.Buffer[(pLdrEntry->FullDllName.Length - cbName) / sizeof(WCHAR) - 1] == '/')
            && RTUtf16ICmpAscii(&pLdrEntry->FullDllName.Buffer[(pLdrEntry->FullDllName.Length - cbName) / sizeof(WCHAR)],
                                pDll->pszName) == 0)
        {
            pDll->pbImageBase = (uint8_t *)pLdrEntry->DllBase;
            return;
        }

        pListEntry = pListEntry->Flink;
        cLoops++;
    }

    if (!pDll->cImports)
        pDll->pbImageBase = NULL; /* optional */
    else
        SUPHNTIMP_ERROR(false, 1, "supR3HardenedFindOrLoadModule", kSupInitOp_Misc, VERR_MODULE_NOT_FOUND,
                        "Failed to locate %ls", pDll->pwszName);
#else
    HMODULE hmod = GetModuleHandleW(pDll->pwszName);
    if (RT_UNLIKELY(!hmod && pDll->cImports))
        SUPHNTIMP_ERROR(true, 1, "supR3HardenedWinInitImports", kSupInitOp_Misc, VERR_MODULE_NOT_FOUND,
                        "Failed to locate %ls", pDll->pwszName);
    pDll->pbImageBase = (uint8_t *)hmod;
#endif
}


/** @sa rtR0DbgKrnlNtParseModule  */
static void supR3HardenedParseModule(PSUPHNTIMPDLL pDll)
{
    /*
     * Locate the PE header, do some basic validations.
     */
    IMAGE_DOS_HEADER const *pMzHdr = (IMAGE_DOS_HEADER const *)pDll->pbImageBase;
    uint32_t           offNtHdrs = 0;
    PIMAGE_NT_HEADERS  pNtHdrs;
    if (pMzHdr->e_magic == IMAGE_DOS_SIGNATURE)
    {
        offNtHdrs = pMzHdr->e_lfanew;
        if (offNtHdrs > _2K)
            SUPHNTIMP_ERROR(false, 2, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_MODULE_NOT_FOUND,
                            "%ls: e_lfanew=%#x, expected a lower value", pDll->pwszName, offNtHdrs);
    }
    pDll->pNtHdrs = pNtHdrs = (PIMAGE_NT_HEADERS)&pDll->pbImageBase[offNtHdrs];

    if (pNtHdrs->Signature != IMAGE_NT_SIGNATURE)
        SUPHNTIMP_ERROR(false, 3, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                        "%ls: Invalid PE signature: %#x", pDll->pwszName, pNtHdrs->Signature);
    if (pNtHdrs->FileHeader.SizeOfOptionalHeader != sizeof(pNtHdrs->OptionalHeader))
        SUPHNTIMP_ERROR(false, 4, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                        "%ls: Unexpected optional header size: %#x", pDll->pwszName, pNtHdrs->FileHeader.SizeOfOptionalHeader);
    if (pNtHdrs->OptionalHeader.Magic != RT_CONCAT3(IMAGE_NT_OPTIONAL_HDR,ARCH_BITS,_MAGIC))
        SUPHNTIMP_ERROR(false, 5, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                        "%ls: Unexpected optional header magic: %#x", pDll->pwszName, pNtHdrs->OptionalHeader.Magic);
    if (pNtHdrs->OptionalHeader.NumberOfRvaAndSizes != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
        SUPHNTIMP_ERROR(false, 6, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                        "%ls: Unexpected number of RVA and sizes: %#x", pDll->pwszName, pNtHdrs->OptionalHeader.NumberOfRvaAndSizes);

    pDll->offNtHdrs      = offNtHdrs;
    pDll->offEndSectHdrs = offNtHdrs
                         + sizeof(*pNtHdrs)
                         + pNtHdrs->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    pDll->cbImage        = pNtHdrs->OptionalHeader.SizeOfImage;

    /*
     * Find the export directory.
     */
    IMAGE_DATA_DIRECTORY ExpDir = pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (   ExpDir.Size < sizeof(IMAGE_EXPORT_DIRECTORY)
        || ExpDir.VirtualAddress < pDll->offEndSectHdrs
        || ExpDir.VirtualAddress >= pNtHdrs->OptionalHeader.SizeOfImage
        || ExpDir.VirtualAddress + ExpDir.Size > pNtHdrs->OptionalHeader.SizeOfImage)
        SUPHNTIMP_ERROR(false, 7, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                        "%ls: Missing or invalid export directory: %#lx LB %#x", pDll->pwszName, ExpDir.VirtualAddress, ExpDir.Size);
    pDll->offExportDir = ExpDir.VirtualAddress;
    pDll->cbExportDir  = ExpDir.Size;

    IMAGE_EXPORT_DIRECTORY const *pExpDir = (IMAGE_EXPORT_DIRECTORY const *)&pDll->pbImageBase[ExpDir.VirtualAddress];

    if (   pExpDir->NumberOfFunctions >= _1M
        || pExpDir->NumberOfFunctions <  1
        || pExpDir->NumberOfNames     >= _1M
        || pExpDir->NumberOfNames     <  1)
        SUPHNTIMP_ERROR(false, 8, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                        "%ls: NumberOfNames or/and NumberOfFunctions are outside the expected range: nof=%#x non=%#x\n",
                        pDll->pwszName, pExpDir->NumberOfFunctions, pExpDir->NumberOfNames);
    pDll->cNamedExports = pExpDir->NumberOfNames;
    pDll->cExports      = RT_MAX(pExpDir->NumberOfNames,  pExpDir->NumberOfFunctions);

    if (   pExpDir->AddressOfFunctions < pDll->offEndSectHdrs
        || pExpDir->AddressOfFunctions >= pNtHdrs->OptionalHeader.SizeOfImage
        || pExpDir->AddressOfFunctions + pDll->cExports * sizeof(uint32_t) > pNtHdrs->OptionalHeader.SizeOfImage)
           SUPHNTIMP_ERROR(false, 9, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                           "%ls: Bad AddressOfFunctions: %#x\n", pDll->pwszName, pExpDir->AddressOfFunctions);
    pDll->paoffExports = (uint32_t const *)&pDll->pbImageBase[pExpDir->AddressOfFunctions];

    if (   pExpDir->AddressOfNames < pDll->offEndSectHdrs
        || pExpDir->AddressOfNames >= pNtHdrs->OptionalHeader.SizeOfImage
        || pExpDir->AddressOfNames + pExpDir->NumberOfNames * sizeof(uint32_t) > pNtHdrs->OptionalHeader.SizeOfImage)
           SUPHNTIMP_ERROR(false, 10, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                           "%ls: Bad AddressOfNames: %#x\n", pDll->pwszName, pExpDir->AddressOfNames);
    pDll->paoffNamedExports = (uint32_t const *)&pDll->pbImageBase[pExpDir->AddressOfNames];

    if (   pExpDir->AddressOfNameOrdinals < pDll->offEndSectHdrs
        || pExpDir->AddressOfNameOrdinals >= pNtHdrs->OptionalHeader.SizeOfImage
        || pExpDir->AddressOfNameOrdinals + pExpDir->NumberOfNames * sizeof(uint32_t) > pNtHdrs->OptionalHeader.SizeOfImage)
           SUPHNTIMP_ERROR(false, 11, "supR3HardenedParseModule", kSupInitOp_Misc, VERR_INVALID_EXE_SIGNATURE,
                           "%ls: Bad AddressOfNameOrdinals: %#x\n", pDll->pwszName, pExpDir->AddressOfNameOrdinals);
    pDll->pau16NameOrdinals = (uint16_t const *)&pDll->pbImageBase[pExpDir->AddressOfNameOrdinals];
}


/** @sa rtR0DbgKrnlInfoLookupSymbol */
static const char *supR3HardenedResolveImport(PSUPHNTIMPDLL pDll, PCSUPHNTIMPFUNC pImport, bool fReportErrors)
{
    /*
     * Binary search.
     */
    uint32_t iStart = 0;
    uint32_t iEnd   = pDll->cNamedExports;
    while (iStart < iEnd)
    {
        uint32_t iCur        = iStart + (iEnd - iStart) / 2;
        uint32_t offExpName  = pDll->paoffNamedExports[iCur];
        if (RT_UNLIKELY(offExpName < pDll->offEndSectHdrs || offExpName >= pDll->cbImage))
            SUPHNTIMP_ERROR(fReportErrors, 12, "supR3HardenedResolveImport", kSupInitOp_Misc, VERR_SYMBOL_NOT_FOUND,
                            "%ls: Bad export name entry: %#x (iCur=%#x)", pDll->pwszName, offExpName, iCur);

        const char *pszExpName = (const char *)&pDll->pbImageBase[offExpName];
        int iDiff = strcmp(pszExpName, pImport->pszName);
        if (iDiff > 0)      /* pszExpName > pszSymbol: search chunck before i */
            iEnd = iCur;
        else if (iDiff < 0) /* pszExpName < pszSymbol: search chunk after i */
            iStart = iCur + 1;
        else                /* pszExpName == pszSymbol */
        {
            uint16_t iExpOrdinal = pDll->pau16NameOrdinals[iCur];
            if (iExpOrdinal < pDll->cExports)
            {
                uint32_t offExport = pDll->paoffExports[iExpOrdinal];

                /* detect export table patching. */
                if (offExport >= pDll->cbImage)
                    pDll->cPatchedExports++;

                if (offExport - pDll->offExportDir >= pDll->cbExportDir)
                {
                    *pImport->ppfnImport = (PFNRT)&pDll->pbImageBase[offExport];
                    return NULL;
                }

                /* Forwarder. */
                return (const char *)&pDll->pbImageBase[offExport];
            }
            SUPHNTIMP_ERROR(fReportErrors, 14, "supR3HardenedResolveImport", kSupInitOp_Misc, VERR_BAD_EXE_FORMAT,
                            "%ls: Name ordinal for '%s' is out of bounds: %#x (max %#x)",
                            pDll->pwszName, iExpOrdinal, pDll->cExports);
            return NULL;
        }
    }

    if (!pImport->fOptional)
        SUPHNTIMP_ERROR(fReportErrors, 15, "supR3HardenedResolveImport", kSupInitOp_Misc, VERR_SYMBOL_NOT_FOUND,
                        "%ls: Failed to resolve '%s'.", pDll->pwszName, pImport->pszName);
    *pImport->ppfnImport = NULL;
    return NULL;
}


static void supR3HardenedDirectSyscall(PSUPHNTIMPDLL pDll, PCSUPHNTIMPFUNC pImport, PCSUPHNTIMPSYSCALL pSyscall,
                                       PSUPHNTLDRCACHEENTRY pLdrEntry, uint8_t *pbBits, bool fReportErrors)
{
    /*
     * Skip non-syscall entries.
     */
    if (!pSyscall->puApiNo)
        return;

    /*
     * Locate the virgin bits.
     */
    RTLDRADDR uValue;
    int rc = RTLdrGetSymbolEx(pLdrEntry->hLdrMod, pbBits, (uintptr_t)pDll->pbImageBase, UINT32_MAX, pImport->pszName, &uValue);
    if (RT_FAILURE(rc))
    {
        SUPHNTIMP_ERROR(fReportErrors, 16, "supR3HardenedDirectSyscall", kSupInitOp_Misc, rc,
                        "%s: RTLdrGetSymbolEx failed on %s: %Rrc", pDll->pszName, pImport->pszName, rc);
        return;
    }
    uintptr_t offSymbol = (uintptr_t)uValue - (uintptr_t)pDll->pbImageBase;
    uint8_t const *pbFunction = &pbBits[offSymbol];

    /*
     * Parse the code and extract the API call number.
     */
#ifdef RT_ARCH_AMD64
    /* Pattern #1: XP64/W2K3-64 thru Windows 10 build 10240.
            0:000> u ntdll!NtCreateSection
            ntdll!NtCreateSection:
            00000000`779f1750 4c8bd1          mov     r10,rcx
            00000000`779f1753 b847000000      mov     eax,47h
            00000000`779f1758 0f05            syscall
            00000000`779f175a c3              ret
            00000000`779f175b 0f1f440000      nop     dword ptr [rax+rax]

       Pattern #2: Windows 10 build 10525+.
            0:000> u ntdll_7ffc26300000!NtCreateSection
            ntdll_7ffc26300000!ZwCreateSection:
            00007ffc`263943e0 4c8bd1          mov     r10,rcx
            00007ffc`263943e3 b84a000000      mov     eax,4Ah
            00007ffc`263943e8 f604250803fe7f01 test    byte ptr [SharedUserData+0x308 (00000000`7ffe0308)],1
            00007ffc`263943f0 7503            jne     ntdll_7ffc26300000!ZwCreateSection+0x15 (00007ffc`263943f5)
            00007ffc`263943f2 0f05            syscall
            00007ffc`263943f4 c3              ret
            00007ffc`263943f5 cd2e            int     2Eh
            00007ffc`263943f7 c3              ret
       */
    if (   pbFunction[ 0] == 0x4c /* mov r10, rcx */
        && pbFunction[ 1] == 0x8b
        && pbFunction[ 2] == 0xd1
        && pbFunction[ 3] == 0xb8 /* mov eax, 0000yyzzh */
        //&& pbFunction[ 4] == 0xZZ
        //&& pbFunction[ 5] == 0xYY
        && pbFunction[ 6] == 0x00
        && pbFunction[ 7] == 0x00)
    {
        if (   pbFunction[ 8] == 0x0f /* syscall */
            && pbFunction[ 9] == 0x05
            && pbFunction[10] == 0xc3 /* ret */ )
        {
            *pSyscall->puApiNo = RT_MAKE_U16(pbFunction[4], pbFunction[5]);
            *pImport->ppfnImport = pSyscall->pfnType1;
            return;
        }
        if (   pbFunction[ 8] == 0xf6 /* test   byte ptr [SharedUserData+0x308 (00000000`7ffe0308)],1 */
            && pbFunction[ 9] == 0x04
            && pbFunction[10] == 0x25
            && pbFunction[11] == 0x08
            && pbFunction[12] == 0x03
            && pbFunction[13] == 0xfe
            && pbFunction[14] == 0x7f
            && pbFunction[15] == 0x01
            && pbFunction[16] == 0x75 /* jnz +3 */
            && pbFunction[17] == 0x03
            && pbFunction[18] == 0x0f /* syscall*/
            && pbFunction[19] == 0x05
            && pbFunction[20] == 0xc3 /* ret */
            && pbFunction[21] == 0xcd /* int 2eh */
            && pbFunction[22] == 0x2e
            && pbFunction[23] == 0xc3 /* ret */ )
        {
            *pSyscall->puApiNo = RT_MAKE_U16(pbFunction[4], pbFunction[5]);
            *pImport->ppfnImport = pSyscall->pfnType2;
            return;
        }
    }
#else
    /* Pattern #1: XP thru Windows 7
            kd> u ntdll!NtCreateSection
            ntdll!NtCreateSection:
            7c90d160 b832000000      mov     eax,32h
            7c90d165 ba0003fe7f      mov     edx,offset SharedUserData!SystemCallStub (7ffe0300)
            7c90d16a ff12            call    dword ptr [edx]
            7c90d16c c21c00          ret     1Ch
            7c90d16f 90              nop
       The variable bit is the value loaded into eax: XP=32h, W2K3=34h, Vista=4bh, W7=54h

       Pattern #2: Windows 8.1
            0:000:x86> u ntdll_6a0f0000!NtCreateSection
            ntdll_6a0f0000!NtCreateSection:
            6a15eabc b854010000      mov     eax,154h
            6a15eac1 e803000000      call    ntdll_6a0f0000!NtCreateSection+0xd (6a15eac9)
            6a15eac6 c21c00          ret     1Ch
            6a15eac9 8bd4            mov     edx,esp
            6a15eacb 0f34            sysenter
            6a15eacd c3              ret
       The variable bit is the value loaded into eax: W81=154h
       Note! One nice thing here is that we can share code pattern #1.  */

    if (   pbFunction[ 0] == 0xb8 /* mov eax, 0000yyzzh*/
        //&& pbFunction[ 1] <= 0xZZ
        //&& pbFunction[ 2] <= 0xYY
        && pbFunction[ 3] == 0x00
        && pbFunction[ 4] == 0x00)
    {
        *pSyscall->puApiNo = RT_MAKE_U16(pbFunction[1], pbFunction[2]);
        if (   pbFunction[5] == 0xba /* mov edx, offset SharedUserData!SystemCallStub */
            && pbFunction[ 6] == 0x00
            && pbFunction[ 7] == 0x03
            && pbFunction[ 8] == 0xfe
            && pbFunction[ 9] == 0x7f
            && pbFunction[10] == 0xff /* call [edx] */
            && pbFunction[11] == 0x12
            && (   (   pbFunction[12] == 0xc2 /* ret 1ch */
                    && pbFunction[13] == pSyscall->cbParams
                    && pbFunction[14] == 0x00)
                || (   pbFunction[12] == 0xc3 /* ret */
                    && pSyscall->cbParams == 0)
                )
           )
        {
            *pImport->ppfnImport = pSyscall->pfnType1;
            return;
        }

        if (   pbFunction[ 5] == 0xe8 /* call [$+3] */
            && RT_ABS(*(int32_t *)&pbFunction[6]) < 0x10
            && (   (   pbFunction[10] == 0xc2 /* ret 1ch */
                    && pbFunction[11] == pSyscall->cbParams
                    && pbFunction[12] == 0x00)
                || (   pbFunction[10] == 0xc3 /* ret */
                    && pSyscall->cbParams == 0)
               )
           )
        {
            *pImport->ppfnImport = pSyscall->pfnType2;
            return;
        }
    }
#endif

    /*
     * Failed to parse it.
     */
    volatile uint8_t abCopy[16];
    memcpy((void *)&abCopy[0], pbFunction, sizeof(abCopy));
    SUPHNTIMP_ERROR(fReportErrors, 17, "supR3HardenedWinInitImports", kSupInitOp_Misc, rc,
                    "%ls: failed to parse syscall: '%s': %.16Rhxs",
                    pDll->pwszName, pImport->pszName, &abCopy[0]);
}


/**
 * Check out system calls and do the directly instead of via NtDll.
 *
 * We need to have access to the on disk NTDLL.DLL file as we do not trust the
 * stuff we find in memory.  Too early to verify signatures though.
 *
 * @param   fReportErrors       Whether we've got the machinery for reporting
 *                              errors going already.
 * @param   pErrInfo            Buffer for gathering additional error info. This
 *                              is mainly to avoid consuming lots of stacks with
 *                              RTERRINFOSTATIC structures.
 */
DECLHIDDEN(void) supR3HardenedWinInitSyscalls(bool fReportErrors, PRTERRINFO pErrInfo)
{
    for (uint32_t iDll = 0; iDll < RT_ELEMENTS(g_aSupNtImpDlls); iDll++)
        if (g_aSupNtImpDlls[iDll].paSyscalls)
        {
            PSUPHNTLDRCACHEENTRY pLdrEntry;
            int rc = supHardNtLdrCacheOpen(g_aSupNtImpDlls[iDll].pszName, &pLdrEntry, pErrInfo);
            if (RT_SUCCESS(rc))
            {
                uint8_t *pbBits;
                rc = supHardNtLdrCacheEntryGetBits(pLdrEntry, &pbBits, (uintptr_t)g_aSupNtImpDlls[iDll].pbImageBase,
                                                   NULL, NULL, pErrInfo);
                if (RT_SUCCESS(rc))
                {
                    for (uint32_t i = 0; i < g_aSupNtImpDlls[iDll].cImports; i++)
                        supR3HardenedDirectSyscall(&g_aSupNtImpDlls[iDll], &g_aSupNtImpDlls[iDll].paImports[i],
                                                   &g_aSupNtImpDlls[iDll].paSyscalls[i], pLdrEntry, pbBits, fReportErrors);
                }
                else
                    SUPHNTIMP_ERROR(fReportErrors, 20, "supR3HardenedWinInitImports", kSupInitOp_Misc, rc,
                                    "%ls: supHardNtLdrCacheEntryGetBits failed: %Rrc %s",
                                    g_aSupNtImpDlls[iDll].pwszName, rc, pErrInfo ? pErrInfo->pszMsg : "");
            }
            else
                SUPHNTIMP_ERROR(fReportErrors, 21, "supR3HardenedWinInitImports", kSupInitOp_Misc, rc,
                                "%ls: supHardNtLdrCacheOpen failed: %Rrc %s",
                                g_aSupNtImpDlls[iDll].pwszName, rc, pErrInfo ? pErrInfo->pszMsg : "");
        }
}


/**
 * Resolves a few NtDll functions we need before child purification is executed.
 *
 * We must not permanently modify any global data here.
 *
 * @param   uNtDllAddr                  The address of the NTDLL.
 * @param   ppfnNtWaitForSingleObject   Where to store the NtWaitForSingleObject
 *                                      address.
 * @param   ppfnNtSetEvent              Where to store the NtSetEvent address.
 */
DECLHIDDEN(void) supR3HardenedWinGetVeryEarlyImports(uintptr_t uNtDllAddr,
                                                     PFNNTWAITFORSINGLEOBJECT *ppfnNtWaitForSingleObject,
                                                     PFNNTSETEVENT *ppfnNtSetEvent)
{
    /*
     * NTDLL is the first entry in the list.  Save it and do the parsing.
     */
    SUPHNTIMPDLL SavedDllEntry = g_aSupNtImpDlls[0];

    g_aSupNtImpDlls[0].pbImageBase = (uint8_t const *)uNtDllAddr;
    supR3HardenedParseModule(&g_aSupNtImpDlls[0]);

    /*
     * Create a temporary import table for the requested APIs and resolve them.
     */
    SUPHNTIMPFUNC aImports[] =
    {
        { "NtWaitForSingleObject",  (PFNRT *)ppfnNtWaitForSingleObject,     NULL, false },
        { "NtSetEvent",             (PFNRT *)ppfnNtSetEvent,                NULL, false },
    };

    for (uint32_t i = 0; i < RT_ELEMENTS(aImports); i++)
    {
        const char *pszForwarder = supR3HardenedResolveImport(&g_aSupNtImpDlls[0], &aImports[i], false);
        if (pszForwarder)
            SUPHNTIMP_ERROR(false, 31, "supR3HardenedWinGetVeryEarlyImports", kSupInitOp_Misc, VERR_MODULE_NOT_FOUND,
                            "ntdll: Failed to resolve forwarder '%s'.", pszForwarder);
    }

    /*
     * Restore the NtDll entry.
     */
    g_aSupNtImpDlls[0] = SavedDllEntry;
}


/**
 * Resolves NtDll functions we can trust calling before process init.
 *
 * @param   uNtDllAddr          The address of the NTDLL.
 */
DECLHIDDEN(void) supR3HardenedWinInitImportsEarly(uintptr_t uNtDllAddr)
{
    /*
     * NTDLL is the first entry in the list.
     */
    g_aSupNtImpDlls[0].pbImageBase = (uint8_t const *)uNtDllAddr;
    supR3HardenedParseModule(&g_aSupNtImpDlls[0]);
    for (uint32_t i = 0; i < g_aSupNtImpDlls[0].cImports; i++)
        if (!g_aSupNtImpDlls[0].paImports[i].pfnEarlyDummy)
        {
            const char *pszForwarder = supR3HardenedResolveImport(&g_aSupNtImpDlls[0], &g_aSupNtImpDlls[0].paImports[i], false);
            if (pszForwarder)
                SUPHNTIMP_ERROR(false, 32, "supR3HardenedWinInitImports", kSupInitOp_Misc, VERR_MODULE_NOT_FOUND,
                                "ntdll: Failed to resolve forwarder '%s'.", pszForwarder);
        }
        else
            *g_aSupNtImpDlls[0].paImports[i].ppfnImport = g_aSupNtImpDlls[0].paImports[i].pfnEarlyDummy;

    /*
     * Point the other imports at the early init stubs.
     */
    for (uint32_t iDll = 1; iDll < RT_ELEMENTS(g_aSupNtImpDlls); iDll++)
        for (uint32_t i = 0; i < g_aSupNtImpDlls[iDll].cImports; i++)
            if (!g_aSupNtImpDlls[iDll].paImports[i].fOptional)
                *g_aSupNtImpDlls[iDll].paImports[i].ppfnImport = g_aSupNtImpDlls[iDll].paImports[i].pfnEarlyDummy;
            else
                *g_aSupNtImpDlls[iDll].paImports[i].ppfnImport = NULL;
}


/**
 * Resolves imported functions, esp. system calls from NTDLL.
 *
 * This crap is necessary because there are sandboxing products out there that
 * will mess with system calls we make, just like any other wannabe userland
 * rootkit.  Kudos to microsoft for not providing a generic system call hook API
 * in the kernel mode, which I guess is what forcing these kind of products to
 * do ugly userland hacks that doesn't really hold water.
 */
DECLHIDDEN(void) supR3HardenedWinInitImports(void)
{
    RTERRINFOSTATIC ErrInfo;

    /*
     * Find the DLLs we will be needing first (forwarders).
     */
    for (uint32_t iDll = 0; iDll < RT_ELEMENTS(g_aSupNtImpDlls); iDll++)
    {
        supR3HardenedFindOrLoadModule(&g_aSupNtImpDlls[iDll]);
        if (g_aSupNtImpDlls[iDll].pbImageBase)
            supR3HardenedParseModule(&g_aSupNtImpDlls[iDll]);
    }

    /*
     * Resolve the functions.
     */
    for (uint32_t iDll = 0; iDll < RT_ELEMENTS(g_aSupNtImpDlls); iDll++)
        for (uint32_t i = 0; i < g_aSupNtImpDlls[iDll].cImports; i++)
        {
            const char *pszForwarder = supR3HardenedResolveImport(&g_aSupNtImpDlls[iDll], &g_aSupNtImpDlls[iDll].paImports[i],
                                                                  false);
            if (pszForwarder)
            {
                const char *pszDot = strchr(pszForwarder, '.');
                size_t  cchDllName = pszDot - pszForwarder;
                SUPHNTIMPFUNC  Tmp = g_aSupNtImpDlls[iDll].paImports[i];
                Tmp.pszName = pszDot + 1;
                if (cchDllName == sizeof("ntdll") - 1 && RTStrNICmp(pszForwarder, RT_STR_TUPLE("ntdll")) == 0)
                    supR3HardenedResolveImport(&g_aSupNtImpDlls[0], &Tmp, false);
                else if (cchDllName == sizeof("kernelbase") - 1 && RTStrNICmp(pszForwarder, RT_STR_TUPLE("kernelbase")) == 0)
                    supR3HardenedResolveImport(&g_aSupNtImpDlls[1], &Tmp, false);
                else
                    SUPHNTIMP_ERROR(false, 18, "supR3HardenedWinInitImports", kSupInitOp_Misc, VERR_MODULE_NOT_FOUND,
                                    "%ls: Failed to resolve forwarder '%s'.", g_aSupNtImpDlls[iDll].pwszName, pszForwarder);
            }
        }

    /*
     * Do system calls directly.
     */
    supR3HardenedWinInitSyscalls(false, RTErrInfoInitStatic(&ErrInfo));

    /*
     * Use the on disk image to avoid export table patching.  Currently
     * ignoring errors here as can live normally without this step.
     */
    for (uint32_t iDll = 0; iDll < RT_ELEMENTS(g_aSupNtImpDlls); iDll++)
        if (g_aSupNtImpDlls[iDll].cPatchedExports > 0)
        {
            PSUPHNTLDRCACHEENTRY pLdrEntry;
            int rc = supHardNtLdrCacheOpen(g_aSupNtImpDlls[iDll].pszName, &pLdrEntry, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc))
            {
                uint8_t *pbBits;
                rc = supHardNtLdrCacheEntryGetBits(pLdrEntry, &pbBits, (uintptr_t)g_aSupNtImpDlls[iDll].pbImageBase, NULL, NULL,
                                                   RTErrInfoInitStatic(&ErrInfo));
                if (RT_SUCCESS(rc))
                    for (uint32_t i = 0; i < g_aSupNtImpDlls[iDll].cImports; i++)
                    {
                        RTLDRADDR uValue;
                        rc = RTLdrGetSymbolEx(pLdrEntry->hLdrMod, pbBits, (uintptr_t)g_aSupNtImpDlls[iDll].pbImageBase,
                                              UINT32_MAX, g_aSupNtImpDlls[iDll].paImports[i].pszName, &uValue);
                        if (RT_SUCCESS(rc))
                            *g_aSupNtImpDlls[iDll].paImports[i].ppfnImport = (PFNRT)(uintptr_t)uValue;
                    }
            }
        }


#if 0 /* Win7/32 ntdll!LdrpDebugFlags. */
    *(uint8_t *)&g_aSupNtImpDlls[0].pbImageBase[0xdd770] = 0x3;
#endif
}


/**
 * Gets the address of a procedure in a DLL, ignoring our own syscall
 * implementations.
 *
 * Currently restricted to NTDLL and KERNEL32
 *
 * @returns The procedure address.
 * @param   pszDll          The DLL name.
 * @param   pszProcedure    The procedure name.
 */
DECLHIDDEN(PFNRT) supR3HardenedWinGetRealDllSymbol(const char *pszDll, const char *pszProcedure)
{
    RTERRINFOSTATIC ErrInfo;

    /*
     * Look the DLL up in the import DLL table.
     */
    for (uint32_t iDll = 0; iDll < RT_ELEMENTS(g_aSupNtImpDlls); iDll++)
        if (RTStrICmp(g_aSupNtImpDlls[iDll].pszName, pszDll) == 0)
        {

            PSUPHNTLDRCACHEENTRY pLdrEntry;
            int rc = supHardNtLdrCacheOpen(g_aSupNtImpDlls[iDll].pszName, &pLdrEntry, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(rc))
            {
                uint8_t *pbBits;
                rc = supHardNtLdrCacheEntryGetBits(pLdrEntry, &pbBits, (uintptr_t)g_aSupNtImpDlls[iDll].pbImageBase, NULL, NULL,
                                                   RTErrInfoInitStatic(&ErrInfo));
                if (RT_SUCCESS(rc))
                {
                    RTLDRADDR uValue;
                    rc = RTLdrGetSymbolEx(pLdrEntry->hLdrMod, pbBits, (uintptr_t)g_aSupNtImpDlls[iDll].pbImageBase,
                                          UINT32_MAX, pszProcedure, &uValue);
                    if (RT_SUCCESS(rc))
                        return (PFNRT)(uintptr_t)uValue;
                    SUP_DPRINTF(("supR3HardenedWinGetRealDllSymbol: Error getting %s in %s -> %Rrc\n", pszProcedure, pszDll, rc));
                }
                else
                    SUP_DPRINTF(("supR3HardenedWinGetRealDllSymbol: supHardNtLdrCacheEntryAllocBits failed on %s: %Rrc %s\n",
                                 pszDll, rc, ErrInfo.Core.pszMsg));
            }
            else
                SUP_DPRINTF(("supR3HardenedWinGetRealDllSymbol: supHardNtLdrCacheOpen failed on %s: %Rrc %s\n",
                             pszDll, rc, ErrInfo.Core.pszMsg));

            /* Complications, just call GetProcAddress. */
            if (g_enmSupR3HardenedMainState >= SUPR3HARDENEDMAINSTATE_WIN_IMPORTS_RESOLVED)
                return (PFNRT)GetProcAddress(GetModuleHandleW(g_aSupNtImpDlls[iDll].pwszName), pszProcedure);
            return NULL;
        }

    supR3HardenedFatal("supR3HardenedWinGetRealDllSymbol: Unknown DLL %s (proc: %s)\n", pszDll, pszProcedure);
    /* not reached */
}

