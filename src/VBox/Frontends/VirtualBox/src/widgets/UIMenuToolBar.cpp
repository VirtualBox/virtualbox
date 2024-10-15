/* $Id: UIMenuToolBar.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMenuToolBar class implementation.
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
#include <QApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QStyle>
#include <QToolButton>

/* GUI includes: */
#include "UIMenuToolBar.h"
#include "QIToolBar.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QIToolBar extension
  * holding single drop-down menu of actions. */
class UIMenuToolBarPrivate : public QIToolBar
{
    Q_OBJECT;

public:

    /** Constructs toolbar. */
    UIMenuToolBarPrivate(QWidget *pParent = 0);

    /** Rebuilds toolbar shape. */
    void rebuildShape();

    /** Defines toolbar alignment @a enmType. */
    void setAlignmentType(UIMenuToolBar::AlignmentType enmType);

    /** Defines toolbar menu action. */
    void setMenuAction(QAction *pAction);

protected:

    /** Handles show @a pEvent. */
    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;

    /** Handles polish @a pEvent. */
    virtual void polishEvent(QShowEvent *pEvent);

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

private:

    /** Holds whether this widget was polished. */
    bool m_fPolished;

    /** Holds the left margin instance. */
    QWidget *m_pMarginLeft;
    /** Holds the right margin instance. */
    QWidget *m_pMarginRight;

    /** Holds the menu toolbar alignment type. */
    UIMenuToolBar::AlignmentType m_enmAlignmentType;

    /** Holds the shape. */
    QPainterPath m_shape;
};


/*********************************************************************************************************************************
*   Class UIMenuToolBarPrivate implementation.                                                                                   *
*********************************************************************************************************************************/

UIMenuToolBarPrivate::UIMenuToolBarPrivate(QWidget *pParent /* = 0 */)
    : QIToolBar(pParent)
    , m_fPolished(false)
    , m_pMarginLeft(0)
    , m_pMarginRight(0)
    , m_enmAlignmentType(UIMenuToolBar::AlignmentType_TopLeft)
{
    /* Rebuild shape: */
    rebuildShape();
}

void UIMenuToolBarPrivate::rebuildShape()
{
    /* Get the metric: */
    const int iIconMetric = QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize);
    const int iRounding = qMax(iIconMetric / 4, 4);

    /* Configure margins: */
    if (m_pMarginLeft && m_pMarginRight)
    {
        const int iStandardMargin = iRounding;
        int iLeftMargin = iStandardMargin;
        int iRightMargin = iStandardMargin;
        if (   m_enmAlignmentType == UIMenuToolBar::AlignmentType_TopLeft
            || m_enmAlignmentType == UIMenuToolBar::AlignmentType_BottomLeft)
            iRightMargin += iRounding;
        if (   m_enmAlignmentType == UIMenuToolBar::AlignmentType_TopRight
            || m_enmAlignmentType == UIMenuToolBar::AlignmentType_BottomRight)
            iLeftMargin += iRounding;
        m_pMarginLeft->setMinimumWidth(iLeftMargin);
        m_pMarginRight->setMinimumWidth(iRightMargin);
    }

    /* Rebuild shape: */
    QPainterPath shape;
    switch (m_enmAlignmentType)
    {
        case UIMenuToolBar::AlignmentType_TopLeft:
            shape.moveTo(width(), height());
            shape.lineTo(shape.currentPosition().x(), iRounding * 2);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(iRounding * 4, iRounding * 4))
                        .translated(-iRounding * 4, -iRounding * 2), 0, 90);
            shape.lineTo(0, shape.currentPosition().y());
            shape.lineTo(shape.currentPosition().x(), height());
            shape.closeSubpath();
            break;
        case UIMenuToolBar::AlignmentType_TopRight:
            shape.moveTo(0, height());
            shape.lineTo(shape.currentPosition().x(), iRounding * 2);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(iRounding * 4, iRounding * 4))
                        .translated(0, -iRounding * 2), 180, -90);
            shape.lineTo(width(), shape.currentPosition().y());
            shape.lineTo(shape.currentPosition().x(), height());
            shape.closeSubpath();
            break;
        case UIMenuToolBar::AlignmentType_BottomLeft:
            shape.moveTo(width(), 0);
            shape.lineTo(shape.currentPosition().x(), height() - iRounding * 2);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(iRounding * 4, iRounding * 4))
                        .translated(-iRounding * 4, -iRounding * 2), 0, -90);
            shape.lineTo(0, shape.currentPosition().y());
            shape.lineTo(shape.currentPosition().x(), 0);
            shape.closeSubpath();
            break;
        case UIMenuToolBar::AlignmentType_BottomRight:
            shape.moveTo(0, 0);
            shape.lineTo(shape.currentPosition().x(), height() - iRounding * 2);
            shape.arcTo(QRectF(shape.currentPosition(), QSizeF(iRounding * 4, iRounding * 4))
                        .translated(0, -iRounding * 2), 180, 90);
            shape.lineTo(width(), shape.currentPosition().y());
            shape.lineTo(shape.currentPosition().x(), 0);
            shape.closeSubpath();
            break;
    }
    m_shape = shape;
}

void UIMenuToolBarPrivate::setAlignmentType(UIMenuToolBar::AlignmentType enmType)
{
    /* Set alignment type: */
    m_enmAlignmentType = enmType;

    /* Rebuild shape: */
    rebuildShape();
}

void UIMenuToolBarPrivate::setMenuAction(QAction *pAction)
{
    /* Clear first: */
    clear();
    delete m_pMarginLeft;
    m_pMarginLeft = 0;
    delete m_pMarginRight;
    m_pMarginRight = 0;

    /* Create left margin: */
    m_pMarginLeft = widgetForAction(addWidget(new QWidget));

    /* Add action itself: */
    addAction(pAction);

    /* Configure the newly added action's button: */
    QToolButton *pButton = qobject_cast<QToolButton*>(widgetForAction(pAction));
    AssertPtrReturnVoid(pButton);
    {
        /* Configure tool-button: */
        pButton->setAutoRaise(true);
        pButton->setPopupMode(QToolButton::InstantPopup);
    }

    /* Create right margin: */
    m_pMarginRight = widgetForAction(addWidget(new QWidget));

    /* Rebuild shape: */
    rebuildShape();
}

void UIMenuToolBarPrivate::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QIToolBar::showEvent(pEvent);

    /* Make sure we should polish dialog: */
    if (m_fPolished)
        return;

    /* Call to polish-event: */
    polishEvent(pEvent);

    /* Mark dialog as polished: */
    m_fPolished = true;
}

void UIMenuToolBarPrivate::polishEvent(QShowEvent * /* pEvent */)
{
    /* Rebuild shape: */
    rebuildShape();
}

void UIMenuToolBarPrivate::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QIToolBar::resizeEvent(pEvent);

    /* Rebuild shape: */
    rebuildShape();
}

void UIMenuToolBarPrivate::paintEvent(QPaintEvent * /* pEvent */)
{
    /* Prepare painter: */
    QPainter painter(this);

    /* Fill background: */
    if (!m_shape.isEmpty())
    {
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipPath(m_shape);
    }
    QRect backgroundRect = rect();
    QColor backgroundColor = QApplication::palette().color(QPalette::Window);
    QLinearGradient headerGradient(backgroundRect.bottomLeft(), backgroundRect.topLeft());
    headerGradient.setColorAt(0, backgroundColor.darker(120));
    headerGradient.setColorAt(1, backgroundColor.darker(104));
    painter.fillRect(backgroundRect, headerGradient);
}


/*********************************************************************************************************************************
*   Class UIMenuToolBar implementation.                                                                                          *
*********************************************************************************************************************************/

UIMenuToolBar::UIMenuToolBar(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
{
    /* Prepare: */
    prepare();
}

void UIMenuToolBar::prepare()
{
    /* Create layout: */
    new QHBoxLayout(this);
    AssertPtrReturnVoid(layout());
    {
        /* Configure layout: */
        layout()->setContentsMargins(0, 0, 0, 0);

        /* Create menu-toolbar: */
        m_pToolbar = new UIMenuToolBarPrivate;
        AssertPtrReturnVoid(m_pToolbar);
        {
            /* Configure menu-toolbar: */
            m_pToolbar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

            /* Add into layout: */
            layout()->addWidget(m_pToolbar);
        }
    }
}

void UIMenuToolBar::setAlignmentType(AlignmentType enmType)
{
    /* Pass to private object: */
    return m_pToolbar->setAlignmentType(enmType);
}

void UIMenuToolBar::setIconSize(const QSize &size)
{
    /* Pass to private object: */
    return m_pToolbar->setIconSize(size);
}

void UIMenuToolBar::setMenuAction(QAction *pAction)
{
    /* Pass to private object: */
    return m_pToolbar->setMenuAction(pAction);
}

void UIMenuToolBar::setToolButtonStyle(Qt::ToolButtonStyle enmStyle)
{
    /* Pass to private object: */
    return m_pToolbar->setToolButtonStyle(enmStyle);
}

QWidget *UIMenuToolBar::widgetForAction(QAction *pAction) const
{
    /* Pass to private object: */
    return m_pToolbar->widgetForAction(pAction);
}

#include "UIMenuToolBar.moc"

