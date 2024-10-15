/* $Id: UIGraphicsTextPane.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGraphicsTextPane class implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <QAccessibleObject>
#include <QPainter>
#include <QTextLayout>
#include <QApplication>
#include <QFontMetrics>
#include <QGraphicsSceneHoverEvent>
#include <QRegularExpression>

/* GUI includes: */
#include "UIGraphicsTextPane.h"
#include "UIRichTextString.h"

/* Other VBox includes: */
#include <iprt/assert.h>

/** QAccessibleObject extension used as an accessibility interface for UITextTableLine. */
class UIAccessibilityInterfaceForUITextTableLine : public QAccessibleObject
{
public:

    /** Returns an accessibility interface for passed @a strClassname and @a pObject. */
    static QAccessibleInterface *pFactory(const QString &strClassname, QObject *pObject)
    {
        /* Creating Details-view accessibility interface: */
        if (pObject && strClassname == QLatin1String("UITextTableLine"))
            return new UIAccessibilityInterfaceForUITextTableLine(pObject);

        /* Null by default: */
        return 0;
    }

    /** Constructs an accessibility interface passing @a pObject to the base-class. */
    UIAccessibilityInterfaceForUITextTableLine(QObject *pObject)
        : QAccessibleObject(pObject)
    {}

    /** Returns the parent. */
    virtual QAccessibleInterface *parent() const RT_OVERRIDE
    {
        /* Make sure line still alive: */
        AssertPtrReturn(line(), 0);

        /* Return the parent: */
        return QAccessible::queryAccessibleInterface(line()->parent());
    }

    /** Returns the number of children. */
    virtual int childCount() const RT_OVERRIDE { return 0; }
    /** Returns the child with the passed @a iIndex. */
    virtual QAccessibleInterface *child(int /* iIndex */) const RT_OVERRIDE { return 0; }
    /** Returns the index of the passed @a pChild. */
    virtual int indexOfChild(const QAccessibleInterface * /* pChild */) const RT_OVERRIDE { return -1; }

    /** Returns the rect. */
    virtual QRect rect() const RT_OVERRIDE
    {
        /* Make sure parent still alive: */
        AssertPtrReturn(parent(), QRect());

        /* Return the parent's rect for now: */
        /// @todo Return sub-rect.
        return parent()->rect();
    }

    /** Returns a text for the passed @a enmTextRole. */
    virtual QString text(QAccessible::Text enmTextRole) const RT_OVERRIDE
    {
        /* Make sure line still alive: */
        AssertPtrReturn(line(), QString());

        /* Return the description: */
        if (enmTextRole == QAccessible::Description)
            return QString("%1: %2").arg(line()->string1(), line()->string2());

        /* Null-string by default: */
        return QString();
    }

    /** Returns the role. */
    virtual QAccessible::Role role() const RT_OVERRIDE { return QAccessible::StaticText; }
    /** Returns the state. */
    virtual QAccessible::State state() const RT_OVERRIDE { return QAccessible::State(); }

private:

    /** Returns corresponding text-table line. */
    UITextTableLine *line() const { return qobject_cast<UITextTableLine*>(object()); }
};

UIGraphicsTextPane::UIGraphicsTextPane(QIGraphicsWidget *pParent, QPaintDevice *pPaintDevice)
    : QIGraphicsWidget(pParent)
    , m_pPaintDevice(pPaintDevice)
    , m_iMargin(0)
    , m_iSpacing(10)
    , m_iMinimumTextColumnWidth(100)
    , m_fMinimumSizeHintInvalidated(true)
    , m_iMinimumTextWidth(0)
    , m_iMinimumTextHeight(0)
    , m_fAnchorCanBeHovered(true)
{
    /* Install text-table line accessibility interface factory: */
    QAccessible::installFactory(UIAccessibilityInterfaceForUITextTableLine::pFactory);

    /* We do support hover-events: */
    setAcceptHoverEvents(true);
}

UIGraphicsTextPane::~UIGraphicsTextPane()
{
    /* Clear text-layouts: */
    while (!m_leftList.isEmpty()) delete m_leftList.takeLast();
    while (!m_rightList.isEmpty()) delete m_rightList.takeLast();
}

void UIGraphicsTextPane::setText(const UITextTable &text)
{
    /* Clear text: */
    m_text.clear();

    /* For each the line of the passed table: */
    foreach (const UITextTableLine &line, text)
    {
        /* Lines: */
        QString strLeftLine = line.string1();
        QString strRightLine = line.string2();

        /* If 2nd line is NOT empty: */
        if (!strRightLine.isEmpty())
        {
            /* Take both lines 'as is': */
            m_text << UITextTableLine(strLeftLine, strRightLine, parentWidget());
        }
        /* If 2nd line is empty: */
        else
        {
            /* Parse the 1st one to sub-lines: */
            QStringList subLines = strLeftLine.split(QRegularExpression("\\n"));
            foreach (const QString &strSubLine, subLines)
                m_text << UITextTableLine(strSubLine, QString(), parentWidget());
        }
    }

    /* Update text-layout: */
    updateTextLayout(true);

    /* Update minimum size-hint: */
    updateGeometry();
}

void UIGraphicsTextPane::setAnchorRoleRestricted(const QString &strAnchorRole, bool fRestricted)
{
    /* Make sure something changed: */
    if (   (fRestricted && m_restrictedAnchorRoles.contains(strAnchorRole))
        || (!fRestricted && !m_restrictedAnchorRoles.contains(strAnchorRole)))
        return;

    /* Apply new value: */
    if (fRestricted)
        m_restrictedAnchorRoles << strAnchorRole;
    else
        m_restrictedAnchorRoles.remove(strAnchorRole);

    /* Reset hovered anchor: */
    m_strHoveredAnchor.clear();
    updateHoverStuff();
}

void UIGraphicsTextPane::updateTextLayout(bool fFull /* = false */)
{
    /* Prepare variables: */
    QFontMetrics fm(font(), m_pPaintDevice);
    int iMaximumTextWidth = (int)size().width() - 2 * m_iMargin - m_iSpacing;

    /* Search for the maximum column widths: */
    int iMaximumLeftColumnWidth = 0;
    int iMaximumRightColumnWidth = 0;
    bool fSingleColumnText = true;
    foreach (const UITextTableLine &line, m_text)
    {
        const bool fRightColumnPresent = !line.string2().isEmpty();
        if (fRightColumnPresent)
            fSingleColumnText = false;
        const QString strLeftLine = fRightColumnPresent ? line.string1() + ":" : line.string1();
        const QString strRightLine = line.string2();
        iMaximumLeftColumnWidth = qMax(iMaximumLeftColumnWidth, fm.horizontalAdvance(strLeftLine));
        iMaximumRightColumnWidth = qMax(iMaximumRightColumnWidth, fm.horizontalAdvance(strRightLine));
    }
    iMaximumLeftColumnWidth += 1;
    iMaximumRightColumnWidth += 1;

    /* Calculate text attributes: */
    int iLeftColumnWidth = 0;
    int iRightColumnWidth = 0;
    /* Left column only: */
    if (fSingleColumnText)
    {
        /* Full update? */
        if (fFull)
        {
            /* Minimum width for left column: */
            int iMinimumLeftColumnWidth = qMin(m_iMinimumTextColumnWidth, iMaximumLeftColumnWidth);
            /* Minimum width for whole text: */
            m_iMinimumTextWidth = iMinimumLeftColumnWidth;
        }

        /* Current width for left column: */
        iLeftColumnWidth = qMax(m_iMinimumTextColumnWidth, iMaximumTextWidth);
    }
    /* Two columns: */
    else
    {
        /* Full update? */
        if (fFull)
        {
            /* Minimum width for left column: */
            int iMinimumLeftColumnWidth = iMaximumLeftColumnWidth;
            /* Minimum width for right column: */
            int iMinimumRightColumnWidth = qMin(m_iMinimumTextColumnWidth, iMaximumRightColumnWidth);
            /* Minimum width for whole text: */
            m_iMinimumTextWidth = iMinimumLeftColumnWidth + m_iSpacing + iMinimumRightColumnWidth;
        }

        /* Current width for left column: */
        iLeftColumnWidth = iMaximumLeftColumnWidth;
        /* Current width for right column: */
        iRightColumnWidth = iMaximumTextWidth - iLeftColumnWidth;
    }

    /* Clear old text-layouts: */
    while (!m_leftList.isEmpty()) delete m_leftList.takeLast();
    while (!m_rightList.isEmpty()) delete m_rightList.takeLast();

    /* Prepare new text-layouts: */
    int iTextX = m_iMargin;
    int iTextY = m_iMargin;
    /* Populate text-layouts: */
    m_iMinimumTextHeight = 0;
    foreach (const UITextTableLine &line, m_text)
    {
        /* Left layout: */
        int iLeftColumnHeight = 0;
        if (!line.string1().isEmpty())
        {
            bool fRightColumnPresent = !line.string2().isEmpty();
            m_leftList << buildTextLayout(font(), m_pPaintDevice,
                                          fRightColumnPresent ? line.string1() + ":" : line.string1(),
                                          iLeftColumnWidth, iLeftColumnHeight,
                                          m_strHoveredAnchor);
            m_leftList.last()->setPosition(QPointF(iTextX, iTextY));
        }

        /* Right layout: */
        int iRightColumnHeight = 0;
        if (!line.string2().isEmpty())
        {
            m_rightList << buildTextLayout(font(), m_pPaintDevice,
                                           line.string2(),
                                           iRightColumnWidth, iRightColumnHeight,
                                           m_strHoveredAnchor);
            m_rightList.last()->setPosition(QPointF(iTextX + iLeftColumnWidth + m_iSpacing, iTextY));
        }

        /* Maximum colum height? */
        int iMaximumColumnHeight = qMax(iLeftColumnHeight, iRightColumnHeight);

        /* Indent Y: */
        iTextY += iMaximumColumnHeight;
        /* Append summary text height: */
        m_iMinimumTextHeight += iMaximumColumnHeight;
    }
}

void UIGraphicsTextPane::updateGeometry()
{
    /* Discard cached minimum size-hint: */
    m_fMinimumSizeHintInvalidated = true;

    /* Call to base-class to notify layout if any: */
    QIGraphicsWidget::updateGeometry();

    /* And notify listeners which are not layouts: */
    emit sigGeometryChanged();
}

QSizeF UIGraphicsTextPane::sizeHint(Qt::SizeHint which, const QSizeF &constraint /* = QSizeF() */) const
{
    /* For minimum size-hint: */
    if (which == Qt::MinimumSize)
    {
        /* If minimum size-hint invalidated: */
        if (m_fMinimumSizeHintInvalidated)
        {
            /* Recache minimum size-hint: */
            m_minimumSizeHint = QSizeF(2 * m_iMargin + m_iMinimumTextWidth,
                                       2 * m_iMargin + m_iMinimumTextHeight);
            m_fMinimumSizeHintInvalidated = false;
        }
        /* Return cached minimum size-hint: */
        return m_minimumSizeHint;
    }
    /* Call to base-class for other size-hints: */
    return QIGraphicsWidget::sizeHint(which, constraint);
}

void UIGraphicsTextPane::resizeEvent(QGraphicsSceneResizeEvent*)
{
    /* Update text-layout: */
    updateTextLayout();

    /* Update minimum size-hint: */
    updateGeometry();
}

void UIGraphicsTextPane::hoverLeaveEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Redirect to common handler: */
    handleHoverEvent(pEvent);
}

void UIGraphicsTextPane::hoverMoveEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Redirect to common handler: */
    handleHoverEvent(pEvent);
}

void UIGraphicsTextPane::handleHoverEvent(QGraphicsSceneHoverEvent *pEvent)
{
    /* Ignore if anchor can't be hovered: */
    if (!m_fAnchorCanBeHovered)
        return;

    /* Prepare variables: */
    QPoint mousePosition = pEvent->pos().toPoint();
    QString strHoveredAnchor;
    QString strHoveredAnchorRole;

    /* Search for hovered-anchor in the left list: */
    strHoveredAnchor = searchForHoveredAnchor(m_pPaintDevice, m_leftList, mousePosition);
    strHoveredAnchorRole = strHoveredAnchor.section(',', 0, 0);
    if (!strHoveredAnchor.isNull() && !m_restrictedAnchorRoles.contains(strHoveredAnchorRole))
    {
        m_strHoveredAnchor = strHoveredAnchor;
        return updateHoverStuff();
    }

    /* Then search for hovered-anchor in the right one: */
    strHoveredAnchor = searchForHoveredAnchor(m_pPaintDevice, m_rightList, mousePosition);
    strHoveredAnchorRole = strHoveredAnchor.section(',', 0, 0);
    if (!strHoveredAnchor.isNull() && !m_restrictedAnchorRoles.contains(strHoveredAnchorRole))
    {
        m_strHoveredAnchor = strHoveredAnchor;
        return updateHoverStuff();
    }

    /* Finally clear it for good: */
    if (!m_strHoveredAnchor.isNull())
    {
        m_strHoveredAnchor.clear();
        return updateHoverStuff();
    }
}

void UIGraphicsTextPane::updateHoverStuff()
{
    /* Update mouse-cursor: */
    if (m_strHoveredAnchor.isNull())
        unsetCursor();
    else
        setCursor(Qt::PointingHandCursor);

    /* Update text-layout: */
    updateTextLayout();

    /* Update tool-tip: */
    const QString strType = m_strHoveredAnchor.section(',', 0, 0);
    if (strType == "#attach" || strType == "#mount")
        setToolTip(m_strHoveredAnchor.section(',', -1));
    else
        setToolTip(QString());

    /* Update text-pane: */
    update();
}

void UIGraphicsTextPane::mousePressEvent(QGraphicsSceneMouseEvent*)
{
    /* Make sure some anchor hovered: */
    if (m_strHoveredAnchor.isNull())
        return;

    /* Restrict anchor hovering: */
    m_fAnchorCanBeHovered = false;

    /* Cache clicked anchor: */
    QString strClickedAnchor = m_strHoveredAnchor;

    /* Clear hovered anchor: */
    m_strHoveredAnchor.clear();
    updateHoverStuff();

    /* Notify listeners about anchor clicked: */
    emit sigAnchorClicked(strClickedAnchor);

    /* Allow anchor hovering again: */
    m_fAnchorCanBeHovered = true;
}

void UIGraphicsTextPane::paint(QPainter *pPainter, const QStyleOptionGraphicsItem*, QWidget*)
{
    /* Draw all the text-layouts: */
    foreach (QTextLayout *pTextLayout, m_leftList)
        pTextLayout->draw(pPainter, QPoint(0, 0));
    foreach (QTextLayout *pTextLayout, m_rightList)
        pTextLayout->draw(pPainter, QPoint(0, 0));
}

/* static  */
QTextLayout* UIGraphicsTextPane::buildTextLayout(const QFont &font, QPaintDevice *pPaintDevice,
                                                 const QString &strText, int iWidth, int &iHeight,
                                                 const QString &strHoveredAnchor)
{
    /* Prepare variables: */
    QFontMetrics fm(font, pPaintDevice);
    int iLeading = fm.leading();

    /* Parse incoming string with UIRichTextString capabilities: */
    //printf("Text: {%s}\n", strText.toUtf8().constData());
    UIRichTextString ms(strText);
    ms.setHoveredAnchor(strHoveredAnchor);

    /* Create layout; */
    QTextLayout *pTextLayout = new QTextLayout(ms.toString(), font, pPaintDevice);
    pTextLayout->setFormats(ms.formatRanges());

    /* Configure layout: */
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    pTextLayout->setTextOption(textOption);

    /* Build layout: */
    pTextLayout->beginLayout();
    while (1)
    {
        QTextLine line = pTextLayout->createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(iWidth);
        iHeight += iLeading;
        line.setPosition(QPointF(0, iHeight));
        iHeight += (int)line.height();
    }
    pTextLayout->endLayout();

    /* Return layout: */
    return pTextLayout;
}

/* static */
QString UIGraphicsTextPane::searchForHoveredAnchor(QPaintDevice *pPaintDevice, const QList<QTextLayout*> &list, const QPoint &mousePosition)
{
    /* Analyze passed text-layouts: */
    foreach (QTextLayout *pTextLayout, list)
    {
        /* Prepare variables: */
        QFontMetrics fm(pTextLayout->font(), pPaintDevice);

        /* Text-layout attributes: */
        const QPoint layoutPosition = pTextLayout->position().toPoint();
        const QString strLayoutText = pTextLayout->text();

        /* Enumerate format ranges: */
        foreach (const QTextLayout::FormatRange &range, pTextLayout->formats())
        {
            /* Skip unrelated formats: */
            if (!range.format.isAnchor())
                continue;

            /* Parse 'anchor' format: */
            const int iStart = range.start;
            const int iLength = range.length;
            QRegion formatRegion;
            for (int iTextPosition = iStart; iTextPosition < iStart + iLength; ++iTextPosition)
            {
                QTextLine layoutLine = pTextLayout->lineForTextPosition(iTextPosition);
                QPoint linePosition = layoutLine.position().toPoint();
                int iSymbolX = (int)layoutLine.cursorToX(iTextPosition);
                QRect symbolRect = QRect(layoutPosition.x() + linePosition.x() + iSymbolX,
                                         layoutPosition.y() + linePosition.y(),
                                         fm.horizontalAdvance(strLayoutText[iTextPosition]) + 1,
                                         fm.height());
                formatRegion += symbolRect;
            }

            /* Is that something we looking for? */
            if (formatRegion.contains(mousePosition))
                return range.format.anchorHref();
        }
    }

    /* Null string by default: */
    return QString();
}
