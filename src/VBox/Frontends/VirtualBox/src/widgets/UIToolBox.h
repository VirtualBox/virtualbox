/* $Id: UIToolBox.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolBox class declaration.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIToolBox_h
#define FEQT_INCLUDED_SRC_widgets_UIToolBox_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QFrame>
#include <QMap>

/* Local includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QVBoxLayout;
class QLabel;
class UIToolBoxPage;

/** A Qframe extension with similar API and functionality like QToolBox. I needed some for
  * flexibility (like a second icon at the right hand side of the title etc.). */
class  SHARED_LIBRARY_STUFF UIToolBox : public QFrame
{

    Q_OBJECT;

signals:


public:

    UIToolBox(QWidget *pParent = 0);
    bool insertPage(int iIndex, QWidget *pWidget, const QString &strTitle, bool fAddEnableCheckBox = false);
    void setPageEnabled(int iIndex, bool fEnabled);
    void setPageTitle(int iIndex, const QString &strTitle);
    void setPageTitleIcon(int iIndex, const QIcon &icon, const QString &strIconToolTip = QString());
    void setCurrentPage(int iIndex);
    virtual QSize minimumSizeHint() const RT_OVERRIDE;

private slots:

    void sltHandleShowPageWidget();

private:

    void prepare();

    QVBoxLayout *m_pMainLayout;
    QMap<int, UIToolBoxPage*> m_pages;
    int m_iCurrentPageIndex;
    int m_iPageCount;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UIToolBox_h */
