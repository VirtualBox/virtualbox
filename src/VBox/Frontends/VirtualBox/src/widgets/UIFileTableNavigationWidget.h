/* $Id: UIFileTableNavigationWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFileTableNavigationWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIFileTableNavigationWidget_h
#define FEQT_INCLUDED_SRC_widgets_UIFileTableNavigationWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/* Qt includes: */
#include <QWidget>

/* Forward declarations: */
class QLineEdit;
class QStackedWidget;
class QToolButton;
class UIFileManagerBreadCrumbs;
class UIFileManagerHistoryComboBox;

/** UIFileTableNavigationWidget contains a UIFileManagerBreadCrumbs, QComboBox for history and a QToolButton.
  * basically it is a container for these mentioned widgets. */
class UIFileTableNavigationWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigPathChanged(const QString &strPath);
    void sigHistoryListChanged();

public:

    UIFileTableNavigationWidget(QWidget *pParent = 0);
    void setPath(const QString &strLocation);
    void reset();
    void setPathSeparator(const QChar &separator);
    bool canGoForward() const;
    bool canGoBackward() const;
    void goForwardInHistory();
    void goBackwardInHistory();

protected:

    bool eventFilter(QObject *pObject, QEvent *pEvent) override;

private slots:

    void sltHandleSwitch();
    /* Makes sure that we switch to breadcrumbs widget as soon as the combo box popup is hidden. */
    void sltHandleHidePopup();
    void sltHandlePathChange(const QString &strPath);
    void sltAddressLineEdited();

private:

    enum StackedWidgets
    {
        StackedWidgets_History = 0,
        StackedWidgets_BreadCrumbs,
        StackedWidgets_AddressLine
    };

    void prepare();

    QStackedWidget               *m_pContainer;
    UIFileManagerBreadCrumbs     *m_pBreadCrumbs;
    UIFileManagerHistoryComboBox *m_pHistoryComboBox;
    QLineEdit                    *m_pAddressLineEdit;
    QToolButton                  *m_pSwitchButton;
    QChar                         m_pathSeparator;
    /* With non-native separators. */
    QString                       m_strCurrentPath;
};


#endif /* !FEQT_INCLUDED_SRC_widgets_UIFileTableNavigationWidget_h */
