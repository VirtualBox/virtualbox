/* $Id: UIWizardNewVD.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVD class implementation.
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

/* GUI includes: */
#include "UICommon.h"
#include "UIGlobalSession.h"
#include "UIMediumEnumerator.h"
#include "UIMediumTools.h"
#include "UIModalWindowManager.h"
#include "UINotificationCenter.h"
#include "UIWizardNewVD.h"
#include "UIWizardNewVDFileTypePage.h"
#include "UIWizardNewVDVariantPage.h"
#include "UIWizardNewVDSizeLocationPage.h"
#include "UIWizardNewVDExpertPage.h"

/* COM includes: */
#include "CGuestOSType.h"

UIWizardNewVD::UIWizardNewVD(QWidget *pParent,
                             const QString &strDefaultName,
                             const QString &strDefaultPath,
                             qulonglong uDefaultSize)
    : UINativeWizard(pParent, WizardType_NewVD, "create-virtual-hard-disk-image" /* help keyword */)
    , m_strDefaultName(strDefaultName)
    , m_strDefaultPath(strDefaultPath)
    , m_uDefaultSize(uDefaultSize)
    , m_iMediumVariantPageIndex(-1)
    , m_enmDeviceType(KDeviceType_HardDisk)
{
#ifndef VBOX_WS_MAC
    /* Assign watermark: */
    setPixmapName(":/wizard_new_harddisk.png");
#else /* VBOX_WS_MAC */
    /* Assign background image: */
    setPixmapName(":/wizard_new_harddisk_bg.png");
#endif /* VBOX_WS_MAC */
}

UIWizardNewVD::UIWizardNewVD(QWidget *pParent, const QUuid &uMediumId)
    : UINativeWizard(pParent, WizardType_CloneVD)
    , m_iMediumVariantPageIndex(-1)
{
#ifndef VBOX_WS_MAC
    /* Assign watermark: */
    setPixmapName(":/wizard_new_harddisk.png");
#else /* VBOX_WS_MAC */
    /* Assign background image: */
    setPixmapName(":/wizard_new_harddisk_bg.png");
#endif /* VBOX_WS_MAC */

    UIMedium uiMedium = gpMediumEnumerator->medium(uMediumId);
    m_comSourceVirtualDisk = uiMedium.medium();

    m_strDefaultPath = QDir::toNativeSeparators(QFileInfo(m_comSourceVirtualDisk.GetLocation()).absolutePath());
    m_strDefaultName = QString("%1_%2").arg(QFileInfo(m_comSourceVirtualDisk.GetName()).baseName()).arg(UIWizardNewVD::tr("copy"));
    m_uDefaultSize = m_comSourceVirtualDisk.GetLogicalSize();
    m_enmDeviceType = m_comSourceVirtualDisk.GetDeviceType();
}

qulonglong UIWizardNewVD::mediumVariant() const
{
    return m_uMediumVariant;
}

void UIWizardNewVD::setMediumVariant(qulonglong uMediumVariant)
{
    m_uMediumVariant = uMediumVariant;
}

const CMediumFormat &UIWizardNewVD::mediumFormat()
{
    return m_comMediumFormat;
}

void UIWizardNewVD::setMediumFormat(const CMediumFormat &mediumFormat)
{
    m_comMediumFormat = mediumFormat;
    if (mode() == WizardMode_Basic)
        setMediumVariantPageVisibility();
}

const QString &UIWizardNewVD::mediumPath() const
{
    return m_strMediumPath;
}

void UIWizardNewVD::setMediumPath(const QString &strMediumPath)
{
    m_strMediumPath = strMediumPath;
}

qulonglong UIWizardNewVD::mediumSize() const
{
    return m_uMediumSize;
}

void UIWizardNewVD::setMediumSize(qulonglong uMediumSize)
{
    m_uMediumSize = uMediumSize;
}

QUuid UIWizardNewVD::mediumId() const
{
    return m_uMediumId;
}

const QString &UIWizardNewVD::defaultPath() const
{
    return m_strDefaultPath;
}

const QString &UIWizardNewVD::defaultName() const
{
    return m_strDefaultName;
}

qulonglong UIWizardNewVD::defaultSize() const
{
    return m_uDefaultSize;
}

void UIWizardNewVD::populatePages()
{
    switch (mode())
    {
        case WizardMode_Basic:
        {
            addPage(new UIWizardNewVDFileTypePage(m_enmDeviceType));
            m_iMediumVariantPageIndex = addPage(new UIWizardNewVDVariantPage);
            addPage(new UIWizardNewVDSizeLocationPage(diskMinimumSize()));
            break;
        }
        case WizardMode_Expert:
        {
            addPage(new UIWizardNewVDExpertPage(diskMinimumSize(), m_enmDeviceType));
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid mode: %d", mode()));
            break;
        }
    }
}

bool UIWizardNewVD::createVirtualDisk()
{
    AssertReturn(!m_strMediumPath.isNull(), false);
    AssertReturn(m_uMediumSize > 0, false);

    /* Get VBox object: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Create new virtual disk image: */
    CMedium comVirtualDisk = comVBox.CreateMedium(m_comMediumFormat.GetName(),
                                                  m_strMediumPath, KAccessMode_ReadWrite, m_enmDeviceType);
    if (!comVBox.isOk())
    {
        UINotificationMessage::cannotCreateMediumStorage(comVBox, m_strMediumPath, notificationCenter());
        return false;
    }

    /* Compose medium-variant: */
    QVector<KMediumVariant> variants(sizeof(qulonglong) * 8);
    for (int i = 0; i < variants.size(); ++i)
    {
        qulonglong temp = m_uMediumVariant;
        temp &= Q_UINT64_C(1) << i;
        variants[i] = (KMediumVariant)temp;
    }

    if (!isClonning())
    {
        UINotificationProgressMediumCreate *pNotification = new UINotificationProgressMediumCreate(comVirtualDisk,
                                                                                                   m_uMediumSize,
                                                                                                   variants);
        connect(pNotification, &UINotificationProgressMediumCreate::sigMediumCreated,
                gpMediumEnumerator, &UIMediumEnumerator::sltHandleMediumCreated);
        gpNotificationCenter->append(pNotification);
    }
    else
    {
        /* Copy medium: */
        UINotificationProgressMediumCopy *pNotification = new UINotificationProgressMediumCopy(m_comSourceVirtualDisk,
                                                                                               comVirtualDisk,
                                                                                               variants,
                                                                                               m_uMediumSize);
        connect(pNotification, &UINotificationProgressMediumCopy::sigMediumCopied,
                gpMediumEnumerator, &UIMediumEnumerator::sltHandleMediumCreated);
        gpNotificationCenter->append(pNotification);
    }

    m_uMediumId = comVirtualDisk.GetId();
    /* Positive: */
    return true;
}

/* static */
QUuid UIWizardNewVD::createVDWithWizard(QWidget *pParent,
                                        const QString &strMachineFolder /* = QString() */,
                                        const QString &strMachineName /* = QString() */,
                                        const QString &strMachineGuestOSTypeId  /* = QString() */)
{
    /* Default path: */
    const QString strDefaultPath = !strMachineFolder.isEmpty()
                                 ? strMachineFolder
                                 : UIMediumTools::defaultFolderPathForType(UIMediumDeviceType_HardDisk);

    /* Default name: */
    const QString strDiskName = uiCommon().findUniqueFileName(strDefaultPath,
                                                                !strMachineName.isEmpty()
                                                              ? strMachineName
                                                              : "NewVirtualDisk");

    /* Default size: */
    const CGuestOSType comGuestOSType = gpGlobalSession->virtualBox().GetGuestOSType(  !strMachineGuestOSTypeId.isEmpty()
                                                                               ? strMachineGuestOSTypeId
                                                                               : "Other");
    const qulonglong uDefaultSize = comGuestOSType.GetRecommendedHDD();

    /* Show New VD wizard the safe way: */
    QWidget *pRealParent = windowManager().realParentWindow(pParent);
    UISafePointerWizardNewVD pWizard = new UIWizardNewVD(pRealParent,
                                                         strDiskName,
                                                         strDefaultPath,
                                                         uDefaultSize);
    if (!pWizard)
        return QUuid();
    windowManager().registerNewParent(pWizard, pRealParent);
    pWizard->exec();
    const QUuid uMediumId = pWizard->mediumId();
    delete pWizard;
    return uMediumId;
}

void UIWizardNewVD::sltRetranslateUI()
{
    UINativeWizard::sltRetranslateUI();
    if (!isClonning())
        setWindowTitle(tr("Create Virtual Hard Disk"));
    else
        setWindowTitle(tr("Copy Virtual Hard Disk"));
}

void UIWizardNewVD::setMediumVariantPageVisibility()
{
    AssertReturnVoid(!m_comMediumFormat.isNull());
    ULONG uCapabilities = 0;
    QVector<KMediumFormatCapabilities> capabilities;
    capabilities = m_comMediumFormat.GetCapabilities();
    for (int i = 0; i < capabilities.size(); i++)
        uCapabilities |= capabilities[i];

    int cTest = 0;
    if (uCapabilities & KMediumFormatCapabilities_CreateDynamic)
        ++cTest;
    if (uCapabilities & KMediumFormatCapabilities_CreateFixed)
        ++cTest;
    if (uCapabilities & KMediumFormatCapabilities_CreateSplit2G)
        ++cTest;
    setPageVisible(m_iMediumVariantPageIndex, cTest > 1);
}

qulonglong UIWizardNewVD::diskMinimumSize() const
{
    if (!isClonning())
        return _4M;
    return m_comSourceVirtualDisk.GetLogicalSize();
}

KDeviceType UIWizardNewVD::deviceType() const
{
    return m_enmDeviceType;
}

bool UIWizardNewVD::isClonning() const
{
    return !m_comSourceVirtualDisk.isNull();
}
