/* $Id: UIWizardNewVDExpertPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVDExpertPage class implementation.
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
#include <QDir>
#include <QGridLayout>
#include <QStyle>

/* GUI includes: */
#include "UIWizardDiskEditors.h"
#include "UIWizardNewVDExpertPage.h"
#include "UIWizardNewVD.h"
#include "UIGlobalSession.h"
#include "UINotificationCenter.h"

/* COM includes: */
#include "CSystemProperties.h"

UIWizardNewVDExpertPage::UIWizardNewVDExpertPage(qulonglong uDiskMinimumSize, KDeviceType enmDeviceType)
    : UINativeWizardPage()
    , m_pSizeAndPathGroup(0)
    , m_pFormatComboBox(0)
    , m_pVariantWidget(0)
    , m_pFormatVariantGroupBox(0)
    , m_uMediumSizeMin(_4M)
    , m_uMediumSizeMax(gpGlobalSession->virtualBox().GetSystemProperties().GetInfoVDSize())
{
    prepare(uDiskMinimumSize, enmDeviceType);
}

void UIWizardNewVDExpertPage::prepare(qulonglong uDiskMinimumSize, KDeviceType enmDeviceType)
{
    QVBoxLayout *pMainLayout = new QVBoxLayout(this);
    m_pSizeAndPathGroup = new UIMediumSizeAndPathGroupBox(true /* fExpertMode */, 0 /* parent */,uDiskMinimumSize);
    m_pFormatComboBox = new UIDiskFormatsComboBox(true /* fExpertMode */, enmDeviceType, 0);
    m_pVariantWidget = new UIDiskVariantWidget(0);

    m_pFormatVariantGroupBox = new QGroupBox;
    QGridLayout *pFormatVariantLayout = new QGridLayout(m_pFormatVariantGroupBox);
#ifdef VBOX_WS_MAC
    pFormatVariantLayout->setSpacing(2 * 5);
#else
    pFormatVariantLayout->setSpacing(2 * qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing));
#endif
    pFormatVariantLayout->setRowStretch(2, 1);
    pFormatVariantLayout->setColumnStretch(0, 1);
    pFormatVariantLayout->addWidget(m_pFormatComboBox, 0, 0);
    pFormatVariantLayout->addWidget(m_pVariantWidget, 0, 1, 2, 1);

    pMainLayout->addWidget(m_pSizeAndPathGroup);
    pMainLayout->addWidget(m_pFormatVariantGroupBox);

    connect(m_pFormatComboBox, &UIDiskFormatsComboBox::sigMediumFormatChanged,
            this, &UIWizardNewVDExpertPage::sltMediumFormatChanged);
    connect(m_pVariantWidget, &UIDiskVariantWidget::sigMediumVariantChanged,
            this, &UIWizardNewVDExpertPage::sltMediumVariantChanged);
    connect(m_pSizeAndPathGroup, &UIMediumSizeAndPathGroupBox::sigMediumLocationButtonClicked,
            this, &UIWizardNewVDExpertPage::sltSelectLocationButtonClicked);
    connect(m_pSizeAndPathGroup, &UIMediumSizeAndPathGroupBox::sigMediumSizeChanged,
            this, &UIWizardNewVDExpertPage::sltMediumSizeChanged);
    connect(m_pSizeAndPathGroup, &UIMediumSizeAndPathGroupBox::sigMediumPathChanged,
            this, &UIWizardNewVDExpertPage::sltMediumPathChanged);

    sltRetranslateUI();
}

void UIWizardNewVDExpertPage::sltMediumSizeChanged(qulonglong uSize)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVD>());
    wizardWindow<UIWizardNewVD>()->setMediumSize(uSize);
    emit completeChanged();
}

void UIWizardNewVDExpertPage::sltMediumPathChanged(const QString &strPath)
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard);
    QString strMediumPath =
        UIWizardDiskEditors::appendExtension(strPath,
                                             UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), pWizard->deviceType()));
    pWizard->setMediumPath(strMediumPath);
    emit completeChanged();
}

void UIWizardNewVDExpertPage::sltMediumVariantChanged(qulonglong uVariant)
{
    AssertReturnVoid(wizardWindow<UIWizardNewVD>());
    wizardWindow<UIWizardNewVD>()->setMediumVariant(uVariant);
    emit completeChanged();
}

void UIWizardNewVDExpertPage::sltMediumFormatChanged()
{
    AssertReturnVoid(m_pFormatComboBox);
    AssertReturnVoid(wizardWindow<UIWizardNewVD>());
    wizardWindow<UIWizardNewVD>()->setMediumFormat(m_pFormatComboBox->mediumFormat());
    updateDiskWidgetsAfterMediumFormatChange();
    completeChanged();
}

void UIWizardNewVDExpertPage::sltSelectLocationButtonClicked()
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard);
    CMediumFormat comMediumFormat(pWizard->mediumFormat());
    QString strSelectedPath =
        UIWizardDiskEditors::openFileDialogForDiskFile(pWizard->mediumPath(), comMediumFormat,
                                                       pWizard->deviceType(), pWizard);
    if (strSelectedPath.isEmpty())
        return;
    QString strMediumPath =
        UIWizardDiskEditors::appendExtension(strSelectedPath,
                                             UIWizardDiskEditors::defaultExtension(pWizard->mediumFormat(), pWizard->deviceType()));
    QFileInfo mediumPath(strMediumPath);
    m_pSizeAndPathGroup->setMediumFilePath(QDir::toNativeSeparators(mediumPath.absoluteFilePath()));
    emit completeChanged();
}

void UIWizardNewVDExpertPage::sltRetranslateUI()
{
    if (m_pFormatVariantGroupBox)
        m_pFormatVariantGroupBox->setTitle(UIWizardNewVD::tr("Hard Disk File &Type and Variant"));
}

void UIWizardNewVDExpertPage::initializePage()
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard);
    /* First set the medium format of the wizard: */
    AssertReturnVoid(m_pFormatComboBox);
    const CMediumFormat &comMediumFormat = m_pFormatComboBox->mediumFormat();
    AssertReturnVoid(!comMediumFormat.isNull());
    pWizard->setMediumFormat(comMediumFormat);

    QString strExtension = UIWizardDiskEditors::defaultExtension(comMediumFormat, pWizard->deviceType());
    QString strMediumFilePath =
        UIWizardDiskEditors::constructMediumFilePath(UIWizardDiskEditors::appendExtension(pWizard->defaultName(),
                                                                                          strExtension), pWizard->defaultPath());
    m_pSizeAndPathGroup->blockSignals(true);
    m_pSizeAndPathGroup->setMediumFilePath(strMediumFilePath);
    m_pSizeAndPathGroup->blockSignals(false);
    pWizard->setMediumPath(m_pSizeAndPathGroup->mediumFilePath());

    m_pSizeAndPathGroup->blockSignals(true);
    qulonglong uDefaultSize = pWizard->defaultSize();
    m_pSizeAndPathGroup->setMediumSize(uDefaultSize > m_uMediumSizeMin && uDefaultSize < m_uMediumSizeMax ? uDefaultSize : m_uMediumSizeMin);
    m_pSizeAndPathGroup->blockSignals(false);
    pWizard->setMediumSize(m_pSizeAndPathGroup->mediumSize());

    m_pVariantWidget->blockSignals(true);
    m_pVariantWidget->updateMediumVariantWidgetsAfterFormatChange(comMediumFormat);
    m_pVariantWidget->blockSignals(false);

    pWizard->setMediumVariant(m_pVariantWidget->mediumVariant());

    sltRetranslateUI();
}

bool UIWizardNewVDExpertPage::isComplete() const
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturn(pWizard, false);
    if (pWizard->mediumFormat().isNull())
        return false;
    if (pWizard->mediumVariant() == (qulonglong)KMediumVariant_Max)
        return false;
    if (pWizard->mediumPath().isEmpty())
        return false;
    if (pWizard->mediumSize() > m_uMediumSizeMax || pWizard->mediumSize() < m_uMediumSizeMin)
        return false;
    if (!m_pSizeAndPathGroup->filePathUnique() || !m_pSizeAndPathGroup->pathExists())
        return false;
    return true;
}

bool UIWizardNewVDExpertPage::validatePage()
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

void UIWizardNewVDExpertPage::updateDiskWidgetsAfterMediumFormatChange()
{
    UIWizardNewVD *pWizard = wizardWindow<UIWizardNewVD>();
    AssertReturnVoid(pWizard && m_pVariantWidget && m_pSizeAndPathGroup && m_pFormatComboBox);
    const CMediumFormat &comMediumFormat = pWizard->mediumFormat();
    AssertReturnVoid(!comMediumFormat.isNull());

    m_pVariantWidget->updateMediumVariantWidgetsAfterFormatChange(comMediumFormat);
    m_pSizeAndPathGroup->updateMediumPath(comMediumFormat, m_pFormatComboBox->formatExtensions(), pWizard->deviceType());
}
