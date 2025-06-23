/* $Id: DisplayImpl.cpp 109938 2025-06-23 19:03:50Z andreas.loeffler@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_MAIN_DISPLAY
#include "LoggingNew.h"

#include "DisplayImpl.h"
#include "DisplayUtils.h"
#include "ConsoleImpl.h"
#include "ConsoleVRDPServer.h"
#include "GuestImpl.h"
#include "MouseImpl.h"
#include "VMMDev.h"

#include "AutoCaller.h"

/* generated header */
#include "VBoxEvents.h"

#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/asm.h>
#include <iprt/time.h>
#include <iprt/cpp/utils.h>
#include <iprt/alloca.h>

#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/pdmdrv.h>

#ifdef VBOX_WITH_VIDEOHWACCEL
# include <VBoxVideo.h>
#endif
#include <VBoxVideo3D.h>

#include <VBox/com/array.h>

#ifdef VBOX_WITH_RECORDING
# include <iprt/path.h>
# include "Recording.h"
# include "RecordingUtils.h"

# include <VBox/vmm/pdmapi.h>
# include <VBox/vmm/pdmaudioifs.h>
#endif

/**
 * Display driver instance data.
 *
 * @implements PDMIDISPLAYCONNECTOR
 */
typedef struct DRVMAINDISPLAY
{
    /** Pointer to the display object. */
    Display                    *pDisplay;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Pointer to the display port interface of the driver/device above us. */
    PPDMIDISPLAYPORT            pUpPort;
    /** Our display connector interface. */
    PDMIDISPLAYCONNECTOR        IConnector;
#if defined(VBOX_WITH_VIDEOHWACCEL)
    /** VBVA callbacks */
    PPDMIDISPLAYVBVACALLBACKS   pVBVACallbacks;
#endif
} DRVMAINDISPLAY, *PDRVMAINDISPLAY;

/** Converts PDMIDISPLAYCONNECTOR pointer to a DRVMAINDISPLAY pointer. */
#define PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface)  RT_FROM_MEMBER(pInterface, DRVMAINDISPLAY, IConnector)

// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

Display::Display()
    : mParent(NULL)
{
}

Display::~Display()
{
}


HRESULT Display::FinalConstruct()
{
    int vrc = videoAccelConstruct(&mVideoAccelLegacy);
    AssertRC(vrc);

    mfVideoAccelVRDP = false;
    mfu32SupportedOrders = 0;
    mcVRDPRefs = 0;

    mfSeamlessEnabled = false;
    mpRectVisibleRegion = NULL;
    mcRectVisibleRegion = 0;

    mpDrv = NULL;

    vrc = RTCritSectInit(&mVideoAccelLock);
    AssertRC(vrc);

#ifdef VBOX_WITH_HGSMI
    mu32UpdateVBVAFlags = 0;
    mfVMMDevSupportsGraphics = false;
    mfGuestVBVACapabilities = 0;
    mfHostCursorCapabilities = 0;
#endif

    return BaseFinalConstruct();
}

void Display::FinalRelease()
{
    uninit();

    videoAccelDestroy(&mVideoAccelLegacy);
    i_saveVisibleRegion(0, NULL);

    if (RTCritSectIsInitialized(&mVideoAccelLock))
    {
        RTCritSectDelete(&mVideoAccelLock);
        RT_ZERO(mVideoAccelLock);
    }

    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

#define kMaxSizeThumbnail 64

/**
 * Save thumbnail and screenshot of the guest screen.
 */
static int displayMakeThumbnail(uint8_t *pbData, uint32_t cx, uint32_t cy,
                                uint8_t **ppu8Thumbnail, uint32_t *pcbThumbnail, uint32_t *pcxThumbnail, uint32_t *pcyThumbnail)
{
    int vrc = VINF_SUCCESS;

    uint8_t *pu8Thumbnail = NULL;
    uint32_t cbThumbnail = 0;
    uint32_t cxThumbnail = 0;
    uint32_t cyThumbnail = 0;

    if (cx > cy)
    {
        cxThumbnail = kMaxSizeThumbnail;
        cyThumbnail = (kMaxSizeThumbnail * cy) / cx;
    }
    else
    {
        cyThumbnail = kMaxSizeThumbnail;
        cxThumbnail = (kMaxSizeThumbnail * cx) / cy;
    }

    LogRelFlowFunc(("%dx%d -> %dx%d\n", cx, cy, cxThumbnail, cyThumbnail));

    cbThumbnail = cxThumbnail * 4 * cyThumbnail;
    pu8Thumbnail = (uint8_t *)RTMemAlloc(cbThumbnail);

    if (pu8Thumbnail)
    {
        uint8_t *dst = pu8Thumbnail;
        uint8_t *src = pbData;
        int dstW = cxThumbnail;
        int dstH = cyThumbnail;
        int srcW = cx;
        int srcH = cy;
        int iDeltaLine = cx * 4;

        BitmapScale32(dst,
                      dstW, dstH,
                      src,
                      iDeltaLine,
                      srcW, srcH);

        *ppu8Thumbnail = pu8Thumbnail;
        *pcbThumbnail = cbThumbnail;
        *pcxThumbnail = cxThumbnail;
        *pcyThumbnail = cyThumbnail;
    }
    else
    {
        vrc = VERR_NO_MEMORY;
    }

    return vrc;
}

/**
 * @callback_method_impl{FNSSMEXTSAVEEXEC}
 */
DECLCALLBACK(int) Display::i_displaySSMSaveScreenshot(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser)
{
    Display * const pThat = static_cast<Display *>(pvUser);
    AssertPtrReturn(pThat, VERR_INVALID_POINTER);

    /* 32bpp small RGB image. */
    uint8_t *pu8Thumbnail = NULL;
    uint32_t cbThumbnail = 0;
    uint32_t cxThumbnail = 0;
    uint32_t cyThumbnail = 0;

    /* PNG screenshot. */
    uint8_t *pu8PNG = NULL;
    uint32_t cbPNG = 0;
    uint32_t cxPNG = 0;
    uint32_t cyPNG = 0;

    Console::SafeVMPtr ptrVM(pThat->mParent);
    if (ptrVM.isOk())
    {
        /* Query RGB bitmap. */
        /* SSM code is executed on EMT(0), therefore no need to use VMR3ReqCallWait. */
        uint8_t *pbData = NULL;
        size_t cbData = 0;
        uint32_t cx = 0;
        uint32_t cy = 0;
        bool fFreeMem = false;
        int vrc = Display::i_displayTakeScreenshotEMT(pThat, VBOX_VIDEO_PRIMARY_SCREEN, &pbData, &cbData, &cx, &cy, &fFreeMem);

        /*
         * It is possible that success is returned but everything is 0 or NULL.
         * (no display attached if a VM is running with VBoxHeadless on OSE for example)
         */
        if (RT_SUCCESS(vrc) && pbData)
        {
            Assert(cx && cy);

            /* Prepare a small thumbnail and a PNG screenshot. */
            displayMakeThumbnail(pbData, cx, cy, &pu8Thumbnail, &cbThumbnail, &cxThumbnail, &cyThumbnail);
            vrc = DisplayMakePNG(pbData, cx, cy, &pu8PNG, &cbPNG, &cxPNG, &cyPNG, 1);
            if (RT_FAILURE(vrc))
            {
                if (pu8PNG)
                {
                    RTMemFree(pu8PNG);
                    pu8PNG = NULL;
                }
                cbPNG = 0;
                cxPNG = 0;
                cyPNG = 0;
            }

            if (fFreeMem)
                RTMemFree(pbData);
            else
                pThat->mpDrv->pUpPort->pfnFreeScreenshot(pThat->mpDrv->pUpPort, pbData);
        }
    }
    else
    {
        LogFunc(("Failed to get VM pointer 0x%x\n", ptrVM.hrc()));
    }

    /* Regardless of vrc, save what is available:
     * Data format:
     *    uint32_t cBlocks;
     *    [blocks]
     *
     *  Each block is:
     *    uint32_t cbBlock;        if 0 - no 'block data'.
     *    uint32_t typeOfBlock;    0 - 32bpp RGB bitmap, 1 - PNG, ignored if 'cbBlock' is 0.
     *    [block data]
     *
     *  Block data for bitmap and PNG:
     *    uint32_t cx;
     *    uint32_t cy;
     *    [image data]
     */
    pVMM->pfnSSMR3PutU32(pSSM, 2); /* Write thumbnail and PNG screenshot. */

    /* First block. */
    pVMM->pfnSSMR3PutU32(pSSM, (uint32_t)(cbThumbnail + 2 * sizeof(uint32_t)));
    pVMM->pfnSSMR3PutU32(pSSM, 0); /* Block type: thumbnail. */

    if (cbThumbnail)
    {
        pVMM->pfnSSMR3PutU32(pSSM, cxThumbnail);
        pVMM->pfnSSMR3PutU32(pSSM, cyThumbnail);
        pVMM->pfnSSMR3PutMem(pSSM, pu8Thumbnail, cbThumbnail);
    }

    /* Second block. */
    pVMM->pfnSSMR3PutU32(pSSM, (uint32_t)(cbPNG + 2 * sizeof(uint32_t)));
    pVMM->pfnSSMR3PutU32(pSSM, 1); /* Block type: png. */

    if (cbPNG)
    {
        pVMM->pfnSSMR3PutU32(pSSM, cxPNG);
        pVMM->pfnSSMR3PutU32(pSSM, cyPNG);
        pVMM->pfnSSMR3PutMem(pSSM, pu8PNG, cbPNG);
    }

    RTMemFree(pu8PNG);
    RTMemFree(pu8Thumbnail);

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMEXTLOADEXEC}
 */
DECLCALLBACK(int)
Display::i_displaySSMLoadScreenshot(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser, uint32_t uVersion, uint32_t uPass)
{
    Display * const pThat = static_cast<Display *>(pvUser);
    AssertPtrReturn(pThat, VERR_INVALID_POINTER);
    Assert(uPass == SSM_PASS_FINAL); RT_NOREF_PV(uPass);

    if (uVersion != sSSMDisplayScreenshotVer)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* Skip data. */
    uint32_t cBlocks;
    int vrc = pVMM->pfnSSMR3GetU32(pSSM, &cBlocks);
    AssertRCReturn(vrc, vrc);

    for (uint32_t i = 0; i < cBlocks; i++)
    {
        uint32_t cbBlock;
        vrc = pVMM->pfnSSMR3GetU32(pSSM, &cbBlock);
        AssertRCReturn(vrc, vrc);

        uint32_t typeOfBlock;
        vrc = pVMM->pfnSSMR3GetU32(pSSM, &typeOfBlock);
        AssertRCReturn(vrc, vrc);

        LogRelFlowFunc(("[%d] type %d, size %d bytes\n", i, typeOfBlock, cbBlock));

        /* Note: displaySSMSaveScreenshot writes size of a block = 8 and
         * do not write any data if the image size was 0.
         */
        /** @todo Fix and increase saved state version. */
        if (cbBlock > 2 * sizeof(uint32_t))
        {
            vrc = pVMM->pfnSSMR3Skip(pSSM, cbBlock);
            AssertRCReturn(vrc, vrc);
        }
    }

    return vrc;
}

/**
 * @callback_method_impl{FNSSMEXTSAVEEXEC, Save some important guest state}
 */
/*static*/ DECLCALLBACK(int)
Display::i_displaySSMSave(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser)
{
    Display * const pThat = static_cast<Display *>(pvUser);
    AssertPtrReturn(pThat, VERR_INVALID_POINTER);

    pVMM->pfnSSMR3PutU32(pSSM, pThat->mcMonitors);
    for (unsigned i = 0; i < pThat->mcMonitors; i++)
    {
        pVMM->pfnSSMR3PutU32(pSSM, pThat->maFramebuffers[i].u32Offset);
        pVMM->pfnSSMR3PutU32(pSSM, pThat->maFramebuffers[i].u32MaxFramebufferSize);
        pVMM->pfnSSMR3PutU32(pSSM, pThat->maFramebuffers[i].u32InformationSize);
        pVMM->pfnSSMR3PutU32(pSSM, pThat->maFramebuffers[i].w);
        pVMM->pfnSSMR3PutU32(pSSM, pThat->maFramebuffers[i].h);
        pVMM->pfnSSMR3PutS32(pSSM, pThat->maFramebuffers[i].xOrigin);
        pVMM->pfnSSMR3PutS32(pSSM, pThat->maFramebuffers[i].yOrigin);
        pVMM->pfnSSMR3PutU32(pSSM, pThat->maFramebuffers[i].flags);
    }
    pVMM->pfnSSMR3PutS32(pSSM, pThat->xInputMappingOrigin);
    pVMM->pfnSSMR3PutS32(pSSM, pThat->yInputMappingOrigin);
    pVMM->pfnSSMR3PutU32(pSSM, pThat->cxInputMapping);
    pVMM->pfnSSMR3PutU32(pSSM, pThat->cyInputMapping);
    pVMM->pfnSSMR3PutU32(pSSM, pThat->mfGuestVBVACapabilities);
    return pVMM->pfnSSMR3PutU32(pSSM, pThat->mfHostCursorCapabilities);
}

/**
 * @callback_method_impl{FNSSMEXTLOADEXEC, Load some important guest state}
 */
/*static*/ DECLCALLBACK(int)
Display::i_displaySSMLoad(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser, uint32_t uVersion, uint32_t uPass)
{
    Display * const pThat = static_cast<Display *>(pvUser);
    AssertPtrReturn(pThat, VERR_INVALID_POINTER);

    if (   uVersion != sSSMDisplayVer
        && uVersion != sSSMDisplayVer2
        && uVersion != sSSMDisplayVer3
        && uVersion != sSSMDisplayVer4
        && uVersion != sSSMDisplayVer5)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    uint32_t cMonitors;
    int vrc = pVMM->pfnSSMR3GetU32(pSSM, &cMonitors);
    AssertRCReturn(vrc, vrc);
    if (cMonitors != pThat->mcMonitors)
        return pVMM->pfnSSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Number of monitors changed (%d->%d)!"), cMonitors, pThat->mcMonitors);

    for (uint32_t i = 0; i < cMonitors; i++)
    {
        pVMM->pfnSSMR3GetU32(pSSM, &pThat->maFramebuffers[i].u32Offset);
        pVMM->pfnSSMR3GetU32(pSSM, &pThat->maFramebuffers[i].u32MaxFramebufferSize);
        pVMM->pfnSSMR3GetU32(pSSM, &pThat->maFramebuffers[i].u32InformationSize);
        if (   uVersion == sSSMDisplayVer2
            || uVersion == sSSMDisplayVer3
            || uVersion == sSSMDisplayVer4
            || uVersion == sSSMDisplayVer5)
        {
            uint32_t w;
            uint32_t h;
            pVMM->pfnSSMR3GetU32(pSSM, &w);
            vrc = pVMM->pfnSSMR3GetU32(pSSM, &h);
            AssertRCReturn(vrc, vrc);
            pThat->maFramebuffers[i].w = w;
            pThat->maFramebuffers[i].h = h;
        }
        if (   uVersion == sSSMDisplayVer3
            || uVersion == sSSMDisplayVer4
            || uVersion == sSSMDisplayVer5)
        {
            int32_t xOrigin;
            int32_t yOrigin;
            uint32_t flags;
            pVMM->pfnSSMR3GetS32(pSSM, &xOrigin);
            pVMM->pfnSSMR3GetS32(pSSM, &yOrigin);
            vrc = pVMM->pfnSSMR3GetU32(pSSM, &flags);
            AssertRCReturn(vrc, vrc);
            pThat->maFramebuffers[i].xOrigin = xOrigin;
            pThat->maFramebuffers[i].yOrigin = yOrigin;
            pThat->maFramebuffers[i].flags = (uint16_t)flags;
            pThat->maFramebuffers[i].fDisabled = (pThat->maFramebuffers[i].flags & VBVA_SCREEN_F_DISABLED) != 0;
        }
    }
    if (   uVersion == sSSMDisplayVer4
        || uVersion == sSSMDisplayVer5)
    {
        pVMM->pfnSSMR3GetS32(pSSM, &pThat->xInputMappingOrigin);
        pVMM->pfnSSMR3GetS32(pSSM, &pThat->yInputMappingOrigin);
        pVMM->pfnSSMR3GetU32(pSSM, &pThat->cxInputMapping);
        pVMM->pfnSSMR3GetU32(pSSM, &pThat->cyInputMapping);
    }
    if (uVersion == sSSMDisplayVer5)
    {
        pVMM->pfnSSMR3GetU32(pSSM, &pThat->mfGuestVBVACapabilities);
        pVMM->pfnSSMR3GetU32(pSSM, &pThat->mfHostCursorCapabilities);
    }

    return VINF_SUCCESS;
}

/**
 * Initializes the display object.
 *
 * @returns COM result indicator
 * @param aParent   handle of our parent object
 */
HRESULT Display::init(Console *aParent)
{
    ComAssertRet(aParent, E_INVALIDARG);
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    mfSourceBitmapEnabled = true;
    fVGAResizing = false;

    ComPtr<IGraphicsAdapter> pGraphicsAdapter;
    HRESULT hrc = mParent->i_machine()->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    AssertComRCReturnRC(hrc);
    AssertReturn(!pGraphicsAdapter.isNull(), E_FAIL);

    ULONG ul;
    pGraphicsAdapter->COMGETTER(MonitorCount)(&ul);
    mcMonitors = ul;
    xInputMappingOrigin = 0;
    yInputMappingOrigin = 0;
    cxInputMapping = 0;
    cyInputMapping = 0;

    for (ul = 0; ul < mcMonitors; ul++)
    {
        maFramebuffers[ul].u32Offset = 0;
        maFramebuffers[ul].u32MaxFramebufferSize = 0;
        maFramebuffers[ul].u32InformationSize = 0;

        maFramebuffers[ul].pFramebuffer = NULL;
        /* All secondary monitors are disabled at startup. */
        maFramebuffers[ul].fDisabled = ul > 0;

        maFramebuffers[ul].u32Caps = 0;

        maFramebuffers[ul].updateImage.pu8Address = NULL;
        maFramebuffers[ul].updateImage.cbLine = 0;

        maFramebuffers[ul].xOrigin = 0;
        maFramebuffers[ul].yOrigin = 0;

        maFramebuffers[ul].w = 0;
        maFramebuffers[ul].h = 0;

        maFramebuffers[ul].flags = maFramebuffers[ul].fDisabled? VBVA_SCREEN_F_DISABLED: 0;

        maFramebuffers[ul].u16BitsPerPixel = 0;
        maFramebuffers[ul].pu8FramebufferVRAM = NULL;
        maFramebuffers[ul].u32LineSize = 0;

        maFramebuffers[ul].pHostEvents = NULL;

        maFramebuffers[ul].fDefaultFormat = false;

#ifdef VBOX_WITH_HGSMI
        maFramebuffers[ul].fVBVAEnabled = false;
        maFramebuffers[ul].fVBVAForceResize = false;
        maFramebuffers[ul].pVBVAHostFlags = NULL;
#endif /* VBOX_WITH_HGSMI */
    }

    {
        // register listener for state change events
        ComPtr<IEventSource> es;
        mParent->COMGETTER(EventSource)(es.asOutParam());
        com::SafeArray<VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnStateChanged);
        es->RegisterListener(this, ComSafeArrayAsInParam(eventTypes), true);
    }

#ifdef VBOX_WITH_RECORDING
    unconst(Recording.pCtx) = mParent->i_recordingGetContext();
#endif

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void Display::uninit()
{
    LogRelFlowFunc(("this=%p\n", this));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unsigned uScreenId;
    for (uScreenId = 0; uScreenId < mcMonitors; uScreenId++)
    {
        maFramebuffers[uScreenId].pSourceBitmap.setNull();
        maFramebuffers[uScreenId].updateImage.pSourceBitmap.setNull();
        maFramebuffers[uScreenId].updateImage.pu8Address = NULL;
        maFramebuffers[uScreenId].updateImage.cbLine = 0;
        maFramebuffers[uScreenId].pFramebuffer.setNull();
    }

    if (mParent)
    {
        ComPtr<IEventSource> es;
        mParent->COMGETTER(EventSource)(es.asOutParam());
        es->UnregisterListener(this);
    }

    unconst(mParent) = NULL;

    if (mpDrv)
        mpDrv->pDisplay = NULL;

    mpDrv = NULL;
}

/**
 * Register the SSM methods. Called by the power up thread to be able to
 * pass pVM
 */
int Display::i_registerSSM(PUVM pUVM)
{
    PCVMMR3VTABLE const pVMM = mParent->i_getVMMVTable();
    AssertPtrReturn(pVMM, VERR_INTERNAL_ERROR_3);

    /* Version 2 adds width and height of the framebuffer; version 3 adds
     * the framebuffer offset in the virtual desktop and the framebuffer flags;
     * version 4 adds guest to host input event mapping and version 5 adds
     * guest VBVA and host cursor capabilities.
     */
    int vrc = pVMM->pfnSSMR3RegisterExternal(pUVM, "DisplayData", 0, sSSMDisplayVer5,
                                             mcMonitors * sizeof(uint32_t) * 8 + sizeof(uint32_t),
                                             NULL, NULL, NULL,
                                             NULL, i_displaySSMSave, NULL,
                                             NULL, i_displaySSMLoad, NULL, this);
    AssertRCReturn(vrc, vrc);

    /*
     * Register loaders for old saved states where iInstance was
     * 3 * sizeof(uint32_t *) due to a code mistake.
     */
    vrc = pVMM->pfnSSMR3RegisterExternal(pUVM, "DisplayData", 12 /*uInstance*/, sSSMDisplayVer, 0 /*cbGuess*/,
                                         NULL, NULL, NULL,
                                         NULL, NULL, NULL,
                                         NULL, i_displaySSMLoad, NULL, this);
    AssertRCReturn(vrc, vrc);

    vrc = pVMM->pfnSSMR3RegisterExternal(pUVM, "DisplayData", 24 /*uInstance*/, sSSMDisplayVer, 0 /*cbGuess*/,
                                         NULL, NULL, NULL,
                                         NULL, NULL, NULL,
                                         NULL, i_displaySSMLoad, NULL, this);
    AssertRCReturn(vrc, vrc);

    /* uInstance is an arbitrary value greater than 1024. Such a value will ensure a quick seek in saved state file. */
    vrc = pVMM->pfnSSMR3RegisterExternal(pUVM, "DisplayScreenshot", 1100 /*uInstance*/, sSSMDisplayScreenshotVer, 0 /*cbGuess*/,
                                         NULL, NULL, NULL,
                                         NULL, i_displaySSMSaveScreenshot, NULL,
                                         NULL, i_displaySSMLoadScreenshot, NULL, this);

    AssertRCReturn(vrc, vrc);

    return VINF_SUCCESS;
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 *  Handles display resize event.
 *
 * @param uScreenId Screen ID
 * @param bpp       New bits per pixel.
 * @param pvVRAM    VRAM pointer.
 * @param cbLine    New bytes per line.
 * @param w         New display width.
 * @param h         New display height.
 * @param flags     Flags of the new video mode.
 * @param xOrigin   New display origin X.
 * @param yOrigin   New display origin Y.
 * @param fVGAResize Whether the resize is originated from the VGA device (DevVGA).
 */
int Display::i_handleDisplayResize(unsigned uScreenId, uint32_t bpp, void *pvVRAM,
                                   uint32_t cbLine, uint32_t w, uint32_t h, uint16_t flags,
                                   int32_t xOrigin, int32_t yOrigin, bool fVGAResize)
{
    LogRel2(("Display::i_handleDisplayResize: uScreenId=%d pvVRAM=%p w=%d h=%d bpp=%d cbLine=0x%X flags=0x%X\n", uScreenId,
             pvVRAM, w, h, bpp, cbLine, flags));

    /* Caller must not hold the object lock. */
    AssertReturn(!isWriteLockOnCurrentThread(), VERR_INVALID_STATE);

    /* Note: the old code checked if the video mode was actually changed and
     * did not invalidate the source bitmap if the mode did not change.
     * The new code always invalidates the source bitmap, i.e. it will
     * notify the frontend even if nothing actually changed.
     *
     * Implementing the filtering is possible but might lead to pfnSetRenderVRAM races
     * between this method and QuerySourceBitmap. Such races can be avoided by implementing
     * the @todo below.
     */

    /* Make sure that the VGA device does not access the source bitmap. */
    if (uScreenId == VBOX_VIDEO_PRIMARY_SCREEN && mpDrv)
    {
        /// @todo It is probably more convenient to implement
        // mpDrv->pUpPort->pfnSetOutputBitmap(pvVRAM, cbScanline, cBits, cx, cy, bool fSet);
        // and remove IConnector.pbData, cbScanline, cBits, cx, cy.
        // fSet = false disables rendering and VGA can check
        // if it is already rendering to a different bitmap, avoiding
        // enable/disable rendering races.
        mpDrv->pUpPort->pfnSetRenderVRAM(mpDrv->pUpPort, false);

        mpDrv->IConnector.pbData     = NULL;
        mpDrv->IConnector.cbScanline = 0;
        mpDrv->IConnector.cBits      = 32; /* DevVGA does not work with cBits == 0. */
        mpDrv->IConnector.cx         = 0;
        mpDrv->IConnector.cy         = 0;
    }

    /* Update maFramebuffers[uScreenId] under lock. */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (uScreenId >= mcMonitors)
    {
        LogRel(("Display::i_handleDisplayResize: mcMonitors=%u < uScreenId=%u (pvVRAM=%p w=%u h=%u bpp=%d cbLine=0x%X flags=0x%X)\n",
                mcMonitors, uScreenId, pvVRAM, w, h, bpp, cbLine, flags));
        return VINF_SUCCESS;
    }

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[uScreenId];

    /* Whether the monitor position has changed.
     * A resize initiated by the VGA device does not change the monitor position.
     */
    const bool fNewOrigin =    !fVGAResize
                            && (   pFBInfo->xOrigin != xOrigin
                                || pFBInfo->yOrigin != yOrigin);

    /* The event for disabled->enabled transition.
     * VGA resizes also come when the guest uses VBVA mode. They do not affect pFBInfo->fDisabled.
     * The primary screen is re-enabled when the guest leaves the VBVA mode in i_displayVBVADisable.
     */
    const bool fGuestMonitorChangedEvent =   !fVGAResize
                                          && (pFBInfo->fDisabled != RT_BOOL(flags & VBVA_SCREEN_F_DISABLED));

    /* Reset the update mode. */
    pFBInfo->updateImage.pSourceBitmap.setNull();
    pFBInfo->updateImage.pu8Address = NULL;
    pFBInfo->updateImage.cbLine = 0;

    /* Release the current source bitmap. */
    pFBInfo->pSourceBitmap.setNull();

    /* VGA blanking is signaled as w=0, h=0, bpp=0 and cbLine=0, and it's
     * best to keep the old resolution, as otherwise the window size would
     * change before the new resolution is known. */
    const bool fVGABlank = fVGAResize && uScreenId == VBOX_VIDEO_PRIMARY_SCREEN
                        && w == 0 && h == 0 && bpp == 0 && cbLine == 0;
    if (fVGABlank)
    {
        w = pFBInfo->w;
        h = pFBInfo->h;
    }

    /* Log changes. */
    if (   pFBInfo->w != w
        || pFBInfo->h != h
        || pFBInfo->u32LineSize != cbLine
        /*|| pFBInfo->pu8FramebufferVRAM != (uint8_t *)pvVRAM - too noisy */
        || (   !fVGAResize
            && (   pFBInfo->xOrigin != xOrigin
                || pFBInfo->yOrigin != yOrigin
                || pFBInfo->flags != flags)))
        LogRel(("Display::i_handleDisplayResize: uScreenId=%d pvVRAM=%p w=%d h=%d bpp=%d cbLine=0x%X flags=0x%X origin=%d,%d\n",
                uScreenId, pvVRAM, w, h, bpp, cbLine, flags, xOrigin, yOrigin));

    /* Update the video mode information. */
    pFBInfo->w = w;
    pFBInfo->h = h;
    pFBInfo->u16BitsPerPixel = (uint16_t)bpp;
    pFBInfo->pu8FramebufferVRAM = (uint8_t *)pvVRAM;
    pFBInfo->u32LineSize = cbLine;
    if (!fVGAResize)
    {
        /* Fields which are not used in not VBVA modes and not affected by a VGA resize. */
        pFBInfo->flags = flags;
        pFBInfo->xOrigin = xOrigin;
        pFBInfo->yOrigin = yOrigin;
        pFBInfo->fDisabled = RT_BOOL(flags & VBVA_SCREEN_F_DISABLED);
        pFBInfo->fVBVAForceResize = false;
    }
    else
    {
        pFBInfo->flags = VBVA_SCREEN_F_ACTIVE;
        if (fVGABlank)
            pFBInfo->flags |= VBVA_SCREEN_F_BLANK;
        pFBInfo->fDisabled = false;
    }

    /* Prepare local vars for the notification code below. */
    ComPtr<IFramebuffer> pFramebuffer = pFBInfo->pFramebuffer;
    const bool fDisabled = pFBInfo->fDisabled;

    alock.release(); /* Release lock before recording code gets involved below. */

#ifdef VBOX_WITH_RECORDING
    /* Recording needs to be called before releasing the display's lock below. */
    i_recordingScreenChanged(uScreenId, pFBInfo);
#endif

    if (!pFramebuffer.isNull())
    {
        HRESULT hrc = pFramebuffer->NotifyChange(uScreenId, 0, 0, w, h); /** @todo origin */
        LogFunc(("NotifyChange hr %08X\n", hrc));
        RT_NOREF(hrc);
    }

    if (fGuestMonitorChangedEvent)
    {
        if (fDisabled)
            ::FireGuestMonitorChangedEvent(mParent->i_getEventSource(),
                                           GuestMonitorChangedEventType_Disabled, uScreenId, 0, 0, 0, 0);
        else
            ::FireGuestMonitorChangedEvent(mParent->i_getEventSource(),
                                           GuestMonitorChangedEventType_Enabled, uScreenId, xOrigin, yOrigin, w, h);
    }

    if (fNewOrigin)
        ::FireGuestMonitorChangedEvent(mParent->i_getEventSource(),
                                       GuestMonitorChangedEventType_NewOrigin, uScreenId, xOrigin, yOrigin, 0, 0);

    /* Inform the VRDP server about the change of display parameters. */
    LogRelFlowFunc(("Calling VRDP\n"));
    mParent->i_consoleVRDPServer()->SendResize();

    /* And re-send the seamless rectangles if necessary. */
    if (mfSeamlessEnabled)
        i_handleSetVisibleRegion(mcRectVisibleRegion, mpRectVisibleRegion);

    LogRelFlowFunc(("[%d]: default format %d\n", uScreenId, pFBInfo->fDefaultFormat));

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

static void i_checkCoordBounds(int *px, int *py, int *pw, int *ph, int cx, int cy)
{
    /* Correct negative x and y coordinates. */
    if (*px < 0)
    {
        *px += *pw; /* Compute xRight which is also the new width. */

        *pw = (*px < 0)? 0: *px;

        *px = 0;
    }

    if (*py < 0)
    {
        *py += *ph; /* Compute xBottom, which is also the new height. */

        *ph = (*py < 0)? 0: *py;

        *py = 0;
    }

    /* Also check if coords are greater than the display resolution. */
    if (*px + *pw > cx)
    {
        *pw = cx > *px? cx - *px: 0;
    }

    if (*py + *ph > cy)
    {
        *ph = cy > *py? cy - *py: 0;
    }
}

void Display::i_handleDisplayUpdate(unsigned uScreenId, int x, int y, int w, int h)
{
    /*
     * Always runs under either VBVA lock or, for HGSMI, DevVGA lock.
     * Safe to use VBVA vars and take the framebuffer lock.
     */

#ifdef DEBUG_sunlover
    LogFlowFunc(("[%d] %d,%d %dx%d\n",
                 uScreenId, x, y, w, h));
#endif /* DEBUG_sunlover */

    /* No updates for a disabled guest screen. */
    if (maFramebuffers[uScreenId].fDisabled)
        return;

    /* No updates for a blank guest screen. */
    /** @note Disabled for now, as the GUI does not update the picture when we
     * first blank. */
    /* if (maFramebuffers[uScreenId].flags & VBVA_SCREEN_F_BLANK)
        return; */

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[uScreenId];
    AutoReadLock alockr(this COMMA_LOCKVAL_SRC_POS);

    ComPtr<IFramebuffer> pFramebuffer = pFBInfo->pFramebuffer;
    ComPtr<IDisplaySourceBitmap> pSourceBitmap = pFBInfo->updateImage.pSourceBitmap;

    alockr.release();

    if (RT_LIKELY(!pFramebuffer.isNull()))
    {
        if (RT_LIKELY(!RT_BOOL(pFBInfo->u32Caps & FramebufferCapabilities_UpdateImage)))
        {
            i_checkCoordBounds(&x, &y, &w, &h, pFBInfo->w, pFBInfo->h);

            if (w != 0 && h != 0)
            {
                pFramebuffer->NotifyUpdate(x, y, w, h);

#ifdef VBOX_WITH_RECORDING
                i_recordingScreenUpdate(uScreenId, x, y, w, h);
#endif
            }
        }
        else
        {
            if (RT_LIKELY(!pSourceBitmap.isNull()))
            { /* likely */ }
            else
            {
                /* Create a source bitmap if UpdateImage mode is used. */
                HRESULT hr = QuerySourceBitmap(uScreenId, pSourceBitmap.asOutParam());
                if (SUCCEEDED(hr))
                {
                    BYTE *pAddress = NULL;
                    ULONG ulWidth = 0;
                    ULONG ulHeight = 0;
                    ULONG ulBitsPerPixel = 0;
                    ULONG ulBytesPerLine = 0;
                    BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

                    hr = pSourceBitmap->QueryBitmapInfo(&pAddress,
                                                        &ulWidth,
                                                        &ulHeight,
                                                        &ulBitsPerPixel,
                                                        &ulBytesPerLine,
                                                        &bitmapFormat);
                    if (SUCCEEDED(hr))
                    {
                        AutoWriteLock alockw(this COMMA_LOCKVAL_SRC_POS);

                        if (pFBInfo->updateImage.pSourceBitmap.isNull())
                        {
                            pFBInfo->updateImage.pSourceBitmap = pSourceBitmap;
                            pFBInfo->updateImage.pu8Address = pAddress;
                            pFBInfo->updateImage.cbLine = ulBytesPerLine;
                        }

                        pSourceBitmap = pFBInfo->updateImage.pSourceBitmap;

                        alockw.release();
                    }
                }
            }

            if (RT_LIKELY(!pSourceBitmap.isNull()))
            {
                BYTE *pbAddress = NULL;
                ULONG ulWidth = 0;
                ULONG ulHeight = 0;
                ULONG ulBitsPerPixel = 0;
                ULONG ulBytesPerLine = 0;
                BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

                HRESULT hr = pSourceBitmap->QueryBitmapInfo(&pbAddress,
                                                            &ulWidth,
                                                            &ulHeight,
                                                            &ulBitsPerPixel,
                                                            &ulBytesPerLine,
                                                            &bitmapFormat);
                if (SUCCEEDED(hr))
                {
                    /* Make sure that the requested update is within the source bitmap dimensions. */
                    i_checkCoordBounds(&x, &y, &w, &h, ulWidth, ulHeight);

                    if (   w != 0
                        && h != 0)
                    {
                        unsigned const uBytesPerPixel = ulBitsPerPixel / 8;

                        const size_t cbData = w * h * uBytesPerPixel;
                        com::SafeArray<BYTE> image(cbData);

                        uint8_t *pu8Dst = image.raw();
                        const uint8_t *pu8Src = pbAddress + ulBytesPerLine * y + x * uBytesPerPixel;

                        for (int i = 0; i < h; ++i)
                        {
                            memcpy(pu8Dst, pu8Src, w * uBytesPerPixel);
                            pu8Dst += w * uBytesPerPixel;
                            pu8Src += ulBytesPerLine;
                        }

                        pFramebuffer->NotifyUpdateImage(x, y, w, h, ComSafeArrayAsInParam(image));

#ifdef VBOX_WITH_RECORDING
                        i_recordingScreenUpdate(uScreenId,
                                                image.raw(), ulBytesPerLine * h,
                                                x, y, w, h, ulBytesPerLine);
#endif
                    }
                }
            }
        }
    }

#ifndef VBOX_WITH_HGSMI
    if (!mVideoAccelLegacy.fVideoAccelEnabled)
#else
    if (!mVideoAccelLegacy.fVideoAccelEnabled && !maFramebuffers[uScreenId].fVBVAEnabled)
#endif
    {
        /* When VBVA is enabled, the VRDP server is informed
         * either in VideoAccelFlush or displayVBVAUpdateProcess.
         * Inform the server here only if VBVA is disabled.
         */
        mParent->i_consoleVRDPServer()->SendUpdateBitmap(uScreenId, x, y, w, h);
    }
}

void Display::i_updateGuestGraphicsFacility(void)
{
    Guest* pGuest = mParent->i_getGuest();
    AssertPtrReturnVoid(pGuest);
    /* The following is from GuestImpl.cpp. */
    /** @todo A nit: The timestamp is wrong on saved state restore. Would be better
     *  to move the graphics and seamless capability -> facility translation to
     *  VMMDev so this could be saved.  */
    RTTIMESPEC TimeSpecTS;
    RTTimeNow(&TimeSpecTS);

    if (   mfVMMDevSupportsGraphics
        || (mfGuestVBVACapabilities & VBVACAPS_VIDEO_MODE_HINTS) != 0)
        pGuest->i_setAdditionsStatus(VBoxGuestFacilityType_Graphics,
                                     VBoxGuestFacilityStatus_Active,
                                     0 /*fFlags*/, &TimeSpecTS);
    else
        pGuest->i_setAdditionsStatus(VBoxGuestFacilityType_Graphics,
                                     VBoxGuestFacilityStatus_Inactive,
                                     0 /*fFlags*/, &TimeSpecTS);
}

void Display::i_handleUpdateVMMDevSupportsGraphics(bool fSupportsGraphics)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mfVMMDevSupportsGraphics == fSupportsGraphics)
        return;
    mfVMMDevSupportsGraphics = fSupportsGraphics;
    i_updateGuestGraphicsFacility();
    /* The VMMDev interface notifies the console. */
}

void Display::i_handleUpdateGuestVBVACapabilities(uint32_t fNewCapabilities)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    bool fNotify = (fNewCapabilities & VBVACAPS_VIDEO_MODE_HINTS) != (mfGuestVBVACapabilities & VBVACAPS_VIDEO_MODE_HINTS);

    mfGuestVBVACapabilities = fNewCapabilities;
    if (!fNotify)
        return;
    i_updateGuestGraphicsFacility();
    /* Tell the console about it */
    mParent->i_onAdditionsStateChange();
}

void Display::i_handleUpdateVBVAInputMapping(int32_t xOrigin, int32_t yOrigin, uint32_t cx, uint32_t cy)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    xInputMappingOrigin = xOrigin;
    yInputMappingOrigin = yOrigin;
    cxInputMapping      = cx;
    cyInputMapping      = cy;

    /* Re-send the seamless rectangles if necessary. */
    if (mfSeamlessEnabled)
        i_handleSetVisibleRegion(mcRectVisibleRegion, mpRectVisibleRegion);
}

/**
 * Returns the upper left and lower right corners of the virtual framebuffer.
 * The lower right is "exclusive" (i.e. first pixel beyond the framebuffer),
 * and the origin is (0, 0), not (1, 1) like the GUI returns.
 */
void Display::i_getFramebufferDimensions(int32_t *px1, int32_t *py1,
                                         int32_t *px2, int32_t *py2)
{
    int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertPtrReturnVoid(px1);
    AssertPtrReturnVoid(py1);
    AssertPtrReturnVoid(px2);
    AssertPtrReturnVoid(py2);
    LogRelFlowFunc(("\n"));

    if (!mpDrv)
        return;

    if (maFramebuffers[0].fVBVAEnabled && cxInputMapping && cyInputMapping)
    {
        /* Guest uses VBVA with explicit mouse mapping dimensions. */
        x1 = xInputMappingOrigin;
        y1 = yInputMappingOrigin;
        x2 = xInputMappingOrigin + cxInputMapping;
        y2 = yInputMappingOrigin + cyInputMapping;
    }
    else
    {
        /* If VBVA is not in use then this flag will not be set and this
         * will still work as it should. */
        if (!maFramebuffers[0].fDisabled)
        {
            x1 = (int32_t)maFramebuffers[0].xOrigin;
            y1 = (int32_t)maFramebuffers[0].yOrigin;
            x2 = (int32_t)maFramebuffers[0].w + (int32_t)maFramebuffers[0].xOrigin;
            y2 = (int32_t)maFramebuffers[0].h + (int32_t)maFramebuffers[0].yOrigin;
        }

        for (unsigned i = 1; i < mcMonitors; ++i)
        {
            if (!maFramebuffers[i].fDisabled)
            {
                x1 = RT_MIN(x1, maFramebuffers[i].xOrigin);
                y1 = RT_MIN(y1, maFramebuffers[i].yOrigin);
                x2 = RT_MAX(x2, maFramebuffers[i].xOrigin + (int32_t)maFramebuffers[i].w);
                y2 = RT_MAX(y2, maFramebuffers[i].yOrigin + (int32_t)maFramebuffers[i].h);
            }
        }
    }

    *px1 = x1;
    *py1 = y1;
    *px2 = x2;
    *py2 = y2;
}

/** Updates the device's view of the host cursor handling capabilities.
 *  Calls into mpDrv->pUpPort. */
void Display::i_updateDeviceCursorCapabilities(void)
{
    bool fRenderCursor = true;
    bool fMoveCursor = mcVRDPRefs == 0;
#ifdef VBOX_WITH_RECORDING
    RecordingContext *pCtx = Recording.pCtx;
    if (   pCtx->IsStarted()
        && pCtx->IsFeatureEnabled(RecordingFeature_Video))
        fRenderCursor = fMoveCursor = false;
    else
#endif /* VBOX_WITH_RECORDING */
    {
        for (unsigned uScreenId = 0; uScreenId < mcMonitors; uScreenId++)
        {
            DISPLAYFBINFO *pFBInfo = &maFramebuffers[uScreenId];
            if (!(pFBInfo->u32Caps & FramebufferCapabilities_RenderCursor))
                fRenderCursor = false;
            if (!(pFBInfo->u32Caps & FramebufferCapabilities_MoveCursor))
                fMoveCursor = false;
        }
    }

    if (mpDrv)
        mpDrv->pUpPort->pfnReportHostCursorCapabilities(mpDrv->pUpPort, fRenderCursor, fMoveCursor);
}

HRESULT Display::i_reportHostCursorCapabilities(uint32_t fCapabilitiesAdded, uint32_t fCapabilitiesRemoved)
{
    /* Do we need this to access mParent?  I presume that the safe VM pointer
     * ensures that mpDrv will remain valid. */
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    uint32_t fHostCursorCapabilities =   (mfHostCursorCapabilities | fCapabilitiesAdded)
                                       & ~fCapabilitiesRemoved;

    Console::SafeVMPtr ptrVM(mParent);
    if (!ptrVM.isOk())
        return ptrVM.hrc();
    if (mfHostCursorCapabilities == fHostCursorCapabilities)
        return S_OK;
    CHECK_CONSOLE_DRV(mpDrv);
    alock.release();  /* Release before calling up for lock order reasons. */
    mfHostCursorCapabilities = fHostCursorCapabilities;
    i_updateDeviceCursorCapabilities();
    return S_OK;
}

HRESULT Display::i_reportHostCursorPosition(int32_t x, int32_t y, bool fOutOfRange)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    uint32_t xAdj = (uint32_t)RT_MAX(x - xInputMappingOrigin, 0);
    uint32_t yAdj = (uint32_t)RT_MAX(y - yInputMappingOrigin, 0);
    xAdj = RT_MIN(xAdj, cxInputMapping);
    yAdj = RT_MIN(yAdj, cyInputMapping);

    Console::SafeVMPtr ptrVM(mParent);
    if (!ptrVM.isOk())
        return ptrVM.hrc();
    CHECK_CONSOLE_DRV(mpDrv);
    alock.release();  /* Release before calling up for lock order reasons. */
    if (fOutOfRange)
        mpDrv->pUpPort->pfnReportHostCursorPosition(mpDrv->pUpPort, 0, 0, true);
    else
        mpDrv->pUpPort->pfnReportHostCursorPosition(mpDrv->pUpPort, xAdj, yAdj, false);
    return S_OK;
}

static bool displayIntersectRect(RTRECT *prectResult,
                                 const RTRECT *prect1,
                                 const RTRECT *prect2)
{
    /* Initialize result to an empty record. */
    memset(prectResult, 0, sizeof(RTRECT));

    int xLeftResult = RT_MAX(prect1->xLeft, prect2->xLeft);
    int xRightResult = RT_MIN(prect1->xRight, prect2->xRight);

    if (xLeftResult < xRightResult)
    {
        /* There is intersection by X. */

        int yTopResult = RT_MAX(prect1->yTop, prect2->yTop);
        int yBottomResult = RT_MIN(prect1->yBottom, prect2->yBottom);

        if (yTopResult < yBottomResult)
        {
            /* There is intersection by Y. */

            prectResult->xLeft   = xLeftResult;
            prectResult->yTop    = yTopResult;
            prectResult->xRight  = xRightResult;
            prectResult->yBottom = yBottomResult;

            return true;
        }
    }

    return false;
}

int Display::i_saveVisibleRegion(uint32_t cRect, PRTRECT pRect)
{
    RTRECT *pRectVisibleRegion = NULL;

    if (pRect == mpRectVisibleRegion)
        return VINF_SUCCESS;
    if (cRect != 0)
    {
        pRectVisibleRegion = (RTRECT *)RTMemAlloc(cRect * sizeof(RTRECT));
        if (!pRectVisibleRegion)
        {
            return VERR_NO_MEMORY;
        }
        memcpy(pRectVisibleRegion, pRect, cRect * sizeof(RTRECT));
    }
    if (mpRectVisibleRegion)
        RTMemFree(mpRectVisibleRegion);
    mcRectVisibleRegion = cRect;
    mpRectVisibleRegion = pRectVisibleRegion;
    return VINF_SUCCESS;
}

int Display::i_handleSetVisibleRegion(uint32_t cRect, PRTRECT pRect)
{
    RTRECT *pVisibleRegion = (RTRECT *)RTMemTmpAlloc(  RT_MAX(cRect, 1)
                                                     * sizeof(RTRECT));
    LogRel2(("%s: cRect=%u\n", __PRETTY_FUNCTION__, cRect));
    if (!pVisibleRegion)
    {
        return VERR_NO_TMP_MEMORY;
    }
    int vrc = i_saveVisibleRegion(cRect, pRect);
    if (RT_FAILURE(vrc))
    {
        RTMemTmpFree(pVisibleRegion);
        return vrc;
    }

    unsigned uScreenId;
    for (uScreenId = 0; uScreenId < mcMonitors; uScreenId++)
    {
        DISPLAYFBINFO *pFBInfo = &maFramebuffers[uScreenId];

        if (   !pFBInfo->pFramebuffer.isNull()
            && RT_BOOL(pFBInfo->u32Caps & FramebufferCapabilities_VisibleRegion))
        {
            /* Prepare a new array of rectangles which intersect with the framebuffer.
             */
            RTRECT rectFramebuffer;
            rectFramebuffer.xLeft   = pFBInfo->xOrigin - xInputMappingOrigin;
            rectFramebuffer.yTop    = pFBInfo->yOrigin - yInputMappingOrigin;
            rectFramebuffer.xRight  = rectFramebuffer.xLeft + pFBInfo->w;
            rectFramebuffer.yBottom = rectFramebuffer.yTop  + pFBInfo->h;

            uint32_t cRectVisibleRegion = 0;

            uint32_t i;
            for (i = 0; i < cRect; i++)
            {
                if (displayIntersectRect(&pVisibleRegion[cRectVisibleRegion], &pRect[i], &rectFramebuffer))
                {
                    pVisibleRegion[cRectVisibleRegion].xLeft -= rectFramebuffer.xLeft;
                    pVisibleRegion[cRectVisibleRegion].yTop -= rectFramebuffer.yTop;
                    pVisibleRegion[cRectVisibleRegion].xRight -= rectFramebuffer.xLeft;
                    pVisibleRegion[cRectVisibleRegion].yBottom -= rectFramebuffer.yTop;

                    cRectVisibleRegion++;
                }
            }
            pFBInfo->pFramebuffer->SetVisibleRegion((BYTE *)pVisibleRegion, cRectVisibleRegion);
        }
    }

    RTMemTmpFree(pVisibleRegion);

    return VINF_SUCCESS;
}

int  Display::i_handleUpdateMonitorPositions(uint32_t cPositions, PCRTPOINT paPositions)
{
    AssertMsgReturn(paPositions, ("Empty monitor position array\n"), E_INVALIDARG);
    for (unsigned i = 0; i < cPositions; ++i)
        LogRel2(("Display::i_handleUpdateMonitorPositions: uScreenId=%d xOrigin=%d yOrigin=%dX\n",
                 i, paPositions[i].x, paPositions[i].y));

    if (mpDrv && mpDrv->pUpPort->pfnReportMonitorPositions)
        mpDrv->pUpPort->pfnReportMonitorPositions(mpDrv->pUpPort, cPositions, paPositions);
    return VINF_SUCCESS;
}

int Display::i_handleQueryVisibleRegion(uint32_t *pcRects, PRTRECT paRects)
{
    /// @todo Currently not used by the guest and is not implemented in
    /// framebuffers. Remove?
    RT_NOREF(pcRects, paRects);
    return VERR_NOT_SUPPORTED;
}

#ifdef VBOX_WITH_HGSMI
static void vbvaSetMemoryFlagsHGSMI(unsigned uScreenId,
                                    uint32_t fu32SupportedOrders,
                                    bool fVideoAccelVRDP,
                                    DISPLAYFBINFO *pFBInfo)
{
    LogRelFlowFunc(("HGSMI[%d]: %p\n", uScreenId, pFBInfo->pVBVAHostFlags));

    if (pFBInfo->pVBVAHostFlags)
    {
        uint32_t fu32HostEvents = VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET;

        if (pFBInfo->fVBVAEnabled)
        {
            fu32HostEvents |= VBVA_F_MODE_ENABLED;

            if (fVideoAccelVRDP)
            {
                fu32HostEvents |= VBVA_F_MODE_VRDP;
            }
        }

        ASMAtomicWriteU32(&pFBInfo->pVBVAHostFlags->u32HostEvents, fu32HostEvents);
        ASMAtomicWriteU32(&pFBInfo->pVBVAHostFlags->u32SupportedOrders, fu32SupportedOrders);

        LogRelFlowFunc(("    fu32HostEvents = 0x%08X, fu32SupportedOrders = 0x%08X\n", fu32HostEvents, fu32SupportedOrders));
    }
}

static void vbvaSetMemoryFlagsAllHGSMI(uint32_t fu32SupportedOrders,
                                       bool fVideoAccelVRDP,
                                       DISPLAYFBINFO *paFBInfos,
                                       unsigned cFBInfos)
{
    unsigned uScreenId;

    for (uScreenId = 0; uScreenId < cFBInfos; uScreenId++)
    {
        vbvaSetMemoryFlagsHGSMI(uScreenId, fu32SupportedOrders, fVideoAccelVRDP, &paFBInfos[uScreenId]);
    }
}
#endif /* VBOX_WITH_HGSMI */

int Display::VideoAccelEnableVMMDev(bool fEnable, VBVAMEMORY *pVbvaMemory)
{
    LogFlowFunc(("%d %p\n", fEnable, pVbvaMemory));
    int vrc = videoAccelEnterVMMDev(&mVideoAccelLegacy);
    if (RT_SUCCESS(vrc))
    {
        vrc = i_VideoAccelEnable(fEnable, pVbvaMemory, mpDrv->pUpPort);
        videoAccelLeaveVMMDev(&mVideoAccelLegacy);
    }
    LogFlowFunc(("leave %Rrc\n", vrc));
    return vrc;
}

int Display::VideoAccelEnableVGA(bool fEnable, VBVAMEMORY *pVbvaMemory)
{
    LogFlowFunc(("%d %p\n", fEnable, pVbvaMemory));
    int vrc = videoAccelEnterVGA(&mVideoAccelLegacy);
    if (RT_SUCCESS(vrc))
    {
        vrc = i_VideoAccelEnable(fEnable, pVbvaMemory, mpDrv->pUpPort);
        videoAccelLeaveVGA(&mVideoAccelLegacy);
    }
    LogFlowFunc(("leave %Rrc\n", vrc));
    return vrc;
}

void Display::VideoAccelFlushVMMDev(void)
{
    LogFlowFunc(("enter\n"));
    int vrc = videoAccelEnterVMMDev(&mVideoAccelLegacy);
    if (RT_SUCCESS(vrc))
    {
        i_VideoAccelFlush(mpDrv->pUpPort);
        videoAccelLeaveVMMDev(&mVideoAccelLegacy);
    }
    LogFlowFunc(("leave\n"));
}

/* Called always by one VRDP server thread. Can be thread-unsafe.
 */
void Display::i_VRDPConnectionEvent(bool fConnect)
{
    LogRelFlowFunc(("fConnect = %d\n", fConnect));

    int c = fConnect?
                ASMAtomicIncS32(&mcVRDPRefs):
                ASMAtomicDecS32(&mcVRDPRefs);

    i_VideoAccelVRDP(fConnect, c);
    i_updateDeviceCursorCapabilities();
}


void Display::i_VideoAccelVRDP(bool fEnable, int c)
{
    VIDEOACCEL *pVideoAccel = &mVideoAccelLegacy;

    Assert (c >= 0);
    RT_NOREF(fEnable);

    /* This can run concurrently with Display videoaccel state change. */
    RTCritSectEnter(&mVideoAccelLock);

    if (c == 0)
    {
        /* The last client has disconnected, and the accel can be
         * disabled.
         */
        Assert(fEnable == false);

        mfVideoAccelVRDP = false;
        mfu32SupportedOrders = 0;

        i_vbvaSetMemoryFlags(pVideoAccel->pVbvaMemory, pVideoAccel->fVideoAccelEnabled, mfVideoAccelVRDP, mfu32SupportedOrders,
                             maFramebuffers, mcMonitors);
#ifdef VBOX_WITH_HGSMI
        /* Here is VRDP-IN thread. Process the request in vbvaUpdateBegin under DevVGA lock on an EMT. */
        ASMAtomicIncU32(&mu32UpdateVBVAFlags);
#endif /* VBOX_WITH_HGSMI */

        LogRel(("VBVA: VRDP acceleration has been disabled.\n"));
    }
    else if (   c == 1
             && !mfVideoAccelVRDP)
    {
        /* The first client has connected. Enable the accel.
         */
        Assert(fEnable == true);

        mfVideoAccelVRDP = true;
        /* Supporting all orders. */
        mfu32SupportedOrders = UINT32_MAX;

        i_vbvaSetMemoryFlags(pVideoAccel->pVbvaMemory, pVideoAccel->fVideoAccelEnabled, mfVideoAccelVRDP, mfu32SupportedOrders,
                             maFramebuffers, mcMonitors);
#ifdef VBOX_WITH_HGSMI
        /* Here is VRDP-IN thread. Process the request in vbvaUpdateBegin under DevVGA lock on an EMT. */
        ASMAtomicIncU32(&mu32UpdateVBVAFlags);
#endif /* VBOX_WITH_HGSMI */

        LogRel(("VBVA: VRDP acceleration has been requested.\n"));
    }
    else
    {
        /* A client is connected or disconnected but there is no change in the
         * accel state. It remains enabled.
         */
        Assert(mfVideoAccelVRDP == true);
    }

    RTCritSectLeave(&mVideoAccelLock);
}

void Display::i_notifyPowerDown(void)
{
    LogRelFlowFunc(("\n"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Source bitmaps are not available anymore. */
    mfSourceBitmapEnabled = false;

    alock.release();

    /* Resize all displays to tell framebuffers to forget current source bitmap. */
    unsigned uScreenId = mcMonitors;
    while (uScreenId > 0)
    {
        --uScreenId;

        DISPLAYFBINFO *pFBInfo = &maFramebuffers[uScreenId];
        if (!pFBInfo->fDisabled)
        {
            i_handleDisplayResize(uScreenId, 32,
                                  pFBInfo->pu8FramebufferVRAM,
                                  pFBInfo->u32LineSize,
                                  pFBInfo->w,
                                  pFBInfo->h,
                                  pFBInfo->flags,
                                  pFBInfo->xOrigin,
                                  pFBInfo->yOrigin,
                                  false);
        }
    }
}

// Wrapped IDisplay methods
/////////////////////////////////////////////////////////////////////////////
HRESULT Display::getScreenResolution(ULONG aScreenId, ULONG *aWidth, ULONG *aHeight, ULONG *aBitsPerPixel,
                                     LONG *aXOrigin, LONG *aYOrigin, GuestMonitorStatus_T *aGuestMonitorStatus)
{
    LogRelFlowFunc(("aScreenId=%RU32\n", aScreenId));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aScreenId >= mcMonitors)
        return E_INVALIDARG;

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[aScreenId];

    GuestMonitorStatus_T guestMonitorStatus = GuestMonitorStatus_Enabled;

    if (pFBInfo->flags & VBVA_SCREEN_F_DISABLED)
        guestMonitorStatus = GuestMonitorStatus_Disabled;
    else if (pFBInfo->flags & (VBVA_SCREEN_F_BLANK | VBVA_SCREEN_F_BLANK2))
        guestMonitorStatus = GuestMonitorStatus_Blank;

    if (aWidth)
        *aWidth = pFBInfo->w;
    if (aHeight)
        *aHeight = pFBInfo->h;
    if (aBitsPerPixel)
        *aBitsPerPixel = pFBInfo->u16BitsPerPixel;
    if (aXOrigin)
        *aXOrigin = pFBInfo->xOrigin;
    if (aYOrigin)
        *aYOrigin = pFBInfo->yOrigin;
    if (aGuestMonitorStatus)
        *aGuestMonitorStatus = guestMonitorStatus;

    return S_OK;
}


HRESULT Display::attachFramebuffer(ULONG aScreenId, const ComPtr<IFramebuffer> &aFramebuffer, com::Guid &aId)
{
    LogRelFlowFunc(("aScreenId = %d\n", aScreenId));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aScreenId >= mcMonitors)
        return setError(E_INVALIDARG, tr("AttachFramebuffer: Invalid screen %d (total %d)"),
                        aScreenId, mcMonitors);

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[aScreenId];
    if (!pFBInfo->pFramebuffer.isNull())
        return setError(E_FAIL, tr("AttachFramebuffer: Framebuffer already attached to %d"),
                        aScreenId);

    pFBInfo->pFramebuffer = aFramebuffer;
    pFBInfo->framebufferId.create();
    aId = pFBInfo->framebufferId;

    SafeArray<FramebufferCapabilities_T> caps;
    pFBInfo->pFramebuffer->COMGETTER(Capabilities)(ComSafeArrayAsOutParam(caps));
    pFBInfo->u32Caps = 0;
    size_t i;
    for (i = 0; i < caps.size(); ++i)
        pFBInfo->u32Caps |= caps[i];

    alock.release();

    /* The driver might not have been constructed yet */
    if (mpDrv)
    {
        /* Inform the framebuffer about the actual screen size. */
        HRESULT hr = aFramebuffer->NotifyChange(aScreenId, 0, 0, pFBInfo->w, pFBInfo->h); /** @todo origin */
        LogFunc(("NotifyChange hr %08X\n", hr)); NOREF(hr);

        /* Re-send the seamless rectangles if necessary. */
        if (mfSeamlessEnabled)
            i_handleSetVisibleRegion(mcRectVisibleRegion, mpRectVisibleRegion);
    }

    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk()) /** @todo r=andy This apparently *never* is true at this point? */
        ptrVM.vtable()->pfnVMR3ReqCallNoWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)Display::i_InvalidateAndUpdateEMT, 3,
                                              this, aScreenId, false);

    LogRelFlowFunc(("Attached to %d %RTuuid\n", aScreenId, aId.raw()));
    return S_OK;
}

HRESULT Display::detachFramebuffer(ULONG aScreenId, const com::Guid &aId)
{
    LogRelFlowFunc(("aScreenId = %d %RTuuid\n", aScreenId, aId.raw()));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aScreenId >= mcMonitors)
        return setError(E_INVALIDARG, tr("DetachFramebuffer: Invalid screen %d (total %d)"),
                        aScreenId, mcMonitors);

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[aScreenId];

    if (pFBInfo->framebufferId != aId)
    {
        LogRelFlowFunc(("Invalid framebuffer aScreenId = %d, attached %p\n", aScreenId, pFBInfo->framebufferId.raw()));
        return setError(E_FAIL, tr("DetachFramebuffer: Invalid framebuffer object"));
    }

    pFBInfo->pFramebuffer.setNull();
    pFBInfo->framebufferId.clear();

    alock.release();
    return S_OK;
}

HRESULT Display::queryFramebuffer(ULONG aScreenId, ComPtr<IFramebuffer> &aFramebuffer)
{
    LogRelFlowFunc(("aScreenId = %d\n", aScreenId));

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aScreenId >= mcMonitors)
        return setError(E_INVALIDARG, tr("QueryFramebuffer: Invalid screen %d (total %d)"),
                        aScreenId, mcMonitors);

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[aScreenId];

    pFBInfo->pFramebuffer.queryInterfaceTo(aFramebuffer.asOutParam());

    return S_OK;
}

HRESULT Display::setVideoModeHint(ULONG aDisplay, BOOL aEnabled,
                                  BOOL aChangeOrigin, LONG aOriginX, LONG aOriginY,
                                  ULONG aWidth, ULONG aHeight, ULONG aBitsPerPixel,
                                  BOOL aNotify)
{
    if (aWidth == 0 || aHeight == 0 || aBitsPerPixel == 0)
    {
        /* Some of parameters must not change. Query current mode. */
        ULONG ulWidth        = 0;
        ULONG ulHeight       = 0;
        ULONG ulBitsPerPixel = 0;
        HRESULT hr = getScreenResolution(aDisplay, &ulWidth, &ulHeight, &ulBitsPerPixel, NULL, NULL, NULL);
        if (FAILED(hr))
            return hr;

        /* Assign current values to not changing parameters. */
        if (aWidth == 0)
            aWidth = ulWidth;
        if (aHeight == 0)
            aHeight = ulHeight;
        if (aBitsPerPixel == 0)
             aBitsPerPixel = ulBitsPerPixel;
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aDisplay >= mcMonitors)
        return E_INVALIDARG;

    VMMDevDisplayDef d;
    d.idDisplay     = aDisplay;
    d.xOrigin       = aOriginX;
    d.yOrigin       = aOriginY;
    d.cx            = aWidth;
    d.cy            = aHeight;
    d.cBitsPerPixel = aBitsPerPixel;
    d.fDisplayFlags = VMMDEV_DISPLAY_CX | VMMDEV_DISPLAY_CY | VMMDEV_DISPLAY_BPP;
    if (!aEnabled)
        d.fDisplayFlags |= VMMDEV_DISPLAY_DISABLED;
    if (aChangeOrigin)
        d.fDisplayFlags |= VMMDEV_DISPLAY_ORIGIN;
    if (aDisplay == 0)
        d.fDisplayFlags |= VMMDEV_DISPLAY_PRIMARY;

    /* Remember the monitor information. */
    maFramebuffers[aDisplay].monitorDesc = d;

    CHECK_CONSOLE_DRV(mpDrv);

    /*
     * It is up to the guest to decide whether the hint is
     * valid. Therefore don't do any VRAM sanity checks here.
     */

    /* Have to release the lock because the pfnRequestDisplayChange
     * will call EMT.  */
    alock.release();

    /* We always send the hint to the graphics card in case the guest enables
     * support later.  For now we notify exactly when support is enabled. */
    mpDrv->pUpPort->pfnSendModeHint(mpDrv->pUpPort, aWidth, aHeight,
                                    aBitsPerPixel, aDisplay,
                                    aChangeOrigin ? aOriginX : ~0,
                                    aChangeOrigin ? aOriginY : ~0,
                                    RT_BOOL(aEnabled),
                                       (mfGuestVBVACapabilities & VBVACAPS_VIDEO_MODE_HINTS)
                                    && aNotify);
    if (   mfGuestVBVACapabilities & VBVACAPS_VIDEO_MODE_HINTS
        && !(mfGuestVBVACapabilities & VBVACAPS_IRQ)
        && aNotify)
        mParent->i_sendACPIMonitorHotPlugEvent();

    /* We currently never suppress the VMMDev hint if the guest has requested
     * it.  Specifically the video graphics driver may not be responsible for
     * screen positioning in the guest virtual desktop, and the component
     * responsible may want to get the hint from VMMDev. */
    VMMDev *pVMMDev = mParent->i_getVMMDev();
    if (pVMMDev)
    {
        PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
        if (pVMMDevPort)
            pVMMDevPort->pfnRequestDisplayChange(pVMMDevPort, 1, &d, false, RT_BOOL(aNotify));
    }
    /* Notify listeners. */
    ::FireGuestMonitorInfoChangedEvent(mParent->i_getEventSource(), aDisplay);
    return S_OK;
}

HRESULT Display::getVideoModeHint(ULONG cDisplay, BOOL *pfEnabled,
                                  BOOL *pfChangeOrigin, LONG *pxOrigin, LONG *pyOrigin,
                                  ULONG *pcx, ULONG *pcy, ULONG *pcBitsPerPixel)
{
    if (cDisplay >= mcMonitors)
        return E_INVALIDARG;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (pfEnabled)
        *pfEnabled      = !(  maFramebuffers[cDisplay].monitorDesc.fDisplayFlags
                            & VMMDEV_DISPLAY_DISABLED);
    if (pfChangeOrigin)
        *pfChangeOrigin = RT_BOOL(  maFramebuffers[cDisplay].monitorDesc.fDisplayFlags
                                  & VMMDEV_DISPLAY_ORIGIN);
    if (pxOrigin)
        *pxOrigin       = maFramebuffers[cDisplay].monitorDesc.xOrigin;
    if (pyOrigin)
        *pyOrigin       = maFramebuffers[cDisplay].monitorDesc.yOrigin;
    if (pcx)
        *pcx            = maFramebuffers[cDisplay].monitorDesc.cx;
    if (pcy)
        *pcy            = maFramebuffers[cDisplay].monitorDesc.cy;
    if (pcBitsPerPixel)
        *pcBitsPerPixel = maFramebuffers[cDisplay].monitorDesc.cBitsPerPixel;
    return S_OK;
}

HRESULT Display::setSeamlessMode(BOOL enabled)
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Have to release the lock because the pfnRequestSeamlessChange will call EMT.  */
    alock.release();

    VMMDev *pVMMDev = mParent->i_getVMMDev();
    if (pVMMDev)
    {
        PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
        if (pVMMDevPort)
            pVMMDevPort->pfnRequestSeamlessChange(pVMMDevPort, !!enabled);
    }
    mfSeamlessEnabled = RT_BOOL(enabled);
    return S_OK;
}

/*static*/ DECLCALLBACK(int)
Display::i_displayTakeScreenshotEMT(Display *pDisplay, ULONG aScreenId, uint8_t **ppbData, size_t *pcbData,
                                    uint32_t *pcx, uint32_t *pcy, bool *pfMemFree)
{
    int vrc;
    if (   aScreenId == VBOX_VIDEO_PRIMARY_SCREEN
        && pDisplay->maFramebuffers[aScreenId].fVBVAEnabled == false) /* A non-VBVA mode. */
    {
        if (pDisplay->mpDrv)
        {
            vrc = pDisplay->mpDrv->pUpPort->pfnTakeScreenshot(pDisplay->mpDrv->pUpPort, ppbData, pcbData, pcx, pcy);
            *pfMemFree = false;
        }
        else
        {
            /* No image. */
            *ppbData = NULL;
            *pcbData = 0;
            *pcx = 0;
            *pcy = 0;
            *pfMemFree = true;
            vrc = VINF_SUCCESS;
        }
    }
    else if (aScreenId < pDisplay->mcMonitors)
    {
        DISPLAYFBINFO *pFBInfo = &pDisplay->maFramebuffers[aScreenId];

        uint32_t width = pFBInfo->w;
        uint32_t height = pFBInfo->h;

        /* Allocate 32 bit per pixel bitmap. */
        size_t cbRequired = width * 4 * height;

        if (cbRequired)
        {
            uint8_t *pbDst = (uint8_t *)RTMemAlloc(cbRequired);
            if (pbDst != NULL)
            {
                if (pFBInfo->flags & VBVA_SCREEN_F_ACTIVE)
                {
                    /* Copy guest VRAM to the allocated 32bpp buffer. */
                    const uint8_t *pu8Src       = pFBInfo->pu8FramebufferVRAM;
                    int32_t xSrc                = 0;
                    int32_t ySrc                = 0;
                    uint32_t u32SrcWidth        = width;
                    uint32_t u32SrcHeight       = height;
                    uint32_t u32SrcLineSize     = pFBInfo->u32LineSize;
                    uint32_t u32SrcBitsPerPixel = pFBInfo->u16BitsPerPixel;

                    int32_t xDst                = 0;
                    int32_t yDst                = 0;
                    uint32_t u32DstWidth        = u32SrcWidth;
                    uint32_t u32DstHeight       = u32SrcHeight;
                    uint32_t u32DstLineSize     = u32DstWidth * 4;
                    uint32_t u32DstBitsPerPixel = 32;

                    vrc = pDisplay->mpDrv->pUpPort->pfnCopyRect(pDisplay->mpDrv->pUpPort,
                                                                width, height,
                                                                pu8Src,
                                                                xSrc, ySrc,
                                                                u32SrcWidth, u32SrcHeight,
                                                                u32SrcLineSize, u32SrcBitsPerPixel,
                                                                pbDst,
                                                                xDst, yDst,
                                                                u32DstWidth, u32DstHeight,
                                                                u32DstLineSize, u32DstBitsPerPixel);
                }
                else
                {
                    memset(pbDst, 0, cbRequired);
                    vrc = VINF_SUCCESS;
                }
                if (RT_SUCCESS(vrc))
                {
                    *ppbData = pbDst;
                    *pcbData = cbRequired;
                    *pcx = width;
                    *pcy = height;
                    *pfMemFree = true;
                }
                else
                {
                    RTMemFree(pbDst);

                    /* CopyRect can fail if VBVA was paused in VGA device, retry using the generic method. */
                    if (   vrc == VERR_INVALID_STATE
                        && aScreenId == VBOX_VIDEO_PRIMARY_SCREEN)
                    {
                        vrc = pDisplay->mpDrv->pUpPort->pfnTakeScreenshot(pDisplay->mpDrv->pUpPort, ppbData, pcbData, pcx, pcy);
                        *pfMemFree = false;
                    }
                }
            }
            else
                vrc = VERR_NO_MEMORY;
        }
        else
        {
            /* No image. */
            *ppbData = NULL;
            *pcbData = 0;
            *pcx = 0;
            *pcy = 0;
            *pfMemFree = true;
            vrc = VINF_SUCCESS;
        }
    }
    else
        vrc = VERR_INVALID_PARAMETER;
    return vrc;
}

static int i_displayTakeScreenshot(PUVM pUVM, PCVMMR3VTABLE pVMM, Display *pDisplay, struct DRVMAINDISPLAY *pDrv,
                                   ULONG aScreenId, BYTE *address, ULONG width, ULONG height)
{
    uint8_t *pbData = NULL;
    size_t cbData = 0;
    uint32_t cx = 0;
    uint32_t cy = 0;
    bool fFreeMem = false;
    int vrc = VINF_SUCCESS;

    int cRetries = 5;
    while (cRetries-- > 0)
    {
        /* Note! Not sure if the priority call is such a good idea here, but
                 it would be nice to have an accurate screenshot for the bug
                 report if the VM deadlocks. */
        vrc = pVMM->pfnVMR3ReqPriorityCallWaitU(pUVM, VMCPUID_ANY, (PFNRT)Display::i_displayTakeScreenshotEMT, 7,
                                                pDisplay, aScreenId, &pbData, &cbData, &cx, &cy, &fFreeMem);
        if (vrc != VERR_TRY_AGAIN)
        {
            break;
        }

        RTThreadSleep(10);
    }

    if (RT_SUCCESS(vrc) && pbData)
    {
        if (cx == width && cy == height)
        {
            /* No scaling required. */
            memcpy(address, pbData, cbData);
        }
        else
        {
            /* Scale. */
            LogRelFlowFunc(("SCALE: %dx%d -> %dx%d\n", cx, cy, width, height));

            uint8_t *dst = address;
            uint8_t *src = pbData;
            int dstW = width;
            int dstH = height;
            int srcW = cx;
            int srcH = cy;
            int iDeltaLine = cx * 4;

            BitmapScale32(dst,
                          dstW, dstH,
                          src,
                          iDeltaLine,
                          srcW, srcH);
        }

        if (fFreeMem)
            RTMemFree(pbData);
        else
        {
            /* This can be called from any thread. */
            pDrv->pUpPort->pfnFreeScreenshot(pDrv->pUpPort, pbData);
        }
    }

    return vrc;
}

HRESULT Display::takeScreenShotWorker(ULONG aScreenId,
                                      BYTE *aAddress,
                                      ULONG aWidth,
                                      ULONG aHeight,
                                      BitmapFormat_T aBitmapFormat,
                                      ULONG *pcbOut)
{
    HRESULT hrc = S_OK;

    /* Do not allow too small and too large screenshots. This also filters out negative
     * values passed as either 'aWidth' or 'aHeight'.
     */
    CheckComArgExpr(aWidth, aWidth != 0 && aWidth <= 32767);
    CheckComArgExpr(aHeight, aHeight != 0 && aHeight <= 32767);

    if (   aBitmapFormat != BitmapFormat_BGR0
        && aBitmapFormat != BitmapFormat_BGRA
        && aBitmapFormat != BitmapFormat_RGBA
        && aBitmapFormat != BitmapFormat_PNG)
    {
        return setError(E_NOTIMPL,
                        tr("Unsupported screenshot format 0x%08X"), aBitmapFormat);
    }

    Console::SafeVMPtr ptrVM(mParent);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    int vrc = i_displayTakeScreenshot(ptrVM.rawUVM(), ptrVM.vtable(), this, mpDrv, aScreenId, aAddress, aWidth, aHeight);
    if (RT_SUCCESS(vrc))
    {
        const size_t cbData = aWidth * 4 * aHeight;

        /* Most of uncompressed formats. */
        *pcbOut = (ULONG)cbData;

        if (aBitmapFormat == BitmapFormat_BGR0)
        {
            /* Do nothing. */
        }
        else if (aBitmapFormat == BitmapFormat_BGRA)
        {
            uint32_t *pu32 = (uint32_t *)aAddress;
            size_t cPixels = aWidth * aHeight;
            while (cPixels--)
                *pu32++ |= UINT32_C(0xFF000000);
        }
        else if (aBitmapFormat == BitmapFormat_RGBA)
        {
            uint8_t *pu8 = aAddress;
            size_t cPixels = aWidth * aHeight;
            while (cPixels--)
            {
                uint8_t u8 = pu8[0];
                pu8[0] = pu8[2];
                pu8[2] = u8;
                pu8[3] = 0xFF;

                pu8 += 4;
            }
        }
        else if (aBitmapFormat == BitmapFormat_PNG)
        {
            uint8_t *pu8PNG = NULL;
            uint32_t cbPNG = 0;
            uint32_t cxPNG = 0;
            uint32_t cyPNG = 0;

            vrc = DisplayMakePNG(aAddress, aWidth, aHeight, &pu8PNG, &cbPNG, &cxPNG, &cyPNG, 0);
            if (RT_SUCCESS(vrc))
            {
                if (cbPNG <= cbData)
                {
                    memcpy(aAddress, pu8PNG, cbPNG);
                    *pcbOut = cbPNG;
                }
                else
                    hrc = setError(E_FAIL, tr("PNG is larger than 32bpp bitmap"));
            }
            else
                hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not convert screenshot to PNG (%Rrc)"), vrc);
            RTMemFree(pu8PNG);
        }
    }
    else if (vrc == VERR_NOT_SUPPORTED) /* zero screen size (or too large screen size for vram) */
        hrc = setErrorBothNoLog(VBOX_E_NOT_SUPPORTED, vrc, tr("Screenshot is not possible at this time, invalid size"));
    else if (vrc == VERR_TRY_AGAIN)     /* resizing while taking screenshot */
        hrc = setErrorBoth(E_UNEXPECTED, vrc, tr("Screenshot is not possible at this time"));
    else if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not take a screenshot (%Rrc)"), vrc);

    return hrc;
}

HRESULT Display::takeScreenShot(ULONG aScreenId,
                                BYTE *aAddress,
                                ULONG aWidth,
                                ULONG aHeight,
                                BitmapFormat_T aBitmapFormat)
{
    LogRelFlowFunc(("[%d] address=%p, width=%d, height=%d, format 0x%08X\n",
                     aScreenId, aAddress, aWidth, aHeight, aBitmapFormat));

    ULONG cbOut = 0;
    HRESULT hrc = takeScreenShotWorker(aScreenId, aAddress, aWidth, aHeight, aBitmapFormat, &cbOut);
    NOREF(cbOut);

    LogRelFlowFunc(("%Rhrc\n", hrc));
    return hrc;
}

HRESULT Display::takeScreenShotToArray(ULONG aScreenId,
                                       ULONG aWidth,
                                       ULONG aHeight,
                                       BitmapFormat_T aBitmapFormat,
                                       std::vector<BYTE> &aScreenData)
{
    LogRelFlowFunc(("[%d] width=%d, height=%d, format 0x%08X\n",
                     aScreenId, aWidth, aHeight, aBitmapFormat));

    /* Do not allow too small and too large screenshots. This also filters out negative
     * values passed as either 'aWidth' or 'aHeight'.
     */
    CheckComArgExpr(aWidth, aWidth != 0 && aWidth <= 32767);
    CheckComArgExpr(aHeight, aHeight != 0 && aHeight <= 32767);

    const size_t cbData = aWidth * 4 * aHeight;
    aScreenData.resize(cbData);

    ULONG cbOut = 0;
    HRESULT hrc = takeScreenShotWorker(aScreenId, &aScreenData.front(), aWidth, aHeight, aBitmapFormat, &cbOut);
    if (FAILED(hrc))
        cbOut = 0;

    aScreenData.resize(cbOut);

    LogRelFlowFunc(("%Rhrc\n", hrc));
    return hrc;
}

#ifdef VBOX_WITH_RECORDING
/**
 * Starts video (+ audio) recording.
 *
 * @returns VBox status code.
 */
int Display::i_recordingStart(void)
{
#ifdef VBOX_WITH_STATISTICS
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
    {
        ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &Stats.profileDisplayRefreshCallback,
                                                STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                                "Profiling display refresh.", "/Main/Display/ProfRefresh");
        ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &Stats.Recording.profileRecording,
                                            STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                            "Profiling recording time for all monitors.", "/Main/Display/ProfRecording");

        for (unsigned i = 0; i < mcMonitors; i++)
            ptrVM.vtable()->pfnSTAMR3RegisterFU(ptrVM.rawUVM(), &Stats.Monitor[i].Recording.profileRecording,
                                                STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                                                "Profiling recording time for this monitor.", "/Main/Display/Monitor%RU32/ProfRecording", i);
    }
#endif

    return i_recordingInvalidate();
}

/**
 * Stops video (+ audio) recording.
 *
 * @returns VBox status code.
 */
int Display::i_recordingStop(void)
{
#ifdef VBOX_WITH_STATISTICS
    Console::SafeVMPtrQuiet ptrVM(mParent);
    if (ptrVM.isOk())
    {
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Display/ProfRefresh");
        ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Display/ProfRecording");

        for (unsigned i = 0; i < mcMonitors; i++)
            ptrVM.vtable()->pfnSTAMR3DeregisterF(ptrVM.rawUVM(), "/Main/Display/Monitor%RU32/ProfRecording", i);
    }
#endif

    return i_recordingInvalidate();
}

/**
 * Invalidates the recording configuration.
 *
 * @returns VBox status code.
 *
 * @note    Takes the display's write lock.
 */
int Display::i_recordingInvalidate(void)
{
    LogFlowFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Invalidate screens.
     */
    for (unsigned uScreen = 0; uScreen < mcMonitors; uScreen++)
    {
        DISPLAYFBINFO const *pFBInfo = &maFramebuffers[uScreen];
        /* ignore rc */ i_recordingScreenChanged(uScreen, pFBInfo);
    }

    return VINF_SUCCESS;
}

/**
 * Called when the recording state of a screen got changed.
 *
 * @returns VBox status code.
 * @param   uScreenId           ID of screen for which the recording state got changed.
 * @param   pFBInfo             Frame buffer information to use.
 */
int Display::i_recordingScreenChanged(unsigned uScreenId, const DISPLAYFBINFO *pFBInfo)
{
    RecordingContext *pCtx = Recording.pCtx;

    Log2Func(("uScreenId=%u, w=%u, h=%u, bpp=%u\n", uScreenId, pFBInfo->w, pFBInfo->h, pFBInfo->u16BitsPerPixel));

    if (   !pCtx->IsFeatureEnabled(uScreenId, RecordingFeature_Video)
        || !pFBInfo->pu8FramebufferVRAM)
    {
        /* Skip recording this screen. */
        return VINF_SUCCESS;
    }

    if (uScreenId == 0xFFFFFFFF /* SVGA_ID_INVALID -- The old register interface is single screen only */)
        uScreenId = VBOX_VIDEO_PRIMARY_SCREEN;

    AssertReturn(uScreenId < mcMonitors, VERR_INVALID_PARAMETER);
    AssertReturn(pFBInfo->w, VERR_INVALID_PARAMETER);
    AssertReturn(pFBInfo->h, VERR_INVALID_PARAMETER);
    AssertReturn(pFBInfo->u16BitsPerPixel && pFBInfo->u16BitsPerPixel % 8 == 0, VERR_INVALID_PARAMETER);
    AssertReturn(pFBInfo->u32LineSize, VERR_INVALID_PARAMETER);

    i_updateDeviceCursorCapabilities();

    RECORDINGSURFACEINFO ScreenInfo;
    ScreenInfo.uWidth      = pFBInfo->w;
    ScreenInfo.uHeight     = pFBInfo->h;
    ScreenInfo.uBPP        = pFBInfo->u16BitsPerPixel;
    ScreenInfo.enmPixelFmt = RECORDINGPIXELFMT_BRGA32; /** @todo Does this apply everywhere? */

    uint64_t const tsNowMs = pCtx->GetCurrentPTS();

    int vrc = pCtx->SendScreenChange(uScreenId, &ScreenInfo, tsNowMs);
    if (RT_SUCCESS(vrc))
    {
        /* Make sure that we get the latest mouse pointer shape required for recording. */
        MousePointerData pointerData;
        mParent->i_getMouse()->i_getPointerShape(pointerData);
        mParent->i_recordingCursorShapeChange(pointerData.fVisible, pointerData.fAlpha,
                                              pointerData.hotX, pointerData.hotY,
                                              pointerData.width, pointerData.height,
                                              pointerData.pu8Shape, pointerData.cbShape);
        /* Send the full screen update. */
        vrc = i_recordingScreenUpdate(uScreenId, pFBInfo->pu8FramebufferVRAM, pFBInfo->h * pFBInfo->u32LineSize,
                                      0, 0, pFBInfo->w, pFBInfo->h, pFBInfo->u32LineSize);
    }

    Log2Func(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/**
 * Sends a screen update to the recording facility.
 *
 * @returns VBox status code.
 * @param   uScreenId           ID of screen.
 * @param   pauFramebuffer      Pointer to start of framebuffer.
 * @param   cbFramebuffer       Size (in bytes) of \a pauFramebuffer.
 * @param   x                   Absolute X starting position (in pixel) within current framebuffer.
 * @param   y                   Absolute Y starting position (in pixel) within current framebuffer.
 * @param   w                   Width (in pixel) of the changed area.
 * @param   h                   Height (in pixel) of the changed area.
 * @param   uBytesPerLine       Bytes per line of current framebuffer.
 */
int Display::i_recordingScreenUpdate(unsigned uScreenId, uint8_t *pauFramebuffer, size_t cbFramebuffer,
                                     uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t uBytesPerLine)
{
    RT_NOREF(cbFramebuffer);

    RecordingContext *pCtx = Recording.pCtx;

    Log2Func(("uScreenId=%u, pauFramebuffer=%p, cbFramebuffer=%zu, x=%RU32, y=%RU32, w=%RU32, h=%RU32, uBytesPerLine=%RU32\n",
              uScreenId, pauFramebuffer, cbFramebuffer, x, y, w, h, uBytesPerLine));

    if (uScreenId == 0xFFFFFFFF /* SVGA_ID_INVALID -- The old register interface is single screen only */)
        uScreenId = VBOX_VIDEO_PRIMARY_SCREEN;

    if (   !pCtx->IsFeatureEnabled(uScreenId, RecordingFeature_Video)
        || !pauFramebuffer)
        return VINF_SUCCESS;

#ifdef VBOX_STRICT /* Skipped in release build for speed reasons. */
    AssertReturn   (cbFramebuffer,  VERR_INVALID_PARAMETER);
    AssertReturn   (uBytesPerLine,  VERR_INVALID_PARAMETER);
    AssertReturn   (w,              VERR_INVALID_PARAMETER);
    AssertReturn   (h,              VERR_INVALID_PARAMETER);
#endif

    uint64_t const tsNowMs = pCtx->GetCurrentPTS();

    STAM_PROFILE_START(&Stats.Monitor[uScreenId].Recording.profileRecording, a);

    uint8_t const uBytesPerPixel = 4;
    size_t  const offFrame = (y * uBytesPerLine) + (x * uBytesPerPixel);
    size_t  const cbFrame  = w * h * uBytesPerPixel;

    RECORDINGVIDEOFRAME Frame =
    {
        { w, h, 32 /* BPP */, RECORDINGPIXELFMT_BRGA32, uBytesPerLine },
        pauFramebuffer + offFrame, cbFrame,
        { x, y }
    };

#if 0
    RecordingUtilsDbgDumpImageData(pauFramebuffer + offFrame, cbFramebuffer,
                                   "/tmp/recording", "display-screen-update", w, h, uBytesPerLine, 32 /* BPP */);
#endif

    int const vrc = pCtx->SendVideoFrame(uScreenId, &Frame, tsNowMs);

    STAM_PROFILE_STOP(&Stats.Monitor[uScreenId].Recording.profileRecording, a);

    Log2Func(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/**
 * Sends a screen upate to the recording facility using the current framebuffer.
 *
 * @returns VBox status code.
 * @param   uScreenId           ID of screen.
 * @param   x                   Absolute X starting position (in pixel) within current framebuffer.
 * @param   y                   Absolute Y starting position (in pixel) within current framebuffer.
 * @param   w                   Width (in pixel) of the changed area.
 * @param   h                   Height (in pixel) of the changed area.
 */
int Display::i_recordingScreenUpdate(unsigned uScreenId, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    RecordingContext *pCtx = Recording.pCtx;

    if (!pCtx->IsFeatureEnabled(uScreenId, RecordingFeature_Video))
        return VINF_SUCCESS;

    BYTE *pbAddress;
    ULONG ulWidth;
    ULONG ulHeight;
    ULONG ulBitsPerPixel;
    ULONG ulBytesPerLine;

    if (   uScreenId == VBOX_VIDEO_PRIMARY_SCREEN /* Take a shortcut for the primary screen. */
        && mpDrv)
    {
        pbAddress      = mpDrv->IConnector.pbData;
        ulWidth        = mpDrv->IConnector.cx;
        ulHeight       = mpDrv->IConnector.cy;
        ulBitsPerPixel = mpDrv->IConnector.cBits;
        ulBytesPerLine = mpDrv->IConnector.cbScanline;
    }
    else
    {
        BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;
        maFramebuffers[uScreenId].pSourceBitmap->QueryBitmapInfo(&pbAddress,
                                                                 &ulWidth,
                                                                 &ulHeight,
                                                                 &ulBitsPerPixel,
                                                                 &ulBytesPerLine,
                                                                 &bitmapFormat);
    }

    Log2Func(("uScreenId=%u, pbAddress=%p, ulWidth=%RU32, ulHeight=%RU32, ulBitsPerPixel=%RU32\n",
              uScreenId, pbAddress, ulWidth, ulHeight, ulBitsPerPixel));

    int const vrc = i_recordingScreenUpdate(uScreenId,
                                            pbAddress, ulHeight * ulBytesPerLine,
                                            x, y, w, h, ulBytesPerLine);
    Log2Func(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/**
 * Called when the mouse cursor position has changed within the guest.
 *
 * @returns VBox status code.
 * @param   uScreenId           ID of screen.
 * @param   fFlags              Position flags. Not used yet.
 * @param   x                   X position of the mouse cursor (within guest).
 * @param   y                   Y position of the mouse cursor (within guest).
 *
 * @note    Requires Guest Additions installed + mouse integration enabled.
 */
int Display::i_recordingCursorPositionChange(unsigned uScreenId, uint32_t fFlags, int32_t x, int32_t y)
{
    RT_NOREF(fFlags);

    RecordingContext *pCtx = Recording.pCtx;

    Log2Func(("uScreenId=%u, fFlags=%#x, x=%RI32, y=%RI32\n", uScreenId, fFlags, x, y));

    if (uScreenId == 0xFFFFFFFF /* SVGA_ID_INVALID -- The old register interface is single screen only */)
        uScreenId = VBOX_VIDEO_PRIMARY_SCREEN;

    if (!pCtx->IsFeatureEnabled(uScreenId, RecordingFeature_Video))
        return VINF_SUCCESS;

    return pCtx->SendCursorPositionChange(uScreenId, x, y, pCtx->GetCurrentPTS());
}
#endif /* VBOX_WITH_RECORDING */

/*static*/ DECLCALLBACK(int)
Display::i_drawToScreenEMT(Display *pDisplay, ULONG aScreenId, BYTE *address, ULONG x, ULONG y, ULONG width, ULONG height)
{
    int vrc = VINF_SUCCESS;

    DISPLAYFBINFO *pFBInfo = &pDisplay->maFramebuffers[aScreenId];

    if (aScreenId == VBOX_VIDEO_PRIMARY_SCREEN)
    {
        vrc = pDisplay->mpDrv->pUpPort->pfnDisplayBlt(pDisplay->mpDrv->pUpPort, address, x, y, width, height);
    }
    else if (aScreenId < pDisplay->mcMonitors)
    {
        /* Copy the bitmap to the guest VRAM. */
        const uint8_t *pu8Src       = address;
        int32_t xSrc                = 0;
        int32_t ySrc                = 0;
        uint32_t u32SrcWidth        = width;
        uint32_t u32SrcHeight       = height;
        uint32_t u32SrcLineSize     = width * 4;
        uint32_t u32SrcBitsPerPixel = 32;

        uint8_t *pu8Dst             = pFBInfo->pu8FramebufferVRAM;
        int32_t xDst                = x;
        int32_t yDst                = y;
        uint32_t u32DstWidth        = pFBInfo->w;
        uint32_t u32DstHeight       = pFBInfo->h;
        uint32_t u32DstLineSize     = pFBInfo->u32LineSize;
        uint32_t u32DstBitsPerPixel = pFBInfo->u16BitsPerPixel;

        vrc = pDisplay->mpDrv->pUpPort->pfnCopyRect(pDisplay->mpDrv->pUpPort,
                                                    width, height,
                                                    pu8Src,
                                                    xSrc, ySrc,
                                                    u32SrcWidth, u32SrcHeight,
                                                    u32SrcLineSize, u32SrcBitsPerPixel,
                                                    pu8Dst,
                                                    xDst, yDst,
                                                    u32DstWidth, u32DstHeight,
                                                    u32DstLineSize, u32DstBitsPerPixel);
        if (RT_SUCCESS(vrc))
        {
            if (!pFBInfo->pSourceBitmap.isNull())
            {
                /* Update the changed screen area. When source bitmap uses VRAM directly, just notify
                 * frontend to update. And for default format, render the guest VRAM to the source bitmap.
                 */
                if (   pFBInfo->fDefaultFormat
                    && !pFBInfo->fDisabled)
                {
                    BYTE *pAddress = NULL;
                    ULONG ulWidth = 0;
                    ULONG ulHeight = 0;
                    ULONG ulBitsPerPixel = 0;
                    ULONG ulBytesPerLine = 0;
                    BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

                    HRESULT hrc = pFBInfo->pSourceBitmap->QueryBitmapInfo(&pAddress,
                                                                          &ulWidth,
                                                                          &ulHeight,
                                                                          &ulBitsPerPixel,
                                                                          &ulBytesPerLine,
                                                                          &bitmapFormat);
                    if (SUCCEEDED(hrc))
                    {
                        pu8Src       = pFBInfo->pu8FramebufferVRAM;
                        xSrc                = x;
                        ySrc                = y;
                        u32SrcWidth        = pFBInfo->w;
                        u32SrcHeight       = pFBInfo->h;
                        u32SrcLineSize     = pFBInfo->u32LineSize;
                        u32SrcBitsPerPixel = pFBInfo->u16BitsPerPixel;

                        /* Default format is 32 bpp. */
                        pu8Dst             = pAddress;
                        xDst                = xSrc;
                        yDst                = ySrc;
                        u32DstWidth        = u32SrcWidth;
                        u32DstHeight       = u32SrcHeight;
                        u32DstLineSize     = u32DstWidth * 4;
                        u32DstBitsPerPixel = 32;

                        pDisplay->mpDrv->pUpPort->pfnCopyRect(pDisplay->mpDrv->pUpPort,
                                                              width, height,
                                                              pu8Src,
                                                              xSrc, ySrc,
                                                              u32SrcWidth, u32SrcHeight,
                                                              u32SrcLineSize, u32SrcBitsPerPixel,
                                                              pu8Dst,
                                                              xDst, yDst,
                                                              u32DstWidth, u32DstHeight,
                                                              u32DstLineSize, u32DstBitsPerPixel);
                    }
                }
            }

            pDisplay->i_handleDisplayUpdate(aScreenId, x, y, width, height);
        }
    }
    else
    {
        vrc = VERR_INVALID_PARAMETER;
    }

    if (RT_SUCCESS(vrc))
        pDisplay->mParent->i_consoleVRDPServer()->SendUpdateBitmap(aScreenId, x, y, width, height);

    return vrc;
}

HRESULT Display::drawToScreen(ULONG aScreenId, BYTE *aAddress, ULONG aX, ULONG aY, ULONG aWidth, ULONG aHeight)
{
    /// @todo (r=dmik) this function may take too long to complete if the VM
    //  is doing something like saving state right now. Which, in case if it
    //  is called on the GUI thread, will make it unresponsive. We should
    //  check the machine state here (by enclosing the check and VMRequCall
    //  within the Console lock to make it atomic).

    LogRelFlowFunc(("aAddress=%p, x=%d, y=%d, width=%d, height=%d\n",
                   (void *)aAddress, aX, aY, aWidth, aHeight));

    CheckComArgExpr(aWidth, aWidth != 0);
    CheckComArgExpr(aHeight, aHeight != 0);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    CHECK_CONSOLE_DRV(mpDrv);

    Console::SafeVMPtr ptrVM(mParent);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    /* Release lock because the call scheduled on EMT may also try to take it. */
    alock.release();

    /*
     * Again we're lazy and make the graphics device do all the
     * dirty conversion work.
     */
    int vrc = ptrVM.vtable()->pfnVMR3ReqCallWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)Display::i_drawToScreenEMT, 7,
                                                  this, aScreenId, aAddress, aX, aY, aWidth, aHeight);

    /*
     * If the function returns not supported, we'll have to do all the
     * work ourselves using the framebuffer.
     */
    HRESULT hrc = S_OK;
    if (vrc == VERR_NOT_SUPPORTED || vrc == VERR_NOT_IMPLEMENTED)
    {
        /** @todo implement generic fallback for screen blitting. */
        hrc = E_NOTIMPL;
    }
    else if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not draw to the screen (%Rrc)"), vrc);
/// @todo
//    else
//    {
//        /* All ok. Redraw the screen. */
//        handleDisplayUpdate(x, y, width, height);
//    }

    LogRelFlowFunc(("hrc=%Rhrc\n", hrc));
    return hrc;
}

/** @todo r=bird: cannot quite see why this would be required to run on an
 *        EMT any more.  It's not an issue in the COM methods, but for the
 *        VGA device interface it is an issue, see querySourceBitmap. */
/*static*/ DECLCALLBACK(int) Display::i_InvalidateAndUpdateEMT(Display *pDisplay, unsigned uId, bool fUpdateAll)
{
    LogRelFlowFunc(("uId=%d, fUpdateAll %d\n", uId, fUpdateAll));

    unsigned uScreenId;
    for (uScreenId = (fUpdateAll ? 0 : uId); uScreenId < pDisplay->mcMonitors; uScreenId++)
    {
        DISPLAYFBINFO *pFBInfo = &pDisplay->maFramebuffers[uScreenId];

        if (   !pFBInfo->fVBVAEnabled
            && uScreenId == VBOX_VIDEO_PRIMARY_SCREEN)
            pDisplay->mpDrv->pUpPort->pfnUpdateDisplayAll(pDisplay->mpDrv->pUpPort, /* fFailOnResize = */ true);
        else
        {
            if (!pFBInfo->fDisabled)
            {
                /* Render complete VRAM screen to the framebuffer.
                 * When framebuffer uses VRAM directly, just notify it to update.
                 */
                if (pFBInfo->fDefaultFormat && !pFBInfo->pSourceBitmap.isNull())
                {
                    BYTE *pAddress = NULL;
                    ULONG ulWidth = 0;
                    ULONG ulHeight = 0;
                    ULONG ulBitsPerPixel = 0;
                    ULONG ulBytesPerLine = 0;
                    BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

                    HRESULT hrc = pFBInfo->pSourceBitmap->QueryBitmapInfo(&pAddress,
                                                                          &ulWidth,
                                                                          &ulHeight,
                                                                          &ulBitsPerPixel,
                                                                          &ulBytesPerLine,
                                                                          &bitmapFormat);
                    if (SUCCEEDED(hrc))
                    {
                        uint32_t width              = pFBInfo->w;
                        uint32_t height             = pFBInfo->h;

                        const uint8_t *pu8Src       = pFBInfo->pu8FramebufferVRAM;
                        int32_t xSrc                = 0;
                        int32_t ySrc                = 0;
                        uint32_t u32SrcWidth        = pFBInfo->w;
                        uint32_t u32SrcHeight       = pFBInfo->h;
                        uint32_t u32SrcLineSize     = pFBInfo->u32LineSize;
                        uint32_t u32SrcBitsPerPixel = pFBInfo->u16BitsPerPixel;

                        /* Default format is 32 bpp. */
                        uint8_t *pu8Dst             = pAddress;
                        int32_t xDst                = xSrc;
                        int32_t yDst                = ySrc;
                        uint32_t u32DstWidth        = u32SrcWidth;
                        uint32_t u32DstHeight       = u32SrcHeight;
                        uint32_t u32DstLineSize     = u32DstWidth * 4;
                        uint32_t u32DstBitsPerPixel = 32;

                        /* if uWidth != pFBInfo->w and uHeight != pFBInfo->h
                         * implies resize of Framebuffer is in progress and
                         * copyrect should not be called.
                         */
                        if (ulWidth == pFBInfo->w && ulHeight == pFBInfo->h)
                            pDisplay->mpDrv->pUpPort->pfnCopyRect(pDisplay->mpDrv->pUpPort,
                                                                  width, height,
                                                                  pu8Src,
                                                                  xSrc, ySrc,
                                                                  u32SrcWidth, u32SrcHeight,
                                                                  u32SrcLineSize, u32SrcBitsPerPixel,
                                                                  pu8Dst,
                                                                  xDst, yDst,
                                                                  u32DstWidth, u32DstHeight,
                                                                  u32DstLineSize, u32DstBitsPerPixel);
                    }
                }

                pDisplay->i_handleDisplayUpdate(uScreenId, 0, 0, pFBInfo->w, pFBInfo->h);
            }
        }
        if (!fUpdateAll)
            break;
    }
    LogRelFlowFunc(("done\n"));
    return VINF_SUCCESS;
}

/**
 * Does a full invalidation of the VM display and instructs the VM
 * to update it immediately.
 *
 * @returns COM status code
 */

HRESULT Display::invalidateAndUpdate()
{
    LogRelFlowFunc(("\n"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    CHECK_CONSOLE_DRV(mpDrv);

    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        LogRelFlowFunc(("Sending DPYUPDATE request\n"));

        /* Have to release the lock when calling EMT.  */
        alock.release();

        int vrc = ptrVM.vtable()->pfnVMR3ReqCallNoWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)Display::i_InvalidateAndUpdateEMT, 3,
                                                        this, 0, true);
        alock.acquire();

        if (RT_FAILURE(vrc))
            hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not invalidate and update the screen (%Rrc)"), vrc);
    }

    LogRelFlowFunc(("hrc=%Rhrc\n", hrc));
    return hrc;
}

HRESULT Display::invalidateAndUpdateScreen(ULONG aScreenId)
{
    LogRelFlowFunc(("\n"));

    Console::SafeVMPtr ptrVM(mParent);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        int vrc = ptrVM.vtable()->pfnVMR3ReqCallNoWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)Display::i_InvalidateAndUpdateEMT, 3,
                                                        this, aScreenId, false);
        if (RT_FAILURE(vrc))
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("Could not invalidate and update the screen %d (%Rrc)"), aScreenId, vrc);
    }

    LogRelFlowFunc(("hrc=%Rhrc\n", hrc));
    return hrc;
}

HRESULT Display::completeVHWACommand(BYTE *aCommand)
{
#ifdef VBOX_WITH_VIDEOHWACCEL
    AssertPtr(mpDrv->pVBVACallbacks);
    mpDrv->pVBVACallbacks->pfnVHWACommandCompleteAsync(mpDrv->pVBVACallbacks, (VBOXVHWACMD RT_UNTRUSTED_VOLATILE_GUEST *)aCommand);
    return S_OK;
#else
    RT_NOREF(aCommand);
    return E_NOTIMPL;
#endif
}

HRESULT Display::viewportChanged(ULONG aScreenId, ULONG aX, ULONG aY, ULONG aWidth, ULONG aHeight)
{
    AssertMsgReturn(aScreenId < mcMonitors, ("aScreendId=%d mcMonitors=%d\n", aScreenId, mcMonitors), E_INVALIDARG);

    /* The driver might not have been constructed yet */
    if (mpDrv && mpDrv->pUpPort->pfnSetViewport)
        mpDrv->pUpPort->pfnSetViewport(mpDrv->pUpPort, aScreenId, aX, aY, aWidth, aHeight);

    return S_OK;
}

HRESULT Display::querySourceBitmap(ULONG aScreenId,
                                   ComPtr<IDisplaySourceBitmap> &aDisplaySourceBitmap)
{
    LogRelFlowFunc(("aScreenId = %d\n", aScreenId));

    Console::SafeVMPtr ptrVM(mParent);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    CHECK_CONSOLE_DRV(mpDrv);

    bool fSetRenderVRAM = false;
    bool fInvalidate = false;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aScreenId >= mcMonitors)
        return setError(E_INVALIDARG, tr("QuerySourceBitmap: Invalid screen %d (total %d)"), aScreenId, mcMonitors);

    if (!mfSourceBitmapEnabled)
    {
        aDisplaySourceBitmap = NULL;
        return E_FAIL;
    }

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[aScreenId];

    /* No source bitmap for a blank guest screen. */
    if (pFBInfo->flags & VBVA_SCREEN_F_BLANK)
    {
        aDisplaySourceBitmap = NULL;
        return E_FAIL;
    }

    HRESULT hr = S_OK;

    if (pFBInfo->pSourceBitmap.isNull())
    {
        /* Create a new object. */
        ComObjPtr<DisplaySourceBitmap> obj;
        hr = obj.createObject();
        if (SUCCEEDED(hr))
            hr = obj->init(this, aScreenId, pFBInfo);

        if (SUCCEEDED(hr))
        {
            pFBInfo->pSourceBitmap = obj;
            pFBInfo->fDefaultFormat = !obj->i_usesVRAM();

            if (aScreenId == VBOX_VIDEO_PRIMARY_SCREEN)
            {
                /* Start buffer updates. */
                BYTE *pAddress = NULL;
                ULONG ulWidth = 0;
                ULONG ulHeight = 0;
                ULONG ulBitsPerPixel = 0;
                ULONG ulBytesPerLine = 0;
                BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

                pFBInfo->pSourceBitmap->QueryBitmapInfo(&pAddress,
                                                        &ulWidth,
                                                        &ulHeight,
                                                        &ulBitsPerPixel,
                                                        &ulBytesPerLine,
                                                        &bitmapFormat);

                mpDrv->IConnector.pbData     = pAddress;
                mpDrv->IConnector.cbScanline = ulBytesPerLine;
                mpDrv->IConnector.cBits      = ulBitsPerPixel;
                mpDrv->IConnector.cx         = ulWidth;
                mpDrv->IConnector.cy         = ulHeight;

                fSetRenderVRAM = pFBInfo->fDefaultFormat;
            }

            /* Make sure that the bitmap contains the latest image. */
            fInvalidate = pFBInfo->fDefaultFormat;
        }
    }

    if (SUCCEEDED(hr))
    {
        pFBInfo->pSourceBitmap.queryInterfaceTo(aDisplaySourceBitmap.asOutParam());
    }

    /* Leave the IDisplay lock because the VGA device must not be called under it. */
    alock.release();

    if (SUCCEEDED(hr))
    {
        if (fSetRenderVRAM)
            mpDrv->pUpPort->pfnSetRenderVRAM(mpDrv->pUpPort, true);

        if (fInvalidate)
#if 1 /* bird: Cannot see why this needs to run on an EMT. It deadlocks now with timer callback moving to non-EMT worker threads. */
            Display::i_InvalidateAndUpdateEMT(this, aScreenId, false /*fUpdateAll*/);
#else
            VMR3ReqCallWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)Display::i_InvalidateAndUpdateEMT,
                             3, this, aScreenId, false);
#endif
    }

    LogRelFlowFunc(("%Rhrc\n", hr));
    return hr;
}

HRESULT Display::getGuestScreenLayout(std::vector<ComPtr<IGuestScreenInfo> > &aGuestScreenLayout)
{
    NOREF(aGuestScreenLayout);
    return E_NOTIMPL;
}

HRESULT Display::setScreenLayout(ScreenLayoutMode_T aScreenLayoutMode,
                                 const std::vector<ComPtr<IGuestScreenInfo> > &aGuestScreenInfo)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (aGuestScreenInfo.size() != mcMonitors)
        return E_INVALIDARG;

    CHECK_CONSOLE_DRV(mpDrv);

    /*
     * It is up to the guest to decide whether the hint is
     * valid. Therefore don't do any VRAM sanity checks here.
     */

    /* Have to release the lock because the pfnRequestDisplayChange
     * will call EMT.  */
    alock.release();

    VMMDev *pVMMDev = mParent->i_getVMMDev();
    if (pVMMDev)
    {
        PPDMIVMMDEVPORT pVMMDevPort = pVMMDev->getVMMDevPort();
        if (pVMMDevPort)
        {
            uint32_t const cDisplays = (uint32_t)aGuestScreenInfo.size();

            size_t const cbAlloc = cDisplays * sizeof(VMMDevDisplayDef);
            VMMDevDisplayDef *paDisplayDefs = (VMMDevDisplayDef *)RTMemAlloc(cbAlloc);
            if (paDisplayDefs)
            {
                for (uint32_t i = 0; i < cDisplays; ++i)
                {
                    VMMDevDisplayDef *p = &paDisplayDefs[i];
                    ComPtr<IGuestScreenInfo> pScreenInfo = aGuestScreenInfo[i];

                    ULONG screenId     = 0;
                    GuestMonitorStatus_T guestMonitorStatus = GuestMonitorStatus_Enabled;
                    BOOL  origin       = FALSE;
                    BOOL  primary      = FALSE;
                    LONG  originX      = 0;
                    LONG  originY      = 0;
                    ULONG width        = 0;
                    ULONG height       = 0;
                    ULONG bitsPerPixel = 0;

                    pScreenInfo->COMGETTER(ScreenId)    (&screenId);
                    pScreenInfo->COMGETTER(GuestMonitorStatus)(&guestMonitorStatus);
                    pScreenInfo->COMGETTER(Primary)     (&primary);
                    pScreenInfo->COMGETTER(Origin)      (&origin);
                    pScreenInfo->COMGETTER(OriginX)     (&originX);
                    pScreenInfo->COMGETTER(OriginY)     (&originY);
                    pScreenInfo->COMGETTER(Width)       (&width);
                    pScreenInfo->COMGETTER(Height)      (&height);
                    pScreenInfo->COMGETTER(BitsPerPixel)(&bitsPerPixel);

                    LogFlowFunc(("%d %d,%d %dx%d\n", screenId, originX, originY, width, height));

                    p->idDisplay     = screenId;
                    p->xOrigin       = originX;
                    p->yOrigin       = originY;
                    p->cx            = width;
                    p->cy            = height;
                    p->cBitsPerPixel = bitsPerPixel;
                    p->fDisplayFlags = VMMDEV_DISPLAY_CX | VMMDEV_DISPLAY_CY | VMMDEV_DISPLAY_BPP;
                    if (guestMonitorStatus == GuestMonitorStatus_Disabled)
                        p->fDisplayFlags |= VMMDEV_DISPLAY_DISABLED;
                    if (origin)
                        p->fDisplayFlags |= VMMDEV_DISPLAY_ORIGIN;
                    if (primary)
                        p->fDisplayFlags |= VMMDEV_DISPLAY_PRIMARY;
                }

                bool const fForce =    aScreenLayoutMode == ScreenLayoutMode_Reset
                                    || aScreenLayoutMode == ScreenLayoutMode_Apply;
                bool const fNotify = aScreenLayoutMode != ScreenLayoutMode_Silent;
                pVMMDevPort->pfnRequestDisplayChange(pVMMDevPort, cDisplays, paDisplayDefs, fForce, fNotify);

                RTMemFree(paDisplayDefs);
            }
        }
    }
    return S_OK;
}

HRESULT Display::detachScreens(const std::vector<LONG> &aScreenIds)
{
    NOREF(aScreenIds);
    return E_NOTIMPL;
}

HRESULT Display::createGuestScreenInfo(ULONG aDisplay,
                                       GuestMonitorStatus_T aStatus,
                                       BOOL aPrimary,
                                       BOOL aChangeOrigin,
                                       LONG aOriginX,
                                       LONG aOriginY,
                                       ULONG aWidth,
                                       ULONG aHeight,
                                       ULONG aBitsPerPixel,
                                       ComPtr<IGuestScreenInfo> &aGuestScreenInfo)
{
    /* Create a new object. */
    ComObjPtr<GuestScreenInfo> obj;
    HRESULT hr = obj.createObject();
    if (SUCCEEDED(hr))
        hr = obj->init(aDisplay, aStatus, aPrimary, aChangeOrigin, aOriginX, aOriginY,
                       aWidth, aHeight, aBitsPerPixel);
    if (SUCCEEDED(hr))
        obj.queryInterfaceTo(aGuestScreenInfo.asOutParam());

    return hr;
}


/*
 * GuestScreenInfo implementation.
 */
DEFINE_EMPTY_CTOR_DTOR(GuestScreenInfo)

HRESULT GuestScreenInfo::FinalConstruct()
{
    return BaseFinalConstruct();
}

void GuestScreenInfo::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT GuestScreenInfo::init(ULONG aDisplay,
                              GuestMonitorStatus_T aGuestMonitorStatus,
                              BOOL aPrimary,
                              BOOL aChangeOrigin,
                              LONG aOriginX,
                              LONG aOriginY,
                              ULONG aWidth,
                              ULONG aHeight,
                              ULONG aBitsPerPixel)
{
    LogFlowThisFunc(("[%u]\n", aDisplay));

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mScreenId = aDisplay;
    mGuestMonitorStatus = aGuestMonitorStatus;
    mPrimary = aPrimary;
    mOrigin = aChangeOrigin;
    mOriginX =  aOriginX;
    mOriginY = aOriginY;
    mWidth = aWidth;
    mHeight = aHeight;
    mBitsPerPixel = aBitsPerPixel;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

void GuestScreenInfo::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFunc(("[%u]\n", mScreenId));
}

HRESULT GuestScreenInfo::getScreenId(ULONG *aScreenId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aScreenId = mScreenId;
    return S_OK;
}

HRESULT GuestScreenInfo::getGuestMonitorStatus(GuestMonitorStatus_T *aGuestMonitorStatus)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aGuestMonitorStatus = mGuestMonitorStatus;
    return S_OK;
}

HRESULT GuestScreenInfo::getPrimary(BOOL *aPrimary)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aPrimary = mPrimary;
    return S_OK;
}

HRESULT GuestScreenInfo::getOrigin(BOOL *aOrigin)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aOrigin = mOrigin;
    return S_OK;
}

HRESULT GuestScreenInfo::getOriginX(LONG *aOriginX)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aOriginX = mOriginX;
    return S_OK;
}

HRESULT GuestScreenInfo::getOriginY(LONG *aOriginY)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aOriginY = mOriginY;
    return S_OK;
}

HRESULT GuestScreenInfo::getWidth(ULONG *aWidth)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aWidth = mWidth;
    return S_OK;
}

HRESULT GuestScreenInfo::getHeight(ULONG *aHeight)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aHeight = mHeight;
    return S_OK;
}

HRESULT GuestScreenInfo::getBitsPerPixel(ULONG *aBitsPerPixel)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    *aBitsPerPixel = mBitsPerPixel;
    return S_OK;
}

HRESULT GuestScreenInfo::getExtendedInfo(com::Utf8Str &aExtendedInfo)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aExtendedInfo = com::Utf8Str();
    return S_OK;
}

// wrapped IEventListener method
HRESULT Display::handleEvent(const ComPtr<IEvent> &aEvent)
{
    VBoxEventType_T aType = VBoxEventType_Invalid;

    aEvent->COMGETTER(Type)(&aType);
    switch (aType)
    {
        case VBoxEventType_OnStateChanged:
        {
            ComPtr<IStateChangedEvent> scev = aEvent;
            Assert(scev);
            MachineState_T machineState;
            scev->COMGETTER(State)(&machineState);
            if (   machineState == MachineState_Running
                || machineState == MachineState_Teleporting
                || machineState == MachineState_LiveSnapshotting
                || machineState == MachineState_DeletingSnapshotOnline
                   )
            {
                LogRelFlowFunc(("Machine is running.\n"));

            }
            break;
        }
        default:
            AssertFailed();
    }

    return S_OK;
}


// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Handle display resize event issued by the VGA device for the primary screen.
 *
 * @see PDMIDISPLAYCONNECTOR::pfnResize
 */
DECLCALLBACK(int) Display::i_displayResizeCallback(PPDMIDISPLAYCONNECTOR pInterface,
                                                   uint32_t bpp, void *pvVRAM, uint32_t cbLine, uint32_t cx, uint32_t cy)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;

    LogRelFlowFunc(("bpp %d, pvVRAM %p, cbLine %d, cx %d, cy %d\n",
                  bpp, pvVRAM, cbLine, cx, cy));

    bool f = ASMAtomicCmpXchgBool(&pThis->fVGAResizing, true, false);
    if (!f)
    {
        /* This is a result of recursive call when the source bitmap is being updated
         * during a VGA resize. Tell the VGA device to ignore the call.
         *
         * @todo It is a workaround, actually pfnUpdateDisplayAll must
         * fail on resize.
         */
        LogRel(("displayResizeCallback: already processing\n"));
        return VINF_VGA_RESIZE_IN_PROGRESS;
    }

    int vrc = pThis->i_handleDisplayResize(VBOX_VIDEO_PRIMARY_SCREEN, bpp, pvVRAM, cbLine, cx, cy, 0, 0, 0, true);

    /* Restore the flag.  */
    f = ASMAtomicCmpXchgBool(&pThis->fVGAResizing, false, true);
    AssertRelease(f);

    return vrc;
}

/**
 * Handle display update.
 *
 * @see PDMIDISPLAYCONNECTOR::pfnUpdateRect
 */
DECLCALLBACK(void) Display::i_displayUpdateCallback(PPDMIDISPLAYCONNECTOR pInterface,
                                                    uint32_t x, uint32_t y, uint32_t cx, uint32_t cy)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);

#ifdef DEBUG_sunlover
    LogFlowFunc(("fVideoAccelEnabled = %d, %d,%d %dx%d\n",
                 pDrv->pDisplay->mVideoAccelLegacy.fVideoAccelEnabled, x, y, cx, cy));
#endif /* DEBUG_sunlover */

    /* This call does update regardless of VBVA status.
     * But in VBVA mode this is called only as result of
     * pfnUpdateDisplayAll in the VGA device.
     */

    pDrv->pDisplay->i_handleDisplayUpdate(VBOX_VIDEO_PRIMARY_SCREEN, x, y, cx, cy);
}

/**
 * Periodic display refresh callback.
 *
 * @see PDMIDISPLAYCONNECTOR::pfnRefresh
 * @thread EMT
 */
/*static*/ DECLCALLBACK(void) Display::i_displayRefreshCallback(PPDMIDISPLAYCONNECTOR pInterface)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);

    STAM_PROFILE_START(&pDisplay->Stats.profileDisplayRefreshCallback, a);

    Display *pDisplay = pDrv->pDisplay;
    unsigned uScreenId;

    int vrc = pDisplay->i_videoAccelRefreshProcess(pDrv->pUpPort);
    if (vrc != VINF_TRY_AGAIN) /* Means 'do nothing' here. */
    {
        if (vrc == VWRN_INVALID_STATE)
        {
            /* No VBVA do a display update. */
            pDrv->pUpPort->pfnUpdateDisplay(pDrv->pUpPort);
        }

        /* Inform the VRDP server that the current display update sequence is
         * completed. At this moment the framebuffer memory contains a definite
         * image, that is synchronized with the orders already sent to VRDP client.
         * The server can now process redraw requests from clients or initial
         * fullscreen updates for new clients.
         */
        for (uScreenId = 0; uScreenId < pDisplay->mcMonitors; uScreenId++)
        {
            Assert(pDisplay->mParent && pDisplay->mParent->i_consoleVRDPServer());
            pDisplay->mParent->i_consoleVRDPServer()->SendUpdate(uScreenId, NULL, 0);
        }
    }

    STAM_PROFILE_STOP(&pDisplay->Stats.profileDisplayRefreshCallback, a);
}

/**
 * Reset notification
 *
 * @see PDMIDISPLAYCONNECTOR::pfnReset
 */
DECLCALLBACK(void) Display::i_displayResetCallback(PPDMIDISPLAYCONNECTOR pInterface)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);

    LogRelFlowFunc(("\n"));

   /* Disable VBVA mode. */
    pDrv->pDisplay->VideoAccelEnableVGA(false, NULL);
}

/**
 * LFBModeChange notification
 *
 * @see PDMIDISPLAYCONNECTOR::pfnLFBModeChange
 */
DECLCALLBACK(void) Display::i_displayLFBModeChangeCallback(PPDMIDISPLAYCONNECTOR pInterface, bool fEnabled)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);

    LogRelFlowFunc(("fEnabled=%d\n", fEnabled));

    NOREF(fEnabled);

    /* Disable VBVA mode in any case. The guest driver reenables VBVA mode if necessary. */
    pDrv->pDisplay->VideoAccelEnableVGA(false, NULL);
}

/**
 * Adapter information change notification.
 *
 * @see PDMIDISPLAYCONNECTOR::pfnProcessAdapterData
 */
DECLCALLBACK(void) Display::i_displayProcessAdapterDataCallback(PPDMIDISPLAYCONNECTOR pInterface, void *pvVRAM,
                                                                uint32_t u32VRAMSize)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    pDrv->pDisplay->processAdapterData(pvVRAM, u32VRAMSize);
}

/**
 * Display information change notification.
 *
 * @see PDMIDISPLAYCONNECTOR::pfnProcessDisplayData
 */
DECLCALLBACK(void) Display::i_displayProcessDisplayDataCallback(PPDMIDISPLAYCONNECTOR pInterface,
                                                                void *pvVRAM, unsigned uScreenId)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    pDrv->pDisplay->processDisplayData(pvVRAM, uScreenId);
}

#ifdef VBOX_WITH_VIDEOHWACCEL

int Display::i_handleVHWACommandProcess(int enmCmd, bool fGuestCmd, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_GUEST *pCommand)
{
    /* bugref:9691 Disable the legacy VHWA interface.
     * Keep the host commands enabled because they are needed when an old saved state is loaded.
     */
    if (fGuestCmd)
        return VERR_NOT_IMPLEMENTED;

    unsigned id = (unsigned)pCommand->iDisplay;
    if (id >= mcMonitors)
        return VERR_INVALID_PARAMETER;

    ComPtr<IFramebuffer> pFramebuffer;
    AutoReadLock arlock(this COMMA_LOCKVAL_SRC_POS);
    pFramebuffer = maFramebuffers[id].pFramebuffer;
    bool fVHWASupported = RT_BOOL(maFramebuffers[id].u32Caps & FramebufferCapabilities_VHWA);
    arlock.release();

    if (pFramebuffer == NULL || !fVHWASupported)
        return VERR_NOT_IMPLEMENTED; /* Implementation is not available. */

    HRESULT hr = pFramebuffer->ProcessVHWACommand((BYTE *)pCommand, enmCmd, fGuestCmd);
    if (hr == S_FALSE)
        return VINF_SUCCESS;
    if (SUCCEEDED(hr))
        return VINF_CALLBACK_RETURN;
    if (hr == E_ACCESSDENIED)
        return VERR_INVALID_STATE; /* notify we can not handle request atm */
    if (hr == E_NOTIMPL)
        return VERR_NOT_IMPLEMENTED;
    return VERR_GENERAL_FAILURE;
}

DECLCALLBACK(int) Display::i_displayVHWACommandProcess(PPDMIDISPLAYCONNECTOR pInterface, int enmCmd, bool fGuestCmd,
                                                       VBOXVHWACMD RT_UNTRUSTED_VOLATILE_GUEST *pCommand)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);

    return pDrv->pDisplay->i_handleVHWACommandProcess(enmCmd, fGuestCmd, pCommand);
}

#endif /* VBOX_WITH_VIDEOHWACCEL */

int Display::i_handle3DNotifyProcess(VBOX3DNOTIFY *p3DNotify)
{
    unsigned const id = (unsigned)p3DNotify->iDisplay;
    if (id >= mcMonitors)
        return VERR_INVALID_PARAMETER;

    ComPtr<IFramebuffer> pFramebuffer;
    AutoReadLock arlock(this COMMA_LOCKVAL_SRC_POS);
    pFramebuffer = maFramebuffers[id].pFramebuffer;
    arlock.release();

    int vrc = VINF_SUCCESS;

    if (!pFramebuffer.isNull())
    {
        if (p3DNotify->enmNotification == VBOX3D_NOTIFY_TYPE_HW_OVERLAY_GET_ID)
        {
            LONG64 winId = 0;
            HRESULT hrc = pFramebuffer->COMGETTER(WinId)(&winId);
            if (SUCCEEDED(hrc))
            {
                *(uint64_t *)&p3DNotify->au8Data[0] = winId;
            }
            else
                vrc = VERR_NOT_SUPPORTED;
        }
        else
        {
            com::SafeArray<BYTE> data;
            data.initFrom((BYTE *)&p3DNotify->au8Data[0], p3DNotify->cbData);

            HRESULT hrc = pFramebuffer->Notify3DEvent((ULONG)p3DNotify->enmNotification, ComSafeArrayAsInParam(data));
            if (FAILED(hrc))
                vrc = VERR_NOT_SUPPORTED;
        }
    }
    else
        vrc = VERR_NOT_IMPLEMENTED;

    return vrc;
}

DECLCALLBACK(int) Display::i_display3DNotifyProcess(PPDMIDISPLAYCONNECTOR pInterface,
                                                    VBOX3DNOTIFY *p3DNotify)
{
    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    return pDrv->pDisplay->i_handle3DNotifyProcess(p3DNotify);
}

HRESULT Display::notifyScaleFactorChange(ULONG aScreenId, ULONG aScaleFactorWMultiplied, ULONG aScaleFactorHMultiplied)
{
    RT_NOREF(aScreenId, aScaleFactorWMultiplied, aScaleFactorHMultiplied);
# if 0 /** @todo Thank you so very much from anyone using VMSVGA3d!  */
    AssertMsgFailed(("Attempt to specify OpenGL content scale factor while 3D acceleration is disabled in VM config. Ignored.\n"));
# else
    /* Need an interface like this here (and the #ifdefs needs adjusting):
    PPDMIDISPLAYPORT pUpPort = mpDrv ? mpDrv->pUpPort : NULL;
    if (pUpPort && pUpPort->pfnSetScaleFactor)
        pUpPort->pfnSetScaleFactor(pUpPort, aScreeId, aScaleFactorWMultiplied, aScaleFactorHMultiplied); */
# endif
    return S_OK;
}

HRESULT Display::notifyHiDPIOutputPolicyChange(BOOL fUnscaledHiDPI)
{
    RT_NOREF(fUnscaledHiDPI);

    /* Need an interface like this here (and the #ifdefs needs adjusting):
    PPDMIDISPLAYPORT pUpPort = mpDrv ? mpDrv->pUpPort : NULL;
    if (pUpPort && pUpPort->pfnSetScaleFactor)
        pUpPort->pfnSetScaleFactor(pUpPort, aScreeId, aScaleFactorWMultiplied, aScaleFactorHMultiplied); */

    return S_OK;
}

#ifdef VBOX_WITH_HGSMI
/**
 * @interface_method_impl{PDMIDISPLAYCONNECTOR,pfnVBVAEnable}
 */
DECLCALLBACK(int) Display::i_displayVBVAEnable(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId,
                                               VBVAHOSTFLAGS RT_UNTRUSTED_VOLATILE_GUEST *pHostFlags)
{
    LogRelFlowFunc(("uScreenId %d\n", uScreenId));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;
    AssertReturn(uScreenId < pThis->mcMonitors, VERR_INVALID_PARAMETER);

    if (pThis->maFramebuffers[uScreenId].fVBVAEnabled)
    {
        LogRel(("Enabling different vbva mode\n"));
#ifdef DEBUG_misha
        AssertMsgFailed(("enabling different vbva mode\n"));
#endif
        return VERR_INVALID_STATE;
    }

    pThis->maFramebuffers[uScreenId].fVBVAEnabled = true;
    pThis->maFramebuffers[uScreenId].pVBVAHostFlags = pHostFlags;
    pThis->maFramebuffers[uScreenId].fVBVAForceResize = true;

    vbvaSetMemoryFlagsHGSMI(uScreenId, pThis->mfu32SupportedOrders, pThis->mfVideoAccelVRDP, &pThis->maFramebuffers[uScreenId]);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIDISPLAYCONNECTOR,pfnVBVADisable}
 */
DECLCALLBACK(void) Display::i_displayVBVADisable(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId)
{
    LogRelFlowFunc(("uScreenId %d\n", uScreenId));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;
    AssertReturnVoid(uScreenId < pThis->mcMonitors);

    DISPLAYFBINFO *pFBInfo = &pThis->maFramebuffers[uScreenId];

    if (uScreenId == VBOX_VIDEO_PRIMARY_SCREEN)
    {
        /* Make sure that the primary screen is visible now.
         * The guest can't use VBVA anymore, so only only the VGA device output works.
         */
        pFBInfo->flags = 0;
        if (pFBInfo->fDisabled)
        {
            pFBInfo->fDisabled = false;
            ::FireGuestMonitorChangedEvent(pThis->mParent->i_getEventSource(), GuestMonitorChangedEventType_Enabled, uScreenId,
                                           pFBInfo->xOrigin, pFBInfo->yOrigin, pFBInfo->w, pFBInfo->h);
        }
    }

    pFBInfo->fVBVAEnabled = false;
    pFBInfo->fVBVAForceResize = false;

    vbvaSetMemoryFlagsHGSMI(uScreenId, 0, false, pFBInfo);

    pFBInfo->pVBVAHostFlags = NULL;

    if (uScreenId == VBOX_VIDEO_PRIMARY_SCREEN)
    {
        /* Force full screen update, because VGA device must take control, do resize, etc. */
        pThis->mpDrv->pUpPort->pfnUpdateDisplayAll(pThis->mpDrv->pUpPort, /* fFailOnResize = */ false);
    }
}

DECLCALLBACK(void) Display::i_displayVBVAUpdateBegin(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId)
{
    RT_NOREF(uScreenId);
    LogFlowFunc(("uScreenId %d\n", uScreenId));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;

    if (ASMAtomicReadU32(&pThis->mu32UpdateVBVAFlags) > 0)
    {
        vbvaSetMemoryFlagsAllHGSMI(pThis->mfu32SupportedOrders, pThis->mfVideoAccelVRDP, pThis->maFramebuffers,
                                   pThis->mcMonitors);
        ASMAtomicDecU32(&pThis->mu32UpdateVBVAFlags);
    }
}

/**
 * @interface_method_impl{PDMIDISPLAYCONNECTOR,pfnVBVAUpdateProcess}
 */
DECLCALLBACK(void) Display::i_displayVBVAUpdateProcess(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId,
                                                       struct VBVACMDHDR const RT_UNTRUSTED_VOLATILE_GUEST *pCmd, size_t cbCmd)
{
    LogFlowFunc(("uScreenId %d pCmd %p cbCmd %d, @%d,%d %dx%d\n", uScreenId, pCmd, cbCmd, pCmd->x, pCmd->y, pCmd->w, pCmd->h));
    VBVACMDHDR hdrSaved;
    RT_COPY_VOLATILE(hdrSaved, *pCmd);
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;
    DISPLAYFBINFO *pFBInfo;
    AssertReturnVoid(uScreenId < pThis->mcMonitors);

    pFBInfo = &pThis->maFramebuffers[uScreenId];

    if (pFBInfo->fDefaultFormat)
    {
        /* Make sure that framebuffer contains the same image as the guest VRAM. */
        if (   uScreenId == VBOX_VIDEO_PRIMARY_SCREEN
            && !pFBInfo->fDisabled)
        {
            pDrv->pUpPort->pfnUpdateDisplayRect(pDrv->pUpPort, hdrSaved.x, hdrSaved.y, hdrSaved.w, hdrSaved.h);
        }
        else if (   !pFBInfo->pSourceBitmap.isNull()
                 && !pFBInfo->fDisabled)
        {
            /* Render VRAM content to the framebuffer. */
            BYTE *pAddress = NULL;
            ULONG ulWidth = 0;
            ULONG ulHeight = 0;
            ULONG ulBitsPerPixel = 0;
            ULONG ulBytesPerLine = 0;
            BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

            HRESULT hrc = pFBInfo->pSourceBitmap->QueryBitmapInfo(&pAddress,
                                                                  &ulWidth,
                                                                  &ulHeight,
                                                                  &ulBitsPerPixel,
                                                                  &ulBytesPerLine,
                                                                  &bitmapFormat);
            if (SUCCEEDED(hrc))
            {
                uint32_t width              = hdrSaved.w;
                uint32_t height             = hdrSaved.h;

                const uint8_t *pu8Src       = pFBInfo->pu8FramebufferVRAM;
                int32_t xSrc                = hdrSaved.x - pFBInfo->xOrigin;
                int32_t ySrc                = hdrSaved.y - pFBInfo->yOrigin;
                uint32_t u32SrcWidth        = pFBInfo->w;
                uint32_t u32SrcHeight       = pFBInfo->h;
                uint32_t u32SrcLineSize     = pFBInfo->u32LineSize;
                uint32_t u32SrcBitsPerPixel = pFBInfo->u16BitsPerPixel;

                uint8_t *pu8Dst             = pAddress;
                int32_t xDst                = xSrc;
                int32_t yDst                = ySrc;
                uint32_t u32DstWidth        = u32SrcWidth;
                uint32_t u32DstHeight       = u32SrcHeight;
                uint32_t u32DstLineSize     = u32DstWidth * 4;
                uint32_t u32DstBitsPerPixel = 32;

                pDrv->pUpPort->pfnCopyRect(pDrv->pUpPort,
                                           width, height,
                                           pu8Src,
                                           xSrc, ySrc,
                                           u32SrcWidth, u32SrcHeight,
                                           u32SrcLineSize, u32SrcBitsPerPixel,
                                           pu8Dst,
                                           xDst, yDst,
                                           u32DstWidth, u32DstHeight,
                                           u32DstLineSize, u32DstBitsPerPixel);
            }
        }
    }

    /*
     * Here is your classic 'temporary' solution.
     */
    /** @todo New SendUpdate entry which can get a separate cmd header or coords. */
    VBVACMDHDR *pHdrUnconst = (VBVACMDHDR *)pCmd;

    pHdrUnconst->x -= (int16_t)pFBInfo->xOrigin;
    pHdrUnconst->y -= (int16_t)pFBInfo->yOrigin;

    pThis->mParent->i_consoleVRDPServer()->SendUpdate(uScreenId, pHdrUnconst, (uint32_t)cbCmd);

    *pHdrUnconst = hdrSaved;
}

DECLCALLBACK(void) Display::i_displayVBVAUpdateEnd(PPDMIDISPLAYCONNECTOR pInterface, unsigned uScreenId, int32_t x, int32_t y,
                                                   uint32_t cx, uint32_t cy)
{
    LogFlowFunc(("uScreenId %d %d,%d %dx%d\n", uScreenId, x, y, cx, cy));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;
    DISPLAYFBINFO *pFBInfo;
    AssertReturnVoid(uScreenId < pThis->mcMonitors);

    pFBInfo = &pThis->maFramebuffers[uScreenId];

    /** @todo handleFramebufferUpdate (uScreenId,
     *                                x - pThis->maFramebuffers[uScreenId].xOrigin,
     *                                y - pThis->maFramebuffers[uScreenId].yOrigin,
     *                                cx, cy);
     */
    pThis->i_handleDisplayUpdate(uScreenId, x - pFBInfo->xOrigin, y - pFBInfo->yOrigin, cx, cy);
}

#ifdef DEBUG_sunlover
static void logVBVAResize(PCVBVAINFOVIEW pView, PCVBVAINFOSCREEN pScreen, const DISPLAYFBINFO *pFBInfo)
{
    LogRel(("displayVBVAResize: [%d] %s\n"
            "    pView->u32ViewIndex     %d\n"
            "    pView->u32ViewOffset    0x%08X\n"
            "    pView->u32ViewSize      0x%08X\n"
            "    pView->u32MaxScreenSize 0x%08X\n"
            "    pScreen->i32OriginX      %d\n"
            "    pScreen->i32OriginY      %d\n"
            "    pScreen->u32StartOffset  0x%08X\n"
            "    pScreen->u32LineSize     0x%08X\n"
            "    pScreen->u32Width        %d\n"
            "    pScreen->u32Height       %d\n"
            "    pScreen->u16BitsPerPixel %d\n"
            "    pScreen->u16Flags        0x%04X\n"
            "    pFBInfo->u32Offset             0x%08X\n"
            "    pFBInfo->u32MaxFramebufferSize 0x%08X\n"
            "    pFBInfo->u32InformationSize    0x%08X\n"
            "    pFBInfo->fDisabled             %d\n"
            "    xOrigin, yOrigin, w, h:        %d,%d %dx%d\n"
            "    pFBInfo->u16BitsPerPixel       %d\n"
            "    pFBInfo->pu8FramebufferVRAM    %p\n"
            "    pFBInfo->u32LineSize           0x%08X\n"
            "    pFBInfo->flags                 0x%04X\n"
            "    pFBInfo->pHostEvents           %p\n"
            "    pFBInfo->fDefaultFormat        %d\n"
            "    pFBInfo->fVBVAEnabled    %d\n"
            "    pFBInfo->fVBVAForceResize %d\n"
            "    pFBInfo->pVBVAHostFlags  %p\n"
            "",
            pScreen->u32ViewIndex,
            (pScreen->u16Flags & VBVA_SCREEN_F_DISABLED)? "DISABLED": "ENABLED",
            pView->u32ViewIndex,
            pView->u32ViewOffset,
            pView->u32ViewSize,
            pView->u32MaxScreenSize,
            pScreen->i32OriginX,
            pScreen->i32OriginY,
            pScreen->u32StartOffset,
            pScreen->u32LineSize,
            pScreen->u32Width,
            pScreen->u32Height,
            pScreen->u16BitsPerPixel,
            pScreen->u16Flags,
            pFBInfo->u32Offset,
            pFBInfo->u32MaxFramebufferSize,
            pFBInfo->u32InformationSize,
            pFBInfo->fDisabled,
            pFBInfo->xOrigin,
            pFBInfo->yOrigin,
            pFBInfo->w,
            pFBInfo->h,
            pFBInfo->u16BitsPerPixel,
            pFBInfo->pu8FramebufferVRAM,
            pFBInfo->u32LineSize,
            pFBInfo->flags,
            pFBInfo->pHostEvents,
            pFBInfo->fDefaultFormat,
            pFBInfo->fVBVAEnabled,
            pFBInfo->fVBVAForceResize,
            pFBInfo->pVBVAHostFlags
          ));
}
#endif /* DEBUG_sunlover */

DECLCALLBACK(int) Display::i_displayVBVAResize(PPDMIDISPLAYCONNECTOR pInterface, PCVBVAINFOVIEW pView,
                                               PCVBVAINFOSCREEN pScreen, void *pvVRAM, bool fResetInputMapping)
{
    LogRelFlowFunc(("pScreen %p, pvVRAM %p\n", pScreen, pvVRAM));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;

    return pThis->processVBVAResize(pView, pScreen, pvVRAM, fResetInputMapping);
}

int Display::processVBVAResize(PCVBVAINFOVIEW pView, PCVBVAINFOSCREEN pScreen, void *pvVRAM, bool fResetInputMapping)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    RT_NOREF(pView);

    DISPLAYFBINFO *pFBInfo = &maFramebuffers[pScreen->u32ViewIndex];

#ifdef DEBUG_sunlover
    logVBVAResize(pView, pScreen, pFBInfo);
#endif

    if (pScreen->u16Flags & VBVA_SCREEN_F_DISABLED)
    {
        /* Ask the framebuffer to resize using a default format. The framebuffer will be black.
         * So if the frontend does not support GuestMonitorChangedEventType_Disabled event,
         * the VM window will be black. */
        uint32_t u32Width = pFBInfo->w ? pFBInfo->w : 640;
        uint32_t u32Height = pFBInfo->h ? pFBInfo->h : 480;
        int32_t xOrigin = pFBInfo->xOrigin;
        int32_t yOrigin = pFBInfo->yOrigin;

        alock.release();

        i_handleDisplayResize(pScreen->u32ViewIndex, 0, (uint8_t *)NULL, 0,
                              u32Width, u32Height, pScreen->u16Flags, xOrigin, yOrigin, false);

        return VINF_SUCCESS;
    }

    VBVAINFOSCREEN screenInfo;
    RT_ZERO(screenInfo);

    if (pScreen->u16Flags & VBVA_SCREEN_F_BLANK2)
    {
        /* Init a local VBVAINFOSCREEN structure, which will be used instead of
         * the original pScreen. Set VBVA_SCREEN_F_BLANK, which will force
         * the code below to choose the "blanking" branches.
         */
        screenInfo.u32ViewIndex    = pScreen->u32ViewIndex;
        screenInfo.i32OriginX      = pFBInfo->xOrigin;
        screenInfo.i32OriginY      = pFBInfo->yOrigin;
        screenInfo.u32StartOffset  = 0; /* Irrelevant */
        screenInfo.u32LineSize     = pFBInfo->u32LineSize;
        screenInfo.u32Width        = pFBInfo->w;
        screenInfo.u32Height       = pFBInfo->h;
        screenInfo.u16BitsPerPixel = pFBInfo->u16BitsPerPixel;
        screenInfo.u16Flags        = pScreen->u16Flags | VBVA_SCREEN_F_BLANK;

        pScreen = &screenInfo;
    }

    if (fResetInputMapping)
    {
        /// @todo Rename to m* and verify whether some kind of lock is required.
        xInputMappingOrigin = 0;
        yInputMappingOrigin = 0;
        cxInputMapping = 0;
        cyInputMapping = 0;
    }

    alock.release();

    return i_handleDisplayResize(pScreen->u32ViewIndex, pScreen->u16BitsPerPixel,
                                 (uint8_t *)pvVRAM + pScreen->u32StartOffset,
                                 pScreen->u32LineSize, pScreen->u32Width, pScreen->u32Height, pScreen->u16Flags,
                                 pScreen->i32OriginX, pScreen->i32OriginY, false);
}

DECLCALLBACK(int) Display::i_displayVBVAMousePointerShape(PPDMIDISPLAYCONNECTOR pInterface, bool fVisible, bool fAlpha,
                                                          uint32_t xHot, uint32_t yHot,
                                                          uint32_t cx, uint32_t cy,
                                                          const void *pvShape)
{
    LogFlowFunc(("\n"));
    LogRel2(("%s: fVisible=%RTbool\n", __PRETTY_FUNCTION__, fVisible));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);

    uint32_t cbShape = 0;
    if (pvShape)
    {
        cbShape = (cx + 7) / 8 * cy; /* size of the AND mask */
        cbShape = ((cbShape + 3) & ~3) + cx * 4 * cy; /* + gap + size of the XOR mask */
    }

    /* Tell the console about it */
    pDrv->pDisplay->mParent->i_onMousePointerShapeChange(fVisible, fAlpha,
                                                         xHot, yHot, cx, cy, (uint8_t *)pvShape, cbShape);

    return VINF_SUCCESS;
}

DECLCALLBACK(void) Display::i_displayVBVAGuestCapabilityUpdate(PPDMIDISPLAYCONNECTOR pInterface, uint32_t fCapabilities)
{
    LogFlowFunc(("\n"));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;

    pThis->i_handleUpdateGuestVBVACapabilities(fCapabilities);
}

DECLCALLBACK(void) Display::i_displayVBVAInputMappingUpdate(PPDMIDISPLAYCONNECTOR pInterface, int32_t xOrigin, int32_t yOrigin,
                                                            uint32_t cx, uint32_t cy)
{
    LogFlowFunc(("\n"));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;

    pThis->i_handleUpdateVBVAInputMapping(xOrigin, yOrigin, cx, cy);
}

DECLCALLBACK(void) Display::i_displayVBVAReportCursorPosition(PPDMIDISPLAYCONNECTOR pInterface, uint32_t fFlags, uint32_t aScreenId, uint32_t x, uint32_t y)
{
    LogFlowFunc(("\n"));
    LogRel2(("%s: fFlags=%RU32, aScreenId=%RU32, x=%RU32, y=%RU32\n",
             __PRETTY_FUNCTION__, fFlags, aScreenId, x, y));

    PDRVMAINDISPLAY pDrv = PDMIDISPLAYCONNECTOR_2_MAINDISPLAY(pInterface);
    Display *pThis = pDrv->pDisplay;

    if (fFlags & VBVA_CURSOR_SCREEN_RELATIVE)
    {
        AssertReturnVoid(aScreenId < pThis->mcMonitors);

        x += pThis->maFramebuffers[aScreenId].xOrigin;
        y += pThis->maFramebuffers[aScreenId].yOrigin;
    }
    ::FireCursorPositionChangedEvent(pThis->mParent->i_getEventSource(), RT_BOOL(fFlags & VBVA_CURSOR_VALID_DATA), x, y);

# ifdef VBOX_WITH_RECORDING
    pThis->i_recordingCursorPositionChange(aScreenId, fFlags, x, y);
# endif
}

#endif /* VBOX_WITH_HGSMI */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *)  Display::i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINDISPLAY pDrv = PDMINS_2_DATA(pDrvIns, PDRVMAINDISPLAY);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIDISPLAYCONNECTOR, &pDrv->IConnector);
    return NULL;
}


/**
 * @interface_method_impl{PDMDRVREG,pfnPowerOff,
 *  Tries to ensure no client calls gets to HGCM or the VGA device from here on.}
 */
DECLCALLBACK(void) Display::i_drvPowerOff(PPDMDRVINS pDrvIns)
{
    PDRVMAINDISPLAY pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINDISPLAY);
    LogRelFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Do much of the work that i_drvDestruct does.
     */
    if (pThis->pUpPort)
        pThis->pUpPort->pfnSetRenderVRAM(pThis->pUpPort, false);

    pThis->IConnector.pbData     = NULL;
    pThis->IConnector.cbScanline = 0;
    pThis->IConnector.cBits      = 32;
    pThis->IConnector.cx         = 0;
    pThis->IConnector.cy         = 0;

    if (pThis->pDisplay)
    {
        AutoWriteLock displayLock(pThis->pDisplay COMMA_LOCKVAL_SRC_POS);
#ifdef VBOX_WITH_RECORDING
        pThis->pDisplay->mParent->i_recordingStop();
#endif
#if defined(VBOX_WITH_VIDEOHWACCEL)
        pThis->pVBVACallbacks = NULL;
#endif
    }
}


/**
 * Destruct a display driver instance.
 *
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) Display::i_drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINDISPLAY pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINDISPLAY);
    LogRelFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));

    /*
     * We repeat much of what i_drvPowerOff does in case it wasn't called.
     * In addition we sever the connection between us and the display.
     */
    if (pThis->pUpPort)
        pThis->pUpPort->pfnSetRenderVRAM(pThis->pUpPort, false);

    pThis->IConnector.pbData     = NULL;
    pThis->IConnector.cbScanline = 0;
    pThis->IConnector.cBits      = 32;
    pThis->IConnector.cx         = 0;
    pThis->IConnector.cy         = 0;

    if (pThis->pDisplay)
    {
        AutoWriteLock displayLock(pThis->pDisplay COMMA_LOCKVAL_SRC_POS);
#ifdef VBOX_WITH_RECORDING
        pThis->pDisplay->mParent->i_recordingStop();
#endif
#if defined(VBOX_WITH_VIDEOHWACCEL)
        pThis->pVBVACallbacks = NULL;
#endif

        pThis->pDisplay->mpDrv = NULL;
        pThis->pDisplay = NULL;
    }
#if defined(VBOX_WITH_VIDEOHWACCEL)
    pThis->pVBVACallbacks = NULL;
#endif
}


/**
 * Construct a display driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
DECLCALLBACK(int) Display::i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    RT_NOREF(fFlags, pCfg);
    PDRVMAINDISPLAY pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINDISPLAY);
    LogRelFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "", "");
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Init Interfaces.
     */
    pDrvIns->IBase.pfnQueryInterface           = Display::i_drvQueryInterface;

    pThis->IConnector.pfnResize                = Display::i_displayResizeCallback;
    pThis->IConnector.pfnUpdateRect            = Display::i_displayUpdateCallback;
    pThis->IConnector.pfnRefresh               = Display::i_displayRefreshCallback;
    pThis->IConnector.pfnReset                 = Display::i_displayResetCallback;
    pThis->IConnector.pfnLFBModeChange         = Display::i_displayLFBModeChangeCallback;
    pThis->IConnector.pfnProcessAdapterData    = Display::i_displayProcessAdapterDataCallback;
    pThis->IConnector.pfnProcessDisplayData    = Display::i_displayProcessDisplayDataCallback;
#ifdef VBOX_WITH_VIDEOHWACCEL
    pThis->IConnector.pfnVHWACommandProcess    = Display::i_displayVHWACommandProcess;
#endif
#ifdef VBOX_WITH_HGSMI
    pThis->IConnector.pfnVBVAEnable            = Display::i_displayVBVAEnable;
    pThis->IConnector.pfnVBVADisable           = Display::i_displayVBVADisable;
    pThis->IConnector.pfnVBVAUpdateBegin       = Display::i_displayVBVAUpdateBegin;
    pThis->IConnector.pfnVBVAUpdateProcess     = Display::i_displayVBVAUpdateProcess;
    pThis->IConnector.pfnVBVAUpdateEnd         = Display::i_displayVBVAUpdateEnd;
    pThis->IConnector.pfnVBVAResize            = Display::i_displayVBVAResize;
    pThis->IConnector.pfnVBVAMousePointerShape = Display::i_displayVBVAMousePointerShape;
    pThis->IConnector.pfnVBVAGuestCapabilityUpdate = Display::i_displayVBVAGuestCapabilityUpdate;
    pThis->IConnector.pfnVBVAInputMappingUpdate = Display::i_displayVBVAInputMappingUpdate;
    pThis->IConnector.pfnVBVAReportCursorPosition = Display::i_displayVBVAReportCursorPosition;
#endif
    pThis->IConnector.pfn3DNotifyProcess       = Display::i_display3DNotifyProcess;

    /*
     * Get the IDisplayPort interface of the above driver/device.
     */
    pThis->pUpPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIDISPLAYPORT);
    if (!pThis->pUpPort)
    {
        AssertMsgFailed(("Configuration error: No display port interface above!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }
#if defined(VBOX_WITH_VIDEOHWACCEL)
    pThis->pVBVACallbacks = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIDISPLAYVBVACALLBACKS);
#endif
    /*
     * Get the Display object pointer and update the mpDrv member.
     */
    com::Guid uuid(COM_IIDOF(IDisplay));
    IDisplay *pIDisplay = (IDisplay *)PDMDrvHlpQueryGenericUserObject(pDrvIns, uuid.raw());
    if (!pIDisplay)
    {
        AssertMsgFailed(("Configuration error: No/bad Keyboard object!\n"));
        return VERR_NOT_FOUND;
    }
    pThis->pDisplay = static_cast<Display *>(pIDisplay);
    pThis->pDisplay->mpDrv = pThis;

    /* Disable VRAM to a buffer copy initially. */
    pThis->pUpPort->pfnSetRenderVRAM(pThis->pUpPort, false);
    pThis->IConnector.cBits = 32; /* DevVGA does nothing otherwise. */

    /*
     * Start periodic screen refreshes
     */
    pThis->pUpPort->pfnSetRefreshRate(pThis->pUpPort, 20);

    return VINF_SUCCESS;
}


/**
 * Display driver registration record.
 */
const PDMDRVREG Display::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "MainDisplay",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main display driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_DISPLAY,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINDISPLAY),
    /* pfnConstruct */
    Display::i_drvConstruct,
    /* pfnDestruct */
    Display::i_drvDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    Display::i_drvPowerOff,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
