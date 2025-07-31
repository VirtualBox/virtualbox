/* $Id: PlatformPropertiesImpl.cpp 110370 2025-07-23 09:14:02Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox COM class implementation - Platform properties.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_PLATFORMPROPERTIES
#include "PlatformPropertiesImpl.h"
#include "GraphicsAdapterImpl.h" /* For static helper functions. */
#include "VirtualBoxImpl.h"
#include "LoggingNew.h"
#include "Global.h"

#include <algorithm>

#include <iprt/asm.h>
#include <iprt/cpp/utils.h>

#include <VBox/param.h> /* For VRAM ranges. */
#include <VBox/settings.h>

// generated header
#include "SchemaDefs.h"


/*
 * PlatformProperties implementation.
 */
PlatformProperties::PlatformProperties()
    : mParent(NULL)
    , mPlatformArchitecture(PlatformArchitecture_None)
    , mfIsHost(false)
{
}

PlatformProperties::~PlatformProperties()
{
    uninit();
}

HRESULT PlatformProperties::FinalConstruct()
{
    return BaseFinalConstruct();
}

void PlatformProperties::FinalRelease()
{
    uninit();

    BaseFinalRelease();
}

/**
 * Initializes platform properties.
 *
 * @returns HRESULT
 * @param   aParent             Pointer to IVirtualBox parent object (weak).
 * @param   fIsHost             Set to \c true if this instance handles platform properties of the host,
 *                              or set to \c false for guests (default).
 */
HRESULT PlatformProperties::init(VirtualBox *aParent, bool fIsHost /* = false */)
{
    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent)  = aParent;
    unconst(mfIsHost) = fIsHost;

    if (mfIsHost)
    {
        /* On Windows, macOS and Solaris hosts, HW virtualization use isn't exclusive
         * by default so that VT-x or AMD-V can be shared with other
         * hypervisors without requiring user intervention.
         * NB: See also PlatformProperties constructor in settings.h
         */
#if defined(RT_OS_DARWIN) || defined(RT_OS_WINDOWS) || defined(RT_OS_SOLARIS)
        mData.fExclusiveHwVirt = false; /** @todo BUGBUG Applies for MacOS on ARM as well? */
#else
        mData.fExclusiveHwVirt = true;
#endif
    }

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 * Sets the platform architecture.
 *
 * @returns HRESULT
 * @param   aArchitecture       Platform architecture to set.
 *
 * @note    Usually only called when creating a new machine.
 */
HRESULT PlatformProperties::i_setArchitecture(PlatformArchitecture_T aArchitecture)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mPlatformArchitecture = aArchitecture;

    return S_OK;
}

/**
 * Returns the host's platform architecture.
 *
 * @returns The host's platform architecture.
 */
PlatformArchitecture_T PlatformProperties::s_getHostPlatformArchitecture()
{
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
    return PlatformArchitecture_x86;
#elif defined(RT_ARCH_ARM64) || defined(RT_ARCH_ARM32)
    return PlatformArchitecture_ARM;
#else
# error "Port me!"
    return PlatformArchitecture_None;
#endif
}

void PlatformProperties::uninit()
{
    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;
}

HRESULT PlatformProperties::getSerialPortCount(ULONG *count)
{
    /* no need to lock, this is const */
    *count = SchemaDefs::SerialPortCount;

    return S_OK;
}

HRESULT PlatformProperties::getParallelPortCount(ULONG *count)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            /* no need to lock, this is const */
            *count = SchemaDefs::ParallelPortCount;
            break;
        }

        case PlatformArchitecture_ARM:
        default:
        {
            *count = 0; /* Not supported. */
            break;
        }
    }

    return S_OK;
}

HRESULT PlatformProperties::getMaxBootPosition(ULONG *aMaxBootPosition)
{
    /* no need to lock, this is const */
    *aMaxBootPosition = SchemaDefs::MaxBootPosition;

    return S_OK;
}

HRESULT PlatformProperties::getRawModeSupported(BOOL *aRawModeSupported)
{
    *aRawModeSupported = FALSE;
    return S_OK;
}

HRESULT PlatformProperties::getExclusiveHwVirt(BOOL *aExclusiveHwVirt)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aExclusiveHwVirt = mData.fExclusiveHwVirt;

    /* Makes no sense for guest platform properties, but we return FALSE anyway. */
    return S_OK;
}

HRESULT PlatformProperties::setExclusiveHwVirt(BOOL aExclusiveHwVirt)
{
    /* Only makes sense when running in VBoxSVC, as this is a pure host setting -- ignored within clients. */
#ifdef IN_VBOXSVC
    /* No locking required, as mfIsHost is const. */
    if (!mfIsHost) /* Ignore setting the attribute if not host properties. */
        return S_OK;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    mData.fExclusiveHwVirt = !!aExclusiveHwVirt;
    alock.release();

    // VirtualBox::i_saveSettings() needs vbox write lock
    AutoWriteLock vboxLock(mParent COMMA_LOCKVAL_SRC_POS);
    return mParent->i_saveSettings();
#else /* VBoxC */
    RT_NOREF(aExclusiveHwVirt);
    return VBOX_E_NOT_SUPPORTED;
#endif
}

/* static */
ULONG PlatformProperties::s_getMaxNetworkAdapters(ChipsetType_T aChipset)
{
    AssertReturn(aChipset != ChipsetType_Null, 0);

    /* no need for locking, no state */
    switch (aChipset)
    {
        case ChipsetType_PIIX3:        return 8;
        case ChipsetType_ICH9:         return 36;
        case ChipsetType_ARMv8Virtual: return 64; /** @todo BUGBUG Put a sane value here. Just a wild guess for now. */
        case ChipsetType_Null:
           RT_FALL_THROUGH();
        default:
            break;
    }

    AssertMsgFailedReturn(("Chipset type %#x not handled\n", aChipset), 0);
}

HRESULT PlatformProperties::getMaxNetworkAdapters(ChipsetType_T aChipset, ULONG *aMaxNetworkAdapters)
{
    *aMaxNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(aChipset);

    return S_OK;
}

/* static */
ULONG PlatformProperties::s_getMaxNetworkAdaptersOfType(ChipsetType_T aChipset, NetworkAttachmentType_T aType)
{
    /* no need for locking, no state */
    uint32_t cMaxNetworkAdapters = PlatformProperties::s_getMaxNetworkAdapters(aChipset);

    switch (aType)
    {
        case NetworkAttachmentType_NAT:
        case NetworkAttachmentType_Internal:
        case NetworkAttachmentType_NATNetwork:
            /* chipset default is OK */
            break;
        case NetworkAttachmentType_Bridged:
            /* Maybe use current host interface count here? */
            break;
        case NetworkAttachmentType_HostOnly:
            cMaxNetworkAdapters = RT_MIN(cMaxNetworkAdapters, 8);
            break;
        default:
            AssertMsgFailed(("Unhandled attachment type %d\n", aType));
    }

    return cMaxNetworkAdapters;
}

HRESULT PlatformProperties::getMaxNetworkAdaptersOfType(ChipsetType_T aChipset, NetworkAttachmentType_T aType,
                                                        ULONG *aMaxNetworkAdapters)
{
    *aMaxNetworkAdapters = PlatformProperties::s_getMaxNetworkAdaptersOfType(aChipset, aType);

    return S_OK;
}

HRESULT PlatformProperties::getMaxDevicesPerPortForStorageBus(StorageBus_T aBus,
                                                              ULONG *aMaxDevicesPerPort)
{
    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        case StorageBus_SCSI:
        case StorageBus_SAS:
        case StorageBus_USB:
        case StorageBus_PCIe:
        case StorageBus_VirtioSCSI:
        {
            /* SATA and both SCSI controllers only support one device per port. */
            *aMaxDevicesPerPort = 1;
            break;
        }
        case StorageBus_IDE:
        case StorageBus_Floppy:
        {
            /* The IDE and Floppy controllers support 2 devices. One as master
             * and one as slave (or floppy drive 0 and 1). */
            *aMaxDevicesPerPort = 2;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

HRESULT PlatformProperties::getMinPortCountForStorageBus(StorageBus_T aBus,
                                                         ULONG *aMinPortCount)
{
    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        case StorageBus_SAS:
        case StorageBus_PCIe:
        case StorageBus_VirtioSCSI:
        {
            *aMinPortCount = 1;
            break;
        }
        case StorageBus_SCSI:
        {
            *aMinPortCount = 16;
            break;
        }
        case StorageBus_IDE:
        {
            *aMinPortCount = 2;
            break;
        }
        case StorageBus_Floppy:
        {
            *aMinPortCount = 1;
            break;
        }
        case StorageBus_USB:
        {
            *aMinPortCount = 8;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

HRESULT PlatformProperties::getMaxPortCountForStorageBus(StorageBus_T aBus,
                                                         ULONG *aMaxPortCount)
{
    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        {
            *aMaxPortCount = 30;
            break;
        }
        case StorageBus_SCSI:
        {
            *aMaxPortCount = 16;
            break;
        }
        case StorageBus_IDE:
        {
            *aMaxPortCount = 2;
            break;
        }
        case StorageBus_Floppy:
        {
            *aMaxPortCount = 1;
            break;
        }
        case StorageBus_SAS:
        case StorageBus_PCIe:
        {
            *aMaxPortCount = 255;
            break;
        }
        case StorageBus_USB:
        {
            *aMaxPortCount = 8;
            break;
        }
        case StorageBus_VirtioSCSI:
        {
            *aMaxPortCount = 256;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

HRESULT PlatformProperties::getMaxInstancesOfStorageBus(ChipsetType_T aChipset,
                                                        StorageBus_T  aBus,
                                                        ULONG *aMaxInstances)
{
    ULONG cCtrs = 0;

    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_SATA:
        case StorageBus_SCSI:
        case StorageBus_SAS:
        case StorageBus_PCIe:
        case StorageBus_VirtioSCSI:
            cCtrs = aChipset == ChipsetType_ICH9 || aChipset == ChipsetType_ARMv8Virtual ? 8 : 1;
            break;
        case StorageBus_USB:
        case StorageBus_IDE:
        case StorageBus_Floppy:
        {
            cCtrs = 1;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    *aMaxInstances = cCtrs;

    return S_OK;
}

HRESULT PlatformProperties::getDeviceTypesForStorageBus(StorageBus_T aBus,
                                                        std::vector<DeviceType_T> &aDeviceTypes)
{
    aDeviceTypes.resize(0);

    /* no need to lock, this is const */
    switch (aBus)
    {
        case StorageBus_IDE:
        case StorageBus_SATA:
        case StorageBus_SCSI:
        case StorageBus_SAS:
        case StorageBus_USB:
        case StorageBus_VirtioSCSI:
        {
            aDeviceTypes.resize(2);
            aDeviceTypes[0] = DeviceType_DVD;
            aDeviceTypes[1] = DeviceType_HardDisk;
            break;
        }
        case StorageBus_Floppy:
        {
            aDeviceTypes.resize(1);
            aDeviceTypes[0] = DeviceType_Floppy;
            break;
        }
        case StorageBus_PCIe:
        {
            aDeviceTypes.resize(1);
            aDeviceTypes[0] = DeviceType_HardDisk;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aBus));
    }

    return S_OK;
}

HRESULT PlatformProperties::getStorageBusForControllerType(StorageControllerType_T aStorageControllerType,
                                                           StorageBus_T *aStorageBus)
{
    /* no need to lock, this is const */
    switch (aStorageControllerType)
    {
        case StorageControllerType_LsiLogic:
        case StorageControllerType_BusLogic:
            *aStorageBus = StorageBus_SCSI;
            break;
        case StorageControllerType_IntelAhci:
            *aStorageBus = StorageBus_SATA;
            break;
        case StorageControllerType_PIIX3:
        case StorageControllerType_PIIX4:
        case StorageControllerType_ICH6:
            *aStorageBus = StorageBus_IDE;
            break;
        case StorageControllerType_I82078:
            *aStorageBus = StorageBus_Floppy;
            break;
        case StorageControllerType_LsiLogicSas:
            *aStorageBus = StorageBus_SAS;
            break;
        case StorageControllerType_USB:
            *aStorageBus = StorageBus_USB;
            break;
        case StorageControllerType_NVMe:
            *aStorageBus = StorageBus_PCIe;
            break;
        case StorageControllerType_VirtioSCSI:
            *aStorageBus = StorageBus_VirtioSCSI;
            break;
        default:
            return setError(E_FAIL, tr("Invalid storage controller type %d\n"), aStorageBus);
    }

    return S_OK;
}

HRESULT PlatformProperties::getStorageControllerTypesForBus(StorageBus_T aStorageBus,
                                                            std::vector<StorageControllerType_T> &aStorageControllerTypes)
{
    aStorageControllerTypes.resize(0);

    /* no need to lock, this is const */
    switch (aStorageBus)
    {
        case StorageBus_IDE:
            aStorageControllerTypes.resize(3);
            aStorageControllerTypes[0] = StorageControllerType_PIIX4;
            aStorageControllerTypes[1] = StorageControllerType_PIIX3;
            aStorageControllerTypes[2] = StorageControllerType_ICH6;
            break;
        case StorageBus_SATA:
            aStorageControllerTypes.resize(1);
            aStorageControllerTypes[0] = StorageControllerType_IntelAhci;
            break;
        case StorageBus_SCSI:
            aStorageControllerTypes.resize(2);
            aStorageControllerTypes[0] = StorageControllerType_LsiLogic;
            aStorageControllerTypes[1] = StorageControllerType_BusLogic;
            break;
        case StorageBus_Floppy:
            aStorageControllerTypes.resize(1);
            aStorageControllerTypes[0] = StorageControllerType_I82078;
            break;
        case StorageBus_SAS:
            aStorageControllerTypes.resize(1);
            aStorageControllerTypes[0] = StorageControllerType_LsiLogicSas;
            break;
        case StorageBus_USB:
            aStorageControllerTypes.resize(1);
            aStorageControllerTypes[0] = StorageControllerType_USB;
            break;
        case StorageBus_PCIe:
            aStorageControllerTypes.resize(1);
            aStorageControllerTypes[0] = StorageControllerType_NVMe;
            break;
        case StorageBus_VirtioSCSI:
            aStorageControllerTypes.resize(1);
            aStorageControllerTypes[0] = StorageControllerType_VirtioSCSI;
            break;
        default:
            return setError(E_FAIL, tr("Invalid storage bus %d\n"), aStorageBus);
    }

    return S_OK;
}

HRESULT PlatformProperties::getStorageControllerHotplugCapable(StorageControllerType_T aControllerType,
                                                               BOOL *aHotplugCapable)
{
    switch (aControllerType)
    {
        case StorageControllerType_IntelAhci:
        case StorageControllerType_USB:
            *aHotplugCapable = true;
            break;
        case StorageControllerType_LsiLogic:
        case StorageControllerType_LsiLogicSas:
        case StorageControllerType_BusLogic:
        case StorageControllerType_NVMe:
        case StorageControllerType_VirtioSCSI:
        case StorageControllerType_PIIX3:
        case StorageControllerType_PIIX4:
        case StorageControllerType_ICH6:
        case StorageControllerType_I82078:
            *aHotplugCapable = false;
            break;
        default:
            AssertMsgFailedReturn(("Invalid controller type %d\n", aControllerType), E_FAIL);
    }

    return S_OK;
}

HRESULT PlatformProperties::getMaxInstancesOfUSBControllerType(ChipsetType_T aChipset,
                                                               USBControllerType_T aType,
                                                               ULONG *aMaxInstances)
{
    NOREF(aChipset);
    ULONG cCtrs = 0;

    /* no need to lock, this is const */
    switch (aType)
    {
        case USBControllerType_OHCI:
        case USBControllerType_EHCI:
        case USBControllerType_XHCI:
        {
            cCtrs = 1;
            break;
        }
        default:
            AssertMsgFailed(("Invalid bus type %d\n", aType));
    }

    *aMaxInstances = cCtrs;

    return S_OK;
}

HRESULT PlatformProperties::getSupportedParavirtProviders(std::vector<ParavirtProvider_T> &aSupportedParavirtProviders)
{
    static const ParavirtProvider_T aParavirtProviders[] =
    {
        ParavirtProvider_None,
        ParavirtProvider_Default,
        ParavirtProvider_Legacy,
        ParavirtProvider_Minimal,
        ParavirtProvider_HyperV,
        ParavirtProvider_KVM,
    };
    aSupportedParavirtProviders.assign(aParavirtProviders,
                                       aParavirtProviders + RT_ELEMENTS(aParavirtProviders));
    return S_OK;
}

HRESULT PlatformProperties::getSupportedFirmwareTypes(std::vector<FirmwareType_T> &aSupportedFirmwareTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const FirmwareType_T aFirmwareTypes[] =
            {
                FirmwareType_BIOS,
                FirmwareType_EFI,
                FirmwareType_EFI32,
                FirmwareType_EFI64,
                FirmwareType_EFIDUAL
            };
            aSupportedFirmwareTypes.assign(aFirmwareTypes,
                                           aFirmwareTypes + RT_ELEMENTS(aFirmwareTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const FirmwareType_T aFirmwareTypes[] =
            {
                FirmwareType_EFI,
                FirmwareType_EFI32,
                FirmwareType_EFI64,
                FirmwareType_EFIDUAL
            };
            aSupportedFirmwareTypes.assign(aFirmwareTypes,
                                           aFirmwareTypes + RT_ELEMENTS(aFirmwareTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedFirmwareTypes.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedGfxControllerTypes(std::vector<GraphicsControllerType_T> &aSupportedGraphicsControllerTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const GraphicsControllerType_T aGraphicsControllerTypes[] =
            {
                GraphicsControllerType_Null,
                GraphicsControllerType_VBoxVGA,
                GraphicsControllerType_VBoxSVGA
#ifdef VBOX_WITH_VMSVGA
              , GraphicsControllerType_VMSVGA
#endif
            };
            aSupportedGraphicsControllerTypes.assign(aGraphicsControllerTypes + 1 /* Don't include _Null */,
                                                     aGraphicsControllerTypes + RT_ELEMENTS(aGraphicsControllerTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const GraphicsControllerType_T aGraphicsControllerTypes[] =
            {
                GraphicsControllerType_Null,
                GraphicsControllerType_QemuRamFB
#ifdef VBOX_WITH_VMSVGA
              , GraphicsControllerType_VBoxSVGA
              , GraphicsControllerType_VMSVGA
#endif
            };
            aSupportedGraphicsControllerTypes.assign(aGraphicsControllerTypes + 1 /* Don't include _Null */,
                                                     aGraphicsControllerTypes + RT_ELEMENTS(aGraphicsControllerTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedGraphicsControllerTypes.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedGuestOSTypes(std::vector<ComPtr<IGuestOSType> > &aSupportedGuestOSTypes)
{
   /* We only have all supported guest OS types as part of VBoxSVC, not in VBoxC itself. */
#ifdef IN_VBOXSVC
    std::vector<PlatformArchitecture_T> vecArchitectures(1 /* Size */, mPlatformArchitecture);
    return mParent->i_getSupportedGuestOSTypes(vecArchitectures, aSupportedGuestOSTypes);
#else /* VBoxC */
    RT_NOREF(aSupportedGuestOSTypes);
    return VBOX_E_NOT_SUPPORTED;
#endif
}

HRESULT PlatformProperties::getSupportedNetAdpPromiscModePols(std::vector<NetworkAdapterPromiscModePolicy_T> &aSupportedNetworkAdapterPromiscModePolicies)
{
    static const NetworkAdapterPromiscModePolicy_T aNetworkAdapterPromiscModePolicies[] =
    {
        NetworkAdapterPromiscModePolicy_Deny,
        NetworkAdapterPromiscModePolicy_AllowNetwork,
        NetworkAdapterPromiscModePolicy_AllowAll
    };

    aSupportedNetworkAdapterPromiscModePolicies.assign(aNetworkAdapterPromiscModePolicies,
                                                       aNetworkAdapterPromiscModePolicies + RT_ELEMENTS(aNetworkAdapterPromiscModePolicies));
    return S_OK;
}

HRESULT PlatformProperties::getSupportedNetworkAdapterTypes(std::vector<NetworkAdapterType_T> &aSupportedNetworkAdapterTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const NetworkAdapterType_T aNetworkAdapterTypes[] =
            {
                NetworkAdapterType_Null,
                NetworkAdapterType_Am79C970A,
                NetworkAdapterType_Am79C973
#ifdef VBOX_WITH_E1000
              , NetworkAdapterType_I82540EM
              , NetworkAdapterType_I82543GC
              , NetworkAdapterType_I82545EM
#endif
#ifdef VBOX_WITH_VIRTIO
              , NetworkAdapterType_Virtio
#endif
              , NetworkAdapterType_UsbNet
            };
            aSupportedNetworkAdapterTypes.assign(aNetworkAdapterTypes + 1 /* Don't include _Null */,
                                                 aNetworkAdapterTypes + RT_ELEMENTS(aNetworkAdapterTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const NetworkAdapterType_T aNetworkAdapterTypes[] =
            {
                NetworkAdapterType_Null
#ifdef VBOX_WITH_E1000
              , NetworkAdapterType_I82540EM
              , NetworkAdapterType_I82543GC
              , NetworkAdapterType_I82545EM
#endif
#ifdef VBOX_WITH_VIRTIO
              , NetworkAdapterType_Virtio
#endif
              , NetworkAdapterType_UsbNet
            };
            aSupportedNetworkAdapterTypes.assign(aNetworkAdapterTypes + 1 /* Don't include _Null */,
                                                 aNetworkAdapterTypes + RT_ELEMENTS(aNetworkAdapterTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedNetworkAdapterTypes.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedUartTypes(std::vector<UartType_T> &aSupportedUartTypes)
{
    static const UartType_T aUartTypes[] =
    {
        UartType_U16450,
        UartType_U16550A,
        UartType_U16750
    };
    aSupportedUartTypes.assign(aUartTypes,
                               aUartTypes + RT_ELEMENTS(aUartTypes));
    return S_OK;
}

HRESULT PlatformProperties::getSupportedUSBControllerTypes(std::vector<USBControllerType_T> &aSupportedUSBControllerTypes)
{
    static const USBControllerType_T aUSBControllerTypes[] =
    {
        USBControllerType_OHCI,
        USBControllerType_EHCI,
        USBControllerType_XHCI
    };
    aSupportedUSBControllerTypes.assign(aUSBControllerTypes,
                                        aUSBControllerTypes + RT_ELEMENTS(aUSBControllerTypes));
    return S_OK;
}

/**
 * Static helper function to return all supported features for a given graphics controller.
 *
 * @returns VBox status code.
 * @param   enmArchitecture              Platform architecture to query a feature for.
 * @param   enmController                Graphics controller to return supported features for.
 * @param   vecSupportedGraphicsFeatures Returned features on success.
 */
/* static */
int PlatformProperties::s_getSupportedGraphicsControllerFeatures(PlatformArchitecture_T enmArchitecture,
                                                                 GraphicsControllerType_T enmController,
                                                                 std::vector<GraphicsFeature_T> &vecSupportedGraphicsFeatures)
{
    vecSupportedGraphicsFeatures.clear();

    /* Note! The legacy VHWA acceleration has been disabled completely (see @bugref{9691}). */
    switch (enmArchitecture)
    {
        case PlatformArchitecture_x86:
        case PlatformArchitecture_ARM:
        {
            switch (enmController)
            {
                case GraphicsControllerType_VMSVGA:
                case GraphicsControllerType_VBoxSVGA:
#ifdef VBOX_WITH_VMSVGA
# ifdef VBOX_WITH_3D_ACCELERATION
                    vecSupportedGraphicsFeatures.push_back(GraphicsFeature_Acceleration3D);
# endif
#endif
                    return VINF_SUCCESS;

                case GraphicsControllerType_VBoxVGA:
                case GraphicsControllerType_QemuRamFB:
                    /* None supported. */
                    return VINF_SUCCESS;

                /* (no default to get compiler warnings) */
                case GraphicsControllerType_Null:
#ifdef VBOX_WITH_XPCOM
                case GraphicsControllerType_32BitHack:
#endif
                    break;
            }
            break;
        }

        /* (no default to get compiler warnings) */
        case PlatformArchitecture_None:
#ifdef VBOX_WITH_XPCOM
        case PlatformArchitecture_32BitHack:
#endif
            break;
    }
    return VERR_INVALID_PARAMETER;
}

/**
 * Static helper function to return whether a given graphics feature for a graphics controller is enabled or not.
 *
 * @returns \c true if the given feature is supported, or \c false if not.
 * @param   enmArchitecture         Platform architecture to query a feature for.
 * @param   enmController           Graphics controlller to query a feature for.
 * @param   enmFeature              Feature to query.
 */
/* static */
bool PlatformProperties::s_isGraphicsControllerFeatureSupported(PlatformArchitecture_T enmArchitecture,
                                                                GraphicsControllerType_T enmController,
                                                                GraphicsFeature_T enmFeature)
{
    std::vector<GraphicsFeature_T> vecSupportedGraphicsFeatures;
    int vrc = PlatformProperties::s_getSupportedGraphicsControllerFeatures(enmArchitecture, enmController,
                                                                           vecSupportedGraphicsFeatures);
    if (RT_SUCCESS(vrc))
        return std::find(vecSupportedGraphicsFeatures.begin(),
                         vecSupportedGraphicsFeatures.end(), enmFeature) != vecSupportedGraphicsFeatures.end();
    return false;
}

/**
 * Returns the [minimum, maximum] VRAM range and stride size for a given graphics controller.
 *
 * @returns HRESULT
 * @param   aGraphicsControllerType Graphics controller type to return values for.
 * @param   fAccelerate3DEnabled    whether 3D acceleration is enabled / disabled for the selected graphics controller.
 * @param   aMinMB                  Where to return the minimum VRAM (in MB).
 * @param   aMaxMB                  Where to return the maximum VRAM (in MB).
 * @param   aStrideSizeMB           Where to return stride size (in MB). Optional, can be NULL.
 */
/* static */
HRESULT PlatformProperties::s_getSupportedVRAMRange(GraphicsControllerType_T aGraphicsControllerType, BOOL fAccelerate3DEnabled,
                                                    ULONG *aMinMB, ULONG *aMaxMB, ULONG *aStrideSizeMB)
{
    size_t cbMin;
    size_t cbMax;
    size_t cbStride = _1M; /* Default stride for all controllers. */

    switch (aGraphicsControllerType)
    {
        case GraphicsControllerType_VBoxVGA:
        {
            cbMin = VGA_VRAM_MIN;
            cbMax = VGA_VRAM_MAX;
            break;
        }

        case GraphicsControllerType_VMSVGA:
        case GraphicsControllerType_VBoxSVGA:
        {
            if (fAccelerate3DEnabled)
                cbMin = VBOX_SVGA_VRAM_MIN_SIZE_3D;
            else
                cbMin = VBOX_SVGA_VRAM_MIN_SIZE;
            /* We don't want to limit ourselves to VBOX_SVGA_VRAM_MAX_SIZE,
             * so we use VGA_VRAM_MAX for all controllers. */
            cbMax = VGA_VRAM_MAX;
            break;
        }

        case GraphicsControllerType_QemuRamFB:
        {
            /* We seem to hardcode 32-bit (4 bytes) as BPP, see RAMFB_BPP in QemuRamfb.c. */
            cbMin = 4 /* BPP in bytes */ * 16    * 16;    /* Values taken from qemu/hw/display/ramfb.c */
            cbMax = 4 /* BPP in bytes */ * 16000 * 12000; /* Values taken from bochs-vbe.h. */
            /* Make the maximum value a power of two. */
            cbMax = RT_BIT_64(ASMBitLastSetU64(cbMax));
            break;
        }

        default:
            return E_INVALIDARG;
    }

    /* Sanity. We want all max values to be a power of two. */
    AssertMsg(RT_IS_POWER_OF_TWO(cbMax), ("Maximum VRAM value is not a power of two!\n"));

    /* Convert bytes -> MB, align to stride. */
    cbMin    = (ULONG)(RT_ALIGN_64(cbMin, cbStride) / _1M);
    cbMax    = (ULONG)(RT_ALIGN_64(cbMax, cbStride) / _1M);
    cbStride = (ULONG)cbStride / _1M;

    /* Finally, clamp the values to our schema definitions before returning. */
    if (cbMax > SchemaDefs::MaxGuestVRAM)
        cbMax = SchemaDefs::MaxGuestVRAM;

    *aMinMB = (ULONG)cbMin;
    *aMaxMB = (ULONG)cbMax;
    if (aStrideSizeMB)
        *aStrideSizeMB = (ULONG)cbStride;

    return S_OK;
}

HRESULT PlatformProperties::getSupportedVRAMRange(GraphicsControllerType_T aGraphicsControllerType, BOOL fAccelerate3DEnabled,
                                                  ULONG *aMinMB, ULONG *aMaxMB, ULONG *aStrideSizeMB)
{
    HRESULT hrc = PlatformProperties::s_getSupportedVRAMRange(aGraphicsControllerType, fAccelerate3DEnabled, aMinMB, aMaxMB,
                                                              aStrideSizeMB);
    switch (hrc)
    {
        case VBOX_E_NOT_SUPPORTED:
            return setError(VBOX_E_NOT_SUPPORTED, tr("Selected graphics controller not supported in this version"));

        case E_INVALIDARG:
            return setError(E_INVALIDARG, tr("The graphics controller type (%d) is invalid"), aGraphicsControllerType);

        default:
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedGfxFeaturesForType(GraphicsControllerType_T aGraphicsControllerType,
                                                           std::vector<GraphicsFeature_T> &aSupportedGraphicsFeatures)
{
    int vrc = PlatformProperties::s_getSupportedGraphicsControllerFeatures(mPlatformArchitecture,
                                                                           aGraphicsControllerType, aSupportedGraphicsFeatures);
    if (RT_SUCCESS(vrc))
        return S_OK;
    return setError(E_INVALIDARG, tr("The graphics controller type (%d) is invalid"), aGraphicsControllerType);
}

HRESULT PlatformProperties::getSupportedAudioControllerTypes(std::vector<AudioControllerType_T> &aSupportedAudioControllerTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const AudioControllerType_T aAudioControllerTypes[] =
            {
                AudioControllerType_AC97,
                AudioControllerType_SB16,
                AudioControllerType_HDA,
            };
            aSupportedAudioControllerTypes.assign(aAudioControllerTypes,
                                                  aAudioControllerTypes + RT_ELEMENTS(aAudioControllerTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const AudioControllerType_T aAudioControllerTypes[] =
            {
                AudioControllerType_AC97,
                AudioControllerType_HDA,
            };
            aSupportedAudioControllerTypes.assign(aAudioControllerTypes,
                                                  aAudioControllerTypes + RT_ELEMENTS(aAudioControllerTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedAudioControllerTypes.clear());
            break;
    }


    return S_OK;
}

HRESULT PlatformProperties::getSupportedBootDevices(std::vector<DeviceType_T> &aSupportedBootDevices)
{
    /* Note: This function returns the supported boot devices for the given architecture,
             in the default order, to keep it simple for the caller. */

    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const DeviceType_T aBootDevices[] =
            {
                DeviceType_Floppy,
                DeviceType_DVD,
                DeviceType_HardDisk,
                DeviceType_Network
            };
            aSupportedBootDevices.assign(aBootDevices,
                                         aBootDevices + RT_ELEMENTS(aBootDevices));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const DeviceType_T aBootDevices[] =
            {
                DeviceType_DVD,
                DeviceType_HardDisk
                /** @todo BUGBUG We need to test network booting via PXE on ARM first! */
            };
            aSupportedBootDevices.assign(aBootDevices,
                                         aBootDevices + RT_ELEMENTS(aBootDevices));
            break;
        }

        default:
            AssertFailedStmt(aSupportedBootDevices.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedStorageBuses(std::vector<StorageBus_T> &aSupportedStorageBuses)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const StorageBus_T aStorageBuses[] =
            {
                StorageBus_SATA,
                StorageBus_IDE,
                StorageBus_SCSI,
                StorageBus_Floppy,
                StorageBus_SAS,
                StorageBus_USB,
                StorageBus_PCIe,
                StorageBus_VirtioSCSI,
            };
            aSupportedStorageBuses.assign(aStorageBuses,
                                          aStorageBuses + RT_ELEMENTS(aStorageBuses));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const StorageBus_T aStorageBuses[] =
            {
                StorageBus_SATA,
                StorageBus_SCSI,
                StorageBus_SAS,
                StorageBus_USB,
                StorageBus_PCIe,
                StorageBus_VirtioSCSI
            };
            aSupportedStorageBuses.assign(aStorageBuses,
                                          aStorageBuses + RT_ELEMENTS(aStorageBuses));
            break;
        }

        default:
            AssertFailedStmt(aSupportedStorageBuses.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedStorageControllerTypes(std::vector<StorageControllerType_T> &aSupportedStorageControllerTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const StorageControllerType_T aStorageControllerTypes[] =
            {
                StorageControllerType_IntelAhci,
                StorageControllerType_PIIX4,
                StorageControllerType_PIIX3,
                StorageControllerType_ICH6,
                StorageControllerType_LsiLogic,
                StorageControllerType_BusLogic,
                StorageControllerType_I82078,
                StorageControllerType_LsiLogicSas,
                StorageControllerType_USB,
                StorageControllerType_NVMe,
                StorageControllerType_VirtioSCSI
            };
            aSupportedStorageControllerTypes.assign(aStorageControllerTypes,
                                                    aStorageControllerTypes + RT_ELEMENTS(aStorageControllerTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const StorageControllerType_T aStorageControllerTypes[] =
            {
                StorageControllerType_IntelAhci,
                StorageControllerType_LsiLogic,
                StorageControllerType_LsiLogicSas,
                StorageControllerType_USB,
                StorageControllerType_NVMe,
                StorageControllerType_VirtioSCSI
            };
            aSupportedStorageControllerTypes.assign(aStorageControllerTypes,
                                                    aStorageControllerTypes + RT_ELEMENTS(aStorageControllerTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedStorageControllerTypes.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedChipsetTypes(std::vector<ChipsetType_T> &aSupportedChipsetTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const ChipsetType_T aChipsetTypes[] =
            {
                ChipsetType_PIIX3,
                ChipsetType_ICH9
            };
            aSupportedChipsetTypes.assign(aChipsetTypes,
                                          aChipsetTypes + RT_ELEMENTS(aChipsetTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const ChipsetType_T aChipsetTypes[] =
            {
                ChipsetType_ARMv8Virtual
            };
            aSupportedChipsetTypes.assign(aChipsetTypes,
                                          aChipsetTypes + RT_ELEMENTS(aChipsetTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedChipsetTypes.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedIommuTypes(std::vector<IommuType_T> &aSupportedIommuTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const IommuType_T aIommuTypes[] =
            {
                IommuType_None,
                IommuType_Automatic,
                IommuType_AMD
                /** @todo Add Intel when it's supported. */
            };
            aSupportedIommuTypes.assign(aIommuTypes,
                                        aIommuTypes + RT_ELEMENTS(aIommuTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const IommuType_T aIommuTypes[] =
            {
                IommuType_None
            };
            aSupportedIommuTypes.assign(aIommuTypes,
                                        aIommuTypes + RT_ELEMENTS(aIommuTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedIommuTypes.clear());
            break;
    }

    return S_OK;
}

HRESULT PlatformProperties::getSupportedTpmTypes(std::vector<TpmType_T> &aSupportedTpmTypes)
{
    switch (mPlatformArchitecture)
    {
        case PlatformArchitecture_x86:
        {
            static const TpmType_T aTpmTypes[] =
            {
                TpmType_None,
                TpmType_v1_2,
                TpmType_v2_0
            };
            aSupportedTpmTypes.assign(aTpmTypes,
                                      aTpmTypes + RT_ELEMENTS(aTpmTypes));
            break;
        }

        case PlatformArchitecture_ARM:
        {
            static const TpmType_T aTpmTypes[] =
            {
                TpmType_None,
                TpmType_v2_0
            };
            aSupportedTpmTypes.assign(aTpmTypes,
                                      aTpmTypes + RT_ELEMENTS(aTpmTypes));
            break;
        }

        default:
            AssertFailedStmt(aSupportedTpmTypes.clear());
            break;
    }

    return S_OK;
}
