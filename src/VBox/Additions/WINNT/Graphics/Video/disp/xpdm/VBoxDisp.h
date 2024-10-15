/* $Id: VBoxDisp.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver
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
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDisp_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDisp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxDispInternal.h"
#include "VBoxDispVrdpBmp.h"

/* VirtualBox display driver version, could be seen in Control Panel */
#define VBOXDISPDRIVERVERSION 0x01UL

#if (VBOXDISPDRIVERVERSION & (~0xFFUL))
#error VBOXDISPDRIVERVERSION can't be more than 0xFF
#endif

#define VBOXDISP_DEVICE_NAME L"VBoxDisp"

/* Current mode info */
typedef struct _VBOXDISPCURRENTMODE
{
    ULONG ulIndex;                      /* miniport's video mode index */
    ULONG ulWidth, ulHeight;            /* visible screen width and height */
    ULONG ulBitsPerPel;                 /* number of bits per pel */
    LONG  lScanlineStride;              /* distance between scanlines */
    FLONG flMaskR, flMaskG, flMaskB;    /* RGB mask */
    ULONG ulPaletteShift;               /* number of bits we have to shift 888 palette to match device palette */
} VBOXDISPCURRENTMODE, *PVBOXDISPCURRENTMODE;

/* Pointer related info */
typedef struct _VBOXDISPPOINTERINFO
{
    VIDEO_POINTER_CAPABILITIES caps;    /* Pointer capabilities */
    PVIDEO_POINTER_ATTRIBUTES pAttrs;   /* Preallocated buffer to pass pointer shape to miniport driver */
    DWORD  cbAttrs;                     /* Size of pAttrs buffer */
    POINTL orgHotSpot;                  /* Hot spot origin */
} VBOXDISPPOINTERINFO, *PVBOXDISPPOINTERINFO;

/* Surface info */
typedef struct _VBOXDISPSURF
{
    HBITMAP  hBitmap;        /* GDI's handle to framebuffer bitmap */
    SURFOBJ* psoBitmap;      /* lock pointer to framebuffer bitmap */
    HSURF    hSurface;       /* GDI's handle to framebuffer device-managed surface */
    ULONG    ulFormat;       /* Bitmap format, one of BMF_XXBPP */
} VBOXDISPSURF, *PVBOXDISPSURF;

/* VRAM Layout */
typedef struct _VBOXDISPVRAMLAYOUT
{
    ULONG cbVRAM;

    ULONG offFramebuffer, cbFramebuffer;
    ULONG offDDrawHeap, cbDDrawHeap;
    ULONG offVBVABuffer, cbVBVABuffer;
    ULONG offDisplayInfo, cbDisplayInfo;
} VBOXDISPVRAMLAYOUT;

/* HGSMI info */
typedef struct _VBOXDISPHGSMIINFO
{
    BOOL bSupported;               /* HGSMI is supported and enabled */

    HGSMIQUERYCALLBACKS mp;        /* HGSMI miniport's callbacks and context */
    HGSMIGUESTCOMMANDCONTEXT ctx;  /* HGSMI guest context */
} VBOXDISPHGSMIINFO;

/* Saved screen bits information. */
typedef struct _SSB
{
    ULONG ident;   /* 1 based index in the stack = the handle returned by VBoxDispDrvSaveScreenBits (SS_SAVE) */
    BYTE *pBuffer; /* Buffer where screen bits are saved. */
} SSB;

#ifdef VBOX_WITH_DDRAW
/* DirectDraw surface lock information */
typedef struct _VBOXDDLOCKINFO
{
    BOOL bLocked;
    RECTL rect;
} VBOXDDLOCKINFO;
#endif

/* Structure holding driver private device info. */
typedef struct _VBOXDISPDEV
{
    HANDLE hDriver;                          /* Display device handle which was passed to VBoxDispDrvEnablePDEV */
    HDEV   hDevGDI;                          /* GDI's handle for PDEV created in VBoxDispDrvEnablePDEV */

    VBOXDISPCURRENTMODE mode;                /* Current device mode */
    ULONG iDevice;                           /* Miniport's device index */
    POINTL orgDev;                           /* Device origin for DualView (0,0 is primary) */
    POINTL orgDisp;                          /* Display origin in virtual desktop, NT4 only */

    VBOXDISPPOINTERINFO pointer;             /* Pointer info */

    HPALETTE hDefaultPalette;                /* Default palette handle */
    PALETTEENTRY *pPalette;                  /* Palette entries for device managed palette */

    VBOXDISPSURF surface;                    /* Device surface */
    FLONG flDrawingHooks;                    /* Enabled drawing hooks */

    VIDEO_MEMORY_INFORMATION memInfo;        /* Mapped Framebuffer/vram info */
    VBOXDISPVRAMLAYOUT layout;               /* VRAM layout information */

    VBOXDISPHGSMIINFO hgsmi;                 /* HGSMI Info */
    HGSMIQUERYCPORTPROCS vpAPI;              /* Video Port API callbacks and miniport's context */

    VBVABUFFERCONTEXT vbvaCtx;               /* VBVA context */
    VRDPBC            vrdpCache;             /* VRDP bitmap cache */

    ULONG cSSB;                              /* Number of active saved screen bits records in the following array. */
    SSB aSSB[4];                             /* LIFO type stack for saved screen areas. */

#ifdef VBOX_WITH_DDRAW
    VBOXDDLOCKINFO ddpsLock;                 /* Primary surface DirectDraw lock information */
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
    VBOXDISPVHWAINFO  vhwa;                  /* VHWA Info */
#endif

    BOOL bBitmapCacheDisabled;
} VBOXDISPDEV, *PVBOXDISPDEV;

/* -------------------- Driver callbacks -------------------- */
RT_C_DECLS_BEGIN
ULONG APIENTRY DriverEntry(IN PVOID Context1, IN PVOID Context2);
RT_C_DECLS_END

DHPDEV APIENTRY VBoxDispDrvEnablePDEV(DEVMODEW *pdm, LPWSTR pwszLogAddress,
                                      ULONG cPat, HSURF *phsurfPatterns,
                                      ULONG cjCaps, ULONG *pdevcaps,
                                      ULONG cjDevInfo, DEVINFO  *pdi,
                                      HDEV  hdev, PWSTR pwszDeviceName, HANDLE hDriver);
VOID APIENTRY VBoxDispDrvCompletePDEV(DHPDEV dhpdev, HDEV hdev);
VOID APIENTRY VBoxDispDrvDisablePDEV(DHPDEV dhpdev);
HSURF APIENTRY VBoxDispDrvEnableSurface(DHPDEV dhpdev);
VOID APIENTRY VBoxDispDrvDisableSurface(DHPDEV dhpdev);

BOOL APIENTRY VBoxDispDrvLineTo(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo,
                                LONG x1, LONG y1, LONG x2, LONG y2, RECTL *prclBounds, MIX mix);
BOOL APIENTRY VBoxDispDrvStrokePath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, XFORMOBJ *pxo,
                                    BRUSHOBJ  *pbo, POINTL *pptlBrushOrg, LINEATTRS *plineattrs, MIX mix);

BOOL APIENTRY VBoxDispDrvFillPath(SURFOBJ *pso, PATHOBJ *ppo, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg,
                                  MIX mix, FLONG flOptions);
BOOL APIENTRY VBoxDispDrvPaint(SURFOBJ *pso, CLIPOBJ *pco, BRUSHOBJ *pbo, POINTL *pptlBrushOrg, MIX mix);

BOOL APIENTRY VBoxDispDrvRealizeBrush(BRUSHOBJ *pbo, SURFOBJ *psoTarget, SURFOBJ *psoPattern, SURFOBJ *psoMask,
                                      XLATEOBJ *pxlo, ULONG iHatch);
ULONG APIENTRY VBoxDispDrvDitherColor(DHPDEV dhpdev, ULONG iMode, ULONG rgb, ULONG *pul);

BOOL APIENTRY VBoxDispDrvBitBlt(SURFOBJ *psoTrg, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                                RECTL *prclTrg, POINTL *pptlSrc, POINTL *pptlMask, BRUSHOBJ *pbo, POINTL *pptlBrush,
                                ROP4 rop4);
BOOL APIENTRY VBoxDispDrvStretchBlt(SURFOBJ *psoDest, SURFOBJ *psoSrc, SURFOBJ *psoMask, CLIPOBJ *pco, XLATEOBJ *pxlo,
                                    COLORADJUSTMENT *pca, POINTL *pptlHTOrg, RECTL *prclDest, RECTL *prclSrc,
                                    POINTL *pptlMask, ULONG iMode);
BOOL APIENTRY VBoxDispDrvCopyBits(SURFOBJ *psoDest, SURFOBJ *psoSrc, CLIPOBJ *pco, XLATEOBJ *pxlo,
                                  RECTL *prclDest, POINTL *pptlSrc);

ULONG APIENTRY VBoxDispDrvSetPointerShape(SURFOBJ *pso, SURFOBJ *psoMask, SURFOBJ *psoColor, XLATEOBJ *pxlo,
                                          LONG xHot, LONG yHot, LONG x, LONG y, RECTL *prcl, FLONG fl);
VOID APIENTRY VBoxDispDrvMovePointer(SURFOBJ *pso, LONG x, LONG y, RECTL *prcl);

BOOL APIENTRY VBoxDispDrvAssertMode(DHPDEV dhpdev, BOOL bEnable);
VOID APIENTRY VBoxDispDrvDisableDriver();
BOOL APIENTRY VBoxDispDrvTextOut(SURFOBJ *pso, STROBJ *pstro, FONTOBJ *pfo, CLIPOBJ *pco,
                                 RECTL *prclExtra, RECTL *prclOpaque, BRUSHOBJ *pboFore,
                                 BRUSHOBJ *pboOpaque, POINTL *pptlOrg, MIX mix);
BOOL APIENTRY VBoxDispDrvSetPalette(DHPDEV dhpdev, PALOBJ *ppalo, FLONG fl, ULONG iStart, ULONG cColors);
ULONG APIENTRY VBoxDispDrvEscape(SURFOBJ *pso, ULONG iEsc, ULONG cjIn, PVOID pvIn, ULONG cjOut, PVOID pvOut);
ULONG_PTR APIENTRY VBoxDispDrvSaveScreenBits(SURFOBJ *pso, ULONG iMode, ULONG_PTR ident, RECTL *prcl);
ULONG APIENTRY VBoxDispDrvGetModes(HANDLE hDriver, ULONG cjSize, DEVMODEW *pdm);
BOOL APIENTRY VBoxDispDrvOffset(SURFOBJ* pso, LONG x, LONG y, FLONG flReserved);

VOID APIENTRY VBoxDispDrvNotify(SURFOBJ *pso, ULONG iType, PVOID pvData);

#ifdef VBOX_WITH_DDRAW
BOOL APIENTRY VBoxDispDrvGetDirectDrawInfo(DHPDEV dhpdev, DD_HALINFO *pHalInfo, DWORD *pdwNumHeaps,
                                           VIDEOMEMORY *pvmList, DWORD *pdwNumFourCCCodes, DWORD *pdwFourCC);
BOOL APIENTRY VBoxDispDrvEnableDirectDraw(DHPDEV dhpdev, DD_CALLBACKS *pCallBacks,
                                          DD_SURFACECALLBACKS *pSurfaceCallBacks,
                                          DD_PALETTECALLBACKS *pPaletteCallBacks);
VOID APIENTRY VBoxDispDrvDisableDirectDraw(DHPDEV  dhpdev);
HBITMAP APIENTRY VBoxDispDrvDeriveSurface(DD_DIRECTDRAW_GLOBAL *pDirectDraw, DD_SURFACE_LOCAL *pSurface);
#endif /*#ifdef VBOX_WITH_DDRAW*/

/* -------------------- Internal helpers -------------------- */
DECLINLINE(SURFOBJ) *getSurfObj(SURFOBJ *pso)
{
    if (pso)
    {
        PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;

        if (pDev && pDev->surface.psoBitmap && pso->hsurf == pDev->surface.hSurface)
        {
            /* Convert the device PSO to the bitmap PSO which can be passed to Eng*. */
            pso = pDev->surface.psoBitmap;
        }
    }

    return pso;
}

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDisp_h */
