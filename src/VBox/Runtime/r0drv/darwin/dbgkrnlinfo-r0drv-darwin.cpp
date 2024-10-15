/* $Id: dbgkrnlinfo-r0drv-darwin.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Kernel Debug Information, R0 Driver, Darwin.
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
#ifdef IN_RING0
# include "the-darwin-kernel.h"
# include <sys/kauth.h>
RT_C_DECLS_BEGIN /* Buggy 10.4 headers, fixed in 10.5. */
# if MAC_OS_X_VERSION_MIN_REQUIRED < 101500
#  include <sys/kpi_mbuf.h>
#  include <net/kpi_interfacefilter.h>
#  include <sys/kpi_socket.h>
# endif
# include <sys/kpi_socketfilter.h>
RT_C_DECLS_END
# include <sys/buf.h>
# include <sys/vm.h>
# include <sys/vnode_if.h>
/*# include <sys/sysctl.h>*/
# include <sys/systm.h>
# include <vfs/vfs_support.h>
/*# include <miscfs/specfs/specdev.h>*/
#else
# include <stdio.h> /* for printf */
#endif

#if !defined(IN_RING0) && !defined(DOXYGEN_RUNNING) /* A linking tweak for the testcase: */
# include <iprt/cdefs.h>
# undef  RTR0DECL
# define RTR0DECL(type) DECLHIDDEN(type) RTCALL
#endif

#include "internal/iprt.h"
#include <iprt/dbg.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/formats/mach-o.h>
#include "internal/magics.h"

/** @def MY_CPU_TYPE
 * The CPU type targeted by the compiler. */
/** @def MY_CPU_TYPE
 * The "ALL" CPU subtype targeted by the compiler. */
/** @def MY_MACHO_HEADER
 * The Mach-O header targeted by the compiler.  */
/** @def MY_MACHO_MAGIC
 * The Mach-O header magic we're targeting.  */
/** @def MY_SEGMENT_COMMAND
 * The segment command targeted by the compiler.  */
/** @def MY_SECTION
 * The section struture targeted by the compiler.  */
/** @def MY_NLIST
 * The symbol table entry targeted by the compiler.  */
#ifdef RT_ARCH_X86
# define MY_CPU_TYPE            CPU_TYPE_I386
# define MY_CPU_SUBTYPE_ALL     CPU_SUBTYPE_I386_ALL
# define MY_MACHO_HEADER        mach_header_32_t
# define MY_MACHO_MAGIC         IMAGE_MACHO32_SIGNATURE
# define MY_SEGMENT_COMMAND     segment_command_32_t
# define MY_SECTION             section_32_t
# define MY_NLIST               macho_nlist_32_t

#elif defined(RT_ARCH_AMD64)
# define MY_CPU_TYPE            CPU_TYPE_X86_64
# define MY_CPU_SUBTYPE_ALL     CPU_SUBTYPE_X86_64_ALL
# define MY_MACHO_HEADER        mach_header_64_t
# define MY_MACHO_MAGIC         IMAGE_MACHO64_SIGNATURE
# define MY_SEGMENT_COMMAND     segment_command_64_t
# define MY_SECTION             section_64_t
# define MY_NLIST               macho_nlist_64_t

#elif defined(RT_ARCH_ARM64)
# define MY_CPU_TYPE            CPU_TYPE_ARM64
# define MY_CPU_SUBTYPE_ALL     CPU_SUBTYPE_ARM64_ALL
# define MY_MACHO_HEADER        mach_header_64_t
# define MY_MACHO_MAGIC         IMAGE_MACHO64_SIGNATURE
# define MY_SEGMENT_COMMAND     segment_command_64_t
# define MY_SECTION             section_64_t
# define MY_NLIST               macho_nlist_64_t

#else
# error "Port me!"
#endif

/** @name Return macros for make it simpler to track down too paranoid code.
 * @{
 */
#ifdef DEBUG
# define RETURN_VERR_BAD_EXE_FORMAT \
    do { Assert(!g_fBreakpointOnError);         return VERR_BAD_EXE_FORMAT; } while (0)
# define RETURN_VERR_LDR_UNEXPECTED \
    do { Assert(!g_fBreakpointOnError);         return VERR_LDR_UNEXPECTED; } while (0)
# define RETURN_VERR_LDR_ARCH_MISMATCH \
    do { Assert(!g_fBreakpointOnError);         return VERR_LDR_ARCH_MISMATCH; } while (0)
#else
# define RETURN_VERR_BAD_EXE_FORMAT     do {    return VERR_BAD_EXE_FORMAT; } while (0)
# define RETURN_VERR_LDR_UNEXPECTED     do {    return VERR_LDR_UNEXPECTED; } while (0)
# define RETURN_VERR_LDR_ARCH_MISMATCH  do {    return VERR_LDR_ARCH_MISMATCH; } while (0)
#endif
#if defined(DEBUG_bird) && !defined(IN_RING3)
# define LOG_MISMATCH(...)                      kprintf(__VA_ARGS__)
# define LOG_NOT_PRESENT(...)                   kprintf(__VA_ARGS__)
# define LOG_BAD_SYM(...)                       kprintf(__VA_ARGS__)
# define LOG_SUCCESS(...)                       kprintf(__VA_ARGS__)
#else
# define LOG_MISMATCH(...)                      Log((__VA_ARGS__))
# define LOG_NOT_PRESENT(...)                   Log((__VA_ARGS__))
# define LOG_BAD_SYM(...)                       printf(__VA_ARGS__)
# define LOG_SUCCESS(...)                       printf(__VA_ARGS__)
#endif
/** @} */

#define VERR_LDR_UNEXPECTED     (-641)

#ifndef RT_OS_DARWIN
# define MAC_OS_X_VERSION_MIN_REQUIRED 1050
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Our internal representation of the mach_kernel after loading it's symbols
 * and successfully resolving their addresses.
 */
typedef struct RTDBGKRNLINFOINT
{
    /** Magic value (RTDBGKRNLINFO_MAGIC). */
    uint32_t            u32Magic;
    /** Reference counter.  */
    uint32_t volatile   cRefs;

    /** Set if this is an in-memory rather than on-disk instance. */
    bool                fIsInMem;
    bool                afAlignment[7];

    /** @name Result.
     * @{ */
    /** Pointer to the string table. */
    char               *pachStrTab;
    /** The size of the string table. */
    uint32_t            cbStrTab;
    /** The file offset of the string table. */
    uint32_t            offStrTab;
    /** The link address of the string table. */
    uintptr_t           uStrTabLinkAddr;
    /** Pointer to the symbol table. */
    MY_NLIST           *paSyms;
    /** The size of the symbol table. */
    uint32_t            cSyms;
    /** The file offset of the symbol table. */
    uint32_t            offSyms;
    /** The link address of the symbol table. */
    uintptr_t           uSymTabLinkAddr;
    /** The link address of the text segment. */
    uintptr_t           uTextSegLinkAddr;
    /** Size of the text segment. */
    uintptr_t           cbTextSeg;
    /** Offset between link address and actual load address of the text segment. */
    uintptr_t           offLoad;
    /** The minimum OS version (A.B.C; A is 16 bits, B & C each 8 bits). */
    uint32_t            uMinOsVer;
    /** The SDK version (A.B.C; A is 16 bits, B & C each 8 bits). */
    uint32_t            uSdkVer;
    /** The source version (A.B.C.D.E; A is 24 bits, the rest 10 each). */
    uint64_t            uSrcVer;
    /** @} */

    /** @name Used during loading.
     * @{ */
    /** The file handle.  */
    RTFILE              hFile;
    /** The architecture image offset (fat_arch_t::offset). */
    uint64_t            offArch;
    /** The architecture image size (fat_arch_t::size). */
    uint32_t            cbArch;
    /** The number of load commands (mach_header_XX_t::ncmds). */
    uint32_t            cLoadCmds;
    /** The size of the load commands. */
    uint32_t            cbLoadCmds;
    /** The load commands. */
    load_command_t     *pLoadCmds;
    /** The number of segments. */
    uint32_t            cSegments;
    /** The number of sections. */
    uint32_t            cSections;
    /** Section pointer table (points into the load commands). */
    MY_SEGMENT_COMMAND const *apSegments[MACHO_MAX_SECT / 2];
    /** Load displacement table for each segment. */
    uintptr_t          aoffLoadSegments[MACHO_MAX_SECT / 2];
    /** Section pointer table (points into the load commands). */
    MY_SECTION const   *apSections[MACHO_MAX_SECT];
    /** Mapping table to quickly get to a segment from MY_NLIST::n_sect. */
    uint8_t            auSections2Segment[MACHO_MAX_SECT];
    /** @} */

    /** Buffer space. */
    char                abBuf[_4K];
} RTDBGKRNLINFOINT;


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#ifdef DEBUG
static bool g_fBreakpointOnError = false;
#endif


/**
 * Close and free up resources we no longer needs.
 *
 * @param   pThis               The internal scratch data.
 */
static void rtR0DbgKrnlDarwinLoadDone(RTDBGKRNLINFOINT *pThis)
{
    if (!pThis->fIsInMem)
        RTFileClose(pThis->hFile);
    pThis->hFile = NIL_RTFILE;

    if (!pThis->fIsInMem)
        RTMemFree(pThis->pLoadCmds);
    pThis->pLoadCmds = NULL;
    RT_ZERO(pThis->apSections);
    RT_ZERO(pThis->apSegments);
}


/**
 * Looks up a kernel symbol record.
 *
 * @returns Pointer to the symbol record or NULL if not found.
 * @param   pThis               The internal scratch data.
 * @param   pszSymbol           The symbol to resolve.  Automatically prefixed
 *                              with an underscore.
 */
static MY_NLIST const *rtR0DbgKrnlDarwinLookupSym(RTDBGKRNLINFOINT *pThis, const char *pszSymbol)
{
    uint32_t const  cSyms = pThis->cSyms;
    MY_NLIST const *pSym = pThis->paSyms;

#if 1
    /* linear search. */
    for (uint32_t iSym = 0; iSym < cSyms; iSym++, pSym++)
    {
        if (pSym->n_type & MACHO_N_STAB)
            continue;

        const char *pszTabName= &pThis->pachStrTab[(uint32_t)pSym->n_un.n_strx];
        if (   *pszTabName == '_'
            && strcmp(pszTabName + 1, pszSymbol) == 0)
            return pSym;
    }
#else
    /** @todo binary search. */
#endif

    return NULL;
}


/**
 * Looks up a kernel symbol.
 *
 * @returns The symbol address on success, 0 on failure.
 * @param   pThis               The internal scratch data.
 * @param   pszSymbol           The symbol to resolve.  Automatically prefixed
 *                              with an underscore.
 */
static uintptr_t rtR0DbgKrnlDarwinLookup(RTDBGKRNLINFOINT *pThis, const char *pszSymbol)
{
    MY_NLIST const *pSym = rtR0DbgKrnlDarwinLookupSym(pThis, pszSymbol);
    if (pSym)
    {
        uint8_t idxSeg = pThis->auSections2Segment[pSym->n_sect];
        if (pThis->aoffLoadSegments[idxSeg] != UINTPTR_MAX)
            return pSym->n_value + pThis->aoffLoadSegments[idxSeg];
    }

    return 0;
}


/* Rainy day: Find the right headers for these symbols ... if there are any. */
extern "C" void ev_try_lock(void);
extern "C" void OSMalloc(void);
extern "C" void OSlibkernInit(void);
extern "C" void kdp_set_interface(void);


/*
 * Determine the load displacement (10.8 kernels are PIE).
 *
 * Starting with 11.0 (BigSur) all segments can have different load displacements
 * so determine the displacements from known symbols.
 *
 * @returns IPRT status code
 * @param   pThis               The internal scratch data.
 */
static int rtR0DbgKrnlDarwinInitLoadDisplacements(RTDBGKRNLINFOINT *pThis)
{
    static struct
    {
        const char *pszName;
        uintptr_t   uAddr;
    } const s_aStandardSyms[] =
    {
#ifdef IN_RING0
# define KNOWN_ENTRY(a_Sym)  { #a_Sym, (uintptr_t)&a_Sym }
#else
# define KNOWN_ENTRY(a_Sym)  { #a_Sym, 0 }
#endif
        KNOWN_ENTRY(vm_map_unwire),   /* __TEXT */
        KNOWN_ENTRY(kernel_map),      /* __HIB */
        KNOWN_ENTRY(gIOServicePlane), /* __DATA (__HIB on ElCapitan) */
        KNOWN_ENTRY(page_mask)        /* __DATA on ElCapitan */
#undef KNOWN_ENTRY
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aStandardSyms); i++)
    {
        MY_NLIST const *pSym = rtR0DbgKrnlDarwinLookupSym(pThis, s_aStandardSyms[i].pszName);
        if (RT_UNLIKELY(!pSym))
            return VERR_INTERNAL_ERROR_2;

        uint8_t idxSeg = pThis->auSections2Segment[pSym->n_sect];
#ifdef IN_RING0
        /*
         * The segment should either not have the load displacement determined or it should
         * be the same for all symbols in the same segment.
         */
        if (   pThis->aoffLoadSegments[idxSeg] != UINTPTR_MAX
            && pThis->aoffLoadSegments[idxSeg] != s_aStandardSyms[i].uAddr - pSym->n_value)
            return VERR_INTERNAL_ERROR_2;

        pThis->aoffLoadSegments[idxSeg] = s_aStandardSyms[i].uAddr - pSym->n_value;
#elif defined(IN_RING3)
        pThis->aoffLoadSegments[idxSeg] = 0;
#else
# error "Either IN_RING0 or IN_RING3 msut be defined"
#endif
    }

    return VINF_SUCCESS;
}


/**
 * Check the symbol table against symbols we known symbols.
 *
 * This is done to detect whether the on disk image and the in
 * memory images matches.  Mismatches could stem from user
 * replacing the default kernel image on disk.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal scratch data.
 * @param   pszKernelFile       The name of the kernel file.
 */
static int rtR0DbgKrnlDarwinCheckStandardSymbols(RTDBGKRNLINFOINT *pThis, const char *pszKernelFile)
{
    static struct
    {
        const char *pszName;
        uintptr_t   uAddr;
    } const s_aStandardCandles[] =
    {
#ifdef IN_RING0
# define KNOWN_ENTRY(a_Sym)  { #a_Sym, (uintptr_t)&a_Sym }
#else
# define KNOWN_ENTRY(a_Sym)  { #a_Sym, 0 }
#endif
        /* IOKit: */
        KNOWN_ENTRY(IOMalloc),
        KNOWN_ENTRY(IOFree),
        KNOWN_ENTRY(IOSleep),
        KNOWN_ENTRY(IORWLockAlloc),
        KNOWN_ENTRY(IORecursiveLockLock),
        KNOWN_ENTRY(IOSimpleLockAlloc),
        KNOWN_ENTRY(PE_cpu_halt),
        KNOWN_ENTRY(gIOKitDebug),
        KNOWN_ENTRY(gIOServicePlane),
        KNOWN_ENTRY(ev_try_lock),

        /* Libkern: */
        KNOWN_ENTRY(OSAddAtomic),
        KNOWN_ENTRY(OSBitAndAtomic),
        KNOWN_ENTRY(OSBitOrAtomic),
        KNOWN_ENTRY(OSBitXorAtomic),
        KNOWN_ENTRY(OSCompareAndSwap),
        KNOWN_ENTRY(OSMalloc),
        KNOWN_ENTRY(OSlibkernInit),
        KNOWN_ENTRY(bcmp),
        KNOWN_ENTRY(copyout),
        KNOWN_ENTRY(copyin),
        KNOWN_ENTRY(kprintf),
        KNOWN_ENTRY(printf),
        KNOWN_ENTRY(lck_grp_alloc_init),
        KNOWN_ENTRY(lck_mtx_alloc_init),
        KNOWN_ENTRY(lck_rw_alloc_init),
        KNOWN_ENTRY(lck_spin_alloc_init),
        KNOWN_ENTRY(osrelease),
        KNOWN_ENTRY(ostype),
        KNOWN_ENTRY(panic),
        KNOWN_ENTRY(strprefix),
        //KNOWN_ENTRY(sysctlbyname), - we get kernel_sysctlbyname from the 10.10+ kernels.
        KNOWN_ENTRY(vsscanf),
        KNOWN_ENTRY(page_mask),

        /* Mach: */
        KNOWN_ENTRY(absolutetime_to_nanoseconds),
        KNOWN_ENTRY(assert_wait),
        KNOWN_ENTRY(clock_delay_until),
        KNOWN_ENTRY(clock_get_uptime),
        KNOWN_ENTRY(current_task),
        KNOWN_ENTRY(current_thread),
        KNOWN_ENTRY(kernel_task),
        KNOWN_ENTRY(lck_mtx_sleep),
        KNOWN_ENTRY(lck_rw_sleep),
        KNOWN_ENTRY(lck_spin_sleep),
        KNOWN_ENTRY(mach_absolute_time),
        KNOWN_ENTRY(semaphore_create),
        KNOWN_ENTRY(task_reference),
        KNOWN_ENTRY(thread_block),
        KNOWN_ENTRY(thread_reference),
        KNOWN_ENTRY(thread_terminate),
        KNOWN_ENTRY(thread_wakeup_prim),

        /* BSDKernel: */
        KNOWN_ENTRY(buf_size),
        KNOWN_ENTRY(copystr),
        KNOWN_ENTRY(current_proc),
        KNOWN_ENTRY(kauth_getuid),
#if MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
        KNOWN_ENTRY(kauth_cred_unref),
#else
        KNOWN_ENTRY(kauth_cred_rele),
#endif
        KNOWN_ENTRY(msleep),
        KNOWN_ENTRY(nanotime),
        KNOWN_ENTRY(nop_close),
        KNOWN_ENTRY(proc_pid),
#if MAC_OS_X_VERSION_MIN_REQUIRED < 101500
        KNOWN_ENTRY(mbuf_data),
        KNOWN_ENTRY(ifnet_hdrlen),
        KNOWN_ENTRY(ifnet_set_promiscuous),
        KNOWN_ENTRY(sock_accept),
        KNOWN_ENTRY(sockopt_name),
#endif
        //KNOWN_ENTRY(spec_write),
        KNOWN_ENTRY(suword),
        //KNOWN_ENTRY(sysctl_int),
        KNOWN_ENTRY(uio_rw),
        KNOWN_ENTRY(vfs_flags),
        KNOWN_ENTRY(vfs_name),
        KNOWN_ENTRY(vfs_statfs),
        KNOWN_ENTRY(VNOP_READ),
        KNOWN_ENTRY(uio_create),
        KNOWN_ENTRY(uio_addiov),
        KNOWN_ENTRY(uio_free),
        KNOWN_ENTRY(vnode_get),
        KNOWN_ENTRY(vnode_open),
        KNOWN_ENTRY(vnode_ref),
        KNOWN_ENTRY(vnode_rele),
        KNOWN_ENTRY(vnop_close_desc),
        KNOWN_ENTRY(wakeup),
        KNOWN_ENTRY(wakeup_one),

        /* Unsupported: */
        KNOWN_ENTRY(kdp_set_interface),
        KNOWN_ENTRY(pmap_find_phys),
        KNOWN_ENTRY(vm_map),
        KNOWN_ENTRY(vm_protect),
        KNOWN_ENTRY(vm_region),
        KNOWN_ENTRY(vm_map_unwire), /* vm_map_wire has an alternative symbol, vm_map_wire_external, in 10.11  */
        KNOWN_ENTRY(PE_kputc),
        KNOWN_ENTRY(kernel_map),
        KNOWN_ENTRY(kernel_pmap),
#undef KNOWN_ENTRY
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aStandardCandles); i++)
    {
        uintptr_t uAddr = rtR0DbgKrnlDarwinLookup(pThis, s_aStandardCandles[i].pszName);
#ifdef IN_RING0
        if (uAddr != s_aStandardCandles[i].uAddr)
#else
        if (uAddr == 0)
#endif
        {
#if defined(IN_RING0) && defined(DEBUG_bird)
            kprintf("RTR0DbgKrnlInfoOpen: error: %s (%p != %p) in %s\n",
                    s_aStandardCandles[i].pszName, (void *)uAddr, (void *)s_aStandardCandles[i].uAddr, pszKernelFile);
#endif
            printf("RTR0DbgKrnlInfoOpen: error: %s (%p != %p) in %s\n",
                   s_aStandardCandles[i].pszName, (void *)uAddr, (void *)s_aStandardCandles[i].uAddr, pszKernelFile);
            return VERR_INTERNAL_ERROR_2;
        }
    }
    return VINF_SUCCESS;
}


/**
 * Loads and validates the symbol and string tables.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal scratch data.
 * @param   pszKernelFile       The name of the kernel file.
 */
static int rtR0DbgKrnlDarwinParseSymTab(RTDBGKRNLINFOINT *pThis, const char *pszKernelFile)
{
    /*
     * The first string table symbol must be a zero length name.
     */
    if (pThis->pachStrTab[0] != '\0')
        RETURN_VERR_BAD_EXE_FORMAT;

    /*
     * Validate the symbol table.
     */
    const char     *pszPrev = "";
    uint32_t const  cSyms   = pThis->cSyms;
    MY_NLIST const  *pSym   = pThis->paSyms;
    for (uint32_t iSym = 0; iSym < cSyms; iSym++, pSym++)
    {
        if ((uint32_t)pSym->n_un.n_strx >= pThis->cbStrTab)
        {
            LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Symbol #%u has a bad string table index: %#x vs cbStrTab=%#x\n",
                        pszKernelFile, iSym, pSym->n_un.n_strx, pThis->cbStrTab);
            RETURN_VERR_BAD_EXE_FORMAT;
        }
        const char *pszSym = &pThis->pachStrTab[(uint32_t)pSym->n_un.n_strx];
#ifdef IN_RING3
        RTAssertMsg2("%05i: %02x:%08llx %02x %04x %s\n", iSym, pSym->n_sect, (uint64_t)pSym->n_value, pSym->n_type, pSym->n_desc, pszSym);
#endif

        if (strcmp(pszSym, pszPrev) < 0)
            RETURN_VERR_BAD_EXE_FORMAT; /* not sorted */

        if (!(pSym->n_type & MACHO_N_STAB))
        {
            switch (pSym->n_type & MACHO_N_TYPE)
            {
                case MACHO_N_SECT:
                    if (pSym->n_sect == MACHO_NO_SECT)
                    {
                        LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Symbol #%u '%s' problem: n_sect = MACHO_NO_SECT\n",
                                    pszKernelFile, iSym, pszSym);
                        RETURN_VERR_BAD_EXE_FORMAT;
                    }
                    if (pSym->n_sect > pThis->cSections)
                    {
                        LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Symbol #%u '%s' problem: n_sect (%u) is higher than cSections (%u)\n",
                                    pszKernelFile, iSym, pszSym, pSym->n_sect, pThis->cSections);
                        RETURN_VERR_BAD_EXE_FORMAT;
                    }
                    if (pSym->n_desc & ~(REFERENCED_DYNAMICALLY | N_WEAK_DEF))
                    {
                        LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Symbol #%u '%s' problem: Unexpected value n_desc=%#x\n",
                                    pszKernelFile, iSym, pszSym, pSym->n_desc);
                        RETURN_VERR_BAD_EXE_FORMAT;
                    }
                    if (   pSym->n_value < pThis->apSections[pSym->n_sect - 1]->addr
                        && strcmp(pszSym, "__mh_execute_header"))    /* in 10.8 it's no longer absolute (PIE?). */
                    {
                        LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Symbol #%u '%s' problem: n_value (%#llx) < section addr (%#llx)\n",
                                    pszKernelFile, iSym, pszSym, (uint64_t)pSym->n_value,
                                    (uint64_t)pThis->apSections[pSym->n_sect - 1]->addr);
                        RETURN_VERR_BAD_EXE_FORMAT;
                    }
                    if (      pSym->n_value - pThis->apSections[pSym->n_sect - 1]->addr
                           > pThis->apSections[pSym->n_sect - 1]->size
                        && strcmp(pszSym, "__mh_execute_header"))    /* see above. */
                    {
                        LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Symbol #%u '%s' problem: n_value (%#llx) >= end of section (%#llx + %#llx)\n",
                                    pszKernelFile, iSym, pszSym, (uint64_t)pSym->n_value,
                                    (uint64_t)pThis->apSections[pSym->n_sect - 1]->addr,
                                    (uint64_t)pThis->apSections[pSym->n_sect - 1]->size);
                        RETURN_VERR_BAD_EXE_FORMAT;
                    }
                    break;

                case MACHO_N_ABS:
                    if (   pSym->n_sect != MACHO_NO_SECT
                        && (   strcmp(pszSym, "__mh_execute_header") /* n_sect=1 in 10.7/amd64 */
                            || pSym->n_sect > pThis->cSections) )
                    {
                        LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Abs symbol #%u '%s' problem: n_sect (%u) is not MACHO_NO_SECT (cSections is %u)\n",
                                    pszKernelFile, iSym, pszSym, pSym->n_sect, pThis->cSections);
                        RETURN_VERR_BAD_EXE_FORMAT;
                    }
                    if (pSym->n_desc & ~(REFERENCED_DYNAMICALLY | N_WEAK_DEF))
                    {
                        LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Abs symbol #%u '%s' problem: Unexpected value n_desc=%#x\n",
                                    pszKernelFile, iSym, pszSym, pSym->n_desc);
                        RETURN_VERR_BAD_EXE_FORMAT;
                    }
                    break;

                case MACHO_N_UNDF:
                    /* No undefined or common symbols in the kernel. */
                    LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Unexpected undefined symbol #%u '%s'\n", pszKernelFile, iSym, pszSym);
                    RETURN_VERR_BAD_EXE_FORMAT;

                case MACHO_N_INDR:
                    /* No indirect symbols in the kernel. */
                    LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Unexpected indirect symbol #%u '%s'\n", pszKernelFile, iSym, pszSym);
                    RETURN_VERR_BAD_EXE_FORMAT;

                case MACHO_N_PBUD:
                    /* No prebound symbols in the kernel. */
                    LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Unexpected prebound symbol #%u '%s'\n", pszKernelFile, iSym, pszSym);
                    RETURN_VERR_BAD_EXE_FORMAT;

                default:
                    LOG_BAD_SYM("RTR0DbgKrnlInfoOpen: %s: Unexpected symbol n_type %#x for symbol #%u '%s'\n",
                                pszKernelFile, pSym->n_type, iSym, pszSym);
                    RETURN_VERR_BAD_EXE_FORMAT;
            }
        }
        /* else: Ignore debug symbols. */
    }

    return VINF_SUCCESS;
}


/**
 * Uses the segment table to translate a file offset into a virtual memory
 * address.
 *
 * @returns The virtual memory address on success, 0 if not found.
 * @param   pThis               The instance.
 * @param   offFile             The file offset to translate.
 */
static uintptr_t rtR0DbgKrnlDarwinFileOffToVirtAddr(RTDBGKRNLINFOINT *pThis, uint64_t offFile)
{
    uint32_t iSeg = pThis->cSegments;
    while (iSeg-- > 0)
    {
        uint64_t offSeg = offFile - pThis->apSegments[iSeg]->fileoff;
        if (offSeg < pThis->apSegments[iSeg]->vmsize)
            return pThis->apSegments[iSeg]->vmaddr + (uintptr_t)offSeg;
    }
    return 0;
}


/**
 * Parses and validates the load commands.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal scratch data.
 */
static int rtR0DbgKrnlDarwinParseCommands(RTDBGKRNLINFOINT *pThis)
{
    Assert(pThis->pLoadCmds);

    /*
     * Reset the state.
     */
    pThis->offStrTab        = 0;
    pThis->cbStrTab         = 0;
    pThis->offSyms          = 0;
    pThis->cSyms            = 0;
    pThis->cSections        = 0;
    pThis->uTextSegLinkAddr = 0;
    pThis->cbTextSeg        = 0;
    pThis->uMinOsVer        = 0;
    pThis->uSdkVer          = 0;
    pThis->uSrcVer          = 0;

    /*
     * Validate the relevant commands, picking up sections and the symbol
     * table location.
     */
    load_command_t const   *pCmd = pThis->pLoadCmds;
    for (uint32_t iCmd = 0; ; iCmd++)
    {
        /* cmd index & offset. */
        uintptr_t offCmd = (uintptr_t)pCmd - (uintptr_t)pThis->pLoadCmds;
        if (offCmd == pThis->cbLoadCmds && iCmd == pThis->cLoadCmds)
            break;
        if (offCmd + sizeof(*pCmd) > pThis->cbLoadCmds)
            RETURN_VERR_BAD_EXE_FORMAT;
        if (iCmd >= pThis->cLoadCmds)
            RETURN_VERR_BAD_EXE_FORMAT;

        /* cmdsize */
        if (pCmd->cmdsize < sizeof(*pCmd))
            RETURN_VERR_BAD_EXE_FORMAT;
        if (pCmd->cmdsize > pThis->cbLoadCmds)
            RETURN_VERR_BAD_EXE_FORMAT;
        if (RT_ALIGN_32(pCmd->cmdsize, 4) != pCmd->cmdsize)
            RETURN_VERR_BAD_EXE_FORMAT;

        /* cmd */
        switch (pCmd->cmd & ~LC_REQ_DYLD)
        {
            /* Validate and store the symbol table details. */
            case LC_SYMTAB:
            {
                struct symtab_command const *pSymTab = (struct symtab_command const *)pCmd;
                if (pSymTab->cmdsize != sizeof(*pSymTab))
                    RETURN_VERR_BAD_EXE_FORMAT;
                if (pSymTab->nsyms > _1M)
                    RETURN_VERR_BAD_EXE_FORMAT;
                if (pSymTab->strsize > _2M)
                    RETURN_VERR_BAD_EXE_FORMAT;

                pThis->offStrTab = pSymTab->stroff;
                pThis->cbStrTab  = pSymTab->strsize;
                pThis->offSyms   = pSymTab->symoff;
                pThis->cSyms     = pSymTab->nsyms;
                break;
            }

            /* Validate the segment. */
#if ARCH_BITS == 32
            case LC_SEGMENT_32:
#elif ARCH_BITS == 64
            case LC_SEGMENT_64:
#else
# error ARCH_BITS
#endif
            {
                MY_SEGMENT_COMMAND const *pSeg = (MY_SEGMENT_COMMAND const *)pCmd;
                if (pSeg->cmdsize < sizeof(*pSeg))
                    RETURN_VERR_BAD_EXE_FORMAT;

                if (pSeg->segname[0] == '\0')
                    RETURN_VERR_BAD_EXE_FORMAT;

                if (pSeg->nsects > MACHO_MAX_SECT)
                    RETURN_VERR_BAD_EXE_FORMAT;
                if (pSeg->nsects * sizeof(MY_SECTION) + sizeof(*pSeg) != pSeg->cmdsize)
                    RETURN_VERR_BAD_EXE_FORMAT;

                if (pSeg->flags & ~(SG_HIGHVM | SG_FVMLIB | SG_NORELOC | SG_PROTECTED_VERSION_1))
                    RETURN_VERR_BAD_EXE_FORMAT;

                if (   pSeg->vmaddr != 0
                    || !strcmp(pSeg->segname, "__PAGEZERO"))
                {
                    if (pSeg->vmaddr + RT_ALIGN_Z(pSeg->vmsize, RT_BIT_32(12)) < pSeg->vmaddr)
                        RETURN_VERR_BAD_EXE_FORMAT;
                }
                else if (pSeg->vmsize)
                    RETURN_VERR_BAD_EXE_FORMAT;

                if (pSeg->maxprot & ~VM_PROT_ALL)
                    RETURN_VERR_BAD_EXE_FORMAT;
                if (pSeg->initprot & ~VM_PROT_ALL)
                    RETURN_VERR_BAD_EXE_FORMAT;

                /* Validate the sections. */
                uint32_t            uAlignment = 0;
                MY_SECTION const   *paSects    = (MY_SECTION const *)(pSeg + 1);
                for (uint32_t i = 0; i < pSeg->nsects; i++)
                {
                    if (paSects[i].sectname[0] == '\0')
                        RETURN_VERR_BAD_EXE_FORMAT;
                    if (memcmp(paSects[i].segname, pSeg->segname, sizeof(pSeg->segname)))
                        RETURN_VERR_BAD_EXE_FORMAT;

                    switch (paSects[i].flags & SECTION_TYPE)
                    {
                        case S_REGULAR:
                        case S_CSTRING_LITERALS:
                        case S_NON_LAZY_SYMBOL_POINTERS:
                        case S_MOD_INIT_FUNC_POINTERS:
                        case S_MOD_TERM_FUNC_POINTERS:
                        case S_COALESCED:
                        case S_4BYTE_LITERALS:
                            if (  pSeg->filesize != 0
                                ? paSects[i].offset - pSeg->fileoff >= pSeg->filesize
                                : paSects[i].offset - pSeg->fileoff != pSeg->filesize)
                                RETURN_VERR_BAD_EXE_FORMAT;
                            if (   paSects[i].addr != 0
                                && paSects[i].offset - pSeg->fileoff != paSects[i].addr - pSeg->vmaddr)
                                RETURN_VERR_BAD_EXE_FORMAT;
                            break;

                        case S_ZEROFILL:
                            if (paSects[i].offset != 0)
                                RETURN_VERR_BAD_EXE_FORMAT;
                            break;

                        /* not observed */
                        case S_SYMBOL_STUBS:
                        case S_INTERPOSING:
                        case S_8BYTE_LITERALS:
                        case S_16BYTE_LITERALS:
                        case S_DTRACE_DOF:
                        case S_LAZY_SYMBOL_POINTERS:
                        case S_LAZY_DYLIB_SYMBOL_POINTERS:
                            RETURN_VERR_LDR_UNEXPECTED;
                        case S_GB_ZEROFILL:
                            RETURN_VERR_LDR_UNEXPECTED;
                        default:
                            RETURN_VERR_BAD_EXE_FORMAT;
                    }

                    if (paSects[i].align > 12)
                        RETURN_VERR_BAD_EXE_FORMAT;
                    if (paSects[i].align > uAlignment)
                        uAlignment = paSects[i].align;

                    /* Add to the section table. */
                    if (pThis->cSections >= RT_ELEMENTS(pThis->apSections))
                        RETURN_VERR_BAD_EXE_FORMAT;
                    pThis->auSections2Segment[pThis->cSections] = pThis->cSegments;
                    pThis->apSections[pThis->cSections++] = &paSects[i];
                }

                if (RT_ALIGN_Z(pSeg->vmaddr, RT_BIT_32(uAlignment)) != pSeg->vmaddr)
                    RETURN_VERR_BAD_EXE_FORMAT;
                if (   pSeg->filesize > RT_ALIGN_Z(pSeg->vmsize, RT_BIT_32(uAlignment))
                    && pSeg->vmsize != 0)
                    RETURN_VERR_BAD_EXE_FORMAT;

                /*
                 * Add to the segment table.
                 */
                if (pThis->cSegments >= RT_ELEMENTS(pThis->apSegments))
                    RETURN_VERR_BAD_EXE_FORMAT;
                pThis->apSegments[pThis->cSegments++] = pSeg;

                /*
                 * Take down the text segment size and link address (for in-mem variant):
                 */
                if (!strcmp(pSeg->segname, "__TEXT"))
                {
                    if (pThis->cbTextSeg != 0)
                        RETURN_VERR_BAD_EXE_FORMAT;
                    pThis->uTextSegLinkAddr = pSeg->vmaddr;
                    pThis->cbTextSeg        = pSeg->vmsize;
                }
                break;
            }

            case LC_UUID:
                if (pCmd->cmdsize != sizeof(uuid_command))
                    RETURN_VERR_BAD_EXE_FORMAT;
                break;

            case LC_DYSYMTAB:
            case LC_UNIXTHREAD:
            case LC_CODE_SIGNATURE:
            case LC_VERSION_MIN_MACOSX:
            case LC_FUNCTION_STARTS:
            case LC_MAIN:
            case LC_DATA_IN_CODE:
            case LC_ENCRYPTION_INFO_64:
            case LC_LINKER_OPTION:
            case LC_LINKER_OPTIMIZATION_HINT:
            case LC_VERSION_MIN_TVOS:
            case LC_VERSION_MIN_WATCHOS:
            case LC_NOTE:
            case LC_SEGMENT_SPLIT_INFO:
                break;

            case LC_BUILD_VERSION:
                if (pCmd->cmdsize >= RT_UOFFSETOF(build_version_command_t, aTools))
                {
                    build_version_command_t *pBldVerCmd = (build_version_command_t *)pCmd;
                    pThis->uMinOsVer = pBldVerCmd->minos;
                    pThis->uSdkVer   = pBldVerCmd->sdk;
                }
                break;

            case LC_SOURCE_VERSION:
                if (pCmd->cmdsize == sizeof(source_version_command_t))
                {
                    source_version_command_t *pSrcVerCmd = (source_version_command_t *)pCmd;
                    pThis->uSrcVer = pSrcVerCmd->version;
                }
                break;

            /* not observed */
            case LC_SYMSEG:
#if ARCH_BITS == 32
            case LC_SEGMENT_64:
#elif ARCH_BITS == 64
            case LC_SEGMENT_32:
#endif
            case LC_ROUTINES_64:
            case LC_ROUTINES:
            case LC_THREAD:
            case LC_LOADFVMLIB:
            case LC_IDFVMLIB:
            case LC_IDENT:
            case LC_FVMFILE:
            case LC_PREPAGE:
            case LC_TWOLEVEL_HINTS:
            case LC_PREBIND_CKSUM:
            case LC_ENCRYPTION_INFO:
                RETURN_VERR_LDR_UNEXPECTED;

            /* no phones here yet */
            case LC_VERSION_MIN_IPHONEOS:
                RETURN_VERR_LDR_UNEXPECTED;

            /* dylib */
            case LC_LOAD_DYLIB:
            case LC_ID_DYLIB:
            case LC_LOAD_DYLINKER:
            case LC_ID_DYLINKER:
            case LC_PREBOUND_DYLIB:
            case LC_LOAD_WEAK_DYLIB & ~LC_REQ_DYLD:
            case LC_SUB_FRAMEWORK:
            case LC_SUB_UMBRELLA:
            case LC_SUB_CLIENT:
            case LC_SUB_LIBRARY:
            case LC_RPATH:
            case LC_REEXPORT_DYLIB:
            case LC_LAZY_LOAD_DYLIB:
            case LC_DYLD_INFO:
            case LC_DYLD_INFO_ONLY:
            case LC_LOAD_UPWARD_DYLIB:
            case LC_DYLD_ENVIRONMENT:
            case LC_DYLIB_CODE_SIGN_DRS:
                RETURN_VERR_LDR_UNEXPECTED;

            default:
                RETURN_VERR_BAD_EXE_FORMAT;
        }

        /* next */
        pCmd = (load_command_t *)((uintptr_t)pCmd + pCmd->cmdsize);
    }

    /*
     * Try figure out the virtual addresses for the symbol and string tables.
     */
    if (pThis->cbStrTab > 0)
        pThis->uStrTabLinkAddr = rtR0DbgKrnlDarwinFileOffToVirtAddr(pThis, pThis->offStrTab);
    if (pThis->cSyms > 0)
        pThis->uSymTabLinkAddr = rtR0DbgKrnlDarwinFileOffToVirtAddr(pThis, pThis->offSyms);

    return VINF_SUCCESS;
}


/**
 * Loads and validates the symbol and string tables.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal scratch data.
 * @param   pszKernelFile       The name of the kernel file.
 */
static int rtR0DbgKrnlDarwinLoadSymTab(RTDBGKRNLINFOINT *pThis, const char *pszKernelFile)
{
    /*
     * Load the tables.
     */
    int rc;
    pThis->paSyms = (MY_NLIST *)RTMemAllocZ(pThis->cSyms * sizeof(MY_NLIST));
    if (pThis->paSyms)
    {
        rc = RTFileReadAt(pThis->hFile, pThis->offArch + pThis->offSyms, pThis->paSyms, pThis->cSyms * sizeof(MY_NLIST), NULL);
        if (RT_SUCCESS(rc))
        {
            pThis->pachStrTab = (char *)RTMemAllocZ(pThis->cbStrTab + 1);
            if (pThis->pachStrTab)
            {
                rc = RTFileReadAt(pThis->hFile, pThis->offArch + pThis->offStrTab, pThis->pachStrTab, pThis->cbStrTab, NULL);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Join paths with the in-memory code path.
                     */
                    rc = rtR0DbgKrnlDarwinParseSymTab(pThis, pszKernelFile);
                }
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Loads the load commands and validates them.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal scratch data.
 */
static int rtR0DbgKrnlDarwinLoadCommands(RTDBGKRNLINFOINT *pThis)
{
    int rc;
    pThis->pLoadCmds = (load_command_t *)RTMemAlloc(pThis->cbLoadCmds);
    if (pThis->pLoadCmds)
    {
        rc = RTFileReadAt(pThis->hFile, pThis->offArch + sizeof(MY_MACHO_HEADER), pThis->pLoadCmds, pThis->cbLoadCmds, NULL);
        if (RT_SUCCESS(rc))
            rc = rtR0DbgKrnlDarwinParseCommands(pThis);
    }
    else
        rc = VERR_NO_MEMORY;
   return rc;
}


/**
 * Loads the FAT and MACHO headers, noting down the relevant info.
 *
 * @returns IPRT status code.
 * @param   pThis               The internal scratch data.
 */
static int rtR0DbgKrnlDarwinLoadFileHeaders(RTDBGKRNLINFOINT *pThis)
{
    uint32_t i;

    pThis->offArch = 0;
    pThis->cbArch  = 0;

    /*
     * Read the first bit of the file, parse the FAT if found there.
     */
    int rc = RTFileReadAt(pThis->hFile, 0, pThis->abBuf, sizeof(fat_header_t) + sizeof(fat_arch_t) * 16, NULL);
    if (RT_FAILURE(rc))
        return rc;

    fat_header_t   *pFat        = (fat_header *)pThis->abBuf;
    fat_arch_t     *paFatArches = (fat_arch_t *)(pFat + 1);

    /* Correct FAT endian first. */
    if (pFat->magic == IMAGE_FAT_SIGNATURE_OE)
    {
        pFat->magic     = RT_BSWAP_U32(pFat->magic);
        pFat->nfat_arch = RT_BSWAP_U32(pFat->nfat_arch);
        i = RT_MIN(pFat->nfat_arch, 16);
        while (i-- > 0)
        {
            paFatArches[i].cputype    = RT_BSWAP_U32(paFatArches[i].cputype);
            paFatArches[i].cpusubtype = RT_BSWAP_U32(paFatArches[i].cpusubtype);
            paFatArches[i].offset     = RT_BSWAP_U32(paFatArches[i].offset);
            paFatArches[i].size       = RT_BSWAP_U32(paFatArches[i].size);
            paFatArches[i].align      = RT_BSWAP_U32(paFatArches[i].align);
        }
    }

    /* Lookup our architecture in the FAT. */
    if (pFat->magic == IMAGE_FAT_SIGNATURE)
    {
        if (pFat->nfat_arch > 16)
            RETURN_VERR_BAD_EXE_FORMAT;

        for (i = 0; i < pFat->nfat_arch; i++)
        {
            if (   paFatArches[i].cputype == MY_CPU_TYPE
                && paFatArches[i].cpusubtype == MY_CPU_SUBTYPE_ALL)
            {
                pThis->offArch = paFatArches[i].offset;
                pThis->cbArch  = paFatArches[i].size;
                if (!pThis->cbArch)
                    RETURN_VERR_BAD_EXE_FORMAT;
                if (pThis->offArch < sizeof(fat_header_t) + sizeof(fat_arch_t) * pFat->nfat_arch)
                    RETURN_VERR_BAD_EXE_FORMAT;
                if (pThis->offArch + pThis->cbArch <= pThis->offArch)
                    RETURN_VERR_LDR_ARCH_MISMATCH;
                break;
            }
        }
        if (i >= pFat->nfat_arch)
            RETURN_VERR_LDR_ARCH_MISMATCH;
    }

    /*
     * Read the Mach-O header and validate it.
     */
    rc = RTFileReadAt(pThis->hFile, pThis->offArch, pThis->abBuf, sizeof(MY_MACHO_HEADER), NULL);
    if (RT_FAILURE(rc))
        return rc;
    MY_MACHO_HEADER const *pHdr = (MY_MACHO_HEADER const *)pThis->abBuf;
    if (pHdr->magic != MY_MACHO_MAGIC)
    {
        if (   pHdr->magic == IMAGE_MACHO32_SIGNATURE
            || pHdr->magic == IMAGE_MACHO32_SIGNATURE_OE
            || pHdr->magic == IMAGE_MACHO64_SIGNATURE
            || pHdr->magic == IMAGE_MACHO64_SIGNATURE_OE)
            RETURN_VERR_LDR_ARCH_MISMATCH;
        RETURN_VERR_BAD_EXE_FORMAT;
    }

    if (pHdr->cputype    != MY_CPU_TYPE)
        RETURN_VERR_LDR_ARCH_MISMATCH;
    if (pHdr->cpusubtype != MY_CPU_SUBTYPE_ALL)
        RETURN_VERR_LDR_ARCH_MISMATCH;
    if (pHdr->filetype   != MH_EXECUTE)
        RETURN_VERR_LDR_UNEXPECTED;
    if (pHdr->ncmds      < 4)
        RETURN_VERR_LDR_UNEXPECTED;
    if (pHdr->ncmds      > 256)
        RETURN_VERR_LDR_UNEXPECTED;
    if (pHdr->sizeofcmds <= pHdr->ncmds * sizeof(load_command_t))
        RETURN_VERR_LDR_UNEXPECTED;
    if (pHdr->sizeofcmds >= _1M)
        RETURN_VERR_LDR_UNEXPECTED;
    if (pHdr->flags & ~MH_VALID_FLAGS)
        RETURN_VERR_LDR_UNEXPECTED;

    pThis->cLoadCmds  = pHdr->ncmds;
    pThis->cbLoadCmds = pHdr->sizeofcmds;
    return VINF_SUCCESS;
}


/**
 * Destructor.
 *
 * @param   pThis               The instance to destroy.
 */
static void rtR0DbgKrnlDarwinDtor(RTDBGKRNLINFOINT *pThis)
{
    pThis->u32Magic = ~RTDBGKRNLINFO_MAGIC;

    if (!pThis->fIsInMem)
        RTMemFree(pThis->pachStrTab);
    pThis->pachStrTab = NULL;

    if (!pThis->fIsInMem)
        RTMemFree(pThis->paSyms);
    pThis->paSyms = NULL;

    RTMemFree(pThis);
}


/**
 * Completes a handle, logging details.
 *
 * @returns VINF_SUCCESS
 * @param   phKrnlInfo      Where to return the handle.
 * @param   pThis           The instance to complete.
 * @param   pszKernelFile   What kernel file it's based on.
 */
static int rtR0DbgKrnlDarwinSuccess(PRTDBGKRNLINFO phKrnlInfo, RTDBGKRNLINFOINT *pThis, const char *pszKernelFile)
{
    pThis->u32Magic = RTDBGKRNLINFO_MAGIC;
    pThis->cRefs    = 1;

#if defined(DEBUG) || defined(IN_RING3)
    LOG_SUCCESS("RTR0DbgKrnlInfoOpen: Found: %#zx + %#zx - %s\n", pThis->uTextSegLinkAddr, pThis->offLoad, pszKernelFile);
#else
    LOG_SUCCESS("RTR0DbgKrnlInfoOpen: Found: %s\n", pszKernelFile);
#endif
    LOG_SUCCESS("RTR0DbgKrnlInfoOpen: SDK version: %u.%u.%u  MinOS version: %u.%u.%u  Source version: %u.%u.%u.%u.%u\n",
                pThis->uSdkVer   >> 16, (pThis->uSdkVer   >> 8) & 0xff, pThis->uSdkVer   & 0xff,
                pThis->uMinOsVer >> 16, (pThis->uMinOsVer >> 8) & 0xff, pThis->uMinOsVer & 0xff,
                (uint32_t)(pThis->uSrcVer >> 40),
                (uint32_t)(pThis->uSrcVer >> 30) & 0x3ff,
                (uint32_t)(pThis->uSrcVer >> 20) & 0x3ff,
                (uint32_t)(pThis->uSrcVer >> 10) & 0x3ff,
                (uint32_t)(pThis->uSrcVer)       & 0x3ff);

    *phKrnlInfo = pThis;
    return VINF_SUCCESS;
}


static int rtR0DbgKrnlDarwinOpen(PRTDBGKRNLINFO phKrnlInfo, const char *pszKernelFile)
{
    RTDBGKRNLINFOINT *pThis = (RTDBGKRNLINFOINT *)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    pThis->hFile = NIL_RTFILE;

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aoffLoadSegments); i++)
        pThis->aoffLoadSegments[i] = UINTPTR_MAX;

    int rc = RTFileOpen(&pThis->hFile, pszKernelFile, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(rc))
        rc = rtR0DbgKrnlDarwinLoadFileHeaders(pThis);
    if (RT_SUCCESS(rc))
        rc = rtR0DbgKrnlDarwinLoadCommands(pThis);
    if (RT_SUCCESS(rc))
        rc = rtR0DbgKrnlDarwinLoadSymTab(pThis, pszKernelFile);
    if (RT_SUCCESS(rc))
    {
        rc = rtR0DbgKrnlDarwinInitLoadDisplacements(pThis);
        if (RT_SUCCESS(rc))
            rc = rtR0DbgKrnlDarwinCheckStandardSymbols(pThis, pszKernelFile);
    }

    rtR0DbgKrnlDarwinLoadDone(pThis);
    if (RT_SUCCESS(rc))
        rtR0DbgKrnlDarwinSuccess(phKrnlInfo, pThis, pszKernelFile);
    else
        rtR0DbgKrnlDarwinDtor(pThis);
    return rc;
}


#ifdef IN_RING0

/**
 * Checks if a page is present.
 * @returns true if it is, false if it isn't.
 * @param   uPageAddr   The address of/in the page to check.
 */
static bool rtR0DbgKrnlDarwinIsPagePresent(uintptr_t uPageAddr)
{
    /** @todo the dtrace code subjects the result to pmap_is_valid, but that
     *        isn't exported, so we'll have to make to with != 0 here. */
    return pmap_find_phys(kernel_pmap, uPageAddr) != 0;
}


/**
 * Used to check whether a memory range is present or not.
 *
 * This is applied to the to the load commands and selected portions of the link
 * edit segment.
 *
 * @returns true if all present, false if not.
 * @param   uAddress    The start address.
 * @param   cb          Number of bytes to check.
 * @param   pszWhat     What we're checking, for logging.
 * @param   pHdr        The header address (for logging).
 */
static bool rtR0DbgKrnlDarwinIsRangePresent(uintptr_t uAddress, size_t cb,
                                            const char *pszWhat, MY_MACHO_HEADER const volatile *pHdr)
{
    uintptr_t const uStartAddress = uAddress;
    intptr_t        cPages        = RT_ALIGN_Z(cb + (uAddress & PAGE_OFFSET_MASK), PAGE_SIZE);
    RT_NOREF(uStartAddress, pszWhat, pHdr);
    for (;;)
    {
        if (!rtR0DbgKrnlDarwinIsPagePresent(uAddress))
        {
            LOG_NOT_PRESENT("RTR0DbgInfo: %p: Page in %s is not present: %#zx - rva %#zx; in structure %#zx (%#zx LB %#zx)\n",
                            (void *)pHdr, pszWhat, uAddress, uAddress - (uintptr_t)pHdr, uAddress - uStartAddress, uStartAddress, cb);
            return false;
        }

        cPages -= 1;
        if (cPages <= 0)
            uAddress += PAGE_SIZE;
        else
            return true;
    }
}


/**
 * Try "open" the in-memory kernel image
 *
 * @returns IPRT stauts code
 * @param   phKrnlInfo          Where to return the info instance on success.
 */
static int rtR0DbgKrnlDarwinOpenInMemory(PRTDBGKRNLINFO phKrnlInfo)
{
    RTDBGKRNLINFOINT *pThis = (RTDBGKRNLINFOINT *)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;
    pThis->hFile    = NIL_RTFILE;
    pThis->fIsInMem = true;

    for (uint32_t i = 0; i < RT_ELEMENTS(pThis->aoffLoadSegments); i++)
        pThis->aoffLoadSegments[i] = UINTPTR_MAX;

    /*
     * Figure the search range based on a symbol that is supposed to be in
     * kernel text segment, using it as the upper boundrary.  The lower boundary
     * is determined by subtracting a max kernel size of 64MB (the largest kernel
     * file, kernel.kasan, is around 45MB, but the end of __TEXT is about 27 MB,
     * which means we should still have plenty of room for future growth with 64MB).
     */
    uintptr_t  const    uSomeKernelAddr   = (uintptr_t)&absolutetime_to_nanoseconds;
    uintptr_t  const    uLowestKernelAddr = uSomeKernelAddr - _64M;

    /*
     * The kernel is probably aligned at some boundrary larger than a page size,
     * so to speed things up we start by assuming the alignment is page directory
     * sized.  In case we're wrong and it's smaller, we decrease the alignment till
     * we've reach the page size.
     */
    uintptr_t           fPrevAlignMask    = ~(uintptr_t)0;
    uintptr_t           uCurAlign         = _2M;                    /* ASSUMES the kernel is typically 2MB aligned. */
    while (uCurAlign >= PAGE_SIZE)
    {
        /*
         * Search down from the symbol address looking for a mach-O header that
         * looks like it might belong to the kernel.
         */
        for (uintptr_t uCur = uSomeKernelAddr & ~(uCurAlign - 1); uCur >= uLowestKernelAddr; uCur -= uCurAlign)
        {
            /* Skip pages we've checked in previous iterations and pages that aren't present: */
            /** @todo This is a little bogus in case the header is paged out. */
            if (   (uCur & fPrevAlignMask)
                && rtR0DbgKrnlDarwinIsPagePresent(uCur))
            {
                /*
                 * Look for valid mach-o header (we skip cpusubtype on purpose here).
                 */
                MY_MACHO_HEADER const volatile *pHdr = (MY_MACHO_HEADER const volatile *)uCur;
                if (   pHdr->magic    == MY_MACHO_MAGIC
                    && pHdr->filetype == MH_EXECUTE
                    && pHdr->cputype  == MY_CPU_TYPE)
                {
                    /* More header validation: */
                    pThis->cLoadCmds  = pHdr->ncmds;
                    pThis->cbLoadCmds = pHdr->sizeofcmds;
                    if (pHdr->ncmds < 4)
                        LOG_MISMATCH("RTR0DbgInfo: %p: ncmds=%u is too small\n", (void *)pHdr, pThis->cLoadCmds);
                    else if (pThis->cLoadCmds > 256)
                        LOG_MISMATCH("RTR0DbgInfo: %p: ncmds=%u is too big\n", (void *)pHdr, pThis->cLoadCmds);
                    else if (pThis->cbLoadCmds <= pThis->cLoadCmds * sizeof(load_command_t))
                        LOG_MISMATCH("RTR0DbgInfo: %p: sizeofcmds=%u is too small for ncmds=%u\n",
                                     (void *)pHdr, pThis->cbLoadCmds, pThis->cLoadCmds);
                    else if (pThis->cbLoadCmds >= _1M)
                        LOG_MISMATCH("RTR0DbgInfo: %p: sizeofcmds=%u is too big\n", (void *)pHdr, pThis->cbLoadCmds);
                    else if (pHdr->flags & ~MH_VALID_FLAGS)
                        LOG_MISMATCH("RTR0DbgInfo: %p: invalid flags=%#x\n", (void *)pHdr, pHdr->flags);
                    /*
                     * Check that we can safely read the load commands, then parse & validate them.
                     */
                    else if (rtR0DbgKrnlDarwinIsRangePresent((uintptr_t)(pHdr + 1), pThis->cbLoadCmds, "load commands", pHdr))
                    {
                        pThis->pLoadCmds = (load_command_t *)(pHdr + 1);
                        int rc = rtR0DbgKrnlDarwinParseCommands(pThis);
                        if (RT_SUCCESS(rc))
                        {
                            /* Calculate the slide value.  This is typically zero as the
                               load commands has been relocated (the case with 10.14.0 at least). */
                            /** @todo ASSUMES that the __TEXT segment comes first and includes the
                             *        mach-o header and load commands and all that. */
                            pThis->offLoad = uCur - pThis->uTextSegLinkAddr;

                            /* Check that the kernel symbol is in the text segment: */
                            uintptr_t const offSomeKernAddr = uSomeKernelAddr - uCur;
                            if (offSomeKernAddr >= pThis->cbTextSeg)
                                LOG_MISMATCH("RTR0DbgInfo: %p: Our symbol at %zx (off %zx) isn't within the text segment (size %#zx)\n",
                                             (void *)pHdr, uSomeKernelAddr, offSomeKernAddr, pThis->cbTextSeg);
                            /*
                             * Parse the symbol+string tables.
                             */
                            else if (pThis->uSymTabLinkAddr == 0)
                                LOG_MISMATCH("RTR0DbgInfo: %p: No symbol table VA (off %#x L %#x)\n",
                                             (void *)pHdr, pThis->offSyms, pThis->cSyms);
                            else if (pThis->uStrTabLinkAddr == 0)
                                LOG_MISMATCH("RTR0DbgInfo: %p: No string table VA (off %#x LB %#x)\n",
                                             (void *)pHdr, pThis->offSyms, pThis->cbStrTab);
                            else if (   rtR0DbgKrnlDarwinIsRangePresent(pThis->uStrTabLinkAddr + pThis->offLoad,
                                                                        pThis->cbStrTab, "string table", pHdr)
                                     && rtR0DbgKrnlDarwinIsRangePresent(pThis->uSymTabLinkAddr + pThis->offLoad,
                                                                        pThis->cSyms * sizeof(pThis->paSyms),
                                                                        "symbol table", pHdr))
                            {
                                pThis->pachStrTab = (char *)pThis->uStrTabLinkAddr + pThis->offLoad;
                                pThis->paSyms = (MY_NLIST *)pThis->uSymTabLinkAddr + pThis->offLoad;
                                rc = rtR0DbgKrnlDarwinParseSymTab(pThis, "in-memory");
                                if (RT_SUCCESS(rc))
                                {
                                    rc = rtR0DbgKrnlDarwinInitLoadDisplacements(pThis);
                                    if (RT_SUCCESS(rc))
                                    {
                                        /*
                                         * Finally check the standard candles.
                                         */
                                        rc = rtR0DbgKrnlDarwinCheckStandardSymbols(pThis, "in-memory");
                                        rtR0DbgKrnlDarwinLoadDone(pThis);
                                        if (RT_SUCCESS(rc))
                                            return rtR0DbgKrnlDarwinSuccess(phKrnlInfo, pThis, "in-memory");
                                    }
                                }
                            }
                        }

                        RT_ZERO(pThis->apSections);
                        RT_ZERO(pThis->apSegments);
                        pThis->pLoadCmds = NULL;
                    }
                }
            }
        }

        fPrevAlignMask = uCurAlign - 1;
        uCurAlign >>= 1;
    }

    RTMemFree(pThis);
    return VERR_GENERAL_FAILURE;
}

#endif /* IN_RING0 */

RTR0DECL(int) RTR0DbgKrnlInfoOpen(PRTDBGKRNLINFO phKrnlInfo, uint32_t fFlags)
{
    AssertPtrReturn(phKrnlInfo, VERR_INVALID_POINTER);
    *phKrnlInfo = NIL_RTDBGKRNLINFO;
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);

#ifdef IN_RING0
    /*
     * Try see if we can use the kernel memory directly.  This depends on not
     * having the __LINKEDIT segment jettisoned or swapped out.  For older
     * kernels this is typically the case, unless kallsyms=1 is in boot-args.
     */
    int rc = rtR0DbgKrnlDarwinOpenInMemory(phKrnlInfo);
    if (RT_SUCCESS(rc))
    {
        Log(("RTR0DbgKrnlInfoOpen: Using in-memory kernel.\n"));
        return rc;
    }
#else
    int rc = VERR_WRONG_ORDER; /* shut up stupid MSC */
#endif

    /*
     * Go thru likely kernel locations
     *
     * Note! Check the OS X version and reorder the list?
     * Note! We should try fish kcsuffix out of bootargs or somewhere one day.
     */
    static bool s_fFirstCall = true;
#ifdef IN_RING3
    extern const char *g_pszTestKernel;
#endif
    struct
    {
        const char *pszLocation;
        int         rc;
    } aKernels[] =
    {
#ifdef IN_RING3
        { g_pszTestKernel, VERR_WRONG_ORDER },
#endif
        { "/System/Library/Kernels/kernel", VERR_WRONG_ORDER },
        { "/System/Library/Kernels/kernel.development", VERR_WRONG_ORDER },
        { "/System/Library/Kernels/kernel.debug", VERR_WRONG_ORDER },
        { "/mach_kernel", VERR_WRONG_ORDER },
    };
    for (uint32_t i = 0; i < RT_ELEMENTS(aKernels); i++)
    {
        aKernels[i].rc = rc = rtR0DbgKrnlDarwinOpen(phKrnlInfo, aKernels[i].pszLocation);
        if (RT_SUCCESS(rc))
        {
            if (s_fFirstCall)
            {
                printf("RTR0DbgKrnlInfoOpen: Using kernel file '%s'\n", aKernels[i].pszLocation);
                s_fFirstCall = false;
            }
            return rc;
        }
    }

    /*
     * Failed.
     */
    /* Pick the best error code. */
    for (uint32_t i = 0; rc == VERR_FILE_NOT_FOUND && i < RT_ELEMENTS(aKernels); i++)
        if (aKernels[i].rc != VERR_FILE_NOT_FOUND)
            rc = aKernels[i].rc;

    /* Bitch about it. */
    printf("RTR0DbgKrnlInfoOpen: failed to find matching kernel file! rc=%d\n", rc);
    if (s_fFirstCall)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(aKernels); i++)
            printf("RTR0DbgKrnlInfoOpen: '%s' -> %d\n", aKernels[i].pszLocation, aKernels[i].rc);
        s_fFirstCall = false;
    }

    return rc;
}


RTR0DECL(uint32_t) RTR0DbgKrnlInfoRetain(RTDBGKRNLINFO hKrnlInfo)
{
    RTDBGKRNLINFOINT *pThis = hKrnlInfo;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs && cRefs < 100000);
    return cRefs;
}


RTR0DECL(uint32_t) RTR0DbgKrnlInfoRelease(RTDBGKRNLINFO hKrnlInfo)
{
    RTDBGKRNLINFOINT *pThis = hKrnlInfo;
    if (pThis == NIL_RTDBGKRNLINFO)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    if (cRefs == 0)
        rtR0DbgKrnlDarwinDtor(pThis);
    return cRefs;
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
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertMsgReturn(pThis->u32Magic == RTDBGKRNLINFO_MAGIC, ("%p: u32Magic=%RX32\n", pThis, pThis->u32Magic), VERR_INVALID_HANDLE);
    AssertPtrReturn(pszSymbol, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(ppvSymbol, VERR_INVALID_PARAMETER);
    AssertReturn(!pszModule, VERR_MODULE_NOT_FOUND);

    uintptr_t uValue = rtR0DbgKrnlDarwinLookup(pThis, pszSymbol);
    if (ppvSymbol)
        *ppvSymbol = (void *)uValue;
    if (uValue)
        return VINF_SUCCESS;
    return VERR_SYMBOL_NOT_FOUND;
}

