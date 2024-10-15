/* $Id: dbgstackdumpself.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Dump current thread stack to buffer.
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
#include "internal/iprt.h"
#include <iprt/dbg.h>

#include <iprt/errcore.h>
#include <iprt/string.h>
#include <iprt/ldr.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>

#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/x86.h>
#else
# error "PORTME"
#endif

#ifdef RT_OS_WINDOWS
# include <iprt/param.h>
# include <iprt/utf16.h>
# include <iprt/win/windows.h>
#elif defined(RT_OS_LINUX) || defined(RT_OS_DARWIN)
# include <dlfcn.h>
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Cached module.
 */
typedef struct RTDBGSTACKSELFMOD
{
    /** List entry. */
    RTLISTNODE  ListEntry;
    /** The mapping address. */
    uintptr_t   uMapping;
    /** The size of the mapping. */
    size_t      cbMapping;
    /** The loader module handle. */
    RTLDRMOD    hLdrMod;
    /** The debug module handle, if available. */
    RTDBGMOD    hDbgMod;
    /** Offset into szFilename of the name part. */
    size_t      offName;
    /** the module filename. */
    char        szFilename[RTPATH_MAX];
} RTDBGSTACKSELFMOD;
/** Pointer to a cached module. */
typedef RTDBGSTACKSELFMOD *PRTDBGSTACKSELFMOD;


/**
 * Symbol search state.
 */
typedef struct RTDBGSTACKSELFSYMSEARCH
{
    /** The address (not RVA) we're searching for a symbol for. */
    uintptr_t    uSearch;
    /** The distance of the current hit.  This is ~(uintptr_t)0 if no hit. */
    uintptr_t    offBestDist;
    /** Where to return symbol information. */
    PRTDBGSYMBOL pSymInfo;
} RTDBGSTACKSELFSYMSEARCH;
typedef RTDBGSTACKSELFSYMSEARCH *PRTDBGSTACKSELFSYMSEARCH;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
/* Wanted to use DECLHIDDEN(DECLASM(size_t)) here, but early solaris 11 doesn't like it. */
RT_C_DECLS_BEGIN
DECL_HIDDEN_CALLBACK(size_t) rtDbgStackDumpSelfWorker(char *pszStack, size_t cbStack, uint32_t fFlags, PCRTCCUINTREG pauRegs);
RT_C_DECLS_END


/**
 * Worker for stack and module reader callback.
 *
 * @returns IPRT status code
 * @param   pvDst               Where to put the request memory.
 * @param   cbToRead            How much to read.
 * @param   uSrcAddr            Where to read the memory from.
 */
static int rtDbgStackDumpSelfSafeMemoryReader(void *pvDst, size_t cbToRead, uintptr_t uSrcAddr)
{
# ifdef RT_OS_WINDOWS
#  if 1
    __try
    {
        memcpy(pvDst, (void const *)uSrcAddr, cbToRead);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return VERR_ACCESS_DENIED;
    }
#  else
    SIZE_T cbActual = 0;
    if (ReadProcessMemory(GetCurrentProcess(), (void const *)uSrcAddr, pvDst, cbToRead, &cbActual))
    {
        if (cbActual >= cbToRead)
            return VINF_SUCCESS;
        memset((uint8_t *)pvDst + cbActual, 0, cbToRead - cbActual);
        return VINF_SUCCESS;
    }
#  endif
# else
    /** @todo secure this from SIGSEGV.  */
    memcpy(pvDst, (void const *)uSrcAddr, cbToRead);
# endif
    return VINF_SUCCESS;
}


#if defined(RT_OS_WINDOWS) && 0
/**
 * @callback_method_impl{FNRTLDRRDRMEMREAD}
 */
static DECLCALLBACK(int) rtDbgStackDumpSelfModReader(void *pvBuf, size_t cb, size_t off, void *pvUser)
{
    PRTDBGSTACKSELFMOD pMod = (PRTDBGSTACKSELFMOD)pvUser;
    return rtDbgStackDumpSelfSafeMemoryReader(pvBuf, cb, pMod->uMapping + off);
}
#endif


/**
 * @interface_method_impl{RTDBGUNWINDSTATE,pfnReadStack}
 */
static DECLCALLBACK(int) rtDbgStackDumpSelfReader(PRTDBGUNWINDSTATE pThis, RTUINTPTR uSp, size_t cbToRead, void *pvDst)
{
    RT_NOREF(pThis);
    return rtDbgStackDumpSelfSafeMemoryReader(pvDst, cbToRead, uSp);
}


#ifdef RT_OS_WINDOWS
/**
 * Figure the size of a loaded PE image.
 * @returns The size.
 * @param   uBase       The image base address.
 */
static size_t rtDbgStackDumpSelfGetPeImageSize(uintptr_t uBase)
{
    union
    {
        IMAGE_DOS_HEADER DosHdr;
        IMAGE_NT_HEADERS NtHdrs;
    } uBuf;
    int rc = rtDbgStackDumpSelfSafeMemoryReader(&uBuf, sizeof(uBuf.DosHdr), uBase);
    if (RT_SUCCESS(rc))
    {
        if (   uBuf.DosHdr.e_magic == IMAGE_DOS_SIGNATURE
            && uBuf.DosHdr.e_lfanew < _2M)
        {
            rc = rtDbgStackDumpSelfSafeMemoryReader(&uBuf, sizeof(uBuf.NtHdrs), uBase + uBuf.DosHdr.e_lfanew);
            if (RT_SUCCESS(rc))
            {
                if (uBuf.NtHdrs.Signature == IMAGE_NT_SIGNATURE)
                    return uBuf.NtHdrs.OptionalHeader.SizeOfImage;
            }
        }
    }
    return _64K;
}
#endif


/**
 * Works the module cache.
 *
 * @returns Pointer to module cache entry on success, NULL otherwise.
 * @param   uPc             The PC to locate a module for.
 * @param   pCachedModules  The module cache.
 */
static PRTDBGSTACKSELFMOD rtDbgStackDumpSelfQueryModForPC(uintptr_t uPc, PRTLISTANCHOR pCachedModules)
{
    /*
     * Search the list first.
     */
    PRTDBGSTACKSELFMOD pMod;
    RTListForEach(pCachedModules, pMod, RTDBGSTACKSELFMOD, ListEntry)
    {
        if (uPc - pMod->uMapping < pMod->cbMapping)
            return pMod;
    }

    /*
     * Try figure out the module using the native loader or similar.
     */
#ifdef RT_OS_WINDOWS
    HMODULE hmod = NULL;
    static bool volatile                 s_fResolvedSymbols = false;
    static decltype(GetModuleHandleExW) *g_pfnGetModuleHandleExW = NULL;
    decltype(GetModuleHandleExW) *pfnGetModuleHandleExW;
    if (s_fResolvedSymbols)
        pfnGetModuleHandleExW = g_pfnGetModuleHandleExW;
    else
    {
        pfnGetModuleHandleExW = (decltype(GetModuleHandleExW) *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                               "GetModuleHandleExW");
        g_pfnGetModuleHandleExW = pfnGetModuleHandleExW;
        s_fResolvedSymbols = true;
    }
    if (   pfnGetModuleHandleExW
        && pfnGetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 (LPCWSTR)uPc, &hmod))
    {
        WCHAR wszFilename[RTPATH_MAX];
        DWORD cwcFilename = GetModuleFileNameW(hmod, wszFilename, RT_ELEMENTS(wszFilename));
        if (cwcFilename > 0)
        {
            pMod = (PRTDBGSTACKSELFMOD)RTMemAllocZ(sizeof(*pMod));
            if (pMod)
            {
                char *pszDst = pMod->szFilename;
                int rc = RTUtf16ToUtf8Ex(wszFilename, cwcFilename, &pszDst, sizeof(pMod->szFilename), NULL);
                if (RT_SUCCESS(rc))
                {
                    const char *pszFilename = RTPathFilename(pMod->szFilename);
                    pMod->offName   = pszFilename ? pszFilename - &pMod->szFilename[0] : 0;
                    pMod->uMapping  = (uintptr_t)hmod & ~(uintptr_t)(PAGE_SIZE - 1);
                    pMod->cbMapping = rtDbgStackDumpSelfGetPeImageSize(pMod->uMapping);
                    pMod->hLdrMod   = NIL_RTLDRMOD;
                    pMod->hDbgMod   = NIL_RTDBGMOD;

# if 0 /* this ain't reliable, trouble enumerate symbols in VBoxRT. But why bother when we can load it off the disk. */
                    rc = RTLdrOpenInMemory(&pMod->szFilename[pMod->offName], RTLDR_O_FOR_DEBUG, RTLdrGetHostArch(),
                                           pMod->cbMapping, rtDbgStackDumpSelfModReader, NULL /*pfnDtor*/, pMod /*pvUser*/,
                                           &pMod->hLdrMod, NULL /*pErrInfo*/);
# else
                    rc = RTLdrOpen(pMod->szFilename, RTLDR_O_FOR_DEBUG, RTLdrGetHostArch(), &pMod->hLdrMod);
# endif
                    if (RT_SUCCESS(rc))
                    {
                        pMod->cbMapping = RTLdrSize(pMod->hLdrMod);

                        /* Try open debug info for the module. */
                        uint32_t uTimeDateStamp = 0;
                        RTLdrQueryProp(pMod->hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uTimeDateStamp, sizeof(uTimeDateStamp));
                        rc = RTDbgModCreateFromPeImage(&pMod->hDbgMod, pMod->szFilename, &pMod->szFilename[pMod->offName],
                                                       &pMod->hLdrMod, (uint32_t)pMod->cbMapping, uTimeDateStamp, NIL_RTDBGCFG);
                        RTListPrepend(pCachedModules, &pMod->ListEntry);
                        return pMod;
                    }
                }
                RTMemFree(pMod);
            }
        }
    }
#elif defined(RT_OS_LINUX) || defined(RT_OS_DARWIN)
    Dl_info Info = { NULL, NULL, NULL, NULL };
    int rc = dladdr((const void *)uPc, &Info);
    if (rc != 0 && Info.dli_fname)
    {
        pMod = (PRTDBGSTACKSELFMOD)RTMemAllocZ(sizeof(*pMod));
        if (pMod)
        {
            /** @todo better filename translation... */
            rc = RTStrCopy(pMod->szFilename, sizeof(pMod->szFilename), Info.dli_fname);
            if (RT_SUCCESS(rc))
            {
                RTStrPurgeEncoding(pMod->szFilename);

                const char *pszFilename = RTPathFilename(pMod->szFilename);
                pMod->offName   = pszFilename ? pszFilename - &pMod->szFilename[0] : 0;
                pMod->uMapping  = (uintptr_t)Info.dli_fbase;
                pMod->cbMapping = 0;
                pMod->hLdrMod   = NIL_RTLDRMOD;
                pMod->hDbgMod   = NIL_RTDBGMOD;

                rc = RTLdrOpen(pMod->szFilename, RTLDR_O_FOR_DEBUG, RTLdrGetHostArch(), &pMod->hLdrMod);
                if (RT_SUCCESS(rc))
                {
                    pMod->cbMapping = RTLdrSize(pMod->hLdrMod);

                    /* Try open debug info for the module. */
                    //uint32_t uTimeDateStamp = 0;
                    //RTLdrQueryProp(pMod->hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uTimeDateStamp, sizeof(uTimeDateStamp));
                    //RTDbgModCreateFromImage()??
                    //rc = RTDbgModCreateFromPeImage(&pMod->hDbgMod, pMod->szFilename, &pMod->szFilename[pMod->offName],
                    //                               &pMod->hLdrMod, (uint32_t)pMod->cbMapping, uTimeDateStamp, NIL_RTDBGCFG);

                    RTListPrepend(pCachedModules, &pMod->ListEntry);
                    return pMod;
                }
            }
            RTMemFree(pMod);
        }
    }
#endif
    return NULL;
}


/**
 * @callback_method_impl{FNRTLDRENUMSYMS}
 */
static DECLCALLBACK(int) rtDbgStackdumpSelfSymbolSearchCallback(RTLDRMOD hLdrMod, const char *pszSymbol,
                                                                unsigned uSymbol, RTLDRADDR Value, void *pvUser)
{
    PRTDBGSTACKSELFSYMSEARCH pSearch = (PRTDBGSTACKSELFSYMSEARCH)pvUser;
    if (Value >= pSearch->uSearch)
    {
        uintptr_t const offDist = (uintptr_t)Value - pSearch->uSearch;
        if (offDist < pSearch->offBestDist)
        {
            pSearch->offBestDist = offDist;

            PRTDBGSYMBOL pSymInfo = pSearch->pSymInfo;
            pSymInfo->Value    = Value;
            pSymInfo->offSeg   = Value;
            pSymInfo->iSeg     = RTDBGSEGIDX_ABS;
            pSymInfo->iOrdinal = uSymbol;
            pSymInfo->fFlags   = 0;
            if (pszSymbol)
                RTStrCopy(pSymInfo->szName, sizeof(pSymInfo->szName), pszSymbol);
            else
                RTStrPrintf(pSymInfo->szName, sizeof(pSymInfo->szName), "Ordinal#%u", uSymbol);

            if (offDist < 8)
                return VINF_CALLBACK_RETURN;
        }
    }
    RT_NOREF(hLdrMod);
    return VINF_SUCCESS;
}


/**
 * Searches for a symbol close to @a uRva.
 *
 * @returns true if found, false if not.
 * @param   pMod        The module cache entry to search in.
 * @param   uRva        The RVA to locate a symbol for.
 * @param   poffDisp    Where to return the distance between uRva and the returned symbol.
 * @param   pSymInfo    Where to return the symbol information.
 */
static bool rtDbgStackDumpSelfQuerySymbol(PRTDBGSTACKSELFMOD pMod, uintptr_t uRva, PRTINTPTR poffDisp, PRTDBGSYMBOL pSymInfo)
{
    if (pMod->hDbgMod != NIL_RTDBGMOD)
    {
        int rc = RTDbgModSymbolByAddr(pMod->hDbgMod, RTDBGSEGIDX_RVA, uRva, RTDBGSYMADDR_FLAGS_LESS_OR_EQUAL,
                                      poffDisp, pSymInfo);
        if (RT_SUCCESS(rc))
            return true;
    }

    if (pMod->hLdrMod != NIL_RTLDRMOD)
    {
        RTDBGSTACKSELFSYMSEARCH SearchInfo = { pMod->uMapping + uRva, ~(uintptr_t)0, pSymInfo };
        int rc = RTLdrEnumSymbols(pMod->hLdrMod, 0, (const void *)pMod->uMapping, pMod->uMapping,
                                  rtDbgStackdumpSelfSymbolSearchCallback, &SearchInfo);
        if (RT_SUCCESS(rc) && SearchInfo.offBestDist != ~(uintptr_t)0)
        {
            *poffDisp = SearchInfo.offBestDist;
            return true;
        }
    }

    return false;
}


/**
 * Does the grunt work for RTDbgStackDumpSelf.
 *
 * Called thru an assembly wrapper that collects the necessary register state.
 *
 * @returns Length of the string returned in pszStack.
 * @param   pszStack    Where to output the stack dump.
 * @param   cbStack     The size of the @a pszStack buffer.
 * @param   fFlags      Flags, MBZ.
 * @param   pauRegs     Register state.  For AMD64 and x86 this starts with the
 *                      PC and us followed by the general purpose registers.
 */
DECL_HIDDEN_CALLBACK(size_t) rtDbgStackDumpSelfWorker(char *pszStack, size_t cbStack, uint32_t fFlags, PCRTCCUINTREG pauRegs)
{
    RT_NOREF(fFlags);

    /*
     * Initialize the unwind state.
     */
    RTDBGUNWINDSTATE UnwindState;
    RT_ZERO(UnwindState);

    UnwindState.u32Magic        = RTDBGUNWINDSTATE_MAGIC;
    UnwindState.pfnReadStack    = rtDbgStackDumpSelfReader;
#ifdef RT_ARCH_AMD64
    UnwindState.enmArch         = RTLDRARCH_AMD64;
    UnwindState.uPc             = pauRegs[0];
    UnwindState.enmRetType      = RTDBGRETURNTYPE_NEAR64;
    for (unsigned i = 0; i < 16; i++)
        UnwindState.u.x86.auRegs[i] = pauRegs[i + 1];
#elif defined(RT_ARCH_X86)
    UnwindState.enmArch         = RTLDRARCH_X86_32;
    UnwindState.uPc             = pauRegs[0];
    UnwindState.enmRetType      = RTDBGRETURNTYPE_NEAR32;
    for (unsigned i = 0; i < 8; i++)
        UnwindState.u.x86.auRegs[i] = pauRegs[i + 1];
#else
# error "PORTME"
#endif

    /*
     * We cache modules.
     */
    PRTDBGSTACKSELFMOD  pCurModule = NULL;
    RTLISTANCHOR        CachedModules;
    RTListInit(&CachedModules);

    /*
     * Work the stack.
     */
    size_t offDst = 0;
    while (offDst + 64 < cbStack)
    {
        /* Try locate the module containing the current PC. */
        if (   !pCurModule
            || UnwindState.uPc - pCurModule->uMapping >= pCurModule->cbMapping)
            pCurModule = rtDbgStackDumpSelfQueryModForPC(UnwindState.uPc, &CachedModules);
        bool fManualUnwind = true;
        if (!pCurModule)
            offDst += RTStrPrintf(&pszStack[offDst], cbStack - offDst, "%p\n", UnwindState.uPc);
        else
        {
            uintptr_t const uRva = UnwindState.uPc - pCurModule->uMapping;

            /*
             * Add a call stack entry with the symbol if we can.
             */
            union
            {
                RTDBGSYMBOL SymbolInfo;
                RTDBGLINE   LineInfo;
            } uBuf;
            RTINTPTR offDisp = 0;
            if (!rtDbgStackDumpSelfQuerySymbol(pCurModule, uRva, &offDisp, &uBuf.SymbolInfo))
                offDst += RTStrPrintf(&pszStack[offDst], cbStack - offDst, "%p %s + %#zx\n",
                                      UnwindState.uPc, &pCurModule->szFilename[pCurModule->offName], (size_t)uRva);
            else if (offDisp == 0)
                offDst += RTStrPrintf(&pszStack[offDst], cbStack - offDst, "%p %s!%s (rva:%#zx)\n", UnwindState.uPc,
                                      &pCurModule->szFilename[pCurModule->offName], uBuf.SymbolInfo.szName, (size_t)uRva);
            else
                offDst += RTStrPrintf(&pszStack[offDst], cbStack - offDst, "%p %s!%s%c%#zx (rva:%#zx)\n",
                                      UnwindState.uPc, &pCurModule->szFilename[pCurModule->offName], uBuf.SymbolInfo.szName,
                                      offDisp >= 0 ? '+' : '-',  (size_t)RT_ABS(offDisp), (size_t)uRva);

            /*
             * Try supply the line number.
             */
            if (pCurModule->hDbgMod != NIL_RTDBGMOD)
            {
                offDisp = 0;
                int rc = RTDbgModLineByAddr(pCurModule->hDbgMod, RTDBGSEGIDX_RVA, uRva, &offDisp, &uBuf.LineInfo);
                if (RT_SUCCESS(rc) && offDisp)
                    offDst += RTStrPrintf(&pszStack[offDst], cbStack - offDst, "  [%s:%u]\n",
                                          uBuf.LineInfo.szFilename, uBuf.LineInfo.uLineNo);
                else if (RT_SUCCESS(rc))
                    offDst += RTStrPrintf(&pszStack[offDst], cbStack - offDst, "  [%s:%u (%c%#zx)]\n", uBuf.LineInfo.szFilename,
                                          uBuf.LineInfo.uLineNo, offDisp >= 0 ? '+' : '-',  (size_t)RT_ABS(offDisp));
            }

            /*
             * Try unwind using the module info.
             */
            int rc;
            if (pCurModule->hDbgMod != NIL_RTDBGMOD)
                rc = RTDbgModUnwindFrame(pCurModule->hDbgMod, RTDBGSEGIDX_RVA, uRva, &UnwindState);
            else
                rc = RTLdrUnwindFrame(pCurModule->hLdrMod, (void const *)pCurModule->uMapping, UINT32_MAX, uRva, &UnwindState);
            if (RT_SUCCESS(rc))
                fManualUnwind = false;
        }
        if (fManualUnwind)
        {
            break;
        }
    }

    /*
     * Destroy the cache.
     */
    PRTDBGSTACKSELFMOD pNextModule;
    RTListForEachSafe(&CachedModules, pCurModule, pNextModule, RTDBGSTACKSELFMOD, ListEntry)
    {
        if (pCurModule->hDbgMod != NIL_RTDBGMOD)
        {
            RTDbgModRelease(pCurModule->hDbgMod);
            pCurModule->hDbgMod = NIL_RTDBGMOD;
        }
        if (pCurModule->hLdrMod != NIL_RTLDRMOD)
        {
            RTLdrClose(pCurModule->hLdrMod);
            pCurModule->hLdrMod = NIL_RTLDRMOD;
        }
        RTMemFree(pCurModule);
    }

    return offDst;
}

