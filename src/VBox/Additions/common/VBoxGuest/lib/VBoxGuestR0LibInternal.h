/* $Id: VBoxGuestR0LibInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuestLibR0 - Internal header.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_common_VBoxGuest_lib_VBoxGuestR0LibInternal_h
#define GA_INCLUDED_SRC_common_VBoxGuest_lib_VBoxGuestR0LibInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/*
 * Define the private IDC handle structure before we include the VBoxGuestLib.h header.
 */
#include <iprt/types.h>
#include <iprt/assert.h>
RT_C_DECLS_BEGIN

# ifndef VBGL_VBOXGUEST
/**
 * The hidden part of VBGLIDCHANDLE.
 */
struct VBGLIDCHANDLEPRIVATE
{
    /** Pointer to the session handle. */
    void           *pvSession;
# if defined(RT_OS_WINDOWS) && (defined(IPRT_INCLUDED_nt_ntddk_h) || defined(IPRT_INCLUDED_nt_nt_h))
    /** Pointer to the NT device object. */
    PDEVICE_OBJECT  pDeviceObject;
    /** Pointer to the NT file object. */
    PFILE_OBJECT    pFileObject;
# elif defined(RT_OS_SOLARIS) && defined(_SYS_SUNLDI_H)
    /** LDI device handle to keep the device attached. */
    ldi_handle_t    hDev;
# endif
};
/** Indicate that the VBGLIDCHANDLEPRIVATE structure is present. */
# define VBGLIDCHANDLEPRIVATE_DECLARED 1
#endif

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxGuestLib.h>

#ifdef VBGLIDCHANDLEPRIVATE_DECLARED
AssertCompile(RT_SIZEOFMEMB(VBGLIDCHANDLE, apvPadding) >= sizeof(struct VBGLIDCHANDLEPRIVATE));
#endif


/*
 * Native IDC functions.
 */
int VBOXCALL vbglR0IdcNativeOpen(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCCONNECT pReq);
int VBOXCALL vbglR0IdcNativeClose(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCDISCONNECT pReq);


/*
 * Deprecated logging macro
 */
#include <VBox/log.h>
#ifdef RT_OS_WINDOWS /** @todo dprintf() -> Log() */
# if (defined(DEBUG) && !defined(NO_LOGGING)) || defined(LOG_ENABLED)
#  define dprintf(a) RTLogBackdoorPrintf a
# else
#  define dprintf(a) do {} while (0)
# endif
#else
# define dprintf(a) Log(a)
#endif

/*
 * Lazy bird: OS/2 doesn't currently implement the RTSemMutex API in ring-0, so
 *  use a fast mutex instead.   Unlike Windows, the OS/2 implementation
 *  doesn't have any nasty side effects on IRQL-like context properties, so the
 *  fast mutexes on OS/2 are identical to normal mutexes except for the missing
 *  timeout aspec.  Fortunately we don't need timeouts here.
 */
#ifdef RT_OS_OS2
# define VBGLDATA_USE_FAST_MUTEX
#endif

struct VBGLPHYSHEAPBLOCK;
typedef struct VBGLPHYSHEAPBLOCK VBGLPHYSHEAPBLOCK;
struct VBGLPHYSHEAPFREEBLOCK;
typedef struct VBGLPHYSHEAPFREEBLOCK VBGLPHYSHEAPFREEBLOCK;
struct VBGLPHYSHEAPCHUNK;
typedef struct VBGLPHYSHEAPCHUNK VBGLPHYSHEAPCHUNK;

enum VbglLibStatus
{
    VbglStatusNotInitialized = 0,
    VbglStatusInitializing,
    VbglStatusReady
};

/**
 * Global VBGL ring-0 data.
 * Lives in VBoxGuestR0LibInit.cpp.
 */
typedef struct VBGLDATA
{
    /** Init status of the library. */
    enum VbglLibStatus      status;
    /** I/O port to issue requests to. */
    RTIOPORT                portVMMDev;
    /** MMIO request region if available. */
    volatile uintptr_t      *pMmioReq;
    /** VMMDev adapter memory region if available. */
    VMMDevMemory            *pVMMDevMemory;

    /** Physical memory heap data.
     * @{
     */
    RTSEMFASTMUTEX          hMtxHeap;
    /** Head of the block list (both free and allocated blocks).
     * This runs parallel to the chunk list and is sorted by address within each
     * chunk.  This allows merging with blocks both after and before when
     * freeing. */
    VBGLPHYSHEAPBLOCK      *pBlockHead;
    /** Head of the free list.
     *  This is not sorted and more on the most recently freed approach. */
    VBGLPHYSHEAPFREEBLOCK  *pFreeHead;
    /** Number of block of any kind. */
    int32_t                 cBlocks;
    /** Number of free blocks. */
    int32_t                 cFreeBlocks;
    /** Head of the chunk list. */
    VBGLPHYSHEAPCHUNK      *pChunkHead;
    /** Maximum physical address allowed for allocations, inclusive. */
    RTHCPHYS                HCPhysMax;
    /** @} */

    /**
     * The host version data.
     */
    VMMDevReqHostVersion hostVersion;


#ifndef VBGL_VBOXGUEST
    /** The IDC handle.  This is used for talking to the main driver. */
    VBGLIDCHANDLE IdcHandle;
    /** Mutex used to serialize IDC setup.   */
# ifdef VBGLDATA_USE_FAST_MUTEX
    RTSEMFASTMUTEX hMtxIdcSetup;
# else
    RTSEMMUTEX hMtxIdcSetup;
# endif
#endif
} VBGLDATA;


extern VBGLDATA g_vbgldata;

/**
 * Internal macro for checking whether we can pass physical page lists to the
 * host.
 *
 * ASSUMES that vbglR0Enter has been called already.
 *
 * @param   a_fLocked       For the windows shared folders workarounds.
 *
 * @remarks Disabled the PageList feature for locked memory on Windows,
 *          because a new MDL is created by VBGL to get the page addresses
 *          and the pages from the MDL are marked as dirty when they should not.
 */
#if defined(RT_OS_WINDOWS)
# define VBGLR0_CAN_USE_PHYS_PAGE_LIST(a_fLocked) \
    ( !(a_fLocked) && (g_vbgldata.hostVersion.features & VMMDEV_HVF_HGCM_PHYS_PAGE_LIST) )
#else
# define VBGLR0_CAN_USE_PHYS_PAGE_LIST(a_fLocked) \
    ( !!(g_vbgldata.hostVersion.features & VMMDEV_HVF_HGCM_PHYS_PAGE_LIST) )
#endif

int vbglR0Enter (void);

#ifdef VBOX_WITH_HGCM
struct VBGLHGCMHANDLEDATA  *vbglR0HGCMHandleAlloc(void);
void                        vbglR0HGCMHandleFree(struct VBGLHGCMHANDLEDATA *pHandle);
#endif /* VBOX_WITH_HGCM */

#ifndef VBGL_VBOXGUEST
/**
 * Get the IDC handle to the main VBoxGuest driver.
 * @returns VERR_TRY_AGAIN if the main driver has not yet been loaded.
 */
int VBOXCALL vbglR0QueryIdcHandle(PVBGLIDCHANDLE *ppIdcHandle);
#endif

RT_C_DECLS_END

#endif /* !GA_INCLUDED_SRC_common_VBoxGuest_lib_VBoxGuestR0LibInternal_h */

