/* $Id: QILineEdit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QILineEdit class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <QClipboard>
#include <QContextMenuEvent>
#include <QLabel>
#include <QMenu>
#include <QStyleOptionFrame>

/* GUI includes: */
#include "QILineEdit.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIIconPool.h"


QILineEdit::QILineEdit(QWidget *pParent /* = 0 */)
    : QLineEdit(pParent)
    , m_fAllowToCopyContentsWhenDisabled(false)
    , m_pCopyAction(0)
    , m_fMarkable(false)
    , m_fMarkForError(false)
    , m_pLabelIcon(0)
    , m_iIconMargin(0)
{
    prepare();
}

QILineEdit::QILineEdit(const QString &strText, QWidget *pParent /* = 0 */)
    : QLineEdit(strText, pParent)
    , m_fAllowToCopyContentsWhenDisabled(false)
    , m_pCopyAction(0)
    , m_fMarkable(false)
    , m_fMarkForError(false)
    , m_pLabelIcon(0)
    , m_iIconMargin(0)
{
    prepare();
}

void QILineEdit::setAllowToCopyContentsWhenDisabled(bool fAllow)
{
    m_fAllowToCopyContentsWhenDisabled = fAllow;
}

void QILineEdit::setMinimumWidthByText(const QString &strText)
{
    setMinimumWidth(fitTextWidth(strText).width());
}

void QILineEdit::setFixedWidthByText(const QString &strText)
{
    setFixedWidth(fitTextWidth(strText).width());
}

void QILineEdit::setMarkable(bool fMarkable)
{
    /* Sanity check: */
    if (m_fMarkable == fMarkable)
        return;

    /* Save new value, show/hide label accordingly: */
    m_fMarkable = fMarkable;
    if (m_pLabelIcon)
        m_pLabelIcon->setVisible(fMarkable);

    /* Update label position on visibility changes: */
    moveIconLabel();
}

void QILineEdit::mark(bool fError, const QString &strErrorMessage, const QString &strNoErrorMessage)
{
    /* Sanity check: */
    if (   !m_pLabelIcon
        || !m_fMarkable)
        return;

    /* Assign corresponding icon: */
    const QIcon icon = fError ? UIIconPool::iconSet(":/status_error_16px.png") : UIIconPool::iconSet(":/status_check_16px.png");
    const int iIconMetric = qMin((int)(QApplication::style()->pixelMetric(QStyle::PM_SmallIconSize) * .625), height());
    const qreal fDevicePixelRatio = gpDesktop->devicePixelRatio(m_pLabelIcon);
    const QString strToolTip = fError ? strErrorMessage : strNoErrorMessage;
    const QPixmap iconPixmap = icon.pixmap(QSize(iIconMetric, iIconMetric), fDevicePixelRatio);
    m_pLabelIcon->setPixmap(iconPixmap);
    m_pLabelIcon->resize(m_pLabelIcon->minimumSizeHint());
    m_pLabelIcon->setToolTip(strToolTip);
    m_iIconMargin = (height() - m_pLabelIcon->height()) / 2;

    /* Update label position on icon changes: */
    moveIconLabel();
}

bool QILineEdit::event(QEvent *pEvent)
{
    /* Depending on event type: */
    switch (pEvent->type())
    {
        case QEvent::ContextMenu:
        {
            /* For disabled widget if requested: */
            if (!isEnabled() && m_fAllowToCopyContentsWhenDisabled)
            {
                /* Create a context menu for the copy to clipboard action: */
                QContextMenuEvent *pContextMenuEvent = static_cast<QContextMenuEvent*>(pEvent);
                QMenu menu;
                m_pCopyAction->setText(tr("&Copy"));
                menu.addAction(m_pCopyAction);
                menu.exec(pContextMenuEvent->globalPos());
                pEvent->accept();
            }
            break;
        }
        case QEvent::Move:
        case QEvent::Resize:
        {
            /* Update label position on each move/resize: */
            moveIconLabel();
            break;
        }
        default:
            break;
    }

    /* Call to base-class: */
    return QLineEdit::event(pEvent);
}

void QILineEdit::copy()
{
    /* Copy the current text to the global and selection clipboards: */
    QApplication::clipboard()->setText(text(), QClipboard::Clipboard);
    QApplication::clipboard()->setText(text(), QClipboard::Selection);
}

void QILineEdit::prepare()
{
    /* Prepare invisible copy action: */
    m_pCopyAction = new QAction(this);
    if (m_pCopyAction)
    {
        m_pCopyAction->setShortcut(QKeySequence(QKeySequence::Copy));
        m_pCopyAction->setShortcutContext(Qt::WidgetShortcut);
        connect(m_pCopyAction, &QAction::triggered, this, &QILineEdit::copy);
        addAction(m_pCopyAction);
    }

    /* Prepare icon label: */
    m_pLabelIcon = new QLabel(this);
    if (m_pLabelIcon)
        m_pLabelIcon->hide();
}

QSize QILineEdit::fitTextWidth(const QString &strText) const
{
    QStyleOptionFrame sof;
    sof.initFrom(this);
    sof.rect = contentsRect();
    sof.lineWidth = hasFrame() ? style()->pixelMetric(QStyle::PM_DefaultFrameWidth) : 0;
    sof.midLineWidth = 0;
    sof.state |= QStyle::State_Sunken;

    /** @todo make it wise.. */
    // WORKAROUND:
    // The margins are based on qlineedit.cpp of Qt.
    // Maybe they where changed at some time in the future.
    QSize sc(fontMetrics().horizontalAdvance(strText) + 2 * 2,
             fontMetrics().xHeight()                  + 2 * 1);
    const QSize sa = style()->sizeFromContents(QStyle::CT_LineEdit, &sof, sc, this);

    return sa;
}

void QILineEdit::moveIconLabel()
{
    /* Sanity check: */
    if (   !m_pLabelIcon
        || !m_fMarkable)
        return;

    /* We do it cause we have manual layout for the label: */
    m_pLabelIcon->move(width() - m_pLabelIcon->width() - m_iIconMargin, m_iIconMargin);
    update();
}
