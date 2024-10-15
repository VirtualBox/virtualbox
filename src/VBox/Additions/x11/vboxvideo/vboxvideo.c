/* $Id: vboxvideo.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Linux Additions X11 graphics driver
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 * This file is based on the X.Org VESA driver:
 *
 * Copyright (c) 2000 by Conectiva S.A. (http://www.conectiva.com)
 * Copyright 2008 Red Hat, Inc.
 * Copyright 2012 Red Hat, Inc.
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
 *          David Dawes <dawes@xfree86.org>
 *          Adam Jackson <ajax@redhat.com>
 *          Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com>
 */

#include "vboxvideo.h"
#include <VBoxVideoVBE.h>

/* Basic definitions and functions needed by all drivers. */
#include "xf86.h"
/* For video memory mapping. */
#include "xf86_OSproc.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
/* PCI resources. */
# include "xf86Resources.h"
#endif
/* Generic server linear frame-buffer APIs. */
#include "fb.h"
/* Colormap and visual handling. */
#include "micmap.h"
#include "xf86cmap.h"
/* ShadowFB support */
#include "shadowfb.h"
/* VGA hardware functions for setting and restoring text mode */
#include "vgaHW.h"
#ifdef VBOXVIDEO_13
/* X.org 1.3+ mode setting */
# define _HAVE_STRING_ARCH_strsep /* bits/string2.h, __strsep_1c. */
# include "xf86Crtc.h"
# include "xf86Modes.h"
/* For xf86RandR12GetOriginalVirtualSize(). */
# include "xf86RandR12.h"
#endif
/* For setting the root window property. */
#include "property.h"
#include <X11/Xatom.h>

#ifdef XORG_7X
# include <stdlib.h>
# include <string.h>
# include <fcntl.h>
# include <unistd.h>
#endif

/* Mandatory functions */

static const OptionInfoRec * VBOXAvailableOptions(int chipid, int busid);
static void VBOXIdentify(int flags);
#ifndef PCIACCESS
static Bool VBOXProbe(DriverPtr drv, int flags);
#else
static Bool VBOXPciProbe(DriverPtr drv, int entity_num,
     struct pci_device *dev, intptr_t match_data);
#endif
static Bool VBOXPreInit(ScrnInfoPtr pScrn, int flags);
static Bool VBOXScreenInit(ScreenPtr pScreen, int argc, char **argv);
static Bool VBOXEnterVT(ScrnInfoPtr pScrn);
static void VBOXLeaveVT(ScrnInfoPtr pScrn);
static Bool VBOXCloseScreen(ScreenPtr pScreen);
#ifndef VBOXVIDEO_13
static Bool VBOXSaveScreen(ScreenPtr pScreen, int mode);
#endif
static Bool VBOXSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr pMode);
static void VBOXAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
static void VBOXFreeScreen(ScrnInfoPtr pScrn);
#ifndef VBOXVIDEO_13
static void VBOXDisplayPowerManagementSet(ScrnInfoPtr pScrn, int mode, int flags);
#endif

/* locally used functions */
static Bool VBOXMapVidMem(ScrnInfoPtr pScrn);
static void VBOXUnmapVidMem(ScrnInfoPtr pScrn);
static void VBOXSaveMode(ScrnInfoPtr pScrn);
static void VBOXRestoreMode(ScrnInfoPtr pScrn);
static void setSizesAndCursorIntegration(ScrnInfoPtr pScrn, Bool fScreenInitTime);

#ifndef XF86_SCRN_INTERFACE
# define xf86ScreenToScrn(pScreen) xf86Screens[(pScreen)->myNum]
# define xf86ScrnToScreen(pScrn) screenInfo.screens[(pScrn)->scrnIndex]
#endif

static inline void VBOXSetRec(ScrnInfoPtr pScrn)
{
    if (!pScrn->driverPrivate)
    {
        VBOXPtr pVBox = (VBOXPtr)xnfcalloc(sizeof(VBOXRec), 1);
        pScrn->driverPrivate = pVBox;
#if defined(VBOXVIDEO_13) && defined(RT_OS_LINUX)
        pVBox->fdACPIDevices = -1;
#endif
    }
}

enum GenericTypes
{
    CHIP_VBOX_GENERIC
};

#ifdef PCIACCESS
static const struct pci_id_match vbox_device_match[] = {
    {
        VBOX_VENDORID, VBOX_DEVICEID, PCI_MATCH_ANY, PCI_MATCH_ANY,
        0, 0, 0
    },

    { 0, 0, 0 },
};
#endif

/* Supported chipsets */
static SymTabRec VBOXChipsets[] =
{
    {VBOX_DEVICEID, "vbox"},
    {-1,            NULL}
};

static PciChipsets VBOXPCIchipsets[] = {
  { VBOX_DEVICEID, VBOX_DEVICEID, RES_SHARED_VGA },
  { -1,            -1,            RES_UNDEFINED },
};

/*
 * This contains the functions needed by the server after loading the
 * driver module.  It must be supplied, and gets added the driver list by
 * the Module Setup function in the dynamic case.  In the static case a
 * reference to this is compiled in, and this requires that the name of
 * this DriverRec be an upper-case version of the driver name.
 */

#ifdef XORG_7X
_X_EXPORT
#endif
DriverRec VBOXVIDEO = {
    VBOX_VERSION,
    VBOX_DRIVER_NAME,
    VBOXIdentify,
#ifdef PCIACCESS
    NULL,
#else
    VBOXProbe,
#endif
    VBOXAvailableOptions,
    NULL,
    0,
#ifdef XORG_7X
    NULL,
#endif
#ifdef PCIACCESS
    vbox_device_match,
    VBOXPciProbe
#endif
};

/* No options for now */
static const OptionInfoRec VBOXOptions[] = {
    { -1, NULL, OPTV_NONE, {0}, FALSE }
};

#ifndef XORG_7X
/*
 * List of symbols from other modules that this module references.  This
 * list is used to tell the loader that it is OK for symbols here to be
 * unresolved providing that it hasn't been told that they haven't been
 * told that they are essential via a call to xf86LoaderReqSymbols() or
 * xf86LoaderReqSymLists().  The purpose is this is to avoid warnings about
 * unresolved symbols that are not required.
 */
static const char *fbSymbols[] = {
    "fbPictureInit",
    "fbScreenInit",
    NULL
};

static const char *shadowfbSymbols[] = {
    "ShadowFBInit2",
    NULL
};

static const char *ramdacSymbols[] = {
    "xf86DestroyCursorInfoRec",
    "xf86InitCursor",
    "xf86CreateCursorInfoRec",
    NULL
};

static const char *vgahwSymbols[] = {
    "vgaHWFreeHWRec",
    "vgaHWGetHWRec",
    "vgaHWGetIOBase",
    "vgaHWGetIndex",
    "vgaHWRestore",
    "vgaHWSave",
    "vgaHWSetStdFuncs",
    NULL
};
#endif /* !XORG_7X */

/** Resize the virtual framebuffer. */
static Bool adjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = xf86ScrnToScreen(pScrn);
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    int adjustedWidth = pScrn->bitsPerPixel == 16 ? (width + 1) & ~1 : width;
    int cbLine = adjustedWidth * pScrn->bitsPerPixel / 8;
    PixmapPtr pPixmap;

    TRACE_LOG("width=%d, height=%d\n", width, height);
    AssertMsg(width >= 0 && height >= 0, ("Invalid negative width (%d) or height (%d)\n", width, height));
    if (pScreen == NULL)  /* Not yet initialised. */
        return TRUE;
    pPixmap = pScreen->GetScreenPixmap(pScreen);
    AssertMsg(pPixmap != NULL, ("Failed to get the screen pixmap.\n"));
    TRACE_LOG("pPixmap=%p adjustedWidth=%d height=%d pScrn->depth=%d pScrn->bitsPerPixel=%d cbLine=%d pVBox->base=%p pPixmap->drawable.width=%d pPixmap->drawable.height=%d\n",
              (void *)pPixmap, adjustedWidth, height, pScrn->depth,
              pScrn->bitsPerPixel, cbLine, pVBox->base,
              pPixmap->drawable.width, pPixmap->drawable.height);
    if (   adjustedWidth != pPixmap->drawable.width
        || height != pPixmap->drawable.height)
    {
        if (   adjustedWidth > VBOX_VIDEO_MAX_VIRTUAL || height > VBOX_VIDEO_MAX_VIRTUAL
            || (unsigned)cbLine * (unsigned)height >= pVBox->cbFBMax)
        {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Virtual framebuffer %dx%d too large.  For information, video memory: %u Kb.\n",
                       adjustedWidth, height, (unsigned) pVBox->cbFBMax / 1024);
            return FALSE;
        }
        if (pScrn->vtSema)
            vbvxClearVRAM(pScrn, ((size_t)pScrn->virtualX) * pScrn->virtualY * (pScrn->bitsPerPixel / 8),
                          ((size_t)adjustedWidth) * height * (pScrn->bitsPerPixel / 8));
        pScreen->ModifyPixmapHeader(pPixmap, adjustedWidth, height, pScrn->depth, pScrn->bitsPerPixel, cbLine, pVBox->base);
    }
    pScrn->displayWidth = pScrn->virtualX = adjustedWidth;
    pScrn->virtualY = height;
    return TRUE;
}

#ifndef VBOXVIDEO_13
/** Set a video mode to the hardware, RandR 1.1 version.
 *
 * Since we no longer do virtual frame buffers, adjust the screen pixmap
 * dimensions to match.  The "override" parameters are for when we received a
 * mode hint while switched to a virtual terminal.  In this case VBoxClient will
 * have told us about the mode, but not yet been able to do a mode switch using
 * RandR.  We solve this by setting the requested mode to the host but keeping
 * the virtual frame-
 * buffer matching what the X server expects. */
static void setModeRandR11(ScrnInfoPtr pScrn, DisplayModePtr pMode, Bool fScreenInitTime, Bool fEnterVTTime,
                           int cXOverRide, int cYOverRide)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    struct vbvxFrameBuffer frameBuffer = { 0, 0, pMode->HDisplay, pMode->VDisplay, pScrn->bitsPerPixel};
    int cXPhysical = cXOverRide > 0 ? min(cXOverRide, pMode->HDisplay) : pMode->HDisplay;
    int cYPhysical = cYOverRide > 0 ? min(cYOverRide, pMode->VDisplay) : pMode->VDisplay;

    pVBox->pScreens[0].aScreenLocation.cx = pMode->HDisplay;
    pVBox->pScreens[0].aScreenLocation.cy = pMode->VDisplay;
    if (fScreenInitTime)
    {
        /* The screen structure is not fully set up yet, so do not touch it. */
        pScrn->displayWidth = pScrn->virtualX = pMode->HDisplay;
        pScrn->virtualY = pMode->VDisplay;
    }
    else
    {
        xf86ScrnToScreen(pScrn)->width = pMode->HDisplay;
        xf86ScrnToScreen(pScrn)->height = pMode->VDisplay;
        /* This prevents a crash in CentOS 3.  I was unable to debug it to
         * satisfaction, partly due to the lack of symbols.  My guess is that
         * pScrn->ModifyPixmapHeader() expects certain things to be set up when
         * it sees pScrn->vtSema set to true which are not quite done at this
         * point of the VT switch. */
        if (fEnterVTTime)
            pScrn->vtSema = FALSE;
        adjustScreenPixmap(pScrn, pMode->HDisplay, pMode->VDisplay);
        if (fEnterVTTime)
            pScrn->vtSema = TRUE;
    }
    if (pMode->HDisplay != 0 && pMode->VDisplay != 0 && pScrn->vtSema)
        vbvxSetMode(pScrn, 0, cXPhysical, cYPhysical, 0, 0, true, true, &frameBuffer);
    pScrn->currentMode = pMode;
}
#endif

#ifdef VBOXVIDEO_13
/* X.org 1.3+ mode-setting support ******************************************/

/** Set a video mode to the hardware, RandR 1.2 version.  If this is the first
 * screen, re-set the current mode for all others (the offset for the first
 * screen is always treated as zero by the hardware, so all other screens need
 * to be changed to compensate for any changes!).  The mode to set is taken
 * from the X.Org Crtc structure. */
static void setModeRandR12(ScrnInfoPtr pScrn, unsigned cScreen)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    unsigned i;
    struct vbvxFrameBuffer frameBuffer = { pVBox->pScreens[0].paCrtcs->x, pVBox->pScreens[0].paCrtcs->y, pScrn->virtualX,
                                           pScrn->virtualY, pScrn->bitsPerPixel };
    unsigned cFirst = cScreen;
    unsigned cLast = cScreen != 0 ? cScreen + 1 : pVBox->cScreens;
    int originalX, originalY;

    /* Check that this code cannot trigger the resizing bug in X.Org Server 1.3.
     * See the work-around in ScreenInit. */
    xf86RandR12GetOriginalVirtualSize(pScrn, &originalX, &originalY);
    AssertMsg(originalX == VBOX_VIDEO_MAX_VIRTUAL && originalY == VBOX_VIDEO_MAX_VIRTUAL, ("OriginalSize=%dx%d",
               originalX, originalY));
    for (i = cFirst; i < cLast; ++i)
        if (pVBox->pScreens[i].paCrtcs->mode.HDisplay != 0 && pVBox->pScreens[i].paCrtcs->mode.VDisplay != 0 && pScrn->vtSema)
            vbvxSetMode(pScrn, i, pVBox->pScreens[i].paCrtcs->mode.HDisplay, pVBox->pScreens[i].paCrtcs->mode.VDisplay,
                        pVBox->pScreens[i].paCrtcs->x, pVBox->pScreens[i].paCrtcs->y, pVBox->pScreens[i].fPowerOn,
                        pVBox->pScreens[i].paOutputs->status == XF86OutputStatusConnected, &frameBuffer);
}

/** Wrapper around setModeRandR12() to avoid exposing non-obvious semantics.
 */
static void setAllModesRandR12(ScrnInfoPtr pScrn)
{
    setModeRandR12(pScrn, 0);
}

/* For descriptions of these functions and structures, see
   hw/xfree86/modes/xf86Crtc.h and hw/xfree86/modes/xf86Modes.h in the
   X.Org source tree. */

static Bool vbox_config_resize(ScrnInfoPtr pScrn, int cw, int ch)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    Bool rc;
    unsigned i;

    TRACE_LOG("width=%d, height=%d\n", cw, ch);
    rc = adjustScreenPixmap(pScrn, cw, ch);
    /* Power-on all screens (the server expects this) and set the new pitch to them. */
    for (i = 0; i < pVBox->cScreens; ++i)
        pVBox->pScreens[i].fPowerOn = true;
    setAllModesRandR12(pScrn);
    vbvxSetSolarisMouseRange(cw, ch);
    return rc;
}

static const xf86CrtcConfigFuncsRec VBOXCrtcConfigFuncs = {
    vbox_config_resize
};

static void
vbox_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    ScrnInfoPtr pScrn = crtc->scrn;
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    unsigned cDisplay = (uintptr_t)crtc->driver_private;

    TRACE_LOG("mode=%d\n", mode);
    pVBox->pScreens[cDisplay].fPowerOn = (mode != DPMSModeOff);
    setModeRandR12(pScrn, cDisplay);
}

static Bool
vbox_crtc_lock (xf86CrtcPtr crtc)
{ RT_NOREF(crtc); return FALSE; }


/* We use this function to check whether the X server owns the active virtual
 * terminal before attempting a mode switch, since the RandR extension isn't
 * very dilligent here, which can mean crashes if we are unlucky.  This is
 * not the way it the function is intended - it is meant for reporting modes
 * which the hardware can't handle.  I hope that this won't confuse any clients
 * connecting to us. */
static Bool
vbox_crtc_mode_fixup (xf86CrtcPtr crtc, DisplayModePtr mode,
                      DisplayModePtr adjusted_mode)
{ RT_NOREF(crtc, mode, adjusted_mode); return TRUE; }

static void
vbox_crtc_stub (xf86CrtcPtr crtc)
{ RT_NOREF(crtc); }

static void
vbox_crtc_mode_set (xf86CrtcPtr crtc, DisplayModePtr mode,
                    DisplayModePtr adjusted_mode, int x, int y)
{
    RT_NOREF(mode);
    VBOXPtr pVBox = VBOXGetRec(crtc->scrn);
    unsigned cDisplay = (uintptr_t)crtc->driver_private;

    TRACE_LOG("name=%s, HDisplay=%d, VDisplay=%d, x=%d, y=%d\n", adjusted_mode->name,
           adjusted_mode->HDisplay, adjusted_mode->VDisplay, x, y);
    pVBox->pScreens[cDisplay].fPowerOn = true;
    pVBox->pScreens[cDisplay].aScreenLocation.cx = adjusted_mode->HDisplay;
    pVBox->pScreens[cDisplay].aScreenLocation.cy = adjusted_mode->VDisplay;
    pVBox->pScreens[cDisplay].aScreenLocation.x = x;
    pVBox->pScreens[cDisplay].aScreenLocation.y = y;
    setModeRandR12(crtc->scrn, cDisplay);
}

static void
vbox_crtc_gamma_set (xf86CrtcPtr crtc, CARD16 *red,
                     CARD16 *green, CARD16 *blue, int size)
{ RT_NOREF(crtc, red, green, blue, size); }

static void *
vbox_crtc_shadow_allocate (xf86CrtcPtr crtc, int width, int height)
{ RT_NOREF(crtc, width, height); return NULL; }

static const xf86CrtcFuncsRec VBOXCrtcFuncs = {
    .dpms = vbox_crtc_dpms,
    .save = NULL, /* These two are never called by the server. */
    .restore = NULL,
    .lock = vbox_crtc_lock,
    .unlock = NULL, /* This will not be invoked if lock returns FALSE. */
    .mode_fixup = vbox_crtc_mode_fixup,
    .prepare = vbox_crtc_stub,
    .mode_set = vbox_crtc_mode_set,
    .commit = vbox_crtc_stub,
    .gamma_set = vbox_crtc_gamma_set,
    .shadow_allocate = vbox_crtc_shadow_allocate,
    .shadow_create = NULL, /* These two should not be invoked if allocate
                              returns NULL. */
    .shadow_destroy = NULL,
    .set_cursor_colors = NULL, /* We are still using the old cursor API. */
    .set_cursor_position = NULL,
    .show_cursor = NULL,
    .hide_cursor = NULL,
    .load_cursor_argb = NULL,
    .destroy = vbox_crtc_stub
};

static void
vbox_output_stub (xf86OutputPtr output)
{ RT_NOREF(output); }

static void
vbox_output_dpms (xf86OutputPtr output, int mode)
{
    RT_NOREF(output, mode);
}

static int
vbox_output_mode_valid (xf86OutputPtr output, DisplayModePtr mode)
{
    return MODE_OK;
}

static Bool
vbox_output_mode_fixup (xf86OutputPtr output, DisplayModePtr mode,
                        DisplayModePtr adjusted_mode)
{ RT_NOREF(output, mode, adjusted_mode); return TRUE; }

static void
vbox_output_mode_set (xf86OutputPtr output, DisplayModePtr mode,
                        DisplayModePtr adjusted_mode)
{ RT_NOREF(output, mode, adjusted_mode); }

static xf86OutputStatus
vbox_output_detect (xf86OutputPtr output)
{
    ScrnInfoPtr pScrn = output->scrn;
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    uint32_t iScreen = (uintptr_t)output->driver_private;
    return   pVBox->pScreens[iScreen].afConnected
           ? XF86OutputStatusConnected : XF86OutputStatusDisconnected;
}

static DisplayModePtr vbox_output_add_mode(VBOXPtr pVBox, DisplayModePtr *pModes, const char *pszName, int x, int y,
                                           Bool isPreferred, Bool isUserDef)
{
    TRACE_LOG("pszName=%s, x=%d, y=%d\n", pszName ? pszName : "(null)", x, y);
    DisplayModePtr pMode = xnfcalloc(1, sizeof(DisplayModeRec));
    int cRefresh = 60;

    pMode->status        = MODE_OK;
    /* We don't ask the host whether it likes user defined modes,
     * as we assume that the user really wanted that mode. */
    pMode->type          = isUserDef ? M_T_USERDEF : M_T_BUILTIN;
    if (isPreferred)
        pMode->type     |= M_T_PREFERRED;
    /* Older versions of VBox only support screen widths which are a multiple
     * of 8 */
    if (pVBox->fAnyX)
        pMode->HDisplay  = x;
    else
        pMode->HDisplay  = x & ~7;
    pMode->HSyncStart    = pMode->HDisplay + 2;
    pMode->HSyncEnd      = pMode->HDisplay + 4;
    pMode->HTotal        = pMode->HDisplay + 6;
    pMode->VDisplay      = y;
    pMode->VSyncStart    = pMode->VDisplay + 2;
    pMode->VSyncEnd      = pMode->VDisplay + 4;
    pMode->VTotal        = pMode->VDisplay + 6;
    pMode->Clock         = pMode->HTotal * pMode->VTotal * cRefresh / 1000; /* kHz */
    if (NULL == pszName) {
        xf86SetModeDefaultName(pMode);
    } else {
        pMode->name          = xnfstrdup(pszName);
    }
    *pModes = xf86ModesAdd(*pModes, pMode);
    return pMode;
}

static DisplayModePtr
vbox_output_get_modes (xf86OutputPtr output)
{
    DisplayModePtr pModes = NULL;
    DisplayModePtr pPreferred = NULL;
    ScrnInfoPtr pScrn = output->scrn;
    VBOXPtr pVBox = VBOXGetRec(pScrn);

    TRACE_ENTRY();
    uint32_t iScreen = (uintptr_t)output->driver_private;
    pPreferred = vbox_output_add_mode(pVBox, &pModes, NULL,
                         RT_CLAMP(pVBox->pScreens[iScreen].aPreferredSize.cx, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MAX_VIRTUAL),
                         RT_CLAMP(pVBox->pScreens[iScreen].aPreferredSize.cy, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MAX_VIRTUAL),
                         TRUE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 2560, 1600, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 2560, 1440, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 2048, 1536, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 1920, 1600, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 1920, 1080, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 1680, 1050, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 1600, 1200, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 1400, 1050, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 1280, 1024, FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 1024, 768,  FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 800,  600,  FALSE, FALSE);
    vbox_output_add_mode(pVBox, &pModes, NULL, 640,  480,  FALSE, FALSE);
    VBOXEDIDSet(output, pPreferred);
    TRACE_EXIT();
    return pModes;
}

static const xf86OutputFuncsRec VBOXOutputFuncs = {
    .create_resources = vbox_output_stub,
    .dpms = vbox_output_dpms,
    .save = NULL, /* These two are never called by the server. */
    .restore = NULL,
    .mode_valid = vbox_output_mode_valid,
    .mode_fixup = vbox_output_mode_fixup,
    .prepare = vbox_output_stub,
    .commit = vbox_output_stub,
    .mode_set = vbox_output_mode_set,
    .detect = vbox_output_detect,
    .get_modes = vbox_output_get_modes,
#ifdef RANDR_12_INTERFACE
     .set_property = NULL,
#endif
    .destroy = vbox_output_stub
};
#endif /* VBOXVIDEO_13 */

/* Module loader interface */
static MODULESETUPPROTO(vboxSetup);

static XF86ModuleVersionInfo vboxVersionRec =
{
    VBOX_DRIVER_NAME,
    "Oracle Corporation",
    MODINFOSTRING1,
    MODINFOSTRING2,
#ifdef XORG_7X
    XORG_VERSION_CURRENT,
#else
    XF86_VERSION_CURRENT,
#endif
    1,                          /* Module major version. Xorg-specific */
    0,                          /* Module minor version. Xorg-specific */
    1,                          /* Module patchlevel. Xorg-specific */
    ABI_CLASS_VIDEODRV,         /* This is a video driver */
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0}
};

/*
 * This data is accessed by the loader.  The name must be the module name
 * followed by "ModuleData".
 */
#ifdef XORG_7X
_X_EXPORT
#endif
XF86ModuleData vboxvideoModuleData = { &vboxVersionRec, vboxSetup, NULL };

static pointer
vboxSetup(pointer Module, pointer Options, int *ErrorMajor, int *ErrorMinor)
{
    static Bool Initialised = FALSE;
    RT_NOREF(Options, ErrorMinor);

    if (!Initialised)
    {
        Initialised = TRUE;
#ifdef PCIACCESS
        xf86AddDriver(&VBOXVIDEO, Module, HaveDriverFuncs);
#else
        xf86AddDriver(&VBOXVIDEO, Module, 0);
#endif
#ifndef XORG_7X
        LoaderRefSymLists(fbSymbols,
                          shadowfbSymbols,
                          ramdacSymbols,
                          vgahwSymbols,
                          NULL);
#endif
        xf86Msg(X_CONFIG, "Load address of symbol \"VBOXVIDEO\" is %p\n",
                (void *)&VBOXVIDEO);
        return (pointer)TRUE;
    }

    if (ErrorMajor)
        *ErrorMajor = LDR_ONCEONLY;
    return (NULL);
}


static const OptionInfoRec *
VBOXAvailableOptions(int chipid, int busid)
{
    RT_NOREF(chipid, busid);
    return (VBOXOptions);
}

static void
VBOXIdentify(int flags)
{
    RT_NOREF(flags);
    xf86PrintChipsets(VBOX_NAME, "guest driver for VirtualBox", VBOXChipsets);
}

#ifndef XF86_SCRN_INTERFACE
# define SCRNINDEXAPI(pfn) pfn ## Index
static Bool VBOXScreenInitIndex(int scrnIndex, ScreenPtr pScreen, int argc, char **argv)
{
    RT_NOREF(scrnIndex);
    return VBOXScreenInit(pScreen, argc, argv);
}

static Bool VBOXEnterVTIndex(int scrnIndex, int flags)
{ RT_NOREF(flags); return VBOXEnterVT(xf86Screens[scrnIndex]); }

static void VBOXLeaveVTIndex(int scrnIndex, int flags)
{ RT_NOREF(flags); VBOXLeaveVT(xf86Screens[scrnIndex]); }

static Bool VBOXCloseScreenIndex(int scrnIndex, ScreenPtr pScreen)
{ RT_NOREF(scrnIndex); return VBOXCloseScreen(pScreen); }

static Bool VBOXSwitchModeIndex(int scrnIndex, DisplayModePtr pMode, int flags)
{ RT_NOREF(flags); return VBOXSwitchMode(xf86Screens[scrnIndex], pMode); }

static void VBOXAdjustFrameIndex(int scrnIndex, int x, int y, int flags)
{ RT_NOREF(flags); VBOXAdjustFrame(xf86Screens[scrnIndex], x, y); }

static void VBOXFreeScreenIndex(int scrnIndex, int flags)
{ RT_NOREF(flags); VBOXFreeScreen(xf86Screens[scrnIndex]); }
# else
# define SCRNINDEXAPI(pfn) pfn
#endif /* XF86_SCRN_INTERFACE */

static void setScreenFunctions(ScrnInfoPtr pScrn, xf86ProbeProc pfnProbe)
{
    pScrn->driverVersion = VBOX_VERSION;
    pScrn->driverName    = VBOX_DRIVER_NAME;
    pScrn->name          = VBOX_NAME;
    pScrn->Probe         = pfnProbe;
    pScrn->PreInit       = VBOXPreInit;
    pScrn->ScreenInit    = SCRNINDEXAPI(VBOXScreenInit);
    pScrn->SwitchMode    = SCRNINDEXAPI(VBOXSwitchMode);
    pScrn->AdjustFrame   = SCRNINDEXAPI(VBOXAdjustFrame);
    pScrn->EnterVT       = SCRNINDEXAPI(VBOXEnterVT);
    pScrn->LeaveVT       = SCRNINDEXAPI(VBOXLeaveVT);
    pScrn->FreeScreen    = SCRNINDEXAPI(VBOXFreeScreen);
}

/*
 * One of these functions is called once, at the start of the first server
 * generation to do a minimal probe for supported hardware.
 */

#ifdef PCIACCESS
static Bool
VBOXPciProbe(DriverPtr drv, int entity_num, struct pci_device *dev,
             intptr_t match_data)
{
    ScrnInfoPtr pScrn;
    int drmFd;

    TRACE_ENTRY();

    drmFd = open("/dev/dri/card0", O_RDWR, 0);
    if (drmFd >= 0)
    {
        xf86Msg(X_INFO, "vboxvideo: kernel driver found, not loading.\n");
        close(drmFd);
        return FALSE;
    }
    /* It is safe to call this, as the X server enables I/O access before
     * calling the probe call-backs. */
    if (!xf86EnableIO())
    {
        xf86Msg(X_INFO, "vboxvideo: this driver requires direct hardware access.  You may wish to use the kernel driver instead.\n");
        return FALSE;
    }
    pScrn = xf86ConfigPciEntity(NULL, 0, entity_num, VBOXPCIchipsets,
                                NULL, NULL, NULL, NULL, NULL);
    if (pScrn != NULL) {
        VBOXPtr pVBox;

        VBOXSetRec(pScrn);
        pVBox = VBOXGetRec(pScrn);
        if (!pVBox)
            return FALSE;
        setScreenFunctions(pScrn, NULL);
        pVBox->pciInfo = dev;
    }

    TRACE_LOG("returning %s\n", pScrn == NULL ? "false" : "true");
    return (pScrn != NULL);
}
#endif

#ifndef PCIACCESS
static Bool
VBOXProbe(DriverPtr drv, int flags)
{
    Bool foundScreen = FALSE;
    int numDevSections;
    GDevPtr *devSections;

    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */
    if ((numDevSections = xf86MatchDevice(VBOX_NAME,
                      &devSections)) <= 0)
    return (FALSE);

    /* PCI BUS */
    if (xf86GetPciVideoInfo())
    {
        int numUsed;
        int *usedChips;
        int i;
        numUsed = xf86MatchPciInstances(VBOX_NAME, VBOX_VENDORID,
                        VBOXChipsets, VBOXPCIchipsets,
                        devSections, numDevSections,
                        drv, &usedChips);
        if (numUsed > 0)
        {
            if (flags & PROBE_DETECT)
                foundScreen = TRUE;
            else
                for (i = 0; i < numUsed; i++)
                {
                    ScrnInfoPtr pScrn = NULL;
                    /* Allocate a ScrnInfoRec  */
                    if ((pScrn = xf86ConfigPciEntity(pScrn,0,usedChips[i],
                                     VBOXPCIchipsets,NULL,
                                     NULL,NULL,NULL,NULL)))
                    {
                        setScreenFunctions(pScrn, VBOXProbe);
                        foundScreen = TRUE;
                    }
                }
            free(usedChips);
        }
    }
    free(devSections);
    return (foundScreen);
}
#endif


/*
 * QUOTE from the XFree86 DESIGN document:
 *
 * The purpose of this function is to find out all the information
 * required to determine if the configuration is usable, and to initialise
 * those parts of the ScrnInfoRec that can be set once at the beginning of
 * the first server generation.
 *
 * (...)
 *
 * This includes probing for video memory, clocks, ramdac, and all other
 * HW info that is needed. It includes determining the depth/bpp/visual
 * and related info. It includes validating and determining the set of
 * video modes that will be used (and anything that is required to
 * determine that).
 *
 * This information should be determined in the least intrusive way
 * possible. The state of the HW must remain unchanged by this function.
 * Although video memory (including MMIO) may be mapped within this
 * function, it must be unmapped before returning.
 *
 * END QUOTE
 */

static Bool
VBOXPreInit(ScrnInfoPtr pScrn, int flags)
{
    VBOXPtr pVBox;
    Gamma gzeros = {0.0, 0.0, 0.0};
    rgb rzeros = {0, 0, 0};

    TRACE_ENTRY();
    /* Are we really starting the server, or is this just a dummy run? */
    if (flags & PROBE_DETECT)
        return (FALSE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "VirtualBox guest additions video driver version %d.%d\n",
               VBOX_VERSION_MAJOR, VBOX_VERSION_MINOR);

    /* The ramdac module is needed for the hardware cursor. */
    if (!xf86LoadSubModule(pScrn, "ramdac"))
        return FALSE;

    /* The framebuffer module. */
    if (!xf86LoadSubModule(pScrn, "fb"))
        return (FALSE);

    if (!xf86LoadSubModule(pScrn, "shadowfb"))
        return FALSE;

    if (!xf86LoadSubModule(pScrn, "vgahw"))
        return FALSE;

    /* Get our private data from the ScrnInfoRec structure. */
    VBOXSetRec(pScrn);
    pVBox = VBOXGetRec(pScrn);
    if (!pVBox)
        return FALSE;

    /* Entity information seems to mean bus information. */
    pVBox->pEnt = xf86GetEntityInfo(pScrn->entityList[0]);

#ifndef PCIACCESS
    if (pVBox->pEnt->location.type != BUS_PCI)
        return FALSE;

    pVBox->pciInfo = xf86GetPciInfoForEntity(pVBox->pEnt->index);
    pVBox->pciTag = pciTag(pVBox->pciInfo->bus,
                           pVBox->pciInfo->device,
                           pVBox->pciInfo->func);
#endif

    /* Set up our ScrnInfoRec structure to describe our virtual
       capabilities to X. */

    pScrn->chipset = "vbox";
    /** @note needed during colourmap initialisation */
    pScrn->rgbBits = 8;

    /* Let's create a nice, capable virtual monitor. */
    pScrn->monitor = pScrn->confScreen->monitor;
    pScrn->monitor->DDC = NULL;
    pScrn->monitor->nHsync = 1;
    pScrn->monitor->hsync[0].lo = 1;
    pScrn->monitor->hsync[0].hi = 10000;
    pScrn->monitor->nVrefresh = 1;
    pScrn->monitor->vrefresh[0].lo = 1;
    pScrn->monitor->vrefresh[0].hi = 100;

    pScrn->progClock = TRUE;

    /* Using the PCI information caused problems with non-powers-of-two
       sized video RAM configurations */
    pVBox->cbFBMax = VBoxVideoGetVRAMSize();
    pScrn->videoRam = pVBox->cbFBMax / 1024;

    /* Check if the chip restricts horizontal resolution or not. */
    pVBox->fAnyX = VBoxVideoAnyWidthAllowed();

    /* Set up clock information that will support all modes we need. */
    pScrn->clockRanges = xnfcalloc(sizeof(ClockRange), 1);
    pScrn->clockRanges->minClock = 1000;
    pScrn->clockRanges->maxClock = 1000000000;
    pScrn->clockRanges->clockIndex = -1;
    pScrn->clockRanges->ClockMulFactor = 1;
    pScrn->clockRanges->ClockDivFactor = 1;

    if (!xf86SetDepthBpp(pScrn, 24, 0, 0, Support32bppFb))
        return FALSE;
    /* We only support 16 and 24 bits depth (i.e. 16 and 32bpp) */
    if (pScrn->bitsPerPixel != 32 && pScrn->bitsPerPixel != 16)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "The VBox additions only support 16 and 32bpp graphics modes\n");
        return FALSE;
    }
    xf86PrintDepthBpp(pScrn);
    vboxAddModes(pScrn);

#ifdef VBOXVIDEO_13
    pScrn->virtualX = VBOX_VIDEO_MAX_VIRTUAL;
    pScrn->virtualY = VBOX_VIDEO_MAX_VIRTUAL;
#else
    /* We don't validate with xf86ValidateModes and xf86PruneModes as we
     * already know what we like and what we don't. */

    pScrn->currentMode = pScrn->modes;

    /* Set the right virtual resolution. */
    pScrn->virtualX = pScrn->bitsPerPixel == 16 ? (pScrn->currentMode->HDisplay + 1) & ~1 : pScrn->currentMode->HDisplay;
    pScrn->virtualY = pScrn->currentMode->VDisplay;

#endif /* !VBOXVIDEO_13 */

    pScrn->displayWidth = pScrn->virtualX;

    xf86PrintModes(pScrn);

    /* VGA hardware initialisation */
    if (!vgaHWGetHWRec(pScrn))
        return FALSE;
    /* Must be called before any VGA registers are saved or restored */
    vgaHWSetStdFuncs(VGAHWPTR(pScrn));
    vgaHWGetIOBase(VGAHWPTR(pScrn));

    /* Colour weight - we always call this, since we are always in
       truecolour. */
    if (!xf86SetWeight(pScrn, rzeros, rzeros))
        return (FALSE);

    /* visual init */
    if (!xf86SetDefaultVisual(pScrn, -1))
        return (FALSE);

    xf86SetGamma(pScrn, gzeros);

    /* Set the DPI.  Perhaps we should read this from the host? */
    xf86SetDpi(pScrn, 96, 96);

    if (pScrn->memPhysBase == 0) {
#ifdef PCIACCESS
        pScrn->memPhysBase = pVBox->pciInfo->regions[0].base_addr;
#else
        pScrn->memPhysBase = pVBox->pciInfo->memBase[0];
#endif
        pScrn->fbOffset = 0;
    }

    TRACE_EXIT();
    return (TRUE);
}

/**
 * Dummy function for setting the colour palette, which we actually never
 * touch.  However, the server still requires us to provide this.
 */
static void
vboxLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
          LOCO *colors, VisualPtr pVisual)
{
    RT_NOREF(pScrn, numColors, indices, colors, pVisual);
}

/** Set the graphics and guest cursor support capabilities to the host if
 *  the user-space helper is running. */
static void updateGraphicsCapability(ScrnInfoPtr pScrn, Bool hasVT)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);

    if (!pVBox->fHaveHGSMIModeHints)
        return;
    VBoxHGSMISendCapsInfo(&pVBox->guestCtx,   hasVT
                                            ? VBVACAPS_VIDEO_MODE_HINTS | VBVACAPS_DISABLE_CURSOR_INTEGRATION
                                            : VBVACAPS_DISABLE_CURSOR_INTEGRATION);
}

#ifndef VBOXVIDEO_13

#define PREFERRED_MODE_ATOM_NAME "VBOXVIDEO_PREFERRED_MODE"

static void setSizesRandR11(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    DisplayModePtr pNewMode;
    int32_t propertyValue;

    pNewMode = pScrn->modes != pScrn->currentMode ? pScrn->modes : pScrn->modes->next;
    pNewMode->HDisplay = RT_CLAMP(pVBox->pScreens[0].aPreferredSize.cx, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MAX_VIRTUAL);
    pNewMode->VDisplay = RT_CLAMP(pVBox->pScreens[0].aPreferredSize.cy, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MAX_VIRTUAL);
    propertyValue = (pNewMode->HDisplay << 16) + pNewMode->VDisplay;
    ChangeWindowProperty(ROOT_WINDOW(pScrn), MakeAtom(PREFERRED_MODE_ATOM_NAME,
                         sizeof(PREFERRED_MODE_ATOM_NAME) - 1, TRUE), XA_INTEGER, 32,
                         PropModeReplace, 1, &propertyValue, TRUE);
}

#endif

static void reprobeCursor(ScrnInfoPtr pScrn)
{
    if (ROOT_WINDOW(pScrn) == NULL)
        return;
#ifdef XF86_SCRN_INTERFACE
    pScrn->EnableDisableFBAccess(pScrn, FALSE);
    pScrn->EnableDisableFBAccess(pScrn, TRUE);
#else
    pScrn->EnableDisableFBAccess(pScrn->scrnIndex, FALSE);
    pScrn->EnableDisableFBAccess(pScrn->scrnIndex, TRUE);
#endif
}

static void setSizesAndCursorIntegration(ScrnInfoPtr pScrn, Bool fScreenInitTime)
{
    RT_NOREF(fScreenInitTime);
    TRACE_LOG("fScreenInitTime=%d\n", (int)fScreenInitTime);
#ifdef VBOXVIDEO_13
# if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) >= 5
    RRGetInfo(xf86ScrnToScreen(pScrn), TRUE);
# else
    RRGetInfo(xf86ScrnToScreen(pScrn));
# endif
#else
    setSizesRandR11(pScrn);
#endif
    /* This calls EnableDisableFBAccess(), so only use when switched in. */
    if (pScrn->vtSema)
        reprobeCursor(pScrn);
}

/* We update the size hints from the X11 property set by VBoxClient every time
 * that the X server goes to sleep (to catch the property change request).
 * Although this is far more often than necessary it should not have real-life
 * performance consequences and allows us to simplify the code quite a bit. */
static void vboxBlockHandler(pointer pData,
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 23
                             OSTimePtr pTimeout,
                             pointer pReadmask
#else
                             void *pTimeout
#endif
                  )
{
    ScrnInfoPtr pScrn = (ScrnInfoPtr)pData;
    Bool fNeedUpdate = false;

    RT_NOREF(pTimeout);
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 23
    RT_NOREF(pReadmask);
#endif
    if (pScrn->vtSema)
        vbvxReadSizesAndCursorIntegrationFromHGSMI(pScrn, &fNeedUpdate);
    if (fNeedUpdate)
        setSizesAndCursorIntegration(pScrn, false);
}

/*
 * QUOTE from the XFree86 DESIGN document:
 *
 * This is called at the start of each server generation.
 *
 * (...)
 *
 * Decide which operations need to be placed under resource access
 * control. (...) Map any video memory or other memory regions. (...)
 * Save the video card state. (...) Initialise the initial video
 * mode.
 *
 * End QUOTE.
 */
static Bool VBOXScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    VisualPtr visual;
    RT_NOREF(argc, argv);

    TRACE_ENTRY();

    if (!VBOXMapVidMem(pScrn))
        return (FALSE);

    /* save current video state */
    VBOXSaveMode(pScrn);

    /* mi layer - reset the visual list (?)*/
    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, TrueColorMask,
                          pScrn->rgbBits, TrueColor))
        return (FALSE);
    if (!miSetPixmapDepths())
        return (FALSE);

    if (!fbScreenInit(pScreen, pVBox->base,
                      pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi,
                      pScrn->displayWidth, pScrn->bitsPerPixel))
        return (FALSE);

    /* Fixup RGB ordering */
    /** @note the X server uses this even in true colour. */
    visual = pScreen->visuals + pScreen->numVisuals;
    while (--visual >= pScreen->visuals) {
        if ((visual->class | DynamicClass) == DirectColor) {
            visual->offsetRed   = pScrn->offset.red;
            visual->offsetGreen = pScrn->offset.green;
            visual->offsetBlue  = pScrn->offset.blue;
            visual->redMask     = pScrn->mask.red;
            visual->greenMask   = pScrn->mask.green;
            visual->blueMask    = pScrn->mask.blue;
        }
    }

    /* must be after RGB ordering fixed */
    fbPictureInit(pScreen, 0, 0);

    xf86SetBlackWhitePixels(pScreen);
    pScrn->vtSema = TRUE;

#if defined(VBOXVIDEO_13) && defined(RT_OS_LINUX)
    vbvxSetUpLinuxACPI(pScreen);
#endif

    if (!VBoxHGSMIIsSupported())
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Graphics device too old to support.\n");
        return FALSE;
    }
    vbvxSetUpHGSMIHeapInGuest(pVBox, pScrn->videoRam * 1024);
    pVBox->cScreens = VBoxHGSMIGetMonitorCount(&pVBox->guestCtx);
    pVBox->pScreens = xnfcalloc(pVBox->cScreens, sizeof(*pVBox->pScreens));
    pVBox->paVBVAModeHints = xnfcalloc(pVBox->cScreens, sizeof(*pVBox->paVBVAModeHints));
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Requested monitor count: %u\n", pVBox->cScreens);
    vboxEnableVbva(pScrn);
    /* Set up the dirty rectangle handler.  It will be added into a function
     * chain and gets removed when the screen is cleaned up. */
    if (ShadowFBInit2(pScreen, NULL, vbvxHandleDirtyRect) != TRUE)
        return FALSE;
    VBoxInitialiseSizeHints(pScrn);

#ifdef VBOXVIDEO_13
    /* Initialise CRTC and output configuration for use with randr1.2. */
    xf86CrtcConfigInit(pScrn, &VBOXCrtcConfigFuncs);

    {
        uint32_t i;

        for (i = 0; i < pVBox->cScreens; ++i)
        {
            char szOutput[256];

            /* Setup our virtual CRTCs. */
            pVBox->pScreens[i].paCrtcs = xf86CrtcCreate(pScrn, &VBOXCrtcFuncs);
            pVBox->pScreens[i].paCrtcs->driver_private = (void *)(uintptr_t)i;

            /* Set up our virtual outputs. */
            snprintf(szOutput, sizeof(szOutput), "VGA-%u", i);
            pVBox->pScreens[i].paOutputs
                = xf86OutputCreate(pScrn, &VBOXOutputFuncs, szOutput);

            /* We are not interested in the monitor section in the
             * configuration file. */
            xf86OutputUseScreenMonitor(pVBox->pScreens[i].paOutputs, FALSE);
            pVBox->pScreens[i].paOutputs->possible_crtcs = 1 << i;
            pVBox->pScreens[i].paOutputs->possible_clones = 0;
            pVBox->pScreens[i].paOutputs->driver_private = (void *)(uintptr_t)i;
            TRACE_LOG("Created crtc (%p) and output %s (%p)\n",
                      (void *)pVBox->pScreens[i].paCrtcs, szOutput,
                      (void *)pVBox->pScreens[i].paOutputs);
        }
    }

    /* Set a sane minimum and maximum mode size to match what the hardware
     * supports. */
    xf86CrtcSetSizeRange(pScrn, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MAX_VIRTUAL, VBOX_VIDEO_MAX_VIRTUAL);

    /* Now create our initial CRTC/output configuration. */
    if (!xf86InitialConfiguration(pScrn, TRUE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Initial CRTC configuration failed!\n");
        return (FALSE);
    }

    /* Work around a bug in the original X server modesetting code, which took
     * the first valid values set to these two as maxima over the server
     * lifetime.  This bug was introduced on Feb 15 2007 and was fixed in commit
     * fa877d7f three months later, so it was present in X.Org Server 1.3. */
    pScrn->virtualX = VBOX_VIDEO_MAX_VIRTUAL;
    pScrn->virtualY = VBOX_VIDEO_MAX_VIRTUAL;

    /* Initialise randr 1.2 mode-setting functions. */
    if (!xf86CrtcScreenInit(pScreen)) {
        return FALSE;
    }

    /* set first video mode */
    if (!xf86SetDesiredModes(pScrn)) {
        return FALSE;
    }
#else  /* !VBOXVIDEO_13 */
    /* set first video mode */
    setModeRandR11(pScrn, pScrn->currentMode, true, false, 0, 0);
#endif /* !VBOXVIDEO_13 */

    /* Say that we support graphics. */
    updateGraphicsCapability(pScrn, TRUE);

#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) >= 23
# define WakeupHandlerProcPtr ServerWakeupHandlerProcPtr
#endif

    /* Register block and wake-up handlers for getting new screen size hints. */
    RegisterBlockAndWakeupHandlers(vboxBlockHandler, (WakeupHandlerProcPtr)NoopDDA, (pointer)pScrn);

    /* software cursor */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    /* colourmap code */
    if (!miCreateDefColormap(pScreen))
        return (FALSE);

    if(!xf86HandleColormaps(pScreen, 256, 8, vboxLoadPalette, NULL, 0))
        return (FALSE);

    pVBox->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = SCRNINDEXAPI(VBOXCloseScreen);
#ifdef VBOXVIDEO_13
    pScreen->SaveScreen = xf86SaveScreen;
#else
    pScreen->SaveScreen = VBOXSaveScreen;
#endif

#ifdef VBOXVIDEO_13
    xf86DPMSInit(pScreen, xf86DPMSSet, 0);
#else
    /* We probably do want to support power management - even if we just use
       a dummy function. */
    xf86DPMSInit(pScreen, VBOXDisplayPowerManagementSet, 0);
#endif

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1)
        xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

    if (vbvxCursorInit(pScreen) != TRUE)
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Unable to start the VirtualBox mouse pointer integration with the host system.\n");

    return (TRUE);
}

#define NO_VT_ATOM_NAME "VBOXVIDEO_NO_VT"

static Bool VBOXEnterVT(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
#ifndef VBOXVIDEO_13
    /* If we got a mode request while we were switched out, temporarily override
     * the physical mode set to the device while keeping things consistent from
     * the server's point of view. */
    int cXOverRide = RT_CLAMP(pVBox->pScreens[0].aPreferredSize.cx, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MAX_VIRTUAL);
    int cYOverRide = RT_CLAMP(pVBox->pScreens[0].aPreferredSize.cy, VBOX_VIDEO_MIN_SIZE, VBOX_VIDEO_MAX_VIRTUAL);
#endif

    TRACE_ENTRY();
    vbvxSetUpHGSMIHeapInGuest(pVBox, pScrn->videoRam * 1024);
    vboxEnableVbva(pScrn);
    /* Re-set video mode */
#ifdef VBOXVIDEO_13
    if (!xf86SetDesiredModes(pScrn)) {
        return FALSE;
    }
#else
    setModeRandR11(pScrn, pScrn->currentMode, false, true, cXOverRide, cYOverRide);
    DeleteProperty(ROOT_WINDOW(pScrn), MakeAtom(NO_VT_ATOM_NAME, sizeof(NO_VT_ATOM_NAME) - 1, TRUE));
#endif
    updateGraphicsCapability(pScrn, TRUE);
    return TRUE;
}

static void VBOXLeaveVT(ScrnInfoPtr pScrn)
{
#ifdef VBOXVIDEO_13
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    unsigned i;
#else
    int32_t propertyValue = 0;
#endif

    TRACE_ENTRY();
#ifdef VBOXVIDEO_13
    for (i = 0; i < pVBox->cScreens; ++i)
        vbox_crtc_dpms(pVBox->pScreens[i].paCrtcs, DPMSModeOff);
#else
    ChangeWindowProperty(ROOT_WINDOW(pScrn), MakeAtom(NO_VT_ATOM_NAME, sizeof(NO_VT_ATOM_NAME) - 1, FALSE), XA_INTEGER, 32,
                         PropModeReplace, 1, &propertyValue, TRUE);
#endif
    updateGraphicsCapability(pScrn, FALSE);
    vboxDisableVbva(pScrn);
    vbvxClearVRAM(pScrn, ((size_t)pScrn->virtualX) * pScrn->virtualY * (pScrn->bitsPerPixel / 8), 0);
    VBOXRestoreMode(pScrn);
    TRACE_EXIT();
}

static Bool VBOXCloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    BOOL ret;

    if (pScrn->vtSema)
    {
#ifdef VBOXVIDEO_13
        unsigned i;

        for (i = 0; i < pVBox->cScreens; ++i)
            vbox_crtc_dpms(pVBox->pScreens[i].paCrtcs, DPMSModeOff);
#endif
        vboxDisableVbva(pScrn);
        vbvxClearVRAM(pScrn, ((size_t)pScrn->virtualX) * pScrn->virtualY * (pScrn->bitsPerPixel / 8), 0);
    }
    if (pScrn->vtSema)
        VBOXRestoreMode(pScrn);
    if (pScrn->vtSema)
        VBOXUnmapVidMem(pScrn);
    pScrn->vtSema = FALSE;

    vbvxCursorTerm(pVBox);

    pScreen->CloseScreen = pVBox->CloseScreen;
#if defined(VBOXVIDEO_13) && defined(RT_OS_LINUX)
    vbvxCleanUpLinuxACPI(pScreen);
#endif
#ifndef XF86_SCRN_INTERFACE
    ret = pScreen->CloseScreen(pScreen->myNum, pScreen);
#else
    ret = pScreen->CloseScreen(pScreen);
#endif
    return ret;
}

static Bool VBOXSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr pMode)
{
    Bool rc = TRUE;

    TRACE_LOG("HDisplay=%d, VDisplay=%d\n", pMode->HDisplay, pMode->VDisplay);
#ifdef VBOXVIDEO_13
    rc = xf86SetSingleMode(pScrn, pMode, RR_Rotate_0);
#else
    setModeRandR11(pScrn, pMode, false, false, 0, 0);
#endif
    TRACE_LOG("returning %s\n", rc ? "TRUE" : "FALSE");
    return rc;
}

static void VBOXAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{ RT_NOREF(pScrn, x, y); }

static void VBOXFreeScreen(ScrnInfoPtr pScrn)
{
    /* Destroy the VGA hardware record */
    vgaHWFreeHWRec(pScrn);
    /* And our private record */
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static Bool
VBOXMapVidMem(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    Bool rc = TRUE;

    TRACE_ENTRY();
    if (!pVBox->base)
    {
#ifdef PCIACCESS
        (void) pci_device_map_range(pVBox->pciInfo,
                                    pScrn->memPhysBase,
                                    pScrn->videoRam * 1024,
                                    PCI_DEV_MAP_FLAG_WRITABLE,
                                    & pVBox->base);
#else
        pVBox->base = xf86MapPciMem(pScrn->scrnIndex,
                                    VIDMEM_FRAMEBUFFER,
                                    pVBox->pciTag, pScrn->memPhysBase,
                                    (unsigned) pScrn->videoRam * 1024);
#endif
        if (!pVBox->base)
            rc = FALSE;
    }
    TRACE_LOG("returning %s\n", rc ? "TRUE" : "FALSE");
    return rc;
}

static void
VBOXUnmapVidMem(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);

    TRACE_ENTRY();
    if (pVBox->base == NULL)
        return;

#ifdef PCIACCESS
    (void) pci_device_unmap_range(pVBox->pciInfo,
                                  pVBox->base,
                                  pScrn->videoRam * 1024);
#else
    xf86UnMapVidMem(pScrn->scrnIndex, pVBox->base,
                    (unsigned) pScrn->videoRam * 1024);
#endif
    pVBox->base = NULL;
    TRACE_EXIT();
}

#ifndef VBOXVIDEO_13
static Bool
VBOXSaveScreen(ScreenPtr pScreen, int mode)
{
    RT_NOREF(pScreen, mode);
    return TRUE;
}
#endif

void
VBOXSaveMode(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    vgaRegPtr vgaReg;

    TRACE_ENTRY();
    vgaReg = &VGAHWPTR(pScrn)->SavedReg;
    vgaHWSave(pScrn, vgaReg, VGA_SR_ALL);
    pVBox->fSavedVBEMode = VBoxVideoGetModeRegisters(&pVBox->cSavedWidth,
                                                     &pVBox->cSavedHeight,
                                                     &pVBox->cSavedPitch,
                                                     &pVBox->cSavedBPP,
                                                     &pVBox->fSavedFlags);
}

void
VBOXRestoreMode(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    vgaRegPtr vgaReg;

    TRACE_ENTRY();
    vgaReg = &VGAHWPTR(pScrn)->SavedReg;
    vgaHWRestore(pScrn, vgaReg, VGA_SR_ALL);
    if (pVBox->fSavedVBEMode)
        VBoxVideoSetModeRegisters(pVBox->cSavedWidth, pVBox->cSavedHeight,
                                  pVBox->cSavedPitch, pVBox->cSavedBPP,
                                  pVBox->fSavedFlags, 0, 0);
    else
        VBoxVideoDisableVBE();
}

#ifndef VBOXVIDEO_13
static void
VBOXDisplayPowerManagementSet(ScrnInfoPtr pScrn, int mode, int flags)
{
    RT_NOREF(pScrn, mode, flags);
}
#endif
