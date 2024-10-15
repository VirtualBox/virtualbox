/* $Id: UIMiniToolbarSettingsEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMiniToolbarSettingsEditor class implementation.
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
#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>

/* GUI includes: */
#include "UIMiniToolbarSettingsEditor.h"


UIMiniToolbarSettingsEditor::UIMiniToolbarSettingsEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_fShowMiniToolbar(false)
    , m_fMiniToolbarAtTop(false)
    , m_pLabel(0)
    , m_pCheckBoxShowMiniToolBar(0)
    , m_pCheckBoxMiniToolBarAtTop(0)
{
    prepare();
}

void UIMiniToolbarSettingsEditor::setShowMiniToolbar(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fShowMiniToolbar != fOn)
    {
        m_fShowMiniToolbar = fOn;
        if (m_pCheckBoxShowMiniToolBar)
            m_pCheckBoxShowMiniToolBar->setCheckState(m_fShowMiniToolbar ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIMiniToolbarSettingsEditor::showMiniToolbar() const
{
    return   m_pCheckBoxShowMiniToolBar
           ? m_pCheckBoxShowMiniToolBar->checkState() == Qt::Checked
           : m_fShowMiniToolbar;
}

void UIMiniToolbarSettingsEditor::setMiniToolbarAtTop(bool fOn)
{
    /* Update cached value and
     * check-box if value has changed: */
    if (m_fMiniToolbarAtTop != fOn)
    {
        m_fMiniToolbarAtTop = fOn;
        if (m_pCheckBoxMiniToolBarAtTop)
            m_pCheckBoxMiniToolBarAtTop->setCheckState(m_fMiniToolbarAtTop ? Qt::Checked : Qt::Unchecked);
    }
}

bool UIMiniToolbarSettingsEditor::miniToolbarAtTop() const
{
    return   m_pCheckBoxMiniToolBarAtTop
           ? m_pCheckBoxMiniToolBarAtTop->checkState() == Qt::Checked
           : m_fMiniToolbarAtTop;
}

int UIMiniToolbarSettingsEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIMiniToolbarSettingsEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIMiniToolbarSettingsEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Mini ToolBar:"));
    if (m_pCheckBoxShowMiniToolBar)
    {
        m_pCheckBoxShowMiniToolBar->setText(tr("Show in &Full-screen/Seamless"));
        m_pCheckBoxShowMiniToolBar->setToolTip(tr("When checked, show the Mini ToolBar in full-screen and seamless modes."));
    }
    if (m_pCheckBoxMiniToolBarAtTop)
    {
        m_pCheckBoxMiniToolBarAtTop->setText(tr("Show at &Top of Screen"));
        m_pCheckBoxMiniToolBarAtTop->setToolTip(tr("When checked, show the Mini ToolBar at the top of the screen, rather than in "
                                                   "its default position at the bottom of the screen."));
    }
}

void UIMiniToolbarSettingsEditor::prepare()
{
    /* Prepare main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(1, 1);

        /* Prepare label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }
        /* Prepare 'enable output' check-box: */
        m_pCheckBoxShowMiniToolBar = new QCheckBox(this);
        if (m_pCheckBoxShowMiniToolBar)
            m_pLayout->addWidget(m_pCheckBoxShowMiniToolBar, 0, 1);
        /* Prepare 'enable input' check-box: */
        m_pCheckBoxMiniToolBarAtTop = new QCheckBox(this);
        if (m_pCheckBoxMiniToolBarAtTop)
            m_pLayout->addWidget(m_pCheckBoxMiniToolBarAtTop, 1, 1);
    }

    /* Prepare connections and widget availability: */
    if (   m_pCheckBoxShowMiniToolBar
        && m_pCheckBoxMiniToolBarAtTop)
    {
        connect(m_pCheckBoxShowMiniToolBar, &QCheckBox::toggled,
                m_pCheckBoxMiniToolBarAtTop, &QCheckBox::setEnabled);
        m_pCheckBoxMiniToolBarAtTop->setEnabled(m_pCheckBoxShowMiniToolBar->isChecked());
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
