/* $Id: UIWizardNewVDSizeLocationPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVDSizeLocationPage class implementation.
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
#include <QDir>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIWizardNewVDSizeLocationPage.h"
#include "UIWizardNewVD.h"
#include "UIGlobalSession.h"
#include "UINotificationCenter.h"
#include "UIWizardDiskEditors.h"

/* COM includes: */
#include "CSystemProperties.h"

UIWizardNewVDSizeLocationPage::UIWizardNewVDSizeLocationPage(qulonglong uDiskMinimumSize)
    : m_pMediumSizePathGroup(0)
    , m_uMediumSizeMin(uDiskMinimumSize)
    , m_uMediumSizeMax(gpGlobalSession->virtualBox().GetSystemProperties().GetInfoVDSize())
{
    prepare();
}

void UIWizardNewVDSizeLocationPage::prepare()
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    AssertReturnVoid(pMainLayout);
    m_pMediumSizePathGroup = new UIMediumSizeAndPathGroupBox(false /* fExpertMode */, 0 /* parent */, m_uMediumSizeMin);
    connect(m_pMediumSizePathGroup, &UIMediumSizeAndPathGroupBox::sigMediumSizeChanged,
            this, &UIWizardNewVDSizeLocationPage::sltMediumSizeChanged);
    connect(m_pMediumSizePathGroup, &UIMediumSizeAndPathGroupBox::sigMediumPathChanged,
            this, &UIWizardNewVDSizeLocationPage::sltMediumPathChanged);
    connect(m_pMediumSizePathGroup, &UIMediumSizeAndPathGroupBox::sigMediumLocationButtonClicked,
            this, &UIWizardNewVDSizeLocationPage::sltSelectLocationButtonClicked);
    pMainLayout->addWidget(m_pMediumSizePathGroup);
    pMainLayout->addStretch();
    sltRetranslateUI();
}

void UIWizardNewVDSizeLocationPage::sltSelectLocationButtonClicked()
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard);
    QString strSelectedPath =
        UIWizardDiskEditors::openFileDialogForDiskFile(pWizard->mediumPath(), pWizard->mediumFormat(),
                                                                pWizard->deviceType(), pWizard);

    if (strSelectedPath.isEmpty())
        return;
    QString strMediumPath =
        UIWizardDiskEditors::appendExtension(strSelectedPath,
                                              UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), pWizard->deviceType()));
    QFileInfo mediumPath(strMediumPath);
    m_pMediumSizePathGroup->setMediumFilePath(QDir::toNativeSeparators(mediumPath.absoluteFilePath()));
}

void UIWizardNewVDSizeLocationPage::sltMediumSizeChanged(qulonglong uSize)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVD>());
    m_userModifiedParameters << "MediumSize";
    wizardWindow<UIWizardNewVD>()->setMediumSize(uSize);
    emit completeChanged();
}

void UIWizardNewVDSizeLocationPage::sltMediumPathChanged(const QString &strPath)
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard);
    m_userModifiedParameters << "MediumPath";
    QString strMediumPath =
        UIWizardDiskEditors::appendExtension(strPath,
                                              UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), pWizard->deviceType()));
    pWizard->setMediumPath(strMediumPath);
    emit completeChanged();
}

void UIWizardNewVDSizeLocationPage::sltRetranslateUI()
{
    setTitle(UIWizardNewVD::tr("Location and size of the disk image"));
}

void UIWizardNewVDSizeLocationPage::initializePage()
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard && m_pMediumSizePathGroup);

    QString strExtension = UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), pWizard->deviceType());
    QString strMediumFilePath;
    /* Initialize the medium file path with default name and path if user has not exclusively modified them yet: */
    if (!m_userModifiedParameters.contains("MediumPath"))
    {
        strMediumFilePath =
            UIWizardDiskEditors::constructMediumFilePath(UIWizardDiskEditors::appendExtension(pWizard->defaultName(),
                                                                                              strExtension), pWizard->defaultPath());
    }
    /* Initialize the medium file path with file path and file name from the location editor. This part is to update the
     * file extention correctly in case user has gone back and changed the file format after modifying medium file path: */
    else
        strMediumFilePath =
            UIWizardDiskEditors::constructMediumFilePath(UIWizardDiskEditors::appendExtension(m_pMediumSizePathGroup->mediumName(),
                                                                                                strExtension), m_pMediumSizePathGroup->mediumPath());
    m_pMediumSizePathGroup->blockSignals(true);
    m_pMediumSizePathGroup->setMediumFilePath(strMediumFilePath);
    m_pMediumSizePathGroup->blockSignals(false);
    pWizard->setMediumPath(m_pMediumSizePathGroup->mediumFilePath());

    if (!m_userModifiedParameters.contains("MediumSize"))
    {
        m_pMediumSizePathGroup->blockSignals(true);
        qulonglong uDefaultSize = pWizard->defaultSize();
        m_pMediumSizePathGroup->setMediumSize(uDefaultSize > m_uMediumSizeMin && uDefaultSize < m_uMediumSizeMax ? uDefaultSize : m_uMediumSizeMin);
        m_pMediumSizePathGroup->blockSignals(false);
        pWizard->setMediumSize(m_pMediumSizePathGroup->mediumSize());
    }
    sltRetranslateUI();
}

bool UIWizardNewVDSizeLocationPage::isComplete() const
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturn(pWizard, false);
    if (pWizard->mediumPath().isEmpty())
        return false;
    if (pWizard->mediumSize() > m_uMediumSizeMax || pWizard->mediumSize() < m_uMediumSizeMin)
        return false;
    if (!m_pMediumSizePathGroup->filePathUnique() || !m_pMediumSizePathGroup->pathExists())
        return false;
    return true;
}

bool UIWizardNewVDSizeLocationPage::validatePage()
{
    bool fResult = true;
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturn(pWizard, false);
    /* Make sure such file doesn't exist already: */
    const QString strMediumPath(pWizard->mediumPath());
    fResult = !QFileInfo(strMediumPath).exists();
    if (!fResult)
    {
        UINotificationMessage::cannotOverwriteMediumStorage(strMediumPath, wizard()->notificationCenter());
        return fResult;
    }

    /* Make sure we are passing FAT size limitation: */
    fResult = UIWizardDiskEditors::checkFATSizeLimitation(pWizard->mediumVariant(),
                                     pWizard->mediumPath(),
                                     pWizard->mediumSize());
    if (!fResult)
    {
        UINotificationMessage::cannotCreateMediumStorageInFAT(strMediumPath, wizard()->notificationCenter());
        return fResult;
    }

    fResult = pWizard->createVirtualDisk();
    return fResult;
}
