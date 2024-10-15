/* $Id: QIFlowLayout.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIFlowLayout class declaration.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIFlowLayout_h
#define FEQT_INCLUDED_SRC_extensions_QIFlowLayout_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QLayout>
#include <QStyle>

/* GUI includes: */
#include "UILibraryDefs.h"

/** QLayout extension providing GUI with the possibility to build flow-layout.
  * This kind of horizonal layout can wrap children down to the next line (row)
  * performing calculations on the basis of layout size and children size-hints.
  * It is also takes into account that some of the children can be expandable
  * horizontally allowing them to grow up to all the available width. */
class SHARED_LIBRARY_STUFF QIFlowLayout : public QLayout
{
    Q_OBJECT;

    /** Layout item expand policy. */
    enum ExpandPolicy
    {
        ExpandPolicy_Fixed,
        ExpandPolicy_Dynamic
    };

    /** Layout item data. */
    struct LayoutData
    {
        /** Constructs layout item data on the bases of passed @a pItem.
          * @param  enmPolicy  Brings the layout item expand policy.
          * @param  iWidth     Brings the layout item desired width. */
        LayoutData(QLayoutItem *pItem, ExpandPolicy enmPolicy, int iWidth)
            : item(pItem), policy(enmPolicy), width(iWidth)
        {}

        /** Holds the layout item. */
        QLayoutItem  *item;
        /** Holds the layout item expand policy. */
        ExpandPolicy  policy;
        /** Holds the layout item desired width. */
        int           width;
    };
    /** Layout item data list. */
    typedef QList<LayoutData> LayoutDataList;
    /** Layout item data table. */
    typedef QList<LayoutDataList> LayoutDataTable;

public:

    /** Constructs flow-layout passing @a pParent to the base-class.
      * @param  iMargin    Brings the layout contents margin.
      * @param  iSpacingH  Brings the layout horizontal spacing.
      * @param  iSpacingV  Brings the layout vertical spacing. */
    QIFlowLayout(QWidget *pParent, int iMargin = -1, int iSpacingH = -1, int iSpacingV = -1);

    /** Constructs flow-layout.
      * @param  iMargin    Brings the layout contents margin.
      * @param  iSpacingH  Brings the layout horizontal spacing.
      * @param  iSpacingV  Brings the layout vertical spacing. */
    QIFlowLayout(int iMargin = -1, int iSpacingH = -1, int iSpacingV = -1);

    /** Destructs flow-layout. */
    virtual ~QIFlowLayout() RT_OVERRIDE;

    /** Returns the number of layout items. */
    virtual int count() const RT_OVERRIDE;
    /** Adds @a pItem into layout. */
    virtual void addItem(QLayoutItem *pItem) RT_OVERRIDE;
    /** Returns the layout item at passed @a iIndex. */
    virtual QLayoutItem *itemAt(int iIndex) const RT_OVERRIDE;
    /** Removes the layout item at passed @a iIndex and returns it. */
    virtual QLayoutItem *takeAt(int index) RT_OVERRIDE;

    /** Returns whether this layout can make use of more space than sizeHint().
      * A value of Qt::Vertical or Qt::Horizontal means that it wants to grow in only one dimension,
      * whereas Qt::Vertical | Qt::Horizontal means that it wants to grow in both dimensions. */
    virtual Qt::Orientations expandingDirections() const RT_OVERRIDE;

    /** Returns whether this layout's preferred height depends on its width. */
    virtual bool hasHeightForWidth() const RT_OVERRIDE;
    /** Returns the preferred height for this layout item, given the width. */
    virtual int heightForWidth(int) const RT_OVERRIDE;

    /** Returns the minimum layout size. */
    virtual QSize minimumSize() const RT_OVERRIDE;
    /** Returns this item's preferred size. */
    virtual QSize sizeHint() const RT_OVERRIDE;

    /** Defines this item's geometry to @a rect. */
    virtual void setGeometry(const QRect &rect) RT_OVERRIDE;

private:

    /** Recalculates layout on the basis of passed @a rect.
      * Adjusts layout items if @a fDoLayout is true.
      * @returns recalculated layout height. */
    int relayout(const QRect &rect, bool fDoLayout) const;

    /** Returns smart spacing based on parent if present. */
    int smartSpacing(QStyle::PixelMetric pm) const;
    /** Returns horizontal spacing. */
    int horizontalSpacing() const;
    /** Returns vertical spacing. */
    int verticalSpacing() const;

    /** Holds the layout item list. */
    QList<QLayoutItem *> m_items;

    /** Holds the horizontal spacing. */
    int m_iSpacingH;
    /** Holds the vertical spacing. */
    int m_iSpacingV;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIFlowLayout_h */
