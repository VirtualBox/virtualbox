/* $Id: UISoftKeyboard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISoftKeyboard class declaration.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_softkeyboard_UISoftKeyboard_h
#define FEQT_INCLUDED_SRC_softkeyboard_UISoftKeyboard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMainWindow>

/* GUI includes: */
#include "QIWithRestorableGeometry.h"

/* Forward declarations: */
class QHBoxLayout;
class QToolButton;
class UIKeyboardLayoutEditor;
class UILayoutSelector;
class UIMachine;
class UISoftKeyboardKey;
class UISoftKeyboardSettingsWidget;
class UISoftKeyboardStatusBarWidget;
class UISoftKeyboardWidget;
class QSplitter;
class QStackedWidget;

/* Type definitions: */
typedef QIWithRestorableGeometry<QMainWindow> QMainWindowWithRestorableGeometry;

class UISoftKeyboard : public QMainWindowWithRestorableGeometry
{
    Q_OBJECT;

signals:

    void sigClose();

public:

    UISoftKeyboard(QWidget *pParent, UIMachine *pMachine,
                   QWidget *pCenterWidget, QString strMachineName = QString());

protected:

    virtual bool shouldBeMaximized() const RT_OVERRIDE;
    virtual void closeEvent(QCloseEvent *event) RT_OVERRIDE;
    bool event(QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles the signal we get from the layout selector widget.
      * Selection changed is forwarded to the keyboard widget. */
    void sltLayoutSelectionChanged(const QUuid &layoutUid);
    /** Handles the signal we get from the keyboard widget. */
    void sltCurentLayoutChanged();
    void sltShowLayoutSelector();
    void sltShowLayoutEditor();
    void sltKeyToEditChanged(UISoftKeyboardKey* pKey);
    void sltLayoutEdited();
    /** Make th necessary changes to data structures when th key captions updated. */
    void sltKeyCaptionsEdited(UISoftKeyboardKey* pKey);
    void sltShowHideSidePanel();
    void sltShowHideSettingsWidget();
    void sltHandleColorThemeListSelection(const QString &strColorThemeName);
    void sltHandleKeyboardWidgetColorThemeChange();
    void sltCopyLayout();
    void sltSaveLayout();
    void sltDeleteLayout();
    void sltStatusBarMessage(const QString &strMessage);
    void sltShowHideOSMenuKeys(bool fShow);
    void sltShowHideNumPad(bool fShow);
    void sltShowHideMultimediaKeys(bool fHide);
    void sltHandleColorCellClick(int iColorRow);
    void sltResetKeyboard();
    void sltHandleHelpRequest();
    void sltSaveSettings();
    void sltReleaseKeys();

private:

    void prepareObjects();
    void prepareConnections();
    void loadSettings();
    void saveCustomColorTheme();
    void saveSelectedColorThemeName();
    void saveCurrentLayout();
    void saveDialogGeometry();
    void configure();
    void updateStatusBarMessage(const QString &strLayoutName);
    void updateLayoutSelectorList();

    UIMachine     *m_pMachine;
    QWidget       *m_pCenterWidget;
    QHBoxLayout   *m_pMainLayout;
    QString        m_strMachineName;
    QSplitter      *m_pSplitter;
    QStackedWidget *m_pSidePanelWidget;
    UISoftKeyboardWidget   *m_pKeyboardWidget;
    UIKeyboardLayoutEditor *m_pLayoutEditor;
    UILayoutSelector       *m_pLayoutSelector;

    UISoftKeyboardSettingsWidget  *m_pSettingsWidget;
    UISoftKeyboardStatusBarWidget *m_pStatusBarWidget;
    int m_iGeometrySaveTimerId;
};

#endif /* !FEQT_INCLUDED_SRC_softkeyboard_UISoftKeyboard_h */
