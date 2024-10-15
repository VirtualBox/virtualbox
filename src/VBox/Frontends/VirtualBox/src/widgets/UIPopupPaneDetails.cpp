/* $Id: UIPopupPaneDetails.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupPaneDetails class implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <QCheckBox>
#include <QTextDocument>
#include <QTextEdit>

/* GUI includes: */
#include "UIAnimationFramework.h"
#include "UIPopupPane.h"
#include "UIPopupPaneDetails.h"

/* Other VBox includes: */
#include <iprt/assert.h>

UIPopupPaneDetails::UIPopupPaneDetails(QWidget *pParent, const QString &strText, bool fFocused)
    : QWidget(pParent)
    , m_iLayoutMargin(5)
    , m_iLayoutSpacing(10)
    , m_strText(strText)
    , m_pTextEdit(0)
    , m_iDesiredTextEditWidth(-1)
    , m_iMaximumPaneHeight(-1)
    , m_iMaximumTextEditHeight(0)
    , m_iTextContentMargin(5)
    , m_fFocused(fFocused)
    , m_pAnimation(0)
{
    /* Prepare: */
    prepare();
}

void UIPopupPaneDetails::setText(const QString &strText)
{
    /* Make sure the text has changed: */
    if (m_strText == strText)
        return;

    /* Fetch new text: */
    m_strText = strText;
    m_pTextEdit->setText(m_strText);

    /* Update size-hint/visibility: */
    updateSizeHint();
    updateVisibility();
}

QSize UIPopupPaneDetails::minimumSizeHint() const
{
    /* Check if desired-width set: */
    if (m_iDesiredTextEditWidth >= 0)
        /* Dependent size-hint: */
        return m_minimumSizeHint;
    /* Golden-rule size-hint by default: */
    return QWidget::minimumSizeHint();
}

void UIPopupPaneDetails::setMinimumSizeHint(const QSize &minimumSizeHint)
{
    /* Make sure the size-hint has changed: */
    if (m_minimumSizeHint == minimumSizeHint)
        return;

    /* Fetch new size-hint: */
    m_minimumSizeHint = minimumSizeHint;

    /* Notify parent popup-pane: */
    emit sigSizeHintChanged();
}

void UIPopupPaneDetails::layoutContent()
{
    /* Variables: */
    const int iWidth = width();
    const int iHeight = height();
    const int iTextEditWidth = m_textEditSizeHint.width();
    const int iTextEditHeight = m_textEditSizeHint.height();

    /* TextEdit: */
    m_pTextEdit->move(m_iLayoutMargin, m_iLayoutMargin);
    m_pTextEdit->resize(qMin(iWidth, iTextEditWidth), qMin(iHeight, iTextEditHeight));
    /* Text-document: */
    QTextDocument *pTextDocument = m_pTextEdit->document();
    if (pTextDocument)
    {
        pTextDocument->adjustSize();
        pTextDocument->setTextWidth(m_pTextEdit->width() - m_iTextContentMargin);
    }
}

void UIPopupPaneDetails::sltHandleProposalForWidth(int iWidth)
{
    /* Make sure the desired-width has changed: */
    if (m_iDesiredTextEditWidth == iWidth)
        return;

    /* Fetch new desired-width: */
    m_iDesiredTextEditWidth = iWidth;

    /* Update size-hint: */
    updateSizeHint();
}

void UIPopupPaneDetails::sltHandleProposalForHeight(int iHeight)
{
    /* Make sure the desired-height has changed: */
    if (m_iMaximumPaneHeight == iHeight)
        return;

    /* Fetch new desired-height: */
    m_iMaximumPaneHeight = iHeight;
    m_iMaximumTextEditHeight = m_iMaximumPaneHeight - 2 * m_iLayoutMargin;

    /* Update size-hint: */
    updateSizeHint();
}

void UIPopupPaneDetails::sltFocusEnter()
{
    /* Ignore if already focused: */
    if (m_fFocused)
        return;

    /* Update focus state: */
    m_fFocused = true;

    /* Update visibility: */
    updateVisibility();

    /* Notify listeners: */
    emit sigFocusEnter();
}

void UIPopupPaneDetails::sltFocusLeave()
{
    /* Ignore if already unfocused: */
    if (!m_fFocused)
        return;

    /* Update focus state: */
    m_fFocused = false;

    /* Update visibility: */
    updateVisibility();

    /* Notify listeners: */
    emit sigFocusLeave();
}

void UIPopupPaneDetails::prepare()
{
    /* Prepare content: */
    prepareContent();
    /* Prepare animation: */
    prepareAnimation();

    /* Update size-hint/visibility: */
    updateSizeHint();
    updateVisibility();
}

void UIPopupPaneDetails::prepareContent()
{
    /* Create text-editor: */
    m_pTextEdit = new QTextEdit(this);
    if (m_pTextEdit)
    {
        /* Configure text-editor: */
        m_pTextEdit->setFont(tuneFont(m_pTextEdit->font()));
        m_pTextEdit->setText(m_strText);
        m_pTextEdit->setFocusProxy(this);
    }
}

void UIPopupPaneDetails::prepareAnimation()
{
    UIPopupPane *pPopupPane = qobject_cast<UIPopupPane*>(parent());
    AssertReturnVoid(pPopupPane);
    {
        /* Propagate parent signals: */
        connect(pPopupPane, &UIPopupPane::sigFocusEnter, this, &UIPopupPaneDetails::sltFocusEnter);
        connect(pPopupPane, &UIPopupPane::sigFocusLeave, this, &UIPopupPaneDetails::sltFocusLeave);
    }
    /* Install geometry animation for 'minimumSizeHint' property: */
    m_pAnimation = UIAnimation::installPropertyAnimation(this, "minimumSizeHint", "collapsedSizeHint", "expandedSizeHint",
                                                         SIGNAL(sigFocusEnter()), SIGNAL(sigFocusLeave()), m_fFocused);
}

void UIPopupPaneDetails::updateSizeHint()
{
    /* Recalculate collapsed size-hint: */
    {
        /* Collapsed size-hint with 0 height: */
        m_collapsedSizeHint = QSize(m_iDesiredTextEditWidth, 0);
    }

    /* Recalculate expanded size-hint: */
    {
        int iNewHeight = m_iMaximumPaneHeight;
        QTextDocument *pTextDocument = m_pTextEdit->document();
        if (pTextDocument)
        {
            /* Adjust text-edit size: */
            pTextDocument->adjustSize();
            /* Get corresponding QTextDocument size: */
            QSize textSize = pTextDocument->size().toSize();
            /* Make sure the text edits height is no larger than that of container widget: */
            iNewHeight = qMin(m_iMaximumTextEditHeight, textSize.height() + 2 * m_iLayoutMargin);
        }
        /* Recalculate label size-hint: */
        m_textEditSizeHint = QSize(m_iDesiredTextEditWidth, iNewHeight);
        /* Expanded size-hint contains full-size label: */
        m_expandedSizeHint = m_textEditSizeHint;
    }

    /* Update current size-hint: */
    m_minimumSizeHint = m_fFocused ? m_expandedSizeHint : m_collapsedSizeHint;

    /* Update animation: */
    if (m_pAnimation)
        m_pAnimation->update();

    /* Notify parent popup-pane: */
    emit sigSizeHintChanged();
}

void UIPopupPaneDetails::updateVisibility()
{
    if (m_fFocused && !m_strText.isEmpty())
        show();
    else
        hide();
}

/* static */
QFont UIPopupPaneDetails::tuneFont(QFont font)
{
#if defined(VBOX_WS_MAC)
    font.setPointSize(font.pointSize() - 2);
#elif defined(VBOX_WS_NIX)
    font.setPointSize(font.pointSize() - 1);
#endif
    return font;
}
