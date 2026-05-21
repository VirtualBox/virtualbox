/* $Id: dbgkrnlinfo-r0drv-linux.c 114167 2026-05-21 12:28:29Z vadim.galitsyn@oracle.com $ */
/** @file
 * IPRT - Kernel Debug Information, R0 Driver, Linux.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
# include "the-linux-kernel.h"
# include <linux/uio.h>
# if RTLNX_VER_MIN(2,30,0) && defined(CONFIG_KPROBES)
#  include <linux/kprobes.h>
#  include <linux/kallsyms.h>
# endif
#else
# include <iprt/stream.h>
# define printk RTPrintf
# define RTLNX_VER_MIN(x, y, z) 1
#endif

#if !defined(IN_RING0) && !defined(DOXYGEN_RUNNING) /* A linking tweak for the testcase: */
# include <iprt/cdefs.h>
# undef  RTR0DECL
# define RTR0DECL(type) DECLHIDDEN(type) RTCALL
#endif

#include "internal/iprt.h"
#include <iprt/dbg.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include "internal/magics.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if RTLNX_VER_MIN(3,16,0) || defined(IN_RING3) /** @todo support this for older kernels (see also initterm-r0drv-linux.c and fileio-r0drv-linux.c) */
# define IPRT_LNX_CAN_USE_PROC_KALLSYMS
#endif

#if RTLNX_VER_MIN(2,30,0) && defined(CONFIG_KPROBES) && defined(IN_RING0)
# define IPRT_LNX_CAN_USE_KPROBES
#endif

#if defined(IPRT_LNX_CAN_USE_PROC_KALLSYMS) || defined(IPRT_LNX_CAN_USE_KPROBES)
# define IPRT_LNX_HAVE_IMPLEMENTATION
#endif


#define X86_CPUID_STEXT_FEATURE_EDX_CET_IBT RT_BIT_32(20)
#ifndef MSR_IA32_S_CET
# define MSR_IA32_S_CET                     0x6a2
#endif
#ifndef MSR_IA32_CET_ENDBR_EN
# define MSR_IA32_CET_ENDBR_EN              RT_BIT_64(2)
#endif
#ifndef MSR_IA32_CET_SUPPRESS
# define MSR_IA32_CET_SUPPRESS              RT_BIT_64(10)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Linux kernel debug info instance data.
 */
typedef struct RTDBGKRNLINFOINT
{
    /** Magic value (RTDBGKRNLINFO_MAGIC). */
    uint32_t            u32Magic;
    /** Reference counter.  */
    uint32_t volatile   cRefs;
    /** The /proc/kallsyms file handle.
     * @note This can be NIL_RTFILE. */
    RTFILE              hFile;
    /** Buffer space (the file is typically several MBs, so larger is better). */
    char                abBuf[_16K - 64];
} RTDBGKRNLINFOINT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IPRT_LNX_CAN_USE_KPROBES
/** Pointer to kallsyms_lookup_name if in kprobes mode and available.
 * @note Added in Linux 2.6.4. */
static __typeof__(kallsyms_lookup_name) *g_pfnKallsymsLookupName = NULL;
/** Only try resolve g_pfnKallsymsLookupName once. */
static bool g_fTriedResolvingKallsymsLookupName = false;
#endif
#ifdef IN_RING3
/** This is for the testcase. */
extern const char *g_pszTestKallsyms;
#endif


#ifdef IPRT_LNX_HAVE_IMPLEMENTATION
/**
 * Destructor.
 *
 * @param   pThis               The instance to destroy.
 */
static void rtR0DbgKrnlLinuxDtor(RTDBGKRNLINFOINT *pThis)
{
    pThis->u32Magic = ~RTDBGKRNLINFO_MAGIC;

# ifdef IPRT_LNX_CAN_USE_PROC_KALLSYMS
    if (pThis->hFile != NIL_RTFILE)
    {
        RTFileClose(pThis->hFile);
        pThis->hFile = NIL_RTFILE;
    }
# endif

    RTMemFree(pThis);
}
#endif


#ifdef IPRT_LNX_CAN_USE_PROC_KALLSYMS
/**
 * Worker for resolving a symbol using the kallsyms file.
 */
static int
rtR0DbgKrnlInfoLnxQuerySymbolKallsyms(RTDBGKRNLINFOINT *pThis, const char *pszModule, const char *pszSymbol, void **ppvSymbol)
{
    size_t const cchSymbol            = strlen(pszSymbol);
    size_t const cchModule            = pszModule ? strlen(pszModule) : 0;
    size_t const cchMinLineLength     = ARCH_BITS / 4 + 1 + 1 + 1 + cchSymbol + (cchModule ? 2 + cchModule + 1 : 0);
    size_t const cchLineLengthKSymTab = cchModule ? cchMinLineLength + sizeof("__ksymtab_") - 1 : ~(size_t)0 / 2;
    char *       pchBuf;
    RTFOFF       offFile;
    uint32_t     cbInBuf;
    uint32_t     off;
    bool         fSeenKSymtabEntry;
    uintptr_t    uCandidate;

    /*
     * Scan the entire file for the requested symbol.
     */
    pchBuf            = pThis->abBuf;
    offFile           = 0;
    cbInBuf           = 0;
    off               = 0;
    fSeenKSymtabEntry = false;
    uCandidate        = ~(uintptr_t)0;
    for (;;)
    {
        /*
         * Locate end of the current line, read more file content as needed.
         */
        uint32_t offLine = off;
        while (off < cbInBuf && pchBuf[off] != '\n')
            off++;
        if (off == cbInBuf)
        {
            size_t cbRead = 0;
            int    rc;

            /* Read more. (ASSUMES that we get full buffers returned and that
               the last line has a trailing newline as well.) */
            if (off != offLine)
            {
                if (offLine == 0)
                    break;
                off     -= offLine;
                memmove(pchBuf, &pchBuf[offLine], off);
            }
            else
                off = 0;
            cbInBuf = off;
            offLine = 0;
            rc = RTFileReadAt(pThis->hFile, offFile, &pchBuf[off], sizeof(pThis->abBuf) - off, &cbRead);
            //printk("dbgkrnlinfo: read %#lx bytes (rc=%d)\n", (unsigned long)cbRead, rc);
            if (RT_FAILURE(rc))
                return rc;
            offFile += cbRead;
            cbInBuf += cbRead;

            /* Continue the search for the end of line: */
            while (off < cbInBuf && pchBuf[off] != '\n')
                off++;
            if (off == cbInBuf)
                break;
        }

        /*
         * Try parse it.
         */
        pchBuf[off] = '\0'; /* terminate the line */
        //printk("dbgkrnlinfo: %s\n",  &pchBuf[offLine]);
        if (   off - offLine == cchMinLineLength
            || off - offLine == cchLineLengthKSymTab
#if 0 /* paranoia - not needed as we match the exact lengths anyway. */
            || (   off - offLine >= cchMinLineLength
                && off - offLine <= cchMinLineLength + 8)
#endif
           )
        {
            /* Parse the address. */
            char    *psz;
            uint64_t uAddr = 0;
            int rc = RTStrToUInt64Ex(&pchBuf[offLine], &psz, 16, &uAddr);
            if (rc == VWRN_TRAILING_CHARS && RT_C_IS_SPACE(*psz))
            {
                char const chType = *++psz; /* skip space and get type */

                /* Check for __ksymtab_ entries. */
                static const char s_szKSymTabPrefix[] = "__ksymtab_";
                if (   chType == 'r'
                    && RT_C_IS_BLANK(psz[1])
                    && strncmp(&psz[2], s_szKSymTabPrefix, sizeof(s_szKSymTabPrefix) - 1) == 0)
                {
                    psz += 2 + sizeof(s_szKSymTabPrefix) - 1;

                    /* Look for the one for the symbol. */
                    if (   cchModule > 0
                        && strncmp(psz, pszSymbol, cchSymbol) == 0)
                    {
                        psz += cchSymbol;
                        if (RT_C_IS_SPACE(*psz) && psz[1] == '[')
                        {
                            psz += 2;
                            if (strncmp(psz, pszModule, cchModule) == 0)
                            {
                                psz += cchModule;
                                if (*psz == ']')
                                {
                                    fSeenKSymtabEntry = true;
                                    if (uCandidate != ~(uintptr_t)0)
                                    {
                                        *ppvSymbol = (void *)(uintptr_t)uCandidate;
                                        return VINF_SUCCESS;
                                    }
                                }
                            }
                        }
                    }
                }
                /* Check that the symbol type is okay.  We only do lower case
                   text symbols in modules with a matching __ksymtab_ entry. */
                else if (   (   chType == 'T' || (chType == 't' && cchModule > 0)
                             || chType == 'D' || chType == 'd'
                             || chType == 'B' || chType == 'b'
                             || chType == 'R' || chType == 'r'
                             || chType == 'V'
                             || chType == 'W')
                         && RT_C_IS_BLANK(psz[1]))
                {
                    psz += 2; /* skip type & following space */

                    /* Match the symbol. */
                    if (strncmp(psz, pszSymbol, cchSymbol) == 0)
                    {
                        psz += cchSymbol;

                        /* If we're matching a kernel symbol and have reached the end of the line now, we're good. */
                        if (!cchModule && *psz == '\0')
                        {
                            *ppvSymbol = (void *)(uintptr_t)uAddr;
                            return VINF_SUCCESS;
                        }

                        /* If we're matching a specific module, we must check the module name as well. */
                        if (cchModule && RT_C_IS_SPACE(*psz) && psz[1] == '[')
                        {
                            psz += 2;
                            if (strncmp(psz, pszModule, cchModule) == 0)
                            {
                                psz += cchModule;
                                if (*psz == ']')
                                {
                                    if (chType == 'T' || fSeenKSymtabEntry)
                                    {
                                        *ppvSymbol = (void *)(uintptr_t)uAddr;
                                        return VINF_SUCCESS;
                                    }
                                    uCandidate = (uintptr_t)uAddr;
                                }
                            }
                        }
                    }
                }
            }
        }

        /*
         * Advance to the next line (skips the newline).
         */
        off++;
    }

    return VERR_SYMBOL_NOT_FOUND;
}
#endif /* IPRT_LNX_CAN_USE_PROC_KALLSYMS */


#ifdef IPRT_LNX_CAN_USE_KPROBES
/**
 * Worker for using register_kprobe to lookup a function symbol.
 */
static int rtR0DbgKrnlInfoLnxQuerySymbolKprobe(const char *pszSymbol, void **ppvSymbol)
{
    struct kprobe KernProbe;
    int rc;

    RT_ZERO(KernProbe);
    KernProbe.flags       = KPROBE_FLAG_DISABLED;
    KernProbe.symbol_name = pszSymbol;
    rc = register_kprobe(&KernProbe);
    if (rc == 0)
    {
        uint8_t const *pbAddr = (uint8_t const *)KernProbe.addr;
        unregister_kprobe(&KernProbe);

        if (RT_VALID_PTR(pbAddr)) /* paranoia */
        {
# if defined(CONFIG_X86_KERNEL_IBT) && (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64))
            /* We must include the endbr instruction that arch_adjust_kprobe_addr()
               skips after symbol resolving for register_kprobe(). */
            uint32_t u32EndBr = 0;
            __get_kernel_nofault(&u32EndBr, ((u32 *)&pbAddr[-4]), u32, l_fault);
#  if RTLNX_VER_MIN(6,15,0) || RTLNX_SUSE_ADLP_MAJ_LNX_PREREQ(6, 12)
            if (__is_endbr(u32EndBr))
#  else
            if (is_endbr(u32EndBr))
#  endif
                pbAddr -= 4;
l_fault:
# endif
            if (ppvSymbol)
                *ppvSymbol = (void *)pbAddr;
            return VINF_SUCCESS;
        }
    }

    return VERR_SYMBOL_NOT_FOUND;
}
#endif


RTR0DECL(int) RTR0DbgKrnlInfoOpen(PRTDBGKRNLINFO phKrnlInfo, uint32_t fFlags)
{
#ifdef IPRT_LNX_HAVE_IMPLEMENTATION
    struct RTDBGKRNLINFOINT *pThis;
# ifdef IPRT_LNX_CAN_USE_PROC_KALLSYMS
    int rc;
# endif
#endif

    /*
     * Validate input.
     */
    AssertPtrReturn(phKrnlInfo, VERR_INVALID_POINTER);
    *phKrnlInfo = NIL_RTDBGKRNLINFO;
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);

#ifdef IPRT_LNX_HAVE_IMPLEMENTATION
    /*
     * Create the instance first, as that'll let us check that the /proc/kallsyms
     * file works correctly.
     */
    pThis = (struct RTDBGKRNLINFOINT *)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic = RTDBGKRNLINFO_MAGIC;
    pThis->cRefs    = 1;
    pThis->hFile    = NIL_RTFILE;

# ifdef IPRT_LNX_CAN_USE_PROC_KALLSYMS
    /*
     * Try open the kernel symbol file first, as it allow us to check the symbol and stuff.
     */
    rc = RTFileOpen(&pThis->hFile,
#  ifdef IN_RING3
                    g_pszTestKallsyms ? g_pszTestKallsyms : "/proc/kallsyms",
#  else
                    "/proc/kallsyms",
#  endif
                    RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN);
    if (RT_SUCCESS(rc))
    {
        /* Look up the address of a known symbol. */
#  ifndef IN_RING3
        uintptr_t const uExpected = (uintptr_t)get_user_pages;
#  endif
        void           *pvActual  = NULL;
        rc = rtR0DbgKrnlInfoLnxQuerySymbolKallsyms(pThis, NULL, "get_user_pages", &pvActual);
        if (   RT_SUCCESS(rc)
#  ifndef IN_RING3
            && (uintptr_t)pvActual == uExpected
#  endif
           )
        {
            *phKrnlInfo = pThis;
            return VINF_SUCCESS;
        }
#  ifndef IN_RING3
        /* Complain and close up. */
        if (RT_SUCCESS(rc))
        {
            intptr_t offDelta = (intptr_t)pvActual - (intptr_t)uExpected;
            printk("RTR0DbgKrnlInfoOpen: /proc/kallsym method failed. get_user_pages address off by %c%#lx\n",
                   offDelta < 0 ? '-' : '+', offDelta < 0 ? (unsigned long)-offDelta : (unsigned long)offDelta);
        }
        else
#  endif
            printk("RTR0DbgKrnlInfoOpen: /proc/kallsym method failed - get_user_pages not found (rc=%d)\n", rc);
        RTFileClose(pThis->hFile);
        pThis->hFile = NIL_RTFILE;
        rc = VERR_DBG_FILE_MISMATCH;
    }
# endif /* IPRT_LNX_CAN_USE_PROC_KALLSYMS */

# ifdef IPRT_LNX_CAN_USE_KPROBES
    /*
     * Then try use kprobes, preferably via kallsyms_lookup_name.
     */
    if (!g_pfnKallsymsLookupName && !g_fTriedResolvingKallsymsLookupName)
    {
        void *pvLookupFunction = NULL;
        if (RT_SUCCESS(rtR0DbgKrnlInfoLnxQuerySymbolKprobe("kallsyms_lookup_name", &pvLookupFunction)))
            g_pfnKallsymsLookupName = (__typeof__(g_pfnKallsymsLookupName))(uintptr_t)pvLookupFunction;
        g_fTriedResolvingKallsymsLookupName = true;
    }
    *phKrnlInfo = pThis;
    return VINF_SUCCESS;

# else
    /* Ditch the instance and return failure. */
    pThis->u32Magic = 0;
    RTMemFree(pThis);
    return rc;
# endif /* !IPRT_LNX_CAN_USE_KPROBES */

#else  /* !IPRT_LNX_HAVE_IMPLEMENTATION */
    return VERR_NOT_IMPLEMENTED;
#endif /* !IPRT_LNX_HAVE_IMPLEMENTATION */
}


RTR0DECL(uint32_t) RTR0DbgKrnlInfoRetain(RTDBGKRNLINFO hKrnlInfo)
{
#ifdef IPRT_LNX_HAVE_IMPLEMENTATION
    RTDBGKRNLINFOINT *pThis = hKrnlInfo;
    uint32_t cRefs;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), UINT32_MAX);

    cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000);
    return cRefs;
#else
    RT_NOREF(hKrnlInfo);
    return UINT32_MAX;
#endif
}


RTR0DECL(uint32_t) RTR0DbgKrnlInfoRelease(RTDBGKRNLINFO hKrnlInfo)
{
#ifdef IPRT_LNX_HAVE_IMPLEMENTATION
    RTDBGKRNLINFOINT *pThis = hKrnlInfo;
    uint32_t cRefs;
    if (pThis == NIL_RTDBGKRNLINFO)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), UINT32_MAX);

    cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (cRefs == 0)
        rtR0DbgKrnlLinuxDtor(pThis);
    return cRefs;
#else
    RT_NOREF(hKrnlInfo);
    return UINT32_MAX;
#endif
}


RTR0DECL(int) RTR0DbgKrnlInfoQueryMember(RTDBGKRNLINFO hKrnlInfo, const char *pszModule, const char *pszStructure,
                                         const char *pszMember, size_t *poffMember)
{
    RTDBGKRNLINFOINT *pThis = hKrnlInfo;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertPtrReturn(pszMember, VERR_INVALID_POINTER);
    AssertPtrReturn(pszModule, VERR_INVALID_POINTER);
    AssertPtrReturn(pszStructure, VERR_INVALID_POINTER);
    AssertPtrReturn(poffMember, VERR_INVALID_POINTER);
    return VERR_NOT_FOUND;
}


RTR0DECL(int) RTR0DbgKrnlInfoQuerySymbol(RTDBGKRNLINFO hKrnlInfo, const char *pszModule,
                                         const char *pszSymbol, void **ppvSymbol)
{
    RTDBGKRNLINFOINT *pThis = hKrnlInfo;
    void *pvTmpSymbol = NULL;

    /*
     * Validate input.
     */
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertPtrReturn(pszSymbol, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(pszModule, VERR_MODULE_NOT_FOUND);
    AssertPtrNullReturn(ppvSymbol, VERR_INVALID_PARAMETER);

    if (!ppvSymbol)
        ppvSymbol = &pvTmpSymbol;
    *ppvSymbol = NULL;

#ifdef IPRT_LNX_CAN_USE_PROC_KALLSYMS
    /*
     * Try kallsyms first if we have it.
     */
    if (pThis->hFile != NIL_RTFILE)
    {
        int rc = rtR0DbgKrnlInfoLnxQuerySymbolKallsyms(pThis, pszModule, pszSymbol, ppvSymbol);
        if (RT_SUCCESS(rc))
            return rc;
    }
#endif

#ifdef IPRT_LNX_CAN_USE_KPROBES
    /*
     * Try using the lookup function next, either directly or indirectly
     * via register_kprobe.
     *
     * The "module:function" format has been supported since 2.6.4 when
     * the lookup function was added.  Use the kallsyms read buffer
     * for temporary storage.
     */
    if (pszModule)
    {
        size_t const cchModule = strlen(pszModule);
        size_t const cchSymbol = strlen(pszSymbol);
        if (cchModule + 1 + cchSymbol >= sizeof(pThis->abBuf))
            return VERR_SYMBOL_NOT_FOUND;
        memcpy(pThis->abBuf, pszModule, cchModule);
        pThis->abBuf[cchModule] = ':';
        memcpy(&pThis->abBuf[cchModule + 1], pszSymbol, cchSymbol);
        pThis->abBuf[cchModule + 1 + cchSymbol] = '\0';
        pszSymbol = (char *)&pThis->abBuf[0];
    }

    if (!g_pfnKallsymsLookupName)
        return rtR0DbgKrnlInfoLnxQuerySymbolKprobe(pszSymbol, ppvSymbol);

    /*
     * Note! kallsyms_lookup_name isn't exported, so we have to temporarily disable the
     *       indirect branch track machinery in order to call it safely.
     *       (Setting the SUPPRESS bit to 1 probably won't help much here, as
     *       the call is done via __x86_indirect_thunk_xxx.)
     */
    RTTHREADPREEMPTSTATE Preempt = RTTHREADPREEMPTSTATE_INITIALIZER;
    uint32_t const uLeaves = ASMCpuId_EAX(0);
    RTThreadPreemptDisable(&Preempt);
    if (   uLeaves >= 7
        && RTX86IsValidStdRange(uLeaves)
        && (ASMCpuIdEx_EDX(7, 0) & X86_CPUID_STEXT_FEATURE_EDX_CET_IBT))
    {
        uint64_t const fSupCet = ASMRdMsr(MSR_IA32_S_CET);
        ASMWrMsr(MSR_IA32_S_CET, fSupCet & ~MSR_IA32_CET_ENDBR_EN);
        *ppvSymbol = (void *)g_pfnKallsymsLookupName(pszSymbol);
        ASMWrMsr(MSR_IA32_S_CET, fSupCet);
    }
    else
        *ppvSymbol = (void *)g_pfnKallsymsLookupName(pszSymbol);
    RTThreadPreemptRestore(&Preempt);

    if (*ppvSymbol != NULL)
        return VINF_SUCCESS;
#endif

    return VERR_SYMBOL_NOT_FOUND;
}

