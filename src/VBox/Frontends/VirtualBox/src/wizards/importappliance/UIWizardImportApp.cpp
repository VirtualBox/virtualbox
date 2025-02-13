/* $Id: UIWizardImportApp.cpp 108358 2025-02-13 14:26:13Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardImportApp class implementation.
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
#include <QLabel>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialog.h"
#include "QIFileDialog.h"
#include "UICommon.h"
#include "UIGlobalSession.h"
#include "UINotificationCenter.h"
#include "UIWizardImportApp.h"
#include "UIWizardImportAppPageExpert.h"
#include "UIWizardImportAppPageSettings.h"
#include "UIWizardImportAppPageSource.h"


/* Import license viewer: */
class UIImportLicenseViewer : public QIDialog
{
    Q_OBJECT;

public:

    /* Constructor: */
    UIImportLicenseViewer(QWidget *pParent)
        : QIDialog(pParent)
    {
        /* Create widgets: */
        QVBoxLayout *pMainLayout = new QVBoxLayout(this);
            pMainLayout->setContentsMargins(12, 12, 12, 12);
            m_pCaption = new QLabel(this);
                m_pCaption->setWordWrap(true);
            m_pLicenseText = new QTextEdit(this);
                m_pLicenseText->setReadOnly(true);
            m_pPrintButton = new QPushButton(this);
            m_pSaveButton = new QPushButton(this);
            m_pButtonBox = new QDialogButtonBox(QDialogButtonBox::No | QDialogButtonBox::Yes, Qt::Horizontal, this);
                m_pButtonBox->addButton(m_pPrintButton, QDialogButtonBox::ActionRole);
                m_pButtonBox->addButton(m_pSaveButton, QDialogButtonBox::ActionRole);
                m_pButtonBox->button(QDialogButtonBox::Yes)->setDefault(true);
        pMainLayout->addWidget(m_pCaption);
        pMainLayout->addWidget(m_pLicenseText);
        pMainLayout->addWidget(m_pButtonBox);

        /* Translate */
        sltRetranslateUI();

        /* Setup connections: */
        connect(m_pButtonBox, &QDialogButtonBox::rejected, this, &UIImportLicenseViewer::reject);
        connect(m_pButtonBox, &QDialogButtonBox::accepted, this, &UIImportLicenseViewer::accept);
        connect(m_pPrintButton, &QPushButton::clicked,     this, &UIImportLicenseViewer::sltPrint);
        connect(m_pSaveButton,  &QPushButton::clicked,     this, &UIImportLicenseViewer::sltSave);
    }

    /* Content setter: */
    void setContents(const QString &strName, const QString &strText)
    {
        m_strName = strName;
        m_strText = strText;
        sltRetranslateUI();
    }

private slots:

    /* Print stuff: */
    void sltPrint()
    {
        QPrinter printer;
        QPrintDialog pd(&printer, this);
        if (pd.exec() == QDialog::Accepted)
            m_pLicenseText->print(&printer);
    }

    /* Save stuff: */
    void sltSave()
    {
        QString fileName = QIFileDialog::getSaveFileName(uiCommon().documentsPath(), tr("Text (*.txt)"),
                                                         this, tr("Save license to file..."));
        if (!fileName.isEmpty())
        {
            QFile file(fileName);
            if (file.open(QFile::WriteOnly | QFile::Truncate))
            {
                QTextStream out(&file);
                out << m_pLicenseText->toPlainText();
            }
        }
    }

private:

    /* Translation stuff: */
    void sltRetranslateUI()
    {
        /* Translate dialog: */
        setWindowTitle(tr("Software License Agreement"));
        /* Translate widgets: */
        m_pCaption->setText(tr("<b>The virtual system \"%1\" requires that you agree to the terms and conditions "
                               "of the software license agreement shown below.</b><br /><br />Click <b>Agree</b> "
                               "to continue or click <b>Disagree</b> to cancel the import.").arg(m_strName));
        m_pLicenseText->setText(m_strText);
        m_pButtonBox->button(QDialogButtonBox::No)->setText(tr("&Disagree"));
        m_pButtonBox->button(QDialogButtonBox::Yes)->setText(tr("&Agree"));
        m_pPrintButton->setText(tr("&Print..."));
        m_pSaveButton->setText(tr("&Save..."));
    }

    /* Variables: */
    QLabel *m_pCaption;
    QTextEdit *m_pLicenseText;
    QDialogButtonBox *m_pButtonBox;
    QPushButton *m_pPrintButton;
    QPushButton *m_pSaveButton;
    QString m_strName;
    QString m_strText;
};


/*********************************************************************************************************************************
*   Class UIWizardImportApp implementation.                                                                                      *
*********************************************************************************************************************************/

UIWizardImportApp::UIWizardImportApp(QWidget *pParent,
                                     bool fImportFromOCIByDefault,
                                     const QString &strFileName)
    : UINativeWizard(pParent, WizardType_ImportAppliance, "ovf")
    , m_fImportFromOCIByDefault(fImportFromOCIByDefault)
    , m_strFileName(strFileName)
    , m_fSourceCloudOne(false)
    , m_enmMacAddressImportPolicy(MACAddressImportPolicy_MAX)
    , m_fImportHDsAsVDI(false)
{
#ifndef VBOX_WS_MAC
    /* Assign watermark: */
    setPixmapName(":/wizard_ovf_import.png");
#else /* VBOX_WS_MAC */
    /* Assign background image: */
    setPixmapName(":/wizard_ovf_import_bg.png");
#endif /* VBOX_WS_MAC */
}

bool UIWizardImportApp::setFile(const QString &strName)
{
    /* Clear object: */
    m_comLocalAppliance = CAppliance();

    if (strName.isEmpty())
        return false;

    /* Create an appliance object: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();
    CAppliance comAppliance = comVBox.CreateAppliance();
    if (!comVBox.isOk())
    {
        UINotificationMessage::cannotCreateAppliance(comVBox, notificationCenter());
        return false;
    }

    /* Read the file to appliance: */
    UINotificationProgressApplianceRead *pNotification = new UINotificationProgressApplianceRead(comAppliance, strName);
    if (!handleNotificationProgressNow(pNotification))
        return false;

    /* Now we have to interpret that stuff: */
    comAppliance.Interpret();
    if (!comAppliance.isOk())
    {
        UINotificationMessage::cannotInterpretAppliance(comAppliance, notificationCenter());
        return false;
    }

    /* Remember appliance: */
    m_comLocalAppliance = comAppliance;

    /* Success finally: */
    return true;
}

bool UIWizardImportApp::importAppliance()
{
    /* Check whether there was cloud source selected: */
    if (isSourceCloudOne())
    {
        /* Make sure cloud appliance valid: */
        AssertReturn(m_comCloudAppliance.isNotNull(), false);

        /* No options for cloud VMs for now: */
        QVector<KImportOptions> options;

        /* Import appliance: */
        UINotificationProgressApplianceImport *pNotification = new UINotificationProgressApplianceImport(m_comCloudAppliance,
                                                                                                         options);
        gpNotificationCenter->append(pNotification);

        /* Positive: */
        return true;
    }
    else
    {
        /* Check if there are license agreements the user must confirm: */
        QList < QPair <QString, QString> > licAgreements = licenseAgreements();
        if (!licAgreements.isEmpty())
        {
            for (int i = 0; i < licAgreements.size(); ++i)
            {
                UIImportLicenseViewer ilv(this);
                const QPair<QString, QString> &lic = licAgreements.at(i);
                ilv.setContents(lic.first, lic.second);
                if (ilv.exec() == QDialog::Rejected)
                    return false;
            }
        }

        /* Gather import options: */
        QVector<KImportOptions> options;
        switch (macAddressImportPolicy())
        {
            case MACAddressImportPolicy_KeepAllMACs: options.append(KImportOptions_KeepAllMACs); break;
            case MACAddressImportPolicy_KeepNATMACs: options.append(KImportOptions_KeepNATMACs); break;
            default: break;
        }
        if (isImportHDsAsVDI())
            options.append(KImportOptions_ImportToVDI);

        /* Import appliance: */
        UINotificationProgressApplianceImport *pNotification = new UINotificationProgressApplianceImport(m_comLocalAppliance,
                                                                                                         options);
        gpNotificationCenter->append(pNotification);

        /* Positive: */
        return true;
    }
}

void UIWizardImportApp::populatePages()
{
    /* Create corresponding pages: */
    switch (mode())
    {
        case WizardMode_Basic:
        {
            if (m_fImportFromOCIByDefault || m_strFileName.isEmpty())
                addPage(new UIWizardImportAppPageSource(m_fImportFromOCIByDefault, m_strFileName));
            addPage(new UIWizardImportAppPageSettings(m_strFileName));
            break;
        }
        case WizardMode_Expert:
        {
            addPage(new UIWizardImportAppPageExpert(m_fImportFromOCIByDefault, m_strFileName));
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid mode: %d", mode()));
            break;
        }
    }
}

void UIWizardImportApp::sltRetranslateUI()
{
    /* Call to base-class: */
    UINativeWizard::sltRetranslateUI();

    /* Translate wizard: */
    setWindowTitle(tr("Import Virtual Appliance"));
    /// @todo implement this?
    //setButtonText(QWizard::FinishButton, tr("Import"));
}

QList<QPair<QString, QString> > UIWizardImportApp::licenseAgreements() const
{
    QList<QPair<QString, QString> > list;

    foreach (CVirtualSystemDescription comVsd, m_comLocalAppliance.GetVirtualSystemDescriptions())
    {
        QVector<QString> strLicense;
        strLicense = comVsd.GetValuesByType(KVirtualSystemDescriptionType_License,
                                            KVirtualSystemDescriptionValueType_Original);
        if (!strLicense.isEmpty())
        {
            QVector<QString> strName;
            strName = comVsd.GetValuesByType(KVirtualSystemDescriptionType_Name,
                                             KVirtualSystemDescriptionValueType_Auto);
            list << QPair<QString, QString>(strName.first(), strLicense.first());
        }
    }

    return list;
}


#include "UIWizardImportApp.moc"
