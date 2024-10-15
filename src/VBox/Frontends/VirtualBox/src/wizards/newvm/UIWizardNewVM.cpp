/* $Id: UIWizardNewVM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWizardNewVM class implementation.
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
#include <QAbstractButton>
#include <QLayout>

/* GUI includes: */
#include "UIGlobalSession.h"
#include "UIGuestOSType.h"
#include "UILocalMachineStuff.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UINotificationCenter.h"
#include "UIWizardNewVM.h"
#include "UIWizardNewVMNameOSTypePage.h"
#include "UIWizardNewVMUnattendedPage.h"
#include "UIWizardNewVMHardwarePage.h"
#include "UIWizardNewVMDiskPage.h"
#include "UIWizardNewVMExpertPage.h"
#include "UIWizardNewVMSummaryPage.h"

/* COM includes: */
#include "CAudioAdapter.h"
#include "CAudioSettings.h"
#include "CFirmwareSettings.h"
#include "CGraphicsAdapter.h"
#include "CExtPackManager.h"
#include "CMediumFormat.h"
#include "CPlatform.h"
#include "CPlatformX86.h"
#include "CStorageController.h"
#include "CUSBController.h"
#include "CUSBDeviceFilters.h"

/* Namespaces: */
using namespace UIExtraDataDefs;


UIWizardNewVM::UIWizardNewVM(QWidget *pParent,
                             UIActionPool *pActionPool,
                             const QString &strMachineGroup,
                             const QString &strISOFilePath /* = QString() */)
    : UINativeWizard(pParent, WizardType_NewVM, "create-vm-wizard" /* help keyword */)
    , m_strMachineGroup(strMachineGroup)
    , m_iIDECount(0)
    , m_iSATACount(0)
    , m_iSCSICount(0)
    , m_iFloppyCount(0)
    , m_iSASCount(0)
    , m_iUSBCount(0)
    , m_fInstallGuestAdditions(false)
    , m_fSkipUnattendedInstall(false)
    , m_fEFIEnabled(false)
    , m_iCPUCount(1)
    , m_iMemorySize(0)
    , m_iUnattendedInstallPageIndex(-1)
    , m_uMediumVariant(0)
    , m_uMediumSize(0)
    , m_enmDiskSource(SelectedDiskSource_New)
    , m_fEmptyDiskRecommended(false)
    , m_pActionPool(pActionPool)
    , m_fStartHeadless(false)
    , m_strInitialISOFilePath(strISOFilePath)
{
#ifndef VBOX_WS_MAC
    /* Assign watermark: */
    setPixmapName(":/wizard_new_welcome.png");
#else /* VBOX_WS_MAC */
    /* Assign background image: */
    setPixmapName(":/wizard_new_welcome_bg.png");
#endif /* VBOX_WS_MAC */

    /* Create installer: */
    m_comUnattended = gpGlobalSession->virtualBox().CreateUnattendedInstaller();
    AssertMsg(!m_comUnattended.isNull(), ("Could not create unattended installer!\n"));
}

void UIWizardNewVM::populatePages()
{
    switch (mode())
    {
        case WizardMode_Basic:
        {
            UIWizardNewVMNameOSTypePage *pNamePage = new UIWizardNewVMNameOSTypePage;
            addPage(pNamePage);
            if (!m_strInitialISOFilePath.isEmpty())
                pNamePage->setISOFilePath(m_strInitialISOFilePath);
            m_iUnattendedInstallPageIndex = addPage(new UIWizardNewVMUnattendedPage);
            setUnattendedPageVisible(false);
            addPage(new UIWizardNewVMHardwarePage);
            addPage(new UIWizardNewVMDiskPage(m_pActionPool));
            addPage(new UIWizardNewVMSummaryPage);
            break;
        }
        case WizardMode_Expert:
        {
            UIWizardNewVMExpertPage *pExpertPage = new UIWizardNewVMExpertPage(m_pActionPool);
            addPage(pExpertPage);
            if (!m_strInitialISOFilePath.isEmpty())
                pExpertPage->setISOFilePath(m_strInitialISOFilePath);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid mode: %d", mode()));
            break;
        }
    }
}

void UIWizardNewVM::cleanWizard()
{
    /* Try to delete the hard disk in case we have created one: */
    deleteVirtualDisk();
    /* Cleanup the machine folder: */
    UIWizardNewVMNameOSTypeCommon::cleanupMachineFolder(this, true);

    if (!m_machine.isNull())
        m_machine.detach();
}

bool UIWizardNewVM::createVM()
{
    CVirtualBox vbox = gpGlobalSession->virtualBox();

    /* Create virtual machine: */
    if (m_machine.isNull())
    {
        QVector<QString> groups;
        if (!m_strMachineGroup.isEmpty())
            groups << m_strMachineGroup;
        m_machine = vbox.CreateMachine(m_strMachineFilePath,
                                       m_strMachineBaseName,
                                       KPlatformArchitecture_x86,
                                       groups, m_guestOSTypeId, QString(),
                                       QString(), QString(), QString());
        if (!vbox.isOk())
        {
            UINotificationMessage::cannotCreateMachine(vbox, notificationCenter());
            cleanWizard();
            return false;
        }
    }

    CFirmwareSettings comFirmwareSettings = m_machine.GetFirmwareSettings();

    /* The newer and less tested way of configuring vms: */
    m_machine.ApplyDefaults(QString());
    /* Apply user preferences again. IMachine::applyDefaults may have overwritten the user setting: */
    m_machine.SetMemorySize(m_iMemorySize);
    int iVPUCount = qMax(1, m_iCPUCount);
    m_machine.SetCPUCount(iVPUCount);
    /* Correct the VRAM size since API does not take fullscreen memory requirements into account: */
    CGraphicsAdapter comGraphics = m_machine.GetGraphicsAdapter();
    comGraphics.SetVRAMSize(qMax(comGraphics.GetVRAMSize(),
                                 (ULONG)(UIGuestOSTypeHelpers::requiredVideoMemory(m_guestOSTypeId) / _1M)));
    /* Enabled I/O APIC explicitly in we have more than 1 VCPU: */
    if (iVPUCount > 1)
        comFirmwareSettings.SetIOAPICEnabled(true);

    /* Set recommended firmware type: */
    comFirmwareSettings.SetFirmwareType(m_fEFIEnabled ? KFirmwareType_EFI : KFirmwareType_BIOS);

    /* Register the VM prior to attaching hard disks: */
    vbox.RegisterMachine(m_machine);
    if (!vbox.isOk())
    {
        UINotificationMessage::cannotRegisterMachine(vbox, m_machine.GetName(), notificationCenter());
        cleanWizard();
        return false;
    }

    if (!attachDefaultDevices())
    {
        cleanWizard();
        return false;
    }

    if (isUnattendedEnabled())
    {
        m_comUnattended.SetMachine(m_machine);
        if (!checkUnattendedInstallError(m_comUnattended))
        {
            cleanWizard();
            return false;
        }
    }
    return true;
}

bool UIWizardNewVM::createVirtualDisk()
{
    /* Prepare result: */
    bool fResult = false;

    /* Check attributes: */
    AssertReturn(!m_strMediumPath.isNull(), false);
    AssertReturn(m_uMediumSize > 0, false);

    /* Acquire VBox: */
    CVirtualBox comVBox = gpGlobalSession->virtualBox();

    /* Create new virtual hard-disk: */
    CMedium newVirtualDisk = comVBox.CreateMedium(m_comMediumFormat.GetName(), m_strMediumPath, KAccessMode_ReadWrite, KDeviceType_HardDisk);
    if (!comVBox.isOk())
    {
        UINotificationMessage::cannotCreateMediumStorage(comVBox, m_strMediumPath, notificationCenter());
        return fResult;
    }

    /* Create base storage for the new virtual-disk: */
    UINotificationProgressMediumCreate *pNotification = new UINotificationProgressMediumCreate(newVirtualDisk,
                                                                                               m_uMediumSize,
                                                                                               mediumVariants());
    if (!handleNotificationProgressNow(pNotification))
        return fResult;

    /* Inform UIMediumEnumerator about it: */
    gpMediumEnumerator->createMedium(UIMedium(newVirtualDisk, UIMediumDeviceType_HardDisk, KMediumState_Created));

    /* Remember created virtual-disk: */
    m_virtualDisk = newVirtualDisk;

    /* True finally: */
    fResult = true;

    /* Return result: */
    return fResult;
}

void UIWizardNewVM::deleteVirtualDisk()
{
    /* Do nothing if an existing disk has been selected: */
    if (m_enmDiskSource == SelectedDiskSource_Existing)
        return;

    /* Make sure virtual-disk valid: */
    if (m_virtualDisk.isNull())
        return;

    /* Delete storage of existing disk: */
    UINotificationProgressMediumDeletingStorage *pNotification = new UINotificationProgressMediumDeletingStorage(m_virtualDisk);
    if (!handleNotificationProgressNow(pNotification))
        return;

    /* Detach virtual-disk finally: */
    m_virtualDisk.detach();
}

bool UIWizardNewVM::attachDefaultDevices()
{
    bool success = false;
    QUuid uMachineId = m_machine.GetId();
    CSession session = openSession(uMachineId);
    if (!session.isNull())
    {
        CMachine machine = session.GetMachine();
        if (!m_virtualDisk.isNull())
        {
            KStorageBus enmHDDBus = gpGlobalSession->guestOSTypeManager().getRecommendedHDStorageBus(m_guestOSTypeId);
            CStorageController comHDDController = m_machine.GetStorageControllerByInstance(enmHDDBus, 0);
            if (!comHDDController.isNull())
            {
                machine.AttachDevice(comHDDController.GetName(), 0, 0, KDeviceType_HardDisk, m_virtualDisk);
                if (!machine.isOk())
                    UINotificationMessage::cannotAttachDevice(machine, UIMediumDeviceType_HardDisk, m_strMediumPath,
                                                              StorageSlot(enmHDDBus, 0, 0), notificationCenter());
            }
        }

        /* Attach optical drive: */
        KStorageBus enmDVDBus = gpGlobalSession->guestOSTypeManager().getRecommendedDVDStorageBus(m_guestOSTypeId);
        CStorageController comDVDController = m_machine.GetStorageControllerByInstance(enmDVDBus, 0);
        if (!comDVDController.isNull())
        {
            CMedium opticalDisk;
            QString strISOFilePath = ISOFilePath();
            if (!strISOFilePath.isEmpty() && !isUnattendedEnabled())
            {
                CVirtualBox vbox = gpGlobalSession->virtualBox();
                opticalDisk =
                    vbox.OpenMedium(strISOFilePath, KDeviceType_DVD, KAccessMode_ReadWrite, false);
                if (!vbox.isOk())
                    UINotificationMessage::cannotOpenMedium(vbox, strISOFilePath, notificationCenter());
            }
            machine.AttachDevice(comDVDController.GetName(), 1, 0, KDeviceType_DVD, opticalDisk);
            if (!machine.isOk())
                UINotificationMessage::cannotAttachDevice(machine, UIMediumDeviceType_DVD, QString(),
                                                          StorageSlot(enmDVDBus, 1, 0), notificationCenter());
        }

        /* Attach an empty floppy drive if recommended */
        if (gpGlobalSession->guestOSTypeManager().getRecommendedFloppy(m_guestOSTypeId))
        {
            CStorageController comFloppyController = m_machine.GetStorageControllerByInstance(KStorageBus_Floppy, 0);
            if (!comFloppyController.isNull())
            {
                machine.AttachDevice(comFloppyController.GetName(), 0, 0, KDeviceType_Floppy, CMedium());
                if (!machine.isOk())
                    UINotificationMessage::cannotAttachDevice(machine, UIMediumDeviceType_Floppy, QString(),
                                                              StorageSlot(KStorageBus_Floppy, 0, 0), notificationCenter());
            }
        }

        if (machine.isOk())
        {
            machine.SaveSettings();
            if (machine.isOk())
                success = true;
            else
                UINotificationMessage::cannotSaveMachineSettings(machine, notificationCenter());
        }

        session.UnlockMachine();
    }
    if (!success)
    {
        /* Unregister VM on failure: */
        const QVector<CMedium> media = m_machine.Unregister(KCleanupMode_DetachAllReturnHardDisksAndVMRemovable);
        if (!m_machine.isOk())
            UINotificationMessage::cannotRemoveMachine(m_machine, notificationCenter());
        else
        {
            UINotificationProgressMachineMediaRemove *pNotification =
                new UINotificationProgressMachineMediaRemove(m_machine, media);
            handleNotificationProgressNow(pNotification);
        }
    }

    /* Make sure we detach CMedium wrapper from IMedium pointer to avoid deletion of IMedium as m_virtualDisk
     * is deallocated.  Or in case of UINotificationProgressMachineMediaRemove handling, IMedium has been
     * already deleted so detach in this case as well. */
    if (!m_virtualDisk.isNull())
        m_virtualDisk.detach();

    return success;
}

void UIWizardNewVM::sltRetranslateUI()
{
    UINativeWizard::sltRetranslateUI();
    setWindowTitle(tr("Create Virtual Machine"));
}

QString UIWizardNewVM::getNextControllerName(KStorageBus type)
{
    QString strControllerName;
    switch (type)
    {
        case KStorageBus_IDE:
        {
            strControllerName = "IDE";
            ++m_iIDECount;
            if (m_iIDECount > 1)
                strControllerName = QString("%1 %2").arg(strControllerName).arg(m_iIDECount);
            break;
        }
        case KStorageBus_SATA:
        {
            strControllerName = "SATA";
            ++m_iSATACount;
            if (m_iSATACount > 1)
                strControllerName = QString("%1 %2").arg(strControllerName).arg(m_iSATACount);
            break;
        }
        case KStorageBus_SCSI:
        {
            strControllerName = "SCSI";
            ++m_iSCSICount;
            if (m_iSCSICount > 1)
                strControllerName = QString("%1 %2").arg(strControllerName).arg(m_iSCSICount);
            break;
        }
        case KStorageBus_Floppy:
        {
            strControllerName = "Floppy";
            ++m_iFloppyCount;
            if (m_iFloppyCount > 1)
                strControllerName = QString("%1 %2").arg(strControllerName).arg(m_iFloppyCount);
            break;
        }
        case KStorageBus_SAS:
        {
            strControllerName = "SAS";
            ++m_iSASCount;
            if (m_iSASCount > 1)
                strControllerName = QString("%1 %2").arg(strControllerName).arg(m_iSASCount);
            break;
        }
        case KStorageBus_USB:
        {
            strControllerName = "USB";
            ++m_iUSBCount;
            if (m_iUSBCount > 1)
                strControllerName = QString("%1 %2").arg(strControllerName).arg(m_iUSBCount);
            break;
        }
        default:
            break;
    }
    return strControllerName;
}

QUuid UIWizardNewVM::createdMachineId() const
{
    if (m_machine.isOk())
        return m_machine.GetId();
    return QUuid();
}

CMedium &UIWizardNewVM::virtualDisk()
{
    return m_virtualDisk;
}

void UIWizardNewVM::setVirtualDisk(const CMedium &medium)
{
    m_virtualDisk = medium;
}

void UIWizardNewVM::setVirtualDisk(const QUuid &mediumId)
{
    if (m_virtualDisk.isOk() && m_virtualDisk.GetId() == mediumId)
        return;
    CMedium medium = gpMediumEnumerator->medium(mediumId).medium();
    setVirtualDisk(medium);
}

const QString &UIWizardNewVM::machineGroup() const
{
    return m_strMachineGroup;
}

const QString &UIWizardNewVM::machineFilePath() const
{
    return m_strMachineFilePath;
}

void UIWizardNewVM::setMachineFilePath(const QString &strMachineFilePath)
{
    m_strMachineFilePath = strMachineFilePath;
}

QString UIWizardNewVM::machineFileName() const
{
    return QFileInfo(machineFilePath()).completeBaseName();
}

const QString &UIWizardNewVM::machineFolder() const
{
    return m_strMachineFolder;
}

void UIWizardNewVM::setMachineFolder(const QString &strMachineFolder)
{
    m_strMachineFolder = strMachineFolder;
}

const QString &UIWizardNewVM::machineBaseName() const
{
    return m_strMachineBaseName;
}

void UIWizardNewVM::setMachineBaseName(const QString &strMachineBaseName)
{
    m_strMachineBaseName = strMachineBaseName;
}

const QString &UIWizardNewVM::createdMachineFolder() const
{
    return m_strCreatedFolder;
}

void UIWizardNewVM::setCreatedMachineFolder(const QString &strCreatedMachineFolder)
{
    m_strCreatedFolder = strCreatedMachineFolder;
}

QString UIWizardNewVM::detectedOSTypeId() const
{
    AssertReturn(!m_comUnattended.isNull(), QString());
    return m_comUnattended.GetDetectedOSTypeId();
}

const QString &UIWizardNewVM::guestOSFamilyId() const
{
    return m_strGuestOSFamilyId;
}

void UIWizardNewVM::setGuestOSFamilyId(const QString &strGuestOSFamilyId)
{
    m_strGuestOSFamilyId = strGuestOSFamilyId;
}

const QString &UIWizardNewVM::guestOSTypeId() const
{
    return m_guestOSTypeId;
}

void UIWizardNewVM::setGuestOSTypeId(const QString &guestOSTypeId)
{
    m_guestOSTypeId = guestOSTypeId;
}

bool UIWizardNewVM::installGuestAdditions() const
{
    AssertReturn(!m_comUnattended.isNull(), false);
    return m_comUnattended.GetInstallGuestAdditions();
}

void UIWizardNewVM::setInstallGuestAdditions(bool fInstallGA)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetInstallGuestAdditions(fInstallGA);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));
}

bool UIWizardNewVM::startHeadless() const
{
    return m_fStartHeadless;
}

void UIWizardNewVM::setStartHeadless(bool fStartHeadless)
{
    m_fStartHeadless = fStartHeadless;
}

bool UIWizardNewVM::skipUnattendedInstall() const
{
    return m_fSkipUnattendedInstall;
}

void UIWizardNewVM::setSkipUnattendedInstall(bool fSkipUnattendedInstall)
{
    m_fSkipUnattendedInstall = fSkipUnattendedInstall;
    /* We hide/show unattended install page depending on the value of isUnattendedEnabled: */
    setUnattendedPageVisible(isUnattendedEnabled());
}

bool UIWizardNewVM::EFIEnabled() const
{
    return m_fEFIEnabled;
}

void UIWizardNewVM::setEFIEnabled(bool fEnabled)
{
    m_fEFIEnabled = fEnabled;
}

QString UIWizardNewVM::ISOFilePath() const
{
    AssertReturn(!m_comUnattended.isNull(), QString());
    return m_comUnattended.GetIsoPath();
}

void UIWizardNewVM::setISOFilePath(const QString &strISOFilePath)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetIsoPath(strISOFilePath);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));

    m_comUnattended.DetectIsoOS();

    const QVector<ULONG> &indices = m_comUnattended.GetDetectedImageIndices();
    QVector<ulong> qIndices;
    for (int i = 0; i < indices.size(); ++i)
        qIndices << indices[i];
    setDetectedWindowsImageNamesAndIndices(m_comUnattended.GetDetectedImageNames(), qIndices);
    /* We hide/show unattended install page depending on the value of isUnattendedEnabled: */
    setUnattendedPageVisible(isUnattendedEnabled());
}

QString UIWizardNewVM::userName() const
{
    AssertReturn(!m_comUnattended.isNull(), QString());
    return m_comUnattended.GetUser();
}

void UIWizardNewVM::setUserName(const QString &strUserName)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetUser(strUserName);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));
}

QString UIWizardNewVM::password() const
{
    AssertReturn(!m_comUnattended.isNull(), QString());
    return m_comUnattended.GetUserPassword();
}

void UIWizardNewVM::setPassword(const QString &strPassword)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetUserPassword(strPassword);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));
}

QString UIWizardNewVM::guestAdditionsISOPath() const
{
    AssertReturn(!m_comUnattended.isNull(), QString());
    return m_comUnattended.GetAdditionsIsoPath();
}

void UIWizardNewVM::setGuestAdditionsISOPath(const QString &strGAISOPath)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetAdditionsIsoPath(strGAISOPath);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));
}

QString UIWizardNewVM::hostnameDomainName() const
{
    AssertReturn(!m_comUnattended.isNull(), QString());
    return m_comUnattended.GetHostname();
}

void UIWizardNewVM::setHostnameDomainName(const QString &strHostnameDomain)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetHostname(strHostnameDomain);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));
}

QString UIWizardNewVM::productKey() const
{
    AssertReturn(!m_comUnattended.isNull(), QString());
    return  m_comUnattended.GetProductKey();
}

void UIWizardNewVM::setProductKey(const QString &productKey)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetProductKey(productKey);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));
}

int UIWizardNewVM::CPUCount() const
{
    return m_iCPUCount;
}

void UIWizardNewVM::setCPUCount(int iCPUCount)
{
    m_iCPUCount = iCPUCount;
}

int UIWizardNewVM::memorySize() const
{
    return m_iMemorySize;
}

void UIWizardNewVM::setMemorySize(int iMemory)
{
    m_iMemorySize = iMemory;
}


qulonglong UIWizardNewVM::mediumVariant() const
{
    return m_uMediumVariant;
}

void UIWizardNewVM::setMediumVariant(qulonglong uMediumVariant)
{
    m_uMediumVariant = uMediumVariant;
}

const CMediumFormat &UIWizardNewVM::mediumFormat()
{
    return m_comMediumFormat;
}

void UIWizardNewVM::setMediumFormat(const CMediumFormat &mediumFormat)
{
    m_comMediumFormat = mediumFormat;
}

const QString &UIWizardNewVM::mediumPath() const
{
    return m_strMediumPath;
}

void UIWizardNewVM::setMediumPath(const QString &strMediumPath)
{
    m_strMediumPath = strMediumPath;
}

qulonglong UIWizardNewVM::mediumSize() const
{
    return m_uMediumSize;
}

void UIWizardNewVM::setMediumSize(qulonglong uMediumSize)
{
    m_uMediumSize = uMediumSize;
}

SelectedDiskSource UIWizardNewVM::diskSource() const
{
    return m_enmDiskSource;
}

void UIWizardNewVM::setDiskSource(SelectedDiskSource enmDiskSource)
{
    m_enmDiskSource = enmDiskSource;
}

bool UIWizardNewVM::emptyDiskRecommended() const
{
    return m_fEmptyDiskRecommended;
}

void UIWizardNewVM::setEmptyDiskRecommended(bool fEmptyDiskRecommended)
{
    m_fEmptyDiskRecommended = fEmptyDiskRecommended;
}

void UIWizardNewVM::setDetectedWindowsImageNamesAndIndices(const QVector<QString> &names, const QVector<ulong> &ids)
{
    AssertMsg(names.size() == ids.size(),
              ("Sizes of the arrays for names and indices of the detected images should be equal."));
    m_detectedWindowsImageNames = names;
    m_detectedWindowsImageIndices = ids;
}

const QVector<QString> &UIWizardNewVM::detectedWindowsImageNames() const
{
    return m_detectedWindowsImageNames;
}

const QVector<ulong> &UIWizardNewVM::detectedWindowsImageIndices() const
{
    return m_detectedWindowsImageIndices;
}

void UIWizardNewVM::setSelectedWindowImageIndex(ulong uIndex)
{
    AssertReturnVoid(!m_comUnattended.isNull());
    m_comUnattended.SetImageIndex(uIndex);
    AssertReturnVoid(checkUnattendedInstallError(m_comUnattended));
}

ulong UIWizardNewVM::selectedWindowImageIndex() const
{
    AssertReturn(!m_comUnattended.isNull(), 0);
    return m_comUnattended.GetImageIndex();
}

QVector<KMediumVariant> UIWizardNewVM::mediumVariants() const
{
    /* Compose medium-variant: */
    QVector<KMediumVariant> variants(sizeof(qulonglong)*8);
    for (int i = 0; i < variants.size(); ++i)
    {
        qulonglong temp = m_uMediumVariant;
        temp &= UINT64_C(1)<<i;
        variants[i] = (KMediumVariant)temp;
    }
    return variants;
}

QString UIWizardNewVM::getGuestOSTypeDescription() const
{
    return gpGlobalSession->guestOSTypeManager().getDescription(m_guestOSTypeId);
}

bool UIWizardNewVM::isUnattendedEnabled() const
{
    if (m_comUnattended.isNull())
        return false;
    if (m_comUnattended.GetIsoPath().isEmpty())
        return false;
    if (m_fSkipUnattendedInstall)
        return false;
    if (!isUnattendedInstallSupported())
        return false;
    return true;
}

bool UIWizardNewVM::isUnattendedInstallSupported() const
{
    AssertReturn(!m_comUnattended.isNull(), false);
    return m_comUnattended.GetIsUnattendedInstallSupported();
}

bool UIWizardNewVM::isGuestOSTypeWindows() const
{
    return m_strGuestOSFamilyId.contains("windows", Qt::CaseInsensitive);
}

void UIWizardNewVM::setUnattendedPageVisible(bool fVisible)
{
    if (m_iUnattendedInstallPageIndex != -1)
        setPageVisible(m_iUnattendedInstallPageIndex, fVisible);
}

bool UIWizardNewVM::checkUnattendedInstallError(const CUnattended &comUnattended) const
{
    if (!comUnattended.isOk())
    {
        UINotificationMessage::cannotRunUnattendedGuestInstall(comUnattended);
        return false;
    }
    return true;
}
