/* $Id: VBoxDispVHWA.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox XPDM Display driver, helper functions which interacts with our miniport driver
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

#ifndef GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispVHWA_h
#define GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispVHWA_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxDisp.h"

#ifdef VBOX_WITH_VIDEOHWACCEL
typedef struct _VBOXDISPVHWAINFO
{
    uint32_t caps;
    uint32_t caps2;
    uint32_t colorKeyCaps;
    uint32_t stretchCaps;
    uint32_t surfaceCaps;
    uint32_t numOverlays;
    uint32_t numFourCC;
    HGSMIOFFSET FourCC;
    ULONG_PTR offVramBase;
    BOOLEAN bEnabled;
} VBOXDISPVHWAINFO;
#endif

typedef struct _VBOXVHWAREGION
{
    RECTL Rect;
    bool bValid;
}VBOXVHWAREGION, *PVBOXVHWAREGION;

typedef struct _VBOXVHWASURFDESC
{
    VBOXVHWA_SURFHANDLE hHostHandle;
    volatile uint32_t cPendingBltsSrc;
    volatile uint32_t cPendingBltsDst;
    volatile uint32_t cPendingFlipsCurr;
    volatile uint32_t cPendingFlipsTarg;
#ifdef DEBUG
    volatile uint32_t cFlipsCurr;
    volatile uint32_t cFlipsTarg;
#endif
    bool bVisible;
    VBOXVHWAREGION UpdatedMemRegion;
    VBOXVHWAREGION NonupdatedMemRegion;
}VBOXVHWASURFDESC, *PVBOXVHWASURFDESC;

typedef DECLCALLBACKTYPE(void, FNVBOXVHWACMDCOMPLETION,(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                                        void *pvContext));
typedef FNVBOXVHWACMDCOMPLETION *PFNVBOXVHWACMDCOMPLETION;

void VBoxDispVHWAInit(PVBOXDISPDEV pDev);
int  VBoxDispVHWAEnable(PVBOXDISPDEV pDev);
int  VBoxDispVHWADisable(PVBOXDISPDEV pDev);
int  VBoxDispVHWAInitHostInfo1(PVBOXDISPDEV pDev);
int  VBoxDispVHWAInitHostInfo2(PVBOXDISPDEV pDev, DWORD *pFourCC);

VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *VBoxDispVHWACommandCreate(PVBOXDISPDEV pDev, VBOXVHWACMD_TYPE enmCmd, VBOXVHWACMD_LENGTH cbCmd);
void VBoxDispVHWACommandRelease(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd);
BOOL VBoxDispVHWACommandSubmit(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST*pCmd);
void VBoxDispVHWACommandSubmitAsynch(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd,
                                     PFNVBOXVHWACMDCOMPLETION pfnCompletion, void * pContext);
void VBoxDispVHWACommandSubmitAsynchAndComplete(PVBOXDISPDEV pDev, VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HOST *pCmd);
void VBoxDispVHWACommandCheckHostCmds(PVBOXDISPDEV pDev);

PVBOXVHWASURFDESC VBoxDispVHWASurfDescAlloc();
void VBoxDispVHWASurfDescFree(PVBOXVHWASURFDESC pDesc);

uint64_t VBoxDispVHWAVramOffsetFromPDEV(PVBOXDISPDEV pDev, ULONG_PTR offPdev);

void VBoxDispVHWARectUnited(RECTL * pDst, RECTL * pRect1, RECTL * pRect2);
bool VBoxDispVHWARectIsEmpty(RECTL * pRect);
bool VBoxDispVHWARectIntersect(RECTL * pRect1, RECTL * pRect2);
bool VBoxDispVHWARectInclude(RECTL * pRect1, RECTL * pRect2);
bool VBoxDispVHWARegionIntersects(PVBOXVHWAREGION pReg, RECTL * pRect);
bool VBoxDispVHWARegionIncludes(PVBOXVHWAREGION pReg, RECTL * pRect);
bool VBoxDispVHWARegionIncluded(PVBOXVHWAREGION pReg, RECTL * pRect);
void VBoxDispVHWARegionSet(PVBOXVHWAREGION pReg, RECTL * pRect);
void VBoxDispVHWARegionAdd(PVBOXVHWAREGION pReg, RECTL * pRect);
void VBoxDispVHWARegionInit(PVBOXVHWAREGION pReg);
void VBoxDispVHWARegionClear(PVBOXVHWAREGION pReg);
bool VBoxDispVHWARegionValid(PVBOXVHWAREGION pReg);
void VBoxDispVHWARegionTrySubstitute(PVBOXVHWAREGION pReg, const RECTL *pRect);

uint32_t VBoxDispVHWAFromDDCAPS(uint32_t caps);
uint32_t VBoxDispVHWAToDDCAPS(uint32_t caps);
uint32_t VBoxDispVHWAFromDDSCAPS(uint32_t caps);
uint32_t VBoxDispVHWAToDDSCAPS(uint32_t caps);
uint32_t VBoxDispVHWAFromDDPFS(uint32_t caps);
uint32_t VBoxDispVHWAToDDPFS(uint32_t caps);
uint32_t VBoxDispVHWAFromDDCKEYCAPS(uint32_t caps);
uint32_t VBoxDispVHWAToDDCKEYCAPS(uint32_t caps);
uint32_t VBoxDispVHWAToDDBLTs(uint32_t caps);
uint32_t VBoxDispVHWAFromDDBLTs(uint32_t caps);
uint32_t VBoxDispVHWAFromDDCAPS2(uint32_t caps);
uint32_t VBoxDispVHWAToDDCAPS2(uint32_t caps);
uint32_t VBoxDispVHWAFromDDOVERs(uint32_t caps);
uint32_t VBoxDispVHWAToDDOVERs(uint32_t caps);
uint32_t VBoxDispVHWAFromDDCKEYs(uint32_t caps);
uint32_t VBoxDispVHWAToDDCKEYs(uint32_t caps);

int VBoxDispVHWAFromDDSURFACEDESC(VBOXVHWA_SURFACEDESC RT_UNTRUSTED_VOLATILE_HOST *pVHWADesc, DDSURFACEDESC *pDdDesc);
int VBoxDispVHWAFromDDPIXELFORMAT(VBOXVHWA_PIXELFORMAT RT_UNTRUSTED_VOLATILE_HOST *pVHWAFormat, DDPIXELFORMAT *pDdFormat);
void VBoxDispVHWAFromDDOVERLAYFX(VBOXVHWA_OVERLAYFX RT_UNTRUSTED_VOLATILE_HOST  *pVHWAOverlay, DDOVERLAYFX *pDdOverlay);
void VBoxDispVHWAFromDDCOLORKEY(VBOXVHWA_COLORKEY RT_UNTRUSTED_VOLATILE_HOST *pVHWACKey, DDCOLORKEY  *pDdCKey);
void VBoxDispVHWAFromDDBLTFX(VBOXVHWA_BLTFX RT_UNTRUSTED_VOLATILE_HOST *pVHWABlt, DDBLTFX *pDdBlt);
void VBoxDispVHWAFromRECTL(VBOXVHWA_RECTL *pDst, RECTL const *pSrc);
void VBoxDispVHWAFromRECTL(VBOXVHWA_RECTL RT_UNTRUSTED_VOLATILE_HOST *pDst, RECTL const *pSrc);

uint32_t VBoxDispVHWAUnsupportedDDCAPS(uint32_t caps);
uint32_t VBoxDispVHWAUnsupportedDDSCAPS(uint32_t caps);
uint32_t VBoxDispVHWAUnsupportedDDPFS(uint32_t caps);
uint32_t VBoxDispVHWAUnsupportedDDCEYCAPS(uint32_t caps);
uint32_t VBoxDispVHWASupportedDDCAPS(uint32_t caps);
uint32_t VBoxDispVHWASupportedDDSCAPS(uint32_t caps);
uint32_t VBoxDispVHWASupportedDDPFS(uint32_t caps);
uint32_t VBoxDispVHWASupportedDDCEYCAPS(uint32_t caps);

#endif /* !GA_INCLUDED_SRC_WINNT_Graphics_Video_disp_xpdm_VBoxDispVHWA_h */

