/* $Id: DisplaySourceBitmapImpl.cpp 114182 2026-05-22 16:32:53Z vitali.pelenjow@oracle.com $ */
/** @file
 * Bitmap of a guest screen implementation.
 */

/*
 * Copyright (C) 2014-2025 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_DISPLAYSOURCEBITMAP
#include "LoggingNew.h"

#include "DisplayImpl.h"

#include <VBox/vmm/pdmifs.h>

/*
 * DisplaySourceBitmap implementation.
 */
DEFINE_EMPTY_CTOR_DTOR(DisplaySourceBitmap)

HRESULT DisplaySourceBitmap::FinalConstruct()
{
    return BaseFinalConstruct();
}

void DisplaySourceBitmap::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

HRESULT DisplaySourceBitmap::init(ComObjPtr<Display> pDisplay, unsigned uScreenId, DISPLAYFBINFO *pFBInfo)
{
    LogFlowThisFunc(("[%u]\n", uScreenId));

    ComAssertRet(!pDisplay.isNull(), E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m.pDisplay = pDisplay;
    m.uScreenId = uScreenId;
    m.pFBInfo = pFBInfo;

    m.pu8Allocated = NULL;
    m.u64OutputTargetToken = 0;

    m.pu8Address = NULL;
    m.ulWidth = 0;
    m.ulHeight = 0;
    m.ulBitsPerPixel = 0;
    m.ulBytesPerLine = 0;
    m.bitmapFormat = BitmapFormat_Opaque;

    int vrc = initSourceBitmap(uScreenId, pFBInfo);
    if (RT_FAILURE(vrc))
        return E_FAIL;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

void DisplaySourceBitmap::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFunc(("[%u]\n", m.uScreenId));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (m.u64OutputTargetToken != 0)
    {
        /* mpDrv can be NULL when VM is powered off. The graphics device cleans up output targets in this case. */
        if (m.pDisplay->mpDrv)
            m.pDisplay->mpDrv->pUpPort->pfnReleaseOutputTarget(m.pDisplay->mpDrv->pUpPort, m.u64OutputTargetToken);
        m.u64OutputTargetToken = 0;
    }

    m.pDisplay.setNull();
    RTMemFree(m.pu8Allocated);
}

HRESULT DisplaySourceBitmap::getScreenId(ULONG *aScreenId)
{
    HRESULT hrc = S_OK;
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aScreenId = m.uScreenId;
    return hrc;
}

HRESULT DisplaySourceBitmap::queryBitmapInfo(BYTE **aAddress,
                                             ULONG *aWidth,
                                             ULONG *aHeight,
                                             ULONG *aBitsPerPixel,
                                             ULONG *aBytesPerLine,
                                             BitmapFormat_T *aBitmapFormat)
{
    HRESULT hrc = S_OK;
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aAddress      = m.pu8Address;
    *aWidth        = m.ulWidth;
    *aHeight       = m.ulHeight;
    *aBitsPerPixel = m.ulBitsPerPixel;
    *aBytesPerLine = m.ulBytesPerLine;
    *aBitmapFormat  = m.bitmapFormat;

    return hrc;
}

int DisplaySourceBitmap::initSourceBitmap(unsigned aScreenId,
                                          DISPLAYFBINFO *pFBInfo)
{
    RT_NOREF(aScreenId);
    int vrc = VINF_SUCCESS;

    if (pFBInfo->w == 0 || pFBInfo->h == 0)
    {
        return VERR_NOT_SUPPORTED;
    }

    BYTE *pAddress = NULL;
    ULONG ulWidth = 0;
    ULONG ulHeight = 0;
    ULONG ulBitsPerPixel = 0;
    ULONG ulBytesPerLine = 0;
    BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

    /* If VRAM pointer is NULL, then it is either a VGA mode or the output targets interface must be used. */
    int vrc2 = pFBInfo->pu8FramebufferVRAM == NULL
             ? m.pDisplay->mpDrv->pUpPort->pfnQueryDefaultOutputTargetToken(m.pDisplay->mpDrv->pUpPort,
                                                                            m.uScreenId,
                                                                            &m.u64OutputTargetToken)
             : VERR_NOT_SUPPORTED;
    if (RT_SUCCESS(vrc2))
    {
        /* From output target. */
        LogFunc(("%d from output target\n", aScreenId));
        PDMDISPLAYOUTPUTTARGETDESC desc;
        vrc = m.pDisplay->mpDrv->pUpPort->pfnOutputTargetDesc(m.pDisplay->mpDrv->pUpPort,
                                                              m.u64OutputTargetToken, &desc);
        if (RT_SUCCESS(vrc))
        {
            pAddress       = (BYTE *)desc.pvOutputBuffer;
            ulWidth        = desc.cWidth;
            ulHeight       = desc.cHeight;
            ulBitsPerPixel = 32;
            ulBytesPerLine = desc.cWidth * 4;
            bitmapFormat   = BitmapFormat_BGR;
            m.pu8Allocated = NULL;
        }
    }
    else if (pFBInfo->pu8FramebufferVRAM && pFBInfo->u16BitsPerPixel == 32 && !pFBInfo->fDisabled)
    {
        /* From VRAM. */
        LogFunc(("%d from VRAM\n", aScreenId));
        pAddress       = pFBInfo->pu8FramebufferVRAM;
        ulWidth        = pFBInfo->w;
        ulHeight       = pFBInfo->h;
        ulBitsPerPixel = pFBInfo->u16BitsPerPixel;
        ulBytesPerLine = pFBInfo->u32LineSize;
        bitmapFormat   = BitmapFormat_BGR;
        m.pu8Allocated = NULL;
    }
    else
    {
        /* Allocated byffer */
        LogFunc(("%d allocated\n", aScreenId));
        pAddress       = NULL;
        ulWidth        = pFBInfo->w;
        ulHeight       = pFBInfo->h;
        ulBitsPerPixel = 32;
        ulBytesPerLine = ulWidth * 4;
        bitmapFormat   = BitmapFormat_BGR;

        m.pu8Allocated = (uint8_t *)RTMemAllocZ(ulBytesPerLine * ulHeight);
        if (m.pu8Allocated == NULL)
        {
            vrc = VERR_NO_MEMORY;
        }
        else
        {
            pAddress = m.pu8Allocated;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        m.pu8Address = pAddress;
        m.ulWidth = ulWidth;
        m.ulHeight = ulHeight;
        m.ulBitsPerPixel = ulBitsPerPixel;
        m.ulBytesPerLine = ulBytesPerLine;
        m.bitmapFormat = bitmapFormat;
        if (pFBInfo->fDisabled)
        {
            RT_BZERO(pAddress, ulBytesPerLine * ulHeight);
        }
    }

    return vrc;
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
