/* $Id: VBoxMPVidModes.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Miniport video modes related functions
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

#include "VBoxMPCommon.h"

#include <VBoxVideoVBE.h>
#include <iprt/utf16.h>

#ifdef VBOX_WITH_WDDM
# define VBOX_WITHOUT_24BPP_MODES
#endif

/* Custom video modes which are being read from registry at driver startup. */
static VIDEO_MODE_INFORMATION g_CustomVideoModes[VBOX_VIDEO_MAX_SCREENS] = { {0} };

static BOOLEAN
VBoxMPValidateVideoModeParamsGuest(PVBOXMP_DEVEXT pExt, uint32_t iDisplay, uint32_t xres, uint32_t yres, uint32_t bpp)
{
    RT_NOREF(iDisplay, xres, yres);

    switch (bpp)
    {
        case 32:
            break;
        case 24:
#ifdef VBOX_WITHOUT_24BPP_MODES
            return FALSE;
#else
            break;
#endif
        case 16:
            break;
        case 8:
#ifndef VBOX_WITH_8BPP_MODES
            return FALSE;
#else
#ifdef VBOX_XPDM_MINIPORT
            if (pExt->iDevice != 0) /* Secondary monitors do not support 8 bit */
                return FALSE;
#endif
            break;
#endif
        default:
            WARN(("Unexpected bpp (%d)", bpp));
            return FALSE;
    }
    return TRUE;
}

/* Fills given video mode BPP related fields */
static void
VBoxFillVidModeBPP(VIDEO_MODE_INFORMATION *pMode, ULONG bitsR, ULONG bitsG, ULONG bitsB,
                   ULONG maskR, ULONG maskG, ULONG maskB)
{
    pMode->NumberRedBits   = bitsR;
    pMode->NumberGreenBits = bitsG;
    pMode->NumberBlueBits  = bitsB;
    pMode->RedMask         = maskR;
    pMode->GreenMask       = maskG;
    pMode->BlueMask        = maskB;
}

/* Fills given video mode structure */
static void
VBoxFillVidModeInfo(VIDEO_MODE_INFORMATION *pMode, ULONG xres, ULONG yres, ULONG bpp, ULONG index, ULONG yoffset)
{
    LOGF(("%dx%d:%d (idx=%d, yoffset=%d)", xres, yres, bpp, index, yoffset));

    memset(pMode, 0, sizeof(VIDEO_MODE_INFORMATION));

    /*Common entries*/
    pMode->Length                       = sizeof(VIDEO_MODE_INFORMATION);
    pMode->ModeIndex                    = index;
    pMode->VisScreenWidth               = xres;
    pMode->VisScreenHeight              = yres - yoffset;
    pMode->ScreenStride                 = xres * ((bpp + 7) / 8);
    pMode->NumberOfPlanes               = 1;
    pMode->BitsPerPlane                 = bpp;
    pMode->Frequency                    = 60;
    pMode->XMillimeter                  = 320;
    pMode->YMillimeter                  = 240;
    pMode->VideoMemoryBitmapWidth       = xres;
    pMode->VideoMemoryBitmapHeight      = yres - yoffset;
    pMode->DriverSpecificAttributeFlags = 0;
    pMode->AttributeFlags               = VIDEO_MODE_GRAPHICS | VIDEO_MODE_COLOR | VIDEO_MODE_NO_OFF_SCREEN;

    /*BPP related entries*/
    switch (bpp)
    {
#ifdef VBOX_WITH_8BPP_MODES
        case 8:
            VBoxFillVidModeBPP(pMode, 6, 6, 6, 0, 0, 0);

            pMode->AttributeFlags |= VIDEO_MODE_PALETTE_DRIVEN | VIDEO_MODE_MANAGED_PALETTE;
            break;
#endif
        case 16:
            VBoxFillVidModeBPP(pMode, 5, 6, 5, 0xF800, 0x7E0, 0x1F);
            break;
        case 24:
        case 32:
            VBoxFillVidModeBPP(pMode, 8, 8, 8, 0xFF0000, 0xFF00, 0xFF);
            break;
        default:
            Assert(0);
            break;
    }
}

void VBoxMPCmnInitCustomVideoModes(PVBOXMP_DEVEXT pExt)
{
    VBOXMPCMNREGISTRY Registry;
    VP_STATUS rc;
    int iMode;

    LOGF_ENTER();

    rc = VBoxMPCmnRegInit(pExt, &Registry);
    VBOXMP_WARN_VPS(rc);

    /* Initialize all custom modes to the 800x600x32 */
    VBoxFillVidModeInfo(&g_CustomVideoModes[0], 800, 600, 32, 0, 0);
    for (iMode=1; iMode<RT_ELEMENTS(g_CustomVideoModes); ++iMode)
    {
        g_CustomVideoModes[iMode] = g_CustomVideoModes[0];
    }

    /* Read stored custom resolution info from registry */
    for (iMode=0; iMode<VBoxCommonFromDeviceExt(pExt)->cDisplays; ++iMode)
    {
        uint32_t CustomXRes = 0, CustomYRes = 0, CustomBPP = 0;

        if (iMode==0)
        {
            /*First name without a suffix*/
            rc = VBoxMPCmnRegQueryDword(Registry, L"CustomXRes", &CustomXRes);
            VBOXMP_WARN_VPS_NOBP(rc);
            rc = VBoxMPCmnRegQueryDword(Registry, L"CustomYRes", &CustomYRes);
            VBOXMP_WARN_VPS_NOBP(rc);
            rc = VBoxMPCmnRegQueryDword(Registry, L"CustomBPP", &CustomBPP);
            VBOXMP_WARN_VPS_NOBP(rc);
        }
        else
        {
            wchar_t wszKeyName[32];
            RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomXRes%d", iMode);
            rc = VBoxMPCmnRegQueryDword(Registry, wszKeyName, &CustomXRes);
            VBOXMP_WARN_VPS_NOBP(rc);
            RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomYRes%d", iMode);
            rc = VBoxMPCmnRegQueryDword(Registry, wszKeyName, &CustomYRes);
            VBOXMP_WARN_VPS_NOBP(rc);
            RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomBPP%d", iMode);
            rc = VBoxMPCmnRegQueryDword(Registry, wszKeyName, &CustomBPP);
            VBOXMP_WARN_VPS_NOBP(rc);
        }

        LOG(("got stored custom resolution[%d] %dx%dx%d", iMode, CustomXRes, CustomYRes, CustomBPP));

        if (CustomXRes || CustomYRes || CustomBPP)
        {
            if (CustomXRes == 0)
            {
                CustomXRes = g_CustomVideoModes[iMode].VisScreenWidth;
            }
            if (CustomYRes == 0)
            {
                CustomYRes = g_CustomVideoModes[iMode].VisScreenHeight;
            }
            if (CustomBPP == 0)
            {
                CustomBPP = g_CustomVideoModes[iMode].BitsPerPlane;
            }

            if (VBoxMPValidateVideoModeParamsGuest(pExt, iMode, CustomXRes, CustomYRes, CustomBPP))
            {
                VBoxFillVidModeInfo(&g_CustomVideoModes[iMode], CustomXRes, CustomYRes, CustomBPP, 0, 0);
            }
        }
    }

    rc = VBoxMPCmnRegFini(Registry);
    VBOXMP_WARN_VPS(rc);
    LOGF_LEAVE();
}

VIDEO_MODE_INFORMATION *VBoxMPCmnGetCustomVideoModeInfo(ULONG ulIndex)
{
    return (ulIndex<RT_ELEMENTS(g_CustomVideoModes)) ? &g_CustomVideoModes[ulIndex] : NULL;
}

#ifdef VBOX_XPDM_MINIPORT
VIDEO_MODE_INFORMATION* VBoxMPCmnGetVideoModeInfo(PVBOXMP_DEVEXT pExt, ULONG ulIndex)
{
    return (ulIndex<RT_ELEMENTS(pExt->aVideoModes)) ? &pExt->aVideoModes[ulIndex] : NULL;
}
#endif

static bool VBoxMPVideoModesMatch(const PVIDEO_MODE_INFORMATION pMode1, const PVIDEO_MODE_INFORMATION pMode2)
{
    return pMode1->VisScreenHeight == pMode2->VisScreenHeight
           && pMode1->VisScreenWidth == pMode2->VisScreenWidth
           && pMode1->BitsPerPlane == pMode2->BitsPerPlane;
}

static int
VBoxMPFindVideoMode(const PVIDEO_MODE_INFORMATION pModesTable, int cModes, const PVIDEO_MODE_INFORMATION pMode)
{
    for (int i = 0; i < cModes; ++i)
    {
        if (VBoxMPVideoModesMatch(pMode, &pModesTable[i]))
        {
            return i;
        }
    }
    return -1;
}

/* Helper function to dynamically build our table of standard video
 * modes. We take the amount of VRAM and create modes with standard
 * geometries until we've either reached the maximum number of modes
 * or the available VRAM does not allow for additional modes.
 * We also check registry for manually added video modes.
 * Returns number of modes added to the table.
 */
static uint32_t
VBoxMPFillModesTable(PVBOXMP_DEVEXT pExt, int iDisplay, PVIDEO_MODE_INFORMATION pModesTable, size_t tableSize,
                     int32_t *pPrefModeIdx)
{
    /* the resolution matrix */
    struct
    {
        uint16_t xRes;
        uint16_t yRes;
    } resolutionMatrix[] =
    {
        /* standard modes */
        { 640,   480 },
        { 800,   600 },
        { 1024,  768 },
        { 1152,  864 },
        { 1280,  960 },
        { 1280, 1024 },
        { 1400, 1050 },
        { 1600, 1200 },
        { 1920, 1440 },
#ifndef VBOX_WITH_WDDM
        /* multi screen modes with 1280x1024 */
        { 2560, 1024 },
        { 3840, 1024 },
        { 5120, 1024 },
        /* multi screen modes with 1600x1200 */
        { 3200, 1200 },
        { 4800, 1200 },
        { 6400, 1200 },
#endif
    };

#ifdef VBOX_XPDM_MINIPORT
    ULONG vramSize = pExt->pPrimary->u.primary.ulMaxFrameBufferSize;
#else
    ULONG vramSize = vboxWddmVramCpuVisibleSegmentSize(pExt);
    vramSize /= pExt->u.primary.commonInfo.cDisplays;
    if (!g_VBoxDisplayOnly)
    {
        /* at least two surfaces will be needed: primary & shadow */
        vramSize /= 2;
    }
    vramSize &= ~PAGE_OFFSET_MASK;
#endif

    uint32_t iMode=0, iPrefIdx=0;
    /* there are 4 color depths: 8, 16, 24 and 32bpp and we reserve 50% of the modes for other sources */
    size_t   maxModesPerColorDepth = VBOXMP_MAX_VIDEO_MODES / 2 / 4;

    /* Always add 800x600 video modes. Windows XP+ needs at least 800x600 resolution
     * and fallbacks to 800x600x4bpp VGA mode if the driver did not report suitable modes.
     * This resolution could be rejected by a low resolution host (netbooks, etc).
     */
#ifdef VBOX_WITH_8BPP_MODES
    int bytesPerPixel=1;
#else
    int bytesPerPixel=2;
#endif
    for (; bytesPerPixel<=4; bytesPerPixel++)
    {
        int bitsPerPixel = 8*bytesPerPixel;

        if (800*600*bytesPerPixel > (LONG)vramSize)
        {
            /* we don't have enough VRAM for this mode */
            continue;
        }

        if (!VBoxMPValidateVideoModeParamsGuest(pExt, iMode, 800, 600, bitsPerPixel))
            continue;

        VBoxFillVidModeInfo(&pModesTable[iMode], 800, 600, bitsPerPixel, iMode+1, 0);

        if (32==bitsPerPixel)
        {
            iPrefIdx = iMode;
        }
        ++iMode;
    }

    /* Query yoffset from the host */
    ULONG yOffset = VBoxGetHeightReduction();

    /* Iterate through our static resolution table and add supported video modes for different bpp's */
#ifdef VBOX_WITH_8BPP_MODES
    bytesPerPixel=1;
#else
    bytesPerPixel=2;
#endif
    for (; bytesPerPixel<=4; bytesPerPixel++)
    {
        int bitsPerPixel = 8*bytesPerPixel;
        size_t cAdded, resIndex;

        for (cAdded=0, resIndex=0; resIndex<RT_ELEMENTS(resolutionMatrix) && cAdded<maxModesPerColorDepth; resIndex++)
        {
            if (resolutionMatrix[resIndex].xRes * resolutionMatrix[resIndex].yRes * bytesPerPixel > (LONG)vramSize)
            {
                /* we don't have enough VRAM for this mode */
                continue;
            }

            if (yOffset == 0 && resolutionMatrix[resIndex].xRes == 800 && resolutionMatrix[resIndex].yRes == 600)
            {
                /* this mode was already added */
                continue;
            }

            if (
#ifdef VBOX_WDDM_MINIPORT
                    /* 1024x768 resolution is a minimal resolutions for win8 to make most metro apps run.
                     * For small host display resolutions, host will dislike the mode 1024x768 and above
                     * if the framebuffer window requires scrolling to fit the guest resolution.
                     * So add 1024x768 resolution for win8 guest to allow user switch to it */
                       (   (VBoxQueryWinVersion(NULL) != WIN8 && VBoxQueryWinVersion(NULL) != WIN81)
                        || resolutionMatrix[resIndex].xRes != 1024
                        || resolutionMatrix[resIndex].yRes != 768)
                    &&
#endif
                       !VBoxLikesVideoMode(iDisplay, resolutionMatrix[resIndex].xRes,
                                           resolutionMatrix[resIndex].yRes - yOffset, bitsPerPixel))
            {
                /* host doesn't like this mode */
                continue;
            }

            if (!VBoxMPValidateVideoModeParamsGuest(pExt, iDisplay, resolutionMatrix[resIndex].xRes, resolutionMatrix[resIndex].yRes, bitsPerPixel))
            {
                /* guest does not like this mode */
                continue;
            }

            /* Sanity check, we shouldn't ever get here */
            if (iMode >= tableSize)
            {
                WARN(("video modes table overflow!"));
                break;
            }

            VBoxFillVidModeInfo(&pModesTable[iMode], resolutionMatrix[resIndex].xRes, resolutionMatrix[resIndex].yRes, bitsPerPixel, iMode+1, yOffset);
            ++iMode;
            ++cAdded;
        }
    }

    /* Check registry for manually added modes, up to 128 entries is supported
     * Give up on the first error encountered.
     */
    VBOXMPCMNREGISTRY Registry;
    int fPrefSet=0;
    VP_STATUS rc;

    rc = VBoxMPCmnRegInit(pExt, &Registry);
    VBOXMP_WARN_VPS(rc);

    for (int curKey=0; curKey<128; curKey++)
    {
        if (iMode>=tableSize)
        {
            WARN(("ignoring possible custom mode(s), table is full!"));
            break;
        }

        wchar_t wszKeyName[24];
        uint32_t xres, yres, bpp = 0;

        RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomMode%dWidth", curKey);
        rc = VBoxMPCmnRegQueryDword(Registry, wszKeyName, &xres);
        VBOXMP_CHECK_VPS_BREAK(rc);

        RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomMode%dHeight", curKey);
        rc = VBoxMPCmnRegQueryDword(Registry, wszKeyName, &yres);
        VBOXMP_CHECK_VPS_BREAK(rc);

        RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomMode%dBPP", curKey);
        rc = VBoxMPCmnRegQueryDword(Registry, wszKeyName, &bpp);
        VBOXMP_CHECK_VPS_BREAK(rc);

        LOG(("got custom mode[%u]=%ux%u:%u", curKey, xres, yres, bpp));

        /* round down width to be a multiple of 8 if necessary */
        if (!VBoxCommonFromDeviceExt(pExt)->fAnyX)
        {
            xres &= 0xFFF8;
        }

        if (   (xres > (1 << 16))
            || (yres > (1 << 16))
            || (   (bpp != 16)
                && (bpp != 24)
                && (bpp != 32)))
        {
            /* incorrect values */
            break;
        }

        /* does it fit within our VRAM? */
        if (xres * yres * (bpp / 8) > vramSize)
        {
            /* we don't have enough VRAM for this mode */
            break;
        }

        if (!VBoxLikesVideoMode(iDisplay, xres, yres, bpp))
        {
            /* host doesn't like this mode */
            break;
        }

        if (!VBoxMPValidateVideoModeParamsGuest(pExt, iDisplay, xres, yres, bpp))
        {
            /* guest does not like this mode */
            continue;
        }

        LOG(("adding video mode from registry."));

        VBoxFillVidModeInfo(&pModesTable[iMode], xres, yres, bpp, iMode+1, yOffset);

        if (!fPrefSet)
        {
            fPrefSet = 1;
            iPrefIdx = iMode;
        }
#ifdef VBOX_WDDM_MINIPORT
        /*check if the same mode has been added to the table already*/
        int foundIdx = VBoxMPFindVideoMode(pModesTable, iMode, &pModesTable[iMode]);

        if (foundIdx>=0)
        {
            if (iPrefIdx==iMode)
            {
                iPrefIdx=foundIdx;
            }
        }
        else
#endif
        {
            ++iMode;
        }
    }

    rc = VBoxMPCmnRegFini(Registry);
    VBOXMP_WARN_VPS(rc);

    if (pPrefModeIdx)
    {
        *pPrefModeIdx = iPrefIdx;
    }

    return iMode;
}

/* Returns if we're in the first mode change, ie doesn't have valid video mode set yet */
static BOOLEAN VBoxMPIsStartingUp(PVBOXMP_DEVEXT pExt, uint32_t iDisplay)
{
#ifdef VBOX_XPDM_MINIPORT
    RT_NOREF(iDisplay);
    return (pExt->CurrentMode == 0);
#else
    VBOXWDDM_SOURCE *pSource = &pExt->aSources[iDisplay];
    return !pSource->AllocData.SurfDesc.width || !pSource->AllocData.SurfDesc.height;
#endif
}

#ifdef VBOX_WDDM_MINIPORT
static const uint32_t g_aVBoxVidModesSupportedBpps[] = {
        32
#ifndef VBOX_WITHOUT_24BPP_MODES
        , 24
#endif
        , 16
#ifdef VBOX_WITH_8BPP_MODES
        , 8
#endif
};
DECLINLINE(BOOLEAN) VBoxMPIsSupportedBpp(uint32_t bpp)
{
    for (int i = 0; i < RT_ELEMENTS(g_aVBoxVidModesSupportedBpps); ++i)
    {
        if (bpp == g_aVBoxVidModesSupportedBpps[i])
            return TRUE;
    }
    return FALSE;
}

DECLINLINE(uint32_t) VBoxMPAdjustBpp(uint32_t bpp)
{
    if (VBoxMPIsSupportedBpp(bpp))
        return bpp;
    Assert(g_aVBoxVidModesSupportedBpps[0] == 32);
    return g_aVBoxVidModesSupportedBpps[0];
}
#endif
/* Updates missing video mode params with current values,
 * Checks if resulting mode is liked by the host and fits into VRAM.
 * Returns TRUE if resulting mode could be used.
 */
static BOOLEAN
VBoxMPValidateVideoModeParams(PVBOXMP_DEVEXT pExt, uint32_t iDisplay, uint32_t &xres, uint32_t &yres, uint32_t &bpp)
{
    /* Make sure all important video mode values are set */
    if (VBoxMPIsStartingUp(pExt, iDisplay))
    {
        /* Use stored custom values only if nothing was read from host. */
        xres = xres ? xres:g_CustomVideoModes[iDisplay].VisScreenWidth;
        yres = yres ? yres:g_CustomVideoModes[iDisplay].VisScreenHeight;
        bpp  = bpp  ? bpp :g_CustomVideoModes[iDisplay].BitsPerPlane;
    }
    else
    {
        /* Use current values for field which weren't read from host. */
#ifdef VBOX_XPDM_MINIPORT
        xres = xres ? xres:pExt->CurrentModeWidth;
        yres = yres ? yres:pExt->CurrentModeHeight;
        bpp  = bpp  ? bpp :pExt->CurrentModeBPP;
#else
        PVBOXWDDM_ALLOC_DATA pAllocData = pExt->aSources[iDisplay].pPrimaryAllocation ?
                  &pExt->aSources[iDisplay].pPrimaryAllocation->AllocData
                : &pExt->aSources[iDisplay].AllocData;
        xres = xres ? xres:pAllocData->SurfDesc.width;
        yres = yres ? yres:pAllocData->SurfDesc.height;
        /* VBox WDDM driver does not allow 24 modes since OS could choose the 24bit mode as default in that case,
         * the pExt->aSources[iDisplay].AllocData.SurfDesc.bpp could be initially 24 though,
         * i.e. when driver occurs the current mode on driver load via DxgkCbAcquirePostDisplayOwnership
         * and until driver reports the supported modes
         * This is true for Win8 Display-Only driver currently since DxgkCbAcquirePostDisplayOwnership is only used by it
         *
         * This is why we need to adjust the current mode bpp to the value we actually report as supported */
        bpp  = bpp  ? bpp : VBoxMPAdjustBpp(pAllocData->SurfDesc.bpp);
#endif
    }

    /* Round down width to be a multiple of 8 if necessary */
    if (!VBoxCommonFromDeviceExt(pExt)->fAnyX)
    {
        xres &= 0xFFF8;
    }

    /* We always need bpp to be set */
    if (!bpp)
    {
        bpp=32;
    }

    if (!VBoxMPValidateVideoModeParamsGuest(pExt, iDisplay, xres, yres, bpp))
    {
        WARN(("GUEST does not like special mode %dx%d:%d for display %d", xres, yres, bpp, iDisplay));
        return FALSE;
    }

    /* Check if host likes this mode */
    if (!VBoxLikesVideoMode(iDisplay, xres, yres, bpp))
    {
        WARN_NOBP(("HOST does not like special mode %dx%d:%d for display %d", xres, yres, bpp, iDisplay));
        return FALSE;
    }

#ifdef VBOX_XPDM_MINIPORT
    ULONG vramSize = pExt->pPrimary->u.primary.ulMaxFrameBufferSize;
#else
    ULONG vramSize = vboxWddmVramCpuVisibleSegmentSize(pExt);
    vramSize /= pExt->u.primary.commonInfo.cDisplays;
    if (!g_VBoxDisplayOnly)
    {
        /* at least two surfaces will be needed: primary & shadow */
        vramSize /= 2;
    }
    vramSize &= ~PAGE_OFFSET_MASK;
#endif

    /* Check that values are valid and mode fits into VRAM */
    if (!xres || !yres
        || !((bpp == 16)
#ifdef VBOX_WITH_8BPP_MODES
             || (bpp == 8)
#endif
             || (bpp == 24)
             || (bpp == 32)))
    {
        LOG(("invalid params for special mode %dx%d:%d", xres, yres, bpp));
        return FALSE;
    }



    if ((xres * yres * (bpp / 8) >= vramSize))
    {
        /* Store values of last reported release log message to avoid log flooding. */
        static uint32_t s_xresNoVRAM=0, s_yresNoVRAM=0, s_bppNoVRAM=0;

        LOG(("not enough VRAM for video mode %dx%dx%dbpp. Available: %d bytes. Required: more than %d bytes.",
             xres, yres, bpp, vramSize, xres * yres * (bpp / 8)));

        s_xresNoVRAM = xres;
        s_yresNoVRAM = yres;
        s_bppNoVRAM = bpp;

        return FALSE;
    }

    return TRUE;
}

/* Checks if there's a pending video mode change hint,
 * and fills pPendingMode with associated info.
 * returns TRUE if there's a pending change. Otherwise returns FALSE.
 */
static BOOLEAN
VBoxMPCheckPendingVideoMode(PVBOXMP_DEVEXT pExt, PVIDEO_MODE_INFORMATION pPendingMode)
{
    uint32_t xres=0, yres=0, bpp=0, display=0;

    /* Check if there's a pending display change request for this display */
    if (VBoxQueryDisplayRequest(&xres, &yres, &bpp, &display) && (xres || yres || bpp))
    {
        if (display >= RT_ELEMENTS(g_CustomVideoModes))
        {
            /*display = RT_ELEMENTS(g_CustomVideoModes) - 1;*/
            WARN(("VBoxQueryDisplayRequest returned invalid display number %d", display));
            return FALSE;
        }
    }
    else
    {
        LOG(("no pending request"));
        return FALSE;
    }

    /* Correct video mode params and check if host likes it */
    if (VBoxMPValidateVideoModeParams(pExt, display, xres, yres, bpp))
    {
        VBoxFillVidModeInfo(pPendingMode, xres, yres, bpp, display, 0);
        return TRUE;
    }

    return FALSE;
}

/* Save custom mode info to registry */
static void VBoxMPRegSaveModeInfo(PVBOXMP_DEVEXT pExt, uint32_t iDisplay, PVIDEO_MODE_INFORMATION pMode)
{
    VBOXMPCMNREGISTRY Registry;
    VP_STATUS rc;

    rc = VBoxMPCmnRegInit(pExt, &Registry);
    VBOXMP_WARN_VPS(rc);

    if (iDisplay==0)
    {
        /*First name without a suffix*/
        rc = VBoxMPCmnRegSetDword(Registry, L"CustomXRes", pMode->VisScreenWidth);
        VBOXMP_WARN_VPS(rc);
        rc = VBoxMPCmnRegSetDword(Registry, L"CustomYRes", pMode->VisScreenHeight);
        VBOXMP_WARN_VPS(rc);
        rc = VBoxMPCmnRegSetDword(Registry, L"CustomBPP", pMode->BitsPerPlane);
        VBOXMP_WARN_VPS(rc);
    }
    else
    {
        wchar_t wszKeyName[32];
        RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomXRes%d", iDisplay);
        rc = VBoxMPCmnRegSetDword(Registry, wszKeyName, pMode->VisScreenWidth);
        VBOXMP_WARN_VPS(rc);
        RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomYRes%d", iDisplay);
        rc = VBoxMPCmnRegSetDword(Registry, wszKeyName, pMode->VisScreenHeight);
        VBOXMP_WARN_VPS(rc);
        RTUtf16Printf(wszKeyName, RT_ELEMENTS(wszKeyName), "CustomBPP%d", iDisplay);
        rc = VBoxMPCmnRegSetDword(Registry, wszKeyName, pMode->BitsPerPlane);
        VBOXMP_WARN_VPS(rc);
    }

    rc = VBoxMPCmnRegFini(Registry);
    VBOXMP_WARN_VPS(rc);
}

#ifdef VBOX_XPDM_MINIPORT
VIDEO_MODE_INFORMATION* VBoxMPXpdmCurrentVideoMode(PVBOXMP_DEVEXT pExt)
{
    return VBoxMPCmnGetVideoModeInfo(pExt, pExt->CurrentMode - 1);
}

ULONG VBoxMPXpdmGetVideoModesCount(PVBOXMP_DEVEXT pExt)
{
    return pExt->cVideoModes;
}

/* Makes a table of video modes consisting of:
 * Default modes
 * Custom modes manually added to registry
 * Custom modes for all displays (either from a display change hint or stored in registry)
 * 2 special modes, for a pending display change for this adapter. See comments below.
 */
void VBoxMPXpdmBuildVideoModesTable(PVBOXMP_DEVEXT pExt)
{
    uint32_t cStandartModes;
    BOOLEAN bPending, bHaveSpecial;
    VIDEO_MODE_INFORMATION specialMode;

    /* Fill table with standart modes and ones manually added to registry.
     * Up to VBOXMP_MAX_VIDEO_MODES elements can be used, the rest is reserved
     * for custom mode alternating indexes.
     */
    cStandartModes = VBoxMPFillModesTable(pExt, pExt->iDevice, pExt->aVideoModes, VBOXMP_MAX_VIDEO_MODES, NULL);

    /* Add custom mode for this display to the table */
    /* Make 2 entries in the video mode table. */
    uint32_t iModeBase = cStandartModes;

    /* Take the alternating index into account. */
    BOOLEAN bAlternativeIndex = (pExt->iInvocationCounter % 2)? TRUE: FALSE;

    uint32_t iSpecialMode = iModeBase + (bAlternativeIndex? 1: 0);
    uint32_t iStandardMode = iModeBase + (bAlternativeIndex? 0: 1);

    /* Fill the special mode. */
    memcpy(&pExt->aVideoModes[iSpecialMode], &g_CustomVideoModes[pExt->iDevice], sizeof(VIDEO_MODE_INFORMATION));
    pExt->aVideoModes[iSpecialMode].ModeIndex = iSpecialMode + 1;

    /* Wipe the other entry so it is not selected. */
    memcpy(&pExt->aVideoModes[iStandardMode], &pExt->aVideoModes[3], sizeof(VIDEO_MODE_INFORMATION));
    pExt->aVideoModes[iStandardMode].ModeIndex = iStandardMode + 1;

    LOG(("added special mode[%d] %dx%d:%d for display %d\n",
         iSpecialMode,
         pExt->aVideoModes[iSpecialMode].VisScreenWidth,
         pExt->aVideoModes[iSpecialMode].VisScreenHeight,
         pExt->aVideoModes[iSpecialMode].BitsPerPlane,
         pExt->iDevice));

    /* Check if host wants us to switch video mode and it's for this adapter */
    bPending = VBoxMPCheckPendingVideoMode(pExt, &specialMode);
    bHaveSpecial = bPending && (pExt->iDevice == specialMode.ModeIndex);
    LOG(("bPending %d, pExt->iDevice %d, specialMode.ModeIndex %d",
          bPending, pExt->iDevice, specialMode.ModeIndex));

    /* Check the startup case */
    if (!bHaveSpecial && VBoxMPIsStartingUp(pExt, pExt->iDevice))
    {
        uint32_t xres=0, yres=0, bpp=0;
        LOG(("Startup for screen %d", pExt->iDevice));
        /* Check if we could make valid mode from values stored to registry */
        if (VBoxMPValidateVideoModeParams(pExt, pExt->iDevice, xres, yres, bpp))
        {
            LOG(("Startup for screen %d validated %dx%d %d", pExt->iDevice, xres, yres, bpp));
            VBoxFillVidModeInfo(&specialMode, xres, yres, bpp, 0, 0);
            bHaveSpecial = TRUE;
        }
    }

    /* Update number of modes. Each display has 2 entries for alternating custom mode index. */
    pExt->cVideoModes = cStandartModes + 2;

    if (bHaveSpecial)
    {
        /* We need to alternate mode index entry for a pending mode change,
         * else windows will ignore actual mode change call.
         * Only alternate index if one of mode parameters changed and
         * regardless of conditions always add 2 entries to the table.
         */
        bAlternativeIndex = FALSE;

        BOOLEAN bChanged = (pExt->Prev_xres!=specialMode.VisScreenWidth
                            || pExt->Prev_yres!=specialMode.VisScreenHeight
                            || pExt->Prev_bpp!=specialMode.BitsPerPlane);

        LOG(("prev %dx%dx%d, special %dx%dx%d",
             pExt->Prev_xres, pExt->Prev_yres, pExt->Prev_bpp,
             specialMode.VisScreenWidth, specialMode.VisScreenHeight, specialMode.BitsPerPlane));

        if (bChanged)
        {
            pExt->Prev_xres = specialMode.VisScreenWidth;
            pExt->Prev_yres = specialMode.VisScreenHeight;
            pExt->Prev_bpp = specialMode.BitsPerPlane;
        }

        /* Check if we need to alternate the index */
        if (!VBoxMPIsStartingUp(pExt, pExt->iDevice))
        {
            if (bChanged)
            {
                pExt->iInvocationCounter++;
            }

            if (pExt->iInvocationCounter % 2)
            {
                bAlternativeIndex = TRUE;
            }
        }

        uint32_t iSpecialModeElement = cStandartModes + (bAlternativeIndex? 1: 0);
        uint32_t iSpecialModeElementOld = cStandartModes + (bAlternativeIndex? 0: 1);

        LOG(("add special mode[%d] %dx%d:%d for display %d (bChanged=%d, bAlternativeIndex=%d)",
             iSpecialModeElement, specialMode.VisScreenWidth, specialMode.VisScreenHeight, specialMode.BitsPerPlane,
             pExt->iDevice, bChanged, bAlternativeIndex));

        /* Add special mode to the table
         * Note: Y offset isn't used for a host-supplied modes
         */
        specialMode.ModeIndex = iSpecialModeElement + 1;
        memcpy(&pExt->aVideoModes[iSpecialModeElement], &specialMode, sizeof(VIDEO_MODE_INFORMATION));

        /* Save special mode in the custom modes table */
        memcpy(&g_CustomVideoModes[pExt->iDevice], &specialMode, sizeof(VIDEO_MODE_INFORMATION));

        /* Wipe the old entry so the special mode will be found in the new positions. */
        memcpy(&pExt->aVideoModes[iSpecialModeElementOld], &pExt->aVideoModes[3], sizeof(VIDEO_MODE_INFORMATION));
        pExt->aVideoModes[iSpecialModeElementOld].ModeIndex = iSpecialModeElementOld + 1;

        /* Save special mode info to registry */
        VBoxMPRegSaveModeInfo(pExt, pExt->iDevice, &specialMode);
    }

#if defined(LOG_ENABLED)
    do
    {
        LOG(("Filled %d modes for display %d", pExt->cVideoModes, pExt->iDevice));

        for (uint32_t i=0; i < pExt->cVideoModes; ++i)
        {
            LOG(("Mode[%2d]: %4dx%4d:%2d (idx=%d)",
                 i, pExt->aVideoModes[i].VisScreenWidth, pExt->aVideoModes[i].VisScreenHeight,
                 pExt->aVideoModes[i].BitsPerPlane, pExt->aVideoModes[i].ModeIndex));
        }
    } while (0);
#endif
}
#endif /*VBOX_XPDM_MINIPORT*/
