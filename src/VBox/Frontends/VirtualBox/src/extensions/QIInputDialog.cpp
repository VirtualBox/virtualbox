/* $Id: QIInputDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIInputDialog class implementation.
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
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QIInputDialog.h"


QIInputDialog::QIInputDialog(QWidget *pParent /* = 0 */, Qt::WindowFlags enmFlags /* = Qt::WindowFlags() */)
    : QDialog(pParent, enmFlags)
    , m_fDefaultLabelTextRedefined(false)
    , m_pLabel(0)
    , m_pTextValueEditor(0)
    , m_pButtonBox(0)
{
    /* Prepare: */
    prepare();
}

QString QIInputDialog::labelText() const
{
    return m_pLabel ? m_pLabel->text() : QString();
}

void QIInputDialog::resetLabelText()
{
    m_fDefaultLabelTextRedefined = false;
    retranslateUi();
}

void QIInputDialog::setLabelText(const QString &strText)
{
    m_fDefaultLabelTextRedefined = true;
    if (m_pLabel)
        m_pLabel->setText(strText);
}

QString QIInputDialog::textValue() const
{
    return m_pTextValueEditor ? m_pTextValueEditor->text() : QString();
}

void QIInputDialog::setTextValue(const QString &strText)
{
    if (m_pTextValueEditor)
        m_pTextValueEditor->setText(strText);
}

void QIInputDialog::retranslateUi()
{
    if (m_pLabel && !m_fDefaultLabelTextRedefined)
        m_pLabel->setText(tr("Name:"));
}

void QIInputDialog::sltTextChanged()
{
    if (m_pButtonBox)
        m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(!textValue().isEmpty());
}

void QIInputDialog::prepare()
{
    /* Do not count that window as important for application,
     * it will NOT be taken into account when other
     * top-level windows will be closed: */
    setAttribute(Qt::WA_QuitOnClose, false);

    /* Create main layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    if (pMainLayout)
    {
        /* Create label: */
        m_pLabel = new QLabel(this);
        if (m_pLabel)
            pMainLayout->addWidget(m_pLabel);

        /* Create text value editor: */
        m_pTextValueEditor = new QLineEdit(this);
        if (m_pTextValueEditor)
        {
            connect(m_pTextValueEditor, &QLineEdit::textChanged, this, &QIInputDialog::sltTextChanged);
            pMainLayout->addWidget(m_pTextValueEditor);
        }

        /* Create button-box: */
        m_pButtonBox = new QIDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
        if (m_pButtonBox)
        {
            connect(m_pButtonBox, &QIDialogButtonBox::accepted, this, &QIInputDialog::accept);
            connect(m_pButtonBox, &QIDialogButtonBox::rejected, this, &QIInputDialog::reject);
            pMainLayout->addWidget(m_pButtonBox);
        }
    }

    /* Apply language settings: */
    retranslateUi();

    /* Initialize editors: */
    sltTextChanged();
}
