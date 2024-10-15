/* $Id: BusAssignmentManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox bus slots assignment manager
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN
#include "LoggingNew.h"

#include "BusAssignmentManager.h"

#include <iprt/asm.h>
#include <iprt/string.h>

#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/com/array.h>

#include <map>
#include <vector>
#include <algorithm>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct DeviceAssignmentRule
{
    const char *pszName;
    int         iBus;
    int         iDevice;
    int         iFn;
    int         iPriority;
};

struct DeviceAliasRule
{
    const char *pszDevName;
    const char *pszDevAlias;
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/* Those rules define PCI slots assignment */
/** @note
 * The EFI takes assumptions about PCI slot assignments which are different
 * from the following tables in certain cases, for example the IDE device
 * is assumed to be 00:01.1! */

/* Device           Bus  Device Function Priority */

/* Generic rules */
static const DeviceAssignmentRule g_aGenericRules[] =
{
    /* VGA controller */
    {"vga",           0,  2, 0,  0},

    /* VMM device */
    {"VMMDev",        0,  4, 0,  0},

    /* Audio controllers */
    {"ichac97",       0,  5, 0,  0},
    {"hda",           0,  5, 0,  0},

    /* Storage controllers */
    {"buslogic",      0, 21, 0,  1},
    {"lsilogicsas",   0, 22, 0,  1},
    {"nvme",          0, 14, 0,  1},
    {"virtio-scsi",   0, 15, 0,  1},

    /* USB controllers */
    {"usb-ohci",      0,  6,  0, 0},
    {"usb-ehci",      0, 11,  0, 0},
    {"usb-xhci",      0, 12,  0, 0},

    /* ACPI controller */
#if 0
    // It really should be this for 440FX chipset (part of PIIX4 actually)
    {"acpi",          0,  1,  3, 0},
#else
    {"acpi",          0,  7,  0, 0},
#endif

    /* Network controllers */
    /* the first network card gets the PCI ID 3, the next 3 gets 8..10,
     * next 4 get 16..19. In "VMWare compatibility" mode the IDs 3 and 17
     * swap places, i.e. the first card goes to ID 17=0x11. */
    {"nic",           0,  3,  0, 1},
    {"nic",           0,  8,  0, 1},
    {"nic",           0,  9,  0, 1},
    {"nic",           0, 10,  0, 1},
    {"nic",           0, 16,  0, 1},
    {"nic",           0, 17,  0, 1},
    {"nic",           0, 18,  0, 1},
    {"nic",           0, 19,  0, 1},

    /* ISA/LPC controller */
    {"lpc",           0, 31,  0, 0},

    { NULL,          -1, -1, -1,  0}
};

/* PIIX3 chipset rules */
static const DeviceAssignmentRule g_aPiix3Rules[] =
{
    {"piix3ide",      0,  1,  1, 0},
    {"ahci",          0, 13,  0, 1},
    {"lsilogic",      0, 20,  0, 1},
    {"pcibridge",     0, 24,  0, 0},
    {"pcibridge",     0, 25,  0, 0},
    { NULL,          -1, -1, -1, 0}
};


/* ICH9 chipset rules */
static const DeviceAssignmentRule g_aIch9Rules[] =
{
    /* Host Controller */
    {"i82801",        0, 30, 0,  0},

    /* Those are functions of LPC at 00:1e:00 */
    /**
     *  Please note, that for devices being functions, like we do here, device 0
     *  must be multifunction, i.e. have header type 0x80. Our LPC device is.
     *  Alternative approach is to assign separate slot to each device.
     */
    {"piix3ide",      0, 31, 1,  2},
    {"ahci",          0, 31, 2,  2},
    {"smbus",         0, 31, 3,  2},
    {"usb-ohci",      0, 31, 4,  2},
    {"usb-ehci",      0, 31, 5,  2},
    {"thermal",       0, 31, 6,  2},

    /* to make sure rule never used before rules assigning devices on it */
    {"ich9pcibridge", 0, 24, 0,  10},
    {"ich9pcibridge", 0, 25, 0,  10},
    {"ich9pcibridge", 2, 24, 0,   9}, /* Bridges must be instantiated depth */
    {"ich9pcibridge", 2, 25, 0,   9}, /* first (assumption in PDM and other */
    {"ich9pcibridge", 4, 24, 0,   8}, /* places), so make sure that nested */
    {"ich9pcibridge", 4, 25, 0,   8}, /* bridges are added to the last bridge */
    {"ich9pcibridge", 6, 24, 0,   7}, /* only, avoiding the need to re-sort */
    {"ich9pcibridge", 6, 25, 0,   7}, /* everything before starting the VM. */
    {"ich9pcibridge", 8, 24, 0,   6},
    {"ich9pcibridge", 8, 25, 0,   6},
    {"ich9pcibridge", 10, 24, 0,  5},
    {"ich9pcibridge", 10, 25, 0,  5},

    /* Storage controllers */
    {"ahci",          1,  0, 0,   0},
    {"ahci",          1,  1, 0,   0},
    {"ahci",          1,  2, 0,   0},
    {"ahci",          1,  3, 0,   0},
    {"ahci",          1,  4, 0,   0},
    {"ahci",          1,  5, 0,   0},
    {"ahci",          1,  6, 0,   0},
    {"lsilogic",      1,  7, 0,   0},
    {"lsilogic",      1,  8, 0,   0},
    {"lsilogic",      1,  9, 0,   0},
    {"lsilogic",      1, 10, 0,   0},
    {"lsilogic",      1, 11, 0,   0},
    {"lsilogic",      1, 12, 0,   0},
    {"lsilogic",      1, 13, 0,   0},
    {"buslogic",      1, 14, 0,   0},
    {"buslogic",      1, 15, 0,   0},
    {"buslogic",      1, 16, 0,   0},
    {"buslogic",      1, 17, 0,   0},
    {"buslogic",      1, 18, 0,   0},
    {"buslogic",      1, 19, 0,   0},
    {"buslogic",      1, 20, 0,   0},
    {"lsilogicsas",   1, 21, 0,   0},
    {"lsilogicsas",   1, 26, 0,   0},
    {"lsilogicsas",   1, 27, 0,   0},
    {"lsilogicsas",   1, 28, 0,   0},
    {"lsilogicsas",   1, 29, 0,   0},
    {"lsilogicsas",   1, 30, 0,   0},
    {"lsilogicsas",   1, 31, 0,   0},

    /* NICs */
    {"nic",           2,  0, 0,   0},
    {"nic",           2,  1, 0,   0},
    {"nic",           2,  2, 0,   0},
    {"nic",           2,  3, 0,   0},
    {"nic",           2,  4, 0,   0},
    {"nic",           2,  5, 0,   0},
    {"nic",           2,  6, 0,   0},
    {"nic",           2,  7, 0,   0},
    {"nic",           2,  8, 0,   0},
    {"nic",           2,  9, 0,   0},
    {"nic",           2, 10, 0,   0},
    {"nic",           2, 11, 0,   0},
    {"nic",           2, 12, 0,   0},
    {"nic",           2, 13, 0,   0},
    {"nic",           2, 14, 0,   0},
    {"nic",           2, 15, 0,   0},
    {"nic",           2, 16, 0,   0},
    {"nic",           2, 17, 0,   0},
    {"nic",           2, 18, 0,   0},
    {"nic",           2, 19, 0,   0},
    {"nic",           2, 20, 0,   0},
    {"nic",           2, 21, 0,   0},
    {"nic",           2, 26, 0,   0},
    {"nic",           2, 27, 0,   0},
    {"nic",           2, 28, 0,   0},
    {"nic",           2, 29, 0,   0},
    {"nic",           2, 30, 0,   0},
    {"nic",           2, 31, 0,   0},

    /* Storage controller #2 (NVMe, virtio-scsi) */
    {"nvme",          3,  0, 0,   0},
    {"nvme",          3,  1, 0,   0},
    {"nvme",          3,  2, 0,   0},
    {"nvme",          3,  3, 0,   0},
    {"nvme",          3,  4, 0,   0},
    {"nvme",          3,  5, 0,   0},
    {"nvme",          3,  6, 0,   0},
    {"virtio-scsi",   3,  7, 0,   0},
    {"virtio-scsi",   3,  8, 0,   0},
    {"virtio-scsi",   3,  9, 0,   0},
    {"virtio-scsi",   3, 10, 0,   0},
    {"virtio-scsi",   3, 11, 0,   0},
    {"virtio-scsi",   3, 12, 0,   0},
    {"virtio-scsi",   3, 13, 0,   0},

    { NULL,          -1, -1, -1,  0}
};


/* Virtual Armv8 platform chipset rules */
static const DeviceAssignmentRule g_aArmv8Rules[] =
{
    /* VGA controller */
    {"vga",           0,  0, 0,  0},

    /* VMM device */
    {"VMMDev",        0,  1, 0,  0},

    /* Audio controllers */
    {"ichac97",       0,  2, 0,  0},
    {"hda",           0,  2, 0,  0},

    /* Storage controllers */
    {"virtio-scsi",   0,  3, 0,  1},
    {"nvme",          0,  4, 0,  1},
    {"ahci",          0, 16, 0,  1},
    {"lsilogicsas",   0, 17, 0,  1},

    /* USB controllers */
    {"usb-ehci",      0,  5,  0, 0},
    {"usb-xhci",      0,  6,  0, 0},
    {"usb-ohci",      0,  7,  0, 0},

    /* Network controllers */
    {"nic",           0,  8,  0, 1},
    {"nic",           0,  9,  0, 1},
    {"nic",           0, 10,  0, 1},
    {"nic",           0, 11,  0, 1},
    {"nic",           0, 12,  0, 1},
    {"nic",           0, 13,  0, 1},
    {"nic",           0, 14,  0, 1},
    {"nic",           0, 15,  0, 1},

    /* to make sure rule never used before rules assigning devices on it */
    {"pci-generic-ecam-bridge", 0, 24, 0,  10},
    {"pci-generic-ecam-bridge", 0, 25, 0,  10},
    {"pci-generic-ecam-bridge", 2, 24, 0,   9}, /* Bridges must be instantiated depth */
    {"pci-generic-ecam-bridge", 2, 25, 0,   9}, /* first (assumption in PDM and other */
    {"pci-generic-ecam-bridge", 4, 24, 0,   8}, /* places), so make sure that nested */
    {"pci-generic-ecam-bridge", 4, 25, 0,   8}, /* bridges are added to the last bridge */
    {"pci-generic-ecam-bridge", 6, 24, 0,   7}, /* only, avoiding the need to re-sort */
    {"pci-generic-ecam-bridge", 6, 25, 0,   7}, /* everything before starting the VM. */
    {"pci-generic-ecam-bridge", 8, 24, 0,   6},
    {"pci-generic-ecam-bridge", 8, 25, 0,   6},
    {"pci-generic-ecam-bridge", 10, 24, 0,  5},
    {"pci-generic-ecam-bridge", 10, 25, 0,  5},

    /* Storage controllers */
    {"ahci",          1,  0, 0,   0},
    {"ahci",          1,  1, 0,   0},
    {"ahci",          1,  2, 0,   0},
    {"ahci",          1,  3, 0,   0},
    {"ahci",          1,  4, 0,   0},
    {"ahci",          1,  5, 0,   0},
    {"ahci",          1,  6, 0,   0},
    {"lsilogic",      1,  7, 0,   0},
    {"lsilogic",      1,  8, 0,   0},
    {"lsilogic",      1,  9, 0,   0},
    {"lsilogic",      1, 10, 0,   0},
    {"lsilogic",      1, 11, 0,   0},
    {"lsilogic",      1, 12, 0,   0},
    {"lsilogic",      1, 13, 0,   0},
    {"buslogic",      1, 14, 0,   0},
    {"buslogic",      1, 15, 0,   0},
    {"buslogic",      1, 16, 0,   0},
    {"buslogic",      1, 17, 0,   0},
    {"buslogic",      1, 18, 0,   0},
    {"buslogic",      1, 19, 0,   0},
    {"buslogic",      1, 20, 0,   0},
    {"lsilogicsas",   1, 21, 0,   0},
    {"lsilogicsas",   1, 26, 0,   0},
    {"lsilogicsas",   1, 27, 0,   0},
    {"lsilogicsas",   1, 28, 0,   0},
    {"lsilogicsas",   1, 29, 0,   0},
    {"lsilogicsas",   1, 30, 0,   0},
    {"lsilogicsas",   1, 31, 0,   0},

    /* NICs */
    {"nic",           2,  0, 0,   0},
    {"nic",           2,  1, 0,   0},
    {"nic",           2,  2, 0,   0},
    {"nic",           2,  3, 0,   0},
    {"nic",           2,  4, 0,   0},
    {"nic",           2,  5, 0,   0},
    {"nic",           2,  6, 0,   0},
    {"nic",           2,  7, 0,   0},
    {"nic",           2,  8, 0,   0},
    {"nic",           2,  9, 0,   0},
    {"nic",           2, 10, 0,   0},
    {"nic",           2, 11, 0,   0},
    {"nic",           2, 12, 0,   0},
    {"nic",           2, 13, 0,   0},
    {"nic",           2, 14, 0,   0},
    {"nic",           2, 15, 0,   0},
    {"nic",           2, 16, 0,   0},
    {"nic",           2, 17, 0,   0},
    {"nic",           2, 18, 0,   0},
    {"nic",           2, 19, 0,   0},
    {"nic",           2, 20, 0,   0},
    {"nic",           2, 21, 0,   0},
    {"nic",           2, 26, 0,   0},
    {"nic",           2, 27, 0,   0},
    {"nic",           2, 28, 0,   0},
    {"nic",           2, 29, 0,   0},
    {"nic",           2, 30, 0,   0},
    {"nic",           2, 31, 0,   0},

    /* Storage controller #2 (NVMe, virtio-scsi) */
    {"nvme",          3,  0, 0,   0},
    {"nvme",          3,  1, 0,   0},
    {"nvme",          3,  2, 0,   0},
    {"nvme",          3,  3, 0,   0},
    {"nvme",          3,  4, 0,   0},
    {"nvme",          3,  5, 0,   0},
    {"nvme",          3,  6, 0,   0},
    {"virtio-scsi",   3,  7, 0,   0},
    {"virtio-scsi",   3,  8, 0,   0},
    {"virtio-scsi",   3,  9, 0,   0},
    {"virtio-scsi",   3, 10, 0,   0},
    {"virtio-scsi",   3, 11, 0,   0},
    {"virtio-scsi",   3, 12, 0,   0},
    {"virtio-scsi",   3, 13, 0,   0},

    { NULL,          -1, -1, -1,  0}
};


#ifdef VBOX_WITH_IOMMU_AMD
/*
 * AMD IOMMU and LSI Logic controller rules.
 *
 * Since the PCI slot (BDF=00:20.0) of the LSI Logic controller
 * conflicts with the SB I/O APIC, we assign the LSI Logic controller
 * to device number 23 when the VM is configured for an AMD IOMMU.
 */
static const DeviceAssignmentRule g_aIch9IommuAmdRules[] =
{
    /* AMD IOMMU. */
    {"iommu-amd",     0,  0,  0, 0},
    /* AMD IOMMU: Reserved for southbridge I/O APIC. */
    {"sb-ioapic",     0, 20,  0, 0},

    /* Storage controller */
    {"lsilogic",      0, 23,  0, 1},
    { NULL,          -1, -1, -1, 0}
};
#endif

#ifdef VBOX_WITH_IOMMU_INTEL
/*
 * Intel IOMMU.
 * The VT-d misc, address remapping, system management device is
 * located at BDF 0:5:0 on real hardware but we use 0:1:0 since that
 * slot isn't used for anything else.
 *
 * While we could place the I/O APIC anywhere, we keep it consistent
 * with the AMD IOMMU and we assign the LSI Logic controller to
 * device number 23 (and I/O APIC at device 20).
 */
static const DeviceAssignmentRule g_aIch9IommuIntelRules[] =
{
    /* Intel IOMMU. */
    {"iommu-intel",   0,  1,  0, 0},
    /* Intel IOMMU: Reserved for I/O APIC. */
    {"sb-ioapic",     0, 20,  0, 0},

    /* Storage controller */
    {"lsilogic",      0, 23,  0, 1},
    { NULL,          -1, -1, -1, 0}
};
#endif

/* LSI Logic Controller. */
static const DeviceAssignmentRule g_aIch9LsiRules[] =
{
    /* Storage controller */
    {"lsilogic",      0, 20,  0, 1},
    { NULL,          -1, -1, -1, 0}
};

/* Aliasing rules */
static const DeviceAliasRule g_aDeviceAliases[] =
{
    {"e1000",       "nic"},
    {"pcnet",       "nic"},
    {"virtio-net",  "nic"},
    {"ahci",        "storage"},
    {"lsilogic",    "storage"},
    {"buslogic",    "storage"},
    {"lsilogicsas", "storage"},
    {"nvme",        "storage"},
    {"virtio-scsi", "storage"}
};



/**
 * Bus assignment manage state data.
 * @internal
 */
struct BusAssignmentManager::State
{
    struct PCIDeviceRecord
    {
        char          szDevName[32];
        PCIBusAddress HostAddress;

        PCIDeviceRecord(const char *pszName, PCIBusAddress aHostAddress)
        {
            RTStrCopy(this->szDevName, sizeof(szDevName), pszName);
            this->HostAddress = aHostAddress;
        }

        PCIDeviceRecord(const char *pszName)
        {
            RTStrCopy(this->szDevName, sizeof(szDevName), pszName);
        }

        bool operator<(const PCIDeviceRecord &a) const
        {
            return RTStrNCmp(szDevName, a.szDevName, sizeof(szDevName)) < 0;
        }

        bool operator==(const PCIDeviceRecord &a) const
        {
            return RTStrNCmp(szDevName, a.szDevName, sizeof(szDevName)) == 0;
        }
    };

    typedef std::map<PCIBusAddress,PCIDeviceRecord>   PCIMap;
    typedef std::vector<PCIBusAddress>                PCIAddrList;
    typedef std::vector<const DeviceAssignmentRule *> PCIRulesList;
    typedef std::map<PCIDeviceRecord,PCIAddrList>     ReversePCIMap;

    volatile int32_t cRefCnt;
    ChipsetType_T    mChipsetType;
    const char *     mpszBridgeName;
    IommuType_T      mIommuType;
    PCIMap           mPCIMap;
    ReversePCIMap    mReversePCIMap;
    PCVMMR3VTABLE    mpVMM;

    State()
        : cRefCnt(1), mChipsetType(ChipsetType_Null), mpszBridgeName("unknownbridge"), mpVMM(NULL)
    {}
    ~State()
    {}

    HRESULT init(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType);

    HRESULT record(const char *pszName, PCIBusAddress& GuestAddress, PCIBusAddress HostAddress);
    HRESULT autoAssign(const char *pszName, PCIBusAddress& Address);
    bool    checkAvailable(PCIBusAddress& Address);
    bool    findPCIAddress(const char *pszDevName, int iInstance, PCIBusAddress& Address);

    const char *findAlias(const char *pszName);
    void addMatchingRules(const char *pszName, PCIRulesList& aList);
    void listAttachedPCIDevices(std::vector<PCIDeviceInfo> &aAttached);
};


HRESULT BusAssignmentManager::State::init(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType)
{
    mpVMM = pVMM;

    if (iommuType != IommuType_None)
    {
#if defined(VBOX_WITH_IOMMU_AMD) && defined(VBOX_WITH_IOMMU_INTEL)
        Assert(iommuType == IommuType_AMD || iommuType == IommuType_Intel);
#elif defined(VBOX_WITH_IOMMU_AMD)
        Assert(iommuType == IommuType_AMD);
#elif defined(VBOX_WITH_IOMMU_INTEL)
        Assert(iommuType == IommuType_Intel);
#endif
    }

    mChipsetType = chipsetType;
    mIommuType   = iommuType;
    switch (chipsetType)
    {
        case ChipsetType_PIIX3:
            mpszBridgeName = "pcibridge";
            break;
        case ChipsetType_ICH9:
            mpszBridgeName = "ich9pcibridge";
            break;
        case ChipsetType_ARMv8Virtual:
            mpszBridgeName = "pci-generic-ecam-bridge";
            break;
        default:
            mpszBridgeName = "unknownbridge";
            AssertFailed();
            break;
    }
    return S_OK;
}

HRESULT BusAssignmentManager::State::record(const char *pszName, PCIBusAddress& Address, PCIBusAddress HostAddress)
{
    PCIDeviceRecord devRec(pszName, HostAddress);

    /* Remember address -> device mapping */
    mPCIMap.insert(PCIMap::value_type(Address, devRec));

    ReversePCIMap::iterator it = mReversePCIMap.find(devRec);
    if (it == mReversePCIMap.end())
    {
        mReversePCIMap.insert(ReversePCIMap::value_type(devRec, PCIAddrList()));
        it = mReversePCIMap.find(devRec);
    }

    /* Remember device name -> addresses mapping */
    it->second.push_back(Address);

    return S_OK;
}

bool BusAssignmentManager::State::findPCIAddress(const char *pszDevName, int iInstance, PCIBusAddress& Address)
{
    PCIDeviceRecord devRec(pszDevName);

    ReversePCIMap::iterator it = mReversePCIMap.find(devRec);
    if (it == mReversePCIMap.end())
        return false;

    if (iInstance >= (int)it->second.size())
        return false;

    Address = it->second[iInstance];
    return true;
}

void BusAssignmentManager::State::addMatchingRules(const char *pszName, PCIRulesList& aList)
{
    size_t iRuleset, iRule;
    const DeviceAssignmentRule *aArrays[3] = {g_aGenericRules, NULL, NULL};

    switch (mChipsetType)
    {
        case ChipsetType_PIIX3:
            aArrays[1] = g_aPiix3Rules;
            break;
        case ChipsetType_ICH9:
        {
            aArrays[1] = g_aIch9Rules;
#ifdef VBOX_WITH_IOMMU_AMD
            if (mIommuType == IommuType_AMD)
                aArrays[2] = g_aIch9IommuAmdRules;
            else
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
            if (mIommuType == IommuType_Intel)
                aArrays[2] = g_aIch9IommuIntelRules;
            else
#endif
            {
                aArrays[2] = g_aIch9LsiRules;
            }
            break;
        }
        case ChipsetType_ARMv8Virtual:
            aArrays[0] = g_aArmv8Rules;
            break;
        default:
            AssertFailed();
            break;
    }

    for (iRuleset = 0; iRuleset < RT_ELEMENTS(aArrays); iRuleset++)
    {
        if (aArrays[iRuleset] == NULL)
            continue;

        for (iRule = 0; aArrays[iRuleset][iRule].pszName != NULL; iRule++)
        {
            if (RTStrCmp(pszName, aArrays[iRuleset][iRule].pszName) == 0)
                aList.push_back(&aArrays[iRuleset][iRule]);
        }
    }
}

const char *BusAssignmentManager::State::findAlias(const char *pszDev)
{
    for (size_t iAlias = 0; iAlias < RT_ELEMENTS(g_aDeviceAliases); iAlias++)
    {
        if (strcmp(pszDev, g_aDeviceAliases[iAlias].pszDevName) == 0)
            return g_aDeviceAliases[iAlias].pszDevAlias;
    }
    return NULL;
}

static bool  RuleComparator(const DeviceAssignmentRule *r1, const DeviceAssignmentRule *r2)
{
    return (r1->iPriority > r2->iPriority);
}

HRESULT BusAssignmentManager::State::autoAssign(const char *pszName, PCIBusAddress& Address)
{
    PCIRulesList matchingRules;

    addMatchingRules(pszName, matchingRules);
    const char *pszAlias = findAlias(pszName);
    if (pszAlias)
        addMatchingRules(pszAlias, matchingRules);

    AssertMsg(matchingRules.size() > 0, ("No rule for %s(%s)\n", pszName, pszAlias));

    stable_sort(matchingRules.begin(), matchingRules.end(), RuleComparator);

    for (size_t iRule = 0; iRule < matchingRules.size(); iRule++)
    {
        const DeviceAssignmentRule *rule = matchingRules[iRule];

        Address.miBus = rule->iBus;
        Address.miDevice = rule->iDevice;
        Address.miFn = rule->iFn;

        if (checkAvailable(Address))
            return S_OK;
    }
    AssertLogRelMsgFailed(("BusAssignmentManager: All possible candidate positions for %s exhausted\n", pszName));

    return E_INVALIDARG;
}

bool BusAssignmentManager::State::checkAvailable(PCIBusAddress& Address)
{
    PCIMap::const_iterator it = mPCIMap.find(Address);

    return (it == mPCIMap.end());
}

void BusAssignmentManager::State::listAttachedPCIDevices(std::vector<PCIDeviceInfo> &aAttached)
{
    aAttached.resize(mPCIMap.size());

    size_t i = 0;
    PCIDeviceInfo dev;
    for (PCIMap::const_iterator it = mPCIMap.begin(); it !=  mPCIMap.end(); ++it, ++i)
    {
        dev.strDeviceName = it->second.szDevName;
        dev.guestAddress = it->first;
        dev.hostAddress = it->second.HostAddress;
        aAttached[i] = dev;
    }
}

BusAssignmentManager::BusAssignmentManager()
    : pState(NULL)
{
    pState = new State();
    Assert(pState);
}

BusAssignmentManager::~BusAssignmentManager()
{
    if (pState)
    {
        delete pState;
        pState = NULL;
    }
}

BusAssignmentManager *BusAssignmentManager::createInstance(PCVMMR3VTABLE pVMM, ChipsetType_T chipsetType, IommuType_T iommuType)
{
    BusAssignmentManager *pInstance = new BusAssignmentManager();
    pInstance->pState->init(pVMM, chipsetType, iommuType);
    Assert(pInstance);
    return pInstance;
}

void BusAssignmentManager::AddRef()
{
    ASMAtomicIncS32(&pState->cRefCnt);
}

void BusAssignmentManager::Release()
{
    if (ASMAtomicDecS32(&pState->cRefCnt) == 0)
        delete this;
}

DECLINLINE(HRESULT) InsertConfigInteger(PCVMMR3VTABLE pVMM, PCFGMNODE pCfg, const char *pszName, uint64_t u64)
{
    int vrc = pVMM->pfnCFGMR3InsertInteger(pCfg, pszName, u64);
    if (RT_FAILURE(vrc))
        return E_INVALIDARG;

    return S_OK;
}

DECLINLINE(HRESULT) InsertConfigNode(PCVMMR3VTABLE pVMM, PCFGMNODE pNode, const char *pcszName, PCFGMNODE *ppChild)
{
    int vrc = pVMM->pfnCFGMR3InsertNode(pNode, pcszName, ppChild);
    if (RT_FAILURE(vrc))
        return E_INVALIDARG;

    return S_OK;
}


HRESULT BusAssignmentManager::assignPCIDeviceImpl(const char *pszDevName,
                                                  PCFGMNODE pCfg,
                                                  PCIBusAddress& GuestAddress,
                                                  PCIBusAddress HostAddress,
                                                  bool fGuestAddressRequired)
{
    HRESULT hrc = S_OK;

    if (!GuestAddress.valid())
        hrc = pState->autoAssign(pszDevName, GuestAddress);
    else
    {
        bool fAvailable = pState->checkAvailable(GuestAddress);

        if (!fAvailable)
        {
            if (fGuestAddressRequired)
                hrc = E_ACCESSDENIED;
            else
                hrc = pState->autoAssign(pszDevName, GuestAddress);
        }
    }

    if (FAILED(hrc))
        return hrc;

    Assert(GuestAddress.valid() && pState->checkAvailable(GuestAddress));

    hrc = pState->record(pszDevName, GuestAddress, HostAddress);
    if (FAILED(hrc))
        return hrc;

    PCVMMR3VTABLE const pVMM = pState->mpVMM;
    if (pCfg)
    {
        hrc = InsertConfigInteger(pVMM, pCfg, "PCIBusNo",      GuestAddress.miBus);
        if (FAILED(hrc))
            return hrc;
        hrc = InsertConfigInteger(pVMM, pCfg, "PCIDeviceNo",   GuestAddress.miDevice);
        if (FAILED(hrc))
            return hrc;
        hrc = InsertConfigInteger(pVMM, pCfg, "PCIFunctionNo", GuestAddress.miFn);
        if (FAILED(hrc))
            return hrc;
    }

    /* Check if the bus is still unknown, i.e. the bridge to it is missing */
    if (   GuestAddress.miBus > 0
        && !hasPCIDevice(pState->mpszBridgeName, GuestAddress.miBus - 1))
    {
        PCFGMNODE pDevices = pVMM->pfnCFGMR3GetParent(pVMM->pfnCFGMR3GetParent(pCfg));
        AssertLogRelMsgReturn(pDevices, ("BusAssignmentManager: cannot find base device configuration\n"), E_UNEXPECTED);
        PCFGMNODE pBridges = pVMM->pfnCFGMR3GetChild(pDevices, pState->mpszBridgeName);
        AssertLogRelMsgReturn(pBridges, ("BusAssignmentManager: cannot find bridge configuration base\n"), E_UNEXPECTED);

        /* Device should be on a not yet existing bus, add it automatically */
        for (int iBridge = 0; iBridge <= GuestAddress.miBus - 1; iBridge++)
        {
            if (!hasPCIDevice(pState->mpszBridgeName, iBridge))
            {
                PCIBusAddress BridgeGuestAddress;
                hrc = pState->autoAssign(pState->mpszBridgeName, BridgeGuestAddress);
                if (FAILED(hrc))
                    return hrc;
                if (BridgeGuestAddress.miBus > iBridge)
                    AssertLogRelMsgFailedReturn(("BusAssignmentManager: cannot create bridge for bus %i because the possible parent bus positions are exhausted\n", iBridge + 1), E_UNEXPECTED);

                PCFGMNODE pInst;
                InsertConfigNode(pVMM, pBridges, Utf8StrFmt("%d", iBridge).c_str(), &pInst);
                InsertConfigInteger(pVMM, pInst, "Trusted", 1);
                hrc = assignPCIDevice(pState->mpszBridgeName, pInst);
                if (FAILED(hrc))
                    return hrc;
            }
        }
    }

    return S_OK;
}


bool BusAssignmentManager::findPCIAddress(const char *pszDevName, int iInstance, PCIBusAddress& Address)
{
    return pState->findPCIAddress(pszDevName, iInstance, Address);
}
void BusAssignmentManager::listAttachedPCIDevices(std::vector<PCIDeviceInfo> &aAttached)
{
    pState->listAttachedPCIDevices(aAttached);
}
