/* $Id: UIDefaultMachineFolderEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDefaultMachineFolderEditor class implementation.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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
#include <QGridLayout>
#include <QLabel>

/* GUI includes: */
#include "UIDefaultMachineFolderEditor.h"
#include "UIFilePathSelector.h"
#include "UIGlobalSession.h"


UIDefaultMachineFolderEditor::UIDefaultMachineFolderEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent, true /* show in basic mode? */)
    , m_strValue(QString())
    , m_pLayout(0)
    , m_pLabel(0)
    , m_pSelector(0)
{
    prepare();
}

void UIDefaultMachineFolderEditor::setValue(const QString &strValue)
{
    /* Update cached value and editor
     * if value has changed: */
    if (m_strValue != strValue)
    {
        m_strValue = strValue;
        if (m_pSelector)
            m_pSelector->setPath(m_strValue);
    }
}

QString UIDefaultMachineFolderEditor::value() const
{
    return m_pSelector ? m_pSelector->path() : m_strValue;
}

int UIDefaultMachineFolderEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UIDefaultMachineFolderEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UIDefaultMachineFolderEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("Default &Machine Folder:"));
    if (m_pSelector)
        m_pSelector->setToolTip(tr("Holds the path to the default virtual machine folder. This folder is used, "
                                   "if not explicitly specified otherwise, when creating new virtual machines."));
}

void UIDefaultMachineFolderEditor::prepare()
{
    /* Create main layout: */
    m_pLayout = new QGridLayout(this);
    if (m_pLayout)
    {
        m_pLayout->setContentsMargins(0, 0, 0, 0);
        m_pLayout->setColumnStretch(1, 1);

        /* Create label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
        {
            m_pLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_pLayout->addWidget(m_pLabel, 0, 0);
        }

        /* Create selector: */
        m_pSelector = new UIFilePathSelector(this);
        if (m_pSelector)
        {
            if (m_pLabel)
                m_pLabel->setBuddy(m_pSelector);
            m_pSelector->setInitialPath(gpGlobalSession->homeFolder());
            connect(m_pSelector, &UIFilePathSelector::pathChanged,
                    this, &UIDefaultMachineFolderEditor::sigPathChanged);

            m_pLayout->addWidget(m_pSelector, 0, 1);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
