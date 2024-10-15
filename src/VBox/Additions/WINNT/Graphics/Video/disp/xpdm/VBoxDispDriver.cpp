/* $Id: VBoxDispDriver.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver interface functions
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

#include "VBoxDisp.h"
#include "VBoxDispMini.h"
#include "VBoxDispDDraw.h"
#include <iprt/initterm.h>

/* Engine version we're running on, set in DrvEnableDriver */
static ULONG g_EngineVersionDDI = DDI_DRIVER_VERSION_NT4;

/* Callback function supported by our driver, stored in index/address pairs (see winddi.h) */
/* NT4 version */
static DRVFN g_aDrvFnTableNT4[] =
{
    /* Required functions */
    {INDEX_DrvGetModes,          (PFN) VBoxDispDrvGetModes         },
    {INDEX_DrvEnablePDEV,        (PFN) VBoxDispDrvEnablePDEV       },
    {INDEX_DrvCompletePDEV,      (PFN) VBoxDispDrvCompletePDEV     },
    {INDEX_DrvDisablePDEV,       (PFN) VBoxDispDrvDisablePDEV      },
    {INDEX_DrvEnableSurface,     (PFN) VBoxDispDrvEnableSurface    },
    {INDEX_DrvDisableSurface,    (PFN) VBoxDispDrvDisableSurface   },
    {INDEX_DrvAssertMode,        (PFN) VBoxDispDrvAssertMode       },
    /* Drawing commands */
    {INDEX_DrvLineTo,            (PFN) VBoxDispDrvLineTo           },
    {INDEX_DrvStrokePath,        (PFN) VBoxDispDrvStrokePath       },
    {INDEX_DrvFillPath,          (PFN) VBoxDispDrvFillPath         },
    {INDEX_DrvPaint,             (PFN) VBoxDispDrvPaint            },
    {INDEX_DrvTextOut,           (PFN) VBoxDispDrvTextOut          },
    {INDEX_DrvSaveScreenBits,    (PFN) VBoxDispDrvSaveScreenBits   },
    /* BitBlt's*/
    {INDEX_DrvBitBlt,            (PFN) VBoxDispDrvBitBlt           },
    {INDEX_DrvStretchBlt,        (PFN) VBoxDispDrvStretchBlt,      },
    {INDEX_DrvCopyBits,          (PFN) VBoxDispDrvCopyBits         },
    /* Brush related */
    {INDEX_DrvRealizeBrush,      (PFN) VBoxDispDrvRealizeBrush     },
    {INDEX_DrvDitherColor,       (PFN) VBoxDispDrvDitherColor      },
    /* Pointer related */
    {INDEX_DrvSetPointerShape,   (PFN) VBoxDispDrvSetPointerShape  },
    {INDEX_DrvMovePointer,       (PFN) VBoxDispDrvMovePointer      },
    /* Misc */
    {INDEX_DrvDisableDriver,     (PFN) VBoxDispDrvDisableDriver    },
    {INDEX_DrvSetPalette,        (PFN) VBoxDispDrvSetPalette       },
    {INDEX_DrvEscape,            (PFN) VBoxDispDrvEscape           },
#ifdef VBOX_WITH_DDRAW
    {INDEX_DrvGetDirectDrawInfo, (PFN) VBoxDispDrvGetDirectDrawInfo},
    {INDEX_DrvEnableDirectDraw,  (PFN) VBoxDispDrvEnableDirectDraw },
    {INDEX_DrvDisableDirectDraw, (PFN) VBoxDispDrvDisableDirectDraw},
#endif
    /* g_aDrvFnTableNT4, NT4 specific */
    {INDEX_DrvOffset,            (PFN) VBoxDispDrvOffset           } /*Obsolete*/
};

/* WIN2K+ version */
static DRVFN g_aDrvFnTableNT5[] =
{
    /* Required functions */
    {INDEX_DrvGetModes,          (PFN) VBoxDispDrvGetModes         },
    {INDEX_DrvEnablePDEV,        (PFN) VBoxDispDrvEnablePDEV       },
    {INDEX_DrvCompletePDEV,      (PFN) VBoxDispDrvCompletePDEV     },
    {INDEX_DrvDisablePDEV,       (PFN) VBoxDispDrvDisablePDEV      },
    {INDEX_DrvEnableSurface,     (PFN) VBoxDispDrvEnableSurface    },
    {INDEX_DrvDisableSurface,    (PFN) VBoxDispDrvDisableSurface   },
    {INDEX_DrvAssertMode,        (PFN) VBoxDispDrvAssertMode       },
    /* Drawing commands */
    {INDEX_DrvLineTo,            (PFN) VBoxDispDrvLineTo           },
    {INDEX_DrvStrokePath,        (PFN) VBoxDispDrvStrokePath       },
    {INDEX_DrvFillPath,          (PFN) VBoxDispDrvFillPath         },
    {INDEX_DrvPaint,             (PFN) VBoxDispDrvPaint            },
    {INDEX_DrvTextOut,           (PFN) VBoxDispDrvTextOut          },
    {INDEX_DrvSaveScreenBits,    (PFN) VBoxDispDrvSaveScreenBits   },
    /* BitBlt's*/
    {INDEX_DrvBitBlt,            (PFN) VBoxDispDrvBitBlt           },
    {INDEX_DrvStretchBlt,        (PFN) VBoxDispDrvStretchBlt,      },
    {INDEX_DrvCopyBits,          (PFN) VBoxDispDrvCopyBits         },
    /* Brush related */
    {INDEX_DrvRealizeBrush,      (PFN) VBoxDispDrvRealizeBrush     },
    {INDEX_DrvDitherColor,       (PFN) VBoxDispDrvDitherColor      },
    /* Pointer related */
    {INDEX_DrvSetPointerShape,   (PFN) VBoxDispDrvSetPointerShape  },
    {INDEX_DrvMovePointer,       (PFN) VBoxDispDrvMovePointer      },
    /* Misc */
    {INDEX_DrvDisableDriver,     (PFN) VBoxDispDrvDisableDriver    },
    {INDEX_DrvSetPalette,        (PFN) VBoxDispDrvSetPalette       },
    {INDEX_DrvEscape,            (PFN) VBoxDispDrvEscape           },
#ifdef VBOX_WITH_DDRAW
    {INDEX_DrvGetDirectDrawInfo, (PFN) VBoxDispDrvGetDirectDrawInfo},
    {INDEX_DrvEnableDirectDraw,  (PFN) VBoxDispDrvEnableDirectDraw },
    {INDEX_DrvDisableDirectDraw, (PFN) VBoxDispDrvDisableDirectDraw},
#endif
    /* g_aDrvFnTableNT5, NT5 specific */
    {INDEX_DrvNotify,            (PFN) VBoxDispDrvNotify           },
#ifdef VBOX_WITH_DDRAW
    {INDEX_DrvDeriveSurface,     (PFN) VBoxDispDrvDeriveSurface    }
#endif
};

RT_C_DECLS_BEGIN
ULONG __cdecl DbgPrint(PCH pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    RTLogBackdoorPrintfV(pszFormat, args);
    va_end(args);

    return 0;
}
RT_C_DECLS_END

VOID _wcsncpy(WCHAR *pwcd, WCHAR *pwcs, ULONG dstsize)
{
    ULONG cnt=0;

    while (*pwcs!=*L"")
    {
        if (cnt>=dstsize)
        {
            break;
        }

        *pwcd = *pwcs;

        pwcs++;
        pwcd++;
        cnt ++;
    }

    if (cnt<dstsize)
    {
        memset(pwcd, 0, (dstsize-cnt) * sizeof(WCHAR));
    }
}

#define VBOXDISPSETCIEC(_ciec, _x, _y, _lum) \
    do {                                     \
        _ciec.x = _x;                        \
        _ciec.y = _y;                        \
        _ciec.Y = _lum;                      \
    } while (0)


#define VBOXDISPMAKELOGFONTW(_font, _w, _h, _weight, _clip, _quality, _pitch, _name) \
    do {                                                                             \
        _font.lfHeight = _w;                                                         \
        _font.lfWidth = _h;                                                          \
        _font.lfEscapement = 0;                                                      \
        _font.lfOrientation = 0;                                                     \
        _font.lfWeight = _weight;                                                    \
        _font.lfItalic = 0;                                                          \
        _font.lfUnderline = 0;                                                       \
        _font.lfStrikeOut = 0;                                                       \
        _font.lfCharSet = ANSI_CHARSET;                                              \
        _font.lfOutPrecision = OUT_DEFAULT_PRECIS;                                   \
        _font.lfClipPrecision = _clip;                                               \
        _font.lfQuality = _quality;                                                  \
        _font.lfPitchAndFamily = (_pitch) | FF_DONTCARE;                             \
        memset(_font.lfFaceName, 0, sizeof(_font.lfFaceName));                       \
        memcpy(_font.lfFaceName, _name, sizeof(_name));                              \
    } while (0)

static int VBoxDispInitDevice(PVBOXDISPDEV pDev, DEVMODEW *pdm, GDIINFO *pGdiInfo, DEVINFO *pDevInfo)
{
    VIDEO_MODE_INFORMATION *pModesTable = NULL, selectedMode;
    ULONG cModes, i=0;
    int rc;

    LOGF_ENTER();

    memset(&selectedMode, 0, sizeof(VIDEO_MODE_INFORMATION));

    /* Get a list of supported modes by both miniport and display driver */
    rc = VBoxDispMPGetVideoModes(pDev->hDriver, &pModesTable, &cModes);
    VBOX_WARNRC_RETRC(rc);

    /* Check if requested mode is available in the list */
    if ((g_EngineVersionDDI < DDI_DRIVER_VERSION_NT5)
        && (pdm->dmBitsPerPel==0)
        && (pdm->dmPelsWidth==0)
        && (pdm->dmPelsHeight==0)
        && (pdm->dmDisplayFrequency==0))
    {
        /* Special case for NT4, just return default(first) mode */
        memcpy(&selectedMode, &pModesTable[0], sizeof(VIDEO_MODE_INFORMATION));
    }
    else
    {
        for (; i<cModes; ++i)
        {
            if ((pdm->dmBitsPerPel == (pModesTable[i].BitsPerPlane * pModesTable[i].NumberOfPlanes))
                && (pdm->dmPelsWidth == pModesTable[i].VisScreenWidth)
                && (pdm->dmPelsHeight == pModesTable[i].VisScreenHeight)
                && (pdm->dmDisplayFrequency == pModesTable[i].Frequency))
            {
                memcpy(&selectedMode, &pModesTable[i], sizeof(VIDEO_MODE_INFORMATION));
                break;
            }
        }
    }
    EngFreeMem(pModesTable);

    if (i>=cModes)
    {
        WARN(("can't support requested mode %dx%d@%dbpp(%dHz)!",
              pdm->dmPelsWidth, pdm->dmPelsHeight, pdm->dmBitsPerPel, pdm->dmDisplayFrequency));
        return VERR_NOT_SUPPORTED;
    }

    LOG(("match for requested mode %dx%d@%dbpp(%dHz)",
         selectedMode.VisScreenWidth, selectedMode.VisScreenHeight, selectedMode.BitsPerPlane, selectedMode.Frequency));

    /* Update private device info with mode information */
    pDev->mode.ulIndex = selectedMode.ModeIndex;
    pDev->mode.ulWidth = selectedMode.VisScreenWidth;
    pDev->mode.ulHeight = selectedMode.VisScreenHeight;
    pDev->mode.ulBitsPerPel = selectedMode.BitsPerPlane * selectedMode.NumberOfPlanes;
    pDev->mode.lScanlineStride = RT_ALIGN_32(selectedMode.ScreenStride, 4);
    pDev->mode.flMaskR = selectedMode.RedMask;
    pDev->mode.flMaskG = selectedMode.GreenMask;
    pDev->mode.flMaskB = selectedMode.BlueMask;
    pDev->mode.ulPaletteShift = (pDev->mode.ulBitsPerPel==8) ? (8-selectedMode.NumberRedBits) : 0;

    /* Fill GDIINFO structure */
    memset(pGdiInfo, 0, sizeof(GDIINFO));

    pGdiInfo->ulVersion = (g_EngineVersionDDI<DDI_DRIVER_VERSION_NT5) ? GDI_DRIVER_VERSION:0x5000;
    pGdiInfo->ulVersion |= VBOXDISPDRIVERVERSION;

    pGdiInfo->ulTechnology = DT_RASDISPLAY;

    pGdiInfo->ulHorzSize = selectedMode.XMillimeter;
    pGdiInfo->ulVertSize = selectedMode.YMillimeter;

    pGdiInfo->ulHorzRes = pDev->mode.ulWidth;
    pGdiInfo->ulVertRes = pDev->mode.ulHeight;

    pGdiInfo->cBitsPixel = pDev->mode.ulBitsPerPel;
    pGdiInfo->cPlanes = selectedMode.NumberOfPlanes;

    pGdiInfo->ulNumColors = (pDev->mode.ulBitsPerPel==8) ? 20 : ((ULONG)(-1));

    pGdiInfo->ulLogPixelsX = pdm->dmLogPixels;
    pGdiInfo->ulLogPixelsY = pdm->dmLogPixels;
    if (pdm->dmLogPixels!=96)
    {
        WARN(("requested logical pixel res %d isn't 96", pdm->dmLogPixels));
    }

    pGdiInfo->flTextCaps = TC_RA_ABLE;

    pGdiInfo->ulDACRed = selectedMode.NumberRedBits;
    pGdiInfo->ulDACGreen = selectedMode.NumberGreenBits;
    pGdiInfo->ulDACBlue = selectedMode.NumberBlueBits;

    pGdiInfo->ulAspectX = 0x24;
    pGdiInfo->ulAspectY = 0x24;
    /* note: ulAspectXY should be square root of sum of squares of x and y aspects */
    pGdiInfo->ulAspectXY = 0x33;

    /* search for "styled cosmetic lines" on msdn for more info */
    pGdiInfo->xStyleStep = 1;
    pGdiInfo->yStyleStep = 1;
    pGdiInfo->denStyleStep = 3;

    pGdiInfo->ulNumPalReg = (pDev->mode.ulBitsPerPel==8) ? (1<<pDev->mode.ulBitsPerPel) : 0;

    /** @todo might want to implement IOCTL_VIDEO_QUERY_COLOR_CAPABILITIES in miniport driver
     *        and query host for this info there
     */
    VBOXDISPSETCIEC(pGdiInfo->ciDevice.Red, 6700, 3300, 0);
    VBOXDISPSETCIEC(pGdiInfo->ciDevice.Green, 2100, 7100, 0);
    VBOXDISPSETCIEC(pGdiInfo->ciDevice.Blue, 1400, 800, 0);
    VBOXDISPSETCIEC(pGdiInfo->ciDevice.AlignmentWhite, 3127, 3290, 0);
    VBOXDISPSETCIEC(pGdiInfo->ciDevice.Cyan, 0, 0, 0);
    VBOXDISPSETCIEC(pGdiInfo->ciDevice.Magenta, 0, 0, 0);
    VBOXDISPSETCIEC(pGdiInfo->ciDevice.Yellow, 0, 0, 0);
    pGdiInfo->ciDevice.RedGamma = 20000;
    pGdiInfo->ciDevice.GreenGamma = 20000;
    pGdiInfo->ciDevice.BlueGamma = 20000;

    pGdiInfo->ulPrimaryOrder = PRIMARY_ORDER_CBA;

    pGdiInfo->ulHTPatternSize = HT_PATSIZE_4x4_M;
    switch (pDev->mode.ulBitsPerPel)
    {
        case 8:
        {
            pGdiInfo->ulHTOutputFormat = HT_FORMAT_8BPP;
            break;
        }
        case 16:
        {
            pGdiInfo->ulHTOutputFormat = HT_FORMAT_16BPP;
            break;
        }
        case 24:
        {
            pGdiInfo->ulHTOutputFormat = HT_FORMAT_24BPP;
            break;
        }
        case 32:
        {
            pGdiInfo->ulHTOutputFormat = HT_FORMAT_32BPP;
            break;
        }
    }
    pGdiInfo->flHTFlags = HT_FLAG_ADDITIVE_PRIMS;

    pGdiInfo->ulVRefresh = selectedMode.Frequency;

    /* 0 means BitBlt's are accelerated by driver */
    pGdiInfo->ulBltAlignment = 0;

    pGdiInfo->ulPhysicalPixelCharacteristics = PPC_UNDEFINED;
    pGdiInfo->ulPhysicalPixelGamma = PPG_DEFAULT;

    /* Fill DEVINFO structure */
    memset(pDevInfo, 0, sizeof(DEVINFO));

    pDevInfo->flGraphicsCaps = GCAPS_OPAQUERECT;
#ifdef VBOX_WITH_DDRAW
    pDevInfo->flGraphicsCaps |= GCAPS_DIRECTDRAW;
#endif
    VBOXDISPMAKELOGFONTW(pDevInfo->lfDefaultFont,
                         16, 7, FW_BOLD, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH, L"System");

    VBOXDISPMAKELOGFONTW(pDevInfo->lfAnsiVarFont,
                         12, 9, FW_NORMAL, CLIP_STROKE_PRECIS, PROOF_QUALITY, VARIABLE_PITCH, L"MS Sans Serif");

    VBOXDISPMAKELOGFONTW(pDevInfo->lfAnsiFixFont,
                         12, 9, FW_NORMAL, CLIP_STROKE_PRECIS, PROOF_QUALITY, FIXED_PITCH, L"Courier");
    pDevInfo->cFonts = 0;
    pDevInfo->cxDither = 8;
    pDevInfo->cyDither = 8;
    pDevInfo->hpalDefault = 0;
    pDevInfo->flGraphicsCaps2 = 0;

    switch (pDev->mode.ulBitsPerPel)
    {
        case 8:
        {
            pDevInfo->flGraphicsCaps |= GCAPS_PALMANAGED|GCAPS_COLOR_DITHER;
            pDevInfo->iDitherFormat = BMF_8BPP;
            break;
        }
        case 16:
        {
            pDevInfo->iDitherFormat = BMF_16BPP;
            break;
        }
        case 24:
        {
            pDevInfo->iDitherFormat = BMF_24BPP;
            break;
        }
        case 32:
        {
            pDevInfo->iDitherFormat = BMF_32BPP;
            break;
        }
    }

    LOGF_LEAVE();
    return rc;
}

/* Display Driver entry point,
 * Returns DDI version number and callbacks supported by driver.
 */
BOOL DrvEnableDriver(ULONG iEngineVersion, ULONG cj, PDRVENABLEDATA pded)
{
    /** @todo can't link with hal.lib
    int irc = RTR0Init(0);
    if (RT_FAILURE(irc))
    {
        LOGREL(("failed to init IPRT (rc=%#x)", irc));
        return FALSE;
    }
    */

    LOGF(("iEngineVersion=%#08X, cj=%d", iEngineVersion, cj));

    g_EngineVersionDDI = iEngineVersion;

    /* Driver can't work if we can't fill atleast first 3 fields in passed PDRVENABLEDATA */
    if (cj < (2*sizeof(ULONG)+sizeof(DRVFN*)))
    {
        WARN(("cj<%d, terminating\n", sizeof(DRVENABLEDATA)));
        return FALSE;
    }

    /* Report driver DDI version and appropriate callbacks table based on engine DDI */
    if (iEngineVersion>=DDI_DRIVER_VERSION_NT5)
    {
        /* WIN2K and above */
        pded->iDriverVersion = DDI_DRIVER_VERSION_NT5;
        pded->pdrvfn = g_aDrvFnTableNT5;
        pded->c = RT_ELEMENTS(g_aDrvFnTableNT5);
    }
    else
    {
        /* NT4_SP3 and below*/
        pded->iDriverVersion = DDI_DRIVER_VERSION_NT4;
        pded->pdrvfn = g_aDrvFnTableNT4;
        pded->c = RT_ELEMENTS(g_aDrvFnTableNT4);
    }

    LOGF_LEAVE();
    return TRUE;
}

/* Free all resources allocated in DrvEnableDriver */
VOID APIENTRY VBoxDispDrvDisableDriver()
{
    LOGF_ENTER();

    /* Intentionally left blank */

    LOGF_LEAVE();
    return;
}

/* Returns video modes supported by our device/driver
 * Note: If we fail here we'd be asked to enter 800x600@4bpp mode later in VBoxDispDrvEnablePDEV.
 */
ULONG APIENTRY VBoxDispDrvGetModes(HANDLE hDriver, ULONG cjSize, DEVMODEW *pdm)
{
    int rc;
    VIDEO_MODE_INFORMATION *pModesTable;
    ULONG cModes;
    LOGF_ENTER();

    rc = VBoxDispMPGetVideoModes(hDriver, &pModesTable, &cModes);
    VBOX_WARNRC_RETV(rc, 0);

    if (!pdm) /* return size of buffer required to store all supported modes */
    {
        EngFreeMem(pModesTable);
        LOGF_LEAVE();
        return cModes * sizeof(DEVMODEW);
    }

    ULONG mode, cMaxNodes=cjSize/sizeof(DEVMODEW);

    for (mode=0; mode<cModes && mode<cMaxNodes; ++mode, ++pdm)
    {
        memset(pdm, 0, sizeof(DEVMODEW));
        memcpy(pdm->dmDeviceName, VBOXDISP_DEVICE_NAME, sizeof(VBOXDISP_DEVICE_NAME));

        pdm->dmSpecVersion   = DM_SPECVERSION;
        pdm->dmDriverVersion = DM_SPECVERSION;
        pdm->dmSize          = sizeof(DEVMODEW);
        pdm->dmDriverExtra   = 0;

        pdm->dmBitsPerPel       = pModesTable[mode].NumberOfPlanes*pModesTable[mode].BitsPerPlane;
        pdm->dmPelsWidth        = pModesTable[mode].VisScreenWidth;
        pdm->dmPelsHeight       = pModesTable[mode].VisScreenHeight;
        pdm->dmDisplayFrequency = pModesTable[mode].Frequency;
        pdm->dmDisplayFlags     = 0;
        pdm->dmFields           = DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT|DM_DISPLAYFREQUENCY|DM_DISPLAYFLAGS;
    }
    EngFreeMem(pModesTable);

    LOG(("%d mode(s) reported", mode));

    LOGF_LEAVE();
    return mode * sizeof(DEVMODEW);
}

/* First function which is called after entry point, provides info about device to GDI.
 * Returns pointer to our driver private info structure which would be passed by GDI to our other callbacks.
 */
DHPDEV APIENTRY
VBoxDispDrvEnablePDEV(DEVMODEW *pdm, LPWSTR pwszLogAddress, ULONG cPat, HSURF *phsurfPatterns,
                      ULONG cjCaps, ULONG *pdevcaps,
                      ULONG cjDevInfo, DEVINFO  *pdi,
                      HDEV  hdev, PWSTR pwszDeviceName, HANDLE hDriver)
{
    RT_NOREF(hdev);
    PVBOXDISPDEV pDev = NULL;
    GDIINFO gdiInfo;
    DEVINFO devInfo;
    int rc;

    /* Next 3 are only used for printer drivers */
    NOREF(pwszLogAddress);
    NOREF(cPat);
    NOREF(phsurfPatterns);
    NOREF(pwszDeviceName);

    LOGF_ENTER();

    pDev = (PVBOXDISPDEV) EngAllocMem(FL_ZERO_MEMORY, sizeof(VBOXDISPDEV), MEM_ALLOC_TAG);
    if (!pDev)
    {
        WARN(("EngAllocMem failed!\n"));
        return NULL;
    }
    pDev->hDriver = hDriver;

    ULONG ulRegistryFlags = 0;
    rc = VBoxDispMPQueryRegistryFlags(hDriver, &ulRegistryFlags);
    if (RT_SUCCESS(rc))
    {
        pDev->bBitmapCacheDisabled = (ulRegistryFlags & VBOXVIDEO_REGISTRY_FLAGS_DISABLE_BITMAP_CACHE) != 0;
        LOG(("Bitmap cache %s", pDev->bBitmapCacheDisabled? "disabled": "enabled"));
    }

    /* Initialize device structure and query miniport to fill device and gdi infos */
    rc = VBoxDispInitDevice(pDev, pdm, &gdiInfo, &devInfo);
    if (RT_FAILURE(rc))
    {
        VBOX_WARNRC(rc);
        EngFreeMem(pDev);
        return NULL;
    }

    /* Initialize mouse pointer caps */
    rc = VBoxDispInitPointerCaps(pDev, &devInfo);
    if (RT_FAILURE(rc))
    {
        VBOX_WARNRC(rc);
    }

    /* Initialize palette */
    rc = VBoxDispInitPalette(pDev, &devInfo);
    if (RT_FAILURE(rc))
    {
        VBOX_WARNRC(rc);
        EngFreeMem(pDev);
        return NULL;
    }

    if(g_EngineVersionDDI >= DDI_DRIVER_VERSION_NT5)
    {
        devInfo.flGraphicsCaps2 |= GCAPS2_RESERVED1;
    }

    /* Copy gathered info to supplied buffers */
    memcpy(pdevcaps, &gdiInfo, min(sizeof(GDIINFO), cjCaps));
    memcpy(pdi, &devInfo, min(sizeof(DEVINFO), cjDevInfo));

    LOGF_LEAVE();
    return (DHPDEV)pDev;
}

/* Called to provide us GDI handle for our device, which we should use later for GDI calls */
VOID APIENTRY VBoxDispDrvCompletePDEV(DHPDEV dhpdev, HDEV hdev)
{
    LOGF_ENTER();

    ((PVBOXDISPDEV)dhpdev)->hDevGDI = hdev;

    LOGF_LEAVE();
}

/* Called to free resources allocated for device in VBoxDispDrvEnablePDEV */
VOID APIENTRY VBoxDispDrvDisablePDEV(DHPDEV dhpdev)
{
    LOGF_ENTER();

    VBoxDispDestroyPalette((PVBOXDISPDEV) dhpdev);

    EngFreeMem(dhpdev);

    LOGF_LEAVE();
}

/* Called to create and associate surface with device */
HSURF APIENTRY VBoxDispDrvEnableSurface(DHPDEV dhpdev)
{
    int rc;
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)dhpdev;

    LOGF_ENTER();

    /* Switch device to mode requested in VBoxDispDrvEnablePDEV */
    rc = VBoxDispMPSetCurrentMode(pDev->hDriver, pDev->mode.ulIndex);
    VBOX_WARNRC_RETV(rc, NULL);

    /* Map fb and vram */
    rc = VBoxDispMPMapMemory(pDev, &pDev->memInfo);
    VBOX_WARNRC_RETV(rc, NULL);

    /* Clear mapped memory, to avoid garbage while video mode is switching */
    /** @todo VIDEO_MODE_NO_ZERO_MEMORY does nothing in miniport's IOCTL_VIDEO_SET_CURRENT_MODE*/
    memset(pDev->memInfo.FrameBufferBase, 0, pDev->mode.ulHeight * abs(pDev->mode.lScanlineStride));

    /* Allocate memory for pointer attrs */
    rc = VBoxDispInitPointerAttrs(pDev);
    VBOX_WARNRC_RETV(rc, NULL);

    /* Init VBVA */
    rc = VBoxDispVBVAInit(pDev);
    VBOX_WARNRC_RETV(rc, NULL);

    /* Enable VBVA */
    if (pDev->hgsmi.bSupported)
    {
        if (pDev->mode.ulBitsPerPel==16 || pDev->mode.ulBitsPerPel==24 || pDev->mode.ulBitsPerPel==32)
        {
            VBVABUFFER *pVBVA = (VBVABUFFER *)((uint8_t *)pDev->memInfo.VideoRamBase+pDev->layout.offVBVABuffer);
            pDev->hgsmi.bSupported = VBoxVBVAEnable(&pDev->vbvaCtx, &pDev->hgsmi.ctx, pVBVA, -1);
            LogRel(("VBoxDisp[%d]: VBVA %senabled\n", pDev->iDevice, pDev->hgsmi.bSupported? "":"not "));
        }
    }

    /* Inform host */
    if (pDev->hgsmi.bSupported)
    {
        VBoxHGSMIProcessDisplayInfo(&pDev->hgsmi.ctx, pDev->iDevice, pDev->orgDev.x, pDev->orgDev.y,
                                    0, abs(pDev->mode.lScanlineStride), pDev->mode.ulWidth, pDev->mode.ulHeight,
                                    (uint16_t)pDev->mode.ulBitsPerPel, VBVA_SCREEN_F_ACTIVE);
    }

#ifdef VBOX_WITH_VIDEOHWACCEL
    VBoxDispVHWAEnable(pDev);
#endif

    /* Set device palette if needed */
    if (pDev->mode.ulBitsPerPel == 8)
    {
        rc = VBoxDispSetPalette8BPP(pDev);
        VBOX_WARNRC_RETV(rc, NULL);
    }

    pDev->orgDisp.x = 0;
    pDev->orgDisp.y = 0;

    /* Create GDI managed bitmap, which resides in our framebuffer memory */
    ULONG iFormat;
    SIZEL size;

    switch (pDev->mode.ulBitsPerPel)
    {
        case 8:
        {
            iFormat = BMF_8BPP;
            break;
        }
        case 16:
        {
            iFormat = BMF_16BPP;
            break;
        }
        case 24:
        {
            iFormat = BMF_24BPP;
            break;
        }
        case 32:
        {
            iFormat = BMF_32BPP;
            break;
        }
        default:
            AssertMsgFailedReturn(("ulBitsPerPel=%#x\n", pDev->mode.ulBitsPerPel), NULL);
    }

    size.cx = pDev->mode.ulWidth;
    size.cy = pDev->mode.ulHeight;

    pDev->surface.hBitmap = EngCreateBitmap(size, pDev->mode.lScanlineStride, iFormat,
                                            pDev->mode.lScanlineStride > 0 ? BMF_TOPDOWN:0,
                                            pDev->memInfo.FrameBufferBase);
    if (!pDev->surface.hBitmap)
    {
        WARN(("EngCreateBitmap failed!"));
        return NULL;
    }
    pDev->surface.psoBitmap = EngLockSurface((HSURF)pDev->surface.hBitmap);

    /* Create device-managed surface */
    pDev->surface.hSurface = EngCreateDeviceSurface((DHSURF)pDev, size, iFormat);
    if (!pDev->surface.hSurface)
    {
        WARN(("EngCreateDeviceSurface failed!"));
        VBoxDispDrvDisableSurface(dhpdev);
        return NULL;
    }

    FLONG flHooks = HOOK_BITBLT|HOOK_TEXTOUT|HOOK_FILLPATH|HOOK_COPYBITS|HOOK_STROKEPATH|HOOK_LINETO|
                    HOOK_PAINT|HOOK_STRETCHBLT;

    /* Associate created surface with our device */
    if (!EngAssociateSurface(pDev->surface.hSurface, pDev->hDevGDI, flHooks))
    {
        WARN(("EngAssociateSurface failed!"));
        VBoxDispDrvDisableSurface(dhpdev);
        return NULL;
    }

    pDev->surface.ulFormat = iFormat;
    pDev->flDrawingHooks = flHooks;

    LOG(("Created surface %p for physical device %p", pDev->surface.hSurface, pDev));

    LOGF_LEAVE();
    return pDev->surface.hSurface;
}

VOID APIENTRY VBoxDispDrvDisableSurface(DHPDEV dhpdev)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)dhpdev;
    LOGF_ENTER();

    if (pDev->surface.hSurface)
    {
        EngDeleteSurface(pDev->surface.hSurface);
        pDev->surface.hSurface = NULL;
    }

    if (pDev->surface.psoBitmap)
    {
        Assert(pDev->surface.hBitmap);
        EngUnlockSurface(pDev->surface.psoBitmap);
        pDev->surface.psoBitmap = NULL;
    }

    if (pDev->surface.hBitmap)
    {
        EngDeleteSurface((HSURF) pDev->surface.hBitmap);
        pDev->surface.hBitmap = NULL;
    }

    int rc;
    rc = VBoxDispMPUnmapMemory(pDev);
    VBOX_WARNRC(rc);

    LOGF_LEAVE();
}

BOOL APIENTRY
VBoxDispDrvRealizeBrush(BRUSHOBJ *pbo, SURFOBJ *psoTarget, SURFOBJ *psoPattern, SURFOBJ *psoMask,
                        XLATEOBJ *pxlo, ULONG iHatch)
{
    BOOL bRc = FALSE;
    LOGF_ENTER();

    if (VBoxDispIsScreenSurface(psoTarget))
    {
        PVBOXDISPDEV pDev = (PVBOXDISPDEV)psoTarget->dhpdev;

        if (pDev->vbvaCtx.pVBVA && (pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_ENABLED))
        {
            if (pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents & VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET)
            {
                vrdpReset(pDev);
                pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents &= ~VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET;
            }

            if (pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_VRDP)
            {
                bRc = vrdpDrvRealizeBrush(pbo, psoTarget, psoPattern, psoMask, pxlo, iHatch);
            }
        }
    }

    LOGF_LEAVE();
    return bRc;
}

ULONG APIENTRY VBoxDispDrvDitherColor(DHPDEV dhpdev, ULONG iMode, ULONG rgb, ULONG *pul)
{
    RT_NOREF(dhpdev, iMode, rgb, pul);
    ULONG rc;
    LOGF_ENTER();

    /* There is no EngDitherColor on NT4, so take the easy path and tell the graphics
     * engine to create a halftone approximation.
     */
    rc = DCR_HALFTONE;

    LOGF_LEAVE();
    return rc;
}

/* Called to reset device to default mode or to mode specified with dhpdev */
BOOL APIENTRY VBoxDispDrvAssertMode(DHPDEV dhpdev, BOOL bEnable)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV) dhpdev;
    int rc;
    LOGF_ENTER();

    if (!bEnable)
    {
        LOGF(("!bEnable"));
#ifdef VBOX_WITH_VIDEOHWACCEL
        /* tells we can not process host commands any more and ensures that
         * we've completed processing of the host VHWA commands
         */
        VBoxDispVHWADisable(pDev);
#endif

        /* disable VBVA */
        if (pDev->hgsmi.bSupported)
        {
            VBoxVBVADisable(&pDev->vbvaCtx, &pDev->hgsmi.ctx, -1);
        }

        /* reset the device to default mode */
        rc = VBoxDispMPResetDevice(pDev->hDriver);
        VBOX_WARNRC_RETV(rc, FALSE);
    }
    else
    {
        LOGF(("bEnable"));

        /* switch device to previous pDev mode */
        rc = VBoxDispMPSetCurrentMode(pDev->hDriver, pDev->mode.ulIndex);
        VBOX_WARNRC_RETV(rc, NULL);

        /* enable VBVA */
        if (pDev->hgsmi.bSupported)
        {
            if (pDev->mode.ulBitsPerPel==16 || pDev->mode.ulBitsPerPel==24 || pDev->mode.ulBitsPerPel==32)
            {
                VBVABUFFER *pVBVA = (VBVABUFFER *)((uint8_t *)pDev->memInfo.VideoRamBase+pDev->layout.offVBVABuffer);
                pDev->hgsmi.bSupported = VBoxVBVAEnable(&pDev->vbvaCtx, &pDev->hgsmi.ctx, pVBVA, -1);
                LogRel(("VBoxDisp[%d]: VBVA %senabled\n", pDev->iDevice, pDev->hgsmi.bSupported? "":"not "));
            }
        }

        /* inform host */
        if (pDev->hgsmi.bSupported)
        {
            VBoxHGSMIProcessDisplayInfo(&pDev->hgsmi.ctx, pDev->iDevice, pDev->orgDev.x, pDev->orgDev.y,
                                        0, abs(pDev->mode.lScanlineStride), pDev->mode.ulWidth, pDev->mode.ulHeight,
                                        (uint16_t)pDev->mode.ulBitsPerPel, VBVA_SCREEN_F_ACTIVE);
        }

#ifdef VBOX_WITH_VIDEOHWACCEL
        /* tells we can process host commands */
       VBoxDispVHWAEnable(pDev);
#endif

        /* Associate back GDI bitmap residing in our framebuffer memory with GDI's handle to our device */
        if (!EngAssociateSurface((HSURF)pDev->surface.hBitmap, pDev->hDevGDI, 0))
        {
            WARN(("EngAssociateSurface on bitmap failed"));
            return FALSE;
        }

        /* Associate device managed surface with GDI's handle to our device */
        if (!EngAssociateSurface(pDev->surface.hSurface, pDev->hDevGDI, pDev->flDrawingHooks))
        {
            WARN(("EngAssociateSurface on surface failed"));
            return FALSE;
        }
    }

    LOGF_LEAVE();
    return TRUE;
}

ULONG APIENTRY VBoxDispDrvEscape(SURFOBJ *pso, ULONG iEsc, ULONG cjIn, PVOID pvIn, ULONG cjOut, PVOID pvOut)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    LOGF_ENTER();

    switch (iEsc)
    {
        case VBOXESC_ISVRDPACTIVE:
        {
            if (pDev && pDev->vbvaCtx.pVBVA && pDev->vbvaCtx.pVBVA->hostFlags.u32HostEvents&VBVA_F_MODE_VRDP)
            {
                LOGF(("VBOXESC_ISVRDPACTIVE: 1"));
                return 1;
            }
            LOGF(("VBOXESC_ISVRDPACTIVE: 0"));
            return 0;
        }
        case VBOXESC_SETVISIBLEREGION:
        {
            LOGF(("VBOXESC_SETVISIBLEREGION"));
            LPRGNDATA lpRgnData = (LPRGNDATA)pvIn;
            DWORD cRects;

            if (    cjIn >= sizeof(RGNDATAHEADER)
                &&  pvIn
                &&  lpRgnData->rdh.dwSize == sizeof(RGNDATAHEADER)
                &&  lpRgnData->rdh.iType  == RDH_RECTANGLES
                &&  (cRects = lpRgnData->rdh.nCount) <= _1M
                &&  cjIn == cRects * (uint64_t)sizeof(RECT) + sizeof(RGNDATAHEADER))
            {
                /** @todo this whole conversion thing could maybe be skipped
                 *        since RTRECT matches the RECT layout. */
#if 0
                AssertCompile(sizeof(RTRECT) == sizeof(RECT));
                AssertCompileMembersSameSizeAndOffset(RTRECT, xLeft,    RECT, left);
                AssertCompileMembersSameSizeAndOffset(RTRECT, xBottom,  RECT, bottom);
                AssertCompileMembersSameSizeAndOffset(RTRECT, xRight,   RECT, right);
                AssertCompileMembersSameSizeAndOffset(RTRECT, xTop,     RECT, top);

                rc = VBoxDispMPSetVisibleRegion(pDev->hDriver, (PRTRECT)&lpRgnData->Buffer[0], cRects);
                VBOX_WARNRC(rc);
#else
                DWORD   i;
                PRTRECT pRTRect;
                int     rc;
                RECT   *pRect = (RECT *)&lpRgnData->Buffer;

                pRTRect = (PRTRECT) EngAllocMem(0, cRects*sizeof(RTRECT), MEM_ALLOC_TAG);
                if (!pRTRect)
                {
                    WARN(("failed to allocate %d bytes", cRects*sizeof(RTRECT)));
                    break;
                }

                for (i = 0; i < cRects; ++i)
                {
                    LOG(("New visible rectangle (%d,%d) (%d,%d)",
                         pRect[i].left, pRect[i].bottom, pRect[i].right, pRect[i].top));
                    pRTRect[i].xLeft   = pRect[i].left;
                    pRTRect[i].yBottom = pRect[i].bottom;
                    pRTRect[i].xRight  = pRect[i].right;
                    pRTRect[i].yTop    = pRect[i].top;
                }

                rc = VBoxDispMPSetVisibleRegion(pDev->hDriver, pRTRect, cRects);
                VBOX_WARNRC(rc);

                EngFreeMem(pRTRect);

#endif
                if (RT_SUCCESS(rc))
                {
                    LOGF_LEAVE();
                    return 1;
                }
            }
            else
            {
                if (pvIn)
                {
                    WARN(("check failed rdh.dwSize=%x iType=%d size=%d expected size=%d",
                          lpRgnData->rdh.dwSize, lpRgnData->rdh.iType, cjIn,
                          lpRgnData->rdh.nCount * sizeof(RECT) + sizeof(RGNDATAHEADER)));
                }
            }
            break;
        }
        case VBOXESC_ISANYX:
        {
            if (pvOut && cjOut == sizeof(DWORD))
            {
                DWORD cbReturned;
                DWORD dwrc = EngDeviceIoControl(pDev->hDriver, IOCTL_VIDEO_VBOX_ISANYX, NULL, 0,
                        pvOut, sizeof (uint32_t), &cbReturned);
                if (dwrc == NO_ERROR && cbReturned == sizeof (uint32_t))
                    return 1;
                WARN(("EngDeviceIoControl failed, dwrc(%d), cbReturned(%d)", dwrc, cbReturned));
                return 0;
            }
            else
            {
                WARN(("VBOXESC_ISANYX invalid parms"));
                return 0;
            }
            break;
        }
        default:
        {
            LOG(("unsupported iEsc %#x", iEsc));
        }
    }

    LOGF_LEAVE();
    return 0;
}

#define FB_OFFSET(_dev, _x, _y) ((_y)*pDev->mode.lScanlineStride) + ((_x)*((pDev->mode.ulBitsPerPel+1)/8))
/* Obsolete, NT4 specific. Called to set display offset in virtual desktop */
BOOL APIENTRY VBoxDispDrvOffset(SURFOBJ* pso, LONG x, LONG y, FLONG flReserved)
{
    RT_NOREF(flReserved);
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    LOGF(("%x %x %x\n", x, y, flReserved));

    pDev->memInfo.FrameBufferBase = ((BYTE*)pDev->memInfo.VideoRamBase) + pDev->layout.offFramebuffer
                                    - FB_OFFSET(pDev, x, y);

    pDev->orgDisp.x = x;
    pDev->orgDisp.y = y;

    LOGF_LEAVE();
    return TRUE;
}

/* Called to notify driver about various events */
VOID APIENTRY VBoxDispDrvNotify(SURFOBJ *pso, ULONG iType, PVOID pvData)
{
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)pso->dhpdev;
    LOGF_ENTER();

    switch (iType)
    {
        case DN_DEVICE_ORIGIN:
        {
            /*device origin in dualview*/
            POINTL *pOrg = (POINTL *)pvData;
            if (pOrg)
            {
                LOG(("DN_DEVICE_ORIGIN (pso=%p, pDev[%d]=%p) old=%d,%d new=%d,%d",
                     pso, pDev->iDevice, pDev, pDev->orgDev.x, pDev->orgDev.y, pOrg->x, pOrg->y));
                if (pDev->orgDev.x!=pOrg->x || pDev->orgDev.y!=pOrg->y)
                {
                    pDev->orgDev = *pOrg;

                    /* Inform host about display change */
                    VBoxHGSMIProcessDisplayInfo(&pDev->hgsmi.ctx, pDev->iDevice, pDev->orgDev.x, pDev->orgDev.y,
                                                0, abs(pDev->mode.lScanlineStride),
                                                pDev->mode.ulWidth, pDev->mode.ulHeight,
                                                (uint16_t)pDev->mode.ulBitsPerPel, VBVA_SCREEN_F_ACTIVE);
                }
            }
            else
            {
                WARN(("DN_DEVICE_ORIGIN pvData==NULL"));
            }

            break;
        }
        case DN_DRAWING_BEGIN:
        {
            /*first drawing op is about to happen for this device*/
            LOG(("DN_DRAWING_BEGIN (pso=%p, pDev[%d]=%p)", pso, pDev->iDevice, pDev));
            break;
        }
        default:
        {
            LOG(("unknown iType=%#x", iType));
        }
    }

    LOGF_LEAVE();
    return;
}
