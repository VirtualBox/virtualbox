/* $Id: QIToolBar.cpp 110386 2025-07-23 13:20:48Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - QIToolBar class implementation.
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

/* Qt includes: */
#include <QLayout>
#include <QMainWindow>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

/* GUI includes: */
#include "QIToolBar.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils.h"
#endif


QIToolBar::QIToolBar(QWidget *pParent /* = 0 */)
    : QToolBar(pParent)
    , m_pMainWindow(qobject_cast<QMainWindow*>(pParent))
    , m_fEmulateUnifiedToolbar(false)
    , m_iOverallContentsWidth(0)
    , m_iBrandingWidth(0)
{
    prepare();
}

void QIToolBar::setUseTextLabels(bool fEnable)
{
    /* Determine tool-button style on the basis of passed flag: */
    Qt::ToolButtonStyle tbs = fEnable ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonIconOnly;

    /* Depending on parent, assign this style: */
    if (m_pMainWindow)
        m_pMainWindow->setToolButtonStyle(tbs);
    else
        setToolButtonStyle(tbs);
}

bool QIToolBar::useTextLabels() const
{
    /* Depending on parent, return the style: */
    if (m_pMainWindow)
        return m_pMainWindow->toolButtonStyle() == Qt::ToolButtonTextUnderIcon;
    else
        return toolButtonStyle() == Qt::ToolButtonTextUnderIcon;
}

#ifdef VBOX_WS_MAC
void QIToolBar::enableMacToolbar()
{
    /* Depending on parent, enable unified title/tool-bar: */
    if (m_pMainWindow)
        m_pMainWindow->setUnifiedTitleAndToolBarOnMac(true);
}

void QIToolBar::setShowToolBarButton(bool fShow)
{
    ::darwinSetShowsToolbarButton(this, fShow);
}
#endif /* VBOX_WS_MAC */

void QIToolBar::emulateUnifiedToolbar()
{
    /* Remember request, to be used in paintEvent: */
    m_fEmulateUnifiedToolbar = true;
}

void QIToolBar::enableBranding(const QIcon &icnBranding,
                               const QString &strBranding,
                               const QColor &clrBranding,
                               int iBrandingWidth)
{
    m_icnBranding = icnBranding;
    m_strBranding = strBranding;
    m_clrBranding = clrBranding;
    m_iBrandingWidth = iBrandingWidth;
    update();
}

bool QIToolBar::event(QEvent *pEvent)
{
    /* Sanity check: */
    if (!pEvent)
        return QToolBar::event(pEvent);

    /* Handle required event types: */
    switch (pEvent->type())
    {
        case QEvent::LayoutRequest:
        {
            /* Recalculate overall contents width on layout
             * request if we have branding stuff: */
            if (!m_icnBranding.isNull())
                recalculateOverallContentsWidth();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QToolBar::event(pEvent);
}

void QIToolBar::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QToolBar::resizeEvent(pEvent);

    /* Notify listeners about new size: */
    emit sigResized(pEvent->size());
}

void QIToolBar::paintEvent(QPaintEvent *pEvent)
{
    /* Call to base-class: */
    QToolBar::paintEvent(pEvent);

    /* If we have request to emulate unified tool-bar: */
    if (m_fEmulateUnifiedToolbar)
    {
        /* Limit painting with incoming rectangle: */
        QPainter painter(this);
        painter.setClipRect(pEvent->rect());

        /* Acquire full rectangle: */
        const QRect rectangle = rect();

        /* Do we have branding stuff and a place for it? */
        if (   !m_icnBranding.isNull()
            && width() >= m_iOverallContentsWidth + m_iBrandingWidth)
        {
            /* A bit of common stuff: */
            QFont fnt = font();
            int iTextWidth = 0;
            int iTextHeight = 0;

            /* Configure font to fit width (m_iBrandingWidth - 2 * 4): */
            if (useTextLabels())
            {
                for (int i = 0; i <= 10; ++i) // no more than 10 tries ..
                {
                    if (fnt.pixelSize() == -1)
                        fnt.setPointSize(fnt.pointSize() - i);
                    else
                        fnt.setPixelSize(fnt.pixelSize() - i);
                    iTextWidth = QFontMetrics(fnt).size(0, m_strBranding).width();
                    if (iTextWidth <= m_iBrandingWidth - 2 * 4)
                        break;
                }
                iTextHeight = QFontMetrics(fnt).height();
            }

            /* Draw pixmap: */
            const int iIconSize = qMin(rectangle.height(), 32 /* default */);
            const int iIconMarginH = (m_iBrandingWidth - iIconSize) / 2;
            const int iIconMarginV = (rectangle.height() - iIconSize - iTextHeight) / 2;
            const int iIconX = rectangle.width() - iIconSize - iIconMarginH;
            const int iIconY = iIconMarginV;
            painter.drawPixmap(iIconX, iIconY, m_icnBranding.pixmap(QSize(iIconSize, iIconSize)));

            /* Draw text path: */
            if (useTextLabels())
            {
                const int iTextMargingH = (m_iBrandingWidth - iTextWidth) / 2;
                const int iTextX = rectangle.width() - iTextWidth - iTextMargingH;
                const int iTextY = iIconY + iIconSize + iTextHeight;
                QPainterPath textPath;
                textPath.addText(0, 0, fnt, m_strBranding);
                textPath.translate(iTextX, iTextY);
                painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
                painter.setPen(QPen(m_clrBranding.darker(80), 2, Qt::SolidLine, Qt::RoundCap));
                painter.drawPath(QPainterPathStroker().createStroke(textPath));
                painter.setBrush(Qt::black);
                painter.setPen(Qt::NoPen);
                painter.drawPath(textPath);
            }
        }
    }
}

void QIToolBar::prepare()
{
    /* Configure tool-bar: */
    setFloatable(false);
    setMovable(false);

#ifdef VBOX_WS_MAC
    setStyleSheet("QToolBar { border: 0px none black; }");
#endif

    /* Configure tool-bar' layout: */
    if (layout())
        layout()->setContentsMargins(0, 0, 0, 0);

    /* Configure tool-bar' context-menu policy: */
    setContextMenuPolicy(Qt::PreventContextMenu);
}

void QIToolBar::recalculateOverallContentsWidth()
{
    /* Reset contents width: */
    m_iOverallContentsWidth = 0;

    /* Caclulate new value: */
    if (!layout())
        return;
    int iResult = 0;
    const int iSpacing = layout()->spacing();
    foreach (QAction *pAction, actions())
    {
        if (!pAction || !pAction->isVisible())
            continue;
        QWidget *pWidget = widgetForAction(pAction);
        if (!pWidget)
            continue;
        /* Add each widget width and spacing: */
        const int iWidth = pWidget->width() + iSpacing;
        iResult += iWidth;
    }
    /* Subtract last spacing: */
    iResult -= iSpacing;

    /* Update result: */
    m_iOverallContentsWidth = qMax(m_iOverallContentsWidth, iResult);
}
