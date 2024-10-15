/* $Id: QIMessageBox.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIMessageBox class implementation.
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
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIArrowSplitter.h"
#include "QIDialogButtonBox.h"
#include "QIMessageBox.h"
#include "QIRichTextLabel.h"
#include "UICommon.h"
#include "UIIconPool.h"
#include "UIHelpBrowserDialog.h"
#include "UIMessageCenter.h"

/* Other VBox includes: */
#include <iprt/assert.h>


QIMessageBox::QIMessageBox(const QString &strTitle, const QString &strMessage, AlertIconType iconType,
                           int iButton1 /* = 0*/, int iButton2 /* = 0*/, int iButton3 /* = 0*/, QWidget *pParent /* = 0*/,
                           const QString &strHelpKeyword /* = QString() */)
    : QIDialog(pParent)
    , m_strTitle(strTitle)
    , m_iconType(iconType)
    , m_pLabelIcon(0)
    , m_strMessage(strMessage)
    , m_pLabelText(0)
    , m_pFlagCheckBox(0)
    , m_pDetailsContainer(0)
    , m_iButton1(iButton1)
    , m_iButton2(iButton2)
    , m_iButton3(iButton3)
    , m_iButtonEsc(0)
    , m_pButton1(0)
    , m_pButton2(0)
    , m_pButton3(0)
    , m_pButtonHelp(0)
    , m_pButtonBox(0)
    , m_strHelpKeyword(strHelpKeyword)
    , m_fDone(false)
{
    /* Prepare: */
    prepare();
}

void QIMessageBox::setDetailsText(const QString &strText)
{
    /* Make sure details-text is NOT empty: */
    AssertReturnVoid(!strText.isEmpty());

    /* Split details into paragraphs: */
    QStringList paragraphs(strText.split("<!--EOP-->", Qt::SkipEmptyParts));
    /* Make sure details-text has at least one paragraph: */
    AssertReturnVoid(!paragraphs.isEmpty());

    /* Enumerate all the paragraphs: */
    QStringPairList details;
    foreach (const QString &strParagraph, paragraphs)
    {
        /* Split each paragraph into pairs: */
        QStringList parts(strParagraph.split("<!--EOM-->", Qt::KeepEmptyParts));
        /* Make sure each paragraph consist of 2 parts: */
        AssertReturnVoid(parts.size() == 2);
        /* Append each pair into details-list: */
        details << QStringPair(parts[0], parts[1]);
    }

    /* Pass details-list to details-container: */
    m_pDetailsContainer->setDetails(details);
    /* Update details-container finally: */
    updateDetailsContainer();
}

bool QIMessageBox::flagChecked() const
{
    return m_pFlagCheckBox->isChecked();
}

void QIMessageBox::setFlagChecked(bool fChecked)
{
    m_pFlagCheckBox->setChecked(fChecked);
}

void QIMessageBox::setFlagText(const QString &strFlagText)
{
    /* Pass text to flag check-box: */
    m_pFlagCheckBox->setText(strFlagText);
    /* Update flag check-box finally: */
    updateCheckBox();
}

void QIMessageBox::setButtonText(int iButton, const QString &strText)
{
    switch (iButton)
    {
        case 0: if (m_pButton1) m_pButton1->setText(strText); break;
        case 1: if (m_pButton2) m_pButton2->setText(strText); break;
        case 2: if (m_pButton3) m_pButton3->setText(strText); break;
        default: break;
    }
}

void QIMessageBox::polishEvent(QShowEvent *pPolishEvent)
{
    /* Call to base-class: */
    QIDialog::polishEvent(pPolishEvent);

    /* Update size finally: */
    sltUpdateSize();
}

void QIMessageBox::closeEvent(QCloseEvent *pCloseEvent)
{
    if (m_fDone)
        pCloseEvent->accept();
    else
    {
        pCloseEvent->ignore();
        reject();
    }
}

void QIMessageBox::sltUpdateSize()
{
    /* Fix minimum possible size: */
    setFixedSize(minimumSizeHint());
}

void QIMessageBox::sltCopy() const
{
    /* Create the error string with all errors. First the html version. */
    QString strError = "<html><body><p>" + m_strMessage + "</p>";
    foreach (const QStringPair &pair, m_pDetailsContainer->details())
        strError += pair.first + pair.second + "<br>";
    strError += "</body></html>";
    strError.remove(QRegularExpression("</+qt>"));
    strError = strError.replace(QRegularExpression("&nbsp;"), " ");
    /* Create a new mime data object holding both the html and the plain text version. */
    QMimeData *pMimeData = new QMimeData();
    pMimeData->setHtml(strError);
    /* Replace all the html entities. */
    strError = strError.replace(QRegularExpression("<br>|</tr>"), "\n");
    strError = strError.replace(QRegularExpression("</p>"), "\n\n");
    strError = strError.remove(QRegularExpression("<[^>]*>"));
    pMimeData->setText(strError);
    /* Add the mime data to the global clipboard. */
    QClipboard *pClipboard = QApplication::clipboard();
    pClipboard->setMimeData(pMimeData);
}

void QIMessageBox::reject()
{
    if (m_iButtonEsc)
    {
        QDialog::reject();
        setResult(m_iButtonEsc & AlertButtonMask);
    }
}

void QIMessageBox::prepare()
{
    /* Set caption: */
    setWindowTitle(m_strTitle);

    /* Create main-layout: */
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertPtrReturnVoid(pMainLayout);
    {
        /* Configure main-layout: */
#ifdef VBOX_WS_MAC
        pMainLayout->setContentsMargins(40, 20, 40, 20);
        pMainLayout->setSpacing(15);
#else
        pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) * 2);
#endif
        /* Create top-layout: */
        QHBoxLayout *pTopLayout = new QHBoxLayout;
        AssertPtrReturnVoid(pTopLayout);
        {
            /* Configure top-layout: */
            pTopLayout->setContentsMargins(0, 0, 0, 0);
            /* Create icon-label: */
            m_pLabelIcon = new QLabel;
            AssertPtrReturnVoid(m_pLabelIcon);
            {
                /* Configure icon-label: */
                m_pLabelIcon->setPixmap(standardPixmap(m_iconType, this));
                m_pLabelIcon->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
                m_pLabelIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
                /* Add icon-label into top-layout: */
                pTopLayout->addWidget(m_pLabelIcon);
            }
            /* Create text-label: */
            m_pLabelText = new QIRichTextLabel;
            AssertPtrReturnVoid(m_pLabelText);
            {
                /* Configure text-label: */
                m_pLabelText->setText(compressLongWords(m_strMessage));
                /* Add text-label into top-layout: */
                pTopLayout->addWidget(m_pLabelText);
            }
            /* Add top-layout into main-layout: */
            pMainLayout->addLayout(pTopLayout);
        }
        /* Create details-container: */
        m_pDetailsContainer = new QIArrowSplitter;
        AssertPtrReturnVoid(m_pDetailsContainer);
        {
            /* Configure container: */
            connect(m_pDetailsContainer, &QIArrowSplitter::sigSizeHintChange,
                    this, &QIMessageBox::sltUpdateSize);
            /* Add details-container into main-layout: */
            pMainLayout->addWidget(m_pDetailsContainer);
            /* Update details-container finally: */
            updateDetailsContainer();
        }
        /* Create flag check-box: */
        m_pFlagCheckBox = new QCheckBox;
        AssertPtrReturnVoid(m_pFlagCheckBox);
        {
            /* Configure flag check-box: */
            m_pFlagCheckBox->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            /* Add flag check-box into main-layout: */
            pMainLayout->addWidget(m_pFlagCheckBox, 0, Qt::AlignHCenter | Qt::AlignVCenter);
            /* Update flag check-box finally: */
            updateCheckBox();
        }
        /* Create button-box: */
        m_pButtonBox = new QIDialogButtonBox;
        AssertPtrReturnVoid(m_pButtonBox);
        {
            /* Configure button-box: */
            m_pButtonBox->setCenterButtons(true);
            m_pButton1 = createButton(m_iButton1);
            if (m_pButton1)
                connect(m_pButton1, &QPushButton::clicked, this, &QIMessageBox::sltDone1);
            m_pButton2 = createButton(m_iButton2);
            if (m_pButton2)
                connect(m_pButton2, &QPushButton::clicked, this, &QIMessageBox::sltDone2);
            m_pButton3 = createButton(m_iButton3);
            if (m_pButton3)
                connect(m_pButton3, &QPushButton::clicked, this, &QIMessageBox::sltDone3);
            /* Create the help button and connect it to relevant slot in case a help word is supplied: */
            if (!m_strHelpKeyword.isEmpty())
            {
                m_pButtonHelp = createButton(AlertButton_Help);
                if (m_pButtonHelp)
                {
                    uiCommon().setHelpKeyword(m_pButtonHelp, m_strHelpKeyword);
                    connect(m_pButtonHelp, &QPushButton::clicked, m_pButtonBox, &QIDialogButtonBox::sltHandleHelpRequest);
                }
            }

            /* Make sure Escape button always set: */
            Assert(m_iButtonEsc);
            /* If this is a critical message add a "Copy to clipboard" button: */
            if (m_iconType == AlertIconType_Critical)
            {
                QPushButton *pCopyButton = createButton(AlertButton_Copy);
                pCopyButton->setToolTip(tr("Copy all errors to the clipboard"));
                connect(pCopyButton, &QPushButton::clicked, this, &QIMessageBox::sltCopy);
            }
            /* Add button-box into main-layout: */
            pMainLayout->addWidget(m_pButtonBox);

            /* Prepare focus. It is important to prepare focus after adding button-box to the layout as
             * parenting the button-box to the QDialog changes default button focus by Qt: */
            prepareFocus();
        }
    }
}

void QIMessageBox::prepareFocus()
{
    /* Configure default button and focus: */
    if (m_pButton1 && (m_iButton1 & AlertButtonOption_Default))
    {
        m_pButton1->setDefault(true);
        m_pButton1->setFocus();
    }
    if (m_pButton2 && (m_iButton2 & AlertButtonOption_Default))
    {
        m_pButton2->setDefault(true);
        m_pButton2->setFocus();
    }
    if (m_pButton3 && (m_iButton3 & AlertButtonOption_Default))
    {
        m_pButton3->setDefault(true);
        m_pButton3->setFocus();
    }
}

QPushButton *QIMessageBox::createButton(int iButton)
{
    /* Not for AlertButton_NoButton: */
    if (iButton == 0)
        return 0;

    /* Prepare button text & role: */
    QString strText;
    QDialogButtonBox::ButtonRole role;
    switch (iButton & AlertButtonMask)
    {
        case AlertButton_Ok:      strText = tr("OK");     role = QDialogButtonBox::AcceptRole; break;
        case AlertButton_Cancel:  strText = tr("Cancel"); role = QDialogButtonBox::RejectRole; break;
        case AlertButton_Choice1: strText = tr("Yes");    role = QDialogButtonBox::YesRole; break;
        case AlertButton_Choice2: strText = tr("No");     role = QDialogButtonBox::NoRole; break;
        case AlertButton_Copy:    strText = tr("Copy");   role = QDialogButtonBox::ActionRole; break;
        case AlertButton_Help:    strText = tr("Help");   role = QDialogButtonBox::HelpRole; break;
        default:
            AssertMsgFailed(("Type %d is not supported!", iButton));
            return 0;
    }

    /* Create push-button: */
    QPushButton *pButton = m_pButtonBox->addButton(strText, role);

    /* Configure <escape> button: */
    if (iButton & AlertButtonOption_Escape)
        m_iButtonEsc = iButton & AlertButtonMask;

    /* Return button: */
    return pButton;
}

void QIMessageBox::updateDetailsContainer()
{
    /* Details-container with details is always visible: */
    m_pDetailsContainer->setVisible(!m_pDetailsContainer->details().isEmpty());
    /* Update size: */
    sltUpdateSize();
}

void QIMessageBox::updateCheckBox()
{
    /* Flag check-box with text is always visible: */
    m_pFlagCheckBox->setVisible(!m_pFlagCheckBox->text().isEmpty());
    /* Update size: */
    sltUpdateSize();
}

/* static */
QPixmap QIMessageBox::standardPixmap(AlertIconType iconType, QWidget *pWidget /* = 0*/)
{
    /* Prepare standard icon: */
    QIcon icon;
    switch (iconType)
    {
        case AlertIconType_Information:    icon = UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_MessageBoxInformation, pWidget); break;
        case AlertIconType_Warning:        icon = UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_MessageBoxWarning, pWidget); break;
        case AlertIconType_Critical:       icon = UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_MessageBoxCritical, pWidget); break;
        case AlertIconType_Question:       icon = UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_MessageBoxQuestion, pWidget); break;
        case AlertIconType_GuruMeditation: icon = UIIconPool::iconSet(":/meditation_32px.png"); break;
        default: break;
    }
    /* Return empty pixmap if nothing found: */
    if (icon.isNull())
        return QPixmap();
    /* Return pixmap of standard size if possible: */
    QStyle *pStyle = pWidget ? pWidget->style() : QApplication::style();
    int iSize = pStyle->pixelMetric(QStyle::PM_MessageBoxIconSize, 0, pWidget);
    return icon.pixmap(iSize, iSize);
}

/* static */
QString QIMessageBox::compressLongWords(QString strText)
{
    // WORKAROUND:
    // The idea is to compress long words of more than 100 symbols in size consisting of alphanumeric
    // characters with ellipsiss using the following template:
    // "[50 first symbols]...[50 last symbols]"
    const QRegularExpression re("[a-zA-Z0-9]{101,}");
    QRegularExpressionMatch mt = re.match(strText);
    int iPosition = mt.capturedStart();
    bool fChangeAllowed = iPosition != -1;
    while (fChangeAllowed)
    {
        QString strNewText = strText;
        const QString strFound = mt.captured();
        strNewText.replace(iPosition, strFound.size(), strFound.left(50) + "..." + strFound.right(50));
        fChangeAllowed = fChangeAllowed && strText != strNewText;
        strText = strNewText;
        mt = re.match(strText);
        iPosition = mt.capturedStart();
        fChangeAllowed = fChangeAllowed && iPosition != -1;
    }
    return strText;
}
