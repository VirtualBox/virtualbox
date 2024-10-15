/* $Id: UIUSBFilterDetailsEditor.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIUSBFilterDetailsEditor class implementation.
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
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpressionValidator>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QILineEdit.h"
#include "UIConverter.h"
#include "UITranslationEventListener.h"
#include "UIUSBFilterDetailsEditor.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UIUSBFilterDetailsEditor::UIUSBFilterDetailsEditor(QWidget *pParent /* = 0 */)
    : QIDialog(pParent, Qt::Sheet)
    , m_pLabelName(0)
    , m_pEditorName(0)
    , m_pLabelVendorID(0)
    , m_pEditorVendorID(0)
    , m_pLabelProductID(0)
    , m_pEditorProductID(0)
    , m_pLabelRevision(0)
    , m_pEditorRevision(0)
    , m_pLabelManufacturer(0)
    , m_pEditorManufacturer(0)
    , m_pLabelProduct(0)
    , m_pEditorProduct(0)
    , m_pLabelSerialNo(0)
    , m_pEditorSerialNo(0)
    , m_pLabelPort(0)
    , m_pEditorPort(0)
    , m_pLabelRemote(0)
    , m_pComboRemote(0)
    , m_pButtonBox(0)
{
    prepare();
}

void UIUSBFilterDetailsEditor::setName(const QString &strName)
{
    if (m_pEditorName)
        m_pEditorName->setText(strName);
}

QString UIUSBFilterDetailsEditor::name() const
{
    return m_pEditorName ? wiped(m_pEditorName->text()) : QString();
}

void UIUSBFilterDetailsEditor::setVendorID(const QString &strVendorID)
{
    if (m_pEditorVendorID)
        m_pEditorVendorID->setText(strVendorID);
}

QString UIUSBFilterDetailsEditor::vendorID() const
{
    return m_pEditorVendorID ? wiped(m_pEditorVendorID->text()) : QString();
}

void UIUSBFilterDetailsEditor::setProductID(const QString &strProductID)
{
    if (m_pEditorProductID)
        m_pEditorProductID->setText(strProductID);
}

QString UIUSBFilterDetailsEditor::productID() const
{
    return m_pEditorProductID ? wiped(m_pEditorProductID->text()) : QString();
}

void UIUSBFilterDetailsEditor::setRevision(const QString &strRevision)
{
    if (m_pEditorRevision)
        m_pEditorRevision->setText(strRevision);
}

QString UIUSBFilterDetailsEditor::revision() const
{
    return m_pEditorRevision ? wiped(m_pEditorRevision->text()) : QString();
}

void UIUSBFilterDetailsEditor::setManufacturer(const QString &strManufacturer)
{
    if (m_pEditorManufacturer)
        m_pEditorManufacturer->setText(strManufacturer);
}

QString UIUSBFilterDetailsEditor::manufacturer() const
{
    return m_pEditorManufacturer ? wiped(m_pEditorManufacturer->text()) : QString();
}

void UIUSBFilterDetailsEditor::setProduct(const QString &strProduct)
{
    if (m_pEditorProduct)
        m_pEditorProduct->setText(strProduct);
}

QString UIUSBFilterDetailsEditor::product() const
{
    return m_pEditorProduct ? wiped(m_pEditorProduct->text()) : QString();
}

void UIUSBFilterDetailsEditor::setSerialNo(const QString &strSerialNo)
{
    if (m_pEditorSerialNo)
        m_pEditorSerialNo->setText(strSerialNo);
}

QString UIUSBFilterDetailsEditor::serialNo() const
{
    return m_pEditorSerialNo ? wiped(m_pEditorSerialNo->text()) : QString();
}

void UIUSBFilterDetailsEditor::setPort(const QString &strPort)
{
    if (m_pEditorPort)
        m_pEditorPort->setText(strPort);
}

QString UIUSBFilterDetailsEditor::port() const
{
    return m_pEditorPort ? wiped(m_pEditorPort->text()) : QString();
}

void UIUSBFilterDetailsEditor::setRemoteMode(const UIRemoteMode &enmRemoteMode)
{
    /* Look for proper index to choose: */
    if (m_pComboRemote)
    {
        const int iIndex = m_pComboRemote->findData(QVariant::fromValue(enmRemoteMode));
        if (iIndex != -1)
            m_pComboRemote->setCurrentIndex(iIndex);
    }
}

UIRemoteMode UIUSBFilterDetailsEditor::remoteMode() const
{
    return m_pComboRemote ? m_pComboRemote->currentData().value<UIRemoteMode>() : UIRemoteMode_Any;
}

void UIUSBFilterDetailsEditor::sltRetranslateUI()
{
    setWindowTitle(tr("USB Filter Details"));

    if (m_pLabelName)
        m_pLabelName->setText(tr("&Name:"));
    if (m_pEditorName)
        m_pEditorName->setToolTip(tr("Holds the filter name."));

    if (m_pLabelVendorID)
        m_pLabelVendorID->setText(tr("&Vendor ID:"));
    if (m_pEditorVendorID)
        m_pEditorVendorID->setToolTip(tr("Holds the vendor ID filter. The exact match string format is 'XXXX' "
                                         "where 'X' is a hexadecimal digit. An empty string will match any value."));

    if (m_pLabelProductID)
        m_pLabelProductID->setText(tr("&Product ID:"));
    if (m_pEditorProductID)
        m_pEditorProductID->setToolTip(tr("Holds the product ID filter. The exact match string format is 'XXXX' "
                                          "where 'X' is a hexadecimal digit. An empty string will match any value."));

    if (m_pLabelRevision)
        m_pLabelRevision->setText(tr("&Revision:"));
    if (m_pEditorRevision)
        m_pEditorRevision->setToolTip(tr("Holds the revision number filter. The exact match string format is "
                                         "'IIFF' where 'I' is a decimal digit of the integer part and 'F' "
                                         "is a decimal digit of the fractional part. An empty string will match any value."));

    if (m_pLabelManufacturer)
        m_pLabelManufacturer->setText(tr("&Manufacturer:"));
    if (m_pEditorManufacturer)
        m_pEditorManufacturer->setToolTip(tr("Holds the manufacturer filter as an exact match string. An empty string "
                                             "will match any value."));

    if (m_pLabelProduct)
        m_pLabelProduct->setText(tr("Pro&duct:"));
    if (m_pEditorProduct)
        m_pEditorProduct->setToolTip(tr("Holds the product name filter as an exact match string. An empty string will "
                                        "match any value."));

    if (m_pLabelSerialNo)
        m_pLabelSerialNo->setText(tr("&Serial No.:"));
    if (m_pEditorSerialNo)
        m_pEditorSerialNo->setToolTip(tr("Holds the serial number filter as an exact match string. An empty string will "
                                         "match any value."));

    if (m_pLabelPort)
        m_pLabelPort->setText(tr("Por&t:"));
    if (m_pEditorPort)
        m_pEditorPort->setToolTip(tr("Holds the host USB port filter as an exact match string. An empty string will match "
                                     "any value."));

    if (m_pLabelRemote)
        m_pLabelRemote->setText(tr("R&emote:"));
    if (m_pComboRemote)
    {
        for (int i = 0; i < m_pComboRemote->count(); ++i)
        {
            const UIRemoteMode enmType = m_pComboRemote->itemData(i).value<UIRemoteMode>();
            m_pComboRemote->setItemText(i, gpConverter->toString(enmType));
        }
        m_pComboRemote->setToolTip(tr("Holds whether this filter applies to USB devices attached locally to the host computer "
                                      "(No), to a VRDP client's computer (Yes), or both (Any)."));
    }
}

void UIUSBFilterDetailsEditor::sltRevalidate()
{
    /* Cast sender to editor: */
    QILineEdit *pEditor = qobject_cast<QILineEdit*>(sender());
    AssertPtrReturnVoid(pEditor);

    /* Performs sender's validation: */
    revalidate(pEditor);
}

void UIUSBFilterDetailsEditor::prepare()
{
    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIUSBFilterDetailsEditor::sltRetranslateUI);

    /* Adjust dialog size: */
    adjustSize();
}

void UIUSBFilterDetailsEditor::prepareWidgets()
{
    /* Name field validation: */
    const QRegularExpression re1(".+");
    /* Integer field validation, supports hex ranges: */
    const QString strHexValue("((0[xX])?[0-9a-fA-F]{1,4})");
    const QString strRange = QString("(%1|-%1|%1-|%1-%1)").arg(strHexValue);
    const QString strRanges = QString("(%1(,%1)*)?").arg(strRange);
    const QRegularExpression re2(strRanges);

    /* Prepare main layout: */
    QGridLayout *pLayout = new QGridLayout(this);
    if (pLayout)
    {
        pLayout->setRowStretch(9, 1);

        /* Prepare name label: */
        m_pLabelName = new QLabel(this);
        if (m_pLabelName)
        {
            m_pLabelName->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelName, 0, 0);
        }
        /* Prepare name editor: */
        m_pEditorName = new QILineEdit(this);
        if (m_pEditorName)
        {
            if (m_pLabelName)
                m_pLabelName->setBuddy(m_pEditorName);
            m_pEditorName->setMinimumWidthByText(QString().fill('0', 32));
            m_pEditorName->setValidator(new QRegularExpressionValidator(re1, this));
            connect(m_pEditorName, &QLineEdit::textChanged,
                    this, &UIUSBFilterDetailsEditor::sltRevalidate);
            pLayout->addWidget(m_pEditorName, 0, 1);
        }

        /* Prepare vendor ID label: */
        m_pLabelVendorID = new QLabel(this);
        if (m_pLabelVendorID)
        {
            m_pLabelVendorID->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelVendorID, 1, 0);
        }
        /* Prepare vendor ID editor: */
        m_pEditorVendorID = new QILineEdit(this);
        if (m_pEditorVendorID)
        {
            if (m_pLabelVendorID)
                m_pLabelVendorID->setBuddy(m_pEditorVendorID);
            m_pEditorVendorID->setMinimumWidthByText(QString().fill('0', 8));
            m_pEditorVendorID->setValidator(new QRegularExpressionValidator(re2, this));
            connect(m_pEditorVendorID, &QLineEdit::textChanged,
                    this, &UIUSBFilterDetailsEditor::sltRevalidate);
            pLayout->addWidget(m_pEditorVendorID, 1, 1);
        }

        /* Prepare product ID label: */
        m_pLabelProductID = new QLabel(this);
        if (m_pLabelProductID)
        {
            m_pLabelProductID->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelProductID, 2, 0);
        }
        /* Prepare product ID editor: */
        m_pEditorProductID = new QILineEdit(this);
        if (m_pEditorProductID)
        {
            if (m_pLabelProductID)
                m_pLabelProductID->setBuddy(m_pEditorProductID);
            m_pEditorProductID->setMinimumWidthByText(QString().fill('0', 8));
            m_pEditorProductID->setValidator(new QRegularExpressionValidator(re2, this));
            connect(m_pEditorProductID, &QLineEdit::textChanged,
                    this, &UIUSBFilterDetailsEditor::sltRevalidate);
            pLayout->addWidget(m_pEditorProductID, 2, 1);
        }

        /* Prepare revision label: */
        m_pLabelRevision = new QLabel(this);
        if (m_pLabelRevision)
        {
            m_pLabelRevision->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelRevision, 3, 0);
        }
        /* Prepare revision editor: */
        m_pEditorRevision = new QILineEdit(this);
        if (m_pEditorRevision)
        {
            if (m_pLabelRevision)
                m_pLabelRevision->setBuddy(m_pEditorRevision);
            m_pEditorRevision->setMinimumWidthByText(QString().fill('0', 8));
            m_pEditorRevision->setValidator(new QRegularExpressionValidator(re2, this));
            connect(m_pEditorRevision, &QLineEdit::textChanged,
                    this, &UIUSBFilterDetailsEditor::sltRevalidate);
            pLayout->addWidget(m_pEditorRevision, 3, 1);
        }

        /* Prepare manufacturer label: */
        m_pLabelManufacturer = new QLabel(this);
        if (m_pLabelManufacturer)
        {
            m_pLabelManufacturer->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelManufacturer, 4, 0);
        }
        /* Prepare manufacturer editor: */
        m_pEditorManufacturer = new QILineEdit(this);
        if (m_pEditorManufacturer)
        {
            if (m_pLabelManufacturer)
                m_pLabelManufacturer->setBuddy(m_pEditorManufacturer);
            m_pEditorManufacturer->setMinimumWidthByText(QString().fill('0', 8));
            pLayout->addWidget(m_pEditorManufacturer, 4, 1);
        }

        /* Prepare product label: */
        m_pLabelProduct = new QLabel(this);
        if (m_pLabelProduct)
        {
            m_pLabelProduct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelProduct, 5, 0);
        }
        /* Prepare product editor: */
        m_pEditorProduct = new QILineEdit(this);
        if (m_pEditorProduct)
        {
            if (m_pLabelProduct)
                m_pLabelProduct->setBuddy(m_pEditorProduct);
            m_pEditorProduct->setMinimumWidthByText(QString().fill('0', 8));
            pLayout->addWidget(m_pEditorProduct, 5, 1);
        }

        /* Prepare serial NO label: */
        m_pLabelSerialNo = new QLabel(this);
        if (m_pLabelSerialNo)
        {
            m_pLabelSerialNo->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelSerialNo, 6, 0);
        }
        /* Prepare serial NO editor: */
        m_pEditorSerialNo = new QILineEdit(this);
        if (m_pEditorSerialNo)
        {
            if (m_pLabelSerialNo)
                m_pLabelSerialNo->setBuddy(m_pEditorSerialNo);
            m_pEditorSerialNo->setMinimumWidthByText(QString().fill('0', 8));
            pLayout->addWidget(m_pEditorSerialNo, 6, 1);
        }

        /* Prepare port label: */
        m_pLabelPort = new QLabel(this);
        if (m_pLabelPort)
        {
            m_pLabelPort->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelPort, 7, 0);
        }
        /* Prepare port editor: */
        m_pEditorPort = new QILineEdit(this);
        if (m_pEditorPort)
        {
            if (m_pLabelPort)
                m_pLabelPort->setBuddy(m_pEditorPort);
            m_pEditorPort->setMinimumWidthByText(QString().fill('0', 8));
            m_pEditorPort->setValidator(new QRegularExpressionValidator(re2, this));
            connect(m_pEditorPort, &QLineEdit::textChanged,
                    this, &UIUSBFilterDetailsEditor::sltRevalidate);
            pLayout->addWidget(m_pEditorPort, 7, 1);
        }

        /* Prepare remote label: */
        m_pLabelRemote = new QLabel(this);
        if (m_pLabelRemote)
        {
            m_pLabelRemote->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayout->addWidget(m_pLabelRemote, 8, 0);
        }
        /* Prepare remote combo: */
        m_pComboRemote = new QComboBox(this);
        if (m_pComboRemote)
        {
            if (m_pLabelRemote)
                m_pLabelRemote->setBuddy(m_pComboRemote);
            m_pComboRemote->addItem(QString(), QVariant::fromValue(UIRemoteMode_Any)); /* Any */
            m_pComboRemote->addItem(QString(), QVariant::fromValue(UIRemoteMode_On));  /* Yes */
            m_pComboRemote->addItem(QString(), QVariant::fromValue(UIRemoteMode_Off)); /* No */
            pLayout->addWidget(m_pComboRemote, 8, 1);
        }

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox(this);
        if (m_pButtonBox)
        {
            m_pButtonBox->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
            pLayout->addWidget(m_pButtonBox, 10, 0, 1, 2);
        }
    }
}

void UIUSBFilterDetailsEditor::prepareConnections()
{
    if (m_pButtonBox)
    {
        connect(m_pButtonBox, &QIDialogButtonBox::accepted,
                this, &UIUSBFilterDetailsEditor::accept);
        connect(m_pButtonBox, &QIDialogButtonBox::rejected,
                this, &UIUSBFilterDetailsEditor::reject);
    }
}

void UIUSBFilterDetailsEditor::revalidate(QILineEdit *pEditor)
{
    /* Acquire current validator: */
    const QValidator *pValidator = pEditor->validator();
    AssertPtrReturnVoid(pValidator);

    /* Validate current text: */
    QString strText = pEditor->text();
    int iPos = 0;
    const QValidator::State enmState = pValidator->validate(strText, iPos);

    /* Store current validation verdict: */
    m_valid[pEditor] = enmState == QValidator::Acceptable;

    /* Calculate overall validation result: */
    bool fValid = true;
    foreach (bool fValidOne, m_valid.values())
        if (!fValidOne)
        {
            fValid = false;
            break;
        }

    /* Enable/disable button-box Ok button accordingly: */
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(fValid);
}

/* static */
QString UIUSBFilterDetailsEditor::wiped(const QString &strString)
{
    return strString.isEmpty() ? QString() : strString;
}
