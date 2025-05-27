/* $Id: UIMachineSettingsSystem.cpp 169023 2025-05-27 15:31:04Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsSystem class implementation.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "QITabWidget.h"
#include "UIAccelerationFeaturesEditor.h"
#include "UIBaseMemoryEditor.h"
#include "UIBootOrderEditor.h"
#include "UIChipsetEditor.h"
#include "UIErrorString.h"
#include "UIExecutionCapEditor.h"
#include "UIGlobalSession.h"
#include "UIMachineSettingsSystem.h"
#include "UIMotherboardFeaturesEditor.h"
#include "UIParavirtProviderEditor.h"
#include "UIPointingHIDEditor.h"
#include "UIProcessorFeaturesEditor.h"
#include "UITpmEditor.h"
#include "UITranslator.h"
#include "UIVirtualCPUEditor.h"

/* COM includes: */
#include "CFirmwareSettings.h"
#include "CPlatform.h"
#include "CPlatformX86.h"
#include "CNvramStore.h"
#include "CTrustedPlatformModule.h"
#include "CUefiVariableStore.h"


/** Machine settings: System page data structure. */
struct UIDataSettingsMachineSystem
{
    /** Constructs data. */
    UIDataSettingsMachineSystem()
        /* Support flags: */
        : m_fSupportedPAE(false)
        , m_fSupportedNestedHwVirtEx(false)
        , m_fSupportedHwVirtEx(false)
        , m_fSupportedNestedPaging(false)
        /* Motherboard data: */
        , m_iMemorySize(-1)
        , m_bootItems(UIBootItemDataList())
        , m_chipsetType(KChipsetType_Null)
        , m_tpmType(KTpmType_None)
        , m_pointingHIDType(KPointingHIDType_None)
        , m_fEnabledIoApic(false)
        , m_fEnabledEFI(false)
        , m_fEnabledUTC(false)
        , m_fAvailableSecureBoot(false)
        , m_fEnabledSecureBoot(false)
        , m_fResetSecureBoot(false)
        /* CPU data: */
        , m_cCPUCount(-1)
        , m_iCPUExecCap(-1)
        , m_fEnabledPAE(false)
        , m_fEnabledNestedHwVirtEx(false)
        /* Acceleration data: */
        , m_paravirtProvider(KParavirtProvider_None)
        , m_fEnabledNestedPaging(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineSystem &other) const
    {
        return true
               /* Support flags: */
               && (m_fSupportedPAE == other.m_fSupportedPAE)
               && (m_fSupportedNestedHwVirtEx == other.m_fSupportedNestedHwVirtEx)
               && (m_fSupportedHwVirtEx == other.m_fSupportedHwVirtEx)
               && (m_fSupportedNestedPaging == other.m_fSupportedNestedPaging)
               /* Motherboard data: */
               && (m_iMemorySize == other.m_iMemorySize)
               && (m_bootItems == other.m_bootItems)
               && (m_chipsetType == other.m_chipsetType)
               && (m_tpmType == other.m_tpmType)
               && (m_pointingHIDType == other.m_pointingHIDType)
               && (m_fEnabledIoApic == other.m_fEnabledIoApic)
               && (m_fEnabledEFI == other.m_fEnabledEFI)
               && (m_fEnabledUTC == other.m_fEnabledUTC)
               && (m_fAvailableSecureBoot == other.m_fAvailableSecureBoot)
               && (m_fEnabledSecureBoot == other.m_fEnabledSecureBoot)
               && (m_fResetSecureBoot == other.m_fResetSecureBoot)
               /* CPU data: */
               && (m_cCPUCount == other.m_cCPUCount)
               && (m_iCPUExecCap == other.m_iCPUExecCap)
               && (m_fEnabledPAE == other.m_fEnabledPAE)
               && (m_fEnabledNestedHwVirtEx == other.m_fEnabledNestedHwVirtEx)
               /* Acceleration data: */
               && (m_paravirtProvider == other.m_paravirtProvider)
               && (m_fEnabledNestedPaging == other.m_fEnabledNestedPaging)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineSystem &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineSystem &other) const { return !equal(other); }

    /** Holds whether the PAE is supported. */
    bool  m_fSupportedPAE;
    /** Holds whether the Nested HW Virt Ex is supported. */
    bool  m_fSupportedNestedHwVirtEx;
    /** Holds whether the HW Virt Ex is supported. */
    bool  m_fSupportedHwVirtEx;
    /** Holds whether the Nested Paging is supported. */
    bool  m_fSupportedNestedPaging;

    /** Holds the RAM size. */
    int                 m_iMemorySize;
    /** Holds the boot items. */
    UIBootItemDataList  m_bootItems;
    /** Holds the chipset type. */
    KChipsetType        m_chipsetType;
    /** Holds the TPM type. */
    KTpmType            m_tpmType;
    /** Holds the pointing HID type. */
    KPointingHIDType    m_pointingHIDType;
    /** Holds whether the IO APIC is enabled. */
    bool                m_fEnabledIoApic;
    /** Holds whether the EFI is enabled. */
    bool                m_fEnabledEFI;
    /** Holds whether the UTC is enabled. */
    bool                m_fEnabledUTC;
    /** Holds whether the secure boot is available. */
    bool                m_fAvailableSecureBoot;
    /** Holds whether the secure boot is enabled. */
    bool                m_fEnabledSecureBoot;
    /** Holds whether the secure boot is reseted. */
    bool                m_fResetSecureBoot;

    /** Holds the CPU count. */
    int   m_cCPUCount;
    /** Holds the CPU execution cap. */
    int   m_iCPUExecCap;
    /** Holds whether the PAE is enabled. */
    bool  m_fEnabledPAE;
    /** Holds whether the Nested HW Virt Ex is enabled. */
    bool  m_fEnabledNestedHwVirtEx;

    /** Holds the paravirtualization provider. */
    KParavirtProvider  m_paravirtProvider;
    /** Holds whether the Nested Paging is enabled. */
    bool               m_fEnabledNestedPaging;
};


UIMachineSettingsSystem::UIMachineSettingsSystem()
    : m_fIsUSBEnabled(false)
    , m_pCache(0)
    , m_pTabWidget(0)
    , m_pTabMotherboard(0)
    , m_pEditorBaseMemory(0)
    , m_pEditorBootOrder(0)
    , m_pEditorChipset(0)
    , m_pEditorTpm(0)
    , m_pEditorPointingHID(0)
    , m_pEditorMotherboardFeatures(0)
    , m_pTabProcessor(0)
    , m_pEditorVCPU(0)
    , m_pEditorExecCap(0)
    , m_pEditorProcessorFeatures(0)
    , m_pTabAcceleration(0)
    , m_pEditorParavirtProvider(0)
    , m_pEditorAccelerationFeatures(0)
{
    prepare();
}

UIMachineSettingsSystem::~UIMachineSettingsSystem()
{
    cleanup();
}

bool UIMachineSettingsSystem::isHWVirtExSupported() const
{
    AssertPtrReturn(m_pCache, false);
    return m_pCache->base().m_fSupportedHwVirtEx;
}

bool UIMachineSettingsSystem::isNestedPagingSupported() const
{
    AssertPtrReturn(m_pCache, false);
    return m_pCache->base().m_fSupportedNestedPaging;
}

bool UIMachineSettingsSystem::isNestedPagingEnabled() const
{
    return   m_pEditorAccelerationFeatures
           ? m_pEditorAccelerationFeatures->isEnabledNestedPaging()
           : m_pCache->base().m_fEnabledNestedPaging;
}

bool UIMachineSettingsSystem::isNestedHWVirtExSupported() const
{
    AssertPtrReturn(m_pCache, false);
    return m_pCache->base().m_fSupportedNestedHwVirtEx;
}

bool UIMachineSettingsSystem::isNestedHWVirtExEnabled() const
{
    return m_pEditorProcessorFeatures->isEnabledNestedVirtualization();
}

bool UIMachineSettingsSystem::isHIDEnabled() const
{
    return m_pEditorPointingHID->value() != KPointingHIDType_PS2Mouse;
}

KChipsetType UIMachineSettingsSystem::chipsetType() const
{
    return   m_pEditorChipset
           ? m_pEditorChipset->value()
           : m_pCache->base().m_chipsetType;
}

void UIMachineSettingsSystem::setUSBEnabled(bool fEnabled)
{
    /* Make sure USB status has changed: */
    if (m_fIsUSBEnabled == fEnabled)
        return;

    /* Update USB status value: */
    m_fIsUSBEnabled = fEnabled;

    /* Revalidate: */
    revalidate();
}

bool UIMachineSettingsSystem::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIMachineSettingsSystem::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Prepare old data: */
    UIDataSettingsMachineSystem oldSystemData;

    CPlatform comPlatform = m_machine.GetPlatform();
    CFirmwareSettings comFirmwareSettings = m_machine.GetFirmwareSettings();
    CNvramStore comStoreLvl1 = m_machine.GetNonVolatileStore();
    CUefiVariableStore comStoreLvl2 = comStoreLvl1.GetUefiVariableStore();

    /* Gather support flags: */
    oldSystemData.m_fSupportedPAE = gpGlobalSession->host().GetProcessorFeature(KProcessorFeature_PAE);
    oldSystemData.m_fSupportedNestedHwVirtEx = gpGlobalSession->host().GetProcessorFeature(KProcessorFeature_NestedHWVirt);
    oldSystemData.m_fSupportedHwVirtEx = gpGlobalSession->host().GetProcessorFeature(KProcessorFeature_HWVirtEx);
    oldSystemData.m_fSupportedNestedPaging = gpGlobalSession->host().GetProcessorFeature(KProcessorFeature_NestedPaging);

    /* Gather old 'Motherboard' data: */
    oldSystemData.m_iMemorySize = m_machine.GetMemorySize();
    oldSystemData.m_bootItems = loadBootItems(m_machine);
    oldSystemData.m_chipsetType = comPlatform.GetChipsetType();
    oldSystemData.m_tpmType = m_machine.GetTrustedPlatformModule().GetType();
    oldSystemData.m_pointingHIDType = m_machine.GetPointingHIDType();
    oldSystemData.m_fEnabledIoApic = comFirmwareSettings.GetIOAPICEnabled();
    oldSystemData.m_fEnabledEFI = comFirmwareSettings.GetFirmwareType() >= KFirmwareType_EFI && comFirmwareSettings.GetFirmwareType() <= KFirmwareType_EFIDUAL;
    oldSystemData.m_fEnabledUTC = comPlatform.GetRTCUseUTC();
    oldSystemData.m_fAvailableSecureBoot = comStoreLvl2.isNotNull();
    oldSystemData.m_fEnabledSecureBoot = oldSystemData.m_fAvailableSecureBoot
                                       ? comStoreLvl2.GetSecureBootEnabled()
                                       : false;
    oldSystemData.m_fResetSecureBoot = false;

    /* Gather old 'Processor' data: */
    oldSystemData.m_cCPUCount = oldSystemData.m_fSupportedHwVirtEx ? m_machine.GetCPUCount() : 1;
    oldSystemData.m_iCPUExecCap = m_machine.GetCPUExecutionCap();
    switch (comPlatform.GetArchitecture())
    {
        case KPlatformArchitecture_x86:
        {
            CPlatformX86 comPlatformX86 = comPlatform.GetX86();
            oldSystemData.m_fEnabledPAE = comPlatformX86.GetCPUProperty(KCPUPropertyTypeX86_PAE);
            oldSystemData.m_fEnabledNestedHwVirtEx = comPlatformX86.GetCPUProperty(KCPUPropertyTypeX86_HWVirt);
            oldSystemData.m_fEnabledNestedPaging = comPlatformX86.GetHWVirtExProperty(KHWVirtExPropertyType_NestedPaging);
            break;
        }
#ifdef VBOX_WITH_VIRT_ARMV8
        case KPlatformArchitecture_ARM:
        {
            /** @todo BUGBUG ARM stuff goes here. */
            break;
        }
#endif
        default:
            break;
    }

    /* Gather old 'Acceleration' data: */
    oldSystemData.m_paravirtProvider = m_machine.GetParavirtProvider();

    /* Cache old data: */
    m_pCache->cacheInitialData(oldSystemData);

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsSystem::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Get old data from cache: */
    const UIDataSettingsMachineSystem &oldSystemData = m_pCache->base();

    /* Load old 'Motherboard' data from cache: */
    if (m_pEditorBaseMemory)
        m_pEditorBaseMemory->setValue(oldSystemData.m_iMemorySize);
    if (m_pEditorBootOrder)
        m_pEditorBootOrder->setValue(oldSystemData.m_bootItems);
    if (m_pEditorChipset)
        m_pEditorChipset->setValue(oldSystemData.m_chipsetType);
    if (m_pEditorTpm)
        m_pEditorTpm->setValue(oldSystemData.m_tpmType);
    if (m_pEditorPointingHID)
        m_pEditorPointingHID->setValue(oldSystemData.m_pointingHIDType);
    if (m_pEditorMotherboardFeatures)
    {
        m_pEditorMotherboardFeatures->setEnableIoApic(oldSystemData.m_fEnabledIoApic);
        m_pEditorMotherboardFeatures->setEnableEfi(oldSystemData.m_fEnabledEFI);
        m_pEditorMotherboardFeatures->setEnableUtcTime(oldSystemData.m_fEnabledUTC);
        m_pEditorMotherboardFeatures->setEnableSecureBoot(oldSystemData.m_fEnabledSecureBoot);
    }

    /* Load old 'Processor' data from cache: */
    if (m_pEditorVCPU)
        m_pEditorVCPU->setValue(oldSystemData.m_cCPUCount);
    if (m_pEditorExecCap)
        m_pEditorExecCap->setValue(oldSystemData.m_iCPUExecCap);
    if (m_pEditorProcessorFeatures)
    {
        m_pEditorProcessorFeatures->setEnablePae(oldSystemData.m_fEnabledPAE);
        m_pEditorProcessorFeatures->setEnableNestedVirtualization(oldSystemData.m_fEnabledNestedHwVirtEx);
    }

    /* Load old 'Acceleration' data from cache: */
    if (m_pEditorParavirtProvider)
        m_pEditorParavirtProvider->setValue(oldSystemData.m_paravirtProvider);
    if (m_pEditorAccelerationFeatures)
        m_pEditorAccelerationFeatures->setEnableNestedPaging(oldSystemData.m_fEnabledNestedPaging);

    /* Polish page finally: */
    polishPage();

    /* Revalidate: */
    revalidate();
}

void UIMachineSettingsSystem::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Some options for x86 machines only: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_x86;
    const bool fx86Machine = enmArch == KPlatformArchitecture_x86;

    /* Prepare new data: */
    UIDataSettingsMachineSystem newSystemData;

    /* Gather support flags: */
    newSystemData.m_fSupportedPAE = m_pCache->base().m_fSupportedPAE;
    newSystemData.m_fSupportedNestedHwVirtEx = isNestedHWVirtExSupported();
    newSystemData.m_fSupportedHwVirtEx = isHWVirtExSupported();
    newSystemData.m_fSupportedNestedPaging = isNestedPagingSupported();

    /* Gather 'Motherboard' data: */
    if (m_pEditorBaseMemory)
        newSystemData.m_iMemorySize = m_pEditorBaseMemory->value();
    if (m_pEditorBootOrder)
        newSystemData.m_bootItems = m_pEditorBootOrder->value();
    newSystemData.m_chipsetType = chipsetType();
    if (m_pEditorTpm)
        newSystemData.m_tpmType = m_pEditorTpm->value();
    if (m_pEditorPointingHID)
        newSystemData.m_pointingHIDType = m_pEditorPointingHID->value();
    if (   m_pEditorMotherboardFeatures
        && m_pEditorVCPU)
        newSystemData.m_fEnabledIoApic =    m_pEditorMotherboardFeatures->isEnabledIoApic()
                                         || (m_pEditorVCPU->value() > 1 && fx86Machine)
                                         || (chipsetType() == KChipsetType_ICH9 && fx86Machine);
    if (m_pEditorMotherboardFeatures)
        newSystemData.m_fEnabledEFI = m_pEditorMotherboardFeatures->isEnabledEfi();
    if (m_pEditorMotherboardFeatures)
        newSystemData.m_fEnabledUTC = m_pEditorMotherboardFeatures->isEnabledUtcTime();
    if (m_pEditorMotherboardFeatures)
    {
        newSystemData.m_fAvailableSecureBoot = m_pCache->base().m_fAvailableSecureBoot;
        newSystemData.m_fEnabledSecureBoot = m_pEditorMotherboardFeatures->isEnabledSecureBoot();
        newSystemData.m_fResetSecureBoot = m_pEditorMotherboardFeatures->isResetSecureBoot();
    }

    /* Gather 'Processor' data: */
    if (m_pEditorVCPU)
        newSystemData.m_cCPUCount = m_pEditorVCPU->value();
    if (m_pEditorExecCap)
        newSystemData.m_iCPUExecCap = m_pEditorExecCap->value();
    if (m_pEditorProcessorFeatures)
        newSystemData.m_fEnabledPAE = m_pEditorProcessorFeatures->isEnabledPae();
    newSystemData.m_fEnabledNestedHwVirtEx = isNestedHWVirtExEnabled();

    /* Gather 'Acceleration' data: */
    if (m_pEditorParavirtProvider)
        newSystemData.m_paravirtProvider = m_pEditorParavirtProvider->value();
    /* Enable Nested Paging automatically if it's supported and
     * Nested HW Virt Ex is requested. */
    newSystemData.m_fEnabledNestedPaging =    isNestedPagingEnabled()
                                           || (   isNestedPagingSupported()
                                               && isNestedHWVirtExEnabled());

    /* Cache new data: */
    m_pCache->cacheCurrentData(newSystemData);
}

void UIMachineSettingsSystem::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

bool UIMachineSettingsSystem::validate(QList<UIValidationMessage> &messages)
{
    /* Pass by default: */
    bool fPass = true;

    /* Some options for x86 machines only: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_x86;
    const bool fx86Machine = enmArch == KPlatformArchitecture_x86;

    /* Motherboard tab: */
    {
        /* Prepare message: */
        UIValidationMessage message;
        message.first = UITranslator::removeAccelMark(m_pTabWidget->tabText(0));

        /* RAM amount test: */
        const ulong uFullSize = gpGlobalSession->host().GetMemorySize();
        if (m_pEditorBaseMemory->value() > (int)m_pEditorBaseMemory->maxRAMAlw())
        {
            message.second << tr(
                "More than <b>%1%</b> of the host computer's memory (<b>%2</b>) is assigned to the virtual machine. "
                "Not enough memory is left for the host operating system. Please select a smaller amount.")
                .arg((unsigned)qRound((double)m_pEditorBaseMemory->maxRAMAlw() / uFullSize * 100.0))
                .arg(UITranslator::formatSize((uint64_t)uFullSize * _1M));
            fPass = false;
        }
        else if (m_pEditorBaseMemory->value() > (int)m_pEditorBaseMemory->maxRAMOpt())
        {
            message.second << tr(
                "More than <b>%1%</b> of the host computer's memory (<b>%2</b>) is assigned to the virtual machine. "
                "There might not be enough memory left for the host operating system. Please consider selecting a smaller amount.")
                .arg((unsigned)qRound((double)m_pEditorBaseMemory->maxRAMOpt() / uFullSize * 100.0))
                .arg(UITranslator::formatSize((uint64_t)uFullSize * _1M));
        }

        /* Chipset type vs IO-APIC test: */
        if (   fx86Machine
            && chipsetType() == KChipsetType_ICH9
            && !m_pEditorMotherboardFeatures->isEnabledIoApic())
        {
            message.second << tr(
                "The I/O APIC feature is not currently enabled in the Motherboard section of the System page. "
                "This is needed to support a chipset of type ICH9. "
                "It will be enabled automatically if you confirm your changes.");
        }

        /* HID vs USB test: */
        if (isHIDEnabled() && !m_fIsUSBEnabled)
        {
            message.second << tr(
                "The USB controller emulation is not currently enabled on the USB page. "
                "This is needed to support an emulated USB pointing device. "
                "It will be enabled automatically if you confirm your changes.");
        }

        /* Serialize message: */
        if (!message.second.isEmpty())
            messages << message;
    }

    /* CPU tab: */
    {
        /* Prepare message: */
        UIValidationMessage message;
        message.first = UITranslator::removeAccelMark(m_pTabWidget->tabText(1));

        /* VCPU amount test: */
        const int cTotalCPUs = gpGlobalSession->host().GetProcessorOnlineCoreCount();
        if (m_pEditorVCPU->value() > 2 * cTotalCPUs)
        {
            message.second << tr(
                "For performance reasons, the number of virtual CPUs attached to the virtual machine may not be more than twice the number "
                "of physical CPUs on the host (<b>%1</b>). Please reduce the number of virtual CPUs.")
                .arg(cTotalCPUs);
            fPass = false;
        }
        else if (m_pEditorVCPU->value() > cTotalCPUs)
        {
            message.second << tr(
                "More virtual CPUs are assigned to the virtual machine than the number of physical CPUs on the host system (<b>%1</b>). "
                "This is likely to degrade the performance of your virtual machine. Please consider reducing the number of virtual CPUs.")
                .arg(cTotalCPUs);
        }

        /* VCPU vs IO-APIC test: */
        if (   fx86Machine
            && m_pEditorVCPU->value() > 1
            && !m_pEditorMotherboardFeatures->isEnabledIoApic())
        {
            message.second << tr(
                "The I/O APIC feature is not currently enabled in the Motherboard section of the System page. "
                "This is needed to support more than one virtual processor. "
                "It will be enabled automatically if you confirm your changes.");
        }

        /* CPU execution cap test: */
        if (m_pEditorExecCap->value() < m_pEditorExecCap->medExecCap())
        {
            message.second << tr("The processor execution cap is set to a low value. This may make the machine feel slow to respond.");
        }

        /* Warn user about possible performance degradation and suggest lowering # of CPUs assigned to the VM instead: */
        if (m_pEditorExecCap->value() < 100)
        {
            if (m_pEditorVCPU->maxVCPUCount() > 1 && m_pEditorVCPU->value() > 1)
            {
                message.second << tr("Please consider lowering the number of CPUs assigned to the virtual machine rather "
                                     "than setting the processor execution cap.");
            }
            else if (m_pEditorVCPU->maxVCPUCount() > 1)
            {
                message.second << tr("Lowering the processor execution cap may result in a decline in performance.");
            }
        }

        /* Nested HW Virt Ex: */
        if (isNestedHWVirtExEnabled())
        {
            /* Nested Paging test: */
            if (isHWVirtExSupported() && isNestedPagingSupported() && !isNestedPagingEnabled())
            {
                message.second << tr(
                    "The nested paging is not currently enabled in the Acceleration section of the System page. "
                    "This is needed to support nested hardware virtualization. "
                    "It will be enabled automatically if you confirm your changes.");
            }
        }

        /* Serialize message: */
        if (!message.second.isEmpty())
            messages << message;
    }

    /* Return result: */
    return fPass;
}

void UIMachineSettingsSystem::setOrderAfter(QWidget *pWidget)
{
    /* Configure navigation for 'motherboard' tab: */
    setTabOrder(pWidget, m_pTabWidget->focusProxy());
    setTabOrder(m_pTabWidget->focusProxy(), m_pEditorBaseMemory);
    setTabOrder(m_pEditorBaseMemory, m_pEditorBootOrder);
    setTabOrder(m_pEditorBootOrder, m_pEditorChipset);
    setTabOrder(m_pEditorChipset, m_pEditorTpm);
    setTabOrder(m_pEditorTpm, m_pEditorPointingHID);
    setTabOrder(m_pEditorPointingHID, m_pEditorMotherboardFeatures);
    setTabOrder(m_pEditorMotherboardFeatures, m_pEditorVCPU);

    /* Configure navigation for 'processor' tab: */
    setTabOrder(m_pEditorVCPU, m_pEditorExecCap);
    setTabOrder(m_pEditorExecCap, m_pEditorProcessorFeatures);
    setTabOrder(m_pEditorProcessorFeatures, m_pEditorParavirtProvider);

    /* Configure navigation for 'acceleration' tab: */
    setTabOrder(m_pEditorParavirtProvider, m_pEditorAccelerationFeatures);
}

void UIMachineSettingsSystem::sltRetranslateUI()
{
    m_pTabWidget->setTabText(m_pTabWidget->indexOf(m_pTabMotherboard), tr("&Motherboard"));
    m_pTabWidget->setTabText(m_pTabWidget->indexOf(m_pTabProcessor), tr("&Processor"));
    m_pTabWidget->setTabText(m_pTabWidget->indexOf(m_pTabAcceleration), tr("Acce&leration"));

    updateMinimumLayoutHint();
}

void UIMachineSettingsSystem::sltHandleFirmwareTypeChanged()
{
    /// @todo move all the System page editors to new encapsulated UISystemSettingsEditor class,
    ///       cause we are hiding logic in separate editors, not pages, this is the 1st bit of logic.
    m_pEditorBootOrder->setEnabled(!m_pEditorMotherboardFeatures->isEnabledEfi());
}

void UIMachineSettingsSystem::handleFilterChange()
{
    /* Update some stuff: */
    updateMinimumLayoutHint();

    /* Some options hidden for ARM machines: */
    const KPlatformArchitecture enmArch = optionalFlags().contains("arch")
                                        ? optionalFlags().value("arch").value<KPlatformArchitecture>()
                                        : KPlatformArchitecture_x86;
    const bool fARMMachine = enmArch == KPlatformArchitecture_ARM;
    if (fARMMachine)
    {
        if (m_pEditorChipset)
            m_pEditorChipset->hide();
        if (m_pEditorProcessorFeatures)
            m_pEditorProcessorFeatures->hide();
    }
}

void UIMachineSettingsSystem::polishPage()
{
    /* Get old data from cache: */
    const UIDataSettingsMachineSystem &systemData = m_pCache->base();

    /* Polish 'Motherboard' availability: */
    m_pEditorBaseMemory->setEnabled(isMachineOffline());
    m_pEditorBootOrder->setEnabled(isMachineOffline());
    if (m_pEditorChipset)
        m_pEditorChipset->setEnabled(isMachineOffline());
    if (m_pEditorTpm)
        m_pEditorTpm->setEnabled(isMachineOffline());
    m_pEditorPointingHID->setEnabled(isMachineOffline());
    m_pEditorMotherboardFeatures->setEnabled(isMachineOffline());
    sltHandleFirmwareTypeChanged();

    /* Polish 'Processor' availability: */
    m_pEditorVCPU->setEnabled(isMachineOffline() && systemData.m_fSupportedHwVirtEx);
    m_pEditorExecCap->setEnabled(isMachineInValidMode());
    m_pEditorProcessorFeatures->setEnablePaeAvailable(isMachineOffline() && systemData.m_fSupportedPAE);
    m_pEditorProcessorFeatures->setEnableNestedVirtualizationAvailable(   isMachineOffline()
                                                                       && (   systemData.m_fSupportedNestedHwVirtEx
                                                                           || systemData.m_fEnabledNestedHwVirtEx));

    /* Polish 'Acceleration' availability: */
    m_pEditorParavirtProvider->setEnabled(isMachineOffline());
    if (m_pEditorAccelerationFeatures)
    {
        m_pEditorAccelerationFeatures->setEnabled(isMachineOffline());
        m_pEditorAccelerationFeatures->setEnableNestedPagingAvailable(   (systemData.m_fSupportedNestedPaging && isMachineOffline())
                                                                      || (systemData.m_fEnabledNestedPaging && isMachineOffline()));
    }
}

void UIMachineSettingsSystem::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheMachineSystem;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMachineSettingsSystem::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* Prepare tab-widget: */
        m_pTabWidget = new QITabWidget(this);
        if (m_pTabWidget)
        {
            /* Prepare each tab separately: */
            prepareTabMotherboard();
            prepareTabProcessor();
            prepareTabAcceleration();

            pLayoutMain->addWidget(m_pTabWidget);
        }
    }
}

void UIMachineSettingsSystem::prepareTabMotherboard()
{
    /* Prepare 'Motherboard' tab: */
    m_pTabMotherboard = new UIEditor(m_pTabWidget);
    if (m_pTabMotherboard)
    {
        /* Prepare 'Motherboard' tab layout: */
        QGridLayout *pLayoutMotherboard = new QGridLayout(m_pTabMotherboard);
        if (pLayoutMotherboard)
        {
            pLayoutMotherboard->setColumnStretch(1, 1);
            pLayoutMotherboard->setRowStretch(6, 1);
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayoutMotherboard->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayoutMotherboard->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare base memory editor: */
            m_pEditorBaseMemory = new UIBaseMemoryEditor(m_pTabMotherboard);
            if (m_pEditorBaseMemory)
            {
                m_pTabMotherboard->addEditor(m_pEditorBaseMemory);
                pLayoutMotherboard->addWidget(m_pEditorBaseMemory, 0, 0, 1, 2);
            }

            /* Prepare boot order editor: */
            m_pEditorBootOrder = new UIBootOrderEditor(m_pTabMotherboard);
            if (m_pEditorBootOrder)
            {
                m_pTabMotherboard->addEditor(m_pEditorBootOrder);
                pLayoutMotherboard->addWidget(m_pEditorBootOrder, 1, 0);
            }

            /* Prepare chipset editor: */
            m_pEditorChipset = new UIChipsetEditor(m_pTabMotherboard);
            if (m_pEditorChipset)
            {
                m_pTabMotherboard->addEditor(m_pEditorChipset);
                pLayoutMotherboard->addWidget(m_pEditorChipset, 2, 0);
            }

            /* Prepare TPM editor: */
            m_pEditorTpm = new UITpmEditor(m_pTabMotherboard);
            if (m_pEditorTpm)
            {
                m_pTabMotherboard->addEditor(m_pEditorTpm);
                pLayoutMotherboard->addWidget(m_pEditorTpm, 3, 0);
            }

            /* Prepare pointing HID editor: */
            m_pEditorPointingHID = new UIPointingHIDEditor(m_pTabMotherboard);
            if (m_pEditorPointingHID)
            {
                m_pTabMotherboard->addEditor(m_pEditorPointingHID);
                pLayoutMotherboard->addWidget(m_pEditorPointingHID, 4, 0);
            }

            /* Prepare motherboard features editor: */
            m_pEditorMotherboardFeatures = new UIMotherboardFeaturesEditor(m_pTabMotherboard);
            if (m_pEditorMotherboardFeatures)
            {
                m_pTabMotherboard->addEditor(m_pEditorMotherboardFeatures);
                pLayoutMotherboard->addWidget(m_pEditorMotherboardFeatures, 5, 0);
            }
        }

        addEditor(m_pTabMotherboard);
        m_pTabWidget->addTab(m_pTabMotherboard, QString());
    }
}

void UIMachineSettingsSystem::prepareTabProcessor()
{
    /* Prepare 'Processor' tab: */
    m_pTabProcessor = new UIEditor(m_pTabWidget);
    if (m_pTabProcessor)
    {
        /* Prepare 'Processor' tab layout: */
        QGridLayout *pLayoutProcessor = new QGridLayout(m_pTabProcessor);
        if (pLayoutProcessor)
        {
            pLayoutProcessor->setColumnStretch(1, 1);
            pLayoutProcessor->setRowStretch(3, 1);
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayoutProcessor->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayoutProcessor->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare VCPU editor : */
            m_pEditorVCPU = new UIVirtualCPUEditor(m_pTabProcessor);
            if (m_pEditorVCPU)
            {
                m_pTabProcessor->addEditor(m_pEditorVCPU);
                pLayoutProcessor->addWidget(m_pEditorVCPU, 0, 0, 1, 2);
            }

            /* Prepare exec cap editor : */
            m_pEditorExecCap = new UIExecutionCapEditor(m_pTabProcessor);
            if (m_pEditorExecCap)
            {
                m_pTabProcessor->addEditor(m_pEditorExecCap);
                pLayoutProcessor->addWidget(m_pEditorExecCap, 1, 0, 1, 2);
            }

            /* Prepare processor features editor: */
            m_pEditorProcessorFeatures = new UIProcessorFeaturesEditor(m_pTabProcessor);
            if (m_pEditorProcessorFeatures)
            {
                m_pTabProcessor->addEditor(m_pEditorProcessorFeatures);
                pLayoutProcessor->addWidget(m_pEditorProcessorFeatures, 2, 0);
            }
        }

        addEditor(m_pTabProcessor);
        m_pTabWidget->addTab(m_pTabProcessor, QString());
    }
}

void UIMachineSettingsSystem::prepareTabAcceleration()
{
    /* Prepare 'Acceleration' tab: */
    m_pTabAcceleration = new UIEditor(m_pTabWidget);
    if (m_pTabAcceleration)
    {
        /* Prepare 'Acceleration' tab layout: */
        QGridLayout *pLayoutAcceleration = new QGridLayout(m_pTabAcceleration);
        if (pLayoutAcceleration)
        {
            pLayoutAcceleration->setColumnStretch(2, 1);
            pLayoutAcceleration->setRowStretch(3, 1);
#ifdef VBOX_WS_MAC
            /* On Mac OS X we can do a bit of smoothness: */
            int iLeft, iTop, iRight, iBottom;
            pLayoutAcceleration->getContentsMargins(&iLeft, &iTop, &iRight, &iBottom);
            pLayoutAcceleration->setContentsMargins(iLeft / 2, iTop / 2, iRight / 2, iBottom / 2);
#endif

            /* Prepare paravirtualization provider editor: */
            m_pEditorParavirtProvider = new UIParavirtProviderEditor(m_pTabAcceleration);
            if (m_pEditorParavirtProvider)
            {
                m_pTabAcceleration->addEditor(m_pEditorParavirtProvider);
                pLayoutAcceleration->addWidget(m_pEditorParavirtProvider, 0, 0, 1, 2);
            }

            /* Prepare acceleration features editor: */
#ifndef VBOX_WITH_VIRT_ARMV8
            m_pEditorAccelerationFeatures = new UIAccelerationFeaturesEditor(m_pTabAcceleration);
#endif
            if (m_pEditorAccelerationFeatures)
            {
                m_pTabAcceleration->addEditor(m_pEditorAccelerationFeatures);
                pLayoutAcceleration->addWidget(m_pEditorAccelerationFeatures, 1, 0);
            }

            addEditor(m_pTabAcceleration);
            m_pTabWidget->addTab(m_pTabAcceleration, QString());
        }
    }
}

void UIMachineSettingsSystem::prepareConnections()
{
    /* Configure 'Motherboard' connections: */
    if (m_pEditorChipset)
        connect(m_pEditorChipset, &UIChipsetEditor::sigValueChanged,
                this, &UIMachineSettingsSystem::revalidate);
    if (m_pEditorTpm)
        connect(m_pEditorTpm, &UITpmEditor::sigValueChanged,
                this, &UIMachineSettingsSystem::revalidate);
    connect(m_pEditorPointingHID, &UIPointingHIDEditor::sigValueChanged,
            this, &UIMachineSettingsSystem::revalidate);
    connect(m_pEditorBaseMemory, &UIBaseMemoryEditor::sigValidChanged,
            this, &UIMachineSettingsSystem::revalidate);
    connect(m_pEditorMotherboardFeatures, &UIMotherboardFeaturesEditor::sigChangedIoApic,
            this, &UIMachineSettingsSystem::revalidate);
    connect(m_pEditorMotherboardFeatures, &UIMotherboardFeaturesEditor::sigChangedEfi,
            this, &UIMachineSettingsSystem::sltHandleFirmwareTypeChanged);

    /* Configure 'Processor' connections: */
    connect(m_pEditorVCPU, &UIVirtualCPUEditor::sigValueChanged,
            this, &UIMachineSettingsSystem::revalidate);
    connect(m_pEditorExecCap, &UIExecutionCapEditor::sigValueChanged,
            this, &UIMachineSettingsSystem::revalidate);
    connect(m_pEditorProcessorFeatures, &UIProcessorFeaturesEditor::sigChangedNestedVirtualization,
            this, &UIMachineSettingsSystem::revalidate);

    /* Configure 'Acceleration' connections: */
    if (m_pEditorAccelerationFeatures)
        connect(m_pEditorAccelerationFeatures, &UIAccelerationFeaturesEditor::sigChangedNestedPaging,
                this, &UIMachineSettingsSystem::revalidate);
}

void UIMachineSettingsSystem::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIMachineSettingsSystem::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save general settings from cache: */
    if (fSuccess && isMachineInValidMode() && m_pCache->wasChanged())
    {
        /* Save 'Motherboard' data from cache: */
        if (fSuccess)
            fSuccess = saveMotherboardData();
        /* Save 'Processor' data from cache: */
        if (fSuccess)
            fSuccess = saveProcessorData();
        /* Save 'Acceleration' data from cache: */
        if (fSuccess)
            fSuccess = saveAccelerationData();
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSystem::saveMotherboardData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save 'Motherboard' settings from cache: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineSystem &oldSystemData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsMachineSystem &newSystemData = m_pCache->data();

        /* Save memory size: */
        if (fSuccess && isMachineOffline() && newSystemData.m_iMemorySize != oldSystemData.m_iMemorySize)
        {
            m_machine.SetMemorySize(newSystemData.m_iMemorySize);
            fSuccess = m_machine.isOk();
        }
        /* Save boot items: */
        if (fSuccess && isMachineOffline() && newSystemData.m_bootItems != oldSystemData.m_bootItems)
        {
            saveBootItems(newSystemData.m_bootItems, m_machine);
            fSuccess = m_machine.isOk();
        }
        /* Save chipset type: */
        if (fSuccess && isMachineOffline() && newSystemData.m_chipsetType != oldSystemData.m_chipsetType)
        {
            CPlatform comPlatform = m_machine.GetPlatform();
            comPlatform.SetChipsetType(newSystemData.m_chipsetType);
            if (!comPlatform.isOk())
            {
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comPlatform));
                return false;
            }
        }
        /* Save TPM type: */
        if (fSuccess && isMachineOffline() && newSystemData.m_tpmType != oldSystemData.m_tpmType)
        {
            CTrustedPlatformModule comModule = m_machine.GetTrustedPlatformModule();
            comModule.SetType(newSystemData.m_tpmType);
            if (!comModule.isOk())
            {
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comModule));
                return false;
            }
        }
        /* Save pointing HID type: */
        if (fSuccess && isMachineOffline() && newSystemData.m_pointingHIDType != oldSystemData.m_pointingHIDType)
        {
            m_machine.SetPointingHIDType(newSystemData.m_pointingHIDType);
            fSuccess = m_machine.isOk();
        }
        /* Save whether IO APIC is enabled: */
        if (fSuccess && isMachineOffline() && newSystemData.m_fEnabledIoApic != oldSystemData.m_fEnabledIoApic)
        {
            CFirmwareSettings comFirmwareSettings = m_machine.GetFirmwareSettings();
            comFirmwareSettings.SetIOAPICEnabled(newSystemData.m_fEnabledIoApic);
            if (!comFirmwareSettings.isOk())
            {
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comFirmwareSettings));
                return false;
            }
        }
        /* Save firware type (whether EFI is enabled): */
        if (fSuccess && isMachineOffline() && newSystemData.m_fEnabledEFI != oldSystemData.m_fEnabledEFI)
        {
            CFirmwareSettings comFirmwareSettings = m_machine.GetFirmwareSettings();
            comFirmwareSettings.SetFirmwareType(newSystemData.m_fEnabledEFI ? KFirmwareType_EFI : KFirmwareType_BIOS);
            if (!comFirmwareSettings.isOk())
            {
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comFirmwareSettings));
                return false;
            }
        }
        /* Save whether UTC is enabled: */
        if (fSuccess && isMachineOffline() && newSystemData.m_fEnabledUTC != oldSystemData.m_fEnabledUTC)
        {
            CPlatform comPlatform = m_machine.GetPlatform();
            comPlatform.SetRTCUseUTC(newSystemData.m_fEnabledUTC);
            if (!comPlatform.isOk())
            {
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comPlatform));
                return false;
            }
        }
        /* Save whether secure boot is enabled: */
        if (   fSuccess && isMachineOffline()
            && (   newSystemData.m_fEnabledSecureBoot != oldSystemData.m_fEnabledSecureBoot
                || newSystemData.m_fResetSecureBoot != oldSystemData.m_fResetSecureBoot))
        {
            CNvramStore comStoreLvl1 = m_machine.GetNonVolatileStore();
            CUefiVariableStore comStoreLvl2 = comStoreLvl1.GetUefiVariableStore();

            /* Enabling secure boot? */
            if (   newSystemData.m_fEnabledSecureBoot
                && newSystemData.m_fEnabledEFI)
            {
                /* Secure boot was NOT available
                 * or requested to be reseted: */
                if (   !newSystemData.m_fAvailableSecureBoot
                    || newSystemData.m_fResetSecureBoot)
                {
                    /* Init if required: */
                    if (!newSystemData.m_fAvailableSecureBoot)
                    {
                        comStoreLvl1.InitUefiVariableStore(0);
                        if (!comStoreLvl1.isOk())
                        {
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(comStoreLvl1));
                            return false;
                        }
                    }
                    /* Enroll everything: */
                    comStoreLvl2 = comStoreLvl1.GetUefiVariableStore();
                    comStoreLvl2.EnrollOraclePlatformKey();
                    if (!comStoreLvl2.isOk())
                    {
                        notifyOperationProgressError(UIErrorString::formatErrorInfo(comStoreLvl2));
                        return false;
                    }
                    comStoreLvl2.EnrollDefaultMsSignatures();
                    if (!comStoreLvl2.isOk())
                    {
                        notifyOperationProgressError(UIErrorString::formatErrorInfo(comStoreLvl2));
                        return false;
                    }
                }
                comStoreLvl2.SetSecureBootEnabled(true);
                if (!comStoreLvl2.isOk())
                {
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(comStoreLvl2));
                    return false;
                }
            }
            /* Disabling secure boot? */
            else if (!newSystemData.m_fEnabledSecureBoot)
            {
                comStoreLvl2.SetSecureBootEnabled(false);
                if (!comStoreLvl2.isOk())
                {
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(comStoreLvl2));
                    return false;
                }
            }
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSystem::saveProcessorData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save 'Processor' settings from cache: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineSystem &oldSystemData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsMachineSystem &newSystemData = m_pCache->data();

        /* Save CPU count: */
        if (fSuccess && isMachineOffline() && newSystemData.m_cCPUCount != oldSystemData.m_cCPUCount)
        {
            m_machine.SetCPUCount(newSystemData.m_cCPUCount);
            fSuccess = m_machine.isOk();
        }
        if (fSuccess)
        {
            const CPlatform comPlatform = m_machine.GetPlatform();
            switch (comPlatform.GetArchitecture())
            {
                case KPlatformArchitecture_x86:
                {
                    CPlatformX86 comPlatformX86 = comPlatform.GetX86();

                    /* Save whether PAE is enabled: */
                    if (/*fSuccess &&*/ isMachineOffline() && newSystemData.m_fEnabledPAE != oldSystemData.m_fEnabledPAE)
                    {
                        comPlatformX86.SetCPUProperty(KCPUPropertyTypeX86_PAE, newSystemData.m_fEnabledPAE);
                        if (!comPlatformX86.isOk())
                        {
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(comPlatformX86));
                            return false;
                        }
                    }
                    /* Save whether Nested HW Virt Ex is enabled: */
                    if (fSuccess && isMachineOffline() && newSystemData.m_fEnabledNestedHwVirtEx != oldSystemData.m_fEnabledNestedHwVirtEx)
                    {
                        comPlatformX86.SetCPUProperty(KCPUPropertyTypeX86_HWVirt, newSystemData.m_fEnabledNestedHwVirtEx);
                        if (!comPlatformX86.isOk())
                        {
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(comPlatformX86));
                            return false;
                        }
                    }

                    break;
                }

#ifdef VBOX_WITH_VIRT_ARMV8
                case KPlatformArchitecture_ARM:
                {
                    /** @todo BUGBUG ARM stuff goes here. */
                    break;
                }
#endif

                default:
                    break;
            }
        }
        /* Save CPU execution cap: */
        if (fSuccess && newSystemData.m_iCPUExecCap != oldSystemData.m_iCPUExecCap)
        {
            m_machine.SetCPUExecutionCap(newSystemData.m_iCPUExecCap);
            fSuccess = m_machine.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsSystem::saveAccelerationData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save 'Acceleration' settings from cache: */
    if (fSuccess)
    {
        /* Get old data from cache: */
        const UIDataSettingsMachineSystem &oldSystemData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsMachineSystem &newSystemData = m_pCache->data();

        /* Save paravirtualization provider: */
        if (fSuccess && isMachineOffline() && newSystemData.m_paravirtProvider != oldSystemData.m_paravirtProvider)
        {
            m_machine.SetParavirtProvider(newSystemData.m_paravirtProvider);
            fSuccess = m_machine.isOk();
        }
        if (fSuccess)
        {
            const CPlatform comPlatform = m_machine.GetPlatform();
            switch (comPlatform.GetArchitecture())
            {
                case KPlatformArchitecture_x86:
                {
                    CPlatformX86 comPlatformX86 = comPlatform.GetX86();

                    /* Save whether the nested paging is enabled: */
                    if (/*fSuccess &&*/ isMachineOffline() && newSystemData.m_fEnabledNestedPaging != oldSystemData.m_fEnabledNestedPaging)
                    {
                        comPlatformX86.SetHWVirtExProperty(KHWVirtExPropertyType_NestedPaging, newSystemData.m_fEnabledNestedPaging);
                        if (!comPlatformX86.isOk())
                        {
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(comPlatformX86));
                            return false;
                        }
                    }

                    break;
                }

#ifdef VBOX_WITH_VIRT_ARMV8
                case KPlatformArchitecture_ARM:
                {
                    /** @todo BUGBUG ARM stuff goes here. */
                    break;
                }
#endif

                default:
                    break;
             }
        }

         /* Show error message if necessary: */
         if (!fSuccess)
             notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
    }
    /* Return result: */
    return fSuccess;
}

void UIMachineSettingsSystem::updateMinimumLayoutHint()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    if (m_pEditorBaseMemory && !m_pEditorBaseMemory->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorBaseMemory->minimumLabelHorizontalHint());
    if (m_pEditorBootOrder && !m_pEditorBootOrder->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorBootOrder->minimumLabelHorizontalHint());
    if (m_pEditorChipset && !m_pEditorChipset->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorChipset->minimumLabelHorizontalHint());
    if (m_pEditorTpm && !m_pEditorTpm->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorTpm->minimumLabelHorizontalHint());
    if (m_pEditorPointingHID && !m_pEditorPointingHID->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorPointingHID->minimumLabelHorizontalHint());
    if (m_pEditorMotherboardFeatures && !m_pEditorMotherboardFeatures->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorMotherboardFeatures->minimumLabelHorizontalHint());
    if (m_pEditorBaseMemory)
        m_pEditorBaseMemory->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorBootOrder)
        m_pEditorBootOrder->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorChipset)
        m_pEditorChipset->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorTpm)
        m_pEditorTpm->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorPointingHID)
        m_pEditorPointingHID->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorMotherboardFeatures)
        m_pEditorMotherboardFeatures->setMinimumLayoutIndent(iMinimumLayoutHint);

    iMinimumLayoutHint = 0;
    if (m_pEditorVCPU && !m_pEditorVCPU->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorVCPU->minimumLabelHorizontalHint());
    if (m_pEditorExecCap && !m_pEditorExecCap->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorExecCap->minimumLabelHorizontalHint());
    if (m_pEditorProcessorFeatures && !m_pEditorProcessorFeatures->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorProcessorFeatures->minimumLabelHorizontalHint());
    if (m_pEditorVCPU)
        m_pEditorVCPU->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorExecCap)
        m_pEditorExecCap->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorProcessorFeatures)
        m_pEditorProcessorFeatures->setMinimumLayoutIndent(iMinimumLayoutHint);

    iMinimumLayoutHint = 0;
    if (m_pEditorParavirtProvider && !m_pEditorParavirtProvider->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorParavirtProvider->minimumLabelHorizontalHint());
    if (m_pEditorAccelerationFeatures && !m_pEditorAccelerationFeatures->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorAccelerationFeatures->minimumLabelHorizontalHint());
    if (m_pEditorParavirtProvider)
        m_pEditorParavirtProvider->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorAccelerationFeatures)
        m_pEditorAccelerationFeatures->setMinimumLayoutIndent(iMinimumLayoutHint);
}
