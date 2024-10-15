/* $Id: UIFDCreationDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFDCreationDialog class implementation.
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

/* Qt includes */
#include <QCheckBox>
#include <QDir>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

/* GUI includes */
#include "QIDialogButtonBox.h"
#include "UICommon.h"
#include "UIFDCreationDialog.h"
#include "UIFilePathSelector.h"
#include "UIGlobalSession.h"
#include "UIIconPool.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UIMediumTools.h"
#include "UIMessageCenter.h"
#include "UINotificationCenter.h"
#include "UIModalWindowManager.h"
#include "UITranslationEventListener.h"

/* COM includes: */
#include "CSystemProperties.h"
#include "CMediumFormat.h"


UIFDCreationDialog::UIFDCreationDialog(QWidget *pParent,
                                       const QString &strDefaultFolder,
                                       const QString &strMachineName /* = QString() */)
    : QDialog(pParent)
    , m_strDefaultFolder(strDefaultFolder)
    , m_strMachineName(strMachineName)
    , m_pPathLabel(0)
    , m_pFilePathSelector(0)
    , m_pSizeLabel(0)
    , m_pSizeCombo(0)
    , m_pFormatCheckBox(0)
    , m_pButtonBox(0)
{
    prepare();
}

QUuid UIFDCreationDialog::mediumID() const
{
    return m_uMediumID;
}

/* static */
QUuid UIFDCreationDialog::createFloppyDisk(QWidget *pParent, const QString &strDefaultFolder /* QString() */,
                                           const QString &strMachineName /* = QString() */ )
{
    QString strStartPath(strDefaultFolder);

    if (strStartPath.isEmpty())
        strStartPath = UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_Floppy);

    QWidget *pDialogParent = windowManager().realParentWindow(pParent);

    UIFDCreationDialog *pDialog = new UIFDCreationDialog(pParent, strStartPath, strMachineName);
    if (!pDialog)
        return QUuid();
    windowManager().registerNewParent(pDialog, pDialogParent);

    if (pDialog->exec())
    {
        QUuid uMediumID = pDialog->mediumID();
        delete pDialog;
        return uMediumID;
    }
    delete pDialog;
    return QUuid();
}

void UIFDCreationDialog::accept()
{
    /* Make Ok button disabled first of all: */
    m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    /* Acquire medium path & formats: */
    const QString strMediumLocation = m_pFilePathSelector->path();
    const QVector<CMediumFormat> mediumFormats = getFormatsForDeviceType(KDeviceType_Floppy);
    /* Make sure we have both path and formats selected: */
    if (strMediumLocation.isEmpty() || mediumFormats.isEmpty())
        return;

    /* Get VBox for further activities: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Create medium: */
    CMedium comMedium = comVBox.CreateMedium(mediumFormats[0].GetName(), strMediumLocation,
                                             KAccessMode_ReadWrite, KDeviceType_Floppy);
    if (!comVBox.isOk())
    {
        msgCenter().cannotCreateMediumStorage(comVBox, strMediumLocation, this);
        return;
    }

    /* Compose medium storage variants: */
    QVector<KMediumVariant> variants(1, KMediumVariant_Fixed);
    /* Decide if disk formatting is required: */
    if (m_pFormatCheckBox && m_pFormatCheckBox->checkState() == Qt::Checked)
        variants.push_back(KMediumVariant_Formatted);

    /* Create medium storage, asynchronously: */
    UINotificationProgressMediumCreate *pNotification =
        new UINotificationProgressMediumCreate(comMedium, m_pSizeCombo->currentData().toLongLong(), variants);
    connect(pNotification, &UINotificationProgressMediumCreate::sigMediumCreated,
            gpMediumEnumerator, &UIMediumEnumerator::sltHandleMediumCreated);
    connect(pNotification, &UINotificationProgressMediumCreate::sigMediumCreated,
            this, &UIFDCreationDialog::sltHandleMediumCreated);
    gpNotificationCenter->append(pNotification);
}

void UIFDCreationDialog::sltRetranslateUI()
{
    if (m_strMachineName.isEmpty())
        setWindowTitle(QString("%1").arg(tr("Floppy Disk Creator")));
    else
        setWindowTitle(QString("%1 - %2").arg(m_strMachineName).arg(tr("Floppy Disk Creator")));
    if (m_pPathLabel)
        m_pPathLabel->setText(tr("File &Path:"));
    if (m_pSizeLabel)
    {
        m_pSizeLabel->setText(tr("&Size:"));
        m_pSizeLabel->setToolTip(tr("Sets the size of the floppy disk."));
    }
    if (m_pButtonBox)
        m_pButtonBox->button(QDialogButtonBox::Ok)->setText("C&reate");
    if (m_pFormatCheckBox)
    {
        m_pFormatCheckBox->setText(tr("&Format disk as FAT12"));
        m_pFormatCheckBox->setToolTip(tr("Formats the floppy disk as FAT12."));
    }
    if (m_pSizeCombo)
    {
        m_pSizeCombo->setItemText(FDSize_2_88M, tr("2.88M"));
        m_pSizeCombo->setItemText(FDSize_1_44M, tr("1.44M"));
        m_pSizeCombo->setItemText(FDSize_1_2M, tr("1.2M"));
        m_pSizeCombo->setItemText(FDSize_720K, tr("720K"));
        m_pSizeCombo->setItemText(FDSize_360K, tr("360K"));
    }

    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Ok))
        m_pButtonBox->button(QDialogButtonBox::Ok)->setToolTip(tr("Create the disk and close this dialog."));

    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Cancel))
        m_pButtonBox->button(QDialogButtonBox::Cancel)->setToolTip(tr("Cancel"));
}

void UIFDCreationDialog::sltHandleMediumCreated(const CMedium &comMedium)
{
    /* Store the ID of the newly created medium: */
    m_uMediumID = comMedium.GetId();

    /* Close the dialog now: */
    QDialog::accept();
}

void UIFDCreationDialog::sltPathChanged(const QString &strPath)
{
    bool fIsFileUnique = checkFilePath(strPath);
    m_pFilePathSelector->mark(!fIsFileUnique, tr("File already exists"), tr("File path is valid"));

    if (m_pButtonBox && m_pButtonBox->button(QDialogButtonBox::Ok))
        m_pButtonBox->button(QDialogButtonBox::Ok)->setEnabled(fIsFileUnique);
}

void UIFDCreationDialog::prepare()
{
#ifndef VBOX_WS_MAC
    /* Assign window icon: */
    setWindowIcon(UIIconPool::iconSetFull(":/fd_add_32px.png", ":/fd_add_16px.png"));
#endif

    setWindowModality(Qt::WindowModal);
    setSizeGripEnabled(false);

    /* Prepare main layout: */
    QGridLayout *pLayoutMain = new QGridLayout(this);
    if (pLayoutMain)
    {
        /* Prepare path label: */
        m_pPathLabel = new QLabel(this);
        if (m_pPathLabel)
        {
            m_pPathLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayoutMain->addWidget(m_pPathLabel, 0, 0);
        }
        /* Prepare file path selector: */
        m_pFilePathSelector = new UIFilePathSelector(this);
        if (m_pFilePathSelector)
        {
            m_pFilePathSelector->setMode(UIFilePathSelector::Mode_File_Save);
            const QString strFilePath = getDefaultFilePath();
            m_pFilePathSelector->setDefaultPath(strFilePath);
            m_pFilePathSelector->setPath(strFilePath);

            pLayoutMain->addWidget(m_pFilePathSelector, 0, 1, 1, 3);
            connect(m_pFilePathSelector, &UIFilePathSelector::pathChanged,
                    this, &UIFDCreationDialog::sltPathChanged);
            if (m_pPathLabel)
                m_pPathLabel->setBuddy(m_pFilePathSelector);
        }

        /* Prepare size label: */
        m_pSizeLabel = new QLabel(this);
        if (m_pSizeLabel)
        {
            m_pSizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            pLayoutMain->addWidget(m_pSizeLabel, 1, 0);
        }
        /* Prepare size combo: */
        m_pSizeCombo = new QComboBox(this);
        if (m_pSizeCombo)
        {
            m_pSizeCombo->insertItem(FDSize_2_88M, "2.88M", 2949120);
            m_pSizeCombo->insertItem(FDSize_1_44M, "1.44M", 1474560);
            m_pSizeCombo->insertItem(FDSize_1_2M, "1.2M", 1228800);
            m_pSizeCombo->insertItem(FDSize_720K, "720K", 737280);
            m_pSizeCombo->insertItem(FDSize_360K, "360K", 368640);
            m_pSizeCombo->setCurrentIndex(FDSize_1_44M);

            pLayoutMain->addWidget(m_pSizeCombo, 1, 1);

            if (m_pSizeLabel)
                m_pSizeLabel->setBuddy(m_pSizeCombo);
        }

        /* Prepare format check-box: */
        m_pFormatCheckBox = new QCheckBox;
        if (m_pFormatCheckBox)
        {
            m_pFormatCheckBox->setCheckState(Qt::Checked);
            pLayoutMain->addWidget(m_pFormatCheckBox, 2, 1, 1, 2);
        }

        /* Prepare button-box: */
        m_pButtonBox = new QIDialogButtonBox(QDialogButtonBox::Help | QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
        if (m_pButtonBox)
        {
            uiCommon().setHelpKeyword(m_pButtonBox->button(QDialogButtonBox::Help), "create-floppy-disk-image");
            connect(m_pButtonBox, &QDialogButtonBox::accepted, this, &UIFDCreationDialog::accept);
            connect(m_pButtonBox, &QDialogButtonBox::rejected, this, &UIFDCreationDialog::reject);
            connect(m_pButtonBox->button(QDialogButtonBox::Help), &QPushButton::pressed,
                    m_pButtonBox, &QIDialogButtonBox::sltHandleHelpRequest);
            pLayoutMain->addWidget(m_pButtonBox, 3, 0, 1, 3);
        }
    }

    /* Apply language settings: */
    sltRetranslateUI();

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIFDCreationDialog::sltRetranslateUI);

#ifdef VBOX_WS_MAC
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(minimumSize());
#endif /* VBOX_WS_MAC */

    /* Adjust dialog size: */
    adjustSize();
}

QString UIFDCreationDialog::getDefaultFilePath() const
{
    /* Prepare default file-path on the basis of passerd default folder: */
    QString strDefaultFilePath = m_strDefaultFolder;

    /* Make sure it's not empty if possible: */
    if (strDefaultFilePath.isEmpty())
        strDefaultFilePath = gpGlobalSession->virtualBox().GetSystemProperties().GetDefaultMachineFolder();
    if (strDefaultFilePath.isEmpty())
        return strDefaultFilePath;

    /* Append file-path with disc name, generate unique file-name if necessary: */
    QString strDiskname = !m_strMachineName.isEmpty() ? m_strMachineName : "NewFloppyDisk";
    strDiskname = UICommon::findUniqueFileName(m_strDefaultFolder, strDiskname);

    /* Append file-path with preferred extension finally: */
    const QString strPreferredExtension = UIMediumDefs::getPreferredExtensionForMedium(KDeviceType_Floppy);
    strDefaultFilePath = QDir(strDefaultFilePath).absoluteFilePath(strDiskname + "." + strPreferredExtension);

    /* Return default file-path: */
    return strDefaultFilePath;
}

bool UIFDCreationDialog::checkFilePath(const QString &strPath) const
{
    return !QFileInfo(strPath).exists();
}

/* static */
QVector<CMediumFormat> UIFDCreationDialog::getFormatsForDeviceType(KDeviceType enmDeviceType)
{
    CSystemProperties comSystemProperties = gpGlobalSession->virtualBox().GetSystemProperties();
    QVector<CMediumFormat> mediumFormats = comSystemProperties.GetMediumFormats();
    QVector<CMediumFormat> formatList;
    for (int i = 0; i < mediumFormats.size(); ++i)
    {
        /* File extensions */
        QVector<QString> fileExtensions;
        QVector<KDeviceType> deviceTypes;

        mediumFormats[i].DescribeFileExtensions(fileExtensions, deviceTypes);
        if (deviceTypes.contains(enmDeviceType))
            formatList.push_back(mediumFormats[i]);
    }
    return formatList;
}
