/* $Id: UIApplianceUnverifiedCertificateViewer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIApplianceUnverifiedCertificateViewer class implementation.
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
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "UIApplianceUnverifiedCertificateViewer.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CCertificate.h"


UIApplianceUnverifiedCertificateViewer::UIApplianceUnverifiedCertificateViewer(QWidget *pParent,
                                                                               const CCertificate &comCertificate)
    : QIDialog(pParent)
    , m_comCertificate(comCertificate)
    , m_pTextLabel(0)
    , m_pTextBrowser(0)
{
    prepare();
}

void UIApplianceUnverifiedCertificateViewer::prepare()
{
    /* Create layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Create text-label: */
        m_pTextLabel = new QLabel;
        if (m_pTextLabel)
        {
            m_pTextLabel->setWordWrap(true);

            /* Add into layout: */
            pLayout->addWidget(m_pTextLabel);
        }

        /* Create text-browser: */
        m_pTextBrowser = new QTextBrowser;
        if (m_pTextBrowser)
        {
            m_pTextBrowser->setMinimumSize(500, 300);

            /* Add into layout: */
            pLayout->addWidget(m_pTextBrowser);
        }

        /* Create button-box: */
        QIDialogButtonBox *pButtonBox = new QIDialogButtonBox;
        if (pButtonBox)
        {
            pButtonBox->setStandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No);
            pButtonBox->button(QDialogButtonBox::Yes)->setShortcut(Qt::Key_Enter);
            //pButtonBox->button(QDialogButtonBox::No)->setShortcut(Qt::Key_Esc);
            connect(pButtonBox, &QIDialogButtonBox::accepted, this, &UIApplianceUnverifiedCertificateViewer::accept);
            connect(pButtonBox, &QIDialogButtonBox::rejected, this, &UIApplianceUnverifiedCertificateViewer::reject);

            /* Add into layout: */
            pLayout->addWidget(pButtonBox);
        }
    }

    /* Translate UI: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIApplianceUnverifiedCertificateViewer::sltRetranslateUI);
}

void UIApplianceUnverifiedCertificateViewer::sltRetranslateUI()
{
    /* Translate dialog title: */
    setWindowTitle(tr("Unverifiable Certificate! Continue?"));

    /* Translate text-label caption: */
    if (m_comCertificate.GetSelfSigned())
        m_pTextLabel->setText(tr("<b>The appliance is signed by an unverified self signed certificate issued by '%1'. "
                                 "We recommend to only proceed with the importing if you are sure you should trust this entity.</b>"
                                 ).arg(m_comCertificate.GetFriendlyName()));
    else
        m_pTextLabel->setText(tr("<b>The appliance is signed by an unverified certificate issued to '%1'. "
                                 "We recommend to only proceed with the importing if you are sure you should trust this entity.</b>"
                                 ).arg(m_comCertificate.GetFriendlyName()));

    /* Translate text-browser contents: */
    const QString strTemplateRow = tr("<tr><td>%1:</td><td>%2</td></tr>", "key: value");
    QString strTableContent;
    strTableContent += strTemplateRow.arg(tr("Issuer"),               QStringList(m_comCertificate.GetIssuerName().toList()).join(", "));
    strTableContent += strTemplateRow.arg(tr("Subject"),              QStringList(m_comCertificate.GetSubjectName().toList()).join(", "));
    strTableContent += strTemplateRow.arg(tr("Not Valid Before"),     m_comCertificate.GetValidityPeriodNotBefore());
    strTableContent += strTemplateRow.arg(tr("Not Valid After"),      m_comCertificate.GetValidityPeriodNotAfter());
    strTableContent += strTemplateRow.arg(tr("Serial Number"),        m_comCertificate.GetSerialNumber());
    strTableContent += strTemplateRow.arg(tr("Self-Signed"),          m_comCertificate.GetSelfSigned() ? tr("True") : tr("False"));
    strTableContent += strTemplateRow.arg(tr("Authority (CA)"),       m_comCertificate.GetCertificateAuthority() ? tr("True") : tr("False"));
//    strTableContent += strTemplateRow.arg(tr("Trusted"),              m_comCertificate.GetTrusted() ? tr("True") : tr("False"));
    strTableContent += strTemplateRow.arg(tr("Public Algorithm"),     tr("%1 (%2)", "value (clarification)").arg(m_comCertificate.GetPublicKeyAlgorithm()).arg(m_comCertificate.GetPublicKeyAlgorithmOID()));
    strTableContent += strTemplateRow.arg(tr("Signature Algorithm"),  tr("%1 (%2)", "value (clarification)").arg(m_comCertificate.GetSignatureAlgorithmName()).arg(m_comCertificate.GetSignatureAlgorithmOID()));
    strTableContent += strTemplateRow.arg(tr("X.509 Version Number"), QString::number(m_comCertificate.GetVersionNumber()));
    m_pTextBrowser->setText(QString("<table>%1</table>").arg(strTableContent));
}
