/* $Id: SUPDrvInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Support Driver - Internal header.
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

#ifndef VBOX_INCLUDED_SRC_Support_SUPDrvInternal_h
#define VBOX_INCLUDED_SRC_Support_SUPDrvInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <VBox/sup.h>

#include <iprt/assert.h>
#include <iprt/list.h>
#include <iprt/memobj.h>
#include <iprt/time.h>
#include <iprt/timer.h>
#include <iprt/string.h>
#include <iprt/err.h>

#if defined(SUPDRV_AGNOSTIC) && !defined(RT_OS_LINUX)
/* do nothing */

#elif defined(RT_OS_WINDOWS)
#   include <iprt/nt/nt.h>
#   include <memory.h>

#elif defined(RT_OS_LINUX)
#   include <iprt/linux/version.h>
#   if RTLNX_VER_MIN(2,6,33)
#    include <generated/autoconf.h>
#   else
#    ifndef AUTOCONF_INCLUDED
#     include <linux/autoconf.h>
#    endif
#   endif
#   if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#       define MODVERSIONS
#       if RTLNX_VER_MAX(2,5,71)
#           include <linux/modversions.h>
#       endif
#   endif
#   ifndef KBUILD_STR
#       if RTLNX_VER_MAX(2,6,16)
#            define KBUILD_STR(s) s
#       else
#            define KBUILD_STR(s) #s
#       endif
#   endif
#   ifndef SUPDRV_AGNOSTIC
#       include <linux/string.h>
#       include <linux/spinlock.h>
#       include <linux/slab.h>
#       if RTLNX_VER_MIN(2,6,27)
#           include <linux/semaphore.h>
#       else /* older kernels */
#           include <asm/semaphore.h>
#       endif /* older kernels */
#       include <linux/timer.h>
#   endif
#   if RTLNX_VER_MIN(3,2,0)
#       include <linux/export.h>
#   else
#       include <linux/module.h>
#       if (defined(RT_ARCH_X86) || defined(RT_ARCH_AMD64)) && defined(SUPDRV_AGNOSTIC) /* fix conflicts with iprt/x86.h */
#           undef CS
#           undef DS
#           undef ES
#           undef FS
#           undef GS
#           undef SS
#           undef EFLAGS
#           undef R15
#           undef R14
#           undef R13
#           undef R12
#           undef R11
#           undef R10
#           undef R9
#           undef R8
#           undef RDI
#           undef RSI
#           undef RBP
#           undef RSP
#           undef RBX
#           undef RDX
#           undef RCX
#           undef RAX
#           undef MSR_CORE_PERF_LIMIT_REASONS
#           undef MSR_DRAM_ENERGY_STATUS
#           undef MSR_DRAM_PERF_STATUS
#           undef MSR_DRAM_POWER_INFO
#           undef MSR_DRAM_POWER_LIMIT
#           undef MSR_IA32_APERF
#           undef MSR_IA32_ARCH_CAPABILITIES
#           undef MSR_IA32_CR_PAT
#           undef MSR_IA32_DS_AREA
#           undef MSR_IA32_FEATURE_CONTROL
#           undef MSR_IA32_FLUSH_CMD
#           undef MSR_IA32_MC0_CTL
#           undef MSR_IA32_MC0_STATUS
#           undef MSR_IA32_MCG_CAP
#           undef MSR_IA32_MCG_STATUS
#           undef MSR_IA32_MISC_ENABLE
#           undef MSR_IA32_MISC_ENABLE_BTS_UNAVAIL
#           undef MSR_IA32_MISC_ENABLE_LIMIT_CPUID
#           undef MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL
#           undef MSR_IA32_MISC_ENABLE_TCC
#           undef MSR_IA32_MISC_ENABLE_XD_DISABLE
#           undef MSR_IA32_MPERF
#           undef MSR_IA32_PEBS_ENABLE
#           undef MSR_IA32_PERF_CTL
#           undef MSR_IA32_PERF_STATUS
#           undef MSR_IA32_PLATFORM_ID
#           undef MSR_IA32_PMC0
#           undef MSR_IA32_PRED_CMD
#           undef MSR_IA32_RTIT_CTL
#           undef MSR_IA32_SMBASE
#           undef MSR_IA32_SMM_MONITOR_CTL
#           undef MSR_IA32_SPEC_CTRL
#           undef MSR_IA32_THERM_STATUS
#           undef MSR_IA32_TSC
#           undef MSR_IA32_TSC_ADJUST
#           undef MSR_IA32_TSX_CTRL
#           undef MSR_IA32_VMX_BASIC
#           undef MSR_IA32_VMX_CR0_FIXED0
#           undef MSR_IA32_VMX_CR0_FIXED1
#           undef MSR_IA32_VMX_CR4_FIXED0
#           undef MSR_IA32_VMX_CR4_FIXED1
#           undef MSR_IA32_VMX_ENTRY_CTLS
#           undef MSR_IA32_VMX_EPT_VPID_CAP
#           undef MSR_IA32_VMX_EXIT_CTLS
#           undef MSR_IA32_VMX_MISC
#           undef MSR_IA32_VMX_PINBASED_CTLS
#           undef MSR_IA32_VMX_PROCBASED_CTLS
#           undef MSR_IA32_VMX_PROCBASED_CTLS2
#           undef MSR_IA32_VMX_TRUE_ENTRY_CTLS
#           undef MSR_IA32_VMX_TRUE_EXIT_CTLS
#           undef MSR_IA32_VMX_TRUE_PINBASED_CTLS
#           undef MSR_IA32_VMX_TRUE_PROCBASED_CTLS
#           undef MSR_IA32_VMX_VMCS_ENUM
#           undef MSR_IA32_VMX_VMFUNC
#           undef MSR_K6_PFIR
#           undef MSR_K6_PSOR
#           undef MSR_K6_UWCCR
#           undef MSR_K6_WHCR
#           undef MSR_K7_EVNTSEL0
#           undef MSR_K7_EVNTSEL1
#           undef MSR_K7_EVNTSEL2
#           undef MSR_K7_EVNTSEL3
#           undef MSR_K7_PERFCTR0
#           undef MSR_K7_PERFCTR1
#           undef MSR_K7_PERFCTR2
#           undef MSR_K7_PERFCTR3
#           undef MSR_K8_SYSCFG
#           undef MSR_K8_TOP_MEM1
#           undef MSR_K8_TOP_MEM2
#           undef MSR_OFFCORE_RSP_0
#           undef MSR_OFFCORE_RSP_1
#           undef MSR_PKG_C10_RESIDENCY
#           undef MSR_PKG_C2_RESIDENCY
#           undef MSR_PKG_CST_CONFIG_CONTROL
#           undef MSR_PKG_ENERGY_STATUS
#           undef MSR_PKG_PERF_STATUS
#           undef MSR_PKG_POWER_INFO
#           undef MSR_PKG_POWER_LIMIT
#           undef MSR_PKGC3_IRTL
#           undef MSR_PP0_ENERGY_STATUS
#           undef MSR_PP1_ENERGY_STATUS
#           undef MSR_RAPL_POWER_UNIT
#           undef MSR_TURBO_ACTIVATION_RATIO
#           undef VMX_BASIC_MEM_TYPE_WB
#           undef X86_CR0_AM
#           undef X86_CR0_CD
#           undef X86_CR0_EM
#           undef X86_CR0_ET
#           undef X86_CR0_MP
#           undef X86_CR0_NE
#           undef X86_CR0_NW
#           undef X86_CR0_PE
#           undef X86_CR0_PG
#           undef X86_CR0_TS
#           undef X86_CR0_WP
#           undef X86_CR3_PCD
#           undef X86_CR3_PWT
#           undef X86_CR4_DE
#           undef X86_CR4_FSGSBASE
#           undef X86_CR4_MCE
#           undef X86_CR4_OSFXSR
#           undef X86_CR4_OSXSAVE
#           undef X86_CR4_PAE
#           undef X86_CR4_PCE
#           undef X86_CR4_PCIDE
#           undef X86_CR4_PGE
#           undef X86_CR4_PKE
#           undef X86_CR4_PSE
#           undef X86_CR4_PVI
#           undef X86_CR4_SMAP
#           undef X86_CR4_SMEP
#           undef X86_CR4_SMXE
#           undef X86_CR4_TSD
#           undef X86_CR4_UMIP
#           undef X86_CR4_VME
#           undef X86_CR4_VMXE
#       endif
#   endif
#   define SUPR0_EXPORT_SYMBOL(a_Name) EXPORT_SYMBOL(a_Name)

#elif defined(RT_OS_DARWIN)
#   include <libkern/libkern.h>
#   include <iprt/string.h>

#elif defined(RT_OS_OS2)

#elif defined(RT_OS_FREEBSD)
#   define memset  libkern_memset /** @todo these are just hacks to get it compiling, check out later. */
#   define memcmp  libkern_memcmp
#   define strchr  libkern_strchr
#   define strrchr libkern_strrchr
#   define ffsl    libkern_ffsl
#   define fls     libkern_fls
#   define flsl    libkern_flsl
#   include <sys/libkern.h>
#   undef  memset
#   undef  memcmp
#   undef  strchr
#   undef  strrchr
#   undef  ffs
#   undef  ffsl
#   undef  fls
#   undef  flsl
#   include <iprt/string.h>

#elif defined(RT_OS_SOLARIS)
#   include <sys/cmn_err.h>
#   include <iprt/string.h>

#else
#   error "unsupported OS."
#endif

#include "SUPDrvIOC.h"
#include "SUPDrvIDC.h"



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * Hardcoded cookies.
 */
#define BIRD        0x64726962 /* 'bird' */
#define BIRD_INV    0x62697264 /* 'drib' */


#ifdef RT_OS_WINDOWS
/** Use a normal mutex for the loader so we remain at the same IRQL after
 * taking it.
 * @todo fix the mutex implementation on linux and make this the default. */
# define SUPDRV_USE_MUTEX_FOR_LDR

/** Use a normal mutex for the GIP so we remain at the same IRQL after
 * taking it.
 * @todo fix the mutex implementation on linux and make this the default. */
# define SUPDRV_USE_MUTEX_FOR_GIP
#endif

#ifndef SUPR0_EXPORT_SYMBOL
# define SUPR0_EXPORT_SYMBOL(a_Name) extern int g_supDrvExportSymbolDummyVariable
#endif

/**
 * OS debug print macro.
 */
#define OSDBGPRINT(a) SUPR0Printf a


/** @name Context values for the per-session handle tables.
 * The context value is used to distinguish between the different kinds of
 * handles, making the handle table API do all the work.
 * @{ */
/** Handle context value for single release event handles.  */
#define SUPDRV_HANDLE_CTX_EVENT         ((void *)(uintptr_t)(SUPDRVOBJTYPE_SEM_EVENT))
/** Handle context value for multiple release event handles.  */
#define SUPDRV_HANDLE_CTX_EVENT_MULTI   ((void *)(uintptr_t)(SUPDRVOBJTYPE_SEM_EVENT_MULTI))
/** @} */


/**
 * Validates a session pointer.
 *
 * @returns true/false accordingly.
 * @param   pSession    The session.
 */
#define SUP_IS_SESSION_VALID(pSession)  \
    (   RT_VALID_PTR(pSession) \
     && pSession->u32Cookie == BIRD_INV)

/**
 * Validates a device extension pointer.
 *
 * @returns true/false accordingly.
 * @param   pDevExt     The device extension.
 */
#define SUP_IS_DEVEXT_VALID(pDevExt) \
    (   RT_VALID_PTR(pDevExt) \
     && pDevExt->u32Cookie == BIRD)


/** @def SUPDRV_WITH_MSR_PROBER
 * Enables the SUP_IOCTL_MSR_PROBER function.
 * By default, only enabled in DEBUG builds as it's a sensitive feature.
 */
#if defined(DEBUG) && !defined(SUPDRV_WITH_MSR_PROBER) && !defined(SUPDRV_WITHOUT_MSR_PROBER)
# define SUPDRV_WITH_MSR_PROBER
#endif

/** @def SUPDRV_WITHOUT_MSR_PROBER
 * Executive overide for disabling the SUP_IOCTL_MSR_PROBER function.
 */
#ifdef SUPDRV_WITHOUT_MSR_PROBER
# undef SUPDRV_WITH_MSR_PROBER
#endif

#ifdef DOXYGEN_RUNNING
# define SUPDRV_WITH_MSR_PROBER
# define SUPDRV_WITHOUT_MSR_PROBER
#endif

#if 1
/** @def SUPDRV_USE_TSC_DELTA_THREAD
 * Use a dedicated kernel thread to service TSC-delta measurement requests.
 * @todo Test on servers with many CPUs and sockets. */
# define SUPDRV_USE_TSC_DELTA_THREAD
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the device extension. */
typedef struct SUPDRVDEVEXT *PSUPDRVDEVEXT;

#ifdef SUPDRV_USE_TSC_DELTA_THREAD
/**
 * TSC-delta measurement thread state machine.
 */
typedef enum SUPDRVTSCDELTATHREADSTATE
{
    /** Uninitialized/invalid value. */
    kTscDeltaThreadState_Invalid = 0,
    /** The thread is being created.
     * Next state: Listening, Butchered, Terminating  */
    kTscDeltaThreadState_Creating,
    /** The thread is listening for events.
     * Previous state: Creating, Measuring
     * Next state: WaitAndMeasure, Butchered, Terminated */
    kTscDeltaThreadState_Listening,
    /** The thread is sleeping before starting a measurement.
     * Previous state: Listening, Measuring
     * Next state:     Measuring, Butchered, Terminating
     * @remarks The thread won't enter this state on its own, it is put into this
     *          state by the GIP timer, the CPU online callback and by the
     *          SUP_IOCTL_TSC_DELTA_MEASURE code. */
    kTscDeltaThreadState_WaitAndMeasure,
    /** The thread is currently servicing a measurement request.
     * Previous state: WaitAndMeasure
     * Next state:     Listening, WaitAndMeasure, Terminate */
    kTscDeltaThreadState_Measuring,
    /** The thread is terminating.
     * @remarks The thread won't enter this state on its own, is put into this state
     *          by supdrvTscDeltaTerm. */
    kTscDeltaThreadState_Terminating,
    /** The thread is butchered due to an unexpected error.
     * Previous State: Creating, Listening, WaitAndMeasure  */
    kTscDeltaThreadState_Butchered,
    /** The thread is destroyed (final).
     * Previous state: Terminating */
    kTscDeltaThreadState_Destroyed,
    /** The usual 32-bit blowup hack. */
    kTscDeltaThreadState_32BitHack = 0x7fffffff
} SUPDRVTSCDELTATHREADSTATE;
#endif /* SUPDRV_USE_TSC_DELTA_THREAD */

/**
 * Memory reference types.
 */
typedef enum
{
    /** Unused entry */
    MEMREF_TYPE_UNUSED = 0,
    /** Locked memory (r3 mapping only). */
    MEMREF_TYPE_LOCKED,
    /** Continuous memory block (r3 and r0 mapping). */
    MEMREF_TYPE_CONT,
    /** Low memory block (r3 and r0 mapping). */
    MEMREF_TYPE_LOW,
    /** Memory block (r3 and r0 mapping). */
    MEMREF_TYPE_MEM,
    /** Locked memory (r3 mapping only) allocated by the support driver. */
    MEMREF_TYPE_PAGE,
    /** Blow the type up to 32-bit and mark the end. */
    MEMREF_TYPE_32BIT_HACK = 0x7fffffff
} SUPDRVMEMREFTYPE, *PSUPDRVMEMREFTYPE;


/**
 * Structure used for tracking memory a session
 * references in one way or another.
 */
typedef struct SUPDRVMEMREF
{
    /** The memory object handle. */
    RTR0MEMOBJ                      MemObj;
    /** The ring-3 mapping memory object handle. */
    RTR0MEMOBJ                      MapObjR3;
    /** Type of memory. */
    SUPDRVMEMREFTYPE                eType;
} SUPDRVMEMREF, *PSUPDRVMEMREF;


/**
 * Bundle of locked memory ranges.
 */
typedef struct SUPDRVBUNDLE
{
    /** Pointer to the next bundle. */
    struct SUPDRVBUNDLE * volatile  pNext;
    /** Referenced memory. */
    SUPDRVMEMREF                    aMem[64];
    /** Number of entries used. */
    uint32_t volatile   cUsed;
} SUPDRVBUNDLE, *PSUPDRVBUNDLE;


/**
 * Loaded image.
 */
typedef struct SUPDRVLDRIMAGE
{
    /** Next in chain. */
    struct SUPDRVLDRIMAGE * volatile pNext;
    /** Pointer to the image. */
    void                           *pvImage;
    /** The memory object for the module allocation. */
    RTR0MEMOBJ                      hMemObjImage;
    /** Magic value (SUPDRVLDRIMAGE_MAGIC). */
    uint32_t                        uMagic;
    /** Size of the image including the tables. This is mainly for verification
     * of the load request. */
    uint32_t                        cbImageWithEverything;
    /** Size of the image. */
    uint32_t                        cbImageBits;
    /** The number of entries in the symbol table. */
    uint32_t                        cSymbols;
    /** Pointer to the symbol table. */
    PSUPLDRSYM                      paSymbols;
    /** The offset of the string table. */
    char                           *pachStrTab;
    /** Size of the string table. */
    uint32_t                        cbStrTab;
    /** Number of segments. */
    uint32_t                        cSegments;
    /** Segments (for memory protection). */
    PSUPLDRSEG                      paSegments;
    /** Pointer to the optional module initialization callback. */
    PFNR0MODULEINIT                 pfnModuleInit;
    /** Pointer to the optional module termination callback. */
    PFNR0MODULETERM                 pfnModuleTerm;
    /** Service request handler. This is NULL for non-service modules. */
    PFNSUPR0SERVICEREQHANDLER       pfnServiceReqHandler;
    /** The ldr image state. (IOCtl code of last operation.) */
    uint32_t                        uState;
    /** Usage count. */
    uint32_t volatile               cImgUsage;
    /** Pointer to the device extension. */
    struct SUPDRVDEVEXT            *pDevExt;
    /** Image (VMMR0.r0) containing functions/data that this one uses. */
    struct SUPDRVLDRIMAGE          *pImageImport;
#ifdef RT_OS_WINDOWS
    /** The section object for the loaded image (fNative=true). */
    void                           *pvNtSectionObj;
    /** Lock object. */
    RTR0MEMOBJ                      hMemLock;
#endif
#if defined(RT_OS_SOLARIS) && defined(VBOX_WITH_NATIVE_SOLARIS_LOADING)
    /** The Solaris module ID. */
    int                             idSolMod;
    /** Pointer to the module control structure. */
    struct modctl                  *pSolModCtl;
#endif
#ifdef RT_OS_LINUX
    /** Hack for seeing the module in perf, dtrace and other stack crawlers. */
    struct module                  *pLnxModHack;
    /** The wrapper module.  */
    struct module                  *pLnxWrapperModule;
    /** Set if we're holding a reference to the wrapper module. */
    bool                            fLnxWrapperRef;
#endif
#if defined(RT_OS_DARWIN) && defined(VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION)
    /** Load module handle. */
    RTLDRMOD                        hLdrMod;
    /** Allocate object. */
    RTR0MEMOBJ                      hMemAlloc;
#endif
    /** This points to the module info if the image is a wrapped up in a native one. */
    PCSUPLDRWRAPPEDMODULE           pWrappedModInfo;
    /** OS specific information for wrapped modules. */
    void                           *pvWrappedNative;
    /** Whether it's loaded by the native loader or not. */
    bool                            fNative;
    /** Image name. */
    char                            szName[32];
} SUPDRVLDRIMAGE, *PSUPDRVLDRIMAGE;

/** Magic value for SUPDRVLDRIMAGE::uMagic (Charlotte Bronte). */
#define SUPDRVLDRIMAGE_MAGIC        UINT32_C(0x18160421)
/** Magic value for SUPDRVLDRIMAGE::uMagic when freed. */
#define SUPDRVLDRIMAGE_MAGIC_DEAD   UINT32_C(0x18550331)


/** Image usage record. */
typedef struct SUPDRVLDRUSAGE
{
    /** Next in chain. */
    struct SUPDRVLDRUSAGE * volatile pNext;
    /** The image. */
    PSUPDRVLDRIMAGE                 pImage;
    /** Load count (ring-3). */
    uint32_t volatile               cRing3Usage;
    /** Ring-0 usage counter. */
    uint32_t volatile               cRing0Usage;
} SUPDRVLDRUSAGE, *PSUPDRVLDRUSAGE;


/**
 * Component factory registration record.
 */
typedef struct SUPDRVFACTORYREG
{
    /** Pointer to the next registration. */
    struct SUPDRVFACTORYREG        *pNext;
    /** Pointer to the registered factory. */
    PCSUPDRVFACTORY                 pFactory;
    /** The session owning the factory.
     * Used for deregistration and session cleanup. */
    PSUPDRVSESSION                  pSession;
    /** Length of the name. */
    size_t                          cchName;
} SUPDRVFACTORYREG;
/** Pointer to a component factory registration record. */
typedef SUPDRVFACTORYREG *PSUPDRVFACTORYREG;
/** Pointer to a const component factory registration record. */
typedef SUPDRVFACTORYREG const *PCSUPDRVFACTORYREG;


/**
 * Registered object.
 * This takes care of reference counting and tracking data for access checks.
 */
typedef struct SUPDRVOBJ
{
    /** Magic value (SUPDRVOBJ_MAGIC). */
    uint32_t                        u32Magic;
    /** The object type. */
    SUPDRVOBJTYPE                   enmType;
    /** Pointer to the next in the global list. */
    struct SUPDRVOBJ * volatile     pNext;
    /** Pointer to the object destructor.
     * This may be set to NULL if the image containing the destructor get unloaded. */
    PFNSUPDRVDESTRUCTOR             pfnDestructor;
    /** User argument 1. */
    void                           *pvUser1;
    /** User argument 2. */
    void                           *pvUser2;
    /** The total sum of all per-session usage. */
    uint32_t volatile               cUsage;
    /** The creator user id. */
    RTUID                           CreatorUid;
    /** The creator group id. */
    RTGID                           CreatorGid;
    /** The creator process id. */
    RTPROCESS                       CreatorProcess;
} SUPDRVOBJ, *PSUPDRVOBJ;

/** Magic number for SUPDRVOBJ::u32Magic. (Dame Agatha Mary Clarissa Christie). */
#define SUPDRVOBJ_MAGIC             UINT32_C(0x18900915)
/** Dead number magic for SUPDRVOBJ::u32Magic. */
#define SUPDRVOBJ_MAGIC_DEAD        UINT32_C(0x19760112)

/**
 * The per-session object usage record.
 */
typedef struct SUPDRVUSAGE
{
    /** Pointer to the next in the list. */
    struct SUPDRVUSAGE * volatile   pNext;
    /** Pointer to the object we're recording usage for. */
    PSUPDRVOBJ                      pObj;
    /** The usage count. */
    uint32_t volatile               cUsage;
} SUPDRVUSAGE, *PSUPDRVUSAGE;


/**
 * I/O control context.
 */
typedef struct SUPR0IOCTLCTX
{
    /** Magic value (SUPR0IOCTLCTX_MAGIC). */
    uint32_t                u32Magic;
    /** Reference counter. */
    uint32_t volatile       cRefs;
#ifdef RT_OS_WINDOWS
# ifndef SUPDRV_AGNOSTIC
    /** The file object, referenced. */
    PFILE_OBJECT            pFileObject;
    /** The device object, not referenced. */
    PDEVICE_OBJECT          pDeviceObject;
    /** Pointer to fast I/O routine if available. */
    FAST_IO_DEVICE_CONTROL *pfnFastIoDeviceControl;
# else
    void                   *apvPadding[3];
# endif
#endif
} SUPR0IOCTLCTX;
/** Magic value for SUPR0IOCTLCTX (Ahmad Jamal). */
#define SUPR0IOCTLCTX_MAGIC     UINT32_C(0x19300702)


/**
 * Per session data.
 * This is mainly for memory tracking.
 */
typedef struct SUPDRVSESSION
{
    /** Pointer to the device extension. */
    PSUPDRVDEVEXT                   pDevExt;
    /** Session Cookie. */
    uint32_t                        u32Cookie;
    /** Set if is an unrestricted session, clear if restricted. */
    bool                            fUnrestricted;

    /** Set if we're in the hash table, clear if not.  Protected by the hash
     * table spinlock. */
    bool                            fInHashTable;
    /** Reference counter. */
    uint32_t volatile               cRefs;
    /** Pointer to the next session with the same hash (common hash table).
     *  Protected by the hash table spinlock. */
    PSUPDRVSESSION                  pCommonNextHash;
    /** Pointer to the OS specific session pointer, if available and in use.
     * This is atomically set and cleared as the session is inserted and removed
     * from the hash table (protected by the session hash table spinlock). */
    PSUPDRVSESSION                 *ppOsSessionPtr;
    /** The process (id) of the session. */
    RTPROCESS                       Process;
    /** Which process this session is associated with.
     * This is NIL_RTR0PROCESS for kernel sessions and valid for user ones. */
    RTR0PROCESS                     R0Process;

    /** The GVM associated with the session.
     * This is set by VMMR0.  */
    PGVM                            pSessionGVM;
    /** The VM associated with the session.
     * This is set by VMMR0.  */
    PVM                             pSessionVM;
    /** Set to pSessionVM if fast I/O controlls are enabled. */
    PVM                             pFastIoCtrlVM;
    /** Handle table for IPRT semaphore wrapper APIs.
     * This takes care of its own locking in an IRQ safe manner. */
    RTHANDLETABLE                   hHandleTable;
    /** Load usage records (LIFO!). (protected by SUPDRVDEVEXT::mtxLdr) */
    PSUPDRVLDRUSAGE volatile        pLdrUsage;

    /** Spinlock protecting the bundles, the GIP members and the
     * fProcessCleanupDone flag.  It continues to be valid until the last
     * reference to the session is released. */
    RTSPINLOCK                      Spinlock;
    /** The ring-3 mapping of the GIP (readonly). */
    RTR0MEMOBJ                      GipMapObjR3;
    /** Set if the session is using the GIP. */
    uint32_t                        fGipReferenced;
    /** Bundle of locked memory objects. */
    SUPDRVBUNDLE                    Bundle;
    /** List of generic usage records. (protected by SUPDRVDEVEXT::SpinLock) */
    PSUPDRVUSAGE volatile           pUsage;

    /** The user id of the session - set by the OS part or NIL_RTUID.
     * This should be unique accross namespace/zones/whatever. */
    RTUID                           Uid;
    /** The group id of the session - set by the OS part or NIL_RTGID.
     * This should be unique accross namespace/zones/whatever.  */
    RTGID                           Gid;
    /** Per session tracer specfic data. */
    uintptr_t                       uTracerData;
    /** The thread currently actively talking to the tracer. (One at the time!) */
    RTNATIVETHREAD                  hTracerCaller;
    /** List of tracepoint providers associated with the session
     * (SUPDRVTPPROVIDER). */
    RTLISTANCHOR                    TpProviders;
    /** The number of providers in TpProviders. */
    uint32_t                        cTpProviders;
    /** The number of threads active in supdrvIOCtl_TracerUmodProbeFire or
     *  SUPR0TracerUmodProbeFire. */
    uint32_t volatile               cTpProbesFiring;
    /** User tracepoint modules (PSUPDRVTRACKERUMOD). */
    RTLISTANCHOR                    TpUmods;
    /** The user tracepoint module lookup table. */
    struct SUPDRVTRACERUMOD        *apTpLookupTable[32];
    /** Whether this is a GIP test-mode client session or not. */
    bool                            fGipTestMode;
#ifndef SUPDRV_AGNOSTIC
# if defined(RT_OS_DARWIN)
    /** Pointer to the associated org_virtualbox_SupDrvClient object. */
    void                           *pvSupDrvClient;
    /** Whether this session has been opened or not. */
    bool                            fOpened;
# endif
# if defined(RT_OS_OS2)
    /** The system file number of this session. */
    uint16_t                        sfn;
    uint16_t                        Alignment; /**< Alignment */
# endif
# if defined(RT_OS_DARWIN) || defined(RT_OS_OS2) || defined(RT_OS_SOLARIS)
    /** Pointer to the next session with the same hash. */
    PSUPDRVSESSION                  pNextHash;
# endif
# if defined(RT_OS_WINDOWS) && defined(VBOX_WITH_HARDENING)
    /** Pointer to the process protection structure for this session. */
    struct SUPDRVNTPROTECT         *pNtProtect;
# endif
# if defined(RT_OS_WINDOWS)
    /** Reference to the user ID structure corresponding to the Uid member. */
    struct SUPDRVNTUSERID          *pNtUserId;
# endif
#endif /* !SUPDRV_AGNOSTIC */
} SUPDRVSESSION;


/**
 * Device extension.
 */
typedef struct SUPDRVDEVEXT
{
    /** Global cookie. */
    uint32_t                        u32Cookie;
    /** The actual size of SUPDRVSESSION. (SUPDRV_AGNOSTIC) */
    uint32_t                        cbSession;

    /** Spinlock to serialize the initialization, usage counting and objects.
     * This is IRQ safe because we want to be able signal semaphores from the
     * special HM context (and later maybe interrupt handlers), so we must be able
     * to reference and dereference handles when IRQs are disabled. */
    RTSPINLOCK                      Spinlock;

    /** List of registered objects. Protected by the spinlock. */
    PSUPDRVOBJ volatile             pObjs;
    /** List of free object usage records. */
    PSUPDRVUSAGE volatile           pUsageFree;

    /** Loader mutex.
     * This protects pvVMMR0, pvVMMR0Entry, pImages and SUPDRVSESSION::pLdrUsage. */
#ifdef SUPDRV_USE_MUTEX_FOR_LDR
    RTSEMMUTEX                      mtxLdr;
#else
    RTSEMFASTMUTEX                  mtxLdr;
#endif

    /** VMM Module 'handle'.
     * 0 if the code VMM isn't loaded and Idt are nops. */
    void * volatile                 pvVMMR0;
    /** VMMR0EntryFast() pointer. */
    DECLR0CALLBACKMEMBER(void,      pfnVMMR0EntryFast, (PGVM pGVM, PVM pVM, VMCPUID idCpu, uint32_t uOperation));
    /** VMMR0EntryEx() pointer. */
    DECLR0CALLBACKMEMBER(int,       pfnVMMR0EntryEx, (PGVM pGVM, PVM pVM, VMCPUID idCpu, uint32_t uOperation,
                                                      PSUPVMMR0REQHDR pReq, uint64_t u64Arg, PSUPDRVSESSION pSession));

    /** Linked list of loaded code. */
    PSUPDRVLDRIMAGE volatile        pLdrImages;
    /** Set if the image loading interface got disabled after loading all needed images */
    bool                            fLdrLockedDown;

    /** @name These members for detecting whether an API caller is in ModuleInit.
     * Certain APIs are only permitted from ModuleInit, like for instance tracepoint
     * registration.
     * @{ */
    /** The image currently executing its ModuleInit. */
    PSUPDRVLDRIMAGE volatile        pLdrInitImage;
    /** The thread currently executing a ModuleInit function. */
    RTNATIVETHREAD volatile         hLdrInitThread;
    /** The thread currently executing a ModuleTerm function. */
    RTNATIVETHREAD volatile         hLdrTermThread;
    /** @} */

    /** Number of times someone reported bad execution context via SUPR0BadContext.
     * (This is times EFLAGS.AC is zero when we expected it to be 1.) */
    uint32_t volatile               cBadContextCalls;

    /** GIP mutex.
     * Any changes to any of the GIP members requires ownership of this mutex,
     * except on driver init and termination. */
#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSEMMUTEX                      mtxGip;
#else
    RTSEMFASTMUTEX                  mtxGip;
#endif
    /** GIP spinlock protecting GIP members during Mp events.
     * This is IRQ safe since be may get MP callbacks in contexts where IRQs are
     * disabled (on some platforms). */
    RTSPINLOCK                      hGipSpinlock;
    /** Pointer to the Global Info Page (GIP). */
    PSUPGLOBALINFOPAGE              pGip;
    /** The physical address of the GIP. */
    RTHCPHYS                        HCPhysGip;
    /** Number of processes using the GIP.
     * (The updates are suspend while cGipUsers is 0.)*/
    uint32_t volatile               cGipUsers;
    /** The ring-0 memory object handle for the GIP page. */
    RTR0MEMOBJ                      GipMemObj;
    /** The GIP timer handle. */
    PRTTIMER                        pGipTimer;
    /** If non-zero we've successfully called RTTimerRequestSystemGranularity(). */
    uint32_t                        u32SystemTimerGranularityGrant;
    /** The CPU id of the GIP master.
     * This CPU is responsible for the updating the common GIP data and it is
     * the one used to calculate TSC deltas relative to.
     * (The initial master will have a 0 zero value, but it it goes offline the
     * new master may have a non-zero value.) */
    RTCPUID volatile                idGipMaster;

    /** Component factory mutex.
     * This protects pComponentFactoryHead and component factory querying. */
    RTSEMFASTMUTEX                  mtxComponentFactory;
    /** The head of the list of registered component factories. */
    PSUPDRVFACTORYREG               pComponentFactoryHead;

    /** Lock protecting The tracer members. */
    RTSEMFASTMUTEX                  mtxTracer;
    /** List of tracer providers (SUPDRVTPPROVIDER). */
    RTLISTANCHOR                    TracerProviderList;
    /** List of zombie tracer providers (SUPDRVTPPROVIDER). */
    RTLISTANCHOR                    TracerProviderZombieList;
    /** Pointer to the tracer registration record. */
    PCSUPDRVTRACERREG               pTracerOps;
    /** The ring-0 session of a native tracer provider. */
    PSUPDRVSESSION                  pTracerSession;
    /** The image containing the tracer. */
    PSUPDRVLDRIMAGE                 pTracerImage;
    /** The tracer helpers. */
    SUPDRVTRACERHLP                 TracerHlp;
    /** The number of session having opened the tracer currently. */
    uint32_t                        cTracerOpens;
    /** The number of threads currently calling into the tracer. */
    uint32_t volatile               cTracerCallers;
    /** Set if the tracer is being unloaded. */
    bool                            fTracerUnloading;
    /** Hash table for user tracer modules (SUPDRVVTGCOPY). */
    RTLISTANCHOR                    aTrackerUmodHash[128];

    /** @name Session Handle Table.
     * @{ */
    /** Spinlock protecting apSessionHashTab, cSessions,
     * SUPDRVSESSION::ppOsSessionPtr, SUPDRVSESSION::pCommonNextHash, and possibly
     * others depending on the OS. */
    RTSPINLOCK                      hSessionHashTabSpinlock;
    /** Session hash table hash table.  The size of this table must make sense in
     * comparison to GVMM_MAX_HANDLES. */
    PSUPDRVSESSION                  apSessionHashTab[HC_ARCH_BITS == 64 ? 8191 : 127];
    /** The number of open sessions. */
    int32_t                         cSessions;
    /** @} */

    /** @name Invariant TSC frequency refinement.
     * @{ */
    /** Nanosecond timestamp at the start of the TSC frequency refinement phase. */
    uint64_t                        nsStartInvarTscRefine;
    /** TSC reading at the start of the TSC frequency refinement phase. */
    uint64_t                        uTscStartInvarTscRefine;
    /** The CPU id of the CPU that u64TscAnchor was measured on. */
    RTCPUID                         idCpuInvarTscRefine;
    /** Pointer to the timer used to refine the TSC frequency. */
    PRTTIMER                        pInvarTscRefineTimer;
    /** Stop the timer on the next tick because we saw a power event. */
    bool volatile                   fInvTscRefinePowerEvent;
    /** @} */

    /** @name TSC-delta measurement.
     *  @{ */
    /** Number of online/offline events, incremented each time a CPU goes online
     *  or offline. */
    uint32_t volatile               cMpOnOffEvents;
    /** TSC-delta measurement mutext.
     * At the moment, we don't want to have more than one measurement going on at
     * any one time.  We might be using broadcast IPIs which are heavy and could
     * perhaps get in each others way. */
#ifdef SUPDRV_USE_MUTEX_FOR_GIP
    RTSEMMUTEX                      mtxTscDelta;
#else
    RTSEMFASTMUTEX                  mtxTscDelta;
#endif
    /** The set of CPUs we need to take measurements for. */
    RTCPUSET                        TscDeltaCpuSet;
    /** The set of CPUs we have completed taken measurements for. */
    RTCPUSET                        TscDeltaObtainedCpuSet;
    /** @}  */

#ifdef SUPDRV_USE_TSC_DELTA_THREAD
    /** @name TSC-delta measurement thread.
     *  @{ */
    /** Spinlock protecting enmTscDeltaThreadState. */
    RTSPINLOCK                      hTscDeltaSpinlock;
    /** TSC-delta measurement thread. */
    RTTHREAD                        hTscDeltaThread;
    /** The event signalled during state changes to the TSC-delta thread. */
    RTSEMEVENT                      hTscDeltaEvent;
    /** The state of the TSC-delta measurement thread. */
    SUPDRVTSCDELTATHREADSTATE       enmTscDeltaThreadState;
    /** Thread timeout time before rechecking state in ms. */
    RTMSINTERVAL                    cMsTscDeltaTimeout;
    /** Whether the TSC-delta measurement was successful. */
    int32_t volatile                rcTscDelta;
    /** Tell the thread we want TSC-deltas for all CPUs with retries. */
    bool                            fTscThreadRecomputeAllDeltas;
    /** @} */
#endif

    /** @name GIP test mode.
     *  @{ */
    /** Reference counter for GIP test-mode sessions. */
    uint32_t                        cGipTestModeRefs;
    /** Cache of TSC frequency before enabling test-mode on invariant GIP systems. */
    uint64_t                        uGipTestModeInvariantCpuHz;
    /** @} */

    /*
     * Note! The non-agnostic bits must be at the very end of the structure!
     */
#ifndef SUPDRV_AGNOSTIC
# ifdef RT_OS_WINDOWS
    /** Callback object returned by ExCreateCallback. */
    PCALLBACK_OBJECT                pObjPowerCallback;
    /** Callback handle returned by ExRegisterCallback. */
    PVOID                           hPowerCallback;
# elif defined(RT_OS_DARWIN) && defined(VBOX_WITH_DARWIN_R0_DARWIN_IMAGE_VERIFICATION)
    /** Trusted root certificates for code signing validation. */
    RTCRSTORE                       hRootStore;
    /** Intermedite certificates for code signing validation. */
    RTCRSTORE                       hAdditionalStore;
# endif
#endif
} SUPDRVDEVEXT;

/** Calculates the index into g_apSessionHashTab.*/
#define SUPDRV_SESSION_HASH(a_pid)     ( (a_pid) % RT_ELEMENTS(((SUPDRVDEVEXT *)NULL)->apSessionHashTab) )


RT_C_DECLS_BEGIN

/*******************************************************************************
*   OS Specific Functions                                                      *
*******************************************************************************/
/**
 * Called to clean up the session structure before it's freed.
 *
 * @param   pDevExt             The device globals.
 * @param   pSession            The session that's being cleaned up.
 */
void VBOXCALL   supdrvOSCleanupSession(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession);

/**
 * Called to let the OS specfic code perform additional insertion work while
 * still under the protection of the hash table spinlock.
 *
 * @param   pDevExt             The device globals.
 * @param   pSession            The session that was inserted.
 * @param   pvUser              User context specified to the insert call.
 */
void VBOXCALL   supdrvOSSessionHashTabInserted(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, void *pvUser);

/**
 * Called to let the OS specfic code perform additional removal work while still
 * under the protection of the hash table spinlock.
 *
 * @param   pDevExt             The device globals.
 * @param   pSession            The session that was removed.
 * @param   pvUser              User context specified to the remove call.
 */
void VBOXCALL   supdrvOSSessionHashTabRemoved(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, void *pvUser);

/**
 * Called during GIP initializtion to calc the CPU group table size.
 *
 * This is currently only implemented on windows [lazy bird].
 *
 * @returns Number of bytes needed for SUPGIPCPUGROUP structures.
 * @param   pDevExt             The device globals.
 */
size_t VBOXCALL supdrvOSGipGetGroupTableSize(PSUPDRVDEVEXT pDevExt);

/**
 * Called during GIP initialization to set up the group table and group count.
 *
 * This is currently only implemented on windows [lazy bird].
 *
 * @param   pDevExt             The device globals.
 * @param   pGip                The GIP which group table needs initialization.
 *                              It's only partially initialized at this point.
 * @param   cbGipCpuGroups      What supdrvOSGipGetGroupTableSize returned.
 */
int VBOXCALL    supdrvOSInitGipGroupTable(PSUPDRVDEVEXT pDevExt, PSUPGLOBALINFOPAGE pGip, size_t cbGipCpuGroups);

/**
 * Initializes the group related members when a CPU is added to the GIP.
 *
 * This is called both during GIP initalization and during an CPU online event.
 *
 * This is currently only implemented on windows [lazy bird].
 *
 * @param   pDevExt             The device globals.
 * @param   pGip                The GIP.
 * @param   pGipCpu             The GIP CPU structure being initialized.
 */
void VBOXCALL supdrvOSGipInitGroupBitsForCpu(PSUPDRVDEVEXT pDevExt, PSUPGLOBALINFOPAGE pGip, PSUPGIPCPU pGipCpu);

void VBOXCALL   supdrvOSObjInitCreator(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession);
bool VBOXCALL   supdrvOSObjCanAccess(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession, const char *pszObjName, int *prc);
bool VBOXCALL   supdrvOSGetForcedAsyncTscMode(PSUPDRVDEVEXT pDevExt);
bool VBOXCALL   supdrvOSAreCpusOfflinedOnSuspend(void);
bool VBOXCALL   supdrvOSAreTscDeltasInSync(void);
int  VBOXCALL   supdrvOSEnableVTx(bool fEnabled);
RTCCUINTREG VBOXCALL supdrvOSChangeCR4(RTCCUINTREG fOrMask, RTCCUINTREG fAndMask);
bool VBOXCALL   supdrvOSSuspendVTxOnCpu(void);
void VBOXCALL   supdrvOSResumeVTxOnCpu(bool fSuspended);
int  VBOXCALL   supdrvOSGetCurrentGdtRw(RTHCUINTPTR *pGdtRw);

/**
 * Try open the image using the native loader.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if native loading isn't supported.
 *
 * @param   pDevExt             The device globals.
 * @param   pImage              The image handle.  pvImage should be set on
 *                              success, pvImageAlloc can also be set if
 *                              appropriate.
 * @param   pszFilename         The file name - UTF-8, may containing UNIX
 *                              slashes on non-UNIX systems.
 */
int  VBOXCALL   supdrvOSLdrOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename);

/**
 * Notification call indicating that a image is being opened for the first time.
 *
 * Called for both native and non-native images (after supdrvOSLdrOpen).  Can be
 * used to log the load address of the image or inform the kernel about the
 * alien image.
 *
 * @param   pDevExt             The device globals.
 * @param   pImage              The image handle.
 * @param   pszFilename         The file name - UTF-8, may containing UNIX
 *                              slashes on non-UNIX systems.
 */
void VBOXCALL   supdrvOSLdrNotifyOpened(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename);

/**
 * Validates an entry point address.
 *
 * Called before supdrvOSLdrLoad.
 *
 * @returns IPRT status code.
 * @param   pDevExt             The device globals.
 * @param   pImage              The image data (still in the open state).
 * @param   pv                  The address within the image.
 * @param   pbImageBits         The image bits as loaded by ring-3.
 * @param   pszSymbol           The name of the entrypoint being checked.
 */
int  VBOXCALL   supdrvOSLdrValidatePointer(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage,
                                           void *pv, const uint8_t *pbImageBits, const char *pszSymbol);

/**
 * Load the image.
 *
 * @returns IPRT status code.
 * @param   pDevExt             The device globals.
 * @param   pImage              The image data (up to date). Adjust entrypoints
 *                              and exports if necessary.
 * @param   pbImageBits         The image bits as loaded by ring-3.
 * @param   pReq                Pointer to the request packet so that the VMMR0
 *                              entry points can be adjusted.
 */
int  VBOXCALL   supdrvOSLdrLoad(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const uint8_t *pbImageBits, PSUPLDRLOAD pReq);

/**
 * Unload the image (only called if supdrvOSLdrOpen returned success).
 *
 * @param   pDevExt             The device globals.
 * @param   pImage              The image data (mostly still valid).
 */
void VBOXCALL   supdrvOSLdrUnload(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage);

/**
 * Notification call indicating that a image is being unloaded.
 *
 * Called for both native and non-native images.  In the former case, it's
 * called after supdrvOSLdrUnload.
 *
 * @param   pDevExt             The device globals.
 * @param   pImage              The image handle.
 */
void VBOXCALL   supdrvOSLdrNotifyUnloaded(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage);

/**
 * Queries a symbol address is a native module.
 *
 * @returns IPRT status code.
 * @param   pDevExt             The device globals.
 * @param   pImage              The image to search.
 * @param   pszSymbol           The symbol to search for.
 * @param   cchSymbol           The length of the symbol.
 * @param   ppvSymbol           Where to return the symbol address if found.
 */
int  VBOXCALL   supdrvOSLdrQuerySymbol(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage,
                                       const char *pszSymbol, size_t cchSymbol, void **ppvSymbol);

/**
 * Retains a native wrapper module when it is first being used.
 *
 * This will be call when pImage->cImgUsage is incremented to 2.
 *
 * @param   pDevExt             The device globals.
 * @param   pImage              The wrapped image.
 */
void VBOXCALL   supdrvOSLdrRetainWrapperModule(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage);

/**
 * Release a native wrapper module when it is no longer being used.
 *
 * This will be call when pImage->cImgUsage is decremented to 1.
 *
 * @param   pDevExt             The device globals.
 * @param   pImage              The wrapped image.
 */
void VBOXCALL   supdrvOSLdrReleaseWrapperModule(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage);

#ifdef SUPDRV_WITH_MSR_PROBER

/**
 * Tries to read an MSR.
 *
 * @returns One of the listed VBox status codes.
 * @retval  VINF_SUCCESS if read successfully, value in *puValue.
 * @retval  VERR_ACCESS_DENIED if we couldn't read it (GP).
 * @retval  VERR_NOT_SUPPORTED if not supported.
 *
 * @param   uMsr                The MSR to read from.
 * @param   idCpu               The CPU to read the MSR on. NIL_RTCPUID
 *                              indicates any suitable CPU.
 * @param   puValue             Where to return the value.
 */
int VBOXCALL    supdrvOSMsrProberRead(uint32_t uMsr, RTCPUID idCpu, uint64_t *puValue);

/**
 * Tries to write an MSR.
 *
 * @returns One of the listed VBox status codes.
 * @retval  VINF_SUCCESS if written successfully.
 * @retval  VERR_ACCESS_DENIED if we couldn't write the value to it (GP).
 * @retval  VERR_NOT_SUPPORTED if not supported.
 *
 * @param   uMsr                The MSR to write to.
 * @param   idCpu               The CPU to write the MSR on. NIL_RTCPUID
 *                              indicates any suitable CPU.
 * @param   uValue              The value to write.
 */
int VBOXCALL    supdrvOSMsrProberWrite(uint32_t uMsr, RTCPUID idCpu, uint64_t uValue);

/**
 * Tries to modify an MSR value.
 *
 * @returns One of the listed VBox status codes.
 * @retval  VINF_SUCCESS if succeeded.
 * @retval  VERR_NOT_SUPPORTED if not supported.
 *
 * @param   idCpu               The CPU to modify the MSR on. NIL_RTCPUID
 *                              indicates any suitable CPU.
 * @param   pReq                The request packet with input arguments and
 *                              where to store the results.
 */
int VBOXCALL    supdrvOSMsrProberModify(RTCPUID idCpu, PSUPMSRPROBER pReq);

#endif /* SUPDRV_WITH_MSR_PROBER */

#if defined(RT_OS_DARWIN)
int VBOXCALL    supdrvDarwinResumeSuspendedKbds(void);
#endif


/*********************************************************************************************************************************
*   Shared Functions                                                                                                             *
*********************************************************************************************************************************/
/* SUPDrv.c */
int  VBOXCALL   supdrvIOCtl(uintptr_t uIOCtl, PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPREQHDR pReqHdr, size_t cbReq);
int  VBOXCALL   supdrvIOCtlFast(uintptr_t uOperation, VMCPUID idCpu, PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession);
int  VBOXCALL   supdrvIDC(uintptr_t uIOCtl, PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPDRVIDCREQHDR pReqHdr);
int  VBOXCALL   supdrvInitDevExt(PSUPDRVDEVEXT pDevExt, size_t cbSession);
void VBOXCALL   supdrvDeleteDevExt(PSUPDRVDEVEXT pDevExt);
int  VBOXCALL   supdrvCreateSession(PSUPDRVDEVEXT pDevExt, bool fUser, bool fUnrestricted,  PSUPDRVSESSION *ppSession);
int  VBOXCALL   supdrvSessionHashTabInsert(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPDRVSESSION *ppOsSessionPtr, void *pvUser);
int  VBOXCALL   supdrvSessionHashTabRemove(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, void *pvUser);
PSUPDRVSESSION VBOXCALL supdrvSessionHashTabLookup(PSUPDRVDEVEXT pDevExt, RTPROCESS Process, RTR0PROCESS R0Process,
                                                   PSUPDRVSESSION *ppOsSessionPtr);
uint32_t VBOXCALL supdrvSessionRetain(PSUPDRVSESSION pSession);
uint32_t VBOXCALL supdrvSessionRelease(PSUPDRVSESSION pSession);
void VBOXCALL   supdrvBadContext(PSUPDRVDEVEXT pDevExt, const char *pszFile, uint32_t uLine, const char *pszExtra);
int VBOXCALL    supdrvQueryVTCapsInternal(uint32_t *pfCaps);
int VBOXCALL    supdrvLdrLoadError(int rc, PSUPLDRLOAD pReq, const char *pszFormat, ...);
int VBOXCALL    supdrvLdrGetExportedSymbol(const char *pszSymbol, uintptr_t *puValue);
int VBOXCALL    supdrvLdrRegisterWrappedModule(PSUPDRVDEVEXT pDevExt, PCSUPLDRWRAPPEDMODULE pWrappedModInfo,
                                               void *pvNative, void **phMod);
int VBOXCALL    supdrvLdrDeregisterWrappedModule(PSUPDRVDEVEXT pDevExt, PCSUPLDRWRAPPEDMODULE pWrappedModInfo, void **phMod);


/* SUPDrvGip.cpp */
int  VBOXCALL   supdrvGipCreate(PSUPDRVDEVEXT pDevExt);
void VBOXCALL   supdrvGipDestroy(PSUPDRVDEVEXT pDevExt);
int  VBOXCALL   supdrvIOCtl_TscDeltaMeasure(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPTSCDELTAMEASURE pReq);
int  VBOXCALL   supdrvIOCtl_TscRead(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPTSCREAD pReq);
int  VBOXCALL   supdrvIOCtl_GipSetFlags(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uint32_t fOrMask, uint32_t fAndMask);


/* SUPDrvTracer.cpp */
int  VBOXCALL   supdrvTracerInit(PSUPDRVDEVEXT pDevExt);
void VBOXCALL   supdrvTracerTerm(PSUPDRVDEVEXT pDevExt);
void VBOXCALL   supdrvTracerModuleUnloading(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage);
void VBOXCALL   supdrvTracerCleanupSession(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession);
int  VBOXCALL   supdrvIOCtl_TracerUmodRegister(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession,
                                               RTR3PTR R3PtrVtgHdr, RTUINTPTR uVtgHdrAddr,
                                               RTR3PTR R3PtrStrTab, uint32_t cbStrTab,
                                               const char *pszModName, uint32_t fFlags);
int  VBOXCALL   supdrvIOCtl_TracerUmodDeregister(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, RTR3PTR R3PtrVtgHdr);
void  VBOXCALL  supdrvIOCtl_TracerUmodProbeFire(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, PSUPDRVTRACERUSRCTX pCtx);
int  VBOXCALL   supdrvIOCtl_TracerOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uint32_t uCookie, uintptr_t uArg);
int  VBOXCALL   supdrvIOCtl_TracerClose(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession);
int  VBOXCALL   supdrvIOCtl_TracerIOCtl(PSUPDRVDEVEXT pDevExt, PSUPDRVSESSION pSession, uintptr_t uCmd, uintptr_t uArg, int32_t *piRetVal);
extern PFNRT    g_pfnSupdrvProbeFireKernel;
DECLASM(void)   supdrvTracerProbeFireStub(void);

#ifdef VBOX_WITH_NATIVE_DTRACE
const SUPDRVTRACERREG * VBOXCALL supdrvDTraceInit(void);
void                    VBOXCALL supdrvDTraceFini(void);
#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Support_SUPDrvInternal_h */

