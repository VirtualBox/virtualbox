/* $Id: DisplayImpl.h 110427 2025-07-28 10:43:42Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_DisplayImpl_h
#define MAIN_INCLUDED_DisplayImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "SchemaDefs.h"

#include <iprt/semaphore.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBoxVideo.h>
#include <VBox/vmm/pdmifs.h>
#include <VBox/VMMDev.h>  /* For struct VMMDevDisplayDef - why is it in that file? */
#include "DisplayWrap.h"

#include "DisplaySourceBitmapWrap.h"
#include "GuestScreenInfoWrap.h"

#ifdef VBOX_WITH_RECORDING
# include "RecordingContext.h"
#endif

class Console;

typedef struct _DISPLAYFBINFO
{
    /* The following 3 fields (u32Offset, u32MaxFramebufferSize and u32InformationSize)
     * are not used by the current HGSMI. They are needed for backward compatibility with
     * pre-HGSMI additions.
     */
    uint32_t u32Offset;
    uint32_t u32MaxFramebufferSize;
    uint32_t u32InformationSize;

    ComPtr<IFramebuffer> pFramebuffer;
    com::Guid framebufferId;
    ComPtr<IDisplaySourceBitmap> pSourceBitmap;
    bool fDisabled;

    uint32_t u32Caps;

    struct
    {
        ComPtr<IDisplaySourceBitmap> pSourceBitmap;
        uint8_t *pu8Address;
        uint32_t cbLine;
    } updateImage;

    LONG xOrigin;
    LONG yOrigin;

    ULONG w;
    ULONG h;

    uint16_t u16BitsPerPixel;
    uint8_t *pu8FramebufferVRAM;
    uint32_t u32LineSize;

    uint16_t flags;

    VBOXVIDEOINFOHOSTEVENTS *pHostEvents;

    /** The framebuffer has default format and must be updates immediately. */
    bool fDefaultFormat;

#ifdef VBOX_WITH_HGSMI
    bool fVBVAEnabled;
    bool fVBVAForceResize;
    VBVAHOSTFLAGS RT_UNTRUSTED_VOLATILE_GUEST *pVBVAHostFlags;
#endif /* VBOX_WITH_HGSMI */

    /** Description of the currently plugged monitor with preferred mode,
     * a.k.a the last mode hint sent. */
    struct VMMDevDisplayDef monitorDesc;
} DISPLAYFBINFO;

/* The legacy VBVA (VideoAccel) data.
 *
 * Backward compatibility with the Guest Additions 3.x or older.
 */
typedef struct VIDEOACCEL
{
    VBVAMEMORY *pVbvaMemory;
    bool        fVideoAccelEnabled;

    uint8_t    *pu8VbvaPartial;
    uint32_t    cbVbvaPartial;

    /* Old Guest Additions (3.x and older) use both VMMDev and DevVGA refresh timer
     * to process the VBVABUFFER memory. Therefore the legacy VBVA (VideoAccel) host
     * code can be executed concurrently by VGA refresh timer and the guest VMMDev
     * request in SMP VMs. The semaphore serialized this.
     */
    RTSEMXROADS hXRoadsVideoAccel;

} VIDEOACCEL;

class DisplayMouseInterface
{
public:
    virtual ~DisplayMouseInterface() { }
    virtual HRESULT i_getScreenResolution(ULONG cScreen, ULONG *pcx,
                                          ULONG *pcy, ULONG *pcBPP, LONG *pXOrigin, LONG *pYOrigin) = 0;
    virtual void i_getFramebufferDimensions(int32_t *px1, int32_t *py1,
                                            int32_t *px2, int32_t *py2) = 0;
    virtual HRESULT i_reportHostCursorCapabilities(uint32_t fCapabilitiesAdded, uint32_t fCapabilitiesRemoved) = 0;
    virtual HRESULT i_reportHostCursorPosition(int32_t x, int32_t y, bool fOutOfRange) = 0;
    virtual bool i_isInputMappingSet(void) = 0;
};

class VMMDev;

class ATL_NO_VTABLE Display :
    public DisplayWrap,
    public DisplayMouseInterface
{
public:

    DECLARE_COMMON_CLASS_METHODS(Display)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(Console *aParent);
    void uninit();
    int  i_registerSSM(PUVM pUVM);

    // public methods only for internal purposes
    unsigned i_getMonitorCount() { return mcMonitors; }
    int i_handleDisplayResize(unsigned uScreenId, uint32_t bpp, void *pvVRAM,
                              uint32_t cbLine, uint32_t w, uint32_t h, uint16_t flags,
                              int32_t xOrigin, int32_t yOrigin, bool fVGAResize);
    void i_handleDisplayUpdate(unsigned uScreenId, int x, int y, int w, int h);
    void i_handleUpdateVMMDevSupportsGraphics(bool fSupportsGraphics);
    void i_handleUpdateGuestVBVACapabilities(uint32_t fNewCapabilities);
    void i_handleUpdateVBVAInputMapping(int32_t xOrigin, int32_t yOrigin, uint32_t cx, uint32_t cy);
    int  i_handle3DNotifyProcess(VBOX3DNOTIFY *p3DNotify);

    int  i_saveVisibleRegion(uint32_t cRect, PRTRECT pRect);
    int  i_handleSetVisibleRegion(uint32_t cRect, PRTRECT pRect);
    int  i_handleUpdateMonitorPositions(uint32_t cPositions, PCRTPOINT paPositions);
    int  i_handleQueryVisibleRegion(uint32_t *pcRects, PRTRECT paRects);

    void i_VRDPConnectionEvent(bool fConnect);
    void i_VideoAccelVRDP(bool fEnable, int c);

    /* Legacy video acceleration requests coming from the VGA refresh timer. */
    int  VideoAccelEnableVGA(bool fEnable, VBVAMEMORY *pVbvaMemory);

    /* Legacy video acceleration requests coming from VMMDev. */
    int  VideoAccelEnableVMMDev(bool fEnable, VBVAMEMORY *pVbvaMemory);
    void VideoAccelFlushVMMDev(void);

    void i_updateDeviceCursorCapabilities(void);

#ifdef VBOX_WITH_RECORDING
    int i_recordingStart(void);
    int i_recordingStop(void);
    int i_recordingInvalidate(void);
    int i_recordingScreenChanged(unsigned uScreenId, const DISPLAYFBINFO *pFBInfo);
    int i_recordingScreenUpdate(unsigned uScreenId, uint8_t *pauFramebuffer, size_t cbFramebuffer, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t uBytesPerLine);
    int i_recordingScreenUpdate(unsigned uScreenId, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
    int i_recordingCursorPositionChange(unsigned uScreenId, uint32_t fFlags, int32_t x, int32_t y);
#endif

    void i_notifyPowerDown(void);

    // DisplayMouseInterface methods
    virtual HRESULT i_getScreenResolution(ULONG cScreen, ULONG *pcx,
                                          ULONG *pcy, ULONG *pcBPP, LONG *pXOrigin, LONG *pYOrigin)
    {
        return getScreenResolution(cScreen, pcx, pcy, pcBPP, pXOrigin, pYOrigin, NULL);
    }
    virtual void i_getFramebufferDimensions(int32_t *px1, int32_t *py1,
                                            int32_t *px2, int32_t *py2);
    virtual HRESULT i_reportHostCursorCapabilities(uint32_t fCapabilitiesAdded, uint32_t fCapabilitiesRemoved);
    virtual HRESULT i_reportHostCursorPosition(int32_t x, int32_t y, bool fOutOfRange);
    virtual bool i_isInputMappingSet(void)
    {
        return cxInputMapping != 0 && cyInputMapping != 0;
    }

    static const PDMDRVREG  DrvReg;

private:
    // Wrapped IDisplay properties
    virtual HRESULT getGuestScreenLayout(std::vector<ComPtr<IGuestScreenInfo> > &aGuestScreenLayout);

    // Wrapped IDisplay methods
    virtual HRESULT getScreenResolution(ULONG aScreenId,
                                        ULONG *aWidth,
                                        ULONG *aHeight,
                                        ULONG *aBitsPerPixel,
                                        LONG *aXOrigin,
                                        LONG *aYOrigin,
                                        GuestMonitorStatus_T *aGuestMonitorStatus);
    virtual HRESULT attachFramebuffer(ULONG aScreenId,
                                      const ComPtr<IFramebuffer> &aFramebuffer,
                                      com::Guid &aId);
    virtual HRESULT detachFramebuffer(ULONG aScreenId,
                                      const com::Guid &aId);
    virtual HRESULT queryFramebuffer(ULONG aScreenId,
                                     ComPtr<IFramebuffer> &aFramebuffer);
    virtual HRESULT setVideoModeHint(ULONG aDisplay,
                                     BOOL aEnabled,
                                     BOOL aChangeOrigin,
                                     LONG aOriginX,
                                     LONG aOriginY,
                                     ULONG aWidth,
                                     ULONG aHeight,
                                     ULONG aBitsPerPixel,
                                     BOOL aNotify);
    virtual HRESULT getVideoModeHint(ULONG aDisplay,
                                     BOOL *aEnabled,
                                     BOOL *aChangeOrigin,
                                     LONG *aOriginX,
                                     LONG *aOriginY,
                                     ULONG *aWidth,
                                     ULONG *aHeight,
                                     ULONG *aBitsPerPixel);
    virtual HRESULT setSeamlessMode(BOOL aEnabled);
    virtual HRESULT takeScreenShot(ULONG aScreenId,
                                   BYTE *aAddress,
                                   ULONG aWidth,
                                   ULONG aHeight,
                                   BitmapFormat_T aBitmapFormat);
    virtual HRESULT takeScreenShotToArray(ULONG aScreenId,
                                          ULONG aWidth,
                                          ULONG aHeight,
                                          BitmapFormat_T aBitmapFormat,
                                          std::vector<BYTE> &aScreenData);
    virtual HRESULT drawToScreen(ULONG aScreenId,
                                 BYTE *aAddress,
                                 ULONG aX,
                                 ULONG aY,
                                 ULONG aWidth,
                                 ULONG aHeight);
    virtual HRESULT invalidateAndUpdate();
    virtual HRESULT invalidateAndUpdateScreen(ULONG aScreenId);
    virtual HRESULT viewportChanged(ULONG aScreenId,
                                    ULONG aX,
                                    ULONG aY,
                                    ULONG aWidth,
                                    ULONG aHeight);
    virtual HRESULT querySourceBitmap(ULONG aScreenId,
                                      ComPtr<IDisplaySourceBitmap> &aDisplaySourceBitmap);
    virtual HRESULT notifyScaleFactorChange(ULONG aScreenId,
                                            ULONG aScaleFactorWMultiplied,
                                            ULONG aScaleFactorHMultiplied);
    virtual HRESULT notifyHiDPIOutputPolicyChange(BOOL fUnscaledHiDPI);
    virtual HRESULT setScreenLayout(ScreenLayoutMode_T aScreenLayoutMode,
                                    const std::vector<ComPtr<IGuestScreenInfo> > &aGuestScreenInfo);
    virtual HRESULT detachScreens(const std::vector<LONG> &aScreenIds);
    virtual HRESULT createGuestScreenInfo(ULONG aDisplay,
                                          GuestMonitorStatus_T aStatus,
                                          BOOL aPrimary,
                                          BOOL aChangeOrigin,
                                          LONG aOriginX,
                                          LONG aOriginY,
                                          ULONG aWidth,
                                          ULONG aHeight,
                                          ULONG aBitsPerPixel,
                                          ComPtr<IGuestScreenInfo> &aGuestScreenInfo);

    // Wrapped IEventListener properties

    // Wrapped IEventListener methods
    virtual HRESULT handleEvent(const ComPtr<IEvent> &aEvent);

    // other internal methods
    HRESULT takeScreenShotWorker(ULONG aScreenId,
                                 BYTE *aAddress,
                                 ULONG aWidth,
                                 ULONG aHeight,
                                 BitmapFormat_T aBitmapFormat,
                                 ULONG *pcbOut);
    int processVBVAResize(PCVBVAINFOVIEW pView, PCVBVAINFOSCREEN pScreen, void *pvVRAM, bool fResetInputMapping);

    static DECLCALLBACK(void*) i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(int)   i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)  i_drvDestruct(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(void)  i_drvPowerOff(PPDMDRVINS pDrvIns);
    static DECLCALLBACK(int)   i_displayResizeCallback(PPDMIDISPLAYCONNECTOR pInterface, uint32_t bpp, void *pvVRAM,
                                                       uint32_t cbLine, uint32_t cx, uint32_t cy);
    static DECLCALLBACK(void)  i_displayUpdateCallback(PPDMIDISPLAYCONNECTOR pInterface,
                                                       uint32_t x, uint32_t y, uint32_t cx, uint32_t cy);
    static DECLCALLBACK(void)  i_displayRefreshCallback(PPDMIDISPLAYCONNECTOR pInterface);
    static DECLCALLBACK(void)  i_displayResetCallback(PPDMIDISPLAYCONNECTOR pInterface);
    static DECLCALLBACK(void)  i_displayLFBModeChangeCallback(PPDMIDISPLAYCONNECTOR pInterface, bool fEnabled);
    static DECLCALLBACK(void)  i_displayProcessAdapterDataCallback(PPDMIDISPLAYCONNECTOR pInterface,
                                                                   void *pvVRAM, uint32_t u32VRAMSize);
    static DECLCALLBACK(void)  i_displayProcessDisplayDataCallback(PPDMIDISPLAYCONNECTOR pInterface,
                                                                   void *pvVRAM, unsigned uScreenId);

    static DECLCALLBACK(int)  i_display3DNotifyProcess(PPDMIDISPLAYCONNECTOR pInterface,
                                                       VBOX3DNOTIFY *p3DNotify);

#ifdef VBOX_WITH_HGSMI
    static DECLCALLBACK(int)   i_displayVBVAEnable(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId,
                                                   VBVAHOSTFLAGS RT_UNTRUSTED_VOLATILE_GUEST *pHostFlags);
    static DECLCALLBACK(void)  i_displayVBVADisable(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId);
    static DECLCALLBACK(void)  i_displayVBVAUpdateBegin(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId);
    static DECLCALLBACK(void)  i_displayVBVAUpdateProcess(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId,
                                                          struct VBVACMDHDR const RT_UNTRUSTED_VOLATILE_GUEST *pCmd, size_t cbCmd);
    static DECLCALLBACK(void)  i_displayVBVAUpdateEnd(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId, int32_t x, int32_t y,
                                                      uint32_t cx, uint32_t cy);
    static DECLCALLBACK(int)   i_displayVBVAResize(PPDMIDISPLAYCONNECTOR pInterface, PCVBVAINFOVIEW pView,
                                                   PCVBVAINFOSCREEN pScreen, void *pvVRAM,
                                                   bool fResetInputMapping);
    static DECLCALLBACK(int)   i_displayVBVAMousePointerShape(PPDMIDISPLAYCONNECTOR pInterface, bool fVisible, bool fAlpha,
                                                              uint32_t xHot, uint32_t yHot, uint32_t cx, uint32_t cy,
                                                              const void *pvShape);
    static DECLCALLBACK(void)  i_displayVBVAGuestCapabilityUpdate(PPDMIDISPLAYCONNECTOR pInterface, uint32_t fCapabilities);

    static DECLCALLBACK(void)  i_displayVBVAInputMappingUpdate(PPDMIDISPLAYCONNECTOR pInterface, int32_t xOrigin, int32_t yOrigin,
                                                               uint32_t cx, uint32_t cy);
    static DECLCALLBACK(void)  i_displayVBVAReportCursorPosition(PPDMIDISPLAYCONNECTOR pInterface, uint32_t fFlags, uint32_t uScreen, uint32_t x, uint32_t y);
#endif

    static DECLCALLBACK(int)  i_displaySSMSaveScreenshot(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser);
    static DECLCALLBACK(int)  i_displaySSMLoadScreenshot(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser,
                                                         uint32_t uVersion, uint32_t uPass);
    static DECLCALLBACK(int)  i_displaySSMSave(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser);
    static DECLCALLBACK(int)  i_displaySSMLoad(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser,
                                               uint32_t uVersion, uint32_t uPass);

    Console * const         mParent;
    /** Pointer to the associated display driver. */
    struct DRVMAINDISPLAY   *mpDrv;

    unsigned mcMonitors;
    /** Input mapping rectangle top left X relative to the first screen. */
    int32_t     xInputMappingOrigin;
    /** Input mapping rectangle top left Y relative to the first screen. */
    int32_t     yInputMappingOrigin;
    uint32_t    cxInputMapping;  /**< Input mapping rectangle width. */
    uint32_t    cyInputMapping;  /**< Input mapping rectangle height. */
    DISPLAYFBINFO maFramebuffers[SchemaDefs::MaxGuestMonitors];
    /** Does the VMM device have the "supports graphics" capability set?
     *  Does not go into the saved state as it is refreshed on restore. */
    bool        mfVMMDevSupportsGraphics;
    /** Mirror of the current guest VBVA capabilities. */
    uint32_t    mfGuestVBVACapabilities;
    /** Mirror of the current host cursor capabilities. */
    uint32_t    mfHostCursorCapabilities;

    bool mfSourceBitmapEnabled;
    bool volatile fVGAResizing;

    /** Are we in seamless mode?  Not saved, as we exit seamless on saving. */
    bool        mfSeamlessEnabled;
    /** Last set seamless visible region, number of rectangles. */
    uint32_t    mcRectVisibleRegion;
    /** Last set seamless visible region, data.  Freed on final clean-up. */
    PRTRECT     mpRectVisibleRegion;

    bool        mfVideoAccelVRDP;
    uint32_t    mfu32SupportedOrders;
    /** Number of currently connected VRDP clients. */
    int32_t volatile mcVRDPRefs;

    /* The legacy VBVA data and methods. */
    VIDEOACCEL mVideoAccelLegacy;

    int  i_VideoAccelEnable(bool fEnable, VBVAMEMORY *pVbvaMemory, PPDMIDISPLAYPORT pUpPort);
    void i_VideoAccelFlush(PPDMIDISPLAYPORT pUpPort);
    bool i_VideoAccelAllowed(void);

    int  i_videoAccelRefreshProcess(PPDMIDISPLAYPORT pUpPort);
    int  i_videoAccelEnable(bool fEnable, VBVAMEMORY *pVbvaMemory, PPDMIDISPLAYPORT pUpPort);
    int  i_videoAccelFlush(PPDMIDISPLAYPORT pUpPort);

    /* Legacy pre-HGSMI handlers. */
    void processAdapterData(void *pvVRAM, uint32_t u32VRAMSize);
    void processDisplayData(void *pvVRAM, unsigned uScreenId);

    /** Serializes access to mVideoAccelLegacy and mfVideoAccelVRDP, etc between VRDP and Display. */
    RTCRITSECT           mVideoAccelLock;

#ifdef VBOX_WITH_RECORDING
    /** Struct which holds information and state about (video) recording. */
    struct Recording
    {
        Recording()
            : pCtx(NULL) { }

        /** Recording context. Constant across lifetime.
         *  Might be NULL if not being used. */
        RecordingContext * const pCtx;
    } Recording;
#endif

#ifdef VBOX_WITH_STATISTICS
    /** Struct for keeping STAM values. */
    struct
    {
        /** Profiling Display::i_displayRefreshCallback(). */
        STAMPROFILE profileDisplayRefreshCallback;
        /** Statistics for monitor N. */
        struct
        {
            /** Statistics for recording of monitor N. */
            struct
            {
                /** Profiling recording code of monitor N. */
                STAMPROFILE profileRecording;
            } Recording;
        } Monitor[SchemaDefs::MaxGuestMonitors];
        /** Statistics for audio/video recording. */
        struct
        {
            /** Profiling recording code of all active monitors. */
            STAMPROFILE profileRecording;
        } Recording;
    } Stats;
#endif

public:

    static DECLCALLBACK(int) i_displayTakeScreenshotEMT(Display *pDisplay, ULONG aScreenId, uint8_t **ppbData, size_t *pcbData,
                                                        uint32_t *pcx, uint32_t *pcy, bool *pfMemFree);

private:
    static DECLCALLBACK(int) i_InvalidateAndUpdateEMT(Display *pDisplay, unsigned uId, bool fUpdateAll);
    static DECLCALLBACK(int) i_drawToScreenEMT(Display *pDisplay, ULONG aScreenId, BYTE *address, ULONG x, ULONG y,
                                               ULONG width, ULONG height);

    void i_updateGuestGraphicsFacility(void);

#ifdef VBOX_WITH_HGSMI
    volatile uint32_t mu32UpdateVBVAFlags;
#endif

private:
    DECLARE_CLS_COPY_CTOR_ASSIGN_NOOP(Display); /* Shuts up MSC warning C4625. */
};

/* The legacy VBVA helpers. */
int videoAccelConstruct(VIDEOACCEL *pVideoAccel);
void videoAccelDestroy(VIDEOACCEL *pVideoAccel);
void i_vbvaSetMemoryFlags(VBVAMEMORY *pVbvaMemory,
                          bool fVideoAccelEnabled,
                          bool fVideoAccelVRDP,
                          uint32_t fu32SupportedOrders,
                          DISPLAYFBINFO *paFBInfos,
                          unsigned cFBInfos);
int videoAccelEnterVGA(VIDEOACCEL *pVideoAccel);
void videoAccelLeaveVGA(VIDEOACCEL *pVideoAccel);
int videoAccelEnterVMMDev(VIDEOACCEL *pVideoAccel);
void videoAccelLeaveVMMDev(VIDEOACCEL *pVideoAccel);


/* helper function, code in DisplayResampleImage.cpp */
void BitmapScale32(uint8_t *dst, int dstW, int dstH,
                   const uint8_t *src, int iDeltaLine, int srcW, int srcH);

/* helper function, code in DisplayPNGUtul.cpp */
int DisplayMakePNG(uint8_t *pbData, uint32_t cx, uint32_t cy,
                   uint8_t **ppu8PNG, uint32_t *pcbPNG, uint32_t *pcxPNG, uint32_t *pcyPNG,
                     uint8_t fLimitSize);

class ATL_NO_VTABLE DisplaySourceBitmap:
    public DisplaySourceBitmapWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(DisplaySourceBitmap)

    HRESULT FinalConstruct();
    void FinalRelease();

    /* Public initializer/uninitializer for internal purposes only. */
    HRESULT init(ComObjPtr<Display> pDisplay, unsigned uScreenId, DISPLAYFBINFO *pFBInfo);
    void uninit();

    bool i_usesVRAM(void) { return m.pu8Allocated == NULL; }

private:
    // wrapped IDisplaySourceBitmap properties
    virtual HRESULT getScreenId(ULONG *aScreenId);

    // wrapped IDisplaySourceBitmap methods
    virtual HRESULT queryBitmapInfo(BYTE **aAddress,
                                    ULONG *aWidth,
                                    ULONG *aHeight,
                                    ULONG *aBitsPerPixel,
                                    ULONG *aBytesPerLine,
                                    BitmapFormat_T *aBitmapFormat);

    int initSourceBitmap(unsigned aScreenId, DISPLAYFBINFO *pFBInfo);

    struct Data
    {
        ComObjPtr<Display> pDisplay;
        unsigned uScreenId;
        DISPLAYFBINFO *pFBInfo;

        uint8_t *pu8Allocated;

        uint8_t *pu8Address;
        ULONG ulWidth;
        ULONG ulHeight;
        ULONG ulBitsPerPixel;
        ULONG ulBytesPerLine;
        BitmapFormat_T bitmapFormat;
    };

    Data m;
};

class ATL_NO_VTABLE GuestScreenInfo:
    public GuestScreenInfoWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(GuestScreenInfo)

    HRESULT FinalConstruct();
    void FinalRelease();

    /* Public initializer/uninitializer for internal purposes only. */
    HRESULT init(ULONG aDisplay,
                 GuestMonitorStatus_T aGuestMonitorStatus,
                 BOOL aPrimary,
                 BOOL aChangeOrigin,
                 LONG aOriginX,
                 LONG aOriginY,
                 ULONG aWidth,
                 ULONG aHeight,
                 ULONG aBitsPerPixel);
    void uninit();

private:
    // wrapped IGuestScreenInfo properties
    virtual HRESULT getScreenId(ULONG *aScreenId);
    virtual HRESULT getGuestMonitorStatus(GuestMonitorStatus_T *aGuestMonitorStatus);
    virtual HRESULT getPrimary(BOOL *aPrimary);
    virtual HRESULT getOrigin(BOOL *aOrigin);
    virtual HRESULT getOriginX(LONG *aOriginX);
    virtual HRESULT getOriginY(LONG *aOriginY);
    virtual HRESULT getWidth(ULONG *aWidth);
    virtual HRESULT getHeight(ULONG *aHeight);
    virtual HRESULT getBitsPerPixel(ULONG *aBitsPerPixel);
    virtual HRESULT getExtendedInfo(com::Utf8Str &aExtendedInfo);

    ULONG mScreenId;
    GuestMonitorStatus_T mGuestMonitorStatus;
    BOOL  mPrimary;
    BOOL  mOrigin;
    LONG  mOriginX;
    LONG  mOriginY;
    ULONG mWidth;
    ULONG mHeight;
    ULONG mBitsPerPixel;
};

#endif /* !MAIN_INCLUDED_DisplayImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
