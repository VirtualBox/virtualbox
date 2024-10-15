/* $Id: UIPopupStack.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupStack class declaration.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIPopupStack_h
#define FEQT_INCLUDED_SRC_widgets_UIPopupStack_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>
#include <QMap>

/* GUI includes: */
#include "UILibraryDefs.h"
#include "UIPopupCenter.h"

/* Forward declaration: */
class QEvent;
class QObject;
class QScrollArea;
class QShowEvent;
class QSize;
class QString;
class QVBoxLayout;
class UIPopupStackViewport;

/** QWidget extension providing GUI with popup-stack prototype class. */
class SHARED_LIBRARY_STUFF UIPopupStack : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies about popup-stack viewport size change. */
    void sigProposeStackViewportSize(QSize newSize);

    /** Asks to close popup-pane with @a strID and @a iResultCode. */
    void sigPopupPaneDone(QString strID, int iResultCode);

    /** Asks to close popup-stack with @a strID. */
    void sigRemove(QString strID);

public:

    /** Constructs popup-stack with passed @a strID and @a enmOrientation. */
    UIPopupStack(const QString &strID, UIPopupStackOrientation enmOrientation);

    /** Returns whether pane with passed @a strID exists. */
    bool exists(const QString &strID) const;
    /** Creates pane with passed @a strID, @a strMessage, @a strDetails and @a buttonDescriptions. */
    void createPopupPane(const QString &strID,
                         const QString &strMessage, const QString &strDetails,
                         const QMap<int, QString> &buttonDescriptions);
    /** Updates pane with passed @a strID, @a strMessage and @a strDetails. */
    void updatePopupPane(const QString &strID,
                         const QString &strMessage, const QString &strDetails);
    /** Recalls pane with passed @a strID. */
    void recallPopupPane(const QString &strID);
    /** Defines stack @a enmOrientation. */
    void setOrientation(UIPopupStackOrientation enmOrientation);

    /** Defines stack @a pParent*/
    void setParent(QWidget *pParent);
    /** Defines stack @a pParent and @a enmFlags. */
    void setParent(QWidget *pParent, Qt::WindowFlags enmFlags);

protected:

    /** Pre-handles standard Qt @a pEvent for passed @a pObject. */
    virtual bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Adjusts geometry. */
    void sltAdjustGeometry();

    /** Handles removal of the popup-pane with @a strID. */
    void sltPopupPaneRemoved(QString strID);
    /** Handles removal of all the popup-panes. */
    void sltPopupPanesRemoved();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares contents. */
    void prepareContent();

    /** Propagates size. */
    void propagateSize();

    /** Returns @a pParent menu-bar height. */
    static int parentMenuBarHeight(QWidget *pParent);
    /** Returns @a pParent status-bar height. */
    static int parentStatusBarHeight(QWidget *pParent);

    /** Holds the stack ID. */
    QString                 m_strID;
    /** Holds the stack orientation. */
    UIPopupStackOrientation m_enmOrientation;

    /** Holds the main-layout instance. */
    QVBoxLayout          *m_pMainLayout;
    /** Holds the scroll-area instance. */
    QScrollArea          *m_pScrollArea;
    /** Holds the scroll-viewport instance. */
    UIPopupStackViewport *m_pScrollViewport;

    /** Holds the parent menu-bar height. */
    int m_iParentMenuBarHeight;
    /** Holds the parent status-bar height. */
    int m_iParentStatusBarHeight;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIPopupStack_h */

