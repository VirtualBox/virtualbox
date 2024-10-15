/* $Id: UIPaneContainer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class implementation.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#include <QAbstractButton>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UIIconPool.h"
#include "UIPaneContainer.h"
#include "UITranslationEventListener.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif

/* Other VBox includes: */
#include <iprt/assert.h>

UIPaneContainer::UIPaneContainer(QWidget *pParent, EmbedTo enmEmbedTo /* = EmbedTo_Stack */, bool fDetachAllowed /* = false */)
    : QWidget(pParent)
    , m_enmEmbedTo(enmEmbedTo)
    , m_fDetachAllowed(fDetachAllowed)
    , m_pTabWidget(0)
    , m_pButtonBox(0)
{
    prepare();
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIPaneContainer::sltRetranslateUI);
}

void UIPaneContainer::sltRetranslateUI()
{
    if (m_pButtonBox)
    {
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setText(tr("Detach"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setStatusTip(tr("Open the tool in separate window"));
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setToolTip(tr("Open in Separate Window"));
    }
}

void UIPaneContainer::prepare()
{
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    AssertReturnVoid(pLayout);
    pLayout->setContentsMargins(0, 0, 0, 0);

    /* Add the tab widget: */
    m_pTabWidget = new QTabWidget;
    AssertReturnVoid(m_pTabWidget);
    connect(m_pTabWidget, &QTabWidget::currentChanged, this, &UIPaneContainer::sigCurrentTabChanged);
    pLayout->addWidget(m_pTabWidget);

    /* If parent embedded into stack: */
    if (m_enmEmbedTo == EmbedTo_Stack && m_fDetachAllowed)
    {
        /* Add the button-box: */
        QWidget *pContainer = new QWidget;
        AssertReturnVoid(pContainer);
        QHBoxLayout *pSubLayout = new QHBoxLayout(pContainer);
        AssertReturnVoid(pSubLayout);
        const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
        const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin);
        const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin);
        pSubLayout->setContentsMargins(iL, 0, iR, iB);
        m_pButtonBox = new QIDialogButtonBox;
        AssertReturnVoid(m_pButtonBox);
        m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel);
        connect(m_pButtonBox, &QIDialogButtonBox::clicked, this, &UIPaneContainer::sltHandleButtonBoxClick);
        pSubLayout->addWidget(m_pButtonBox);
        pLayout->addWidget(pContainer);
    }
}

void UIPaneContainer::sltHide()
{
    hide();
    emit sigHidden();
}

void UIPaneContainer::sltHandleButtonBoxClick(QAbstractButton *pButton)
{
    /* Make sure button-box exists: */
    AssertPtrReturnVoid(m_pButtonBox);

    /* Disable buttons first of all: */
    m_pButtonBox->button(QDialogButtonBox::Cancel)->setEnabled(false);

    /* Compare with known buttons: */
    if (pButton == m_pButtonBox->button(QDialogButtonBox::Cancel))
        emit sigDetach();
}

void UIPaneContainer::insertTab(int iIndex, QWidget *pPage, const QString &strLabel /* = QString() */)
{
    if (m_pTabWidget)
        m_pTabWidget->insertTab(iIndex, pPage, strLabel);
}

void UIPaneContainer::setTabText(int iIndex, const QString &strText)
{
    if (!m_pTabWidget || iIndex < 0 || iIndex >= m_pTabWidget->count())
        return;
    m_pTabWidget->setTabText(iIndex, strText);
}

void UIPaneContainer::setCurrentIndex(int iIndex)
{
    if (!m_pTabWidget || iIndex >= m_pTabWidget->count() || iIndex < 0)
        return;
    m_pTabWidget->setCurrentIndex(iIndex);
}

int UIPaneContainer::currentIndex() const
{
    if (!m_pTabWidget)
        return -1;
    return m_pTabWidget->currentIndex();
}
