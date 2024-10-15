/* $Id: UIHelpBrowserDialog.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHelpBrowserDialog class declaration.
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

#ifndef FEQT_INCLUDED_SRC_helpbrowser_UIHelpBrowserDialog_h
#define FEQT_INCLUDED_SRC_helpbrowser_UIHelpBrowserDialog_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QMainWindow>
#include <QPointer>

/* GUI includes: */
#include "QIWithRestorableGeometry.h"

/* Forward declarations: */
class QLabel;
class UIHelpBrowserWidget;

class SHARED_LIBRARY_STUFF UIHelpBrowserDialog : public QIWithRestorableGeometry<QMainWindow>
{
    Q_OBJECT;

public:

    static void findManualFileAndShow(const QString &strKeyword = QString());

    /** @name Remove default ctor, and copying.
     * @{ */
       UIHelpBrowserDialog() = delete;
       UIHelpBrowserDialog(const UIHelpBrowserDialog &other) = delete;
       void operator=(const UIHelpBrowserDialog &other) = delete;
    /** @} */

protected:

    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

    /** @name Prepare/cleanup cascade.
     * @{ */
       virtual void prepareCentralWidget();
       virtual void loadSettings();
       virtual void saveDialogGeometry();
    /** @} */

    /** Returns whether the window should be maximized when geometry being restored. */
    virtual bool shouldBeMaximized() const RT_OVERRIDE;

private slots:

    void sltStatusBarMessage(const QString& strLink, int iTimeOut);
    void sltStatusBarVisibilityChange(bool fVisible);
    void sltZoomPercentageChanged(int iPercentage);
    void sltRetranslateUI();

private:

    UIHelpBrowserDialog(QWidget *pParent, QWidget *pCenterWidget, const QString &strHelpFilePath);
    static void showUserManual(const QString &strHelpFilePath, const QString &strKeyword);

    /** A passthru function for QHelpIndexWidget::showHelpForKeyword. */
    void showHelpForKeyword(const QString &strKeyword);

    QString m_strHelpFilePath;
    UIHelpBrowserWidget *m_pWidget;
    QWidget *m_pCenterWidget;
    int m_iGeometrySaveTimerId;
    QLabel *m_pZoomLabel;
    static QPointer<UIHelpBrowserDialog> m_pInstance;
};


#endif /* !FEQT_INCLUDED_SRC_helpbrowser_UIHelpBrowserDialog_h */
