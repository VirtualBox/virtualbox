/* $Id: UIMachineWindow.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineWindow class declaration.
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

#ifndef FEQT_INCLUDED_SRC_runtime_UIMachineWindow_h
#define FEQT_INCLUDED_SRC_runtime_UIMachineWindow_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMainWindow>

/* GUI includes: */
#include "UIExtraDataDefs.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif /* VBOX_WS_MAC */

/* Forward declarations: */
class QCloseEvent;
class QEvent;
class QHideEvent;
class QGridLayout;
class QShowEvent;
class QSpacerItem;
class UIActionPool;
class UIMachine;
class UIMachineLogic;
class UIMachineView;
class CSession;


/* Machine-window interface: */
class UIMachineWindow : public QMainWindow
{
    Q_OBJECT;

public:

    /* Factory functions to create/destroy machine-window: */
    static UIMachineWindow* create(UIMachineLogic *pMachineLogic, ulong uScreenId = 0);
    static void destroy(UIMachineWindow *pWhichWindow);

    /* Prepare/cleanup machine-window: */
    void prepare();
    void cleanup();

    /* Public getters: */
    ulong screenId() const { return m_uScreenId; }
    UIMachineView* machineView() const { return m_pMachineView; }
    UIMachineLogic* machineLogic() const { return m_pMachineLogic; }

    /** Returns machine UI reference. */
    UIMachine *uimachine() const;

    /** Returns action-pool reference. */
    UIActionPool *actionPool() const;

    /** Returns the machine name. */
    QString machineName() const;

    /** Returns whether the machine-window should resize to fit to the guest display.
      * @note Relevant only to normal (windowed) case. */
    bool shouldResizeToGuestDisplay() const;

    /** Restores cached window geometry.
      * @note Reimplemented in sub-classes. Base implementation does nothing. */
    virtual void restoreCachedGeometry() {}

    /** Adjusts machine-window size to correspond current machine-view size.
      * @param fAdjustPosition determines whether is it necessary to adjust position too.
      * @param fResizeToGuestDisplay determines if is it necessary to resize the window to fit to guest display size.
      * @note  Reimplemented in sub-classes. Base implementation does nothing. */
    virtual void normalizeGeometry(bool fAdjustPosition, bool fResizeToGuestDisplay) { Q_UNUSED(fAdjustPosition); Q_UNUSED(fResizeToGuestDisplay); }

    /** Adjusts machine-view size to correspond current machine-window size. */
    virtual void adjustMachineViewSize();

    /** Sends machine-view size-hint to the guest. */
    virtual void sendMachineViewSizeHint();

#ifdef VBOX_WITH_MASKED_SEAMLESS
    /* Virtual caller for base class setMask: */
    virtual void setMask(const QRegion &region);
#endif /* VBOX_WITH_MASKED_SEAMLESS */

    /** Makes sure window is exposed in required mode/state. */
    virtual void showInNecessaryMode() = 0;

    /** Updates appearance for specified @a iElement. */
    virtual void updateAppearanceOf(int iElement);

protected slots:

#ifdef VBOX_WS_NIX
    /** X11: Performs machine-window geometry normalization. */
    void sltNormalizeGeometry() { normalizeGeometry(true /* adjust position */, shouldResizeToGuestDisplay()); }
#endif /* VBOX_WS_NIX */

    /** Performs machine-window activation. */
    void sltActivateWindow() { activateWindow(); }

    /* Session event-handlers: */
    virtual void sltMachineStateChanged();

protected:

    /* Constructor: */
    UIMachineWindow(UIMachineLogic *pMachineLogic, ulong uScreenId);

    /** Handles any Qt @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    /** Handles hide @a pEvent. */
    virtual void hideEvent(QHideEvent *pEvent) RT_OVERRIDE;

    /** Close event handler. */
    void closeEvent(QCloseEvent *pCloseEvent) RT_OVERRIDE;

#ifdef VBOX_WS_MAC
    /** Mac OS X: Handles native notifications.
      * @param  strNativeNotificationName  Native notification name. */
    virtual void handleNativeNotification(const QString & /* strNativeNotificationName */) {}

    /** Mac OS X: Handles standard window button callbacks.
      * @param  enmButtonType   Brings standard window button type.
      * @param  fWithOptionKey  Brings whether the Option key was held. */
    virtual void handleStandardWindowButtonCallback(StandardWindowButtonType enmButtonType, bool fWithOptionKey);
#endif /* VBOX_WS_MAC */

    /* Prepare helpers: */
    virtual void prepareSelf();
    virtual void prepareSessionConnections();
    virtual void prepareMainLayout();
    virtual void prepareMenu() {}
    virtual void prepareStatusBar() {}
    virtual void prepareMachineView();
    virtual void prepareNotificationCenter();
    virtual void prepareVisualState() {}
    virtual void prepareHandlers();
    virtual void loadSettings() {}

    /* Cleanup helpers: */
    virtual void saveSettings() {}
    virtual void cleanupHandlers();
    virtual void cleanupVisualState() {}
    virtual void cleanupNotificationCenter();
    virtual void cleanupMachineView();
    virtual void cleanupStatusBar() {}
    virtual void cleanupMenu() {}
    virtual void cleanupMainLayout() {}
    virtual void cleanupSessionConnections();
    virtual void cleanupSelf() {}

    /* Update stuff: */
#ifdef VBOX_WITH_DEBUGGER_GUI
    void updateDbgWindows();
#endif /* VBOX_WITH_DEBUGGER_GUI */

    /* Helpers: */
    QString defaultWindowTitle() const { return m_strWindowTitlePrefix; }
    static Qt::Alignment viewAlignment(UIVisualStateType visualStateType);

#ifdef VBOX_WS_MAC
    /** Mac OS X: Handles native notifications.
      * @param  strNativeNotificationName  Native notification name.
      * @param  pWidget                    Widget, notification related to. */
    static void handleNativeNotification(const QString &strNativeNotificationName, QWidget *pWidget);

    /** Mac OS X: Handles standard window button callbacks.
      * @param  enmButtonType   Brings standard window button type.
      * @param  fWithOptionKey  Brings whether the Option key was held.
      * @param  pWidget         Brings widget, callback related to. */
    static void handleStandardWindowButtonCallback(StandardWindowButtonType enmButtonType, bool fWithOptionKey, QWidget *pWidget);
#endif /* VBOX_WS_MAC */

    /* Variables: */
    UIMachineLogic *m_pMachineLogic;
    UIMachineView *m_pMachineView;
    QString m_strWindowTitlePrefix;
    ulong m_uScreenId;
    QGridLayout *m_pMainLayout;
    QSpacerItem *m_pTopSpacer;
    QSpacerItem *m_pBottomSpacer;
    QSpacerItem *m_pLeftSpacer;
    QSpacerItem *m_pRightSpacer;

private slots:

    /* Translate stuff: */
    void sltRetranslateUI();

};

#endif /* !FEQT_INCLUDED_SRC_runtime_UIMachineWindow_h */
