/* $Id: QIRichToolButton.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIRichToolButton class declaration.
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

/* Qt includes: */
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QStyleOptionFocusRect>
#include <QStylePainter>

/* GUI includes: */
#include "QIRichToolButton.h"
#include "QIToolButton.h"

/* Other VBox includes: */
#include "iprt/assert.h"


QIRichToolButton::QIRichToolButton(QWidget *pParent)
    : QWidget(pParent)
    , m_pButton(0)
    , m_pLabel(0)
{
    /* Prepare: */
    prepare();
}

void QIRichToolButton::setIconSize(const QSize &iconSize)
{
    m_pButton->setIconSize(iconSize);
}

void QIRichToolButton::setIcon(const QIcon &icon)
{
    m_pButton->setIcon(icon);
}

void QIRichToolButton::animateClick()
{
    m_pButton->animateClick();
}

void QIRichToolButton::setText(const QString &strText)
{
    m_pLabel->setText(strText);
}

void QIRichToolButton::paintEvent(QPaintEvent *pEvent)
{
    /* Draw focus around whole button if focused: */
    if (hasFocus())
    {
        QStylePainter painter(this);
        QStyleOptionFocusRect option;
        option.initFrom(this);
        option.rect = geometry();
        painter.drawPrimitive(QStyle::PE_FrameFocusRect, option);
    }
    /* Call to base-class: */
    QWidget::paintEvent(pEvent);
}

void QIRichToolButton::keyPressEvent(QKeyEvent *pEvent)
{
    /* Handle different keys: */
    switch (pEvent->key())
    {
        /* Animate-click for the Space key: */
        case Qt::Key_Space: return animateClick();
        default: break;
    }
    /* Call to base-class: */
    QWidget::keyPressEvent(pEvent);
}

void QIRichToolButton::mousePressEvent(QMouseEvent *pEvent)
{
    NOREF(pEvent);
    /* Animate-click: */
    animateClick();
}

void QIRichToolButton::prepare()
{
    /* Enable string focus: */
    setFocusPolicy(Qt::StrongFocus);

    /* Create main-layout: */
    QHBoxLayout *pMainLayout = new QHBoxLayout(this);
    AssertPtrReturnVoid(pMainLayout);
    {
        /* Configure main-layout: */
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        pMainLayout->setSpacing(0);
        /* Create tool-button: */
        m_pButton = new QIToolButton;
        AssertPtrReturnVoid(m_pButton);
        {
            /* Configure tool-button: */
            m_pButton->removeBorder();
            m_pButton->setFocusPolicy(Qt::NoFocus);
            connect(m_pButton, &QIToolButton::clicked, this, &QIRichToolButton::sltButtonClicked);
            connect(m_pButton, &QIToolButton::clicked, this, &QIRichToolButton::sigClicked);
            /* Add tool-button into main-layout: */
            pMainLayout->addWidget(m_pButton);
        }
        /* Create text-label: */
        m_pLabel = new QLabel;
        AssertPtrReturnVoid(m_pLabel);
        {
            /* Configure text-label: */
            m_pLabel->setBuddy(m_pButton);
            m_pLabel->setStyleSheet("QLabel {padding: 2px 0px 2px 0px;}");
            /* Add text-label into main-layout: */
            pMainLayout->addWidget(m_pLabel);
        }
    }
}
