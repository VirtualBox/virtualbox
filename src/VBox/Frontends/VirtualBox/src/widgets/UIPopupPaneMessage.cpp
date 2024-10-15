/* $Id: UIPopupPaneMessage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupPaneMessage class implementation.
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
#include <QLabel>
#include <QCheckBox>

/* GUI includes: */
#include "UIAnimationFramework.h"
#include "UIPopupPane.h"
#include "UIPopupPaneMessage.h"

/* Other VBox includes: */
#include <iprt/assert.h>

UIPopupPaneMessage::UIPopupPaneMessage(QWidget *pParent, const QString &strText, bool fFocused)
    : QWidget(pParent)
    , m_iLayoutMargin(0)
    , m_iLayoutSpacing(10)
    , m_strText(strText)
    , m_pLabel(0)
    , m_iDesiredLabelWidth(-1)
    , m_fFocused(fFocused)
    , m_pAnimation(0)
{
    /* Prepare: */
    prepare();
}

void UIPopupPaneMessage::setText(const QString &strText)
{
    /* Make sure the text has changed: */
    if (m_strText == strText)
        return;

    /* Fetch new text: */
    m_strText = strText;
    m_pLabel->setText(m_strText);

    /* Update size-hint: */
    updateSizeHint();
}

QSize UIPopupPaneMessage::minimumSizeHint() const
{
    /* Check if desired-width set: */
    if (m_iDesiredLabelWidth >= 0)
        /* Dependent size-hint: */
        return m_minimumSizeHint;
    /* Golden-rule size-hint by default: */
    return QWidget::minimumSizeHint();
}

void UIPopupPaneMessage::setMinimumSizeHint(const QSize &minimumSizeHint)
{
    /* Make sure the size-hint has changed: */
    if (m_minimumSizeHint == minimumSizeHint)
        return;

    /* Fetch new size-hint: */
    m_minimumSizeHint = minimumSizeHint;

    /* Notify parent popup-pane: */
    emit sigSizeHintChanged();
}

void UIPopupPaneMessage::layoutContent()
{
    /* Variables: */
    const int iWidth = width();
    const int iHeight = height();
    const int iLabelWidth = m_labelSizeHint.width();
    const int iLabelHeight = m_labelSizeHint.height();

    /* Label: */
    m_pLabel->move(m_iLayoutMargin, m_iLayoutMargin);
    m_pLabel->resize(qMin(iWidth, iLabelWidth), qMin(iHeight, iLabelHeight));
}

void UIPopupPaneMessage::sltHandleProposalForWidth(int iWidth)
{
    /* Make sure the desired-width has changed: */
    if (m_iDesiredLabelWidth == iWidth)
        return;

    /* Fetch new desired-width: */
    m_iDesiredLabelWidth = iWidth;

    /* Update size-hint: */
    updateSizeHint();
}

void UIPopupPaneMessage::sltFocusEnter()
{
    /* Ignore if already focused: */
    if (m_fFocused)
        return;

    /* Update focus state: */
    m_fFocused = true;

    /* Notify listeners: */
    emit sigFocusEnter();
}

void UIPopupPaneMessage::sltFocusLeave()
{
    /* Ignore if already unfocused: */
    if (!m_fFocused)
        return;

    /* Update focus state: */
    m_fFocused = false;

    /* Notify listeners: */
    emit sigFocusLeave();
}

void UIPopupPaneMessage::prepare()
{
    /* Prepare content: */
    prepareContent();
    /* Prepare animation: */
    prepareAnimation();

    /* Update size-hint: */
    updateSizeHint();
}

void UIPopupPaneMessage::prepareContent()
{
    /* Create label: */
    m_pLabel = new QLabel(this);
    if (m_pLabel)
    {
        /* Configure label: */
        m_pLabel->setFont(tuneFont(m_pLabel->font()));
        m_pLabel->setWordWrap(true);
        m_pLabel->setFocusPolicy(Qt::NoFocus);
        m_pLabel->setText(m_strText);
    }
}

void UIPopupPaneMessage::prepareAnimation()
{
    UIPopupPane *pPopupPane = qobject_cast<UIPopupPane*>(parent());
    AssertReturnVoid(pPopupPane);
    {
        /* Propagate parent signals: */
        connect(pPopupPane, &UIPopupPane::sigFocusEnter, this, &UIPopupPaneMessage::sltFocusEnter);
        connect(pPopupPane, &UIPopupPane::sigFocusLeave, this, &UIPopupPaneMessage::sltFocusLeave);
    }
    /* Install geometry animation for 'minimumSizeHint' property: */
    m_pAnimation = UIAnimation::installPropertyAnimation(this, "minimumSizeHint", "collapsedSizeHint", "expandedSizeHint",
                                                         SIGNAL(sigFocusEnter()), SIGNAL(sigFocusLeave()), m_fFocused);
}

void UIPopupPaneMessage::updateSizeHint()
{
    /* Recalculate collapsed size-hint: */
    {
        /* Collapsed size-hint contains only one-text-line label: */
        QFontMetrics fm(m_pLabel->font(), m_pLabel);
        m_collapsedSizeHint = QSize(m_iDesiredLabelWidth, fm.height());
    }

    /* Recalculate expanded size-hint: */
    {
        /* Recalculate label size-hint: */
        m_labelSizeHint = QSize(m_iDesiredLabelWidth, m_pLabel->heightForWidth(m_iDesiredLabelWidth));
        /* Expanded size-hint contains full-size label: */
        m_expandedSizeHint = m_labelSizeHint;
    }

    /* Update current size-hint: */
    m_minimumSizeHint = m_fFocused ? m_expandedSizeHint : m_collapsedSizeHint;

    /* Update animation: */
    if (m_pAnimation)
        m_pAnimation->update();

    /* Notify parent popup-pane: */
    emit sigSizeHintChanged();
}

/* static */
QFont UIPopupPaneMessage::tuneFont(QFont font)
{
#if defined(VBOX_WS_MAC)
    font.setPointSize(font.pointSize() - 2);
#elif defined(VBOX_WS_NIX)
    font.setPointSize(font.pointSize() - 1);
#endif
    return font;
}
