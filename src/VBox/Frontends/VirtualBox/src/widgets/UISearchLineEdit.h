/* $Id: UISearchLineEdit.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISearchLineEdit class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UISearchLineEdit_h
#define FEQT_INCLUDED_SRC_widgets_UISearchLineEdit_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/* Qt includes */
#include <QLineEdit>

/* GUI includes: */
#include "UILibraryDefs.h"

/** A QLineEdit extension with an overlay label drawn on the right hand side of it.
  * mostly used for entering a search term and then label show total number of matched items
  * and currently selected, scrolled item. */
class SHARED_LIBRARY_STUFF UISearchLineEdit : public QLineEdit
{

    Q_OBJECT;

public:

    UISearchLineEdit(QWidget *pParent = 0);
    void setMatchCount(int iMatchCount);
    void setScrollToIndex(int iScrollToIndex);
    void reset();

protected:

    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private:

    void colorBackground(bool fWarning);

    /** Stores the total number of matched items. */
    int  m_iMatchCount;
    /** Stores the index of the currently scrolled/made-visible item withing the list of search results.
      * Must be smaller that or equal to m_iMatchCount. */
    int  m_iScrollToIndex;
    /** When true we color line edit background with a more reddish color. */
    bool m_fMark;
    QColor m_unmarkColor;
    QColor m_markColor;
};

#endif /* !FEQT_INCLUDED_SRC_widgets_UISearchLineEdit_h */
