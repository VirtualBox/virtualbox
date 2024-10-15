/* $Id: UIMachineWindowNormal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineWindowNormal class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_runtime_normal_UIMachineWindowNormal_h
#define FEQT_INCLUDED_SRC_runtime_normal_UIMachineWindowNormal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIMachineWindow.h"

/* Forward declarations: */
class CMediumAttachment;
class UIIndicatorsPool;
class UIAction;

/** UIMachineWindow subclass used as normal machine window implementation. */
class UIMachineWindowNormal : public UIMachineWindow
{
    Q_OBJECT;

signals:

    /** Notifies about geometry change. */
    void sigGeometryChange(const QRect &rect);

public:

    /** Constructor, passes @a pMachineLogic and @a uScreenId to the UIMachineWindow constructor. */
    UIMachineWindowNormal(UIMachineLogic *pMachineLogic, ulong uScreenId);

private slots:

    /** Handles machine state change event. */
    void sltMachineStateChanged() RT_OVERRIDE;

#ifndef RT_OS_DARWIN
    /** Handles menu-bar configuration-change. */
    void sltHandleMenuBarConfigurationChange(const QUuid &uMachineID);
    /** Handles menu-bar context-menu-request. */
    void sltHandleMenuBarContextMenuRequest(const QPoint &position);
#endif /* !RT_OS_DARWIN */

    /** Handles status-bar configuration-change. */
    void sltHandleStatusBarConfigurationChange(const QUuid &uMachineID);
    /** Handles status-bar context-menu-request. */
    void sltHandleStatusBarContextMenuRequest(const QPoint &position);
    /** Handles status-bar indicator context-menu-request. */
    void sltHandleIndicatorContextMenuRequest(IndicatorType enmIndicatorType, const QPoint &indicatorPosition);

#ifdef VBOX_WS_MAC
    /** Handles signal about some @a pAction hovered. */
    void sltActionHovered(UIAction *pAction);
#endif /* VBOX_WS_MAC */

private:

#ifndef VBOX_WS_MAC
    /** Prepare menu routine. */
    void prepareMenu()  RT_OVERRIDE RT_FINAL;
#endif /* !VBOX_WS_MAC */
    /** Prepare status-bar routine. */
    void prepareStatusBar() RT_OVERRIDE;
    /** Prepare notification-center routine. */
    void prepareNotificationCenter() RT_OVERRIDE;
    /** Prepare visual-state routine. */
    void prepareVisualState() RT_OVERRIDE;
    /** Load settings routine. */
    void loadSettings() RT_OVERRIDE;

    /** Cleanup visual-state routine. */
    void cleanupVisualState() RT_OVERRIDE;
    /** Cleanup notification-center routine. */
    void cleanupNotificationCenter() RT_OVERRIDE;
    /** Cleanup status-bar routine. */
    void cleanupStatusBar() RT_OVERRIDE;

    /** Updates visibility according to visual-state. */
    void showInNecessaryMode() RT_OVERRIDE;

    /** Restores cached window geometry. */
    virtual void restoreCachedGeometry() RT_OVERRIDE;

    /** Performs window geometry normalization according to guest-size and host's available geometry.
      * @param  fAdjustPosition        Determines whether is it necessary to adjust position as well.
      * @param  fResizeToGuestDisplay  Determines whether is it necessary to resize the window to fit to guest display size. */
    virtual void normalizeGeometry(bool fAdjustPosition, bool fResizeToGuestDisplay) RT_OVERRIDE;

    /** Common update routine. */
    void updateAppearanceOf(int aElement) RT_OVERRIDE;

#ifndef VBOX_WS_MAC
    /** Updates menu-bar content. */
    void updateMenu();
#endif /* !VBOX_WS_MAC */

    /** Common @a pEvent handler. */
    bool event(QEvent *pEvent) RT_OVERRIDE;

    /** Returns whether this window is maximized. */
    bool isMaximizedChecked();

    /** Holds the indicator-pool instance. */
    UIIndicatorsPool *m_pIndicatorsPool;

    /** Holds the current window geometry. */
    QRect  m_geometry;
    /** Holds the geometry save timer ID. */
    int  m_iGeometrySaveTimerId;
};

#endif /* !FEQT_INCLUDED_SRC_runtime_normal_UIMachineWindowNormal_h */
