/* $Id: QIFlowLayout.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIFlowLayout class implementation.
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

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "QIFlowLayout.h"


QIFlowLayout::QIFlowLayout(QWidget *pParent, int iMargin /* = -1 */, int iSpacingH /* = -1 */, int iSpacingV /* = -1 */)
    : QLayout(pParent)
    , m_iSpacingH(iSpacingH)
    , m_iSpacingV(iSpacingV)
{
    setContentsMargins(iMargin, iMargin, iMargin, iMargin);
}

QIFlowLayout::QIFlowLayout(int iMargin, int iSpacingH, int iSpacingV)
    : m_iSpacingH(iSpacingH)
    , m_iSpacingV(iSpacingV)
{
    setContentsMargins(iMargin, iMargin, iMargin, iMargin);
}

QIFlowLayout::~QIFlowLayout()
{
    /* Delete all the children: */
    QLayoutItem *pItem = 0;
    while ((pItem = takeAt(0)))
        delete pItem;
}

int QIFlowLayout::count() const
{
    return m_items.size();
}

void QIFlowLayout::addItem(QLayoutItem *pItem)
{
    m_items.append(pItem);
}

QLayoutItem *QIFlowLayout::itemAt(int iIndex) const
{
    return m_items.value(iIndex);
}

QLayoutItem *QIFlowLayout::takeAt(int iIndex)
{
    return iIndex >= 0 && iIndex < m_items.size() ? m_items.takeAt(iIndex) : 0;
}

Qt::Orientations QIFlowLayout::expandingDirections() const
{
    return Qt::Horizontal;
}

bool QIFlowLayout::hasHeightForWidth() const
{
    return true;
}

int QIFlowLayout::heightForWidth(int iWidth) const
{
    return relayout(QRect(0, 0, iWidth, 0), false);
}

QSize QIFlowLayout::minimumSize() const
{
    /* Walk through all the children: */
    QSize size;
    foreach (QLayoutItem *pItem, m_items)
        size = size.expandedTo(pItem->minimumSize());

    /* Do not forget the margins: */
    int iLeft, iTop, iRight, iBottom;
    getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
    size += QSize(iLeft + iRight, iTop + iBottom);

    /* Return resulting size: */
    return size;
}

QSize QIFlowLayout::sizeHint() const
{
    return minimumSize();
}

void QIFlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    relayout(rect, true);
}

int QIFlowLayout::relayout(const QRect &rect, bool fDoLayout) const
{
    /* Acquire contents margins: */
    int iLeft, iTop, iRight, iBottom;
    getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);

    /* Calculate available contents rectangle: */
    const QRect contentsRect = rect.adjusted(iLeft, iTop, -iRight, -iBottom);

    /* Acquire horizontal/vertical spacings: */
    const int iSpaceX = horizontalSpacing();
    //if (iSpaceX == -1)
    //    iSpaceX = pWidged->style()->layoutSpacing(
    //        QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
    const int iSpaceY = verticalSpacing();
    //if (iSpaceY == -1)
    //    iSpaceY = pWidged->style()->layoutSpacing(
    //        QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);

    /* Split items to rows: */
    int iLastX = contentsRect.x();
    LayoutDataTable rows;
    LayoutDataList row;
    foreach (QLayoutItem *pItem, m_items)
    {
        /* Skip items of zero width: */
        if (pItem->sizeHint().width() == 0)
            continue;

        /* Get item policy and width: */
        const ExpandPolicy enmPolicy = pItem->expandingDirections() & Qt::Horizontal ? ExpandPolicy_Dynamic : ExpandPolicy_Fixed;
        const int iWidth = pItem->sizeHint().width();

        /* Check whether it's possible to insert this item to current row: */
        int iNextX = iLastX + iWidth + iSpaceX;
        if (iNextX - iSpaceX <= contentsRect.right())
        {
            /* Append item to current row: */
            row << LayoutData(pItem, enmPolicy, iWidth);
        }
        else
        {
            /* Flush the row to rows: */
            rows << row;
            row.clear();
            /* Move the caret to the next row: */
            iLastX = contentsRect.x();
            iNextX = iLastX + iWidth + iSpaceX;
            /* Append item to new row: */
            row << LayoutData(pItem, enmPolicy, iWidth);
        }

        /* Remember the last caret position: */
        iLastX = iNextX;
    }
    /* Flush the row to rows: */
    rows << row;
    row.clear();

    /* Iterate through all the rows: */
    for (int i = 0; i < rows.count(); ++i)
    {
        /* Acquire current row: */
        LayoutDataList &row = rows[i];
        /* Width expand delta is equal to total-width minus all spacing widths ... */
        int iExpandingWidth = contentsRect.width() - (row.size() - 1) * iSpaceX;

        /* Iterate through whole the row: */
        int cExpandingItems = 0;
        for (int j = 0; j < row.count(); ++j)
        {
            /* Acquire current record: */
            const LayoutData &record = row.at(j);
            /* Calcualte the amount of expandable items: */
            if (record.policy == ExpandPolicy_Dynamic)
                ++cExpandingItems;
            /* ... minus all item widths ... */
            iExpandingWidth -= record.width;
        }

        /* If there are expandable items: */
        if (cExpandingItems > 0)
        {
            /* ... devided by the amount of ExpandPolicy_Dynamic items: */
            iExpandingWidth /= cExpandingItems;
            /* Expand all the expandable item widths with delta: */
            for (int j = 0; j < row.count(); ++j)
                if (row.at(j).policy == ExpandPolicy_Dynamic)
                    row[j].width += iExpandingWidth;
        }
    }

    /* Iterate through all the items: */
    int iX = contentsRect.x();
    int iY = contentsRect.y();
    for (int i = 0; i < rows.count(); ++i)
    {
        /* Acquire current row: */
        const LayoutDataList &row = rows.at(i);
        int iRowHeight = 0;
        for (int j = 0; j < row.count(); ++j)
        {
            /* Acquire current record: */
            const LayoutData &record = row.at(j);
            /* Acquire the desired width/height: */
            const int iDesiredWidth = record.width;
            const int iDesiredHeight = record.item->sizeHint().height();

            /* Do the layout if requested: */
            if (fDoLayout)
                record.item->setGeometry(QRect(QPoint(iX, iY), QSize(iDesiredWidth, iDesiredHeight)));

            /* Acquire the next item location: */
            iX = iX + iDesiredWidth + iSpaceX;
            /* Remember the maximum row height: */
            iRowHeight = qMax(iRowHeight, iDesiredHeight);
        }
        /* Move the caret to the next row: */
        iX = contentsRect.x();
        iY = iY + iRowHeight + iSpaceY;
    }

    /* Return effective layout height: */
    return iY - iSpaceY - rect.y() + iBottom;
}

int QIFlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject *pParent = this->parent();
    if (!pParent)
    {
        return -1;
    }
    else if (pParent->isWidgetType())
    {
        QWidget *pParentWidget = static_cast<QWidget*>(pParent);
        return pParentWidget->style()->pixelMetric(pm, 0, pParentWidget);
    }
    else
    {
        return static_cast<QLayout*>(pParent)->spacing();
    }
}

int QIFlowLayout::horizontalSpacing() const
{
    return m_iSpacingH >= 0 ? m_iSpacingH : smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int QIFlowLayout::verticalSpacing() const
{
    return m_iSpacingV >= 0 ? m_iSpacingV : smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}
