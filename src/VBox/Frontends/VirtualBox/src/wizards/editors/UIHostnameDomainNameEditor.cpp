/* $Id: UIHostnameDomainNameEditor.cpp 110463 2025-07-29 16:48:53Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIHostnameDomainNameEditor class implementation.
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
#include <QLineEdit>
#include <QRegularExpressionValidator>
#include <QStyle>
#include <QVBoxLayout>

/* GUI includes: */
#include "QILineEdit.h"
#include "QIRichTextLabel.h"
#include "QIToolButton.h"
#include "UIIconPool.h"
#include "UIHostnameDomainNameEditor.h"
#include "UITranslationEventListener.h"
#include "UIWizardNewVM.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIHostnameDomainNameEditor::UIHostnameDomainNameEditor(QWidget *pParent /*  = 0 */)
    : QWidget(pParent)
    , m_pHostnameLineEdit(0)
    , m_pDomainNameLineEdit(0)
    , m_pProductKeyLineEdit(0)
    , m_pHostnameLabel(0)
    , m_pDomainNameLabel(0)
    , m_pProductKeyLabel(0)
    , m_pMainLayout(0)
    , m_pStartHeadlessCheckBox(0)
{
    prepare();
}

QString UIHostnameDomainNameEditor::hostname() const
{
    if (m_pHostnameLineEdit)
        return m_pHostnameLineEdit->text();
    return QString();
}

bool UIHostnameDomainNameEditor::hostDomainNameComplete() const
{
    return m_pHostnameLineEdit && m_pHostnameLineEdit->hasAcceptableInput() &&
        m_pDomainNameLineEdit && m_pDomainNameLineEdit->hasAcceptableInput();
}

void UIHostnameDomainNameEditor::mark(bool fProductKeyRequired)
{
    if (m_pHostnameLineEdit)
        m_pHostnameLineEdit->mark(!m_pHostnameLineEdit->hasAcceptableInput(),
                                  tr("Host name should be at least 2 character long. "
                                     "Allowed characters are alphanumerics, \"-\" and \".\""),
                                  tr("Host name is valid"));
    if (m_pDomainNameLineEdit)
        m_pDomainNameLineEdit->mark(!m_pDomainNameLineEdit->hasAcceptableInput(),
                                    tr("Domain name should be at least 2 character long. "
                                       "Allowed characters are alphanumerics, \"-\" and \".\""),
                                    tr("Domain name is valid"));
    if (m_pProductKeyLineEdit)
        m_pProductKeyLineEdit->mark(fProductKeyRequired && !m_pProductKeyLineEdit->hasAcceptableInput(),
                                    tr("Selected OS requires a valid product key"),
                                    tr("Product key is valid"));
}

void UIHostnameDomainNameEditor::setHostname(const QString &strHostname)
{
    if (m_pHostnameLineEdit)
        m_pHostnameLineEdit->setText(strHostname);
}

QString UIHostnameDomainNameEditor::domainName() const
{
    if (m_pDomainNameLineEdit)
        return m_pDomainNameLineEdit->text();
    return QString();
}

void UIHostnameDomainNameEditor::setDomainName(const QString &strDomain)
{
    if (m_pDomainNameLineEdit)
        m_pDomainNameLineEdit->setText(strDomain);
}

QString UIHostnameDomainNameEditor::hostnameDomainName() const
{
    if (m_pHostnameLineEdit && m_pDomainNameLineEdit)
        return QString("%1.%2").arg(m_pHostnameLineEdit->text()).arg(m_pDomainNameLineEdit->text());
    return QString();
}

void UIHostnameDomainNameEditor::sltRetranslateUI()
{
    if (m_pHostnameLabel)
        m_pHostnameLabel->setText(tr("Host Na&me"));
    if (m_pHostnameLineEdit)
        m_pHostnameLineEdit->setToolTip(tr("Host name to be assigned to the virtual machine"));
    if (m_pDomainNameLabel)
        m_pDomainNameLabel->setText(tr("&Domain Name"));
    if (m_pDomainNameLineEdit)
        m_pDomainNameLineEdit->setToolTip(tr("Domain name to be assigned to the virtual machine"));
    if (m_pProductKeyLabel)
        m_pProductKeyLabel->setText(UIWizardNewVM::tr("&Product Key"));
    if (m_pProductKeyLineEdit)
        m_pProductKeyLineEdit->setToolTip(UIWizardNewVM::tr("The product key"));

    if (m_pStartHeadlessCheckBox)
    {
        m_pStartHeadlessCheckBox->setText(UIWizardNewVM::tr("&Install in Background"));
        m_pStartHeadlessCheckBox->setToolTip(UIWizardNewVM::tr("Start the virtual machine without a GUI"));
    }
}

void UIHostnameDomainNameEditor::addLineEdit(int &iRow, QLabel *&pLabel, QILineEdit *&pLineEdit, QGridLayout *pLayout)
{
    AssertReturnVoid(pLayout);
    if (pLabel || pLineEdit)
        return;

    pLabel = new QLabel;
    AssertReturnVoid(pLabel);
    pLabel->setAlignment(Qt::AlignRight);
    pLayout->addWidget(pLabel, iRow, 0, 1, 1);

    pLineEdit = new QILineEdit;
    AssertReturnVoid(pLineEdit);
    pLineEdit->setMarkable(true);

    pLayout->addWidget(pLineEdit, iRow, 1, 1, 3);
    pLabel->setBuddy(pLineEdit);
    ++iRow;
}

void UIHostnameDomainNameEditor::prepare()
{
    m_pMainLayout = new QGridLayout;
    if (!m_pMainLayout)
        return;
    setLayout(m_pMainLayout);
    m_pMainLayout->setContentsMargins(0, 0, 0, 0);
    int iRow = 0;

    addLineEdit(iRow, m_pProductKeyLabel, m_pProductKeyLineEdit, m_pMainLayout);
    addLineEdit(iRow, m_pHostnameLabel, m_pHostnameLineEdit, m_pMainLayout);
    addLineEdit(iRow, m_pDomainNameLabel, m_pDomainNameLineEdit, m_pMainLayout);

    if (m_pProductKeyLineEdit)
    {
        m_pProductKeyLineEdit->setInputMask(">NNNNN-NNNNN-NNNNN-NNNNN-NNNNN;#");
        m_pProductKeyLineEdit->setMinimumWidthByText("NNNNN-NNNNN-NNNNN-NNNNN-NNNNN");
    }

    /* Host name and domain should be strings of minimum length of 2 and composed of alpha numerics, '-', and '.'
     * Exclude strings with . at the end: */
    m_pHostnameLineEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("^[a-zA-Z0-9-.]{2,}[$a-zA-Z0-9-]"), this));
    m_pDomainNameLineEdit->setValidator(new QRegularExpressionValidator(QRegularExpression("^[a-zA-Z0-9-.]{2,}[$a-zA-Z0-9-]"), this));

    connect(m_pHostnameLineEdit, &QILineEdit::textChanged,
            this, &UIHostnameDomainNameEditor::sltHostnameChanged);
    connect(m_pDomainNameLineEdit, &QILineEdit::textChanged,
            this, &UIHostnameDomainNameEditor::sltDomainChanged);
    connect(m_pProductKeyLineEdit, &QILineEdit::textChanged,
            this, &UIHostnameDomainNameEditor::sigProductKeyChanged);

    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIHostnameDomainNameEditor::sltRetranslateUI);

    m_pStartHeadlessCheckBox = new QCheckBox;
    if (m_pStartHeadlessCheckBox)
        m_pMainLayout->addWidget(m_pStartHeadlessCheckBox, iRow, 1, 1, 3);

    if (m_pStartHeadlessCheckBox)
        connect(m_pStartHeadlessCheckBox, &QCheckBox::toggled,
                this, &UIHostnameDomainNameEditor::sigStartHeadlessChanged);

    sltRetranslateUI();
}

void UIHostnameDomainNameEditor::sltHostnameChanged()
{
    m_pHostnameLineEdit->mark(!m_pHostnameLineEdit->hasAcceptableInput(),
                              tr("Host name should be at least 2 character long. "
                                 "Allowed characters are alphanumerics, \"-\" and \".\""),
                              tr("Host name is valid"));
    emit sigHostnameDomainNameChanged(hostnameDomainName(), hostDomainNameComplete());
}

void UIHostnameDomainNameEditor::sltDomainChanged()
{
    m_pDomainNameLineEdit->mark(!m_pDomainNameLineEdit->hasAcceptableInput(),
                                tr("Domain name should be at least 2 character long. "
                                   "Allowed characters are alphanumerics, \"-\" and \".\""),
                                tr("Domain name is valid"));
    emit sigHostnameDomainNameChanged(hostnameDomainName(), hostDomainNameComplete());
}

void UIHostnameDomainNameEditor::disableEnableProductKeyWidgets(bool fEnabled)
{
    if (m_pProductKeyLabel)
        m_pProductKeyLabel->setEnabled(fEnabled);
    if (m_pProductKeyLineEdit)
        m_pProductKeyLineEdit->setEnabled(fEnabled);
}

bool UIHostnameDomainNameEditor::hasProductKeyAcceptableInput() const
{
    if (m_pProductKeyLineEdit)
        return m_pProductKeyLineEdit->hasAcceptableInput();
    return false;
}
