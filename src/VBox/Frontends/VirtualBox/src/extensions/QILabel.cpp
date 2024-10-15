/* $Id: QILabel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QILabel class implementation.
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

/*
 * This class is based on the original QLabel implementation.
 */

/* Qt includes: */
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFocusEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionFocusRect>

/* GUI includes: */
#include "QILabel.h"

/* Type definitions: */
#define HOR_PADDING 1


/* static */
const QRegularExpression QILabel::s_regExpCopy = QRegularExpression("<[^>]*>");
const QRegularExpression QILabel::s_regExpElide = QRegularExpression("(<compact\\s+elipsis=\"(start|middle|end)\"?>([^<]*)</compact>)");

QILabel::QILabel(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    : QLabel(pParent, enmFlags)
{
    init();
}

QILabel::QILabel(const QString &strText, QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    : QLabel(pParent, enmFlags)
{
    init();
    setFullText(strText);
}

void QILabel::setFullSizeSelection(bool fEnabled)
{
    /* Remember new value: */
    m_fFullSizeSelection = fEnabled;
    if (m_fFullSizeSelection)
    {
        /* Enable mouse interaction only */
        setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        /* The label should be able to get the focus */
        setFocusPolicy(Qt::StrongFocus);
        /* Change the appearance in the focus state a little bit.
         * Note: Unfortunately QLabel, precisely the text of a QLabel isn't
         * styleable. The trolls have forgotten the simplest case ... So this
         * is done by changing the currently used palette in the In/Out-focus
         * events below. Next broken feature is drawing a simple dotted line
         * around the label. So this is done manually in the paintEvent. Not
         * sure if the stylesheet stuff is ready for production environments. */
        setStyleSheet(QString("QLabel::focus {\
                              background-color: palette(highlight);\
                              }\
                              QLabel {\
                              padding: 0px %1px 0px %1px;\
                              }").arg(HOR_PADDING));
    }
    else
    {
        /* Text should be selectable/copyable */
        setTextInteractionFlags(Qt::TextBrowserInteraction);
        /* No Focus an the label */
        setFocusPolicy(Qt::NoFocus);
        /* No focus style change */
        setStyleSheet("");
    }
}

void QILabel::useSizeHintForWidth(int iWidthHint) const
{
    /* Remember new value: */
    m_iWidthHint = iWidthHint;
    updateSizeHint();
}

QSize QILabel::sizeHint() const
{
    /* Update size-hint if it's invalid: */
    if (!m_fHintValid)
        updateSizeHint();

    /* If there is an updated sizeHint() present - using it: */
    return m_ownSizeHint.isValid() ? m_ownSizeHint : QLabel::sizeHint();
}

QSize QILabel::minimumSizeHint() const
{
    /* Update size-hint if it's invalid: */
    if (!m_fHintValid)
        updateSizeHint();

    /* If there is an updated minimumSizeHint() present - using it. */
    return m_ownSizeHint.isValid() ? m_ownSizeHint : QLabel::minimumSizeHint();
}

void QILabel::clear()
{
    QLabel::clear();
    setFullText("");
}

void QILabel::setText(const QString &strText)
{
    /* Call to wrapper below: */
    setFullText(strText);

    /* If QILabel forced to be fixed vertically */
    if (minimumHeight() == maximumHeight())
    {
        /* Check if new text requires label growing */
        QSize sh(width(), heightForWidth(width()));
        if (sh.height() > minimumHeight())
            setFixedHeight(sh.height());
    }
}

void QILabel::copy()
{
    /* Strip the text of all HTML subsets: */
    QString strText = removeHtmlTags(m_strText);
    /* Copy the current text to the global and selection clipboard. */
    QApplication::clipboard()->setText(strText, QClipboard::Clipboard);
    QApplication::clipboard()->setText(strText, QClipboard::Selection);
}

void QILabel::resizeEvent(QResizeEvent *pEvent)
{
    /* Call to base-class: */
    QLabel::resizeEvent(pEvent);
    /* Recalculate the elipsis of the text after every resize. */
    updateText();
}

void QILabel::mousePressEvent(QMouseEvent *pEvent)
{
    /* Start dragging: */
    if (pEvent->button() == Qt::LeftButton && geometry().contains(pEvent->position().toPoint()) && m_fFullSizeSelection)
        m_fStartDragging = true;
    /* Call to base-class: */
    else
        QLabel::mousePressEvent(pEvent);
}

void QILabel::mouseReleaseEvent(QMouseEvent *pEvent)
{
    /* Reset dragging: */
    m_fStartDragging = false;
    /* Call to base-class: */
    QLabel::mouseReleaseEvent(pEvent);
}

void QILabel::mouseMoveEvent(QMouseEvent *pEvent)
{
    /* If we have an order to start dragging: */
    if (m_fStartDragging)
    {
        /* Reset dragging: */
        m_fStartDragging = false;
        /* Create a drag object out of the given data: */
        QDrag *pDrag = new QDrag(this);
        QMimeData *pMimeData = new QMimeData;
        pMimeData->setText(removeHtmlTags(m_strText));
        pDrag->setMimeData(pMimeData);
        /* Start the dragging finally: */
        pDrag->exec();
    }
    /* Call to base-class: */
    else
        QLabel::mouseMoveEvent(pEvent);
}

void QILabel::contextMenuEvent(QContextMenuEvent *pEvent)
{
    /* If we have an order for full-size selection: */
    if (m_fFullSizeSelection)
    {
        /* Create a context menu for the copy to clipboard action: */
        QMenu menu;
        m_pCopyAction->setText(tr("&Copy"));
        menu.addAction(m_pCopyAction);
        menu.exec(pEvent->globalPos());
    }
    /* Call to base-class: */
    else
        QLabel::contextMenuEvent(pEvent);
}

void QILabel::focusInEvent(QFocusEvent *)
{
    /* If we have an order for full-size selection: */
    if (m_fFullSizeSelection)
    {
        /* Set the text color to the current used highlight text color: */
        QPalette pal = qApp->palette();
        pal.setBrush(QPalette::WindowText, pal.brush(QPalette::HighlightedText));
        setPalette(pal);
    }
}

void QILabel::focusOutEvent(QFocusEvent *pEvent)
{
    /* Reset to the default palette: */
    if (m_fFullSizeSelection && pEvent->reason() != Qt::PopupFocusReason)
        setPalette(qApp->palette());
}

void QILabel::paintEvent(QPaintEvent *pEvent)
{
    /* Call to base-class: */
    QLabel::paintEvent(pEvent);

    /* If we have an order for full-size selection and have focus: */
    if (m_fFullSizeSelection && hasFocus())
    {
        /* Paint a focus rect based on the current style: */
        QPainter painter(this);
        QStyleOptionFocusRect option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, this);
    }
}

void QILabel::init()
{
    /* Initial setup: */
    m_fHintValid = false;
    m_iWidthHint = -1;
    m_fStartDragging = false;
    setFullSizeSelection(false);
    setOpenExternalLinks(true);

    /* Create invisible copy action: */
    m_pCopyAction = new QAction(this);
    if (m_pCopyAction)
    {
        /* Configure action: */
        m_pCopyAction->setShortcut(QKeySequence(QKeySequence::Copy));
        m_pCopyAction->setShortcutContext(Qt::WidgetShortcut);
        connect(m_pCopyAction, &QAction::triggered, this, &QILabel::copy);
        /* Add action to label: */
        addAction(m_pCopyAction);
    }
}

void QILabel::updateSizeHint() const
{
    /* Recalculate size-hint if necessary: */
    m_ownSizeHint = m_iWidthHint == -1 ? QSize() : QSize(m_iWidthHint, heightForWidth(m_iWidthHint));
    m_fHintValid = true;
}

void QILabel::setFullText(const QString &strText)
{
    /* Reapply size-policy: */
    QSizePolicy sp = sizePolicy();
    sp.setHeightForWidth(wordWrap());
    setSizePolicy(sp);

    /* Reset size-hint validity: */
    m_fHintValid = false;

    /* Remember new value: */
    m_strText = strText;
    updateText();
}

void QILabel::updateText()
{
    /* Compress text: */
    const QString strCompText = compressText(m_strText);

    /* Assign it: */
    QLabel::setText(strCompText);

    /* Only set the tool-tip if the text is shortened in any way: */
    if (removeHtmlTags(strCompText) != removeHtmlTags(m_strText))
        setToolTip(removeHtmlTags(m_strText));
    else
        setToolTip("");
}

QString QILabel::compressText(const QString &strText) const
{
    /* Prepare result: */
    QStringList result;
    QFontMetrics fm = fontMetrics();
    /* Split up any multi-line text: */
    foreach (QString strLine, strText.split(QRegularExpression("<br */?>")))
    {
        /* Search for the compact tag: */
        const QRegularExpressionMatch mt = s_regExpElide.match(strLine);
        if (mt.hasMatch())
        {
            /* USe the untouchable text to work on: */
            const QString strWork = strLine;
            /* Grep out the necessary info of the regexp: */
            const QString strCompact   = mt.captured(1);
            const QString strElideMode = mt.captured(2);
            const QString strElide     = mt.captured(3);
            /* Remove the whole compact tag (also the text): */
            const QString strFlat = removeHtmlTags(QString(strWork).remove(strCompact));
            /* What size will the text have without the compact text: */
            const int iFlatWidth = fm.horizontalAdvance(strFlat);
            /* Create the shortened text: */
            const QString strNew = fm.elidedText(strElide, toTextElideMode(strElideMode), width() - (2 * HOR_PADDING) - iFlatWidth);
            /* Replace the compact part with the shortened text in the initial string: */
            strLine = QString(strWork).replace(strCompact, strNew);
        }
        /* Append the line: */
        result << strLine;
    }
    /* Return result: */
    return result.join("<br />");
}

/* static */
QString QILabel::removeHtmlTags(const QString &strText)
{
    /* Remove all HTML tags from the text and return it: */
    return QString(strText).remove(s_regExpCopy);
}

/* static */
Qt::TextElideMode QILabel::toTextElideMode(const QString &strType)
{
    /* Converts a string-represented type to a Qt elide mode: */
    Qt::TextElideMode enmMode = Qt::ElideNone;
    if (strType == "start")
        enmMode = Qt::ElideLeft;
    else if (strType == "middle")
        enmMode = Qt::ElideMiddle;
    else if (strType == "end")
        enmMode  = Qt::ElideRight;
    return enmMode;
}
