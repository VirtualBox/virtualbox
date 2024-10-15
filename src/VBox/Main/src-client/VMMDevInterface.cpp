/* $Id: VMMDevInterface.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Driver Interface to VMM device.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define LOG_GROUP LOG_GROUP_MAIN_VMMDEVINTERFACES
#include "LoggingNew.h"

#include "VMMDev.h"
#include "ConsoleImpl.h"
#include "DisplayImpl.h"
#include "GuestImpl.h"
#include "MouseImpl.h"

#include <VBox/vmm/pdmdrv.h>
#include <VBox/VMMDev.h>
#include <VBox/shflsvc.h>
#include <iprt/asm.h>

#ifdef VBOX_WITH_HGCM
# include "HGCM.h"
# include "HGCMObjects.h"
#endif

//
// defines
//

#ifdef RT_OS_OS2
# define VBOXSHAREDFOLDERS_DLL "VBoxSFld"
#else
# define VBOXSHAREDFOLDERS_DLL "VBoxSharedFolders"
#endif

//
// globals
//


/**
 * VMMDev driver instance data.
 */
typedef struct DRVMAINVMMDEV
{
    /** Pointer to the VMMDev object. */
    VMMDev                     *pVMMDev;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the VMMDev port interface of the driver/device above us. */
    PPDMIVMMDEVPORT             pUpPort;
    /** Our VMM device connector interface. */
    PDMIVMMDEVCONNECTOR         Connector;

#ifdef VBOX_WITH_HGCM
    /** Pointer to the HGCM port interface of the driver/device above us. */
    PPDMIHGCMPORT               pHGCMPort;
    /** Our HGCM connector interface. */
    PDMIHGCMCONNECTOR           HGCMConnector;
#endif

#ifdef VBOX_WITH_GUEST_PROPS
    HGCMSVCEXTHANDLE            hHgcmSvcExtGstProps;
#endif
#ifdef VBOX_WITH_GUEST_CONTROL
    HGCMSVCEXTHANDLE            hHgcmSvcExtGstCtrl;
#endif
} DRVMAINVMMDEV, *PDRVMAINVMMDEV;

//
// constructor / destructor
//
VMMDev::VMMDev(Console *console)
    : mpDrv(NULL)
    , mParent(console)
{
    int vrc = RTSemEventCreate(&mCredentialsEvent);
    AssertRC(vrc);
#ifdef VBOX_WITH_HGCM
    vrc = HGCMHostInit();
    AssertRC(vrc);
    m_fHGCMActive = true;
#endif /* VBOX_WITH_HGCM */
    mu32CredentialsFlags = 0;
}

VMMDev::~VMMDev()
{
#ifdef VBOX_WITH_HGCM
    if (ASMAtomicCmpXchgBool(&m_fHGCMActive, false, true))
        HGCMHostShutdown(true /*fUvmIsInvalid*/);
#endif
    RTSemEventDestroy(mCredentialsEvent);
    if (mpDrv)
        mpDrv->pVMMDev = NULL;
    mpDrv = NULL;
}

PPDMIVMMDEVPORT VMMDev::getVMMDevPort()
{
    if (!mpDrv)
        return NULL;
    return mpDrv->pUpPort;
}



//
// public methods
//

/**
 * Wait on event semaphore for guest credential judgement result.
 */
int VMMDev::WaitCredentialsJudgement(uint32_t u32Timeout, uint32_t *pu32CredentialsFlags)
{
    if (u32Timeout == 0)
    {
        u32Timeout = 5000;
    }

    int vrc = RTSemEventWait(mCredentialsEvent, u32Timeout);

    if (RT_SUCCESS(vrc))
    {
        *pu32CredentialsFlags = mu32CredentialsFlags;
    }

    return vrc;
}

int VMMDev::SetCredentialsJudgementResult(uint32_t u32Flags)
{
    mu32CredentialsFlags = u32Flags;

    int vrc = RTSemEventSignal(mCredentialsEvent);
    AssertRC(vrc);

    return vrc;
}


/**
 * @interface_method_impl{PDMIVMMDEVCONNECTOR,pfnUpdateGuestStatus}
 */
static DECLCALLBACK(void) vmmdevUpdateGuestStatus(PPDMIVMMDEVCONNECTOR pInterface, uint32_t uFacility, uint16_t uStatus,
                                                  uint32_t fFlags, PCRTTIMESPEC pTimeSpecTS)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    /* Store that information in IGuest */
    Guest* guest = pConsole->i_getGuest();
    AssertPtrReturnVoid(guest);

    guest->i_setAdditionsStatus((VBoxGuestFacilityType)uFacility, (VBoxGuestFacilityStatus)uStatus, fFlags, pTimeSpecTS);
    pConsole->i_onAdditionsStateChange();
}


/**
 * @interface_method_impl{PDMIVMMDEVCONNECTOR,pfnUpdateGuestUserState}
 */
static DECLCALLBACK(void) vmmdevUpdateGuestUserState(PPDMIVMMDEVCONNECTOR pInterface,
                                                     const char *pszUser, const char *pszDomain,
                                                     uint32_t uState,
                                                     const uint8_t *pabDetails, uint32_t cbDetails)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    AssertPtr(pDrv);
    Console *pConsole = pDrv->pVMMDev->getParent();
    AssertPtr(pConsole);

    /* Store that information in IGuest. */
    Guest* pGuest = pConsole->i_getGuest();
    AssertPtrReturnVoid(pGuest);

    pGuest->i_onUserStateChanged(Utf8Str(pszUser), Utf8Str(pszDomain), (VBoxGuestUserState)uState, pabDetails, cbDetails);
}


/**
 * Reports Guest Additions API and OS version.
 *
 * Called whenever the Additions issue a guest version report request or the VM
 * is reset.
 *
 * @param   pInterface          Pointer to this interface.
 * @param   guestInfo           Pointer to guest information structure.
 * @thread  The emulation thread.
 */
static DECLCALLBACK(void) vmmdevUpdateGuestInfo(PPDMIVMMDEVCONNECTOR pInterface, const VBoxGuestInfo *guestInfo)
{
    AssertPtrReturnVoid(guestInfo);

    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    /* Store that information in IGuest */
    Guest* guest = pConsole->i_getGuest();
    AssertPtrReturnVoid(guest);

    if (guestInfo->interfaceVersion != 0)
    {
        char version[16];
        RTStrPrintf(version, sizeof(version), "%d", guestInfo->interfaceVersion);
        guest->i_setAdditionsInfo(Bstr(version), guestInfo->osType);

        /*
         * Tell the console interface about the event
         * so that it can notify its consumers.
         */
        pConsole->i_onAdditionsStateChange();

        if (guestInfo->interfaceVersion < VMMDEV_VERSION)
            pConsole->i_onAdditionsOutdated();
    }
    else
    {
        /*
         * The Guest Additions was disabled because of a reset
         * or driver unload.
         */
        guest->i_setAdditionsInfo(Bstr(), guestInfo->osType); /* Clear interface version + OS type. */
        /** @todo Would be better if GuestImpl.cpp did all this in the above method call
         *        while holding down the. */
        guest->i_setAdditionsInfo2(0, "", 0,  0); /* Clear Guest Additions version. */
        RTTIMESPEC TimeSpecTS;
        RTTimeNow(&TimeSpecTS);
        guest->i_setAdditionsStatus(VBoxGuestFacilityType_All, VBoxGuestFacilityStatus_Inactive, 0 /*fFlags*/, &TimeSpecTS);
        pConsole->i_onAdditionsStateChange();
    }
}

/**
 * @interface_method_impl{PDMIVMMDEVCONNECTOR,pfnUpdateGuestInfo2}
 */
static DECLCALLBACK(void) vmmdevUpdateGuestInfo2(PPDMIVMMDEVCONNECTOR pInterface, uint32_t uFullVersion,
                                                 const char *pszName, uint32_t uRevision, uint32_t fFeatures)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    AssertPtr(pszName);
    Assert(uFullVersion);

    /* Store that information in IGuest. */
    Guest *pGuest = pDrv->pVMMDev->getParent()->i_getGuest();
    AssertPtrReturnVoid(pGuest);

    /* Just pass it on... */
    pGuest->i_setAdditionsInfo2(uFullVersion, pszName, uRevision, fFeatures);

    /*
     * No need to tell the console interface about the update;
     * vmmdevUpdateGuestInfo takes care of that when called as the
     * last event in the chain.
     */
}

/**
 * Update the Guest Additions capabilities.
 * This is called when the Guest Additions capabilities change. The new capabilities
 * are given and the connector should update its internal state.
 *
 * @param   pInterface          Pointer to this interface.
 * @param   newCapabilities     New capabilities.
 * @thread  The emulation thread.
 */
static DECLCALLBACK(void) vmmdevUpdateGuestCapabilities(PPDMIVMMDEVCONNECTOR pInterface, uint32_t newCapabilities)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    AssertPtr(pDrv);
    Console *pConsole = pDrv->pVMMDev->getParent();

    /* store that information in IGuest */
    Guest* pGuest = pConsole->i_getGuest();
    AssertPtrReturnVoid(pGuest);

    /*
     * Report our current capabilities (and assume none is active yet).
     */
    pGuest->i_setSupportedFeatures(newCapabilities);

    /*
     * Tell the Display, so that it can update the "supports graphics"
     * capability if the graphics card has not asserted it.
     */
    Display* pDisplay = pConsole->i_getDisplay();
    AssertPtrReturnVoid(pDisplay);
    pDisplay->i_handleUpdateVMMDevSupportsGraphics(RT_BOOL(newCapabilities & VMMDEV_GUEST_SUPPORTS_GRAPHICS));

    /*
     * Tell the console interface about the event
     * so that it can notify its consumers.
     */
    pConsole->i_onAdditionsStateChange();
}

/**
 * Update the mouse capabilities.
 * This is called when the mouse capabilities change. The new capabilities
 * are given and the connector should update its internal state.
 *
 * @param   pInterface          Pointer to this interface.
 * @param   fNewCaps            New capabilities.
 * @thread  The emulation thread.
 */
static DECLCALLBACK(void) vmmdevUpdateMouseCapabilities(PPDMIVMMDEVCONNECTOR pInterface, uint32_t fNewCaps)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    /*
     * Tell the console interface about the event
     * so that it can notify its consumers.
     */
    Mouse *pMouse = pConsole->i_getMouse();
    if (pMouse)  /** @todo and if not?  Can that actually happen? */
        pMouse->i_onVMMDevGuestCapsChange(fNewCaps & VMMDEV_MOUSE_GUEST_MASK);
}

/**
 * Update the pointer shape or visibility.
 *
 * This is called when the mouse pointer shape changes or pointer is hidden/displaying.
 * The new shape is passed as a caller allocated buffer that will be freed after returning.
 *
 * @param   pInterface          Pointer to this interface.
 * @param   fVisible            Whether the pointer is visible or not.
 * @param   fAlpha              Alpha channel information is present.
 * @param   xHot                Horizontal coordinate of the pointer hot spot.
 * @param   yHot                Vertical coordinate of the pointer hot spot.
 * @param   width               Pointer width in pixels.
 * @param   height              Pointer height in pixels.
 * @param   pShape              The shape buffer. If NULL, then only pointer visibility is being changed.
 * @thread  The emulation thread.
 */
static DECLCALLBACK(void) vmmdevUpdatePointerShape(PPDMIVMMDEVCONNECTOR pInterface, bool fVisible, bool fAlpha,
                                                   uint32_t xHot, uint32_t yHot,
                                                   uint32_t width, uint32_t height,
                                                   void *pShape)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    /* tell the console about it */
    uint32_t cbShape = 0;
    if (pShape)
    {
        cbShape = (width + 7) / 8 * height; /* size of the AND mask */
        cbShape = ((cbShape + 3) & ~3) + width * 4 * height; /* + gap + size of the XOR mask */
    }
    pConsole->i_onMousePointerShapeChange(fVisible, fAlpha, xHot, yHot, width, height, (uint8_t *)pShape, cbShape);
}

static DECLCALLBACK(int) iface_VideoAccelEnable(PPDMIVMMDEVCONNECTOR pInterface, bool fEnable, VBVAMEMORY *pVbvaMemory)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    Display *display = pConsole->i_getDisplay();

    if (display)
    {
        Log9(("MAIN::VMMDevInterface::iface_VideoAccelEnable: %d, %p\n", fEnable, pVbvaMemory));
        return display->VideoAccelEnableVMMDev(fEnable, pVbvaMemory);
    }

    return VERR_NOT_SUPPORTED;
}

static DECLCALLBACK(void) iface_VideoAccelFlush(PPDMIVMMDEVCONNECTOR pInterface)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    Display *display = pConsole->i_getDisplay();

    if (display)
    {
        Log9(("MAIN::VMMDevInterface::iface_VideoAccelFlush\n"));
        display->VideoAccelFlushVMMDev();
    }
}

static DECLCALLBACK(int) vmmdevVideoModeSupported(PPDMIVMMDEVCONNECTOR pInterface, uint32_t display, uint32_t width, uint32_t height,
                                                  uint32_t bpp, bool *fSupported)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    if (!fSupported)
        return VERR_INVALID_PARAMETER;
#ifdef DEBUG_sunlover
    Log(("vmmdevVideoModeSupported: [%d]: %dx%dx%d\n", display, width, height, bpp));
#endif
    IFramebuffer *framebuffer = NULL;
    HRESULT hrc = pConsole->i_getDisplay()->QueryFramebuffer(display, &framebuffer);
    if (SUCCEEDED(hrc) && framebuffer)
    {
        framebuffer->VideoModeSupported(width, height, bpp, (BOOL*)fSupported);
        framebuffer->Release();
    }
    else
    {
#ifdef DEBUG_sunlover
        Log(("vmmdevVideoModeSupported: hrc %x, framebuffer %p!!!\n", hrc, framebuffer));
#endif
        *fSupported = true;
    }
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmmdevGetHeightReduction(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *heightReduction)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    if (!heightReduction)
        return VERR_INVALID_PARAMETER;
    IFramebuffer *framebuffer = NULL;
    HRESULT hrc = pConsole->i_getDisplay()->QueryFramebuffer(0, &framebuffer);
    if (SUCCEEDED(hrc) && framebuffer)
    {
        framebuffer->COMGETTER(HeightReduction)((ULONG*)heightReduction);
        framebuffer->Release();
    }
    else
        *heightReduction = 0;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmmdevSetCredentialsJudgementResult(PPDMIVMMDEVCONNECTOR pInterface, uint32_t u32Flags)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);

    if (pDrv->pVMMDev)
        return pDrv->pVMMDev->SetCredentialsJudgementResult(u32Flags);

    return VERR_GENERAL_FAILURE;
}

static DECLCALLBACK(int) vmmdevSetVisibleRegion(PPDMIVMMDEVCONNECTOR pInterface, uint32_t cRect, PRTRECT pRect)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    /* Forward to Display, which calls corresponding framebuffers. */
    pConsole->i_getDisplay()->i_handleSetVisibleRegion(cRect, pRect);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVCONNECTOR,pfnUpdateMonitorPositions}
 */
static DECLCALLBACK(int) vmmdevUpdateMonitorPositions(PPDMIVMMDEVCONNECTOR pInterface, uint32_t cPositions, PCRTPOINT paPositions)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    pConsole->i_getDisplay()->i_handleUpdateMonitorPositions(cPositions, paPositions);

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) vmmdevQueryVisibleRegion(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *pcRects, PRTRECT paRects)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    /* Forward to Display, which calls corresponding framebuffers. */
    pConsole->i_getDisplay()->i_handleQueryVisibleRegion(pcRects, paRects);

    return VINF_SUCCESS;
}

/**
 * Request the statistics interval
 *
 * @returns VBox status code.
 * @param   pInterface          Pointer to this interface.
 * @param   pulInterval         Pointer to interval in seconds
 * @thread  The emulation thread.
 */
static DECLCALLBACK(int) vmmdevQueryStatisticsInterval(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *pulInterval)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();
    ULONG          val = 0;

    if (!pulInterval)
        return VERR_INVALID_POINTER;

    /* store that information in IGuest */
    Guest* guest = pConsole->i_getGuest();
    AssertPtrReturn(guest, VERR_GENERAL_FAILURE);

    guest->COMGETTER(StatisticsUpdateInterval)(&val);
    *pulInterval = val;
    return VINF_SUCCESS;
}

/**
 * Query the current balloon size
 *
 * @returns VBox status code.
 * @param   pInterface          Pointer to this interface.
 * @param   pcbBalloon          Balloon size
 * @thread  The emulation thread.
 */
static DECLCALLBACK(int) vmmdevQueryBalloonSize(PPDMIVMMDEVCONNECTOR pInterface, uint32_t *pcbBalloon)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();
    ULONG          val = 0;

    if (!pcbBalloon)
        return VERR_INVALID_POINTER;

    /* store that information in IGuest */
    Guest* guest = pConsole->i_getGuest();
    AssertPtrReturn(guest, VERR_GENERAL_FAILURE);

    guest->COMGETTER(MemoryBalloonSize)(&val);
    *pcbBalloon = val;
    return VINF_SUCCESS;
}

/**
 * Query the current page fusion setting
 *
 * @returns VBox status code.
 * @param   pInterface          Pointer to this interface.
 * @param   pfPageFusionEnabled Pointer to boolean
 * @thread  The emulation thread.
 */
static DECLCALLBACK(int) vmmdevIsPageFusionEnabled(PPDMIVMMDEVCONNECTOR pInterface, bool *pfPageFusionEnabled)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    if (!pfPageFusionEnabled)
        return VERR_INVALID_POINTER;

    /* store that information in IGuest */
    Guest* guest = pConsole->i_getGuest();
    AssertPtrReturn(guest, VERR_GENERAL_FAILURE);

    *pfPageFusionEnabled = !!guest->i_isPageFusionEnabled();
    return VINF_SUCCESS;
}

/**
 * Report new guest statistics
 *
 * @returns VBox status code.
 * @param   pInterface          Pointer to this interface.
 * @param   pGuestStats         Guest statistics
 * @thread  The emulation thread.
 */
static DECLCALLBACK(int) vmmdevReportStatistics(PPDMIVMMDEVCONNECTOR pInterface, VBoxGuestStatistics *pGuestStats)
{
    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, Connector);
    Console *pConsole = pDrv->pVMMDev->getParent();

    AssertPtrReturn(pGuestStats, VERR_INVALID_POINTER);

    /* store that information in IGuest */
    Guest* guest = pConsole->i_getGuest();
    AssertPtrReturn(guest, VERR_GENERAL_FAILURE);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_CPU_LOAD_IDLE)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_CPUIDLE, pGuestStats->u32CpuLoad_Idle);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_CPU_LOAD_KERNEL)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_CPUKERNEL, pGuestStats->u32CpuLoad_Kernel);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_CPU_LOAD_USER)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_CPUUSER, pGuestStats->u32CpuLoad_User);


    /** @todo r=bird: Convert from 4KB to 1KB units?
     *        CollectorGuestHAL::i_getGuestMemLoad says it returns KB units to
     *        preCollect().  I might be wrong ofc, this is convoluted code... */
    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PHYS_MEM_TOTAL)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_MEMTOTAL, pGuestStats->u32PhysMemTotal);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PHYS_MEM_AVAIL)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_MEMFREE, pGuestStats->u32PhysMemAvail);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PHYS_MEM_BALLOON)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_MEMBALLOON, pGuestStats->u32PhysMemBalloon);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_MEM_SYSTEM_CACHE)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_MEMCACHE, pGuestStats->u32MemSystemCache);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PAGE_FILE_SIZE)
        guest->i_setStatistic(pGuestStats->u32CpuId, GUESTSTATTYPE_PAGETOTAL, pGuestStats->u32PageFileSize);

    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_HGCM

/* HGCM connector interface */

static DECLCALLBACK(int) iface_hgcmConnect(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd,
                                           PHGCMSERVICELOCATION pServiceLocation,
                                           uint32_t *pu32ClientID)
{
    Log9(("Enter\n"));

    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, HGCMConnector);

    if (    !pServiceLocation
        || (   pServiceLocation->type != VMMDevHGCMLoc_LocalHost
            && pServiceLocation->type != VMMDevHGCMLoc_LocalHost_Existing))
    {
        return VERR_INVALID_PARAMETER;
    }

    /* Check if service name is a string terminated by zero*/
    size_t cchInfo = 0;
    if (RTStrNLenEx(pServiceLocation->u.host.achName, sizeof(pServiceLocation->u.host.achName), &cchInfo) != VINF_SUCCESS)
    {
        return VERR_INVALID_PARAMETER;
    }

    if (!pDrv->pVMMDev || !pDrv->pVMMDev->hgcmIsActive())
        return VERR_INVALID_STATE;
    return HGCMGuestConnect(pDrv->pHGCMPort, pCmd, pServiceLocation->u.host.achName, pu32ClientID);
}

static DECLCALLBACK(int) iface_hgcmDisconnect(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd, uint32_t u32ClientID)
{
    Log9(("Enter\n"));

    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, HGCMConnector);

    if (!pDrv->pVMMDev || !pDrv->pVMMDev->hgcmIsActive())
        return VERR_INVALID_STATE;

    return HGCMGuestDisconnect(pDrv->pHGCMPort, pCmd, u32ClientID);
}

static DECLCALLBACK(int) iface_hgcmCall(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd, uint32_t u32ClientID,
                                        uint32_t u32Function, uint32_t cParms, PVBOXHGCMSVCPARM paParms, uint64_t tsArrival)
{
    Log9(("Enter\n"));

    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, HGCMConnector);

    if (!pDrv->pVMMDev || !pDrv->pVMMDev->hgcmIsActive())
        return VERR_INVALID_STATE;

    return HGCMGuestCall(pDrv->pHGCMPort, pCmd, u32ClientID, u32Function, cParms, paParms, tsArrival);
}

static DECLCALLBACK(void) iface_hgcmCancelled(PPDMIHGCMCONNECTOR pInterface, PVBOXHGCMCMD pCmd, uint32_t idClient)
{
    Log9(("Enter\n"));

    PDRVMAINVMMDEV pDrv = RT_FROM_MEMBER(pInterface, DRVMAINVMMDEV, HGCMConnector);
    if (   pDrv->pVMMDev
        && pDrv->pVMMDev->hgcmIsActive())
        return HGCMGuestCancelled(pDrv->pHGCMPort, pCmd, idClient);
}

/**
 * Execute state save operation.
 *
 * @returns VBox status code.
 * @param   pDrvIns         Driver instance of the driver which registered the data unit.
 * @param   pSSM            SSM operation handle.
 */
/*static*/ DECLCALLBACK(int) VMMDev::hgcmSave(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM)
{
    PDRVMAINVMMDEV pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINVMMDEV);
    Log9(("Enter\n"));

    AssertReturn(pThis->pVMMDev, VERR_INTERNAL_ERROR_2);
    Console::SafeVMPtrQuiet ptrVM(pThis->pVMMDev->mParent);
    AssertReturn(ptrVM.isOk(), VERR_INTERNAL_ERROR_3);
    return HGCMHostSaveState(pSSM, ptrVM.vtable());
}


/**
 * Execute state load operation.
 *
 * @returns VBox status code.
 * @param   pDrvIns         Driver instance of the driver which registered the data unit.
 * @param   pSSM            SSM operation handle.
 * @param   uVersion        Data layout version.
 * @param   uPass           The data pass.
 */
/*static*/ DECLCALLBACK(int) VMMDev::hgcmLoad(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDRVMAINVMMDEV pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINVMMDEV);
    LogFlowFunc(("Enter\n"));

    if (   uVersion != HGCM_SAVED_STATE_VERSION
        && uVersion != HGCM_SAVED_STATE_VERSION_V2)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    AssertReturn(pThis->pVMMDev, VERR_INTERNAL_ERROR_2);
    Console::SafeVMPtrQuiet ptrVM(pThis->pVMMDev->mParent);
    AssertReturn(ptrVM.isOk(), VERR_INTERNAL_ERROR_3);
    return HGCMHostLoadState(pSSM, ptrVM.vtable(), uVersion);
}

int VMMDev::hgcmLoadService(const char *pszServiceLibrary, const char *pszServiceName)
{
    if (!hgcmIsActive())
        return VERR_INVALID_STATE;

    /** @todo Construct all the services in the VMMDev::drvConstruct()!! */
    Assert(   (mpDrv && mpDrv->pHGCMPort)
           || !strcmp(pszServiceLibrary, "VBoxHostChannel")
           || !strcmp(pszServiceLibrary, "VBoxSharedClipboard")
           || !strcmp(pszServiceLibrary, "VBoxDragAndDropSvc")
           || !strcmp(pszServiceLibrary, "VBoxGuestPropSvc")
           || !strcmp(pszServiceLibrary, "VBoxSharedCrOpenGL")
           );
    Console::SafeVMPtrQuiet ptrVM(mParent);
    return HGCMHostLoad(pszServiceLibrary, pszServiceName, ptrVM.rawUVM(), ptrVM.vtable(), mpDrv ? mpDrv->pHGCMPort : NULL);
}

int VMMDev::hgcmHostCall(const char *pszServiceName, uint32_t u32Function,
                         uint32_t cParms, PVBOXHGCMSVCPARM paParms)
{
    if (!hgcmIsActive())
        return VERR_INVALID_STATE;
    return HGCMHostCall(pszServiceName, u32Function, cParms, paParms);
}

/**
 * Used by Console::i_powerDown to shut down the services before the VM is destroyed.
 */
void VMMDev::hgcmShutdown(bool fUvmIsInvalid /*= false*/)
{
#ifdef VBOX_WITH_GUEST_PROPS
    if (mpDrv && mpDrv->hHgcmSvcExtGstProps)
    {
        HGCMHostUnregisterServiceExtension(mpDrv->hHgcmSvcExtGstProps);
        mpDrv->hHgcmSvcExtGstProps = NULL;
    }
#endif

#ifdef VBOX_WITH_GUEST_CONTROL
    if (mpDrv && mpDrv->hHgcmSvcExtGstCtrl)
    {
        HGCMHostUnregisterServiceExtension(mpDrv->hHgcmSvcExtGstCtrl);
        mpDrv->hHgcmSvcExtGstCtrl = NULL;
    }
#endif

    if (ASMAtomicCmpXchgBool(&m_fHGCMActive, false, true))
        HGCMHostShutdown(fUvmIsInvalid);
}

#endif /* HGCM */


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *) VMMDev::drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS      pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINVMMDEV  pDrv    = PDMINS_2_DATA(pDrvIns, PDRVMAINVMMDEV);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIVMMDEVCONNECTOR, &pDrv->Connector);
#ifdef VBOX_WITH_HGCM
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHGCMCONNECTOR, &pDrv->HGCMConnector);
#endif
    return NULL;
}

/**
 * @interface_method_impl{PDMDRVREG,pfnSuspend}
 */
/*static*/ DECLCALLBACK(void) VMMDev::drvSuspend(PPDMDRVINS pDrvIns)
{
    RT_NOREF(pDrvIns);
#ifdef VBOX_WITH_HGCM
    HGCMBroadcastEvent(HGCMNOTIFYEVENT_SUSPEND);
#endif
}

/**
 * @interface_method_impl{PDMDRVREG,pfnResume}
 */
/*static*/ DECLCALLBACK(void) VMMDev::drvResume(PPDMDRVINS pDrvIns)
{
    RT_NOREF(pDrvIns);
#ifdef VBOX_WITH_HGCM
    HGCMBroadcastEvent(HGCMNOTIFYEVENT_RESUME);
#endif
}

/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOff}
 */
/*static*/ DECLCALLBACK(void) VMMDev::drvPowerOff(PPDMDRVINS pDrvIns)
{
    RT_NOREF(pDrvIns);
#ifdef VBOX_WITH_HGCM
    HGCMBroadcastEvent(HGCMNOTIFYEVENT_POWER_ON);
#endif
}

/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOn}
 */
/*static*/ DECLCALLBACK(void) VMMDev::drvPowerOn(PPDMDRVINS pDrvIns)
{
    RT_NOREF(pDrvIns);
#ifdef VBOX_WITH_HGCM
    HGCMBroadcastEvent(HGCMNOTIFYEVENT_POWER_ON);
#endif
}

/**
 * @interface_method_impl{PDMDRVREG,pfnReset}
 */
DECLCALLBACK(void) VMMDev::drvReset(PPDMDRVINS pDrvIns)
{
    RT_NOREF(pDrvIns);
    LogFlow(("VMMDev::drvReset: iInstance=%d\n", pDrvIns->iInstance));
#ifdef VBOX_WITH_HGCM
    HGCMHostReset(false /*fForShutdown*/);
#endif
}

/**
 * @interface_method_impl{PDMDRVREG,pfnDestruct}
 */
DECLCALLBACK(void) VMMDev::drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINVMMDEV pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINVMMDEV);
    LogFlow(("VMMDev::drvDestruct: iInstance=%d\n", pDrvIns->iInstance));

#ifdef VBOX_WITH_GUEST_PROPS
    if (pThis->hHgcmSvcExtGstProps)
    {
        HGCMHostUnregisterServiceExtension(pThis->hHgcmSvcExtGstProps);
        pThis->hHgcmSvcExtGstProps = NULL;
    }
#endif

#ifdef VBOX_WITH_GUEST_CONTROL
    if (pThis->hHgcmSvcExtGstCtrl)
    {
        HGCMHostUnregisterServiceExtension(pThis->hHgcmSvcExtGstCtrl);
        pThis->hHgcmSvcExtGstCtrl = NULL;
    }
#endif

    if (pThis->pVMMDev)
    {
#ifdef VBOX_WITH_HGCM
        /* When VM construction goes wrong, we prefer shutting down HGCM here
           while pUVM is still valid, rather than in ~VMMDev. */
        if (ASMAtomicCmpXchgBool(&pThis->pVMMDev->m_fHGCMActive, false, true))
            HGCMHostShutdown();
#endif
        pThis->pVMMDev->mpDrv = NULL;
    }
}

#ifdef VBOX_WITH_GUEST_PROPS

/**
 * Set an array of guest properties
 */
void VMMDev::i_guestPropSetMultiple(void *names, void *values, void *timestamps, void *flags)
{
    VBOXHGCMSVCPARM parms[4];

    parms[0].type = VBOX_HGCM_SVC_PARM_PTR;
    parms[0].u.pointer.addr = names;
    parms[0].u.pointer.size = 0;  /* We don't actually care. */
    parms[1].type = VBOX_HGCM_SVC_PARM_PTR;
    parms[1].u.pointer.addr = values;
    parms[1].u.pointer.size = 0;  /* We don't actually care. */
    parms[2].type = VBOX_HGCM_SVC_PARM_PTR;
    parms[2].u.pointer.addr = timestamps;
    parms[2].u.pointer.size = 0;  /* We don't actually care. */
    parms[3].type = VBOX_HGCM_SVC_PARM_PTR;
    parms[3].u.pointer.addr = flags;
    parms[3].u.pointer.size = 0;  /* We don't actually care. */

    hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_SET_PROPS, 4, &parms[0]);
}

/**
 * Set a single guest property
 */
void VMMDev::i_guestPropSet(const char *pszName, const char *pszValue, const char *pszFlags)
{
    VBOXHGCMSVCPARM parms[4];

    AssertPtrReturnVoid(pszName);
    AssertPtrReturnVoid(pszValue);
    AssertPtrReturnVoid(pszFlags);
    parms[0].type = VBOX_HGCM_SVC_PARM_PTR;
    parms[0].u.pointer.addr = (void *)pszName;
    parms[0].u.pointer.size = (uint32_t)strlen(pszName) + 1;
    parms[1].type = VBOX_HGCM_SVC_PARM_PTR;
    parms[1].u.pointer.addr = (void *)pszValue;
    parms[1].u.pointer.size = (uint32_t)strlen(pszValue) + 1;
    parms[2].type = VBOX_HGCM_SVC_PARM_PTR;
    parms[2].u.pointer.addr = (void *)pszFlags;
    parms[2].u.pointer.size = (uint32_t)strlen(pszFlags) + 1;
    hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_SET_PROP, 3, &parms[0]);
}

/**
 * Set the global flags value by calling the service
 * @returns the status returned by the call to the service
 *
 * @param   pTable  the service instance handle
 * @param   eFlags  the flags to set
 */
int VMMDev::i_guestPropSetGlobalPropertyFlags(uint32_t fFlags)
{
    VBOXHGCMSVCPARM parm;
    HGCMSvcSetU32(&parm, fFlags);
    int vrc = hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_SET_GLOBAL_FLAGS, 1, &parm);
    if (RT_FAILURE(vrc))
    {
        char szFlags[GUEST_PROP_MAX_FLAGS_LEN];
        if (RT_FAILURE(GuestPropWriteFlags(fFlags, szFlags)))
            Log(("Failed to set the global flags.\n"));
        else
            Log(("Failed to set the global flags \"%s\".\n", szFlags));
    }
    return vrc;
}


/**
 * Set up the Guest Property service, populate it with properties read from
 * the machine XML and set a couple of initial properties.
 */
int VMMDev::i_guestPropLoadAndConfigure()
{
    Assert(mpDrv);
    ComObjPtr<Console> ptrConsole = this->mParent;
    AssertReturn(ptrConsole.isNotNull(), VERR_INVALID_POINTER);

    /*
     * Load the service
     */
    int vrc = hgcmLoadService("VBoxGuestPropSvc", "VBoxGuestPropSvc");
    if (RT_FAILURE(vrc))
    {
        LogRel(("VBoxGuestPropSvc is not available. vrc = %Rrc\n", vrc));
        return VINF_SUCCESS; /* That is not a fatal failure. */
    }

    /*
     * Pull over the properties from the server.
     */
    SafeArray<BSTR>     namesOut;
    SafeArray<BSTR>     valuesOut;
    SafeArray<LONG64>   timestampsOut;
    SafeArray<BSTR>     flagsOut;
    HRESULT hrc = ptrConsole->i_pullGuestProperties(ComSafeArrayAsOutParam(namesOut),
                                                    ComSafeArrayAsOutParam(valuesOut),
                                                    ComSafeArrayAsOutParam(timestampsOut),
                                                    ComSafeArrayAsOutParam(flagsOut));
    AssertLogRelMsgReturn(SUCCEEDED(hrc), ("hrc=%Rhrc\n", hrc), VERR_MAIN_CONFIG_CONSTRUCTOR_COM_ERROR);
    size_t const cProps = namesOut.size();
    size_t const cAlloc = cProps + 1;
    AssertLogRelReturn(valuesOut.size()     == cProps, VERR_INTERNAL_ERROR_2);
    AssertLogRelReturn(timestampsOut.size() == cProps, VERR_INTERNAL_ERROR_3);
    AssertLogRelReturn(flagsOut.size()      == cProps, VERR_INTERNAL_ERROR_4);

    char    szEmpty[] = "";
    char  **papszNames      = (char  **)RTMemTmpAllocZ(sizeof(char *) * cAlloc);
    char  **papszValues     = (char  **)RTMemTmpAllocZ(sizeof(char *) * cAlloc);
    LONG64 *pai64Timestamps = (LONG64 *)RTMemTmpAllocZ(sizeof(LONG64) * cAlloc);
    char  **papszFlags      = (char  **)RTMemTmpAllocZ(sizeof(char *) * cAlloc);
    if (papszNames && papszValues && pai64Timestamps && papszFlags)
    {
        for (unsigned i = 0; RT_SUCCESS(vrc) && i < cProps; ++i)
        {
            AssertPtrBreakStmt(namesOut[i], vrc = VERR_INVALID_PARAMETER);
            vrc = RTUtf16ToUtf8(namesOut[i], &papszNames[i]);
            if (RT_FAILURE(vrc))
                break;
            if (valuesOut[i])
                vrc = RTUtf16ToUtf8(valuesOut[i], &papszValues[i]);
            else
                papszValues[i] = szEmpty;
            if (RT_FAILURE(vrc))
                break;
            pai64Timestamps[i] = timestampsOut[i];
            if (flagsOut[i])
                vrc = RTUtf16ToUtf8(flagsOut[i], &papszFlags[i]);
            else
                papszFlags[i] = szEmpty;
        }
        if (RT_SUCCESS(vrc))
            i_guestPropSetMultiple((void *)papszNames, (void *)papszValues, (void *)pai64Timestamps, (void *)papszFlags);
        for (unsigned i = 0; i < cProps; ++i)
        {
            RTStrFree(papszNames[i]);
            if (valuesOut[i])
                RTStrFree(papszValues[i]);
            if (flagsOut[i])
                RTStrFree(papszFlags[i]);
        }
    }
    else
        vrc = VERR_NO_MEMORY;
    RTMemTmpFree(papszNames);
    RTMemTmpFree(papszValues);
    RTMemTmpFree(pai64Timestamps);
    RTMemTmpFree(papszFlags);
    AssertRCReturn(vrc, vrc);

    /*
     * Register the host notification callback
     */
    HGCMHostRegisterServiceExtension(&mpDrv->hHgcmSvcExtGstProps, "VBoxGuestPropSvc", Console::i_doGuestPropNotification, ptrConsole.m_p);

# ifdef VBOX_WITH_GUEST_PROPS_RDONLY_GUEST
    vrc = i_guestPropSetGlobalPropertyFlags(GUEST_PROP_F_RDONLYGUEST);
    AssertRCReturn(vrc, vrc);
# endif

    Log(("Set VBoxGuestPropSvc property store\n"));
    return VINF_SUCCESS;
}

#endif /* VBOX_WITH_GUEST_PROPS */

/**
 * @interface_method_impl{PDMDRVREG,pfnConstruct}
 */
DECLCALLBACK(int) VMMDev::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    RT_NOREF(fFlags, pCfg);
    PDRVMAINVMMDEV pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINVMMDEV);
    LogFlow(("Keyboard::drvConstruct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "", "");
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * IBase.
     */
    pDrvIns->IBase.pfnQueryInterface                  = VMMDev::drvQueryInterface;

    pThis->Connector.pfnUpdateGuestStatus             = vmmdevUpdateGuestStatus;
    pThis->Connector.pfnUpdateGuestUserState          = vmmdevUpdateGuestUserState;
    pThis->Connector.pfnUpdateGuestInfo               = vmmdevUpdateGuestInfo;
    pThis->Connector.pfnUpdateGuestInfo2              = vmmdevUpdateGuestInfo2;
    pThis->Connector.pfnUpdateGuestCapabilities       = vmmdevUpdateGuestCapabilities;
    pThis->Connector.pfnUpdateMouseCapabilities       = vmmdevUpdateMouseCapabilities;
    pThis->Connector.pfnUpdatePointerShape            = vmmdevUpdatePointerShape;
    pThis->Connector.pfnVideoAccelEnable              = iface_VideoAccelEnable;
    pThis->Connector.pfnVideoAccelFlush               = iface_VideoAccelFlush;
    pThis->Connector.pfnVideoModeSupported            = vmmdevVideoModeSupported;
    pThis->Connector.pfnGetHeightReduction            = vmmdevGetHeightReduction;
    pThis->Connector.pfnSetCredentialsJudgementResult = vmmdevSetCredentialsJudgementResult;
    pThis->Connector.pfnSetVisibleRegion              = vmmdevSetVisibleRegion;
    pThis->Connector.pfnUpdateMonitorPositions        = vmmdevUpdateMonitorPositions;
    pThis->Connector.pfnQueryVisibleRegion            = vmmdevQueryVisibleRegion;
    pThis->Connector.pfnReportStatistics              = vmmdevReportStatistics;
    pThis->Connector.pfnQueryStatisticsInterval       = vmmdevQueryStatisticsInterval;
    pThis->Connector.pfnQueryBalloonSize              = vmmdevQueryBalloonSize;
    pThis->Connector.pfnIsPageFusionEnabled           = vmmdevIsPageFusionEnabled;

#ifdef VBOX_WITH_HGCM
    pThis->HGCMConnector.pfnConnect                   = iface_hgcmConnect;
    pThis->HGCMConnector.pfnDisconnect                = iface_hgcmDisconnect;
    pThis->HGCMConnector.pfnCall                      = iface_hgcmCall;
    pThis->HGCMConnector.pfnCancelled                 = iface_hgcmCancelled;
#endif

    /*
     * Get the IVMMDevPort interface of the above driver/device.
     */
    pThis->pUpPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIVMMDEVPORT);
    AssertMsgReturn(pThis->pUpPort, ("Configuration error: No VMMDev port interface above!\n"), VERR_PDM_MISSING_INTERFACE_ABOVE);

#ifdef VBOX_WITH_HGCM
    pThis->pHGCMPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIHGCMPORT);
    AssertMsgReturn(pThis->pHGCMPort, ("Configuration error: No HGCM port interface above!\n"), VERR_PDM_MISSING_INTERFACE_ABOVE);
#endif

    /*
     * Get the Console object pointer and update the mpDrv member.
     */
    com::Guid uuid(VMMDEV_OID);
    pThis->pVMMDev = (VMMDev *)PDMDrvHlpQueryGenericUserObject(pDrvIns, uuid.raw());
    if (!pThis->pVMMDev)
    {
        AssertMsgFailed(("Configuration error: No/bad VMMDev object!\n"));
        return VERR_NOT_FOUND;
    }
    pThis->pVMMDev->mpDrv = pThis;

    int vrc = VINF_SUCCESS;
#ifdef VBOX_WITH_HGCM
    /*
     * Load & configure the shared folders service.
     */
    vrc = pThis->pVMMDev->hgcmLoadService(VBOXSHAREDFOLDERS_DLL, "VBoxSharedFolders");
    pThis->pVMMDev->fSharedFolderActive = RT_SUCCESS(vrc);
    if (RT_SUCCESS(vrc))
    {
        PPDMLED       pLed;
        PPDMILEDPORTS pLedPort;

        LogRel(("Shared Folders service loaded\n"));
        pLedPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMILEDPORTS);
        AssertMsgReturn(pLedPort, ("Configuration error: No LED port interface above!\n"), VERR_PDM_MISSING_INTERFACE_ABOVE);
        vrc = pLedPort->pfnQueryStatusLed(pLedPort, 0, &pLed);
        if (RT_SUCCESS(vrc) && pLed)
        {
            VBOXHGCMSVCPARM  parm;

            parm.type = VBOX_HGCM_SVC_PARM_PTR;
            parm.u.pointer.addr = pLed;
            parm.u.pointer.size = sizeof(*pLed);

            vrc = HGCMHostCall("VBoxSharedFolders", SHFL_FN_SET_STATUS_LED, 1, &parm);
        }
        else
            AssertMsgFailed(("pfnQueryStatusLed failed with %Rrc (pLed=%x)\n", vrc, pLed));
    }
    else
        LogRel(("Failed to load Shared Folders service %Rrc\n", vrc));


    /*
     * Load and configure the guest control service.
     */
# ifdef VBOX_WITH_GUEST_CONTROL
    vrc = pThis->pVMMDev->hgcmLoadService("VBoxGuestControlSvc", "VBoxGuestControlSvc");
    if (RT_SUCCESS(vrc))
    {
        vrc = HGCMHostRegisterServiceExtension(&pThis->hHgcmSvcExtGstCtrl, "VBoxGuestControlSvc",
                                               &Guest::i_notifyCtrlDispatcher,
                                               pThis->pVMMDev->mParent->i_getGuest());
        if (RT_SUCCESS(vrc))
            LogRel(("Guest Control service loaded\n"));
        else
            LogRel(("Warning: Cannot register VBoxGuestControlSvc extension! vrc=%Rrc\n", vrc));
    }
    else
        LogRel(("Warning!: Failed to load the Guest Control Service! %Rrc\n", vrc));
# endif /* VBOX_WITH_GUEST_CONTROL */


    /*
     * Load and configure the guest properties service.
     */
# ifdef VBOX_WITH_GUEST_PROPS
    vrc = pThis->pVMMDev->i_guestPropLoadAndConfigure();
    AssertLogRelRCReturn(vrc, vrc);
# endif


    /*
     * The HGCM saved state.
     */
    vrc = PDMDrvHlpSSMRegisterEx(pDrvIns, HGCM_SAVED_STATE_VERSION, 4096 /* bad guess */,
                                 NULL, NULL, NULL,
                                 NULL, VMMDev::hgcmSave, NULL,
                                 NULL, VMMDev::hgcmLoad, NULL);
    if (RT_FAILURE(vrc))
        return vrc;

#endif /* VBOX_WITH_HGCM */

    return VINF_SUCCESS;
}


/**
 * VMMDevice driver registration record.
 */
const PDMDRVREG VMMDev::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "HGCM",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main VMMDev driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_VMMDEV,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINVMMDEV),
    /* pfnConstruct */
    VMMDev::drvConstruct,
    /* pfnDestruct */
    VMMDev::drvDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    VMMDev::drvPowerOn,
    /* pfnReset */
    VMMDev::drvReset,
    /* pfnSuspend */
    VMMDev::drvSuspend,
    /* pfnResume */
    VMMDev::drvResume,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    VMMDev::drvPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
