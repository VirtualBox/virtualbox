/* $Id: SUPHardenedVerifyProcess-win.cpp 115209 2026-09-09 11:55:35Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox Support Library/Driver - Hardened Process Verification, Windows.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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
#ifdef IN_RING0
# define LOG_GROUP LOG_GROUP_SUP_DRV
# ifndef IPRT_NT_MAP_TO_ZW
#  define IPRT_NT_MAP_TO_ZW
# endif
# include <iprt/nt/nt.h>
# include <ntimage.h>
#else
# include <iprt/nt/nt-and-windows.h>
#endif

#include <VBox/sup.h>
#include <VBox/err.h>
#include <VBox/dis.h>
#include <iprt/alloca.h>
#include <iprt/ctype.h>
#include <iprt/param.h>
#include <iprt/sha.h>
#include <iprt/stackcheck.h>
#include <iprt/string.h>
#include <iprt/utf16.h>
#include <iprt/zero.h>

#ifdef IN_RING0
# include "SUPDrvInternal.h"
#else
# include "SUPLibInternal.h"
#endif
#include "win/SUPHardenedVerify-win.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_ARCH_ARM64)
AssertCompile(PAGE_SIZE == _4K);
# define g_abRTZeroPage     g_abRTZero4K
#endif

#ifdef VBOX_WITH_MINIMAL_HARDENING
# define IMAGE_LOG_NAME_FMT         "%ls"
# define IMAGE_LOG_NAME(pImage)     ((pImage)->Name.awcBuffer)
#else
# define IMAGE_LOG_NAME_FMT         "%s"
# define IMAGE_LOG_NAME(pImage)     ((pImage)->pszName)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Known image IDs. */
typedef enum SUPHNTVPIMAGEID
{
    kSupHNtVpImageId_Invalid = 0,
    kSupHNtVpImageId_NtDll,
    kSupHNtVpImageId_Kernel32,
    kSupHNtVpImageId_KernelBase,
    kSupHNtVpImageId_AppHelp,
    kSupHNtVpImageId_AppSetSchema,
    kSupHNtVpImageId_Executable,
    kSupHNtVpImageId_Other
} SUPHNTVPIMAGEID;

/**
 * Virtual address space region.
 */
typedef struct SUPHNTVPREGION
{
    /** The RVA of the region. */
    uint32_t    uRva;
    /** The size of the region. */
    uint32_t    cb;
    /** The protection of the region. */
    uint32_t    fProt;
} SUPHNTVPREGION;
/** Pointer to a virtual address space region. */
typedef SUPHNTVPREGION *PSUPHNTVPREGION;

/**
 * Virtual address space image information.
 */
typedef struct SUPHNTVPIMAGE
{
    /** The base address of the image. */
    uintptr_t       uImageBase;
    /** The size of the image mapping. */
    uintptr_t       cbImage;

#ifndef VBOX_WITH_MINIMAL_HARDENING
    /** The name from the allowed lists. */
    const char     *pszName;
    /** Image ID for easy identification. */
    SUPHNTVPIMAGEID enmImageId;
#endif
    /** Name structure for NtQueryVirtualMemory/MemorySectionName. */
    struct
    {
        /** The full unicode name. */
        UNICODE_STRING  UniStr;
        /** Buffer space. */
        WCHAR           awcBuffer[260];
    } Name;

    /** The number of mapping regions. */
    uint32_t        cRegions;
    /** Mapping regions. */
#ifdef VBOX_WITH_MINIMAL_HARDENING /* 2024-11-21: Qt6Gui.dll has 19 regions, .data is split up quite a bit. */
    SUPHNTVPREGION  aRegions[40];
#else
    SUPHNTVPREGION  aRegions[16];
#endif

    /** The image characteristics from the FileHeader. */
    uint16_t        fImageCharecteristics;
    /** The DLL characteristics from the OptionalHeader. */
    uint16_t        fDllCharecteristics;

    /** Set if this is the DLL. */
    bool            fDll;
    /** Set if the image is NTDLL and the verficiation code needs to watch out for
     *  the NtCreateSection patch. */
    bool            fNtCreateSectionPatch;
    /** Whether the API set schema hack needs to be applied when verifying memory
     * content.  The hack means that we only check if the 1st section is mapped. */
    bool            fApiSetSchemaOnlySection1;
    /** This may be a 32-bit resource DLL. */
    bool            f32bitResourceDll;

    /** Pointer to the loader cache entry for the image. */
    PSUPHNTLDRCACHEENTRY    pCacheEntry;
#ifdef IN_RING0
    /** In ring-0 we don't currently cache images, so put it here. */
    SUPHNTLDRCACHEENTRY     CacheEntry;
#endif
} SUPHNTVPIMAGE;
/** Pointer to image info from the virtual address space scan. */
typedef SUPHNTVPIMAGE *PSUPHNTVPIMAGE;

/**
 * Virtual address space scanning state.
 */
typedef struct SUPHNTVPSTATE
{
    /** Type of verification to perform. */
    SUPHARDNTVPKIND         enmKind;
    /** Combination of SUPHARDNTVP_F_XXX. */
    uint32_t                fFlags;
    /** The result. */
    int                     rcResult;
    /** Number of fixes we've done.
     * Only applicable in the purification modes.  */
    uint32_t                cFixes;
    /** Number of images in aImages. */
    uint32_t                cImages;
    /** The index of the last image we looked up. */
    uint32_t                iImageHint;
    /** The process handle. */
    HANDLE                  hProcess;
    /** Images found in the process.
     * The array is large enough to hold the executable, all allowed DLLs, and one
     * more so we can get the image name of the first unwanted DLL. */
    SUPHNTVPIMAGE           aImages[1 + 6 + 1
#ifdef VBOX_PERMIT_VERIFIER_DLL
                                    + 1
#endif
#ifdef VBOX_PERMIT_MORE
                                    + 5
#endif
#ifdef VBOX_PERMIT_VISUAL_STUDIO_PROFILING
                                    + 16
#endif
                                   ];
    /** Memory compare scratch buffer.*/
    uint8_t                 abMemory[_4K];
    /** File compare scratch buffer.*/
    uint8_t                 abFile[_4K];
    /** Section headers for use when comparing file and loaded image.
     * @note NTDLL.DLL from W11/26200 has 15...  */
    IMAGE_SECTION_HEADER    aSecHdrs[24];
    /** Pointer to the error info. */
    PRTERRINFO              pErrInfo;
    /** Pointer to RWX page verification information (can be NULL). */
    PSUPHARDNTVPRWXPGINFO   pRwxPgInfo;
} SUPHNTVPSTATE;
/** Pointer to stat information of a virtual address space scan. */
typedef SUPHNTVPSTATE *PSUPHNTVPSTATE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#if defined(IN_RING3) || !defined(VBOX_WITH_MINIMAL_HARDENING)
/**
 * System DLLs allowed to be loaded into the process.
 * @remarks supHardNtVpCheckDlls assumes these are lower case.
 */
static struct { const char *pszName; SUPHNTVPIMAGEID enmImageId; } const g_aSupNtVpAllowedDlls[] =
{
    { "ntdll.dll",              kSupHNtVpImageId_NtDll },
    { "kernel32.dll",           kSupHNtVpImageId_Kernel32 },
    { "kernelbase.dll",         kSupHNtVpImageId_KernelBase },
    { "apphelp.dll",            kSupHNtVpImageId_AppHelp },
    { "apisetschema.dll",       kSupHNtVpImageId_AppSetSchema },
# ifdef VBOX_PERMIT_VERIFIER_DLL
    { "verifier.dll",           kSupHNtVpImageId_Other },
# endif
# ifdef VBOX_PERMIT_MORE
#  define VBOX_PERMIT_MORE_FIRST_IDX 5
    { "sfc.dll",                kSupHNtVpImageId_Other },
    { "sfc_os.dll",             kSupHNtVpImageId_Other },
    { "user32.dll",             kSupHNtVpImageId_Other },
    { "acres.dll",              kSupHNtVpImageId_Other },
    { "acgenral.dll",           kSupHNtVpImageId_Other },
# endif
# ifdef VBOX_PERMIT_VISUAL_STUDIO_PROFILING
    { "psapi.dll",              kSupHNtVpImageId_Other },
    { "msvcrt.dll",             kSupHNtVpImageId_Other },
    { "advapi32.dll",           kSupHNtVpImageId_Other },
    { "sechost.dll",            kSupHNtVpImageId_Other },
    { "rpcrt4.dll",             kSupHNtVpImageId_Other },
    { "SamplingRuntime.dll",    kSupHNtVpImageId_Other },
# endif
};
#endif

/**
 * VBox executables allowed to start VMs.
 * @remarks Remember to keep in sync with g_aSupInstallFiles in
 *          SUPR3HardenedVerify.cpp.
 */
static const char *g_apszSupNtVpAllowedVmExes[] =
{
    "VBoxHeadless.exe",
    "VirtualBoxVM.exe",
    "VBoxSDL.exe",
    "VBoxNetDHCP.exe",
    "VBoxNetNAT.exe",
    "VBoxVMMPreload.exe",
    "VBoxCpuReport.exe",

    "tstMicro.exe",
    "tstPDMAsyncCompletion.exe",
    "tstPDMAsyncCompletionStress.exe",
    "tstVMM.exe",
    "tstVMREQ.exe",
    "tstCFGM.exe",
    "tstGIP-2.exe",
    "tstIntNet-1.exe",
    "tstMMHyperHeap.exe",
    "tstRTR0ThreadPreemptionDriver.exe",
    "tstRTR0MemUserKernelDriver.exe",
    "tstRTR0SemMutexDriver.exe",
    "tstRTR0TimerDriver.exe",
    "tstSSM.exe",
    "tstInt.exe",
#ifdef VBOX_WITH_MINIMAL_HARDENING /* (These testcases are disabled in regular hardened builds.) */
    "tstContiguous.exe",
    "tstGetPagingMode.exe",
    "tstInit.exe",
    "tstLow.exe",
    "tstPage.exe",
    "tstPin.exe",
    "tstSupLoadModule.exe",
    "tstSupSem.exe",
    "tstSupSem-Zombie.exe",
    "tstSupTscDelta.exe",
#endif
};

/** Pointer to NtQueryVirtualMemory.  Initialized by SUPDrv-win.cpp in
 *  ring-0, in ring-3 it's just a slightly confusing define. */
#ifdef IN_RING0
PFNNTQUERYVIRTUALMEMORY g_pfnNtQueryVirtualMemory = NULL;
#else
# define g_pfnNtQueryVirtualMemory NtQueryVirtualMemory
#endif

#ifdef IN_RING3
/** The number of valid entries in the loader cache. */
static uint32_t                 g_cSupNtVpLdrCacheEntries = 0;
/** The loader cache entries. */
static SUPHNTLDRCACHEENTRY      g_aSupNtVpLdrCacheEntries[RT_ELEMENTS(g_aSupNtVpAllowedDlls) + 1 + 3];
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifndef VBOX_WITH_MINIMAL_HARDENING
static PSUPHNTVPIMAGE supHardNtVpFindModule(PSUPHNTVPSTATE pThis, const char *pszModule);
#endif



/**
 * Fills in error information.
 *
 * @returns @a rc.
 * @param   pErrInfo            Pointer to the extended error info structure.
 *                              Can be NULL.
 * @param   rc                  The status to return.
 * @param   pszMsg              The format string for the message.
 * @param   ...                 The arguments for the format string.
 */
static int supHardNtVpSetInfo1(PRTERRINFO pErrInfo, int rc, const char *pszMsg, ...)
{
    va_list va;
#ifdef IN_RING3
    va_start(va, pszMsg);
    supR3HardenedError(rc, false /*fFatal*/, "%N\n", pszMsg, &va);
    va_end(va);
#endif

    va_start(va, pszMsg);
    RTErrInfoSetV(pErrInfo, rc, pszMsg, va);
    va_end(va);

    return rc;
}


/**
 * Adds error information.
 *
 * @returns @a rc.
 * @param   pErrInfo            Pointer to the extended error info structure
 *                              which may contain some details already.  Can be
 *                              NULL.
 * @param   rc                  The status to return.
 * @param   pszMsg              The format string for the message.
 * @param   ...                 The arguments for the format string.
 */
static int supHardNtVpAddInfo1(PRTERRINFO pErrInfo, int rc, const char *pszMsg, ...)
{
    va_list va;
#ifdef IN_RING3
    va_start(va, pszMsg);
    if (pErrInfo && pErrInfo->pszMsg)
        supR3HardenedError(rc, false /*fFatal*/, "%N - %s\n", pszMsg, &va, pErrInfo->pszMsg);
    else
        supR3HardenedError(rc, false /*fFatal*/, "%N\n", pszMsg, &va);
    va_end(va);
#endif

    va_start(va, pszMsg);
    RTErrInfoAddV(pErrInfo, rc, pszMsg, va);
    va_end(va);

    return rc;
}


/**
 * Fills in error information.
 *
 * @returns @a rc.
 * @param   pThis               The process validator instance.
 * @param   rc                  The status to return.
 * @param   pszMsg              The format string for the message.
 * @param   va                  The arguments for the format string.
 */
static int supHardNtVpSetInfo2V(PSUPHNTVPSTATE pThis, int rc, const char *pszMsg, va_list va)
{
#ifdef IN_RING3
    va_list va2;
    va_copy(va2, va);
    supR3HardenedError(rc, false /*fFatal*/, "%N\n", pszMsg, &va2);
    va_end(va2);
#endif

#if 0 //def IN_RING0
    RTErrInfoSetV(pThis->pErrInfo, rc, pszMsg, va);
    pThis->rcResult = rc;
#else
    if (RT_SUCCESS(pThis->rcResult))
    {
        RTErrInfoSetV(pThis->pErrInfo, rc, pszMsg, va);
        pThis->rcResult = rc;
    }
    else
    {
        RTErrInfoAddF(pThis->pErrInfo, rc, " \n[rc=%d] ", rc);
        RTErrInfoAddV(pThis->pErrInfo, rc, pszMsg, va);
    }
#endif

    return rc;
}

/**
 * Fills in error information.
 *
 * @returns @a rc.
 * @param   pThis               The process validator instance.
 * @param   rc                  The status to return.
 * @param   pszMsg              The format string for the message.
 * @param   ...                 The arguments for the format string.
 */
static int supHardNtVpSetInfo2(PSUPHNTVPSTATE pThis, int rc, const char *pszMsg, ...)
{
    va_list va;
    va_start(va, pszMsg);
    rc = supHardNtVpSetInfo2V(pThis, rc, pszMsg, va);
    va_end(va);
    return rc;
}


static int supHardNtVpReadImage(PSUPHNTVPIMAGE pImage, uint64_t off, void *pvBuf, size_t cbRead)
{
    return pImage->pCacheEntry->pNtViRdr->Core.pfnRead(&pImage->pCacheEntry->pNtViRdr->Core, pvBuf, cbRead, off);
}


static NTSTATUS supHardNtVpReadMem(HANDLE hProcess, uintptr_t uPtr, void *pvBuf, size_t cbRead)
{
#ifdef IN_RING0
    /* ASSUMES hProcess is the current process. */
    RT_NOREF1(hProcess);
    /** @todo use MmCopyVirtualMemory where available! */
    int rc = RTR0MemUserCopyFrom(pvBuf, uPtr, cbRead);
    if (RT_SUCCESS(rc))
        return STATUS_SUCCESS;
    return STATUS_ACCESS_DENIED;
#else
    SIZE_T cbIgn;
    NTSTATUS rcNt = NtReadVirtualMemory(hProcess, (PVOID)uPtr, pvBuf, cbRead, &cbIgn);
    if (NT_SUCCESS(rcNt) && cbIgn != cbRead)
        rcNt = STATUS_IO_DEVICE_ERROR;
    return rcNt;
#endif
}


#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
/**
 * Checks if a disassembled instruction can move patched (moved).
 */
DECLHIDDEN(bool) supHardNtRwxPageIsPrologInstrPatchable(PDISSTATE pDis)
{
    if (pDis->pCurInstr->fOpType & DISOPTYPE_CONTROLFLOW)
        return false;
# ifdef RT_ARCH_AMD64
    if (pDis->x86.ModRM.Bits.Mod == 0 && pDis->x86.ModRM.Bits.Rm == 5 /* wrt RIP */)
        return false;
# endif
    return true;
}
#endif


/**
 * Checks one entry in the rwx page.
 *
 * @returns true if it seems okay, false if it isn't okay.
 * @param   pbEntry             Pointer to the entry.
 * @param   iEntry              The entry number.
 * @param   fOptional           Whether the entry is optional.
 * @param   pfnVerifyJumpTarget Jump target validation callback.
 * @param   pfnSetError         For reporting errors.
 * @param   pvUser              User argument for @a pfnVerifyJumpTarget.
 */
static bool supHardNtRwxPageVerifyEntry(uint8_t const *pbEntry, uint32_t iEntry, bool fOptional,
                                        PFNSUPHARDNTRWXPAGEVERIFYJUMPTARGET pfnVerifyJumpTarget,
                                        PFNSUPHARDNTRWXPAGESETERROR pfnSetError, void *pvUser)
{
    /* Check trailing 0xcc padding. */
    uint32_t cb = SUPHARDNT_RWXPG_ENTRY_SIZE;
    while (cb > 0 && pbEntry[cb - 1] == 0xcc)
        cb--;
    if (cb == 0)
        return fOptional ? true : pfnSetError(pvUser, "RWX Page Entry #%u: All 0xcc!", iEntry);

#ifdef RT_ARCH_AMD64
    /* Check max and min length. */
    if (cb < 8 /*ptr*/ + 4 /*endbr64*/ + 12 /*copy*/ + 7 /*notrack jmp */ + 0 /*int3*/)
        return pfnSetError(pvUser, "RWX Page Entry #%u: %u bytes is below minimum", iEntry, cb);
    if (cb > 8 /*ptr*/ + 4 /*endbr64*/ + 12 /*copy*/ + 14 /* long last instruction */ + 7 /*notrack jmp */ + 0 /*int3*/)
        return pfnSetError(pvUser, "RWX Page Entry #%u: %u bytes is above maximum", iEntry, cb);

    /* These all start with a pointer to somewhere inside ntdll. */
    if (!pfnVerifyJumpTarget(*(uint64_t const *)pbEntry, iEntry, pvUser))
        return pfnSetError(pvUser, "RWX Page Entry #%u: Invalid jump target %p", iEntry, *(uint64_t const *)pbEntry);

    /* Followed by 'endbr64' */
    if (*(uint32_t const *)&pbEntry[8] != RT_MAKE_U32_FROM_U8(0xf3, 0x0f, 0x1e, 0xfa))
        return pfnSetError(pvUser, "RWX Page Entry #%u: endbr64", iEntry);

    /* And ending with 'notrack jmp [pbEntry wrt RIP]' */
    if (   pbEntry[cb - 7] != 0x3e /*notrack*/
        || pbEntry[cb - 6] != 0xff /* jmp qword [pbEntry wrt RIP] */
        || pbEntry[cb - 5] != 0x25)
        return pfnSetError(pvUser, "RWX Page Entry #%u: notrack jmp (opcode)", iEntry);
    if (*(int32_t const *)&pbEntry[cb - 4] != -(int32_t)cb)
        return pfnSetError(pvUser, "RWX Page Entry #%u: notrack jmp (slot)", iEntry);

    /* Check the instructions in the body. */
    uint32_t const offBodyEnd   = cb - 7 /*notrack jmp*/;
    uint32_t const offBodyBegin = 8 /*ptr*/ + 4 /*endbr64*/;
    uint32_t       offBody      = offBodyBegin;
    while (offBody < offBodyEnd && offBody - offBodyBegin < 12)
    {
        DISSTATE Dis;
        uint32_t cbInstr = 1;
        int rc = DISInstr(&pbEntry[offBody], DISCPUMODE_64BIT, &Dis, &cbInstr);
        if (RT_FAILURE(rc))
            return pfnSetError(pvUser, "RWX Page Entry #%u: DISInstr failure at %#x in %.*Rhxs: %Rrc",
                               iEntry, offBody - offBodyBegin, cb - offBodyBegin, &pbEntry[offBodyBegin], rc);
        if (!supHardNtRwxPageIsPrologInstrPatchable(&Dis))
            return pfnSetError(pvUser, "RWX Page Entry #%u: unwanted/illegal instruction at %#x in %.*Rhxs: fOpType=%#x uOpcode=%#x",
                               iEntry, offBody - offBodyBegin, cb - offBodyBegin, &pbEntry[offBodyBegin],
                               Dis.pCurInstr->fOpType, Dis.pCurInstr->uOpcode);
        offBody += cbInstr;
    }
    if (offBody != offBodyEnd)
        return pfnSetError(pvUser, "RWX Page Entry #%u: bogus end of body: %#x, expected %#x (%.*Rhxs)",
                           iEntry, offBody - offBodyBegin, offBodyEnd - offBodyBegin, cb - offBodyBegin, &pbEntry[offBodyBegin]);

#elif defined(RT_ARCH_X86)
    /* Check max and min length. */
    if (cb < 4 /*endbr32*/ + 5 /*copy*/ + 5 /*jmp*/ + 0 /*int3*/)
        return false;
    if (cb > 4 /*endbr32*/ + 5 /*copy*/ + 14 /* long last instruction */ + 5 /*jmp*/ + 0 /*int3*/)
        return false;

    /* Check 'endbr32'. */
    if (*(uint32_t const *)pbEntry != RT_MAKE_U32_FROM_U8(0xf3, 0x0f, 0x1e, 0xfb))
        return false;

    /* Check 'jmp rel32'. */
    if (pbEntry[cb - 5] != 0xe9)
        return false;

    /* Check the jump target. */
# ifdef IN_RING3
    uintptr_t const uJmpTarget = *(int32_t const *)&pbEntry[cb - 4] + (intptr_t)&pbEntry[cb];
    if (!pfnVerifyJumpTarget(uJmpTarget, iEntry, pvUser))
        return false;
# endif

    /* Check the instructions in the body. */
    uint32_t const offBodyEnd   = cb - 5 /*jmp rel*/;
    uint32_t const offBodyBegin = 4 /*endbr32*/;
    uint32_t       offBody      = offBodyBegin;
    while (offBody < offBodyEnd && offBody - offBodyBegin < 5)
    {
        DISSTATE Dis;
        uint32_t cbInstr = 1;
        int rc = DISInstr(&pbEntry[offBody], DISCPUMODE_32BIT, &Dis, &cbInstr);
        if (RT_FAILURE(rc) || !supHardNtRwxPageIsPrologInstrPatchable(&Dis))
            return false;
        offBody += cbInstr;
    }
    if (offBody != offBodyEnd)
        return false;

#elif defined(RT_ARCH_ARM64)
    /* Check the length (fixed). */
    if (cb != 16 /* org opcodes */ + 16 /*load addr*/ + 4 /*br x17*/ + 4 /*brk #0x06a1*/ )
        return false;
    uint32_t const * const pu32Entry = (uint32_t const *)pbEntry;

    /* Check EXEC_PAGE_LOAD_ADDR_IN_X17 sequence */
    uintptr_t uJmpTarget = (pu32Entry[4] >> 5) & 0xffff;
    if ((pu32Entry[4] & UINT32_C(0xffe0001f)) != UINT32_C(0xd2800011)) /* movz x17, const16 */
        return false;
    uJmpTarget |= ((pu32Entry[5] >> 5) & 0xffff) << 16;
    if ((pu32Entry[5] & UINT32_C(0xffe0001f)) != UINT32_C(0xf2a00011)) /* movk x17, const16 lsl 16 */
        return false;
    uJmpTarget |= (uintptr_t)((pu32Entry[6] >> 5) & 0xffff) << 32;
    if ((pu32Entry[6] & UINT32_C(0xffe0001f)) != UINT32_C(0xf2c00011)) /* movk x17, const16 lsl 32 */
        return false;
    uJmpTarget |= (uintptr_t)((pu32Entry[7] >> 5) & 0xffff) << 48;
    if ((pu32Entry[7] & UINT32_C(0xffe0001f)) != UINT32_C(0xf2e00011)) /* movk x17, const16 lsl 48 */
        return false;

    /* Validate the target address. */
    if (!pfnVerifyJumpTarget(uJmpTarget, iEntry, pvUser))
        return false;

    /* Check EXEC_PAGE_LOAD_ADDR_IN_X17 (br & brk).  */
    if (pu32Entry[8] != UINT32_C(0xd61f0220)) /* br x17 */
        return false;
    if (pu32Entry[8] != UINT32_C(0x20d420d4)) /* brk #0x06a1 */
        return false;

#else
    RT_NOREF(pbEntry, iEntry, pfnVerifyJumpTarget, pvUser);
#endif
    return true;
}


/**
 * Verifies the content of the rwx page.
 *
 * @returns true if it seems okay, false if it isn't.
 * @param   pbPage              The rwx page content.
 * @param   pfnVerifyJumpTarget Jump target validation callback.
 * @param   pfnSetError         For reporting errors.
 * @param   pvUser              User argument for @a pfnVerifyJumpTarget.
 */
DECLHIDDEN(bool) supHardNtRwxPageVerify(uint8_t const *pbPage, PFNSUPHARDNTRWXPAGEVERIFYJUMPTARGET pfnVerifyJumpTarget,
                                        PFNSUPHARDNTRWXPAGESETERROR pfnSetError, void *pvUser)
{
#if SUPHARDNT_RWXPG_MIN_ENTRIES == 2
    if (!supHardNtRwxPageVerifyEntry(&pbPage[SUPHARDNT_RWXPG_ENTRY_SIZE * 0], 0, false, pfnVerifyJumpTarget, pfnSetError, pvUser))
        return false;
    if (!supHardNtRwxPageVerifyEntry(&pbPage[SUPHARDNT_RWXPG_ENTRY_SIZE * 1], 1, false, pfnVerifyJumpTarget, pfnSetError, pvUser))
        return false;
# if SUPHARDNT_RWXPG_MAX_ENTRIES == 3
    if (!supHardNtRwxPageVerifyEntry(&pbPage[SUPHARDNT_RWXPG_ENTRY_SIZE * 2], 2, true,  pfnVerifyJumpTarget, pfnSetError, pvUser))
        return false;
# elif SUPHARDNT_RWXPG_MAX_ENTRIES != 2
#  error "fixme"
# endif
#else
# error "fixme"
#endif

    /* Check that the remainder is all 0xcc. */
    if (!ASMMemIsAllU8(&pbPage[SUPHARDNT_RWXPG_ENTRY_SIZE * SUPHARDNT_RWXPG_MAX_ENTRIES],
                       PAGE_SIZE - (SUPHARDNT_RWXPG_ENTRY_SIZE * SUPHARDNT_RWXPG_MAX_ENTRIES) - RTSHA256_HASH_SIZE,
                       0xcc))
        return pfnSetError(pvUser, "RWX Page: Unused parts not padded with 0xcc.");

    /* Check the SHA256 checksum at the very end of the page. */
    if (!RTSha256Check(pbPage, PAGE_SIZE - RTSHA256_HASH_SIZE, &pbPage[PAGE_SIZE - RTSHA256_HASH_SIZE]))
        return pfnSetError(pvUser, "RWX Page: SHA-256 mismatch.");
    return true;
}


#if defined(IN_RING0) && !defined(VBOX_WITH_MINIMAL_HARDENING)

/**
 * @callback_method_impl{FNSUPHARDNTRWXPAGESETERROR,
 *      Helper for supHardNtVpVerifyImageMemoryRwxPage (ring-0 only).}
 */
static DECLCALLBACK(bool) supHardNtVpVerifyImageMemoryRwxPageSetError(void *pvUser, const char *pszMsg, ...)
{
    va_list va;
    va_start(va, pszMsg);
    supHardNtVpSetInfo2V((PSUPHNTVPSTATE)pvUser, VERR_SUP_VP_EXE_CORRUPTED_RWX_SECTION, pszMsg, va);
    va_end(va);
    return false;
}


/**
 * @callback_method_impl{FNSUPHARDNTRWXPAGEVERIFYJUMPTARGET,
 *      Helper for supHardNtVpVerifyImageMemoryRwxPage (ring-0 only).}
 */
static DECLCALLBACK(bool) supHardNtVpVerifyImageMemoryRwxPageJumpTarget(uintptr_t uTarget, uint32_t iEntry, void *pvUser)
{
    PSUPHNTVPSTATE const pThis = (PSUPHNTVPSTATE)pvUser;
    PSUPHNTVPIMAGE pNtDll = supHardNtVpFindModule(pThis, "ntdll.dll");
    if (!pNtDll)
        return false;
    if (uTarget - pNtDll->uImageBase >= pNtDll->cbImage)
        return false;

    /** @todo narrow it down to the three possible entry points as well like we
     *        do in ring-3. */

    RT_NOREF(iEntry);
    return true;
}


/**
 * Called in ring-0 to verify the RWX page in the executable image.
 */
static int supHardNtVpVerifyImageMemoryRwxPage(PSUPHNTVPSTATE pThis, PSUPHNTVPIMAGE pImage, uint32_t uRva, uint32_t iSh)
{
    /*
     * The section is exactly one page (caller checked already), make sure we
     * can buffer that much.
     */
    AssertCompile(sizeof(pThis->abMemory) >= sizeof(PAGE_SIZE));
    NTSTATUS rcNt = supHardNtVpReadMem(pThis->hProcess, pImage->uImageBase + uRva, pThis->abMemory, PAGE_SIZE);
    if (!NT_SUCCESS(rcNt))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_MEMORY_READ_ERROR,
                                   IMAGE_LOG_NAME_FMT ": Error reading %#x bytes at %p (rva %#x, #%u, .rwxpg) from memory: %#x",
                                   IMAGE_LOG_NAME(pImage), PAGE_SIZE, pImage->uImageBase + uRva, uRva, iSh + 1, rcNt);

    /*
     * Calc the SHA-384 before we start.
     *
     * We're using SHA-384 rather than SHA-512 because userland is already
     * using it and this way we save a few bytes of kernel heap.
     */
    if (pThis->pRwxPgInfo)
    {
        pThis->pRwxPgInfo->pvRwxPgR3Ptr = pImage->uImageBase + uRva;
        RTSha384(pThis->abMemory, PAGE_SIZE, pThis->pRwxPgInfo->abSha384);
    }

    /*
     * Do the verification.
     */
    if (!supHardNtRwxPageVerify(pThis->abMemory, supHardNtVpVerifyImageMemoryRwxPageJumpTarget,
                                supHardNtVpVerifyImageMemoryRwxPageSetError, pThis))
    {
        pThis->pRwxPgInfo->abSha384[0] ^= 0xff;
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EXE_CORRUPTED_RWX_SECTION,
                                   IMAGE_LOG_NAME_FMT ": Corrupted RWX page.", IMAGE_LOG_NAME(pImage));
    }

    /*
     * Re-check the SHA-384 we calculated above.
     */
    if (   pThis->pRwxPgInfo
        && !RTSha384Check(pThis->abMemory, PAGE_SIZE, pThis->pRwxPgInfo->abSha384))
    {
        pThis->pRwxPgInfo->abSha384[0] ^= 0xff;
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EXE_CORRUPTED_RWX_SECTION,
                                   IMAGE_LOG_NAME_FMT ": In-flight RWX page corruption.", IMAGE_LOG_NAME(pImage));
    }

    return VINF_SUCCESS;
}

#endif /* IN_RING0 && !defined(VBOX_WITH_MINIMAL_HARDENING) */

#ifdef IN_RING3
static NTSTATUS supHardNtVpFileMemRestore(PSUPHNTVPSTATE pThis, PVOID pvRestoreAddr, uint8_t const *pbFile, uint32_t cbToRestore,
                                          uint32_t fCorrectProtection)
{
    PVOID  pvProt   = pvRestoreAddr;
    SIZE_T cbProt   = cbToRestore;
    ULONG  fOldProt = 0;
    NTSTATUS rcNt = NtProtectVirtualMemory(pThis->hProcess, &pvProt, &cbProt, PAGE_READWRITE, &fOldProt);
    if (NT_SUCCESS(rcNt))
    {
        SIZE_T cbIgnored;
        rcNt = NtWriteVirtualMemory(pThis->hProcess, pvRestoreAddr, pbFile, cbToRestore, &cbIgnored);

        pvProt = pvRestoreAddr;
        cbProt = cbToRestore;
        NTSTATUS rcNt2 = NtProtectVirtualMemory(pThis->hProcess, &pvProt, &cbProt, fCorrectProtection, &fOldProt);
        if (NT_SUCCESS(rcNt))
            rcNt = rcNt2;
    }
    pThis->cFixes++;
    return rcNt;
}
#endif /* IN_RING3 */


typedef struct SUPHNTVPSKIPAREA
{
    uint32_t uRva;
    uint32_t cb;
} SUPHNTVPSKIPAREA;
typedef SUPHNTVPSKIPAREA *PSUPHNTVPSKIPAREA;

static int supHardNtVpFileMemCompareSection(PSUPHNTVPSTATE pThis, PSUPHNTVPIMAGE pImage,
                                            uint32_t uRva, uint32_t cb, const uint8_t *pbFile,
                                            int32_t iSh, PSUPHNTVPSKIPAREA paSkipAreas, uint32_t cSkipAreas,
                                            uint32_t fCorrectProtection)
{
#ifndef IN_RING3
    RT_NOREF1(fCorrectProtection);
#endif
    AssertCompileAdjacentMembers(SUPHNTVPSTATE, abMemory, abFile); /* Use both the memory and file buffers here. Parfait might hate me for this... */
    uint32_t  const cbMemory = sizeof(pThis->abMemory) + sizeof(pThis->abFile);
    uint8_t * const pbMemory = &pThis->abMemory[0];

    while (cb > 0)
    {
        uint32_t cbThis = RT_MIN(cb, cbMemory);

        /* Clipping. */
        uint32_t uNextRva = uRva + cbThis;
        if (cSkipAreas)
        {
            uint32_t uRvaEnd = uNextRva;
            uint32_t i = cSkipAreas;
            while (i-- > 0)
            {
                uint32_t uSkipEnd = paSkipAreas[i].uRva + paSkipAreas[i].cb;
                if (   uRva    < uSkipEnd
                    && uRvaEnd > paSkipAreas[i].uRva)
                {
                    if (uRva < paSkipAreas[i].uRva)
                    {
                        cbThis   = paSkipAreas[i].uRva - uRva;
                        uRvaEnd  = paSkipAreas[i].uRva;
                        uNextRva = uSkipEnd;
                    }
                    else if (uRvaEnd >= uSkipEnd)
                    {
                        cbThis  -= uSkipEnd - uRva;
                        pbFile  += uSkipEnd - uRva;
                        uRva     = uSkipEnd;
                    }
                    else
                    {
                        uNextRva = uSkipEnd;
                        cbThis   = 0;
                        break;
                    }
                }
            }
        }

        /* Read the memory. */
        NTSTATUS rcNt = supHardNtVpReadMem(pThis->hProcess, pImage->uImageBase + uRva, pbMemory, cbThis);
        if (!NT_SUCCESS(rcNt))
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_MEMORY_READ_ERROR,
                                       IMAGE_LOG_NAME_FMT ": Error reading %#x bytes at %p (rva %#x, #%u, %.8s) from memory: %#x",
                                       IMAGE_LOG_NAME(pImage), cbThis, pImage->uImageBase + uRva, uRva, iSh + 1,
                                       iSh >= 0 ? (char *)pThis->aSecHdrs[iSh].Name : "headers", rcNt);

        /* Do the compare. */
        if (memcmp(pbFile, pbMemory, cbThis) != 0)
        {
            const char *pachSectNm = iSh >= 0 ? (char *)pThis->aSecHdrs[iSh].Name : "headers";
            SUP_DPRINTF((IMAGE_LOG_NAME_FMT ": Differences in section #%u (%s) between file and memory:\n",
                         IMAGE_LOG_NAME(pImage), iSh + 1, pachSectNm));

            uint32_t off = 0;
            while (off < cbThis && pbFile[off] == pbMemory[off])
                off++;
            SUP_DPRINTF(("  %p / %#09x: %02x != %02x\n",
                         pImage->uImageBase + uRva + off, uRva + off, pbFile[off], pbMemory[off]));
            uint32_t offLast = off;
            uint32_t cDiffs  = 1;
            for (uint32_t off2 = off + 1; off2 < cbThis; off2++)
                if (pbFile[off2] != pbMemory[off2])
                {
                    SUP_DPRINTF(("  %p / %#09x: %02x != %02x\n",
                                 pImage->uImageBase + uRva + off2, uRva + off2, pbFile[off2], pbMemory[off2]));
                    cDiffs++;
                    offLast = off2;
                }

#ifdef IN_RING3
            if (   pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION
                || pThis->enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION
                || pThis->enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED)
            {
                PVOID pvRestoreAddr = (uint8_t *)pImage->uImageBase + uRva;
                rcNt = supHardNtVpFileMemRestore(pThis, pvRestoreAddr, pbFile, cbThis, fCorrectProtection);
                if (NT_SUCCESS(rcNt))
                    SUP_DPRINTF(("  Restored %#x bytes of original file content at %p\n", cbThis, pvRestoreAddr));
                else
                    return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_MEMORY_VS_FILE_MISMATCH,
                                               IMAGE_LOG_NAME_FMT  ": Failed to restore %#x bytes at %p (%#x, #%u, %s): %#x (cDiffs=%#x, first=%#x)",
                                               IMAGE_LOG_NAME(pImage), cbThis, pvRestoreAddr, uRva, iSh + 1, pachSectNm, rcNt,
                                               cDiffs, uRva + off);
            }
            else
#endif /* IN_RING3 */
                return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_MEMORY_VS_FILE_MISMATCH,
                                           IMAGE_LOG_NAME_FMT ": %u differences between %#x and %#x in #%u (%.8s), first: %02x != %02x",
                                           IMAGE_LOG_NAME(pImage), cDiffs, uRva + off, uRva + offLast, iSh + 1,
                                           pachSectNm, pbFile[off], pbMemory[off]);
        }

        /* Advance. The clipping makes it a little bit complicated. */
        cbThis  = uNextRva - uRva;
        if (cbThis >= cb)
            break;
        cb     -= cbThis;
        pbFile += cbThis;
        uRva    = uNextRva;
    }
    return VINF_SUCCESS;
}



static int supHardNtVpCheckSectionProtection(PSUPHNTVPSTATE pThis, PSUPHNTVPIMAGE pImage,
                                             uint32_t uRva, uint32_t cb, uint32_t fProt)
{
    uint32_t const cbOrg = cb;
    if (!cb)
        return VINF_SUCCESS;
    if (   pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION
        || pThis->enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION
        || pThis->enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED)
        return VINF_SUCCESS;

    for (uint32_t i = 0; i < pImage->cRegions; i++)
    {
        uint32_t offRegion = uRva - pImage->aRegions[i].uRva;
        if (offRegion < pImage->aRegions[i].cb)
        {
            uint32_t cbLeft = pImage->aRegions[i].cb - offRegion;
            if (   pImage->aRegions[i].fProt != fProt
                && !(fProt == PAGE_READWRITE && pImage->aRegions[i].fProt == PAGE_WRITECOPY)
                && !(fProt == PAGE_READWRITE && pImage->aRegions[i].fProt == PAGE_READONLY) /* .fptable on arm */ )
                return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_SECTION_PROTECTION_MISMATCH,
                                           IMAGE_LOG_NAME_FMT ": RVA range %#x-%#x protection is %#x, expected %#x. (cb=%#x)",
                                           IMAGE_LOG_NAME(pImage), uRva, uRva + cbLeft - 1, pImage->aRegions[i].fProt, fProt, cb);
            if (cbLeft >= cb)
                return VINF_SUCCESS;
            cb   -= cbLeft;
            uRva += cbLeft;

#if 0 /* This shouldn't ever be necessary. */
            if (   i + 1 < pImage->cRegions
                && uRva < pImage->aRegions[i + 1].uRva)
            {
                cbLeft = pImage->aRegions[i + 1].uRva - uRva;
                if (cbLeft >= cb)
                    return VINF_SUCCESS;
                cb   -= cbLeft;
                uRva += cbLeft;
            }
#endif
        }
    }

    return supHardNtVpSetInfo2(pThis, cbOrg == cb ? VERR_SUP_VP_SECTION_NOT_MAPPED : VERR_SUP_VP_SECTION_NOT_FULLY_MAPPED,
                               IMAGE_LOG_NAME_FMT ": RVA range %#x-%#x is not mapped?", IMAGE_LOG_NAME(pImage), uRva, uRva + cb - 1);
}

#ifndef VBOX_WITH_MINIMAL_HARDENING

DECLINLINE(bool) supHardNtVpIsModuleNameMatch(PSUPHNTVPIMAGE pImage, const char *pszModule)
{
    if (pImage->fDll)
    {
        const char *pszImageNm = pImage->pszName;
        for (;;)
        {
            char chLeft  = *pszImageNm++;
            char chRight = *pszModule++;
            if (chLeft != chRight)
            {
                Assert(chLeft == RT_C_TO_LOWER(chLeft));
                if (chLeft != RT_C_TO_LOWER(chRight))
                {
                    if (   chRight == '\0'
                        && chLeft  == '.'
                        && pszImageNm[0] == 'd'
                        && pszImageNm[1] == 'l'
                        && pszImageNm[2] == 'l'
                        && pszImageNm[3] == '\0')
                        return true;
                    break;
                }
            }

            if (chLeft == '\0')
                return true;
        }
    }

    return false;
}


/**
 * Worker for supHardNtVpGetImport that looks up a module in the module table.
 *
 * @returns Pointer to the module if found, NULL if not found.
 * @param   pThis               The process validator instance.
 * @param   pszModule           The name of the module we're looking for.
 */
static PSUPHNTVPIMAGE supHardNtVpFindModule(PSUPHNTVPSTATE pThis, const char *pszModule)
{
    /*
     * Check out the hint first.
     */
    if (   pThis->iImageHint < pThis->cImages
        && supHardNtVpIsModuleNameMatch(&pThis->aImages[pThis->iImageHint], pszModule))
        return &pThis->aImages[pThis->iImageHint];

    /*
     * Linear array search next.
     */
    uint32_t i = pThis->cImages;
    while (i-- > 0)
        if (supHardNtVpIsModuleNameMatch(&pThis->aImages[i], pszModule))
        {
            pThis->iImageHint = i;
            return &pThis->aImages[i];
        }

    /* No cigar. */
    return NULL;
}

#endif /* !VBOX_WITH_MINIMAL_HARDENING */


/**
 * @callback_method_impl{FNRTLDRIMPORT}
 */
static DECLCALLBACK(int) supHardNtVpGetImport(RTLDRMOD hLdrMod, const char *pszModule, const char *pszSymbol, unsigned uSymbol,
                                              PRTLDRADDR pValue, void *pvUser)
{
    /*SUP_DPRINTF(("supHardNtVpGetImport: %s / %#x / %s.\n", pszModule, uSymbol, pszSymbol));*/
    PSUPHNTVPSTATE const pThis = (PSUPHNTVPSTATE)pvUser;

#ifndef VBOX_WITH_MINIMAL_HARDENING
    RT_NOREF1(hLdrMod);

    int rc = VERR_MODULE_NOT_FOUND;
    PSUPHNTVPIMAGE pImage = supHardNtVpFindModule(pThis, pszModule);
    if (pImage)
    {
        rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pImage->pCacheEntry->pbBits,
                              pImage->uImageBase, uSymbol, pszSymbol, pValue);
        if (RT_SUCCESS(rc))
            return rc;
    }
    /*
     * API set hacks.
     */
    else if (!RTStrNICmp(pszModule, RT_STR_TUPLE("api-ms-win-")))
    {
        static const char * const s_apszDlls[] = { "ntdll.dll", "kernelbase.dll", "kernel32.dll" };
        for (uint32_t i = 0; i < RT_ELEMENTS(s_apszDlls); i++)
        {
            pImage = supHardNtVpFindModule(pThis, s_apszDlls[i]);
            if (pImage)
            {
                rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pImage->pCacheEntry->pbBits,
                                      pImage->uImageBase, uSymbol, pszSymbol, pValue);
                if (RT_SUCCESS(rc))
                    return rc;
                if (rc != VERR_SYMBOL_NOT_FOUND)
                    break;
            }
        }
    }

    /*
     * Deal with forwarders.
     * ASSUMES no forwarders thru any api-ms-win-core-*.dll.
     * ASSUMES forwarders are resolved after one redirection.
     */
    if (rc == VERR_LDR_FORWARDER)
    {
        size_t           cbInfo = RT_MIN((uint32_t)*pValue, sizeof(RTLDRIMPORTINFO) + 32);
        PRTLDRIMPORTINFO pInfo  = (PRTLDRIMPORTINFO)alloca(cbInfo);
        rc = RTLdrQueryForwarderInfo(pImage->pCacheEntry->hLdrMod, pImage->pCacheEntry->pbBits,
                                     uSymbol, pszSymbol, pInfo, cbInfo);
        if (RT_SUCCESS(rc))
        {
            rc = VERR_MODULE_NOT_FOUND;
            pImage = supHardNtVpFindModule(pThis, pInfo->szModule);
            if (pImage)
            {
                rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pImage->pCacheEntry->pbBits,
                                      pImage->uImageBase, pInfo->iOrdinal, pInfo->pszSymbol, pValue);
                if (RT_SUCCESS(rc))
                    return rc;

                SUP_DPRINTF(("supHardNtVpGetImport: Failed to find symbol '%s' in '%s' (forwarded from %s / %s): %Rrc\n",
                             pInfo->pszSymbol, pInfo->szModule, pszModule, pszSymbol, rc));
                if (rc == VERR_LDR_FORWARDER)
                    rc = VERR_LDR_FORWARDER_CHAIN_TOO_LONG;
            }
            else
                SUP_DPRINTF(("supHardNtVpGetImport: Failed to find forwarder module '%s' (%#x / %s; originally %s / %#x / %s): %Rrc\n",
                             pInfo->szModule, pInfo->iOrdinal, pInfo->pszSymbol, pszModule, uSymbol, pszSymbol, rc));
        }
        else
            SUP_DPRINTF(("supHardNtVpGetImport: RTLdrQueryForwarderInfo failed on symbol %#x/'%s' in '%s': %Rrc\n",
                         uSymbol, pszSymbol, pszModule, rc));
    }
    else
        SUP_DPRINTF(("supHardNtVpGetImport: Failed to find symbol %#x / '%s' in '%s': %Rrc\n",
                     uSymbol, pszSymbol, pszModule, rc));
    return rc;

#else  /* VBOX_WITH_MINIMAL_HARDENING */
    /*
     * We don't care about correct imports here, as we will skip the import
     * table while comparing image bits.  (If we wanted to produce correct
     * imports, we'd have to track all the DLLs in the process, which would be
     * bothersome and expensive while not really gaining any better certainty
     * that we are actually executing the image we're looking at.)
     */
    RT_NOREF(hLdrMod, pszModule, pszSymbol, uSymbol);
    *pValue = pThis->aImages[0].uImageBase - PAGE_SIZE;
    return VINF_SUCCESS;
#endif /* VBOX_WITH_MINIMAL_HARDENING */
}


/**
 * Compares process memory with the disk content.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure (for the
 *                              two scratch buffers).
 * @param   pImage              The image data collected during the address
 *                              space scan.
 */
static int supHardNtVpVerifyImageMemoryCompare(PSUPHNTVPSTATE pThis, PSUPHNTVPIMAGE pImage)
{
    /*
     * Read and find the file headers.
     */
    int rc = supHardNtVpReadImage(pImage, 0 /*off*/, pThis->abFile, sizeof(pThis->abFile));
    if (RT_FAILURE(rc))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_IMAGE_HDR_READ_ERROR,
                                   IMAGE_LOG_NAME_FMT ": Error reading image header: %Rrc", IMAGE_LOG_NAME(pImage), rc);

    uint32_t offNtHdrs = 0;
    PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)&pThis->abFile[0];
    if (pDosHdr->e_magic == IMAGE_DOS_SIGNATURE)
    {
        offNtHdrs = pDosHdr->e_lfanew;
        if (offNtHdrs > 512 || offNtHdrs < sizeof(*pDosHdr))
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_MZ_OFFSET,
                                       IMAGE_LOG_NAME_FMT ": Unexpected e_lfanew value: %#x", IMAGE_LOG_NAME(pImage), offNtHdrs);
    }
    PIMAGE_NT_HEADERS   pNtHdrs   = (PIMAGE_NT_HEADERS)&pThis->abFile[offNtHdrs];
    PIMAGE_NT_HEADERS32 pNtHdrs32 = (PIMAGE_NT_HEADERS32)pNtHdrs;
    if (pNtHdrs->Signature != IMAGE_NT_SIGNATURE)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_SIGNATURE,
                                   IMAGE_LOG_NAME_FMT ": No PE signature at %#x: %#x", IMAGE_LOG_NAME(pImage), offNtHdrs, pNtHdrs->Signature);

    /*
     * Do basic header validation.
     */
#ifdef RT_ARCH_AMD64
    const uint16_t uExpectedMachine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(RT_ARCH_ARM64)
    const uint16_t uExpectedMachine = IMAGE_FILE_MACHINE_ARM64;
#elif defined(RT_ARCH_X86)
    const uint16_t uExpectedMachine = IMAGE_FILE_MACHINE_I386;
#else
# error "port me"
#endif
#if ARCH_BITS == 64
    if (pNtHdrs->FileHeader.Machine != uExpectedMachine && !pImage->f32bitResourceDll)
#elif defined
    if (pNtHdrs->FileHeader.Machine != uExpectedMachine)
#endif
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_UNEXPECTED_IMAGE_MACHINE,
                                   IMAGE_LOG_NAME_FMT ": Unexpected machine: %#x (expected %#x)",
                                   IMAGE_LOG_NAME(pImage), pNtHdrs->FileHeader.Machine, uExpectedMachine);
    bool const fIs32Bit = pNtHdrs->FileHeader.Machine == IMAGE_FILE_MACHINE_I386;

    if (pNtHdrs->FileHeader.SizeOfOptionalHeader != (fIs32Bit ? sizeof(IMAGE_OPTIONAL_HEADER32) : sizeof(IMAGE_OPTIONAL_HEADER64)))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_OPTIONAL_HEADER,
                                   IMAGE_LOG_NAME_FMT ": Unexpected optional header size: %#x",
                                   IMAGE_LOG_NAME(pImage), pNtHdrs->FileHeader.SizeOfOptionalHeader);

    if (pNtHdrs->OptionalHeader.Magic != (fIs32Bit ? IMAGE_NT_OPTIONAL_HDR32_MAGIC : IMAGE_NT_OPTIONAL_HDR64_MAGIC))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_OPTIONAL_HEADER,
                                   IMAGE_LOG_NAME_FMT ": Unexpected optional header magic: %#x", IMAGE_LOG_NAME(pImage), pNtHdrs->OptionalHeader.Magic);

    uint32_t cDirs = (fIs32Bit ? pNtHdrs32->OptionalHeader.NumberOfRvaAndSizes : pNtHdrs->OptionalHeader.NumberOfRvaAndSizes);
    if (cDirs != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_OPTIONAL_HEADER,
                                   IMAGE_LOG_NAME_FMT ": Unexpected data dirs: %#x", IMAGE_LOG_NAME(pImage), cDirs);

    /*
     * Before we start comparing things, store what we need to know from the headers.
     */
    uint32_t  const cSections  = pNtHdrs->FileHeader.NumberOfSections;
    if (cSections > RT_ELEMENTS(pThis->aSecHdrs))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_TOO_MANY_SECTIONS,
                                   IMAGE_LOG_NAME_FMT ": Too many section headers: %#x", IMAGE_LOG_NAME(pImage), cSections);
    suplibHardenedMemCopy(pThis->aSecHdrs, (fIs32Bit ? (void *)(pNtHdrs32 + 1) : (void *)(pNtHdrs + 1)),
                          cSections * sizeof(IMAGE_SECTION_HEADER));

    uintptr_t const uImageBase = fIs32Bit ? pNtHdrs32->OptionalHeader.ImageBase : pNtHdrs->OptionalHeader.ImageBase;
    if (uImageBase & PAGE_OFFSET_MASK)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_BASE,
                                   IMAGE_LOG_NAME_FMT ": Invalid image base: %p", IMAGE_LOG_NAME(pImage), uImageBase);

    uint32_t  const cbImage    = fIs32Bit ? pNtHdrs32->OptionalHeader.SizeOfImage : pNtHdrs->OptionalHeader.SizeOfImage;
    uint32_t  const cbImagePgAligned = RT_ALIGN_32(cbImage, PAGE_SIZE);
    if (RT_ALIGN_32(pImage->cbImage, PAGE_SIZE) != cbImagePgAligned && !pImage->fApiSetSchemaOnlySection1)
    {
        /*
         * Since 24h2, the kernel loader appends three pages to system images (from
         * the looks of it).  This manifests in an additional 2 (or 1) mapping regions,
         * one which is execute/read (one page in 27718) and a 2nd one which is reserved.
         * They both assocated with the DLL section object, so not like the typical AV
         * allocations placed right after DLLs for entertaining unsolicited code changes.
         */
        if (pImage->cbImage < cbImagePgAligned)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_SIZE,
                                       IMAGE_LOG_NAME_FMT ": SizeOfImage (%#x) is larger than the mapping size (%#x)",
                                       IMAGE_LOG_NAME(pImage), cbImage, pImage->cbImage);

        /* This code has to be paranoid, so we must put some kind of limit on this extra
           space.  64KB is taken out of thin (late night office) air, as per usual. */
        if (pImage->cbImage > cbImagePgAligned + _64K)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_SIZE,
                                       IMAGE_LOG_NAME_FMT ": SizeOfImage (%#x) isn't close enough to the mapping size (%#x) - diff %#x bytes; max expected is 64KB",
                                       IMAGE_LOG_NAME(pImage), cbImage, pImage->cbImage, pImage->cbImage - cbImage);

        /* Locate the mapping region for the extra pages: */
        if (pImage->cRegions <= 1)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_SIZE,
                                       IMAGE_LOG_NAME_FMT ": SizeOfImage (%#x) is smaller than the mapping size (%#x) and there are less than two mapping regions!",
                                       IMAGE_LOG_NAME(pImage), cbImage, pImage->cbImage);
        uint32_t iRegion = pImage->cRegions - 1;
        while (iRegion > 0 && pImage->aRegions[iRegion].uRva > cbImagePgAligned)
            iRegion--;
        if (pImage->aRegions[iRegion].uRva != cbImagePgAligned)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_SIZE,
                                       IMAGE_LOG_NAME_FMT ": SizeOfImage (%#x) is smaller than the mapping size (%#x) and we cannot locate the region(s) for the extra space! (iRegion=%d: uRva=%#x, expected %#x)",
                                       IMAGE_LOG_NAME(pImage), cbImage, pImage->cbImage, iRegion, pImage->aRegions[iRegion].uRva,
                                       cbImagePgAligned);

        /* Check that none of the pages are both writable & executable. */
        for (uint32_t i = iRegion; i < pImage->cRegions; i++)
            if (pImage->aRegions[i].fProt & PAGE_EXECUTE_READWRITE)
                return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_SIZE,
                                           IMAGE_LOG_NAME_FMT ": SizeOfImage (%#x) is smaller than the mapping size (%#x) and extra page %#x LB %#x are RWX (%#x)!",
                                           IMAGE_LOG_NAME(pImage), cbImage, pImage->cbImage,
                                           pImage->aRegions[i].uRva, pImage->aRegions[i].cb, pImage->aRegions[i].fProt);
        /** @todo more restrictions on this? */
    }

    if (cbImage != RTLdrSize(pImage->pCacheEntry->hLdrMod))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_IMAGE_SIZE,
                                   IMAGE_LOG_NAME_FMT ": SizeOfImage (%#x) differs from what RTLdrSize returns (%#zx)",
                                   IMAGE_LOG_NAME(pImage), cbImage, RTLdrSize(pImage->pCacheEntry->hLdrMod));

    uint32_t const cbSectAlign = fIs32Bit ? pNtHdrs32->OptionalHeader.SectionAlignment : pNtHdrs->OptionalHeader.SectionAlignment;
    if (   !RT_IS_POWER_OF_TWO(cbSectAlign)
        || cbSectAlign < PAGE_SIZE
        || cbSectAlign > (pImage->fApiSetSchemaOnlySection1 ? _64K : (uint32_t)PAGE_SIZE) )
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_SECTION_ALIGNMENT_VALUE,
                                   IMAGE_LOG_NAME_FMT ": Unexpected SectionAlignment value: %#x", IMAGE_LOG_NAME(pImage), cbSectAlign);

    uint32_t const cbFileAlign = fIs32Bit ? pNtHdrs32->OptionalHeader.FileAlignment : pNtHdrs->OptionalHeader.FileAlignment;
    if (!RT_IS_POWER_OF_TWO(cbFileAlign) || cbFileAlign < 512 || cbFileAlign > PAGE_SIZE || cbFileAlign > cbSectAlign)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_FILE_ALIGNMENT_VALUE,
                                   IMAGE_LOG_NAME_FMT ": Unexpected FileAlignment value: %#x (cbSectAlign=%#x)",
                                   IMAGE_LOG_NAME(pImage), cbFileAlign, cbSectAlign);

    uint32_t  const cbHeaders  = fIs32Bit ? pNtHdrs32->OptionalHeader.SizeOfHeaders : pNtHdrs->OptionalHeader.SizeOfHeaders;
    uint32_t  const cbMinHdrs  = offNtHdrs + (fIs32Bit ? sizeof(*pNtHdrs32) : sizeof(*pNtHdrs) )
                               + sizeof(IMAGE_SECTION_HEADER) * cSections;
    if (cbHeaders < cbMinHdrs)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_SIZE_OF_HEADERS,
                                   IMAGE_LOG_NAME_FMT ": Headers are too small: %#x < %#x (cSections=%#x)",
                                   IMAGE_LOG_NAME(pImage), cbHeaders, cbMinHdrs, cSections);
    uint32_t  const cbHdrsFile = RT_ALIGN_32(cbHeaders, cbFileAlign);
    if (cbHdrsFile > sizeof(pThis->abFile))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_SIZE_OF_HEADERS,
                                   IMAGE_LOG_NAME_FMT ": Headers are larger than expected: %#x/%#x (expected max %zx)",
                                   IMAGE_LOG_NAME(pImage), cbHeaders, cbHdrsFile, sizeof(pThis->abFile));

    /*
     * Save some header fields we might be using later on.
     */
    pImage->fImageCharecteristics = pNtHdrs->FileHeader.Characteristics;
    pImage->fDllCharecteristics   = fIs32Bit ? pNtHdrs32->OptionalHeader.DllCharacteristics : pNtHdrs->OptionalHeader.DllCharacteristics;

    /*
     * Correct the apisetschema image base, size and region rva.
     */
    if (pImage->fApiSetSchemaOnlySection1)
    {
        pImage->uImageBase      -= pThis->aSecHdrs[0].VirtualAddress;
        pImage->cbImage         += pThis->aSecHdrs[0].VirtualAddress;
        pImage->aRegions[0].uRva = pThis->aSecHdrs[0].VirtualAddress;
    }

    /*
     * This is as far as we go for the initial process.
     */
    if (pThis->enmKind == SUPHARDNTVPKIND_LIMITED_VERIFY_ONLY)
        return VINF_SUCCESS;

    /*
     * Get relocated bits.
     */
    uint8_t *pbBits;
    if (pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION)
        rc = supHardNtLdrCacheEntryGetBits(pImage->pCacheEntry, &pbBits, pImage->uImageBase, NULL /*pfnGetImport*/, pThis,
                                           pThis->pErrInfo);
    else
        rc = supHardNtLdrCacheEntryGetBits(pImage->pCacheEntry, &pbBits, pImage->uImageBase, supHardNtVpGetImport, pThis,
                                           pThis->pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    /* XP SP3 does not set ImageBase to load address. It fixes up the image on load time though. */
    if (g_uNtVerCombined >= SUP_NT_VER_VISTA)
    {
        if (fIs32Bit)
            ((PIMAGE_NT_HEADERS32)&pbBits[offNtHdrs])->OptionalHeader.ImageBase = (uint32_t)pImage->uImageBase;
        else
            ((PIMAGE_NT_HEADERS)&pbBits[offNtHdrs])->OptionalHeader.ImageBase   = pImage->uImageBase;
    }

    /*
     * Figure out areas we should skip during comparison.
     */
    uint32_t         cSkipAreas = 0;
    SUPHNTVPSKIPAREA aSkipAreas[7];
#ifndef VBOX_WITH_MINIMAL_HARDENING
    if (pImage->enmImageId == kSupHNtVpImageId_NtDll)
    {
        RTLDRADDR uValue;
        if (pThis->enmKind == SUPHARDNTVPKIND_VERIFY_ONLY)
        {
            /* Ignore our NtCreateSection hack. */
            rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pbBits, 0, UINT32_MAX, "NtCreateSection", &uValue);
            if (RT_FAILURE(rc))
                return supHardNtVpSetInfo2(pThis, rc, "%s: Failed to find 'NtCreateSection': %Rrc", pImage->pszName, rc);
            aSkipAreas[cSkipAreas].uRva = (uint32_t)uValue;
            aSkipAreas[cSkipAreas++].cb = ARCH_BITS == 32 ? 5 : 12;

            /* Ignore our LdrLoadDll hack. */
            rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pbBits, 0, UINT32_MAX, "LdrLoadDll", &uValue);
            if (RT_FAILURE(rc))
                return supHardNtVpSetInfo2(pThis, rc, "%s: Failed to find 'LdrLoadDll': %Rrc", pImage->pszName, rc);
            aSkipAreas[cSkipAreas].uRva = (uint32_t)uValue;
            aSkipAreas[cSkipAreas++].cb = ARCH_BITS == 32 ? 5 : 12;
        }

        /* Ignore our patched LdrInitializeThunk hack. */
        rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pbBits, 0, UINT32_MAX, "LdrInitializeThunk", &uValue);
        if (RT_FAILURE(rc))
            return supHardNtVpSetInfo2(pThis, rc, "%s: Failed to find 'LdrInitializeThunk': %Rrc", pImage->pszName, rc);
        aSkipAreas[cSkipAreas].uRva = (uint32_t)uValue;
        aSkipAreas[cSkipAreas++].cb = 14;

        /* Ignore our patched KiUserApcDispatcher hack. */
        rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pbBits, 0, UINT32_MAX, "KiUserApcDispatcher", &uValue);
        if (RT_FAILURE(rc))
            return supHardNtVpSetInfo2(pThis, rc, "%s: Failed to find 'KiUserApcDispatcher': %Rrc", pImage->pszName, rc);
        aSkipAreas[cSkipAreas].uRva = (uint32_t)uValue;
        aSkipAreas[cSkipAreas++].cb = 14;

# ifndef VBOX_WITHOUT_HARDENDED_XCPT_LOGGING
        /* Ignore our patched KiUserExceptionDispatcher hack. */
        rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pbBits, 0, UINT32_MAX, "KiUserExceptionDispatcher", &uValue);
        if (RT_FAILURE(rc))
            return supHardNtVpSetInfo2(pThis, rc, "%s: Failed to find 'KiUserExceptionDispatcher': %Rrc", pImage->pszName, rc);
        aSkipAreas[cSkipAreas].uRva = (uint32_t)uValue + (HC_ARCH_BITS == 64);
        aSkipAreas[cSkipAreas++].cb = HC_ARCH_BITS == 64 ? 13 : 12;
# endif

        /* LdrSystemDllInitBlock is filled in by the kernel. It mainly contains addresses of 32-bit ntdll method for wow64.
           Lately, it also includes CET and other compatibility stuff.  It keeps growing... */
        rc = RTLdrGetSymbolEx(pImage->pCacheEntry->hLdrMod, pbBits, 0, UINT32_MAX, "LdrSystemDllInitBlock", &uValue);
        if (RT_SUCCESS(rc))
        {
            uint32_t uRva = (uint32_t)uValue;
            uint32_t cb   = *(uint32_t const *)&pbBits[uRva]; /* from the image file, not memory */
            uint32_t iSh  = 0;
            while (   iSh < cSections
                   && uRva - pThis->aSecHdrs[iSh].VirtualAddress >= pThis->aSecHdrs[iSh].Misc.VirtualSize)
                iSh++;
            if (iSh < cSections)
            {
                uint32_t cbMax = pThis->aSecHdrs[iSh].Misc.VirtualSize - (uRva - pThis->aSecHdrs[iSh].VirtualAddress);
                cbMax = RT_MIN(cbMax, _16K);
                if (   cb <= cbMax
                    && cb >= 0x40 /* initial size was 0x60 according to Geoff. */ )
                {   /* The size seems sane, so drop it from the sanitizing. */
                    uRva += sizeof(uint32_t);
                    cb   -= sizeof(uint32_t);
                }
                else if (cb > cbMax)
                    cb = cbMax;
            }
            else
                AssertFailedStmt(cb = RT_MIN(cb, _4K));
            aSkipAreas[cSkipAreas].uRva = uRva;
            aSkipAreas[cSkipAreas++].cb = cb;
        }

        Assert(cSkipAreas <= RT_ELEMENTS(aSkipAreas));
    }

#else  /* VBOX_WITH_MINIMAL_HARDENING */
# if 0 /* This is .rdata stuff, which it turns out contains more random stuff modified via the loadcfg. So, skipping it .rdata. */
    /* Skip the IAT as we don't process fixups correctly. */
    uint32_t const offAfterHdrs = offNtHdrs
                                + (!fIs32Bit ? pNtHdrs->OptionalHeader.SizeOfHeaders : pNtHdrs32->OptionalHeader.SizeOfHeaders);
    aSkipAreas[cSkipAreas].uRva = !fIs32Bit
                                ? pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress
                                : pNtHdrs32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress;
    aSkipAreas[cSkipAreas].cb   = !fIs32Bit
                                ? pNtHdrs->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size
                                : pNtHdrs32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size;
    if (   aSkipAreas[cSkipAreas].cb   > 0
        && aSkipAreas[cSkipAreas].cb   < pImage->cbImage / 4
        && aSkipAreas[cSkipAreas].uRva > offAfterHdrs
        && aSkipAreas[cSkipAreas].uRva < pImage->cbImage
        && aSkipAreas[cSkipAreas].uRva + aSkipAreas[cSkipAreas].cb <= pImage->cbImage)
        cSkipAreas++;
# endif
#endif /* VBOX_WITH_MINIMAL_HARDENING */

    /*
     * Compare the file header with the loaded bits.  The loader will fiddle
     * with image base, changing it to the actual load address.
     */
    if (!pImage->fApiSetSchemaOnlySection1)
    {
        rc = supHardNtVpFileMemCompareSection(pThis, pImage, 0 /*uRva*/, cbHdrsFile, pbBits, -1, NULL, 0, PAGE_READONLY);
        if (RT_FAILURE(rc))
            return rc;

        rc = supHardNtVpCheckSectionProtection(pThis, pImage, 0 /*uRva*/, cbHdrsFile, PAGE_READONLY);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Validate sections:
     *      - Check them against the mapping regions.
     *      - Check section bits according to enmKind.
     */
    uint32_t fPrevProt = PAGE_READONLY;
    uint32_t uRva      = cbHdrsFile;
    for (uint32_t i = 0; i < cSections; i++)
    {
        /* Validate the section. */
        uint32_t uSectRva = pThis->aSecHdrs[i].VirtualAddress;
        if (uSectRva < uRva || uSectRva > cbImage || RT_ALIGN_32(uSectRva, cbSectAlign) != uSectRva)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_SECTION_RVA,
                                       IMAGE_LOG_NAME_FMT ": Section %u: Invalid virtual address: %#x (uRva=%#x, cbImage=%#x, cbSectAlign=%#x)",
                                       IMAGE_LOG_NAME(pImage), i, uSectRva, uRva, cbImage, cbSectAlign);
        uint32_t cbMap  = pThis->aSecHdrs[i].Misc.VirtualSize;
        if (cbMap > cbImage || uRva + cbMap > cbImage)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_SECTION_VIRTUAL_SIZE,
                                       IMAGE_LOG_NAME_FMT ": Section %u: Invalid virtual size: %#x (uSectRva=%#x, uRva=%#x, cbImage=%#x)",
                                       IMAGE_LOG_NAME(pImage), i, cbMap, uSectRva, uRva, cbImage);
        uint32_t cbFile = pThis->aSecHdrs[i].SizeOfRawData;
        if (cbFile != RT_ALIGN_32(cbFile, cbFileAlign) || cbFile > RT_ALIGN_32(cbMap, cbSectAlign))
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_BAD_SECTION_FILE_SIZE,
                                       IMAGE_LOG_NAME_FMT ": Section %u: Invalid file size: %#x (cbMap=%#x, uSectRva=%#x)",
                                       IMAGE_LOG_NAME(pImage), i, cbFile, cbMap, uSectRva);

        /* Validate the protection and bits. */
        if (!pImage->fApiSetSchemaOnlySection1 || i == 0)
        {
            uint32_t fProt;
            switch (pThis->aSecHdrs[i].Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE))
            {
                case IMAGE_SCN_MEM_READ:
                    fProt = PAGE_READONLY;
                    break;
                case IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE:
                    fProt = PAGE_READWRITE;
                    if (   pThis->enmKind != SUPHARDNTVPKIND_VERIFY_ONLY
                        && pThis->enmKind != SUPHARDNTVPKIND_CHILD_PURIFICATION
                        && !suplibHardenedMemComp(pThis->aSecHdrs[i].Name, ".mrdata", 8)) /* w8.1, ntdll. Changed by proc init. */
                        fProt = PAGE_READONLY;
                    break;
                case IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE:
                    fProt = PAGE_EXECUTE_READ;
                    break;
                case IMAGE_SCN_MEM_EXECUTE:
                    fProt = PAGE_EXECUTE;
                    break;
                case IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE:
                    /* Only the executable is allowed to have this section,
                       and it's protected after we're done patching. */
                    if (!pImage->fDll)
                    {
                        if (   cbMap != PAGE_SIZE
                            || memcmp(pThis->aSecHdrs[i].Name, ".rwxpg\0", sizeof(".rwxpg\0")) != 0)
                            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EXE_MALFORMED_RWX_SECTION,
                                                       IMAGE_LOG_NAME_FMT ": Section %u: uSectRva=%#x cbMap=%#x szName=%8.8s Characteristics=%#x",
                                                       IMAGE_LOG_NAME(pImage), i, uSectRva, cbMap, &pThis->aSecHdrs[i].Name[0],
                                                       pThis->aSecHdrs[i].Characteristics);
                        if (pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION)
                            fProt = PAGE_EXECUTE_READWRITE;
                        else
                        {
#if defined(IN_RING0) && !defined(VBOX_WITH_MINIMAL_HARDENING)
                            if (pThis->enmKind == SUPHARDNTVPKIND_VERIFY_ONLY)
                            {
                                rc = supHardNtVpVerifyImageMemoryRwxPage(pThis, pImage, uRva, i);
                                if (RT_FAILURE(rc))
                                    return rc;
                            }
#endif
                            fProt = PAGE_EXECUTE_READ;
                        }
                        break;
                    }
                    RT_FALL_THRU();
                default:
                    return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_UNEXPECTED_SECTION_FLAGS,
                                               IMAGE_LOG_NAME_FMT ": Section %u: Unexpected characteristics: %#x (uSectRva=%#x, cbMap=%#x)",
                                               IMAGE_LOG_NAME(pImage), i, pThis->aSecHdrs[i].Characteristics, uSectRva, cbMap);
            }

            /* The section bits. Child purification verifies all, normal
               verification verifies all except where the executable is
               concerned (due to opening vboxdrv during early process init). */
            if (   (   (pThis->aSecHdrs[i].Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE))
                    && !(pThis->aSecHdrs[i].Characteristics & IMAGE_SCN_MEM_WRITE))
#ifndef VBOX_WITH_MINIMAL_HARDENING /* only executable segments */
                || (pThis->aSecHdrs[i].Characteristics & (IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE)) == IMAGE_SCN_MEM_READ
                || (pThis->enmKind == SUPHARDNTVPKIND_VERIFY_ONLY && pImage->fDll)
                || pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION
#endif
               )
            {
                rc = VINF_SUCCESS;
                if (uRva < uSectRva && !pImage->fApiSetSchemaOnlySection1) /* Any gap worth checking? */
                    rc = supHardNtVpFileMemCompareSection(pThis, pImage, uRva, uSectRva - uRva, pbBits + uRva,
                                                          i - 1, NULL, 0, fPrevProt);
                if (RT_SUCCESS(rc))
                    rc = supHardNtVpFileMemCompareSection(pThis, pImage, uSectRva, cbMap, pbBits + uSectRva,
                                                          i, aSkipAreas, cSkipAreas, fProt);
                if (RT_SUCCESS(rc))
                {
                    uint32_t cbMapAligned = i + 1 < cSections && !pImage->fApiSetSchemaOnlySection1
                                          ? RT_ALIGN_32(cbMap, cbSectAlign) : RT_ALIGN_32(cbMap, PAGE_SIZE);
                    if (cbMapAligned > cbMap)
                        rc = supHardNtVpFileMemCompareSection(pThis, pImage, uSectRva + cbMap, cbMapAligned - cbMap,
                                                              g_abRTZeroPage, i, NULL, 0, fProt);
                }
                if (RT_FAILURE(rc))
                    return rc;
            }

            /* The protection (must be checked afterwards!). */
            rc = supHardNtVpCheckSectionProtection(pThis, pImage, uSectRva, RT_ALIGN_32(cbMap, PAGE_SIZE), fProt);
            if (RT_FAILURE(rc))
                return rc;

            fPrevProt = fProt;
        }

        /* Advance the RVA. */
        uRva = uSectRva + RT_ALIGN_32(cbMap, cbSectAlign);
    }

    return VINF_SUCCESS;
}


/**
 * Verifies the signature of the given image on disk, then checks if the memory
 * mapping matches what we verified.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure (for the
 *                              two scratch buffers).
 * @param   pImage              The image data collected during the address
 *                              space scan.
 */
static int supHardNtVpVerifyImage(PSUPHNTVPSTATE pThis, PSUPHNTVPIMAGE pImage)
{
    /*
     * Validate the file signature first, then do the memory compare.
     */
    int rc;
    if (   pImage->pCacheEntry != NULL
        && pImage->pCacheEntry->hLdrMod != NIL_RTLDRMOD)
    {
        rc = supHardNtLdrCacheEntryVerify(pImage->pCacheEntry, pImage->Name.UniStr.Buffer, pThis->pErrInfo);
        if (RT_SUCCESS(rc))
            rc = supHardNtVpVerifyImageMemoryCompare(pThis, pImage);
    }
    else
        rc = supHardNtVpSetInfo2(pThis, VERR_OPEN_FAILED, "pCacheEntry/hLdrMod is NIL! Impossible!");
    return rc;
}


/**
 * Verifies that there is only one thread in the process.
 *
 * @returns VBox status code.
 * @param   hProcess            The process.
 * @param   hThread             The thread.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 */
DECLHIDDEN(int) supHardNtVpThread(HANDLE hProcess, HANDLE hThread, PRTERRINFO pErrInfo)
{
    RT_NOREF1(hProcess);

    /*
     * Use the ThreadAmILastThread request to check that there is only one
     * thread in the process.
     * Seems this isn't entirely reliable when hThread isn't the current thread?
     */
    ULONG cbIgn = 0;
    ULONG fAmI  = 0;
    NTSTATUS rcNt = NtQueryInformationThread(hThread, ThreadAmILastThread, &fAmI, sizeof(fAmI), &cbIgn);
    if (!NT_SUCCESS(rcNt))
        return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_NT_QI_THREAD_ERROR,
                                   "NtQueryInformationThread/ThreadAmILastThread -> %#x", rcNt);
    if (!fAmI)
        return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_THREAD_NOT_ALONE,
                                   "More than one thread in process");

    /** @todo Would be nice to verify the relationship between hProcess and hThread
     *        as well... */
    return VINF_SUCCESS;
}


/**
 * Verifies that there isn't a debugger attached to the process.
 *
 * @returns VBox status code.
 * @param   hProcess            The process.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 */
DECLHIDDEN(int) supHardNtVpDebugger(HANDLE hProcess, PRTERRINFO pErrInfo)
{
#ifndef VBOX_WITHOUT_DEBUGGER_CHECKS
    /*
     * Use the ProcessDebugPort request to check there is no debugger
     * currently attached to the process.
     */
    ULONG     cbIgn = 0;
    uintptr_t uPtr  = ~(uintptr_t)0;
    NTSTATUS rcNt = NtQueryInformationProcess(hProcess,
                                              ProcessDebugPort,
                                              &uPtr, sizeof(uPtr), &cbIgn);
    if (!NT_SUCCESS(rcNt))
        return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_NT_QI_PROCESS_DBG_PORT_ERROR,
                                   "NtQueryInformationProcess/ProcessDebugPort -> %#x", rcNt);
    if (uPtr != 0)
        return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_DEBUGGED,
                                   "Debugger attached (%#zx)", uPtr);
#else
    RT_NOREF2(hProcess, pErrInfo);
#endif /* !VBOX_WITHOUT_DEBUGGER_CHECKS */
    return VINF_SUCCESS;
}


/**
 * Matches two UNICODE_STRING structures in a case sensitive fashion.
 *
 * @returns true if equal, false if not.
 * @param   pUniStr1            The first unicode string.
 * @param   pUniStr2            The first unicode string.
 */
static bool supHardNtVpAreUniStringsEqual(PCUNICODE_STRING pUniStr1, PCUNICODE_STRING pUniStr2)
{
    if (pUniStr1->Length != pUniStr2->Length)
        return false;
    return suplibHardenedMemComp(pUniStr1->Buffer, pUniStr2->Buffer, pUniStr1->Length) == 0;
}


/**
 * Performs a case insensitive comparison of an ASCII and an UTF-16 file name.
 *
 * @returns true / false
 * @param   pszName1            The ASCII name.
 * @param   pwszName2           The UTF-16 name.
 */
static bool supHardNtVpAreNamesEqual(const char *pszName1, PCRTUTF16 pwszName2)
{
    for (;;)
    {
        char    ch1 = *pszName1++;
        RTUTF16 wc2 = *pwszName2++;
        if (ch1 != wc2)
        {
            ch1 = RT_C_TO_LOWER(ch1);
            wc2 = wc2 < 0x80 ? RT_C_TO_LOWER(wc2) : wc2;
            if (ch1 != wc2)
                return false;
        }
        if (!ch1)
            return true;
    }
}


/**
 * Compares two paths, expanding 8.3 short names as needed.
 *
 * @returns true / false.
 * @param   pUniStr1        The first path.  Must be zero terminated!
 * @param   pUniStr2        The second path.  Must be zero terminated!
 */
static bool supHardNtVpArePathsEqual(PCUNICODE_STRING pUniStr1, PCUNICODE_STRING pUniStr2)
{
    /* Both strings must be null terminated. */
    Assert(pUniStr1->Buffer[pUniStr1->Length / sizeof(WCHAR)] == '\0');
    Assert(pUniStr2->Buffer[pUniStr1->Length / sizeof(WCHAR)] == '\0');

    /* Simple compare first.*/
    if (supHardNtVpAreUniStringsEqual(pUniStr1, pUniStr2))
        return true;

    /* Make long names if needed. */
    UNICODE_STRING UniStrLong1 = { 0, 0, NULL };
    if (RTNtPathFindPossible8dot3Name(pUniStr1->Buffer))
    {
        int rc = RTNtPathExpand8dot3PathA(pUniStr1, false /*fPathOnly*/, &UniStrLong1);
        if (RT_SUCCESS(rc))
            pUniStr1 = &UniStrLong1;
    }

    UNICODE_STRING UniStrLong2 = { 0, 0, NULL };
    if (RTNtPathFindPossible8dot3Name(pUniStr2->Buffer))
    {
        int rc = RTNtPathExpand8dot3PathA(pUniStr2, false /*fPathOnly*/, &UniStrLong2);
        if (RT_SUCCESS(rc))
            pUniStr2 = &UniStrLong2;
    }

    /* Compare again. */
    bool fCompare = supHardNtVpAreUniStringsEqual(pUniStr1, pUniStr2);

    /* Clean up. */
    if (UniStrLong1.Buffer)
        RTUtf16Free(UniStrLong1.Buffer);
    if (UniStrLong2.Buffer)
        RTUtf16Free(UniStrLong2.Buffer);

    return fCompare;
}


/**
 * Records an additional memory region for an image.
 *
 * May trash pThis->abMemory.
 *
 * @returns VBox status code.
 * @retval  VINF_OBJECT_DESTROYED if we've unmapped the image (child
 *          purification only).
 * @param   pThis               The process scanning state structure.
 * @param   pImage              The new image structure.  Only the unicode name
 *                              buffer is valid (it's zero-terminated).
 * @param   pMemInfo            The memory information for the image.
 */
static int supHardNtVpNewImage(PSUPHNTVPSTATE pThis, PSUPHNTVPIMAGE pImage, PMEMORY_BASIC_INFORMATION pMemInfo)
{
    /*
     * If the filename or path contains short names, we have to get the long
     * path so that we will recognize the DLLs and their location.
     */
    int rc83Exp = VERR_IGNORED;
    PUNICODE_STRING pLongName = &pImage->Name.UniStr;
    if (RTNtPathFindPossible8dot3Name(pLongName->Buffer))
    {
        AssertCompile(sizeof(pThis->abMemory) > sizeof(pImage->Name));
        PUNICODE_STRING pTmp = (PUNICODE_STRING)pThis->abMemory;
        pTmp->MaximumLength = (USHORT)RT_MIN(_64K - 1, sizeof(pThis->abMemory) - sizeof(*pTmp)) - sizeof(RTUTF16);
        pTmp->Length = pImage->Name.UniStr.Length;
        pTmp->Buffer = (PRTUTF16)(pTmp + 1);
        memcpy(pTmp->Buffer, pLongName->Buffer, pLongName->Length + sizeof(RTUTF16));

        rc83Exp = RTNtPathExpand8dot3Path(pTmp, false /*fPathOnly*/);
        Assert(rc83Exp == VINF_SUCCESS);
        Assert(pTmp->Buffer[pTmp->Length / sizeof(RTUTF16)] == '\0');
        if (rc83Exp == VINF_SUCCESS)
            SUP_DPRINTF(("supHardNtVpNewImage: 8dot3 -> long: '%ls' -> '%ls'\n", pLongName->Buffer, pTmp->Buffer));
        else
            SUP_DPRINTF(("supHardNtVpNewImage: RTNtPathExpand8dot3Path returns %Rrc for '%ls' (-> '%ls')\n",
                         rc83Exp, pLongName->Buffer, pTmp->Buffer));

        pLongName = pTmp;
    }

    /*
     * Extract the final component.
     */
    RTUTF16   wc;
    unsigned  cwcDirName   = pLongName->Length / sizeof(WCHAR);
    PCRTUTF16 pwszFilename = &pLongName->Buffer[cwcDirName];
    while (   cwcDirName > 0
           && (wc = pwszFilename[-1]) != '\\'
           && wc != '/'
           && wc != ':')
    {
        pwszFilename--;
        cwcDirName--;
    }
    if (!*pwszFilename)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NO_IMAGE_MAPPING_NAME,
                                   "Empty filename (len=%u) for image at %p.", pLongName->Length, pMemInfo->BaseAddress);

    /*
     * Drop trailing slashes from the directory name.
     */
    while (   cwcDirName > 0
           && (   pLongName->Buffer[cwcDirName - 1] == '\\'
               || pLongName->Buffer[cwcDirName - 1] == '/'))
        cwcDirName--;

#ifndef VBOX_WITH_MINIMAL_HARDENING
    /*
     * Match it against known DLLs.
     */
    pImage->pszName    = NULL;
    pImage->enmImageId = kSupHNtVpImageId_Invalid;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aSupNtVpAllowedDlls); i++)
        if (supHardNtVpAreNamesEqual(g_aSupNtVpAllowedDlls[i].pszName, pwszFilename))
        {
            pImage->pszName    = g_aSupNtVpAllowedDlls[i].pszName;
            pImage->enmImageId = g_aSupNtVpAllowedDlls[i].enmImageId;
            pImage->fDll       = true;

# ifndef VBOX_PERMIT_VISUAL_STUDIO_PROFILING
            /* The directory name must match the one we've got for System32. */
            if (   (   cwcDirName * sizeof(WCHAR) != g_System32NtPath.UniStr.Length
                    || suplibHardenedMemComp(pLongName->Buffer, g_System32NtPath.UniStr.Buffer, cwcDirName * sizeof(WCHAR)) )
#  ifdef VBOX_PERMIT_MORE
                && (   pImage->pszName[0] != 'a'
                    || pImage->pszName[1] != 'c'
                    || !supHardViIsAppPatchDir(pLongName->Buffer, pLongName->Length / sizeof(WCHAR)) )
#  endif
                )
                return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NON_SYSTEM32_DLL,
                                           "Expected %ls to be loaded from %ls.",
                                           pLongName->Buffer, g_System32NtPath.UniStr.Buffer);
#  ifdef VBOX_PERMIT_MORE
            if (g_uNtVerCombined < SUP_NT_VER_W70 && i >= VBOX_PERMIT_MORE_FIRST_IDX)
                pImage->pszName = NULL; /* hard limit: user32.dll is unwanted prior to w7. */
#  endif

# endif /* VBOX_PERMIT_VISUAL_STUDIO_PROFILING */
            break;
        }
    if (!pImage->pszName)
#endif /* !VBOX_WITH_MINIMAL_HARDENING */
    {
        /*
         * Not a known DLL, is it a known executable?
         */
#ifdef VBOX_WITH_MINIMAL_HARDENING
        pImage->fDll = true;
#endif
        for (uint32_t i = 0; i < RT_ELEMENTS(g_apszSupNtVpAllowedVmExes); i++)
            if (supHardNtVpAreNamesEqual(g_apszSupNtVpAllowedVmExes[i], pwszFilename))
            {
#ifndef VBOX_WITH_MINIMAL_HARDENING
                pImage->pszName     = g_apszSupNtVpAllowedVmExes[i];
                pImage->enmImageId  = kSupHNtVpImageId_Executable;
#endif
                pImage->fDll        = false;
                break;
            }
    }
#ifndef VBOX_WITH_MINIMAL_HARDENING
    if (!pImage->pszName)
    {
        /*
         * Unknown image.
         *
         * We ignore these when performing limited self-purifications or limited
         * verifications, as these are for the initial process where we are not
         * able to limit the DLLs loaded so strictly.
         */
        if (   pThis->enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED
            || pThis->enmKind == SUPHARDNTVPKIND_LIMITED_VERIFY_ONLY)
        {
            SUP_DPRINTF(("supHardNtVpScanVirtualMemory: Ignoring unknown mem at %p LB %#zx (base %p) - '%ls'\n",
                         pMemInfo->BaseAddress, pMemInfo->RegionSize, pMemInfo->AllocationBase, pwszFilename));
            return VINF_OBJECT_DESTROYED;
        }

# ifdef IN_RING3
        /*
         * If we're in ring-3 and doing child purification, we can unmap the
         * offending DLL ...  but this could have interesting side effects.
         */
        if (   pMemInfo->AllocationBase == pMemInfo->BaseAddress
            && pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION)
        {
            SUP_DPRINTF(("supHardNtVpScanVirtualMemory: Unmapping image mem at %p (%p LB %#zx) - '%ls'\n",
                         pMemInfo->AllocationBase, pMemInfo->BaseAddress, pMemInfo->RegionSize, pwszFilename));
            NTSTATUS rcNt = NtUnmapViewOfSection(pThis->hProcess, pMemInfo->AllocationBase);
            if (NT_SUCCESS(rcNt))
                return VINF_OBJECT_DESTROYED;
            pThis->cFixes++;
            SUP_DPRINTF(("supHardNtVpScanVirtualMemory: NtUnmapViewOfSection(,%p) failed: %#x\n", pMemInfo->AllocationBase, rcNt));
        }
# endif

        /*
         * Fail. Produce a special error message if we can.
         */
        if (   pMemInfo->AllocationBase == pMemInfo->BaseAddress
            && (   supHardNtVpAreNamesEqual("sysfer.dll", pwszFilename)
                || supHardNtVpAreNamesEqual("sysfer32.dll", pwszFilename)
                || supHardNtVpAreNamesEqual("sysfer64.dll", pwszFilename)
                || supHardNtVpAreNamesEqual("sysfrethunk.dll", pwszFilename)) )
        {
            supHardNtVpSetInfo2(pThis, VERR_SUP_VP_SYSFER_DLL,
                                "Found %ls at %p - This is probably part of Symantec Endpoint Protection. \n"
                                "You or your admin need to add and exception to the Application and Device Control (ADC) "
                                "component (or disable it) to prevent ADC from injecting itself into the VirtualBox VM processes. "
                                "See http://www.symantec.com/connect/articles/creating-application-control-exclusions-symantec-endpoint-protection-121"
                                , pLongName->Buffer, pMemInfo->BaseAddress);
            return pThis->rcResult = VERR_SUP_VP_SYSFER_DLL; /* Try make sure this is what the user sees first! */
        }
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NOT_KNOWN_DLL_OR_EXE,
                                   "Unknown image file %ls at %p. (rc83Exp=%Rrc)",
                                   pLongName->Buffer, pMemInfo->BaseAddress, rc83Exp);
    }

    /*
     * Checks for multiple mappings of the same DLL but with different image file paths.
     */
    uint32_t i = pThis->cImages;
    while (i-- > 1)
        if (pImage->pszName == pThis->aImages[i].pszName)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_DUPLICATE_DLL_MAPPING,
                                       "Duplicate image entries for %s: %ls and %ls",
                                       pImage->pszName, pImage->Name.UniStr.Buffer, pThis->aImages[i].Name.UniStr.Buffer);
#endif /* !VBOX_WITH_MINIMAL_HARDENING */

    /*
     * Since it's a new image, we expect to be at the start of the mapping now.
     */
    if (pMemInfo->AllocationBase != pMemInfo->BaseAddress)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_IMAGE_MAPPING_BASE_ERROR,
                                   "Invalid AllocationBase/BaseAddress for " IMAGE_LOG_NAME_FMT ": %p vs %p.",
                                   IMAGE_LOG_NAME(pImage), pMemInfo->AllocationBase, pMemInfo->BaseAddress);

    /*
     * Check for size/rva overflow.
     */
    if (pMemInfo->RegionSize >= _2G)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_TOO_LARGE_REGION,
                                   "Region 0 of image " IMAGE_LOG_NAME_FMT " is too large: %p.",
                                   IMAGE_LOG_NAME(pImage), pMemInfo->RegionSize);

    /*
     * Fill in details from the memory info.
     */
    pImage->uImageBase = (uintptr_t)pMemInfo->AllocationBase;
    pImage->cbImage    = pMemInfo->RegionSize;
    pImage->pCacheEntry= NULL;
    pImage->cRegions   = 1;
    pImage->aRegions[0].uRva    = 0;
    pImage->aRegions[0].cb      = (uint32_t)pMemInfo->RegionSize;
    pImage->aRegions[0].fProt   = pMemInfo->Protect;

#ifndef VBOX_WITH_MINIMAL_HARDENING
    if (pImage->enmImageId == kSupHNtVpImageId_NtDll)
        pImage->fNtCreateSectionPatch = true;
    else if (pImage->enmImageId == kSupHNtVpImageId_AppSetSchema)
        pImage->fApiSetSchemaOnlySection1 = true; /** @todo Check the ApiSetMap field in the PEB. */
# ifdef VBOX_PERMIT_MORE
    else if (suplibHardenedStrCmp(pImage->pszName, "acres.dll") == 0)
        pImage->f32bitResourceDll = true;
# endif
#endif /* !VBOX_WITH_MINIMAL_HARDENING */

    return VINF_SUCCESS;
}


/**
 * Records an additional memory region for an image.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure.
 * @param   pImage              The image.
 * @param   pMemInfo            The memory information for the region.
 */
static int supHardNtVpAddRegion(PSUPHNTVPSTATE pThis, PSUPHNTVPIMAGE pImage, PMEMORY_BASIC_INFORMATION pMemInfo)
{
    /*
     * Make sure the base address matches.
     */
    if (pImage->uImageBase != (uintptr_t)pMemInfo->AllocationBase)
        return supHardNtVpSetInfo2(pThis, VERR_SUPLIB_NT_PROCESS_UNTRUSTED_3,
                                   "Base address mismatch for " IMAGE_LOG_NAME_FMT ": have %p, found %p for region %p LB %#zx.",
                                   IMAGE_LOG_NAME(pImage), pImage->uImageBase, pMemInfo->AllocationBase,
                                   pMemInfo->BaseAddress, pMemInfo->RegionSize);

    /*
     * Check for size and rva overflows.
     */
    uintptr_t uRva = (uintptr_t)pMemInfo->BaseAddress - pImage->uImageBase;
    if (pMemInfo->RegionSize >= _2G)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_TOO_LARGE_REGION,
                                   "Region %u of image " IMAGE_LOG_NAME_FMT " is too large: %p/%p.",
                                   IMAGE_LOG_NAME(pImage), pMemInfo->RegionSize, uRva);
    if (uRva >= _2G)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_TOO_HIGH_REGION_RVA,
                                   "Region %u of image " IMAGE_LOG_NAME_FMT " is too high: %p/%p.",
                                   IMAGE_LOG_NAME(pImage), pMemInfo->RegionSize, uRva);


    /*
     * Record the region.
     */
    uint32_t iRegion = pImage->cRegions;
    if (iRegion + 1 >= RT_ELEMENTS(pImage->aRegions))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_TOO_MANY_IMAGE_REGIONS,
                                   "Too many regions for " IMAGE_LOG_NAME_FMT ".", IMAGE_LOG_NAME(pImage));
    pImage->aRegions[iRegion].uRva  = (uint32_t)uRva;
    pImage->aRegions[iRegion].cb    = (uint32_t)pMemInfo->RegionSize;
    pImage->aRegions[iRegion].fProt = pMemInfo->Protect;
    pImage->cbImage = pImage->aRegions[iRegion].uRva + pImage->aRegions[iRegion].cb;
    pImage->cRegions++;
    pImage->fApiSetSchemaOnlySection1 = false;

    return VINF_SUCCESS;
}

#ifdef IN_RING3

/** Helper for supHardNtVpFreeOrReplacePrivateExecMemory(). */
static int supHardNtVpFreeOrReplacePrivateExecMemoryFailed(HANDLE hProcess, void *pvCopy)
{
    supR3HardenedLogFlush();
    NtTerminateProcess(hProcess, VERR_SUP_VP_REPLACE_VIRTUAL_MEMORY_FAILED);
    if (pvCopy)
        RTMemFree(pvCopy);
    return VERR_SUP_VP_REPLACE_VIRTUAL_MEMORY_FAILED;
}


/**
 * Frees (or replaces) executable memory of allocation type private.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure. Details
 *                              about images are added to this.
 * @param   hProcess            The process to verify.
 * @param   pMemInfo            The information we've got on this private
 *                              executable memory.
 */
static int supHardNtVpFreeOrReplacePrivateExecMemory(PSUPHNTVPSTATE pThis, HANDLE hProcess,
                                                     MEMORY_BASIC_INFORMATION const *pMemInfo)
{
    NTSTATUS rcNt;

    /*
     * Try figure the entire allocation size. Free/Alloc may fail otherwise.
     */
    PVOID   pvFree = pMemInfo->AllocationBase;
    SIZE_T  cbFree = pMemInfo->RegionSize + ((uintptr_t)pMemInfo->BaseAddress - (uintptr_t)pMemInfo->AllocationBase);
    for (;;)
    {
        SIZE_T                      cbActual = 0;
        MEMORY_BASIC_INFORMATION    MemInfo2 = { 0, 0, 0, 0, 0, 0, 0 };
        uintptr_t                   uPtrNext = (uintptr_t)pvFree + cbFree;
        rcNt = g_pfnNtQueryVirtualMemory(hProcess,
                                         (void const *)uPtrNext,
                                         MemoryBasicInformation,
                                         &MemInfo2,
                                         sizeof(MemInfo2),
                                         &cbActual);
        if (!NT_SUCCESS(rcNt))
            break;
        if (pMemInfo->AllocationBase != MemInfo2.AllocationBase)
            break;
        if (MemInfo2.RegionSize == 0)
            break;
        cbFree += MemInfo2.RegionSize;
    }
    SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: %s exec mem at %p LB %#zx ([%p]/%p LB %#zx s=%#x ap=%#x rp=%#x t=%#x)\n",
                 pThis->fFlags & SUPHARDNTVP_F_EXEC_ALLOC_REPLACE_WITH_RW ? "Replacing" : "Freeing",
                 pvFree, cbFree, pMemInfo->AllocationBase, pMemInfo->BaseAddress, pMemInfo->RegionSize, pMemInfo->State,
                 pMemInfo->AllocationProtect, pMemInfo->Protect, pMemInfo->Type));

    /*
     * In the BSOD workaround mode, we need to make a copy of the memory before
     * freeing it.  Bird abuses this code for logging purposes too.
     */
    uintptr_t   uCopySrc  = (uintptr_t)pvFree;
    size_t      cbCopy    = 0;
    void       *pvCopy    = NULL;
    //if (pThis->fFlags & SUPHARDNTVP_F_EXEC_ALLOC_REPLACE_WITH_RW)
    {
        cbCopy = cbFree;
        pvCopy = RTMemAllocZ(cbCopy);
        if (!pvCopy)
        {
            supHardNtVpSetInfo2(pThis, VERR_SUP_VP_REPLACE_VIRTUAL_MEMORY_FAILED, "RTMemAllocZ(%#zx) failed", cbCopy);
            return supHardNtVpFreeOrReplacePrivateExecMemoryFailed(hProcess, NULL);
        }

        rcNt = supHardNtVpReadMem(hProcess, uCopySrc, pvCopy, cbCopy);
        if (!NT_SUCCESS(rcNt))
            supHardNtVpSetInfo2(pThis, VERR_SUP_VP_REPLACE_VIRTUAL_MEMORY_FAILED,
                                "Error reading data from original alloc: %#x (%p LB %#zx)", rcNt, uCopySrc, cbCopy, rcNt);
        for (size_t off = 0; off < cbCopy; off += 256)
        {
            size_t const cbChunk = RT_MIN(256, cbCopy - off);
            void const  *pvChunk = (uint8_t const *)pvCopy + off;
            if (!ASMMemIsZero(pvChunk, cbChunk))
                SUP_DPRINTF(("%.*RhxD\n", cbChunk, pvChunk));
        }
        if (pThis->fFlags & SUPHARDNTVP_F_EXEC_ALLOC_REPLACE_WITH_RW)
            supR3HardenedLogFlush();
    }

    /*
     * Free the memory.
     */
    for (uint32_t i = 0; i < 10; i++)
    {
        PVOID  pvFreeInOut = pvFree;
        SIZE_T cbFreeInOut = 0;
        rcNt = NtFreeVirtualMemory(hProcess, &pvFreeInOut, &cbFreeInOut, MEM_RELEASE);
        if (NT_SUCCESS(rcNt))
        {
            SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: Free attempt #1 succeeded: %#x [%p/%p LB 0/%#zx]\n",
                         rcNt, pvFree, pvFreeInOut, cbFreeInOut));
            supR3HardenedLogFlush();
        }
        else
        {
            SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: Free attempt #1 failed: %#x [%p LB 0]\n", rcNt, pvFree));
            supR3HardenedLogFlush();
            pvFreeInOut = pvFree;
            cbFreeInOut = cbFree;
            rcNt = NtFreeVirtualMemory(hProcess, &pvFreeInOut, &cbFreeInOut, MEM_RELEASE);
            if (NT_SUCCESS(rcNt))
            {
                SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: Free attempt #2 succeeded: %#x [%p/%p LB %#zx/%#zx]\n",
                             rcNt, pvFree, pvFreeInOut, cbFree, cbFreeInOut));
                supR3HardenedLogFlush();
            }
            else
            {
                SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: Free attempt #2 failed: %#x [%p LB %#zx]\n",
                             rcNt, pvFree, cbFree));
                supR3HardenedLogFlush();
                pvFreeInOut = pMemInfo->BaseAddress;
                cbFreeInOut = pMemInfo->RegionSize;
                rcNt = NtFreeVirtualMemory(hProcess, &pvFreeInOut, &cbFreeInOut, MEM_RELEASE);
                if (NT_SUCCESS(rcNt))
                {
                    pvFree = pMemInfo->BaseAddress;
                    cbFree = pMemInfo->RegionSize;
                    SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: Free attempt #3 succeeded [%p LB %#zx]\n",
                                 pvFree, cbFree));
                    supR3HardenedLogFlush();
                }
                else
                    supHardNtVpSetInfo2(pThis, VERR_SUP_VP_FREE_VIRTUAL_MEMORY_FAILED,
                                        "NtFreeVirtualMemory [%p LB %#zx and %p LB %#zx] failed: %#x",
                                        pvFree, cbFree, pMemInfo->BaseAddress, pMemInfo->RegionSize, rcNt);
            }
        }

        /*
         * Query the region again, redo the free operation if there's still memory there.
         */
        if (!NT_SUCCESS(rcNt))
            break;
        SIZE_T                      cbActual = 0;
        MEMORY_BASIC_INFORMATION    MemInfo3 = { 0, 0, 0, 0, 0, 0, 0 };
        NTSTATUS rcNt2 = g_pfnNtQueryVirtualMemory(hProcess, pvFree, MemoryBasicInformation,
                                                   &MemInfo3, sizeof(MemInfo3), &cbActual);
        if (!NT_SUCCESS(rcNt2))
            break;
        SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: QVM after free %u: [%p]/%p LB %#zx s=%#x ap=%#x rp=%#x t=%#x\n",
                     i, MemInfo3.AllocationBase, MemInfo3.BaseAddress, MemInfo3.RegionSize, MemInfo3.State,
                     MemInfo3.AllocationProtect, MemInfo3.Protect, MemInfo3.Type));
        supR3HardenedLogFlush();
        if (MemInfo3.State == MEM_FREE || !(pThis->fFlags & SUPHARDNTVP_F_EXEC_ALLOC_REPLACE_WITH_RW))
            break;
        NtYieldExecution();
        SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: Retrying free...\n"));
        supR3HardenedLogFlush();
    }

    /*
     * Restore memory as non-executable - Kludge for Trend Micro sakfile.sys
     * and Digital Guardian dgmaster.sys BSODs.
     */
    if (NT_SUCCESS(rcNt) && (pThis->fFlags & SUPHARDNTVP_F_EXEC_ALLOC_REPLACE_WITH_RW))
    {
        PVOID  pvAlloc = pvFree;
        SIZE_T cbAlloc = cbFree;
        rcNt = NtAllocateVirtualMemory(hProcess, &pvAlloc, 0, &cbAlloc, MEM_COMMIT, PAGE_READWRITE);
        if (!NT_SUCCESS(rcNt))
        {
            supHardNtVpSetInfo2(pThis, VERR_SUP_VP_REPLACE_VIRTUAL_MEMORY_FAILED,
                                "NtAllocateVirtualMemory (%p LB %#zx) failed with rcNt=%#x allocating "
                                "replacement memory for working around buggy protection software. "
                                "See VBoxHardening.log for more details",
                                pvAlloc, cbFree, rcNt);
            return supHardNtVpFreeOrReplacePrivateExecMemoryFailed(hProcess, pvCopy);
        }

        if (   (uintptr_t)pvFree < (uintptr_t)pvAlloc
            || (uintptr_t)pvFree + cbFree > (uintptr_t)pvAlloc + cbFree)
        {
            supHardNtVpSetInfo2(pThis, VERR_SUP_VP_REPLACE_VIRTUAL_MEMORY_FAILED,
                                "We wanted NtAllocateVirtualMemory to get us %p LB %#zx, but it returned %p LB %#zx.",
                                pMemInfo->BaseAddress, pMemInfo->RegionSize, pvFree, cbFree, rcNt);
            return supHardNtVpFreeOrReplacePrivateExecMemoryFailed(hProcess, pvCopy);
        }

        /*
         * Copy what we can, considering the 2nd free attempt.
         */
        uint8_t *pbDst = (uint8_t *)pvFree;
        size_t   cbDst = cbFree;
        uint8_t *pbSrc = (uint8_t *)pvCopy;
        size_t   cbSrc = cbCopy;
        if ((uintptr_t)pbDst != uCopySrc)
        {
            if ((uintptr_t)pbDst > uCopySrc)
            {
                uintptr_t cbAdj = (uintptr_t)pbDst - uCopySrc;
                pbSrc += cbAdj;
                cbSrc -= cbAdj;
            }
            else
            {
                uintptr_t cbAdj = uCopySrc - (uintptr_t)pbDst;
                pbDst += cbAdj;
                cbDst -= cbAdj;
            }
        }
        if (cbSrc > cbDst)
            cbSrc = cbDst;

        SIZE_T cbWritten;
        rcNt = NtWriteVirtualMemory(hProcess, pbDst, pbSrc, cbSrc, &cbWritten);
        if (NT_SUCCESS(rcNt))
        {
            SUP_DPRINTF(("supHardNtVpFreeOrReplacePrivateExecMemory: Restored the exec memory as non-exec.\n"));
            supR3HardenedLogFlush();
        }
        else
        {
            supHardNtVpSetInfo2(pThis, VERR_SUP_VP_FREE_VIRTUAL_MEMORY_FAILED, "NtWriteVirtualMemory (%p LB %#zx) failed: %#x",
                                pMemInfo->BaseAddress, pMemInfo->RegionSize, rcNt);
            return supHardNtVpFreeOrReplacePrivateExecMemoryFailed(hProcess, pvCopy);
        }
    }
    if (pvCopy)
        RTMemFree(pvCopy);
    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * Scans the virtual memory of the process.
 *
 * This collects the locations of DLLs and the EXE, and verifies that executable
 * memory is only associated with these.  May trash pThis->abMemory.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure. Details
 *                              about images are added to this.
 * @param   hProcess            The process to verify.
 */
static int supHardNtVpScanVirtualMemory(PSUPHNTVPSTATE pThis, HANDLE hProcess)
{
    SUP_DPRINTF(("supHardNtVpScanVirtualMemory: enmKind=%s\n",
                 pThis->enmKind == SUPHARDNTVPKIND_VERIFY_ONLY ? "VERIFY_ONLY" :
                 pThis->enmKind == SUPHARDNTVPKIND_LIMITED_VERIFY_ONLY ? "LIMITED_VERIFY_ONLY" :
                 pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION ? "CHILD_PURIFICATION" : "SELF_PURIFICATION"));

    uint32_t    cXpExceptions = 0;
    uintptr_t   cbAdvance = 0;
    uintptr_t   uPtrWhere = 0;
#if defined(VBOX_PERMIT_VERIFIER_DLL) || defined(VBOX_WITH_MINIMAL_HARDENING)
    for (uint32_t i = 0; i < 10240; i++)
#else
    for (uint32_t i = 0; i < 1024; i++)
#endif
    {
        SIZE_T                      cbActual = 0;
        MEMORY_BASIC_INFORMATION    MemInfo  = { 0, 0, 0, 0, 0, 0, 0 };
        NTSTATUS rcNt = g_pfnNtQueryVirtualMemory(hProcess,
                                                  (void const *)uPtrWhere,
                                                  MemoryBasicInformation,
                                                  &MemInfo,
                                                  sizeof(MemInfo),
                                                  &cbActual);
        if (!NT_SUCCESS(rcNt))
        {
            if (rcNt == STATUS_INVALID_PARAMETER)
                return pThis->rcResult;
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NT_QI_VIRTUAL_MEMORY_ERROR,
                                       "NtQueryVirtualMemory failed for %p: %#x", uPtrWhere, rcNt);
        }

        /*
         * Record images.
         */
        if (   MemInfo.Type == SEC_IMAGE
            || MemInfo.Type == SEC_PROTECTED_IMAGE
            || MemInfo.Type == (SEC_IMAGE | SEC_PROTECTED_IMAGE))
        {
            uint32_t iImg = pThis->cImages;
            rcNt = g_pfnNtQueryVirtualMemory(hProcess,
                                             (void const *)uPtrWhere,
                                             MemorySectionName,
                                             &pThis->aImages[iImg].Name,
                                             sizeof(pThis->aImages[iImg].Name) - sizeof(WCHAR),
                                             &cbActual);
            if (!NT_SUCCESS(rcNt))
                return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NT_QI_VIRTUAL_MEMORY_NM_ERROR,
                                           "NtQueryVirtualMemory/MemorySectionName failed for %p: %#x", uPtrWhere, rcNt);
            pThis->aImages[iImg].Name.UniStr.Buffer[pThis->aImages[iImg].Name.UniStr.Length / sizeof(WCHAR)] = '\0';
            SUP_DPRINTF((MemInfo.AllocationBase == MemInfo.BaseAddress
                         ? " *%p-%p %#06x/%#06x %#09x  %ls\n"
                         : "  %p-%p %#06x/%#06x %#09x  %ls\n",
                         MemInfo.BaseAddress, (uintptr_t)MemInfo.BaseAddress + MemInfo.RegionSize - 1, MemInfo.Protect,
                         MemInfo.AllocationProtect, MemInfo.Type, pThis->aImages[iImg].Name.UniStr.Buffer));

            /* New or existing image? */
            bool fNew = true;
            uint32_t iSearch = iImg;
            while (iSearch-- > 0)
                if (supHardNtVpAreUniStringsEqual(&pThis->aImages[iSearch].Name.UniStr, &pThis->aImages[iImg].Name.UniStr))
                {
                    int rc = supHardNtVpAddRegion(pThis, &pThis->aImages[iSearch], &MemInfo);
                    if (RT_FAILURE(rc))
                        return rc;
                    fNew = false;
                    break;
                }
                else if (pThis->aImages[iSearch].uImageBase == (uintptr_t)MemInfo.AllocationBase)
                    return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NT_MAPPING_NAME_CHANGED,
                                               "Unexpected base address match");

            if (fNew)
            {
#ifdef VBOX_WITH_MINIMAL_HARDENING
                /* Prune non-executable images before trying to add another. We only want the executable. */
                if (iImg > 0 && pThis->aImages[iImg - 1].fDll)
                {
                    pThis->cImages = --iImg;
                    RT_ZERO(pThis->aImages[iImg]);
                    pThis->aImages[iImg].Name = pThis->aImages[iImg + 1].Name;
                    pThis->aImages[iImg].Name.UniStr.Buffer = pThis->aImages[iImg].Name.awcBuffer;
                }
#endif
                int rc = supHardNtVpNewImage(pThis, &pThis->aImages[iImg], &MemInfo);
                if (RT_SUCCESS(rc))
                {
                    if (rc != VINF_OBJECT_DESTROYED)
                    {
                        pThis->cImages++;
                        if (pThis->cImages >= RT_ELEMENTS(pThis->aImages))
                            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_TOO_MANY_DLLS_LOADED,
                                                       "Internal error: aImages is full.\n");
                    }
                }
#ifdef IN_RING3 /* Continue and add more information if unknown DLLs are found. */
                else if (rc != VERR_SUP_VP_NOT_KNOWN_DLL_OR_EXE && rc != VERR_SUP_VP_NON_SYSTEM32_DLL)
                    return rc;
#else
                else
                    return rc;
#endif
            }
        }
        /*
         * XP, W2K3: Ignore the CSRSS read-only region as best we can.
         */
        else if (      (MemInfo.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                    == PAGE_EXECUTE_READ
                 && cXpExceptions == 0
                 && (uintptr_t)MemInfo.BaseAddress >= UINT32_C(0x78000000)
                 /* && MemInfo.BaseAddress == pPeb->ReadOnlySharedMemoryBase */
                 && g_uNtVerCombined < SUP_MAKE_NT_VER_SIMPLE(6, 0) )
        {
            cXpExceptions++;
            SUP_DPRINTF(("  %p-%p %#06x/%#06x %#09x  XP CSRSS read-only region\n", MemInfo.BaseAddress,
                         (uintptr_t)MemInfo.BaseAddress + MemInfo.RegionSize - 1, MemInfo.Protect,
                         MemInfo.AllocationProtect, MemInfo.Type));
        }
        /*
         * Executable memory?
         */
#ifndef VBOX_PERMIT_VISUAL_STUDIO_PROFILING
        else if (MemInfo.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
        {
            SUP_DPRINTF((MemInfo.AllocationBase == MemInfo.BaseAddress
                         ? " *%p-%p %#06x/%#06x %#09x !!\n"
                         : "  %p-%p %#06x/%#06x %#09x !!\n",
                         MemInfo.BaseAddress, (uintptr_t)MemInfo.BaseAddress + MemInfo.RegionSize - 1,
                         MemInfo.Protect, MemInfo.AllocationProtect, MemInfo.Type));
# ifdef IN_RING3
            if (pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION)
            {
                /*
                 * Free any private executable memory (sysplant.sys allocates executable memory).
                 */
                if (MemInfo.Type == MEM_PRIVATE)
                {
                    int const rc = supHardNtVpFreeOrReplacePrivateExecMemory(pThis, hProcess, &MemInfo);
                    if (RT_FAILURE(rc))
                        return rc;
                }
                /*
                 * Unmap mapped memory, failing that, drop exec privileges.
                 */
                else if (MemInfo.Type == MEM_MAPPED)
                {
                    SUP_DPRINTF(("supHardNtVpScanVirtualMemory: Unmapping exec mem at %p (%p/%p LB %#zx)\n",
                                 uPtrWhere, MemInfo.AllocationBase, MemInfo.BaseAddress, MemInfo.RegionSize));
                    rcNt = NtUnmapViewOfSection(hProcess, MemInfo.AllocationBase);
                    if (!NT_SUCCESS(rcNt))
                    {
                        PVOID  pvCopy = MemInfo.BaseAddress;
                        SIZE_T cbCopy = MemInfo.RegionSize;
                        NTSTATUS rcNt2 = NtProtectVirtualMemory(hProcess, &pvCopy, &cbCopy, PAGE_NOACCESS, NULL);
                        if (!NT_SUCCESS(rcNt2))
                            rcNt2 = NtProtectVirtualMemory(hProcess, &pvCopy, &cbCopy, PAGE_READONLY, NULL);
                        if (!NT_SUCCESS(rcNt2))
                            supHardNtVpSetInfo2(pThis, VERR_SUP_VP_UNMAP_AND_PROTECT_FAILED,
                                                "NtUnmapViewOfSection (%p/%p LB %#zx) failed: %#x (%#x)",
                                                MemInfo.AllocationBase, MemInfo.BaseAddress, MemInfo.RegionSize, rcNt, rcNt2);
                    }
                }
                else
                    supHardNtVpSetInfo2(pThis, VERR_SUP_VP_UNKOWN_MEM_TYPE,
                                        "Unknown executable memory type %#x at %p/%p LB %#zx",
                                        MemInfo.Type, MemInfo.AllocationBase, MemInfo.BaseAddress, MemInfo.RegionSize);
                pThis->cFixes++;
            }
            else if (   pThis->enmKind != SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED
                     && pThis->enmKind != SUPHARDNTVPKIND_LIMITED_VERIFY_ONLY)
# endif /* IN_RING3 */
                supHardNtVpSetInfo2(pThis, VERR_SUP_VP_FOUND_EXEC_MEMORY,
                                    "Found executable memory at %p (%p LB %#zx): type=%#x prot=%#x state=%#x aprot=%#x abase=%p",
                                    uPtrWhere, MemInfo.BaseAddress, MemInfo.RegionSize, MemInfo.Type, MemInfo.Protect,
                                    MemInfo.State, MemInfo.AllocationProtect, MemInfo.AllocationBase);

# ifndef IN_RING3
            if (RT_FAILURE(pThis->rcResult))
                return pThis->rcResult;
# endif
            /* Continue add more information about the problematic process. */
        }
#endif /* VBOX_PERMIT_VISUAL_STUDIO_PROFILING */
        else
            SUP_DPRINTF((MemInfo.AllocationBase == MemInfo.BaseAddress
                         ? " *%p-%p %#06x/%#06x %#09x\n"
                         : "  %p-%p %#06x/%#06x %#09x\n",
                         MemInfo.BaseAddress, (uintptr_t)MemInfo.BaseAddress + MemInfo.RegionSize - 1,
                         MemInfo.Protect, MemInfo.AllocationProtect, MemInfo.Type));

        /*
         * Advance.
         */
        cbAdvance = MemInfo.RegionSize;
        if (uPtrWhere + cbAdvance <= uPtrWhere)
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EMPTY_REGION_TOO_LARGE,
                                       "Empty region at %p.", uPtrWhere);
        uPtrWhere += MemInfo.RegionSize;
    }

    return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_TOO_MANY_MEMORY_REGIONS,
                               "Too many virtual memory regions.\n");
}


/**
 * Verifies the loader image, i.e. check cryptographic signatures if present.
 *
 * @returns VBox status code.
 * @param   pEntry              The loader cache entry.
 * @param   pwszName            The filename to use in error messages.
 * @param   pErrInfo            Where to return extened error information.
 */
DECLHIDDEN(int) supHardNtLdrCacheEntryVerify(PSUPHNTLDRCACHEENTRY pEntry, PCRTUTF16 pwszName, PRTERRINFO pErrInfo)
{
    int rc = VINF_SUCCESS;
    if (!pEntry->fVerified)
    {
        rc = supHardenedWinVerifyImageByLdrMod(pEntry->hLdrMod, pwszName, pEntry->pNtViRdr,
                                               false /*fAvoidWinVerifyTrust*/, NULL /*pfWinVerifyTrust*/, pErrInfo);
        pEntry->fVerified = RT_SUCCESS(rc);
    }
    return rc;
}


/**
 * Allocates a image bits buffer and calls RTLdrGetBits on them.
 *
 * An assumption here is that there won't ever be concurrent use of the cache.
 * It's currently 104% single threaded, non-reentrant.  Thus, we can't reuse the
 * pbBits allocation.
 *
 * @returns VBox status code
 * @param   pEntry              The loader cache entry.
 * @param   ppbBits             Where to return the pointer to the allocation.
 * @param   uBaseAddress        The image base address, see RTLdrGetBits.
 * @param   pfnGetImport        Import getter, see RTLdrGetBits.
 * @param   pvUser              The user argument for @a pfnGetImport.
 * @param   pErrInfo            Where to return extened error information.
 */
DECLHIDDEN(int) supHardNtLdrCacheEntryGetBits(PSUPHNTLDRCACHEENTRY pEntry, uint8_t **ppbBits,
                                              RTLDRADDR uBaseAddress, PFNRTLDRIMPORT pfnGetImport, void *pvUser,
                                              PRTERRINFO pErrInfo)
{
    RT_STACK_CHECK_RET_ADDR();
    int rc;

    /*
     * First time around we have to allocate memory before we can get the image bits.
     */
    if (!pEntry->pbBits)
    {
        size_t cbBits = RTLdrSize(pEntry->hLdrMod);
        if (cbBits >= _1M*32U)
            return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_IMAGE_TOO_BIG, "Image %s is too large: %zu bytes (%#zx).",
                                       pEntry->pszName, cbBits, cbBits);

        pEntry->pbBits = (uint8_t *)RTMemAllocZ(cbBits);
        if (!pEntry->pbBits)
            return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_NO_MEMORY, "Failed to allocate %zu bytes for image %s.",
                                       cbBits, pEntry->pszName);

        pEntry->fValidBits = false; /* paranoia */

        rc = RTLdrGetBits(pEntry->hLdrMod, pEntry->pbBits, uBaseAddress, pfnGetImport, pvUser);
        if (RT_FAILURE(rc))
            return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_NO_MEMORY, "RTLdrGetBits failed on image %s: %Rrc",
                                       pEntry->pszName, rc);
        pEntry->uImageBase = uBaseAddress;
        pEntry->fValidBits = pfnGetImport == NULL;

    }
    /*
     * Cache hit? No?
     *
     * Note! We cannot currently cache image bits for images with imports as we
     *       don't control the way they're resolved.  Fortunately, NTDLL and
     *       the VM process images all have no imports.
     */
    else if (   !pEntry->fValidBits
             || pEntry->uImageBase != uBaseAddress
             || pfnGetImport)
    {
        pEntry->fValidBits = false;

        rc = RTLdrGetBits(pEntry->hLdrMod, pEntry->pbBits, uBaseAddress, pfnGetImport, pvUser);
        if (RT_FAILURE(rc))
            return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_NO_MEMORY, "RTLdrGetBits failed on image %s: %Rrc",
                                       pEntry->pszName, rc);
        pEntry->uImageBase = uBaseAddress;
        pEntry->fValidBits = pfnGetImport == NULL;
    }

    *ppbBits = pEntry->pbBits;
    return VINF_SUCCESS;
}


/**
 * Frees all resources associated with a cache entry and wipes the members
 * clean.
 *
 * @param   pEntry              The entry to delete.
 */
static void supHardNTLdrCacheDeleteEntry(PSUPHNTLDRCACHEENTRY pEntry)
{
    if (pEntry->pbBits)
    {
        RTMemFree(pEntry->pbBits);
        pEntry->pbBits = NULL;
    }

    if (pEntry->hLdrMod != NIL_RTLDRMOD)
    {
        RTLdrClose(pEntry->hLdrMod);
        pEntry->hLdrMod = NIL_RTLDRMOD;
        pEntry->pNtViRdr = NULL;
    }
    else if (pEntry->pNtViRdr)
    {
        pEntry->pNtViRdr->Core.pfnDestroy(&pEntry->pNtViRdr->Core);
        pEntry->pNtViRdr = NULL;
    }

    if (pEntry->hFile)
    {
        NtClose(pEntry->hFile);
        pEntry->hFile = NULL;
    }

    pEntry->pszName    = NULL;
    pEntry->fVerified  = false;
    pEntry->fValidBits = false;
    pEntry->uImageBase = 0;
}

#ifdef IN_RING3

/**
 * Flushes the cache.
 *
 * This is called from one of two points in the hardened main code, first is
 * after respawning and the second is when we open the vboxdrv device for
 * unrestricted access.
 */
DECLHIDDEN(void) supR3HardenedWinFlushLoaderCache(void)
{
    uint32_t i = g_cSupNtVpLdrCacheEntries;
    while (i-- > 0)
        supHardNTLdrCacheDeleteEntry(&g_aSupNtVpLdrCacheEntries[i]);
    g_cSupNtVpLdrCacheEntries = 0;
}


/**
 * Searches the cache for a loader image.
 *
 * @returns Pointer to the cache entry if found, NULL if not.
 * @param   pszName             The name (from g_apszSupNtVpAllowedVmExes or
 *                              g_aSupNtVpAllowedDlls).
 */
static PSUPHNTLDRCACHEENTRY supHardNtLdrCacheLookupEntry(const char *pszName)
{
    /*
     * Since the caller is supplying us a pszName from one of the two tables,
     * we can dispense with string compare and simply compare string pointers.
     */
    uint32_t i = g_cSupNtVpLdrCacheEntries;
    while (i-- > 0)
        if (g_aSupNtVpLdrCacheEntries[i].pszName == pszName)
            return &g_aSupNtVpLdrCacheEntries[i];
    return NULL;
}

#endif /* IN_RING3 */

static int supHardNtLdrCacheNewEntry(PSUPHNTLDRCACHEENTRY pEntry, const char *pszName, PUNICODE_STRING pUniStrPath,
                                     bool fDll, bool f32bitResourceDll, PRTERRINFO pErrInfo)
{
    /*
     * Open the image file.
     */
    HANDLE              hFile = RTNT_INVALID_HANDLE_VALUE;
    IO_STATUS_BLOCK     Ios   = RTNT_IO_STATUS_BLOCK_INITIALIZER;

    OBJECT_ATTRIBUTES   ObjAttr;
    InitializeObjectAttributes(&ObjAttr, pUniStrPath, OBJ_CASE_INSENSITIVE, NULL /*hRootDir*/, NULL /*pSecDesc*/);
#ifdef IN_RING0
    ObjAttr.Attributes |= OBJ_KERNEL_HANDLE;
#endif

    NTSTATUS rcNt = NtCreateFile(&hFile,
                                 GENERIC_READ | SYNCHRONIZE,
                                 &ObjAttr,
                                 &Ios,
                                 NULL /* Allocation Size*/,
                                 FILE_ATTRIBUTE_NORMAL,
                                 FILE_SHARE_READ,
                                 FILE_OPEN,
                                 FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                                 NULL /*EaBuffer*/,
                                 0 /*EaLength*/);
    if (NT_SUCCESS(rcNt))
        rcNt = Ios.Status;
    if (!NT_SUCCESS(rcNt))
        return supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_IMAGE_FILE_OPEN_ERROR,
                                   "Error opening image for scanning: %#x (name %ls)", rcNt, pUniStrPath->Buffer);

    /*
     * Figure out validation flags we'll be using and create the reader
     * for this image.
     */
    uint32_t fFlags = fDll
                    ? SUPHNTVI_F_TRUSTED_INSTALLER_OR_SIMILAR_OWNER | SUPHNTVI_F_ALLOW_CAT_FILE_VERIFICATION
                    : SUPHNTVI_F_REQUIRE_BUILD_CERT;
    if (f32bitResourceDll)
        fFlags |= SUPHNTVI_F_IGNORE_ARCHITECTURE;

    PSUPHNTVIRDR pNtViRdr;
    int rc = supHardNtViRdrCreate(hFile, pUniStrPath->Buffer, fFlags, &pNtViRdr);
    if (RT_FAILURE(rc))
    {
        NtClose(hFile);
        return rc;
    }

    /*
     * Finally, open the image with the loader
     */
    RTLDRMOD hLdrMod;
    RTLDRARCH enmArch = fFlags & SUPHNTVI_F_RC_IMAGE ? RTLDRARCH_X86_32 : RTLDRARCH_HOST;
    if (fFlags & SUPHNTVI_F_IGNORE_ARCHITECTURE)
        enmArch = RTLDRARCH_WHATEVER;
    rc = RTLdrOpenWithReader(&pNtViRdr->Core, RTLDR_O_FOR_VALIDATION, enmArch, &hLdrMod, pErrInfo);
    if (RT_FAILURE(rc))
        return supHardNtVpAddInfo1(pErrInfo, rc, "RTLdrOpenWithReader failed: %Rrc (Image='%ls').",
                                   rc, pUniStrPath->Buffer);

    /*
     * Fill in the cache entry.
     */
    pEntry->pszName    = pszName;
    pEntry->hLdrMod    = hLdrMod;
    pEntry->pNtViRdr   = pNtViRdr;
    pEntry->hFile      = hFile;
    pEntry->pbBits     = NULL;
    pEntry->fVerified  = false;
    pEntry->fValidBits = false;
    pEntry->uImageBase = ~(uintptr_t)0;

#ifdef IN_SUP_HARDENED_R3
    /*
     * Log the image timestamp when in the hardened exe.
     */
    uint64_t uTimestamp = 0;
    rc = RTLdrQueryProp(hLdrMod, RTLDRPROP_TIMESTAMP_SECONDS, &uTimestamp, sizeof(uint64_t));
    SUP_DPRINTF(("%s: timestamp %#llx (rc=%Rrc)\n", pszName, uTimestamp, rc));
#endif

    return VINF_SUCCESS;
}

#ifdef IN_RING3
/**
 * Opens a loader cache entry.
 *
 * Currently this is only used by the import code for getting NTDLL.
 *
 * @returns VBox status code.
 * @param   pszName             The DLL name.  Must be one from the
 *                              g_aSupNtVpAllowedDlls array.
 * @param   ppEntry             Where to return the entry we've opened/found.
 * @param   pErrInfo            Optional buffer where to return additional error
 *                              information.
 */
DECLHIDDEN(int) supHardNtLdrCacheOpen(const char *pszName, PSUPHNTLDRCACHEENTRY *ppEntry, PRTERRINFO pErrInfo)
{
    RT_STACK_CHECK_RET_ADDR();

    /*
     * Locate the dll.
     */
    uint32_t i = 0;
    while (   i < RT_ELEMENTS(g_aSupNtVpAllowedDlls)
           && strcmp(pszName, g_aSupNtVpAllowedDlls[i].pszName))
        i++;
    if (i >= RT_ELEMENTS(g_aSupNtVpAllowedDlls))
        return VERR_FILE_NOT_FOUND;
    pszName = g_aSupNtVpAllowedDlls[i].pszName;

    /*
     * Try the cache.
     */
    *ppEntry = supHardNtLdrCacheLookupEntry(pszName);
    if (*ppEntry)
        return VINF_SUCCESS;

    /*
     * Not in the cache, so open it.
     * Note! We cannot assume that g_System32NtPath has been initialized at this point.
     */
    if (g_cSupNtVpLdrCacheEntries >= RT_ELEMENTS(g_aSupNtVpLdrCacheEntries))
        return VERR_INTERNAL_ERROR_3;

    static WCHAR s_wszSystem32[] = L"\\SystemRoot\\System32\\";
    WCHAR wszPath[64];
    memcpy(wszPath, s_wszSystem32, sizeof(s_wszSystem32));
    RTUtf16CatAscii(wszPath, sizeof(wszPath), pszName);

    UNICODE_STRING UniStr;
    UniStr.Buffer = wszPath;
    UniStr.Length = (USHORT)(RTUtf16Len(wszPath) * sizeof(WCHAR));
    UniStr.MaximumLength = UniStr.Length + sizeof(WCHAR);

    int rc = supHardNtLdrCacheNewEntry(&g_aSupNtVpLdrCacheEntries[g_cSupNtVpLdrCacheEntries], pszName, &UniStr,
                                       true /*fDll*/, false /*f32bitResourceDll*/, pErrInfo);
    if (RT_SUCCESS(rc))
    {
        *ppEntry = &g_aSupNtVpLdrCacheEntries[g_cSupNtVpLdrCacheEntries];
        g_cSupNtVpLdrCacheEntries++;
        return VINF_SUCCESS;
    }
    return rc;
}
#endif /* IN_RING3 */


/**
 * Opens all the images with the IPRT loader, setting both, hFile, pNtViRdr and
 * hLdrMod for each image.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure.
 */
static int supHardNtVpOpenImages(PSUPHNTVPSTATE pThis)
{
    unsigned i = pThis->cImages;
    while (i-- > 0)
    {
        PSUPHNTVPIMAGE pImage = &pThis->aImages[i];

#ifdef VBOX_WITH_MINIMAL_HARDENING
        /* Only the process executable image is verified here, so don't bother open all the other ones. */
        if (pImage->fDll)
            continue;
#endif

#ifdef IN_RING3
        /*
         * Try the cache first.
         */
        pImage->pCacheEntry = supHardNtLdrCacheLookupEntry(pImage->pszName);
        if (pImage->pCacheEntry)
            continue;

        /*
         * Not in the cache, so load it into the cache.
         */
        if (g_cSupNtVpLdrCacheEntries >= RT_ELEMENTS(g_aSupNtVpLdrCacheEntries))
            return supHardNtVpSetInfo2(pThis, VERR_INTERNAL_ERROR_3, "Loader cache overflow.");
        pImage->pCacheEntry = &g_aSupNtVpLdrCacheEntries[g_cSupNtVpLdrCacheEntries];
#else
        /*
         * In ring-0 we don't have a cache at the moment (resource reasons), so
         * we have a static cache entry in each image structure that we use instead.
         */
        pImage->pCacheEntry = &pImage->CacheEntry;
#endif

#ifndef VBOX_WITH_MINIMAL_HARDENING
        const char * const pszName = pImage->pszName;
#else
        const char * const pszName = "ignored.exe";
#endif
        int rc = supHardNtLdrCacheNewEntry(pImage->pCacheEntry, pszName, &pImage->Name.UniStr,
                                           pImage->fDll, pImage->f32bitResourceDll, pThis->pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
#ifdef IN_RING3
        g_cSupNtVpLdrCacheEntries++;
#endif
    }

    return VINF_SUCCESS;
}


/**
 * Check the integrity of the executable of the process.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure. Details
 *                              about images are added to this.  The hProcess
 *                              member holds the handle to the process that is
 *                              to be verified.
 */
static int supHardNtVpCheckExe(PSUPHNTVPSTATE pThis)
{
    /*
     * Make sure there is exactly one executable image.
     */
    unsigned cExecs = 0;
    unsigned iExe   = ~0U;
    unsigned i = pThis->cImages;
    while (i-- > 0)
    {
        if (!pThis->aImages[i].fDll)
        {
            cExecs++;
            iExe = i;
        }
    }
    if (cExecs == 0)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NO_FOUND_NO_EXE_MAPPING,
                                   "No executable mapping found in the virtual address space.");
    if (cExecs != 1)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_FOUND_MORE_THAN_ONE_EXE_MAPPING,
                                   "Found more than one executable mapping in the virtual address space.");
    PSUPHNTVPIMAGE pImage = &pThis->aImages[iExe];

    /*
     * Check that it matches the executable image of the process.
     */
    int             rc;
    ULONG           cbUniStr = sizeof(UNICODE_STRING) + RTPATH_MAX * sizeof(RTUTF16);
    PUNICODE_STRING pUniStr  = (PUNICODE_STRING)RTMemAllocZ(cbUniStr);
    if (!pUniStr)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NO_MEMORY,
                                  "Error allocating %zu bytes for process name.", cbUniStr);
    ULONG    cbIgn = 0;
    NTSTATUS rcNt = NtQueryInformationProcess(pThis->hProcess, ProcessImageFileName, pUniStr, cbUniStr - sizeof(WCHAR), &cbIgn);
    if (NT_SUCCESS(rcNt))
    {
        pUniStr->Buffer[pUniStr->Length / sizeof(WCHAR)] = '\0';
        if (supHardNtVpArePathsEqual(pUniStr, &pImage->Name.UniStr))
            rc = VINF_SUCCESS;
        else
            rc = supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EXE_VS_PROC_NAME_MISMATCH,
                                     "Process image name does not match the exectuable we found: %ls vs %ls.",
                                     pUniStr->Buffer, pImage->Name.UniStr.Buffer);
    }
    else
        rc = supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NT_QI_PROCESS_NM_ERROR,
                                 "NtQueryInformationProcess/ProcessImageFileName failed: %#x", rcNt);
    RTMemFree(pUniStr);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Validate the signing of the executable image.
     * This will load the fDllCharecteristics and fImageCharecteristics members we use below.
     */
    rc = supHardNtVpVerifyImage(pThis, pImage);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Check linking requirements.
     * This query is only available using the current process pseudo handle on
     * older windows versions.  The cut-off seems to be Vista.
     */
    SECTION_IMAGE_INFORMATION ImageInfo;
    rcNt = NtQueryInformationProcess(pThis->hProcess, ProcessImageInformation, &ImageInfo, sizeof(ImageInfo), NULL);
    if (!NT_SUCCESS(rcNt))
    {
        if (   rcNt == STATUS_INVALID_PARAMETER
            && g_uNtVerCombined < SUP_NT_VER_VISTA
            && pThis->hProcess != NtCurrentProcess() )
            return VINF_SUCCESS;
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NT_QI_PROCESS_IMG_INFO_ERROR,
                                   "NtQueryInformationProcess/ProcessImageInformation failed: %#x hProcess=%#x",
                                   rcNt, pThis->hProcess);
    }
#ifndef VBOX_WITHOUT_WINDOWS_KERNEL_CODE_SIGNING_CERT /* A kernel code signing cert is only via way to use /IntegrityCheck. */
    if ( !(ImageInfo.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EXE_MISSING_FORCE_INTEGRITY,
                                   "EXE DllCharacteristics=%#x, expected IMAGE_DLLCHARACTERISTICS_FORCE_INTEGRITY to be set.",
                                   ImageInfo.DllCharacteristics);
#endif
    if (!(ImageInfo.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EXE_MISSING_DYNAMIC_BASE,
                                   "EXE DllCharacteristics=%#x, expected IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE to be set.",
                                   ImageInfo.DllCharacteristics);
    if (!(ImageInfo.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_EXE_MISSING_NX_COMPAT,
                                   "EXE DllCharacteristics=%#x, expected IMAGE_DLLCHARACTERISTICS_NX_COMPAT to be set.",
                                   ImageInfo.DllCharacteristics);

    if (pImage->fDllCharecteristics != ImageInfo.DllCharacteristics)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_DLL_CHARECTERISTICS_MISMATCH,
                                   "EXE Info.DllCharacteristics=%#x fDllCharecteristics=%#x.",
                                   ImageInfo.DllCharacteristics, pImage->fDllCharecteristics);

    if (pImage->fImageCharecteristics != ImageInfo.ImageCharacteristics)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_DLL_CHARECTERISTICS_MISMATCH,
                                   "EXE Info.ImageCharacteristics=%#x fImageCharecteristics=%#x.",
                                   ImageInfo.ImageCharacteristics, pImage->fImageCharecteristics);

    return VINF_SUCCESS;
}


#ifndef VBOX_WITH_MINIMAL_HARDENING
/**
 * Check the integrity of the DLLs found in the process.
 *
 * @returns VBox status code.
 * @param   pThis               The process scanning state structure. Details
 *                              about images are added to this.  The hProcess
 *                              member holds the handle to the process that is
 *                              to be verified.
 */
static int supHardNtVpCheckDlls(PSUPHNTVPSTATE pThis)
{
    /*
     * Check for duplicate entries (paranoia).
     */
    uint32_t i = pThis->cImages;
    while (i-- > 1)
    {
        const char *pszName = pThis->aImages[i].pszName;
        uint32_t j = i;
        while (j-- > 0)
            if (pThis->aImages[j].pszName == pszName)
                return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_DUPLICATE_DLL_MAPPING,
                                           "Duplicate image entries for %s: %ls and %ls",
                                           pszName, pThis->aImages[i].Name.UniStr.Buffer, pThis->aImages[j].Name.UniStr.Buffer);
    }

    /*
     * Check that both ntdll and kernel32 are present.
     * ASSUMES the entries in g_aSupNtVpAllowedDlls are all lower case.
     */
    uint32_t iNtDll    = UINT32_MAX;
    uint32_t iKernel32 = UINT32_MAX;
    i = pThis->cImages;
    while (i-- > 0)
        if (suplibHardenedStrCmp(pThis->aImages[i].pszName, "ntdll.dll") == 0)
            iNtDll = i;
        else if (suplibHardenedStrCmp(pThis->aImages[i].pszName, "kernel32.dll") == 0)
            iKernel32 = i;
    if (iNtDll == UINT32_MAX)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NO_NTDLL_MAPPING,
                                   "The process has no NTDLL.DLL.");
    if (iKernel32 == UINT32_MAX && (   pThis->enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION
                                    || pThis->enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED))
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NO_KERNEL32_MAPPING,
                                   "The process has no KERNEL32.DLL.");
    else if (iKernel32 != UINT32_MAX && pThis->enmKind == SUPHARDNTVPKIND_CHILD_PURIFICATION)
        return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_KERNEL32_ALREADY_MAPPED,
                                   "The process already has KERNEL32.DLL loaded.");

    /*
     * Verify that the DLLs are correctly signed (by MS).
     */
    i = pThis->cImages;
    while (i-- > 0)
    {
        int rc = supHardNtVpVerifyImage(pThis, &pThis->aImages[i]);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}
#endif /* !VBOX_WITH_MINIMAL_HARDENING */


#ifdef IN_RING3
/**
 * Verifies that we don't have any inheritable handles around, other than a few
 * ones for file and event objects.
 *
 * When finding an inheritable handle of a different type, it will change it to
 * non-inhertiable.  This must NOT be called in the final process prior to
 * opening the device!
 *
 * @returns VBox status code
 * @param   pThis               The process scanning state structure.
 */
static int supHardNtVpCheckHandles(PSUPHNTVPSTATE pThis)
{
    SUP_DPRINTF(("supHardNtVpCheckHandles:\n"));

    /*
     * Take a snapshot of all the handles in the system.
     * (Because the current process handle snapshot was added in Windows 8,
     * so we cannot use that yet.)
     */
    uint32_t    cbBuf    = _256K;
    uint8_t    *pbBuf    = (uint8_t *)RTMemAlloc(cbBuf);
    ULONG       cbNeeded = cbBuf;
    NTSTATUS rcNt = NtQuerySystemInformation(SystemExtendedHandleInformation, pbBuf, cbBuf, &cbNeeded);
    if (!NT_SUCCESS(rcNt))
    {
        while (   rcNt == STATUS_INFO_LENGTH_MISMATCH
               && cbNeeded > cbBuf
               && cbBuf <= _32M)
        {
            cbBuf = RT_ALIGN_32(cbNeeded + _4K, _64K);
            RTMemFree(pbBuf);
            pbBuf = (uint8_t *)RTMemAlloc(cbBuf);
            if (!pbBuf)
                return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NO_MEMORY, "Failed to allocate %zu bytes querying handles.", cbBuf);
            rcNt = NtQuerySystemInformation(SystemExtendedHandleInformation, pbBuf, cbBuf, &cbNeeded);
        }
        if (!NT_SUCCESS(rcNt))
        {
            RTMemFree(pbBuf);
            return supHardNtVpSetInfo2(pThis, VERR_SUP_VP_NO_MEMORY, "Failed to allocate %zu bytes querying handles.", cbBuf);
        }
    }

    /*
     * Examine the snapshot for handles for this process.
     */
    int                                 rcRet     = VINF_SUCCESS;
    HANDLE const                        idProcess = RTNtCurrentTeb()->ClientId.UniqueProcess;
    SYSTEM_HANDLE_INFORMATION_EX const *pInfo     = (SYSTEM_HANDLE_INFORMATION_EX const *)pbBuf;
    ULONG_PTR                           i         = pInfo->NumberOfHandles;
    AssertRelease(RT_UOFFSETOF_DYN(SYSTEM_HANDLE_INFORMATION_EX, Handles[i]) == cbNeeded);
    while (i-- > 0)
    {
        SYSTEM_HANDLE_ENTRY_INFO_EX const *pHandleInfo = &pInfo->Handles[i];
        if (   (pHandleInfo->HandleAttributes & OBJ_INHERIT)
            && pHandleInfo->UniqueProcessId == idProcess)
        {
            ULONG cbNeeded2 = 0;
            rcNt = NtQueryObject(pHandleInfo->HandleValue, ObjectTypeInformation,
                                 pThis->abMemory, sizeof(pThis->abMemory), &cbNeeded2);
            if (NT_SUCCESS(rcNt))
            {
                POBJECT_TYPE_INFORMATION pTypeInfo = (POBJECT_TYPE_INFORMATION)pThis->abMemory;
                if (   pTypeInfo->TypeName.Length == sizeof(L"File") - sizeof(wchar_t)
                    && memcmp(pTypeInfo->TypeName.Buffer, L"File", sizeof(L"File") - sizeof(wchar_t)) == 0)
                    SUP_DPRINTF(("supHardNtVpCheckHandles: Inheritable file handle: %p\n", pHandleInfo->HandleValue));
                else if (   pTypeInfo->TypeName.Length == sizeof(L"Event") - sizeof(wchar_t)
                         && memcmp(pTypeInfo->TypeName.Buffer, L"Event", sizeof(L"Event") - sizeof(wchar_t)) == 0)
                    SUP_DPRINTF(("supHardNtVpCheckHandles: Inheritable event handle: %p\n", pHandleInfo->HandleValue));
                else
                {
                    OBJECT_HANDLE_FLAG_INFORMATION SetInfo;
                    SetInfo.Inherit = FALSE;
                    SetInfo.ProtectFromClose = FALSE;
                    rcNt = NtSetInformationObject(pHandleInfo->HandleValue, ObjectHandleFlagInformation,
                                                  &SetInfo, sizeof(SetInfo));
                    if (NT_SUCCESS(rcNt))
                    {
                        SUP_DPRINTF(("supHardNtVpCheckHandles: Marked %ls handle non-inheritable: %p\n",
                                     pTypeInfo->TypeName.Buffer, pHandleInfo->HandleValue));
                        pThis->cFixes++;
                    }
                    else
                    {
                        rcRet = supHardNtVpSetInfo2(pThis, VERR_SUP_VP_SET_HANDLE_NOINHERIT,
                                                    "NtSetInformationObject(%p,,,) -> %#x", pHandleInfo->HandleValue, rcNt);
                        break;
                    }
                }
            }
            else
            {
                rcRet = supHardNtVpSetInfo2(pThis, VERR_SUP_VP_QUERY_HANDLE_TYPE,
                                            "NtQueryObject(%p,,,,) -> %#x", pHandleInfo->HandleValue, rcNt);
                break;
            }

        }
    }
    RTMemFree(pbBuf);
    return rcRet;
}
#endif /* IN_RING3 */


/**
 * Verifies the given process.
 *
 * The following requirements are checked:
 *  - The process only has one thread, the calling thread.
 *  - The process has no debugger attached.
 *  - The executable image of the process is verified to be signed with
 *    certificate known to this code at build time.
 *  - The executable image is one of a predefined set.
 *  - The process has only a very limited set of system DLLs loaded.
 *  - The system DLLs signatures check out fine.
 *  - The only executable memory in the process belongs to the system DLLs and
 *    the executable image.
 *
 * @returns VBox status code.
 * @param   hProcess            The process to verify.
 * @param   hThread             A thread in the process (the caller).
 * @param   enmKind             The kind of process verification to perform.
 * @param   fFlags              Valid combination of SUPHARDNTVP_F_XXX flags.
 * @param   pRwxPgInfo          Where to return verfication information for the
 *                              RWX page (in the executable image). Optional.
 * @param   pErrInfo            Pointer to error info structure. Optional.
 * @param   pcFixes             Where to return the number of fixes made during
 *                              purification.  Optional.
 */
DECLHIDDEN(int) supHardenedWinVerifyProcess(HANDLE hProcess, HANDLE hThread, SUPHARDNTVPKIND enmKind, uint32_t fFlags,
                                            PSUPHARDNTVPRWXPGINFO pRwxPgInfo, uint32_t *pcFixes, PRTERRINFO pErrInfo)
{
    RT_NOREF(hThread);
    RT_STACK_CHECK_RET_ADDR();
    if (pcFixes)
        *pcFixes = 0;
    if (pRwxPgInfo)
        pRwxPgInfo->pvRwxPgR3Ptr = NIL_RTR3PTR;

    /*
     * Some basic checks regarding threads and debuggers. We don't need
     * allocate any state memory for these.
     */
    int rc = VINF_SUCCESS;
#ifndef VBOX_WITH_MINIMAL_HARDENING
    if (   enmKind != SUPHARDNTVPKIND_CHILD_PURIFICATION
        && enmKind != SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED
        && enmKind != SUPHARDNTVPKIND_LIMITED_VERIFY_ONLY)
       rc = supHardNtVpThread(hProcess, hThread, pErrInfo);
    if (RT_SUCCESS(rc))
        rc = supHardNtVpDebugger(hProcess, pErrInfo);
    if (RT_SUCCESS(rc))
#endif /* !VBOX_WITH_MINIMAL_HARDENING */
    {
        /*
         * Allocate and initialize memory for the state.
         */
        PSUPHNTVPSTATE pThis = (PSUPHNTVPSTATE)RTMemAllocZ(sizeof(*pThis));
        if (pThis)
        {
            pThis->enmKind    = enmKind;
            pThis->fFlags     = fFlags;
            pThis->rcResult   = VINF_SUCCESS;
            pThis->hProcess   = hProcess;
            pThis->pErrInfo   = pErrInfo;
            pThis->pRwxPgInfo = pRwxPgInfo;

            /*
             * Perform the verification.
             */
            rc = supHardNtVpScanVirtualMemory(pThis, hProcess);
            if (RT_SUCCESS(rc))
                rc = supHardNtVpOpenImages(pThis);
            if (RT_SUCCESS(rc))
                rc = supHardNtVpCheckExe(pThis);
#ifndef VBOX_WITH_MINIMAL_HARDENING
            if (RT_SUCCESS(rc))
                rc = supHardNtVpCheckDlls(pThis);
# ifdef IN_RING3
            if (enmKind == SUPHARDNTVPKIND_SELF_PURIFICATION_LIMITED)
                rc = supHardNtVpCheckHandles(pThis);
# endif
#endif /* !VBOX_WITH_MINIMAL_HARDENING */


            if (pcFixes)
                *pcFixes = pThis->cFixes;

            /*
             * Clean up the state.
             */
#ifdef IN_RING0
            for (uint32_t i = 0; i < pThis->cImages; i++)
                supHardNTLdrCacheDeleteEntry(&pThis->aImages[i].CacheEntry);
#endif
            RTMemFree(pThis);
        }
        else
            rc = supHardNtVpSetInfo1(pErrInfo, VERR_SUP_VP_NO_MEMORY_STATE,
                                     "Failed to allocate %zu bytes for state structures.", sizeof(*pThis));
    }
    return rc;
}

