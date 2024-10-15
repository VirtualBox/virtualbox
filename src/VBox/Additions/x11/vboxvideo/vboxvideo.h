/* $Id: vboxvideo.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox X11 Additions graphics driver
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 * This file is based on the X11 VESA driver:
 *
 * Copyright (c) 2000 by Conectiva S.A. (http://www.conectiva.com)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of Conectiva Linux shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization from
 * Conectiva Linux.
 *
 * Authors: Paulo César Pereira de Andrade <pcpa@conectiva.com.br>
 *          Michael Thayer <michael.thayer@oracle.com>
 */

#ifndef GA_INCLUDED_SRC_x11_vboxvideo_vboxvideo_h
#define GA_INCLUDED_SRC_x11_vboxvideo_vboxvideo_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBoxVideoGuest.h>
#include <VBoxVideo.h>
#include "version-generated.h"

#define VBOX_VENDORID 0x80EE
#define VBOX_DEVICEID 0xBEEF

#ifndef VBVA_SCREEN_F_BLANK
# define VBVA_SCREEN_F_BLANK    0x0004
#endif

#include <VBoxVideoVBE.h>

#include "xf86.h"
#include "xf86str.h"
#include "xf86Cursor.h"

#ifdef DEBUG

#define TRACE_ENTRY() do { xf86ErrorF("%s: entering\n", __func__); } while(0)
#define TRACE_EXIT() do { xf86ErrorF("%s: leaving\n", __func__); } while(0)
#define TRACE_LINE() \
    do { xf86ErrorF("%s: line\n", __func__, __LINE__); } while(0)
#define TRACE_LOG(...) \
do { \
    xf86ErrorF("%s: ", __func__); \
    xf86ErrorF(__VA_ARGS__); \
} while(0)

#else  /* !DEBUG */

#define TRACE_ENTRY()         do { } while (0)
#define TRACE_EXIT()          do { } while (0)
#define TRACE_LOG(...)        do { } while (0)

#endif  /* !DEBUG */

#define VBOX_VERSION            VBOX_VERSION_MAJOR * 10000 \
                              + VBOX_VERSION_MINOR * 100
#define VBOX_NAME               "VBoxVideo"
#define VBOX_DRIVER_NAME        "vboxvideo"

#define VBOX_VIDEO_MAJOR  VBOX_VERSION_MAJOR
#define VBOX_VIDEO_MINOR  VBOX_VERSION_MINOR

#define VBOX_VIDEO_MIN_SIZE    64
#define VBOX_VIDEO_MAX_VIRTUAL (INT16_MAX - 1)

#define VBOXPTR(p) ((VBOXPtr)((p)->driverPrivate))

/** Helper to work round different ways of getting the root window in different
 * server versions. */
#if defined(XORG_VERSION_CURRENT) && XORG_VERSION_CURRENT < 70000000 \
    && XORG_VERSION_CURRENT >= 10900000
# define ROOT_WINDOW(pScrn) screenInfo.screens[(pScrn)->scrnIndex]->root
#else
# define ROOT_WINDOW(pScrn) WindowTable[(pScrn)->scrnIndex]
#endif

/** ChangeWindowProperty for X.Org Server 1.19 and later */
#if defined(XORG_VERSION_CURRENT) && XORG_VERSION_CURRENT < 70000000 \
    && XORG_VERSION_CURRENT >= 11900000
# define ChangeWindowProperty(pWin, property, type, format, mode, \
                              len, value, sendevent) \
        dixChangeWindowProperty(serverClient, pWin, property, type, format, \
                                mode, len, value, sendevent)
#endif

/** Structure containing all virtual monitor-specific information. */
struct VBoxScreen
{
    /** Position information for each virtual screen for the purposes of
     * sending dirty rectangle information to the right one. */
    RTRECT2 aScreenLocation;
    /** Is this CRTC enabled or in DPMS off state? */
    Bool fPowerOn;
#ifdef VBOXVIDEO_13
    /** The virtual crtcs. */
    struct _xf86Crtc *paCrtcs;
    /** The virtual outputs, logically not distinct from crtcs. */
    struct _xf86Output *paOutputs;
#endif
    /** Offsets of VBVA buffers in video RAM */
    uint32_t aoffVBVABuffer;
    /** Context information about the VBVA buffers for each screen */
    struct VBVABUFFERCONTEXT aVbvaCtx;
    /** The current preferred resolution for the screen */
    RTRECTSIZE aPreferredSize;
    /** The current preferred location for the screen. */
    RTPOINT aPreferredLocation;
    /** Has this screen been enabled by the host? */
    Bool afConnected;
    /** Does this screen have a preferred location? */
    Bool afHaveLocation;
};

typedef struct VBOXRec
{
    EntityInfoPtr pEnt;
#ifdef PCIACCESS
    struct pci_device *pciInfo;
#else
    pciVideoPtr pciInfo;
    PCITAG pciTag;
#endif
    void *base;
    /** The amount of VRAM available for use as a framebuffer */
    unsigned long cbFBMax;
    /** The size of the framebuffer and the VBVA buffers at the end of it. */
    unsigned long cbView;
    /** Whether the pre-X-server mode was a VBE mode */
    Bool fSavedVBEMode;
    /** Paramters of the saved pre-X-server VBE mode, invalid if there is none
     */
    uint16_t cSavedWidth, cSavedHeight, cSavedPitch, cSavedBPP, fSavedFlags;
    CloseScreenProcPtr CloseScreen;
    /** Default X server procedure for enabling and disabling framebuffer access */
    xf86EnableDisableFBAccessProc *EnableDisableFBAccess;
    OptionInfoPtr Options;
    /** @todo we never actually free this */
    xf86CursorInfoPtr pCurs;
    /** Do we currently want to use the host cursor? */
    Bool fUseHardwareCursor;
    /** Number of screens attached */
    uint32_t cScreens;
    /** Information about each virtual screen. */
    struct VBoxScreen *pScreens;
    /** Can we get mode hint and cursor integration information from HGSMI? */
    Bool fHaveHGSMIModeHints;
    /** Does the host support the screen blanking flag? */
    Bool fHostHasScreenBlankingFlag;
    /** Array of structures for receiving mode hints. */
    VBVAMODEHINT *paVBVAModeHints;
#ifdef VBOXVIDEO_13
# ifdef RT_OS_LINUX
    /** Input device file descriptor for getting ACPI hot-plug events. */
    int fdACPIDevices;
    /** Input handler handle for ACPI hot-plug listener. */
    void *hACPIEventHandler;
# endif
#endif
    /** HGSMI guest heap context */
    HGSMIGUESTCOMMANDCONTEXT guestCtx;
    /** Unrestricted horizontal resolution flag. */
    Bool fAnyX;
} VBOXRec, *VBOXPtr;

#define VBOXGetRec(pScrn) ((VBOXPtr)(pScrn)->driverPrivate)

/* setmode.c */

/** Structure describing the virtual frame buffer.  It starts at the beginning
 * of the video RAM. */
struct vbvxFrameBuffer {
    /** X offset of first screen in frame buffer. */
    int x0;
    /** Y offset of first screen in frame buffer. */
    int y0;
    /** Frame buffer virtual width. */
    unsigned cWidth;
    /** Frame buffer virtual height. */
    unsigned cHeight;
    /** Bits per pixel. */
    unsigned cBPP;
};

extern void vbvxClearVRAM(ScrnInfoPtr pScrn, size_t cbOldSize, size_t cbNewSize);
extern void vbvxSetMode(ScrnInfoPtr pScrn, unsigned cDisplay, unsigned cWidth, unsigned cHeight, int x, int y, Bool fEnabled,
                        Bool fConnected, struct vbvxFrameBuffer *pFrameBuffer);
extern void vbvxSetSolarisMouseRange(int width, int height);

/* pointer.h */
extern Bool vbvxCursorInit(ScreenPtr pScreen);
extern void vbvxCursorTerm(VBOXPtr pVBox);

/* vbva.c */
extern void vbvxHandleDirtyRect(ScrnInfoPtr pScrn, int iRects, BoxPtr aRects);
extern void vbvxSetUpHGSMIHeapInGuest(VBOXPtr pVBox, uint32_t cbVRAM);
extern Bool vboxEnableVbva(ScrnInfoPtr pScrn);
extern void vboxDisableVbva(ScrnInfoPtr pScrn);

/* getmode.c */
extern void vboxAddModes(ScrnInfoPtr pScrn);
extern void VBoxInitialiseSizeHints(ScrnInfoPtr pScrn);
extern void vbvxReadSizesAndCursorIntegrationFromProperties(ScrnInfoPtr pScrn, Bool *pfNeedUpdate);
extern void vbvxReadSizesAndCursorIntegrationFromHGSMI(ScrnInfoPtr pScrn, Bool *pfNeedUpdate);
extern void vbvxSetUpLinuxACPI(ScreenPtr pScreen);
extern void vbvxCleanUpLinuxACPI(ScreenPtr pScreen);

/* EDID generation */
#ifdef VBOXVIDEO_13
extern Bool VBOXEDIDSet(struct _xf86Output *output, DisplayModePtr pmode);
#endif

#endif /* !GA_INCLUDED_SRC_x11_vboxvideo_vboxvideo_h */

