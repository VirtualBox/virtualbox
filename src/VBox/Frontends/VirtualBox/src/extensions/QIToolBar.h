/* $Id: QIToolBar.h 110384 2025-07-23 13:12:08Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - QIToolBar class declaration.
 */

/*
 * Copyright (C) 2006-2025 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIToolBar_h
#define FEQT_INCLUDED_SRC_extensions_QIToolBar_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QColor>
#include <QIcon>
#include <QToolBar>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QMainWindow;
class QPaintEvent;
class QResizeEvent;
class QWidget;

/** QToolBar extension with few settings presets. */
class SHARED_LIBRARY_STUFF QIToolBar : public QToolBar
{
    Q_OBJECT;

signals:

    /** Notifies listeners about @a newSize. */
    void sigResized(const QSize &newSize);

public:

    /** Constructs tool-bar passing @a pParent to the base-class. */
    QIToolBar(QWidget *pParent = 0);

    /** Defines whether tool-bar should use text-labels. */
    void setUseTextLabels(bool fEnable);
    /** Returns whether tool-bar should use text-labels. */
    bool useTextLabels() const;

#ifdef VBOX_WS_MAC
    /** Mac OS X: Defines whether native tool-bar should be enabled. */
    void enableMacToolbar();

    /** Mac OS X: Defines whether native tool-bar button should be shown. */
    void setShowToolBarButton(bool fShow);
#endif /* VBOX_WS_MAC */

    /** Defines whether unified tool-bar should be emulated. */
    void emulateUnifiedToolbar();

    /** Defines branding stuff to be shown.
      * @param  icnBranding     Brings branding icon to be shown.
      * @param  strBranding     Brings branding text to be shown.
      * @param  clrBranding     Brings branding color to be used.
      * @param  iBrandingWidth  Holds the branding stuff width. */
    void enableBranding(const QIcon &icnBranding,
                        const QString &strBranding,
                        const QColor &clrBranding,
                        int iBrandingWidth);

protected:

    /** Handles @a pEvent. */
    virtual bool event(QEvent *pEvent) RT_OVERRIDE;

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private:

    /** Prepares all. */
    void prepare();

    /** Recalculates overall contents width. */
    void recalculateOverallContentsWidth();

    /** Holds the parent main-window isntance. */
    QMainWindow *m_pMainWindow;

    /** Holds whether unified tool-bar should be emulated. */
    bool  m_fEmulateUnifiedToolbar;

    /** Holds overall contents width. */
    int  m_iOverallContentsWidth;

    /** Holds branding icon to be shown. */
    QIcon    m_icnBranding;
    /** Holds branding text to be shown. */
    QString  m_strBranding;
    /** Holds branding color to be used. */
    QColor   m_clrBranding;
    /** Holds the branding stuff width. */
    int      m_iBrandingWidth;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIToolBar_h */
