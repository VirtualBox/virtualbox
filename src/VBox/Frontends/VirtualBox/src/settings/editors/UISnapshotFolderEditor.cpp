/* $Id: UISnapshotFolderEditor.cpp 168930 2025-05-19 15:06:14Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UISnapshotFolderEditor class implementation.
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
#include "UIFilePathSelector.h"
#include "UISnapshotFolderEditor.h"


UISnapshotFolderEditor::UISnapshotFolderEditor(QWidget *pParent /* = 0 */)
    : UIEditor(pParent)
    , m_pLabel(0)
    , m_pSelector(0)
{
    prepare();
}

void UISnapshotFolderEditor::setPath(const QString &strPath)
{
    /* Update cached value and
     * editor if value has changed: */
    if (m_strPath != strPath)
    {
        m_strPath = strPath;
        if (m_pSelector)
            m_pSelector->setPath(m_strPath);
    }
}

QString UISnapshotFolderEditor::path() const
{
    return m_pSelector ? m_pSelector->path() : m_strPath;
}

void UISnapshotFolderEditor::setInitialPath(const QString &strInitialPath)
{
    /* Update cached value and
     * editor if value has changed: */
    if (m_strInitialPath != strInitialPath)
    {
        m_strInitialPath = strInitialPath;
        if (m_pSelector)
            m_pSelector->setInitialPath(m_strInitialPath);
    }
}

QString UISnapshotFolderEditor::initialPath() const
{
    return m_pSelector ? m_pSelector->initialPath() : m_strInitialPath;
}

int UISnapshotFolderEditor::minimumLabelHorizontalHint() const
{
    return m_pLabel ? m_pLabel->minimumSizeHint().width() : 0;
}

void UISnapshotFolderEditor::setMinimumLayoutIndent(int iIndent)
{
    if (m_pLayout)
        m_pLayout->setColumnMinimumWidth(0, iIndent);
}

void UISnapshotFolderEditor::sltRetranslateUI()
{
    if (m_pLabel)
        m_pLabel->setText(tr("S&napshot Folder"));
    if (m_pSelector)
        m_pSelector->setToolTip(tr("Folder where snapshots of this VM are saved"));
}

void UISnapshotFolderEditor::prepare()
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
            m_pLayout->addWidget(m_pSelector, 0, 1);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();
}
