/* $Id: VBoxDispPalette.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver, palette related functions
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

#define MAX_CLUT_SIZE (sizeof(VIDEO_CLUT)+(sizeof(ULONG) * 256))

/* 10 default palette colors with it's complementors which are used for window decoration colors*/
const PALETTEENTRY defPal[10] =
{
    {0,    0,    0,    0},
    {0x80, 0,    0,    0},
    {0,    0x80, 0,    0},
    {0x80, 0x80, 0,    0},
    {0,    0,    0x80, 0},
    {0x80, 0,    0x80, 0},
    {0,    0x80, 0x80, 0},
    {0xC0, 0xC0, 0xC0, 0},
    {0xC0, 0xDC, 0xC0, 0},
    {0xA6, 0xCA, 0xF0, 0},
};

const PALETTEENTRY defPalComp[10] =
{
    {0xFF, 0xFF, 0xFF, 0},
    {0,    0xFF, 0xFF, 0},
    {0xFF, 0,    0xFF, 0},
    {0,    0,    0xFF, 0},
    {0xFF, 0xFF, 0,    0},
    {0,    0xFF, 0,    0},
    {0xFF, 0,    0,    0},
    {0x80, 0x80, 0x80, 0},
    {0xA0, 0xA0, 0xA4, 0},
    {0xFF, 0xFB, 0xF0, 0},
};

/* Creates default device palette */
int VBoxDispInitPalette(PVBOXDISPDEV pDev, DEVINFO *pDevInfo)
{
    if (pDev->mode.ulBitsPerPel!=8)
    {
        pDev->hDefaultPalette = EngCreatePalette(PAL_BITFIELDS, 0, NULL,
                                                 pDev->mode.flMaskR, pDev->mode.flMaskG, pDev->mode.flMaskB);

        if (!pDev->hDefaultPalette)
        {
            WARN(("EngCreatePalette failed"));
            return VERR_GENERAL_FAILURE;
        }

        pDevInfo->hpalDefault = pDev->hDefaultPalette;
        return VINF_SUCCESS;
    }

    /* Create driver managed palette.
     * First entry should be black (0,0,0), last should be while (255, 255, 255).
     * Note: Other entries should be set in similar way, so that entries with complementing indicies
     * have quite contrast colors stored.
     */

    pDev->pPalette = (PPALETTEENTRY) EngAllocMem(0, sizeof(PALETTEENTRY) * 256, MEM_ALLOC_TAG);
    if (!pDev->pPalette)
    {
        WARN(("not enough memory!"));
        return VERR_NO_MEMORY;
    }

    BYTE r=0, g=0, b=0;
    for (ULONG i=0; i<256; ++i)
    {
        pDev->pPalette[i].peRed = r;
        pDev->pPalette[i].peGreen = g;
        pDev->pPalette[i].peBlue = b;
        pDev->pPalette[i].peFlags = 0;

        r += 32;
        if (!r)
        {
            g += 32;
            if (!g)
            {
                b += 64;
            }
        }
    }

    /* Now overwrite window decoration colors by common ones */
    for (ULONG i=0; i<RT_ELEMENTS(defPal); ++i)
    {
        pDev->pPalette[i] = defPal[i];
        pDev->pPalette[(~i)&0xFF] = defPalComp[i];
    }

    /* Sanity check in case we'd change palette filling */
    Assert(pDev->pPalette[0].peRed==0 && pDev->pPalette[0].peGreen==0 && pDev->pPalette[0].peBlue==0);
    Assert(pDev->pPalette[255].peRed==255 && pDev->pPalette[255].peGreen==255 && pDev->pPalette[255].peBlue==255);

    pDev->hDefaultPalette = EngCreatePalette(PAL_INDEXED, 256, (PULONG)pDev->pPalette, 0, 0, 0);
    if (!pDev->hDefaultPalette)
    {
        WARN(("EngCreatePalette failed"));
        EngFreeMem(pDev->pPalette);
        pDev->pPalette = NULL;
        return VERR_GENERAL_FAILURE;
    }

    pDevInfo->hpalDefault = pDev->hDefaultPalette;
    return VINF_SUCCESS;
}

void VBoxDispDestroyPalette(PVBOXDISPDEV pDev)
{
    if (pDev->hDefaultPalette)
    {
        EngDeletePalette(pDev->hDefaultPalette);
        pDev->hDefaultPalette = 0;
    }

    if (pDev->pPalette)
    {
        EngFreeMem(pDev->pPalette);
    }
}

int VBoxDispSetPalette8BPP(PVBOXDISPDEV pDev)
{
    if (pDev->mode.ulBitsPerPel!=8)
    {
        return VERR_NOT_SUPPORTED;
    }

    BYTE aClut[MAX_CLUT_SIZE];
    PVIDEO_CLUT pClut = (PVIDEO_CLUT) aClut;
    PVIDEO_CLUTDATA pData = &pClut->LookupTable[0].RgbArray;

    /* Prepare palette info to pass to miniport */
    pClut->NumEntries = 256;
    pClut->FirstEntry = 0;
    for (ULONG idx=0; idx<256; ++idx)
    {
        pData[idx].Red = pDev->pPalette[idx].peRed >> pDev->mode.ulPaletteShift;
        pData[idx].Green = pDev->pPalette[idx].peGreen >> pDev->mode.ulPaletteShift;
        pData[idx].Blue = pDev->pPalette[idx].peBlue >> pDev->mode.ulPaletteShift;
        pData[idx].Unused = 0;
    }

    return VBoxDispMPSetColorRegisters(pDev->hDriver, pClut, MAX_CLUT_SIZE);
}

/*
 * Display driver callbacks.
 */
BOOL APIENTRY VBoxDispDrvSetPalette(DHPDEV dhpdev, PALOBJ *ppalo, FLONG fl, ULONG iStart, ULONG cColors)
{
    BYTE aClut[MAX_CLUT_SIZE];
    PVIDEO_CLUT pClut = (PVIDEO_CLUT) aClut;
    PVIDEO_CLUTDATA pData = &pClut->LookupTable[0].RgbArray;
    PVBOXDISPDEV pDev = (PVBOXDISPDEV)dhpdev;
    NOREF(fl);
    int rc;
    LOGF_ENTER();

    pClut->NumEntries = (USHORT) cColors;
    pClut->FirstEntry = (USHORT) iStart;

    /* Copy PALOBJ colors to PVIDEO_CLUT */
    if (cColors != PALOBJ_cGetColors(ppalo, iStart, cColors, (ULONG*) pData))
    {
        WARN(("PALOBJ_cGetColors failed"));
        return FALSE;
    }

    /* Set reserved bytes to 0 and perform shifting if needed */
    for (ULONG idx=0; idx<cColors; ++idx)
    {
        pData[idx].Unused = 0;
        if (pDev->mode.ulPaletteShift)
        {
            pData[idx].Red >>= pDev->mode.ulPaletteShift;
            pData[idx].Green >>= pDev->mode.ulPaletteShift;
            pData[idx].Blue >>= pDev->mode.ulPaletteShift;
        }
    }

    rc =  VBoxDispMPSetColorRegisters(pDev->hDriver, pClut, MAX_CLUT_SIZE);
    VBOX_WARNRC_RETV(rc, FALSE);

    LOGF_LEAVE();
    return TRUE;
}
