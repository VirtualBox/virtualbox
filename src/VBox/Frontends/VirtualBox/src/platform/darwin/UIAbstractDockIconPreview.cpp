/* $Id: UIAbstractDockIconPreview.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Realtime Dock Icon Preview
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

/* Qt includes: */
#include <QStyle>

/* GUI includes: */
#include "UIAbstractDockIconPreview.h"
#include "UICommon.h"
#include "UIConverter.h"
#include "UIExtraDataManager.h"
#include "UIFrameBuffer.h"
#include "UIMachine.h"
#include "UIMachineLogic.h"
#include "UIMachineView.h"

/* COM includes: */


UIAbstractDockIconPreview::UIAbstractDockIconPreview(UIMachine *, const QPixmap &)
{
}

void UIAbstractDockIconPreview::updateDockPreview(UIFrameBuffer *pFrameBuffer)
{
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    Assert(cs);
    /* Create the image copy of the framebuffer */
    CGDataProviderRef dp = CGDataProviderCreateWithData(pFrameBuffer, pFrameBuffer->address(), pFrameBuffer->bitsPerPixel() / 8 * pFrameBuffer->width() * pFrameBuffer->height(), NULL);
    Assert(dp);
    CGImageRef ir = CGImageCreate(pFrameBuffer->width(), pFrameBuffer->height(), 8, 32, pFrameBuffer->bytesPerLine(), cs,
                                  kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host, dp, 0, false,
                                  kCGRenderingIntentDefault);
    Assert(ir);

    /* Update the dock preview icon */
    updateDockPreview(ir);

    /* Release the temp data and image */
    CGImageRelease(ir);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
}

UIAbstractDockIconPreviewHelper::UIAbstractDockIconPreviewHelper(UIMachine *pMachine, const QPixmap& overlayImage)
    : m_pMachine(pMachine)
    , m_dockIconRect(CGRectMake(0, 0, 128, 128))
    , m_dockMonitor(NULL)
    , m_dockMonitorGlossy(NULL)
    , m_updateRect(CGRectMake(0, 0, 0, 0))
    , m_monitorRect(CGRectMake(0, 0, 0, 0))
{
    m_overlayImage = ::darwinToCGImageRef(&overlayImage);
    Assert(m_overlayImage);
}

void* UIAbstractDockIconPreviewHelper::currentPreviewWindowId() const
{
    /* Get the MachineView which is currently previewed and return the win id
       of the viewport. */
    UIMachineView* pView = m_pMachine->machineLogic()->dockPreviewView();
    if (pView)
        return (void*)pView->viewport()->winId();
    return 0;
}

UIAbstractDockIconPreviewHelper::~UIAbstractDockIconPreviewHelper()
{
    CGImageRelease(m_overlayImage);
    if (m_dockMonitor)
        CGImageRelease(m_dockMonitor);
    if (m_dockMonitorGlossy)
        CGImageRelease(m_dockMonitorGlossy);
}

void UIAbstractDockIconPreviewHelper::initPreviewImages()
{
    if (!m_dockMonitor)
    {
        m_dockMonitor = ::darwinToCGImageRef("monitor.png");
        Assert(m_dockMonitor);
        /* Center it on the dock icon context */
        m_monitorRect = centerRect(CGRectMake(0, 0,
                                              CGImageGetWidth(m_dockMonitor),
                                              CGImageGetWidth(m_dockMonitor)));
    }

    if (!m_dockMonitorGlossy)
    {
        m_dockMonitorGlossy = ::darwinToCGImageRef("monitor_glossy.png");
        Assert(m_dockMonitorGlossy);
        /* This depends on the content of monitor.png */
        m_updateRect = CGRectMake(m_monitorRect.origin.x + 8 /* left-frame */ + 1 /* indent-size */,
                                  m_monitorRect.origin.y + 8 /* top-frame  */ + 1 /* indent-size */,
                                  128 /* .png-width  */ - 8 /* left-frame */ -  8 /* right-frame  */ - 2 * 1 /* indent-size */,
                                  128 /* .png-height */ - 8 /* top-frame  */ - 25 /* bottom-frame */ - 2 * 1 /* indent-size */);
    }
}

void UIAbstractDockIconPreviewHelper::drawOverlayIcons(CGContextRef context)
{
    /* Determine whether dock icon overlay is not disabled: */
    if (!gEDataManager->dockIconDisableOverlay(uiCommon().managedVMUuid()))
    {
        /* Initialize overlay rectangle: */
        CGRect overlayRect = CGRectMake(0, 0, 0, 0);
        /* Make sure overlay image is valid: */
        if (m_overlayImage)
        {
            /* Draw overlay image at bottom-right of dock icon: */
            overlayRect = CGRectMake(m_dockIconRect.size.width - CGImageGetWidth(m_overlayImage),
                                     m_dockIconRect.size.height - CGImageGetHeight(m_overlayImage),
                                     CGImageGetWidth(m_overlayImage),
                                     CGImageGetHeight(m_overlayImage));
            CGContextDrawImage(context, flipRect(overlayRect), m_overlayImage);
        }
    }
}

