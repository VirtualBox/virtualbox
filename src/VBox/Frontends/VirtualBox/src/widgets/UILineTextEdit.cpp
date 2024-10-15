/* $Id: UILineTextEdit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UILineTextEdit class definitions.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIFileDialog.h"
#include "UICommon.h"
#include "UILineTextEdit.h"
#include "UITranslationEventListener.h"

////////////////////////////////////////////////////////////////////////////////
// UITextEditor

UITextEditor::UITextEditor(QWidget *pParent /* = NULL */)
  : QIDialog(pParent)
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    pMainLayout->setContentsMargins(12, 12, 12, 12);

    /* We need a text editor */
    m_pTextEdit = new QTextEdit(this);
    pMainLayout->addWidget(m_pTextEdit);
    /* and some buttons to interact with */
    m_pButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    m_pOpenButton = new QPushButton(this);
    m_pButtonBox->addButton(m_pOpenButton, QDialogButtonBox::ActionRole);
    pMainLayout->addWidget(m_pButtonBox);
    /* Connect the buttons so that they are useful */
    connect(m_pButtonBox, &QDialogButtonBox::accepted,
            this, &UITextEditor::accept);
    connect(m_pButtonBox, &QDialogButtonBox::rejected,
            this, &UITextEditor::reject);
    connect(m_pOpenButton, &QPushButton::clicked,
            this, &UITextEditor::open);

    /* Applying language settings */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UITextEditor::sltRetranslateUI);
}

void UITextEditor::setText(const QString& strText)
{
    m_pTextEdit->setText(strText);
}

QString UITextEditor::text() const
{
    return m_pTextEdit->toPlainText();
}

void UITextEditor::sltRetranslateUI()
{
    setWindowTitle(tr("Edit text"));
    m_pOpenButton->setText(tr("&Replace..."));
    m_pOpenButton->setToolTip(tr("Replaces the current text with the content of a file."));
}

void UITextEditor::open()
{
    QString fileName = QIFileDialog::getOpenFileName(uiCommon().documentsPath(), tr("Text (*.txt);;All (*.*)"), this, tr("Select a file to open..."));
    if (!fileName.isEmpty())
    {
        QFile file(fileName);
        if (file.open(QFile::ReadOnly))
        {
            QTextStream in(&file);
            m_pTextEdit->setPlainText(in.readAll());
        }
    }

}

////////////////////////////////////////////////////////////////////////////////
// UILineTextEdit

UILineTextEdit::UILineTextEdit(QWidget *pParent /* = NULL */)
  : QPushButton(pParent)
{
    connect(this, &UILineTextEdit::clicked,
            this, &UILineTextEdit::edit);

    /* Don't interpret the Enter Key. */
    setAutoDefault(false);
    setDefault(false);

    setFocusPolicy(Qt::StrongFocus);
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UILineTextEdit::sltRetranslateUI);
}

void UILineTextEdit::sltRetranslateUI()
{
    QPushButton::setText(tr("&Edit"));
}

void UILineTextEdit::edit()
{
    UITextEditor te(this);
    te.setText(m_strText);
    if (te.exec() == QDialog::Accepted)
    {
        m_strText = te.text();
        /* Notify listener(s) about we finished: */
        emit sigFinished(this);
    }
}
